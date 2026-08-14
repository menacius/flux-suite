#include "audio-interfaces.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>

namespace {

class TestAudioBackend final : public fxm::audio::IAudioBackend {
public:
    std::uint32_t sample_rate() const noexcept override
    {
        return 48000;
    }

    bool submit(const fxm::audio::AudioPacketView &packet,
                std::string *error) override
    {
        if (error)
            error->clear();
        submitted = packet.valid();
        return submitted;
    }

    bool submitted = false;
};

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "audio interfaces failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    static_assert(std::is_abstract_v<fxm::audio::IAudioBackend>);
    static_assert(
        std::has_virtual_destructor_v<fxm::audio::IAudioBackend>);

    bool ok = true;
    float left[16] = {};
    float right[16] = {};
    fxm::audio::AudioPacketView packet;
    packet.planes[0] = left;
    packet.planes[1] = right;
    packet.frames = 16;
    packet.sample_rate = 48000;
    packet.timestamp_ns = 1000000;

    ok &= expect(packet.valid(), "stereo planar packet is valid");
    ok &= expect(packet.channel_count() == 2,
                 "stereo layout reports two channels");

    TestAudioBackend backend;
    ok &= expect(backend.sample_rate() == 48000,
                 "backend exposes its configured sample rate");
    ok &= expect(backend.submit(packet, nullptr) && backend.submitted,
                 "backend receives a backend-neutral packet");

    packet.planes[1] = nullptr;
    ok &= expect(!packet.valid(), "missing stereo plane is rejected");
    return ok ? 0 : 1;
}
