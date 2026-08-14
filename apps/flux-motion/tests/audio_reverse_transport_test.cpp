#include "audio-transport-direction.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    using fxm::audio::advance_transport_cursor;
    using fxm::audio::transport_sample_at;
    using fxm::audio::transport_sample_cursor;

    constexpr uint32_t sample_rate = 48000;
    assert(transport_sample_cursor(1.0, sample_rate, false) == 48000);
    assert(transport_sample_cursor(1.0, sample_rate, true) == 47999);
    assert(transport_sample_cursor(0.0, sample_rate, true) == -1);

    int64_t reverse_cursor =
        transport_sample_cursor(1.0, sample_rate, true);
    assert(transport_sample_at(reverse_cursor, 0, true) == 47999);
    assert(transport_sample_at(reverse_cursor, 1, true) == 47998);
    assert(transport_sample_at(reverse_cursor, 479, true) == 47520);
    advance_transport_cursor(reverse_cursor, 480, true);
    assert(reverse_cursor == 47519);

    int64_t forward_cursor = 0;
    advance_transport_cursor(forward_cursor, 480, false);
    assert(forward_cursor == 480);

    int64_t minimum = std::numeric_limits<int64_t>::min() + 10;
    advance_transport_cursor(minimum, 480, true);
    assert(minimum == std::numeric_limits<int64_t>::min());

    std::cout << "audio reverse transport test passed\n";
    return 0;
}
