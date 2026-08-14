#include "external-data.h"
#include "external-data-log.h"
#include "performance-counters.h"

#include "title-data.h"
#include "layer-model.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

static std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static std::string upper_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}


static std::string title_ascii(std::string value)
{
    bool word_start = true;
    for (char &raw : value) {
        const unsigned char ch = static_cast<unsigned char>(raw);
        if (std::isalnum(ch)) {
            raw = static_cast<char>(word_start ? std::toupper(ch) : std::tolower(ch));
            word_start = false;
        } else {
            word_start = true;
        }
    }
    return value;
}

static std::string add_thousands_separators(std::string value)
{
    const size_t exponent = value.find_first_of("eE");
    const std::string exponent_part = exponent == std::string::npos ? std::string{} : value.substr(exponent);
    if (exponent != std::string::npos)
        value.resize(exponent);
    const size_t decimal = value.find('.');
    const size_t integer_end = decimal == std::string::npos ? value.size() : decimal;
    const size_t integer_start = !value.empty() && (value.front() == '-' || value.front() == '+') ? 1 : 0;
    if (integer_end > integer_start + 3) {
        size_t pos = integer_end;
        while (pos > integer_start + 3) {
            pos -= 3;
            value.insert(pos, 1, ',');
        }
    }
    return value + exponent_part;
}

static bool utc_tm_from_value(const ExternalDataValue &value, std::tm &tm)
{
    std::time_t timestamp = 0;
    bool has_timestamp = false;
    if (value.type == ExternalDataType::Integer) {
        int64_t raw = value.integer_value;
        if (std::llabs(raw) > 100000000000LL)
            raw /= 1000;
        timestamp = static_cast<std::time_t>(raw);
        has_timestamp = true;
    } else if (value.type == ExternalDataType::Float && std::isfinite(value.float_value)) {
        double raw = value.float_value;
        if (std::abs(raw) > 100000000000.0)
            raw /= 1000.0;
        timestamp = static_cast<std::time_t>(raw);
        has_timestamp = true;
    }
    if (has_timestamp) {
#if defined(_WIN32)
        return gmtime_s(&tm, &timestamp) == 0;
#else
        return gmtime_r(&timestamp, &tm) != nullptr;
#endif
    }

    std::string text = value.string_value;
    if (!text.empty() && (text.back() == 'Z' || text.back() == 'z'))
        text.pop_back();
    const char *patterns[] = {"%Y-%m-%dT%H:%M:%S", "%Y-%m-%d %H:%M:%S",
                              "%Y-%m-%dT%H:%M", "%Y-%m-%d %H:%M", "%Y-%m-%d"};
    for (const char *pattern : patterns) {
        std::tm candidate{};
        std::istringstream input(text);
        input >> std::get_time(&candidate, pattern);
        if (!input.fail()) {
            tm = candidate;
            return true;
        }
    }
    return false;
}

static bool formatter_config_is_default(const ExternalDataFormatterConfig &formatter)
{
    return formatter.prefix.empty() && formatter.suffix.empty() &&
           !formatter.number_format_enabled && formatter.decimal_places < 0 &&
           !formatter.thousands_separator &&
           formatter.text_case == ExternalDataTextCase::None &&
           formatter.date_time_format.empty() &&
           formatter.conditional_replacements.empty() &&
           formatter.empty_value_mode == ExternalDataEmptyValueMode::KeepEmpty &&
           formatter.empty_replacement.empty();
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

static void replace_all(std::string &value, const std::string &needle,
                        const std::string &replacement)
{
    if (needle.empty())
        return;
    size_t position = 0;
    while ((position = value.find(needle, position)) != std::string::npos) {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

static const ExternalDataFieldDefinition *field_definition_for(
    const ExternalDataSourceState &source, const std::string &field_path)
{
    const auto field = source.fields.find(field_path);
    return field == source.fields.end() ? nullptr : &field->second.definition;
}

static ExternalDataValue authored_string_value(const std::string &value)
{
    return ExternalDataValue::string(value);
}

static std::string external_live_text_row_id(const Title &title, int row)
{
    if (row < 0 || row >= static_cast<int>(title.live_text_rows.size()))
        return {};
    if (row < static_cast<int>(title.live_text_row_ids.size()) &&
        !title.live_text_row_ids[static_cast<size_t>(row)].empty())
        return title.live_text_row_ids[static_cast<size_t>(row)];
    return std::string("legacy-row-") + std::to_string(row);
}

static bool state_allows_current_value(
    const ExternalDataSourceDefinition &definition,
    ExternalDataConnectionState state)
{
    if (state == ExternalDataConnectionState::Connected ||
        state == ExternalDataConnectionState::Updating)
        return true;
    return definition.provider.type != ExternalDataProviderType::None &&
           definition.provider.keep_last_value &&
           (state == ExternalDataConnectionState::Stale ||
            state == ExternalDataConnectionState::Error ||
            state == ExternalDataConnectionState::Disconnected ||
            state == ExternalDataConnectionState::Connecting);
}

static std::string render_update_key(const ExternalDataUpdate &update)
{
    std::string suffix;
    if (update.connection_state_changed)
        suffix = "@state";
    else if (update.table_changed)
        suffix = "@table:" + update.table_path;
    else
        suffix = update.field_path;
    return std::to_string(update.source_id.size()) + ":" + update.source_id +
           ":" + suffix;
}

static bool table_row_equal(const ExternalDataTableRow &a,
                            const ExternalDataTableRow &b)
{
    return a.key == b.key && a.values == b.values &&
           a.source_field_paths == b.source_field_paths;
}

static bool table_snapshot_equal(const ExternalDataTableSnapshot &a,
                                 const ExternalDataTableSnapshot &b)
{
    if (a.path != b.path || a.columns != b.columns ||
        a.rows.size() != b.rows.size())
        return false;
    for (size_t i = 0; i < a.rows.size(); ++i) {
        if (!table_row_equal(a.rows[i], b.rows[i]))
            return false;
    }
    return true;
}

static uint64_t fnv1a64(const std::string &value)
{
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : value) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

static std::string table_managed_row_prefix(const std::string &binding_id)
{
    return std::string("external-table:") + binding_id + ":";
}

static std::string table_managed_row_id(const std::string &binding_id,
                                        const std::string &key)
{
    std::ostringstream out;
    out << table_managed_row_prefix(binding_id) << std::hex << fnv1a64(key);
    return out.str();
}

static bool starts_with(const std::string &value, const std::string &prefix)
{
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

static void ensure_table_row_ids(Title &title)
{
    if (title.live_text_row_ids.size() > title.live_text_rows.size())
        title.live_text_row_ids.resize(title.live_text_rows.size());
    std::set<std::string> used;
    for (size_t i = 0; i < title.live_text_rows.size(); ++i) {
        if (i >= title.live_text_row_ids.size())
            title.live_text_row_ids.push_back({});
        std::string &id = title.live_text_row_ids[i];
        if (id.empty() || used.find(id) != used.end()) {
            std::ostringstream seed;
            seed << title.id << ':' << i;
            for (const auto &cell : title.live_text_rows[i])
                seed << ':' << cell;
            std::ostringstream generated;
            generated << "runtime-row:" << std::hex << fnv1a64(seed.str());
            id = generated.str();
            unsigned collision = 1;
            while (used.find(id) != used.end()) {
                std::ostringstream retry;
                retry << generated.str() << ':' << collision++;
                id = retry.str();
            }
        }
        used.insert(id);
    }
}

} // namespace

ExternalDataManager &ExternalDataManager::instance()
{
    static ExternalDataManager manager;
    return manager;
}

int64_t ExternalDataManager::now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

ExternalDataSourceState &ExternalDataManager::ensure_source_locked(
    const std::string &source_id)
{
    auto [it, inserted] = sources_.try_emplace(source_id);
    if (inserted) {
        it->second.definition.id = source_id;
        it->second.definition.name = source_id;
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "Manager", [&]() {
                return std::string("created runtime source id=") +
                       ExternalDataLog::sanitize(source_id, 160);
            });
    }
    return it->second;
}

void ExternalDataManager::register_source(
    const ExternalDataSourceDefinition &definition)
{
    if (definition.id.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    ExternalDataSourceState &state = ensure_source_locked(definition.id);
    state.definition.id = definition.id;
    if (!definition.name.empty())
        state.definition.name = definition.name;
    state.definition.provider = definition.provider;

    /* Multiple titles and future providers may contribute fields to the same
     * central source. Merge by stable path instead of replacing another
     * title's schema, while preserving runtime values already received. */
    for (const auto &field_definition : definition.fields) {
        if (field_definition.path.empty())
            continue;
        ExternalDataFieldState &field = state.fields[field_definition.path];
        if (field.has_current_value &&
            field.current_value.type != field_definition.type) {
            field.current_value = {};
            field.has_current_value = false;
        }
        field.definition = field_definition;
        field.authored_definition = true;
        auto authored = std::find_if(
            state.definition.fields.begin(), state.definition.fields.end(),
            [&field_definition](const ExternalDataFieldDefinition &candidate) {
                return candidate.path == field_definition.path;
            });
        if (authored == state.definition.fields.end())
            state.definition.fields.push_back(field_definition);
        else
            *authored = field_definition;
    }
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Manager", [&]() {
            std::ostringstream stream;
            stream << "registered source id=" << ExternalDataLog::sanitize(definition.id, 160)
                   << " provider=" << ExternalDataLog::provider_type_name(definition.provider.type)
                   << " authoredFields=" << definition.fields.size()
                   << " runtimeFields=" << state.fields.size()
                   << " refresh=" << ExternalDataLog::refresh_mode_name(definition.provider.refresh_mode);
            return stream.str();
        });
}

void ExternalDataManager::synchronize_source_definition(
    const ExternalDataSourceDefinition &definition)
{
    if (definition.id.empty())
        return;

    std::lock_guard<std::mutex> lock(mutex_);
    ExternalDataSourceState &state = ensure_source_locked(definition.id);
    state.definition.id = definition.id;
    state.definition.name = definition.name.empty() ? definition.id : definition.name;
    state.definition.provider = definition.provider;

    std::set<std::string> authored_paths;
    for (const auto &field_definition : definition.fields) {
        if (!field_definition.path.empty())
            authored_paths.insert(field_definition.path);
    }

    /* Fields absent from the complete authored snapshot remain discoverable,
     * but stop enforcing a stale alias/type override. Current provider values
     * supply the best inferred scalar type until the next refresh. */
    for (auto &entry : state.fields) {
        ExternalDataFieldState &field = entry.second;
        if (authored_paths.find(entry.first) != authored_paths.end())
            continue;
        field.authored_definition = false;
        field.definition.path = entry.first;
        field.definition.name = entry.first;
        field.definition.type = field.has_current_value
            ? field.current_value.type : ExternalDataType::String;
        field.definition.has_default_value = false;
        field.definition.default_value = {};
    }

    for (const auto &field_definition : definition.fields) {
        if (field_definition.path.empty())
            continue;
        ExternalDataFieldState &field = state.fields[field_definition.path];
        if (field.has_current_value &&
            field.current_value.type != field_definition.type) {
            field.current_value = {};
            field.has_current_value = false;
        }
        field.definition = field_definition;
        field.authored_definition = true;
    }

    state.definition.fields.clear();
    state.definition.fields.reserve(state.fields.size());
    for (const auto &entry : state.fields)
        state.definition.fields.push_back(entry.second.definition);
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Manager", [&]() {
            std::ostringstream stream;
            stream << "synchronized source definition id="
                   << ExternalDataLog::sanitize(definition.id, 160)
                   << " provider=" << ExternalDataLog::provider_type_name(definition.provider.type)
                   << " authoredFields=" << definition.fields.size()
                   << " availableFields=" << state.fields.size();
            return stream.str();
        });
}

