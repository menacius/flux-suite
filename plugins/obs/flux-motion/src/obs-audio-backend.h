#pragma once

#include "audio-interfaces.h"

#include <obs-module.h>

namespace fxm::obs_plugin {

class ObsAudioBackend final : public audio::IAudioBackend {
public:
    explicit ObsAudioBackend(obs_source_t *source) noexcept;
    ~ObsAudioBackend() override = default;

    std::uint32_t sample_rate() const noexcept override;
    bool submit(const audio::AudioPacketView &packet,
                std::string *error) override;

private:
    obs_source_t *source_ = nullptr;
    std::uint32_t sample_rate_ = 48000;
};

} // namespace fxm::obs_plugin
