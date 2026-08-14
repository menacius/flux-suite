#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"
#include <vector>

namespace {

bool require_contains(const std::string &text, const std::string &needle,
                      const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing audit contract: " << label << " (" << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle,
                    const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete audit contract remains: " << label << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 11) {
        std::cerr << "usage: development_audit_contract_test <cmake> <build-info> "
                     "<catalog-cpp> <registry-cpp> <title-source> <plugin-main> <readme> "
                     "<layer-effects> <title-data> <effects-panel>\n";
        return 2;
    }

    const std::string cmake = read_file(argv[1]);
    const std::string build_info = read_file(argv[2]);
    const std::string catalog = read_file(argv[3]);
    const std::string registry = read_file(argv[4]);
    const std::string title_source = read_file(argv[5]);
    const std::string plugin_main = read_file(argv[6]);
    const std::string readme = read_file(argv[7]);
    const std::string layer_effects = read_file(argv[8]);
    const std::string title_data = read_file(argv[9]);
    const std::string effects_panel = read_file(argv[10]);

    bool ok = true;
    ok &= require_contains(cmake, "OBS_FXM_DEVELOPMENT_VERSION", "single development version variable");
    ok &= require_contains(cmake, "FXM_DEVELOPMENT_VERSION=", "development version compile definition");
    ok &= require_contains(build_info, "Development Version", "development version display");
    ok &= require_absent(build_info, "FXM_PROMPT", "old prompt version macros");
    ok &= require_absent(readme, "Prompt p", "old prompt delivery label");

    ok &= require_contains(catalog, "TitleEffectRegistry::definitions()", "catalog derives built-ins from registry");
    ok &= require_absent(catalog, "kBuiltIns", "duplicate built-in metadata table");
    ok &= require_contains(catalog, "unloadNativeLibraries", "native libraries unloaded on reload/shutdown");
    ok &= require_contains(catalog, "QDir::NoSymLinks", "extension discovery avoids symlink traversal");
    ok &= require_contains(catalog, "containedExtensionPath", "extension assets stay inside package");
    ok &= require_contains(catalog, "canvas_handles_schema_json", "ABI v3 canvas handles loaded");
    ok &= require_contains(catalog, "plugin2->validate_state", "ABI state validation callback retained");
    ok &= require_contains(catalog, "plugin2->migrate_state", "ABI state migration callback retained");

    ok &= require_contains(registry, "stable_id", "canonical stable built-in IDs");
    ok &= require_contains(catalog, "definition.legacy_id", "legacy built-in ID migration");
    ok &= require_contains(registry, "compiled.stable_id == def->stable_id", "built-in shader cache keyed by stable ID");
    ok &= require_absent(registry, "return compiled.type == type", "numeric adapter shader cache collision");
    ok &= require_absent(title_source, "static TitleEffectRegistry registry", "process-lifetime GPU registry");
    ok &= require_contains(title_source, "clear_extension_image_textures", "extension textures explicitly released");
    ok &= require_contains(title_source, "kMaxExtensionImageTextures", "extension texture cache bounded");
    ok &= require_contains(plugin_main, "FxmEffectExtensionCatalog::instance().shutdown()", "catalog shutdown on unload");
    ok &= require_contains(layer_effects, "extension_schema_version", "effect instances persist extension schema version");
    ok &= require_contains(title_data, "migrate_and_validate_extension_state", "extension state migration and validation");
    ok &= require_contains(title_data, "extension_schema_version", "extension schema version serialization");
    ok &= require_contains(effects_panel, "effect.extension_schema_version = definition->schemaVersion", "new extension state starts at installed schema");
    ok &= require_absent(effects_panel, "[this, preset, extension_definition]", "catalog definition pointer captured by UI lambda");

    return ok ? 0 : 1;
}
