#include "external-data.h"
#include "layer-model.h"
#include "live-text-cue-utils.h"
#include "title-data.h"
#include "live-text-cue-style-prune-test-support.h"

#include <iostream>
#include <string>

/* The production implementation lives in title-data.cpp, which pulls the Qt
 * editor extension catalog into this intentionally core-only runtime test. A
 * minimal test definition is sufficient for exercising the inline cue
 * normalization contract without adding Qt to this target. */
void ensure_live_text_row_ids(Title &title)
{
    if (title.live_text_row_ids.size() > title.live_text_rows.size())
        title.live_text_row_ids.resize(title.live_text_rows.size());
    while (title.live_text_row_ids.size() < title.live_text_rows.size())
        title.live_text_row_ids.push_back({});
    for (std::size_t i = 0; i < title.live_text_row_ids.size(); ++i) {
        if (title.live_text_row_ids[i].empty())
            title.live_text_row_ids[i] = "runtime-test-row-" + std::to_string(i);
    }
}

namespace {

bool expect(bool condition, const char *message)
{
    if (condition)
        return true;
    std::cerr << "External data runtime failure: " << message << '\n';
    return false;
}

} // namespace

int main()
{
    bool ok = true;
    auto &manager = ExternalDataManager::instance();
    const std::string source_id = "external-data-runtime-test";
    const std::string field_path = "score.home";
    manager.unregister_source(source_id);

    ExternalDataSourceDefinition source;
    source.id = source_id;
    source.name = "Runtime Test";
    ExternalDataFieldDefinition field;
    field.path = field_path;
    field.name = "Home score";
    field.type = ExternalDataType::String;
    field.has_default_value = true;
    field.default_value = ExternalDataValue::string("DEFAULT");
    source.fields.push_back(field);
    manager.register_source(source);

    Layer layer;
    layer.type = LayerType::Text;
    layer.text_content = "AUTHORED";
    ExternalPropertyBinding binding;
    binding.property_path = "text.content";
    binding.source_id = source_id;
    binding.field_path = field_path;
    binding.formatter = "[{value}]";
    binding.has_fallback_value = true;
    binding.fallback_value = ExternalDataValue::string("FALLBACK");
    layer.external_bindings.push_back(binding);

    bool live = false;
    bool fallback = false;
    const ExternalDataResolution fallback_resolution = manager.resolve(
        binding, ExternalDataValue::string(layer.text_content));
    ok &= expect(fallback_resolution.origin ==
                     ExternalDataValueOrigin::BindingFallback,
                 "resolution explicitly distinguishes binding fallback");
    ExternalDataValue resolved = manager.resolve_value(
        binding, ExternalDataValue::string(layer.text_content), &live, &fallback);
    ok &= expect(!live && fallback, "disconnected source selects binding fallback");
    ok &= expect(external_data_value_to_string(resolved) == "FALLBACK",
                 "binding fallback value is preserved");
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "[FALLBACK]",
                 "formatter applies to fallback without mutating authored text");
    ok &= expect(layer.text_content == "AUTHORED", "authored text remains untouched");

    ok &= expect(manager.set_connection_state(
                     source_id, ExternalDataConnectionState::Connected, {}, 1000),
                 "connection transition publishes once");
    ok &= expect(manager.update_value(
                     source_id, field_path, ExternalDataValue::string("LIVE"), 1100),
                 "first live value publishes");
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "[LIVE]",
                 "connected current value reaches effective text");
    ok &= expect(manager.resolve(
                     binding, ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::LiveExternal,
                 "resolution explicitly distinguishes live external value");

    (void)manager.take_render_updates();
    const uint64_t before_same_revision = manager.revision();
    ok &= expect(!manager.update_value(
                     source_id, field_path, ExternalDataValue::string("LIVE"), 1200),
                 "same value is suppressed");
    ok &= expect(manager.revision() == before_same_revision,
                 "same value does not dirty runtime revision");
    ok &= expect(manager.take_render_updates().empty(),
                 "same value does not enqueue render work");
    const auto same_value_state = manager.source_state(source_id);
    const auto same_value_field = same_value_state.fields.find(field_path);
    ok &= expect(same_value_field != same_value_state.fields.end() &&
                     same_value_field->second.last_update_timestamp_ms == 1200,
                 "same-value receipt updates timestamp without render dirtying");

    ok &= expect(manager.update_value(
                     source_id, field_path, ExternalDataValue::string("LIVE 2"), 1300),
                 "changed value publishes");
    ok &= expect(manager.update_value(
                     source_id, field_path, ExternalDataValue::string("LIVE 3"), 1400),
                 "second changed value publishes");
    const auto coalesced = manager.take_render_updates();
    ok &= expect(coalesced.size() == 1,
                 "render queue coalesces repeated field updates");
    ok &= expect(!coalesced.empty() && coalesced.front().timestamp_ms == 1400,
                 "render queue retains newest field update");

    ok &= expect(manager.set_connection_state(
                     source_id, ExternalDataConnectionState::Error,
                     "provider unavailable", 1500),
                 "error state publishes");
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "[FALLBACK]",
                 "lost/error source returns to fallback");
    const auto state = manager.source_state(source_id);
    ok &= expect(state.connection_state == ExternalDataConnectionState::Error,
                 "connection/error state is retained");
    ok &= expect(state.error_message == "provider unavailable",
                 "error message is retained");
    ExternalPropertyBinding default_binding = binding;
    default_binding.has_fallback_value = false;
    layer.external_bindings.front() = default_binding;
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "[DEFAULT]",
                 "field default is used when binding fallback is absent");
    ok &= expect(manager.resolve(
                     default_binding,
                     ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::FieldDefault,
                 "resolution explicitly distinguishes field default");

    ExternalPropertyBinding missing_binding = default_binding;
    missing_binding.source_id = "missing-source";
    ok &= expect(manager.resolve(
                     missing_binding,
                     ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::Authored,
                 "resolution explicitly distinguishes authored value");

    ok &= expect(!manager.update_value(
                     source_id, field_path, ExternalDataValue::integer(42), 1600),
                 "typed field rejects incompatible provider value");

    const std::string retained_id = "external-data-last-known-test";
    manager.unregister_source(retained_id);
    ExternalDataSourceDefinition retained_source = source;
    retained_source.id = retained_id;
    retained_source.provider.type = ExternalDataProviderType::HttpJson;
    retained_source.provider.keep_last_value = true;
    manager.register_source(retained_source);
    ok &= expect(manager.set_connection_state(
                     retained_id, ExternalDataConnectionState::Connected, {}, 1610),
                 "last-known source connects");
    ok &= expect(manager.update_value(
                     retained_id, field_path, ExternalDataValue::string("RETAINED"), 1620),
                 "last-known source receives a value");
    ExternalPropertyBinding retained_binding = binding;
    retained_binding.source_id = retained_id;
    ok &= expect(manager.set_connection_state(
                     retained_id, ExternalDataConnectionState::Stale,
                     "provider data is stale", 1630),
                 "provider can enter stale state");
    ok &= expect(manager.resolve(
                     retained_binding,
                     ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::LiveExternal,
                 "stale provider retains the last valid value when enabled");
    (void)manager.take_render_updates();
    const uint64_t before_retained_error = manager.revision();
    ok &= expect(manager.set_connection_state(
                     retained_id, ExternalDataConnectionState::Error,
                     "temporary endpoint failure", 1635),
                 "last-known source records an informative error");
    ok &= expect(manager.revision() == before_retained_error &&
                     manager.take_render_updates().empty(),
                 "last-known stale-to-error transition does not dirty rendering");
    ok &= expect(manager.resolve(
                     retained_binding,
                     ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::LiveExternal,
                 "last-known value remains effective while provider reports an error");
    retained_source.provider.keep_last_value = false;
    manager.register_source(retained_source);
    ok &= expect(manager.resolve(
                     retained_binding,
                     ExternalDataValue::string(layer.text_content)).origin ==
                     ExternalDataValueOrigin::BindingFallback,
                 "stale provider uses fallback when last-known values are disabled");

    const std::string mock_id = "external-data-mock-test";
    manager.unregister_source(mock_id);
    ok &= expect(manager.update_mock_value(
                     mock_id, "headline", ExternalDataValue::string("MOCK"), 1700),
                 "provider-free mock path creates and updates a field");
    ExternalPropertyBinding mock_binding;
    mock_binding.property_path = "text.content";
    mock_binding.source_id = mock_id;
    mock_binding.field_path = "headline";
    layer.external_bindings.front() = mock_binding;
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "MOCK",
                 "mock external field resolves end to end");
    ok &= expect(manager.set_connection_state(
                     mock_id, ExternalDataConnectionState::Disconnected, {}, 1800),
                 "mock source can disconnect");
    ok &= expect(manager.update_mock_value(
                     mock_id, "headline", ExternalDataValue::string("MOCK"), 1900),
                 "mock update reports reconnect even when value is unchanged");
    ok &= expect(effective_external_string(layer, "text.content", layer.text_content) ==
                     "MOCK",
                 "mock reconnect restores its retained current value");

    ExternalDataFormatterConfig number_formatter;
    number_formatter.prefix = "$";
    number_formatter.suffix = " USD";
    number_formatter.number_format_enabled = true;
    number_formatter.decimal_places = 2;
    number_formatter.thousands_separator = true;
    ok &= expect(format_external_data_value(
                     ExternalDataValue::floating(12345.6), number_formatter) ==
                     "$12,345.60 USD",
                 "structured number formatter applies decimals, grouping, prefix and suffix");

    ExternalDataFormatterConfig text_formatter;
    text_formatter.text_case = ExternalDataTextCase::TitleCase;
    text_formatter.conditional_replacements.push_back({"breaking", "live update", false});
    ok &= expect(format_external_data_value(
                     ExternalDataValue::string("BREAKING"), text_formatter) ==
                     "Live Update",
                 "conditional replacement and title case share one formatter pipeline");

    ExternalDataFormatterConfig empty_formatter;
    empty_formatter.empty_value_mode = ExternalDataEmptyValueMode::Replacement;
    empty_formatter.empty_replacement = "N/A";
    ok &= expect(format_external_data_value(
                     ExternalDataValue::string(""), empty_formatter) == "N/A",
                 "empty-value replacement is deterministic");

    const std::string cue_source_id = "external-data-live-cue-test";
    manager.unregister_source(cue_source_id);
    ok &= expect(manager.update_mock_value(
                     cue_source_id, "headline", ExternalDataValue::string("cue live"), 2000),
                 "live cue source publishes");
    Title cue_title;
    cue_title.live_text_rows = {{"cue authored"}};
    cue_title.live_text_row_ids = {"row-one"};
    Layer cue_layer;
    cue_layer.id = "layer-one";
    cue_layer.type = LayerType::Text;
    cue_layer.text_content = "layer authored";
    LiveTextExternalBinding cue_cell;
    cue_cell.row_id = "row-one";
    cue_cell.layer_id = cue_layer.id;
    cue_cell.binding.property_path = ExternalPropertyPaths::TextContent;
    cue_cell.binding.source_id = cue_source_id;
    cue_cell.binding.field_path = "headline";
    cue_cell.binding.formatter_config.text_case = ExternalDataTextCase::Uppercase;
    set_live_text_external_binding(cue_title, cue_cell);
    ok &= expect(effective_live_text_cue_value(
                     cue_title, 0, cue_layer, cue_title.live_text_rows[0][0]) ==
                     "CUE LIVE",
                 "live cue cells use the common formatter pipeline");
    apply_live_text_runtime_binding(cue_title, 0, cue_layer);
    ok &= expect(cue_layer.runtime_external_bindings.size() == 1 &&
                     effective_external_string(cue_layer, ExternalPropertyPaths::TextContent,
                                               cue_layer.text_content) == "CUE LIVE",
                 "cued binding becomes a runtime override without changing authored text");
    ok &= expect(cue_layer.text_content == "layer authored",
                 "live cue runtime binding preserves authored layer text");

    const std::string discovery_id = "external-data-discovery-test";
    manager.unregister_source(discovery_id);
    ExternalDataSourceDefinition discovery_source;
    discovery_source.id = discovery_id;
    discovery_source.name = "Discovery Test";
    discovery_source.provider.type = ExternalDataProviderType::JsonFile;
    manager.register_source(discovery_source);
    ok &= expect(manager.update_value(
                     discovery_id, "items[0].score",
                     ExternalDataValue::integer(7), 2100),
                 "runtime discovery creates an undeclared field");
    auto discovered_fields = available_external_data_fields(discovery_source);
    ok &= expect(discovered_fields.size() == 1 &&
                     discovered_fields.front().path == "items[0].score" &&
                     discovered_fields.front().type == ExternalDataType::Integer,
                 "available fields include runtime-discovered nested paths");
    ok &= expect(manager.update_value(
                     discovery_id, "items[0].score",
                     ExternalDataValue::floating(7.5), 2200),
                 "unpinned discovered fields can update their inferred scalar type");
    ok &= expect(external_data_field_type(discovery_source, "items[0].score") ==
                     ExternalDataType::Float,
                 "field type lookup follows the latest discovered type");
    ok &= expect(pin_external_data_field(discovery_source, "items[0].score"),
                 "binding can pin a discovered field into authored schema");
    ok &= expect(discovery_source.fields.size() == 1 &&
                     discovery_source.fields.front().type == ExternalDataType::Float,
                 "pinning preserves the discovered type for offline schema");
    ok &= expect(!manager.update_value(
                     discovery_id, "items[0].score",
                     ExternalDataValue::string("changed type"), 2300),
                 "pinned fields reject incompatible provider type changes");
    Title discovery_title;
    discovery_title.external_data_sources.push_back(discovery_source);
    ok &= expect(pin_external_data_field(
                     discovery_title, discovery_id, "new.headline"),
                 "title-level pinning supports fields not yet observed");
    ok &= expect(discovery_title.external_data_sources.front().fields.size() == 2,
                 "title-level pinning persists an offline placeholder field");

    ExternalDataSourceDefinition unpinned_source = discovery_source;
    unpinned_source.fields.clear();
    manager.synchronize_source_definition(unpinned_source);
    ok &= expect(manager.update_value(
                     discovery_id, "items[0].score",
                     ExternalDataValue::string("type is flexible again"), 2400),
                 "removing an override releases the pinned field type");
    ok &= expect(external_data_field_type(unpinned_source, "items[0].score") ==
                     ExternalDataType::String,
                 "unpinning preserves discovery while restoring inferred type behavior");

    const std::string table_source_id = "external-data-table-test";
    manager.unregister_source(table_source_id);
    ExternalDataSourceDefinition table_source;
    table_source.id = table_source_id;
    table_source.provider.type = ExternalDataProviderType::CsvFile;
    manager.register_source(table_source);
    manager.set_connection_state(table_source_id,
                                 ExternalDataConnectionState::Connected, {}, 2500);

    ExternalDataTableSnapshot table;
    table.path = "$rows";
    table.columns = {"name", "score"};
    for (int i = 0; i < 2; ++i) {
        ExternalDataTableRow row;
        row.key = std::to_string(i);
        row.values["name"] = ExternalDataValue::string(i == 0 ? "Athens" : "Patras");
        row.values["score"] = ExternalDataValue::integer(i == 0 ? 3 : 1);
        row.source_field_paths["name"] = "$rows[" + std::to_string(i) + "].name";
        row.source_field_paths["score"] = "$rows[" + std::to_string(i) + "].score";
        manager.update_value(table_source_id, row.source_field_paths["name"],
                             row.values["name"], 2510 + i);
        manager.update_value(table_source_id, row.source_field_paths["score"],
                             row.values["score"], 2520 + i);
        table.rows.push_back(std::move(row));
    }
    ok &= expect(manager.update_table(table_source_id, table, 2530),
                 "table snapshot publishes its row structure");
    const uint64_t table_revision = manager.revision();
    ok &= expect(!manager.update_table(table_source_id, table, 2540) &&
                     manager.revision() == table_revision,
                 "unchanged table snapshots do not continuously dirty runtime state");

    Title table_title;
    table_title.id = "table-title";
    table_title.live_text_rows = {{"manual", "row"}};
    table_title.live_text_row_ids = {"manual-row"};
    table_title.live_text_column_order = {"team-layer", "score-layer"};
    LiveTextTableBinding table_mapping;
    table_mapping.id = "mapping-one";
    table_mapping.source_id = table_source_id;
    table_mapping.table_path = "$rows";
    table_mapping.update_mode = LiveTextTableUpdateMode::SynchronizeRows;
    table_mapping.preserve_manual_rows = true;
    LiveTextTableColumnBinding team_column;
    team_column.layer_id = "team-layer";
    team_column.binding.property_path = ExternalPropertyPaths::TextContent;
    team_column.binding.field_path = "name";
    team_column.binding.formatter_config.text_case = ExternalDataTextCase::Uppercase;
    table_mapping.columns.push_back(team_column);
    LiveTextTableColumnBinding score_column;
    score_column.layer_id = "score-layer";
    score_column.binding.property_path = ExternalPropertyPaths::TextContent;
    score_column.binding.field_path = "score";
    score_column.binding.formatter_config.prefix = "Score: ";
    table_mapping.columns.push_back(score_column);
    table_title.live_text_table_bindings.push_back(table_mapping);

    ok &= expect(synchronize_live_text_table_bindings(table_title),
                 "table mapping materializes source-managed cue rows");
    ok &= expect(table_title.live_text_rows.size() == 3 &&
                     table_title.live_text_external_bindings.size() == 4,
                 "synchronize mode preserves manual rows and generates every mapped cell");
    Layer team_layer;
    team_layer.id = "team-layer";
    team_layer.type = LayerType::Text;
    ok &= expect(effective_live_text_cue_value(
                     table_title, 1, team_layer,
                     table_title.live_text_rows[1][0]) == "ATHENS",
                 "generated table cells resolve through the shared formatter pipeline");
    const std::string managed_row_id = table_title.live_text_row_ids[1];
    ok &= expect(live_text_cue_cell_state(
                     table_title, managed_row_id, team_layer.id) ==
                     LiveTextCueCellState::ExternalTableManaged,
                 "mapped cue cell exposes an explicit read-only managed state");
    table_title.live_text_rows[1][0] = "EDITABLE SNAPSHOT";
    ok &= expect(detach_live_text_table_cell(
                     table_title, managed_row_id, team_layer.id) &&
                     live_text_cue_cell_state(
                         table_title, managed_row_id, team_layer.id) ==
                         LiveTextCueCellState::DetachedFromTable &&
                     effective_live_text_cue_value(
                         table_title, 1, team_layer,
                         table_title.live_text_rows[1][0]) == "EDITABLE SNAPSHOT",
                 "detaching converts a mapped cell into an authored editable snapshot");
    ok &= expect(!synchronize_live_text_table_bindings(table_title, table_source_id) &&
                     table_title.live_text_rows[1][0] == "EDITABLE SNAPSHOT" &&
                     live_text_cue_cell_state(
                         table_title, managed_row_id, team_layer.id) ==
                         LiveTextCueCellState::DetachedFromTable,
                 "provider refresh preserves detached authored cell values");
    ok &= expect(restore_live_text_table_cell(
                     table_title, managed_row_id, team_layer.id) &&
                     synchronize_live_text_table_bindings(table_title, table_source_id) &&
                     live_text_cue_cell_state(
                         table_title, managed_row_id, team_layer.id) ==
                         LiveTextCueCellState::ExternalTableManaged &&
                     effective_live_text_cue_value(
                         table_title, 1, team_layer,
                         table_title.live_text_rows[1][0]) == "ATHENS",
                 "restoring reattaches the generated table-managed cell");
    Layer score_layer;
    score_layer.id = "score-layer";
    score_layer.type = LayerType::Text;
    ok &= expect(effective_live_text_cue_value(
                     table_title, 2, score_layer,
                     table_title.live_text_rows[2][1]) == "Score: 1",
                 "each cue column maps independently to its table field");

    /* Regression: a provider may expose a valid table snapshot without
     * publishing an identical scalar field registry path for each cell. The
     * source-managed cue must still use the authoritative table value. */
    ExternalDataTableSnapshot table_only;
    table_only.path = "$table-only";
    table_only.columns = {"headline"};
    ExternalDataTableRow table_only_row;
    table_only_row.key = "row-a";
    table_only_row.values["headline"] = ExternalDataValue::string("TABLE VALUE");
    /* Regression: a scalar field may exist for the generated path but contain
     * an empty/schema-placeholder value. The row-specific table value remains
     * authoritative and must not be shadowed by that scalar. */
    table_only_row.source_field_paths["headline"] = "shadow.headline";
    ok &= expect(manager.update_value(table_source_id, "shadow.headline",
                                      ExternalDataValue::string(""), 2540),
                 "empty scalar placeholder publishes beside table snapshot");
    table_only.rows.push_back(table_only_row);
    ok &= expect(manager.update_table(table_source_id, table_only, 2541),
                 "table-only snapshot publishes without scalar row paths");
    Title table_only_title;
    table_only_title.id = "table-only-title";
    table_only_title.live_text_column_order = {"headline-layer"};
    LiveTextTableBinding table_only_mapping;
    table_only_mapping.id = "mapping-table-only";
    table_only_mapping.source_id = table_source_id;
    table_only_mapping.table_path = "$table-only";
    LiveTextTableColumnBinding headline_column;
    headline_column.layer_id = "headline-layer";
    headline_column.binding.property_path = ExternalPropertyPaths::TextContent;
    headline_column.binding.field_path = "headline";
    table_only_mapping.columns.push_back(headline_column);
    table_only_title.live_text_table_bindings.push_back(table_only_mapping);
    Layer headline_layer;
    headline_layer.id = "headline-layer";
    headline_layer.type = LayerType::Text;
    ok &= expect(synchronize_live_text_table_bindings(table_only_title) &&
                     effective_live_text_cue_value(
                         table_only_title, 0, headline_layer,
                         table_only_title.live_text_rows[0][0]) == "TABLE VALUE",
                 "mapped cue resolves the table snapshot when scalar lookup is absent");
    apply_live_text_runtime_binding(table_only_title, 0, headline_layer);
    ok &= expect(effective_external_string(
                     headline_layer, ExternalPropertyPaths::TextContent,
                     std::string{}) == "TABLE VALUE",
                 "OBS/source runtime binding carries the authoritative table value");

    /* Regression: the dock normalizes cue columns before constructing widgets.
     * normalize_live_text_rows() used to validate bindings against new_order
     * after moving it into the title, so every generated mapping binding was
     * erased and the correctly sized rows appeared empty. */
    auto normalized_title = std::make_shared<Title>(table_only_title);
    auto normalized_layer = std::make_shared<Layer>();
    normalized_layer->id = "headline-layer";
    normalized_layer->type = LayerType::Text;
    normalized_layer->expose_text = true;
    normalized_title->layers.push_back(normalized_layer);
    const auto normalized_exposed =
        fxm::live_text::exposed_text_layers(normalized_title);
    fxm::live_text::normalize_live_text_rows(normalized_title, normalized_exposed);
    ok &= expect(normalized_title->live_text_external_bindings.size() == 1 &&
                     live_text_cue_cell_state(
                         *normalized_title, normalized_title->live_text_row_ids[0],
                         normalized_layer->id) ==
                         LiveTextCueCellState::ExternalTableManaged &&
                     effective_live_text_cue_value(
                         *normalized_title, 0, *normalized_layer,
                         normalized_title->live_text_rows[0][0]) == "TABLE VALUE",
                 "dock column normalization preserves generated table-cell bindings");
    table_only.rows[0].values["headline"] = ExternalDataValue::string("UPDATED TABLE VALUE");
    ok &= expect(manager.update_table(table_source_id, table_only, 2542) &&
                     synchronize_live_text_table_bindings(table_only_title, table_source_id) &&
                     effective_live_text_cue_value(
                         table_only_title, 0, headline_layer,
                         table_only_title.live_text_rows[0][0]) == "UPDATED TABLE VALUE",
                 "table-only value changes refresh source-managed cue cells");

    table_title.current_cue_row = 2;
    ExternalDataTableSnapshot reordered = table;
    std::reverse(reordered.rows.begin(), reordered.rows.end());
    ok &= expect(manager.update_table(table_source_id, reordered, 2550) &&
                     synchronize_live_text_table_bindings(table_title, table_source_id) &&
                     table_title.current_cue_row == 1,
                 "stable row IDs remap the active cue when source rows reorder");

    Title append_title;
    append_title.id = "append-table-title";
    append_title.live_text_column_order = table_title.live_text_column_order;
    LiveTextTableBinding append_mapping = table_mapping;
    append_mapping.id = "mapping-append";
    append_mapping.update_mode = LiveTextTableUpdateMode::AppendRows;
    append_mapping.preserve_manual_rows = true;
    append_title.live_text_table_bindings.push_back(append_mapping);
    ok &= expect(synchronize_live_text_table_bindings(append_title) &&
                     append_title.live_text_rows.size() == 2 &&
                     append_title.live_text_external_bindings.size() == 4,
                 "append mode materializes every initial provider row");

    ExternalDataTableSnapshot shorter = reordered;
    shorter.rows.resize(1);
    ok &= expect(manager.update_table(table_source_id, shorter, 2600),
                 "changed table structure publishes once");
    ok &= expect(synchronize_live_text_table_bindings(table_title, table_source_id) &&
                     table_title.live_text_rows.size() == 2,
                 "synchronize mode removes source rows that disappeared");
    ok &= expect(synchronize_live_text_table_bindings(append_title, table_source_id) &&
                     append_title.live_text_rows.size() == 2 &&
                     append_title.live_text_external_bindings.size() == 4,
                 "append mode retains last-known managed rows and bindings");
    ok &= expect(remove_live_text_table_binding(append_title, "mapping-append", true) &&
                     append_title.live_text_rows.empty() &&
                     append_title.live_text_external_bindings.empty(),
                 "append mapping cleanup removes all of its managed rows");
    ok &= expect(remove_live_text_table_binding(table_title, "mapping-one", true) &&
                     table_title.live_text_rows.size() == 1 &&
                     table_title.live_text_external_bindings.empty(),
                 "removing a table mapping cleans up only its managed rows and cells");

    manager.unregister_source(source_id);
    manager.unregister_source(retained_id);
    manager.unregister_source(mock_id);
    manager.unregister_source(cue_source_id);
    manager.unregister_source(discovery_id);
    manager.unregister_source(table_source_id);
    return ok ? 0 : 1;
}
