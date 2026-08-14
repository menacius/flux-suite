#pragma once

#include "external-data-types.h"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>

enum class ExternalDataLogLevel {
    Error = 1,
    Warning = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
};

namespace ExternalDataLog {

using Sink = std::function<void(ExternalDataLogLevel level,
                                const std::string &component,
                                const std::string &message)>;
using EnabledProbe = std::function<bool(ExternalDataLogLevel level)>;

void set_sink(Sink sink, EnabledProbe enabled_probe = {});
void clear_sink();
bool enabled(ExternalDataLogLevel level);
void write(ExternalDataLogLevel level, const std::string &component,
           const std::string &message);

template<typename Producer>
void write_lazy(ExternalDataLogLevel level, const std::string &component,
                Producer &&producer)
{
    if (!enabled(level))
        return;
    write(level, component, std::forward<Producer>(producer)());
}

std::string sanitize(const std::string &value, std::size_t maximum_length = 512);
std::string safe_location(const std::string &location);
std::string value_summary(const ExternalDataValue &value);
std::string provider_type_name(ExternalDataProviderType type);
std::string connection_state_name(ExternalDataConnectionState state);
std::string refresh_mode_name(ExternalDataRefreshMode mode);
std::string table_update_mode_name(LiveTextTableUpdateMode mode);

} // namespace ExternalDataLog
