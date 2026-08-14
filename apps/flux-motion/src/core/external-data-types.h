#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <utility>

/* Provider-neutral external-data model. Definitions and bindings are authored
 * and serialized with a title; current provider values and connection state
 * remain runtime-only inside ExternalDataManager. */
enum class ExternalDataType {
    String = 0,
    Integer = 1,
    Float = 2,
    Boolean = 3,
    Color = 4,
    DateTime = 5,
    FilePath = 6,
    Url = 7,
};

enum class ExternalDataConnectionState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
    Error = 3,
    Updating = 4,
    Stale = 5,
};

enum class ExternalDataProviderType {
    None = 0,
    JsonFile = 1,
    CsvFile = 2,
    HttpJson = 3,
    WebSocket = 4,
    LocalTextFile = 5,
    ManualTable = 6,
};

enum class ExternalDataRefreshMode {
    RefreshOnCue = 0,
    RefreshContinuously = 1,
    RefreshManually = 2,
};

enum class ExternalDataTextCase {
    None = 0,
    Uppercase = 1,
    Lowercase = 2,
    TitleCase = 3,
};

enum class ExternalDataEmptyValueMode {
    KeepEmpty = 0,
    UseFallback = 1,
    Replacement = 2,
};

enum class LiveTextTableUpdateMode {
    ReplaceRows = 0,
    AppendRows = 1,
    SynchronizeRows = 2,
};

struct ExternalDataConditionalReplacement {
    std::string match;
    std::string replacement;
    bool case_sensitive = true;
};

struct ExternalDataFormatterConfig {
    std::string prefix;
    std::string suffix;
    bool number_format_enabled = false;
    int decimal_places = -1;
    bool thousands_separator = false;
    ExternalDataTextCase text_case = ExternalDataTextCase::None;
    std::string date_time_format;
    std::vector<ExternalDataConditionalReplacement> conditional_replacements;
    ExternalDataEmptyValueMode empty_value_mode = ExternalDataEmptyValueMode::KeepEmpty;
    std::string empty_replacement;
};

struct ExternalDataValue {
    ExternalDataType type = ExternalDataType::String;
    bool is_set = false;
    std::string string_value;
    int64_t integer_value = 0;
    double float_value = 0.0;
    bool boolean_value = false;
    uint32_t color_value = 0xFFFFFFFF;

    static ExternalDataValue string(std::string value, ExternalDataType value_type = ExternalDataType::String)
    {
        ExternalDataValue result;
        result.type = value_type;
        result.is_set = true;
        result.string_value = std::move(value);
        return result;
    }

    static ExternalDataValue integer(int64_t value)
    {
        ExternalDataValue result;
        result.type = ExternalDataType::Integer;
        result.is_set = true;
        result.integer_value = value;
        return result;
    }

    static ExternalDataValue floating(double value)
    {
        ExternalDataValue result;
        result.type = ExternalDataType::Float;
        result.is_set = true;
        result.float_value = value;
        return result;
    }

    static ExternalDataValue boolean(bool value)
    {
        ExternalDataValue result;
        result.type = ExternalDataType::Boolean;
        result.is_set = true;
        result.boolean_value = value;
        return result;
    }

    static ExternalDataValue color(uint32_t argb)
    {
        ExternalDataValue result;
        result.type = ExternalDataType::Color;
        result.is_set = true;
        result.color_value = argb;
        return result;
    }
};

inline bool operator==(const ExternalDataValue &a, const ExternalDataValue &b)
{
    if (a.type != b.type || a.is_set != b.is_set)
        return false;
    if (!a.is_set)
        return true;
    switch (a.type) {
    case ExternalDataType::Integer:
        return a.integer_value == b.integer_value;
    case ExternalDataType::Float:
        return a.float_value == b.float_value;
    case ExternalDataType::Boolean:
        return a.boolean_value == b.boolean_value;
    case ExternalDataType::Color:
        return a.color_value == b.color_value;
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    default:
        return a.string_value == b.string_value;
    }
}

