#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {
bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing External Data UI contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 14) {
        std::cerr << "usage: external_data_ui_contract_test <types> <manager> <title-data> "
                     "<settings> <binding-dialog> <table-mapping-dialog> <properties-h> <properties> <dock> "
                     "<cue-logic> <hotkeys> <layer-stack> <cmake>\n";
        return 2;
    }

    const std::string types = read_file(argv[1]);
    const std::string manager = read_file(argv[2]);
    const std::string title_data = read_file(argv[3]);
    const std::string settings = read_file(argv[4]);
    const std::string binding_dialog = read_file(argv[5]);
    const std::string table_mapping = read_file(argv[6]);
    const std::string properties_h = read_file(argv[7]);
    const std::string properties = read_file(argv[8]);
    const std::string dock = read_file(argv[9]);
    const std::string cue_logic = read_file(argv[10]);
    const std::string hotkeys = read_file(argv[11]);
    const std::string layer_stack = read_file(argv[12]);
    const std::string cmake = read_file(argv[13]);

    bool ok = true;
    ok &= require(types, "ExternalDataRefreshMode", "refresh behavior model");
    ok &= require(types, "RefreshOnCue", "refresh-on-cue mode");
    ok &= require(types, "RefreshContinuously", "continuous refresh mode");
    ok &= require(types, "RefreshManually", "manual refresh mode");
    ok &= require(types, "ExternalDataFormatterConfig", "structured formatter model");
    ok &= require(types, "conditional_replacements", "conditional replacement rules");
    ok &= require(types, "empty_value_mode", "empty-value handling");
    ok &= require(types, "LiveTextExternalBinding", "live cue cell binding model");

    ok &= require(manager, "format_external_data_value", "shared formatter runtime");
    ok &= require(manager, "add_thousands_separators", "thousands separator formatter");
    ok &= require(manager, "utc_tm_from_value", "date/time formatter");
    ok &= require(manager, "apply_live_text_runtime_binding", "runtime cue binding override");
    ok &= require(manager, "available_external_data_fields", "runtime and authored field union");
    ok &= require(manager, "pin_external_data_field", "automatic field pinning");
    ok &= require(manager, "synchronize_source_definition", "unpinning releases schema overrides");
    ok &= require(manager, "authored_definition", "discovered type remains flexible until pinned");
    ok &= require(title_data, "formatter_config", "formatter serialization");
    ok &= require(title_data, "live_text_external_bindings", "cue binding serialization");
    ok &= require(title_data, "refresh_mode", "refresh mode serialization");

    ok &= require(settings, "Duplicate", "duplicate source UI");
    ok &= require(settings, "Test connection", "connection test UI");
    ok &= require(settings, "Live field preview", "live field preview table");
    ok &= require(settings, "Refresh behavior", "refresh mode control");
    ok &= require(settings, "Refresh on cue", "cue refresh option");
    ok &= require(settings, "Refresh continuously", "continuous refresh option");
    ok &= require(settings, "Refresh manually", "manual refresh option");
    ok &= require(settings, "formatter_summary", "formatter summary in binding list");
    ok &= require(settings, "ExternalDataBindingDialog", "binding popup from settings");
    ok &= require(settings, "Fields (optional)", "optional schema override tab");
    ok &= require(settings, "Fields are discovered automatically", "field discovery guidance");
    ok &= require(settings, "refresh_discovered_fields", "live discovery updates open dialog");
    ok &= require(settings, "Pin / override", "explicit field pinning control");
    ok &= require(settings, "A binding is itself an instruction", "binding auto-pins field");

    ok &= require(binding_dialog, "Formatter pipeline", "formatter popup section");
    ok &= require(binding_dialog, "Prefix", "prefix control");
    ok &= require(binding_dialog, "Suffix", "suffix control");
    ok &= require(binding_dialog, "Decimal places", "decimal places control");
    ok &= require(binding_dialog, "Thousands separator", "thousands separator control");
    ok &= require(binding_dialog, "Title Case", "text case control");
    ok &= require(binding_dialog, "Date/time format", "date format control");
    ok &= require(binding_dialog, "Conditional replacement", "conditional replacement UI");
    ok &= require(binding_dialog, "Empty value", "empty-value UI");
    ok &= require(binding_dialog, "Live preview", "binding preview UI");
    ok &= require(binding_dialog, "available_external_data_fields", "discovered fields listed in binding popup");
    ok &= require(binding_dialog, "[discovered]", "discovered field indicator");

    ok &= require(table_mapping, "Populate Live Text Cues from Table", "table mapping popup");
    ok &= require(table_mapping, "Result preview", "table mapping live preview");
    ok &= require(table_mapping, "ExternalDataBindingDialog", "table columns share formatter UI");

    ok &= require(properties_h, "btn_text_external_binding_", "text property binding button");
    ok &= require(properties_h, "btn_image_external_binding_", "image property binding button");
    ok &= require(properties, "ExternalDataBindingDialog", "property popup wiring");
    ok &= require(properties, "externalDataBound", "bound property visual state");
    ok &= require(properties, "pin_external_data_field", "property binding auto-pins discovery");
    ok &= require(dock, "Bind external data", "live cue binding action");
    ok &= require(dock, "set_live_text_external_binding", "live cue binding save path");
    ok &= require(dock, "pin_external_data_field", "cue binding auto-pins discovery");
    ok &= require(cue_logic, "effective_live_text_cue_value", "formatted cue application");
    ok &= require(cue_logic, "RefreshOnCue", "dock refresh-on-cue trigger");
    ok &= require(hotkeys, "apply_live_text_runtime_binding", "hotkey cue bindings");
    ok &= require(layer_stack, "has_external_binding", "layer binding indicator");
    ok &= require(layer_stack, "Layer has external data binding", "layer indicator tooltip");
    ok &= require(cmake, "external_data_ui_contract_test", "UI contract registered");

    return ok ? 0 : 1;
}