void ExternalDataManager::register_title_sources(const Title &title)
{
    for (const auto &source : title.external_data_sources)
        register_source(source);
}

void ExternalDataManager::unregister_source(const std::string &source_id)
{
    if (source_id.empty())
        return;
    bool removed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        removed = sources_.erase(source_id) != 0;
    }
    if (removed) {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Info, "Manager", [&]() {
                return std::string("unregistered source id=") +
                       ExternalDataLog::sanitize(source_id, 160);
            });
    }
}

bool ExternalDataManager::set_connection_state(
    const std::string &source_id, ExternalDataConnectionState state,
    const std::string &error_message, int64_t timestamp_ms)
{
    if (source_id.empty())
        return false;
    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();

    bool affects_effective_values = false;
    ExternalDataConnectionState previous_state = ExternalDataConnectionState::Disconnected;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ExternalDataSourceState &source = ensure_source_locked(source_id);
        previous_state = source.connection_state;
        if (source.connection_state == state &&
            source.error_message == error_message) {
            source.last_update_timestamp_ms = timestamp_ms;
            return false;
        }
        const bool old_allows_current =
            state_allows_current_value(source.definition, source.connection_state);
        const bool new_allows_current =
            state_allows_current_value(source.definition, state);
        const bool has_current_value = std::any_of(
            source.fields.begin(), source.fields.end(),
            [](const auto &entry) { return entry.second.has_current_value; });
        affects_effective_values =
            has_current_value && old_allows_current != new_allows_current;
        source.connection_state = state;
        source.error_message = error_message;
        source.last_update_timestamp_ms = timestamp_ms;
    }

    const ExternalDataLogLevel state_level =
        state == ExternalDataConnectionState::Error
            ? ExternalDataLogLevel::Error
            : (state == ExternalDataConnectionState::Stale
                   ? ExternalDataLogLevel::Warning
                   : ((state == ExternalDataConnectionState::Connected ||
                       state == ExternalDataConnectionState::Disconnected)
                          ? ExternalDataLogLevel::Info
                          : ExternalDataLogLevel::Debug));
    ExternalDataLog::write_lazy(state_level, "State", [&]() {
        std::ostringstream stream;
        stream << "source=" << ExternalDataLog::sanitize(source_id, 160)
               << " transition=" << ExternalDataLog::connection_state_name(previous_state)
               << "->" << ExternalDataLog::connection_state_name(state)
               << " affectsEffective=" << (affects_effective_values ? 1 : 0);
        if (!error_message.empty())
            stream << " error=\"" << ExternalDataLog::sanitize(error_message, 512) << "\"";
        return stream.str();
    });

    /* Informative provider states remain queryable by the UI, but render/editor
     * observers are woken only when the state can change the effective value.
     * Connected <-> Updating and last-known-value state transitions therefore
     * do not continuously dirty previews or caches during unchanged polling. */
    if (affects_effective_values) {
        ExternalDataUpdate update;
        update.source_id = source_id;
        update.connection_state_changed = true;
        update.timestamp_ms = timestamp_ms;
        publish_change(std::move(update));
    }
    return true;
}

bool ExternalDataManager::update_value(
    const std::string &source_id, const std::string &field_path,
    const ExternalDataValue &value, int64_t timestamp_ms)
{
    if (source_id.empty() || field_path.empty() || !value.is_set ||
        (value.type == ExternalDataType::Float &&
         !std::isfinite(value.float_value))) {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Warning, "Value", [&]() {
                return std::string("rejected invalid update source=") +
                       ExternalDataLog::sanitize(source_id, 160) +
                       " field=" + ExternalDataLog::sanitize(field_path, 320) +
                       " " + ExternalDataLog::value_summary(value);
            });
        return false;
    }
    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ExternalDataSourceState &source = ensure_source_locked(source_id);
        ExternalDataFieldState &field = source.fields[field_path];
        if (field.definition.path.empty()) {
            field.definition.path = field_path;
            field.definition.name = field_path;
            field.definition.type = value.type;
            field.authored_definition = false;
            source.definition.fields.push_back(field.definition);
        } else if (field.definition.type != value.type) {
            if (field.authored_definition) {
                /* A provider cannot silently change an authored override's type. */
                ExternalDataLog::write_lazy(
                    ExternalDataLogLevel::Warning, "Value", [&]() {
                        std::ostringstream stream;
                        stream << "rejected type change source="
                               << ExternalDataLog::sanitize(source_id, 160)
                               << " field=" << ExternalDataLog::sanitize(field_path, 320)
                               << " authoredType=" << static_cast<int>(field.definition.type)
                               << " providerType=" << static_cast<int>(value.type);
                        return stream.str();
                    });
                return false;
            }
            /* Unpinned discovered fields follow the provider's latest inferred
             * scalar type. Once bound/pinned, register_source marks the schema
             * authored and type conversion becomes stable. */
            field.definition.type = value.type;
            auto runtime_definition = std::find_if(
                source.definition.fields.begin(), source.definition.fields.end(),
                [&field_path](const ExternalDataFieldDefinition &candidate) {
                    return candidate.path == field_path;
                });
            if (runtime_definition != source.definition.fields.end())
                runtime_definition->type = value.type;
        }
        source.last_update_timestamp_ms = timestamp_ms;
        field.last_update_timestamp_ms = timestamp_ms;
        if (field.has_current_value && field.current_value == value) {
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Trace, "Value", [&]() {
                    return std::string("suppressed unchanged update source=") +
                           ExternalDataLog::sanitize(source_id, 160) +
                           " field=" + ExternalDataLog::sanitize(field_path, 320) +
                           " " + ExternalDataLog::value_summary(value);
                });
            return false;
        }
        field.current_value = value;
        field.has_current_value = true;
    }

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Trace, "Value", [&]() {
            return std::string("accepted update source=") +
                   ExternalDataLog::sanitize(source_id, 160) +
                   " field=" + ExternalDataLog::sanitize(field_path, 320) +
                   " " + ExternalDataLog::value_summary(value);
        });
    ExternalDataUpdate update;
    update.source_id = source_id;
    update.field_path = field_path;
    update.timestamp_ms = timestamp_ms;
    publish_change(std::move(update));
    return true;
}

