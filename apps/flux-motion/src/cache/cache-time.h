#pragma once

#include <algorithm>
#include <cmath>

/* Cached frame n owns the half-open sample interval [n/fps, (n+1)/fps).
 * All lookup, invalidation, and UI notification paths must use this helper so
 * they cannot disagree at half-frame boundaries. */
inline int cache_frame_index_for_time(double time, double frame_rate)
{
    const double fps = std::max(1.0, frame_rate);
    const double frame_position = std::max(0.0, time) * fps;
    return std::max(0, static_cast<int>(std::floor(frame_position + 1e-7)));
}

inline double cache_time_for_frame(int frame, double frame_rate)
{
    return static_cast<double>(std::max(0, frame)) /
           std::max(1.0, frame_rate);
}

inline int cache_frame_index_for_title_time(double time, double frame_rate,
                                            bool has_timeline_changes,
                                            int last_frame)
{
    if (!has_timeline_changes)
        return 0;
    return std::clamp(cache_frame_index_for_time(time, frame_rate), 0,
                      std::max(0, last_frame));
}
