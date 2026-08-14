#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {
bool require(const std::string &text, const std::string &needle,
             const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing Live Text table mapping contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool require_before(const std::string &text, const std::string &first,
                    const std::string &second, const char *label)
{
    const auto first_pos = text.find(first);
    const auto second_pos = text.find(second);
    if (first_pos != std::string::npos && second_pos != std::string::npos &&
        first_pos < second_pos)
        return true;
    std::cerr << "Invalid Live Text table mapping source order: " << label << "\n";
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 10) {
        std::cerr << "usage: live_text_table_mapping_contract_test <types> <manager-h> "
                     "<manager-cpp> <provider-cpp> <title-data-cpp> <dialog-cpp> "
                     "<dock-lifecycle> <dock-handler> <cmake>\n";
        return 2;
    }

    const std::string types = read_file(argv[1]);
    const std::string manager_h = read_file(argv[2]);
    const std::string manager_cpp = read_file(argv[3]);
    const std::string provider_cpp = read_file(argv[4]);
    const std::string title_data = read_file(argv[5]);
    const std::string dialog = read_file(argv[6]);
    const std::string lifecycle = read_file(argv[7]);
    const std::string dock = read_file(argv[8]);
    const std::string cmake = read_file(argv[9]);

    bool ok = true;
    ok &= require(types, "ExternalDataTableSnapshot", "provider table snapshot model");
    ok &= require(types, "LiveTextTableBinding", "table-to-cue mapping model");
    ok &= require(types, "ReplaceRows", "replace update mode");
    ok &= require(types, "AppendRows", "append update mode");
    ok &= require(types, "SynchronizeRows", "synchronize update mode");
    ok &= require(types, "row_id_field", "stable row ID field");
    ok &= require(types, "table_binding_id", "source-managed cell marker");
    ok &= require(types, "LiveTextCueCellState", "explicit cue cell state model");
    ok &= require(types, "ExternalTableManaged", "read-only table-managed cell state");
    ok &= require(types, "DetachedFromTable", "editable detached cell state");
    ok &= require(types, "runtime_value", "runtime-only authoritative table cell value");
    ok &= require(types, "has_runtime_value", "runtime table value presence marker");

    ok &= require(manager_h, "synchronize_tables", "complete table snapshot synchronization");
    ok &= require(manager_h, "synchronize_live_text_table_bindings", "cue-row synchronization API");
    ok &= require(manager_cpp, "table_snapshot_equal", "unchanged table suppression");
    ok &= require(manager_cpp, "table_managed_row_id", "stable managed row identity");
    ok &= require(manager_cpp, "has_user_override", "explicit cell bindings override generated cells");
    ok &= require(manager_cpp, "detach_live_text_table_cell", "explicit managed-cell detachment");
    ok &= require(manager_cpp, "restore_live_text_table_cell", "managed-cell restoration");
    ok &= require(manager_cpp, "Preserve authored values for explicit per-cell overrides",
                  "detached authored values survive provider refresh");
    ok &= require(manager_cpp, "active_row_id", "active cue remapped by stable row ID");
    ok &= require(manager_cpp, "Append mode retains source-managed rows", "append last-known rows");
    ok &= require(manager_cpp, "format_external_data_value", "shared formatter path");
    ok &= require(manager_cpp, "binding.has_runtime_value", "table value fallback in shared resolver");
    ok &= require(manager_cpp, "Higher-level adapters such as table-to-cue mappings",
                  "authoritative row-specific runtime value priority");
    ok &= require(manager_cpp, "generated.field_path = column.binding.field_path",
                  "table mapping survives missing scalar field paths");

    ok &= require(provider_cpp, "discover_json_tables", "JSON array table discovery");
    ok &= require(provider_cpp, "flatten_json_table_row", "nested row field discovery");
    ok &= require(provider_cpp, "table.path = \"$rows\"", "CSV/text/manual row tables");
    ok &= require(provider_cpp, "source_field_paths", "table cells reference live scalar fields");
    ok &= require(provider_cpp, "synchronize_tables", "provider publishes complete table snapshots");

    ok &= require(title_data, "live_text_table_bindings", "mapping serialization");
    ok &= require(title_data, "table_binding_id", "generated-cell serialization");
    ok &= require(dialog, "Populate Live Text Cues from Table", "mapping dialog");
    ok &= require(dialog, "Cue column mapping", "per-column mapper");
    ok &= require(dialog, "Result preview", "live result preview");
    ok &= require(dialog, "Replace rows", "replace UI");
    ok &= require(dialog, "Append rows", "append UI");
    ok &= require(dialog, "Synchronize rows", "synchronize UI");
    ok &= require(dialog, "Stable row ID field", "row ID UI");
    ok &= require(dialog, "ExternalDataBindingDialog", "shared formatter popup");

    ok &= require(lifecycle, "Populate from external table", "dock menu action");
    ok &= require(lifecycle, "update.table_changed", "live table callback");
    ok &= require(lifecycle, "QTimer::singleShot", "UI-thread marshaling");
    ok &= require(dock, "on_map_external_table", "dock handler");
    ok &= require(dock, "refreshLiveCueStructureAsync", "non-blocking cue cache refresh");
    ok &= require(dock, "displayed_cell_value = exposed[col]",
                  "bound table cells select resolved live display path");
    ok &= require(dock, "effective_live_text_cue_value(",
                  "bound table cells display resolved live values");
    ok &= require(dock, "authored_cell_value",
                  "live display does not overwrite authored cue storage");
    ok &= require(dock, "externalTableManaged",
                  "mapped table cue visual marker");
    ok &= require(dock, "setItalic(table_managed)",
                  "mapped table values render in italics only in the cue table");
    ok &= require(dock, "setReadOnly(table_managed)",
                  "mapped text cue cells are read-only");
    ok &= require(dock, "set_read_only(table_managed)",
                  "mapped image cue cells are read-only");
    ok &= require(dock, "Convert to editable value",
                  "explicit detach action");
    ok &= require(dock, "Restore table-managed value",
                  "explicit restore action");
    ok &= require(dock, "liveCueCellState",
                  "new cue cell UI state property");
    ok &= require_before(dock, "void TitleDock::on_map_external_table()",
                          "TitleDock::create_template_title", "handler stays outside split template factory");

    ok &= require(cmake, "external-data-table-mapping-dialog.cpp", "dialog built");
    ok &= require(cmake, "live_text_table_mapping_contract_test", "mapping contract registered");
    return ok ? 0 : 1;
}