bool ExternalDataManager::clear_value(
    const std::string &source_id, const std::string &field_path,
    int64_t timestamp_ms)
{
    if (source_id.empty() || field_path.empty())
        return false;
    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto source_it = sources_.find(source_id);
        if (source_it == sources_.end())
            return false;
        auto field_it = source_it->second.fields.find(field_path);
        if (field_it == source_it->second.fields.end() ||
            !field_it->second.has_current_value)
            return false;
        field_it->second.current_value = {};
        field_it->second.has_current_value = false;
        field_it->second.last_update_timestamp_ms = timestamp_ms;
        source_it->second.last_update_timestamp_ms = timestamp_ms;
    }

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Value", [&]() {
            return std::string("cleared current value source=") +
                   ExternalDataLog::sanitize(source_id, 160) +
                   " field=" + ExternalDataLog::sanitize(field_path, 320);
        });
    ExternalDataUpdate update;
    update.source_id = source_id;
    update.field_path = field_path;
    update.timestamp_ms = timestamp_ms;
    publish_change(std::move(update));
    return true;
}

bool ExternalDataManager::update_table(
    const std::string &source_id, ExternalDataTableSnapshot table,
    int64_t timestamp_ms)
{
    if (source_id.empty() || table.path.empty())
        return false;
    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();
    const std::string table_path = table.path;
    table.last_update_timestamp_ms = timestamp_ms;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ExternalDataSourceState &source = ensure_source_locked(source_id);
        const auto existing = source.tables.find(table_path);
        if (existing != source.tables.end() &&
            table_snapshot_equal(existing->second, table)) {
            existing->second.last_update_timestamp_ms = timestamp_ms;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Trace, "Table", [&]() {
                    std::ostringstream stream;
                    stream << "suppressed unchanged table source="
                           << ExternalDataLog::sanitize(source_id, 160)
                           << " table=" << ExternalDataLog::sanitize(table_path, 320)
                           << " rows=" << table.rows.size()
                           << " columns=" << table.columns.size();
                    return stream.str();
                });
            return false;
        }
        source.tables[table_path] = std::move(table);
        source.last_update_timestamp_ms = timestamp_ms;
    }

    const ExternalDataTableSnapshot logged_table = table_snapshot(source_id, table_path);
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Table", [&]() {
            std::ostringstream stream;
            stream << "updated table source=" << ExternalDataLog::sanitize(source_id, 160)
                   << " table=" << ExternalDataLog::sanitize(table_path, 320)
                   << " rows=" << logged_table.rows.size()
                   << " columns=" << logged_table.columns.size();
            return stream.str();
        });
    ExternalDataUpdate update;
    update.source_id = source_id;
    update.table_changed = true;
    update.table_path = table_path;
    update.timestamp_ms = timestamp_ms;
    publish_change(std::move(update));
    return true;
}

bool ExternalDataManager::synchronize_tables(
    const std::string &source_id,
    std::map<std::string, ExternalDataTableSnapshot> tables,
    int64_t timestamp_ms)
{
    if (source_id.empty())
        return false;
    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();

    std::set<std::string> changed_paths;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ExternalDataSourceState &source = ensure_source_locked(source_id);
        for (auto &entry : tables) {
            if (entry.first.empty())
                continue;
            entry.second.path = entry.first;
            entry.second.last_update_timestamp_ms = timestamp_ms;
            const auto existing = source.tables.find(entry.first);
            if (existing == source.tables.end() ||
                !table_snapshot_equal(existing->second, entry.second))
                changed_paths.insert(entry.first);
        }
        for (const auto &entry : source.tables) {
            if (tables.find(entry.first) == tables.end())
                changed_paths.insert(entry.first);
        }
        if (changed_paths.empty()) {
            for (auto &entry : source.tables)
                entry.second.last_update_timestamp_ms = timestamp_ms;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Trace, "Table", [&]() {
                    std::ostringstream stream;
                    stream << "suppressed unchanged table set source="
                           << ExternalDataLog::sanitize(source_id, 160)
                           << " tables=" << tables.size();
                    return stream.str();
                });
            return false;
        }
        source.tables = std::move(tables);
        source.last_update_timestamp_ms = timestamp_ms;
    }

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "Table", [&]() {
            std::ostringstream stream;
            stream << "synchronized table set source="
                   << ExternalDataLog::sanitize(source_id, 160)
                   << " changedTables=" << changed_paths.size();
            return stream.str();
        });
    for (const std::string &path : changed_paths) {
        ExternalDataUpdate update;
        update.source_id = source_id;
        update.table_changed = true;
        update.table_path = path;
        update.timestamp_ms = timestamp_ms;
        publish_change(std::move(update));
    }
    return true;
}

std::vector<ExternalDataTableSnapshot> ExternalDataManager::table_snapshots(
    const std::string &source_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ExternalDataTableSnapshot> result;
    const auto source = sources_.find(source_id);
    if (source == sources_.end())
        return result;
    result.reserve(source->second.tables.size());
    for (const auto &entry : source->second.tables)
        result.push_back(entry.second);
    return result;
}

ExternalDataTableSnapshot ExternalDataManager::table_snapshot(
    const std::string &source_id, const std::string &table_path) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto source = sources_.find(source_id);
    if (source == sources_.end())
        return {};
    const auto table = source->second.tables.find(table_path);
    return table == source->second.tables.end()
        ? ExternalDataTableSnapshot{} : table->second;
}

bool ExternalDataManager::update_mock_value(
    const std::string &source_id, const std::string &field_path,
    const ExternalDataValue &value, int64_t timestamp_ms)
{
    if (source_id.empty() || field_path.empty() || !value.is_set ||
        (value.type == ExternalDataType::Float &&
         !std::isfinite(value.float_value)))
        return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        ExternalDataSourceState &source = ensure_source_locked(source_id);
        if (source.definition.name.empty() ||
            source.definition.name == source_id)
            source.definition.name = "Mock: " + source_id;

        auto field_it = source.fields.find(field_path);
        if (field_it == source.fields.end()) {
            ExternalDataFieldDefinition definition;
            definition.path = field_path;
            definition.name = field_path;
            definition.type = value.type;
            ExternalDataFieldState state;
            state.definition = definition;
            state.authored_definition = false;
            source.fields.emplace(field_path, std::move(state));
            source.definition.fields.push_back(std::move(definition));
        } else if (field_it->second.definition.type != value.type) {
            if (field_it->second.authored_definition)
                return false;
            field_it->second.definition.type = value.type;
            auto runtime_definition = std::find_if(
                source.definition.fields.begin(), source.definition.fields.end(),
                [&field_path](const ExternalDataFieldDefinition &candidate) {
                    return candidate.path == field_path;
                });
            if (runtime_definition != source.definition.fields.end())
                runtime_definition->type = value.type;
        }
    }

    if (timestamp_ms <= 0)
        timestamp_ms = now_ms();
    const bool connection_changed = set_connection_state(
        source_id, ExternalDataConnectionState::Connected, {}, timestamp_ms);
    const bool value_changed =
        update_value(source_id, field_path, value, timestamp_ms);
    return connection_changed || value_changed;
}

ExternalDataSourceState ExternalDataManager::source_state(
    const std::string &source_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sources_.find(source_id);
    return it == sources_.end() ? ExternalDataSourceState{} : it->second;
}

std::vector<ExternalDataSourceState> ExternalDataManager::source_states() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ExternalDataSourceState> result;
    result.reserve(sources_.size());
    for (const auto &entry : sources_)
        result.push_back(entry.second);
    return result;
}

std::vector<ExternalDataFieldDefinition> available_external_data_fields(
    const ExternalDataSourceDefinition &source)
{
    std::map<std::string, ExternalDataFieldDefinition> merged;
    const ExternalDataSourceState runtime =
        ExternalDataManager::instance().source_state(source.id);
    for (const auto &entry : runtime.fields) {
        ExternalDataFieldDefinition field = entry.second.definition;
        if (field.path.empty())
            field.path = entry.first;
        if (field.name.empty())
            field.name = field.path;
        if (!field.path.empty())
            merged[field.path] = std::move(field);
    }
    for (const auto &field : source.fields) {
        if (!field.path.empty())
            merged[field.path] = field;
    }

    std::vector<ExternalDataFieldDefinition> result;
    result.reserve(merged.size());
    for (auto &entry : merged)
        result.push_back(std::move(entry.second));
    return result;
}

ExternalDataType external_data_field_type(
    const ExternalDataSourceDefinition &source, const std::string &field_path)
{
    const auto fields = available_external_data_fields(source);
    const auto field = std::find_if(fields.begin(), fields.end(),
        [&field_path](const ExternalDataFieldDefinition &candidate) {
            return candidate.path == field_path;
        });
    return field == fields.end() ? ExternalDataType::String : field->type;
}

