#include "source_bundle_reader.h"

#include <iostream>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Phase 12 editor boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Forbidden Phase 12 editor dependency: " << label
              << " (" << token << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 33)
        return 2;

    const std::string long_press = read_file(argv[1]);
    const std::string palette = read_file(argv[2]);
    const std::string timecode_header = read_file(argv[3]);
    const std::string timecode_source = read_file(argv[4]);
    const std::string frame_rate_provider = read_file(argv[5]);
    const std::string obs_provider = read_file(argv[6]);
    const std::string asset_path_provider = read_file(argv[7]);
    const std::string obs_asset_provider = read_file(argv[8]);
    const std::string translation_provider = read_file(argv[9]);
    const std::string localization_header = read_file(argv[10]);
    const std::string localization_source = read_file(argv[11]);
    const std::string obs_translation_provider = read_file(argv[12]);
    const std::string title_assets_header = read_file(argv[13]);
    const std::string title_assets_source = read_file(argv[14]);
    const std::string modern_header = read_file(argv[15]);
    const std::string modern_source = read_file(argv[16]);
    const std::string settings_header = read_file(argv[17]);
    const std::string settings_source = read_file(argv[18]);
    const std::string binding_header = read_file(argv[19]);
    const std::string binding_source = read_file(argv[20]);
    const std::string mapping_header = read_file(argv[21]);
    const std::string mapping_source = read_file(argv[22]);
    const std::string asset_library_header = read_file(argv[23]);
    const std::string asset_library_source = read_file(argv[24]);
    const std::string frame_rate_source = read_file(argv[25]);
    const std::string editor_internal = read_file(argv[26]);
    const std::string rich_text_adapters = read_file(argv[27]);
    const std::string dock_ui = read_file(argv[28]);
    const std::string template_helpers = read_file(argv[29]);
    const std::string window_session = read_file(argv[30]);
    const std::string plugin_main = read_file(argv[31]);
    const std::string cmake = read_file(argv[32]);

    bool ok = true;
    ok = require_token(long_press, "class LongPressToolButton",
                       "long-press editor control") &&
         ok;
    ok = require_token(palette, "fxm_open_color_palette",
                       "editor color palette") &&
         ok;
    ok = require_token(timecode_header, "class TimecodeSpinBox",
                       "timecode editor control") &&
         ok;
    ok = require_token(timecode_source, "fxm::current_frame_rate()",
                       "timecode frame-rate injection") &&
         ok;
    ok = require_token(frame_rate_provider, "class IFrameRateProvider",
                       "backend-neutral frame-rate provider") &&
         ok;
    ok = require_token(obs_provider, "obs_get_video_info",
                       "OBS frame-rate adapter") &&
         ok;
    ok = require_token(frame_rate_source, "g_frame_rate_provider",
                       "Shared frame-rate provider registry") &&
         ok;
    ok = require_token(rich_text_adapters,
                       "return fxm::current_frame_rate();",
                       "editor timeline frame-rate abstraction") &&
         ok;
    ok = require_token(asset_path_provider, "class IAssetPathProvider",
                       "backend-neutral asset-path provider") &&
         ok;
    ok = require_token(obs_asset_provider, "obs_module_file",
                       "OBS asset-path adapter") &&
         ok;
    ok = require_token(translation_provider, "class ITranslationProvider",
                       "backend-neutral translation provider") &&
         ok;
    ok = require_token(localization_header, "fxm_tr_c",
                       "compatible translation API") &&
         ok;
    ok = require_token(localization_source, "g_translation_provider",
                       "translation provider registration") &&
         ok;
    ok = require_token(obs_translation_provider, "obs_module_text",
                       "OBS translation adapter") &&
         ok;
    ok = require_token(title_assets_header, "fxm_asset_path(",
                       "provider-backed Editor asset lookup") &&
         ok;
    ok = require_token(title_assets_source, "fxm::set_asset_path_provider(provider);",
                       "Editor asset provider registration") &&
         ok;
    ok = require_token(modern_header, "class FxmSwitch",
                       "modern Editor controls") &&
         ok;
    ok = require_token(modern_source, "fxm_icon(",
                       "modern controls use Editor asset helper") &&
         ok;
    ok = require_token(settings_header,
                       "class ExternalDataSettingsDialog",
                       "external-data settings dialog") &&
         ok;
    ok = require_token(binding_header,
                       "class ExternalDataBindingDialog",
                       "external-data binding dialog") &&
         ok;
    ok = require_token(mapping_header,
                       "class ExternalDataTableMappingDialog",
                       "external-data table-mapping dialog") &&
         ok;
    ok = require_token(asset_library_header, "class AssetLibraryPanel",
                       "asset-library panel") &&
         ok;
    ok = require_token(asset_library_source, "AssetLibraryPanel::reload",
                       "asset-library implementation") &&
         ok;
    ok = require_token(dock_ui, "fxm_asset_path(",
                       "canned-template asset lookup") &&
         ok;
    ok = require_token(template_helpers, "fxm_asset_path(",
                       "template effect asset lookup") &&
         ok;
    ok = require_token(title_assets_source,
                       "Satoshi-Medium.otf",
                       "Editor font asset lookup") &&
         ok;
    ok = require_token(window_session,
                       "fxm_satoshi_ui_font()",
                       "About font registration") &&
         ok;
    ok = require_token(window_session,
                       "fxm_asset_path(\"icons/flux-motion-about.svg\")",
                       "About artwork asset lookup") &&
         ok;
    ok = require_token(
             plugin_main,
             "TimecodeSpinBox::set_frame_rate_provider(",
             "OBS provider registration") &&
         ok;
    ok = require_token(plugin_main, "fxm::set_asset_path_provider(",
                       "OBS asset provider registration") &&
         ok;
    ok = require_token(plugin_main, "fxm_set_translation_provider(",
                       "OBS translation provider registration") &&
         ok;
    ok = require_token(cmake, "Editor/long-press-tool-button.h",
                       "Editor module build ownership") &&
         ok;
    ok = require_token(cmake, "Editor/open-color-palette.h",
                       "Editor palette build ownership") &&
         ok;
    ok = require_token(cmake, "Editor/timecode-spinbox.cpp",
                       "Editor timecode build ownership") &&
         ok;
    ok = require_token(cmake, "Editor/title-assets.cpp",
                       "Editor asset-helper build ownership") &&
         ok;
    ok = require_token(cmake, "Editor/fxm-modern-controls.cpp",
                       "modern controls build ownership") &&
         ok;
    ok = require_token(cmake,
                       "Editor/external-data-settings-dialog.cpp",
                       "settings dialog build ownership") &&
         ok;
    ok = require_token(cmake,
                       "Editor/external-data-binding-dialog.cpp",
                       "binding dialog build ownership") &&
         ok;
    ok = require_token(
             cmake, "Editor/external-data-table-mapping-dialog.cpp",
             "table-mapping dialog build ownership") &&
         ok;
    ok = require_token(cmake, "Editor/asset-library.cpp",
                       "asset-library build ownership") &&
         ok;

    ok = reject_token(long_press, "obs_", "OBS symbol in Editor control") &&
         ok;
    ok = reject_token(long_press, "obs-module",
                      "OBS include in Editor control") &&
         ok;
    ok = reject_token(palette, "obs_", "OBS symbol in Editor palette") &&
         ok;
    ok = reject_token(palette, "obs-module",
                      "OBS include in Editor palette") &&
         ok;
    ok = reject_token(timecode_header, "obs_",
                      "OBS symbol in timecode header") &&
         ok;
    ok = reject_token(timecode_source, "obs_",
                      "OBS symbol in timecode source") &&
         ok;
    ok = reject_token(timecode_source, "title-localization",
                      "OBS localization dependency in timecode source") &&
         ok;
    ok = reject_token(frame_rate_provider, "obs_",
                      "OBS symbol in frame-rate interface") &&
         ok;
    ok = reject_token(frame_rate_source, "obs_",
                      "OBS symbol in frame-rate registry") &&
         ok;
    ok = reject_token(editor_internal, "#include <obs-module.h>",
                      "direct OBS include in editor internals") &&
         ok;
    ok = reject_token(rich_text_adapters, "obs_get_video_info",
                      "direct OBS frame-rate query in editor internals") &&
         ok;
    ok = reject_token(asset_path_provider, "obs_",
                      "OBS symbol in asset-path interface") &&
         ok;
    ok = reject_token(translation_provider, "obs_",
                      "OBS symbol in translation interface") &&
         ok;
    ok = reject_token(localization_header, "obs_",
                      "OBS symbol in Shared localization API") &&
         ok;
    ok = reject_token(localization_source, "obs_",
                      "OBS symbol in Shared localization implementation") &&
         ok;
    ok = reject_token(title_assets_header, "obs_",
                      "OBS symbol in Editor asset helper") &&
         ok;
    ok = reject_token(title_assets_source, "obs_",
                      "OBS symbol in Editor asset provider") &&
         ok;
    ok = reject_token(modern_header, "obs_",
                      "OBS symbol in modern control header") &&
         ok;
    ok = reject_token(modern_source, "obs_",
                      "OBS symbol in modern control source") &&
         ok;
    const std::string dialogs = settings_header + settings_source +
                                binding_header + binding_source +
                                mapping_header + mapping_source;
    ok = reject_token(dialogs, "obs_",
                      "OBS symbol in external-data dialogs") &&
         ok;
    ok = reject_token(asset_library_header, "obs_",
                      "OBS symbol in asset-library header") &&
         ok;
    ok = reject_token(asset_library_source, "obs_",
                      "OBS symbol in asset-library source") &&
         ok;
    ok = reject_token(dock_ui, "obs_module_file",
                      "direct module-file lookup in dock UI") &&
         ok;
    ok = reject_token(template_helpers, "obs_module_file",
                      "direct module-file lookup in template helpers") &&
         ok;
    ok = reject_token(window_session, "obs_module_file",
                      "direct module-file lookup in editor window") &&
         ok;
    ok = reject_token(cmake, "src/editor/long-press-tool-button.h",
                      "legacy long-press build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/open-color-palette.h",
                      "legacy palette build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/timecode-spinbox",
                      "legacy timecode build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/title-assets",
                      "legacy asset-helper build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/fxm-modern-controls",
                      "legacy modern-controls build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/external-data-settings-dialog",
                      "legacy settings-dialog build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/external-data-binding-dialog",
                      "legacy binding-dialog build path") &&
         ok;
    ok = reject_token(
             cmake, "src/editor/external-data-table-mapping-dialog",
             "legacy table-mapping-dialog build path") &&
         ok;
    ok = reject_token(cmake, "src/editor/asset-library",
                      "legacy asset-library build path") &&
         ok;
    return ok ? 0 : 1;
}
