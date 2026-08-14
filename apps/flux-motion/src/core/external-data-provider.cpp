#include "external-data-provider.h"
#include "external-json-path.h"

#include "external-data.h"
#include "external-data-log.h"
#include "performance-counters.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <QtWebSockets/QWebSocket>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {

constexpr qint64 kMaxProviderPayloadBytes = 16 * 1024 * 1024;
constexpr size_t kMaxProviderFields = 65536;
constexpr size_t kMaxProviderTableRows = 10000;
constexpr int kMinimumPublishCoalesceMs = 16;

static int64_t now_ms()
{
    return QDateTime::currentMSecsSinceEpoch();
}

static QString qstr(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

static std::string stdstr(const QString &value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<size_t>(utf8.size()));
}

static std::string trim_ascii(std::string value)
{
    auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

static std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

using fxm::external_data::JsonPathToken;
using fxm::external_data::parse_json_path;
using fxm::external_data::resolve_json_path;

static ExternalDataProviderValidation validate_json_paths(
    const ExternalDataSourceDefinition &definition)
{
    std::vector<JsonPathToken> tokens;
    std::string error;
    if (!definition.provider.root_path.empty() &&
        !parse_json_path(definition.provider.root_path, tokens, &error)) {
        return {false, "Invalid JSON root path: " + error};
    }
    for (const auto &field : definition.fields) {
        if (field.path.empty())
            continue;
        if (!parse_json_path(field.path, tokens, &error))
            return {false, "Invalid JSON field path '" + field.path + "': " + error};
    }
    return {};
}

static bool parse_color_string(const std::string &input, uint32_t &argb)
{
    std::string value = trim_ascii(input);
    if (!value.empty() && value.front() == '#')
        value.erase(value.begin());
    if (value.size() != 6 && value.size() != 8)
        return false;
    if (!std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        }))
        return false;
    try {
        const uint32_t parsed = static_cast<uint32_t>(std::stoul(value, nullptr, 16));
        argb = value.size() == 6 ? (0xFF000000u | parsed) : parsed;
        return true;
    } catch (...) {
        return false;
    }
}

static bool json_to_external_value(const json &node, ExternalDataType type,
                                   ExternalDataValue &out, std::string &error)
{
    try {
        switch (type) {
        case ExternalDataType::Integer:
            if (node.is_number_integer()) {
                out = ExternalDataValue::integer(node.get<int64_t>());
                return true;
            }
            if (node.is_number_unsigned()) {
                const uint64_t value = node.get<uint64_t>();
                if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    out = ExternalDataValue::integer(static_cast<int64_t>(value));
                    return true;
                }
            }
            if (node.is_string()) {
                const std::string value = trim_ascii(node.get<std::string>());
                size_t consumed = 0;
                const long long parsed = std::stoll(value, &consumed);
                if (consumed == value.size()) {
                    out = ExternalDataValue::integer(static_cast<int64_t>(parsed));
                    return true;
                }
            }
            break;
        case ExternalDataType::Float:
            if (node.is_number()) {
                const double value = node.get<double>();
                if (std::isfinite(value)) {
                    out = ExternalDataValue::floating(value);
                    return true;
                }
            }
            if (node.is_string()) {
                const std::string value = trim_ascii(node.get<std::string>());
                size_t consumed = 0;
                const double parsed = std::stod(value, &consumed);
                if (consumed == value.size() && std::isfinite(parsed)) {
                    out = ExternalDataValue::floating(parsed);
                    return true;
                }
            }
            break;
        case ExternalDataType::Boolean:
            if (node.is_boolean()) {
                out = ExternalDataValue::boolean(node.get<bool>());
                return true;
            }
            if (node.is_number_integer()) {
                out = ExternalDataValue::boolean(node.get<int64_t>() != 0);
                return true;
            }
            if (node.is_string()) {
                const std::string value = lower_ascii(trim_ascii(node.get<std::string>()));
                if (value == "true" || value == "yes" || value == "on" || value == "1") {
                    out = ExternalDataValue::boolean(true);
                    return true;
                }
                if (value == "false" || value == "no" || value == "off" || value == "0") {
                    out = ExternalDataValue::boolean(false);
                    return true;
                }
            }
            break;
        case ExternalDataType::Color:
            if (node.is_number_unsigned()) {
                const uint64_t value = node.get<uint64_t>();
                if (value <= UINT32_MAX) {
                    out = ExternalDataValue::color(static_cast<uint32_t>(value));
                    return true;
                }
            }
            if (node.is_number_integer()) {
                const int64_t value = node.get<int64_t>();
                if (value >= 0 && value <= UINT32_MAX) {
                    out = ExternalDataValue::color(static_cast<uint32_t>(value));
                    return true;
                }
            }
            if (node.is_string()) {
                uint32_t color = 0;
                if (parse_color_string(node.get<std::string>(), color)) {
                    out = ExternalDataValue::color(color);
                    return true;
                }
            }
            break;
        case ExternalDataType::String:
        case ExternalDataType::DateTime:
        case ExternalDataType::FilePath:
        case ExternalDataType::Url: {
            std::string value;
            if (node.is_string())
                value = node.get<std::string>();
            else if (node.is_boolean())
                value = node.get<bool>() ? "true" : "false";
            else if (node.is_number_integer())
                value = std::to_string(node.get<int64_t>());
            else if (node.is_number_unsigned())
                value = std::to_string(node.get<uint64_t>());
            else if (node.is_number_float()) {
                std::ostringstream stream;
                stream.precision(15);
                stream << node.get<double>();
                value = stream.str();
            } else if (node.is_null())
                value.clear();
            else
                value = node.dump();
            out = ExternalDataValue::string(std::move(value), type);
            return true;
        }
        }
    } catch (...) {
    }
    error = "JSON value cannot be converted to the configured field type.";
    return false;
}

static ExternalDataType infer_json_type(const json &node)
{
    if (node.is_boolean())
        return ExternalDataType::Boolean;
    if (node.is_number_integer() || node.is_number_unsigned())
        return ExternalDataType::Integer;
    if (node.is_number_float())
        return ExternalDataType::Float;
    return ExternalDataType::String;
}

static void flatten_json(const json &node, const std::string &path,
                         std::map<std::string, ExternalDataValue> &values,
                         size_t limit = kMaxProviderFields)
{
    if (values.size() >= limit)
        return;
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end() && values.size() < limit; ++it) {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            flatten_json(it.value(), child, values, limit);
        }
        return;
    }
    if (node.is_array()) {
        for (size_t i = 0; i < node.size() && values.size() < limit; ++i) {
            const std::string child = path + "[" + std::to_string(i) + "]";
            flatten_json(node[i], child, values, limit);
        }
        return;
    }
    if (path.empty())
        return;
    ExternalDataValue value;
    std::string error;
    if (json_to_external_value(node, infer_json_type(node), value, error))
        values[path] = std::move(value);
}