bool pin_external_data_field(ExternalDataSourceDefinition &source,
                             const std::string &field_path)
{
    if (field_path.empty())
        return false;
    const auto existing = std::find_if(
        source.fields.begin(), source.fields.end(),
        [&field_path](const ExternalDataFieldDefinition &field) {
            return field.path == field_path;
        });
    if (existing != source.fields.end())
        return false;

    ExternalDataFieldDefinition pinned;
    const ExternalDataSourceState runtime =
        ExternalDataManager::instance().source_state(source.id);
    const auto discovered = runtime.fields.find(field_path);
    if (discovered != runtime.fields.end())
        pinned = discovered->second.definition;
    pinned.path = field_path;
    if (pinned.name.empty())
        pinned.name = field_path;
    source.fields.push_back(std::move(pinned));
    ExternalDataManager::instance().register_source(source);
    return true;
}

bool pin_external_data_field(Title &title, const std::string &source_id,
                             const std::string &field_path)
{
    const auto source = std::find_if(
        title.external_data_sources.begin(), title.external_data_sources.end(),
        [&source_id](const ExternalDataSourceDefinition &candidate) {
            return candidate.id == source_id;
        });
    return source != title.external_data_sources.end() &&
           pin_external_data_field(*source, field_path);
}

uint64_t ExternalDataManager::on_change(ChangeCallback callback)
{
    if (!callback)
        return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t id = next_observer_id_++;
    observers_.push_back({id, std::move(callback)});
    return id;
}

void ExternalDataManager::remove_change_callback(uint64_t callback_id)
{
    if (callback_id == 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
                       [callback_id](const Observer &observer) {
                           return observer.id == callback_id;
                       }),
        observers_.end());
}

void ExternalDataManager::publish_change(ExternalDataUpdate update)
{
    update.sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_release);

    std::vector<ChangeCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string queue_key = render_update_key(update);
        const auto queued = render_queue_index_.find(queue_key);
        const bool coalesced = queued != render_queue_index_.end();
        if (!coalesced) {
            render_queue_index_.emplace(queue_key, render_queue_.size());
            render_queue_.push_back(update);
        } else {
            render_queue_[queued->second] = update;
            fxm::perf::add(fxm::perf::Counter::ExternalUpdatesCoalesced);
        }
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Trace, "RenderQueue", [&]() {
                std::ostringstream stream;
                stream << (coalesced ? "coalesced" : "enqueued")
                       << " update sequence=" << update.sequence
                       << " source=" << ExternalDataLog::sanitize(update.source_id, 160)
                       << " field=" << ExternalDataLog::sanitize(update.field_path, 320)
                       << " table=" << ExternalDataLog::sanitize(update.table_path, 320)
                       << " stateChanged=" << (update.connection_state_changed ? 1 : 0)
                       << " tableChanged=" << (update.table_changed ? 1 : 0)
                       << " queueDepth=" << render_queue_.size();
                return stream.str();
            });
        callbacks.reserve(observers_.size());
        for (const auto &observer : observers_)
            callbacks.push_back(observer.callback);
    }

    /* The manager revision is deliberately independent from TitleDataStore.
     * Provider updates never write a title, create undo history, schedule
     * persistence, or wake unrelated sources through the global store revision. */
    fxm::perf::add(fxm::perf::Counter::ExternalUpdatesPublished);
    for (auto &callback : callbacks) {
        try {
            callback(update);
        } catch (...) {
            /* Provider threads must survive a faulty optional observer. */
        }
    }
}

std::vector<ExternalDataUpdate> ExternalDataManager::take_render_updates()
{
    std::vector<ExternalDataUpdate> queued;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queued.swap(render_queue_);
        render_queue_index_.clear();
    }
    std::sort(queued.begin(), queued.end(),
              [](const ExternalDataUpdate &a, const ExternalDataUpdate &b) {
                  return a.sequence < b.sequence;
              });
    if (!queued.empty()) {
        ExternalDataLog::write_lazy(
            ExternalDataLogLevel::Debug, "RenderQueue", [&]() {
                std::ostringstream stream;
                stream << "render thread consumed updates=" << queued.size()
                       << " firstSequence=" << queued.front().sequence
                       << " lastSequence=" << queued.back().sequence;
                return stream.str();
            });
    }
    return queued;
}

ExternalDataResolution ExternalDataManager::resolve(
    const ExternalPropertyBinding &binding,
    const ExternalDataValue &authored_value) const
{
    if (!binding.enabled || binding.source_id.empty() ||
        binding.field_path.empty())
        return {authored_value, ExternalDataValueOrigin::Authored};

    std::lock_guard<std::mutex> lock(mutex_);
    const auto source_it = sources_.find(binding.source_id);
    if (source_it != sources_.end()) {
        const ExternalDataSourceState &source = source_it->second;
        const bool current_value_is_allowed =
            state_allows_current_value(source.definition, source.connection_state);

        /* Higher-level adapters such as table-to-cue mappings carry the exact
         * row-specific cell value in runtime_value. It must win over a scalar
         * field with the same path: CSV providers and flattened JSON arrays may
         * expose a source-level scalar that is empty, belongs to another row, or
         * is only a schema placeholder. Checking the scalar first made the cue
         * rows exist while their displayed/output values stayed blank even though
         * the table preview contained valid data. */
        if (current_value_is_allowed && binding.has_runtime_value &&
            binding.runtime_value.is_set) {
            const bool empty_uses_fallback =
                binding.formatter_config.empty_value_mode ==
                    ExternalDataEmptyValueMode::UseFallback &&
                external_data_value_is_empty(binding.runtime_value);
            if (!empty_uses_fallback) {
                return {binding.runtime_value,
                        ExternalDataValueOrigin::LiveExternal};
            }
        }

        const auto field_it = source.fields.find(binding.field_path);
        if (current_value_is_allowed &&
            field_it != source.fields.end() &&
            field_it->second.has_current_value) {
            const bool empty_uses_fallback =
                binding.formatter_config.empty_value_mode ==
                    ExternalDataEmptyValueMode::UseFallback &&
                external_data_value_is_empty(field_it->second.current_value);
            if (!empty_uses_fallback) {
                return {field_it->second.current_value,
                        ExternalDataValueOrigin::LiveExternal};
            }
        }
        if (binding.has_fallback_value && binding.fallback_value.is_set) {
            return {binding.fallback_value,
                    ExternalDataValueOrigin::BindingFallback};
        }
        const ExternalDataFieldDefinition *definition =
            field_definition_for(source, binding.field_path);
        if (definition && definition->has_default_value &&
            definition->default_value.is_set) {
            return {definition->default_value,
                    ExternalDataValueOrigin::FieldDefault};
        }
    } else if (binding.has_runtime_value && binding.runtime_value.is_set) {
        /* A materialized table row can outlive a transient provider registry
         * rebuild. Keep its exact runtime cell value available until the table
         * synchronizer replaces or removes the generated binding. */
        const bool empty_uses_fallback =
            binding.formatter_config.empty_value_mode ==
                ExternalDataEmptyValueMode::UseFallback &&
            external_data_value_is_empty(binding.runtime_value);
        if (!empty_uses_fallback) {
            return {binding.runtime_value,
                    ExternalDataValueOrigin::LiveExternal};
        }
        if (binding.has_fallback_value && binding.fallback_value.is_set) {
            return {binding.fallback_value,
                    ExternalDataValueOrigin::BindingFallback};
        }
    } else if (binding.has_fallback_value && binding.fallback_value.is_set) {
        return {binding.fallback_value,
                ExternalDataValueOrigin::BindingFallback};
    }

    return {authored_value, ExternalDataValueOrigin::Authored};
}

ExternalDataValue ExternalDataManager::resolve_value(
    const ExternalPropertyBinding &binding,
    const ExternalDataValue &authored_value,
    bool *using_live_value,
    bool *using_fallback_value) const
{
    const ExternalDataResolution resolution = resolve(binding, authored_value);
    if (using_live_value) {
        *using_live_value =
            resolution.origin == ExternalDataValueOrigin::LiveExternal;
    }
    if (using_fallback_value) {
        *using_fallback_value =
            resolution.origin == ExternalDataValueOrigin::BindingFallback ||
            resolution.origin == ExternalDataValueOrigin::FieldDefault;
    }
    return resolution.value;
}

const ExternalPropertyBinding *external_binding_for_property(
    const Layer &layer, const std::string &property_path)
{
    const auto runtime = std::find_if(
        layer.runtime_external_bindings.begin(), layer.runtime_external_bindings.end(),
        [&property_path](const ExternalPropertyBinding &binding) {
            return binding.enabled && binding.property_path == property_path;
        });
    if (runtime != layer.runtime_external_bindings.end())
        return &*runtime;
    const auto it = std::find_if(
        layer.external_bindings.begin(), layer.external_bindings.end(),
        [&property_path](const ExternalPropertyBinding &binding) {
            return binding.enabled && binding.property_path == property_path;
        });
    return it == layer.external_bindings.end() ? nullptr : &*it;
}

