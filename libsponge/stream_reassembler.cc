#include "stream_reassembler.hh"

#include <cstddef>
#include <cstdio>
#include <stdexcept>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    string actual_data = data;
    size_t first_unavailable = _cur_index + stream_out().remaining_capacity();
    size_t left = index, right;
    bool should_set_eof = eof;

    // ByteStream is full
    if (first_unavailable == _cur_index)
        return;

    // overlap with existing bytes
    if (left < _cur_index) {
        size_t offset = _cur_index - index;
        if (offset >= actual_data.size())
            goto out;
        actual_data = actual_data.substr(offset, string::npos);
        left = _cur_index;
    }

    // too long to fit in the reassembler
    if (left + actual_data.size() > first_unavailable) {
        actual_data = actual_data.substr(0, first_unavailable - left);
        should_set_eof = false;
    }

    right = left + actual_data.size() - 1;
    for (auto it = _list.begin(); it != _list.end();) {
        // irrelevant
        if (it->_end + 1 < left || it->_beg > right + 1) {
            ++it;
            continue;
        }

        // totally overlapped
        if (it->_beg <= left && it->_end >= right)
            goto out;

        // totally overlapped
        if (it->_end <= right && it->_beg >= left) {
            it = _list.erase(it);
            continue;
        }

        // partially overlapped
        if (it->_end + 1 >= left && it->_end + 1 <= right) {
            actual_data = it->_s.substr(0, left - it->_beg) + actual_data;
            left = it->_beg;
            it = _list.erase(it);
            continue;
        }

        // partially overlapped
        if (it->_beg >= left + 1 && it->_beg <= right + 1) {
            actual_data = actual_data + it->_s.substr(right + 1 - it->_beg, string::npos);
            right = it->_end;
            it = _list.erase(it);
            continue;
        }

        throw runtime_error("Control flow shouldn't reach here!");
    }

    if (left == _cur_index) {
        if (actual_data.size() != stream_out().write(actual_data))
            throw runtime_error("ByteStream::write() return value not expected.");
        _cur_index += actual_data.size();
        goto out;
    }

    for (auto it = _list.begin();; it++) {
        if (it == _list.end() || it->_beg > right + 1) {
            _list.insert(it, {std::move(actual_data), left, right});
            break;
        }
    }

out:
    if (should_set_eof)
        _eof = true;
    if (_eof && empty())
        stream_out().end_input();
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t ret = 0;
    for (auto &e : _list)
        ret += e._s.size();
    return ret;
}

bool StreamReassembler::empty() const { return unassembled_bytes() == 0; }