static void flatten_json_table_row(
    const json &node, const std::string &column_path,
    const std::string &source_path, ExternalDataTableRow &row,
    std::vector<std::string> &columns, size_t limit = kMaxProviderFields)
{
    if (row.values.size() >= limit)
        return;
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end() && row.values.size() < limit; ++it) {
            const std::string child_column = column_path.empty()
                ? it.key() : column_path + "." + it.key();
            const std::string child_source = source_path.empty()
                ? it.key() : source_path + "." + it.key();
            flatten_json_table_row(it.value(), child_column, child_source,
                                   row, columns, limit);
        }
        return;
    }
    if (node.is_array()) {
        for (size_t i = 0; i < node.size() && row.values.size() < limit; ++i) {
            const std::string suffix = "[" + std::to_string(i) + "]";
            flatten_json_table_row(node[i], column_path + suffix,
                                   source_path + suffix, row, columns, limit);
        }
        return;
    }
    const std::string column = column_path.empty() ? "value" : column_path;
    ExternalDataValue value;
    std::string error;
    if (!json_to_external_value(node, infer_json_type(node), value, error))
        return;
    row.values[column] = value;
    row.source_field_paths[column] = source_path;
    if (std::find(columns.begin(), columns.end(), column) == columns.end())
        columns.push_back(column);
}

static void discover_json_tables(
    const json &node, const std::string &path,
    std::map<std::string, ExternalDataTableSnapshot> &tables)
{
    if (tables.size() >= 128)
        return;
    if (node.is_object()) {
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string child = path.empty() ? it.key() : path + "." + it.key();
            discover_json_tables(it.value(), child, tables);
        }
        return;
    }
    if (!node.is_array())
        return;

    ExternalDataTableSnapshot table;
    table.path = path.empty() ? "$" : path;
    const size_t row_count = std::min(node.size(), kMaxProviderTableRows);
    table.rows.reserve(row_count);
    for (size_t i = 0; i < row_count; ++i) {
        ExternalDataTableRow row;
        row.key = std::to_string(i);
        const std::string source_base = path.empty()
            ? "[" + std::to_string(i) + "]"
            : path + "[" + std::to_string(i) + "]";
        flatten_json_table_row(node[i], {}, source_base, row, table.columns);
        if (!row.values.empty())
            table.rows.push_back(std::move(row));
    }
    if (!table.rows.empty())
        tables[table.path] = std::move(table);
}

static bool extract_json_values(
    const ExternalDataSourceDefinition &definition, const QByteArray &payload,
    std::map<std::string, ExternalDataValue> &values,
    std::map<std::string, ExternalDataTableSnapshot> &tables,
    std::string &error)
{
    values.clear();
    tables.clear();
    json document;
    try {
        document = json::parse(payload.constData(), payload.constData() + payload.size());
    } catch (const std::exception &e) {
        error = std::string("Invalid JSON: ") + e.what();
        return false;
    }

    const json *root = &document;
    if (!definition.provider.root_path.empty()) {
        root = resolve_json_path(document, definition.provider.root_path, &error);
        if (!root)
            return false;
    }

    /* Discovery is always performed, even when authored schema entries exist.
     * This keeps every scalar path immediately bindable while configured fields
     * still override inferred types, aliases/defaults and conversion rules. */
    flatten_json(*root, {}, values);
    discover_json_tables(*root, {}, tables);

    std::vector<std::string> missing;
    for (const ExternalDataFieldDefinition &field : definition.fields) {
        if (field.path.empty())
            continue;
        std::string field_error;
        const json *node = resolve_json_path(*root, field.path, &field_error);
        if (!node) {
            missing.push_back(field.path);
            continue;
        }
        ExternalDataValue value;
        if (!json_to_external_value(*node, field.type, value, field_error)) {
            error = "Field '" + field.path + "': " + field_error;
            return false;
        }
        values[field.path] = std::move(value);
    }

    if (values.empty()) {
        if (definition.fields.empty())
            error = "JSON contains no scalar fields.";
        else
            error = missing.empty() ? "No JSON fields were produced."
                                    : "Configured JSON fields were not found and no fields could be discovered.";
        return false;
    }
    return true;
}

static std::vector<std::vector<std::string>> parse_csv(const std::string &input,
                                                        std::string &error)
{
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < input.size() && input[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
            continue;
        }
        if (ch == '"' && field.empty()) {
            quoted = true;
        } else if (ch == ',') {
            row.push_back(std::move(field));
            field.clear();
        } else if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < input.size() && input[i + 1] == '\n')
                ++i;
            row.push_back(std::move(field));
            field.clear();
            rows.push_back(std::move(row));
            row.clear();
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        error = "CSV contains an unterminated quoted field.";
        return {};
    }
    if (!field.empty() || !row.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    while (!rows.empty() && rows.back().size() == 1 && rows.back().front().empty())
        rows.pop_back();
    return rows;
}

static bool string_to_external_value(const std::string &text, ExternalDataType type,
                                     ExternalDataValue &out)
{
    json node = text;
    std::string error;
    return json_to_external_value(node, type, out, error);
}

static bool extract_csv_values(
    const ExternalDataSourceDefinition &definition, const QByteArray &payload,
    std::map<std::string, ExternalDataValue> &values,
    std::map<std::string, ExternalDataTableSnapshot> &tables,
    std::string &error)
{
    values.clear();
    tables.clear();
    const std::string input(payload.constData(), static_cast<size_t>(payload.size()));
    auto rows = parse_csv(input, error);
    if (rows.empty()) {
        if (error.empty())
            error = "CSV file has no rows.";
        return false;
    }

    std::vector<std::string> headers;
    size_t data_begin = 0;
    if (definition.provider.csv_first_row_headers) {
        headers = rows.front();
        data_begin = 1;
    }
    const size_t row_index = data_begin + static_cast<size_t>(std::max(0, definition.provider.csv_row_index));
    if (row_index >= rows.size()) {
        error = "Configured CSV row is outside the available data.";
        return false;
    }
    const auto &selected = rows[row_index];

    std::map<std::string, size_t> header_indexes;
    for (size_t i = 0; i < headers.size(); ++i)
        header_indexes[headers[i]] = i;

    auto resolve_column = [&](const std::string &path, const std::string &name,
                              size_t &column) -> bool {
        std::string spec = path;
        auto mapped = definition.provider.csv_column_mapping.find(path);
        if (mapped != definition.provider.csv_column_mapping.end())
            spec = mapped->second;
        if (definition.provider.csv_first_row_headers) {
            auto found = header_indexes.find(spec);
            if (found == header_indexes.end() && !name.empty())
                found = header_indexes.find(name);
            if (found == header_indexes.end())
                return false;
            column = found->second;
            return true;
        }
        try {
            size_t consumed = 0;
            const unsigned long parsed = std::stoul(spec, &consumed);
            if (consumed != spec.size())
                return false;
            column = static_cast<size_t>(parsed);
            return true;
        } catch (...) {
            return false;
        }
    };

    ExternalDataTableSnapshot table;
    table.path = "$rows";
    const size_t available_rows = rows.size() > data_begin ? rows.size() - data_begin : 0;
    const size_t table_row_count = std::min(available_rows, kMaxProviderTableRows);
    table.rows.reserve(table_row_count);
    for (size_t logical_row = 0; logical_row < table_row_count; ++logical_row) {
        const auto &csv_row = rows[data_begin + logical_row];
        ExternalDataTableRow table_row;
        table_row.key = std::to_string(logical_row);
        for (size_t column = 0; column < csv_row.size(); ++column) {
            const std::string field = definition.provider.csv_first_row_headers &&
                    column < headers.size() && !headers[column].empty()
                ? headers[column] : std::to_string(column);
            const std::string source_path = "$rows[" + std::to_string(logical_row) + "]." + field;
            table_row.values[field] = ExternalDataValue::string(csv_row[column]);
            table_row.source_field_paths[field] = source_path;
            values[source_path] = table_row.values[field];
            if (std::find(table.columns.begin(), table.columns.end(), field) == table.columns.end())
                table.columns.push_back(field);
        }
        for (const ExternalDataFieldDefinition &field_definition : definition.fields) {
            size_t column = 0;
            if (!resolve_column(field_definition.path, field_definition.name, column) ||
                column >= csv_row.size())
                continue;
            ExternalDataValue converted;
            if (!string_to_external_value(csv_row[column], field_definition.type, converted))
                continue;
            const std::string source_path = "$rows[" + std::to_string(logical_row) + "]." +
                                            field_definition.path;
            table_row.values[field_definition.path] = converted;
            table_row.source_field_paths[field_definition.path] = source_path;
            values[source_path] = converted;
            if (std::find(table.columns.begin(), table.columns.end(), field_definition.path) ==
                table.columns.end())
                table.columns.push_back(field_definition.path);
        }
        if (!table_row.values.empty())
            table.rows.push_back(std::move(table_row));
    }
    if (!table.rows.empty())
        tables[table.path] = std::move(table);

    /* Native columns are always discovered first. Authored mappings are then
     * layered on top, allowing aliases and type overrides without hiding other
     * columns from the binding UI. */
    for (size_t column = 0;
         column < selected.size() && values.size() < kMaxProviderFields; ++column) {
        const std::string path = definition.provider.csv_first_row_headers && column < headers.size()
            ? headers[column]
            : std::to_string(column);
        if (!path.empty())
            values[path] = ExternalDataValue::string(selected[column]);
    }

    for (const ExternalDataFieldDefinition &field : definition.fields) {
        size_t column = 0;
        if (!resolve_column(field.path, field.name, column) || column >= selected.size())
            continue;
        ExternalDataValue value;
        if (!string_to_external_value(selected[column], field.type, value)) {
            error = "CSV value for field '" + field.path + "' cannot be converted.";
            return false;
        }
        values[field.path] = std::move(value);
    }
    if (values.empty()) {
        error = "The selected CSV row contains no bindable columns.";
        return false;
    }
    return true;
}

