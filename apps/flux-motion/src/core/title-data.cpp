/*
 * title-data.cpp
 */

#include "title-data.h"
#include "title-snapshot.h"
#include "title-serialization-schema.h"
#include "extensions/effect-extension-catalog.h"
#include "effects/effect-preset-catalog.h"
#include "effects/effect-runtime.h"
#include "title-logger.h"
#include "packed-title-format.h"
#include "ticker-runtime.h"
#include "asset-runtime.h"
#include "linked-asset.h"
#include "external-data.h"
#include "external-data-provider.h"
#include "text-animator-presets.h"
#include "host-context-provider.h"
#include "logger.h"
#include "utf8-filesystem.h"

#include <QSaveFile>
#include <QString>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QJsonDocument>
#include <QRawFont>
#include <QSet>
#include <QStandardPaths>

#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <stdexcept>
#include <cstdio>
#include <limits>
#include <utility>
#include <cctype>
#include <ctime>
#include <thread>

using json = nlohmann::json;

static bool resolve_packed_uri_json(json &value, const QString &root,
                                    std::string *error,
                                    const std::string &owner_key = {});

namespace {
constexpr std::streamoff kMaxJsonFileBytes = 512 * 1024 * 1024;
constexpr std::streamoff kMaxEmbeddedAssetBytes = 100 * 1024 * 1024;
constexpr size_t kMaxTitles = 256;
constexpr size_t kMaxLayersPerTitle = 256;
constexpr size_t kMaxKeyframesPerProperty = 2048;
constexpr size_t kMaxLiveTextRows = 256;
constexpr size_t kMaxLiveTextColumns = 32;
constexpr size_t kMaxExternalDataSources = 64;
constexpr size_t kMaxExternalDataFieldsPerSource = 256;
constexpr size_t kMaxExternalBindingsPerLayer = 128;
constexpr size_t kMaxNameLength = 256;
constexpr size_t kMaxTextLength = 8192;
constexpr size_t kMaxPathLength = 4096;
constexpr size_t kMaxScreenshotBase64Length = 32 * 1024 * 1024;
constexpr double kMaxDuration = 3600.0;
constexpr double kMaxPropertyValue = 100000.0;
constexpr int kMaxCanvasDimension = 16384;

std::mutex g_live_cue_runtime_mutex;
std::unordered_map<std::string, LiveCueRuntimeSnapshot> g_live_cue_runtime_states;
std::mutex g_packed_font_registry_mutex;
std::unordered_set<std::string> g_registered_packed_font_paths;

static double finite_or(double value, double fallback)
{
    return std::isfinite(value) ? value : fallback;
}

static json passthrough_json_object(const std::string &payload)
{
    if (payload.empty())
        return json::object();
    json parsed = json::parse(payload, nullptr, false);
    return parsed.is_object() ? std::move(parsed) : json::object();
}

static json passthrough_json_object(const OpaqueSerializationPassthrough &payload)
{
    return passthrough_json_object(payload.str());
}

static json passthrough_array_item(const json &owner, const char *key, size_t index)
{
    if (!owner.is_object())
        return json::object();
    const auto it = owner.find(key);
    if (it == owner.end() || !it->is_array() || index >= it->size() || !(*it)[index].is_object())
        return json::object();
    return (*it)[index];
}


static const json *matching_passthrough_array_item(const json &base_array,
                                                    const json &known_item,
                                                    size_t fallback_index)
{
    if (!base_array.is_array() || !known_item.is_object())
        return nullptr;
    const char *keys[] = {"id", "time", "path", "property_path", "layer_id"};
    for (const char *key : keys) {
        const auto known = known_item.find(key);
        if (known == known_item.end() || known->is_null())
            continue;
        for (const auto &candidate : base_array) {
            if (candidate.is_object() && candidate.contains(key) && candidate[key] == *known)
                return &candidate;
        }
    }
    if (fallback_index < base_array.size() && base_array[fallback_index].is_object())
        return &base_array[fallback_index];
    return nullptr;
}

static json merge_nested_passthrough(const json &base, const json &known)
{
    if (known.is_object()) {
        json result = base.is_object() ? base : json::object();
        for (auto it = known.begin(); it != known.end(); ++it) {
            const auto base_it = result.find(it.key());
            result[it.key()] = base_it != result.end()
                ? merge_nested_passthrough(*base_it, it.value())
                : it.value();
        }
        return result;
    }
    if (known.is_array()) {
        json result = json::array();
        result.get_ref<json::array_t&>().reserve(known.size());
        for (size_t index = 0; index < known.size(); ++index) {
            const json *candidate = matching_passthrough_array_item(base, known[index], index);
            result.push_back(candidate
                ? merge_nested_passthrough(*candidate, known[index])
                : known[index]);
        }
        return result;
    }
    return known;
}

/* The root object already contains only fields that should survive. This pass
 * deep-merges just those surviving keys, preserving unknown members inside
 * animated properties, keyframes, effects, cameras and provider definitions. */
static json merge_surviving_passthrough(const json &base, const json &known_root)
{
    if (!known_root.is_object())
        return known_root;
    json result = known_root;
    if (!base.is_object())
        return result;
    for (auto it = result.begin(); it != result.end(); ++it) {
        const auto base_it = base.find(it.key());
        if (base_it != base.end())
            it.value() = merge_nested_passthrough(*base_it, it.value());
    }
    return result;
}

static void append_unique_import_diagnostic(std::vector<std::string> &items, const std::string &value)
{
    if (value.empty())
        return;
    if (std::find(items.begin(), items.end(), value) == items.end())
        items.push_back(value);
}

static int normalize_gradient_type(int type)
{
    switch (std::clamp(type, 0, 4)) {
    case 1:
        return 1; /* radial */
    case 2:
        return 2; /* conical; legacy Angle used the same conical renderer */
    case 4:
        return 1; /* legacy Diamond falls back to radial */
    case 0:
    case 3:
    default:
        return 0; /* linear; legacy Reflected is represented by spread */
    }
}

static int normalize_gradient_spread(int spread)
{
    return spread == 1 || spread == 2 ? spread : 0;
}

static std::string current_iso_utc_string()
{
    const std::time_t now = std::time(nullptr);
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now);
#else
    gmtime_r(&now, &tm_utc);
#endif
    std::ostringstream out;
    out << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

static void host_log(fxm::LogLevel level, const QString &message)
{
    fxm::log(level, "TitleStore", message.toStdString());
}

static std::string title_data_dir()
{
    std::string dir;
    if (const auto *provider = fxm::host_context_provider())
        dir = provider->config_path("");
    if (dir.empty())
        dir = QStandardPaths::writableLocation(
                  QStandardPaths::AppDataLocation).toStdString();
    QDir().mkpath(QString::fromStdString(dir));
    return dir;
}

static uint64_t fnv1a64(const std::string &value)
{
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string hex64(uint64_t value)
{
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

static std::string current_scene_collection_name()
{
    std::string name;
    if (const auto *provider = fxm::host_context_provider())
        name = provider->project_scope_name();
    if (name.empty())
        name = "unknown-scene-collection";
    return name;
}

static std::string safe_scene_collection_file_stem(const std::string &name)
{
    std::string safe;
    safe.reserve(std::min<size_t>(name.size(), 80));
    for (unsigned char ch : name) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
            safe.push_back((char)ch);
        else
            safe.push_back('_');
        if (safe.size() >= 80)
            break;
    }
    if (safe.empty())
        safe = "scene-collection";
    return safe + "-" + hex64(fnv1a64(name));
}

static std::string bounded_string(const json &j, const char *key,
                                  const std::string &fallback,
                                  size_t max_len)
{
    if (!j.is_object() || !j.contains(key) || !j[key].is_string())
        return fallback;
    std::string value = j[key].get<std::string>();
    if (value.size() > max_len)
        value.resize(max_len);
    return value;
}

static std::string bounded_json_payload(const json &j, const char *key,
                                        const std::string &fallback,
                                        size_t max_len)
{
    if (!j.is_object() || !j.contains(key))
        return fallback;
    std::string value;
    if (j[key].is_string()) {
        value = j[key].get<std::string>();
    } else if (j[key].is_object() || j[key].is_array()) {
        value = j[key].dump(-1, ' ', false, json::error_handler_t::replace);
    } else {
        return fallback;
    }
    if (value.size() > max_len)
        value.resize(max_len);
    return value;
}

static const json *object_member(const json &j, const char *key)
{
    if (!j.is_object())
        return nullptr;
    auto it = j.find(key);
    return it == j.end() ? nullptr : &*it;
}

static bool json_bool(const json &j, const char *key, bool fallback)
{
    const json *value = object_member(j, key);
    return value && value->is_boolean() ? value->get<bool>() : fallback;
}

static int json_int(const json &j, const char *key, int fallback)
{
    const json *value = object_member(j, key);
    if (!value || !value->is_number_integer())
        return fallback;
    const int64_t parsed = value->get<int64_t>();
    if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
        return fallback;
    return (int)parsed;
}

static int gradient_spread_from_json(const json &j, const char *spread_key,
                                     const char *type_key, int fallback_spread = 0)
{
    const int legacy_type = std::clamp(json_int(j, type_key, 0), 0, 4);
    if (!j.contains(spread_key) && legacy_type == 3)
        return 1; /* legacy Reflected */
    return normalize_gradient_spread(json_int(j, spread_key, fallback_spread));
}

static double json_double(const json &j, const char *key, double fallback)
{
    const json *value = object_member(j, key);
    return value && value->is_number() ? finite_or(value->get<double>(), fallback) : fallback;
}

static uint32_t json_color(const json &j, const char *key, uint32_t fallback)
{
    const json *value = object_member(j, key);
    if (!value)
        return fallback;
    if (value->is_number_unsigned()) {
        const uint64_t parsed = value->get<uint64_t>();
        return parsed <= UINT32_MAX ? (uint32_t)parsed : fallback;
    }
    if (value->is_number_integer()) {
        const int64_t parsed = value->get<int64_t>();
        return parsed >= 0 && parsed <= UINT32_MAX ? (uint32_t)parsed : fallback;
    }
    return fallback;
}


static json external_value_to_json(const ExternalDataValue &value)
{
    json result;
    result["type"] = static_cast<int>(value.type);
    result["is_set"] = value.is_set;
    if (!value.is_set)
        return result;
    switch (value.type) {
    case ExternalDataType::Integer:
        result["value"] = value.integer_value;
        break;
    case ExternalDataType::Float:
        result["value"] = value.float_value;
        break;
    case ExternalDataType::Boolean:
        result["value"] = value.boolean_value;
        break;
    case ExternalDataType::Color:
        result["value"] = value.color_value;
        break;
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    default:
        result["value"] = value.string_value;
        break;
    }
    return result;
}

static ExternalDataValue external_value_from_json(const json &j,
                                                   ExternalDataType fallback_type)
{
    ExternalDataValue value;
    value.type = static_cast<ExternalDataType>(std::clamp(
        json_int(j, "type", static_cast<int>(fallback_type)),
        static_cast<int>(ExternalDataType::String),
        static_cast<int>(ExternalDataType::Url)));
    value.is_set = json_bool(j, "is_set", j.is_object() && j.contains("value"));
    if (!value.is_set || !j.is_object() || !j.contains("value"))
        return value;
    const json &stored = j["value"];
    switch (value.type) {
    case ExternalDataType::Integer:
        if (stored.is_number_integer())
            value.integer_value = stored.get<int64_t>();
        else
            value.is_set = false;
        break;
    case ExternalDataType::Float:
        if (stored.is_number())
            value.float_value = finite_or(stored.get<double>(), 0.0);
        else
            value.is_set = false;
        break;
    case ExternalDataType::Boolean:
        if (stored.is_boolean())
            value.boolean_value = stored.get<bool>();
        else
            value.is_set = false;
        break;
    case ExternalDataType::Color:
        if (stored.is_number_unsigned()) {
            const uint64_t parsed = stored.get<uint64_t>();
            value.color_value = parsed <= UINT32_MAX ? static_cast<uint32_t>(parsed) : 0xFFFFFFFFu;
        } else if (stored.is_number_integer()) {
            const int64_t parsed = stored.get<int64_t>();
            if (parsed >= 0 && parsed <= UINT32_MAX)
                value.color_value = static_cast<uint32_t>(parsed);
            else
                value.is_set = false;
        } else {
            value.is_set = false;
        }
        break;
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    default:
        if (stored.is_string()) {
            value.string_value = stored.get<std::string>();
            if (value.string_value.size() > kMaxTextLength)
                value.string_value.resize(kMaxTextLength);
        } else {
            value.is_set = false;
        }
        break;
    }
    return value;
}

static json external_string_map_to_json(const std::map<std::string, std::string> &values)
{
    json result = json::object();
    for (const auto &entry : values) {
        if (!entry.first.empty())
            result[entry.first] = entry.second;
    }
    return result;
}

static std::map<std::string, std::string> external_string_map_from_json(
    const json &j, size_t max_entries = 256)
{
    std::map<std::string, std::string> result;
    if (!j.is_object())
        return result;
    size_t count = 0;
    for (auto it = j.begin(); it != j.end() && count < max_entries; ++it) {
        if (!it.value().is_string())
            continue;
        std::string key = it.key();
        std::string value = it.value().get<std::string>();
        if (key.size() > kMaxPathLength)
            key.resize(kMaxPathLength);
        if (value.size() > kMaxTextLength)
            value.resize(kMaxTextLength);
        if (!key.empty()) {
            result[std::move(key)] = std::move(value);
            ++count;
        }
    }
    return result;
}

static json external_provider_to_json(const ExternalDataProviderConfig &provider,
                                      const json &passthrough = json::object())
{
    json result = passthrough.is_object() ? passthrough : json::object();
    result.update(json{
        {"type", static_cast<int>(provider.type)},
        {"enabled", provider.enabled},
        {"location", provider.location},
        {"polling_interval_ms", provider.polling_interval_ms},
        {"refresh_mode", static_cast<int>(provider.refresh_mode)},
        {"rate_limit_ms", provider.rate_limit_ms},
        {"keep_last_value", provider.keep_last_value},
        {"stale_after_ms", provider.stale_after_ms},
        {"root_path", provider.root_path},
        {"headers", external_string_map_to_json(provider.headers)},
        {"authentication_token", provider.authentication_token},
        {"timeout_ms", provider.timeout_ms},
        {"retry_count", provider.retry_count},
        {"retry_backoff_ms", provider.retry_backoff_ms},
        {"reconnect_initial_ms", provider.reconnect_initial_ms},
        {"reconnect_max_ms", provider.reconnect_max_ms},
        {"csv_first_row_headers", provider.csv_first_row_headers},
        {"csv_row_index", provider.csv_row_index},
        {"csv_column_mapping", external_string_map_to_json(provider.csv_column_mapping)},
        {"text_field_path", provider.text_field_path},
    });
    if (!provider.manual_values.empty()) {
        json manual = json::object();
        for (const auto &entry : provider.manual_values) {
            if (!entry.first.empty())
                manual[entry.first] = external_value_to_json(entry.second);
        }
        result["manual_values"] = std::move(manual);
    } else {
        result.erase("manual_values");
    }
    return merge_surviving_passthrough(passthrough, result);
}

static ExternalDataProviderConfig external_provider_from_json(const json &j)
{
    ExternalDataProviderConfig provider;
    if (!j.is_object())
        return provider;
    const int provider_type = json_int(
        j, "type", static_cast<int>(ExternalDataProviderType::None));
    const bool provider_supported =
        provider_type >= static_cast<int>(ExternalDataProviderType::None) &&
        provider_type <= static_cast<int>(ExternalDataProviderType::ManualTable);
    provider.type = provider_supported
        ? static_cast<ExternalDataProviderType>(provider_type)
        : ExternalDataProviderType::None;
    provider.enabled = provider_supported && json_bool(j, "enabled", true);
    provider.location = bounded_string(j, "location", "", kMaxTextLength);
    provider.polling_interval_ms = std::clamp(json_int(j, "polling_interval_ms", 0), 0, 86400000);
    provider.refresh_mode = static_cast<ExternalDataRefreshMode>(std::clamp(
        json_int(j, "refresh_mode", static_cast<int>(ExternalDataRefreshMode::RefreshContinuously)),
        static_cast<int>(ExternalDataRefreshMode::RefreshOnCue),
        static_cast<int>(ExternalDataRefreshMode::RefreshManually)));
    provider.rate_limit_ms = std::clamp(json_int(j, "rate_limit_ms", 50), 0, 60000);
    provider.keep_last_value = json_bool(j, "keep_last_value", true);
    provider.stale_after_ms = std::clamp(json_int(j, "stale_after_ms", 0), 0, 604800000);
    provider.root_path = bounded_string(j, "root_path", "", kMaxPathLength);
    if (j.contains("headers"))
        provider.headers = external_string_map_from_json(j["headers"], 128);
    provider.authentication_token = bounded_string(j, "authentication_token", "", kMaxTextLength);
    provider.timeout_ms = std::clamp(json_int(j, "timeout_ms", 5000), 250, 300000);
    provider.retry_count = std::clamp(json_int(j, "retry_count", 2), 0, 20);
    provider.retry_backoff_ms = std::clamp(json_int(j, "retry_backoff_ms", 1000), 50, 300000);
    provider.reconnect_initial_ms = std::clamp(json_int(j, "reconnect_initial_ms", 1000), 100, 300000);
    provider.reconnect_max_ms = std::clamp(json_int(j, "reconnect_max_ms", 30000),
                                           provider.reconnect_initial_ms, 3600000);
    provider.csv_first_row_headers = json_bool(j, "csv_first_row_headers", true);
    provider.csv_row_index = std::clamp(json_int(j, "csv_row_index", 0), 0, 1000000);
    if (j.contains("csv_column_mapping"))
        provider.csv_column_mapping = external_string_map_from_json(j["csv_column_mapping"]);
    provider.text_field_path = bounded_string(j, "text_field_path", "text", kMaxPathLength);
    if (provider.text_field_path.empty())
        provider.text_field_path = "text";
    if (j.contains("manual_values") && j["manual_values"].is_object()) {
        size_t count = 0;
        for (auto it = j["manual_values"].begin();
             it != j["manual_values"].end() && count < kMaxExternalDataFieldsPerSource;
             ++it) {
            std::string path = it.key();
            if (path.size() > kMaxPathLength)
                path.resize(kMaxPathLength);
            if (path.empty() || !it.value().is_object())
                continue;
            ExternalDataValue value = external_value_from_json(
                it.value(), ExternalDataType::String);
            if (value.is_set) {
                provider.manual_values[std::move(path)] = std::move(value);
                ++count;
            }
        }
    }
    return provider;
}

static json external_formatter_to_json(const ExternalDataFormatterConfig &formatter)
{
    json replacements = json::array();
    for (const auto &rule : formatter.conditional_replacements) {
        replacements.push_back({{"match", rule.match},
                                {"replacement", rule.replacement},
                                {"case_sensitive", rule.case_sensitive}});
    }
    return json{{"prefix", formatter.prefix},
                {"suffix", formatter.suffix},
                {"number_format_enabled", formatter.number_format_enabled},
                {"decimal_places", formatter.decimal_places},
                {"thousands_separator", formatter.thousands_separator},
                {"text_case", static_cast<int>(formatter.text_case)},
                {"date_time_format", formatter.date_time_format},
                {"conditional_replacements", std::move(replacements)},
                {"empty_value_mode", static_cast<int>(formatter.empty_value_mode)},
                {"empty_replacement", formatter.empty_replacement}};
}

static ExternalDataFormatterConfig external_formatter_from_json(const json &j)
{
    ExternalDataFormatterConfig formatter;
    if (!j.is_object())
        return formatter;
    formatter.prefix = bounded_string(j, "prefix", "", kMaxTextLength);
    formatter.suffix = bounded_string(j, "suffix", "", kMaxTextLength);
    formatter.number_format_enabled = json_bool(j, "number_format_enabled", false);
    formatter.decimal_places = std::clamp(json_int(j, "decimal_places", -1), -1, 12);
    formatter.thousands_separator = json_bool(j, "thousands_separator", false);
    formatter.text_case = static_cast<ExternalDataTextCase>(std::clamp(
        json_int(j, "text_case", static_cast<int>(ExternalDataTextCase::None)),
        static_cast<int>(ExternalDataTextCase::None),
        static_cast<int>(ExternalDataTextCase::TitleCase)));
    formatter.date_time_format = bounded_string(j, "date_time_format", "", kMaxTextLength);
    formatter.empty_value_mode = static_cast<ExternalDataEmptyValueMode>(std::clamp(
        json_int(j, "empty_value_mode", static_cast<int>(ExternalDataEmptyValueMode::KeepEmpty)),
        static_cast<int>(ExternalDataEmptyValueMode::KeepEmpty),
        static_cast<int>(ExternalDataEmptyValueMode::Replacement)));
    formatter.empty_replacement = bounded_string(j, "empty_replacement", "", kMaxTextLength);
    if (j.contains("conditional_replacements") &&
        j["conditional_replacements"].is_array()) {
        const size_t count = std::min<size_t>(j["conditional_replacements"].size(), 64);
        formatter.conditional_replacements.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto &item = j["conditional_replacements"][i];
            if (!item.is_object())
                continue;
            ExternalDataConditionalReplacement rule;
            rule.match = bounded_string(item, "match", "", kMaxTextLength);
            rule.replacement = bounded_string(item, "replacement", "", kMaxTextLength);
            rule.case_sensitive = json_bool(item, "case_sensitive", true);
            formatter.conditional_replacements.push_back(std::move(rule));
        }
    }
    return formatter;
}

static json external_binding_to_json(const ExternalPropertyBinding &binding)
{
    json result = {
        {"enabled", binding.enabled},
        {"property_path", binding.property_path},
        {"source_id", binding.source_id},
        {"field_path", binding.field_path},
        {"formatter", binding.formatter},
        {"formatter_config", external_formatter_to_json(binding.formatter_config)},
        {"has_fallback_value", binding.has_fallback_value},
    };
    if (binding.has_fallback_value)
        result["fallback_value"] = external_value_to_json(binding.fallback_value);
    return result;
}

static ExternalPropertyBinding external_binding_from_json(const json &j)
{
    ExternalPropertyBinding binding;
    binding.enabled = json_bool(j, "enabled", true);
    binding.property_path = bounded_string(j, "property_path", "", kMaxNameLength);
    binding.source_id = bounded_string(j, "source_id", "", kMaxNameLength);
    binding.field_path = bounded_string(j, "field_path", "", kMaxPathLength);
    binding.formatter = bounded_string(j, "formatter", "", kMaxTextLength);
    if (j.contains("formatter_config"))
        binding.formatter_config = external_formatter_from_json(j["formatter_config"]);
    binding.has_fallback_value = json_bool(j, "has_fallback_value",
        j.is_object() && j.contains("fallback_value"));
    if (binding.has_fallback_value && j.contains("fallback_value"))
        binding.fallback_value = external_value_from_json(
            j["fallback_value"], ExternalDataType::String);
    return binding;
}

static json live_text_external_binding_to_json(const LiveTextExternalBinding &cell)
{
    return json{{"row_id", cell.row_id},
                {"layer_id", cell.layer_id},
                {"table_binding_id", cell.table_binding_id},
                {"binding", external_binding_to_json(cell.binding)}};
}

static LiveTextExternalBinding live_text_external_binding_from_json(const json &j)
{
    LiveTextExternalBinding cell;
    if (!j.is_object())
        return cell;
    cell.row_id = bounded_string(j, "row_id", "", kMaxNameLength);
    cell.layer_id = bounded_string(j, "layer_id", "", kMaxNameLength);
    cell.table_binding_id = bounded_string(j, "table_binding_id", "", kMaxNameLength);
    if (j.contains("binding") && j["binding"].is_object())
        cell.binding = external_binding_from_json(j["binding"]);
    return cell;
}

static json live_text_table_binding_to_json(const LiveTextTableBinding &mapping)
{
    json columns = json::array();
    for (const auto &column : mapping.columns) {
        columns.push_back(json{{"layer_id", column.layer_id},
                               {"binding", external_binding_to_json(column.binding)}});
    }
    return json{{"enabled", mapping.enabled},
                {"id", mapping.id},
                {"source_id", mapping.source_id},
                {"table_path", mapping.table_path},
                {"update_mode", static_cast<int>(mapping.update_mode)},
                {"row_id_field", mapping.row_id_field},
                {"start_row", mapping.start_row},
                {"maximum_rows", mapping.maximum_rows},
                {"ignore_empty_rows", mapping.ignore_empty_rows},
                {"preserve_manual_rows", mapping.preserve_manual_rows},
                {"columns", std::move(columns)}};
}

static LiveTextTableBinding live_text_table_binding_from_json(const json &j)
{
    LiveTextTableBinding mapping;
    if (!j.is_object())
        return mapping;
    mapping.enabled = json_bool(j, "enabled", true);
    mapping.id = bounded_string(j, "id", "", kMaxNameLength);
    mapping.source_id = bounded_string(j, "source_id", "", kMaxNameLength);
    mapping.table_path = bounded_string(j, "table_path", "", kMaxPathLength);
    mapping.update_mode = static_cast<LiveTextTableUpdateMode>(std::clamp(
        json_int(j, "update_mode", static_cast<int>(LiveTextTableUpdateMode::SynchronizeRows)),
        static_cast<int>(LiveTextTableUpdateMode::ReplaceRows),
        static_cast<int>(LiveTextTableUpdateMode::SynchronizeRows)));
    mapping.row_id_field = bounded_string(j, "row_id_field", "", kMaxPathLength);
    mapping.start_row = std::clamp(json_int(j, "start_row", 0), 0, 1000000);
    mapping.maximum_rows = std::clamp(json_int(j, "maximum_rows", 0), 0, 1000000);
    mapping.ignore_empty_rows = json_bool(j, "ignore_empty_rows", true);
    mapping.preserve_manual_rows = json_bool(j, "preserve_manual_rows", true);
    if (j.contains("columns") && j["columns"].is_array()) {
        const size_t count = std::min(j["columns"].size(), kMaxLiveTextColumns);
        mapping.columns.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const json &item = j["columns"][i];
            if (!item.is_object())
                continue;
            LiveTextTableColumnBinding column;
            column.layer_id = bounded_string(item, "layer_id", "", kMaxNameLength);
            if (item.contains("binding") && item["binding"].is_object())
                column.binding = external_binding_from_json(item["binding"]);
            if (!column.layer_id.empty() && !column.binding.field_path.empty())
                mapping.columns.push_back(std::move(column));
        }
    }
    return mapping;
}

static json external_source_to_json(const ExternalDataSourceDefinition &source,
                                    const json &passthrough = json::object())
{
    json result = passthrough.is_object() ? passthrough : json::object();
    json fields = json::array();
    for (size_t index = 0; index < source.fields.size(); ++index) {
        const auto &field = source.fields[index];
        json item = passthrough_array_item(result, "fields", index);
        item.update(json{
            {"path", field.path},
            {"name", field.name},
            {"type", static_cast<int>(field.type)},
            {"has_default_value", field.has_default_value},
        });
        if (field.has_default_value)
            item["default_value"] = external_value_to_json(field.default_value);
        else
            item.erase("default_value");
        fields.push_back(std::move(item));
    }
    const json provider_passthrough = result.contains("provider") && result["provider"].is_object()
        ? result["provider"] : json::object();
    result.update(json{{"id", source.id}, {"name", source.name},
                       {"fields", std::move(fields)},
                       {"provider", external_provider_to_json(source.provider, provider_passthrough)}});
    return merge_surviving_passthrough(passthrough, result);
}

static ExternalDataSourceDefinition external_source_from_json(const json &j)
{
    ExternalDataSourceDefinition source;
    source.id = bounded_string(j, "id", "", kMaxNameLength);
    source.name = bounded_string(j, "name", source.id, kMaxNameLength);
    if (j.is_object() && j.contains("provider"))
        source.provider = external_provider_from_json(j["provider"]);
    if (!j.is_object() || !j.contains("fields") || !j["fields"].is_array())
        return source;
    const size_t count = std::min(j["fields"].size(), kMaxExternalDataFieldsPerSource);
    source.fields.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const json &fj = j["fields"][i];
        if (!fj.is_object())
            continue;
        ExternalDataFieldDefinition field;
        field.path = bounded_string(fj, "path", "", kMaxPathLength);
        field.name = bounded_string(fj, "name", field.path, kMaxNameLength);
        field.type = static_cast<ExternalDataType>(std::clamp(
            json_int(fj, "type", static_cast<int>(ExternalDataType::String)),
            static_cast<int>(ExternalDataType::String),
            static_cast<int>(ExternalDataType::Url)));
        field.has_default_value = json_bool(fj, "has_default_value",
            fj.contains("default_value"));
        if (field.has_default_value && fj.contains("default_value"))
            field.default_value = external_value_from_json(fj["default_value"], field.type);
        if (!field.path.empty())
            source.fields.push_back(std::move(field));
    }
    return source;
}


static bool file_exists(const std::string &path)
{
    std::ifstream f(fxm::filesystem_path_from_utf8(path), std::ios::binary);
    return f.is_open();
}

static bool register_packed_font_file(const QString &path)
{
    const QFileInfo info(path);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();
    if (!info.exists() || !info.isFile() || !info.isReadable())
        return false;
    const std::string key = canonical.toStdString();
    std::lock_guard<std::mutex> lock(g_packed_font_registry_mutex);
    if (g_registered_packed_font_paths.count(key) != 0)
        return true;
    if (QFontDatabase::addApplicationFont(canonical) < 0)
        return false;
    g_registered_packed_font_paths.insert(key);
    return true;
}

static std::string file_name_from_path(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string sanitize_asset_file_name(const std::string &file_name)
{
    std::string sanitized;
    sanitized.reserve(file_name.size());
    for (unsigned char ch : file_name) {
        if (std::isalnum(ch) || ch == '.' || ch == '-' || ch == '_')
            sanitized.push_back((char)ch);
        else
            sanitized.push_back('_');
    }
    while (!sanitized.empty() && sanitized.front() == '.')
        sanitized.erase(sanitized.begin());
    if (sanitized.empty())
        sanitized = "image.bin";
    if (sanitized.size() > 160)
        sanitized.resize(160);
    return sanitized;
}

static std::string lower_extension(const std::string &file_name)
{
    const size_t dot = file_name.find_last_of('.');
    if (dot == std::string::npos)
        return {};
    std::string ext = file_name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    return ext;
}

static std::string mime_type_for_file_name(const std::string &file_name)
{
    const std::string ext = lower_extension(file_name);
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "webp") return "image/webp";
    if (ext == "bmp") return "image/bmp";
    if (ext == "svg" || ext == "svgz") return "image/svg+xml";
    return "application/octet-stream";
}

static bool read_binary_file(const std::string &path, std::string &out, std::streamoff max_bytes, std::string *error)
{
    std::ifstream f(fxm::filesystem_path_from_utf8(path), std::ios::binary);
    if (!f.is_open()) {
        if (error) *error = "Could not open asset file: " + path;
        return false;
    }

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || size > max_bytes) {
        if (error) *error = "Asset file is too large to embed: " + path;
        return false;
    }
    f.seekg(0, std::ios::beg);

    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        if (error) *error = "Failed while reading asset file: " + path;
        return false;
    }
    return true;
}

static std::string base64_encode(const std::string &data)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((data.size() + 2) / 3) * 4);

    int value = 0;
    int bits = -6;
    for (unsigned char ch : data) {
        value = (value << 8) + ch;
        bits += 8;
        while (bits >= 0) {
            encoded.push_back(table[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6)
        encoded.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
    while (encoded.size() % 4)
        encoded.push_back('=');
    return encoded;
}

static bool base64_decode(const std::string &encoded, std::string &out)
{
    static const std::string table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> reverse(256, -1);
    for (int i = 0; i < (int)table.size(); ++i)
        reverse[(unsigned char)table[i]] = i;

    out.clear();
    int value = 0;
    int bits = -8;
    for (unsigned char ch : encoded) {
        if (std::isspace(ch))
            continue;
        if (ch == '=')
            break;
        if (reverse[ch] == -1)
            return false;
        value = (value << 6) + reverse[ch];
        bits += 6;
        if (bits >= 0) {
            out.push_back((char)((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return true;
}

static uint64_t fnv1a_64(const std::string &data)
{
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : data) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::string hex_u64(uint64_t value)
{
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << value;
    return ss.str();
}

static std::string embedded_assets_dir()
{
    const std::string assets = title_data_dir() + "/assets";
    QDir().mkpath(QString::fromStdString(assets));
    return assets;
}

static bool attach_embedded_image_asset(const Layer &layer, json &j, bool required, std::string *error)
{
    if (layer.type != LayerType::Image || layer.image_path.empty())
        return true;

    std::string data;
    if (!read_binary_file(layer.image_path, data, kMaxEmbeddedAssetBytes, error))
        return !required;

    const std::string file_name = sanitize_asset_file_name(file_name_from_path(layer.image_path));
    json asset;
    asset["file_name"] = file_name;
    asset["mime_type"] = mime_type_for_file_name(file_name);
    asset["size"] = data.size();
    asset["hash"] = hex_u64(fnv1a_64(data));
    asset["data_base64"] = base64_encode(data);
    j["embedded_image"] = std::move(asset);
    FXM_LOG_DEBUG("Assets", QStringLiteral(
        "Embedded image asset layer=%1 file=%2 bytes=%3")
        .arg(QString::fromStdString(layer.id), QString::fromStdString(file_name))
        .arg(static_cast<qulonglong>(data.size())));
    return true;
}

static bool restore_embedded_image_asset(const json &j, std::string &image_path)
{
    const json *asset = object_member(j, "embedded_image");
    if (!asset || !asset->is_object())
        return false;

    const std::string data64 = bounded_string(*asset, "data_base64", "", (size_t)kMaxEmbeddedAssetBytes * 2);
    if (data64.empty())
        return false;

    std::string data;
    if (!base64_decode(data64, data) || data.empty() || (std::streamoff)data.size() > kMaxEmbeddedAssetBytes)
        return false;

    std::string file_name = sanitize_asset_file_name(bounded_string(*asset, "file_name", "image.bin", kMaxNameLength));
    const std::string hash = hex_u64(fnv1a_64(data));
    file_name = hash + "-" + file_name;

    const std::string path = embedded_assets_dir() + "/" + file_name;
    if (!file_exists(path)) {
        QSaveFile file(QString::fromStdString(path));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        const qint64 expected = static_cast<qint64>(data.size());
        if (file.write(data.data(), expected) != expected) {
            file.cancelWriting();
            return false;
        }
        if (!file.commit())
            return false;
    }

    image_path = path;
    FXM_LOG_DEBUG("Assets", QStringLiteral(
        "Restored embedded image asset file=%1 bytes=%2")
        .arg(QString::fromStdString(path))
        .arg(static_cast<qulonglong>(data.size())));
    return true;
}

static bool read_json_file(const std::string &path, json &out, std::string *error)
{
    std::ifstream f(fxm::filesystem_path_from_utf8(path), std::ios::binary);
    if (!f.is_open()) {
        if (error) *error = "Could not open the file.";
        return false;
    }

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0 || size > kMaxJsonFileBytes) {
        if (error) *error = "File is too large for a title template.";
        return false;
    }
    f.seekg(0, std::ios::beg);

    try {
        f >> out;
    } catch (const std::exception &e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

static void ensure_unique_title_id(const std::shared_ptr<Title> &title,
                                   std::unordered_set<std::string> &seen)
{
    if (!title)
        return;
    if (title->id.empty() || seen.find(title->id) != seen.end())
        title->id = TitleDataStore::make_uuid();
    seen.insert(title->id);

    /* Layer IDs are document-local persistent identities. Never rewrite a
     * non-empty ID during load/migration, even when damaged input contains a
     * duplicate. Reassigning it would silently break bindings, parenting,
     * masks and cached references. Only genuinely missing IDs are recovered. */
    std::unordered_set<std::string> layer_ids;
    for (const auto &layer : title->layers) {
        if (layer && !layer->id.empty())
            layer_ids.insert(layer->id);
    }
    for (auto &layer : title->layers) {
        if (!layer)
            continue;
        if (layer->id.empty()) {
            do {
                layer->id = TitleDataStore::make_uuid();
            } while (layer_ids.find(layer->id) != layer_ids.end());
        }
        layer_ids.insert(layer->id);
    }
    for (auto &layer : title->layers) {
        if (!layer)
            continue;
        if (!layer->parent_id.empty() && layer_ids.find(layer->parent_id) == layer_ids.end())
            layer->parent_id.clear();
        if (!layer->transform_parent_id.empty() &&
            layer_ids.find(layer->transform_parent_id) == layer_ids.end())
            layer->transform_parent_id.clear();
        if (!layer->mask_source_id.empty() && layer_ids.find(layer->mask_source_id) == layer_ids.end()) {
            layer->mask_source_id.clear();
            layer->mask_mode = MaskMode::None;
        }
    }
}
} // namespace

void publish_live_cue_runtime_state(const std::string &title_id,
                                    uintptr_t source_token, int row,
                                    double playhead, double elapsed_seconds,
                                    int64_t updated_ms)
{
    if (title_id.empty() || source_token == 0 || row < 0)
        return;

    std::lock_guard<std::mutex> lock(g_live_cue_runtime_mutex);
    LiveCueRuntimeSnapshot &state = g_live_cue_runtime_states[title_id];
    state.row = row;
    state.playhead = std::max(0.0, playhead);
    state.elapsed_seconds = std::max(0.0, elapsed_seconds);
    state.updated_ms = updated_ms;
    state.source_token = source_token;
    state.active = true;
}

void clear_live_cue_runtime_state(const std::string &title_id,
                                  uintptr_t source_token)
{
    if (title_id.empty() || source_token == 0)
        return;

    std::lock_guard<std::mutex> lock(g_live_cue_runtime_mutex);
    const auto it = g_live_cue_runtime_states.find(title_id);
    if (it != g_live_cue_runtime_states.end() &&
        it->second.source_token == source_token)
        g_live_cue_runtime_states.erase(it);
}

LiveCueRuntimeSnapshot live_cue_runtime_state(const std::string &title_id)
{
    if (title_id.empty())
        return {};

    std::lock_guard<std::mutex> lock(g_live_cue_runtime_mutex);
    const auto it = g_live_cue_runtime_states.find(title_id);
    return it != g_live_cue_runtime_states.end()
        ? it->second
        : LiveCueRuntimeSnapshot{};
}

static void set_argb_channels(AnimatedProperty &a, AnimatedProperty &r, AnimatedProperty &g, AnimatedProperty &b, uint32_t argb)
{
    a.static_value = (argb >> 24) & 0xFF;
    r.static_value = (argb >> 16) & 0xFF;
    g.static_value = (argb >> 8) & 0xFF;
    b.static_value = argb & 0xFF;
}

static void set_color_channels(Layer &l, bool text, uint32_t argb)
{
    AnimatedProperty &a = text ? l.text_color_a : l.fill_color_a;
    AnimatedProperty &r = text ? l.text_color_r : l.fill_color_r;
    AnimatedProperty &g = text ? l.text_color_g : l.fill_color_g;
    AnimatedProperty &b = text ? l.text_color_b : l.fill_color_b;
    set_argb_channels(a, r, g, b, argb);
}

static void set_stroke_color_channels(Layer &l, uint32_t argb)
{
    set_argb_channels(l.stroke_color_a, l.stroke_color_r,
                      l.stroke_color_g, l.stroke_color_b, argb);
}

static void set_background_color_channels(Layer &l, uint32_t argb)
{
    l.background_color_a.static_value = (argb >> 24) & 0xFF;
    l.background_color_r.static_value = (argb >> 16) & 0xFF;
    l.background_color_g.static_value = (argb >> 8) & 0xFF;
    l.background_color_b.static_value = argb & 0xFF;
}

/* ══════════════════════════════════════════════════════════════════
 *  UUID helper
 * ══════════════════════════════════════════════════════════════════ */
std::string TitleDataStore::make_uuid()
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t hi = dis(gen);
    uint64_t lo = dis(gen);
    // Set UUID version 4 bits
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (hi >> 32);
    ss << '-' << std::setw(4) << ((hi >> 16) & 0xFFFF);
    ss << '-' << std::setw(4) << (hi & 0xFFFF);
    ss << '-' << std::setw(4) << (lo >> 48);
    ss << '-' << std::setw(12) << (lo & 0x0000FFFFFFFFFFFFULL);
    return ss.str();
}

/* ══════════════════════════════════════════════════════════════════
 *  Title helpers
 * ══════════════════════════════════════════════════════════════════ */
std::shared_ptr<Layer> Title::find_layer(const std::string &lid) const
{
    for (auto &l : layers)
        if (l && l->id == lid) return l;
    return nullptr;
}

void Title::add_layer(std::shared_ptr<Layer> l)
{
    if (!l)
        return;
    layers.push_back(l);
    FXM_LOG_DEBUG("Layers", QStringLiteral(
        "Added layer title=%1 layer=%2 type=%3 count=%4")
        .arg(QString::fromStdString(id), QString::fromStdString(l->id))
        .arg(static_cast<int>(l->type))
        .arg(static_cast<int>(layers.size())));
}


void synchronize_video_audio_streams(Title &title,
                                     const std::string &video_layer_id)
{
    for (const auto &audio : title.layers) {
        if (!audio || audio->type != LayerType::Audio ||
            !audio->linked_media_stream || audio->linked_media_layer_id.empty())
            continue;
        if (!video_layer_id.empty() &&
            audio->linked_media_layer_id != video_layer_id)
            continue;
        const auto video = title.find_layer(audio->linked_media_layer_id);
        if (!video || video->type != LayerType::Video)
            continue;
        normalize_layer_media_range_to_timeline_span(*video, false);
        video->use_as_scene_mask = false;
        audio->parent_id = video->id;
        audio->audio_source = video->video_source;
        audio->audio_in_point = video->video_in_point;
        audio->audio_out_point = video->video_out_point;
        audio->audio_media_duration = video->video_media_duration;
        audio->audio_volume = video->audio_volume;
        audio->audio_pan = video->audio_pan;
        audio->audio_volume_prop = video->audio_volume_prop;
        audio->audio_pan_prop = video->audio_pan_prop;
        audio->audio_muted = video->audio_muted;
        audio->audio_solo = video->audio_solo;
        audio->audio_fade_in = video->audio_fade_in;
        audio->audio_fade_out = video->audio_fade_out;
        audio->audio_fade_curve = video->audio_fade_curve;
        audio->audio_effects = video->audio_effects;
        audio->audio_loop = video->video_loop || video->audio_loop ||
            video->audio_playback_mode == AudioPlaybackMode::Loop;
        audio->audio_playback_mode = audio->audio_loop
            ? AudioPlaybackMode::Loop : video->audio_playback_mode;
        audio->audio_independent = video->video_playback_mode == 1 || video->audio_independent;
        audio->in_time = video->in_time;
        audio->out_time = video->out_time;
    }
}
void Title::remove_layer(const std::string &lid)
{
    const auto protected_layer = find_layer(lid);
    if (protected_layer &&
        (stinger_transition_input_layer_is_protected(*protected_layer) ||
         protected_layer->linked_media_stream))
        return;
    const std::size_t previous_count = layers.size();
    const bool removing_video = protected_layer &&
        protected_layer->type == LayerType::Video;
    layers.erase(
        std::remove_if(layers.begin(), layers.end(),
                       [&](auto &l){
                           return !l || l->id == lid ||
                               (removing_video && l->type == LayerType::Audio &&
                                l->linked_media_stream &&
                                l->linked_media_layer_id == lid);
                       }),
        layers.end());
    for (auto &layer : layers) {
        if (!layer) continue;
        if (layer->parent_id == lid) layer->parent_id.clear();
        if (layer->transform_parent_id == lid) layer->transform_parent_id.clear();
        if (layer->mask_source_id == lid) {
            layer->mask_source_id.clear();
            layer->mask_mode = MaskMode::None;
        }
    }
    if (layers.size() != previous_count) {
        fxm::asset_runtime::reset_layer(id, lid);
        FXM_LOG_DEBUG("Layers", QStringLiteral(
            "Removed layer title=%1 layer=%2 count=%3")
            .arg(QString::fromStdString(id), QString::fromStdString(lid))
            .arg(static_cast<int>(layers.size())));
    }
}

void Title::move_layer(const std::string &lid, int delta)
{
    auto it = std::find_if(layers.begin(), layers.end(),
                           [&](auto &l){ return l && l->id == lid; });
    if (it == layers.end()) return;
    int idx = (int)(it - layers.begin());
    int dst = std::clamp(idx + delta, 0, (int)layers.size() - 1);
    if (idx == dst) return;
    auto layer = *it;
    layers.erase(it);
    layers.insert(layers.begin() + dst, layer);
}

/* ══════════════════════════════════════════════════════════════════
 *  TitleDataStore
 * ══════════════════════════════════════════════════════════════════ */
TitleDataStore &TitleDataStore::instance()
{
    static TitleDataStore inst;
    return inst;
}

std::shared_ptr<TitleDataStore::TitleMutex>
TitleDataStore::title_mutex_locked(const std::shared_ptr<Title> &title) const
{
    if (!title)
        return nullptr;
    auto &entry = title_mutexes_[title.get()];
    if (!entry)
        entry = std::make_shared<TitleMutex>();
    return entry;
}

std::vector<TitleDataStore::TitleAccessRecord>
TitleDataStore::title_access_records() const
{
    std::vector<TitleAccessRecord> records;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    records.reserve(titles_.size());
    for (const auto &title : titles_) {
        if (title)
            records.push_back({title, title_mutex_locked(title)});
    }
    return records;
}

std::vector<std::shared_ptr<Title>>
TitleDataStore::snapshot_authoritative_titles() const
{
    const auto records = title_access_records();
    std::vector<std::shared_ptr<Title>> snapshots;
    snapshots.reserve(records.size());
    for (const auto &record : records) {
        if (!record.title || !record.mutex)
            continue;
        /*
         * A caller commonly publishes while still holding its own edit lease.
         * Never wait for a different title here: two source/UI threads can
         * legitimately finish edits to different titles at the same time.
         * The busy title retains its previous committed publication and its
         * owning thread will publish the new state when that edit completes.
         */
        std::unique_lock<TitleMutex> title_lock(*record.mutex,
                                                std::try_to_lock);
        if (!title_lock.owns_lock())
            continue;
        snapshots.push_back(
            std::make_shared<Title>(clone_title_snapshot(*record.title)));
    }
    return snapshots;
}

void TitleDataStore::publish_title_snapshots(
    const std::vector<std::shared_ptr<Title>> &snapshots) const
{
    std::unordered_map<std::string, std::shared_ptr<const Title>> published_sources;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &[id, title] : published_titles_)
            if (title) published_sources[id] = title;
    }
    std::vector<std::shared_ptr<Title>> linked_snapshots = snapshots;
    fxm::linked_asset::refresh_graph(
        linked_snapshots,
        [&published_sources](const std::string &id) -> std::shared_ptr<const Title> {
            const auto source = published_sources.find(id);
            return source != published_sources.end() ? source->second : nullptr;
        },
        []() { return TitleDataStore::make_uuid(); });

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::unordered_set<std::string> live_ids;
    live_ids.reserve(titles_.size());
    for (const auto &title : titles_) {
        if (title)
            live_ids.insert(title->id);
    }
    for (auto it = published_titles_.begin(); it != published_titles_.end();) {
        if (live_ids.find(it->first) == live_ids.end())
            it = published_titles_.erase(it);
        else
            ++it;
    }
    for (const auto &snapshot : linked_snapshots) {
        if (!snapshot || live_ids.find(snapshot->id) == live_ids.end())
            continue;
        const auto current = std::find_if(
            titles_.begin(), titles_.end(),
            [&](const std::shared_ptr<Title> &title) {
                return title && title->id == snapshot->id;
            });
        if (current != titles_.end())
            published_titles_[snapshot->id] = snapshot;
    }
}

void TitleDataStore::clear_title_publications_locked()
{
    published_titles_.clear();
    title_mutexes_.clear();
}

std::vector<std::shared_ptr<Title>> TitleDataStore::titles() const
{
    const auto records = title_access_records();
    std::vector<std::shared_ptr<Title>> result;
    result.reserve(records.size());
    for (const auto &record : records)
        result.push_back(record.title);
    return result;
}

std::vector<std::shared_ptr<Title>> TitleDataStore::title_snapshots() const
{
    std::vector<std::shared_ptr<Title>> snapshots;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    snapshots.reserve(titles_.size());
    for (const auto &title : titles_) {
        if (!title)
            continue;
        const auto published = published_titles_.find(title->id);
        if (published != published_titles_.end() && published->second)
            snapshots.push_back(published->second);
    }
    return snapshots;
}

uint64_t TitleDataStore::on_change(ChangeCallback cb)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const uint64_t id = next_change_cb_id_++;
    change_cbs_.push_back(ChangeObserver {id, std::move(cb)});
    return id;
}

void TitleDataStore::remove_change_callback(uint64_t callback_id)
{
    if (callback_id == 0) return;

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = std::remove_if(change_cbs_.begin(), change_cbs_.end(),
                             [callback_id](const ChangeObserver &observer) {
                                 return observer.id == callback_id;
                             });
    change_cbs_.erase(it, change_cbs_.end());
}

void TitleDataStore::notify_change()
{
    const auto updated_snapshots = snapshot_authoritative_titles();
    publish_title_snapshots(updated_snapshots);
    revision_.fetch_add(1, std::memory_order_release);
    const auto title_snapshot = title_snapshots();

    std::vector<ChangeCallback> callbacks;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        callbacks.reserve(change_cbs_.size());
        for (const auto &observer : change_cbs_)
            callbacks.push_back(observer.callback);
    }

    std::vector<ExternalDataSourceDefinition> provider_definitions;
    for (const auto &title : title_snapshot) {
        if (!title)
            continue;
        ExternalDataManager::instance().register_title_sources(*title);
        provider_definitions.insert(provider_definitions.end(),
                                    title->external_data_sources.begin(),
                                    title->external_data_sources.end());
    }
    ExternalDataProviderService::instance().synchronize(provider_definitions);
    for (auto &cb : callbacks) cb();
}

void TitleDataStore::touch_runtime_change()
{
    const auto snapshots = snapshot_authoritative_titles();
    publish_title_snapshots(snapshots);
    revision_.fetch_add(1, std::memory_order_release);
}

void ensure_live_text_row_ids(Title &title)
{
    if (title.live_text_row_ids.size() > title.live_text_rows.size())
        title.live_text_row_ids.resize(title.live_text_rows.size());

    std::set<std::string> used;
    for (size_t i = 0; i < title.live_text_rows.size(); ++i) {
        if (i >= title.live_text_row_ids.size())
            title.live_text_row_ids.push_back({});
        std::string &id = title.live_text_row_ids[i];
        if (id.empty() || used.count(id))
            id = TitleDataStore::make_uuid();
        used.insert(id);
    }
}

std::string live_text_row_id(const Title &title, int row)
{
    if (row < 0 || row >= static_cast<int>(title.live_text_rows.size()))
        return {};
    if (row < static_cast<int>(title.live_text_row_ids.size()) &&
        !title.live_text_row_ids[static_cast<size_t>(row)].empty())
        return title.live_text_row_ids[static_cast<size_t>(row)];
    return std::string("legacy-row-") + std::to_string(row);
}

static bool live_text_layer_supports_fill_cue(const Layer &layer)
{
    return (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
            layer.type == LayerType::Ticker || layer.type == LayerType::SolidRect ||
            layer.type == LayerType::Shape || layer.type == LayerType::ColorSolid ||
            layer.type == LayerType::TransitionInput) && !layer.use_as_scene_mask;
}

static bool live_text_layer_is_text_like(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock ||
           layer.type == LayerType::Ticker;
}

static uint32_t live_text_layer_authored_fill_color(const Layer &layer)
{
    return live_text_layer_is_text_like(layer) ? layer.text_color : layer.fill_color;
}

void prune_live_text_cue_style_overrides(Title &title)
{
    ensure_live_text_row_ids(title);
    std::set<std::string> valid_rows(title.live_text_row_ids.begin(), title.live_text_row_ids.end());
    std::set<std::string> valid_layers;
    for (const auto &layer : title.layers) {
        if (!layer)
            continue;
        if (layer->expose_stroke_color ||
            (layer->expose_fill_color && live_text_layer_supports_fill_cue(*layer)))
            valid_layers.insert(layer->id);
    }
    title.live_text_cue_style_overrides.erase(
        std::remove_if(title.live_text_cue_style_overrides.begin(),
                       title.live_text_cue_style_overrides.end(),
                       [&](const LiveTextCueStyleOverride &entry) {
                           return entry.row_id.empty() || entry.layer_id.empty() ||
                                  valid_rows.find(entry.row_id) == valid_rows.end() ||
                                  valid_layers.find(entry.layer_id) == valid_layers.end() ||
                                  (!entry.fill_color_set && !entry.stroke_color_set);
                       }),
        title.live_text_cue_style_overrides.end());
}

static std::string live_text_cue_style_row_id(const Title &title, const Layer &layer, int row, bool stroke)
{
    const bool single_value = stroke ? layer.exposed_stroke_single_value
                                     : layer.exposed_fill_single_value;
    return live_text_row_id(title, single_value ? 0 : row);
}

static LiveTextCueStyleOverride *mutable_live_text_cue_style_entry(
    Title &title, const std::string &row_id, const std::string &layer_id, bool create)
{
    if (row_id.empty() || layer_id.empty())
        return nullptr;
    auto it = std::find_if(title.live_text_cue_style_overrides.begin(),
                           title.live_text_cue_style_overrides.end(),
                           [&](const LiveTextCueStyleOverride &entry) {
                               return entry.row_id == row_id && entry.layer_id == layer_id;
                           });
    if (it != title.live_text_cue_style_overrides.end())
        return &*it;
    if (!create)
        return nullptr;
    LiveTextCueStyleOverride entry;
    entry.row_id = row_id;
    entry.layer_id = layer_id;
    title.live_text_cue_style_overrides.push_back(std::move(entry));
    return &title.live_text_cue_style_overrides.back();
}

static const LiveTextCueStyleOverride *live_text_cue_style_entry(
    const Title &title, const std::string &row_id, const std::string &layer_id)
{
    if (row_id.empty() || layer_id.empty())
        return nullptr;
    auto it = std::find_if(title.live_text_cue_style_overrides.begin(),
                           title.live_text_cue_style_overrides.end(),
                           [&](const LiveTextCueStyleOverride &entry) {
                               return entry.row_id == row_id && entry.layer_id == layer_id;
                           });
    return it == title.live_text_cue_style_overrides.end() ? nullptr : &*it;
}

bool live_text_cue_color_override(const Title &title, const Layer &layer, int row, bool stroke, uint32_t *out_argb)
{
    if (stroke && !layer.expose_stroke_color)
        return false;
    if (!stroke && (!layer.expose_fill_color || !live_text_layer_supports_fill_cue(layer)))
        return false;
    const std::string row_id = live_text_cue_style_row_id(title, layer, row, stroke);
    const auto *entry = live_text_cue_style_entry(title, row_id, layer.id);
    if (!entry)
        return false;
    if (stroke) {
        if (!entry->stroke_color_set)
            return false;
        if (out_argb) *out_argb = entry->stroke_color;
        return true;
    }
    if (!entry->fill_color_set)
        return false;
    if (out_argb) *out_argb = entry->fill_color;
    return true;
}

uint32_t live_text_cue_effective_color(const Title &title, const Layer &layer, int row, bool stroke)
{
    uint32_t argb = 0;
    if (live_text_cue_color_override(title, layer, row, stroke, &argb))
        return argb;
    return stroke ? layer.stroke_color : live_text_layer_authored_fill_color(layer);
}

void set_live_text_cue_color_override(Title &title, const Layer &layer, int row, bool stroke, uint32_t argb)
{
    ensure_live_text_row_ids(title);
    const std::string row_id = live_text_cue_style_row_id(title, layer, row, stroke);
    auto *entry = mutable_live_text_cue_style_entry(title, row_id, layer.id, true);
    if (!entry)
        return;
    if (stroke) {
        entry->stroke_color_set = true;
        entry->stroke_color = argb;
    } else {
        entry->fill_color_set = true;
        entry->fill_color = argb;
    }
    prune_live_text_cue_style_overrides(title);
}

void clear_live_text_cue_color_override(Title &title, const Layer &layer, int row, bool stroke)
{
    ensure_live_text_row_ids(title);
    const std::string row_id = live_text_cue_style_row_id(title, layer, row, stroke);
    auto *entry = mutable_live_text_cue_style_entry(title, row_id, layer.id, false);
    if (!entry)
        return;
    if (stroke)
        entry->stroke_color_set = false;
    else
        entry->fill_color_set = false;
    prune_live_text_cue_style_overrides(title);
}

void apply_live_text_cue_style_to_layer(Title &title, Layer &layer, int row)
{
    uint32_t argb = 0;
    if (layer.expose_fill_color && live_text_layer_supports_fill_cue(layer) &&
        live_text_cue_color_override(title, layer, row, false, &argb)) {
        if (live_text_layer_is_text_like(layer)) {
            layer.text_color = argb;
            set_color_channels(layer, true, argb);
        } else {
            layer.fill_color = argb;
            set_color_channels(layer, false, argb);
        }
    }
    if (layer.expose_stroke_color && live_text_cue_color_override(title, layer, row, true, &argb)) {
        layer.stroke_color = argb;
        set_stroke_color_channels(layer, argb);
    }
}

double stinger_transition_point_seconds(const Title &title)
{
    return std::clamp(title.stinger_transition_point, 0.0,
                      std::max(0.0, title.duration));
}

void set_stinger_transition_point_seconds(Title &title, double seconds)
{
    title.stinger_transition_point = std::clamp(
        std::isfinite(seconds) ? seconds : 0.0, 0.0,
        std::max(0.0, title.duration));
}

bool stinger_transition_input_layer_is_protected(const Layer &layer)
{
    return layer.type == LayerType::TransitionInput &&
           layer.transition_input_required &&
           (layer.transition_input_slot == 0 || layer.transition_input_slot == 1);
}

std::shared_ptr<Layer> stinger_transition_input_layer(const Title &title, int slot)
{
    std::shared_ptr<Layer> fallback;
    for (const auto &layer : title.layers) {
        if (!layer || layer->type != LayerType::TransitionInput ||
            layer->transition_input_slot != slot)
            continue;
        if (layer->transition_input_required)
            return layer;
        if (!fallback)
            fallback = layer;
    }
    return fallback;
}

static void set_stinger_transition_input_default_surface(
    Layer &layer, const Title &title, int slot)
{
    const double canvas_width = static_cast<double>(std::max(1, title.width));
    const double canvas_height = static_cast<double>(std::max(1, title.height));
    layer.name = slot == 0 ? "Scene A" : "Scene B";
    layer.type = LayerType::TransitionInput;
    layer.transition_input_slot = slot;
    layer.transition_input_required = true;
    layer.visible = true;
    layer.locked = false;
    layer.in_time = 0.0;
    layer.out_time = std::max(0.001, title.duration);
    layer.position.static_value = {canvas_width * 0.5, canvas_height * 0.5};
    layer.position.keyframes.clear();
    layer.scale.static_value = {1.0, 1.0, 1.0};
    layer.scale.keyframes.clear();
    layer.lock_aspect_ratio = false;
    layer.shape_type = ShapeType::Rectangle;
    layer.rotation.static_value = 0.0;
    layer.rotation.keyframes.clear();
    layer.opacity.static_value = 1.0;
    layer.opacity.keyframes.clear();
    layer.origin_prop.static_value = {0.5, 0.5, 0.0};
    layer.origin_prop.keyframes.clear();
    layer.origin_x = 0.5f;
    layer.origin_y = 0.5f;
    layer.rect_width = static_cast<float>(canvas_width);
    layer.rect_height = static_cast<float>(canvas_height);
    layer.size.static_value = {canvas_width, canvas_height};
    layer.size.keyframes.clear();
    layer.image_width = static_cast<float>(canvas_width);
    layer.image_height = static_cast<float>(canvas_height);
    layer.image_size.static_value = {canvas_width, canvas_height};
    layer.image_size.keyframes.clear();
    layer.parent_id.clear();
    layer.transform_parent_id.clear();
}

static bool stinger_transition_input_has_legacy_point_opacity(
    const Layer &layer, int slot, double point)
{
    const double start_value = slot == 0 ? 1.0 : 0.0;
    const double end_value = slot == 0 ? 0.0 : 1.0;
    const auto near = [](double a, double b) { return std::abs(a - b) <= 1.0e-6; };
    if (point <= 1.0e-9) {
        return layer.opacity.keyframes.size() == 1 &&
               near(layer.opacity.keyframes[0].time, 0.0) &&
               near(layer.opacity.keyframes[0].value, end_value);
    }
    return layer.opacity.keyframes.size() == 2 &&
           near(layer.opacity.keyframes[0].time, 0.0) &&
           near(layer.opacity.keyframes[0].value, start_value) &&
           near(layer.opacity.keyframes[1].time, point) &&
           near(layer.opacity.keyframes[1].value, end_value);
}

static std::shared_ptr<Layer> make_stinger_transition_input_layer(
    const Title &title, int slot)
{
    auto layer = std::make_shared<Layer>();
    layer->id = TitleDataStore::make_uuid();
    set_stinger_transition_input_default_surface(*layer, title, slot);
    return layer;
}

void ensure_stinger_transition_input_layers(Title &title)
{
    /* A/B are ordinary visual layers. This function only guarantees that one
     * required input exists for each runtime scene slot. It must never reset
     * authored timing, transforms, effects, transitions, hierarchy or names. */
    std::shared_ptr<Layer> required_inputs[2];
    std::shared_ptr<Layer> first_inputs[2];

    for (const auto &layer : title.layers) {
        if (!layer || layer->type != LayerType::TransitionInput)
            continue;
        const int input_index = layer->transition_input_slot;
        if (input_index < 0 || input_index > 1)
            continue;
        if (!first_inputs[input_index])
            first_inputs[input_index] = layer;
        if (layer->transition_input_required) {
            if (!required_inputs[input_index])
                required_inputs[input_index] = layer;
            else
                layer->transition_input_required = false;
        }
    }

    for (int input_index = 0; input_index < 2; ++input_index) {
        if (!required_inputs[input_index] && first_inputs[input_index]) {
            required_inputs[input_index] = first_inputs[input_index];
            required_inputs[input_index]->transition_input_required = true;
        }
        if (!required_inputs[input_index]) {
            required_inputs[input_index] =
                make_stinger_transition_input_layer(title, input_index);
        }
    }

    /* Upgrade only the exact obsolete automatic point-cut opacity curves.
     * Preserve every other authored property, including timing and geometry. */
    const double point = stinger_transition_point_seconds(title);
    const bool legacy_generated_pair =
        stinger_transition_input_has_legacy_point_opacity(
            *required_inputs[0], 0, point) &&
        stinger_transition_input_has_legacy_point_opacity(
            *required_inputs[1], 1, point);
    if (legacy_generated_pair) {
        required_inputs[0]->opacity.static_value = 1.0;
        required_inputs[0]->opacity.keyframes.clear();
        required_inputs[1]->opacity.static_value = 1.0;
        required_inputs[1]->opacity.keyframes.clear();
    }

    /* Insert only newly-created required inputs. Model order is bottom-to-top,
     * so Scene B starts below Scene A exactly like two overlapping image/shape
     * layers. Existing user ordering is never rewritten. */
    const auto contains = [&](const std::shared_ptr<Layer> &candidate) {
        return std::find(title.layers.begin(), title.layers.end(), candidate) !=
               title.layers.end();
    };
    if (!contains(required_inputs[1]))
        title.layers.insert(title.layers.begin(), required_inputs[1]);
    if (!contains(required_inputs[0])) {
        auto scene_b = std::find(title.layers.begin(), title.layers.end(),
                                 required_inputs[1]);
        title.layers.insert(scene_b == title.layers.end()
                                ? title.layers.begin()
                                : std::next(scene_b),
                            required_inputs[0]);
    }

    for (int input_index = 0; input_index < 2; ++input_index) {
        auto &input_layer = required_inputs[input_index];
        input_layer->type = LayerType::TransitionInput;
        input_layer->transition_input_slot = input_index;
        input_layer->transition_input_required = true;
        if (input_layer->name.empty())
            input_layer->name = input_index == 0 ? "Scene A" : "Scene B";
    }
}

StingerValidationResult validate_stinger_title(const Title &title)
{
    StingerValidationResult result;
    if (title.graphic_type != TitleGraphicType::Stinger)
        return result;

    if (!std::isfinite(title.duration) || title.duration <= 0.0)
        result.errors.push_back("Stinger duration must be greater than zero.");
    if (!std::isfinite(title.stinger_transition_point) ||
        title.stinger_transition_point < 0.0 ||
        title.stinger_transition_point > title.duration)
        result.errors.push_back("Transition point must be inside the Stinger duration.");
    if (title.width <= 0 || title.height <= 0 ||
        title.width > kMaxCanvasDimension || title.height > kMaxCanvasDimension)
        result.errors.push_back("Stinger output size is invalid.");
    if (!std::isfinite(title.stinger_pre_roll) || title.stinger_pre_roll < 0.0 ||
        !std::isfinite(title.stinger_post_roll) || title.stinger_post_roll < 0.0)
        result.errors.push_back("Pre-roll and post-roll must be zero or greater.");

    const int render_mode = static_cast<int>(title.stinger_render_mode);
    if (render_mode < static_cast<int>(StingerRenderMode::ProceduralLive) ||
        render_mode > static_cast<int>(StingerRenderMode::PrerenderedProxy))
        result.errors.push_back("Stinger render mode is invalid.");

    const int switch_mode = static_cast<int>(title.stinger_switch_mode);
    if (switch_mode < static_cast<int>(StingerSwitchMode::SwitchAtPoint) ||
        switch_mode > static_cast<int>(StingerSwitchMode::ManualSceneAnimation))
        result.errors.push_back("Stinger switch mode is invalid.");
    if (title.stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation) {
        const auto scene_a = stinger_transition_input_layer(title, 0);
        const auto scene_b = stinger_transition_input_layer(title, 1);
        if (!scene_a || !scene_b)
            result.errors.push_back("Manual scene animation requires protected Scene A and Scene B layers.");
        if (title.stinger_render_mode == StingerRenderMode::PrerenderedProxy)
            result.warnings.push_back("Manual Scene A/B composition is runtime-dynamic; the full document renders live instead of using a full-frame proxy.");
    }

    bool has_audio = false;
    bool has_live_content = title.external_data_enabled ||
                            !title.external_data_sources.empty();
    for (const auto &layer : title.layers) {
        if (!layer)
            continue;
        has_audio = has_audio || layer->type == LayerType::Audio;
        has_live_content = has_live_content || layer->type == LayerType::Ticker ||
                           layer->type == LayerType::Clock ||
                           layer->type == LayerType::Chart ||
                           !layer->external_bindings.empty();
    }

    if (title.stinger_audio_enabled && !has_audio)
        result.warnings.push_back("Optional Stinger audio is enabled, but the document has no audio layer.");
    if (title.stinger_alpha_output && ((title.bg_color >> 24) & 0xFFu) == 0xFFu)
        result.warnings.push_back("Alpha output is enabled while the document background is fully opaque.");
    if (has_live_content) {
        if (title.stinger_render_mode == StingerRenderMode::PrerenderedProxy)
            result.warnings.push_back("Ticker, clock, or external data cannot be reliably prerendered and will use live rendering during the OBS transition.");
        else
            result.warnings.push_back("This Stinger contains ticker, clock, or external live data and will render live during the OBS transition.");
    }
    return result;
}


static std::string unique_title_name_locked(
    const std::vector<std::shared_ptr<Title>> &titles,
    const std::string &requested,
    const std::string &exclude_id = {})
{
    const std::string base = requested.empty() ? std::string("New Title") : requested;
    auto exists = [&](const std::string &candidate) {
        return std::any_of(titles.begin(), titles.end(), [&](const auto &title) {
            return title && title->id != exclude_id && title->name == candidate;
        });
    };
    if (!exists(base))
        return base;
    for (int suffix = 1; ; ++suffix) {
        char number[16];
        snprintf(number, sizeof(number), "%02d", suffix);
        const std::string candidate = base + " " + number;
        if (!exists(candidate))
            return candidate;
    }
}

static std::vector<std::shared_ptr<Title>> ordered_published_titles_locked(
    const std::vector<std::shared_ptr<Title>> &titles,
    const std::unordered_map<std::string, std::shared_ptr<Title>> &published)
{
    std::vector<std::shared_ptr<Title>> result;
    result.reserve(titles.size());
    for (const auto &title : titles) {
        if (!title)
            continue;
        const auto it = published.find(title->id);
        if (it != published.end() && it->second)
            result.push_back(it->second);
    }
    return result;
}

std::shared_ptr<Title> TitleDataStore::create_title(const std::string &name)
{
    auto t = std::make_shared<Title>();
    t->id = make_uuid();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        t->name = unique_title_name_locked(
            ordered_published_titles_locked(titles_, published_titles_), name);
    }
    t->creation_date = current_iso_utc_string();

    /* Default: one text layer */
    auto layer = std::make_shared<Layer>();
    layer->id   = make_uuid();
    layer->name = "Title Text";
    layer->type = LayerType::Text;
    layer->position.static_value.x = 960.0;
    layer->position.static_value.y = 540.0;
    layer->rect_width = 960.0f;
    layer->rect_height = 160.0f;
    layer->size.static_value.x = layer->rect_width;
    layer->size.static_value.y = layer->rect_height;
    set_color_channels(*layer, true, layer->text_color);
    set_color_channels(*layer, false, layer->fill_color);
    set_stroke_color_channels(*layer, layer->stroke_color);
    layer->text_content = t->name;
    layer->rich_text = rich_text_document_from_layer_defaults(*layer);
    layer->expose_text = true;
    t->layers.push_back(layer);

    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        titles_.push_back(t);
        title_mutex_locked(t);
        published_titles_[t->id] =
            std::make_shared<Title>(clone_title_snapshot(*t));
    }
    notify_change();
    return get_title(t->id);
}

std::shared_ptr<Title> TitleDataStore::get_title(const std::string &id) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto &title : titles_) {
        if (title && title->id == id)
            return title;
    }
    return nullptr;
}

std::shared_ptr<Title>
TitleDataStore::get_title_snapshot(const std::string &id) const
{
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const auto published = published_titles_.find(id);
        if (published != published_titles_.end())
            return published->second;
    }

    TitleAccessRecord record;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &title : titles_) {
            if (title && title->id == id) {
                record = {title, title_mutex_locked(title)};
                break;
            }
        }
    }
    if (!record.title || !record.mutex)
        return nullptr;

    std::shared_ptr<Title> snapshot;
    {
        std::lock_guard<TitleMutex> title_lock(*record.mutex);
        snapshot = std::make_shared<Title>(
            clone_title_snapshot(*record.title));
    }
    std::vector<std::shared_ptr<Title>> linked_snapshot{snapshot};
    std::unordered_map<std::string, std::shared_ptr<const Title>> published_sources;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &[source_id, source] : published_titles_)
            if (source) published_sources[source_id] = source;
    }
    fxm::linked_asset::refresh_graph(
        linked_snapshot,
        [&published_sources](const std::string &source_id) -> std::shared_ptr<const Title> {
            const auto source = published_sources.find(source_id);
            return source != published_sources.end() ? source->second : nullptr;
        },
        []() { return TitleDataStore::make_uuid(); });
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        const auto current = std::find_if(
            titles_.begin(), titles_.end(),
            [&](const std::shared_ptr<Title> &title) {
                return title == record.title;
            });
        if (current != titles_.end())
            published_titles_[id] = snapshot;
    }
    return snapshot;
}

void TitleDataStore::delete_title(const std::string &id)
{
    bool deleted = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &title : titles_) {
            if (title && title->id == id)
                title_mutexes_.erase(title.get());
        }
        const auto old_size = titles_.size();
        titles_.erase(
            std::remove_if(titles_.begin(), titles_.end(),
                           [&](auto &t){ return t && t->id == id; }),
            titles_.end());
        deleted = titles_.size() != old_size;
        if (deleted)
            published_titles_.erase(id);
    }

    if (deleted) {
        fxm::ticker_runtime::clear_title(id);
        fxm::asset_runtime::clear_title(id);
        notify_change();
    }
}

void TitleDataStore::rename_title(const std::string &id, const std::string &n)
{
    auto title = get_title(id);
    if (!title)
        return;

    std::string unique_name;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        unique_name = unique_title_name_locked(
            ordered_published_titles_locked(titles_, published_titles_), n, id);
    }

    title->name = std::move(unique_name);
    notify_change();
}

/* ── persistence ──────────────────────────────────────────────────── */
std::string TitleDataStore::data_path()
{
    const std::string dir = title_data_dir() + "/scene-collection-titles";
    QDir().mkpath(QString::fromStdString(dir));
    const std::string collection_name = current_scene_collection_name();
    return dir + "/" + safe_scene_collection_file_stem(collection_name) + ".json";
}

/* ---- JSON serialisation helpers (flat, no macros) ---- */
static json keyframe_to_json(const Keyframe &k)
{
    return {
        {"time",   k.time},
        {"value",  k.value},
        {"easing", (int)k.easing},
        {"cx1",    k.cx1}, {"cy1", k.cy1},
        {"cx2",    k.cx2}, {"cy2", k.cy2},
        {"temporal_mode", (int)k.temporal_mode},
        {"temporal_in_influence", k.incoming_influence},
        {"temporal_out_influence", k.outgoing_influence},
        {"temporal_in_speed", k.incoming_speed},
        {"temporal_out_speed", k.outgoing_speed},
        {"temporal_tangents_linked", k.temporal_tangents_linked},
        {"temporal_velocity_explicit", k.temporal_velocity_explicit},
    };
}

static Keyframe keyframe_from_json(const json &j)
{
    Keyframe k;
    if (!j.is_object())
        return k;
    k.time = std::clamp(finite_or(json_double(j, "time", 0.0), 0.0), 0.0, kMaxDuration);
    k.value = std::clamp(finite_or(json_double(j, "value", 0.0), 0.0), -kMaxPropertyValue, kMaxPropertyValue);
    k.easing = (EasingType)std::clamp(json_int(j, "easing", 0), 0, (int)EasingType::Hold);
    k.cx1 = std::clamp(finite_or(json_double(j, "cx1", 0.333), 0.333), 0.0, 1.0);
    k.cy1 = std::clamp(finite_or(json_double(j, "cy1", 0.0), 0.0), 0.0, 1.0);
    k.cx2 = std::clamp(finite_or(json_double(j, "cx2", 0.667), 0.667), 0.0, 1.0);
    k.cy2 = std::clamp(finite_or(json_double(j, "cy2", 1.0), 1.0), 0.0, 1.0);
    k.temporal_mode = (TemporalInterpolationMode)std::clamp(
        json_int(j, "temporal_mode", (int)TemporalInterpolationMode::AutoBezier),
        (int)TemporalInterpolationMode::Linear,
        (int)TemporalInterpolationMode::ManualBezier);
    k.incoming_influence = std::clamp(finite_or(
        json_double(j, "temporal_in_influence", 33.3333333333), 33.3333333333), 0.0, 100.0);
    k.outgoing_influence = std::clamp(finite_or(
        json_double(j, "temporal_out_influence", 33.3333333333), 33.3333333333), 0.0, 100.0);
    k.incoming_speed = std::clamp(finite_or(json_double(j, "temporal_in_speed", 0.0), 0.0),
                                    -kMaxPropertyValue, kMaxPropertyValue);
    k.outgoing_speed = std::clamp(finite_or(json_double(j, "temporal_out_speed", 0.0), 0.0),
                                    -kMaxPropertyValue, kMaxPropertyValue);
    k.temporal_tangents_linked = json_bool(j, "temporal_tangents_linked", true);
    k.temporal_velocity_explicit = json_bool(j, "temporal_velocity_explicit", false);
    return k;
}

static json aprop_to_json(const AnimatedProperty &p)
{
    json j = { {"static_value", p.static_value} };
    json kf = json::array();
    for (auto &k : p.keyframes) kf.push_back(keyframe_to_json(k));
    j["keyframes"] = kf;
    return j;
}

static AnimatedProperty aprop_from_json(const json &j, const std::string &name)
{
    AnimatedProperty p;
    p.name = name;
    if (!j.is_object())
        return p;

    p.static_value = std::clamp(finite_or(json_double(j, "static_value", 0.0), 0.0),
                                -kMaxPropertyValue, kMaxPropertyValue);
    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        const size_t count = std::min(j["keyframes"].size(), kMaxKeyframesPerProperty);
        p.keyframes.reserve(count);
        for (size_t i = 0; i < count; ++i)
            p.keyframes.push_back(keyframe_from_json(j["keyframes"][i]));
        std::sort(p.keyframes.begin(), p.keyframes.end(),
                  [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
    }
    return p;
}


static json discrete_property_to_json(const AnimatedDiscreteProperty &property)
{
    json result = {{"static_value", property.static_value}};
    json keys = json::array();
    for (const DiscreteKeyframe &keyframe : property.keyframes) {
        keys.push_back({{"time", keyframe.time}, {"value", keyframe.value}});
    }
    result["keyframes"] = std::move(keys);
    return result;
}

static AnimatedDiscreteProperty discrete_property_from_json(
    const json &value, const std::string &name, const std::string &fallback)
{
    AnimatedDiscreteProperty property{name, fallback};
    if (!value.is_object())
        return property;
    property.static_value = bounded_string(value, "static_value", fallback, kMaxNameLength);
    if (value.contains("keyframes") && value["keyframes"].is_array()) {
        const size_t count = std::min(value["keyframes"].size(), kMaxKeyframesPerProperty);
        property.keyframes.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            const json &entry = value["keyframes"][index];
            if (!entry.is_object()) continue;
            DiscreteKeyframe keyframe;
            keyframe.time = std::clamp(
                finite_or(json_double(entry, "time", 0.0), 0.0),
                0.0, kMaxDuration);
            keyframe.value = bounded_string(entry, "value", fallback, kMaxNameLength);
            property.keyframes.push_back(std::move(keyframe));
        }
        property.sort_keyframes();
    }
    return property;
}



static json text_animator_property_to_json(const TextAnimatorProperty &property)
{
    return {{"id", property.id}, {"name", property.name},
            {"type", (int)property.type}, {"enabled", property.enabled},
            {"value", aprop_to_json(property.value)},
            {"secondary", aprop_to_json(property.secondary)},
            {"tertiary", aprop_to_json(property.tertiary)},
            {"quaternary", aprop_to_json(property.quaternary)}};
}

static TextAnimatorProperty text_animator_property_from_json(const json &j,
                                                             size_t ordinal)
{
    TextAnimatorProperty property;
    if (!j.is_object()) return property;
    property.type = (TextAnimatorPropertyType)std::clamp(
        json_int(j, "type", (int)TextAnimatorPropertyType::Opacity),
        (int)TextAnimatorPropertyType::Position,
        (int)TextAnimatorPropertyType::ScrambleAmount);
    property.id = bounded_string(j, "id", "", kMaxNameLength);
    property.name = bounded_string(j, "name", "Property", kMaxNameLength);
    if (property.id.empty())
        property.id = make_text_animator_id("property", property.name, ordinal);
    property.enabled = json_bool(j, "enabled", true);
    if (j.contains("value")) property.value = aprop_from_json(j["value"], property.name + ".value");
    if (j.contains("secondary")) property.secondary = aprop_from_json(j["secondary"], property.name + ".secondary");
    if (j.contains("tertiary")) property.tertiary = aprop_from_json(j["tertiary"], property.name + ".tertiary");
    if (j.contains("quaternary")) property.quaternary = aprop_from_json(j["quaternary"], property.name + ".quaternary");
    return property;
}

static json tagged_ranges_to_json(const std::vector<std::pair<size_t, size_t>> &ranges)
{
    json result = json::array();
    for (const auto &range : ranges)
        result.push_back({{"start", range.first}, {"length", range.second}});
    return result;
}

static json text_selector_to_json(const TextSelector &selector)
{
    return {{"id", selector.id}, {"name", selector.name},
            {"type", (int)selector.type}, {"combination", (int)selector.combination},
            {"based_on", (int)selector.based_on}, {"enabled", selector.enabled},
            {"expanded", selector.expanded}, {"range_units", (int)selector.range_units},
            {"range_shape", (int)selector.range_shape},
            {"start", aprop_to_json(selector.start)}, {"end", aprop_to_json(selector.end)},
            {"offset", aprop_to_json(selector.offset)}, {"amount", aprop_to_json(selector.amount)},
            {"ease_high", aprop_to_json(selector.ease_high)}, {"ease_low", aprop_to_json(selector.ease_low)},
            {"smoothness", aprop_to_json(selector.smoothness)},
            {"completion", aprop_to_json(selector.completion)},
            {"stagger_percent", aprop_to_json(selector.stagger_percent)},
            {"unit_easing", (int)selector.unit_easing},
            {"stagger_mode", (int)selector.stagger_mode},
            {"exclude_whitespace", selector.exclude_whitespace},
            {"randomize_order", selector.randomize_order}, {"random_seed", selector.random_seed},
            {"invert", selector.invert}, {"procedural_mode", (int)selector.procedural_mode},
            {"amplitude", aprop_to_json(selector.amplitude)},
            {"frequency", aprop_to_json(selector.frequency)}, {"phase", aprop_to_json(selector.phase)},
            {"speed", aprop_to_json(selector.speed)}, {"falloff", aprop_to_json(selector.falloff)},
            {"minimum", aprop_to_json(selector.minimum)}, {"maximum", aprop_to_json(selector.maximum)},
            {"custom_index", aprop_to_json(selector.custom_index)}, {"direction", (int)selector.direction},
            {"match_mode", (int)selector.match_mode}, {"range_start", selector.range_start},
            {"range_end", selector.range_end}, {"match_text", selector.match_text},
            {"regular_expression", selector.regular_expression}, {"case_sensitive", selector.case_sensitive},
            {"rich_text_run_index", selector.rich_text_run_index},
            {"tagged_byte_ranges", tagged_ranges_to_json(selector.tagged_byte_ranges)},
            {"wiggly_amount", aprop_to_json(selector.wiggly_amount)},
            {"wiggly_frequency", aprop_to_json(selector.wiggly_frequency)},
            {"correlation", aprop_to_json(selector.correlation)},
            {"temporal_phase", aprop_to_json(selector.temporal_phase)},
            {"spatial_phase", aprop_to_json(selector.spatial_phase)},
            {"minimum_influence", aprop_to_json(selector.minimum_influence)},
            {"maximum_influence", aprop_to_json(selector.maximum_influence)},
            {"wiggly_seed", selector.wiggly_seed}, {"lock_dimensions", selector.lock_dimensions},
            {"per_character_random", selector.per_character_random}};
}

static TextSelector text_selector_from_json(const json &j, size_t ordinal)
{
    TextSelector selector;
    if (!j.is_object()) return selector;
    selector.id = bounded_string(j, "id", "", kMaxNameLength);
    selector.name = bounded_string(j, "name", "Selector", kMaxNameLength);
    if (selector.id.empty()) selector.id = make_text_animator_id("selector", selector.name, ordinal);
    selector.type = (TextSelectorType)std::clamp(json_int(j, "type", 0),
        (int)TextSelectorType::Range, (int)TextSelectorType::Staggered);
    selector.combination = (TextSelectorCombinationMode)std::clamp(json_int(j, "combination", 2),
        (int)TextSelectorCombinationMode::Add, (int)TextSelectorCombinationMode::Multiply);
    selector.based_on = (TextAnimatorUnit)std::clamp(json_int(j, "based_on", 0),
        (int)TextAnimatorUnit::Grapheme, (int)TextAnimatorUnit::Sentence);
    selector.enabled = json_bool(j, "enabled", true);
    selector.expanded = json_bool(j, "expanded", true);
    selector.range_units = (TextRangeUnits)std::clamp(json_int(j, "range_units", 0), 0, 1);
    selector.range_shape = (TextRangeShape)std::clamp(json_int(j, "range_shape", 0), 0, 5);
    auto load_prop = [&](const char *key, AnimatedProperty &property) {
        if (j.contains(key)) property = aprop_from_json(j[key], key);
    };
    load_prop("start", selector.start); load_prop("end", selector.end);
    load_prop("offset", selector.offset); load_prop("amount", selector.amount);
    load_prop("ease_high", selector.ease_high); load_prop("ease_low", selector.ease_low);
    load_prop("smoothness", selector.smoothness);
    load_prop("completion", selector.completion);
    load_prop("stagger_percent", selector.stagger_percent);
    selector.unit_easing = (EasingType)std::clamp(
        json_int(j, "unit_easing", (int)EasingType::EaseInOut),
        (int)EasingType::Linear, (int)EasingType::Hold);
    selector.stagger_mode = (TextStaggerMode)std::clamp(
        json_int(j, "stagger_mode", (int)TextStaggerMode::Entrance), 0, 1);
    selector.exclude_whitespace = json_bool(j, "exclude_whitespace", true);
    selector.randomize_order = json_bool(j, "randomize_order", false);
    selector.random_seed = std::clamp(json_int(j, "random_seed", 1), 0, 1000000000);
    selector.invert = json_bool(j, "invert", false);
    selector.procedural_mode = (TextProceduralMode)std::clamp(json_int(j, "procedural_mode", 0), 0, 10);
    load_prop("amplitude", selector.amplitude); load_prop("frequency", selector.frequency);
    load_prop("phase", selector.phase); load_prop("speed", selector.speed);
    load_prop("falloff", selector.falloff); load_prop("minimum", selector.minimum);
    load_prop("maximum", selector.maximum); load_prop("custom_index", selector.custom_index);
    selector.direction = (TextSelectorDirection)std::clamp(json_int(j, "direction", 0), 0, 2);
    selector.match_mode = (TextMatchMode)std::clamp(
        json_int(j, "match_mode", (int)TextMatchMode::ExactText),
        (int)TextMatchMode::CharacterRange,
        (int)TextMatchMode::ChangedText);
    selector.range_start = (size_t)std::max(0, json_int(j, "range_start", 0));
    selector.range_end = (size_t)std::max(0, json_int(j, "range_end", 0));
    selector.match_text = bounded_string(j, "match_text", "", kMaxTextLength);
    selector.regular_expression = bounded_string(j, "regular_expression", "", 4096);
    selector.case_sensitive = json_bool(j, "case_sensitive", true);
    selector.rich_text_run_index = std::clamp(json_int(j, "rich_text_run_index", -1), -1, 65535);
    selector.tagged_byte_ranges.clear();
    if (j.contains("tagged_byte_ranges") && j["tagged_byte_ranges"].is_array()) {
        const size_t count = std::min(j["tagged_byte_ranges"].size(), (size_t)4096);
        for (size_t i = 0; i < count; ++i) {
            const auto &item = j["tagged_byte_ranges"][i];
            if (!item.is_object()) continue;
            const size_t start = (size_t)std::max(0, json_int(item, "start", 0));
            const size_t length = (size_t)std::max(0, json_int(item, "length", 0));
            selector.tagged_byte_ranges.emplace_back(start, length);
        }
    }
    load_prop("wiggly_amount", selector.wiggly_amount);
    load_prop("wiggly_frequency", selector.wiggly_frequency);
    load_prop("correlation", selector.correlation);
    load_prop("temporal_phase", selector.temporal_phase);
    load_prop("spatial_phase", selector.spatial_phase);
    load_prop("minimum_influence", selector.minimum_influence);
    load_prop("maximum_influence", selector.maximum_influence);
    selector.wiggly_seed = std::clamp(json_int(j, "wiggly_seed", 1), 0, 1000000000);
    selector.lock_dimensions = json_bool(j, "lock_dimensions", true);
    selector.per_character_random = json_bool(j, "per_character_random", true);
    return selector;
}

static json text_animator_stack_to_json(const TextAnimatorStack &stack)
{
    json animators = json::array();
    for (const TextAnimator &animator : stack.animators) {
        json properties = json::array();
        for (const auto &property : animator.properties)
            properties.push_back(text_animator_property_to_json(property));
        json selectors = json::array();
        for (const auto &selector : animator.selectors)
            selectors.push_back(text_selector_to_json(selector));
        animators.push_back({{"id", animator.id}, {"name", animator.name},
            {"enabled", animator.enabled}, {"expanded", animator.expanded},
            {"blend_mode", (int)animator.blend_mode}, {"granularity", (int)animator.granularity},
            {"transform_as_unit", animator.transform_as_unit},
            {"clip_to_unit_bounds", animator.clip_to_unit_bounds},
            {"change_behaviour", (int)animator.change_behaviour},
            {"playback_role", (int)animator.playback_role},
            {"local_time_offset", animator.local_time_offset},
            {"preset_id", animator.preset_id},
            {"preset_schema_version", animator.preset_schema_version},
            {"transition_managed", animator.transition_managed},
            {"transition_id", animator.transition_id},
            {"transition_binding_signature", animator.transition_binding_signature},
            {"properties", std::move(properties)}, {"selectors", std::move(selectors)}});
    }
    return {{"schema_version", stack.schema_version},
            {"legacy_migration_version", stack.legacy_migration_version},
            {"animators", std::move(animators)}};
}

static TextAnimatorStack text_animator_stack_from_json(const json &j)
{
    TextAnimatorStack stack;
    if (!j.is_object()) return stack;
    stack.schema_version = std::clamp(json_int(j, "schema_version", 1), 1, 64);
    stack.legacy_migration_version = std::clamp(json_int(j, "legacy_migration_version", 0), 0, 64);
    stack.animators.clear();
    if (!j.contains("animators") || !j["animators"].is_array()) return stack;
    const size_t animator_count = std::min(j["animators"].size(), (size_t)64);
    for (size_t i = 0; i < animator_count; ++i) {
        const auto &item = j["animators"][i];
        if (!item.is_object()) continue;
        TextAnimator animator;
        animator.id = bounded_string(item, "id", "", kMaxNameLength);
        animator.name = bounded_string(item, "name", "Animator", kMaxNameLength);
        if (animator.id.empty()) animator.id = make_text_animator_id("animator", animator.name, i);
        animator.enabled = json_bool(item, "enabled", true);
        animator.expanded = json_bool(item, "expanded", true);
        animator.blend_mode = (TextAnimatorBlendMode)std::clamp(json_int(item, "blend_mode", 0), 0, 2);
        animator.granularity = (TextAnimatorUnit)std::clamp(json_int(item, "granularity", 0), 0, (int)TextAnimatorUnit::Sentence);
        animator.transform_as_unit = json_bool(item, "transform_as_unit", false);
        animator.clip_to_unit_bounds = json_bool(item, "clip_to_unit_bounds", false);
        animator.change_behaviour = (TextChangeBehaviour)std::clamp(json_int(item, "change_behaviour", 6), 0, 6);
        animator.playback_role = (TextAnimatorPlaybackRole)std::clamp(json_int(item, "playback_role", 0), 0, 3);
        animator.local_time_offset = std::clamp(finite_or(json_double(item, "local_time_offset", 0.0), 0.0),
                                                  -kMaxDuration, kMaxDuration);
        animator.preset_id = bounded_string(item, "preset_id", "", kMaxNameLength);
        animator.preset_schema_version = std::clamp(json_int(item, "preset_schema_version", 0), 0, 64);
        animator.transition_managed = json_bool(item, "transition_managed", false);
        animator.transition_id = bounded_string(item, "transition_id", "", kMaxNameLength);
        animator.transition_binding_signature = item.contains("transition_binding_signature") &&
                item["transition_binding_signature"].is_number_unsigned()
            ? item["transition_binding_signature"].get<uint64_t>() : 0;
        if (item.contains("properties") && item["properties"].is_array()) {
            const size_t count = std::min(item["properties"].size(), (size_t)64);
            for (size_t pi = 0; pi < count; ++pi)
                animator.properties.push_back(text_animator_property_from_json(item["properties"][pi], pi));
        }
        if (item.contains("selectors") && item["selectors"].is_array()) {
            const size_t count = std::min(item["selectors"].size(), (size_t)64);
            for (size_t si = 0; si < count; ++si)
                animator.selectors.push_back(text_selector_from_json(item["selectors"][si], si));
        }
        stack.animators.push_back(std::move(animator));
    }
    return stack;
}

static json vec2_aprop_to_json(const AnimatedVec2Property &p)
{
    json j;
    j["static_value"] = {{"x", p.static_value.x}, {"y", p.static_value.y},
                         {"z", p.static_value.z}};
    json kf = json::array();
    for (const auto &k : p.keyframes) {
        kf.push_back({{"time", k.time},
                      {"value", {{"x", k.value.x}, {"y", k.value.y}, {"z", k.value.z}}},
                      {"easing", (int)k.easing},
                      {"cx1", k.cx1}, {"cy1", k.cy1},
                      {"cx2", k.cx2}, {"cy2", k.cy2},
                      {"temporal_mode", (int)k.temporal_mode},
                      {"temporal_in_influence", k.incoming_influence},
                      {"temporal_out_influence", k.outgoing_influence},
                      {"temporal_in_speed", k.incoming_speed},
                      {"temporal_out_speed", k.outgoing_speed},
                      {"temporal_tangents_linked", k.temporal_tangents_linked},
                      {"temporal_velocity_explicit", k.temporal_velocity_explicit},
                      {"spatial_in_tangent", {{"x", k.incoming_tangent.x},
                                               {"y", k.incoming_tangent.y},
                                               {"z", k.incoming_tangent.z}}},
                      {"spatial_out_tangent", {{"x", k.outgoing_tangent.x},
                                                {"y", k.outgoing_tangent.y},
                                                {"z", k.outgoing_tangent.z}}},
                      {"spatial_mode", (int)k.spatial_mode},
                      {"spatial_tangents_linked", k.spatial_tangents_linked},
                      {"rove_across_time", k.rove_across_time}});
    }
    j["keyframes"] = kf;
    return j;
}

static void vec2_aprop_from_json(const json &j, AnimatedVec2Property &p)
{
    if (!j.is_object())
        return;
    if (j.contains("static_value") && j["static_value"].is_object()) {
        p.static_value.x = std::clamp(finite_or(json_double(j["static_value"], "x", p.static_value.x), p.static_value.x),
                                      -kMaxPropertyValue, kMaxPropertyValue);
        p.static_value.y = std::clamp(finite_or(json_double(j["static_value"], "y", p.static_value.y), p.static_value.y),
                                      -kMaxPropertyValue, kMaxPropertyValue);
        /* Pre-212 vector2d payloads have no z member.  The field-specific
         * constructor default (0 for position/size, 1 for scale) preserves the
         * historical 2D result while allowing the same track to carry XYZ. */
        p.static_value.z = std::clamp(finite_or(json_double(j["static_value"], "z", p.static_value.z), p.static_value.z),
                                      -kMaxPropertyValue, kMaxPropertyValue);
    }
    if (j.contains("keyframes") && j["keyframes"].is_array()) {
        const size_t count = std::min(j["keyframes"].size(), kMaxKeyframesPerProperty);
        p.keyframes.clear();
        p.keyframes.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto &item = j["keyframes"][i];
            if (!item.is_object()) continue;
            VectorKeyframe k;
            k.time = std::clamp(finite_or(json_double(item, "time", 0.0), 0.0), 0.0, kMaxDuration);
            if (item.contains("value") && item["value"].is_object()) {
                k.value.x = std::clamp(finite_or(json_double(item["value"], "x", 0.0), 0.0),
                                       -kMaxPropertyValue, kMaxPropertyValue);
                k.value.y = std::clamp(finite_or(json_double(item["value"], "y", 0.0), 0.0),
                                       -kMaxPropertyValue, kMaxPropertyValue);
                k.value.z = std::clamp(finite_or(json_double(item["value"], "z", p.static_value.z), p.static_value.z),
                                       -kMaxPropertyValue, kMaxPropertyValue);
            }
            k.easing = (EasingType)std::clamp(json_int(item, "easing", 0), 0, (int)EasingType::Hold);
            k.cx1 = std::clamp(finite_or(json_double(item, "cx1", 0.333), 0.333), 0.0, 1.0);
            k.cy1 = std::clamp(finite_or(json_double(item, "cy1", 0.0), 0.0), 0.0, 1.0);
            k.cx2 = std::clamp(finite_or(json_double(item, "cx2", 0.667), 0.667), 0.0, 1.0);
            k.cy2 = std::clamp(finite_or(json_double(item, "cy2", 1.0), 1.0), 0.0, 1.0);
            k.temporal_mode = (TemporalInterpolationMode)std::clamp(
                json_int(item, "temporal_mode", (int)TemporalInterpolationMode::AutoBezier),
                (int)TemporalInterpolationMode::Linear,
                (int)TemporalInterpolationMode::ManualBezier);
            k.incoming_influence = std::clamp(finite_or(
                json_double(item, "temporal_in_influence", 33.3333333333), 33.3333333333), 0.0, 100.0);
            k.outgoing_influence = std::clamp(finite_or(
                json_double(item, "temporal_out_influence", 33.3333333333), 33.3333333333), 0.0, 100.0);
            k.incoming_speed = std::clamp(
                finite_or(json_double(item, "temporal_in_speed", 0.0), 0.0),
                -kMaxPropertyValue, kMaxPropertyValue);
            k.outgoing_speed = std::clamp(
                finite_or(json_double(item, "temporal_out_speed", 0.0), 0.0),
                -kMaxPropertyValue, kMaxPropertyValue);
            k.temporal_tangents_linked = json_bool(item, "temporal_tangents_linked", true);
            k.temporal_velocity_explicit = json_bool(item, "temporal_velocity_explicit", false);

            auto read_spatial_tangent = [&](const char *name, Vec2Value &tangent) {
                if (!item.contains(name) || !item[name].is_object())
                    return;
                tangent.x = std::clamp(
                    finite_or(json_double(item[name], "x", 0.0), 0.0),
                    -kMaxPropertyValue, kMaxPropertyValue);
                tangent.y = std::clamp(
                    finite_or(json_double(item[name], "y", 0.0), 0.0),
                    -kMaxPropertyValue, kMaxPropertyValue);
                tangent.z = std::clamp(
                    finite_or(json_double(item[name], "z", 0.0), 0.0),
                    -kMaxPropertyValue, kMaxPropertyValue);
            };
            read_spatial_tangent("spatial_in_tangent", k.incoming_tangent);
            read_spatial_tangent("spatial_out_tangent", k.outgoing_tangent);

            /* Missing spatial fields identify legacy files. Their historical
             * position interpolation was straight-line and remains so. */
            k.spatial_mode = (SpatialInterpolationMode)std::clamp(
                json_int(item, "spatial_mode", (int)SpatialInterpolationMode::Linear),
                (int)SpatialInterpolationMode::Linear,
                (int)SpatialInterpolationMode::ManualBezier);
            k.spatial_tangents_linked = json_bool(
                item, "spatial_tangents_linked", true);
            k.rove_across_time = json_bool(item, "rove_across_time", false);
            p.keyframes.push_back(k);
        }
        std::sort(p.keyframes.begin(), p.keyframes.end(),
                  [](const VectorKeyframe &a, const VectorKeyframe &b) { return a.time < b.time; });
        p.recalculate_rove_times();
    }
}

static bool vector_payload_has_z(const json &j)
{
    if (!j.is_object()) return false;
    if (j.contains("static_value") && j["static_value"].is_object() &&
        j["static_value"].contains("z"))
        return true;
    if (!j.contains("keyframes") || !j["keyframes"].is_array())
        return false;
    for (const auto &item : j["keyframes"]) {
        if (item.is_object() && item.contains("value") &&
            item["value"].is_object() && item["value"].contains("z"))
            return true;
    }
    return false;
}

static void copy_scalar_temporal_metadata(const Keyframe &source,
                                          VectorKeyframe &destination)
{
    destination.easing = source.easing;
    destination.cx1 = source.cx1; destination.cy1 = source.cy1;
    destination.cx2 = source.cx2; destination.cy2 = source.cy2;
    destination.temporal_mode = source.temporal_mode;
    destination.incoming_influence = source.incoming_influence;
    destination.outgoing_influence = source.outgoing_influence;
    destination.incoming_speed = source.incoming_speed;
    destination.outgoing_speed = source.outgoing_speed;
    destination.temporal_tangents_linked = source.temporal_tangents_linked;
    destination.temporal_velocity_explicit = source.temporal_velocity_explicit;
}

/* Development Version 212 migration: older 3D layers stored Z in an adjacent
 * scalar track. Promote the union of XY and Z key times into the unified XYZ
 * vector while retaining the scalar as a compatibility mirror. 2D documents
 * simply receive their constructor-default Z and remain on the affine path. */
static void promote_legacy_scalar_z_track(AnimatedVec2Property &vector,
                                          const AnimatedProperty &legacy_z)
{
    const AnimatedVec2Property previous = vector;
    vector.static_value.z = legacy_z.static_value;

    std::vector<double> times;
    times.reserve(previous.keyframes.size() + legacy_z.keyframes.size());
    for (const VectorKeyframe &key : previous.keyframes) times.push_back(key.time);
    for (const Keyframe &key : legacy_z.keyframes) times.push_back(key.time);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-9;
    }), times.end());

    std::vector<VectorKeyframe> promoted;
    promoted.reserve(times.size());
    for (double time : times) {
        VectorKeyframe key;
        key.time = time;
        key.value = previous.evaluate(time);
        key.value.z = legacy_z.evaluate(time);

        const auto vector_key = std::find_if(
            previous.keyframes.begin(), previous.keyframes.end(),
            [time](const VectorKeyframe &candidate) {
                return std::abs(candidate.time - time) <= 1.0e-9;
            });
        if (vector_key != previous.keyframes.end()) {
            key = *vector_key;
            key.value.z = legacy_z.evaluate(time);
        } else {
            const auto z_key = std::find_if(
                legacy_z.keyframes.begin(), legacy_z.keyframes.end(),
                [time](const Keyframe &candidate) {
                    return std::abs(candidate.time - time) <= 1.0e-9;
                });
            if (z_key != legacy_z.keyframes.end())
                copy_scalar_temporal_metadata(*z_key, key);
        }
        promoted.push_back(key);
    }
    vector.keyframes = std::move(promoted);
    vector.recalculate_rove_times();
}

static json vec3_aprop_to_json(const AnimatedVec3Property &p)
{
    json j;
    j["static_value"] = {{"x", p.static_value.x}, {"y", p.static_value.y},
                         {"z", p.static_value.z}};
    json kf = json::array();
    for (const Vector3Keyframe &k : p.keyframes) {
        kf.push_back({
            {"time", k.time},
            {"value", {{"x", k.value.x}, {"y", k.value.y}, {"z", k.value.z}}},
            {"easing", (int)k.easing},
            {"cx1", k.cx1}, {"cy1", k.cy1},
            {"cx2", k.cx2}, {"cy2", k.cy2},
            {"temporal_mode", (int)k.temporal_mode},
            {"temporal_in_influence", k.incoming_influence},
            {"temporal_out_influence", k.outgoing_influence},
            {"temporal_in_speed", k.incoming_speed},
            {"temporal_out_speed", k.outgoing_speed},
            {"temporal_tangents_linked", k.temporal_tangents_linked},
            {"temporal_velocity_explicit", k.temporal_velocity_explicit},
            {"spatial_in_tangent", {{"x", k.incoming_tangent.x},
                                    {"y", k.incoming_tangent.y},
                                    {"z", k.incoming_tangent.z}}},
            {"spatial_out_tangent", {{"x", k.outgoing_tangent.x},
                                     {"y", k.outgoing_tangent.y},
                                     {"z", k.outgoing_tangent.z}}},
            {"spatial_mode", (int)k.spatial_mode},
            {"spatial_tangents_linked", k.spatial_tangents_linked},
            {"rove_across_time", k.rove_across_time}
        });
    }
    j["keyframes"] = std::move(kf);
    return j;
}

static void vec3_aprop_from_json(const json &j, AnimatedVec3Property &p)
{
    if (!j.is_object()) return;
    auto clamp_component = [](double value) {
        return std::clamp(finite_or(value, 0.0),
                          -kMaxPropertyValue, kMaxPropertyValue);
    };
    if (j.contains("static_value") && j["static_value"].is_object()) {
        const json &value = j["static_value"];
        p.static_value.x = clamp_component(json_double(value, "x", p.static_value.x));
        p.static_value.y = clamp_component(json_double(value, "y", p.static_value.y));
        p.static_value.z = clamp_component(json_double(value, "z", p.static_value.z));
    }
    if (!j.contains("keyframes") || !j["keyframes"].is_array()) return;

    const size_t count = std::min(j["keyframes"].size(), kMaxKeyframesPerProperty);
    p.keyframes.clear();
    p.keyframes.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const json &item = j["keyframes"][i];
        if (!item.is_object()) continue;
        Vector3Keyframe k;
        k.time = std::clamp(finite_or(json_double(item, "time", 0.0), 0.0),
                            0.0, kMaxDuration);
        if (item.contains("value") && item["value"].is_object()) {
            const json &value = item["value"];
            k.value.x = clamp_component(json_double(value, "x", 0.0));
            k.value.y = clamp_component(json_double(value, "y", 0.0));
            k.value.z = clamp_component(json_double(value, "z", 0.0));
        }
        k.easing = (EasingType)std::clamp(
            json_int(item, "easing", 0), 0, (int)EasingType::Hold);
        k.cx1 = std::clamp(finite_or(json_double(item, "cx1", 0.333), 0.333), 0.0, 1.0);
        k.cy1 = std::clamp(finite_or(json_double(item, "cy1", 0.0), 0.0), 0.0, 1.0);
        k.cx2 = std::clamp(finite_or(json_double(item, "cx2", 0.667), 0.667), 0.0, 1.0);
        k.cy2 = std::clamp(finite_or(json_double(item, "cy2", 1.0), 1.0), 0.0, 1.0);
        k.temporal_mode = (TemporalInterpolationMode)std::clamp(
            json_int(item, "temporal_mode", (int)TemporalInterpolationMode::AutoBezier),
            (int)TemporalInterpolationMode::Linear,
            (int)TemporalInterpolationMode::ManualBezier);
        k.incoming_influence = std::clamp(finite_or(
            json_double(item, "temporal_in_influence", 33.3333333333),
            33.3333333333), 0.0, 100.0);
        k.outgoing_influence = std::clamp(finite_or(
            json_double(item, "temporal_out_influence", 33.3333333333),
            33.3333333333), 0.0, 100.0);
        k.incoming_speed = clamp_component(
            json_double(item, "temporal_in_speed", 0.0));
        k.outgoing_speed = clamp_component(
            json_double(item, "temporal_out_speed", 0.0));
        k.temporal_tangents_linked = json_bool(
            item, "temporal_tangents_linked", true);
        k.temporal_velocity_explicit = json_bool(
            item, "temporal_velocity_explicit", false);

        auto read_tangent = [&](const char *name, Vec3Value &tangent) {
            if (!item.contains(name) || !item[name].is_object()) return;
            const json &value = item[name];
            tangent.x = clamp_component(json_double(value, "x", 0.0));
            tangent.y = clamp_component(json_double(value, "y", 0.0));
            tangent.z = clamp_component(json_double(value, "z", 0.0));
        };
        read_tangent("spatial_in_tangent", k.incoming_tangent);
        read_tangent("spatial_out_tangent", k.outgoing_tangent);
        k.spatial_mode = (SpatialInterpolationMode)std::clamp(
            json_int(item, "spatial_mode", (int)SpatialInterpolationMode::Linear),
            (int)SpatialInterpolationMode::Linear,
            (int)SpatialInterpolationMode::ManualBezier);
        k.spatial_tangents_linked = json_bool(
            item, "spatial_tangents_linked", true);
        k.rove_across_time = json_bool(item, "rove_across_time", false);
        p.keyframes.push_back(k);
    }
    std::sort(p.keyframes.begin(), p.keyframes.end(),
              [](const Vector3Keyframe &a, const Vector3Keyframe &b) {
                  return a.time < b.time;
              });
    p.recalculate_rove_times();
}



static json gradient_stops_to_json(const std::vector<GradientStop> &stops)
{
    json arr = json::array();
    for (const auto &stop : stops) {
        arr.push_back({{"color", stop.color},
                       {"position", std::clamp((double)stop.position, 0.0, 1.0)},
                       {"opacity", std::clamp((double)stop.opacity, 0.0, 1.0)}});
    }
    return arr;
}

static std::vector<GradientStop> gradient_stops_from_json(const json &j)
{
    std::vector<GradientStop> stops;
    if (!j.is_array()) return stops;
    for (const auto &item : j) {
        if (!item.is_object()) continue;
        GradientStop stop;
        stop.color = json_color(item, "color", (uint32_t)0xFFFFFFFF);
        stop.position = (float)std::clamp(finite_or(json_double(item, "position", 0.5), 0.5), 0.0, 1.0);
        stop.opacity = (float)std::clamp(finite_or(json_double(item, "opacity", 1.0), 1.0), 0.0, 1.0);
        stops.push_back(stop);
    }
    std::sort(stops.begin(), stops.end(), [](const GradientStop &a, const GradientStop &b) {
        return a.position < b.position;
    });
    return stops;
}

static json bezier_path_points_to_json(const std::vector<BezierPathPoint> &points)
{
    json arr = json::array();
    for (const auto &point : points) {
        arr.push_back({{"x", point.x}, {"y", point.y},
                       {"in_x", point.in_x}, {"in_y", point.in_y},
                       {"out_x", point.out_x}, {"out_y", point.out_y},
                       {"has_in", point.has_in}, {"has_out", point.has_out},
                       {"smooth", point.smooth}, {"starts_subpath", point.starts_subpath},
                       {"corner_radius", point.corner_radius}});
    }
    return arr;
}

static std::vector<BezierPathPoint> bezier_path_points_from_json(const json &j)
{
    constexpr size_t kMaxPathPoints = 4096;
    constexpr double kMaxNormalizedCoordinate = 1024.0;
    std::vector<BezierPathPoint> points;
    if (!j.is_array()) return points;
    points.reserve(std::min(j.size(), kMaxPathPoints));
    for (size_t i = 0; i < j.size() && points.size() < kMaxPathPoints; ++i) {
        const auto &item = j[i];
        if (!item.is_object()) continue;
        BezierPathPoint point;
        point.x = std::clamp(finite_or(json_double(item, "x", 0.0), 0.0),
                             -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.y = std::clamp(finite_or(json_double(item, "y", 0.0), 0.0),
                             -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.in_x = std::clamp(finite_or(json_double(item, "in_x", point.x), point.x),
                                -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.in_y = std::clamp(finite_or(json_double(item, "in_y", point.y), point.y),
                                -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.out_x = std::clamp(finite_or(json_double(item, "out_x", point.x), point.x),
                                 -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.out_y = std::clamp(finite_or(json_double(item, "out_y", point.y), point.y),
                                 -kMaxNormalizedCoordinate, kMaxNormalizedCoordinate);
        point.has_in = json_bool(item, "has_in", false);
        point.has_out = json_bool(item, "has_out", false);
        point.smooth = json_bool(item, "smooth", false);
        point.starts_subpath = !points.empty() && json_bool(item, "starts_subpath", false);
        point.corner_radius = std::clamp(finite_or(json_double(item, "corner_radius", 0.0), 0.0),
                                         0.0, (double)kMaxCanvasDimension);
        if (!point.has_in) { point.in_x = point.x; point.in_y = point.y; }
        if (!point.has_out) { point.out_x = point.x; point.out_y = point.y; }
        points.push_back(point);
    }
    return points;
}

static json rich_fill_to_json(const RichTextFill &f)
{
    return {{"type", f.type}, {"color", f.color}, {"gradient_type", f.gradient_type},
            {"gradient_spread", f.gradient_spread},
            {"gradient_start_color", f.gradient_start_color}, {"gradient_end_color", f.gradient_end_color},
            {"gradient_start_pos", f.gradient_start_pos}, {"gradient_end_pos", f.gradient_end_pos},
            {"gradient_start_opacity", f.gradient_start_opacity}, {"gradient_end_opacity", f.gradient_end_opacity},
            {"gradient_opacity", f.gradient_opacity}, {"gradient_angle", f.gradient_angle},
            {"gradient_center_x", f.gradient_center_x}, {"gradient_center_y", f.gradient_center_y},
            {"gradient_scale", f.gradient_scale}, {"gradient_focal_x", f.gradient_focal_x},
            {"gradient_focal_y", f.gradient_focal_y},
            {"gradient_stops", gradient_stops_to_json(f.gradient_stops)}};
}

static RichTextFill rich_fill_from_json(const json &j, const RichTextFill &fallback = {})
{
    RichTextFill f = fallback;
    if (!j.is_object()) return f;
    f.type = std::clamp(json_int(j, "type", f.type), 0, 1);
    f.color = json_color(j, "color", f.color);
    f.gradient_spread = gradient_spread_from_json(j, "gradient_spread", "gradient_type", f.gradient_spread);
    f.gradient_type = normalize_gradient_type(json_int(j, "gradient_type", f.gradient_type));
    f.gradient_start_color = json_color(j, "gradient_start_color", f.gradient_start_color);
    f.gradient_end_color = json_color(j, "gradient_end_color", f.gradient_end_color);
    f.gradient_start_pos = (float)std::clamp(finite_or(json_double(j, "gradient_start_pos", f.gradient_start_pos), f.gradient_start_pos), 0.0, 1.0);
    f.gradient_end_pos = (float)std::clamp(finite_or(json_double(j, "gradient_end_pos", f.gradient_end_pos), f.gradient_end_pos), 0.0, 1.0);
    f.gradient_start_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_start_opacity", f.gradient_start_opacity), f.gradient_start_opacity), 0.0, 1.0);
    f.gradient_end_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_end_opacity", f.gradient_end_opacity), f.gradient_end_opacity), 0.0, 1.0);
    f.gradient_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_opacity", f.gradient_opacity), f.gradient_opacity), 0.0, 1.0);
    f.gradient_angle = (float)finite_or(json_double(j, "gradient_angle", f.gradient_angle), f.gradient_angle);
    f.gradient_center_x = (float)std::clamp(finite_or(json_double(j, "gradient_center_x", f.gradient_center_x), f.gradient_center_x), -100.0, 100.0);
    f.gradient_center_y = (float)std::clamp(finite_or(json_double(j, "gradient_center_y", f.gradient_center_y), f.gradient_center_y), -100.0, 100.0);
    f.gradient_scale = (float)std::clamp(finite_or(json_double(j, "gradient_scale", f.gradient_scale), f.gradient_scale), 0.01, 100.0);
    f.gradient_focal_x = (float)std::clamp(finite_or(json_double(j, "gradient_focal_x", f.gradient_focal_x), f.gradient_focal_x), -100.0, 100.0);
    f.gradient_focal_y = (float)std::clamp(finite_or(json_double(j, "gradient_focal_y", f.gradient_focal_y), f.gradient_focal_y), -100.0, 100.0);
    if (j.contains("gradient_stops"))
        f.gradient_stops = gradient_stops_from_json(j["gradient_stops"]);
    return f;
}

static json rich_stroke_to_json(const RichTextStroke &stroke)
{
    return {{"enabled", stroke.enabled}, {"width", stroke.width},
            {"opacity", stroke.opacity}, {"on_front", stroke.on_front},
            {"alignment", stroke.alignment}, {"antialias", stroke.antialias},
            {"join_style", stroke.join_style}, {"fill", rich_fill_to_json(stroke.fill)}};
}

static RichTextStroke rich_stroke_from_json(const json &j, const RichTextStroke &fallback = {})
{
    RichTextStroke stroke = fallback;
    if (!j.is_object()) return stroke;
    stroke.enabled = json_bool(j, "enabled", stroke.enabled);
    stroke.width = (float)std::clamp(finite_or(json_double(j, "width", stroke.width), stroke.width), 0.0, 200.0);
    stroke.opacity = (float)std::clamp(finite_or(json_double(j, "opacity", stroke.opacity), stroke.opacity), 0.0, 1.0);
    stroke.on_front = json_bool(j, "on_front", stroke.on_front);
    stroke.alignment = std::clamp(json_int(j, "alignment", stroke.alignment), 0, 2);
    stroke.antialias = json_bool(j, "antialias", stroke.antialias);
    stroke.join_style = std::clamp(json_int(j, "join_style", stroke.join_style), 0, 2);
    if (j.contains("fill")) stroke.fill = rich_fill_from_json(j["fill"], stroke.fill);
    return stroke;
}

static json rich_char_format_to_json(const RichTextCharFormat &f)
{
    return {{"font_family", f.font_family}, {"font_style", f.font_style},
            {"font_size", f.font_size}, {"bold", f.bold}, {"italic", f.italic},
            {"underline", f.underline}, {"strikethrough", f.strikethrough},
            {"kerning", f.kerning}, {"kerning_mode", f.kerning_mode},
            {"manual_kerning", f.manual_kerning}, {"tracking", f.tracking},
            {"scale_x", f.scale_x}, {"scale_y", f.scale_y},
            {"baseline_shift", f.baseline_shift}, {"text_style", f.text_style},
            {"ligatures", f.ligatures}, {"stylistic_alternates", f.stylistic_alternates},
            {"fractions", f.fractions}, {"opentype_features", f.opentype_features},
            {"language", f.language}, {"fill", rich_fill_to_json(f.fill)},
            {"stroke", rich_stroke_to_json(f.stroke)}};
}

static RichTextCharFormat rich_char_format_from_json(const json &j, const RichTextCharFormat &fallback = {})
{
    RichTextCharFormat f = fallback;
    if (!j.is_object()) return f;
    f.font_family = bounded_string(j, "font_family", f.font_family, kMaxNameLength);
    f.font_style = bounded_string(j, "font_style", f.font_style, kMaxNameLength);
    f.font_size = std::clamp(json_int(j, "font_size", f.font_size), 1, 512);
    f.bold = json_bool(j, "bold", f.bold);
    f.italic = json_bool(j, "italic", f.italic);
    f.underline = json_bool(j, "underline", f.underline);
    f.strikethrough = json_bool(j, "strikethrough", f.strikethrough);
    f.kerning = json_bool(j, "kerning", f.kerning);
    f.kerning_mode = std::clamp(json_int(j, "kerning_mode", f.kerning_mode), 0, 2);
    f.manual_kerning = (float)std::clamp(finite_or(json_double(j, "manual_kerning", f.manual_kerning), f.manual_kerning), -1000.0, 1000.0);
    f.tracking = (float)std::clamp(finite_or(json_double(j, "tracking", f.tracking), f.tracking), -1000.0, 1000.0);
    f.scale_x = (float)std::clamp(finite_or(json_double(j, "scale_x", f.scale_x), f.scale_x), 0.01, 100.0);
    f.scale_y = (float)std::clamp(finite_or(json_double(j, "scale_y", f.scale_y), f.scale_y), 0.01, 100.0);
    f.baseline_shift = (float)std::clamp(finite_or(json_double(j, "baseline_shift", f.baseline_shift), f.baseline_shift), -1000.0, 1000.0);
    f.text_style = std::clamp(json_int(j, "text_style", f.text_style), 0, 4);
    f.ligatures = json_bool(j, "ligatures", f.ligatures);
    f.stylistic_alternates = json_bool(j, "stylistic_alternates", f.stylistic_alternates);
    f.fractions = json_bool(j, "fractions", f.fractions);
    f.opentype_features = json_bool(j, "opentype_features", f.opentype_features);
    f.language = bounded_string(j, "language", f.language, kMaxNameLength);
    if (j.contains("fill")) f.fill = rich_fill_from_json(j["fill"], f.fill);
    if (j.contains("stroke")) f.stroke = rich_stroke_from_json(j["stroke"], f.stroke);
    return f;
}

static json rich_paragraph_format_to_json(const RichTextParagraphFormat &f)
{
    return {{"align_h", f.align_h}, {"align_v", f.align_v}, {"indent_left", f.indent_left},
            {"indent_right", f.indent_right}, {"indent_first_line", f.indent_first_line},
            {"line_spacing", f.line_spacing},
            {"space_before", f.space_before}, {"space_after", f.space_after}, {"hyphenate", f.hyphenate}};
}

static RichTextParagraphFormat rich_paragraph_format_from_json(const json &j, const RichTextParagraphFormat &fallback = {})
{
    RichTextParagraphFormat f = fallback;
    if (!j.is_object()) return f;
    f.align_h = std::clamp(json_int(j, "align_h", f.align_h), 0, 6);
    f.align_v = std::clamp(json_int(j, "align_v", f.align_v), 0, 3);
    f.indent_left = (float)std::clamp(finite_or(json_double(j, "indent_left", f.indent_left), f.indent_left), 0.0, 10000.0);
    f.indent_right = (float)std::clamp(finite_or(json_double(j, "indent_right", f.indent_right), f.indent_right), 0.0, 10000.0);
    f.indent_first_line = (float)std::clamp(finite_or(json_double(j, "indent_first_line", f.indent_first_line), f.indent_first_line), -10000.0, 10000.0);
    f.line_spacing = (float)std::clamp(finite_or(json_double(j, "line_spacing", f.line_spacing), f.line_spacing), -1000.0, 1000.0);
    f.space_before = (float)std::clamp(finite_or(json_double(j, "space_before", f.space_before), f.space_before), 0.0, 10000.0);
    f.space_after = (float)std::clamp(finite_or(json_double(j, "space_after", f.space_after), f.space_after), 0.0, 10000.0);
    f.hyphenate = json_bool(j, "hyphenate", f.hyphenate);
    return f;
}

static json rich_doc_to_json(const RichTextDocument &doc)
{
    json blocks = json::array();
    for (const auto &b : doc.blocks)
        blocks.push_back({{"start", b.start}, {"length", b.length}, {"mask", b.mask},
                          {"format", rich_paragraph_format_to_json(b.format)}});
    json ranges = json::array();
    for (const auto &r : doc.ranges)
        ranges.push_back({{"start", r.start}, {"length", r.length}, {"mask", r.mask},
                          {"format", rich_char_format_to_json(r.format)}});

    /* Runtime edit history is intentionally not persisted. Transactions use
     * Unicode-safe boundaries, but the title file contains canonical state only. */

    json auto_rules = json::array();
    for (const auto &rule : doc.auto_style_rules) {
        auto_rules.push_back({{"pattern_schema_version", fxm::serialization::kCurrentPatternSchemaVersion},
                              {"enabled", rule.enabled},
                              {"style_preset_id", rule.style_preset_id},
                              {"rule_id", rule.rule_id},
                              {"display_name", rule.display_name},
                              {"conflict_mode", rule.conflict_mode},
                              {"match_mode", rule.match_mode},
                              {"stop_processing", rule.stop_processing},
                              {"excludes_rule_ids", rule.excludes_rule_ids},
                              {"condition_type", rule.condition_type},
                              {"regex_pattern", rule.regex_pattern},
                              {"regex_capture_group", rule.regex_capture_group},
                              {"regex_case_sensitive", rule.regex_case_sensitive},
                              {"generalization_mode", rule.generalization_mode},
                              {"prevent_duplicates", rule.prevent_duplicates},
                              {"allow_multiple_cases", rule.allow_multiple_cases},
                              {"start_condition", rule.start_condition},
                              {"end_condition", rule.end_condition},
                              {"start_offset", rule.start_offset},
                              {"end_offset", rule.end_offset},
                              {"start_custom_chars", rule.start_custom_chars},
                              {"end_custom_chars", rule.end_custom_chars},
                              {"require_stop_match", rule.require_stop_match},
                              {"include_start_marker", rule.include_start_marker},
                              {"include_end_marker", rule.include_end_marker},
                              {"start", rule.start},
                              {"length", rule.length},
                              {"cached_mask", rule.cached_mask},
                              {"cached_format", rich_char_format_to_json(rule.cached_format)}});
    }
    return {{"version", doc.version},
            {"formatting_schema_version", fxm::serialization::kCurrentFormattingSchemaVersion},
            {"plain_text", doc.plain_text},
            {"default_format", rich_char_format_to_json(doc.default_format)},
            {"default_paragraph_format", rich_paragraph_format_to_json(doc.default_paragraph_format)},
            {"has_typing_format", doc.has_typing_format},
            {"typing_format", rich_char_format_to_json(doc.has_typing_format ? doc.typing_format : doc.default_format)},
            {"typing_format_mask", doc.has_typing_format ? doc.typing_format_mask : 0u},
            {"blocks", blocks}, {"ranges", ranges},
            {"auto_style_enabled", doc.auto_style_enabled},
            {"auto_default_style_preset_id", doc.auto_default_style_preset_id},
            {"auto_default_style_cached_mask", doc.auto_default_style_cached_mask},
            {"auto_default_style_cached_format", rich_char_format_to_json(doc.auto_default_style_cached_format)},
            {"auto_style_rules", auto_rules},
            {"selection", {{"anchor", doc.selection.anchor}, {"head", doc.selection.head}}}};
}

static RichTextDocument rich_doc_from_json(const json &j, const Layer &layer)
{
    RichTextDocument doc = rich_text_document_from_layer_defaults(layer);
    if (!j.is_object()) return doc;
    doc.version = std::clamp(json_int(j, "version", 1), 1, 2);
    doc.plain_text = bounded_string(j, "plain_text", doc.plain_text, kMaxTextLength);
    if (j.contains("default_format")) doc.default_format = rich_char_format_from_json(j["default_format"], doc.default_format);
    if (j.contains("default_paragraph_format")) doc.default_paragraph_format = rich_paragraph_format_from_json(j["default_paragraph_format"], doc.default_paragraph_format);
    doc.has_typing_format = json_bool(j, "has_typing_format", false);
    doc.typing_format = j.contains("typing_format") ? rich_char_format_from_json(j["typing_format"], doc.default_format) : doc.default_format;
    doc.typing_format_mask = j.contains("typing_format_mask")
        ? (uint32_t)std::clamp(json_int(j, "typing_format_mask", 0), 0, (int)RichTextCharAll)
        : (doc.has_typing_format
               ? rich_text_char_format_difference_mask(doc.typing_format, doc.default_format)
               : 0u);
    doc.blocks.clear();
    if (j.contains("blocks") && j["blocks"].is_array()) {
        for (size_t i = 0; i < std::min(j["blocks"].size(), kMaxTextLength); ++i) {
            const auto &bj = j["blocks"][i];
            if (!bj.is_object()) continue;
            RichTextBlock block;
            block.start = (size_t)std::clamp(json_int(bj, "start", 0), 0, (int)kMaxTextLength);
            block.length = (size_t)std::clamp(json_int(bj, "length", 0), 0, (int)kMaxTextLength);
            block.format = bj.contains("format")
                ? rich_paragraph_format_from_json(bj["format"], doc.default_paragraph_format)
                : doc.default_paragraph_format;
            /* Version 1 paragraph blocks had no override mask. Infer the
             * sparse fields that differ from the document default. */
            block.mask = bj.contains("mask")
                ? (uint32_t)std::clamp(json_int(bj, "mask", (int)RichTextParagraphAll),
                                       0, (int)RichTextParagraphAll)
                : rich_text_paragraph_format_difference_mask(block.format,
                                                              doc.default_paragraph_format);
            doc.blocks.push_back(block);
        }
    }
    doc.ranges.clear();
    if (j.contains("ranges") && j["ranges"].is_array()) {
        for (size_t i = 0; i < std::min(j["ranges"].size(), kMaxTextLength); ++i) {
            const auto &rj = j["ranges"][i];
            if (!rj.is_object()) continue;
            RichTextRange r;
            r.start = (size_t)std::clamp(json_int(rj, "start", 0), 0, (int)kMaxTextLength);
            r.length = (size_t)std::clamp(json_int(rj, "length", 0), 0, (int)kMaxTextLength);
            r.format = rj.contains("format") ? rich_char_format_from_json(rj["format"], doc.default_format) : doc.default_format;
            /* Version 1 stored complete style snapshots. Since it had no
             * explicit override intent, infer the sparse fields that differ
             * from the document default during migration. */
            r.mask = rj.contains("mask")
                ? (uint32_t)std::clamp(json_int(rj, "mask", (int)RichTextCharAll), 0, (int)RichTextCharAll)
                : rich_text_char_format_difference_mask(r.format, doc.default_format);
            doc.ranges.push_back(r);
        }
    }
    doc.auto_style_enabled = json_bool(j, "auto_style_enabled", false);
    doc.auto_default_style_preset_id = bounded_string(j, "auto_default_style_preset_id", "", kMaxNameLength);
    doc.auto_default_style_cached_mask = (uint32_t)std::clamp(json_int(j, "auto_default_style_cached_mask", 0), 0, (int)RichTextCharAll);
    doc.auto_default_style_cached_format = j.contains("auto_default_style_cached_format")
        ? rich_char_format_from_json(j["auto_default_style_cached_format"], doc.default_format)
        : doc.default_format;
    doc.auto_style_rules.clear();
    if (j.contains("auto_style_rules") && j["auto_style_rules"].is_array()) {
        for (size_t i = 0; i < std::min(j["auto_style_rules"].size(), (size_t)128); ++i) {
            const auto &rj = j["auto_style_rules"][i];
            if (!rj.is_object()) continue;
            RichTextAutoStyleRule rule;
            rule.enabled = json_bool(rj, "enabled", true);
            rule.style_preset_id = bounded_string(rj, "style_preset_id", "", kMaxNameLength);
            rule.rule_id = bounded_string(rj, "rule_id", "", kMaxNameLength);
            rule.display_name = bounded_string(rj, "display_name", "", kMaxNameLength);
            rule.conflict_mode = bounded_string(rj, "conflict_mode", "override_previous", kMaxNameLength);
            rule.match_mode = bounded_string(rj, "match_mode", "all_matches", kMaxNameLength);
            rule.stop_processing = json_bool(rj, "stop_processing", false);
            rule.excludes_rule_ids.clear();
            if (rj.contains("excludes_rule_ids") && rj["excludes_rule_ids"].is_array()) {
                for (size_t xi = 0; xi < std::min(rj["excludes_rule_ids"].size(), (size_t)64); ++xi) {
                    if (rj["excludes_rule_ids"][xi].is_string())
                        rule.excludes_rule_ids.push_back(rj["excludes_rule_ids"][xi].get<std::string>());
                }
            }
            if (rule.rule_id.empty()) rule.rule_id = std::to_string(i + 1);
            rule.condition_type = bounded_string(rj, "condition_type", "range_markers", kMaxNameLength);
            rule.regex_pattern = bounded_string(rj, "regex_pattern", "", 4096);
            rule.regex_capture_group = (size_t)std::clamp(json_int(rj, "regex_capture_group", 0), 0, 64);
            rule.regex_case_sensitive = json_bool(rj, "regex_case_sensitive", true);
            rule.generalization_mode = bounded_string(rj, "generalization_mode", "auto_merge", 32);
            rule.prevent_duplicates = json_bool(rj, "prevent_duplicates", true);
            rule.allow_multiple_cases = json_bool(rj, "allow_multiple_cases", true);
            rule.start_condition = bounded_string(rj, "start_condition", "text_start", kMaxNameLength);
            rule.end_condition = bounded_string(rj, "end_condition", "character_index", kMaxNameLength);
            rule.start_offset = (size_t)std::clamp(json_int(rj, "start_offset", 0), 0, (int)kMaxTextLength);
            rule.end_offset = (size_t)std::clamp(json_int(rj, "end_offset", json_int(rj, "length", 0)), 0, (int)kMaxTextLength);
            rule.start_custom_chars = bounded_string(rj, "start_custom_chars", "", 16);
            rule.end_custom_chars = bounded_string(rj, "end_custom_chars", "", 16);
            rule.require_stop_match = json_bool(rj, "require_stop_match", true);
            rule.include_start_marker = json_bool(rj, "include_start_marker", true);
            rule.include_end_marker = json_bool(rj, "include_end_marker", false);
            rule.start = (size_t)std::clamp(json_int(rj, "start", 0), 0, (int)kMaxTextLength);
            rule.length = (size_t)std::clamp(json_int(rj, "length", 0), 0, (int)kMaxTextLength);
            if (!rj.contains("start_condition") && !rj.contains("end_condition") && rule.condition_type == "start_to_char") {
                rule.start_condition = "text_start";
                rule.end_condition = "character_index";
                rule.start_offset = 0;
                rule.end_offset = rule.length;
            }
            rule.cached_mask = (uint32_t)std::clamp(json_int(rj, "cached_mask", 0), 0, (int)RichTextCharAll);
            rule.cached_format = rj.contains("cached_format") ? rich_char_format_from_json(rj["cached_format"], doc.default_format) : doc.default_format;
            doc.auto_style_rules.push_back(rule);
        }
    }
    if (j.contains("selection") && j["selection"].is_object()) {
        doc.selection.anchor = (size_t)std::clamp(json_int(j["selection"], "anchor", 0), 0, (int)kMaxTextLength);
        doc.selection.head = (size_t)std::clamp(json_int(j["selection"], "head", 0), 0, (int)kMaxTextLength);
    }
    /* Ignore transactions embedded by older files. They describe a previous
     * editing session and are not part of the canonical document contract. */
    doc.transactions.clear();
    doc.normalize();
    return doc;
}

static json light_to_json(const TitleLight &light);
static TitleLight light_from_json(const json &j, size_t index);

static json layer_to_json(const Layer &l, bool include_embedded_assets = true,
                          bool require_embedded_assets = false, std::string *error = nullptr,
                          bool *asset_embed_failed = nullptr,
                          bool preserve_serialization_passthrough = true)
{
    /* Serialization passthrough is load/save metadata, never render input.
     * Render fingerprints explicitly disable it so the hot path performs no
     * JSON parse/deep merge and is independent of future-schema payload size. */
    const json source_passthrough = preserve_serialization_passthrough
        ? passthrough_json_object(l.serialization_passthrough_json)
        : json::object();
    json j = source_passthrough;
    j["id"]       = l.id;
    j["name"]     = l.name;
    j["type"]     = (int)l.type;
    j["visible"]  = l.visible;
    j["locked"]   = l.locked;
    j["properties_expanded"] = l.properties_expanded;
    j["group_collapsed"] = l.group_collapsed;
    j["custom_ui_color_enabled"] = l.custom_ui_color_enabled;
    j["custom_ui_color"] = l.custom_ui_color;
    if (l.type == LayerType::TransitionInput) {
        j["transition_input_slot"] = l.transition_input_slot;
        j["transition_input_required"] = l.transition_input_required;
    } else {
        j.erase("transition_input_slot");
        j.erase("transition_input_required");
    }
    j["parent_id"] = l.parent_id;
    j["transform_parent_id"] = l.transform_parent_id;
    j["parent_bind_enabled"] = l.parent_bind_enabled;
    if (l.parent_bind_enabled) {
        json bind = json::array();
        for (double value : l.parent_bind_matrix)
            bind.push_back(std::isfinite(value) ? value : 0.0);
        j["parent_bind_matrix"] = std::move(bind);
    } else {
        j.erase("parent_bind_matrix");
    }
    j["asset_title_id"] = l.asset_title_id;
    j["asset_owner_id"] = l.asset_owner_id;
    j["asset_source_layer_id"] = l.asset_source_layer_id;
    j["asset_category"] = l.asset_category;
    j["asset_animated"] = l.asset_animated;
    j["asset_playback_mode"] = l.asset_playback_mode;
    j["asset_playback_offset"] = l.asset_playback_offset;
    j["asset_duration"] = l.asset_duration;
    j["asset_source_playback_mode"] = l.asset_source_playback_mode;
    j["asset_source_loop_type"] = l.asset_source_loop_type;
    j["asset_source_loop_start"] = l.asset_source_loop_start;
    j["asset_source_loop_end"] = l.asset_source_loop_end;
    j["asset_source_pause_time"] = l.asset_source_pause_time;
    j["asset_pause_duration"] = l.asset_pause_duration;
    j["asset_loop_count"] = l.asset_loop_count;
    j["asset_loop"] = l.asset_loop;
    j["asset_isolated_3d_space"] = l.asset_isolated_3d_space;
    j["asset_space_width"] = l.asset_space_width;
    j["asset_space_height"] = l.asset_space_height;
    j["asset_space_center_x"] = l.asset_space_center_x;
    j["asset_space_center_y"] = l.asset_space_center_y;
    j["asset_camera_uses_owner_time"] = l.asset_camera_uses_owner_time;
    j["audio_source"] = l.audio_source;
    j["audio_stream_index"] = l.audio_stream_index;
    j["audio_in_point"] = l.audio_in_point;
    j["audio_out_point"] = l.audio_out_point;
    j["audio_volume"] = l.audio_volume;
    j["audio_pan"] = l.audio_pan;
    j["audio_volume_prop"] = aprop_to_json(l.audio_volume_prop);
    j["audio_pan_prop"] = aprop_to_json(l.audio_pan_prop);
    j["audio_muted"] = l.audio_muted;
    j["audio_solo"] = l.audio_solo;
    j["audio_fade_in"] = l.audio_fade_in;
    j["audio_fade_out"] = l.audio_fade_out;
    j["audio_fade_curve"] = (int)l.audio_fade_curve;
    json audio_effects = json::array();
    for (const auto &fx : l.audio_effects) {
        const json fx_passthrough = passthrough_json_object(fx.serialization_passthrough_json);
        json serialized_fx = {
            {"type", (int)fx.type}, {"enabled", fx.enabled},
            {"gain_db", fx.gain_db}, {"frequency_hz", fx.frequency_hz},
            {"threshold_db", fx.threshold_db}, {"ratio", fx.ratio},
            {"attack_ms", fx.attack_ms}, {"release_ms", fx.release_ms},
            {"makeup_db", fx.makeup_db}, {"fade_in", fx.fade_in},
            {"fade_out", fx.fade_out}, {"fade_curve", (int)fx.fade_curve}
        };
        audio_effects.push_back(merge_surviving_passthrough(fx_passthrough, serialized_fx));
    }
    j["audio_effects"] = std::move(audio_effects);
    j["audio_loop"] = l.audio_loop;
    j["audio_playback_mode"] = (int)l.audio_playback_mode;
    j["audio_independent"] = l.audio_independent;
    j["audio_media_duration"] = l.audio_media_duration;
    j["audio_sample_rate"] = l.audio_sample_rate;
    j["audio_channels"] = l.audio_channels;
    j["audio_waveform"] = l.audio_waveform;
    j["audio_waveform_duration"] = l.audio_waveform_duration;
    j["audio_waveform_progress_percent"] = l.audio_waveform_progress_percent;
    j["audio_waveform_generating"] = l.audio_waveform_generating;
    j["audio_waveform_progress_label"] = l.audio_waveform_progress_label;
    j["linked_media_layer_id"] = l.linked_media_layer_id;
    j["linked_media_stream"] = l.linked_media_stream;
    j["media_stream_label"] = l.media_stream_label;
    j["video_source"] = l.video_source;
    j["video_source_relative"] = l.video_source_relative;
    j["video_source_absolute"] = l.video_source_absolute;
    j["video_media_root"] = l.video_media_root;
    j["video_stream_index"] = l.video_stream_index;
    j["video_audio_stream_index"] = l.video_audio_stream_index;
    j["video_selected_streams"] = l.video_selected_streams_json;
    j["video_in_point"] = l.video_in_point;
    j["video_out_point"] = l.video_out_point;
    j["video_loop"] = l.video_loop;
    j["video_playback_mode"] = l.video_playback_mode;
    j["video_media_duration"] = l.video_media_duration;
    j["video_frame_rate"] = l.video_frame_rate;
    j["video_pixel_width"] = l.video_pixel_width;
    j["video_pixel_height"] = l.video_pixel_height;
    j["video_has_alpha"] = l.video_has_alpha;
    j["video_has_hdr"] = l.video_has_hdr;
    j["video_color_primaries"] = l.video_color_primaries;
    j["video_color_transfer"] = l.video_color_transfer;
    j["video_color_matrix"] = l.video_color_matrix;
    j["video_color_range"] = l.video_color_range;
    j["video_decode_settings"] = l.video_decode_settings_json;
    j["video_prefer_hardware_decode"] = l.video_prefer_hardware_decode;
    j["video_allow_hardware_fallback"] = l.video_allow_hardware_fallback;
    j["video_decode_cache_policy"] = l.video_decode_cache_policy;
    j["video_source_fingerprint"] = l.video_source_fingerprint;
    j["video_proxy_path"] = l.video_proxy_path;
    j["video_proxy_fingerprint"] = l.video_proxy_fingerprint;
    j["video_proxy_profile"] = l.video_proxy_profile;
    j["video_proxy_complete"] = l.video_proxy_complete;
    j["video_proxy_alpha"] = l.video_proxy_alpha;
    j["video_proxy_hdr"] = l.video_proxy_hdr;
    j["video_proxy_audio_preserved"] = l.video_proxy_audio_preserved;
    j["video_proxy_progress_percent"] = l.video_proxy_progress_percent;
    j["video_proxy_generating"] = l.video_proxy_generating;
    j["video_time_remap_enabled"] = l.video_time_remap_enabled;
    j["video_source_time"] = aprop_to_json(l.video_source_time);
    json video_loops = json::array();
    for (const auto &segment : l.video_time_remap_loop_segments) {
        video_loops.push_back({
            {"timeline_start", segment.timeline_start},
            {"timeline_end", segment.timeline_end},
            {"source_start", segment.source_start},
            {"source_end", segment.source_end},
            {"enabled", segment.enabled}
        });
    }
    j["video_time_remap_loop_segments"] = std::move(video_loops);
    j["video_time_remap_audio_mode"] = (int)l.video_time_remap_audio_mode;
    j["video_frame_interpolation"] = (int)l.video_frame_interpolation;
    j["video_optical_flow_enabled"] = l.video_optical_flow_enabled;
    j["video_optical_flow_analysis_running"] = l.video_optical_flow_analysis_running;
    j["video_optical_flow_progress_percent"] = l.video_optical_flow_progress_percent;
    j["video_time_remap_curve_fingerprint"] = l.video_time_remap_curve_fingerprint;
    j["video_optical_flow_cache_fingerprint"] = l.video_optical_flow_cache_fingerprint;
    j["mask_source_id"] = l.mask_source_id;
    j["mask_mode"] = (int)l.mask_mode;
    j["matte_visibility_mode"] = (int)l.matte_visibility_mode;
    j["blend_mode"] = (int)l.blend_mode;
    j["use_as_scene_mask"] = l.use_as_scene_mask;
    j["effect_stack_respects_masks"] = l.effect_stack_respects_masks;
    if (!l.external_bindings.empty()) {
        json bindings = json::array();
        for (const auto &binding : l.external_bindings)
            bindings.push_back(external_binding_to_json(binding));
        j["external_bindings"] = std::move(bindings);
    } else {
        j.erase("external_bindings");
    }
    if (l.type == LayerType::Chart) {
        const json binding_passthrough = passthrough_json_object(
            l.chart_binding.serialization_passthrough_json);
        const json binding_authored = {
            {"source_id", l.chart_binding.source_id},
            {"table_path", l.chart_binding.table_path},
            {"category_field", l.chart_binding.category_field},
            {"series_field", l.chart_binding.series_field},
            {"secondary_value_field", l.chart_binding.secondary_value_field},
            {"metadata_field", l.chart_binding.metadata_field},
            {"color_field", l.chart_binding.color_field},
            {"start_row", l.chart_binding.start_row},
            {"maximum_rows", l.chart_binding.maximum_rows},
            {"ignore_empty_rows", l.chart_binding.ignore_empty_rows}
        };
        const json chart_authored = {
            {"type", (int)l.chart_type},
            {"data_mode", (int)l.chart_data_mode},
            {"binding", merge_surviving_passthrough(binding_passthrough,
                                                     binding_authored)},
            {"background_enabled", l.chart_background_enabled},
            {"grid_enabled", l.chart_grid_enabled},
            {"axes_enabled", l.chart_axes_enabled},
            {"axis_labels_enabled", l.chart_axis_labels_enabled},
            {"data_labels_enabled", l.chart_data_labels_enabled},
            {"legend_enabled", l.chart_legend_enabled},
            {"fill_enabled", l.chart_fill_enabled},
            {"outline_enabled", l.chart_outline_enabled},
            {"background_color", l.chart_background_color},
            {"grid_color", l.chart_grid_color},
            {"axis_color", l.chart_axis_color},
            {"label_color", l.chart_label_color},
            {"font_family", l.chart_font_family},
            {"number_prefix", l.chart_number_prefix},
            {"number_suffix", l.chart_number_suffix},
            {"thousands_separator", l.chart_thousands_separator},
            {"decimal_precision", l.chart_decimal_precision},
            {"data_transition_seconds", l.chart_data_transition_seconds},
            {"animation_progress", aprop_to_json(l.chart_animation_progress)},
            {"reveal_progress", aprop_to_json(l.chart_reveal_progress)},
            {"value_transition", aprop_to_json(l.chart_value_transition)},
            {"label_opacity", aprop_to_json(l.chart_label_opacity)},
            {"element_opacity", aprop_to_json(l.chart_element_opacity)},
            {"font_size", aprop_to_json(l.chart_font_size)},
            {"line_thickness", aprop_to_json(l.chart_line_thickness)},
            {"bar_width", aprop_to_json(l.chart_bar_width)},
            {"bar_spacing", aprop_to_json(l.chart_bar_spacing)},
            {"corner_radius", aprop_to_json(l.chart_corner_radius)},
            {"padding", aprop_to_json(l.chart_padding)},
            {"margin_left", aprop_to_json(l.chart_margin_left)},
            {"margin_top", aprop_to_json(l.chart_margin_top)},
            {"margin_right", aprop_to_json(l.chart_margin_right)},
            {"margin_bottom", aprop_to_json(l.chart_margin_bottom)}
        };
        json chart = merge_surviving_passthrough(
            passthrough_json_object(l.chart_serialization_passthrough_json),
            chart_authored);
        json series = json::array();
        for (const ChartSeries &item : l.chart_series) {
            const json item_passthrough =
                passthrough_json_object(item.serialization_passthrough_json);
            const json authored = {
                {"id", item.id}, {"name", item.name},
                {"value_field", item.value_field},
                {"secondary_value_field", item.secondary_value_field},
                {"metadata_field", item.metadata_field},
                {"color", item.color}, {"fill_color", item.fill_color},
                {"visible", item.visible}, {"fill_enabled", item.fill_enabled},
                {"outline_enabled", item.outline_enabled},
                {"line_thickness", item.line_thickness}
            };
            series.push_back(merge_surviving_passthrough(item_passthrough,
                                                         authored));
        }
        chart["series"] = std::move(series);
        json values = json::array();
        for (const ChartValue &item : l.chart_values) {
            const json authored = {
                {"id", item.id}, {"category", item.category},
                {"series_id", item.series_id}, {"value", item.value},
                {"secondary_value", item.secondary_value},
                {"has_secondary_value", item.has_secondary_value},
                {"metadata", item.metadata},
                {"color_override", item.color_override},
                {"color", item.color},
                {"expose_in_dock", item.expose_in_dock}
            };
            values.push_back(merge_surviving_passthrough(
                passthrough_json_object(item.serialization_passthrough_json), authored));
        }
        chart["values"] = std::move(values);
        j["chart"] = std::move(chart);
    } else {
        j.erase("chart");
    }
    json effects = json::array();
    for (const auto &effect : l.effects) {
        const std::string stable_effect_id = effect.extension_id.empty()
            ? FxmEffectExtensionCatalog::builtInId(effect.type).toStdString()
            : effect.extension_id;
        effects.push_back({{"type", (int)effect.type},
                           {"effect_id", stable_effect_id},
                           {"extension_id", stable_effect_id},
                           {"external_plugin_id", effect.extension_provider_id},
                           {"external_plugin_version", effect.extension_provider_version},
                           {"external_plugin_binary_id", effect.extension_plugin_binary_id},
                           {"extension_parameters", effect.extension_parameters_json},
                           {"extension_schema_version", effect.extension_schema_version},
                           {"extension_loaded_schema_version", effect.extension_loaded_schema_version},
                           {"extension_runtime_schema_version", effect.extension_runtime_schema_version},
                           {"extension_explicit_migration", effect.extension_explicit_migration},
                           {"extension_keyframes", effect.extension_keyframes_json},
                           {"extension_binary_state", effect.extension_binary_state_json},
                           {"extension_binary_state_base64", effect.extension_binary_state_base64},
                           {"missing_plugin_placeholder", effect.missing_plugin_placeholder},
                           {"effect_preset_id", effect.effect_preset_id},
                           {"effect_preset_schema_version", effect.effect_preset_schema_version},
                           {"enabled", effect.enabled},
                           {"brightness", effect.brightness},
                           {"contrast", effect.contrast},
                           {"saturation", effect.saturation},
                           {"tint_color", effect.tint_color},
                           {"tint_amount", effect.tint_amount},
                           {"effect_color", effect.effect_color},
                           {"effect_opacity", effect.effect_opacity},
                           {"effect_size", effect.effect_size},
                           {"effect_distance", effect.effect_distance},
                           {"effect_angle", effect.effect_angle},
                           {"effect_spread", effect.effect_spread},
                           {"effect_falloff", effect.effect_falloff},
                           {"effect_blur_type", effect.effect_blur_type},
                           {"effect_samples", effect.effect_samples},
                           {"effect_centered", effect.effect_centered},
                           {"effect_outside_hard_alpha", effect.effect_outside_hard_alpha},
                           {"effect_outside_hard_alpha_invert", effect.effect_outside_hard_alpha_invert},
                           {"affect_layers_behind", effect.affect_layers_behind},
                           {"affect_layers_behind_invert", effect.affect_layers_behind_invert},
                           {"effect_source_layer_id", effect.effect_source_layer_id},
                           {"effect_source_mode", effect.effect_source_mode},
                           {"effect_x_channel", effect.effect_x_channel},
                           {"effect_y_channel", effect.effect_y_channel},
                           {"effect_wrap_mode", effect.effect_wrap_mode},
                           {"effect_mapping_space", effect.effect_mapping_space},
                           {"effect_alpha_aware", effect.effect_alpha_aware},
                           {"effect_profile", effect.effect_profile},
                           {"effect_animated", effect.effect_animated},
                           {"effect_monochrome", effect.effect_monochrome},
                           {"effect_invert", effect.effect_invert},
                           {"effect_seed", effect.effect_seed},
                           {"effect_amount", effect.effect_amount},
                           {"effect_scale", effect.effect_scale},
                           {"effect_softness", effect.effect_softness},
                           {"effect_roundness", effect.effect_roundness},
                           {"effect_speed", effect.effect_speed},
                           {"effect_center_x", effect.effect_center_x},
                           {"effect_center_y", effect.effect_center_y},
                           {"effect_complexity", effect.effect_complexity},
                           {"effect_evolution", effect.effect_evolution},
                           {"effect_affect_alpha", effect.effect_affect_alpha},
                           {"effect_clamp_output", effect.effect_clamp_output},
                           {"effect_temporal_stability", effect.effect_temporal_stability},
                           {"effect_secondary_color", effect.effect_secondary_color},
                           {"blend_mode", (int)effect.blend_mode},
                           {"effect_fill_type", effect.effect_fill_type},
                           {"effect_join_style", effect.effect_join_style},
                           {"effect_on_front", effect.effect_on_front},
                           {"effect_antialias", effect.effect_antialias},
                           {"effect_stroke_color", effect.effect_stroke_color},
                           {"effect_stroke_width", effect.effect_stroke_width},
                           {"effect_stroke_opacity", effect.effect_stroke_opacity},
                           {"effect_trim_start", effect.effect_trim_start},
                           {"effect_trim_end", effect.effect_trim_end},
                           {"effect_trim_offset", effect.effect_trim_offset},
                           {"effect_trim_multiple_shapes", effect.effect_trim_multiple_shapes},
                           {"effect_padding_left", effect.effect_padding_left},
                           {"effect_padding_right", effect.effect_padding_right},
                           {"effect_padding_top", effect.effect_padding_top},
                           {"effect_padding_bottom", effect.effect_padding_bottom},
                           {"effect_corner_radius_tl", effect.effect_corner_radius_tl},
                           {"effect_corner_radius_tr", effect.effect_corner_radius_tr},
                           {"effect_corner_radius_br", effect.effect_corner_radius_br},
                           {"effect_corner_radius_bl", effect.effect_corner_radius_bl},
                           {"effect_corner_type", effect.effect_corner_type},
                           {"effect_gradient_type", effect.effect_gradient_type},
                           {"effect_gradient_spread", effect.effect_gradient_spread},
                           {"effect_gradient_start_color", effect.effect_gradient_start_color},
                           {"effect_gradient_end_color", effect.effect_gradient_end_color},
                           {"effect_gradient_start_pos", effect.effect_gradient_start_pos},
                           {"effect_gradient_end_pos", effect.effect_gradient_end_pos},
                           {"effect_gradient_start_opacity", effect.effect_gradient_start_opacity},
                           {"effect_gradient_end_opacity", effect.effect_gradient_end_opacity},
                           {"effect_gradient_opacity", effect.effect_gradient_opacity},
                           {"effect_gradient_angle", effect.effect_gradient_angle},
                           {"effect_gradient_center_x", effect.effect_gradient_center_x},
                           {"effect_gradient_center_y", effect.effect_gradient_center_y},
                           {"effect_gradient_scale", effect.effect_gradient_scale},
                           {"effect_gradient_focal_x", effect.effect_gradient_focal_x},
                           {"effect_gradient_focal_y", effect.effect_gradient_focal_y},
                           {"enabled_prop", aprop_to_json(effect.enabled_prop)},
                           {"brightness_prop", aprop_to_json(effect.brightness_prop)},
                           {"contrast_prop", aprop_to_json(effect.contrast_prop)},
                           {"saturation_prop", aprop_to_json(effect.saturation_prop)},
                           {"opacity_prop", aprop_to_json(effect.opacity_prop)},
                           {"size_prop", aprop_to_json(effect.size_prop)},
                           {"distance_prop", aprop_to_json(effect.distance_prop)},
                           {"angle_prop", aprop_to_json(effect.angle_prop)},
                           {"spread_prop", aprop_to_json(effect.spread_prop)},
                           {"falloff_prop", aprop_to_json(effect.falloff_prop)},
                           {"amount_prop", aprop_to_json(effect.amount_prop)},
                           {"scale_prop", aprop_to_json(effect.scale_prop)},
                           {"softness_prop", aprop_to_json(effect.softness_prop)},
                           {"roundness_prop", aprop_to_json(effect.roundness_prop)},
                           {"speed_prop", aprop_to_json(effect.speed_prop)},
                           {"center_x_prop", aprop_to_json(effect.center_x_prop)},
                           {"center_y_prop", aprop_to_json(effect.center_y_prop)},
                           {"complexity_prop", aprop_to_json(effect.complexity_prop)},
                           {"evolution_prop", aprop_to_json(effect.evolution_prop)},
                           {"stroke_width_prop", aprop_to_json(effect.stroke_width_prop)},
                           {"stroke_opacity_prop", aprop_to_json(effect.stroke_opacity_prop)},
                           {"trim_start_prop", aprop_to_json(effect.trim_start_prop)},
                           {"trim_end_prop", aprop_to_json(effect.trim_end_prop)},
                           {"trim_offset_prop", aprop_to_json(effect.trim_offset_prop)},
                           {"padding_left_prop", aprop_to_json(effect.padding_left_prop)},
                           {"padding_right_prop", aprop_to_json(effect.padding_right_prop)},
                           {"padding_top_prop", aprop_to_json(effect.padding_top_prop)},
                           {"padding_bottom_prop", aprop_to_json(effect.padding_bottom_prop)},
                           {"corner_radius_tl_prop", aprop_to_json(effect.corner_radius_tl_prop)},
                           {"corner_radius_tr_prop", aprop_to_json(effect.corner_radius_tr_prop)},
                           {"corner_radius_br_prop", aprop_to_json(effect.corner_radius_br_prop)},
                           {"corner_radius_bl_prop", aprop_to_json(effect.corner_radius_bl_prop)},
                           {"gradient_start_pos_prop", aprop_to_json(effect.gradient_start_pos_prop)},
                           {"gradient_end_pos_prop", aprop_to_json(effect.gradient_end_pos_prop)},
                           {"gradient_start_opacity_prop", aprop_to_json(effect.gradient_start_opacity_prop)},
                           {"gradient_end_opacity_prop", aprop_to_json(effect.gradient_end_opacity_prop)},
                           {"gradient_angle_prop", aprop_to_json(effect.gradient_angle_prop)},
                           {"gradient_center_x_prop", aprop_to_json(effect.gradient_center_x_prop)},
                           {"gradient_center_y_prop", aprop_to_json(effect.gradient_center_y_prop)},
                           {"gradient_scale_prop", aprop_to_json(effect.gradient_scale_prop)},
                           {"gradient_focal_x_prop", aprop_to_json(effect.gradient_focal_x_prop)},
                           {"gradient_focal_y_prop", aprop_to_json(effect.gradient_focal_y_prop)},
                           {"gradient_opacity_prop", aprop_to_json(effect.gradient_opacity_prop)},
                           {"gradient_start_color_a", aprop_to_json(effect.gradient_start_color_a)},
                           {"gradient_start_color_r", aprop_to_json(effect.gradient_start_color_r)},
                           {"gradient_start_color_g", aprop_to_json(effect.gradient_start_color_g)},
                           {"gradient_start_color_b", aprop_to_json(effect.gradient_start_color_b)},
                           {"gradient_end_color_a", aprop_to_json(effect.gradient_end_color_a)},
                           {"gradient_end_color_r", aprop_to_json(effect.gradient_end_color_r)},
                           {"gradient_end_color_g", aprop_to_json(effect.gradient_end_color_g)},
                           {"gradient_end_color_b", aprop_to_json(effect.gradient_end_color_b)},
                           {"color_a", aprop_to_json(effect.color_a)},
                           {"color_r", aprop_to_json(effect.color_r)},
                           {"color_g", aprop_to_json(effect.color_g)},
                           {"color_b", aprop_to_json(effect.color_b)},
                           {"stroke_color_a", aprop_to_json(effect.stroke_color_a)},
                           {"stroke_color_r", aprop_to_json(effect.stroke_color_r)},
                           {"stroke_color_g", aprop_to_json(effect.stroke_color_g)},
                           {"stroke_color_b", aprop_to_json(effect.stroke_color_b)},
                           {"secondary_color_a", aprop_to_json(effect.secondary_color_a)},
                           {"secondary_color_r", aprop_to_json(effect.secondary_color_r)},
                           {"secondary_color_g", aprop_to_json(effect.secondary_color_g)},
                           {"secondary_color_b", aprop_to_json(effect.secondary_color_b)}});
        const json preserved_effect = passthrough_json_object(effect.serialization_passthrough_json);
        effects.back() = merge_surviving_passthrough(preserved_effect, effects.back());
    }
    j["effects"] = effects;
    json transitions = json::array();
    for (const auto &transition : l.transitions) {
        transitions.push_back({
            {"id", transition.id},
            {"preset_id", transition.preset_id},
            {"display_name", transition.display_name},
            {"enabled", transition.enabled},
            {"kind", (int)transition.kind},
            {"type", (int)transition.type},
            {"edge", (int)transition.edge},
            {"unit", (int)transition.unit},
            {"direction", (int)transition.direction},
            {"easing", (int)transition.easing},
            {"duration", transition.duration},
            {"blur_amount", transition.blur_amount},
            {"scale_from", transition.scale_from},
            {"offset", transition.offset},
            {"stagger", transition.stagger},
            {"softness", transition.softness},
            {"reverse_order", transition.reverse_order},
            {"text_slide_fade", transition.text_slide_fade},
            {"text_slide_crop_to_unit_bounds",
             transition.text_slide_crop_to_unit_bounds},
            {"blocks_columns", transition.blocks_columns},
            {"blocks_rows", transition.blocks_rows},
            {"random_seed", transition.random_seed},
            {"image_path", transition.image_path},
            {"image_channel", transition.image_channel},
            {"invert", transition.invert},
            {"clockwise", transition.clockwise},
            {"center_x", transition.center_x},
            {"center_y", transition.center_y},
            {"rotation", transition.rotation},
            {"aspect", transition.aspect},
            {"profile", transition.profile},
        });
        const json preserved_transition = passthrough_json_object(transition.serialization_passthrough_json);
        transitions.back() = merge_surviving_passthrough(preserved_transition, transitions.back());
    }
    j["transitions"] = transitions;
    j["in_time"]  = l.in_time;
    j["out_time"] = l.out_time;

    j["position"] = vec2_aprop_to_json(l.position);
    j["transform_quad"] = {
        l.transform_quad_tl_x, l.transform_quad_tl_y,
        l.transform_quad_tr_x, l.transform_quad_tr_y,
        l.transform_quad_br_x, l.transform_quad_br_y,
        l.transform_quad_bl_x, l.transform_quad_bl_y
    };
    j["transform_quad_tl"] = vec2_aprop_to_json(l.transform_quad_tl);
    j["transform_quad_tr"] = vec2_aprop_to_json(l.transform_quad_tr);
    j["transform_quad_br"] = vec2_aprop_to_json(l.transform_quad_br);
    j["transform_quad_bl"] = vec2_aprop_to_json(l.transform_quad_bl);
    j["scale"]    = vec2_aprop_to_json(l.scale);
    j["scale_lock"] = l.scale_lock;
    j["rotation"] = aprop_to_json(l.rotation);
    j["opacity"]  = aprop_to_json(l.opacity);
    j["dimension_mode"] = static_cast<int>(l.dimension_mode);
    j["transform_axis_space"] = static_cast<int>(l.transform_axis_space);
    j["position_z"] = aprop_to_json(l.position_z);
    j["position_3d_path_enabled"] = l.position_3d_path_enabled;
    if (l.position_3d_path_enabled)
        j["position_3d"] = vec3_aprop_to_json(l.position_3d);
    else
        j.erase("position_3d");
    j["rotation_x"] = aprop_to_json(l.rotation_x);
    j["rotation_y"] = aprop_to_json(l.rotation_y);
    j["scale_z"] = aprop_to_json(l.scale_z);
    j["anchor_z"] = aprop_to_json(l.anchor_z);
    j["orientation_x"] = aprop_to_json(l.orientation_x);
    j["orientation_y"] = aprop_to_json(l.orientation_y);
    j["orientation_z"] = aprop_to_json(l.orientation_z);
    j["camera_id"] = l.camera_assignment.static_value;
    j["camera_assignment"] = discrete_property_to_json(l.camera_assignment);
    j["depth_mode"] = static_cast<int>(l.depth_mode);
    j["depth_test"] = l.depth_test;
    j["write_to_depth"] = l.write_to_depth;
    j["double_sided"] = l.double_sided;
    j["backface_culling"] = l.backface_culling;
    j["material_accepts_lights"] = l.material_accepts_lights;
    j["material_casts_shadows"] = l.material_casts_shadows;
    j["material_accepts_shadows"] = l.material_accepts_shadows;
    j["material_appears_in_reflections"] = l.material_appears_in_reflections;
    j["material_ambient"] = aprop_to_json(l.material_ambient);
    j["material_diffuse"] = aprop_to_json(l.material_diffuse);
    j["material_specular"] = aprop_to_json(l.material_specular);
    j["material_shininess"] = aprop_to_json(l.material_shininess);
    j["material_metallic"] = aprop_to_json(l.material_metallic);
    j["material_roughness"] = aprop_to_json(l.material_roughness);
    j["material_reflection_intensity"] = aprop_to_json(l.material_reflection_intensity);
    j["material_emissive_color"] = l.material_emissive_color;
    j["material_emissive_color_a"] = aprop_to_json(l.material_emissive_color_a);
    j["material_emissive_color_r"] = aprop_to_json(l.material_emissive_color_r);
    j["material_emissive_color_g"] = aprop_to_json(l.material_emissive_color_g);
    j["material_emissive_color_b"] = aprop_to_json(l.material_emissive_color_b);
    j["material_emissive_intensity"] = aprop_to_json(l.material_emissive_intensity);
    j["geometry_extrusion_enabled"] = l.geometry_extrusion_enabled;
    j["geometry_extrusion_depth"] = aprop_to_json(l.geometry_extrusion_depth);
    j["geometry_bevel_depth"] = aprop_to_json(l.geometry_bevel_depth);
    j["geometry_bevel_segments"] = l.geometry_bevel_segments;
    j["geometry_extrusion_segments"] = l.geometry_extrusion_segments;
    j["geometry_bevel_front"] = l.geometry_bevel_front;
    j["geometry_bevel_back"] = l.geometry_bevel_back;
    if (l.type == LayerType::Light) j["light"] = light_to_json(l.light);

    j["text_content"]  = l.text_content;
    /* rich_text is the only style source of truth; do not serialize legacy HTML. */
    j["rich_text"] = rich_doc_to_json(l.rich_text);
    j["text_animators"] = text_animator_stack_to_json(l.text_animators);
    j["clock_format"]  = l.clock_format;
    j["expose_text"]   = l.expose_text;
    j["exposed_hide_if_empty"] = l.exposed_hide_if_empty;
    j["exposed_single_value"] = l.exposed_single_value;
    j["expose_fill_color"] = l.expose_fill_color;
    j["exposed_fill_single_value"] = l.exposed_fill_single_value;
    j["expose_stroke_color"] = l.expose_stroke_color;
    j["exposed_stroke_single_value"] = l.exposed_stroke_single_value;
    j["ignore_persistence"] = l.ignore_persistence;
    j["font_family"]   = l.font_family;
    j["font_style"]    = l.font_style;
    j["font_size"]     = l.font_size;
    j["font_size_prop"] = aprop_to_json(l.font_size_prop);
    j["font_bold"]     = l.font_bold;
    j["font_italic"]   = l.font_italic;
    j["font_kerning"]  = l.font_kerning;
    j["kerning_mode"]  = l.kerning_mode;
    j["manual_kerning"] = l.manual_kerning;
    j["text_leading"]  = l.text_leading;
    j["char_tracking"] = l.char_tracking;
    j["char_tracking_prop"] = aprop_to_json(l.char_tracking_prop);
    j["char_scale_x"]  = l.char_scale_x;
    j["char_scale_x_prop"] = aprop_to_json(l.char_scale_x_prop);
    j["char_scale_y"]  = l.char_scale_y;
    j["char_scale_y_prop"] = aprop_to_json(l.char_scale_y_prop);
    j["baseline_shift"] = l.baseline_shift;
    j["baseline_shift_prop"] = aprop_to_json(l.baseline_shift_prop);
    j["text_style"]    = l.text_style;
    j["text_underline"] = l.text_underline;
    j["text_strikethrough"] = l.text_strikethrough;
    j["text_ligatures"] = l.text_ligatures;
    j["text_stylistic_alternates"] = l.text_stylistic_alternates;
    j["text_fractions"] = l.text_fractions;
    j["text_opentype_features"] = l.text_opentype_features;
    j["text_language"] = l.text_language;
    j["text_overflow_mode"] = l.text_overflow_mode;
    j["text_fit_min_scale"] = l.text_fit_min_scale;
    j["text_box_width_to_text"] = l.text_box_width_to_text;
    j["text_box_height_to_text"] = l.text_box_height_to_text;
    j["max_text_box_width"] = l.max_text_box_width;
    j["max_text_box_height"] = l.max_text_box_height;
    j["max_text_box_width_overridden"] = l.max_text_box_width_overridden;
    j["max_text_box_height_overridden"] = l.max_text_box_height_overridden;
    j["ticker_style"] = l.ticker_style;
    j["ticker_speed"] = l.ticker_speed;
    j["ticker_line_hold"] = l.ticker_line_hold;
    j["ticker_direction"] = l.ticker_direction;
    j["ticker_playback_mode"] = l.ticker_playback_mode;
    j["ticker_completion"] = l.ticker_completion;
    j["ticker_completion_prop"] = aprop_to_json(l.ticker_completion_prop);
    j["text_color"]    = l.text_color;
    j["outline_enabled"] = l.outline_enabled;
    j["stroke_fill_type"] = l.stroke_fill_type;
    j["stroke_color"]  = l.stroke_color;
    j["stroke_width"]  = l.stroke_width;
    j["stroke_width_prop"] = aprop_to_json(l.stroke_width_prop);
    j["stroke_offset"] = l.stroke_offset;
    j["stroke_offset_prop"] = aprop_to_json(l.stroke_offset_prop);
    j["outline_opacity"] = l.outline_opacity;
    j["outline_join_style"] = l.outline_join_style;
    j["outline_on_front"] = l.outline_on_front;
    j["outline_alignment"] = l.outline_alignment;
    j["outline_antialias"] = l.outline_antialias;
    j["stroke_gradient_type"] = l.stroke_gradient_type;
    j["stroke_gradient_spread"] = l.stroke_gradient_spread;
    j["stroke_gradient_start_color"] = l.stroke_gradient_start_color;
    j["stroke_gradient_end_color"] = l.stroke_gradient_end_color;
    j["stroke_gradient_start_pos"] = l.stroke_gradient_start_pos;
    j["stroke_gradient_end_pos"] = l.stroke_gradient_end_pos;
    j["stroke_gradient_start_opacity"] = l.stroke_gradient_start_opacity;
    j["stroke_gradient_end_opacity"] = l.stroke_gradient_end_opacity;
    j["stroke_gradient_opacity"] = l.stroke_gradient_opacity;
    j["stroke_gradient_angle"] = l.stroke_gradient_angle;
    j["stroke_gradient_center_x"] = l.stroke_gradient_center_x;
    j["stroke_gradient_center_y"] = l.stroke_gradient_center_y;
    j["stroke_gradient_scale"] = l.stroke_gradient_scale;
    j["stroke_gradient_focal_x"] = l.stroke_gradient_focal_x;
    j["stroke_gradient_focal_y"] = l.stroke_gradient_focal_y;
    j["stroke_gradient_stops"] = gradient_stops_to_json(l.stroke_gradient_stops);
    j["align_h"]       = l.align_h;
    j["align_v"]       = l.align_v;
    j["paragraph_indent_left"] = l.paragraph_indent_left;
    j["paragraph_indent_right"] = l.paragraph_indent_right;
    j["paragraph_indent_first_line"] = l.paragraph_indent_first_line;
    j["paragraph_indent_left_prop"] = aprop_to_json(l.paragraph_indent_left_prop);
    j["paragraph_indent_right_prop"] = aprop_to_json(l.paragraph_indent_right_prop);
    j["paragraph_indent_first_line_prop"] = aprop_to_json(l.paragraph_indent_first_line_prop);
    j["paragraph_space_before"] = l.paragraph_space_before;
    j["paragraph_space_before_prop"] = aprop_to_json(l.paragraph_space_before_prop);
    j["paragraph_space_after"] = l.paragraph_space_after;
    j["paragraph_space_after_prop"] = aprop_to_json(l.paragraph_space_after_prop);
    j["paragraph_hyphenate"] = l.paragraph_hyphenate;

    j["fill_color"]    = l.fill_color;
    j["fill_type"]     = l.fill_type;
    j["gradient_type"] = l.gradient_type;
    j["gradient_spread"] = l.gradient_spread;
    j["gradient_start_color"] = l.gradient_start_color;
    j["gradient_end_color"] = l.gradient_end_color;
    j["gradient_start_pos"] = l.gradient_start_pos;
    j["gradient_end_pos"] = l.gradient_end_pos;
    j["gradient_start_opacity"] = l.gradient_start_opacity;
    j["gradient_end_opacity"] = l.gradient_end_opacity;
    j["gradient_opacity"] = l.gradient_opacity;
    j["gradient_angle"] = l.gradient_angle;
    j["gradient_center_x"] = l.gradient_center_x;
    j["gradient_center_y"] = l.gradient_center_y;
    j["gradient_scale"] = l.gradient_scale;
    j["gradient_focal_x"] = l.gradient_focal_x;
    j["gradient_focal_y"] = l.gradient_focal_y;
    j["gradient_stops"] = gradient_stops_to_json(l.gradient_stops);
    j["background_enabled"] = l.background_enabled;
    j["background_color"] = l.background_color;
    j["background_opacity"] = l.background_opacity;
    j["background_padding"] = l.background_padding_x;
    j["background_padding_x"] = l.background_padding_x;
    j["background_padding_y"] = l.background_padding_y;
    j["background_padding_left"] = l.background_padding_left;
    j["background_padding_right"] = l.background_padding_right;
    j["background_padding_top"] = l.background_padding_top;
    j["background_padding_bottom"] = l.background_padding_bottom;
    j["background_corner_radius"] = l.background_corner_radius;
    j["background_corner_radius_tl"] = l.background_corner_radius_tl;
    j["background_corner_radius_tr"] = l.background_corner_radius_tr;
    j["background_corner_radius_br"] = l.background_corner_radius_br;
    j["background_corner_radius_bl"] = l.background_corner_radius_bl;
    j["background_corner_type"] = (int)l.background_corner_type;
    j["background_fill_type"] = l.background_fill_type;
    j["background_stroke_color"] = l.background_stroke_color;
    j["background_stroke_width"] = l.background_stroke_width;
    j["background_stroke_opacity"] = l.background_stroke_opacity;
    j["background_stroke_fill_type"] = l.background_stroke_fill_type;
    j["background_gradient_type"] = l.background_gradient_type;
    j["background_gradient_spread"] = l.background_gradient_spread;
    j["background_gradient_start_color"] = l.background_gradient_start_color;
    j["background_gradient_end_color"] = l.background_gradient_end_color;
    j["background_gradient_start_pos"] = l.background_gradient_start_pos;
    j["background_gradient_end_pos"] = l.background_gradient_end_pos;
    j["background_gradient_start_opacity"] = l.background_gradient_start_opacity;
    j["background_gradient_end_opacity"] = l.background_gradient_end_opacity;
    j["background_gradient_opacity"] = l.background_gradient_opacity;
    j["background_gradient_angle"] = l.background_gradient_angle;
    j["background_gradient_center_x"] = l.background_gradient_center_x;
    j["background_gradient_center_y"] = l.background_gradient_center_y;
    j["background_gradient_scale"] = l.background_gradient_scale;
    j["background_gradient_focal_x"] = l.background_gradient_focal_x;
    j["background_gradient_focal_y"] = l.background_gradient_focal_y;
    j["background_gradient_stops"] = gradient_stops_to_json(l.background_gradient_stops);
    j["background_enabled_prop"] = aprop_to_json(l.background_enabled_prop);
    j["background_opacity_prop"] = aprop_to_json(l.background_opacity_prop);
    j["background_padding_x_prop"] = aprop_to_json(l.background_padding_x_prop);
    j["background_padding_y_prop"] = aprop_to_json(l.background_padding_y_prop);
    j["background_padding_left_prop"] = aprop_to_json(l.background_padding_left_prop);
    j["background_padding_right_prop"] = aprop_to_json(l.background_padding_right_prop);
    j["background_padding_top_prop"] = aprop_to_json(l.background_padding_top_prop);
    j["background_padding_bottom_prop"] = aprop_to_json(l.background_padding_bottom_prop);
    j["background_corner_radius_prop"] = aprop_to_json(l.background_corner_radius_prop);
    j["background_corner_radius_tl_prop"] = aprop_to_json(l.background_corner_radius_tl_prop);
    j["background_corner_radius_tr_prop"] = aprop_to_json(l.background_corner_radius_tr_prop);
    j["background_corner_radius_br_prop"] = aprop_to_json(l.background_corner_radius_br_prop);
    j["background_corner_radius_bl_prop"] = aprop_to_json(l.background_corner_radius_bl_prop);
    j["background_stroke_width_prop"] = aprop_to_json(l.background_stroke_width_prop);
    j["background_stroke_opacity_prop"] = aprop_to_json(l.background_stroke_opacity_prop);
    j["background_color_a"] = aprop_to_json(l.background_color_a);
    j["background_color_r"] = aprop_to_json(l.background_color_r);
    j["background_color_g"] = aprop_to_json(l.background_color_g);
    j["background_color_b"] = aprop_to_json(l.background_color_b);
    j["background_stroke_color_a"] = aprop_to_json(l.background_stroke_color_a);
    j["background_stroke_color_r"] = aprop_to_json(l.background_stroke_color_r);
    j["background_stroke_color_g"] = aprop_to_json(l.background_stroke_color_g);
    j["background_stroke_color_b"] = aprop_to_json(l.background_stroke_color_b);
    j["rect_width"]    = l.rect_width;
    j["rect_height"]   = l.rect_height;
    j["corner_radius"] = l.corner_radius;
    j["corner_radius_tl"] = l.corner_radius_tl;
    j["corner_radius_tr"] = l.corner_radius_tr;
    j["corner_radius_br"] = l.corner_radius_br;
    j["corner_radius_bl"] = l.corner_radius_bl;
    j["corner_radius_locked"] = l.corner_radius_locked;
    j["corner_bevel_roundness"] = l.corner_bevel_roundness;
    j["shape_type"] = (int)l.shape_type;
    j["path_points"] = bezier_path_points_to_json(l.path_points);
    j["path_closed"] = l.path_closed;
    j["shape_points"] = l.shape_points;
    j["shape_sides"] = l.shape_sides;
    j["shape_inner_radius"] = l.shape_inner_radius;
    j["shape_outer_radius"] = l.shape_outer_radius;
    j["shape_roundness"] = l.shape_roundness;
    j["shape_inner_roundness"] = l.shape_inner_roundness;
    j["scale_stroke_with_shape"] = l.scale_stroke_with_shape;
    j["scale_corners_with_shape"] = l.scale_corners_with_shape;
    j["size"]          = vec2_aprop_to_json(l.size);
    j["origin"]        = vec2_aprop_to_json(l.origin_prop);
    j["origin_x"]      = l.origin_x;
    j["origin_y"]      = l.origin_y;
    j["shadow_enabled"] = l.shadow_enabled;
    j["shadow_color"] = l.shadow_color;
    j["shadow_opacity"] = l.shadow_opacity;
    j["shadow_distance"] = l.shadow_distance;
    j["shadow_angle"] = l.shadow_angle;
    j["shadow_blur"] = l.shadow_blur;
    j["shadow_spread"] = l.shadow_spread;
    j["shadow_blur_type"] = (int)l.shadow_blur_type;
    j["long_shadow_enabled"] = l.long_shadow_enabled;
    j["long_shadow_color"] = l.long_shadow_color;
    j["long_shadow_opacity"] = l.long_shadow_opacity;
    j["long_shadow_length"] = l.long_shadow_length;
    j["long_shadow_angle"] = l.long_shadow_angle;
    j["long_shadow_falloff"] = l.long_shadow_falloff;
    j["long_shadow_blur_type"] = (int)l.long_shadow_blur_type;
    j["long_shadow_blur"] = l.long_shadow_blur;
    j["shadow_enabled_prop"] = aprop_to_json(l.shadow_enabled_prop);
    j["shadow_opacity_prop"] = aprop_to_json(l.shadow_opacity_prop);
    j["shadow_distance_prop"] = aprop_to_json(l.shadow_distance_prop);
    j["shadow_angle_prop"] = aprop_to_json(l.shadow_angle_prop);
    j["shadow_blur_prop"] = aprop_to_json(l.shadow_blur_prop);
    j["shadow_spread_prop"] = aprop_to_json(l.shadow_spread_prop);
    j["shadow_color_a"] = aprop_to_json(l.shadow_color_a);
    j["shadow_color_r"] = aprop_to_json(l.shadow_color_r);
    j["shadow_color_g"] = aprop_to_json(l.shadow_color_g);
    j["shadow_color_b"] = aprop_to_json(l.shadow_color_b);
    j["text_color_a"]  = aprop_to_json(l.text_color_a);
    j["text_color_r"]  = aprop_to_json(l.text_color_r);
    j["text_color_g"]  = aprop_to_json(l.text_color_g);
    j["text_color_b"]  = aprop_to_json(l.text_color_b);
    j["fill_color_a"]  = aprop_to_json(l.fill_color_a);
    j["fill_color_r"]  = aprop_to_json(l.fill_color_r);
    j["fill_color_g"]  = aprop_to_json(l.fill_color_g);
    j["fill_color_b"]  = aprop_to_json(l.fill_color_b);
    j["stroke_color_a"] = aprop_to_json(l.stroke_color_a);
    j["stroke_color_r"] = aprop_to_json(l.stroke_color_r);
    j["stroke_color_g"] = aprop_to_json(l.stroke_color_g);
    j["stroke_color_b"] = aprop_to_json(l.stroke_color_b);
    j["image_path"]    = l.image_path;
    j["scale_filter"]  = (int)l.scale_filter;
    j["image_box_lock_aspect_ratio"] = l.image_box_lock_aspect_ratio;
    j["image_box_mode"] = (int)l.image_box_mode;
    j["image_size_auto_fit"] = l.image_size_auto_fit;
    j["image_crop_when_outside_box"] = l.image_crop_when_outside_box;
    j["image_anchor_x"] = l.image_anchor_x;
    j["image_anchor_y"] = l.image_anchor_y;
    j["image_width"] = l.image_width;
    j["image_height"] = l.image_height;
    j["image_size"] = vec2_aprop_to_json(l.image_size);
    /* Embedded template payloads are regenerated explicitly and must not leak
     * into render fingerprints or normal scene-collection saves. */
    j.erase("embedded_image");
    if (include_embedded_assets && !attach_embedded_image_asset(l, j, require_embedded_assets, error)) {
        if (asset_embed_failed)
            *asset_embed_failed = true;
    }
    j["lock_aspect_ratio"] = l.lock_aspect_ratio;
    json merged = merge_surviving_passthrough(source_passthrough, j);
    merged["audio_effects"] = j["audio_effects"];
    merged["effects"] = j["effects"];
    merged["transitions"] = j["transitions"];
    return merged;
}

std::string layer_render_fingerprint(const Layer &layer)
{
    json j = layer_to_json(layer, false, false, nullptr, nullptr, false);
    static constexpr const char *kCompositorOnlyKeys[] = {
        "id", "name", "visible", "locked", "properties_expanded",
        "group_collapsed", "custom_ui_color_enabled", "custom_ui_color",
        "parent_id", "transform_parent_id", "parent_bind_enabled", "parent_bind_matrix", "mask_source_id", "mask_mode",
        "matte_visibility_mode", "blend_mode",
        "use_as_scene_mask", "effect_stack_respects_masks",
        "in_time", "out_time", "position", "scale", "scale_lock",
        "rotation", "opacity",
        /* Every 2D/3D transform channel is compositor state.  Leaving the
         * unified XYZ track, the free-transform quad, or the 3D compatibility
         * mirrors in this fingerprint made a Z-only edit look like new pixel
         * content.  The editor then regenerated text/vector rasters during
         * every gizmo tick instead of moving the resident texture. */
        "transform_quad", "transform_quad_tl", "transform_quad_tr",
        "transform_quad_br", "transform_quad_bl",
        "dimension_mode", "transform_axis_space", "position_z",
        "position_3d_path_enabled", "position_3d", "rotation_x",
        "rotation_y", "scale_z", "anchor_z", "orientation_x",
        "orientation_y", "orientation_z", "camera_id",
        "camera_assignment", "depth_mode", "depth_test",
        "write_to_depth", "double_sided", "backface_culling",
        "expose_text", "exposed_hide_if_empty",
        "exposed_single_value", "expose_fill_color", "exposed_fill_single_value",
        "expose_stroke_color", "exposed_stroke_single_value", "ignore_persistence"
    };
    for (const char *key : kCompositorOnlyKeys)
        j.erase(key);

    /* General transitions are implemented as GPU matrices/opacity. Text
     * transitions remain in the fingerprint because they alter glyph coverage. */
    if (j.contains("transitions") && j["transitions"].is_array()) {
        json raster_transitions = json::array();
        for (const auto &transition : j["transitions"]) {
            if (!transition.is_object() ||
                json_int(transition, "kind", 0) !=
                    static_cast<int>(LayerTransitionKind::General))
                raster_transitions.push_back(transition);
        }
        j["transitions"] = std::move(raster_transitions);
    }

    const std::string serialized = j.dump();
    return std::to_string(std::hash<std::string>{}(serialized));
}

static void migrate_and_validate_extension_state(
    LayerEffect &effect, const std::string &layer_name,
    TitleImportDiagnostics *diagnostics)
{
    if (effect.extension_id.empty())
        return;
    auto &catalog = FxmEffectExtensionCatalog::instance();
    if (catalog.effects().empty())
        catalog.reload();
    const auto *definition = catalog.find(QString::fromStdString(effect.extension_id));
    if (!definition) {
        /* Development Version 244: a missing external plugin is represented as
         * an inert placeholder. The effect id, parameters, keyframes, preset id
         * and binary state remain serialized so reinstalling the plugin can
         * restore the effect without data loss. */
        const bool builtin_id = FxmEffectExtensionCatalog::builtInTypeForId(
            QString::fromStdString(effect.extension_id), nullptr);
        if (!builtin_id) {
            effect.missing_plugin_placeholder = true;
            if (diagnostics) {
                append_unique_import_diagnostic(
                    diagnostics->missing_effects,
                    layer_name + ": " + effect.extension_id + " (plugin missing; state preserved)");
            }
        }
        return;
    }
    if (definition->builtIn)
        return;

    effect.missing_plugin_placeholder = false;
    if (effect.extension_provider_id.empty())
        effect.extension_provider_id = definition->providerId.toStdString();
    if (effect.extension_provider_version.empty())
        effect.extension_provider_version = definition->providerVersion.toStdString();

    const uint32_t stored_version = std::max<uint32_t>(1u, effect.extension_schema_version);
    if (stored_version < definition->schemaVersion && definition->migrateState) {
        try {
            const char *migrated = definition->migrateState(
                effect.extension_id.c_str(), stored_version,
                effect.extension_parameters_json.c_str());
            if (migrated) {
                const size_t length = std::char_traits<char>::length(migrated);
                if (length <= 1024u * 1024u) {
                    const std::string candidate(migrated, length);
                    const json parsed = json::parse(candidate, nullptr, false);
                    if (parsed.is_object()) {
                        effect.extension_parameters_json = candidate;
                        effect.extension_schema_version = definition->schemaVersion;
                    }
                }
                if (definition->releaseString)
                    definition->releaseString(migrated);
            }
        } catch (...) {
            if (diagnostics) {
                append_unique_import_diagnostic(
                    diagnostics->missing_effects,
                    layer_name + ": " + effect.extension_id + " (plugin migration threw an exception)");
            }
        }
    }

    if (definition->validateState) {
        try {
            char error_buffer[1024] = {};
            const int valid = definition->validateState(
                effect.extension_id.c_str(), effect.extension_parameters_json.c_str(),
                error_buffer, static_cast<uint32_t>(sizeof(error_buffer)));
            if (!valid && diagnostics) {
                const std::string detail = error_buffer[0] ? error_buffer : "invalid extension state";
                append_unique_import_diagnostic(
                    diagnostics->missing_effects,
                    layer_name + ": " + effect.extension_id + " (" + detail + ")");
            }
        } catch (...) {
            if (diagnostics) {
                append_unique_import_diagnostic(
                    diagnostics->missing_effects,
                    layer_name + ": " + effect.extension_id + " (plugin validation threw an exception)");
            }
        }
    }
}

static std::shared_ptr<Layer> layer_from_json(const json &j, bool require_embedded_assets = false,
                                               std::string *error = nullptr,
                                               TitleImportDiagnostics *diagnostics = nullptr)
{
    auto l = std::make_shared<Layer>();
    if (!j.is_object())
        return l;

    {
        json layer_passthrough = j;
        layer_passthrough.erase("embedded_image");
        l->serialization_passthrough_json = layer_passthrough.dump();
    }
    l->id       = bounded_string(j, "id", "", kMaxNameLength);
    l->name     = bounded_string(j, "name", "Layer", kMaxNameLength);
    l->type     = (LayerType)std::clamp(json_int(j, "type", 0), 0, (int)LayerType::Chart);
    l->visible  = json_bool(j, "visible", true);
    l->locked   = json_bool(j, "locked", false);
    l->properties_expanded = json_bool(j, "properties_expanded", false);
    l->group_collapsed = json_bool(j, "group_collapsed", false);
    l->custom_ui_color_enabled = json_bool(
        j, "custom_ui_color_enabled", false);
    l->custom_ui_color = json_color(
        j, "custom_ui_color", (uint32_t)0xFF4C6EF5u) | 0xFF000000u;
    l->transition_input_slot = std::clamp(json_int(j, "transition_input_slot", -1), -1, 1);
    l->transition_input_required = json_bool(j, "transition_input_required", false);
    l->parent_id = bounded_string(j, "parent_id", "", kMaxNameLength);
    l->transform_parent_id = bounded_string(j, "transform_parent_id", "", kMaxNameLength);
    l->parent_bind_enabled = json_bool(j, "parent_bind_enabled", false);
    if (l->parent_bind_enabled && j.contains("parent_bind_matrix") &&
        j["parent_bind_matrix"].is_array() &&
        j["parent_bind_matrix"].size() == l->parent_bind_matrix.size()) {
        bool valid_bind = true;
        for (std::size_t index = 0; index < l->parent_bind_matrix.size(); ++index) {
            const auto &item = j["parent_bind_matrix"][index];
            if (!item.is_number() || !std::isfinite(item.get<double>())) {
                valid_bind = false;
                break;
            }
            l->parent_bind_matrix[index] = item.get<double>();
        }
        if (!valid_bind) {
            l->parent_bind_enabled = false;
            l->parent_bind_matrix = {
                1.0, 0.0, 0.0, 0.0,
                0.0, 1.0, 0.0, 0.0,
                0.0, 0.0, 1.0, 0.0,
                0.0, 0.0, 0.0, 1.0
            };
        }
    } else if (l->parent_bind_enabled) {
        l->parent_bind_enabled = false;
    }
    l->asset_title_id = bounded_string(j, "asset_title_id", "", kMaxNameLength);
    l->asset_owner_id = bounded_string(j, "asset_owner_id", "", kMaxNameLength);
    l->asset_source_layer_id = bounded_string(j, "asset_source_layer_id", "", kMaxNameLength);
    l->asset_category = bounded_string(j, "asset_category", "Default", kMaxNameLength);
    l->asset_animated = json_bool(j, "asset_animated", false);
    l->asset_playback_mode = std::clamp(json_int(j, "asset_playback_mode", 0), 0, 1);
    l->asset_playback_offset = std::clamp(finite_or(json_double(j, "asset_playback_offset", 0.0), 0.0), -kMaxDuration, kMaxDuration);
    l->asset_duration = std::clamp(finite_or(json_double(j, "asset_duration", 5.0), 5.0), 0.1, kMaxDuration);
    l->asset_source_playback_mode = std::clamp(json_int(j, "asset_source_playback_mode", 0), 0, 2);
    l->asset_source_loop_type = std::clamp(json_int(j, "asset_source_loop_type", 0), 0, 1);
    l->asset_source_loop_start = std::clamp(finite_or(json_double(j, "asset_source_loop_start", 1.0), 1.0), 0.0, l->asset_duration);
    l->asset_source_loop_end = std::clamp(finite_or(json_double(j, "asset_source_loop_end", 4.0), 4.0), l->asset_source_loop_start, l->asset_duration);
    l->asset_source_pause_time = std::clamp(finite_or(json_double(j, "asset_source_pause_time", 0.0), 0.0), 0.0, l->asset_duration);
    l->asset_pause_duration = std::clamp(finite_or(json_double(j, "asset_pause_duration", 1.0), 1.0), 0.0, kMaxDuration);
    l->asset_loop_count = std::clamp(json_int(j, "asset_loop_count", 1), 1, 1000000);
    l->asset_loop = json_bool(j, "asset_loop", false);
    l->asset_isolated_3d_space = json_bool(j, "asset_isolated_3d_space", false);
    l->asset_space_width = std::clamp(json_int(j, "asset_space_width", 0), 0, 1000000);
    l->asset_space_height = std::clamp(json_int(j, "asset_space_height", 0), 0, 1000000);
    l->asset_space_center_x = std::clamp(finite_or(json_double(j, "asset_space_center_x", 0.0), 0.0), -1000000000.0, 1000000000.0);
    l->asset_space_center_y = std::clamp(finite_or(json_double(j, "asset_space_center_y", 0.0), 0.0), -1000000000.0, 1000000000.0);
    l->asset_camera_uses_owner_time = json_bool(
        j, "asset_camera_uses_owner_time", false);
    l->audio_source = bounded_string(j, "audio_source", "", kMaxPathLength);
    l->audio_stream_index = std::clamp(json_int(j, "audio_stream_index", -1), -1, 1024);
    l->audio_in_point = std::clamp(finite_or(json_double(j, "audio_in_point", 0.0), 0.0), 0.0, kMaxDuration);
    l->audio_out_point = std::clamp(finite_or(json_double(j, "audio_out_point", 0.0), 0.0), 0.0, kMaxDuration);
    l->audio_volume = (float)std::clamp(finite_or(json_double(j, "audio_volume", 1.0), 1.0), 0.0, 4.0);
    l->audio_pan = (float)std::clamp(finite_or(json_double(j, "audio_pan", 0.0), 0.0), -1.0, 1.0);
    l->audio_volume_prop = j.contains("audio_volume_prop")
        ? aprop_from_json(j["audio_volume_prop"], "audio_volume")
        : AnimatedProperty{"audio_volume", l->audio_volume};
    l->audio_pan_prop = j.contains("audio_pan_prop")
        ? aprop_from_json(j["audio_pan_prop"], "audio_pan")
        : AnimatedProperty{"audio_pan", l->audio_pan};
    l->audio_muted = json_bool(j, "audio_muted", false);
    l->audio_solo = json_bool(j, "audio_solo", false);
    l->audio_fade_in = std::clamp(finite_or(json_double(j, "audio_fade_in", 0.0), 0.0), 0.0, kMaxDuration);
    l->audio_fade_out = std::clamp(finite_or(json_double(j, "audio_fade_out", 0.0), 0.0), 0.0, kMaxDuration);
    l->audio_fade_curve = (AudioFadeCurve)std::clamp(json_int(j, "audio_fade_curve", 0), 0, 2);
    if (j.contains("audio_effects") && j["audio_effects"].is_array()) {
        const size_t count = std::min<size_t>(j["audio_effects"].size(), 64);
        for (size_t i = 0; i < count; ++i) {
            const auto &a = j["audio_effects"][i];
            if (!a.is_object()) continue;
            AudioEffect fx;
            fx.serialization_passthrough_json = a.dump();
            fx.type = (AudioEffectType)std::clamp(json_int(a, "type", 0), 0, 4);
            fx.enabled = json_bool(a, "enabled", true);
            fx.gain_db = (float)std::clamp(finite_or(json_double(a, "gain_db", 0.0), 0.0), -96.0, 24.0);
            fx.frequency_hz = (float)std::clamp(finite_or(json_double(a, "frequency_hz", 120.0), 120.0), 10.0, 24000.0);
            fx.threshold_db = (float)std::clamp(finite_or(json_double(a, "threshold_db", -6.0), -6.0), -60.0, 0.0);
            fx.ratio = (float)std::clamp(finite_or(json_double(a, "ratio", 4.0), 4.0), 1.0, 100.0);
            fx.attack_ms = (float)std::clamp(finite_or(json_double(a, "attack_ms", 5.0), 5.0), 0.1, 1000.0);
            fx.release_ms = (float)std::clamp(finite_or(json_double(a, "release_ms", 80.0), 80.0), 1.0, 5000.0);
            fx.makeup_db = (float)std::clamp(finite_or(json_double(a, "makeup_db", 0.0), 0.0), -24.0, 24.0);
            fx.fade_in = std::clamp(finite_or(json_double(a, "fade_in", 0.0), 0.0), 0.0, kMaxDuration);
            fx.fade_out = std::clamp(finite_or(json_double(a, "fade_out", 0.0), 0.0), 0.0, kMaxDuration);
            fx.fade_curve = (AudioFadeCurve)std::clamp(json_int(a, "fade_curve", 0), 0, 2);
            l->audio_effects.push_back(fx);
        }
    }
    l->audio_loop = json_bool(j, "audio_loop", false);
    l->audio_playback_mode = (AudioPlaybackMode)std::clamp(json_int(j, "audio_playback_mode", 0), 0, 2);
    l->audio_independent = json_bool(j, "audio_independent", false);
    l->audio_media_duration = std::clamp(finite_or(json_double(j, "audio_media_duration", 0.0), 0.0), 0.0, kMaxDuration);
    l->audio_sample_rate = std::clamp(json_int(j, "audio_sample_rate", 0), 0, 768000);
    l->audio_channels = std::clamp(json_int(j, "audio_channels", 0), 0, 64);
    l->audio_waveform_duration = std::max(0.0, json_double(j, "audio_waveform_duration", 0.0));
    l->audio_waveform_progress_percent = std::clamp(json_int(j, "audio_waveform_progress_percent", 0), 0, 100);
    l->audio_waveform_generating = json_bool(j, "audio_waveform_generating", false);
    l->audio_waveform_progress_label = bounded_string(j, "audio_waveform_progress_label", "", kMaxNameLength);
    if (j.contains("audio_waveform") && j["audio_waveform"].is_array()) {
        const size_t count = std::min<size_t>(j["audio_waveform"].size(), 8192);
        l->audio_waveform.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (j["audio_waveform"][i].is_number())
                l->audio_waveform.push_back((float)std::clamp(j["audio_waveform"][i].get<double>(), -1.0, 1.0));
        }
    }
    l->linked_media_layer_id = bounded_string(j, "linked_media_layer_id", "", kMaxNameLength);
    l->linked_media_stream = json_bool(j, "linked_media_stream", false);
    l->media_stream_label = bounded_string(j, "media_stream_label", "", kMaxNameLength);
    l->video_source = bounded_string(j, "video_source", "", kMaxPathLength);
    l->video_source_relative = bounded_string(j, "video_source_relative", "", kMaxPathLength);
    l->video_source_absolute = bounded_string(j, "video_source_absolute", "", kMaxPathLength);
    l->video_media_root = bounded_string(j, "video_media_root", "", kMaxPathLength);
    if (l->video_source_relative.empty() && !l->video_source.empty()) {
        QFileInfo source_info(QString::fromStdString(l->video_source));
        if (source_info.isRelative())
            l->video_source_relative = l->video_source;
        else
            l->video_source_absolute = l->video_source;
    }
    if (l->video_source.empty())
        l->video_source = !l->video_source_absolute.empty() ? l->video_source_absolute : l->video_source_relative;
    l->video_stream_index = json_int(j, "video_stream_index", -1);
    l->video_audio_stream_index = std::clamp(json_int(j, "video_audio_stream_index", -1), -1, 1024);
    l->video_selected_streams_json = bounded_json_payload(j, "video_selected_streams", "{}", 1048576);
    l->video_in_point = std::clamp(finite_or(json_double(j, "video_in_point", 0.0), 0.0), 0.0, kMaxDuration);
    l->video_out_point = std::clamp(finite_or(json_double(j, "video_out_point", 0.0), 0.0), 0.0, kMaxDuration);
    l->video_loop = json_bool(j, "video_loop", false);
    l->video_playback_mode = std::clamp(json_int(j, "video_playback_mode", 0), 0, 1);
    l->video_media_duration = std::clamp(finite_or(json_double(j, "video_media_duration", 0.0), 0.0), 0.0, kMaxDuration);
    l->video_frame_rate = std::clamp(finite_or(json_double(j, "video_frame_rate", 0.0), 0.0), 0.0, 1000.0);
    l->video_pixel_width = std::clamp(json_int(j, "video_pixel_width", 0), 0, 32768);
    l->video_pixel_height = std::clamp(json_int(j, "video_pixel_height", 0), 0, 32768);
    l->video_has_alpha = json_bool(j, "video_has_alpha", false);
    l->video_has_hdr = json_bool(j, "video_has_hdr", false);
    l->video_color_primaries = bounded_string(j, "video_color_primaries", "", 64);
    l->video_color_transfer = bounded_string(j, "video_color_transfer", "", 64);
    l->video_color_matrix = bounded_string(j, "video_color_matrix", "", 64);
    l->video_color_range = bounded_string(j, "video_color_range", "", 64);
    l->video_decode_settings_json = bounded_json_payload(j, "video_decode_settings", "{}", 1048576);
    l->video_prefer_hardware_decode = json_bool(j, "video_prefer_hardware_decode", true);
    l->video_allow_hardware_fallback = json_bool(j, "video_allow_hardware_fallback", true);
    l->video_decode_cache_policy = bounded_string(j, "video_decode_cache_policy", "auto", 64);
    l->video_source_fingerprint = bounded_string(j, "video_source_fingerprint", "", 512);
    l->video_proxy_path = bounded_string(j, "video_proxy_path", "", kMaxPathLength);
    l->video_proxy_fingerprint = bounded_string(j, "video_proxy_fingerprint", "", 512);
    l->video_proxy_profile = bounded_string(j, "video_proxy_profile", "", kMaxNameLength);
    l->video_proxy_complete = json_bool(j, "video_proxy_complete", false);
    l->video_proxy_alpha = json_bool(j, "video_proxy_alpha", false);
    l->video_proxy_hdr = json_bool(j, "video_proxy_hdr", false);
    l->video_proxy_audio_preserved = json_bool(j, "video_proxy_audio_preserved", false);
    l->video_proxy_progress_percent = std::clamp(json_int(j, "video_proxy_progress_percent", 0), 0, 100);
    l->video_proxy_generating = json_bool(j, "video_proxy_generating", false);
    l->video_time_remap_enabled = json_bool(j, "video_time_remap_enabled", false);
    l->video_source_time = j.contains("video_source_time")
        ? aprop_from_json(j["video_source_time"], "video_source_time")
        : AnimatedProperty{"video_source_time", l->video_in_point};
    if (!l->video_source_time.is_animated() && !j.contains("video_source_time"))
        l->video_source_time.static_value = l->video_in_point;
    if (j.contains("video_time_remap_loop_segments") &&
        j["video_time_remap_loop_segments"].is_array()) {
        const size_t count = std::min<size_t>(j["video_time_remap_loop_segments"].size(), 128);
        for (size_t i = 0; i < count; ++i) {
            const auto &item = j["video_time_remap_loop_segments"][i];
            if (!item.is_object()) continue;
            VideoTimeRemapLoopSegment segment;
            segment.timeline_start = std::clamp(finite_or(json_double(item, "timeline_start", 0.0), 0.0), 0.0, kMaxDuration);
            segment.timeline_end = std::clamp(finite_or(json_double(item, "timeline_end", 0.0), 0.0), segment.timeline_start, kMaxDuration);
            segment.source_start = std::clamp(finite_or(json_double(item, "source_start", 0.0), 0.0), 0.0, kMaxDuration);
            segment.source_end = std::clamp(finite_or(json_double(item, "source_end", 0.0), 0.0), 0.0, kMaxDuration);
            segment.enabled = json_bool(item, "enabled", true);
            if (segment.timeline_end > segment.timeline_start && segment.source_end != segment.source_start)
                l->video_time_remap_loop_segments.push_back(segment);
        }
    }
    l->video_time_remap_audio_mode = (VideoTimeRemapAudioMode)std::clamp(
        json_int(j, "video_time_remap_audio_mode", (int)VideoTimeRemapAudioMode::FollowSourceTime), 0, 2);
    l->video_frame_interpolation = (VideoFrameInterpolationMode)std::clamp(
        json_int(j, "video_frame_interpolation", (int)VideoFrameInterpolationMode::NearestFrame), 0, 2);
    l->video_optical_flow_enabled = json_bool(j, "video_optical_flow_enabled", false);
    l->video_optical_flow_analysis_running = json_bool(j, "video_optical_flow_analysis_running", false);
    l->video_optical_flow_progress_percent = std::clamp(json_int(j, "video_optical_flow_progress_percent", 0), 0, 100);
    l->video_time_remap_curve_fingerprint = bounded_string(j, "video_time_remap_curve_fingerprint", "", 512);
    l->video_optical_flow_cache_fingerprint = bounded_string(j, "video_optical_flow_cache_fingerprint", "", 512);
    l->mask_source_id = bounded_string(j, "mask_source_id", "", kMaxNameLength);
    l->mask_mode = (MaskMode)std::clamp(json_int(j, "mask_mode", 0), 0, (int)MaskMode::InvertedClipping);
    if (j.contains("matte_visibility_mode")) {
        l->matte_visibility_mode = (MatteVisibilityMode)std::clamp(
            json_int(j, "matte_visibility_mode", (int)MatteVisibilityMode::MatteOnly),
            (int)MatteVisibilityMode::HiddenInactive,
            (int)MatteVisibilityMode::VisibleAndMatte);
    } else {
        /* Legacy files used visible=false for an inactive matte and
         * visible=true for an active matte that was hidden from composition. */
        l->matte_visibility_mode = l->visible
            ? MatteVisibilityMode::MatteOnly
            : MatteVisibilityMode::HiddenInactive;
    }
    if (l->mask_source_id.empty()) l->mask_mode = MaskMode::None;
    l->blend_mode = (EffectBlendMode)std::clamp(json_int(j, "blend_mode", (int)EffectBlendMode::Normal), 0, (int)EffectBlendMode::Color);
    l->use_as_scene_mask = json_bool(j, "use_as_scene_mask", false) &&
                           layer_type_can_be_scene_mask(l->type);
    l->effect_stack_respects_masks = json_bool(j, "effect_stack_respects_masks", false);
    if (j.contains("external_bindings") && j["external_bindings"].is_array()) {
        const size_t count = std::min(j["external_bindings"].size(),
                                      kMaxExternalBindingsPerLayer);
        l->external_bindings.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ExternalPropertyBinding binding =
                external_binding_from_json(j["external_bindings"][i]);
            if (!binding.property_path.empty() && !binding.source_id.empty() &&
                !binding.field_path.empty())
                set_external_binding(*l, std::move(binding));
        }
    }
    if (l->type == LayerType::Chart && j.contains("chart") &&
        j["chart"].is_object()) {
        const json &chart = j["chart"];
        l->chart_serialization_passthrough_json = chart.dump();
        l->chart_type = (ChartType)std::clamp(
            json_int(chart, "type", (int)ChartType::Bar),
            (int)ChartType::Bar, (int)ChartType::Scatter);
        l->chart_data_mode = (ChartDataMode)std::clamp(
            json_int(chart, "data_mode", (int)ChartDataMode::ExternalData),
            (int)ChartDataMode::StaticData, (int)ChartDataMode::ExternalData);
        if (chart.contains("binding") && chart["binding"].is_object()) {
            const json &binding = chart["binding"];
            l->chart_binding.serialization_passthrough_json = binding.dump();
            l->chart_binding.source_id = bounded_string(
                binding, "source_id", "", kMaxNameLength);
            l->chart_binding.table_path = bounded_string(
                binding, "table_path", "", kMaxNameLength);
            l->chart_binding.category_field = bounded_string(
                binding, "category_field", "name", kMaxNameLength);
            l->chart_binding.series_field = bounded_string(
                binding, "series_field", "", kMaxNameLength);
            l->chart_binding.secondary_value_field = bounded_string(
                binding, "secondary_value_field", "", kMaxNameLength);
            l->chart_binding.metadata_field = bounded_string(
                binding, "metadata_field", "", kMaxNameLength);
            l->chart_binding.color_field = bounded_string(
                binding, "color_field", "", kMaxNameLength);
            l->chart_binding.start_row = std::clamp(
                json_int(binding, "start_row", 0), 0, 1000000);
            l->chart_binding.maximum_rows = std::clamp(
                json_int(binding, "maximum_rows", 0), 0, 1000000);
            l->chart_binding.ignore_empty_rows = json_bool(
                binding, "ignore_empty_rows", true);
        }
        l->chart_background_enabled = json_bool(chart, "background_enabled", false);
        l->chart_grid_enabled = json_bool(chart, "grid_enabled", true);
        l->chart_axes_enabled = json_bool(chart, "axes_enabled", true);
        l->chart_axis_labels_enabled = json_bool(chart, "axis_labels_enabled", true);
        l->chart_data_labels_enabled = json_bool(chart, "data_labels_enabled", false);
        l->chart_legend_enabled = json_bool(chart, "legend_enabled", false);
        l->chart_fill_enabled = json_bool(chart, "fill_enabled", true);
        l->chart_outline_enabled = json_bool(chart, "outline_enabled", true);
        l->chart_background_color = json_color(chart, "background_color", 0x00000000u);
        l->chart_grid_color = json_color(chart, "grid_color", 0x40FFFFFFu);
        l->chart_axis_color = json_color(chart, "axis_color", 0xBFFFFFFFu);
        l->chart_label_color = json_color(chart, "label_color", 0xFFFFFFFFu);
        l->chart_font_family = bounded_string(
            chart, "font_family", "Helvetica Neue", kMaxNameLength);
        l->chart_number_prefix = bounded_string(
            chart, "number_prefix", "", kMaxNameLength);
        l->chart_number_suffix = bounded_string(
            chart, "number_suffix", "", kMaxNameLength);
        l->chart_thousands_separator = json_bool(chart, "thousands_separator", false);
        l->chart_decimal_precision = std::clamp(
            json_int(chart, "decimal_precision", 0), 0, 12);
        l->chart_data_transition_seconds = std::clamp(finite_or(
            json_double(chart, "data_transition_seconds", 0.35), 0.35),
            0.0, 60.0);
        auto load_chart_property = [&](const char *key, AnimatedProperty &property,
                                       double minimum, double maximum) {
            if (chart.contains(key))
                property = aprop_from_json(chart[key], property.name);
            property.static_value = std::clamp(
                finite_or(property.static_value, minimum), minimum, maximum);
            for (Keyframe &keyframe : property.keyframes)
                keyframe.value = std::clamp(
                    finite_or(keyframe.value, minimum), minimum, maximum);
        };
        load_chart_property("animation_progress", l->chart_animation_progress, 0.0, 1.0);
        load_chart_property("reveal_progress", l->chart_reveal_progress, 0.0, 1.0);
        load_chart_property("value_transition", l->chart_value_transition, 0.0, 1.0);
        load_chart_property("label_opacity", l->chart_label_opacity, 0.0, 1.0);
        load_chart_property("element_opacity", l->chart_element_opacity, 0.0, 1.0);
        load_chart_property("font_size", l->chart_font_size, 6.0, 512.0);
        load_chart_property("line_thickness", l->chart_line_thickness, 0.0, 256.0);
        load_chart_property("bar_width", l->chart_bar_width, 0.05, 1.0);
        load_chart_property("bar_spacing", l->chart_bar_spacing, 0.0, 0.9);
        load_chart_property("corner_radius", l->chart_corner_radius, 0.0, 10000.0);
        load_chart_property("padding", l->chart_padding, 0.0, 10000.0);
        load_chart_property("margin_left", l->chart_margin_left, 0.0, 10000.0);
        load_chart_property("margin_top", l->chart_margin_top, 0.0, 10000.0);
        load_chart_property("margin_right", l->chart_margin_right, 0.0, 10000.0);
        load_chart_property("margin_bottom", l->chart_margin_bottom, 0.0, 10000.0);
        if (chart.contains("series") && chart["series"].is_array()) {
            const size_t count = std::min<size_t>(chart["series"].size(), 64);
            l->chart_series.reserve(count);
            for (size_t index = 0; index < count; ++index) {
                const json &item_json = chart["series"][index];
                if (!item_json.is_object())
                    continue;
                ChartSeries item;
                item.serialization_passthrough_json = item_json.dump();
                item.id = bounded_string(item_json, "id", "", kMaxNameLength);
                item.name = bounded_string(item_json, "name", "Value", kMaxNameLength);
                item.value_field = bounded_string(item_json, "value_field", "value", kMaxNameLength);
                item.secondary_value_field = bounded_string(item_json, "secondary_value_field", "", kMaxNameLength);
                item.metadata_field = bounded_string(item_json, "metadata_field", "", kMaxNameLength);
                item.color = json_color(item_json, "color", 0xFF4C8DFFu);
                item.fill_color = json_color(item_json, "fill_color", 0x804C8DFFu);
                item.visible = json_bool(item_json, "visible", true);
                item.fill_enabled = json_bool(item_json, "fill_enabled", true);
                item.outline_enabled = json_bool(item_json, "outline_enabled", true);
                item.line_thickness = std::clamp(finite_or(
                    json_double(item_json, "line_thickness", -1.0), -1.0),
                    -1.0, 256.0);
                if (item.id.empty())
                    item.id = item.name.empty() ? std::to_string(index + 1)
                                                : item.name;
                l->chart_series.push_back(std::move(item));
            }
        }
        if (chart.contains("values") && chart["values"].is_array()) {
            const size_t count = std::min<size_t>(chart["values"].size(), 4096);
            l->chart_values.reserve(count);
            for (size_t index = 0; index < count; ++index) {
                const json &item_json = chart["values"][index];
                if (!item_json.is_object())
                    continue;
                ChartValue item;
                item.serialization_passthrough_json = item_json.dump();
                item.id = bounded_string(item_json, "id", "", kMaxNameLength);
                item.category = bounded_string(item_json, "category", "", kMaxNameLength);
                item.series_id = bounded_string(item_json, "series_id", "value", kMaxNameLength);
                item.value = finite_or(json_double(item_json, "value", 0.0), 0.0);
                item.secondary_value = finite_or(
                    json_double(item_json, "secondary_value", 0.0), 0.0);
                item.has_secondary_value = json_bool(
                    item_json, "has_secondary_value", false);
                item.metadata = bounded_string(item_json, "metadata", "", kMaxNameLength);
                item.color_override = json_bool(
                    item_json, "color_override", false);
                item.color = json_color(item_json, "color", 0xFF4C8DFFu);
                item.expose_in_dock = json_bool(item_json, "expose_in_dock", false);
                if (item.id.empty())
                    item.id = "value-" + std::to_string(index + 1);
                l->chart_values.push_back(std::move(item));
            }
        }
    }
    if (j.contains("effects") && j["effects"].is_array()) {
        const size_t count = std::min(j["effects"].size(), (size_t)64);
        l->effects.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto &effect_json = j["effects"][i];
            if (!effect_json.is_object()) continue;
            const int raw_effect_type = json_int(effect_json, "type", 0);
            std::string loaded_extension_id = bounded_string(effect_json, "extension_id", "", 256);
            if (loaded_extension_id.empty())
                loaded_extension_id = bounded_string(effect_json, "effect_id", "", 256);
            LayerEffectType resolved_type = LayerEffectType::BackgroundColor;
            const bool extension_is_builtin = FxmEffectExtensionCatalog::builtInTypeForId(
                QString::fromStdString(loaded_extension_id), &resolved_type);
            const bool valid_legacy_type = raw_effect_type >= 0 &&
                raw_effect_type <= (int)LayerEffectType::TrimPaths;
            if (!valid_legacy_type && loaded_extension_id.empty()) {
                if (diagnostics) {
                    append_unique_import_diagnostic(
                        diagnostics->missing_effects,
                        l->name + ": unknown effect type " + std::to_string(raw_effect_type));
                }
                continue;
            }
            LayerEffect effect;
            effect.serialization_passthrough_json = effect_json.dump();
            effect.type = extension_is_builtin ? resolved_type
                                               : (valid_legacy_type ? (LayerEffectType)raw_effect_type
                                                                    : LayerEffectType::BackgroundColor);
            effect.extension_id = loaded_extension_id.empty()
                ? FxmEffectExtensionCatalog::builtInId(effect.type).toStdString()
                : (extension_is_builtin ? FxmEffectExtensionCatalog::builtInId(resolved_type).toStdString()
                                        : loaded_extension_id);
            effect.extension_parameters_json = bounded_json_payload(effect_json, "extension_parameters", "{}", 1048576);
            effect.extension_schema_version = static_cast<uint32_t>(std::clamp(
                json_int(effect_json, "extension_schema_version", 1), 1, 65535));
            effect.extension_loaded_schema_version = static_cast<uint32_t>(std::clamp(
                json_int(effect_json, "extension_loaded_schema_version", static_cast<int>(effect.extension_schema_version)), 1, 65535));
            effect.extension_runtime_schema_version = static_cast<uint32_t>(std::clamp(
                json_int(effect_json, "extension_runtime_schema_version", static_cast<int>(effect.extension_schema_version)), 1, 65535));
            effect.extension_explicit_migration = json_bool(effect_json, "extension_explicit_migration", false) ||
                json_bool(effect_json, "explicit_migration", false);
            effect.extension_keyframes_json = bounded_json_payload(effect_json, "extension_keyframes", "{}", 1048576);
            effect.extension_provider_id = bounded_string(effect_json, "external_plugin_id", "", 256);
            effect.extension_provider_version = bounded_string(effect_json, "external_plugin_version", "", 256);
            effect.extension_plugin_binary_id = bounded_string(effect_json, "external_plugin_binary_id", "", 512);
            effect.extension_binary_state_json = bounded_json_payload(effect_json, "extension_binary_state", "{}", 1048576);
            effect.extension_binary_state_base64 = bounded_string(effect_json, "extension_binary_state_base64", "", 4 * 1048576);
            effect.missing_plugin_placeholder = json_bool(effect_json, "missing_plugin_placeholder", false);
            effect.effect_preset_id = bounded_string(effect_json, "effect_preset_id", "", 256);
            effect.effect_preset_schema_version = static_cast<uint32_t>(std::clamp(
                json_int(effect_json, "effect_preset_schema_version", 0), 0, 65535));
            migrate_and_validate_extension_state(effect, l->name, diagnostics);
            effect.enabled = json_bool(effect_json, "enabled", true);
            switch (effect.type) {
            case LayerEffectType::DropShadow:
            case LayerEffectType::LongShadow:
            case LayerEffectType::InnerShadow:
                effect.blend_mode = EffectBlendMode::Multiply;
                break;
            case LayerEffectType::ColorOverlay:
                effect.blend_mode = EffectBlendMode::Color;
                break;
            case LayerEffectType::Glow:
            case LayerEffectType::InnerGlow:
                effect.blend_mode = EffectBlendMode::Additive;
                break;
            case LayerEffectType::Blur:
                effect.effect_opacity = 1.0f;
                break;
            case LayerEffectType::MotionBlur:
                effect.effect_opacity = 1.0f;
                effect.effect_size = 180.0f;
                effect.effect_angle = 0.0f;
                break;
            default:
                effect.blend_mode = EffectBlendMode::Normal;
                break;
            }
            effect.brightness = (float)std::clamp(finite_or(json_double(effect_json, "brightness", 0.0), 0.0), -1.0, 1.0);
            effect.contrast = (float)std::clamp(finite_or(json_double(effect_json, "contrast", 1.0), 1.0), 0.0, 4.0);
            effect.saturation = (float)std::clamp(finite_or(json_double(effect_json, "saturation", 1.0), 1.0), 0.0, 4.0);
            effect.tint_color = json_color(effect_json, "tint_color", (uint32_t)0xFFFFFFFF);
            effect.tint_amount = (float)std::clamp(finite_or(json_double(effect_json, "tint_amount", 1.0), 1.0), 0.0, 1.0);
            effect.effect_color = json_color(effect_json, "effect_color", effect.tint_color);
            effect.effect_opacity = (float)std::clamp(finite_or(json_double(effect_json, "effect_opacity", effect.tint_amount), effect.tint_amount), 0.0, 1.0);
            effect.effect_size = (float)std::clamp(finite_or(json_double(effect_json, "effect_size", 16.0), 16.0), 0.0, 512.0);
            const double effect_distance_min = effect.type == LayerEffectType::DisplacementMap ? -4096.0 : 0.0;
            effect.effect_distance = (float)std::clamp(finite_or(json_double(effect_json, "effect_distance", 8.0), 8.0), effect_distance_min, 4096.0);
            const double default_effect_angle = effect.type == LayerEffectType::MotionBlur ? 0.0 : 135.0;
            effect.effect_angle = (float)finite_or(json_double(effect_json, "effect_angle", default_effect_angle), default_effect_angle);
            effect.effect_spread = (float)std::clamp(finite_or(json_double(effect_json, "effect_spread", 0.0), 0.0), 0.0, 512.0);
            effect.effect_falloff = (float)std::clamp(finite_or(json_double(effect_json, "effect_falloff", 1.0), 1.0), 0.0, 8.0);
            effect.effect_blur_type = std::clamp(json_int(effect_json, "effect_blur_type", (int)ShadowBlurType::StackFast), 0, (int)ShadowBlurType::DualKawase);
            effect.effect_samples = std::clamp(json_int(effect_json, "effect_samples", 8), 2, 64);
            effect.effect_centered = json_bool(effect_json, "effect_centered", true);
            effect.effect_outside_hard_alpha = json_bool(effect_json, "effect_outside_hard_alpha", false);
            effect.effect_outside_hard_alpha_invert = json_bool(effect_json, "effect_outside_hard_alpha_invert", false);
            effect.affect_layers_behind = json_bool(effect_json, "affect_layers_behind", false);
            effect.affect_layers_behind_invert = json_bool(effect_json, "affect_layers_behind_invert", false);
            effect.effect_source_layer_id = bounded_string(effect_json, "effect_source_layer_id", "", kMaxNameLength);
            effect.effect_source_mode = std::clamp(json_int(effect_json, "effect_source_mode", 0), 0, 1);
            effect.effect_x_channel = std::clamp(json_int(effect_json, "effect_x_channel", 0), 0, 4);
            effect.effect_y_channel = std::clamp(json_int(effect_json, "effect_y_channel", 0), 0, 4);
            effect.effect_wrap_mode = std::clamp(json_int(effect_json, "effect_wrap_mode", 0), 0, 3);
            effect.effect_mapping_space = std::clamp(json_int(effect_json, "effect_mapping_space", 0), 0, 1);
            effect.effect_alpha_aware = json_bool(effect_json, "effect_alpha_aware", true);
            effect.effect_profile = std::clamp(json_int(effect_json, "effect_profile", 0), 0, 32);
            effect.effect_animated = json_bool(effect_json, "effect_animated", false);
            effect.effect_monochrome = json_bool(effect_json, "effect_monochrome", true);
            effect.effect_invert = json_bool(effect_json, "effect_invert", false);
            effect.effect_seed = std::clamp(json_int(effect_json, "effect_seed", 1), 0, 999999);
            double effect_amount_min = 0.0;
            if (effect.type == LayerEffectType::Ripple ||
                effect.type == LayerEffectType::WaveWarp ||
                effect.type == LayerEffectType::DisplacementMap) {
                effect_amount_min = -4096.0;
            } else if (effect.type == LayerEffectType::MatteChoker) {
                effect_amount_min = -1.0;
            }
            double effect_amount_max = 10.0;
            if (effect.type == LayerEffectType::Ripple ||
                effect.type == LayerEffectType::WaveWarp ||
                effect.type == LayerEffectType::DisplacementMap) {
                effect_amount_max = 4096.0;
            } else if (effect.type == LayerEffectType::MatteChoker ||
                       effect.type == LayerEffectType::ChromaKey ||
                       effect.type == LayerEffectType::LumaKey ||
                       effect.type == LayerEffectType::ColorRange ||
                       effect.type == LayerEffectType::SpillSuppression) {
                effect_amount_max = 1.0;
            }
            effect.effect_amount = (float)std::clamp(
                finite_or(json_double(effect_json, "effect_amount", 1.0), 1.0),
                effect_amount_min, effect_amount_max);
            effect.effect_scale = (float)std::clamp(finite_or(json_double(effect_json, "effect_scale", 1.0), 1.0), 0.001, 100.0);
            effect.effect_softness = (float)std::clamp(finite_or(json_double(effect_json, "effect_softness", 0.25), 0.25), 0.0, 1.0);
            if (effect.type == LayerEffectType::ChromaKey ||
                effect.type == LayerEffectType::LumaKey ||
                effect.type == LayerEffectType::ColorRange ||
                effect.type == LayerEffectType::SpillSuppression ||
                effect.type == LayerEffectType::MatteChoker) {
                effect.effect_spread = std::clamp(effect.effect_spread, 0.0f, 1.0f);
                effect.effect_falloff = std::clamp(effect.effect_falloff, 0.0f, 1.0f);
                if (effect.type == LayerEffectType::MatteChoker)
                    effect.effect_size = std::clamp(effect.effect_size, 0.0f, 64.0f);
            }
            effect.effect_roundness = (float)std::clamp(finite_or(json_double(effect_json, "effect_roundness", 0.0), 0.0), -1.0, 1.0);
            effect.effect_speed = (float)std::clamp(finite_or(json_double(effect_json, "effect_speed", 1.0), 1.0), -100.0, 100.0);
            effect.effect_center_x = (float)std::clamp(finite_or(json_double(effect_json, "effect_center_x", 0.5), 0.5), -10.0, 10.0);
            effect.effect_center_y = (float)std::clamp(finite_or(json_double(effect_json, "effect_center_y", 0.5), 0.5), -10.0, 10.0);
            effect.effect_complexity = (float)std::clamp(finite_or(json_double(effect_json, "effect_complexity", 4.0), 4.0), 1.0, 12.0);
            effect.effect_evolution = (float)finite_or(json_double(effect_json, "effect_evolution", 0.0), 0.0);
            /* Current Noise uses pixel-space offsets, anisotropic scale and up
             * to eight octaves. Older schemas are reset to current defaults
             * below, so there is no legacy validation branch. */
            if (effect.type == LayerEffectType::Noise || effect.type == LayerEffectType::Grain ||
                effect.type == LayerEffectType::FilmDistortion || effect.type == LayerEffectType::AnalogDistortion ||
                effect.type == LayerEffectType::DigitalDistortion) {
                effect.effect_scale = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_scale", 1.0), 1.0), 0.001, 4096.0);
                effect.effect_roundness = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_roundness", 0.0), 0.0), -3.0, 3.0);
                effect.effect_center_x = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_center_x", 0.0), 0.0), -100000.0, 100000.0);
                effect.effect_center_y = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_center_y", 0.0), 0.0), -100000.0, 100000.0);
                effect.effect_complexity = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_complexity", 5.0), 5.0), 1.0, 8.0);
                effect.effect_spread = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_spread", 2.0), 2.0), 1.01, 8.0);
                effect.effect_falloff = (float)std::clamp(finite_or(
                    json_double(effect_json, "effect_falloff", 0.5), 0.5), 0.0, 1.0);
            }
            effect.effect_affect_alpha = json_bool(effect_json, "effect_affect_alpha", false);
            effect.effect_clamp_output = json_bool(effect_json, "effect_clamp_output", true);
            effect.effect_temporal_stability = json_bool(effect_json, "effect_temporal_stability", true);
            effect.effect_secondary_color = json_color(effect_json, "effect_secondary_color", 0xFF4EA3FFu);
            if (effect_json.contains("blend_mode"))
                effect.blend_mode = (EffectBlendMode)std::clamp(json_int(effect_json, "blend_mode", (int)effect.blend_mode), 0, (int)EffectBlendMode::Color);
            effect.effect_owned_style_loaded = effect_json.contains("effect_fill_type") ||
                                               effect_json.contains("enabled_prop") ||
                                               effect_json.contains("color_a");
            effect.effect_fill_type = std::clamp(json_int(effect_json, "effect_fill_type", effect.effect_fill_type), 0, 2);
            effect.effect_join_style = std::clamp(json_int(effect_json, "effect_join_style", effect.effect_join_style), 0, 2);
            effect.effect_on_front = json_bool(effect_json, "effect_on_front", effect.effect_on_front);
            effect.effect_antialias = json_bool(effect_json, "effect_antialias", effect.effect_antialias);
            effect.effect_stroke_color = json_color(effect_json, "effect_stroke_color", effect.effect_stroke_color);
            effect.effect_stroke_width = (float)std::clamp(finite_or(json_double(effect_json, "effect_stroke_width", effect.effect_stroke_width), effect.effect_stroke_width), 0.0, (double)kMaxCanvasDimension);
            effect.effect_stroke_opacity = (float)std::clamp(finite_or(json_double(effect_json, "effect_stroke_opacity", effect.effect_stroke_opacity), effect.effect_stroke_opacity), 0.0, 1.0);
            effect.effect_trim_start = (float)std::clamp(finite_or(json_double(effect_json, "effect_trim_start", 0.0), 0.0), 0.0, 100.0);
            effect.effect_trim_end = (float)std::clamp(finite_or(json_double(effect_json, "effect_trim_end", 100.0), 100.0), 0.0, 100.0);
            effect.effect_trim_offset = (float)finite_or(json_double(effect_json, "effect_trim_offset", 0.0), 0.0);
            effect.effect_stroke_offset = (float)std::clamp(finite_or(json_double(effect_json, "effect_stroke_offset", 0.0), 0.0), -16384.0, 16384.0);
            effect.effect_trim_multiple_shapes = std::clamp(json_int(effect_json, "effect_trim_multiple_shapes", 0), 0, 1);
            effect.effect_padding_left = (float)std::clamp(finite_or(json_double(effect_json, "effect_padding_left", effect.effect_padding_left), effect.effect_padding_left), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
            effect.effect_padding_right = (float)std::clamp(finite_or(json_double(effect_json, "effect_padding_right", effect.effect_padding_right), effect.effect_padding_right), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
            effect.effect_padding_top = (float)std::clamp(finite_or(json_double(effect_json, "effect_padding_top", effect.effect_padding_top), effect.effect_padding_top), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
            effect.effect_padding_bottom = (float)std::clamp(finite_or(json_double(effect_json, "effect_padding_bottom", effect.effect_padding_bottom), effect.effect_padding_bottom), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
            effect.effect_corner_radius_tl = (float)std::clamp(finite_or(json_double(effect_json, "effect_corner_radius_tl", effect.effect_corner_radius_tl), effect.effect_corner_radius_tl), 0.0, (double)kMaxCanvasDimension);
            effect.effect_corner_radius_tr = (float)std::clamp(finite_or(json_double(effect_json, "effect_corner_radius_tr", effect.effect_corner_radius_tr), effect.effect_corner_radius_tr), 0.0, (double)kMaxCanvasDimension);
            effect.effect_corner_radius_br = (float)std::clamp(finite_or(json_double(effect_json, "effect_corner_radius_br", effect.effect_corner_radius_br), effect.effect_corner_radius_br), 0.0, (double)kMaxCanvasDimension);
            effect.effect_corner_radius_bl = (float)std::clamp(finite_or(json_double(effect_json, "effect_corner_radius_bl", effect.effect_corner_radius_bl), effect.effect_corner_radius_bl), 0.0, (double)kMaxCanvasDimension);
            effect.effect_corner_type = std::clamp(json_int(effect_json, "effect_corner_type", effect.effect_corner_type), 0, 3);
            effect.effect_gradient_spread = gradient_spread_from_json(effect_json, "effect_gradient_spread",
                                                                       "effect_gradient_type",
                                                                       effect.effect_gradient_spread);
            effect.effect_gradient_type = normalize_gradient_type(json_int(effect_json, "effect_gradient_type",
                                                                           effect.effect_gradient_type));
            effect.effect_gradient_start_color = json_color(effect_json, "effect_gradient_start_color", effect.effect_gradient_start_color);
            effect.effect_gradient_end_color = json_color(effect_json, "effect_gradient_end_color", effect.effect_gradient_end_color);
            effect.effect_gradient_start_pos = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_start_pos", effect.effect_gradient_start_pos), effect.effect_gradient_start_pos), 0.0, 1.0);
            effect.effect_gradient_end_pos = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_end_pos", effect.effect_gradient_end_pos), effect.effect_gradient_end_pos), 0.0, 1.0);
            effect.effect_gradient_start_opacity = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_start_opacity", effect.effect_gradient_start_opacity), effect.effect_gradient_start_opacity), 0.0, 1.0);
            effect.effect_gradient_end_opacity = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_end_opacity", effect.effect_gradient_end_opacity), effect.effect_gradient_end_opacity), 0.0, 1.0);
            effect.effect_gradient_opacity = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_opacity", effect.effect_gradient_opacity), effect.effect_gradient_opacity), 0.0, 1.0);
            effect.effect_gradient_angle = (float)finite_or(json_double(effect_json, "effect_gradient_angle", effect.effect_gradient_angle), effect.effect_gradient_angle);
            effect.effect_gradient_center_x = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_center_x", effect.effect_gradient_center_x), effect.effect_gradient_center_x), -100.0, 100.0);
            effect.effect_gradient_center_y = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_center_y", effect.effect_gradient_center_y), effect.effect_gradient_center_y), -100.0, 100.0);
            effect.effect_gradient_scale = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_scale", effect.effect_gradient_scale), effect.effect_gradient_scale), 0.01, 100.0);
            effect.effect_gradient_focal_x = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_focal_x", effect.effect_gradient_focal_x), effect.effect_gradient_focal_x), -100.0, 100.0);
            effect.effect_gradient_focal_y = (float)std::clamp(finite_or(json_double(effect_json, "effect_gradient_focal_y", effect.effect_gradient_focal_y), effect.effect_gradient_focal_y), -100.0, 100.0);
            effect.enabled_prop.static_value = effect.enabled ? 1.0 : 0.0;
            effect.opacity_prop.static_value = effect.effect_opacity;
            effect.size_prop.static_value = effect.effect_size;
            effect.distance_prop.static_value = effect.effect_distance;
            effect.angle_prop.static_value = effect.effect_angle;
            effect.spread_prop.static_value = effect.effect_spread;
            effect.falloff_prop.static_value = effect.effect_falloff;
            effect.amount_prop.static_value = effect.effect_amount;
            effect.scale_prop.static_value = effect.effect_scale;
            effect.softness_prop.static_value = effect.effect_softness;
            effect.roundness_prop.static_value = effect.effect_roundness;
            effect.speed_prop.static_value = effect.effect_speed;
            effect.center_x_prop.static_value = effect.effect_center_x;
            effect.center_y_prop.static_value = effect.effect_center_y;
            effect.complexity_prop.static_value = effect.effect_complexity;
            effect.evolution_prop.static_value = effect.effect_evolution;
            effect.stroke_width_prop.static_value = effect.effect_stroke_width;
            effect.stroke_opacity_prop.static_value = effect.effect_stroke_opacity;
            effect.trim_start_prop.static_value = effect.effect_trim_start;
            effect.trim_end_prop.static_value = effect.effect_trim_end;
            effect.trim_offset_prop.static_value = effect.effect_trim_offset;
            effect.stroke_offset_prop.static_value = effect.effect_stroke_offset;
            effect.padding_left_prop.static_value = effect.effect_padding_left;
            effect.padding_right_prop.static_value = effect.effect_padding_right;
            effect.padding_top_prop.static_value = effect.effect_padding_top;
            effect.padding_bottom_prop.static_value = effect.effect_padding_bottom;
            effect.corner_radius_tl_prop.static_value = effect.effect_corner_radius_tl;
            effect.corner_radius_tr_prop.static_value = effect.effect_corner_radius_tr;
            effect.corner_radius_br_prop.static_value = effect.effect_corner_radius_br;
            effect.corner_radius_bl_prop.static_value = effect.effect_corner_radius_bl;
            set_argb_channels(effect.color_a, effect.color_r, effect.color_g, effect.color_b, effect.effect_color);
            set_argb_channels(effect.stroke_color_a, effect.stroke_color_r, effect.stroke_color_g, effect.stroke_color_b, effect.effect_stroke_color);
            set_argb_channels(effect.secondary_color_a, effect.secondary_color_r, effect.secondary_color_g, effect.secondary_color_b, effect.effect_secondary_color);
            effect.brightness_prop.static_value = effect.brightness;
            effect.contrast_prop.static_value = effect.contrast;
            effect.saturation_prop.static_value = effect.saturation;
            effect.gradient_start_pos_prop.static_value = effect.effect_gradient_start_pos;
            effect.gradient_end_pos_prop.static_value = effect.effect_gradient_end_pos;
            effect.gradient_start_opacity_prop.static_value = effect.effect_gradient_start_opacity;
            effect.gradient_end_opacity_prop.static_value = effect.effect_gradient_end_opacity;
            effect.gradient_angle_prop.static_value = effect.effect_gradient_angle;
            effect.gradient_center_x_prop.static_value = effect.effect_gradient_center_x;
            effect.gradient_center_y_prop.static_value = effect.effect_gradient_center_y;
            effect.gradient_scale_prop.static_value = effect.effect_gradient_scale;
            effect.gradient_focal_x_prop.static_value = effect.effect_gradient_focal_x;
            effect.gradient_focal_y_prop.static_value = effect.effect_gradient_focal_y;
            effect.gradient_opacity_prop.static_value = effect.effect_gradient_opacity;
            set_argb_channels(effect.gradient_start_color_a, effect.gradient_start_color_r,
                              effect.gradient_start_color_g, effect.gradient_start_color_b,
                              effect.effect_gradient_start_color);
            set_argb_channels(effect.gradient_end_color_a, effect.gradient_end_color_r,
                              effect.gradient_end_color_g, effect.gradient_end_color_b,
                              effect.effect_gradient_end_color);
            if (effect_json.contains("enabled_prop")) effect.enabled_prop = aprop_from_json(effect_json["enabled_prop"], "effect_enabled");
            if (effect_json.contains("brightness_prop")) effect.brightness_prop = aprop_from_json(effect_json["brightness_prop"], "effect_brightness");
            if (effect_json.contains("contrast_prop")) effect.contrast_prop = aprop_from_json(effect_json["contrast_prop"], "effect_contrast");
            if (effect_json.contains("saturation_prop")) effect.saturation_prop = aprop_from_json(effect_json["saturation_prop"], "effect_saturation");
            if (effect_json.contains("opacity_prop")) effect.opacity_prop = aprop_from_json(effect_json["opacity_prop"], "effect_opacity");
            if (effect_json.contains("size_prop")) effect.size_prop = aprop_from_json(effect_json["size_prop"], "effect_size");
            if (effect_json.contains("distance_prop")) effect.distance_prop = aprop_from_json(effect_json["distance_prop"], "effect_distance");
            if (effect_json.contains("angle_prop")) effect.angle_prop = aprop_from_json(effect_json["angle_prop"], "effect_angle");
            if (effect_json.contains("spread_prop")) effect.spread_prop = aprop_from_json(effect_json["spread_prop"], "effect_spread");
            if (effect_json.contains("falloff_prop")) effect.falloff_prop = aprop_from_json(effect_json["falloff_prop"], "effect_falloff");
            if (effect_json.contains("amount_prop")) effect.amount_prop = aprop_from_json(effect_json["amount_prop"], "effect_amount");
            if (effect_json.contains("scale_prop")) effect.scale_prop = aprop_from_json(effect_json["scale_prop"], "effect_scale");
            if (effect_json.contains("softness_prop")) effect.softness_prop = aprop_from_json(effect_json["softness_prop"], "effect_softness");
            if (effect_json.contains("roundness_prop")) effect.roundness_prop = aprop_from_json(effect_json["roundness_prop"], "effect_roundness");
            if (effect_json.contains("speed_prop")) effect.speed_prop = aprop_from_json(effect_json["speed_prop"], "effect_speed");
            if (effect_json.contains("center_x_prop")) effect.center_x_prop = aprop_from_json(effect_json["center_x_prop"], "effect_center_x");
            if (effect_json.contains("center_y_prop")) effect.center_y_prop = aprop_from_json(effect_json["center_y_prop"], "effect_center_y");
            if (effect_json.contains("complexity_prop")) effect.complexity_prop = aprop_from_json(effect_json["complexity_prop"], "effect_complexity");
            if (effect_json.contains("evolution_prop")) effect.evolution_prop = aprop_from_json(effect_json["evolution_prop"], "effect_evolution");
            if (effect_json.contains("stroke_width_prop")) effect.stroke_width_prop = aprop_from_json(effect_json["stroke_width_prop"], "effect_stroke_width");
            if (effect_json.contains("stroke_opacity_prop")) effect.stroke_opacity_prop = aprop_from_json(effect_json["stroke_opacity_prop"], "effect_stroke_opacity");
            if (effect_json.contains("trim_start_prop")) effect.trim_start_prop = aprop_from_json(effect_json["trim_start_prop"], "effect_trim_start");
            if (effect_json.contains("trim_end_prop")) effect.trim_end_prop = aprop_from_json(effect_json["trim_end_prop"], "effect_trim_end");
            if (effect_json.contains("trim_offset_prop")) effect.trim_offset_prop = aprop_from_json(effect_json["trim_offset_prop"], "effect_trim_offset");
            if (effect_json.contains("stroke_offset_prop")) effect.stroke_offset_prop = aprop_from_json(effect_json["stroke_offset_prop"], "effect_stroke_offset");
            if (effect_json.contains("padding_left_prop")) effect.padding_left_prop = aprop_from_json(effect_json["padding_left_prop"], "effect_padding_left");
            if (effect_json.contains("padding_right_prop")) effect.padding_right_prop = aprop_from_json(effect_json["padding_right_prop"], "effect_padding_right");
            if (effect_json.contains("padding_top_prop")) effect.padding_top_prop = aprop_from_json(effect_json["padding_top_prop"], "effect_padding_top");
            if (effect_json.contains("padding_bottom_prop")) effect.padding_bottom_prop = aprop_from_json(effect_json["padding_bottom_prop"], "effect_padding_bottom");
            if (effect_json.contains("corner_radius_tl_prop")) effect.corner_radius_tl_prop = aprop_from_json(effect_json["corner_radius_tl_prop"], "effect_corner_radius_tl");
            if (effect_json.contains("corner_radius_tr_prop")) effect.corner_radius_tr_prop = aprop_from_json(effect_json["corner_radius_tr_prop"], "effect_corner_radius_tr");
            if (effect_json.contains("corner_radius_br_prop")) effect.corner_radius_br_prop = aprop_from_json(effect_json["corner_radius_br_prop"], "effect_corner_radius_br");
            if (effect_json.contains("corner_radius_bl_prop")) effect.corner_radius_bl_prop = aprop_from_json(effect_json["corner_radius_bl_prop"], "effect_corner_radius_bl");
            if (effect_json.contains("gradient_start_pos_prop")) effect.gradient_start_pos_prop = aprop_from_json(effect_json["gradient_start_pos_prop"], "effect_gradient_start_pos");
            if (effect_json.contains("gradient_end_pos_prop")) effect.gradient_end_pos_prop = aprop_from_json(effect_json["gradient_end_pos_prop"], "effect_gradient_end_pos");
            if (effect_json.contains("gradient_start_opacity_prop")) effect.gradient_start_opacity_prop = aprop_from_json(effect_json["gradient_start_opacity_prop"], "effect_gradient_start_opacity");
            if (effect_json.contains("gradient_end_opacity_prop")) effect.gradient_end_opacity_prop = aprop_from_json(effect_json["gradient_end_opacity_prop"], "effect_gradient_end_opacity");
            if (effect_json.contains("gradient_angle_prop")) effect.gradient_angle_prop = aprop_from_json(effect_json["gradient_angle_prop"], "effect_gradient_angle");
            if (effect_json.contains("gradient_center_x_prop")) effect.gradient_center_x_prop = aprop_from_json(effect_json["gradient_center_x_prop"], "effect_gradient_center_x");
            if (effect_json.contains("gradient_center_y_prop")) effect.gradient_center_y_prop = aprop_from_json(effect_json["gradient_center_y_prop"], "effect_gradient_center_y");
            if (effect_json.contains("gradient_scale_prop")) effect.gradient_scale_prop = aprop_from_json(effect_json["gradient_scale_prop"], "effect_gradient_scale");
            if (effect_json.contains("gradient_focal_x_prop")) effect.gradient_focal_x_prop = aprop_from_json(effect_json["gradient_focal_x_prop"], "effect_gradient_focal_x");
            if (effect_json.contains("gradient_focal_y_prop")) effect.gradient_focal_y_prop = aprop_from_json(effect_json["gradient_focal_y_prop"], "effect_gradient_focal_y");
            if (effect_json.contains("gradient_opacity_prop")) effect.gradient_opacity_prop = aprop_from_json(effect_json["gradient_opacity_prop"], "effect_gradient_opacity");
            if (effect_json.contains("gradient_start_color_a")) effect.gradient_start_color_a = aprop_from_json(effect_json["gradient_start_color_a"], "effect_gradient_start_color_a");
            if (effect_json.contains("gradient_start_color_r")) effect.gradient_start_color_r = aprop_from_json(effect_json["gradient_start_color_r"], "effect_gradient_start_color_r");
            if (effect_json.contains("gradient_start_color_g")) effect.gradient_start_color_g = aprop_from_json(effect_json["gradient_start_color_g"], "effect_gradient_start_color_g");
            if (effect_json.contains("gradient_start_color_b")) effect.gradient_start_color_b = aprop_from_json(effect_json["gradient_start_color_b"], "effect_gradient_start_color_b");
            if (effect_json.contains("gradient_end_color_a")) effect.gradient_end_color_a = aprop_from_json(effect_json["gradient_end_color_a"], "effect_gradient_end_color_a");
            if (effect_json.contains("gradient_end_color_r")) effect.gradient_end_color_r = aprop_from_json(effect_json["gradient_end_color_r"], "effect_gradient_end_color_r");
            if (effect_json.contains("gradient_end_color_g")) effect.gradient_end_color_g = aprop_from_json(effect_json["gradient_end_color_g"], "effect_gradient_end_color_g");
            if (effect_json.contains("gradient_end_color_b")) effect.gradient_end_color_b = aprop_from_json(effect_json["gradient_end_color_b"], "effect_gradient_end_color_b");
            if (effect_json.contains("color_a")) effect.color_a = aprop_from_json(effect_json["color_a"], "effect_color_a");
            if (effect_json.contains("color_r")) effect.color_r = aprop_from_json(effect_json["color_r"], "effect_color_r");
            if (effect_json.contains("color_g")) effect.color_g = aprop_from_json(effect_json["color_g"], "effect_color_g");
            if (effect_json.contains("color_b")) effect.color_b = aprop_from_json(effect_json["color_b"], "effect_color_b");
            if (effect_json.contains("stroke_color_a")) effect.stroke_color_a = aprop_from_json(effect_json["stroke_color_a"], "effect_stroke_color_a");
            if (effect_json.contains("stroke_color_r")) effect.stroke_color_r = aprop_from_json(effect_json["stroke_color_r"], "effect_stroke_color_r");
            if (effect_json.contains("stroke_color_g")) effect.stroke_color_g = aprop_from_json(effect_json["stroke_color_g"], "effect_stroke_color_g");
            if (effect_json.contains("stroke_color_b")) effect.stroke_color_b = aprop_from_json(effect_json["stroke_color_b"], "effect_stroke_color_b");
            if (effect_json.contains("secondary_color_a")) effect.secondary_color_a = aprop_from_json(effect_json["secondary_color_a"], "effect_secondary_color_a");
            if (effect_json.contains("secondary_color_r")) effect.secondary_color_r = aprop_from_json(effect_json["secondary_color_r"], "effect_secondary_color_r");
            if (effect_json.contains("secondary_color_g")) effect.secondary_color_g = aprop_from_json(effect_json["secondary_color_g"], "effect_secondary_color_g");
            if (effect_json.contains("secondary_color_b")) effect.secondary_color_b = aprop_from_json(effect_json["secondary_color_b"], "effect_secondary_color_b");
            if (effect.type == LayerEffectType::ColorOverlay) {
                effect.tint_color = effect.effect_color;
                effect.tint_amount = effect.effect_opacity;
            }
            /* Development Version 244 migration audit: built-in effect schema
             * upgrades are preserve-by-default. Older Glow/Noise/etc. instances
             * keep their authored parameter/keyframe envelope and are rendered
             * through the compatibility contract unless an explicit migration
             * flag was saved by a successful user-initiated migration. */
            const bool loaded_builtin_effect = loaded_extension_id.empty() || extension_is_builtin;
            if (const EffectDescriptor *descriptor = effect_descriptor(effect.type);
                loaded_builtin_effect && descriptor) {
                effect.extension_runtime_schema_version = descriptor->schema_version;
                if (effect.extension_schema_version < descriptor->schema_version &&
                    effect.extension_explicit_migration) {
                    effect.extension_loaded_schema_version = effect.extension_schema_version;
                    effect.extension_schema_version = descriptor->schema_version;
                }
            }
            l->effects.push_back(effect);
        }
    }
    l->in_time  = std::clamp(finite_or(json_double(j, "in_time", 0.0), 0.0), 0.0, kMaxDuration);
    l->out_time = std::clamp(finite_or(json_double(j, "out_time", 5.0), 5.0), l->in_time, kMaxDuration);
    l->transitions.clear();
    if (j.contains("transitions") && j["transitions"].is_array()) {
        bool edge_seen[2] = {false, false};
        size_t accepted_count = 0;
        // Scan a small bounded prefix rather than only the first two entries.
        // Older or hand-edited files can contain a duplicate edge before a
        // valid transition for the other edge; stopping at index 2 silently
        // discarded that valid transition.
        const size_t count = std::min(j["transitions"].size(), (size_t)32);
        for (size_t i = 0; i < count && accepted_count < 2; ++i) {
            const auto &transition_json = j["transitions"][i];
            if (!transition_json.is_object()) continue;
            LayerTransition transition;
            transition.serialization_passthrough_json = transition_json.dump();
            transition.id = bounded_string(transition_json, "id", "", kMaxNameLength);
            transition.preset_id = bounded_string(transition_json, "preset_id", "", kMaxNameLength);
            transition.display_name = bounded_string(transition_json, "display_name", "Transition", kMaxNameLength);
            transition.enabled = json_bool(transition_json, "enabled", true);
            transition.kind = (LayerTransitionKind)std::clamp(json_int(transition_json, "kind", 0),
                (int)LayerTransitionKind::General, (int)LayerTransitionKind::Text);
            transition.type = (LayerTransitionType)std::clamp(json_int(transition_json, "type", 0),
                (int)LayerTransitionType::Dissolve, (int)LayerTransitionType::GradientWipe);
            transition.edge = (LayerTransitionEdge)std::clamp(json_int(transition_json, "edge", 0),
                (int)LayerTransitionEdge::In, (int)LayerTransitionEdge::Out);
            transition.unit = (LayerTransitionUnit)std::clamp(json_int(transition_json, "unit", 0),
                (int)LayerTransitionUnit::Character, (int)LayerTransitionUnit::Sentence);
            transition.direction = (LayerTransitionDirection)std::clamp(json_int(transition_json, "direction", 0),
                (int)LayerTransitionDirection::None, (int)LayerTransitionDirection::Down);
            transition.easing = (EasingType)std::clamp(json_int(transition_json, "easing", (int)EasingType::EaseInOut),
                (int)EasingType::Linear, (int)EasingType::Hold);
            const double layer_duration = std::max(1.0 / 240.0, l->out_time - l->in_time);
            transition.duration = std::clamp(finite_or(json_double(transition_json, "duration", 0.5), 0.5),
                                             1.0 / 240.0, layer_duration);
            transition.blur_amount = std::clamp(finite_or(json_double(transition_json, "blur_amount", 18.0), 18.0), 0.0, 256.0);
            transition.scale_from = std::clamp(finite_or(json_double(transition_json, "scale_from", 0.82), 0.82), -10.0, 10.0);
            transition.offset = std::clamp(finite_or(json_double(transition_json, "offset", 80.0), 80.0), 0.0, 10000.0);
            transition.stagger = std::clamp(finite_or(json_double(transition_json, "stagger", 0.35), 0.35), 0.0, 0.95);
            transition.softness = std::clamp(finite_or(json_double(transition_json, "softness", 0.0), 0.0), 0.0, 1.0);
            transition.reverse_order = json_bool(transition_json, "reverse_order", false);
            transition.text_slide_fade = json_bool(
                transition_json, "text_slide_fade", true);
            transition.text_slide_crop_to_unit_bounds = json_bool(
                transition_json, "text_slide_crop_to_unit_bounds", false);
            transition.blocks_columns = std::clamp(json_int(transition_json, "blocks_columns", 8), 1, 128);
            transition.blocks_rows = std::clamp(json_int(transition_json, "blocks_rows", 6), 1, 128);
            transition.random_seed = std::clamp(json_int(transition_json, "random_seed", 1), 0, 999999);
            transition.image_path = bounded_string(transition_json, "image_path", "", kMaxPathLength);
            transition.image_channel = std::clamp(json_int(transition_json, "image_channel", 0), 0, 4);
            transition.invert = json_bool(transition_json, "invert", false);
            transition.clockwise = json_bool(transition_json, "clockwise", true);
            transition.center_x = std::clamp(finite_or(json_double(transition_json, "center_x", 0.5), 0.5), 0.0, 1.0);
            transition.center_y = std::clamp(finite_or(json_double(transition_json, "center_y", 0.5), 0.5), 0.0, 1.0);
            transition.rotation = finite_or(json_double(transition_json, "rotation", 0.0), 0.0);
            transition.aspect = std::clamp(finite_or(json_double(transition_json, "aspect", 1.0), 1.0), 0.05, 20.0);
            transition.profile = std::clamp(json_int(transition_json, "profile", 0), 0, 16);
            const bool text_type = layer_transition_type_is_text(transition.type);
            transition.kind = text_type ? LayerTransitionKind::Text : LayerTransitionKind::General;
            const int edge_index = transition.edge == LayerTransitionEdge::Out ? 1 : 0;
            if (edge_seen[edge_index]) continue;
            edge_seen[edge_index] = true;
            l->transitions.push_back(std::move(transition));
            ++accepted_count;
        }
    }

    if (j.contains("text_animators"))
        l->text_animators = text_animator_stack_from_json(j["text_animators"]);
    std::vector<std::string> text_animator_migration_warnings;
    if (migrate_legacy_text_transitions(l->transitions, l->text_animators,
                                        l->in_time, l->out_time,
                                        &text_animator_migration_warnings)) {
        FXM_LOG_INFO("TextAnimator", QStringLiteral(
            "Migrated legacy text animation layer=%1 name=%2 animators=%3")
            .arg(QString::fromStdString(l->id), QString::fromStdString(l->name))
            .arg(static_cast<int>(l->text_animators.animators.size())));
        for (const std::string &warning : text_animator_migration_warnings) {
            FXM_LOG_WARNING("TextAnimator", QStringLiteral(
                "Legacy text animation conversion warning layer=%1 name=%2 %3")
                .arg(QString::fromStdString(l->id),
                     QString::fromStdString(l->name),
                     QString::fromStdString(warning)));
        }
    }

    if (j.contains("position")) vec2_aprop_from_json(j["position"], l->position);
    if (j.contains("transform_quad") && j["transform_quad"].is_array()) {
        const auto &q = j["transform_quad"];
        auto qv = [&](size_t i) { return i < q.size() && q[i].is_number() ? (float)q[i].get<double>() : 0.0f; };
        l->transform_quad_tl_x = qv(0); l->transform_quad_tl_y = qv(1);
        l->transform_quad_tr_x = qv(2); l->transform_quad_tr_y = qv(3);
        l->transform_quad_br_x = qv(4); l->transform_quad_br_y = qv(5);
        l->transform_quad_bl_x = qv(6); l->transform_quad_bl_y = qv(7);
        l->transform_quad_tl.static_value = {l->transform_quad_tl_x, l->transform_quad_tl_y};
        l->transform_quad_tr.static_value = {l->transform_quad_tr_x, l->transform_quad_tr_y};
        l->transform_quad_br.static_value = {l->transform_quad_br_x, l->transform_quad_br_y};
        l->transform_quad_bl.static_value = {l->transform_quad_bl_x, l->transform_quad_bl_y};
    }
    if (j.contains("transform_quad_tl")) vec2_aprop_from_json(j["transform_quad_tl"], l->transform_quad_tl);
    if (j.contains("transform_quad_tr")) vec2_aprop_from_json(j["transform_quad_tr"], l->transform_quad_tr);
    if (j.contains("transform_quad_br")) vec2_aprop_from_json(j["transform_quad_br"], l->transform_quad_br);
    if (j.contains("transform_quad_bl")) vec2_aprop_from_json(j["transform_quad_bl"], l->transform_quad_bl);
    if (j.contains("scale"))    vec2_aprop_from_json(j["scale"], l->scale);
    l->scale_lock = json_bool(j, "scale_lock", true);
    if (j.contains("rotation")) l->rotation = aprop_from_json(j["rotation"], "rotation");
    if (j.contains("opacity"))  l->opacity  = aprop_from_json(j["opacity"],  "opacity");
    l->dimension_mode = static_cast<LayerDimensionMode>(std::clamp(
        json_int(j, "dimension_mode", static_cast<int>(LayerDimensionMode::TwoD)),
        static_cast<int>(LayerDimensionMode::TwoD),
        static_cast<int>(LayerDimensionMode::ThreeD)));
    l->transform_axis_space = static_cast<TransformAxisSpace>(std::clamp(
        json_int(j, "transform_axis_space", static_cast<int>(TransformAxisSpace::Local)),
        static_cast<int>(TransformAxisSpace::Local),
        static_cast<int>(TransformAxisSpace::World)));
    if (j.contains("position_z")) l->position_z = aprop_from_json(j["position_z"], "position_z");
    const bool has_position_3d = j.contains("position_3d");
    l->position_3d_path_enabled = json_bool(
        j, "position_3d_path_enabled", has_position_3d);
    if (has_position_3d)
        vec3_aprop_from_json(j["position_3d"], l->position_3d);
    if (j.contains("rotation_x")) l->rotation_x = aprop_from_json(j["rotation_x"], "rotation_x");
    if (j.contains("rotation_y")) l->rotation_y = aprop_from_json(j["rotation_y"], "rotation_y");
    if (j.contains("scale_z")) l->scale_z = aprop_from_json(j["scale_z"], "scale_z");
    if (j.contains("anchor_z")) l->anchor_z = aprop_from_json(j["anchor_z"], "anchor_z");
    if (j.contains("orientation_x")) l->orientation_x = aprop_from_json(j["orientation_x"], "orientation_x");
    if (j.contains("orientation_y")) l->orientation_y = aprop_from_json(j["orientation_y"], "orientation_y");
    if (j.contains("orientation_z")) l->orientation_z = aprop_from_json(j["orientation_z"], "orientation_z");
    l->camera_id = bounded_string(j, "camera_id", "", kMaxNameLength);
    l->camera_assignment = j.contains("camera_assignment")
        ? discrete_property_from_json(j["camera_assignment"], "camera_assignment", l->camera_id)
        : AnimatedDiscreteProperty{"camera_assignment", l->camera_id};
    l->camera_id = l->camera_assignment.static_value;
    l->depth_mode = static_cast<LayerDepthMode>(std::clamp(
        json_int(j, "depth_mode", static_cast<int>(LayerDepthMode::Automatic)),
        static_cast<int>(LayerDepthMode::Automatic),
        static_cast<int>(LayerDepthMode::LayerOrder)));
    l->depth_test = json_bool(j, "depth_test", true);
    l->write_to_depth = json_bool(j, "write_to_depth", true);
    l->double_sided = json_bool(j, "double_sided", true);
    l->backface_culling = json_bool(j, "backface_culling", false);
    l->material_accepts_lights = json_bool(j, "material_accepts_lights", false);
    l->material_casts_shadows = json_bool(j, "material_casts_shadows", true);
    l->material_accepts_shadows = json_bool(j, "material_accepts_shadows", true);
    l->material_appears_in_reflections = json_bool(
        j, "material_appears_in_reflections", true);
    auto read_material_property = [&](const char *key, AnimatedProperty &property,
                                      double minimum, double maximum) {
        if (j.contains(key))
            property = aprop_from_json(j[key], property.name);
        property.static_value = std::clamp(
            finite_or(property.static_value, property.static_value), minimum, maximum);
        for (Keyframe &keyframe : property.keyframes)
            keyframe.value = std::clamp(
                finite_or(keyframe.value, property.static_value), minimum, maximum);
    };
    read_material_property("material_ambient", l->material_ambient, 0.0, 4.0);
    read_material_property("material_diffuse", l->material_diffuse, 0.0, 4.0);
    read_material_property("material_specular", l->material_specular, 0.0, 4.0);
    read_material_property("material_shininess", l->material_shininess, 1.0, 512.0);
    read_material_property("material_metallic", l->material_metallic, 0.0, 1.0);
    read_material_property("material_roughness", l->material_roughness, 0.02, 1.0);
    read_material_property("material_reflection_intensity",
                           l->material_reflection_intensity, 0.0, 4.0);
    l->material_emissive_color = json_color(
        j, "material_emissive_color", static_cast<uint32_t>(0xFFFFFFFF));
    l->material_emissive_color_a.static_value = (l->material_emissive_color >> 24) & 0xFF;
    l->material_emissive_color_r.static_value = (l->material_emissive_color >> 16) & 0xFF;
    l->material_emissive_color_g.static_value = (l->material_emissive_color >> 8) & 0xFF;
    l->material_emissive_color_b.static_value = l->material_emissive_color & 0xFF;
    read_material_property("material_emissive_color_a", l->material_emissive_color_a, 0.0, 255.0);
    read_material_property("material_emissive_color_r", l->material_emissive_color_r, 0.0, 255.0);
    read_material_property("material_emissive_color_g", l->material_emissive_color_g, 0.0, 255.0);
    read_material_property("material_emissive_color_b", l->material_emissive_color_b, 0.0, 255.0);
    mirror_material_emissive_color(*l, 0.0);
    read_material_property("material_emissive_intensity",
                           l->material_emissive_intensity, 0.0, 16.0);
    l->geometry_extrusion_enabled = json_bool(j, "geometry_extrusion_enabled", false);
    if (j.contains("geometry_extrusion_depth")) l->geometry_extrusion_depth = aprop_from_json(j["geometry_extrusion_depth"], "geometry_extrusion_depth");
    if (j.contains("geometry_bevel_depth")) l->geometry_bevel_depth = aprop_from_json(j["geometry_bevel_depth"], "geometry_bevel_depth");
    l->geometry_extrusion_depth.static_value = std::clamp(l->geometry_extrusion_depth.static_value, 0.0, 100000.0);
    l->geometry_bevel_depth.static_value = std::clamp(l->geometry_bevel_depth.static_value, 0.0, 100000.0);
    l->geometry_bevel_segments = std::clamp(json_int(j, "geometry_bevel_segments", 3), 1, 16);
    l->geometry_extrusion_segments = std::clamp(json_int(j, "geometry_extrusion_segments", 12), 1, 64);
    l->geometry_bevel_front = json_bool(j, "geometry_bevel_front", true);
    l->geometry_bevel_back = json_bool(j, "geometry_bevel_back", true);
    /* Development Version 330 keeps legacy geometry values for the future
     * retained-mesh migration, but loading them must not mutate the layer into
     * 3D or re-enter the retired alpha-slice hardware-depth path. */
    if (l->type == LayerType::Light && j.contains("light")) {
        l->light = light_from_json(j["light"], 0);
        l->light.id = l->id; l->light.name = l->name;
    }
    l->position_z.static_value = std::clamp(l->position_z.static_value, -1000000.0, 1000000.0);
    l->rotation_x.static_value = finite_or(l->rotation_x.static_value, 0.0);
    l->rotation_y.static_value = finite_or(l->rotation_y.static_value, 0.0);
    l->scale_z.static_value = std::clamp(l->scale_z.static_value, -100.0, 100.0);
    l->anchor_z.static_value = std::clamp(l->anchor_z.static_value, -1000000.0, 1000000.0);
    l->orientation_x.static_value = finite_or(l->orientation_x.static_value, 0.0);
    l->orientation_y.static_value = finite_or(l->orientation_y.static_value, 0.0);
    l->orientation_z.static_value = finite_or(l->orientation_z.static_value, 0.0);
    l->scale.static_value.x = std::clamp(l->scale.static_value.x, -100.0, 100.0);
    l->scale.static_value.y = std::clamp(l->scale.static_value.y, -100.0, 100.0);
    l->opacity.static_value = std::clamp(l->opacity.static_value, 0.0, 1.0);

    l->text_content  = bounded_string(j, "text_content", "Title", kMaxTextLength);
    l->clock_format  = bounded_string(j, "clock_format", "H:i:s", kMaxNameLength);
    l->expose_text   = json_bool(j, "expose_text", false);
    l->exposed_hide_if_empty = json_bool(j, "exposed_hide_if_empty", false);
    l->exposed_single_value = json_bool(j, "exposed_single_value", false);
    l->expose_fill_color = json_bool(j, "expose_fill_color", false);
    l->exposed_fill_single_value = json_bool(j, "exposed_fill_single_value", false);
    l->expose_stroke_color = json_bool(j, "expose_stroke_color", false);
    l->exposed_stroke_single_value = json_bool(j, "exposed_stroke_single_value", false);
    l->live_cue_hidden_if_empty = false;
    l->ignore_persistence = !l->expose_text && json_bool(j, "ignore_persistence", false);
    l->font_family   = bounded_string(j, "font_family", "Helvetica Neue", kMaxNameLength);
    l->font_style    = bounded_string(j, "font_style", "Regular", kMaxNameLength);
    l->font_size     = std::clamp(json_int(j, "font_size", 72), 1, 512);
    l->font_size_prop.static_value = l->font_size;
    if (j.contains("font_size_prop")) l->font_size_prop = aprop_from_json(j["font_size_prop"], "font_size");
    l->font_size_prop.static_value = std::clamp(l->font_size_prop.static_value, 1.0, 512.0);
    l->font_bold     = json_bool(j, "font_bold", false);
    l->font_italic   = json_bool(j, "font_italic", false);
    l->font_kerning  = json_bool(j, "font_kerning", true);
    l->kerning_mode  = std::clamp(json_int(j, "kerning_mode", 0), 0, 2);
    l->manual_kerning = (float)std::clamp(finite_or(json_double(j, "manual_kerning", 0.0), 0.0), -1000.0, 1000.0);
    l->text_leading  = (float)std::clamp(finite_or(json_double(j, "text_leading", 0.0), 0.0), -1000.0, 1000.0);
    l->char_tracking = (float)std::clamp(finite_or(json_double(j, "char_tracking", 0.0), 0.0), -1000.0, 1000.0);
    l->char_tracking_prop.static_value = l->char_tracking;
    if (j.contains("char_tracking_prop")) l->char_tracking_prop = aprop_from_json(j["char_tracking_prop"], "char_tracking");
    l->char_tracking_prop.static_value = std::clamp(l->char_tracking_prop.static_value, -1000.0, 1000.0);
    l->char_scale_x  = (float)std::clamp(finite_or(json_double(j, "char_scale_x", 1.0), 1.0), 0.01, 100.0);
    l->char_scale_x_prop.static_value = l->char_scale_x;
    if (j.contains("char_scale_x_prop")) l->char_scale_x_prop = aprop_from_json(j["char_scale_x_prop"], "char_scale_x");
    l->char_scale_x_prop.static_value = std::clamp(l->char_scale_x_prop.static_value, 0.01, 100.0);
    l->char_scale_y  = (float)std::clamp(finite_or(json_double(j, "char_scale_y", 1.0), 1.0), 0.01, 100.0);
    l->char_scale_y_prop.static_value = l->char_scale_y;
    if (j.contains("char_scale_y_prop")) l->char_scale_y_prop = aprop_from_json(j["char_scale_y_prop"], "char_scale_y");
    l->char_scale_y_prop.static_value = std::clamp(l->char_scale_y_prop.static_value, 0.01, 100.0);
    l->baseline_shift = (float)std::clamp(finite_or(json_double(j, "baseline_shift", 0.0), 0.0), -1000.0, 1000.0);
    l->baseline_shift_prop.static_value = l->baseline_shift;
    if (j.contains("baseline_shift_prop")) l->baseline_shift_prop = aprop_from_json(j["baseline_shift_prop"], "baseline_shift");
    l->baseline_shift_prop.static_value = std::clamp(l->baseline_shift_prop.static_value, -1000.0, 1000.0);
    l->text_style    = std::clamp(json_int(j, "text_style", 0), 0, 4);
    l->text_underline = json_bool(j, "text_underline", false);
    l->text_strikethrough = json_bool(j, "text_strikethrough", false);
    l->text_ligatures = json_bool(j, "text_ligatures", true);
    l->text_stylistic_alternates = json_bool(j, "text_stylistic_alternates", false);
    l->text_fractions = json_bool(j, "text_fractions", false);
    l->text_opentype_features = json_bool(j, "text_opentype_features", false);
    l->text_language = bounded_string(j, "text_language", "English", kMaxNameLength);
    l->text_overflow_mode = std::clamp(json_int(j, "text_overflow_mode", 0), 0, 2);
    l->text_fit_min_scale = (float)std::clamp(finite_or(json_double(j, "text_fit_min_scale", 0.5), 0.5), 0.05, 1.0);
    l->text_box_width_to_text = json_bool(j, "text_box_width_to_text", false);
    l->text_box_height_to_text = json_bool(j, "text_box_height_to_text", false);
    l->max_text_box_width = (float)std::clamp(finite_or(json_double(j, "max_text_box_width", 1920.0), 1920.0), 1.0, (double)kMaxCanvasDimension);
    l->max_text_box_height = (float)std::clamp(finite_or(json_double(j, "max_text_box_height", 100.0), 100.0), 1.0, (double)kMaxCanvasDimension);
    l->max_text_box_width_overridden = json_bool(j, "max_text_box_width_overridden", false);
    l->max_text_box_height_overridden = json_bool(j, "max_text_box_height_overridden", false);
    l->ticker_style = std::clamp(json_int(j, "ticker_style", 0), 0, 2);
    l->ticker_speed = std::clamp(finite_or(json_double(j, "ticker_speed", 120.0), 120.0), 0.0, 10000.0);
    l->ticker_line_hold = std::clamp(finite_or(json_double(j, "ticker_line_hold", 2.0), 2.0), 0.0, kMaxDuration);
    l->ticker_direction = std::clamp(json_int(j, "ticker_direction", 1), 0, 1);
    l->ticker_playback_mode = std::clamp(json_int(j, "ticker_playback_mode", 0), 0, 3);
    l->ticker_completion = std::clamp(json_double(j, "ticker_completion", 0.0), 0.0, 100.0);
    l->ticker_completion_prop.static_value = l->ticker_completion;
    if (j.contains("ticker_completion_prop"))
        l->ticker_completion_prop = aprop_from_json(j["ticker_completion_prop"], "ticker_completion");
    l->ticker_completion_prop.static_value = std::clamp(l->ticker_completion_prop.static_value, 0.0, 100.0);
    l->text_color    = json_color(j, "text_color", (uint32_t)0xFFFFFFFF);
    l->stroke_fill_type = std::clamp(json_int(j, "stroke_fill_type", 1), 0, 2);
    l->stroke_color  = json_color(j, "stroke_color", (uint32_t)0xFF000000);
    l->stroke_width  = std::clamp(finite_or(json_double(j, "stroke_width", 0.0), 0.0), 0.0, 512.0);
    l->stroke_width_prop.static_value = l->stroke_width;
    if (j.contains("stroke_width_prop"))
        l->stroke_width_prop = aprop_from_json(j["stroke_width_prop"], "stroke_width");
    l->stroke_width_prop.static_value = std::clamp(l->stroke_width_prop.static_value, 0.0, 512.0);
    const bool has_general_stroke_offset = j.contains("stroke_offset") ||
                                           j.contains("stroke_offset_prop");
    l->stroke_offset = (float)std::clamp(
        finite_or(json_double(j, "stroke_offset", 0.0), 0.0), -16384.0, 16384.0);
    l->stroke_offset_prop.static_value = l->stroke_offset;
    if (j.contains("stroke_offset_prop"))
        l->stroke_offset_prop = aprop_from_json(j["stroke_offset_prop"], "stroke_offset");
    l->stroke_offset_prop.static_value = std::clamp(
        l->stroke_offset_prop.static_value, -16384.0, 16384.0);
    if (!l->stroke_offset_prop.is_animated())
        l->stroke_offset = static_cast<float>(l->stroke_offset_prop.static_value);

    /* Development Version 283 migration: Development Version 282 stored
     * Stroke Offset inside each Trim Paths effect. Promote the legacy value to
     * the layer-level stroke setting and clear the deprecated effect state so
     * it cannot be applied twice. */
    if (!has_general_stroke_offset) {
        for (LayerEffect &effect : l->effects) {
            if (effect.type != LayerEffectType::TrimPaths)
                continue;
            if (effect.stroke_offset_prop.is_animated()) {
                l->stroke_offset_prop = effect.stroke_offset_prop;
                l->stroke_offset_prop.name = "stroke_offset";
                l->stroke_offset = (float)std::clamp(
                    l->stroke_offset_prop.static_value, -16384.0, 16384.0);
                break;
            }
            if (std::abs(effect.effect_stroke_offset) > 1.0e-7f) {
                l->stroke_offset = (float)std::clamp(
                    (double)effect.effect_stroke_offset, -16384.0, 16384.0);
                l->stroke_offset_prop.static_value = l->stroke_offset;
                break;
            }
        }
    }
    for (LayerEffect &effect : l->effects) {
        if (effect.type != LayerEffectType::TrimPaths)
            continue;
        effect.effect_stroke_offset = 0.0f;
        effect.stroke_offset_prop = AnimatedProperty("effect_stroke_offset", 0.0);
    }
    l->outline_enabled = json_bool(j, "outline_enabled", l->stroke_width > 0.0f);
    l->outline_opacity = std::clamp(finite_or(json_double(j, "outline_opacity", 1.0), 1.0), 0.0, 1.0);
    l->outline_join_style = std::clamp(json_int(j, "outline_join_style", 1), 0, 2);
    l->outline_on_front = json_bool(j, "outline_on_front", false);
    l->outline_alignment = std::clamp(json_int(j, "outline_alignment", 0), 0, 2);
    l->outline_antialias = json_bool(j, "outline_antialias", true);
    l->stroke_gradient_spread = gradient_spread_from_json(j, "stroke_gradient_spread", "stroke_gradient_type", 0);
    l->stroke_gradient_type = normalize_gradient_type(json_int(j, "stroke_gradient_type", 0));
    l->stroke_gradient_start_color = json_color(j, "stroke_gradient_start_color", (uint32_t)0xFFFFFFFF);
    l->stroke_gradient_end_color = json_color(j, "stroke_gradient_end_color", l->stroke_color);
    l->stroke_gradient_start_pos = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_start_pos", 0.0), 0.0), 0.0, 1.0);
    l->stroke_gradient_end_pos = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_end_pos", 1.0), 1.0), 0.0, 1.0);
    l->stroke_gradient_start_opacity = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_start_opacity", 1.0), 1.0), 0.0, 1.0);
    l->stroke_gradient_end_opacity = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_end_opacity", 1.0), 1.0), 0.0, 1.0);
    l->stroke_gradient_opacity = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_opacity", 1.0), 1.0), 0.0, 1.0);
    l->stroke_gradient_angle = (float)finite_or(json_double(j, "stroke_gradient_angle", 0.0), 0.0);
    l->stroke_gradient_center_x = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_center_x", 0.5), 0.5), -100.0, 100.0);
    l->stroke_gradient_center_y = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_center_y", 0.5), 0.5), -100.0, 100.0);
    l->stroke_gradient_scale = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_scale", 1.0), 1.0), 0.01, 100.0);
    l->stroke_gradient_focal_x = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_focal_x", l->stroke_gradient_center_x), l->stroke_gradient_center_x), -100.0, 100.0);
    l->stroke_gradient_focal_y = (float)std::clamp(finite_or(json_double(j, "stroke_gradient_focal_y", l->stroke_gradient_center_y), l->stroke_gradient_center_y), -100.0, 100.0);
    l->stroke_gradient_stops = gradient_stops_from_json(j.value("stroke_gradient_stops", json::array()));
    l->align_h       = std::clamp(json_int(j, "align_h", 1), 0, 6);
    l->align_v       = std::clamp(json_int(j, "align_v", 1), 0, 3);
    l->paragraph_indent_left = (float)std::clamp(finite_or(json_double(j, "paragraph_indent_left", 0.0), 0.0), -10000.0, 10000.0);
    l->paragraph_indent_right = (float)std::clamp(finite_or(json_double(j, "paragraph_indent_right", 0.0), 0.0), -10000.0, 10000.0);
    l->paragraph_indent_first_line = (float)std::clamp(finite_or(json_double(j, "paragraph_indent_first_line", 0.0), 0.0), -10000.0, 10000.0);
    l->paragraph_indent_left_prop.static_value = l->paragraph_indent_left;
    l->paragraph_indent_right_prop.static_value = l->paragraph_indent_right;
    l->paragraph_indent_first_line_prop.static_value = l->paragraph_indent_first_line;
    if (j.contains("paragraph_indent_left_prop")) l->paragraph_indent_left_prop = aprop_from_json(j["paragraph_indent_left_prop"], "paragraph_indent_left");
    if (j.contains("paragraph_indent_right_prop")) l->paragraph_indent_right_prop = aprop_from_json(j["paragraph_indent_right_prop"], "paragraph_indent_right");
    if (j.contains("paragraph_indent_first_line_prop")) l->paragraph_indent_first_line_prop = aprop_from_json(j["paragraph_indent_first_line_prop"], "paragraph_indent_first_line");
    l->paragraph_indent_left_prop.static_value = std::clamp(l->paragraph_indent_left_prop.static_value, -10000.0, 10000.0);
    l->paragraph_indent_right_prop.static_value = std::clamp(l->paragraph_indent_right_prop.static_value, -10000.0, 10000.0);
    l->paragraph_indent_first_line_prop.static_value = std::clamp(l->paragraph_indent_first_line_prop.static_value, -10000.0, 10000.0);
    l->paragraph_space_before = (float)std::clamp(finite_or(json_double(j, "paragraph_space_before", 0.0), 0.0), -10000.0, 10000.0);
    l->paragraph_space_before_prop.static_value = l->paragraph_space_before;
    if (j.contains("paragraph_space_before_prop")) l->paragraph_space_before_prop = aprop_from_json(j["paragraph_space_before_prop"], "paragraph_space_before");
    l->paragraph_space_before_prop.static_value = std::clamp(l->paragraph_space_before_prop.static_value, -10000.0, 10000.0);
    l->paragraph_space_after = (float)std::clamp(finite_or(json_double(j, "paragraph_space_after", 0.0), 0.0), -10000.0, 10000.0);
    l->paragraph_space_after_prop.static_value = l->paragraph_space_after;
    if (j.contains("paragraph_space_after_prop")) l->paragraph_space_after_prop = aprop_from_json(j["paragraph_space_after_prop"], "paragraph_space_after");
    l->paragraph_space_after_prop.static_value = std::clamp(l->paragraph_space_after_prop.static_value, -10000.0, 10000.0);
    l->paragraph_hyphenate = json_bool(j, "paragraph_hyphenate", false);

    l->fill_color    = json_color(j, "fill_color", (uint32_t)0xFF222222);
    l->fill_type     = std::clamp(json_int(j, "fill_type", 0), 0, 1);
    l->gradient_spread = gradient_spread_from_json(j, "gradient_spread", "gradient_type", 0);
    l->gradient_type = normalize_gradient_type(json_int(j, "gradient_type", 0));
    l->gradient_start_color = json_color(j, "gradient_start_color", (uint32_t)0xFF4B6EA8);
    l->gradient_end_color = json_color(j, "gradient_end_color", (uint32_t)0xFF1B1B1B);
    l->gradient_start_pos = (float)std::clamp(finite_or(json_double(j, "gradient_start_pos", 0.0), 0.0), 0.0, 1.0);
    l->gradient_end_pos = (float)std::clamp(finite_or(json_double(j, "gradient_end_pos", 1.0), 1.0), 0.0, 1.0);
    l->gradient_start_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_start_opacity", 1.0), 1.0), 0.0, 1.0);
    l->gradient_end_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_end_opacity", 1.0), 1.0), 0.0, 1.0);
    l->gradient_opacity = (float)std::clamp(finite_or(json_double(j, "gradient_opacity", 1.0), 1.0), 0.0, 1.0);
    l->gradient_angle = (float)finite_or(json_double(j, "gradient_angle", 0.0), 0.0);
    l->gradient_center_x = (float)std::clamp(finite_or(json_double(j, "gradient_center_x", 0.5), 0.5), -100.0, 100.0);
    l->gradient_center_y = (float)std::clamp(finite_or(json_double(j, "gradient_center_y", 0.5), 0.5), -100.0, 100.0);
    l->gradient_scale = (float)std::clamp(finite_or(json_double(j, "gradient_scale", 1.0), 1.0), 0.01, 100.0);
    l->gradient_focal_x = (float)std::clamp(finite_or(json_double(j, "gradient_focal_x", l->gradient_center_x), l->gradient_center_x), -100.0, 100.0);
    l->gradient_focal_y = (float)std::clamp(finite_or(json_double(j, "gradient_focal_y", l->gradient_center_y), l->gradient_center_y), -100.0, 100.0);
    l->gradient_stops = gradient_stops_from_json(j.value("gradient_stops", json::array()));
    l->rich_text = j.contains("rich_text") ? rich_doc_from_json(j["rich_text"], *l) : rich_text_document_from_layer_defaults(*l);
    rich_text_document_ensure_canonical(*l);
    l->background_enabled = json_bool(j, "background_enabled", false);
    l->background_color = json_color(j, "background_color", (uint32_t)0xFF000000);
    l->background_opacity = (float)std::clamp(finite_or(json_double(j, "background_opacity", 0.35), 0.35), 0.0, 1.0);
    const double legacy_padding = finite_or(json_double(j, "background_padding", 0.0), 0.0);
    l->background_padding_x = (float)std::clamp(finite_or(json_double(j, "background_padding_x", legacy_padding), legacy_padding), 0.0, (double)kMaxCanvasDimension);
    l->background_padding_y = (float)std::clamp(finite_or(json_double(j, "background_padding_y", legacy_padding), legacy_padding), 0.0, (double)kMaxCanvasDimension);
    l->background_padding_left = (float)std::clamp(finite_or(json_double(j, "background_padding_left", l->background_padding_x), l->background_padding_x), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
    l->background_padding_right = (float)std::clamp(finite_or(json_double(j, "background_padding_right", l->background_padding_x), l->background_padding_x), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
    l->background_padding_top = (float)std::clamp(finite_or(json_double(j, "background_padding_top", l->background_padding_y), l->background_padding_y), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
    l->background_padding_bottom = (float)std::clamp(finite_or(json_double(j, "background_padding_bottom", l->background_padding_y), l->background_padding_y), -(double)kMaxCanvasDimension, (double)kMaxCanvasDimension);
    l->background_corner_radius = (float)std::clamp(finite_or(json_double(j, "background_corner_radius", 0.0), 0.0), 0.0, (double)kMaxCanvasDimension);
    l->background_corner_radius_tl = (float)std::clamp(finite_or(json_double(j, "background_corner_radius_tl", l->background_corner_radius), l->background_corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->background_corner_radius_tr = (float)std::clamp(finite_or(json_double(j, "background_corner_radius_tr", l->background_corner_radius), l->background_corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->background_corner_radius_br = (float)std::clamp(finite_or(json_double(j, "background_corner_radius_br", l->background_corner_radius), l->background_corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->background_corner_radius_bl = (float)std::clamp(finite_or(json_double(j, "background_corner_radius_bl", l->background_corner_radius), l->background_corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->background_corner_type = (CornerType)std::clamp(json_int(j, "background_corner_type", (int)CornerType::Round), 0, (int)CornerType::Cutout);
    l->background_fill_type = std::clamp(json_int(j, "background_fill_type", 0), 0, 1);
    l->background_stroke_color = json_color(j, "background_stroke_color", (uint32_t)0x00000000);
    l->background_stroke_width = (float)std::clamp(finite_or(json_double(j, "background_stroke_width", 0.0), 0.0), 0.0, (double)kMaxCanvasDimension);
    l->background_stroke_opacity = (float)std::clamp(finite_or(json_double(j, "background_stroke_opacity", 1.0), 1.0), 0.0, 1.0);
    l->background_stroke_fill_type = std::clamp(json_int(j, "background_stroke_fill_type", 0), 0, 1);
    l->background_gradient_spread = gradient_spread_from_json(j, "background_gradient_spread",
                                                             "background_gradient_type",
                                                             l->gradient_spread);
    l->background_gradient_type = normalize_gradient_type(json_int(j, "background_gradient_type",
                                                                  l->gradient_type));
    l->background_gradient_start_color = json_color(j, "background_gradient_start_color", l->gradient_start_color);
    l->background_gradient_end_color = json_color(j, "background_gradient_end_color", l->gradient_end_color);
    l->background_gradient_start_pos = (float)std::clamp(finite_or(json_double(j, "background_gradient_start_pos", l->gradient_start_pos), l->gradient_start_pos), 0.0, 1.0);
    l->background_gradient_end_pos = (float)std::clamp(finite_or(json_double(j, "background_gradient_end_pos", l->gradient_end_pos), l->gradient_end_pos), 0.0, 1.0);
    l->background_gradient_start_opacity = (float)std::clamp(finite_or(json_double(j, "background_gradient_start_opacity", l->gradient_start_opacity), l->gradient_start_opacity), 0.0, 1.0);
    l->background_gradient_end_opacity = (float)std::clamp(finite_or(json_double(j, "background_gradient_end_opacity", l->gradient_end_opacity), l->gradient_end_opacity), 0.0, 1.0);
    l->background_gradient_opacity = (float)std::clamp(finite_or(json_double(j, "background_gradient_opacity", l->gradient_opacity), l->gradient_opacity), 0.0, 1.0);
    l->background_gradient_angle = (float)finite_or(json_double(j, "background_gradient_angle", l->gradient_angle), l->gradient_angle);
    l->background_gradient_center_x = (float)std::clamp(finite_or(json_double(j, "background_gradient_center_x", l->gradient_center_x), l->gradient_center_x), -100.0, 100.0);
    l->background_gradient_center_y = (float)std::clamp(finite_or(json_double(j, "background_gradient_center_y", l->gradient_center_y), l->gradient_center_y), -100.0, 100.0);
    l->background_gradient_scale = (float)std::clamp(finite_or(json_double(j, "background_gradient_scale", l->gradient_scale), l->gradient_scale), 0.01, 100.0);
    l->background_gradient_focal_x = (float)std::clamp(finite_or(json_double(j, "background_gradient_focal_x", l->gradient_focal_x), l->gradient_focal_x), -100.0, 100.0);
    l->background_gradient_focal_y = (float)std::clamp(finite_or(json_double(j, "background_gradient_focal_y", l->gradient_focal_y), l->gradient_focal_y), -100.0, 100.0);
    l->background_gradient_stops = gradient_stops_from_json(j.value("background_gradient_stops", json::array()));
    l->background_enabled_prop.static_value = l->background_enabled ? 1.0 : 0.0;
    l->background_opacity_prop.static_value = l->background_opacity;
    l->background_padding_x_prop.static_value = l->background_padding_x;
    l->background_padding_y_prop.static_value = l->background_padding_y;
    l->background_padding_left_prop.static_value = l->background_padding_left;
    l->background_padding_right_prop.static_value = l->background_padding_right;
    l->background_padding_top_prop.static_value = l->background_padding_top;
    l->background_padding_bottom_prop.static_value = l->background_padding_bottom;
    l->background_corner_radius_prop.static_value = l->background_corner_radius;
    l->background_corner_radius_tl_prop.static_value = l->background_corner_radius_tl;
    l->background_corner_radius_tr_prop.static_value = l->background_corner_radius_tr;
    l->background_corner_radius_br_prop.static_value = l->background_corner_radius_br;
    l->background_corner_radius_bl_prop.static_value = l->background_corner_radius_bl;
    l->background_stroke_width_prop.static_value = l->background_stroke_width;
    l->background_stroke_opacity_prop.static_value = l->background_stroke_opacity;
    set_background_color_channels(*l, l->background_color);
    set_argb_channels(l->background_stroke_color_a, l->background_stroke_color_r, l->background_stroke_color_g, l->background_stroke_color_b, l->background_stroke_color);
    if (j.contains("background_enabled_prop")) l->background_enabled_prop = aprop_from_json(j["background_enabled_prop"], "background_enabled");
    if (j.contains("background_opacity_prop")) l->background_opacity_prop = aprop_from_json(j["background_opacity_prop"], "background_opacity");
    if (j.contains("background_padding_x_prop")) l->background_padding_x_prop = aprop_from_json(j["background_padding_x_prop"], "background_padding_x");
    if (j.contains("background_padding_y_prop")) l->background_padding_y_prop = aprop_from_json(j["background_padding_y_prop"], "background_padding_y");
    if (j.contains("background_padding_left_prop")) l->background_padding_left_prop = aprop_from_json(j["background_padding_left_prop"], "background_padding_left");
    if (j.contains("background_padding_right_prop")) l->background_padding_right_prop = aprop_from_json(j["background_padding_right_prop"], "background_padding_right");
    if (j.contains("background_padding_top_prop")) l->background_padding_top_prop = aprop_from_json(j["background_padding_top_prop"], "background_padding_top");
    if (j.contains("background_padding_bottom_prop")) l->background_padding_bottom_prop = aprop_from_json(j["background_padding_bottom_prop"], "background_padding_bottom");
    if (j.contains("background_corner_radius_prop")) l->background_corner_radius_prop = aprop_from_json(j["background_corner_radius_prop"], "background_corner_radius");
    if (j.contains("background_corner_radius_tl_prop")) l->background_corner_radius_tl_prop = aprop_from_json(j["background_corner_radius_tl_prop"], "background_corner_radius_tl");
    if (j.contains("background_corner_radius_tr_prop")) l->background_corner_radius_tr_prop = aprop_from_json(j["background_corner_radius_tr_prop"], "background_corner_radius_tr");
    if (j.contains("background_corner_radius_br_prop")) l->background_corner_radius_br_prop = aprop_from_json(j["background_corner_radius_br_prop"], "background_corner_radius_br");
    if (j.contains("background_corner_radius_bl_prop")) l->background_corner_radius_bl_prop = aprop_from_json(j["background_corner_radius_bl_prop"], "background_corner_radius_bl");
    if (j.contains("background_stroke_width_prop")) l->background_stroke_width_prop = aprop_from_json(j["background_stroke_width_prop"], "background_stroke_width");
    if (j.contains("background_stroke_opacity_prop")) l->background_stroke_opacity_prop = aprop_from_json(j["background_stroke_opacity_prop"], "background_stroke_opacity");
    if (j.contains("background_color_a")) l->background_color_a = aprop_from_json(j["background_color_a"], "background_color_a");
    if (j.contains("background_color_r")) l->background_color_r = aprop_from_json(j["background_color_r"], "background_color_r");
    if (j.contains("background_color_g")) l->background_color_g = aprop_from_json(j["background_color_g"], "background_color_g");
    if (j.contains("background_color_b")) l->background_color_b = aprop_from_json(j["background_color_b"], "background_color_b");
    if (j.contains("background_stroke_color_a")) l->background_stroke_color_a = aprop_from_json(j["background_stroke_color_a"], "background_stroke_color_a");
    if (j.contains("background_stroke_color_r")) l->background_stroke_color_r = aprop_from_json(j["background_stroke_color_r"], "background_stroke_color_r");
    if (j.contains("background_stroke_color_g")) l->background_stroke_color_g = aprop_from_json(j["background_stroke_color_g"], "background_stroke_color_g");
    if (j.contains("background_stroke_color_b")) l->background_stroke_color_b = aprop_from_json(j["background_stroke_color_b"], "background_stroke_color_b");
    l->rect_width    = std::clamp(finite_or(json_double(j, "rect_width", 1920.0), 1920.0), 0.0, (double)kMaxCanvasDimension);
    l->rect_height   = std::clamp(finite_or(json_double(j, "rect_height", 100.0), 100.0), 0.0, (double)kMaxCanvasDimension);
    if (!l->max_text_box_width_overridden)
        l->max_text_box_width = std::max(1.0f, l->rect_width);
    if (!l->max_text_box_height_overridden)
        l->max_text_box_height = std::max(1.0f, l->rect_height);
    l->corner_radius = std::clamp(finite_or(json_double(j, "corner_radius", 0.0), 0.0), 0.0, (double)kMaxCanvasDimension);
    l->corner_radius_tl = (float)std::clamp(finite_or(json_double(j, "corner_radius_tl", l->corner_radius), l->corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->corner_radius_tr = (float)std::clamp(finite_or(json_double(j, "corner_radius_tr", l->corner_radius), l->corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->corner_radius_br = (float)std::clamp(finite_or(json_double(j, "corner_radius_br", l->corner_radius), l->corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->corner_radius_bl = (float)std::clamp(finite_or(json_double(j, "corner_radius_bl", l->corner_radius), l->corner_radius), 0.0, (double)kMaxCanvasDimension);
    l->corner_radius_locked = json_bool(j, "corner_radius_locked",
                                        l->corner_radius_tl == l->corner_radius_tr &&
                                        l->corner_radius_tl == l->corner_radius_br &&
                                        l->corner_radius_tl == l->corner_radius_bl);
    const int legacy_corner_type = std::clamp(json_int(j, "corner_type", 0), 0, (int)CornerType::Cutout);
    auto legacy_corner_roundness = [](int type) {
        switch ((CornerType)type) {
        case CornerType::Straight:
        case CornerType::Cutout:
            return 0.0;
        case CornerType::Concave:
            return -100.0;
        case CornerType::Round:
        default:
            return 100.0;
        }
    };
    l->corner_bevel_roundness =
        (float)std::clamp(finite_or(json_double(j, "corner_bevel_roundness",
                                                legacy_corner_roundness(legacy_corner_type)),
                                    legacy_corner_roundness(legacy_corner_type)),
                          -100.0, 100.0);
    l->shape_type = (ShapeType)std::clamp(json_int(j, "shape_type", 0), 0, (int)ShapeType::Path);
    l->path_points = bezier_path_points_from_json(j.value("path_points", json::array()));
    l->path_closed = json_bool(j, "path_closed", true);
    if (l->shape_type == ShapeType::Path && l->path_points.size() < 2) {
        l->shape_type = ShapeType::Rectangle;
        l->path_points.clear();
        l->path_closed = true;
    }
    l->shape_points = std::clamp(json_int(j, "shape_points", 5), 3, 64);
    l->shape_sides = std::clamp(json_int(j, "shape_sides", 6), 3, 64);
    l->shape_inner_radius = (float)std::clamp(finite_or(json_double(j, "shape_inner_radius", 0.20), 0.20), 0.0, 1.0);
    l->shape_outer_radius = (float)std::clamp(finite_or(json_double(j, "shape_outer_radius", 0.5), 0.5), 0.0, 1.0);
    l->shape_roundness = (float)std::clamp(finite_or(json_double(j, "shape_roundness", 0.0), 0.0), 0.0, (double)kMaxCanvasDimension);
    l->shape_inner_roundness = (float)std::clamp(finite_or(json_double(j, "shape_inner_roundness", l->shape_roundness), l->shape_roundness), 0.0, (double)kMaxCanvasDimension);
    l->scale_stroke_with_shape = json_bool(j, "scale_stroke_with_shape", false);
    l->scale_corners_with_shape = json_bool(j, "scale_corners_with_shape", false);
    l->size.static_value.x = l->rect_width;
    l->size.static_value.y = l->rect_height;
    if (j.contains("size")) vec2_aprop_from_json(j["size"], l->size);
    l->size.static_value.x = std::clamp(l->size.static_value.x, 0.0, (double)kMaxCanvasDimension);
    l->size.static_value.y = std::clamp(l->size.static_value.y, 0.0, (double)kMaxCanvasDimension);
    l->origin_x      = std::clamp(finite_or(json_double(j, "origin_x", 0.5), 0.5), 0.0, 1.0);
    l->origin_y      = std::clamp(finite_or(json_double(j, "origin_y", 0.5), 0.5), 0.0, 1.0);
    l->origin_prop.static_value.x = l->origin_x;
    l->origin_prop.static_value.y = l->origin_y;
    if (j.contains("origin")) vec2_aprop_from_json(j["origin"], l->origin_prop);
    l->origin_prop.static_value.x = std::clamp(l->origin_prop.static_value.x, 0.0, 1.0);
    l->origin_prop.static_value.y = std::clamp(l->origin_prop.static_value.y, 0.0, 1.0);
    l->shadow_enabled = json_bool(j, "shadow_enabled", false);
    l->shadow_color = json_color(j, "shadow_color", (uint32_t)0x99000000);
    l->shadow_opacity = std::clamp(finite_or(json_double(j, "shadow_opacity", 0.6), 0.6), 0.0, 1.0);
    l->shadow_distance = std::clamp(finite_or(json_double(j, "shadow_distance", 8.0), 8.0), 0.0, 4096.0);
    l->shadow_angle = finite_or(json_double(j, "shadow_angle", 135.0), 135.0);
    l->shadow_blur = std::clamp(finite_or(json_double(j, "shadow_blur", 4.0), 4.0), 0.0, 512.0);
    l->shadow_spread = std::clamp(finite_or(json_double(j, "shadow_spread", 0.0), 0.0), 0.0, 512.0);
    l->shadow_blur_type = (ShadowBlurType)std::clamp(json_int(j, "shadow_blur_type", (int)ShadowBlurType::StackFast), 0, (int)ShadowBlurType::DualKawase);
    l->long_shadow_enabled = json_bool(j, "long_shadow_enabled", false);
    l->long_shadow_color = json_color(j, "long_shadow_color", l->shadow_color);
    l->long_shadow_opacity = std::clamp(finite_or(json_double(j, "long_shadow_opacity", 0.45), 0.45), 0.0, 1.0);
    l->long_shadow_length = std::clamp(finite_or(json_double(j, "long_shadow_length", 0.0), 0.0), 0.0, 4096.0);
    l->long_shadow_angle = finite_or(json_double(j, "long_shadow_angle", l->shadow_angle), l->shadow_angle);
    l->long_shadow_falloff = std::clamp(finite_or(json_double(j, "long_shadow_falloff", 1.0), 1.0), 0.0, 4.0);
    l->long_shadow_blur_type = (LongShadowBlurType)std::clamp(json_int(j, "long_shadow_blur_type", (int)LongShadowBlurType::None), 0, (int)LongShadowBlurType::StackFast);
    l->long_shadow_blur = std::clamp(finite_or(json_double(j, "long_shadow_blur", 8.0), 8.0), 0.0, 512.0);
    l->shadow_enabled_prop.static_value = l->shadow_enabled ? 1.0 : 0.0;
    l->shadow_opacity_prop.static_value = l->shadow_opacity;
    l->shadow_distance_prop.static_value = l->shadow_distance;
    l->shadow_angle_prop.static_value = l->shadow_angle;
    l->shadow_blur_prop.static_value = l->shadow_blur;
    l->shadow_spread_prop.static_value = l->shadow_spread;
    l->shadow_color_a.static_value = (l->shadow_color >> 24) & 0xFF;
    l->shadow_color_r.static_value = (l->shadow_color >> 16) & 0xFF;
    l->shadow_color_g.static_value = (l->shadow_color >> 8) & 0xFF;
    l->shadow_color_b.static_value = l->shadow_color & 0xFF;
    if (j.contains("shadow_enabled_prop")) l->shadow_enabled_prop = aprop_from_json(j["shadow_enabled_prop"], "shadow_enabled");
    if (j.contains("shadow_opacity_prop")) l->shadow_opacity_prop = aprop_from_json(j["shadow_opacity_prop"], "shadow_opacity");
    if (j.contains("shadow_distance_prop")) l->shadow_distance_prop = aprop_from_json(j["shadow_distance_prop"], "shadow_distance");
    if (j.contains("shadow_angle_prop")) l->shadow_angle_prop = aprop_from_json(j["shadow_angle_prop"], "shadow_angle");
    if (j.contains("shadow_blur_prop")) l->shadow_blur_prop = aprop_from_json(j["shadow_blur_prop"], "shadow_blur");
    if (j.contains("shadow_spread_prop")) l->shadow_spread_prop = aprop_from_json(j["shadow_spread_prop"], "shadow_spread");
    if (j.contains("shadow_color_a")) l->shadow_color_a = aprop_from_json(j["shadow_color_a"], "shadow_color_a");
    if (j.contains("shadow_color_r")) l->shadow_color_r = aprop_from_json(j["shadow_color_r"], "shadow_color_r");
    if (j.contains("shadow_color_g")) l->shadow_color_g = aprop_from_json(j["shadow_color_g"], "shadow_color_g");
    if (j.contains("shadow_color_b")) l->shadow_color_b = aprop_from_json(j["shadow_color_b"], "shadow_color_b");
    auto seed_effect_from_layer = [l](LayerEffect &effect) {
        switch (effect.type) {
        case LayerEffectType::BackgroundColor:
            effect.effect_color = l->background_color;
            effect.effect_opacity = l->background_opacity;
            effect.effect_fill_type = l->background_fill_type;
            effect.effect_stroke_color = l->background_stroke_color;
            effect.effect_stroke_width = l->background_stroke_width;
            effect.effect_stroke_opacity = l->background_stroke_opacity;
            effect.effect_padding_left = l->background_padding_left;
            effect.effect_padding_right = l->background_padding_right;
            effect.effect_padding_top = l->background_padding_top;
            effect.effect_padding_bottom = l->background_padding_bottom;
            effect.effect_corner_radius_tl = l->background_corner_radius_tl;
            effect.effect_corner_radius_tr = l->background_corner_radius_tr;
            effect.effect_corner_radius_br = l->background_corner_radius_br;
            effect.effect_corner_radius_bl = l->background_corner_radius_bl;
            effect.effect_corner_type = (int)l->background_corner_type;
            effect.effect_gradient_type = l->background_gradient_type;
            effect.effect_gradient_spread = l->background_gradient_spread;
            effect.effect_gradient_start_color = l->background_gradient_start_color;
            effect.effect_gradient_end_color = l->background_gradient_end_color;
            effect.effect_gradient_start_pos = l->background_gradient_start_pos;
            effect.effect_gradient_end_pos = l->background_gradient_end_pos;
            effect.effect_gradient_start_opacity = l->background_gradient_start_opacity;
            effect.effect_gradient_end_opacity = l->background_gradient_end_opacity;
            effect.effect_gradient_opacity = l->background_gradient_opacity;
            effect.effect_gradient_angle = l->background_gradient_angle;
            effect.effect_gradient_center_x = l->background_gradient_center_x;
            effect.effect_gradient_center_y = l->background_gradient_center_y;
            effect.effect_gradient_scale = l->background_gradient_scale;
            effect.effect_gradient_focal_x = l->background_gradient_focal_x;
            effect.effect_gradient_focal_y = l->background_gradient_focal_y;
            effect.enabled_prop = l->background_enabled_prop;
            effect.enabled_prop.name = "effect_enabled";
            effect.opacity_prop = l->background_opacity_prop;
            effect.opacity_prop.name = "effect_opacity";
            effect.gradient_start_pos_prop.static_value = effect.effect_gradient_start_pos;
            effect.gradient_end_pos_prop.static_value = effect.effect_gradient_end_pos;
            effect.gradient_start_opacity_prop.static_value = effect.effect_gradient_start_opacity;
            effect.gradient_end_opacity_prop.static_value = effect.effect_gradient_end_opacity;
            effect.gradient_opacity_prop.static_value = effect.effect_gradient_opacity;
            effect.gradient_angle_prop.static_value = effect.effect_gradient_angle;
            effect.gradient_center_x_prop.static_value = effect.effect_gradient_center_x;
            effect.gradient_center_y_prop.static_value = effect.effect_gradient_center_y;
            effect.gradient_scale_prop.static_value = effect.effect_gradient_scale;
            effect.gradient_focal_x_prop.static_value = effect.effect_gradient_focal_x;
            effect.gradient_focal_y_prop.static_value = effect.effect_gradient_focal_y;
            set_argb_channels(effect.gradient_start_color_a, effect.gradient_start_color_r,
                              effect.gradient_start_color_g, effect.gradient_start_color_b,
                              effect.effect_gradient_start_color);
            set_argb_channels(effect.gradient_end_color_a, effect.gradient_end_color_r,
                              effect.gradient_end_color_g, effect.gradient_end_color_b,
                              effect.effect_gradient_end_color);
            effect.stroke_width_prop = l->background_stroke_width_prop;
            effect.stroke_width_prop.name = "effect_stroke_width";
            effect.stroke_opacity_prop = l->background_stroke_opacity_prop;
            effect.stroke_opacity_prop.name = "effect_stroke_opacity";
            effect.padding_left_prop = l->background_padding_left_prop;
            effect.padding_left_prop.name = "effect_padding_left";
            effect.padding_right_prop = l->background_padding_right_prop;
            effect.padding_right_prop.name = "effect_padding_right";
            effect.padding_top_prop = l->background_padding_top_prop;
            effect.padding_top_prop.name = "effect_padding_top";
            effect.padding_bottom_prop = l->background_padding_bottom_prop;
            effect.padding_bottom_prop.name = "effect_padding_bottom";
            effect.corner_radius_tl_prop = l->background_corner_radius_tl_prop;
            effect.corner_radius_tl_prop.name = "effect_corner_radius_tl";
            effect.corner_radius_tr_prop = l->background_corner_radius_tr_prop;
            effect.corner_radius_tr_prop.name = "effect_corner_radius_tr";
            effect.corner_radius_br_prop = l->background_corner_radius_br_prop;
            effect.corner_radius_br_prop.name = "effect_corner_radius_br";
            effect.corner_radius_bl_prop = l->background_corner_radius_bl_prop;
            effect.corner_radius_bl_prop.name = "effect_corner_radius_bl";
            effect.color_a = l->background_color_a;
            effect.color_a.name = "effect_color_a";
            effect.color_r = l->background_color_r;
            effect.color_r.name = "effect_color_r";
            effect.color_g = l->background_color_g;
            effect.color_g.name = "effect_color_g";
            effect.color_b = l->background_color_b;
            effect.color_b.name = "effect_color_b";
            effect.stroke_color_a = l->background_stroke_color_a;
            effect.stroke_color_a.name = "effect_stroke_color_a";
            effect.stroke_color_r = l->background_stroke_color_r;
            effect.stroke_color_r.name = "effect_stroke_color_r";
            effect.stroke_color_g = l->background_stroke_color_g;
            effect.stroke_color_g.name = "effect_stroke_color_g";
            effect.stroke_color_b = l->background_stroke_color_b;
            effect.stroke_color_b.name = "effect_stroke_color_b";
            break;
        case LayerEffectType::Outline:
            effect.effect_fill_type = l->stroke_fill_type;
            effect.effect_color = l->stroke_color;
            effect.effect_size = l->stroke_width;
            effect.effect_opacity = l->outline_opacity;
            effect.effect_join_style = l->outline_join_style;
            effect.effect_on_front = l->outline_on_front;
            effect.effect_antialias = l->outline_antialias;
            effect.effect_gradient_type = l->stroke_gradient_type;
            effect.effect_gradient_spread = l->stroke_gradient_spread;
            effect.effect_gradient_start_color = l->stroke_gradient_start_color;
            effect.effect_gradient_end_color = l->stroke_gradient_end_color;
            effect.effect_gradient_start_pos = l->stroke_gradient_start_pos;
            effect.effect_gradient_end_pos = l->stroke_gradient_end_pos;
            effect.effect_gradient_start_opacity = l->stroke_gradient_start_opacity;
            effect.effect_gradient_end_opacity = l->stroke_gradient_end_opacity;
            effect.effect_gradient_opacity = l->stroke_gradient_opacity;
            effect.effect_gradient_angle = l->stroke_gradient_angle;
            effect.effect_gradient_center_x = l->stroke_gradient_center_x;
            effect.effect_gradient_center_y = l->stroke_gradient_center_y;
            effect.effect_gradient_scale = l->stroke_gradient_scale;
            effect.effect_gradient_focal_x = l->stroke_gradient_focal_x;
            effect.effect_gradient_focal_y = l->stroke_gradient_focal_y;
            set_argb_channels(effect.color_a, effect.color_r, effect.color_g, effect.color_b, effect.effect_color);
            effect.opacity_prop.static_value = effect.effect_opacity;
            effect.size_prop.static_value = effect.effect_size;
            break;
        case LayerEffectType::DropShadow:
            effect.effect_color = l->shadow_color;
            effect.effect_opacity = l->shadow_opacity;
            effect.effect_distance = l->shadow_distance;
            effect.effect_angle = l->shadow_angle;
            effect.effect_size = l->shadow_blur;
            effect.effect_spread = l->shadow_spread;
            effect.effect_blur_type = (int)l->shadow_blur_type;
            effect.enabled_prop = l->shadow_enabled_prop;
            effect.enabled_prop.name = "effect_enabled";
            effect.opacity_prop = l->shadow_opacity_prop;
            effect.opacity_prop.name = "effect_opacity";
            effect.distance_prop = l->shadow_distance_prop;
            effect.distance_prop.name = "effect_distance";
            effect.angle_prop = l->shadow_angle_prop;
            effect.angle_prop.name = "effect_angle";
            effect.size_prop = l->shadow_blur_prop;
            effect.size_prop.name = "effect_size";
            effect.spread_prop = l->shadow_spread_prop;
            effect.spread_prop.name = "effect_spread";
            effect.color_a = l->shadow_color_a;
            effect.color_a.name = "effect_color_a";
            effect.color_r = l->shadow_color_r;
            effect.color_r.name = "effect_color_r";
            effect.color_g = l->shadow_color_g;
            effect.color_g.name = "effect_color_g";
            effect.color_b = l->shadow_color_b;
            effect.color_b.name = "effect_color_b";
            break;
        case LayerEffectType::LongShadow:
            effect.effect_color = l->long_shadow_color;
            effect.effect_opacity = l->long_shadow_opacity;
            effect.effect_distance = l->long_shadow_length;
            effect.effect_angle = l->long_shadow_angle;
            effect.effect_falloff = l->long_shadow_falloff;
            effect.effect_size = l->long_shadow_blur;
            effect.effect_blur_type = (int)l->long_shadow_blur_type;
            break;
        default:
            break;
        }
    };
    auto make_legacy_effect = [seed_effect_from_layer](LayerEffectType type) {
        LayerEffect effect;
        effect.type = type;
        effect.enabled = true;
        if (type == LayerEffectType::DropShadow || type == LayerEffectType::LongShadow || type == LayerEffectType::InnerShadow) {
            effect.blend_mode = EffectBlendMode::Multiply;
            seed_effect_from_layer(effect);
        } else if (type == LayerEffectType::ColorOverlay)
            effect.blend_mode = EffectBlendMode::Color;
        else if (type == LayerEffectType::Glow || type == LayerEffectType::InnerGlow)
            effect.blend_mode = EffectBlendMode::Additive;
        else
            seed_effect_from_layer(effect);
        return effect;
    };
    if (!j.contains("effects")) {
        if (l->background_enabled) l->effects.push_back(make_legacy_effect(LayerEffectType::BackgroundColor));
        if (l->outline_enabled) l->effects.push_back(make_legacy_effect(LayerEffectType::Outline));
        if (l->shadow_enabled) l->effects.push_back(make_legacy_effect(LayerEffectType::DropShadow));
        if (l->long_shadow_enabled) l->effects.push_back(make_legacy_effect(LayerEffectType::LongShadow));
    } else if (l->long_shadow_enabled) {
        bool has_long_shadow_effect = false;
        for (const auto &effect : l->effects) {
            if (effect.type == LayerEffectType::LongShadow) {
                has_long_shadow_effect = true;
                break;
            }
        }
        if (!has_long_shadow_effect)
            l->effects.push_back(make_legacy_effect(LayerEffectType::LongShadow));
    }
    for (auto &effect : l->effects) {
        if ((!effect.effect_owned_style_loaded &&
             (effect.type == LayerEffectType::BackgroundColor ||
              effect.type == LayerEffectType::Outline)) ||
            ((effect.type == LayerEffectType::DropShadow || effect.type == LayerEffectType::LongShadow) &&
             effect.effect_color == 0xFFFFFFFF))
            seed_effect_from_layer(effect);
    }
    set_color_channels(*l, true, l->text_color);
    set_color_channels(*l, false, l->fill_color);
    set_stroke_color_channels(*l, l->stroke_color);
    if (j.contains("text_color_a")) l->text_color_a = aprop_from_json(j["text_color_a"], "text_color_a");
    if (j.contains("text_color_r")) l->text_color_r = aprop_from_json(j["text_color_r"], "text_color_r");
    if (j.contains("text_color_g")) l->text_color_g = aprop_from_json(j["text_color_g"], "text_color_g");
    if (j.contains("text_color_b")) l->text_color_b = aprop_from_json(j["text_color_b"], "text_color_b");
    if (j.contains("fill_color_a")) l->fill_color_a = aprop_from_json(j["fill_color_a"], "fill_color_a");
    if (j.contains("fill_color_r")) l->fill_color_r = aprop_from_json(j["fill_color_r"], "fill_color_r");
    if (j.contains("fill_color_g")) l->fill_color_g = aprop_from_json(j["fill_color_g"], "fill_color_g");
    if (j.contains("fill_color_b")) l->fill_color_b = aprop_from_json(j["fill_color_b"], "fill_color_b");
    if (j.contains("stroke_color_a")) l->stroke_color_a = aprop_from_json(j["stroke_color_a"], "stroke_color_a");
    if (j.contains("stroke_color_r")) l->stroke_color_r = aprop_from_json(j["stroke_color_r"], "stroke_color_r");
    if (j.contains("stroke_color_g")) l->stroke_color_g = aprop_from_json(j["stroke_color_g"], "stroke_color_g");
    if (j.contains("stroke_color_b")) l->stroke_color_b = aprop_from_json(j["stroke_color_b"], "stroke_color_b");
    rich_text_document_sync_layer_mirrors(*l);
    l->image_path    = bounded_string(j, "image_path", "", kMaxPathLength);
    if (object_member(j, "embedded_image") && !restore_embedded_image_asset(j, l->image_path) && require_embedded_assets) {
        if (error) *error = "Could not restore an embedded image asset from the template file.";
    }
    if (diagnostics && layer_type_is_image_like(l->type) && !l->image_path.empty() &&
        !QFileInfo::exists(QString::fromStdString(l->image_path))) {
        append_unique_import_diagnostic(
            diagnostics->missing_images,
            l->name + ": " + l->image_path);
    }
    if (diagnostics && l->type == LayerType::Video && !l->video_source.empty() &&
        !QFileInfo::exists(QString::fromStdString(l->video_source))) {
        append_unique_import_diagnostic(
            diagnostics->missing_images,
            l->name + ": " + l->video_source);
    }
    if (diagnostics && l->type == LayerType::Audio && !l->audio_source.empty() &&
        !QFileInfo::exists(QString::fromStdString(l->audio_source))) {
        append_unique_import_diagnostic(
            diagnostics->missing_audio,
            l->name + ": " + l->audio_source);
    }
    l->lock_aspect_ratio = json_bool(j, "lock_aspect_ratio", layer_type_is_image_like(l->type));
    l->image_box_lock_aspect_ratio = json_bool(j, "image_box_lock_aspect_ratio", false);
    l->scale_filter = (ImageScaleFilter)std::clamp(json_int(j, "scale_filter", (int)ImageScaleFilter::Bilinear),
                                                   0, (int)ImageScaleFilter::Area);
    const int stored_image_box_mode = std::clamp(
        json_int(j, "image_box_mode", (int)ImageBoxMode::FitImageToBox),
        0, (int)ImageBoxMode::FitToShortSide);
    const bool legacy_horizontal_crop = stored_image_box_mode == (int)ImageBoxMode::LegacyFitHorizontalCrop;
    const bool legacy_vertical_crop = stored_image_box_mode == (int)ImageBoxMode::LegacyFitVerticalCrop;
    l->image_box_mode = legacy_horizontal_crop ? ImageBoxMode::FillHorizontal
                        : legacy_vertical_crop ? ImageBoxMode::FillVertical
                                               : (ImageBoxMode)stored_image_box_mode;
    l->image_size_auto_fit = json_bool(j, "image_size_auto_fit", true);
    l->image_crop_when_outside_box = json_bool(
        j, "image_crop_when_outside_box", legacy_horizontal_crop || legacy_vertical_crop);
    if (l->image_box_mode == ImageBoxMode::StretchToFill) {
        l->image_size_auto_fit = true;
        l->lock_aspect_ratio = false;
    }
    l->image_anchor_x = (float)std::clamp(finite_or(json_double(j, "image_anchor_x", 0.5), 0.5), 0.0, 1.0);
    l->image_anchor_y = (float)std::clamp(finite_or(json_double(j, "image_anchor_y", 0.5), 0.5), 0.0, 1.0);
    l->image_width = (float)std::clamp(finite_or(json_double(j, "image_width", 1920.0), 1920.0), 0.0, (double)kMaxCanvasDimension);
    l->image_height = (float)std::clamp(finite_or(json_double(j, "image_height", 1080.0), 1080.0), 0.0, (double)kMaxCanvasDimension);
    l->image_size.static_value.x = l->image_width;
    l->image_size.static_value.y = l->image_height;
    if (j.contains("image_size")) vec2_aprop_from_json(j["image_size"], l->image_size);
    l->image_size.static_value.x = std::clamp(l->image_size.static_value.x, 0.0, (double)kMaxCanvasDimension);
    l->image_size.static_value.y = std::clamp(l->image_size.static_value.y, 0.0, (double)kMaxCanvasDimension);

    if (!j.contains("position") || !vector_payload_has_z(j["position"]))
        promote_legacy_scalar_z_track(l->position, l->position_z);
    if (!j.contains("scale") || !vector_payload_has_z(j["scale"]))
        promote_legacy_scalar_z_track(l->scale, l->scale_z);
    if (!j.contains("origin") || !vector_payload_has_z(j["origin"]))
        promote_legacy_scalar_z_track(l->origin_prop, l->anchor_z);

    l->scale.static_value.z = std::clamp(l->scale.static_value.z, -100.0, 100.0);
    l->origin_prop.static_value.z = std::clamp(
        l->origin_prop.static_value.z, -1000000.0, 1000000.0);
    return l;
}

std::string serialize_layer_effect_stack_json(
    const std::vector<LayerEffect> &effects)
{
    Layer carrier;
    carrier.effects = effects;
    const json serialized_layer = layer_to_json(
        carrier, false, false, nullptr, nullptr, false);
    json root = {
        {"format", "flux-motion-effect-stack"},
        {"version", 1},
        {"effects", serialized_layer.value("effects", json::array())}
    };
    return root.dump();
}

bool deserialize_layer_effect_stack_json(
    const std::string &payload, std::vector<LayerEffect> *effects,
    std::string *error)
{
    if (!effects) {
        if (error) *error = "No destination effect stack was supplied.";
        return false;
    }
    const json root = json::parse(payload, nullptr, false);
    if (!root.is_object() ||
        root.value("format", std::string()) !=
            "flux-motion-effect-stack" ||
        root.value("version", 0) != 1 ||
        !root.contains("effects") || !root["effects"].is_array()) {
        if (error) *error = "The effect stack payload is not valid or uses an unsupported version.";
        return false;
    }
    json carrier_json = {
        {"id", "effect-stack-carrier"},
        {"name", "Effect Stack"},
        {"type", static_cast<int>(LayerType::Adjustment)},
        {"effects", root["effects"]}
    };
    auto carrier = layer_from_json(carrier_json, false, error, nullptr);
    if (!carrier) {
        if (error && error->empty()) *error = "Could not deserialize the effect stack.";
        return false;
    }
    *effects = std::move(carrier->effects);
    return true;
}

static json camera_to_json(const TitleCamera &camera)
{
    const json source_passthrough = passthrough_json_object(camera.serialization_passthrough_json);
    json result = source_passthrough;
    result.update(json{
        {"id", camera.id},
        {"name", camera.name},
        {"asset_space_owner_id", camera.asset_space_owner_id},
        {"use_canvas_default", camera.use_canvas_default},
        {"position_x", aprop_to_json(camera.position_x)},
        {"position_y", aprop_to_json(camera.position_y)},
        {"position_z", aprop_to_json(camera.position_z)},
        {"position_3d_path_enabled", camera.position_3d_path_enabled},
        {"target_x", aprop_to_json(camera.target_x)},
        {"target_y", aprop_to_json(camera.target_y)},
        {"target_z", aprop_to_json(camera.target_z)},
        {"target_3d_path_enabled", camera.target_3d_path_enabled},
        {"orientation_x", aprop_to_json(camera.orientation_x)},
        {"orientation_y", aprop_to_json(camera.orientation_y)},
        {"orientation_z", aprop_to_json(camera.orientation_z)},
        {"rotation_x", aprop_to_json(camera.rotation_x)},
        {"rotation_y", aprop_to_json(camera.rotation_y)},
        {"rotation_z", aprop_to_json(camera.rotation_z)},
        {"focal_length", aprop_to_json(camera.focal_length)},
        {"field_of_view", aprop_to_json(camera.field_of_view)},
        {"zoom", aprop_to_json(camera.zoom)},
        {"near_clip", aprop_to_json(camera.near_clip)},
        {"far_clip", aprop_to_json(camera.far_clip)},
        {"projection_mode", aprop_to_json(camera.projection_mode)},
        {"projection", static_cast<int>(camera.projection)},
        {"timeline_expanded", camera.timeline_expanded},
    });
    if (camera.position_3d_path_enabled)
        result["position_3d"] = vec3_aprop_to_json(camera.position_3d);
    else
        result.erase("position_3d");
    if (camera.target_3d_path_enabled)
        result["target_3d"] = vec3_aprop_to_json(camera.target_3d);
    else
        result.erase("target_3d");
    return merge_surviving_passthrough(source_passthrough, result);
}

static TitleCamera camera_from_json(const json &j, size_t index)
{
    TitleCamera camera;
    if (!j.is_object())
        return camera;
    camera.serialization_passthrough_json = j.dump();
    camera.id = bounded_string(j, "id", index == 0 ? "default" : TitleDataStore::make_uuid(), kMaxNameLength);
    camera.name = bounded_string(j, "name", index == 0 ? "Default Camera" : "Camera", kMaxNameLength);
    camera.asset_space_owner_id = bounded_string(
        j, "asset_space_owner_id", "", kMaxNameLength);
    camera.use_canvas_default = json_bool(j, "use_canvas_default", index == 0);
    auto read_prop = [&](const char *key, AnimatedProperty &property) {
        if (j.contains(key))
            property = aprop_from_json(j[key], property.name);
    };
    read_prop("position_x", camera.position_x);
    read_prop("position_y", camera.position_y);
    read_prop("position_z", camera.position_z);
    const bool has_position_3d = j.contains("position_3d");
    camera.position_3d_path_enabled = json_bool(
        j, "position_3d_path_enabled", has_position_3d);
    if (has_position_3d)
        vec3_aprop_from_json(j["position_3d"], camera.position_3d);
    read_prop("target_x", camera.target_x);
    read_prop("target_y", camera.target_y);
    read_prop("target_z", camera.target_z);
    const bool has_target_3d = j.contains("target_3d");
    camera.target_3d_path_enabled = json_bool(
        j, "target_3d_path_enabled", has_target_3d);
    if (has_target_3d)
        vec3_aprop_from_json(j["target_3d"], camera.target_3d);
    read_prop("orientation_x", camera.orientation_x);
    read_prop("orientation_y", camera.orientation_y);
    read_prop("orientation_z", camera.orientation_z);
    read_prop("rotation_x", camera.rotation_x);
    read_prop("rotation_y", camera.rotation_y);
    read_prop("rotation_z", camera.rotation_z);
    read_prop("focal_length", camera.focal_length);
    read_prop("field_of_view", camera.field_of_view);
    read_prop("zoom", camera.zoom);
    read_prop("near_clip", camera.near_clip);
    read_prop("far_clip", camera.far_clip);
    camera.focal_length.static_value = std::clamp(camera.focal_length.static_value, 1.0, 1000000.0);
    camera.field_of_view.static_value = std::clamp(camera.field_of_view.static_value, 0.1, 179.0);
    camera.zoom.static_value = std::clamp(camera.zoom.static_value, 0.0001, 10000.0);
    camera.near_clip.static_value = std::clamp(camera.near_clip.static_value, 0.0001, 1000000.0);
    camera.far_clip.static_value = std::max(camera.near_clip.static_value + 0.001,
                                             std::clamp(camera.far_clip.static_value, 0.001, 1000000000.0));
    camera.projection = static_cast<CameraProjection>(std::clamp(
        json_int(j, "projection", static_cast<int>(CameraProjection::Perspective)),
        static_cast<int>(CameraProjection::Perspective),
        static_cast<int>(CameraProjection::Orthographic)));
    camera.projection_mode.static_value = static_cast<double>(camera.projection);
    if (j.contains("projection_mode"))
        camera.projection_mode = aprop_from_json(j["projection_mode"], "camera_projection");
    camera.projection_mode.static_value = std::clamp(camera.projection_mode.static_value, 0.0, 1.0);
    for (Keyframe &keyframe : camera.projection_mode.keyframes) {
        keyframe.value = std::clamp(std::round(keyframe.value), 0.0, 1.0);
        keyframe.easing = EasingType::Hold;
        keyframe.temporal_mode = TemporalInterpolationMode::Hold;
        keyframe.temporal_velocity_explicit = true;
    }
    camera.projection = static_cast<CameraProjection>(
        static_cast<int>(std::round(camera.projection_mode.static_value)));
    camera.timeline_expanded = json_bool(j, "timeline_expanded", false);
    return camera;
}

std::string serialize_layer_clipboard_json(
    const std::vector<std::shared_ptr<Layer>> &layers,
    const std::vector<TitleCamera> &cameras,
    const std::string &source_title_id)
{
    json root = {
        {"format", "flux-motion-layer-clipboard"},
        {"version", 1},
        {"source_title_id", source_title_id},
        {"layers", json::array()},
        {"cameras", json::array()}
    };
    for (const auto &layer : layers) {
        if (layer)
            root["layers"].push_back(layer_to_json(*layer, true, false, nullptr, nullptr, false));
    }
    for (const TitleCamera &camera : cameras)
        root["cameras"].push_back(camera_to_json(camera));
    return root.dump();
}

bool deserialize_layer_clipboard_json(
    const std::string &payload,
    std::vector<std::shared_ptr<Layer>> *layers,
    std::vector<TitleCamera> *cameras,
    std::string *source_title_id,
    std::string *error)
{
    if (!layers || !cameras || !source_title_id) {
        if (error) *error = "No clipboard destination was supplied.";
        return false;
    }
    const json root = json::parse(payload, nullptr, false);
    if (!root.is_object() ||
        root.value("format", std::string()) !=
            "flux-motion-layer-clipboard" ||
        root.value("version", 0) != 1 ||
        !root.contains("layers") || !root["layers"].is_array() ||
        !root.contains("cameras") || !root["cameras"].is_array()) {
        if (error) *error = "The layer clipboard payload is invalid or unsupported.";
        return false;
    }
    std::vector<std::shared_ptr<Layer>> decoded_layers;
    std::vector<TitleCamera> decoded_cameras;
    decoded_layers.reserve(root["layers"].size());
    decoded_cameras.reserve(root["cameras"].size());
    for (const auto &item : root["layers"]) {
        auto layer = layer_from_json(item, false, error, nullptr);
        if (!layer) {
            if (error && error->empty()) *error = "A copied layer could not be decoded.";
            return false;
        }
        decoded_layers.push_back(std::move(layer));
    }
    for (size_t index = 0; index < root["cameras"].size(); ++index)
        decoded_cameras.push_back(camera_from_json(root["cameras"][index], index));
    if (decoded_layers.empty()) {
        if (error) *error = "The layer clipboard is empty.";
        return false;
    }
    *layers = std::move(decoded_layers);
    *cameras = std::move(decoded_cameras);
    *source_title_id = bounded_string(root, "source_title_id", "", kMaxNameLength);
    return true;
}

static json light_to_json(const TitleLight &light)
{
    const json source_passthrough = passthrough_json_object(
        light.serialization_passthrough_json);
    json result = source_passthrough;
    result.update(json{
        {"id", light.id},
        {"name", light.name},
        {"enabled", light.enabled},
        {"type", static_cast<int>(light.type)},
        {"position", vec3_aprop_to_json(light.position)},
        {"target", vec3_aprop_to_json(light.target)},
        {"color_a", aprop_to_json(light.color_a)},
        {"color_r", aprop_to_json(light.color_r)},
        {"color_g", aprop_to_json(light.color_g)},
        {"color_b", aprop_to_json(light.color_b)},
        {"intensity", aprop_to_json(light.intensity)},
        {"source_size", aprop_to_json(light.source_size)},
        {"falloff", static_cast<int>(light.falloff)},
        {"falloff_start", aprop_to_json(light.falloff_start)},
        {"falloff_distance", aprop_to_json(light.falloff_distance)},
        {"cone_angle", aprop_to_json(light.cone_angle)},
        {"cone_feather", aprop_to_json(light.cone_feather)},
        {"casts_shadows", light.casts_shadows},
        {"shadow_darkness", aprop_to_json(light.shadow_darkness)},
        {"shadow_softness", aprop_to_json(light.shadow_softness)},
        {"shadow_bias", aprop_to_json(light.shadow_bias)},
        {"environment_path", light.environment_path},
        {"environment_rotation", aprop_to_json(light.environment_rotation)},
        {"environment_visible", light.environment_visible},
        {"timeline_expanded", light.timeline_expanded},
    });
    return merge_surviving_passthrough(source_passthrough, result);
}

static TitleLight light_from_json(const json &j, size_t index)
{
    TitleLight light;
    light.id = TitleDataStore::make_uuid();
    light.name = index == 0 ? "Key Light" : "Light";
    if (!j.is_object())
        return light;
    light.serialization_passthrough_json = j.dump();
    light.id = bounded_string(j, "id", light.id, kMaxNameLength);
    light.name = bounded_string(j, "name", light.name, kMaxNameLength);
    light.enabled = json_bool(j, "enabled", true);
    light.type = static_cast<TitleLightType>(std::clamp(
        json_int(j, "type", static_cast<int>(TitleLightType::Parallel)),
        static_cast<int>(TitleLightType::Ambient),
        static_cast<int>(TitleLightType::Environment)));
    if (j.contains("position"))
        vec3_aprop_from_json(j["position"], light.position);
    if (j.contains("target"))
        vec3_aprop_from_json(j["target"], light.target);
    auto read_prop = [&](const char *key, AnimatedProperty &property,
                         double minimum, double maximum) {
        if (j.contains(key))
            property = aprop_from_json(j[key], property.name);
        property.static_value = std::clamp(
            finite_or(property.static_value, property.static_value), minimum, maximum);
        for (Keyframe &keyframe : property.keyframes)
            keyframe.value = std::clamp(
                finite_or(keyframe.value, property.static_value), minimum, maximum);
    };
    read_prop("color_a", light.color_a, 0.0, 255.0);
    read_prop("color_r", light.color_r, 0.0, 255.0);
    read_prop("color_g", light.color_g, 0.0, 255.0);
    read_prop("color_b", light.color_b, 0.0, 255.0);
    read_prop("intensity", light.intensity, 0.0, 100000.0);
    read_prop("source_size", light.source_size, 0.1, 100000.0);
    light.falloff = static_cast<TitleLightFalloff>(std::clamp(
        json_int(j, "falloff", static_cast<int>(TitleLightFalloff::InverseSquare)),
        static_cast<int>(TitleLightFalloff::None),
        static_cast<int>(TitleLightFalloff::InverseSquare)));
    read_prop("falloff_start", light.falloff_start, 0.0, 10000000.0);
    read_prop("falloff_distance", light.falloff_distance, 0.001, 10000000.0);
    read_prop("cone_angle", light.cone_angle, 0.1, 179.0);
    read_prop("cone_feather", light.cone_feather, 0.0, 100.0);
    light.casts_shadows = json_bool(j, "casts_shadows", false);
    read_prop("shadow_darkness", light.shadow_darkness, 0.0, 100.0);
    read_prop("shadow_softness", light.shadow_softness, 0.0, 64.0);
    read_prop("shadow_bias", light.shadow_bias, 0.000001, 0.1);
    light.environment_path = bounded_string(
        j, "environment_path", "", kMaxPathLength);
    read_prop("environment_rotation", light.environment_rotation,
              -360000.0, 360000.0);
    light.environment_visible = json_bool(j, "environment_visible", false);
    light.timeline_expanded = json_bool(j, "timeline_expanded", false);
    return light;
}

static json title_to_json(const Title &t, bool include_embedded_assets = true,
                          bool require_embedded_assets = false, std::string *error = nullptr)
{
    json jt = passthrough_json_object(t.serialization_passthrough_json);
    const json source_passthrough = jt;
    jt["schema_version"] = fxm::serialization::kCurrentTitleSchemaVersion;
    jt["development_version"] = fxm::serialization::kCurrentDevelopmentVersion;
    jt["id"]       = t.id;
    jt["name"]     = t.name;
    if (!t.description.empty()) jt["description"] = t.description; else jt.erase("description");
    if (!t.creator.empty()) jt["creator"] = t.creator; else jt.erase("creator");
    if (!t.creation_date.empty()) jt["creation_date"] = t.creation_date; else jt.erase("creation_date");
    jt["duration"] = t.duration;
    jt["loop_start"] = t.loop_start;
    jt["loop_end"] = t.loop_end;
    jt["playback_mode"] = t.playback_mode;
    jt["loop_type"] = t.loop_type;
    jt["cue_end_behavior"] = t.cue_end_behavior;
    jt["pause_time"] = t.pause_time;
    jt["bg_color"] = t.bg_color;
    jt["width"]    = t.width;
    jt["height"]   = t.height;
    jt["frame_rate"] = t.frame_rate;
    jt["graphic_type"] = static_cast<int>(t.graphic_type);
    jt["active_camera_id"] = t.active_camera.static_value;
    jt["active_camera"] = discrete_property_to_json(t.active_camera);
    jt["camera_switches_expanded"] = t.camera_switches_expanded;
    if (!t.expanded_property_channels.empty()) {
        json expanded = json::array();
        for (const std::string &key : t.expanded_property_channels)
            expanded.push_back(key);
        jt["expanded_property_channels"] = std::move(expanded);
    } else {
        jt.erase("expanded_property_channels");
    }
    json cameras = json::array();
    for (const auto &camera : t.cameras)
        cameras.push_back(camera_to_json(camera));
    jt["cameras"] = std::move(cameras);
    json lights = json::array();
    for (const auto &light : t.lights)
        lights.push_back(light_to_json(light));
    jt["lights"] = std::move(lights);
    jt["default_light_enabled"] = t.default_light_enabled;
    jt["lighting_enabled"] = t.lighting_enabled;
    jt["environment_exposure"] = t.environment_exposure;
    if (t.graphic_type == TitleGraphicType::Stinger) {
        jt["stinger_transition_point"] = t.stinger_transition_point;
        jt["stinger_audio_enabled"] = t.stinger_audio_enabled;
        jt["stinger_alpha_output"] = t.stinger_alpha_output;
        jt["stinger_pre_roll"] = t.stinger_pre_roll;
        jt["stinger_post_roll"] = t.stinger_post_roll;
        jt["stinger_render_mode"] = static_cast<int>(t.stinger_render_mode);
        jt["stinger_switch_mode"] = static_cast<int>(t.stinger_switch_mode);
        jt["stinger_editor_background"] = static_cast<int>(t.stinger_editor_background);
    } else {
        jt.erase("stinger_transition_point");
        jt.erase("stinger_audio_enabled");
        jt.erase("stinger_alpha_output");
        jt.erase("stinger_pre_roll");
        jt.erase("stinger_post_roll");
        jt.erase("stinger_render_mode");
        jt.erase("stinger_switch_mode");
        jt.erase("stinger_editor_background");
    }
    if (t.proxy_metadata.schema_version > 0 || !t.proxy_metadata.content_hash.empty() ||
        !t.proxy_metadata.proxy_path.empty() || t.proxy_metadata.complete) {
        jt["proxy_metadata"] = {
            {"schema_version", fxm::serialization::kCurrentProxyManifestSchemaVersion},
            {"content_hash", t.proxy_metadata.content_hash},
            {"cache_namespace", t.proxy_metadata.cache_namespace},
            {"proxy_path", t.proxy_metadata.proxy_path},
            {"generated_at", t.proxy_metadata.generated_at},
            {"generated_development_version", t.proxy_metadata.generated_development_version},
            {"width", t.proxy_metadata.width},
            {"height", t.proxy_metadata.height},
            {"frame_rate", t.proxy_metadata.frame_rate},
            {"frame_count", t.proxy_metadata.frame_count},
            {"has_audio", t.proxy_metadata.has_audio},
            {"complete", t.proxy_metadata.complete},
        };
    } else {
        jt.erase("proxy_metadata");
    }
    jt["is_asset"] = t.is_asset;
    jt["asset_animated"] = t.asset_animated;
    jt["asset_category"] = t.asset_category;
    if (!t.asset_tags.empty())
        jt["asset_tags"] = t.asset_tags;
    else
        jt.erase("asset_tags");
    if (!t.packed_font_files.empty())
        jt["packed_font_files"] = t.packed_font_files;
    else
        jt.erase("packed_font_files");
    if (t.editor_default_style_enabled) {
        json defaults = layer_to_json(t.editor_default_layer_style, false, false, nullptr, nullptr);
        defaults.erase("effects");
        defaults.erase("effect_stack_respects_masks");
        // Keep an explicit empty array so loading this defaults object never
        // reconstructs legacy shadow/background/outline effects.
        defaults["effects"] = json::array();
        jt["editor_default_layer_style"] = defaults;
        jt["editor_default_foreground_color"] = t.editor_default_foreground_color;
        jt["editor_default_background_color"] = t.editor_default_background_color;
    } else {
        jt.erase("editor_default_layer_style");
        jt.erase("editor_default_foreground_color");
        jt.erase("editor_default_background_color");
    }
    if (!t.editor_recent_color_hexes.empty()) {
        json recent = json::array();
        for (const auto &hex : t.editor_recent_color_hexes) {
            if (!hex.empty())
                recent.push_back(hex);
        }
        jt["editor_recent_color_hexes"] = recent;
    } else {
        jt.erase("editor_recent_color_hexes");
    }
    json layers = json::array();
    for (auto &l : t.layers) {
        bool asset_embed_failed = false;
        layers.push_back(layer_to_json(*l, include_embedded_assets, require_embedded_assets, error, &asset_embed_failed));
        if (require_embedded_assets && asset_embed_failed) {
            if (error && error->empty())
                *error = "Could not embed an image asset in the template file.";
            return {};
        }
    }
    jt["layers"] = layers;
    json live_rows = json::array();
    for (const auto &row : t.live_text_rows)
        live_rows.push_back(row);
    jt["live_text_rows"] = live_rows;
    jt["live_text_row_ids"] = t.live_text_row_ids;
    jt["live_text_column_order"] = t.live_text_column_order;
    if (!t.live_text_cue_style_overrides.empty()) {
        json style_overrides = json::array();
        for (const auto &entry : t.live_text_cue_style_overrides) {
            if (entry.row_id.empty() || entry.layer_id.empty() ||
                (!entry.fill_color_set && !entry.stroke_color_set))
                continue;
            json item;
            item["row_id"] = entry.row_id;
            item["layer_id"] = entry.layer_id;
            if (entry.fill_color_set)
                item["fill_color"] = entry.fill_color;
            if (entry.stroke_color_set)
                item["stroke_color"] = entry.stroke_color;
            style_overrides.push_back(std::move(item));
        }
        if (!style_overrides.empty())
            jt["live_text_cue_style_overrides"] = std::move(style_overrides);
        else
            jt.erase("live_text_cue_style_overrides");
    } else {
        jt.erase("live_text_cue_style_overrides");
    }
    if (!t.live_text_external_bindings.empty()) {
        json cell_bindings = json::array();
        for (const auto &cell : t.live_text_external_bindings)
            cell_bindings.push_back(live_text_external_binding_to_json(cell));
        jt["live_text_external_bindings"] = std::move(cell_bindings);
    } else {
        jt.erase("live_text_external_bindings");
    }
    if (!t.live_text_table_bindings.empty()) {
        json table_bindings = json::array();
        for (const auto &mapping : t.live_text_table_bindings)
            table_bindings.push_back(live_text_table_binding_to_json(mapping));
        jt["live_text_table_bindings"] = std::move(table_bindings);
    } else {
        jt.erase("live_text_table_bindings");
    }
    jt["live_text_header_state"] = t.live_text_header_state;
    if (!t.external_data_sources.empty()) {
        json external_sources = json::array();
        for (size_t index = 0; index < t.external_data_sources.size(); ++index) {
            const auto &source = t.external_data_sources[index];
            external_sources.push_back(external_source_to_json(
                source, passthrough_array_item(source_passthrough, "external_data_sources", index)));
        }
        jt["external_data_sources"] = std::move(external_sources);
    } else {
        jt.erase("external_data_sources");
    }
    jt["external_data_enabled"] = t.external_data_enabled;
    jt["playlist_loop"] = t.playlist_loop;
    jt["playlist_reverse"] = t.playlist_reverse;
    jt["playlist_restart_on_source_active"] = t.playlist_restart_on_source_active;
    jt["playlist_stop_on_source_inactive"] = t.playlist_stop_on_source_inactive;
    jt["playlist_hold_seconds"] = t.playlist_hold_seconds;
    if (!t.preview_screenshot_png_base64.empty())
        jt["preview_screenshot_png_base64"] = t.preview_screenshot_png_base64;
    else
        jt.erase("preview_screenshot_png_base64");
    json merged = merge_surviving_passthrough(source_passthrough, jt);
    merged["layers"] = jt["layers"];
    merged["cameras"] = jt["cameras"];
    merged["lights"] = jt["lights"];
    if (jt.contains("external_data_sources"))
        merged["external_data_sources"] = jt["external_data_sources"];
    return merged;
}

static std::shared_ptr<Title> title_from_json(const json &input, bool regenerate_ids,
                                               bool require_embedded_assets = false, std::string *error = nullptr,
                                               TitleImportDiagnostics *diagnostics = nullptr)
{
    if (!input.is_object())
        throw std::runtime_error("Title entry root must be a JSON object.");

    fxm::serialization::MigrationReport migration_report;
    const json jt = fxm::serialization::migrate_title_json(input, &migration_report);
    auto t = std::make_shared<Title>();
    if (!jt.is_object())
        return t;
    {
        json title_passthrough = jt;
        title_passthrough.erase("layers");
        title_passthrough.erase("cameras");
        title_passthrough.erase("lights");
        t->serialization_passthrough_json = title_passthrough.dump();
    }
    if (!migration_report.recoveries.empty() || !migration_report.warnings.empty()) {
        FXM_LOG_WARNING("Serialization", QStringLiteral(
            "Recovered title JSON sourceSchema=%1 sourceDevelopment=%2 recoveries=%3 warnings=%4")
            .arg(migration_report.source_schema_version)
            .arg(migration_report.source_development_version)
            .arg(static_cast<int>(migration_report.recoveries.size()))
            .arg(static_cast<int>(migration_report.warnings.size())));
        constexpr size_t kMaxLoggedMigrationDetails = 8;
        for (size_t index = 0; index < std::min(kMaxLoggedMigrationDetails, migration_report.recoveries.size()); ++index)
            FXM_LOG_WARNING("Serialization", QStringLiteral("Recovery[%1]: %2")
                .arg(static_cast<int>(index))
                .arg(QString::fromStdString(migration_report.recoveries[index])));
        for (size_t index = 0; index < std::min(kMaxLoggedMigrationDetails, migration_report.warnings.size()); ++index)
            FXM_LOG_WARNING("Serialization", QStringLiteral("Warning[%1]: %2")
                .arg(static_cast<int>(index))
                .arg(QString::fromStdString(migration_report.warnings[index])));
    }

    t->id       = bounded_string(jt, "id", TitleDataStore::make_uuid(), kMaxNameLength);
    t->name     = bounded_string(jt, "name", "Untitled", kMaxNameLength);
    t->description = bounded_string(jt, "description", "", kMaxTextLength);
    t->creator = bounded_string(jt, "creator", "", kMaxNameLength);
    t->creation_date = bounded_string(jt, "creation_date", "", kMaxNameLength);
    t->duration = std::clamp(finite_or(json_double(jt, "duration", 5.0), 5.0), 0.1, kMaxDuration);
    t->loop_start = std::clamp(finite_or(json_double(jt, "loop_start", std::min(1.0, t->duration)), 0.0), 0.0, t->duration);
    t->loop_end = std::clamp(finite_or(json_double(jt, "loop_end", std::max(t->loop_start, t->duration - 1.0)), t->duration), t->loop_start, t->duration);
    t->playback_mode = std::clamp(json_int(jt, "playback_mode", 0), 0, 2);
    t->loop_type = std::clamp(json_int(jt, "loop_type", 0), 0, 1);
    t->cue_end_behavior = std::clamp(json_int(jt, "cue_end_behavior", 0), 0, 2);
    t->pause_time = std::clamp(finite_or(json_double(jt, "pause_time", 0.0), 0.0), 0.0, t->duration);
    t->bg_color = json_color(jt, "bg_color", (uint32_t)0x00000000);
    t->width    = std::clamp(json_int(jt, "width", 1920), 1, kMaxCanvasDimension);
    t->height   = std::clamp(json_int(jt, "height", 1080), 1, kMaxCanvasDimension);
    t->frame_rate = std::clamp(
        finite_or(json_double(jt, "frame_rate", 30.0), 30.0), 1.0, 240.0);
    const bool has_explicit_graphic_type = jt.contains("graphic_type");
    t->graphic_type = static_cast<TitleGraphicType>(std::clamp(
        json_int(jt, "graphic_type", 0),
        static_cast<int>(TitleGraphicType::Title),
        static_cast<int>(TitleGraphicType::Stinger)));
    t->cameras.clear();
    if (jt.contains("cameras") && jt["cameras"].is_array()) {
        /* Asset-space camera snapshots are title-owned for self-contained
         * playback. Keep a defensive bound, but do not truncate ordinary
         * multi-asset documents at the legacy 32-camera UI limit. */
        const size_t camera_count = std::min<size_t>(jt["cameras"].size(), 4096);
        for (size_t i = 0; i < camera_count; ++i)
            t->cameras.push_back(camera_from_json(jt["cameras"][i], i));
    }
    if (t->cameras.empty())
        t->cameras.push_back(TitleCamera{});
    {
        std::unordered_set<std::string> camera_ids;
        for (size_t i = 0; i < t->cameras.size(); ++i) {
            TitleCamera &camera = t->cameras[i];
            if (camera.id.empty() || !camera_ids.insert(camera.id).second) {
                camera.id = i == 0 ? "default" : TitleDataStore::make_uuid();
                while (!camera_ids.insert(camera.id).second)
                    camera.id = TitleDataStore::make_uuid();
            }
            if (camera.name.empty())
                camera.name = i == 0 ? "Default Camera" : "Camera";
        }
    }
    t->active_camera_id = bounded_string(jt, "active_camera_id", t->cameras.front().id, kMaxNameLength);
    t->active_camera = jt.contains("active_camera")
        ? discrete_property_from_json(jt["active_camera"], "active_camera", t->active_camera_id)
        : AnimatedDiscreteProperty{"active_camera", t->active_camera_id};
    t->camera_switches_expanded = json_bool(jt, "camera_switches_expanded", false);
    t->lights.clear();
    if (jt.contains("lights") && jt["lights"].is_array()) {
        const size_t light_count = std::min<size_t>(jt["lights"].size(), 32);
        std::unordered_set<std::string> light_ids;
        for (size_t i = 0; i < light_count; ++i) {
            TitleLight light = light_from_json(jt["lights"][i], i);
            if (light.id.empty() || !light_ids.insert(light.id).second) {
                do { light.id = TitleDataStore::make_uuid(); }
                while (!light_ids.insert(light.id).second);
            }
            if (light.name.empty())
                light.name = "Light";
            t->lights.push_back(std::move(light));
        }
    }
    /* Development Version 300 migration: legacy title-level lights become
     * ordinary non-raster layers exactly once. */
    bool has_light_layers = std::any_of(t->layers.begin(), t->layers.end(), [](const auto &layer) { return layer && layer->type == LayerType::Light; });
    if (!has_light_layers && !t->lights.empty()) {
        for (const TitleLight &legacy : t->lights) {
            auto layer = std::make_shared<Layer>();
            layer->id = legacy.id.empty() ? TitleDataStore::make_uuid() : legacy.id;
            layer->name = legacy.name; layer->type = LayerType::Light;
            layer->visible = legacy.enabled; layer->dimension_mode = LayerDimensionMode::ThreeD;
            layer->light = legacy; layer->light.id = layer->id; layer->light.name = layer->name;
            t->layers.push_back(std::move(layer));
        }
        t->lights.clear();
    }
    t->default_light_enabled = json_bool(jt, "default_light_enabled", true);
    t->lighting_enabled = json_bool(jt, "lighting_enabled", true);
    t->environment_exposure = std::clamp(
        finite_or(json_double(jt, "environment_exposure", 0.0), 0.0),
        -16.0, 16.0);
    t->expanded_property_channels.clear();
    if (jt.contains("expanded_property_channels") &&
        jt["expanded_property_channels"].is_array()) {
        const size_t count = std::min<size_t>(
            jt["expanded_property_channels"].size(), 4096);
        for (size_t index = 0; index < count; ++index) {
            const json &value = jt["expanded_property_channels"][index];
            if (!value.is_string()) continue;
            const std::string key = value.get<std::string>();
            if (!key.empty() && key.size() <= 1024)
                t->expanded_property_channels.insert(key);
        }
    }
    if (std::none_of(t->cameras.begin(), t->cameras.end(), [&](const TitleCamera &camera) {
            return camera.id == t->active_camera.static_value;
        }))
        t->active_camera.static_value = t->cameras.front().id;
    t->active_camera_id = t->active_camera.static_value;
    for (DiscreteKeyframe &keyframe : t->active_camera.keyframes) {
        if (std::none_of(t->cameras.begin(), t->cameras.end(),
                [&](const TitleCamera &camera) { return camera.id == keyframe.value; }))
            keyframe.value = t->active_camera_id;
    }
    t->stinger_transition_point = finite_or(json_double(
        jt, "stinger_transition_point", t->duration * 0.5), t->duration * 0.5);
    /* Legacy transition-point display-mode values are intentionally ignored.
     * Transition points are now always presented as timecode. */
    t->stinger_audio_enabled = json_bool(jt, "stinger_audio_enabled", true);
    t->stinger_alpha_output = json_bool(jt, "stinger_alpha_output", true);
    t->stinger_pre_roll = std::clamp(finite_or(json_double(jt, "stinger_pre_roll", 0.0), 0.0), 0.0, kMaxDuration);
    t->stinger_post_roll = std::clamp(finite_or(json_double(jt, "stinger_post_roll", 0.0), 0.0), 0.0, kMaxDuration);
    t->stinger_render_mode = static_cast<StingerRenderMode>(std::clamp(
        json_int(jt, "stinger_render_mode", 0),
        static_cast<int>(StingerRenderMode::ProceduralLive),
        static_cast<int>(StingerRenderMode::PrerenderedProxy)));
    t->stinger_switch_mode = static_cast<StingerSwitchMode>(std::clamp(
        json_int(jt, "stinger_switch_mode", 0),
        static_cast<int>(StingerSwitchMode::SwitchAtPoint),
        static_cast<int>(StingerSwitchMode::ManualSceneAnimation)));
    int stinger_editor_background = json_int(
        jt, "stinger_editor_background",
        static_cast<int>(StingerEditorBackground::FollowSwitchPoint));
    /* v168-v169 stored a static Scene A or Scene B choice. The canvas preview
     * now represents the actual transition, so both legacy values migrate to
     * the automatic A-before/B-after switch-point background. */
    if (stinger_editor_background == static_cast<int>(StingerEditorBackground::SceneA) ||
        stinger_editor_background == static_cast<int>(StingerEditorBackground::SceneB))
        stinger_editor_background = static_cast<int>(StingerEditorBackground::FollowSwitchPoint);
    t->stinger_editor_background = static_cast<StingerEditorBackground>(std::clamp(
        stinger_editor_background,
        static_cast<int>(StingerEditorBackground::CanvasTransparency),
        static_cast<int>(StingerEditorBackground::FollowSwitchPoint)));
    set_stinger_transition_point_seconds(*t, t->stinger_transition_point);
    if (jt.contains("proxy_metadata") && jt["proxy_metadata"].is_object()) {
        const json &proxy = jt["proxy_metadata"];
        t->proxy_metadata.schema_version = std::clamp(
            json_int(proxy, "schema_version", 0), 0,
            fxm::serialization::kCurrentProxyManifestSchemaVersion);
        t->proxy_metadata.content_hash = bounded_string(proxy, "content_hash", "", 512);
        t->proxy_metadata.cache_namespace = bounded_string(proxy, "cache_namespace", "", kMaxNameLength);
        t->proxy_metadata.proxy_path = bounded_string(proxy, "proxy_path", "", kMaxPathLength);
        t->proxy_metadata.generated_at = bounded_string(proxy, "generated_at", "", kMaxNameLength);
        t->proxy_metadata.generated_development_version = std::clamp(
            json_int(proxy, "generated_development_version", 0), 0, 1000000);
        t->proxy_metadata.width = std::clamp(json_int(proxy, "width", 0), 0, kMaxCanvasDimension);
        t->proxy_metadata.height = std::clamp(json_int(proxy, "height", 0), 0, kMaxCanvasDimension);
        t->proxy_metadata.frame_rate = std::clamp(
            finite_or(json_double(proxy, "frame_rate", 0.0), 0.0), 0.0, 1000.0);
        t->proxy_metadata.frame_count = std::clamp(
            json_int(proxy, "frame_count", 0), 0, 100000000);
        t->proxy_metadata.has_audio = json_bool(proxy, "has_audio", false);
        t->proxy_metadata.complete = json_bool(proxy, "complete", false);
        /* Proxy metadata is advisory. A stale/missing file is treated as an
         * unavailable proxy and never prevents the title from loading. */
        if (!t->proxy_metadata.proxy_path.empty() &&
            !QFileInfo::exists(QString::fromStdString(t->proxy_metadata.proxy_path)))
            t->proxy_metadata.complete = false;
    }
    t->is_asset = json_bool(jt, "is_asset", false);
    t->asset_animated = json_bool(jt, "asset_animated", false);
    t->asset_category = bounded_string(jt, "asset_category", "Default", kMaxNameLength);
    if (jt.contains("asset_tags") && jt["asset_tags"].is_array()) {
        const size_t count = std::min<size_t>(jt["asset_tags"].size(), 32);
        for (size_t index = 0; index < count; ++index) {
            if (!jt["asset_tags"][index].is_string())
                continue;
            const std::string tag = jt["asset_tags"][index].get<std::string>();
            if (!tag.empty() && tag.size() <= kMaxNameLength)
                t->asset_tags.push_back(tag);
        }
    }
    if (jt.contains("packed_font_files") && jt["packed_font_files"].is_array()) {
        const size_t count = std::min<size_t>(jt["packed_font_files"].size(), 256);
        for (size_t index = 0; index < count; ++index) {
            if (!jt["packed_font_files"][index].is_string())
                continue;
            const std::string path = jt["packed_font_files"][index].get<std::string>();
            if (path.empty() || path.size() > kMaxPathLength ||
                !QFileInfo::exists(QString::fromStdString(path)))
                continue;
            t->packed_font_files.push_back(path);
            register_packed_font_file(QString::fromStdString(path));
        }
    }
    if (jt.contains("editor_default_layer_style") && jt["editor_default_layer_style"].is_object()) {
        auto defaults = layer_from_json(jt["editor_default_layer_style"], false, nullptr);
        if (defaults) {
            defaults->effects.clear();
            defaults->effect_stack_respects_masks = false;
            t->editor_default_layer_style = *defaults;
            t->editor_default_style_enabled = true;
        }
        t->editor_default_foreground_color = json_color(jt, "editor_default_foreground_color", t->editor_default_foreground_color);
        t->editor_default_background_color = json_color(jt, "editor_default_background_color", t->editor_default_background_color);
    }
    if (jt.contains("editor_recent_color_hexes") && jt["editor_recent_color_hexes"].is_array()) {
        t->editor_recent_color_hexes.clear();
        const size_t n = std::min<size_t>(jt["editor_recent_color_hexes"].size(), 32);
        for (size_t i = 0; i < n; ++i) {
            if (jt["editor_recent_color_hexes"][i].is_string()) {
                auto value = jt["editor_recent_color_hexes"][i].get<std::string>();
                if (value.size() > 16) value.resize(16);
                if (!value.empty())
                    t->editor_recent_color_hexes.push_back(value);
            }
        }
    }
    if (jt.contains("layers") && jt["layers"].is_array()) {
        const size_t count = std::min(jt["layers"].size(), kMaxLayersPerTitle);
        t->layers.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            t->layers.push_back(layer_from_json(jt["layers"][i], require_embedded_assets, error, diagnostics));
            if (require_embedded_assets && error && !error->empty())
                return t;
        }
    }
    /* Development Version 218: repair identity/reference damage after the
     * bounded model load. Unknown JSON remains in each object's passthrough
     * payload, while known hierarchy links are made safe for runtime use. */
    {
        std::unordered_set<std::string> layer_ids;
        int repaired_ids = 0;
        for (auto &layer : t->layers) {
            if (!layer)
                continue;
            if (layer->id.empty() || !layer_ids.insert(layer->id).second) {
                do { layer->id = TitleDataStore::make_uuid(); }
                while (!layer_ids.insert(layer->id).second);
                ++repaired_ids;
            }
        }
        int repaired_links = 0;
        const auto repair_link = [&](std::string &link, const std::string &self) {
            if (!link.empty() && (link == self || layer_ids.find(link) == layer_ids.end())) {
                link.clear();
                ++repaired_links;
            }
        };
        for (auto &layer : t->layers) {
            if (!layer)
                continue;
            repair_link(layer->parent_id, layer->id);
            repair_link(layer->transform_parent_id, layer->id);
            repair_link(layer->mask_source_id, layer->id);
            repair_link(layer->linked_media_layer_id, layer->id);
        }
        /* Break group/transform-parent cycles deterministically at the first
         * repeated node instead of allowing recursive evaluation to hang. */
        const auto break_cycles = [&](bool transform_parent) {
            for (auto &layer : t->layers) {
                if (!layer) continue;
                std::unordered_set<std::string> visited;
                std::shared_ptr<Layer> cursor = layer;
                while (cursor) {
                    const std::string next = transform_parent
                        ? cursor->transform_parent_id : cursor->parent_id;
                    if (next.empty()) break;
                    if (!visited.insert(cursor->id).second || visited.find(next) != visited.end()) {
                        if (transform_parent) layer->transform_parent_id.clear();
                        else layer->parent_id.clear();
                        ++repaired_links;
                        break;
                    }
                    cursor = t->find_layer(next);
                }
            }
        };
        break_cycles(false);
        break_cycles(true);
        for (auto &layer : t->layers) {
            if (!layer || !layer->linked_media_stream)
                continue;
            const auto owner = t->find_layer(layer->linked_media_layer_id);
            if (!owner || owner->type != LayerType::Video ||
                layer->type != LayerType::Audio) {
                layer->linked_media_stream = false;
                layer->linked_media_layer_id.clear();
                layer->parent_id.clear();
                ++repaired_links;
            } else {
                layer->parent_id = owner->id;
            }
        }
        if (repaired_ids > 0 || repaired_links > 0) {
            FXM_LOG_WARNING("Serialization", QStringLiteral(
                "Repaired title references duplicateOrMissingIds=%1 danglingOrCyclicLinks=%2")
                .arg(repaired_ids).arg(repaired_links));
        }
    }
    const auto valid_camera_assignment = [&](const std::string &camera_id) {
        return camera_id.empty() || std::any_of(
            t->cameras.begin(), t->cameras.end(),
            [&](const TitleCamera &camera) { return camera.id == camera_id; });
    };
    for (auto &layer : t->layers) {
        if (!layer) continue;
        if (!valid_camera_assignment(layer->camera_id))
            layer->camera_id.clear();
        if (!valid_camera_assignment(layer->camera_assignment.static_value))
            layer->camera_assignment.static_value.clear();
        for (DiscreteKeyframe &keyframe : layer->camera_assignment.keyframes)
            if (!valid_camera_assignment(keyframe.value))
                keyframe.value.clear();
        layer->camera_id = layer->camera_assignment.static_value;
    }

    if (t->graphic_type == TitleGraphicType::Stinger &&
        t->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation)
        ensure_stinger_transition_input_layers(*t);

    if (!has_explicit_graphic_type) {
        bool has_exposed_text = false;
        bool has_scene_mask = false;
        for (const auto &layer : t->layers) {
            if (!layer)
                continue;
            has_exposed_text = has_exposed_text ||
                               (layer->type == LayerType::Text && layer->expose_text);
            has_scene_mask = has_scene_mask || (layer->use_as_scene_mask && layer_type_can_be_scene_mask(layer->type));
        }
        t->graphic_type = has_exposed_text ? TitleGraphicType::Title
                          : has_scene_mask ? TitleGraphicType::Mask
                                           : TitleGraphicType::Graphic;
    }
    /* Development builds before 056 stored both group membership and ordinary
     * transform parenting in parent_id. Preserve real Group containers exactly,
     * but migrate a non-Group target to the independent parenting relation. */
    for (auto &layer : t->layers) {
        if (!layer || layer->parent_id.empty())
            continue;
        const auto legacy_parent = t->find_layer(layer->parent_id);
        const bool linked_video_stream_parent = legacy_parent &&
            legacy_parent->type == LayerType::Video &&
            layer->type == LayerType::Audio && layer->linked_media_stream &&
            layer->linked_media_layer_id == legacy_parent->id;
        if (legacy_parent && !layer_type_is_container(legacy_parent->type) &&
            !linked_video_stream_parent) {
            if (layer->transform_parent_id.empty())
                layer->transform_parent_id = layer->parent_id;
            layer->parent_id.clear();
        }
    }

    /* Matte mode controls the legacy visible flag only while the layer is
     * actually referenced as a matte. Normal hidden layers must keep their own
     * visibility when a Version 056 file is reloaded. */
    std::unordered_set<std::string> matte_source_ids;
    for (const auto &layer : t->layers) {
        if (layer && layer->mask_mode != MaskMode::None &&
            !layer->mask_source_id.empty())
            matte_source_ids.insert(layer->mask_source_id);
    }
    for (auto &layer : t->layers) {
        if (layer && matte_source_ids.find(layer->id) != matte_source_ids.end())
            layer->visible = layer->matte_visibility_mode !=
                             MatteVisibilityMode::HiddenInactive;
    }
    if (jt.contains("live_text_rows") && jt["live_text_rows"].is_array()) {
        const size_t row_count = std::min(jt["live_text_rows"].size(), kMaxLiveTextRows);
        for (size_t r = 0; r < row_count; ++r) {
            const auto &jr = jt["live_text_rows"][r];
            if (!jr.is_array())
                continue;
            std::vector<std::string> row;
            const size_t col_count = std::min(jr.size(), kMaxLiveTextColumns);
            row.reserve(col_count);
            for (size_t c = 0; c < col_count; ++c) {
                if (!jr[c].is_string())
                    continue;
                std::string cell = jr[c].get<std::string>();
                if (cell.size() > kMaxTextLength)
                    cell.resize(kMaxTextLength);
                row.push_back(std::move(cell));
            }
            t->live_text_rows.push_back(std::move(row));
        }
    }
    if (jt.contains("live_text_row_ids") && jt["live_text_row_ids"].is_array()) {
        const size_t count = std::min(jt["live_text_row_ids"].size(), kMaxLiveTextRows);
        t->live_text_row_ids.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (!jt["live_text_row_ids"][i].is_string()) {
                t->live_text_row_ids.push_back({});
                continue;
            }
            std::string row_id = jt["live_text_row_ids"][i].get<std::string>();
            if (row_id.size() > kMaxNameLength)
                row_id.resize(kMaxNameLength);
            t->live_text_row_ids.push_back(std::move(row_id));
        }
    }
    ensure_live_text_row_ids(*t);
    if (jt.contains("live_text_cue_style_overrides") &&
        jt["live_text_cue_style_overrides"].is_array()) {
        const size_t count = std::min(jt["live_text_cue_style_overrides"].size(),
                                      kMaxLiveTextRows * kMaxLiveTextColumns * 2);
        t->live_text_cue_style_overrides.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const auto &item = jt["live_text_cue_style_overrides"][i];
            if (!item.is_object())
                continue;
            LiveTextCueStyleOverride entry;
            if (item.contains("row_id") && item["row_id"].is_string())
                entry.row_id = item["row_id"].get<std::string>();
            if (item.contains("layer_id") && item["layer_id"].is_string())
                entry.layer_id = item["layer_id"].get<std::string>();
            if (entry.row_id.size() > kMaxNameLength)
                entry.row_id.resize(kMaxNameLength);
            if (entry.layer_id.size() > kMaxNameLength)
                entry.layer_id.resize(kMaxNameLength);
            if (item.contains("fill_color")) {
                entry.fill_color_set = true;
                entry.fill_color = json_color(item, "fill_color", entry.fill_color);
            }
            if (item.contains("stroke_color")) {
                entry.stroke_color_set = true;
                entry.stroke_color = json_color(item, "stroke_color", entry.stroke_color);
            }
            if (!entry.row_id.empty() && !entry.layer_id.empty() &&
                (entry.fill_color_set || entry.stroke_color_set))
                t->live_text_cue_style_overrides.push_back(std::move(entry));
        }
        prune_live_text_cue_style_overrides(*t);
    }
    if (jt.contains("live_text_column_order") && jt["live_text_column_order"].is_array()) {
        const size_t count = std::min(jt["live_text_column_order"].size(), kMaxLiveTextColumns);
        t->live_text_column_order.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            if (!jt["live_text_column_order"][i].is_string()) continue;
            std::string layer_id = jt["live_text_column_order"][i].get<std::string>();
            if (layer_id.size() > kMaxNameLength)
                layer_id.resize(kMaxNameLength);
            t->live_text_column_order.push_back(std::move(layer_id));
        }
    }
    if (jt.contains("live_text_external_bindings") &&
        jt["live_text_external_bindings"].is_array()) {
        const size_t count = std::min<size_t>(jt["live_text_external_bindings"].size(),
                                              kMaxLiveTextRows * kMaxLiveTextColumns);
        t->live_text_external_bindings.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            LiveTextExternalBinding cell = live_text_external_binding_from_json(
                jt["live_text_external_bindings"][i]);
            if (!cell.row_id.empty() && !cell.layer_id.empty() &&
                !cell.binding.source_id.empty() && !cell.binding.field_path.empty())
                t->live_text_external_bindings.push_back(std::move(cell));
        }
    }
    if (jt.contains("live_text_table_bindings") &&
        jt["live_text_table_bindings"].is_array()) {
        const size_t count = std::min<size_t>(jt["live_text_table_bindings"].size(), 64);
        t->live_text_table_bindings.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            LiveTextTableBinding mapping = live_text_table_binding_from_json(
                jt["live_text_table_bindings"][i]);
            if (!mapping.id.empty() && !mapping.source_id.empty() &&
                !mapping.table_path.empty() && !mapping.columns.empty())
                t->live_text_table_bindings.push_back(std::move(mapping));
        }
    }
    t->live_text_header_state = bounded_string(jt, "live_text_header_state", "", kMaxTextLength);
    if (jt.contains("external_data_sources") &&
        jt["external_data_sources"].is_array()) {
        const size_t count = std::min(jt["external_data_sources"].size(),
                                      kMaxExternalDataSources);
        t->external_data_sources.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            ExternalDataSourceDefinition source =
                external_source_from_json(jt["external_data_sources"][i]);
            if (!source.id.empty())
                t->external_data_sources.push_back(std::move(source));
        }
    }
    t->external_data_enabled = json_bool(jt, "external_data_enabled", false);
    t->playlist_loop = json_bool(jt, "playlist_loop", false);
    t->playlist_reverse = json_bool(jt, "playlist_reverse", false);
    t->playlist_restart_on_source_active = json_bool(jt, "playlist_restart_on_source_active", false);
    t->playlist_stop_on_source_inactive = json_bool(jt, "playlist_stop_on_source_inactive", false);
    t->playlist_hold_seconds = std::clamp(finite_or(json_double(jt, "playlist_hold_seconds", 5.0), 5.0), 0.0, 3600.0);
    t->preview_screenshot_png_base64 = bounded_string(jt, "preview_screenshot_png_base64", "",
                                                       kMaxScreenshotBase64Length);

    if (regenerate_ids) {
        /* Imported templates need a new title identity, but layer and camera
         * IDs remain stable inside the imported document because masks,
         * parenting, bindings and camera-assignment tracks reference them.
         * Proxy manifests are machine/title-instance specific and must never
         * be adopted by a new title identity; the imported title will generate
         * its own proxy/prerender data on demand. */
        t->id = TitleDataStore::make_uuid();
        t->proxy_metadata = TitleProxyMetadata{};
        t->render_camera_override_id.clear();
        t->current_cue_row = -1;
        t->pending_cue_row = -1;
        t->last_cue_row = -1;
        t->cue_uncue_requested = false;
        t->cue_revision = 0;
        t->playlist_active = false;
        t->playlist_next_row = 0;
        t->playlist_next_due_ms = 0;
        t->playlist_stop_after_due = false;
        t->cue_persistence_transition = false;
        t->cue_persistent_text_columns.clear();
    }

    /* Asset animation is derived from the cloned nested composition, not from
     * a stale/manual library flag. This also migrates Development 075-077
     * instances so static assets lose playback controls and animated assets
     * gain them automatically. */
    for (auto &layer : t->layers) {
        if (layer && layer->type == LayerType::Asset)
            layer->asset_animated =
                fxm::asset_runtime::asset_layer_has_timeline_animation(*t, *layer);
    }
    if (t->is_asset)
        t->asset_animated = fxm::asset_runtime::title_has_timeline_animation(*t);

    synchronize_video_audio_streams(*t);
    return t;
}

std::size_t quantize_title_keyframes_to_frame_rate(Title &title,
                                                   double target_fps)
{
    if (!std::isfinite(target_fps) || target_fps <= 0.0)
        return 0;

    json document;
    try {
        document = title_to_json(title, false);
    } catch (...) {
        return 0;
    }
    std::size_t moved_keyframes = 0;
    const double frame_tolerance = 1.0e-7;

    const auto quantize_tree = [&](auto &&self, json &node) -> void {
        if (node.is_object()) {
            for (auto member = node.begin(); member != node.end(); ++member) {
                json &value = member.value();
                if (member.key() == "keyframes" && value.is_array()) {
                    bool array_changed = false;
                    for (json &keyframe : value) {
                        if (!keyframe.is_object() ||
                            !keyframe.contains("time") ||
                            !keyframe["time"].is_number())
                            continue;
                        const double time = keyframe["time"].get<double>();
                        const double frame_position = time * target_fps;
                        if (!std::isfinite(time) ||
                            !std::isfinite(frame_position))
                            continue;
                        const double nearest_frame = std::round(frame_position);
                        if (std::abs(frame_position - nearest_frame) <=
                            frame_tolerance)
                            continue;
                        keyframe["time"] = nearest_frame / target_fps;
                        ++moved_keyframes;
                        array_changed = true;
                    }

                    if (array_changed) {
                        /* Quantization can map adjacent authored keys onto the
                         * same project frame. A property cannot have two
                         * unambiguous values at one instant, so retain the
                         * later authored key just like the editor's set-at-time
                         * operation does. */
                        std::stable_sort(
                            value.begin(), value.end(),
                            [](const json &left, const json &right) {
                                const auto time_or_max = [](const json &item) {
                                    return item.is_object() &&
                                           item.contains("time") &&
                                           item["time"].is_number()
                                        ? item["time"].get<double>()
                                        : std::numeric_limits<double>::max();
                                };
                                return time_or_max(left) < time_or_max(right);
                            });
                        json compacted = json::array();
                        for (const json &keyframe : value) {
                            if (!keyframe.is_object() ||
                                !keyframe.contains("time") ||
                                !keyframe["time"].is_number()) {
                                compacted.push_back(keyframe);
                                continue;
                            }
                            const double time = keyframe["time"].get<double>();
                            if (!compacted.empty() && compacted.back().is_object() &&
                                compacted.back().contains("time") &&
                                compacted.back()["time"].is_number() &&
                                std::abs(compacted.back()["time"].get<double>() -
                                         time) <= frame_tolerance / target_fps) {
                                compacted.back() = keyframe;
                            } else {
                                compacted.push_back(keyframe);
                            }
                        }
                        value = std::move(compacted);
                    }
                }
                self(self, value);
            }
        } else if (node.is_array()) {
            for (json &item : node)
                self(self, item);
        }
    };
    try {
        quantize_tree(quantize_tree, document);
    } catch (...) {
        return 0;
    }

    if (moved_keyframes == 0)
        return 0;
    try {
        if (auto quantized = title_from_json(document, false))
            title = std::move(*quantized);
        else
            moved_keyframes = 0;
    } catch (...) {
        moved_keyframes = 0;
    }
    return moved_keyframes;
}

TitleDataStore::TitleDataStore() = default;

TitleDataStore::~TitleDataStore()
{
    shutdownSaveWorker();
}

bool TitleDataStore::write_snapshot_atomic(
    const std::vector<std::shared_ptr<Title>> &snapshot,
    const std::string &path)
{
    json root = json::array();
    try {
        for (const auto &title : snapshot) {
            if (title)
                root.push_back(title_to_json(*title));
        }
    } catch (const std::exception &e) {
        host_log(fxm::LogLevel::Warning,
                 QStringLiteral("Failed to serialize titles file: %1")
                     .arg(QString::fromUtf8(e.what())));
        return false;
    }

    std::string payload;
    try {
        payload = root.dump(2, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception &e) {
        host_log(fxm::LogLevel::Warning,
                 QStringLiteral("Failed to encode titles file: %1")
                     .arg(QString::fromUtf8(e.what())));
        return false;
    }

    /* QSaveFile writes to a temporary file in the destination directory and
     * atomically replaces the target only when commit() succeeds. Keeping
     * direct-write fallback disabled is intentional: a temporary filesystem
     * or antivirus lock must never cause the last valid titles file to be
     * truncated or deleted. */
    const QString destination = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QSaveFile file(destination);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        host_log(fxm::LogLevel::Warning,
                 QStringLiteral("Failed to open titles file for atomic saving: %1")
                     .arg(file.errorString()));
        return false;
    }

    const qint64 expected = static_cast<qint64>(payload.size());
    const qint64 written = file.write(payload.data(), expected);
    if (written != expected) {
        host_log(fxm::LogLevel::Warning,
                 QStringLiteral("Failed while writing titles file: %1")
                     .arg(file.errorString()));
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        host_log(fxm::LogLevel::Warning,
                 QStringLiteral("Failed to atomically replace titles file: %1")
                     .arg(file.errorString()));
        return false;
    }

    return true;
}

namespace {

bool report_project_progress(const ProjectLoadCallback &callback,
                             const char *step, const std::string &file,
                             std::size_t completed, std::size_t total,
                             std::string *error)
{
    if (!callback)
        return true;
    if (callback(ProjectLoadProgress{step, file, completed, total}))
        return true;
    if (error)
        *error = "Project loading was cancelled.";
    return false;
}

std::string project_string(const json &object, const char *key,
                           const std::string &fallback = {})
{
    if (!object.is_object())
        return fallback;
    const auto value = object.find(key);
    return value != object.end() && value->is_string()
        ? value->get<std::string>() : fallback;
}

} // namespace

bool TitleDataStore::read_project(const std::string &path,
                                  TitleProject *project,
                                  std::string *error,
                                  ProjectLoadCallback progress) const
{
    if (error)
        error->clear();
    if (!project) {
        if (error) *error = "No project result was supplied.";
        return false;
    }
    *project = TitleProject{};
    if (!report_project_progress(progress, "Reading project", path, 0, 5,
                                 error))
        return false;

    try {
        json root;
        const QString input_path = QString::fromStdString(path);
        const bool packed = QFileInfo(input_path).suffix().compare(
                                QStringLiteral("fxmp"), Qt::CaseInsensitive) == 0 ||
                            fxm::packed_title::has_packed_signature(input_path);
        if (packed) {
            std::string packed_path;
            if (const auto *provider = fxm::host_context_provider())
                packed_path = provider->config_path("packed");
            if (packed_path.empty())
                packed_path = title_data_dir() + "/packed";
            if (packed_path.empty()) {
                if (error) *error = "Could not resolve the packed resource directory.";
                return false;
            }
            const QString extraction_base = QString::fromStdString(packed_path);
            QDir().mkpath(extraction_base);
            fxm::packed_title::ReadResult packed_result;
            QString packed_error;
            if (!fxm::packed_title::read(input_path, extraction_base,
                                         &packed_result, &packed_error)) {
                if (error) *error = packed_error.toStdString();
                return false;
            }
            root = json::parse(packed_result.title_json.constData(),
                               packed_result.title_json.constData() +
                                   packed_result.title_json.size(),
                               nullptr, false);
            if (root.is_discarded()) {
                if (error) *error = "Invalid title.json in packed project.";
                return false;
            }
            if (!resolve_packed_uri_json(root, packed_result.extraction_root,
                                         error))
                return false;
            for (const QString &font_path : packed_result.extracted_font_paths)
                register_packed_font_file(font_path);
        } else if (!read_json_file(path, root, error)) {
            return false;
        }

        if (!report_project_progress(progress, "Loading assets", path, 1, 5,
                                     error))
            return false;

        const json *title_array = nullptr;
        json legacy_titles;
        json project_metadata;
        if (root.is_object() && root.contains("titles") &&
            root["titles"].is_array()) {
            title_array = &root["titles"];
            project_metadata = root.value("project", json::object());
            project->schema_version = std::max(1,
                root.value("project_schema_version",
                           root.value("version", 1)));
        } else if (root.is_object() && root.contains("title")) {
            legacy_titles = json::array({root["title"]});
            title_array = &legacy_titles;
            project_metadata = root.value("metadata", json::object());
        } else if (root.is_array()) {
            title_array = &root;
        } else if (root.is_object()) {
            legacy_titles = json::array({root});
            title_array = &legacy_titles;
        } else {
            throw std::runtime_error("Unsupported Flux Motion project format.");
        }

        project->id = project_string(project_metadata, "id");
        project->name = project_string(
            project_metadata, "name",
            QFileInfo(input_path).completeBaseName().toStdString());
        project->description = project_string(project_metadata, "description");
        project->created_at = project_string(project_metadata, "created_at");
        project->modified_at = project_string(project_metadata, "modified_at");
        project->active_title_id = project_string(
            project_metadata, "active_title_id",
            project_string(root, "active_title_id"));
        if (project->id.empty())
            project->id = make_uuid();
        if (project->name.empty())
            project->name = "Untitled Project";

        std::unordered_set<std::string> seen_ids;
        const std::size_t count = std::min(title_array->size(), kMaxTitles);
        project->titles.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            if (!report_project_progress(
                    progress, "Initializing timelines",
                    project_string((*title_array)[index], "name", path),
                    index, std::max<std::size_t>(count, 1), error))
                return false;
            try {
                auto title = title_from_json((*title_array)[index], false);
                ensure_unique_title_id(title, seen_ids);
                if (title)
                    project->titles.push_back(std::move(title));
            } catch (const std::exception &entry_error) {
                FXM_LOG_WARNING("Project", QStringLiteral(
                    "Skipped invalid project title index=%1 error=%2")
                    .arg(static_cast<qulonglong>(index))
                    .arg(QString::fromUtf8(entry_error.what())));
            }
        }
        if (project->titles.empty())
            throw std::runtime_error("The project does not contain a readable title or graphic.");
        if (project->active_title_id.empty() ||
            !std::any_of(project->titles.begin(), project->titles.end(),
                [&](const std::shared_ptr<Title> &title) {
                    return title && title->id == project->active_title_id;
                })) {
            project->active_title_id = project->titles.front()->id;
        }

        if (!report_project_progress(progress, "Building caches", path, 4, 5,
                                     error) ||
            !report_project_progress(progress, "Preparing previews", path, 5, 5,
                                     error))
            return false;
        return true;
    } catch (const std::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool TitleDataStore::export_project(const TitleProject &project,
                                    const std::string &path,
                                    std::string *error) const
{
    if (error)
        error->clear();
    if (path.empty()) {
        if (error) *error = "No project destination was supplied.";
        return false;
    }
    try {
        json titles = json::array();
        for (const auto &title : project.titles)
            if (title)
                titles.push_back(title_to_json(*title));
        if (titles.empty()) {
            if (error) *error = "The project does not contain a title or graphic.";
            return false;
        }

        const std::string now = QDateTime::currentDateTimeUtc()
                                    .toString(Qt::ISODateWithMs).toStdString();
        json root;
        root["format"] = "flux-motion-project";
        root["version"] = TitleProject::CurrentSchemaVersion;
        root["project_schema_version"] = TitleProject::CurrentSchemaVersion;
        root["title_schema_version"] =
            fxm::serialization::kCurrentTitleSchemaVersion;
        root["development_version"] =
            fxm::serialization::kCurrentDevelopmentVersion;
        root["project"] = {
            {"id", project.id.empty() ? make_uuid() : project.id},
            {"name", project.name.empty() ? "Untitled Project" : project.name},
            {"description", project.description},
            {"created_at", project.created_at.empty() ? now : project.created_at},
            {"modified_at", now},
            {"active_title_id", project.active_title_id.empty()
                                    ? project.titles.front()->id
                                    : project.active_title_id},
        };
        root["titles"] = std::move(titles);

        const std::string payload = root.dump(
            2, ' ', false, json::error_handler_t::replace);
        QSaveFile file(QString::fromStdString(path));
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString().toStdString();
            return false;
        }
        const qint64 written = file.write(
            payload.data(), static_cast<qint64>(payload.size()));
        if (written != static_cast<qint64>(payload.size())) {
            if (error) *error = file.errorString().toStdString();
            file.cancelWriting();
            return false;
        }
        if (!file.commit()) {
            if (error) *error = file.errorString().toStdString();
            return false;
        }
        return true;
    } catch (const std::exception &exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool TitleDataStore::activate_project(const TitleProject &project)
{
    if (project.titles.empty())
        return false;
    std::vector<std::shared_ptr<Title>> activated;
    activated.reserve(project.titles.size());
    std::unordered_set<std::string> seen_ids;
    for (const auto &source : project.titles) {
        if (!source)
            continue;
        auto copy = std::make_shared<Title>(clone_title_snapshot(*source));
        ensure_unique_title_id(copy, seen_ids);
        activated.push_back(std::move(copy));
    }
    if (activated.empty())
        return false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        clear_title_publications_locked();
        titles_ = std::move(activated);
        for (const auto &title : titles_) {
            title_mutex_locked(title);
            published_titles_[title->id] =
                std::make_shared<Title>(clone_title_snapshot(*title));
        }
    }
    fxm::ticker_runtime::clear_all();
    fxm::asset_runtime::clear_all();
    notify_change();
    return true;
}

std::vector<std::string> TitleDataStore::append_project(
    const TitleProject &project)
{
    std::vector<std::shared_ptr<Title>> copies;
    copies.reserve(project.titles.size());
    std::unordered_map<std::string, std::string> remapped_ids;
    std::unordered_set<std::string> occupied;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto &title : titles_)
            if (title) occupied.insert(title->id);
    }
    for (const auto &source : project.titles) {
        if (!source) continue;
        auto copy = std::make_shared<Title>(clone_title_snapshot(*source));
        const std::string old_id = copy->id;
        if (copy->id.empty() || occupied.find(copy->id) != occupied.end())
            copy->id = make_uuid();
        occupied.insert(copy->id);
        if (!old_id.empty() && old_id != copy->id)
            remapped_ids[old_id] = copy->id;
        copies.push_back(std::move(copy));
    }
    for (const auto &copy : copies) {
        if (!copy) continue;
        for (const auto &layer : copy->layers) {
            if (!layer || layer->asset_title_id.empty()) continue;
            const auto replacement = remapped_ids.find(layer->asset_title_id);
            if (replacement != remapped_ids.end())
                layer->asset_title_id = replacement->second;
        }
    }

    std::vector<std::string> added_ids;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (auto &copy : copies) {
            copy->name = unique_title_name_locked(
                ordered_published_titles_locked(titles_, published_titles_),
                copy->name.empty() ? "Imported Title" : copy->name);
            added_ids.push_back(copy->id);
            titles_.push_back(copy);
            title_mutex_locked(copy);
            published_titles_[copy->id] =
                std::make_shared<Title>(clone_title_snapshot(*copy));
        }
    }
    if (!added_ids.empty())
        notify_change();
    return added_ids;
}

std::shared_ptr<Title> TitleDataStore::duplicate_title(
    const std::string &id, const std::string &requested_name)
{
    auto source = get_title_snapshot(id);
    if (!source)
        return nullptr;
    auto copy = std::make_shared<Title>(clone_title_snapshot(*source));
    copy->id = make_uuid();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        copy->name = unique_title_name_locked(
            ordered_published_titles_locked(titles_, published_titles_),
            requested_name.empty() ? source->name + " (copy)" : requested_name);
        titles_.push_back(copy);
        title_mutex_locked(copy);
        published_titles_[copy->id] =
            std::make_shared<Title>(clone_title_snapshot(*copy));
    }
    notify_change();
    return get_title(copy->id);
}

void TitleDataStore::reorder_titles(
    const std::vector<std::string> &ordered_ids)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::shared_ptr<Title>> reordered;
    reordered.reserve(titles_.size());
    std::unordered_set<std::string> used;
    for (const std::string &id : ordered_ids) {
        const auto found = std::find_if(titles_.begin(), titles_.end(),
            [&](const std::shared_ptr<Title> &title) {
                return title && title->id == id;
            });
        if (found != titles_.end() && used.insert(id).second)
            reordered.push_back(*found);
    }
    for (const auto &title : titles_)
        if (title && used.insert(title->id).second)
            reordered.push_back(title);
    titles_ = std::move(reordered);
}

void TitleDataStore::save() const
{
    std::string path;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!persistence_ready_for_save_) {
            FXM_LOG_WARNING("TitleStore", QStringLiteral(
                "Skipped save because the title store did not load successfully; refusing to rewrite the previous file in a new schema"));
            return;
        }
        path = loaded_path_.empty() ? data_path() : loaded_path_;
    }
    publish_title_snapshots(snapshot_authoritative_titles());
    const auto snapshot = title_snapshots();

    /* Serialize synchronous and asynchronous writes. Otherwise a manual save
     * can race an outstanding background save through the same .tmp path. */
    std::lock_guard<std::mutex> write_lock(save_io_mutex_);
    const bool saved = write_snapshot_atomic(snapshot, path);
    if (saved) {
        FXM_LOG_DEBUG("TitleStore", QStringLiteral(
            "Saved titles count=%1 path=%2")
            .arg(static_cast<int>(snapshot.size()))
            .arg(QString::fromStdString(path)));
    } else {
        FXM_LOG_ERROR("TitleStore", QStringLiteral(
            "Failed to save titles count=%1 path=%2")
            .arg(static_cast<int>(snapshot.size()))
            .arg(QString::fromStdString(path)));
    }
}

void TitleDataStore::save_worker_loop() const
{
    for (;;) {
        std::unique_ptr<PendingSave> request;
        {
            std::unique_lock<std::mutex> lock(save_mutex_);
            save_cv_.wait(lock, [this] { return save_stop_ || pending_save_; });
            if (save_stop_ && !pending_save_)
                return;
            request = std::move(pending_save_);
        }

        if (!request)
            continue;

        /* Serialization and I/O are done by one long-lived worker. A newer
         * request replaces the pending one instead of spawning another thread. */
        {
            std::lock_guard<std::mutex> write_lock(save_io_mutex_);
            write_snapshot_atomic(request->snapshot, request->path);
        }
    }
}

void TitleDataStore::shutdownSaveWorker() const
{
    {
        std::lock_guard<std::mutex> lock(save_mutex_);
        if (save_stop_ && !save_thread_.joinable())
            return;
        save_stop_ = true;
        /* Discard any coalesced request that has not started yet. The caller
         * performs the final synchronous save after this worker has joined, so
         * an older snapshot cannot overwrite the shutdown snapshot. */
        pending_save_.reset();
    }
    save_cv_.notify_all();
    if (save_thread_.joinable())
        save_thread_.join();
}

void TitleDataStore::save_async() const
{
    auto request = std::make_unique<PendingSave>();
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!persistence_ready_for_save_) {
            FXM_LOG_WARNING("TitleStore", QStringLiteral(
                "Skipped async save because the title store did not load successfully; refusing to rewrite the previous file in a new schema"));
            return;
        }
        request->path = loaded_path_.empty() ? data_path() : loaded_path_;
    }
    publish_title_snapshots(snapshot_authoritative_titles());
    request->snapshot = title_snapshots();

    uint64_t queued_generation = 0;
    {
        std::lock_guard<std::mutex> lock(save_mutex_);
        if (save_stop_)
            return;
        request->generation = ++save_generation_;
        queued_generation = request->generation;
        pending_save_ = std::move(request);
        if (!save_worker_started_) {
            save_worker_started_ = true;
            save_thread_ = std::thread(&TitleDataStore::save_worker_loop, this);
        }
    }
    save_cv_.notify_one();
    FXM_LOG_TRACE("TitleStore", QStringLiteral(
        "Queued asynchronous title-store save generation=%1")
        .arg(queued_generation));
}

static void prepare_template_export_copy(Title &copy,
                                         const TitleTemplateExportMetadata &metadata)
{
    copy.name = metadata.title;
    copy.description = metadata.description;
    copy.creator = metadata.creator;
    copy.creation_date = metadata.creation_date;
    copy.preview_screenshot_png_base64 = metadata.screenshot_png_base64;
    copy.proxy_metadata = TitleProxyMetadata{};
    copy.render_camera_override_id.clear();
    copy.current_cue_row = -1;
    copy.pending_cue_row = -1;
    copy.last_cue_row = -1;
    copy.cue_uncue_requested = false;
    copy.cue_revision = 0;
    copy.playlist_active = false;
    copy.playlist_next_row = 0;
    copy.playlist_next_due_ms = 0;
    copy.playlist_stop_after_due = false;
    copy.cue_persistence_transition = false;
    copy.cue_persistent_text_columns.clear();
}

static json template_export_root(const Title &copy,
                                 const TitleTemplateExportMetadata &metadata,
                                 bool include_embedded_assets,
                                 bool require_embedded_assets,
                                 std::string *error)
{
    json root;
    root["format"] = "flux-motion-title-template";
    root["version"] = 4;
    root["schema_version"] = fxm::serialization::kCurrentTitleSchemaVersion;
    root["development_version"] = fxm::serialization::kCurrentDevelopmentVersion;
    root["template_title"] = metadata.title;
    root["description"] = metadata.description;
    root["creator"] = metadata.creator;
    root["creation_date"] = metadata.creation_date;
    root["category"] = metadata.category;
    root["subcategory"] = metadata.subcategory;
    root["collection"] = metadata.collection;
    root["screenshot"] = {
        {"mime_type", "image/png"},
        {"data_base64", metadata.screenshot_png_base64},
    };
    root["metadata"] = {
        {"title", metadata.title},
        {"description", metadata.description},
        {"creator", metadata.creator},
        {"creation_date", metadata.creation_date},
        {"category", metadata.category},
        {"subcategory", metadata.subcategory},
        {"collection", metadata.collection},
        {"screenshot", root["screenshot"]},
    };
    root["title"] = title_to_json(copy, include_embedded_assets,
                                  require_embedded_assets, error);
    return root;
}

struct PackedFontRequest {
    QString family;
    QString style;
};

static void append_packed_font_request(std::vector<PackedFontRequest> &requests,
                                       const std::string &family,
                                       const std::string &style)
{
    const QString qfamily = QString::fromStdString(family).trimmed();
    const QString qstyle = QString::fromStdString(style).trimmed();
    if (qfamily.isEmpty())
        return;
    const bool exists = std::any_of(requests.begin(), requests.end(),
        [&](const PackedFontRequest &request) {
            return request.family.compare(qfamily, Qt::CaseInsensitive) == 0 &&
                   request.style.compare(qstyle, Qt::CaseInsensitive) == 0;
        });
    if (!exists)
        requests.push_back({qfamily, qstyle});
}

static std::vector<PackedFontRequest> packed_font_requests(const Title &title)
{
    std::vector<PackedFontRequest> requests;
    const auto add_format = [&](const RichTextCharFormat &format) {
        append_packed_font_request(requests, format.font_family,
                                   format.font_style);
    };
    for (const auto &layer : title.layers) {
        if (!layer || (layer->type != LayerType::Text &&
                       layer->type != LayerType::Clock &&
                       layer->type != LayerType::Ticker))
            continue;
        append_packed_font_request(requests, layer->font_family,
                                   layer->font_style);
        add_format(layer->rich_text.default_format);
        if (layer->rich_text.has_typing_format &&
            (layer->rich_text.typing_format_mask & RichTextCharFontFamily))
            add_format(layer->rich_text.typing_format);
        for (const auto &range : layer->rich_text.ranges) {
            if (range.mask & RichTextCharFontFamily)
                add_format(range.format);
        }
        if (!layer->rich_text.auto_default_style_preset_id.empty())
            add_format(layer->rich_text.auto_default_style_cached_format);
        for (const auto &rule : layer->rich_text.auto_style_rules) {
            if (rule.cached_mask & RichTextCharFontFamily)
                add_format(rule.cached_format);
        }
    }
    return requests;
}

struct PackedFontCandidate {
    QString path;
    QString family;
    QString style;
};

static std::vector<PackedFontCandidate> scan_packable_fonts()
{
    QStringList roots = QStandardPaths::standardLocations(QStandardPaths::FontsLocation);
#ifdef Q_OS_WIN
    const QString windows = qEnvironmentVariable("WINDIR");
    if (!windows.isEmpty()) roots.push_back(QDir(windows).filePath(QStringLiteral("Fonts")));
#elif defined(Q_OS_MACOS)
    roots << QStringLiteral("/System/Library/Fonts")
          << QStringLiteral("/Library/Fonts")
          << QDir::home().filePath(QStringLiteral("Library/Fonts"));
#else
    roots << QStringLiteral("/usr/share/fonts")
          << QStringLiteral("/usr/local/share/fonts")
          << QDir::home().filePath(QStringLiteral(".local/share/fonts"))
          << QDir::home().filePath(QStringLiteral(".fonts"));
#endif
    roots.removeDuplicates();

    std::vector<PackedFontCandidate> candidates;
    QSet<QString> visited;
    const QStringList filters = {
        QStringLiteral("*.ttf"), QStringLiteral("*.otf"),
        QStringLiteral("*.ttc"), QStringLiteral("*.otc")};
    for (const QString &root : roots) {
        if (!QFileInfo(root).isDir())
            continue;
        QDirIterator it(root, filters, QDir::Files | QDir::Readable,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = QFileInfo(it.next()).canonicalFilePath();
            if (path.isEmpty() || visited.contains(path))
                continue;
            visited.insert(path);
            const QRawFont raw(path, 16.0, QFont::PreferNoHinting);
            if (!raw.isValid() || raw.familyName().trimmed().isEmpty())
                continue;
            candidates.push_back({path, raw.familyName().trimmed(),
                                  raw.styleName().trimmed()});
        }
    }
    return candidates;
}

static QString resolve_packable_font(const PackedFontRequest &request,
                                     const std::vector<std::string> &known_paths)
{
    QString family_fallback;
    const auto inspect_path = [&](const QString &path, bool allow_fallback) -> QString {
        const QRawFont raw(path, 16.0, QFont::PreferNoHinting);
        if (!raw.isValid() ||
            raw.familyName().compare(request.family, Qt::CaseInsensitive) != 0)
            return {};
        if (request.style.isEmpty() ||
            raw.styleName().compare(request.style, Qt::CaseInsensitive) == 0)
            return path;
        if (allow_fallback && family_fallback.isEmpty())
            family_fallback = path;
        return {};
    };

    for (const std::string &known : known_paths) {
        const QString resolved = inspect_path(QString::fromStdString(known), true);
        if (!resolved.isEmpty())
            return resolved;
    }
    static const std::vector<PackedFontCandidate> candidates = scan_packable_fonts();
    for (const PackedFontCandidate &candidate : candidates) {
        if (candidate.family.compare(request.family, Qt::CaseInsensitive) != 0)
            continue;
        if (request.style.isEmpty() ||
            candidate.style.compare(request.style, Qt::CaseInsensitive) == 0)
            return candidate.path;
        if (family_fallback.isEmpty())
            family_fallback = candidate.path;
    }
    return family_fallback;
}

static QString packed_resource_uri(const QString &archive_path)
{
    return QStringLiteral("packed://") + archive_path;
}

static bool packed_resource_path_key(const std::string &key)
{
    return key == "image_path" || key == "video_source" ||
           key == "video_source_absolute" || key == "video_source_relative" ||
           key == "audio_source" || key == "environment_path" ||
           key == "packed_font_files";
}

static bool resolve_packed_uri_json(json &value, const QString &root,
                                    std::string *error,
                                    const std::string &owner_key)
{
    if (value.is_object()) {
        for (auto &item : value.items()) {
            if (!resolve_packed_uri_json(item.value(), root, error, item.key()))
                return false;
        }
        return true;
    }
    if (value.is_array()) {
        for (auto &item : value) {
            if (!resolve_packed_uri_json(item, root, error, owner_key))
                return false;
        }
        return true;
    }
    if (!value.is_string() || !packed_resource_path_key(owner_key))
        return true;
    const QString text = QString::fromStdString(value.get<std::string>());
    if (!text.startsWith(QStringLiteral("packed://")))
        return true;
    const QString relative = text.mid(9);
    const QString clean_relative = QDir::cleanPath(relative);
    if (clean_relative.isEmpty() || clean_relative == QStringLiteral(".") ||
        clean_relative == QStringLiteral("..") || clean_relative.startsWith('/') ||
        clean_relative.startsWith(QStringLiteral("../")) || clean_relative.contains('\\') ||
        clean_relative.contains(':') || clean_relative.contains(QChar::Null)) {
        if (error) *error = "Packed title contains an unsafe resource reference.";
        return false;
    }
    const QString absolute_root = QDir(root).absolutePath();
    const QString absolute = QFileInfo(QDir(absolute_root).filePath(clean_relative)).absoluteFilePath();
    if (absolute != absolute_root &&
        !absolute.startsWith(absolute_root + QDir::separator())) {
        if (error) *error = "Packed title resource escaped its extraction directory.";
        return false;
    }
    value = absolute.toStdString();
    return true;
}


bool TitleDataStore::export_title(const std::string &id, const std::string &path, std::string *error) const
{
    TitleTemplateExportMetadata metadata;
    return export_title(id, path, metadata, error);
}

bool TitleDataStore::export_title(const std::string &id, const std::string &path,
                                  const TitleTemplateExportMetadata &metadata,
                                  std::string *error) const
{
    if (error) error->clear();
    auto t = get_title(id);
    if (!t) {
        if (error) *error = "No title template is selected.";
        return false;
    }

    TitleTemplateExportMetadata export_metadata = metadata;
    if (export_metadata.title.empty()) export_metadata.title = t->name;
    if (export_metadata.description.empty()) export_metadata.description = t->description;
    if (export_metadata.creator.empty()) export_metadata.creator = t->creator;
    if (export_metadata.creation_date.empty()) {
        export_metadata.creation_date = t->creation_date.empty() ? current_iso_utc_string() : t->creation_date;
    }
    if (export_metadata.screenshot_png_base64.empty())
        export_metadata.screenshot_png_base64 = t->preview_screenshot_png_base64;

    Title exported_copy = *t;
    /* A reusable template contains authored title data, not the source
     * machine's advisory proxy path/cache namespace. Import assigns a new
     * title identity and regenerates cache/proxy data safely. */
    prepare_template_export_copy(exported_copy, export_metadata);
    /* Keep this explicit at the legacy export call site: older compatibility
     * audits verify that .fxmt never carries a machine-local proxy. */
    exported_copy.proxy_metadata = TitleProxyMetadata{};
    exported_copy.render_camera_override_id.clear();
    /* The ordinary JSON format must never gain implicit packed-container
     * dependencies merely because its source title was imported from .fxmp. */
    exported_copy.packed_font_files.clear();
    json root = template_export_root(exported_copy, export_metadata, true, true, error);
    if ((error && !error->empty()) || root["title"].empty()) {
        if (error && error->empty())
            *error = "Could not embed all title assets in the export file.";
        return false;
    }

    std::string payload;
    try {
        payload = root.dump(2, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception &e) {
        if (error) *error = std::string("Failed to serialize the export file: ") + e.what();
        return false;
    }

    QSaveFile file(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = "Could not open the export file for atomic writing.";
        return false;
    }
    const qint64 expected = static_cast<qint64>(payload.size());
    if (file.write(payload.data(), expected) != expected || !file.commit()) {
        if (error) *error = "Failed while atomically writing the export file.";
        FXM_LOG_ERROR("ImportExport", QStringLiteral(
            "Title export write failed id=%1 path=%2")
            .arg(QString::fromStdString(id), QString::fromStdString(path)));
        return false;
    }
    FXM_LOG_INFO("ImportExport", QStringLiteral(
        "Exported title id=%1 path=%2")
        .arg(QString::fromStdString(id), QString::fromStdString(path)));
    return true;
}

bool TitleDataStore::export_packed_title(
    const std::string &id, const std::string &path,
    const TitleTemplateExportMetadata &metadata,
    const PackedTitleExportOptions &options, std::string *error) const
{
    if (error) error->clear();
    auto title = get_title(id);
    if (!title) {
        if (error) *error = "No title template is selected.";
        return false;
    }

    TitleTemplateExportMetadata export_metadata = metadata;
    if (export_metadata.title.empty()) export_metadata.title = title->name;
    if (export_metadata.description.empty()) export_metadata.description = title->description;
    if (export_metadata.creator.empty()) export_metadata.creator = title->creator;
    if (export_metadata.creation_date.empty()) {
        export_metadata.creation_date = title->creation_date.empty()
            ? current_iso_utc_string() : title->creation_date;
    }
    if (export_metadata.screenshot_png_base64.empty())
        export_metadata.screenshot_png_base64 = title->preview_screenshot_png_base64;

    Title copy = *title;
    copy.layers.clear();
    copy.layers.reserve(title->layers.size());
    for (const auto &layer : title->layers)
        copy.layers.push_back(layer ? std::make_shared<Layer>(*layer) : nullptr);
    prepare_template_export_copy(copy, export_metadata);

    QList<fxm::packed_title::SourceEntry> sources;
    QHash<QString, QString> source_to_archive;
    int image_index = 0;
    int media_index = 0;
    int font_index = 0;
    auto pack_path = [&](std::string &authored_path, const QString &kind,
                         const QString &directory, int &index,
                         const QJsonObject &entry_metadata = QJsonObject{}) -> bool {
        if (authored_path.empty())
            return true;
        const QFileInfo info(QString::fromStdString(authored_path));
        if (!info.exists() || !info.isFile() || !info.isReadable()) {
            if (error) *error = QStringLiteral("Could not pack missing or unreadable %1: %2")
                .arg(kind, info.filePath()).toStdString();
            return false;
        }
        QString source_path = info.canonicalFilePath();
        if (source_path.isEmpty())
            source_path = info.absoluteFilePath();
        QString archive_path = source_to_archive.value(source_path);
        if (archive_path.isEmpty()) {
            const QString file_name = QString::fromStdString(
                sanitize_asset_file_name(info.fileName().toStdString()));
            archive_path = QStringLiteral("assets/%1/%2-%3")
                .arg(directory)
                .arg(++index, 4, 10, QLatin1Char('0'))
                .arg(file_name);
            fxm::packed_title::SourceEntry source;
            source.archive_path = archive_path;
            source.kind = kind;
            source.source_path = source_path;
            source.metadata = entry_metadata;
            sources.push_back(std::move(source));
            source_to_archive.insert(source_path, archive_path);
        }
        authored_path = packed_resource_uri(archive_path).toStdString();
        return true;
    };

    if (options.pack_images) {
        for (TitleLight &light : copy.lights) {
            if (!pack_path(light.environment_path, QStringLiteral("image"),
                           QStringLiteral("images"), image_index))
                return false;
        }
        if (!pack_path(copy.editor_default_layer_style.image_path,
                       QStringLiteral("image"), QStringLiteral("images"),
                       image_index) ||
            !pack_path(copy.editor_default_layer_style.light.environment_path,
                       QStringLiteral("image"), QStringLiteral("images"),
                       image_index))
            return false;
    }

    for (auto &layer : copy.layers) {
        if (!layer)
            continue;
        if (options.pack_images) {
            if (layer->type == LayerType::Image &&
                !pack_path(layer->image_path, QStringLiteral("image"),
                           QStringLiteral("images"), image_index))
                return false;
            if (!pack_path(layer->light.environment_path, QStringLiteral("image"),
                           QStringLiteral("images"), image_index))
                return false;
            for (LayerTransition &transition : layer->transitions) {
                if (!pack_path(transition.image_path, QStringLiteral("image"),
                               QStringLiteral("images"), image_index))
                    return false;
            }
        }
        if (options.pack_media && layer->type == LayerType::Video) {
            if (layer->video_source.empty())
                layer->video_source = !layer->video_source_absolute.empty()
                    ? layer->video_source_absolute : layer->video_source_relative;
            if (!pack_path(layer->video_source, QStringLiteral("media"),
                           QStringLiteral("media"), media_index))
                return false;
            layer->video_source_absolute = layer->video_source;
            layer->video_source_relative.clear();
            layer->video_media_root.clear();
            layer->video_proxy_path.clear();
            layer->video_proxy_fingerprint.clear();
            layer->video_proxy_complete = false;
            layer->video_proxy_generating = false;
        }
        if (options.pack_media && layer->type == LayerType::Audio &&
            !pack_path(layer->audio_source, QStringLiteral("media"),
                       QStringLiteral("media"), media_index))
            return false;
    }

    if (options.pack_fonts) {
        const QStringList installed_families = QFontDatabase().families();
        copy.packed_font_files.clear();
        QSet<QString> added_font_paths;
        for (const PackedFontRequest &request : packed_font_requests(copy)) {
            const bool installed = std::any_of(installed_families.begin(), installed_families.end(),
                [&](const QString &family) {
                    return family.compare(request.family, Qt::CaseInsensitive) == 0;
                });
            if (!installed)
                continue; /* Existing import diagnostics will report it. */
            const QString font_path = resolve_packable_font(
                request, title->packed_font_files);
            if (font_path.isEmpty()) {
                if (error) *error = QStringLiteral(
                    "The font '%1' is available but its font file could not be located for packing.")
                    .arg(request.family).toStdString();
                return false;
            }
            const QString canonical = QFileInfo(font_path).canonicalFilePath();
            if (added_font_paths.contains(canonical))
                continue;
            std::string mutable_path = canonical.toStdString();
            QJsonObject font_metadata;
            font_metadata.insert(QStringLiteral("family"), request.family);
            font_metadata.insert(QStringLiteral("style"), request.style);
            if (!pack_path(mutable_path, QStringLiteral("font"),
                           QStringLiteral("fonts"), font_index, font_metadata))
                return false;
            copy.packed_font_files.push_back(mutable_path);
            added_font_paths.insert(canonical);
        }
    }

    json root = template_export_root(copy, export_metadata, false, false, error);
    if ((error && !error->empty()) || root["title"].empty()) {
        if (error && error->empty())
            *error = "Could not serialize title.json for the packed title.";
        return false;
    }
    std::string payload;
    try {
        payload = root.dump(2, ' ', false, json::error_handler_t::replace);
    } catch (const std::exception &exception) {
        if (error) *error = std::string("Failed to serialize packed title data: ") + exception.what();
        return false;
    }
    fxm::packed_title::SourceEntry title_entry;
    title_entry.archive_path = QStringLiteral("title.json");
    title_entry.kind = QStringLiteral("title");
    title_entry.data = QByteArray(payload.data(), static_cast<int>(payload.size()));
    sources.prepend(std::move(title_entry));

    QJsonObject packing;
    packing.insert(QStringLiteral("images"), options.pack_images);
    packing.insert(QStringLiteral("media"), options.pack_media);
    packing.insert(QStringLiteral("fonts"), options.pack_fonts);
    QJsonObject manifest;
    manifest.insert(QStringLiteral("package_id"),
                    QString::fromStdString(make_uuid()));
    manifest.insert(QStringLiteral("created_utc"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    manifest.insert(QStringLiteral("schema_version"),
                    fxm::serialization::kCurrentTitleSchemaVersion);
    manifest.insert(QStringLiteral("development_version"),
                    fxm::serialization::kCurrentDevelopmentVersion);
    manifest.insert(QStringLiteral("packing"), packing);

    QString packed_error;
    if (!fxm::packed_title::write(QString::fromStdString(path), sources,
                                  manifest, &packed_error)) {
        if (error) *error = packed_error.toStdString();
        FXM_LOG_ERROR("ImportExport", QStringLiteral(
            "Packed title export failed id=%1 path=%2 error=%3")
            .arg(QString::fromStdString(id), QString::fromStdString(path), packed_error));
        return false;
    }
    FXM_LOG_INFO("ImportExport", QStringLiteral(
        "Exported packed title id=%1 path=%2 entries=%3 images=%4 media=%5 fonts=%6")
        .arg(QString::fromStdString(id), QString::fromStdString(path))
        .arg(sources.size()).arg(options.pack_images).arg(options.pack_media)
        .arg(options.pack_fonts));
    return true;
}

std::shared_ptr<Title> TitleDataStore::import_title(const std::string &path, std::string *error,
                                                        TitleImportDiagnostics *diagnostics)
{
    if (error) error->clear();
    if (diagnostics)
        *diagnostics = TitleImportDiagnostics{};
    try {
        json root;
        const QString input_path = QString::fromStdString(path);
        const bool packed = QFileInfo(input_path).suffix().compare(
                                QStringLiteral("fxmp"), Qt::CaseInsensitive) == 0 ||
                            fxm::packed_title::has_packed_signature(input_path);
        if (packed) {
            std::string packed_path;
            if (const auto *provider = fxm::host_context_provider())
                packed_path = provider->config_path("packed");
            if (packed_path.empty())
                packed_path = title_data_dir() + "/packed";
            if (packed_path.empty()) {
                if (error) *error = "Could not resolve the packed resource directory.";
                return nullptr;
            }
            const QString extraction_base = QString::fromStdString(packed_path);
            QDir().mkpath(extraction_base);
            fxm::packed_title::ReadResult packed_result;
            QString packed_error;
            if (!fxm::packed_title::read(input_path, extraction_base,
                                         &packed_result, &packed_error)) {
                if (error) *error = packed_error.toStdString();
                return nullptr;
            }
            root = json::parse(packed_result.title_json.constData(),
                               packed_result.title_json.constData() +
                                   packed_result.title_json.size(),
                               nullptr, false);
            if (root.is_discarded()) {
                if (error) *error = "Invalid title.json in packed title.";
                return nullptr;
            }
            if (!resolve_packed_uri_json(root, packed_result.extraction_root, error))
                return nullptr;
            for (const QString &font_path : packed_result.extracted_font_paths)
                register_packed_font_file(font_path);
        } else if (!read_json_file(path, root, error)) {
            return nullptr;
        }
        json jt;
        if (root.is_object() && root.contains("titles") &&
            root["titles"].is_array() && !root["titles"].empty()) {
            std::string active_id;
            if (root.contains("project") && root["project"].is_object())
                active_id = project_string(root["project"], "active_title_id");
            const auto selected = std::find_if(
                root["titles"].begin(), root["titles"].end(),
                [&active_id](const json &candidate) {
                    return !active_id.empty() && candidate.is_object() &&
                           project_string(candidate, "id") == active_id;
                });
            jt = selected != root["titles"].end()
                ? *selected : root["titles"].front();
        } else if (root.is_object() && root.contains("title"))
            jt = root["title"];
        else if (root.is_array() && !root.empty())
            jt = root.front();
        else if (root.is_object())
            jt = root;
        else
            throw std::runtime_error("Unsupported template file format.");

        auto imported = title_from_json(jt, true, true, error, diagnostics);
        if (imported && root.is_object()) {
            json meta = root.value("metadata", json::object());
            if (imported->name.empty())
                imported->name = bounded_string(meta, "title", bounded_string(root, "template_title", "Imported Title", kMaxNameLength), kMaxNameLength);
            if (imported->description.empty())
                imported->description = bounded_string(meta, "description", bounded_string(root, "description", "", kMaxTextLength), kMaxTextLength);
            if (imported->creator.empty())
                imported->creator = bounded_string(meta, "creator", bounded_string(root, "creator", "", kMaxNameLength), kMaxNameLength);
            if (imported->creation_date.empty())
                imported->creation_date = bounded_string(meta, "creation_date", bounded_string(root, "creation_date", "", kMaxNameLength), kMaxNameLength);
        }
        if (imported && imported->preview_screenshot_png_base64.empty() && root.is_object()) {
            json screenshot = root.value("screenshot", json::object());
            if (screenshot.empty() && root.contains("metadata") && root["metadata"].is_object())
                screenshot = root["metadata"].value("screenshot", json::object());
            if (screenshot.is_object()) {
                const std::string png_base64 = bounded_string(screenshot, "data_base64", "",
                                                              kMaxScreenshotBase64Length);
                imported->preview_screenshot_png_base64 = png_base64;
            }
        }
        if (error && !error->empty())
            throw std::runtime_error(*error);
        if (!imported || imported->layers.empty())
            throw std::runtime_error("Template data was empty.");
        std::unordered_set<std::string> seen_ids;
        ensure_unique_title_id(imported, seen_ids);

        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            imported->name = unique_title_name_locked(
                ordered_published_titles_locked(titles_, published_titles_),
                imported->name.empty() ? "Imported Title" : imported->name);
            titles_.push_back(imported);
            title_mutex_locked(imported);
            published_titles_[imported->id] = clone_title_snapshot(imported);
        }

        auto imported_title = get_title(imported->id);
        notify_change();
        save();
        FXM_LOG_INFO("ImportExport", QStringLiteral(
            "Imported title id=%1 name=%2 path=%3 layers=%4")
            .arg(QString::fromStdString(imported_title->id),
                 QString::fromStdString(imported_title->name),
                 QString::fromStdString(path))
            .arg(static_cast<int>(imported_title->layers.size())));
        return imported_title;
    } catch (const std::exception &e) {
        if (error) *error = e.what();
        FXM_LOG_ERROR("ImportExport", QStringLiteral(
            "Title import failed path=%1 error=%2")
            .arg(QString::fromStdString(path), QString::fromUtf8(e.what())));
        return nullptr;
    }
}

void TitleDataStore::load()
{
    const std::string path = data_path();
    json root;
    std::string error;
    if (!read_json_file(path, root, &error)) {
        bool preserved_existing_store = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            /* A reload of the same collection can briefly race filesystem or
             * security-software activity. Never turn that transient failure
             * into an empty in-memory store. A genuinely different collection
             * still starts empty when it has no saved file. */
            const bool same_collection = !loaded_path_.empty() && loaded_path_ == path;
            preserved_existing_store = same_collection && !titles_.empty();
            if (!preserved_existing_store) {
                loaded_path_ = path;
                titles_.clear();
                clear_title_publications_locked();
                persistence_ready_for_save_ = error == "Could not open the file.";
            }
        }

        if (preserved_existing_store) {
            host_log(fxm::LogLevel::Warning, QStringLiteral(
                "Failed to reload titles for the current scene collection; "
                "keeping the already loaded titles in memory: %1")
                    .arg(QString::fromStdString(error)));
            FXM_LOG_WARNING("TitleStore", QStringLiteral(
                "Reload failed; preserved in-memory titles path=%1 error=%2")
                .arg(QString::fromStdString(path), QString::fromStdString(error)));
            return;
        }

        fxm::ticker_runtime::clear_all();
        fxm::asset_runtime::clear_all();
        notify_change();
        if (error == "Could not open the file.") {
            host_log(fxm::LogLevel::Info, QStringLiteral(
                "No saved titles found for this scene collection, starting fresh."));
            FXM_LOG_INFO("TitleStore", QStringLiteral(
                "No saved title store; starting fresh path=%1")
                .arg(QString::fromStdString(path)));
        } else {
            host_log(fxm::LogLevel::Warning, QStringLiteral(
                "Failed to read scene collection titles file: %1")
                    .arg(QString::fromStdString(error)));
            FXM_LOG_WARNING("TitleStore", QStringLiteral(
                "Failed to read title store path=%1 error=%2")
                .arg(QString::fromStdString(path), QString::fromStdString(error)));
        }
        return;
    }

    try {
        const json *stored_titles = nullptr;
        if (root.is_array()) {
            stored_titles = &root;
        } else if (root.is_object() && root.contains("titles") &&
                   root["titles"].is_array()) {
            /* Future-compatible object roots are accepted, while saves remain
             * arrays so pre-schema FXM builds can still open the collection. */
            stored_titles = &root["titles"];
        } else {
            throw std::runtime_error("Saved titles root must be an array or an object containing a titles array.");
        }

        std::vector<std::shared_ptr<Title>> loaded;
        std::unordered_set<std::string> seen_ids;
        const size_t count = std::min(stored_titles->size(), kMaxTitles);
        loaded.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            try {
                auto title = title_from_json((*stored_titles)[i], false);
                ensure_unique_title_id(title, seen_ids);
                loaded.push_back(title);
            } catch (const std::exception &entry_error) {
                FXM_LOG_WARNING("Serialization", QStringLiteral(
                    "Skipped unrecoverable title entry index=%1 error=%2")
                    .arg(static_cast<qulonglong>(i))
                    .arg(QString::fromUtf8(entry_error.what())));
            }
        }
        size_t loaded_count = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            loaded_path_ = path;
            clear_title_publications_locked();
            titles_ = std::move(loaded);
            for (const auto &title : titles_) {
                if (!title)
                    continue;
                title_mutex_locked(title);
                published_titles_[title->id] = clone_title_snapshot(title);
            }
            persistence_ready_for_save_ = true;
            loaded_count = titles_.size();
        }
        fxm::ticker_runtime::clear_all();
        fxm::asset_runtime::clear_all();
        notify_change();
        host_log(fxm::LogLevel::Info, QStringLiteral(
            "Loaded %1 title(s) for this scene collection.")
                .arg(static_cast<qulonglong>(loaded_count)));
        FXM_LOG_INFO("TitleStore", QStringLiteral(
            "Loaded titles count=%1 path=%2")
            .arg(static_cast<qulonglong>(loaded_count))
            .arg(QString::fromStdString(path)));
    } catch (const std::exception &e) {
        bool preserved_existing_store = false;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            const bool same_collection = !loaded_path_.empty() && loaded_path_ == path;
            preserved_existing_store = same_collection && !titles_.empty();
            if (!preserved_existing_store) {
                loaded_path_ = path;
                titles_.clear();
                clear_title_publications_locked();
                persistence_ready_for_save_ = false;
            }
        }

        if (preserved_existing_store) {
            host_log(fxm::LogLevel::Warning, QStringLiteral(
                "Failed to parse the current scene collection titles file; "
                "keeping the already loaded titles in memory: %1")
                    .arg(QString::fromUtf8(e.what())));
            return;
        }

        fxm::ticker_runtime::clear_all();
        fxm::asset_runtime::clear_all();
        notify_change();
        host_log(fxm::LogLevel::Warning, QStringLiteral(
            "Failed to parse scene collection titles file: %1")
                .arg(QString::fromUtf8(e.what())));
    }
}
