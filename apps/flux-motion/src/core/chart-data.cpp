#include "chart-data.h"

#include "external-data.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <functional>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace fxm::chart {
namespace {

double number_value(const ExternalDataValue &value, bool *valid = nullptr)
{
    bool ok = value.is_set;
    double result = 0.0;
    if (ok) {
        switch (value.type) {
        case ExternalDataType::Integer:
            result = static_cast<double>(value.integer_value);
            break;
        case ExternalDataType::Float:
            result = value.float_value;
            break;
        case ExternalDataType::Boolean:
            result = value.boolean_value ? 1.0 : 0.0;
            break;
        default:
            try {
                std::size_t consumed = 0;
                result = std::stod(value.string_value, &consumed);
                ok = consumed == value.string_value.size();
            } catch (...) {
                ok = false;
            }
            break;
        }
    }
    ok = ok && std::isfinite(result);
    if (valid)
        *valid = ok;
    return ok ? result : 0.0;
}

std::string string_value(const ExternalDataValue &value)
{
    return value.is_set ? external_data_value_to_string(value) : std::string();
}

uint32_t color_value(const ExternalDataValue &value, uint32_t fallback,
                     bool *valid = nullptr)
{
    bool ok = value.is_set;
    uint32_t result = fallback;
    if (ok && value.type == ExternalDataType::Integer) {
        result = static_cast<uint32_t>(value.integer_value);
        if (result <= 0x00FFFFFFu)
            result |= 0xFF000000u;
    } else if (ok) {
        std::string text = string_value(value);
        text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), text.end());
        if (!text.empty() && text.front() == '#')
            text.erase(text.begin());
        else if (text.size() > 2 && text[0] == '0' &&
                 (text[1] == 'x' || text[1] == 'X'))
            text.erase(0, 2);
        if (text.size() != 6 && text.size() != 8) {
            ok = false;
        } else {
            uint32_t parsed = 0;
            const auto converted = std::from_chars(
                text.data(), text.data() + text.size(), parsed, 16);
            ok = converted.ec == std::errc{} &&
                 converted.ptr == text.data() + text.size();
            if (ok)
                result = text.size() == 6 ? (parsed | 0xFF000000u) : parsed;
        }
    }
    if (valid)
        *valid = ok;
    return ok ? result : fallback;
}

const ExternalDataValue *row_value(const ExternalDataTableRow &row,
                                   const std::string &field)
{
    if (field.empty())
        return nullptr;
    auto direct = row.values.find(field);
    if (direct != row.values.end())
        return &direct->second;
    /* Table providers expose canonical paths as well as column labels. Honor
     * the provider path map so chart bindings use the same field convention as
     * Live Text table mappings. */
    for (const auto &entry : row.source_field_paths) {
        if (entry.second == field) {
            auto value = row.values.find(entry.first);
            if (value != row.values.end())
                return &value->second;
        }
    }
    return nullptr;
}

int64_t monotonic_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string datum_key(const Datum &datum)
{
    return datum.series_id + "\x1f" + datum.category;
}

std::string mapped_fingerprint(const DataSet &data)
{
    std::ostringstream stream;
    stream << data.source_available << '|' << data.categories.size() << '|'
           << data.series.size() << '|';
    stream << std::setprecision(17);
    for (const auto &series : data.series)
        stream << series.id << ':' << series.name << ':' << series.visible << ':'
               << series.color << ':' << series.fill_color << '|';
    for (const Datum &datum : data.values)
        stream << datum.series_id << ':' << datum.category << ':' << datum.value
               << ':' << datum.has_secondary_value << ':' << datum.secondary_value
               << ':' << datum.metadata << ':' << datum.color << ':'
               << datum.fill_color << '|';
    return std::to_string(std::hash<std::string>{}(stream.str()));
}

struct TransitionEntry {
    DataSet previous;
    DataSet target;
    std::string target_fingerprint;
    int64_t started_ms = 0;
};

struct MappingCacheEntry {
    std::string input_fingerprint;
    DataSet data;
};

