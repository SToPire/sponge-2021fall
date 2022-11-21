#include "tcp_sender.hh"

#include "buffer.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <cstdint>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _cur_rto(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    TCPSegment seg;
    size_t read_size;
    uint64_t actual_window_size = _window_size;

    if (actual_window_size == 0)
        actual_window_size = 1;

    /* This could happen as we don't support partially ack a segment yet.*/
    if (actual_window_size <= _bytes_in_flight)
        return;

    do {
        if (_is_transmission_ended)
            return;

        seg.header().seqno = wrap(_next_seqno, _isn);

        // It's a SYN segment.
        if (_next_seqno == 0) {
            seg.header().syn = true;
            ++_next_seqno;
            ++_bytes_in_flight;
        }

        read_size = min(actual_window_size - _bytes_in_flight, TCPConfig::MAX_PAYLOAD_SIZE);
        seg.payload() = Buffer(_stream.read(read_size));
        read_size = seg.payload().size();

        _bytes_in_flight += read_size;
        _next_seqno += read_size;

        // Set FIN flag when we are assure that outstream was closed and all data has been sent.
        if (_stream.input_ended() and _stream.eof() and actual_window_size - _bytes_in_flight) {
            seg.header().fin = true;
            ++_next_seqno;
            ++_bytes_in_flight;
            _is_transmission_ended = true;
        }

        if (read_size or seg.header().syn or seg.header().fin) {
            _segments_out.push(seg);

            _outstanding_segments.push(seg);
            _timer.start(_cur_rto);
        }
    } while (!_stream.buffer_empty() && actual_window_size - _bytes_in_flight);
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t ackno_abs = unwrap(ackno, _isn, _next_seqno);

    _window_size = window_size;

    /* Unacceptable Ack */
    if (ackno_abs <= _una_seqno || ackno_abs > _next_seqno)
        return;

    _una_seqno = ackno_abs;

    while (!_outstanding_segments.empty()) {
        TCPSegment seg = _outstanding_segments.front();
        if (ackno_abs >= unwrap(seg.header().seqno, _isn, _next_seqno) + seg.length_in_sequence_space()) {
            _bytes_in_flight -= seg.length_in_sequence_space();
            _outstanding_segments.pop();
            /* XXX: We do not support partially acknowledgement yet, which is documented in lab manual. If you do so,
             * you may not pass "Repeated ACKs and outdated ACKs are harmless" test in send_extra.cc. */
        } else {
            break;
        }
    }

    /* An ack is received that acknowledges new data, reset RTO and consecutive retransmissions count. */
    _cur_rto = _initial_retransmission_timeout;
    _consecutive_retrans_times = 0;

    /* When all outstanding data has been acknowledged, stop the timer. Otherwise, reset the timer according to new RTO.
     * (RFC 6298, 5.2 & 5.3) */
    if (_outstanding_segments.empty()) {
        _timer.stop();
    } else {
        _timer.stop();
        _timer.start(_cur_rto);
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    _cur_time += ms_since_last_tick;
    _timer.tick(ms_since_last_tick);

    if (_timer.expired() and !_outstanding_segments.empty()) {
        _segments_out.push(_outstanding_segments.front());
        /* This condition is documented in lab3 manual.*/
        if (_window_size) {
            ++_consecutive_retrans_times;
            _cur_rto <<= 1;
        }
        _timer.stop();
        _timer.start(_cur_rto);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retrans_times; }

void TCPSender::send_empty_segment() {
    TCPSegment seg;

    seg.header().seqno = wrap(_next_seqno, _isn);

    _segments_out.push(seg);
}
