#pragma once

#include <string>

namespace fxm {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual bool enabled(LogLevel level, const std::string &category) const = 0;
    virtual void log(LogLevel level, const std::string &category,
                     const std::string &message) = 0;
};

void set_logger(ILogger *logger) noexcept;
ILogger *logger() noexcept;
void log(LogLevel level, const std::string &category,
         const std::string &message);

} // namespace fxm
