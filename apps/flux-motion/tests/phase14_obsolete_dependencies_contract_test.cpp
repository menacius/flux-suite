#include "source_bundle_reader.h"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Phase 14 boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Obsolete Phase 14 dependency remains: " << label
              << " (" << token << ")\n";
    return false;
}

std::string cmake_set_block(const std::string &cmake,
                            const std::string &variable)
{
    const std::string marker = "set(" + variable;
    const size_t begin = cmake.find(marker);
    if (begin == std::string::npos)
        return {};
    const size_t end = cmake.find("\n)", begin);
    return end == std::string::npos ? cmake.substr(begin)
                                    : cmake.substr(begin, end - begin + 2);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 22)
        return 2;

    const std::string cmake = read_file(argv[1]);
    const std::filesystem::path legacy_obs_root = argv[2];
    const std::string editor_controls_header = read_file(argv[3]);
    const std::string editor_controls_source = read_file(argv[4]);
    const std::string legacy_editor_helpers = read_file(argv[5]);
    const std::string title_properties = read_file(argv[6]);
    const std::string animation_edit_utils = read_file(argv[7]);
    const std::string tools_sidebar = read_file(argv[8]);
    const std::string tool_icons_header = read_file(argv[9]);
    const std::string tool_icons_source = read_file(argv[10]);
    const std::string hold_button_header = read_file(argv[11]);
    const std::string legacy_text_helpers = read_file(argv[12]);
    const std::string title_source = read_file(argv[13]);
    const std::string style_runtime_header = read_file(argv[14]);
    const std::string style_presets_source = read_file(argv[15]);
    const std::string properties_panel = read_file(argv[16]);
    const std::string editor_internal = read_file(argv[17]);
    const std::string preview_renderer = read_file(argv[18]);
    const std::string preview_registry = read_file(argv[19]);
    const std::string obs_preview_renderer = read_file(argv[20]);
    const std::string plugin_main = read_file(argv[21]);
    const std::string coupled_sources = cmake_set_block(
        cmake, "OBS_FXM_OBS_COUPLED_EDITOR_SOURCES");
    const std::string coupled_headers = cmake_set_block(
        cmake, "OBS_FXM_OBS_COUPLED_EDITOR_HEADERS");
    const std::string editor_sources = cmake_set_block(
        cmake, "OBS_FXM_EDITOR_MODULE_SOURCES");
    const std::string editor_headers = cmake_set_block(
        cmake, "OBS_FXM_EDITOR_MODULE_HEADERS");
    const std::string generic_rendering = cmake_set_block(
        cmake, "OBS_FXM_RENDERING_SOURCES");
    const std::string obs_legacy_rendering = cmake_set_block(
        cmake, "OBS_FXM_OBS_PLUGIN_LEGACY_RENDERING_SOURCES");

    bool ok = reject_token(cmake, "set(OBS_FXM_OBS_SOURCES",
                           "legacy OBS source inventory");
    ok = reject_token(cmake, "set(OBS_FXM_OBS_HEADERS",
                      "legacy OBS header inventory") &&
         ok;
    ok = reject_token(cmake, "${OBS_FXM_OBS_SOURCES}",
                      "legacy OBS source list consumer") &&
         ok;
    ok = reject_token(cmake, "${OBS_FXM_OBS_HEADERS}",
                      "legacy OBS header list consumer") &&
         ok;
    ok = reject_token(cmake, "\n    src/obs\n",
                      "legacy OBS include directory") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES",
                       "replacement OBSPlugin ownership") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_CORE_PLAYBACK_SOURCES",
                       "replacement Core playback ownership") &&
         ok;
    ok = require_token(editor_controls_header, "class NumericDragLabel",
                       "shared Editor numeric-drag control declaration") &&
         ok;
    ok = require_token(editor_controls_source,
                       "NumericDragLabel::NumericDragLabel(",
                       "shared Editor numeric-drag control implementation") &&
         ok;
    ok = reject_token(legacy_editor_helpers, "class NumericDragLabel",
                      "translation-unit-local numeric-drag control") &&
         ok;
    ok = reject_token(title_properties, "obs_icon(",
                      "OBS-named icon alias in title properties") &&
         ok;
    ok = require_token(title_properties, "fxm_icon(",
                       "Editor icon provider in title properties") &&
         ok;
    ok = reject_token(title_properties, "#include \"title-editor-internal.h\"",
                      "title properties dependency on editor internals") &&
         ok;
    ok = require_token(title_properties,
                       "#include \"title-animation-edit-utils.h\"",
                       "explicit title animation-edit dependency") &&
         ok;
    ok = require_token(animation_edit_utils,
                       "namespace fxm::editor::animation_edit",
                       "Editor animation-edit boundary") &&
         ok;
    ok = require_token(animation_edit_utils, "inline void toggle_keyframe(",
                       "animation keyframe editing helper") &&
         ok;
    ok = reject_token(coupled_sources, "src/editor/title-properties-panel.cpp",
                      "title properties OBS-coupled source ownership") &&
         ok;
    ok = reject_token(coupled_headers, "src/editor/title-properties-panel.h",
                      "title properties OBS-coupled header ownership") &&
         ok;
    ok = require_token(editor_sources, "src/editor/title-properties-panel.cpp",
                       "title properties Editor source ownership") &&
         ok;
    ok = require_token(editor_headers, "src/editor/title-properties-panel.h",
                       "title properties Editor header ownership") &&
         ok;
    ok = reject_token(tools_sidebar, "#include \"title-editor-internal.h\"",
                      "tools sidebar dependency on editor internals") &&
         ok;
    ok = reject_token(tools_sidebar, "obs_icon(",
                      "OBS-named icon alias in tools sidebar") &&
         ok;
    ok = require_token(tools_sidebar, "#include \"editor-tool-icons.h\"",
                       "tools sidebar icon boundary") &&
         ok;
    ok = require_token(tools_sidebar,
                       "#include \"hold-menu-tool-button.h\"",
                       "tools sidebar hold-menu boundary") &&
         ok;
    ok = require_token(tool_icons_header,
                       "namespace fxm::editor::tool_icons",
                       "Editor tool-icon interface") &&
         ok;
    ok = require_token(tool_icons_source, "QIcon shape_tool_icon(",
                       "Editor tool-icon implementation") &&
         ok;
    ok = require_token(hold_button_header, "class HoldMenuToolButton",
                       "Editor hold-menu control") &&
         ok;
    ok = reject_token(legacy_text_helpers, "class HoldMenuToolButton",
                      "legacy hold-menu control definition") &&
         ok;
    ok = reject_token(coupled_sources, "src/editor/tools-sidebar.cpp",
                      "tools sidebar OBS-coupled source ownership") &&
         ok;
    ok = reject_token(coupled_headers, "src/editor/tools-sidebar.h",
                      "tools sidebar OBS-coupled header ownership") &&
         ok;
    ok = require_token(editor_sources, "src/editor/tools-sidebar.cpp",
                       "tools sidebar Editor source ownership") &&
         ok;
    ok = require_token(editor_headers, "src/editor/tools-sidebar.h",
                       "tools sidebar Editor header ownership") &&
         ok;
    ok = reject_token(title_source, "#include \"style-presets.h\"",
                      "OBS dependency on Editor style presets") &&
         ok;
    ok = require_token(title_source,
                       "#include \"style-preset-runtime.h\"",
                       "OBS dependency on Core style runtime") &&
         ok;
    ok = require_token(style_runtime_header, "resolve_text_preset(",
                       "Core style-preset resolver") &&
         ok;
    ok = reject_token(style_runtime_header, "QWidget",
                      "Qt Widgets in Core style-preset runtime") &&
         ok;
    ok = require_token(style_presets_source,
                       "fxm::style_presets::apply_text_preset_payload",
                       "Editor delegation to Core style conversion") &&
         ok;
    ok = reject_token(coupled_sources, "src/editor/style-presets.cpp",
                      "style presets OBS-coupled source ownership") &&
         ok;
    ok = reject_token(coupled_headers, "src/editor/style-presets.h",
                      "style presets OBS-coupled header ownership") &&
         ok;
    ok = require_token(editor_sources, "src/editor/style-presets.cpp",
                       "style presets Editor source ownership") &&
         ok;
    ok = require_token(editor_headers, "src/editor/style-presets.h",
                       "style presets Editor header ownership") &&
         ok;
    ok = reject_token(editor_internal, "#include \"title-source.h\"",
                      "OBS title-source dependency in Editor internals") &&
         ok;
    ok = reject_token(editor_internal, "#include \"plugin-main.h\"",
                      "OBS plugin-main dependency in Editor internals") &&
         ok;
    ok = reject_token(editor_internal, "#include \"title-editor.h\"",
                      "OBS-coupled title editor dependency in Editor internals") &&
         ok;
    ok = require_token(editor_internal,
                       "#include \"title-preview-renderer.h\"",
                       "Shared preview-render dependency") &&
         ok;
    ok = reject_token(preview_renderer, "obs_",
                      "OBS symbol in Shared preview-render API") &&
         ok;
    ok = reject_token(preview_renderer, "QWidget",
                      "Qt Widgets in Shared preview-render API") &&
         ok;
    ok = require_token(preview_renderer, "class ITitlePreviewRenderer",
                       "Shared preview-render interface") &&
         ok;
    ok = require_token(preview_registry, "title_preview_renderer()",
                       "Shared preview-render provider registry") &&
         ok;
    ok = require_token(obs_preview_renderer,
                       "render_title_to_image_obs(",
                       "OBS preview-render implementation adapter") &&
         ok;
    ok = require_token(plugin_main,
                       "set_title_preview_renderer(",
                       "OBS preview-render provider registration") &&
         ok;
    ok = reject_token(properties_panel, "#include \"title-source.h\"",
                      "OBS title-source dependency in properties panel") &&
         ok;
    ok = reject_token(coupled_sources, "src/editor/properties-panel.cpp",
                      "properties panel OBS-coupled source ownership") &&
         ok;
    ok = reject_token(coupled_headers, "src/editor/properties-panel.h",
                      "properties panel OBS-coupled header ownership") &&
         ok;
    ok = reject_token(coupled_headers, "src/editor/title-editor-internal.h",
                      "editor internals OBS-coupled header ownership") &&
         ok;
    ok = require_token(editor_sources, "src/editor/properties-panel.cpp",
                       "properties panel Editor source ownership") &&
         ok;
    ok = require_token(editor_headers, "src/editor/properties-panel.h",
                       "properties panel Editor header ownership") &&
         ok;
    ok = require_token(editor_headers, "src/editor/title-editor-internal.h",
                       "editor internals Editor header ownership") &&
         ok;
    ok = reject_token(generic_rendering, "title-effect-registry.cpp",
                      "OBS effect compiler in generic rendering ownership") &&
         ok;
    ok = reject_token(generic_rendering, "title-gpu-text-renderer.cpp",
                      "OBS GPU text renderer in generic rendering ownership") &&
         ok;
    ok = require_token(obs_legacy_rendering, "title-effect-registry.cpp",
                       "OBS effect compiler ownership") &&
         ok;
    ok = require_token(obs_legacy_rendering, "title-gpu-text-renderer.cpp",
                       "OBS GPU text renderer ownership") &&
         ok;

    if (std::filesystem::exists(legacy_obs_root)) {
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(legacy_obs_root)) {
            if (entry.is_regular_file()) {
                std::cerr << "Legacy src/obs file remains: "
                          << entry.path().string() << '\n';
                ok = false;
            }
        }
    }

    return ok ? 0 : 1;
}
