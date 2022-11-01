#include <iostream>

#include "stream_reassembler.hh"

int main() {
    auto re = StreamReassembler(1);
    re.push_substring("ab", 0, false);
    // re.push_substring("ab", 0, false);
    re.stream_out().read(1);
    re.push_substring("abc", 0, false);
    std::cout << re.stream_out().bytes_written() << std::endl;
}