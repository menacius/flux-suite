#include "obs-audio-backend.h"

#include <media-io/audio-io.h>

namespace fxm::obs_plugin {
namespace {

static void set_error(std::string *error, const char *message)
{
    if (error)
        *error = message ? message : "";
}

} // namespace

ObsAudioBackend::ObsAudioBackend(obs_source_t *source) noexcept
    : source_(source)
{
    if (audio_t *audio = obs_get_audio()) {
        const std::uint32_t configured = audio_output_get_sample_rate(audio);
        if (configured >= 8000 && configured <= 384000)
            sample_rate_ = configured;
    }
}

std::uint32_t ObsAudioBackend::sample_rate() const noexcept
{
    return sample_rate_;
}

bool ObsAudioBackend::submit(const audio::AudioPacketView &packet,
                             std::string *error)
{
    if (error)
        error->clear();
    if (!source_) {
        set_error(error, "OBS audio output has no source.");
        return false;
    }
    if (!packet.valid() ||
        packet.channel_layout != audio::AudioChannelLayout::Stereo ||
        packet.sample_format != audio::AudioSampleFormat::Float32Planar) {
        set_error(error, "Unsupported OBS audio packet.");
        return false;
    }

    obs_source_audio output = {};
    output.data[0] =
        reinterpret_cast<const std::uint8_t *>(packet.planes[0]);
    output.data[1] =
        reinterpret_cast<const std::uint8_t *>(packet.planes[1]);
    output.frames = packet.frames;
    output.speakers = SPEAKERS_STEREO;
    output.format = AUDIO_FORMAT_FLOAT_PLANAR;
    output.samples_per_sec = packet.sample_rate;
    output.timestamp = packet.timestamp_ns;
    obs_source_output_audio(source_, &output);
    return true;
}

} // namespace fxm::obs_plugin