class ExternalDataProviderBase : public QObject, public IExternalDataProvider {
public:
    explicit ExternalDataProviderBase(ExternalDataSourceDefinition definition)
        : definition_(std::move(definition))
    {
        poll_timer_.setParent(this);
        stale_timer_.setParent(this);
        publish_timer_.setParent(this);
        poll_timer_.setSingleShot(false);
        stale_timer_.setSingleShot(false);
        publish_timer_.setSingleShot(true);
        QObject::connect(&poll_timer_, &QTimer::timeout, this, [this]() { refresh(); });
        QObject::connect(&stale_timer_, &QTimer::timeout, this, [this]() { check_stale(); });
        QObject::connect(&publish_timer_, &QTimer::timeout, this, [this]() { publish_pending(); });
        configure_timers();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Provider", [this]() {
                std::ostringstream stream;
                stream << "created source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " type=" << ExternalDataLog::provider_type_name(definition_.provider.type)
                       << " location=" << ExternalDataLog::safe_location(definition_.provider.location)
                       << " refresh=" << ExternalDataLog::refresh_mode_name(definition_.provider.refresh_mode)
                       << " pollMs=" << definition_.provider.polling_interval_ms
                       << " rateLimitMs=" << definition_.provider.rate_limit_ms
                       << " keepLast=" << (definition_.provider.keep_last_value ? 1 : 0);
                return stream.str();
            });
    }

    std::string source_id() const override { return definition_.id; }
    ExternalDataProviderType provider_type() const override { return definition_.provider.type; }
    ExternalDataConnectionState state() const override { return state_; }
    std::string error_message() const override { return error_message_; }

    ExternalDataProviderValidation validate() const override
    {
        if (definition_.id.empty())
            return {false, "Source ID is required."};
        return validate_configuration();
    }

    void connect_provider() override
    {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "Provider", [this]() {
                return std::string("connect requested source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " type=" + ExternalDataLog::provider_type_name(definition_.provider.type) +
                       " location=" + ExternalDataLog::safe_location(definition_.provider.location);
            });
        active_ = true;
        const ExternalDataProviderValidation result = validate();
        if (!result.valid) {
            fail(result.error_message);
            return;
        }
        configure_timers();
        set_state(ExternalDataConnectionState::Connecting, {});
        if (definition_.provider.refresh_mode ==
            ExternalDataRefreshMode::RefreshContinuously) {
            refresh();
        } else {
            set_state(ExternalDataConnectionState::Connected, {});
        }
    }

    void disconnect_provider() override
    {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "Provider", [this]() {
                return std::string("disconnect requested source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " state=" + ExternalDataLog::connection_state_name(state_);
            });
        active_ = false;
        poll_timer_.stop();
        stale_timer_.stop();
        publish_timer_.stop();
        pending_values_.clear();
        pending_tables_.clear();
        pending_tables_complete_ = false;
        on_disconnect();
        set_state(ExternalDataConnectionState::Disconnected, {});
    }

    void reconfigure(const ExternalDataSourceDefinition &definition) override
    {
        const bool was_active = active_;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "Provider", [&]() {
                std::ostringstream stream;
                stream << "reconfigure source=" << ExternalDataLog::sanitize(definition.id, 160)
                       << " type=" << ExternalDataLog::provider_type_name(definition.provider.type)
                       << " enabled=" << (definition.provider.enabled ? 1 : 0)
                       << " refresh=" << ExternalDataLog::refresh_mode_name(definition.provider.refresh_mode)
                       << " location=" << ExternalDataLog::safe_location(definition.provider.location);
                return stream.str();
            });
        definition_ = definition;
        ExternalDataManager::instance().register_source(definition_);
        configure_timers();
        if (!definition_.provider.enabled) {
            disconnect_provider();
        } else if (was_active) {
            on_reconfigured();
            if (definition_.provider.refresh_mode ==
                ExternalDataRefreshMode::RefreshContinuously)
                refresh();
        }
    }

