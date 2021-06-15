#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

/*void TCPReceiver::segment_received(const TCPSegment &seg) {
    const TCPHeader& header = seg.header();

    if(header.syn){
        _syn = true;
        _isn = header.seqno.raw_value();
    }
    size_t _checkpoint = _reassembler.head_index();
    size_t abs_seqno = unwrap(header.seqno + header.syn, WrappingInt32(_isn), _checkpoint);
    size_t stream_seqno = abs_seqno - 1;
    std::string data = seg.payload().copy();
    _reassembler.push_substring(data, stream_seqno, header.fin);
}*/

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // connection is closed
    if(_state == TRANSMISSION_FINISHED)
        return;
        
    const TCPHeader header = seg.header();
    // repeated connection requirement
    if(_state == SYN_RECEIVED && header.syn)
        return;

    // no connection requirement
    if(_state == LISTEN && !header.syn)
        return;

    size_t abs_seqno = 1;
    size_t length = seg.length_in_sequence_space();
    // a connection requirement arrive firstly
    if(_state == LISTEN && header.syn){
        _state = SYN_RECEIVED;
        _isn = header.seqno;
        if(length == 1) // the datagram only contains a syn info
            return;
    }
    else { // datagram transmition
        size_t checkpoint = _reassembler.head_index();
        abs_seqno = unwrap(header.seqno, _isn, checkpoint);
    }

    //size_t seq_size = length - header.syn - header.fin;
    size_t win_end = _reassembler.head_index() + window_size();
    if(abs_seqno > win_end)
        return;
    std::string data = seg.payload().copy();
    _reassembler.push_substring(data, abs_seqno-1, header.fin);

    if(_reassembler.stream_out().input_ended() && _reassembler.empty())
        _state = TRANSMISSION_FINISHED;
    return;
}


std::optional<WrappingInt32> TCPReceiver::ackno() const { 
    if(_state == LISTEN) 
        return std::nullopt;
    size_t bytes_written = _reassembler.stream_out().bytes_written();
    if(_state == TRANSMISSION_FINISHED)
        return WrappingInt32(wrap(bytes_written+2, _isn));
    return WrappingInt32(wrap(bytes_written+1, _isn));
}

size_t TCPReceiver::window_size() const { 
    return _reassembler.stream_out().remaining_capacity();
}
