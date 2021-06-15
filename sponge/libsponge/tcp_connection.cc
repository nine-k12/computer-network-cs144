#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { 
    return _sender.stream_in().remaining_capacity(); 
}

size_t TCPConnection::bytes_in_flight() const { 
    return _sender.bytes_in_flight(); 
}

size_t TCPConnection::unassembled_bytes() const { 
    return _receiver.unassembled_bytes(); 
}

size_t TCPConnection::time_since_last_segment_received() const { 
    return _time_cnt - _last_ack_time; 
}

size_t TCPConnection::write(const string &data) {
    size_t write_cnt = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return write_cnt;
}

bool TCPConnection::active() const { 
    // unclean shutdown
    if(_sender.stream_in().error() && _receiver.stream_out().error())
        return false;
    // clean shutdown: 1) in wait time 2) send and receive data finished
    return !(_sender.stream_in().eof() && _sender.bytes_in_flight() == 0 && 
        _receiver.stream_out().input_ended()) || _time_wait;
}

void TCPConnection::send_segments(){
    if(_closed) return;

    while(!_sender.segments_out().empty()){
        TCPSegment &seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if(_receiver.ackno().has_value()){
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
        }
        size_t max_win = numeric_limits<uint16_t>().max();
        seg.header().win = min(_receiver.window_size(), max_win);
        _segments_out.push(seg);
    }
    
    // clean shutdown
    if(_sender.stream_in().eof() && _sender.bytes_in_flight() == 0 &&
         _receiver.stream_out().input_ended()){
        if(_linger_after_streams_finish)
            _time_wait = true;
    }
}

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // connection is unestablished
    if(!_syn_sent && !seg.header().syn)
        return;

    // connection is closed
    if(_closed)
        return;
    
    if(seg.header().rst){
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        return;
    }

    _last_ack_time = _time_cnt;
    _receiver.segment_received(seg);
    _sender.ack_received(seg.header().ackno, seg.header().win);
    _sender.fill_window();
    _syn_sent = true;

    // passive close
    if(_receiver.stream_out().input_ended() && !_sender.stream_in().eof())
        _linger_after_streams_finish = false;

    // no need for ack
    if(!_receiver.ackno().has_value()) 
        return;

    if(_sender.segments_out().empty()){
        if(_receiver.stream_out().input_ended() && !seg.header().fin){
            // all data was received, no need to ack, just wait for fin
        }
        else if(seg.length_in_sequence_space() == 0){
            // an empty datagram, no need to ack the empty ack
        }
        else{
            _sender.send_empty_segment();
        }
    }
    send_segments();
}


//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    _time_cnt += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if(_time_wait && _time_cnt >= _last_ack_time + _cfg.rt_timeout*10){
        // closed
        _time_wait =  false;
        _closed = true;
    }

    if(_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS){
        // rst
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        while(!_sender.segments_out().empty())
            _sender.segments_out().pop();
        _sender.send_empty_segment();
        TCPSegment &seg = _sender.segments_out().front();
        seg.header().rst = true;
    }
    send_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments();
}

void TCPConnection::connect() {
    if(!_syn_sent){
        _sender.fill_window();
        _syn_sent = true;
        TCPSegment &seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        size_t max_win = numeric_limits<uint16_t>().max();
        seg.header().win = min(_receiver.window_size(), max_win);
        _segments_out.push(seg);
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _linger_after_streams_finish = false;
            while(!_sender.segments_out().empty())
                _sender.segments_out().pop();
            
            _sender.send_empty_segment();
            TCPSegment &seg = _sender.segments_out().front();
            _sender.segments_out().pop();
            seg.header().rst = true;
            send_segments();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}


