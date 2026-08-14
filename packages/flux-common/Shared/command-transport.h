#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fxm::communication {

enum class CommandType : std::uint8_t {
    LoadProject,
    UnloadProject,
    Play,
    Pause,
    Stop,
    Seek,
    SetPreviewFrame,
    RefreshProject,
    Custom,
};

struct CommandEnvelope {
    std::uint64_t sequence = 0;
    CommandType type = CommandType::Custom;
    std::string project_id;
    std::vector<std::uint8_t> payload;
};

using CommandHandler = std::function<void(const CommandEnvelope &)>;

class ICommandTransport {
public:
    virtual ~ICommandTransport() = default;
    virtual bool start(const std::string &endpoint, CommandHandler handler,
                       std::string *error) = 0;
    virtual void stop() noexcept = 0;
    virtual bool send(const CommandEnvelope &command, std::string *error) = 0;
    virtual bool running() const noexcept = 0;
};

} // namespace fxm::communication
