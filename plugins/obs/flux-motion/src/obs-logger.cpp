#include "obs-logger.h"

#include <obs-module.h>

namespace fxm::obs_plugin {
namespace {

int obs_level(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return LOG_ERROR;
    case LogLevel::Warning: return LOG_WARNING;
    case LogLevel::Trace:
    case LogLevel::Debug: return LOG_DEBUG;
    case LogLevel::Info:
    default: return LOG_INFO;
    }
}

} // namespace

bool ObsLogger::enabled(LogLevel, const std::string &) const
{
    return true;
}

void ObsLogger::log(LogLevel level, const std::string &,
                    const std::string &message)
{
    blog(obs_level(level), "[Flux Motion] %s", message.c_str());
}

ObsLogger &obs_logger() noexcept
{
    static ObsLogger logger;
    return logger;
}

} // namespace fxm::obs_plugin