void set_external_binding(Layer &layer, ExternalPropertyBinding binding)
{
    if (binding.property_path.empty())
        return;
    auto existing = std::find_if(
        layer.external_bindings.begin(), layer.external_bindings.end(),
        [&binding](const ExternalPropertyBinding &candidate) {
            return candidate.property_path == binding.property_path;
        });
    if (existing == layer.external_bindings.end())
        layer.external_bindings.push_back(std::move(binding));
    else
        *existing = std::move(binding);
}

bool remove_external_binding(Layer &layer,
                             const std::string &property_path)
{
    const auto first_removed = std::remove_if(
        layer.external_bindings.begin(), layer.external_bindings.end(),
        [&property_path](const ExternalPropertyBinding &binding) {
            return binding.property_path == property_path;
        });
    if (first_removed == layer.external_bindings.end())
        return false;
    layer.external_bindings.erase(first_removed, layer.external_bindings.end());
    return true;
}

bool layer_has_external_binding(const Layer &layer,
                                const std::string &property_path)
{
    if (property_path.empty()) {
        return std::any_of(layer.external_bindings.begin(),
                           layer.external_bindings.end(),
                           [](const ExternalPropertyBinding &binding) {
                               return binding.enabled &&
                                      !binding.source_id.empty() &&
                                      !binding.field_path.empty();
                           });
    }
    return external_binding_for_property(layer, property_path) != nullptr;
}

bool title_uses_external_update(const Title &title,
                                const ExternalDataUpdate &update)
{
    for (const auto &layer : title.layers) {
        if (!layer)
            continue;
        if (layer->type == LayerType::Chart &&
            layer->chart_binding.source_id == update.source_id &&
            (update.connection_state_changed ||
             (update.table_changed &&
              (update.table_path.empty() ||
               update.table_path == layer->chart_binding.table_path))))
            return true;
        for (const auto *collection : {&layer->runtime_external_bindings,
                                      &layer->external_bindings}) {
            for (const auto &binding : *collection) {
                if (!binding.enabled || binding.source_id != update.source_id)
                    continue;
                if (update.connection_state_changed ||
                    binding.field_path == update.field_path)
                    return true;
            }
        }
    }
    for (const auto &cell : title.live_text_external_bindings) {
        const auto &binding = cell.binding;
        if (binding.enabled && binding.source_id == update.source_id &&
            (update.connection_state_changed || update.table_changed ||
             binding.field_path == update.field_path))
            return true;
    }
    if (update.table_changed) {
        for (const auto &mapping : title.live_text_table_bindings) {
            if (mapping.enabled && mapping.source_id == update.source_id &&
                (mapping.table_path == update.table_path || update.table_path.empty()))
                return true;
        }
    }
    return false;
}

bool external_data_value_is_empty(const ExternalDataValue &value)
{
    if (!value.is_set)
        return true;
    switch (value.type) {
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
        return value.string_value.empty();
    default:
        return false;
    }
}

std::string external_data_value_to_string(const ExternalDataValue &value)
{
    if (!value.is_set)
        return {};
    std::ostringstream out;
    switch (value.type) {
    case ExternalDataType::Integer:
        return std::to_string(value.integer_value);
    case ExternalDataType::Float:
        out << std::setprecision(15) << value.float_value;
        return out.str();
    case ExternalDataType::Boolean:
        return value.boolean_value ? "true" : "false";
    case ExternalDataType::Color:
        out << '#' << std::uppercase << std::hex << std::setw(8)
            << std::setfill('0') << value.color_value;
        return out.str();
    case ExternalDataType::String:
    case ExternalDataType::DateTime:
    case ExternalDataType::FilePath:
    case ExternalDataType::Url:
    default:
        return value.string_value;
    }
}

std::string format_external_data_value(const ExternalDataValue &value,
                                       const std::string &formatter)
{
    std::string rendered = external_data_value_to_string(value);
    if (formatter.empty())
        return rendered;

    if (formatter == "upper")
        return upper_ascii(rendered);
    if (formatter == "lower")
        return lower_ascii(rendered);
    if (formatter == "trim")
        return trim_ascii(rendered);

    if (formatter.rfind("fixed:", 0) == 0 &&
        (value.type == ExternalDataType::Float ||
         value.type == ExternalDataType::Integer)) {
        int decimals = 0;
        try {
            decimals = std::clamp(std::stoi(formatter.substr(6)), 0, 12);
        } catch (...) {
            return rendered;
        }
        std::ostringstream out;
        out << std::fixed << std::setprecision(decimals)
            << (value.type == ExternalDataType::Integer
                    ? static_cast<double>(value.integer_value)
                    : value.float_value);
        return out.str();
    }

    if (formatter.rfind("bool:", 0) == 0 &&
        value.type == ExternalDataType::Boolean) {
        const std::string options = formatter.substr(5);
        const size_t separator = options.find('|');
        if (separator != std::string::npos)
            return value.boolean_value ? options.substr(0, separator)
                                       : options.substr(separator + 1);
    }

    std::string templated = formatter;
    if (templated.find("{value}") != std::string::npos) {
        replace_all(templated, "{value}", rendered);
        return templated;
    }
    return rendered;
}

std::string format_external_data_value(const ExternalDataValue &value,
                                       const ExternalDataFormatterConfig &formatter,
                                       const std::string &legacy_formatter)
{
    std::string rendered = external_data_value_to_string(value);
    if (!legacy_formatter.empty())
        rendered = format_external_data_value(value, legacy_formatter);
    if (formatter_config_is_default(formatter))
        return rendered;

    if (rendered.empty()) {
        if (formatter.empty_value_mode == ExternalDataEmptyValueMode::Replacement)
            rendered = formatter.empty_replacement;
        else if (formatter.empty_value_mode == ExternalDataEmptyValueMode::KeepEmpty)
            return {};
    }

    for (const auto &rule : formatter.conditional_replacements) {
        const bool matches = rule.case_sensitive
            ? rendered == rule.match
            : lower_ascii(rendered) == lower_ascii(rule.match);
        if (matches) {
            rendered = rule.replacement;
            break;
        }
    }

    if (!formatter.date_time_format.empty()) {
        std::tm tm{};
        if (utc_tm_from_value(value, tm)) {
            char buffer[512]{};
            if (std::strftime(buffer, sizeof(buffer), formatter.date_time_format.c_str(), &tm) > 0)
                rendered = buffer;
        }
    } else if (formatter.number_format_enabled &&
               (value.type == ExternalDataType::Integer ||
                value.type == ExternalDataType::Float)) {
        std::ostringstream out;
        if (formatter.decimal_places >= 0) {
            out << std::fixed << std::setprecision(std::clamp(formatter.decimal_places, 0, 12));
        } else {
            out << std::setprecision(15);
        }
        if (value.type == ExternalDataType::Integer)
            out << value.integer_value;
        else
            out << value.float_value;
        rendered = out.str();
        if (formatter.thousands_separator)
            rendered = add_thousands_separators(rendered);
    }

    switch (formatter.text_case) {
    case ExternalDataTextCase::Uppercase: rendered = upper_ascii(rendered); break;
    case ExternalDataTextCase::Lowercase: rendered = lower_ascii(rendered); break;
    case ExternalDataTextCase::TitleCase: rendered = title_ascii(rendered); break;
    case ExternalDataTextCase::None: default: break;
    }

    if (rendered.empty() && formatter.empty_value_mode == ExternalDataEmptyValueMode::KeepEmpty)
        return {};
    return formatter.prefix + rendered + formatter.suffix;
}

static const LiveTextExternalBinding *live_text_external_binding_record_for_cell(
    const Title &title, const std::string &row_id, const std::string &layer_id)
{
    const auto it = std::find_if(title.live_text_external_bindings.begin(),
                                 title.live_text_external_bindings.end(),
        [&row_id, &layer_id](const LiveTextExternalBinding &candidate) {
            return candidate.row_id == row_id && candidate.layer_id == layer_id;
        });
    return it == title.live_text_external_bindings.end() ? nullptr : &*it;
}

const LiveTextExternalBinding *live_text_external_binding_for_cell(
    const Title &title, const std::string &row_id, const std::string &layer_id)
{
    const LiveTextExternalBinding *record =
        live_text_external_binding_record_for_cell(title, row_id, layer_id);
    return record && record->binding.enabled ? record : nullptr;
}

LiveTextCueCellState live_text_cue_cell_state(
    const Title &title, const std::string &row_id, const std::string &layer_id)
{
    const LiveTextExternalBinding *record =
        live_text_external_binding_record_for_cell(title, row_id, layer_id);
    if (!record)
        return LiveTextCueCellState::Authored;
    if (!record->table_binding_id.empty()) {
        return record->binding.enabled
            ? LiveTextCueCellState::ExternalTableManaged
            : LiveTextCueCellState::DetachedFromTable;
    }
    return record->binding.enabled
        ? LiveTextCueCellState::ExternalBound
        : LiveTextCueCellState::Authored;
}

