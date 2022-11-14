#include "tcp_receiver.hh"

#include "wrapping_integers.hh"
#include <cstdint>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    WrappingInt32 data_start(seg.header().seqno);

    if (seg.header().syn) {
        _isn = seg.header().seqno;
        data_start = data_start + 1;
    }

    if (_isn.has_value()) {
        /* Invalid seqno */
        if (_isn.value() == data_start)
            return;

        _reassembler.push_substring(
            seg.payload().copy(), unwrap(data_start, _isn.value(), _reassembler.cur_index()) - 1, seg.header().fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (_isn.has_value()) {
        // SYN takes 1 seqno
        uint64_t asn = _reassembler.cur_index() + 1;
        // FIN takes 1 seqno
        if (stream_out().input_ended())
            asn += 1;
        return wrap(asn, _isn.value());
    }

    return {};
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
