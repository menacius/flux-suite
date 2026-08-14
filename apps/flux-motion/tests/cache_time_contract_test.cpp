#include "cache-time.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
    constexpr double fps = 25.0;
    assert(cache_frame_index_for_time(-1.0, fps) == 0);
    assert(cache_frame_index_for_time(0.0, fps) == 0);
    assert(cache_frame_index_for_time(0.02, fps) == 0);
    assert(cache_frame_index_for_time(0.039999, fps) == 0);
    assert(cache_frame_index_for_time(0.04, fps) == 1);
    assert(cache_frame_index_for_time(1.0, fps) == 25);
    assert(std::abs(cache_time_for_frame(25, fps) - 1.0) < 1e-12);
    assert(cache_frame_index_for_title_time(3.0, fps, false, 200) == 0);
    assert(cache_frame_index_for_title_time(3.0, fps, true, 200) == 75);
    assert(cache_frame_index_for_title_time(20.0, fps, true, 120) == 120);

    std::cout << "cache time, static collapse, and invalidation interval contract passed\n";
    return 0;
}