inline bool operator!=(const ExternalDataValue &a, const ExternalDataValue &b)
{
    return !(a == b);
}

struct ExternalDataFieldDefinition {
    std::string path;
    std::string name;
    ExternalDataType type = ExternalDataType::String;
    ExternalDataValue default_value;
    bool has_default_value = false;
};

struct ExternalDataProviderConfig {
    ExternalDataProviderType type = ExternalDataProviderType::None;
    bool enabled = true;
    std::string location;
    int polling_interval_ms = 0;
    ExternalDataRefreshMode refresh_mode = ExternalDataRefreshMode::RefreshContinuously;
    int rate_limit_ms = 50;
    bool keep_last_value = true;
    int stale_after_ms = 0;

    /* JSON/HTTP/WebSocket */
    std::string root_path;

    /* HTTP/WebSocket */
    std::map<std::string, std::string> headers;
    std::string authentication_token;
    int timeout_ms = 5000;
    int retry_count = 2;
    int retry_backoff_ms = 1000;

    /* WebSocket */
    int reconnect_initial_ms = 1000;
    int reconnect_max_ms = 30000;

    /* CSV */
    bool csv_first_row_headers = true;
    int csv_row_index = 0;
    std::map<std::string, std::string> csv_column_mapping;

    /* Local text file */
    std::string text_field_path = "text";

    /* Manual/internal provider. Values are authored configuration, while the
     * manager's current values remain runtime-only. */
    std::map<std::string, ExternalDataValue> manual_values;
};

struct ExternalDataSourceDefinition {
    std::string id;
    std::string name;
    std::vector<ExternalDataFieldDefinition> fields;
    ExternalDataProviderConfig provider;
};

struct ExternalPropertyBinding {
    bool enabled = true;
    std::string property_path;
    std::string source_id;
    std::string field_path;
    /* Legacy compact formatter retained for backward compatibility. New UI writes
     * formatter_config and the runtime applies both through one deterministic pipeline. */
    std::string formatter;
    ExternalDataFormatterConfig formatter_config;
    ExternalDataValue fallback_value;
    bool has_fallback_value = false;
    /* Runtime-only value supplied by higher-level adapters such as table-row
     * mappings. It participates in the same live/fallback resolution pipeline
     * but is intentionally omitted from title serialization. */
    ExternalDataValue runtime_value;
    bool has_runtime_value = false;
};

enum class LiveTextCueCellState {
    Authored,
    ExternalBound,
    ExternalTableManaged,
    DetachedFromTable
};

struct LiveTextExternalBinding {
    std::string row_id;
    std::string layer_id;
    /* Non-empty only for bindings generated by a table mapping. Clearing this
     * marker turns an edited cell binding into an explicit user override. */
    std::string table_binding_id;
    ExternalPropertyBinding binding;
};

struct ExternalDataTableRow {
    std::string key;
    std::map<std::string, ExternalDataValue> values;
    /* Provider field paths for this row. They allow the table mapper to create
     * ordinary live cell bindings instead of maintaining a second value path. */
    std::map<std::string, std::string> source_field_paths;
};

struct ExternalDataTableSnapshot {
    std::string path;
    std::vector<std::string> columns;
    std::vector<ExternalDataTableRow> rows;
    int64_t last_update_timestamp_ms = 0;
};

struct LiveTextTableColumnBinding {
    std::string layer_id;
    ExternalPropertyBinding binding;
};

struct LiveTextTableBinding {
    bool enabled = true;
    std::string id;
    std::string source_id;
    std::string table_path;
    LiveTextTableUpdateMode update_mode = LiveTextTableUpdateMode::SynchronizeRows;
    std::string row_id_field;
    int start_row = 0;
    int maximum_rows = 0; /* zero means all rows */
    bool ignore_empty_rows = true;
    bool preserve_manual_rows = true;
    std::vector<LiveTextTableColumnBinding> columns;
};
