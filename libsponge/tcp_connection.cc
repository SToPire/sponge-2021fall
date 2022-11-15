#include "tcp_connection.hh"

#include "tcp_segment.hh"
#include "tcp_state.hh"

#include <iostream>
#include <limits>
#include <thread>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_seg_received; }

void TCPConnection::_send_all_outbounding_segments() {
    TCPSegment seg_out;
    while (!_sender.segments_out().empty()) {
        seg_out = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg_out.header().ack = true;
            seg_out.header().ackno = _receiver.ackno().value();
        }

        /* window size should not overflow `TCPSegement::header().win`. */
        seg_out.header().win =
            min(_receiver.window_size(), static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()));

        _segments_out.push(seg_out);
    }
}

void TCPConnection::_send_rst_segmenet() {
    TCPSegment seg_out;
    _sender.send_empty_segment();
    seg_out = _sender.segments_out().front();
    _sender.segments_out().pop();

    seg_out.header().rst = true;
    _segments_out.push(seg_out);
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_seg_received = 0;

    /* Once receive a RST segment, put the connection into reset state */
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    /* Repeat to a TCP keep-alive segment, see RFC 9293, Section 3.8.4 */
    if (_receiver.ackno().has_value() and seg.length_in_sequence_space() == 0 and
        seg.header().seqno == _receiver.ackno().value() - 1) {
        _sender.send_empty_segment();
        _send_all_outbounding_segments();
        return;
    }

    /* Drop any segment except SYN in LISTEN state. */
    if (state() == TCPState(TCPState::State::LISTEN) and !seg.header().syn)
        return;

    _receiver.segment_received(seg);

    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }

    /* We are assure that inbound bytestream ended before outbound bytestream reaches eof, no linger.*/
    if (_receiver.stream_out().input_ended() and !_sender.stream_in().eof())
        _linger_after_streams_finish = false;

    _sender.fill_window();
    if (_sender.segments_out().empty()) {
        /* We need to send an ack, although no data to sent. */
        if (seg.length_in_sequence_space())
            _sender.send_empty_segment();
        else
            return;
    }

    _send_all_outbounding_segments();
}

bool TCPConnection::active() const {
    /* connection has been reset */
    if (_sender.stream_in().error() or _receiver.stream_out().error())
        return false;

    /* need to linger for a while */
    if (_linger_after_streams_finish and _time_since_last_seg_received < 10 * _cfg.rt_timeout)
        return true;

    /* transmission in progress */
    if (_sender.bytes_in_flight())
        return true;

    /* byte stream in two direction all ended */
    if (_sender.stream_in().input_ended() and _receiver.stream_out().input_ended())
        return false;

    return true;
}

size_t TCPConnection::write(const string &data) {
    size_t ret = _sender.stream_in().write(data);
    _sender.fill_window();
    _send_all_outbounding_segments();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_seg_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    /* Sender may need to retransmit a packet. */
    /* Retry too many times, reset the connection. */
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        /* send RST to peer */
        _send_rst_segmenet();

        /* set TCP state to RESET */
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    _send_all_outbounding_segments();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();

    _sender.fill_window();
    /* This may happen, as upper layer may call TCPConnection::end_input_stream() even after FIN has been sent. */
    if (_sender.segments_out().empty() || !_sender.segments_out().front().header().fin)
        return;

    _send_all_outbounding_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _send_all_outbounding_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // We need to send a RST segment to the peer.
            _send_rst_segmenet();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
