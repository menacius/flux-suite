#pragma once

#include <string>

namespace fxm {

enum class PlaybackState {
    Empty,
    Ready,
    Playing,
    Paused,
    Stopped,
    Error,
};

class IPlaybackController {
public:
    virtual ~IPlaybackController() = default;
    virtual bool load_project(const std::string &path, std::string *error) = 0;
    virtual void unload_project() noexcept = 0;
    virtual bool play(std::string *error) = 0;
    virtual bool pause(std::string *error) = 0;
    virtual bool stop(std::string *error) = 0;
    virtual bool seek(double time_seconds, std::string *error) = 0;
    virtual PlaybackState state() const noexcept = 0;
    virtual double position_seconds() const noexcept = 0;
};

} // namespace fxm
