#include "external-data-log.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace {

std::mutex g_sink_mutex;
ExternalDataLog::Sink g_sink;
ExternalDataLog::EnabledProbe g_enabled_probe;

uint64_t fnv1a64(const std::string &value)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

std::string value_payload(const ExternalDataValue &value)
{
    if (!value.is_set)
        return {};
    switch (value.type) {
    case ExternalDataType::Integer:
        return std::to_string(value.integer_value);
    case ExternalDataType::Float: {
        std::ostringstream stream;
        stream << std::setprecision(17) << value.float_value;
        return stream.str();
    }
    case ExternalDataType::Boolean:
        return value.boolean_value ? "true" : "false";
    case ExternalDataType::Color: {
        std::ostringstream stream;
        stream << std::hex << std::setw(8) << std::setfill('0') << value.color_value;
        return stream.str();
    }
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    default:
        return value.string_value;
    }
}

const char *value_type_name(ExternalDataType type)
{
    switch (type) {
    case ExternalDataType::Integer: return "Integer";
    case ExternalDataType::Float: return "Float";
    case ExternalDataType::Boolean: return "Boolean";
    case ExternalDataType::Color: return "Color";
    case ExternalDataType::DateTime: return "DateTime";
    case ExternalDataType::FilePath: return "FilePath";
    case ExternalDataType::Url: return "Url";
    case ExternalDataType::String:
    default: return "String";
    }
}

} // namespace

namespace ExternalDataLog {

void set_sink(Sink sink, EnabledProbe enabled_probe)
{
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    g_sink = std::move(sink);
    g_enabled_probe = std::move(enabled_probe);
}

void clear_sink()
{
    std::lock_guard<std::mutex> lock(g_sink_mutex);
    g_sink = {};
    g_enabled_probe = {};
}

bool enabled(ExternalDataLogLevel level)
{
    EnabledProbe probe;
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(g_sink_mutex);
        sink = g_sink;
        probe = g_enabled_probe;
    }
    if (!sink)
        return false;
    if (!probe)
        return true;
    try {
        return probe(level);
    } catch (...) {
        return false;
    }
}

void write(ExternalDataLogLevel level, const std::string &component,
           const std::string &message)
{
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(g_sink_mutex);
        sink = g_sink;
    }
    if (!sink)
        return;
    try {
        sink(level, sanitize(component, 96), sanitize(message, 4096));
    } catch (...) {
        /* Logging must never affect provider, UI or render behavior. */
    }
}

std::string sanitize(const std::string &value, std::size_t maximum_length)
{
    std::string clean;
    clean.reserve(std::min(value.size(), maximum_length));
    bool previous_space = false;
    for (unsigned char ch : value) {
        if (clean.size() >= maximum_length)
            break;
        const bool whitespace = std::isspace(ch) != 0 || ch < 0x20 || ch == 0x7f;
        if (whitespace) {
            if (!previous_space && !clean.empty())
                clean.push_back(' ');
            previous_space = true;
            continue;
        }
        clean.push_back(static_cast<char>(ch));
        previous_space = false;
    }
    while (!clean.empty() && clean.back() == ' ')
        clean.pop_back();
    if (value.size() > maximum_length)
        clean += "...";
    return clean;
}

std::string safe_location(const std::string &location)
{
    std::string clean = sanitize(location, 1024);
    const std::size_t scheme = clean.find("://");
    if (scheme != std::string::npos) {
        const std::size_t authority_begin = scheme + 3;
        const std::size_t path_begin = clean.find('/', authority_begin);
        const std::size_t authority_end = path_begin == std::string::npos
            ? clean.size() : path_begin;
        const std::size_t at = clean.rfind('@', authority_end);
        if (at != std::string::npos && at >= authority_begin)
            clean.erase(authority_begin, at - authority_begin + 1);
        const std::size_t query = clean.find('?', authority_begin);
        if (query != std::string::npos)
            clean.erase(query);
        const std::size_t fragment = clean.find('#', authority_begin);
        if (fragment != std::string::npos)
            clean.erase(fragment);
    }
    return clean;
}

std::string value_summary(const ExternalDataValue &value)
{
    std::ostringstream stream;
    stream << "type=" << value_type_name(value.type)
           << " set=" << (value.is_set ? 1 : 0);
    if (!value.is_set)
        return stream.str();
    const std::string payload = value_payload(value);
    stream << " empty=" << (payload.empty() ? 1 : 0)
           << " bytes=" << payload.size()
           << " fingerprint=" << std::hex << std::setw(16)
           << std::setfill('0') << fnv1a64(payload);
    return stream.str();
}

std::string provider_type_name(ExternalDataProviderType type)
{
    switch (type) {
    case ExternalDataProviderType::JsonFile: return "JsonFile";
    case ExternalDataProviderType::CsvFile: return "CsvFile";
    case ExternalDataProviderType::HttpJson: return "HttpJson";
    case ExternalDataProviderType::WebSocket: return "WebSocket";
    case ExternalDataProviderType::LocalTextFile: return "LocalTextFile";
    case ExternalDataProviderType::ManualTable: return "ManualTable";
    case ExternalDataProviderType::None:
    default: return "None";
    }
}

std::string connection_state_name(ExternalDataConnectionState state)
{
    switch (state) {
    case ExternalDataConnectionState::Connecting: return "Connecting";
    case ExternalDataConnectionState::Connected: return "Connected";
    case ExternalDataConnectionState::Error: return "Error";
    case ExternalDataConnectionState::Updating: return "Updating";
    case ExternalDataConnectionState::Stale: return "Stale";
    case ExternalDataConnectionState::Disconnected:
    default: return "Disconnected";
    }
}

std::string refresh_mode_name(ExternalDataRefreshMode mode)
{
    switch (mode) {
    case ExternalDataRefreshMode::RefreshOnCue: return "OnCue";
    case ExternalDataRefreshMode::RefreshManually: return "Manual";
    case ExternalDataRefreshMode::RefreshContinuously:
    default: return "Continuous";
    }
}

std::string table_update_mode_name(LiveTextTableUpdateMode mode)
{
    switch (mode) {
    case LiveTextTableUpdateMode::ReplaceRows: return "Replace";
    case LiveTextTableUpdateMode::AppendRows: return "Append";
    case LiveTextTableUpdateMode::SynchronizeRows:
    default: return "Synchronize";
    }
}

} // namespace ExternalDataLog
