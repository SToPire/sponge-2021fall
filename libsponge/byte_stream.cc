#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) {}

size_t ByteStream::write(const string &data) {
    size_t real_len = min(data.size(), remaining_capacity());
    _str += data.substr(0, real_len);
    _bytes_written += real_len;
    _cur_cap += real_len;
    return real_len;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t real_len = min(len, buffer_size());
    return _str.substr(0, real_len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t real_len = min(len, buffer_size());
    if (real_len == buffer_size()) {
        _str = string();
    } else {
        _str = _str.substr(real_len, buffer_size() - real_len);
    }
    _cur_cap -= real_len;
    _bytes_read += real_len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    size_t real_len = min(len, buffer_size());
    string ret = peek_output(real_len);
    pop_output(real_len);
    return ret;
}

void ByteStream::end_input() { _input_is_end = true; }

bool ByteStream::input_ended() const { return _input_is_end; }

size_t ByteStream::buffer_size() const { return _cur_cap; }

bool ByteStream::buffer_empty() const { return buffer_size() == 0; }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return _capacity - _cur_cap; }
