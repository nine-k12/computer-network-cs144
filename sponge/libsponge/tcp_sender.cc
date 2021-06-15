#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{std::random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { 
    return _next_abs_seqno-_send_abs_seqno; 
}

void TCPSender::fill_window() {
    if(_send_abs_seqno == 0){ 
        if(!_syn_sended){
            // set syn
            _syn_sended = true;
            TCPSegment seg;
            seg.header().syn = true;
            seg.header().seqno = wrap(0, _isn);
            _next_abs_seqno = 1;
            _retransmission_timeout = _initial_retransmission_timeout;
            _segments_out.push(seg);
            _segments_bak_out.push(seg);
            //_window_size--;
        }
        else return;
    }

    uint64_t win;
    if(_occupied_space) 
        win = _window_size - _occupied_space;
    else
        win = _window_size - bytes_in_flight();

    if(_stream.eof()){
        if(!_fin_sended && _window_size > 0){
            if(win > 0){
                // set fin
                _fin_sended = true;
                TCPSegment seg;
                seg.header().fin = true;
                seg.header().seqno = wrap(_next_abs_seqno, _isn);
                _next_abs_seqno++;
                _segments_out.push(seg);
                _segments_bak_out.push(seg);
                win--;
            }
            else return;
        }
        else if(!_fin_sended && _window_size == 0){
            if(!_prob_sended){
                _fin_sended = true;
                TCPSegment seg;
                seg.header().fin = true;
                seg.header().seqno = wrap(_next_abs_seqno, _isn);
                _next_abs_seqno++;
                _segments_out.push(seg);
                _segments_bak_out.push(seg);
                _prob_sended = true;
            }
            else return;
        }
    }
    
    else if(!_stream.eof() && _next_abs_seqno > bytes_in_flight()){
        if(_window_size > 0){
            while(!_stream.buffer_empty() && win > 0){
                TCPSegment seg;
                seg.header().seqno = wrap(_next_abs_seqno, _isn);
                uint64_t next_seg_length = std::min(TCPConfig::MAX_PAYLOAD_SIZE, win);
                seg.payload() = _stream.read(next_seg_length);
                win -= seg.length_in_sequence_space();
                if(win > 0 && _stream.eof()){
                    seg.header().fin = true;
                    _fin_sended = true;
                    win--;
                }
                _next_abs_seqno += seg.length_in_sequence_space(); // seq.fin  will change seg's length
                _segments_out.push(seg);
                _segments_bak_out.push(seg);
            }
        }
        else if(_window_size == 0){
            if(!_prob_sended){
                TCPSegment seg;
                seg.header().seqno = wrap(_next_abs_seqno, _isn);
                seg.payload() = _stream.read(1);

                _next_abs_seqno += seg.length_in_sequence_space(); // seq.fin  will change seg's length
                _segments_out.push(seg);
                _segments_bak_out.push(seg);
                _prob_sended = true;
            }
            else return;
        }
    }

    if(!_segments_out.empty() && !_timer_running){
        _timer_running = true;
        _time_cnt = 0;
        _consecutive_retransmissions = 0;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, _isn, _send_abs_seqno);
    // illegal ack
    if(abs_ackno < _send_abs_seqno || abs_ackno > _next_abs_seqno)
        return;
    
    _prob_sended = false;

    _window_size = window_size;
    bool isAcked = false;
    // ack data
    while(!_segments_bak_out.empty()){
        if(abs_ackno-_send_abs_seqno >=_segments_bak_out.front().length_in_sequence_space()){
            _send_abs_seqno += _segments_bak_out.front().length_in_sequence_space();
            _segments_bak_out.pop();
            isAcked = true;
            _occupied_space = 0;
        }
        else{
            _occupied_space = (_send_abs_seqno + _segments_bak_out.front().length_in_sequence_space() - abs_ackno);
            break;
        }
    }


    // restart timer
    if(!_segments_bak_out.empty() && isAcked)
        _time_cnt = 0;

    _retransmission_timeout = _initial_retransmission_timeout;
    _consecutive_retransmissions = 0;
}


//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    // retransmit if the timer is expired
    _time_cnt += ms_since_last_tick;
    if(_timer_running && _time_cnt >= _retransmission_timeout){
        _time_cnt = 0;
        if(!_segments_bak_out.empty()){
            _segments_out.push(_segments_bak_out.front());
            _consecutive_retransmissions++;
            if(_window_size)
                _retransmission_timeout *= 2;
                
            //if(_syn_sended && _next_abs_seqno == bytes_in_flight()){
            //    if(_retransmission_timeout < _initial_retransmission_timeout)
            //        _retransmission_timeout = _initial_retransmission_timeout;
            //}
        }
        
        //if(_segments_bak_out.empty())
        //    _consecutive_retransmissions = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { 
    return _consecutive_retransmissions; 
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = wrap(_next_abs_seqno, _isn);
    _segments_out.push(seg);
}
