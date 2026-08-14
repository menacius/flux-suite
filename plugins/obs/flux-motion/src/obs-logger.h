#pragma once

#include "logger.h"

namespace fxm::obs_plugin {

class ObsLogger final : public ILogger {
public:
    bool enabled(LogLevel level,
                 const std::string &category) const override;
    void log(LogLevel level, const std::string &category,
             const std::string &message) override;
};

ObsLogger &obs_logger() noexcept;

} // namespace fxm::obs_plugin
