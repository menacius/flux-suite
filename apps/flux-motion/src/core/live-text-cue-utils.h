#pragma once

#include "title-data.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/* Shared live-text cue structure contract. Dock editing, OBS hotkeys, source
 * playback and cache generation must agree on exposed-layer order and row
 * remapping; local copies of these helpers previously made that easy to break. */
namespace fxm::live_text {

inline bool chart_has_exposed_values(const Layer &layer)
{
    return layer.type == LayerType::Chart &&
        layer.chart_data_mode == ChartDataMode::StaticData &&
        std::any_of(layer.chart_values.begin(), layer.chart_values.end(),
                    [](const ChartValue &value) { return value.expose_in_dock; });
}

inline std::string chart_cue_value(const Layer &layer)
{
    std::ostringstream stream;
    stream << std::setprecision(15);
    bool first = true;
    for (const ChartValue &value : layer.chart_values) {
        if (!value.expose_in_dock)
            continue;
        if (!first)
            stream << '\n';
        first = false;
        stream << value.category;
        const auto series = std::find_if(
            layer.chart_series.begin(), layer.chart_series.end(),
            [&](const ChartSeries &item) { return item.id == value.series_id; });
        if (series != layer.chart_series.end() && !series->name.empty())
            stream << " / " << series->name;
        stream << " = " << value.value;
    }
    return stream.str();
}

inline bool apply_chart_cue_value(Layer &layer, const std::string &payload)
{
    if (layer.type != LayerType::Chart)
        return false;
    std::istringstream input(payload);
    std::string line;
    bool changed = false;
    for (ChartValue &value : layer.chart_values) {
        if (!value.expose_in_dock || !std::getline(input, line))
            continue;
        const std::size_t equals = line.rfind('=');
        std::string number = equals == std::string::npos ? line : line.substr(equals + 1);
        try {
            std::size_t consumed = 0;
            const double parsed = std::stod(number, &consumed);
            while (consumed < number.size() &&
                   std::isspace(static_cast<unsigned char>(number[consumed])))
                ++consumed;
            if (consumed == number.size() && std::isfinite(parsed)) {
                changed = changed || value.value != parsed;
                value.value = parsed;
            }
        } catch (...) {
        }
    }
    return changed;
}

inline bool is_exposed_cue_layer(const std::shared_ptr<Layer> &layer)
{
    if (!layer || layer->use_as_scene_mask)
        return false;

    const bool exposes_value = layer->expose_text &&
        (layer->type == LayerType::Text ||
         layer->type == LayerType::Ticker ||
         layer->type == LayerType::Image);
    const bool exposes_chart = chart_has_exposed_values(*layer);
    const bool exposes_color = layer->expose_fill_color || layer->expose_stroke_color;
    return exposes_value || exposes_chart || exposes_color;
}

inline std::vector<std::shared_ptr<Layer>> order_exposed_text_layers(
    const std::vector<std::shared_ptr<Layer>> &exposed,
    const std::vector<std::string> &column_order)
{
    if (column_order.empty())
        return exposed;

    std::vector<std::shared_ptr<Layer>> ordered;
    ordered.reserve(exposed.size());
    for (const auto &layer_id : column_order) {
        const auto it = std::find_if(
            exposed.begin(), exposed.end(),
            [&](const std::shared_ptr<Layer> &layer) {
                return layer && layer->id == layer_id;
            });
        if (it != exposed.end())
            ordered.push_back(*it);
    }
    for (const auto &layer : exposed) {
        if (!layer)
            continue;
        const auto it = std::find_if(
            ordered.begin(), ordered.end(),
            [&](const std::shared_ptr<Layer> &ordered_layer) {
                return ordered_layer && ordered_layer->id == layer->id;
            });
        if (it == ordered.end())
            ordered.push_back(layer);
    }
    return ordered;
}

inline std::vector<std::shared_ptr<Layer>> exposed_text_layers(
    const Title &title)
{
    std::vector<std::shared_ptr<Layer>> exposed;
    for (const auto &layer : title.layers) {
        if (is_exposed_cue_layer(layer))
            exposed.push_back(layer);
    }
    return order_exposed_text_layers(exposed, title.live_text_column_order);
}

inline std::vector<std::shared_ptr<Layer>> exposed_text_layers(
    const std::shared_ptr<Title> &title)
{
    return title ? exposed_text_layers(*title)
                 : std::vector<std::shared_ptr<Layer>>{};
}

inline std::string live_cue_layer_value(
    const std::shared_ptr<Layer> &layer)
{
    if (!layer)
        return {};
    if (layer->type == LayerType::Image)
        return layer->image_path;
    if (layer->type == LayerType::Chart)
        return chart_cue_value(*layer);
    return layer->expose_text ? layer->text_content : std::string{};
}

inline void normalize_live_text_rows(
    const std::shared_ptr<Title> &title,
    const std::vector<std::shared_ptr<Layer>> &exposed)
{
    if (!title || exposed.empty())
        return;

    std::vector<std::string> new_order;
    new_order.reserve(exposed.size());
    for (const auto &layer : exposed)
        new_order.push_back(layer ? layer->id : std::string());

    const std::vector<std::string> old_order = title->live_text_column_order;
    if (!old_order.empty() && old_order != new_order) {
        for (auto &row : title->live_text_rows) {
            std::vector<std::string> remapped;
            remapped.reserve(exposed.size());
            for (std::size_t new_col = 0; new_col < new_order.size(); ++new_col) {
                const auto it = std::find(old_order.begin(), old_order.end(),
                                          new_order[new_col]);
                if (it != old_order.end()) {
                    const std::size_t old_col = static_cast<std::size_t>(
                        std::distance(old_order.begin(), it));
                    remapped.push_back(old_col < row.size()
                        ? row[old_col]
                        : live_cue_layer_value(exposed[new_col]));
                } else {
                    remapped.push_back(live_cue_layer_value(exposed[new_col]));
                }
            }
            row = std::move(remapped);
        }
    }
    title->live_text_column_order = std::move(new_order);

    if (title->live_text_rows.empty()) {
        std::vector<std::string> row;
        row.reserve(exposed.size());
        for (const auto &layer : exposed)
            row.push_back(live_cue_layer_value(layer));
        title->live_text_rows.push_back(std::move(row));
    }
    for (auto &row : title->live_text_rows) {
        const std::size_t old_size = row.size();
        row.resize(exposed.size());
        for (std::size_t i = old_size; i < exposed.size(); ++i)
            row[i] = live_cue_layer_value(exposed[i]);
    }
    for (std::size_t col = 0; col < exposed.size(); ++col) {
        if (!exposed[col] || !exposed[col]->exposed_single_value ||
            title->live_text_rows.empty())
            continue;
        const std::string shared_value =
            col < title->live_text_rows.front().size()
                ? title->live_text_rows.front()[col]
                : live_cue_layer_value(exposed[col]);
        for (auto &row : title->live_text_rows) {
            if (col < row.size())
                row[col] = shared_value;
        }
    }
    ensure_live_text_row_ids(*title);
    prune_live_text_cue_style_overrides(*title);

    /* new_order has already been moved into live_text_column_order above.
     * Never validate bindings against the moved-from local vector: on the
     * common STL implementations it is empty, which deleted every generated
     * table-cell binding immediately before the dock rebuilt its widgets. */
    title->live_text_external_bindings.erase(
        std::remove_if(title->live_text_external_bindings.begin(),
                       title->live_text_external_bindings.end(),
            [&title](const LiveTextExternalBinding &cell) {
                const bool row_exists = std::find(title->live_text_row_ids.begin(),
                                                  title->live_text_row_ids.end(),
                                                  cell.row_id) != title->live_text_row_ids.end();
                const bool layer_exists = std::find(
                    title->live_text_column_order.begin(),
                    title->live_text_column_order.end(),
                    cell.layer_id) != title->live_text_column_order.end();
                return !row_exists || !layer_exists;
            }),
        title->live_text_external_bindings.end());
}

} // namespace fxm::live_text
