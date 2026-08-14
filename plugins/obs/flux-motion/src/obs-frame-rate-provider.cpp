#include "obs-frame-rate-provider.h"

#include <obs-module.h>

namespace fxm::obs_plugin {

double ObsFrameRateProvider::frame_rate() const noexcept
{
    obs_video_info video_info = {};
    if (obs_get_video_info(&video_info) && video_info.fps_den > 0 &&
        video_info.fps_num > 0) {
        return static_cast<double>(video_info.fps_num) /
               static_cast<double>(video_info.fps_den);
    }
    return 30.0;
}

ObsFrameRateProvider &obs_frame_rate_provider() noexcept
{
    static ObsFrameRateProvider provider;
    return provider;
}

} // namespace fxm::obs_plugin