std::string mapping_input_fingerprint(const Layer &layer,
                                      const ExternalDataTableSnapshot &table)
{
    std::ostringstream stream;
    stream << std::setprecision(17)
           << static_cast<int>(layer.chart_data_mode) << '|'
           << layer.chart_binding.source_id << '|' << layer.chart_binding.table_path << '|'
           << layer.chart_binding.category_field << '|'
           << layer.chart_binding.series_field << '|'
           << layer.chart_binding.secondary_value_field << '|'
           << layer.chart_binding.metadata_field << '|'
           << layer.chart_binding.color_field << '|'
           << layer.chart_binding.start_row << '|'
           << layer.chart_binding.maximum_rows << '|'
           << layer.chart_binding.ignore_empty_rows << '|'
           << (!table.columns.empty() || !table.rows.empty()) << '|';
    for (const ChartSeries &series : layer.chart_series) {
        stream << series.id << ':' << series.name << ':' << series.value_field << ':'
               << series.secondary_value_field << ':' << series.metadata_field << ':'
               << series.color << ':' << series.fill_color << ':' << series.visible << ':'
               << series.fill_enabled << ':' << series.outline_enabled << ':'
               << series.line_thickness << '|';
    }
    for (const ChartValue &value : layer.chart_values) {
        stream << value.id << ':' << value.category << ':' << value.series_id << ':'
               << value.value << ':' << value.has_secondary_value << ':'
               << value.secondary_value << ':' << value.metadata << ':'
               << value.color_override << ':' << value.color << ':'
               << value.expose_in_dock << '|';
    }
    const int first = std::clamp(layer.chart_binding.start_row, 0,
                                 static_cast<int>(table.rows.size()));
    const int available = static_cast<int>(table.rows.size()) - first;
    const int count = layer.chart_binding.maximum_rows > 0
        ? std::min(available, layer.chart_binding.maximum_rows) : available;
    auto append_field = [&](const ExternalDataTableRow &row,
                            const std::string &field) {
        stream << field.size() << ':' << field << '=';
        if (const ExternalDataValue *value = row_value(row, field))
            stream << static_cast<int>(value->type) << ':' << value->is_set << ':'
                   << external_data_value_to_string(*value);
        stream << ';';
    };
    for (int row_index = first; row_index < first + count; ++row_index) {
        const ExternalDataTableRow &row = table.rows[static_cast<std::size_t>(row_index)];
        stream << row.key.size() << ':' << row.key << '|';
        append_field(row, layer.chart_binding.category_field);
        append_field(row, layer.chart_binding.series_field);
        append_field(row, layer.chart_binding.secondary_value_field);
        append_field(row, layer.chart_binding.metadata_field);
        append_field(row, layer.chart_binding.color_field);
        for (const ChartSeries &series : layer.chart_series) {
            append_field(row, series.value_field);
            append_field(row, series.secondary_value_field);
            append_field(row, series.metadata_field);
        }
        if (layer.chart_series.empty())
            append_field(row, "value");
    }
    return std::to_string(std::hash<std::string>{}(stream.str()));
}

std::mutex g_transition_mutex;
std::unordered_map<std::string, TransitionEntry> g_transition_states;
std::mutex g_mapping_cache_mutex;
std::unordered_map<std::string, MappingCacheEntry> g_mapping_cache;
thread_local unsigned g_deterministic_resolution_depth = 0;

DataSet interpolate(const DataSet &from, const DataSet &to, double amount)
{
    DataSet result = to;
    amount = std::clamp(amount, 0.0, 1.0);
    std::unordered_map<std::string, const Datum *> old;
    old.reserve(from.values.size());
    for (const Datum &datum : from.values)
        old.emplace(datum_key(datum), &datum);
    for (Datum &datum : result.values) {
        auto previous = old.find(datum_key(datum));
        const double old_value = previous == old.end() ? 0.0 : previous->second->value;
        datum.value = old_value + (datum.value - old_value) * amount;
        if (datum.has_secondary_value) {
            const double old_secondary = previous == old.end() ||
                    !previous->second->has_secondary_value
                ? 0.0 : previous->second->secondary_value;
            datum.secondary_value = old_secondary +
                (datum.secondary_value - old_secondary) * amount;
        }
    }
    return result;
}

} // namespace

