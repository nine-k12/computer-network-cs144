class TCPConnection {
  private:   
    //......
    
    size_t _time_since_last_segment_received = 0;
    bool _active = true;
    bool _need_send_rst = false;
    bool _ack_for_fin_sent = false;

    bool push_segments_out(bool send_syn = false);
    void unclean_shutdown(bool send_rst);
    bool clean_shutdown();
    bool in_listen();
    bool in_syn_recv();
    bool in_syn_sent();
    //.......


size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;
    _time_since_last_segment_received = 0;

    // data segments with acceptable ACKs should be ignored in SYN_SENT
    if (in_syn_sent() && seg.header().ack && seg.payload().size() > 0) {
        return;
    }
    bool send_empty = false;
    if (_sender.next_seqno_absolute() > 0 && seg.header().ack) {
        // unacceptable ACKs should produced a segment that existed
        if (!_sender.ack_received(seg.header().ackno, seg.header().win)) {
            send_empty = true;
        }
    }

    bool recv_flag = _receiver.segment_received(seg);
    if (!recv_flag) {
        send_empty = true;
    }

    if (seg.header().syn && _sender.next_seqno_absolute() == 0) {
        connect();
        return;
    }

    if (seg.header().rst) {
        // RST segments without ACKs should be ignored in SYN_SENT
        if (in_syn_sent() && !seg.header().ack) {
            return;
        }
        unclean_shutdown(false);
        return;
    }

    if (seg.length_in_sequence_space() > 0) {
        send_empty = true;
    }

    if (send_empty) {
        if (_receiver.ackno().has_value() && _sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
    }
    push_segments_out();
}

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t ret = _sender.stream_in().write(data);
    push_segments_out();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        unclean_shutdown(true);
    }
    push_segments_out();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    push_segments_out();
}

void TCPConnection::connect() {
    // when connect, must active send a SYN
    push_segments_out(true);
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            // Your code here: need to send a RST segment to the peer
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            unclean_shutdown(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

bool TCPConnection::push_segments_out(bool send_syn) {
    // default not send syn before recv a SYN
    _sender.fill_window(send_syn || in_syn_recv());
    TCPSegment seg;
    while (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        if (_need_send_rst) {
            _need_send_rst = false;
            seg.header().rst = true;
        }
        _segments_out.push(seg);
    }
    clean_shutdown();
    return true;
}

void TCPConnection::unclean_shutdown(bool send_rst) {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    if (send_rst) {
        _need_send_rst = true;
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }
        push_segments_out();
    }
}

bool TCPConnection::clean_shutdown() {
    if (_receiver.stream_out().input_ended() && !(_sender.stream_in().eof())) {
        _linger_after_streams_finish = false;
    }
    if (_sender.stream_in().eof() && _sender.bytes_in_flight() == 0 && _receiver.stream_out().input_ended()) {
        if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _active = false;
        }
    }
    return !_active;
}

bool TCPConnection::in_listen() { return !_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0; }

bool TCPConnection::in_syn_recv() { return _receiver.ackno().has_value() && !_receiver.stream_out().input_ended(); }

bool TCPConnection::in_syn_sent() {
    return _sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute();
}


/////////////
#include "tcp_connection.hh"

#include <iostream>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

bool TCPConnection::active() const { return _active; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (!_active)
        return;
    _time_since_last_segment_received = 0;
    // State: closed
    if (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0) {
        if (!seg.header().syn)
            return;
        _receiver.segment_received(seg);
        connect();
        return;
    }
    // State: syn sent
    if (_sender.next_seqno_absolute() > 0 && _sender.bytes_in_flight() == _sender.next_seqno_absolute() &&
        !_receiver.ackno().has_value()) {
        if (seg.payload().size())
            return;
        if (!seg.header().ack) {
            if (seg.header().syn) {
                // simultaneous open
                _receiver.segment_received(seg);
                _sender.send_empty_segment();
            }
            return;
        }
        if (seg.header().rst) {
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            _active = false;
            return;
        }
    }
    _receiver.segment_received(seg);
    _sender.ack_received(seg.header().ackno, seg.header().win);
    // Lab3 behavior: fill_window() will directly return without sending any segment.
    // See tcp_sender.cc line 42
    if (_sender.stream_in().buffer_empty() && seg.length_in_sequence_space())
        _sender.send_empty_segment();
    if (seg.header().rst) {
        _sender.send_empty_segment();
        unclean_shutdown();
        return;
    }
    send_sender_segments();
}

size_t TCPConnection::write(const string &data) {
    if (!data.size())
        return 0;
    size_t write_size = _sender.stream_in().write(data);
    _sender.fill_window();
    send_sender_segments();
    return write_size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    if (!_active)
        return;
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS)
        unclean_shutdown();
    send_sender_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_sender_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_sender_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            _sender.send_empty_segment();
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_sender_segments() {
    TCPSegment seg;
    while (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
        _segments_out.push(seg);
    }
    clean_shutdown();
}

void TCPConnection::unclean_shutdown() {
    // When this being called, _sender.stream_out() should not be empty.
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
    TCPSegment seg = _sender.segments_out().front();
    _sender.segments_out().pop();
    seg.header().ack = true;
    if (_receiver.ackno().has_value())
        seg.header().ackno = _receiver.ackno().value();
    seg.header().win = _receiver.window_size();
    seg.header().rst = true;
    _segments_out.push(seg);
}

void TCPConnection::clean_shutdown() {
    if (_receiver.stream_out().input_ended()) {
        if (!_sender.stream_in().eof())
            _linger_after_streams_finish = false;
        else if (_sender.bytes_in_flight() == 0) {
            if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
                _active = false;
            }
        }
    }
}
————————————————
版权声明：本文为CSDN博主「KindergartenKing」的原创文章，遵循CC 4.0 BY-SA版权协议，转载请附上原文出处链接及本声明。
原文链接：https://blog.csdn.net/u012495807/article/details/113842110