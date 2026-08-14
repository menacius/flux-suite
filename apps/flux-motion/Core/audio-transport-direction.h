#pragma once

#include "audio-output-scheduler.h"

#include <cstdint>
#include <limits>

namespace fxm::audio {

inline int64_t transport_sample_cursor(double seconds, uint32_t sample_rate,
                                       bool reverse)
{
    const uint64_t sample = timeline_seconds_to_sample(seconds, sample_rate);
    const auto max_sample =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (!reverse)
        return sample > max_sample ? std::numeric_limits<int64_t>::max()
                                   : static_cast<int64_t>(sample);
    if (sample == 0)
        return -1;
    const uint64_t previous = sample - 1;
    return previous > max_sample ? std::numeric_limits<int64_t>::max()
                                 : static_cast<int64_t>(previous);
}

inline void advance_transport_cursor(int64_t &cursor, uint32_t frames,
                                     bool reverse)
{
    const int64_t delta = static_cast<int64_t>(frames);
    if (reverse) {
        cursor = cursor < std::numeric_limits<int64_t>::min() + delta
            ? std::numeric_limits<int64_t>::min()
            : cursor - delta;
    } else {
        cursor = cursor > std::numeric_limits<int64_t>::max() - delta
            ? std::numeric_limits<int64_t>::max()
            : cursor + delta;
    }
}

inline int64_t transport_sample_at(int64_t block_start, uint32_t offset,
                                   bool reverse)
{
    const int64_t delta = static_cast<int64_t>(offset);
    if (reverse)
        return block_start < std::numeric_limits<int64_t>::min() + delta
            ? std::numeric_limits<int64_t>::min()
            : block_start - delta;
    return block_start > std::numeric_limits<int64_t>::max() - delta
        ? std::numeric_limits<int64_t>::max()
        : block_start + delta;
}

} // namespace fxm::audio
