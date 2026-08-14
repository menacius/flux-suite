#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace fxm::audio {

enum class AudioSampleFormat : std::uint8_t {
    Float32Planar,
};

enum class AudioChannelLayout : std::uint8_t {
    Mono = 1,
    Stereo = 2,
};

struct AudioPacketView {
    std::array<const float *, 8> planes{};
    std::uint32_t frames = 0;
    std::uint32_t sample_rate = 0;
    AudioChannelLayout channel_layout = AudioChannelLayout::Stereo;
    AudioSampleFormat sample_format = AudioSampleFormat::Float32Planar;
    std::uint64_t timestamp_ns = 0;

    constexpr std::size_t channel_count() const noexcept
    {
        return static_cast<std::size_t>(channel_layout);
    }

    constexpr bool valid() const noexcept
    {
        const std::size_t channels = channel_count();
        if (frames == 0 || sample_rate == 0 || timestamp_ns == 0 ||
            channels == 0 || channels > planes.size())
            return false;
        for (std::size_t channel = 0; channel < channels; ++channel) {
            if (!planes[channel])
                return false;
        }
        return true;
    }
};

class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual std::uint32_t sample_rate() const noexcept = 0;
    virtual bool submit(const AudioPacketView &packet,
                        std::string *error) = 0;
};

} // namespace fxm::audio