void set_live_text_external_binding(Title &title, LiveTextExternalBinding binding)
{
    if (binding.row_id.empty() || binding.layer_id.empty())
        return;
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "CueBinding", [&]() {
            std::ostringstream stream;
            stream << "set cell binding title=" << ExternalDataLog::sanitize(title.id, 160)
                   << " row=" << ExternalDataLog::sanitize(binding.row_id, 240)
                   << " layer=" << ExternalDataLog::sanitize(binding.layer_id, 160)
                   << " source=" << ExternalDataLog::sanitize(binding.binding.source_id, 160)
                   << " field=" << ExternalDataLog::sanitize(binding.binding.field_path, 320)
                   << " tableBinding=" << ExternalDataLog::sanitize(binding.table_binding_id, 160)
                   << " runtimeValue=" << (binding.binding.has_runtime_value ? 1 : 0);
            if (binding.binding.has_runtime_value)
                stream << " " << ExternalDataLog::value_summary(binding.binding.runtime_value);
            return stream.str();
        });
    auto existing = std::find_if(title.live_text_external_bindings.begin(),
                                 title.live_text_external_bindings.end(),
        [&binding](const LiveTextExternalBinding &candidate) {
            return candidate.row_id == binding.row_id &&
                   candidate.layer_id == binding.layer_id;
        });
    if (existing == title.live_text_external_bindings.end())
        title.live_text_external_bindings.push_back(std::move(binding));
    else
        *existing = std::move(binding);
}

bool remove_live_text_external_binding(Title &title, const std::string &row_id,
                                       const std::string &layer_id)
{
    const auto first = std::remove_if(title.live_text_external_bindings.begin(),
                                      title.live_text_external_bindings.end(),
        [&row_id, &layer_id](const LiveTextExternalBinding &candidate) {
            return candidate.row_id == row_id && candidate.layer_id == layer_id;
        });
    if (first == title.live_text_external_bindings.end())
        return false;
    title.live_text_external_bindings.erase(first, title.live_text_external_bindings.end());
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "CueBinding", [&]() {
            return std::string("removed cell binding title=") +
                   ExternalDataLog::sanitize(title.id, 160) +
                   " row=" + ExternalDataLog::sanitize(row_id, 240) +
                   " layer=" + ExternalDataLog::sanitize(layer_id, 160);
        });
    return true;
}

bool detach_live_text_table_cell(Title &title, const std::string &row_id,
                                 const std::string &layer_id)
{
    auto it = std::find_if(title.live_text_external_bindings.begin(),
                           title.live_text_external_bindings.end(),
        [&row_id, &layer_id](const LiveTextExternalBinding &candidate) {
            return candidate.row_id == row_id && candidate.layer_id == layer_id &&
                   !candidate.table_binding_id.empty();
        });
    if (it == title.live_text_external_bindings.end() || !it->binding.enabled)
        return false;

    /* Keep a disabled table-origin marker. The synchronizer uses it as an
     * explicit authored override and therefore does not regenerate this cell
     * on the next provider refresh. */
    it->binding.enabled = false;
    it->binding.runtime_value = {};
    it->binding.has_runtime_value = false;
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "CueBinding", [&]() {
            return std::string("detached table-managed cell title=") +
                   ExternalDataLog::sanitize(title.id, 160) +
                   " row=" + ExternalDataLog::sanitize(row_id, 240) +
                   " layer=" + ExternalDataLog::sanitize(layer_id, 160) +
                   " tableBinding=" + ExternalDataLog::sanitize(it->table_binding_id, 160);
        });
    return true;
}

bool restore_live_text_table_cell(Title &title, const std::string &row_id,
                                  const std::string &layer_id)
{
    const auto it = std::find_if(title.live_text_external_bindings.begin(),
                                 title.live_text_external_bindings.end(),
        [&row_id, &layer_id](const LiveTextExternalBinding &candidate) {
            return candidate.row_id == row_id && candidate.layer_id == layer_id &&
                   !candidate.table_binding_id.empty() &&
                   !candidate.binding.enabled;
        });
    if (it == title.live_text_external_bindings.end())
        return false;
    const std::string table_binding_id = it->table_binding_id;
    title.live_text_external_bindings.erase(it);
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "CueBinding", [&]() {
            return std::string("restored table-managed cell title=") +
                   ExternalDataLog::sanitize(title.id, 160) +
                   " row=" + ExternalDataLog::sanitize(row_id, 240) +
                   " layer=" + ExternalDataLog::sanitize(layer_id, 160) +
                   " tableBinding=" + ExternalDataLog::sanitize(table_binding_id, 160);
        });
    return true;
}

std::string effective_live_text_cue_value(const Title &title, int row,
                                          const Layer &layer,
                                          const std::string &authored_value,
                                          bool *using_live_value,
                                          bool *using_fallback_value)
{
    const std::string row_id = external_live_text_row_id(title, row);
    const LiveTextExternalBinding *cell =
        live_text_external_binding_for_cell(title, row_id, layer.id);
    if (!cell) {
        if (using_live_value) *using_live_value = false;
        if (using_fallback_value) *using_fallback_value = false;
        return authored_value;
    }
    const ExternalDataValue resolved = ExternalDataManager::instance().resolve_value(
        cell->binding, authored_string_value(authored_value),
        using_live_value, using_fallback_value);
    return format_external_data_value(resolved, cell->binding.formatter_config,
                                      cell->binding.formatter);
}

void apply_live_text_runtime_binding(Title &title, int row, Layer &layer)
{
    const std::string property = layer.type == LayerType::Image
        ? ExternalPropertyPaths::ImagePath : ExternalPropertyPaths::TextContent;
    layer.runtime_external_bindings.erase(
        std::remove_if(layer.runtime_external_bindings.begin(),
                       layer.runtime_external_bindings.end(),
            [&property](const ExternalPropertyBinding &binding) {
                return binding.property_path == property;
            }),
        layer.runtime_external_bindings.end());

    const LiveTextExternalBinding *cell = live_text_external_binding_for_cell(
        title, external_live_text_row_id(title, row), layer.id);
    if (!cell)
        return;
    ExternalPropertyBinding runtime = cell->binding;
    runtime.property_path = property;
    layer.runtime_external_bindings.push_back(std::move(runtime));
}

std::string effective_external_string(const Layer &layer,
                                      const std::string &property_path,
                                      const std::string &authored_value,
                                      bool *using_live_value,
                                      bool *using_fallback_value)
{
    const ExternalPropertyBinding *binding =
        external_binding_for_property(layer, property_path);
    if (!binding) {
        if (using_live_value)
            *using_live_value = false;
        if (using_fallback_value)
            *using_fallback_value = false;
        return authored_value;
    }

    const ExternalDataValue resolved =
        ExternalDataManager::instance().resolve_value(
            *binding, authored_string_value(authored_value),
            using_live_value, using_fallback_value);
    return format_external_data_value(resolved, binding->formatter_config, binding->formatter);
}

double effective_external_number(const Layer &layer,
                                 const std::string &property_path,
                                 double authored_value)
{
    const ExternalPropertyBinding *binding =
        external_binding_for_property(layer, property_path);
    if (!binding)
        return authored_value;
    ExternalDataValue authored = ExternalDataValue::floating(authored_value);
    const ExternalDataValue value =
        ExternalDataManager::instance().resolve_value(*binding, authored);
    switch (value.type) {
    case ExternalDataType::Integer:
        return static_cast<double>(value.integer_value);
    case ExternalDataType::Float:
        return std::isfinite(value.float_value) ? value.float_value
                                                : authored_value;
    case ExternalDataType::Boolean:
        return value.boolean_value ? 1.0 : 0.0;
    default:
        try {
            return std::stod(external_data_value_to_string(value));
        } catch (...) {
            return authored_value;
        }
    }
}

bool effective_external_boolean(const Layer &layer,
                                const std::string &property_path,
                                bool authored_value)
{
    const ExternalPropertyBinding *binding =
        external_binding_for_property(layer, property_path);
    if (!binding)
        return authored_value;
    const ExternalDataValue value = ExternalDataManager::instance().resolve_value(
        *binding, ExternalDataValue::boolean(authored_value));
    switch (value.type) {
    case ExternalDataType::Boolean:
        return value.boolean_value;
    case ExternalDataType::Integer:
        return value.integer_value != 0;
    case ExternalDataType::Float:
        return std::abs(value.float_value) > std::numeric_limits<double>::epsilon();
    default: {
        const std::string normalized = lower_ascii(trim_ascii(
            external_data_value_to_string(value)));
        if (normalized == "true" || normalized == "yes" || normalized == "1" ||
            normalized == "on")
            return true;
        if (normalized == "false" || normalized == "no" || normalized == "0" ||
            normalized == "off")
            return false;
        return authored_value;
    }
    }
}