protected:
    virtual ExternalDataProviderValidation validate_configuration() const = 0;
    virtual void on_disconnect() {}
    virtual void on_reconfigured() {}

    void begin_update()
    {
        if (!active_)
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Provider", [this]() {
                return std::string("refresh begin source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " type=" + ExternalDataLog::provider_type_name(definition_.provider.type) +
                       " state=" + ExternalDataLog::connection_state_name(state_);
            });
        const bool refreshing_live_source =
            state_ == ExternalDataConnectionState::Connected ||
            state_ == ExternalDataConnectionState::Updating ||
            state_ == ExternalDataConnectionState::Stale;
        set_state(refreshing_live_source
                      ? ExternalDataConnectionState::Updating
                      : ExternalDataConnectionState::Connecting,
                  {});
    }

    void submit_values(
        std::map<std::string, ExternalDataValue> values,
        std::map<std::string, ExternalDataTableSnapshot> tables = {})
    {
        if (!active_)
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Provider", [this, &values, &tables]() {
                std::size_t rows = 0;
                for (const auto &entry : tables)
                    rows += entry.second.rows.size();
                std::ostringstream stream;
                stream << "refresh parsed source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " values=" << values.size()
                       << " tables=" << tables.size()
                       << " tableRows=" << rows
                       << " publishDelayMs=" << std::clamp(definition_.provider.rate_limit_ms, 0, 60000);
                return stream.str();
            });
        for (auto &entry : values) {
            if (!entry.first.empty() && entry.second.is_set)
                pending_values_[qstr(entry.first)] = std::move(entry.second);
        }
        pending_tables_ = std::move(tables);
        pending_tables_complete_ = true;
        pending_success_ = true;
        fxm::perf::add(fxm::perf::Counter::ExternalUpdatesSubmitted);
        /* Even providers configured with a zero user rate limit get one event-loop
         * coalescing window. This prevents websocket bursts or repeated file
         * notifications from queueing one UI/render invalidation per field. */
        const int delay = std::max(
            kMinimumPublishCoalesceMs,
            std::clamp(definition_.provider.rate_limit_ms, 0, 60000));
        if (!publish_timer_.isActive())
            publish_timer_.start(delay);
        else
            fxm::perf::add(fxm::perf::Counter::ExternalUpdatesCoalesced);
    }

    void complete_without_values()
    {
        last_success_ms_ = now_ms();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Provider", [this]() {
                return std::string("refresh completed without values source=") +
                       ExternalDataLog::sanitize(definition_.id, 160);
            });
        set_state(ExternalDataConnectionState::Connected, {});
    }

    void fail(const std::string &message)
    {
        /* A newer failed refresh must not be overwritten by an older success
         * that is still waiting in the rate-limit coalescing window. The
         * already parsed values may still become the retained last-known data,
         * but the provider remains in Error until a later refresh succeeds. */
        pending_success_ = false;
        const std::string effective_message =
            message.empty() ? "External data provider error." : message;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Error, "Provider", [this, &effective_message]() {
                return std::string("failure source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " type=" + ExternalDataLog::provider_type_name(definition_.provider.type) +
                       " error=\"" + ExternalDataLog::sanitize(effective_message, 768) + "\"";
            });
        set_state(ExternalDataConnectionState::Error, effective_message);
    }

    void set_state(ExternalDataConnectionState state, const std::string &error)
    {
        state_ = state;
        error_message_ = error;
        ExternalDataManager::instance().set_connection_state(
            definition_.id, state, error, now_ms());
    }

    bool active_ = false;
    ExternalDataSourceDefinition definition_;

    void configure_timers()
    {
        poll_timer_.stop();
        stale_timer_.stop();
        if (active_ &&
            definition_.provider.refresh_mode ==
                ExternalDataRefreshMode::RefreshContinuously &&
            definition_.provider.polling_interval_ms > 0)
            poll_timer_.start(std::max(100, definition_.provider.polling_interval_ms));
        if (active_ && definition_.provider.stale_after_ms > 0) {
            const int interval = std::clamp(definition_.provider.stale_after_ms / 4, 250, 5000);
            stale_timer_.start(interval);
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Trace, "Provider", [this]() {
                std::ostringstream stream;
                stream << "timers source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " pollActive=" << (poll_timer_.isActive() ? 1 : 0)
                       << " pollIntervalMs=" << poll_timer_.interval()
                       << " staleActive=" << (stale_timer_.isActive() ? 1 : 0)
                       << " staleIntervalMs=" << stale_timer_.interval();
                return stream.str();
            });
    }

private:
    void check_stale()
    {
        if (!active_ || definition_.provider.stale_after_ms <= 0 || last_success_ms_ <= 0)
            return;
        if (now_ms() - last_success_ms_ >= definition_.provider.stale_after_ms &&
            state_ != ExternalDataConnectionState::Error &&
            state_ != ExternalDataConnectionState::Disconnected &&
            state_ != ExternalDataConnectionState::Connecting) {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "Provider", [this]() {
                    std::ostringstream stream;
                    stream << "marked stale source=" << ExternalDataLog::sanitize(definition_.id, 160)
                           << " ageMs=" << (now_ms() - last_success_ms_)
                           << " thresholdMs=" << definition_.provider.stale_after_ms;
                    return stream.str();
                });
            set_state(ExternalDataConnectionState::Stale, "Provider data is stale.");
        }
    }

    void publish_pending()
    {
        if (!active_)
            return;
        const int64_t timestamp = now_ms();
        const int submitted_values = pending_values_.size();
        const std::size_t submitted_tables = pending_tables_.size();
        int changed_values = 0;
        for (auto it = pending_values_.begin(); it != pending_values_.end(); ++it) {
            if (ExternalDataManager::instance().update_value(
                    definition_.id, stdstr(it.key()), it.value(), timestamp))
                ++changed_values;
        }
        pending_values_.clear();
        bool changed_tables = false;
        if (pending_tables_complete_) {
            changed_tables = ExternalDataManager::instance().synchronize_tables(
                definition_.id, std::move(pending_tables_), timestamp);
            pending_tables_.clear();
            pending_tables_complete_ = false;
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Provider", [this, submitted_values,
                                                        submitted_tables, changed_values,
                                                        changed_tables]() {
                std::ostringstream stream;
                stream << "published source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " submittedValues=" << submitted_values
                       << " changedValues=" << changed_values
                       << " submittedTables=" << submitted_tables
                       << " tablesChanged=" << (changed_tables ? 1 : 0);
                return stream.str();
            });
        if (pending_success_) {
            pending_success_ = false;
            last_success_ms_ = timestamp;
            set_state(ExternalDataConnectionState::Connected, {});
        }
    }

    ExternalDataConnectionState state_ = ExternalDataConnectionState::Disconnected;
    std::string error_message_;
    int64_t last_success_ms_ = 0;
    bool pending_success_ = false;
    QHash<QString, ExternalDataValue> pending_values_;
    std::map<std::string, ExternalDataTableSnapshot> pending_tables_;
    bool pending_tables_complete_ = false;
    QTimer poll_timer_;
    QTimer stale_timer_;
    QTimer publish_timer_;
};

class JsonFileProvider final : public ExternalDataProviderBase {
public:
    using ExternalDataProviderBase::ExternalDataProviderBase;

    ExternalDataProviderValidation validate_configuration() const override
    {
        if (definition_.provider.location.empty())
            return {false, "JSON file path is required."};
        return validate_json_paths(definition_);
    }

    void refresh() override
    {
        if (!active_)
            return;
        begin_update();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "FileProvider", [this]() {
                return std::string("reading source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " location=" + ExternalDataLog::safe_location(definition_.provider.location);
            });
        QFile file(qstr(definition_.provider.location));
        if (!file.open(QIODevice::ReadOnly)) {
            fail("Could not open JSON file: " + stdstr(file.errorString()));
            return;
        }
        if (file.size() > kMaxProviderPayloadBytes) {
            fail("JSON file exceeds the 16 MiB provider payload limit.");
            return;
        }
        const QByteArray payload = file.read(kMaxProviderPayloadBytes + 1);
        if (payload.size() > kMaxProviderPayloadBytes) {
            fail("JSON file exceeds the 16 MiB provider payload limit.");
            return;
        }
        std::map<std::string, ExternalDataValue> values;
        std::map<std::string, ExternalDataTableSnapshot> tables;
        std::string error;
        if (!extract_json_values(definition_, payload, values, tables, error)) {
            fail(error);
            return;
        }
        submit_values(std::move(values), std::move(tables));
    }
};

class CsvFileProvider final : public ExternalDataProviderBase {
public:
    using ExternalDataProviderBase::ExternalDataProviderBase;

    ExternalDataProviderValidation validate_configuration() const override
    {
        if (definition_.provider.location.empty())
            return {false, "CSV file path is required."};
        if (!definition_.provider.csv_first_row_headers) {
            for (const auto &field : definition_.fields) {
                std::string column = field.path;
                const auto mapped = definition_.provider.csv_column_mapping.find(field.path);
                if (mapped != definition_.provider.csv_column_mapping.end())
                    column = trim_ascii(mapped->second);
                if (column.empty() ||
                    !std::all_of(column.begin(), column.end(), [](unsigned char ch) {
                        return std::isdigit(ch) != 0;
                    })) {
                    return {false, "CSV fields require numeric column indexes when headers are disabled."};
                }
            }
        }
        return {};
    }

