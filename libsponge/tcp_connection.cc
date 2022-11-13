#include "tcp_connection.hh"

#include "tcp_segment.hh"
#include "tcp_state.hh"

#include <iostream>
#include <stdexcept>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return {}; }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_seg_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    TCPSegment seg_out;

    _time_since_last_seg_received = 0;

    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    if (state() == TCPState(TCPState::State::CLOSING) and seg.header().ack) {
        /* CLOSING -> TIME-WAIT */
        _sender.ack_received(seg.header().ackno, seg.header().win);
        /* This ack contains no data, as input stream has been closed. */
        /* So need not call `_receiver.segment_received()`, and need not send a ack. */
    } else if (state() == TCPState(TCPState::State::FIN_WAIT_1) and seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _receiver.segment_received(seg);

        // /* If we have just received a FIN, we have to acknowledge it. */
        // if (state() == TCPState(TCPState::State::CLOSING) or state() == TCPState(TCPState::State::TIME_WAIT) or
        //     /* The segment just acknowledges previous data rather than FIN. */
        //     /* In this case, the segment may still contain inbound data, so we need to acknowledge it. */
        //     state() == TCPState(TCPState::State::FIN_WAIT_1)) {
        if (seg.length_in_sequence_space()) {
            /* Need not call fill_window() as the output stream has been closed. */
            _sender.send_empty_segment();

            seg_out = _sender.segments_out().front();
            _sender.segments_out().pop();
            if (_receiver.ackno().has_value()) {
                seg_out.header().ack = true;
                seg_out.header().ackno = _receiver.ackno().value();
            }
            _segments_out.push(seg_out);
        }
    } else {
        _receiver.segment_received(seg);

        if (seg.header().ack) {
            _sender.ack_received(seg.header().ackno, seg.header().win);
        }

        _sender.fill_window();
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
        }

        seg_out = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg_out.header().ack = true;
            seg_out.header().ackno = _receiver.ackno().value();
        }

        _segments_out.push(seg_out);
    }
}

bool TCPConnection::active() const {
    if (_linger_after_streams_finish and _time_since_last_seg_received < 10 * _cfg.rt_timeout)
        return true;

    if (_sender.stream_in().input_ended() and _receiver.stream_out().input_ended())
        return false;

    return true;
}

size_t TCPConnection::write(const string &data) {
    DUMMY_CODE(data);
    return {};
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_seg_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    /* Sender may need to retransmit a packet. */
    if (!_sender.segments_out().empty()) {
        _segments_out.push(_sender.segments_out().front());
        _sender.segments_out().pop();
    }
}

void TCPConnection::end_input_stream() {
    TCPSegment seg_out;

    _sender.stream_in().end_input();

    _sender.fill_window();
    if (_sender.segments_out().empty() || !_sender.segments_out().front().header().fin)
        throw runtime_error("Expect to have a FIN sigment, which is not true.");

    seg_out = _sender.segments_out().front();
    _sender.segments_out().pop();

    if (_receiver.ackno().has_value()) {
        seg_out.header().ack = true;
        seg_out.header().ackno = _receiver.ackno().value();
    }

    _segments_out.push(seg_out);
}

void TCPConnection::connect() {
    _sender.fill_window();
    if (!_sender.segments_out().empty()) {
        _segments_out.push(_sender.segments_out().front());
        _sender.segments_out().pop();
    }
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
