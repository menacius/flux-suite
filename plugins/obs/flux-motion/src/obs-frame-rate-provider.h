#pragma once

#include "frame-rate-provider.h"

namespace fxm::obs_plugin {

class ObsFrameRateProvider final : public IFrameRateProvider {
public:
    ~ObsFrameRateProvider() override = default;
    double frame_rate() const noexcept override;
};

ObsFrameRateProvider &obs_frame_rate_provider() noexcept;

} // namespace fxm::obs_plugin