    void refresh() override
    {
        if (!active_)
            return;
        begin_update();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "FileProvider", [this]() {
                return std::string("reading source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " location=" + ExternalDataLog::safe_location(definition_.provider.location);
            });
        QFile file(qstr(definition_.provider.location));
        if (!file.open(QIODevice::ReadOnly)) {
            fail("Could not open CSV file: " + stdstr(file.errorString()));
            return;
        }
        if (file.size() > kMaxProviderPayloadBytes) {
            fail("CSV file exceeds the 16 MiB provider payload limit.");
            return;
        }
        const QByteArray payload = file.read(kMaxProviderPayloadBytes + 1);
        if (payload.size() > kMaxProviderPayloadBytes) {
            fail("CSV file exceeds the 16 MiB provider payload limit.");
            return;
        }
        std::map<std::string, ExternalDataValue> values;
        std::map<std::string, ExternalDataTableSnapshot> tables;
        std::string error;
        if (!extract_csv_values(definition_, payload, values, tables, error)) {
            fail(error);
            return;
        }
        submit_values(std::move(values), std::move(tables));
    }
};

class LocalTextFileProvider final : public ExternalDataProviderBase {
public:
    using ExternalDataProviderBase::ExternalDataProviderBase;

    ExternalDataProviderValidation validate_configuration() const override
    {
        if (definition_.provider.location.empty())
            return {false, "Text file path is required."};
        if (definition_.provider.text_field_path.empty())
            return {false, "Text field path is required."};
        return {};
    }

    void refresh() override
    {
        if (!active_)
            return;
        begin_update();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "FileProvider", [this]() {
                return std::string("reading source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " location=" + ExternalDataLog::safe_location(definition_.provider.location);
            });
        QFile file(qstr(definition_.provider.location));
        if (!file.open(QIODevice::ReadOnly)) {
            fail("Could not open text file: " + stdstr(file.errorString()));
            return;
        }
        if (file.size() > kMaxProviderPayloadBytes) {
            fail("Text file exceeds the 16 MiB provider payload limit.");
            return;
        }
        const QByteArray bytes = file.read(kMaxProviderPayloadBytes + 1);
        if (bytes.size() > kMaxProviderPayloadBytes) {
            fail("Text file exceeds the 16 MiB provider payload limit.");
            return;
        }
        std::map<std::string, ExternalDataValue> values;
        values[definition_.provider.text_field_path] = ExternalDataValue::string(
            std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
        ExternalDataTableSnapshot table;
        table.path = "$rows";
        table.columns.push_back(definition_.provider.text_field_path);
        ExternalDataTableRow row;
        row.key = "0";
        row.values[definition_.provider.text_field_path] =
            values[definition_.provider.text_field_path];
        row.source_field_paths[definition_.provider.text_field_path] =
            definition_.provider.text_field_path;
        table.rows.push_back(std::move(row));
        std::map<std::string, ExternalDataTableSnapshot> tables;
        tables[table.path] = std::move(table);
        submit_values(std::move(values), std::move(tables));
    }
};

class ManualTableProvider final : public ExternalDataProviderBase {
public:
    using ExternalDataProviderBase::ExternalDataProviderBase;

    ExternalDataProviderValidation validate_configuration() const override
    {
        for (const auto &entry : definition_.provider.manual_values) {
            const auto field = std::find_if(
                definition_.fields.begin(), definition_.fields.end(),
                [&entry](const ExternalDataFieldDefinition &candidate) {
                    return candidate.path == entry.first;
                });
            if (field != definition_.fields.end() &&
                entry.second.is_set && entry.second.type != field->type) {
                return {false, "Manual value type does not match field '" + entry.first + "'."};
            }
        }
        return {};
    }

    void refresh() override
    {
        if (!active_)
            return;
        begin_update();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "ManualProvider", [this]() {
                std::ostringstream stream;
                stream << "materializing source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " values=" << definition_.provider.manual_values.size();
                return stream.str();
            });
        if (definition_.provider.manual_values.empty()) {
            complete_without_values();
            return;
        }
        ExternalDataTableSnapshot table;
        table.path = "$rows";
        ExternalDataTableRow row;
        row.key = "0";
        for (const auto &entry : definition_.provider.manual_values) {
            table.columns.push_back(entry.first);
            row.values[entry.first] = entry.second;
            row.source_field_paths[entry.first] = entry.first;
        }
        table.rows.push_back(std::move(row));
        std::map<std::string, ExternalDataTableSnapshot> tables;
        tables[table.path] = std::move(table);
        submit_values(definition_.provider.manual_values, std::move(tables));
    }
};

class HttpJsonProvider final : public ExternalDataProviderBase {
public:
    explicit HttpJsonProvider(ExternalDataSourceDefinition definition)
        : ExternalDataProviderBase(std::move(definition))
    {
        network_ = new QNetworkAccessManager(this);
        timeout_timer_.setParent(this);
        retry_timer_.setParent(this);
        timeout_timer_.setSingleShot(true);
        retry_timer_.setSingleShot(true);
        QObject::connect(&timeout_timer_, &QTimer::timeout, this, [this]() {
            if (reply_) {
                timed_out_ = true;
                ExternalDataLog::write_lazy(
                    ExternalDataLogLevel::Warning, "HttpProvider", [this]() {
                        return std::string("timeout source=") +
                               ExternalDataLog::sanitize(definition_.id, 160) +
                               " timeoutMs=" + std::to_string(definition_.provider.timeout_ms);
                    });
                reply_->abort();
            }
        });
        QObject::connect(&retry_timer_, &QTimer::timeout, this, [this]() {
            issue_request();
        });
    }

    ExternalDataProviderValidation validate_configuration() const override
    {
        const QUrl url(qstr(definition_.provider.location));
        if (!url.isValid() || (url.scheme() != QStringLiteral("http") &&
                              url.scheme() != QStringLiteral("https")))
            return {false, "A valid HTTP or HTTPS URL is required."};
        return validate_json_paths(definition_);
    }

    void refresh() override
    {
        if (!active_)
            return;
        if (reply_) {
            refresh_pending_ = true;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Debug, "HttpProvider", [this]() {
                    return std::string("coalesced refresh while request active source=") +
                           ExternalDataLog::sanitize(definition_.id, 160);
                });
            return;
        }
        attempt_ = 0;
        begin_update();
        issue_request();
    }

protected:
    void on_disconnect() override
    {
        retry_timer_.stop();
        timeout_timer_.stop();
        refresh_pending_ = false;
        timed_out_ = false;
        response_too_large_ = false;
        if (reply_) {
            reply_->abort();
            reply_->deleteLater();
            reply_.clear();
        }
    }

    void on_reconfigured() override
    {
        on_disconnect();
        active_ = true;
    }

