#include "chart-data.h"
#include "external-data.h"
#include "live-text-cue-utils.h"

#include <cmath>
#include <iostream>
#include <string>
#include <utility>

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "Chart data runtime failure: " << message << '\n';
    return false;
}

ExternalDataTableRow city_row(const std::string &city, int value_2025,
                              int value_2026, double longitude)
{
    ExternalDataTableRow row;
    row.key = city;
    row.values["name"] = ExternalDataValue::string(city);
    row.values["2025"] = ExternalDataValue::integer(value_2025);
    row.values["2026"] = ExternalDataValue::integer(value_2026);
    row.values["longitude"] = ExternalDataValue::floating(longitude);
    row.values["metadata"] = ExternalDataValue::string("region:" + city);
    row.values["color"] = ExternalDataValue::string(
        city == "Athens" ? "#12AB34" : "#D9485F");
    return row;
}

} // namespace

int main()
{
    bool ok = true;
    auto &manager = ExternalDataManager::instance();
    const std::string source_id = "chart-runtime-source";
    manager.unregister_source(source_id);

    /* The chart registers no provider of its own. A provider-neutral table
     * snapshot is enough to drive the complete mapping path. */
    ExternalDataTableSnapshot table;
    table.path = "cities";
    table.columns = {"name", "2025", "2026", "longitude", "metadata", "color"};
    table.rows.push_back(city_row("Athens", 42, 51, 23.7275));
    table.rows.push_back(city_row("Patras", 18, 24, 21.7346));
    ExternalDataTableRow empty_row;
    empty_row.key = "Larissa";
    empty_row.values["name"] = ExternalDataValue::string("Larissa");
    table.rows.push_back(std::move(empty_row));
    ok &= expect(manager.update_table(source_id, table, 100),
                 "provider table snapshot is accepted");

    Layer chart;
    chart.id = "chart-runtime-layer";
    chart.type = LayerType::Chart;
    chart.chart_binding.source_id = source_id;
    chart.chart_binding.table_path = "cities";
    chart.chart_binding.category_field = "name";
    chart.chart_binding.secondary_value_field = "longitude";
    chart.chart_binding.metadata_field = "metadata";
    chart.chart_binding.color_field = "color";
    ChartSeries current;
    current.id = "2025";
    current.name = "2025";
    current.value_field = "2025";
    current.color = 0xFF4C8DFFu;
    ChartSeries forecast = current;
    forecast.id = "2026";
    forecast.name = "2026";
    forecast.value_field = "2026";
    forecast.color = 0xFFFF8A3Du;
    chart.chart_series = {current, forecast};

    const fxm::chart::DataSet mapped = fxm::chart::map_layer_data(chart);
    ok &= expect(mapped.source_available, "mapped table is marked available");
    ok &= expect(mapped.categories.size() == 2 && mapped.values.size() == 4,
                 "two categories and two series map to four points");
    ok &= expect(mapped.values[0].category == "Athens" &&
                     mapped.values[0].value == 42.0 &&
                     mapped.values[0].has_secondary_value &&
                     std::abs(mapped.values[0].secondary_value - 23.7275) < 1.0e-9 &&
                     mapped.values[0].metadata == "region:Athens" &&
                     mapped.values[0].color == 0xFF12AB34u,
                 "category, primary, secondary, metadata, and color mappings survive");
    Layer include_empty = chart;
    include_empty.id = "chart-runtime-include-empty";
    include_empty.chart_binding.ignore_empty_rows = false;
    const auto zeros = fxm::chart::map_layer_data(include_empty);
    ok &= expect(zeros.categories.size() == 3 && zeros.values.size() == 6 &&
                     zeros.values.back().value == 0.0,
                 "disabled empty-row filtering maps missing values as zero");

    const std::string before = fxm::chart::data_fingerprint(chart);
    ExternalDataTableSnapshot unrelated = table;
    unrelated.path = "unrelated";
    unrelated.rows[0].values["2025"] = ExternalDataValue::integer(999);
    ok &= expect(manager.update_table(source_id, unrelated, 200),
                 "unrelated table can update");
    ok &= expect(fxm::chart::data_fingerprint(chart) == before,
                 "unrelated provider table does not invalidate chart data");

    chart.chart_data_transition_seconds = 1.0;
    fxm::chart::clear_transition_state(chart.id);
    (void)fxm::chart::resolved_layer_data(chart, 0.0, 1000);
    const auto settled = fxm::chart::resolved_layer_data(chart, 0.0, 2000);
    ok &= expect(std::abs(settled.values[0].value - 42.0) < 1.0e-9,
                 "initial live-data transition reaches its target");

    table.rows[0].values["2025"] = ExternalDataValue::integer(62);
    ok &= expect(manager.update_table(source_id, table, 2100),
                 "changed mapped value publishes");
    const auto transition_start = fxm::chart::resolved_layer_data(chart, 0.0, 2100);
    const auto transition_mid = fxm::chart::resolved_layer_data(chart, 0.0, 2600);
    ok &= expect(std::abs(transition_start.values[0].value - 42.0) < 1.0e-9,
                 "data transition begins at the previous value");
    ok &= expect(std::abs(transition_mid.values[0].value - 52.0) < 1.0e-9,
                 "data transition interpolates incoming values");

    chart.chart_value_transition.static_value = 0.5;
    const auto authored_half = fxm::chart::resolved_layer_data(chart, 0.0, 3100);
    ok &= expect(std::abs(authored_half.values[0].value - 31.0) < 1.0e-9,
                 "timeline value-transition property composes with live data");

    table.rows[0].values["2025"] = ExternalDataValue::integer(80);
    ok &= expect(manager.update_table(source_id, table, 3200),
                 "second changed mapped value publishes");
    {
        fxm::chart::ScopedDeterministicDataResolution prerender;
        const auto deterministic = fxm::chart::resolved_layer_data(
            chart, 0.0, 3200);
        ok &= expect(std::abs(deterministic.values[0].value - 40.0) < 1.0e-9,
                     "prerender resolves provider data without wall-clock transition");
    }

    Layer static_chart;
    static_chart.id = "static-chart";
    static_chart.type = LayerType::Chart;
    static_chart.chart_data_mode = ChartDataMode::StaticData;
    ChartSeries static_series;
    static_series.id = "audience";
    static_series.name = "Audience";
    static_chart.chart_series.push_back(static_series);
    ChartValue static_value;
    static_value.id = "athens-audience";
    static_value.category = "Athens";
    static_value.series_id = "audience";
    static_value.value = 73.5;
    static_value.color_override = true;
    static_value.color = 0xFF7C3AEDu;
    static_value.expose_in_dock = true;
    static_chart.chart_values.push_back(static_value);
    auto static_data = fxm::chart::map_layer_data(static_chart);
    ok &= expect(static_data.source_available && static_data.values.size() == 1 &&
                     std::abs(static_data.values.front().value - 73.5) < 1.0e-9 &&
                     static_data.values.front().color == 0xFF7C3AEDu,
                 "static values and per-value colors map without a provider");
    const auto static_prerender = fxm::chart::resolved_layer_data(
        static_chart, 0.0, 1000);
    ok &= expect(std::abs(static_prerender.values.front().value - 73.5) < 1.0e-9,
                 "static chart data is immediately deterministic for prerender");
    const std::string dock_payload = fxm::live_text::chart_cue_value(static_chart);
    ok &= expect(dock_payload == "Athens / Audience = 73.5",
                 "exposed chart value has a readable OBS dock payload");
    ok &= expect(fxm::live_text::apply_chart_cue_value(
                     static_chart, "Athens / Audience = 91.25") &&
                     std::abs(static_chart.chart_values.front().value - 91.25) < 1.0e-9,
                 "OBS cue payload updates only the exposed static value");

    manager.unregister_source(source_id);
    fxm::chart::clear_transition_state(chart.id);
    fxm::chart::clear_transition_state(include_empty.id);
    return ok ? 0 : 1;
}