uint32_t effective_external_color(const Layer &layer,
                                  const std::string &property_path,
                                  uint32_t authored_value)
{
    const ExternalPropertyBinding *binding =
        external_binding_for_property(layer, property_path);
    if (!binding)
        return authored_value;
    const ExternalDataValue value = ExternalDataManager::instance().resolve_value(
        *binding, ExternalDataValue::color(authored_value));
    if (value.type == ExternalDataType::Color)
        return value.color_value;

    std::string text = trim_ascii(external_data_value_to_string(value));
    if (!text.empty() && text.front() == '#')
        text.erase(text.begin());
    if (text.size() != 6 && text.size() != 8)
        return authored_value;
    try {
        const uint32_t parsed = static_cast<uint32_t>(std::stoull(text, nullptr, 16));
        return text.size() == 6 ? (0xFF000000u | parsed) : parsed;
    } catch (...) {
        return authored_value;
    }
}

namespace {

static bool formatter_equal(const ExternalDataFormatterConfig &a,
                            const ExternalDataFormatterConfig &b)
{
    if (a.prefix != b.prefix || a.suffix != b.suffix ||
        a.number_format_enabled != b.number_format_enabled ||
        a.decimal_places != b.decimal_places ||
        a.thousands_separator != b.thousands_separator ||
        a.text_case != b.text_case ||
        a.date_time_format != b.date_time_format ||
        a.empty_value_mode != b.empty_value_mode ||
        a.empty_replacement != b.empty_replacement ||
        a.conditional_replacements.size() != b.conditional_replacements.size())
        return false;
    for (size_t i = 0; i < a.conditional_replacements.size(); ++i) {
        const auto &left = a.conditional_replacements[i];
        const auto &right = b.conditional_replacements[i];
        if (left.match != right.match || left.replacement != right.replacement ||
            left.case_sensitive != right.case_sensitive)
            return false;
    }
    return true;
}

static bool property_binding_equal(const ExternalPropertyBinding &a,
                                   const ExternalPropertyBinding &b)
{
    return a.enabled == b.enabled && a.property_path == b.property_path &&
           a.source_id == b.source_id && a.field_path == b.field_path &&
           a.formatter == b.formatter &&
           formatter_equal(a.formatter_config, b.formatter_config) &&
           a.fallback_value == b.fallback_value &&
           a.has_fallback_value == b.has_fallback_value &&
           a.has_runtime_value == b.has_runtime_value &&
           (!a.has_runtime_value || a.runtime_value == b.runtime_value);
}

static bool cell_binding_equal(const LiveTextExternalBinding &a,
                               const LiveTextExternalBinding &b)
{
    return a.row_id == b.row_id && a.layer_id == b.layer_id &&
           a.table_binding_id == b.table_binding_id &&
           property_binding_equal(a.binding, b.binding);
}

static bool cell_binding_vectors_equal(
    const std::vector<LiveTextExternalBinding> &a,
    const std::vector<LiveTextExternalBinding> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (!cell_binding_equal(a[i], b[i]))
            return false;
    }
    return true;
}

static std::string table_row_key(const ExternalDataTableRow &row,
                                 const LiveTextTableBinding &mapping,
                                 size_t logical_index)
{
    if (!mapping.row_id_field.empty()) {
        const auto value = row.values.find(mapping.row_id_field);
        if (value != row.values.end()) {
            const std::string rendered = external_data_value_to_string(value->second);
            if (!rendered.empty())
                return rendered;
        }
    }
    if (!row.key.empty())
        return row.key;
    return std::to_string(logical_index);
}

static int live_text_column_index(const Title &title,
                                  const std::string &layer_id)
{
    const auto it = std::find(title.live_text_column_order.begin(),
                              title.live_text_column_order.end(), layer_id);
    return it == title.live_text_column_order.end()
        ? -1 : static_cast<int>(it - title.live_text_column_order.begin());
}

static std::string authored_table_cell_fallback(
    const ExternalPropertyBinding &binding)
{
    if (!binding.has_fallback_value || !binding.fallback_value.is_set)
        return {};
    return format_external_data_value(binding.fallback_value,
                                      binding.formatter_config,
                                      binding.formatter);
}

struct MaterializedTableRow {
    std::string row_id;
    std::vector<std::string> values;
    std::vector<LiveTextExternalBinding> generated_bindings;
};

static std::vector<MaterializedTableRow> materialize_table_rows(
    const Title &title, const LiveTextTableBinding &mapping,
    const ExternalDataTableSnapshot &table)
{
    std::vector<MaterializedTableRow> result;
    if (mapping.columns.empty() || table.rows.empty())
        return result;

    const size_t start = std::min<size_t>(
        static_cast<size_t>(std::max(0, mapping.start_row)), table.rows.size());
    size_t end = table.rows.size();
    if (mapping.maximum_rows > 0)
        end = std::min(end, start + static_cast<size_t>(mapping.maximum_rows));
    result.reserve(end - start);

    std::set<std::string> used_ids;
    for (size_t source_index = start; source_index < end; ++source_index) {
        const ExternalDataTableRow &source_row = table.rows[source_index];
        bool has_nonempty_mapped_value = false;
        for (const auto &column : mapping.columns) {
            const auto value = source_row.values.find(column.binding.field_path);
            if (value != source_row.values.end() &&
                !external_data_value_is_empty(value->second)) {
                has_nonempty_mapped_value = true;
                break;
            }
        }
        if (mapping.ignore_empty_rows && !has_nonempty_mapped_value)
            continue;

        std::string key = table_row_key(source_row, mapping, source_index);
        std::string row_id = table_managed_row_id(mapping.id, key);
        unsigned collision = 1;
        while (!used_ids.insert(row_id).second) {
            row_id = table_managed_row_id(
                mapping.id, key + "#" + std::to_string(collision++));
        }

        MaterializedTableRow output;
        output.row_id = std::move(row_id);
        output.values.resize(title.live_text_column_order.size());
        for (const auto &column : mapping.columns) {
            const int cue_column = live_text_column_index(title, column.layer_id);
            if (cue_column < 0)
                continue;
            ExternalPropertyBinding generated = column.binding;
            generated.enabled = true;
            generated.source_id = mapping.source_id;
            const auto source_path = source_row.source_field_paths.find(
                column.binding.field_path);
            if (source_path != source_row.source_field_paths.end() &&
                !source_path->second.empty()) {
                generated.field_path = source_path->second;
            } else {
                /* Keep the binding structurally valid even for providers that
                 * expose table cells without a parallel scalar field registry.
                 * Runtime resolution below uses the snapshot value directly. */
                generated.field_path = column.binding.field_path;
            }
            if (generated.property_path.empty())
                generated.property_path = ExternalPropertyPaths::TextContent;

            output.values[static_cast<size_t>(cue_column)] =
                authored_table_cell_fallback(generated);
            LiveTextExternalBinding cell;
            cell.row_id = output.row_id;
            cell.layer_id = column.layer_id;
            cell.table_binding_id = mapping.id;
            cell.binding = std::move(generated);
            const auto runtime_value = source_row.values.find(
                column.binding.field_path);
            if (runtime_value != source_row.values.end()) {
                cell.binding.runtime_value = runtime_value->second;
                cell.binding.has_runtime_value = runtime_value->second.is_set;
            }
            output.generated_bindings.push_back(std::move(cell));
        }
        if (!output.generated_bindings.empty())
            result.push_back(std::move(output));
    }
    return result;
}

} // namespace