private:
    void issue_request()
    {
        if (!active_ || reply_)
            return;
        QNetworkRequest request{QUrl(qstr(definition_.provider.location))};
        request.setRawHeader("Accept", "application/json");
        for (const auto &header : definition_.provider.headers)
            request.setRawHeader(qstr(header.first).toUtf8(), qstr(header.second).toUtf8());
        if (!definition_.provider.authentication_token.empty() &&
            definition_.provider.headers.find("Authorization") == definition_.provider.headers.end()) {
            request.setRawHeader("Authorization",
                QByteArray("Bearer ") + qstr(definition_.provider.authentication_token).toUtf8());
        }
        timed_out_ = false;
        response_too_large_ = false;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "HttpProvider", [this]() {
                std::ostringstream stream;
                stream << "GET source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " url=" << ExternalDataLog::safe_location(definition_.provider.location)
                       << " attempt=" << attempt_
                       << " timeoutMs=" << definition_.provider.timeout_ms
                       << " headers=" << definition_.provider.headers.size()
                       << " tokenConfigured="
                       << (!definition_.provider.authentication_token.empty() ? 1 : 0);
                return stream.str();
            });
        reply_ = network_->get(request);
        timeout_timer_.start(std::clamp(definition_.provider.timeout_ms, 250, 300000));
        QPointer<QNetworkReply> guarded = reply_;
        QObject::connect(reply_, &QNetworkReply::downloadProgress, this,
                         [this, guarded](qint64 received, qint64 total) {
            if (!guarded || guarded != reply_)
                return;
            if (received > kMaxProviderPayloadBytes ||
                total > kMaxProviderPayloadBytes) {
                response_too_large_ = true;
                guarded->abort();
            }
        });
        QObject::connect(reply_, &QNetworkReply::finished, this, [this, guarded]() {
            if (!guarded || guarded != reply_)
                return;
            timeout_timer_.stop();
            QNetworkReply *finished = reply_;
            reply_.clear();
            const bool timed_out = timed_out_;
            const bool response_too_large = response_too_large_;
            timed_out_ = false;
            response_too_large_ = false;
            const QByteArray payload = response_too_large
                ? QByteArray() : finished->read(kMaxProviderPayloadBytes + 1);
            const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QNetworkReply::NetworkError network_error = finished->error();
            const QString network_error_text = finished->errorString();
            finished->deleteLater();
            const qsizetype payload_size = payload.size();
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Debug, "HttpProvider", [this, status, network_error,
                                                                payload_size, timed_out,
                                                                response_too_large]() {
                    std::ostringstream stream;
                    stream << "response source=" << ExternalDataLog::sanitize(definition_.id, 160)
                           << " status=" << status
                           << " networkError=" << static_cast<int>(network_error)
                           << " bytes=" << payload_size
                           << " timedOut=" << (timed_out ? 1 : 0)
                           << " tooLarge=" << (response_too_large ? 1 : 0);
                    return stream.str();
                });

            if (!active_)
                return;
            if (response_too_large || payload.size() > kMaxProviderPayloadBytes) {
                retry_or_fail("HTTP response exceeds the 16 MiB provider payload limit.");
                return;
            }
            if (timed_out) {
                retry_or_fail("HTTP request timed out.");
                return;
            }
            if (network_error != QNetworkReply::NoError || status >= 400) {
                const std::string message = status >= 400
                    ? "HTTP endpoint returned status " + std::to_string(status) + "."
                    : "HTTP request failed: " + stdstr(network_error_text);
                retry_or_fail(message);
                return;
            }

            std::map<std::string, ExternalDataValue> values;
            std::map<std::string, ExternalDataTableSnapshot> tables;
            std::string error;
            if (!extract_json_values(definition_, payload, values, tables, error)) {
                retry_or_fail(error);
                return;
            }
            attempt_ = 0;
            submit_values(std::move(values), std::move(tables));
            if (refresh_pending_) {
                refresh_pending_ = false;
                QTimer::singleShot(std::max(0, definition_.provider.rate_limit_ms), this,
                                   [this]() { refresh(); });
            }
        });
    }

    void retry_or_fail(const std::string &message)
    {
        if (attempt_ < std::max(0, definition_.provider.retry_count)) {
            const int multiplier = 1 << std::min(attempt_, 10);
            const int delay = std::min(300000,
                std::max(50, definition_.provider.retry_backoff_ms) * multiplier);
            ++attempt_;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "HttpProvider", [this, delay, &message]() {
                    std::ostringstream stream;
                    stream << "retry scheduled source=" << ExternalDataLog::sanitize(definition_.id, 160)
                           << " attempt=" << attempt_
                           << " delayMs=" << delay
                           << " reason=\"" << ExternalDataLog::sanitize(message, 512) << "\"";
                    return stream.str();
                });
            retry_timer_.start(delay);
            return;
        }
        fail(message);
        attempt_ = 0;
        if (refresh_pending_) {
            refresh_pending_ = false;
            QTimer::singleShot(std::max(0, definition_.provider.rate_limit_ms), this,
                               [this]() { refresh(); });
        }
    }

    QNetworkAccessManager *network_ = nullptr;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_timer_;
    QTimer retry_timer_;
    int attempt_ = 0;
    bool refresh_pending_ = false;
    bool timed_out_ = false;
    bool response_too_large_ = false;
};

class WebSocketProvider final : public ExternalDataProviderBase {
public:
    explicit WebSocketProvider(ExternalDataSourceDefinition definition)
        : ExternalDataProviderBase(std::move(definition))
    {
        socket_ = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        reconnect_timer_.setParent(this);
        reconnect_timer_.setSingleShot(true);
        QObject::connect(&reconnect_timer_, &QTimer::timeout, this, [this]() { open_socket(); });
        QObject::connect(socket_, &QWebSocket::connected, this, [this]() {
            reconnect_delay_ms_ = std::max(100, definition_.provider.reconnect_initial_ms);
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Info, "WebSocket", [this]() {
                    return std::string("connected source=") +
                           ExternalDataLog::sanitize(definition_.id, 160) +
                           " url=" + ExternalDataLog::safe_location(definition_.provider.location);
                });
            complete_without_values();
        });
        QObject::connect(socket_, &QWebSocket::disconnected, this, [this]() {
            if (!active_)
                return;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "WebSocket", [this]() {
                    return std::string("disconnected source=") +
                           ExternalDataLog::sanitize(definition_.id, 160) +
                           " socketError=\"" +
                           ExternalDataLog::sanitize(stdstr(socket_->errorString()), 512) + "\"";
                });
            /* Preserve a concrete socket/parser error long enough for the UI
             * to report it. A clean remote close is represented as
             * Disconnected; both paths retain the last value when configured. */
            if (state() != ExternalDataConnectionState::Error) {
                set_state(ExternalDataConnectionState::Disconnected,
                          "WebSocket disconnected; reconnecting.");
            }
            schedule_reconnect();
        });
        QObject::connect(socket_, &QWebSocket::textMessageReceived, this,
                         [this](const QString &message) {
                             handle_message(message.toUtf8());
                         });
        QObject::connect(socket_, &QWebSocket::binaryMessageReceived, this,
                         [this](const QByteArray &message) { handle_message(message); });
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        QObject::connect(socket_, &QWebSocket::errorOccurred, this,
                         [this](QAbstractSocket::SocketError) { handle_socket_error(); });
#else
        QObject::connect(socket_,
                         QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
                         this, [this](QAbstractSocket::SocketError) { handle_socket_error(); });
#endif
    }

    ExternalDataProviderValidation validate_configuration() const override
    {
        const QUrl url(qstr(definition_.provider.location));
        if (!url.isValid() || (url.scheme() != QStringLiteral("ws") &&
                              url.scheme() != QStringLiteral("wss")))
            return {false, "A valid ws:// or wss:// URL is required."};
        return validate_json_paths(definition_);
    }

    void connect_provider() override
    {
        active_ = true;
        const ExternalDataProviderValidation result = validate();
        if (!result.valid) {
            fail(result.error_message);
            return;
        }
        configure_timers();
        reconnect_delay_ms_ = std::max(100, definition_.provider.reconnect_initial_ms);
        open_socket();
    }

    void refresh() override
    {
        if (!active_)
            return;
        if (socket_->state() == QAbstractSocket::ConnectedState) {
            socket_->ping();
        } else if (!reconnect_timer_.isActive()) {
            open_socket();
        }
    }

