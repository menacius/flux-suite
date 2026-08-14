#include "logger.h"

#include <atomic>

namespace {
std::atomic<fxm::ILogger *> g_logger{nullptr};
}

namespace fxm {

void set_logger(ILogger *value) noexcept
{
    g_logger.store(value, std::memory_order_release);
}

ILogger *logger() noexcept
{
    return g_logger.load(std::memory_order_acquire);
}

void log(LogLevel level, const std::string &category,
         const std::string &message)
{
    auto *sink = logger();
    if (sink && sink->enabled(level, category))
        sink->log(level, category, message);
}

} // namespace fxm
