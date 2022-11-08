#include "wrapping_integers.hh"

#include <cstdint>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint64_t mask = 0xFFFFFFFF;
    n &= mask;
    n += isn.raw_value();
    n &= mask;
    return WrappingInt32(static_cast<uint32_t>(n));
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // What if checkpoint=2^31, n=0, isn=0? Return 0 or 2^32 is equally closest to `checkpoint`.
    uint64_t mask = 0xFFFFFFFF;
    uint32_t off = (n - isn) < 0 ? UINT32_MAX - (isn.raw_value() - n.raw_value() - 1) : n.raw_value() - isn.raw_value();
    uint64_t res = (checkpoint & ~mask) + off;

    if (res <= checkpoint) {
        if (checkpoint - res <= (mask + 1) >> 1)
            return res;
        return max(res, res + (mask + 1));  // be care of overflow!
    } else {
        if ((res - checkpoint <= (mask + 1) >> 1) || res < (mask + 1))
            return res;
        return res - (mask + 1);
    }
}