protected:
    void on_disconnect() override
    {
        reconnect_timer_.stop();
        socket_->close();
    }

    void on_reconfigured() override
    {
        reconnect_timer_.stop();
        socket_->abort();
        reconnect_delay_ms_ = std::max(100, definition_.provider.reconnect_initial_ms);
        open_socket();
    }

private:
    void open_socket()
    {
        if (!active_)
            return;
        if (socket_->state() == QAbstractSocket::ConnectedState ||
            socket_->state() == QAbstractSocket::ConnectingState)
            return;
        set_state(ExternalDataConnectionState::Connecting, {});
        QNetworkRequest request{QUrl(qstr(definition_.provider.location))};
        for (const auto &header : definition_.provider.headers)
            request.setRawHeader(qstr(header.first).toUtf8(), qstr(header.second).toUtf8());
        if (!definition_.provider.authentication_token.empty() &&
            definition_.provider.headers.find("Authorization") == definition_.provider.headers.end()) {
            request.setRawHeader("Authorization",
                QByteArray("Bearer ") + qstr(definition_.provider.authentication_token).toUtf8());
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "WebSocket", [this]() {
                std::ostringstream stream;
                stream << "opening source=" << ExternalDataLog::sanitize(definition_.id, 160)
                       << " url=" << ExternalDataLog::safe_location(definition_.provider.location)
                       << " headers=" << definition_.provider.headers.size()
                       << " tokenConfigured="
                       << (!definition_.provider.authentication_token.empty() ? 1 : 0);
                return stream.str();
            });
        socket_->open(request);
    }

    void handle_message(const QByteArray &payload)
    {
        if (!active_)
            return;
        if (payload.size() > kMaxProviderPayloadBytes) {
            fail("WebSocket message exceeds the 16 MiB provider payload limit.");
            return;
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "WebSocket", [this, &payload]() {
                return std::string("message source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " bytes=" + std::to_string(payload.size());
            });
        begin_update();
        std::map<std::string, ExternalDataValue> values;
        std::map<std::string, ExternalDataTableSnapshot> tables;
        std::string error;
        if (!extract_json_values(definition_, payload, values, tables, error)) {
            fail(error);
            return;
        }
        submit_values(std::move(values), std::move(tables));
    }

    void handle_socket_error()
    {
        if (!active_)
            return;
        fail("WebSocket error: " + stdstr(socket_->errorString()));
        schedule_reconnect();
    }

    void schedule_reconnect()
    {
        if (!active_ || reconnect_timer_.isActive())
            return;
        const int initial = std::max(100, definition_.provider.reconnect_initial_ms);
        const int maximum = std::max(initial, definition_.provider.reconnect_max_ms);
        if (reconnect_delay_ms_ <= 0)
            reconnect_delay_ms_ = initial;
        const int scheduled_delay = reconnect_delay_ms_;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Warning, "WebSocket", [this, scheduled_delay]() {
                return std::string("reconnect scheduled source=") +
                       ExternalDataLog::sanitize(definition_.id, 160) +
                       " delayMs=" + std::to_string(scheduled_delay);
            });
        reconnect_timer_.start(scheduled_delay);
        reconnect_delay_ms_ = std::min(maximum, reconnect_delay_ms_ * 2);
    }

    QWebSocket *socket_ = nullptr;
    QTimer reconnect_timer_;
    int reconnect_delay_ms_ = 1000;
};

static std::unique_ptr<IExternalDataProvider> make_provider(
    const ExternalDataSourceDefinition &definition)
{
    switch (definition.provider.type) {
    case ExternalDataProviderType::JsonFile:
        return std::make_unique<JsonFileProvider>(definition);
    case ExternalDataProviderType::CsvFile:
        return std::make_unique<CsvFileProvider>(definition);
    case ExternalDataProviderType::HttpJson:
        return std::make_unique<HttpJsonProvider>(definition);
    case ExternalDataProviderType::WebSocket:
        return std::make_unique<WebSocketProvider>(definition);
    case ExternalDataProviderType::LocalTextFile:
        return std::make_unique<LocalTextFileProvider>(definition);
    case ExternalDataProviderType::ManualTable:
        return std::make_unique<ManualTableProvider>(definition);
    case ExternalDataProviderType::None:
    default:
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Warning, "ProviderController", [&]() {
                return std::string("no provider factory source=") +
                       ExternalDataLog::sanitize(definition.id, 160) +
                       " type=" + ExternalDataLog::provider_type_name(definition.provider.type);
            });
        return {};
    }
}

static ExternalDataSourceDefinition merge_definition(
    ExternalDataSourceDefinition base, const ExternalDataSourceDefinition &incoming)
{
    if (!incoming.name.empty())
        base.name = incoming.name;
    if (incoming.provider.type != ExternalDataProviderType::None)
        base.provider = incoming.provider;
    for (const auto &field : incoming.fields) {
        auto existing = std::find_if(base.fields.begin(), base.fields.end(),
                                     [&field](const ExternalDataFieldDefinition &candidate) {
                                         return candidate.path == field.path;
                                     });
        if (existing == base.fields.end())
            base.fields.push_back(field);
        else
            *existing = field;
    }
    return base;
}

class ProviderController final : public QObject {
public:
    void synchronize(std::vector<ExternalDataSourceDefinition> definitions)
    {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "ProviderController", [&]() {
                return std::string("synchronize requested definitions=") +
                       std::to_string(definitions.size()) +
                       " existingProviders=" + std::to_string(providers_.size());
            });
        std::map<std::string, ExternalDataSourceDefinition> merged;
        for (const auto &definition : definitions) {
            if (definition.id.empty())
                continue;
            auto found = merged.find(definition.id);
            if (found == merged.end())
                merged.emplace(definition.id, definition);
            else
                found->second = merge_definition(std::move(found->second), definition);
        }

        /* Track definitions even when their provider type is None. Concrete
         * providers must be disconnected before manager removal, because the
         * disconnect state publication intentionally touches the manager. */
        std::set<std::string> removed_source_ids;
        for (const std::string &known_id : known_source_ids_) {
            if (merged.find(known_id) == merged.end())
                removed_source_ids.insert(known_id);
        }

        for (auto it = providers_.begin(); it != providers_.end();) {
            if (merged.find(it->first) == merged.end()) {
                it->second->disconnect_provider();
                it = providers_.erase(it);
            } else {
                ++it;
            }
        }
        for (const std::string &removed_id : removed_source_ids)
            ExternalDataManager::instance().unregister_source(removed_id);

        known_source_ids_.clear();
        for (const auto &entry : merged)
            known_source_ids_.insert(entry.first);