DataSet map_layer_data(const Layer &layer)
{
    DataSet result;
    result.source_revision = ExternalDataManager::instance().revision();
    if (layer.type != LayerType::Chart)
        return result;

    if (layer.chart_data_mode == ChartDataMode::StaticData) {
        result.source_available = !layer.chart_values.empty();
        result.series = layer.chart_series;
        if (result.series.empty()) {
            ChartSeries series;
            series.id = "value";
            series.name = "Value";
            series.value_field = "value";
            result.series.push_back(std::move(series));
        }
        for (const ChartValue &value : layer.chart_values) {
            const auto series_it = std::find_if(
                result.series.begin(), result.series.end(),
                [&](const ChartSeries &series) {
                    return series.id == value.series_id || series.name == value.series_id;
                });
            if (series_it == result.series.end() || !series_it->visible)
                continue;
            Datum datum;
            datum.category = value.category;
            datum.series_id = series_it->id.empty() ? series_it->name : series_it->id;
            datum.series_name = series_it->name;
            datum.value = std::isfinite(value.value) ? value.value : 0.0;
            datum.secondary_value = std::isfinite(value.secondary_value)
                ? value.secondary_value : 0.0;
            datum.has_secondary_value = value.has_secondary_value;
            datum.metadata = value.metadata;
            datum.color = value.color_override ? value.color : series_it->color;
            datum.fill_color = value.color_override ? value.color : series_it->fill_color;
            datum.fill_enabled = series_it->fill_enabled;
            datum.outline_enabled = series_it->outline_enabled;
            datum.line_thickness = series_it->line_thickness;
            result.values.push_back(std::move(datum));
            if (std::find(result.categories.begin(), result.categories.end(),
                          value.category) == result.categories.end())
                result.categories.push_back(value.category);
        }
        return result;
    }

    if (layer.chart_binding.source_id.empty())
        return result;

    const ExternalDataTableSnapshot table =
        ExternalDataManager::instance().table_snapshot(
            layer.chart_binding.source_id, layer.chart_binding.table_path);
    const std::string mapping_fingerprint = mapping_input_fingerprint(layer, table);
    if (!layer.id.empty()) {
        std::lock_guard<std::mutex> lock(g_mapping_cache_mutex);
        const auto found = g_mapping_cache.find(layer.id);
        if (found != g_mapping_cache.end() &&
            found->second.input_fingerprint == mapping_fingerprint) {
            result = found->second.data;
            result.source_revision = ExternalDataManager::instance().revision();
            return result;
        }
    }
    result.source_timestamp_ms = table.last_update_timestamp_ms;
    result.source_available = !table.columns.empty() || !table.rows.empty();
    result.series = layer.chart_series;
    if (result.series.empty()) {
        ChartSeries series;
        series.id = "value";
        series.name = "Value";
        series.value_field = "value";
        result.series.push_back(std::move(series));
    }

    const int first = std::clamp(layer.chart_binding.start_row, 0,
                                 static_cast<int>(table.rows.size()));
    const int available = static_cast<int>(table.rows.size()) - first;
    const int count = layer.chart_binding.maximum_rows > 0
        ? std::min(available, layer.chart_binding.maximum_rows) : available;
    for (int row_index = first; row_index < first + count; ++row_index) {
        const ExternalDataTableRow &row = table.rows[static_cast<std::size_t>(row_index)];
        std::string category = row.key;
        if (const auto *value = row_value(row, layer.chart_binding.category_field))
            category = string_value(*value);
        if (category.empty())
            category = std::to_string(row_index + 1);

        bool emitted = false;
        for (const ChartSeries &series : result.series) {
            if (!series.visible)
                continue;
            if (!layer.chart_binding.series_field.empty()) {
                const auto *series_value = row_value(row, layer.chart_binding.series_field);
                if (series_value && string_value(*series_value) != series.name &&
                    string_value(*series_value) != series.id)
                    continue;
            }
            bool valid = false;
            const ExternalDataValue *mapped = row_value(row, series.value_field);
            const double number = mapped ? number_value(*mapped, &valid) : 0.0;
            if (!valid && layer.chart_binding.ignore_empty_rows)
                continue;
            Datum datum;
            datum.category = category;
            datum.series_id = series.id.empty() ? series.name : series.id;
            datum.series_name = series.name;
            datum.value = number;
            datum.color = series.color;
            datum.fill_color = series.fill_color;
            datum.fill_enabled = series.fill_enabled;
            datum.outline_enabled = series.outline_enabled;
            datum.line_thickness = series.line_thickness;
            if (const auto *mapped_color = row_value(
                    row, layer.chart_binding.color_field)) {
                bool valid_color = false;
                const uint32_t parsed = color_value(
                    *mapped_color, datum.color, &valid_color);
                if (valid_color) {
                    datum.color = parsed;
                    datum.fill_color = parsed;
                }
            }
            const std::string secondary_field = series.secondary_value_field.empty()
                ? layer.chart_binding.secondary_value_field
                : series.secondary_value_field;
            if (const auto *secondary = row_value(row, secondary_field)) {
                datum.secondary_value = number_value(*secondary,
                                                     &datum.has_secondary_value);
            }
            const std::string metadata_field = series.metadata_field.empty()
                ? layer.chart_binding.metadata_field : series.metadata_field;
            if (const auto *metadata = row_value(row, metadata_field))
                datum.metadata = string_value(*metadata);
            result.values.push_back(std::move(datum));
            emitted = true;
        }
        if (emitted && std::find(result.categories.begin(), result.categories.end(),
                                 category) == result.categories.end())
            result.categories.push_back(std::move(category));
    }
    if (!layer.id.empty()) {
        std::lock_guard<std::mutex> lock(g_mapping_cache_mutex);
        g_mapping_cache[layer.id] = {mapping_fingerprint, result};
    }
    return result;
}

