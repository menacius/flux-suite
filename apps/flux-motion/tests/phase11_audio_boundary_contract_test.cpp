#include "source_bundle_reader.h"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Phase 11 audio boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Forbidden Phase 11 audio dependency: " << label
              << " (" << token << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 5)
        return 2;

    const std::string interfaces = read_file(argv[1]);
    const std::string adapter_header = read_file(argv[2]);
    const std::string adapter_source = read_file(argv[3]);
    const std::string runtime_source = read_file(argv[4]);

    bool ok = true;
    for (const auto &[token, label] :
         std::vector<std::pair<std::string, const char *>> {
             {"struct AudioPacketView", "shared audio packet"},
             {"enum class AudioSampleFormat", "shared sample format"},
             {"enum class AudioChannelLayout", "shared channel layout"},
             {"class IAudioBackend", "shared audio backend interface"},
             {"virtual std::uint32_t sample_rate()",
              "backend sample-rate query"},
             {"virtual bool submit(const AudioPacketView &packet",
              "backend packet submission"},
         }) {
        ok = require_token(interfaces, token, label) && ok;
    }
    for (const auto &[token, label] :
         std::vector<std::pair<std::string, const char *>> {
             {"obs_", "OBS symbol in Shared audio interface"},
             {"obs-module", "OBS include in Shared audio interface"},
             {"QWidget", "Qt Widgets type in Shared audio interface"},
         }) {
        ok = reject_token(interfaces, token, label) && ok;
    }

    ok = require_token(
             adapter_header,
             "class ObsAudioBackend final : public audio::IAudioBackend",
             "OBS backend implements IAudioBackend") &&
         ok;
    ok = require_token(adapter_source, "audio_output_get_sample_rate",
                       "OBS sample-rate adapter") &&
         ok;
    ok = require_token(adapter_source, "obs_source_output_audio",
                       "OBS packet-output adapter") &&
         ok;

    ok = require_token(runtime_source, "output_backend_->sample_rate()",
                       "runtime sample-rate abstraction") &&
         ok;
    ok = require_token(runtime_source, "output_backend_->submit(packet",
                       "runtime packet-output abstraction") &&
         ok;
    ok = reject_token(runtime_source, "audio_output_get_sample_rate",
                      "direct OBS sample-rate query in runtime") &&
         ok;
    ok = reject_token(runtime_source, "obs_source_output_audio",
                      "direct OBS packet output in runtime") &&
         ok;
    return ok ? 0 : 1;
}