bool synchronize_live_text_table_bindings(Title &title,
                                           const std::string &source_id)
{
    ensure_table_row_ids(title);
    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Debug, "TableMapping", [&]() {
            std::ostringstream stream;
            stream << "synchronize begin title=" << ExternalDataLog::sanitize(title.id, 160)
                   << " sourceFilter=" << ExternalDataLog::sanitize(source_id, 160)
                   << " mappings=" << title.live_text_table_bindings.size()
                   << " rows=" << title.live_text_rows.size()
                   << " cellBindings=" << title.live_text_external_bindings.size();
            return stream.str();
        });
    const auto old_rows = title.live_text_rows;
    const auto old_row_ids = title.live_text_row_ids;
    const auto old_cell_bindings = title.live_text_external_bindings;
    const std::string active_row_id =
        title.current_cue_row >= 0 &&
        title.current_cue_row < static_cast<int>(title.live_text_row_ids.size())
            ? title.live_text_row_ids[static_cast<size_t>(title.current_cue_row)]
            : std::string{};
    const std::string pending_row_id =
        title.pending_cue_row >= 0 &&
        title.pending_cue_row < static_cast<int>(title.live_text_row_ids.size())
            ? title.live_text_row_ids[static_cast<size_t>(title.pending_cue_row)]
            : std::string{};

    for (const LiveTextTableBinding &mapping : title.live_text_table_bindings) {
        if (!mapping.enabled || mapping.id.empty() || mapping.source_id.empty() ||
            mapping.table_path.empty() ||
            (!source_id.empty() && mapping.source_id != source_id))
            continue;

        ExternalDataTableSnapshot table =
            ExternalDataManager::instance().table_snapshot(
                mapping.source_id, mapping.table_path);
        if (table.path.empty()) {
            if (source_id.empty())
                continue;
            table.path = mapping.table_path;
        }
        const std::vector<MaterializedTableRow> desired =
            materialize_table_rows(title, mapping, table);
        ExternalDataLog::write_lazy(
            table.path.empty() ? ExternalDataLogLevel::Warning
                               : ExternalDataLogLevel::Debug,
            "TableMapping", [&]() {
                std::ostringstream stream;
                stream << "mapping title=" << ExternalDataLog::sanitize(title.id, 160)
                       << " id=" << ExternalDataLog::sanitize(mapping.id, 160)
                       << " source=" << ExternalDataLog::sanitize(mapping.source_id, 160)
                       << " table=" << ExternalDataLog::sanitize(mapping.table_path, 320)
                       << " mode=" << ExternalDataLog::table_update_mode_name(mapping.update_mode)
                       << " snapshotRows=" << table.rows.size()
                       << " snapshotColumns=" << table.columns.size()
                       << " desiredRows=" << desired.size()
                       << " mappedColumns=" << mapping.columns.size();
                return stream.str();
            });
        const std::string prefix = table_managed_row_prefix(mapping.id);

        std::map<std::string, size_t> existing_index;
        for (size_t i = 0; i < title.live_text_row_ids.size(); ++i)
            existing_index[title.live_text_row_ids[i]] = i;

        std::vector<std::vector<std::string>> next_rows;
        std::vector<std::string> next_ids;
        if (mapping.update_mode == LiveTextTableUpdateMode::AppendRows) {
            next_rows = title.live_text_rows;
            next_ids = title.live_text_row_ids;
            for (const auto &row : desired) {
                const auto existing = existing_index.find(row.row_id);
                if (existing == existing_index.end()) {
                    next_rows.push_back(row.values);
                    next_ids.push_back(row.row_id);
                } else if (existing->second < next_rows.size()) {
                    next_rows[existing->second] = row.values;
                }
            }
        } else {
            if (mapping.update_mode == LiveTextTableUpdateMode::SynchronizeRows &&
                mapping.preserve_manual_rows) {
                for (size_t i = 0; i < title.live_text_rows.size(); ++i) {
                    const std::string id = i < title.live_text_row_ids.size()
                        ? title.live_text_row_ids[i] : std::string{};
                    if (!starts_with(id, prefix)) {
                        next_rows.push_back(title.live_text_rows[i]);
                        next_ids.push_back(id);
                    }
                }
            }
            for (const auto &row : desired) {
                next_rows.push_back(row.values);
                next_ids.push_back(row.row_id);
            }
        }

        /* Preserve authored values for explicit per-cell overrides. A table
         * refresh replaces the generated row payload, but it must not overwrite
         * cells that the user detached or rebound manually. */
        for (size_t next_index = 0; next_index < next_ids.size(); ++next_index) {
            const auto previous = existing_index.find(next_ids[next_index]);
            if (previous == existing_index.end() ||
                previous->second >= title.live_text_rows.size())
                continue;
            for (const auto &cell : title.live_text_external_bindings) {
                if (cell.row_id != next_ids[next_index])
                    continue;
                const bool explicit_override = cell.table_binding_id.empty() ||
                                               !cell.binding.enabled;
                if (!explicit_override)
                    continue;
                const int cue_column = live_text_column_index(title, cell.layer_id);
                if (cue_column < 0 ||
                    static_cast<size_t>(cue_column) >= next_rows[next_index].size() ||
                    static_cast<size_t>(cue_column) >=
                        title.live_text_rows[previous->second].size())
                    continue;
                next_rows[next_index][static_cast<size_t>(cue_column)] =
                    title.live_text_rows[previous->second][static_cast<size_t>(cue_column)];
            }
        }

        std::set<std::string> surviving_ids(next_ids.begin(), next_ids.end());
        std::set<std::string> desired_ids;
        for (const auto &row : desired)
            desired_ids.insert(row.row_id);
        std::vector<LiveTextExternalBinding> next_bindings;
        next_bindings.reserve(title.live_text_external_bindings.size() +
                              desired.size() * mapping.columns.size());
        for (const auto &cell : title.live_text_external_bindings) {
            if (surviving_ids.find(cell.row_id) == surviving_ids.end())
                continue;
            if (cell.table_binding_id == mapping.id) {
                /* A disabled table-origin record is an explicit detached/authored
                 * cell. Preserve it so later refreshes cannot silently reattach
                 * the generated binding. */
                if (!cell.binding.enabled) {
                    next_bindings.push_back(cell);
                    continue;
                }
                /* Append mode retains source-managed rows that disappeared from
                 * the newest snapshot, including their last-known cell bindings.
                 * Rows still present are regenerated below so changed row indexes
                 * or column paths take effect immediately. */
                if (mapping.update_mode == LiveTextTableUpdateMode::AppendRows &&
                    desired_ids.find(cell.row_id) == desired_ids.end()) {
                    next_bindings.push_back(cell);
                }
                continue;
            }
            next_bindings.push_back(cell);
        }

        for (const auto &row : desired) {
            if (surviving_ids.find(row.row_id) == surviving_ids.end())
                continue;
            for (const auto &generated : row.generated_bindings) {
                const bool has_user_override = std::any_of(
                    next_bindings.begin(), next_bindings.end(),
                    [&generated](const LiveTextExternalBinding &candidate) {
                        return candidate.row_id == generated.row_id &&
                               candidate.layer_id == generated.layer_id &&
                               (candidate.table_binding_id.empty() ||
                                !candidate.binding.enabled);
                    });
                if (!has_user_override)
                    next_bindings.push_back(generated);
            }
        }

        title.live_text_rows = std::move(next_rows);
        title.live_text_row_ids = std::move(next_ids);
        title.live_text_external_bindings = std::move(next_bindings);
        ensure_table_row_ids(title);
    }

    const auto row_index_for_id = [&title](const std::string &row_id) {
        if (row_id.empty())
            return -1;
        const auto it = std::find(title.live_text_row_ids.begin(),
                                  title.live_text_row_ids.end(), row_id);
        return it == title.live_text_row_ids.end()
            ? -1 : static_cast<int>(it - title.live_text_row_ids.begin());
    };
    title.current_cue_row = row_index_for_id(active_row_id);
    title.pending_cue_row = row_index_for_id(pending_row_id);

    const bool changed = old_rows != title.live_text_rows ||
                         old_row_ids != title.live_text_row_ids ||
                         !cell_binding_vectors_equal(old_cell_bindings,
                                                     title.live_text_external_bindings);
    ExternalDataLog::write_lazy(
        changed ? ExternalDataLogLevel::Info : ExternalDataLogLevel::Trace,
        "TableMapping", [&]() {
            std::ostringstream stream;
            stream << "synchronize end title=" << ExternalDataLog::sanitize(title.id, 160)
                   << " changed=" << (changed ? 1 : 0)
                   << " rows=" << title.live_text_rows.size()
                   << " rowDelta=" << static_cast<long long>(title.live_text_rows.size()) -
                                          static_cast<long long>(old_rows.size())
                   << " cellBindings=" << title.live_text_external_bindings.size()
                   << " bindingDelta=" << static_cast<long long>(title.live_text_external_bindings.size()) -
                                             static_cast<long long>(old_cell_bindings.size())
                   << " currentCue=" << title.current_cue_row
                   << " pendingCue=" << title.pending_cue_row;
            return stream.str();
        });
    return changed;
}

bool remove_live_text_table_binding(Title &title, const std::string &binding_id,
                                    bool remove_managed_rows)
{
    if (binding_id.empty())
        return false;
    bool changed = false;
    const auto first = std::remove_if(
        title.live_text_table_bindings.begin(), title.live_text_table_bindings.end(),
        [&binding_id](const LiveTextTableBinding &mapping) {
            return mapping.id == binding_id;
        });
    if (first != title.live_text_table_bindings.end()) {
        title.live_text_table_bindings.erase(first,
                                             title.live_text_table_bindings.end());
        changed = true;
    }

    const std::string prefix = table_managed_row_prefix(binding_id);
    std::set<std::string> removed_rows;
    if (remove_managed_rows) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row_ids;
        for (size_t i = 0; i < title.live_text_rows.size(); ++i) {
            const std::string id = i < title.live_text_row_ids.size()
                ? title.live_text_row_ids[i] : std::string{};
            if (starts_with(id, prefix)) {
                removed_rows.insert(id);
                changed = true;
                continue;
            }
            rows.push_back(title.live_text_rows[i]);
            row_ids.push_back(id);
        }
        title.live_text_rows = std::move(rows);
        title.live_text_row_ids = std::move(row_ids);
    }

    const auto cell_first = std::remove_if(
        title.live_text_external_bindings.begin(),
        title.live_text_external_bindings.end(),
        [&binding_id, &removed_rows](const LiveTextExternalBinding &cell) {
            return cell.table_binding_id == binding_id ||
                   removed_rows.find(cell.row_id) != removed_rows.end();
        });
    if (cell_first != title.live_text_external_bindings.end()) {
        title.live_text_external_bindings.erase(
            cell_first, title.live_text_external_bindings.end());
        changed = true;
    }
    ensure_table_row_ids(title);
    return changed;
}