DataSet resolved_layer_data(const Layer &layer, double local_time, int64_t now_ms)
{
    DataSet mapped = map_layer_data(layer);
    const double authored_amount = std::clamp(
        layer.chart_value_transition.evaluate(local_time), 0.0, 1.0);
    if (layer.chart_data_mode == ChartDataMode::StaticData ||
        g_deterministic_resolution_depth > 0)
        return interpolate(DataSet{}, mapped, authored_amount);
    if (layer.id.empty()) {
        return interpolate(DataSet{}, mapped, authored_amount);
    }
    const int64_t clock = now_ms > 0 ? now_ms : monotonic_ms();
    const std::string fingerprint = mapped_fingerprint(mapped);
    std::lock_guard<std::mutex> lock(g_transition_mutex);
    TransitionEntry &entry = g_transition_states[layer.id];
    if (entry.target_fingerprint.empty()) {
        entry.target = mapped;
        entry.target_fingerprint = fingerprint;
        entry.started_ms = clock;
    } else if (entry.target_fingerprint != fingerprint) {
        const double elapsed = std::max<int64_t>(0, clock - entry.started_ms) / 1000.0;
        const double duration = std::max(0.0, layer.chart_data_transition_seconds);
        const double current_amount = duration <= 0.000001 ? 1.0
            : std::clamp(elapsed / duration, 0.0, 1.0);
        entry.previous = interpolate(entry.previous, entry.target, current_amount);
        entry.target = mapped;
        entry.target_fingerprint = fingerprint;
        entry.started_ms = clock;
    }
    const double elapsed = std::max<int64_t>(0, clock - entry.started_ms) / 1000.0;
    const double duration = std::max(0.0, layer.chart_data_transition_seconds);
    const double live_amount = duration <= 0.000001 ? 1.0
        : std::clamp(elapsed / duration, 0.0, 1.0);
    DataSet live = interpolate(entry.previous, entry.target, live_amount);
    if (live_amount >= 1.0)
        entry.previous = entry.target;
    return interpolate(DataSet{}, live, authored_amount);
}

ScopedDeterministicDataResolution::ScopedDeterministicDataResolution()
{
    ++g_deterministic_resolution_depth;
}

ScopedDeterministicDataResolution::~ScopedDeterministicDataResolution()
{
    if (g_deterministic_resolution_depth > 0)
        --g_deterministic_resolution_depth;
}

std::string data_fingerprint(const Layer &layer)
{
    return mapped_fingerprint(map_layer_data(layer));
}

bool transition_active(const Layer &layer, int64_t now_ms)
{
    if (layer.type != LayerType::Chart || layer.id.empty() ||
        layer.chart_data_mode != ChartDataMode::ExternalData ||
        layer.chart_data_transition_seconds <= 0.000001)
        return false;
    const std::string current = mapped_fingerprint(map_layer_data(layer));
    const int64_t clock = now_ms > 0 ? now_ms : monotonic_ms();
    std::lock_guard<std::mutex> lock(g_transition_mutex);
    const auto found = g_transition_states.find(layer.id);
    if (found == g_transition_states.end() ||
        found->second.target_fingerprint != current)
        return true;
    return clock - found->second.started_ms < static_cast<int64_t>(
        std::ceil(layer.chart_data_transition_seconds * 1000.0));
}

void clear_transition_state(const std::string &layer_id)
{
    {
        std::lock_guard<std::mutex> lock(g_transition_mutex);
        if (layer_id.empty())
            g_transition_states.clear();
        else
            g_transition_states.erase(layer_id);
    }
    {
        std::lock_guard<std::mutex> lock(g_mapping_cache_mutex);
        if (layer_id.empty())
            g_mapping_cache.clear();
        else
            g_mapping_cache.erase(layer_id);
    }
}

} // namespace fxm::chart
