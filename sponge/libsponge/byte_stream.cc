#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity): _iseof(false),
    _buf({}),
    _capacity(capacity),
    _total_read_cnt(0), 
    _total_write_cnt(0) {}

size_t ByteStream::write(const string &data) {
    if(data.size() == 0)
        return 0;

    size_t write_cnt = min(data.size(), _capacity-_buf.size());
    _buf += data.substr(0, write_cnt);
    _total_write_cnt += write_cnt;

    return write_cnt;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    return _buf.substr(0, len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) { 
    _total_read_cnt += len;
    _buf.erase(0, len);
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t read_cnt = min(_buf.size(), len);
    string read_data = peek_output(read_cnt);
    pop_output(read_cnt);
    return read_data;
}

void ByteStream::end_input() { _iseof = true;}

bool ByteStream::input_ended() const { return _iseof; }

size_t ByteStream::buffer_size() const { return _buf.size(); }

bool ByteStream::buffer_empty() const { return _buf.empty(); }

bool ByteStream::eof() const { return _iseof && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _total_write_cnt; }

size_t ByteStream::bytes_read() const { return _total_read_cnt; }

size_t ByteStream::remaining_capacity() const { return _capacity-_buf.size(); }