        for (const auto &entry : merged) {
            const ExternalDataSourceDefinition &definition = entry.second;
            ExternalDataManager::instance().synchronize_source_definition(definition);
            auto existing = providers_.find(entry.first);
            if (definition.provider.type == ExternalDataProviderType::None) {
                if (existing != providers_.end()) {
                    existing->second->disconnect_provider();
                    providers_.erase(existing);
                }
                continue;
            }
            bool created = false;
            if (existing == providers_.end() ||
                existing->second->provider_type() != definition.provider.type) {
                if (existing != providers_.end()) {
                    existing->second->disconnect_provider();
                    providers_.erase(existing);
                }
                auto provider = make_provider(definition);
                if (!provider)
                    continue;
                existing = providers_.emplace(entry.first, std::move(provider)).first;
                created = true;
            } else {
                existing->second->reconfigure(definition);
            }
            if (!definition.provider.enabled) {
                existing->second->disconnect_provider();
            } else if (created ||
                       existing->second->state() == ExternalDataConnectionState::Disconnected ||
                       existing->second->state() == ExternalDataConnectionState::Error) {
                existing->second->connect_provider();
            }
        }
    }

    void configure_source(const ExternalDataSourceDefinition &definition)
    {
        if (definition.id.empty())
            return;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "ProviderController", [&]() {
                return std::string("configure source=") +
                       ExternalDataLog::sanitize(definition.id, 160) +
                       " type=" + ExternalDataLog::provider_type_name(definition.provider.type) +
                       " enabled=" + (definition.provider.enabled ? "1" : "0");
            });
        known_source_ids_.insert(definition.id);
        ExternalDataManager::instance().register_source(definition);
        auto existing = providers_.find(definition.id);
        if (definition.provider.type == ExternalDataProviderType::None) {
            if (existing != providers_.end()) {
                existing->second->disconnect_provider();
                providers_.erase(existing);
            }
            return;
        }
        if (existing == providers_.end() ||
            existing->second->provider_type() != definition.provider.type) {
            if (existing != providers_.end()) {
                existing->second->disconnect_provider();
                providers_.erase(existing);
            }
            auto provider = make_provider(definition);
            if (!provider)
                return;
            existing = providers_.emplace(definition.id, std::move(provider)).first;
        } else {
            existing->second->reconfigure(definition);
        }
        if (!definition.provider.enabled)
            existing->second->disconnect_provider();
    }

    void connect_source(const std::string &source_id)
    {
        auto found = providers_.find(source_id);
        if (found != providers_.end()) {
            found->second->connect_provider();
        } else {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "ProviderController", [&]() {
                    return std::string("connect ignored; provider missing source=") +
                           ExternalDataLog::sanitize(source_id, 160);
                });
        }
    }

    void disconnect_source(const std::string &source_id)
    {
        auto found = providers_.find(source_id);
        if (found != providers_.end()) {
            found->second->disconnect_provider();
        } else {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "ProviderController", [&]() {
                    return std::string("disconnect ignored; provider missing source=") +
                           ExternalDataLog::sanitize(source_id, 160);
                });
        }
    }

    void refresh_source(const std::string &source_id)
    {
        auto found = providers_.find(source_id);
        if (found != providers_.end()) {
            found->second->refresh();
        } else {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "ProviderController", [&]() {
                    return std::string("refresh ignored; provider missing source=") +
                           ExternalDataLog::sanitize(source_id, 160);
                });
        }
    }

    void refresh_all()
    {
        for (auto &entry : providers_)
            entry.second->refresh();
    }

    void stop_all()
    {
        for (auto &entry : providers_)
            entry.second->disconnect_provider();
        providers_.clear();
        for (const std::string &source_id : known_source_ids_)
            ExternalDataManager::instance().unregister_source(source_id);
        known_source_ids_.clear();
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IExternalDataProvider>> providers_;
    std::set<std::string> known_source_ids_;
};

} // namespace

class ExternalDataProviderService::Impl {
public:
    Impl()
    {
        controller = new ProviderController();
        controller->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, controller, &QObject::deleteLater);
        thread.setObjectName(QStringLiteral("FXM External Data Providers"));
        thread.start();
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "ProviderService", []() {
                return std::string("worker thread started");
            });
    }

    ~Impl()
    {
        shutdown();
    }

    template<typename Fn>
    void post(Fn fn)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopped || !controller) {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Warning, "ProviderService", []() {
                    return std::string("queued operation dropped because service is stopped");
                });
            return;
        }
        ProviderController *target = controller;
        QMetaObject::invokeMethod(
            target,
            [target, fn = std::move(fn)]() mutable { fn(target); },
            Qt::QueuedConnection);
    }

    void shutdown()
    {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "ProviderService", []() {
                return std::string("shutdown requested");
            });
        ProviderController *target = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopped)
                return;
            stopped = true;
            target = controller;
        }
        if (target && thread.isRunning()) {
            QMetaObject::invokeMethod(target, [target]() {
                target->stop_all();
            }, Qt::BlockingQueuedConnection);
        }
        thread.quit();
        thread.wait();
        {
            std::lock_guard<std::mutex> lock(mutex);
            controller = nullptr;
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "ProviderService", []() {
                return std::string("worker thread stopped");
            });
    }

    QThread thread;
    ProviderController *controller = nullptr;
    std::mutex mutex;
    bool stopped = false;
};

ExternalDataProviderService &ExternalDataProviderService::instance()
{
    static ExternalDataProviderService service;
    return service;
}

ExternalDataProviderService::ExternalDataProviderService()
    : impl_(std::make_unique<Impl>())
{
}

ExternalDataProviderService::~ExternalDataProviderService() = default;

void ExternalDataProviderService::synchronize(
    const std::vector<ExternalDataSourceDefinition> &definitions)
{
    if (!impl_)
        return;
    impl_->post([definitions](ProviderController *controller) mutable {
        controller->synchronize(std::move(definitions));
    });
}

void ExternalDataProviderService::configure_source(
    const ExternalDataSourceDefinition &definition)
{
    if (!impl_)
        return;
    impl_->post([definition](ProviderController *controller) {
        controller->configure_source(definition);
    });
}

void ExternalDataProviderService::connect_source(const std::string &source_id)
{
    if (!impl_)
        return;
    impl_->post([source_id](ProviderController *controller) {
        controller->connect_source(source_id);
    });
}

void ExternalDataProviderService::disconnect_source(const std::string &source_id)
{
    if (!impl_)
        return;
    impl_->post([source_id](ProviderController *controller) {
        controller->disconnect_source(source_id);
    });
}

void ExternalDataProviderService::refresh_source(const std::string &source_id)
{
    if (!impl_)
        return;
    impl_->post([source_id](ProviderController *controller) {
        controller->refresh_source(source_id);
    });
}

void ExternalDataProviderService::refresh_all()
{
    if (!impl_)
        return;
    impl_->post([](ProviderController *controller) {
        controller->refresh_all();
    });
}

void ExternalDataProviderService::shutdown()
{
    if (impl_)
        impl_->shutdown();
}

const char *external_data_provider_type_name(ExternalDataProviderType type)
{
    switch (type) {
    case ExternalDataProviderType::JsonFile: return "JSON file";
    case ExternalDataProviderType::CsvFile: return "CSV file";
    case ExternalDataProviderType::HttpJson: return "HTTP/HTTPS JSON";
    case ExternalDataProviderType::WebSocket: return "WebSocket";
    case ExternalDataProviderType::LocalTextFile: return "Local text file";
    case ExternalDataProviderType::ManualTable: return "Manual/internal table";
    case ExternalDataProviderType::None:
    default: return "None";
    }
}

const char *external_data_connection_state_name(ExternalDataConnectionState state)
{
    switch (state) {
    case ExternalDataConnectionState::Connecting: return "Connecting";
    case ExternalDataConnectionState::Connected: return "Connected";
    case ExternalDataConnectionState::Updating: return "Updating";
    case ExternalDataConnectionState::Error: return "Error";
    case ExternalDataConnectionState::Stale: return "Stale";
    case ExternalDataConnectionState::Disconnected:
    default: return "Disconnected";
    }
}
