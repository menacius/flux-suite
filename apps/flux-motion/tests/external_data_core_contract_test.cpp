#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require(const std::string &text, const std::string &needle,
             const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing External Data Core contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle,
                    const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Forbidden External Data Core behavior: " << label
              << " (" << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 11) {
        std::cerr << "usage: external_data_core_contract_test <types> <manager-h> "
                     "<manager-cpp> <layer-model> <title-data-h> <title-data-cpp> "
                     "<editor-session> <source> <cache> <cmake>\n";
        return 2;
    }

    const std::string types = read_file(argv[1]);
    const std::string manager_h = read_file(argv[2]);
    const std::string manager_cpp = read_file(argv[3]);
    const std::string layer_model = read_file(argv[4]);
    const std::string title_data_h = read_file(argv[5]);
    const std::string title_data_cpp = read_file(argv[6]);
    const std::string editor = read_file(argv[7]);
    const std::string source = read_file(argv[8]);
    const std::string cache = read_file(argv[9]);
    const std::string cmake = read_file(argv[10]);

    bool ok = true;
    ok &= require(types, "enum class ExternalDataType", "typed value enum");
    ok &= require(types, "Integer = 1", "integer field type");
    ok &= require(types, "Float = 2", "float field type");
    ok &= require(types, "Boolean = 3", "boolean field type");
    ok &= require(types, "Color = 4", "color field type");
    ok &= require(types, "DateTime = 5", "date/time field type");
    ok &= require(types, "FilePath = 6", "image/file path field type");
    ok &= require(types, "Url = 7", "URL field type");
    ok &= require(types, "struct ExternalPropertyBinding", "property binding model");
    ok &= require(types, "property_path", "bound layer property path");
    ok &= require(types, "source_id", "binding source ID");
    ok &= require(types, "field_path", "binding field path");
    ok &= require(types, "formatter", "optional formatter");
    ok &= require(types, "fallback_value", "binding fallback value");

    ok &= require(manager_h, "class ExternalDataManager", "central manager");
    ok &= require(manager_h, "ExternalPropertyPaths", "canonical property paths");
    ok &= require(manager_h, "set_external_binding", "single binding per property helper");
    ok &= require(manager_h, "current_value", "current runtime field value");
    ok &= require(manager_h, "ExternalDataValueOrigin",
                  "explicit authored/live/fallback value origin");
    ok &= require(manager_h, "last_update_timestamp_ms", "last update timestamp");
    ok &= require(manager_h, "connection_state", "connection state");
    ok &= require(manager_h, "error_message", "error state message");
    ok &= require(manager_h, "take_render_updates", "thread-safe render queue API");
    ok &= require(manager_h, "update_mock_value", "provider-free mock update API");
    ok &= require(manager_cpp, "field.current_value == value", "same-value suppression");
    ok &= require(manager_cpp, "return false;", "no publication for unchanged values");
    ok &= require(manager_cpp, "render_queue_.push_back(update)", "MPSC render handoff");
    ok &= require(manager_cpp, "render_queue_index_", "bounded latest-update coalescing");
    ok &= require(manager_cpp, "render_queue_[queued->second] = update",
                  "latest update replaces queued field value");
    ok &= require(manager_cpp, "state_allows_current_value",
                  "live and configured last-known state policy");
    ok &= require(manager_cpp, "binding.has_fallback_value", "fallback resolution");
    ok &= require(manager_cpp, "definition->has_default_value", "field default resolution");
    ok &= require(manager_cpp, "return authored_value", "authored value final fallback");
    ok &= require(manager_cpp, "revision_.fetch_add", "manager-owned runtime revision");
    ok &= require_absent(manager_cpp, "touch_runtime_change",
                         "external updates must not wake global title-store revision");
    ok &= require_absent(manager_cpp, "notify_change()", "external update must not persist title");
    ok &= require_absent(manager_cpp, "Undo", "external update must not create undo command");

    ok &= require(layer_model, "std::vector<ExternalPropertyBinding> external_bindings",
                  "bindings stored on layers");
    ok &= require(title_data_h, "external_data_sources", "title source schema");
    ok &= require(title_data_cpp, "j[\"external_bindings\"]", "binding serialization");
    ok &= require(title_data_cpp, "j.contains(\"external_bindings\")",
                  "optional binding deserialization");
    ok &= require(title_data_cpp, "jt[\"external_data_sources\"]",
                  "source schema serialization");
    ok &= require(title_data_cpp, "jt.contains(\"external_data_sources\")",
                  "backward-compatible optional source deserialization");
    ok &= require(title_data_cpp, "register_title_sources", "restored schemas registered centrally");

    ok &= require(editor, "ExternalDataManager::instance().on_change",
                  "editor live update subscription");
    ok &= require(editor, "canvas_->refresh_preview()", "editor preview refresh");
    ok &= require_absent(editor, "set_dirty(true)", "external editor update does not dirty authored title");
    ok &= require(source, "take_render_updates()", "render-thread queue consumption");
    ok &= require(source, "seen_external_data_revision",
                  "per-source external revision tracking");
    ok &= require(source,
                  "pending_external_revision != data->seen_external_data_revision",
                  "no render-queue polling while external state is unchanged");
    ok &= require(source, "external_visual_change",
                  "rendering only for changed effective visual identity");
    ok &= require(source, "effective_external_string", "live external text/image resolution");
    ok &= require(cache, "effective_external_string", "cache identity includes effective value");
    ok &= require(cache, "external_bindings", "cache identity includes binding definition");
    ok &= require(cmake, "src/core/external-data.cpp", "manager included in plugin build");
    ok &= require(cmake, "external_data_runtime_test", "behavioral runtime test registered");

    return ok ? 0 : 1;
}
