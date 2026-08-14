#include "source_bundle_reader.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace {

bool require_token(const std::string &text, const std::string &token,
                   const char *label)
{
    if (text.find(token) != std::string::npos)
        return true;
    std::cerr << "Missing Phase 13 boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool reject_token(const std::string &text, const std::string &token,
                  const char *label)
{
    if (text.find(token) == std::string::npos)
        return true;
    std::cerr << "Unexpected Phase 13 boundary: " << label
              << " (" << token << ")\n";
    return false;
}

bool verify_source_modules(const std::filesystem::path &root)
{
    const std::set<std::string> expected = {
        "compatibility-effects-compositor.inc",
        "compatibility-layer-raster.inc",
        "compatibility-text-rendering.inc",
        "gpu-effects-transitions.inc",
        "gpu-frame-cache-alias.inc",
        "gpu-masks-groups-cache.inc",
        "gpu-presentation-readback.inc",
        "gpu-resources-primitives.inc",
        "gpu-session-lifecycle.inc",
        "layer-evaluation-layout.inc",
        "scene-masks-properties.inc",
        "source-lifecycle-playback.inc",
        "source-registration.inc",
        "source-runtime.inc",
    };
    std::set<std::string> actual;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        if (entry.is_regular_file() && entry.path().extension() == ".inc")
            actual.insert(entry.path().filename().string());
    }
    if (actual == expected)
        return true;
    std::cerr << "Unexpected Phase 13 title-source module inventory\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 20)
        return 2;

    const std::string hotkey_header = read_file(argv[1]);
    const std::string hotkey_implementation = read_file(argv[2]);
    const std::string plugin_header = read_file(argv[3]);
    const std::string plugin_main = read_file(argv[4]);
    const std::string stinger_header = read_file(argv[5]);
    const std::string stinger_implementation = read_file(argv[6]);
    const std::string audio_header = read_file(argv[7]);
    const std::string audio_implementation = read_file(argv[8]);
    const std::string source_header = read_file(argv[9]);
    const std::string source_implementation = read_file(argv[10]);
    const std::filesystem::path source_module_root = argv[11];
    const std::string video_header = read_file(argv[12]);
    const std::string video_implementation = read_file(argv[13]);
    const std::string audio_scheduler = read_file(argv[14]);
    const std::string transport_direction = read_file(argv[15]);
    const std::string dock_lifecycle = read_file(argv[16]);
    const std::string cmake = read_file(argv[17]);
    const std::filesystem::path legacy_editor_root = argv[18];
    const std::filesystem::path legacy_obs_root = argv[19];

    bool ok = require_token(hotkey_header, "void title_hotkeys_register();",
                            "OBS hotkey registration API");
    ok = require_token(hotkey_header, "TitleProgramHotkeyHandler",
                       "Editor command callback boundary") &&
         ok;
    ok = require_token(hotkey_implementation, "#include <obs-module.h>",
                       "OBS implementation ownership") &&
         ok;
    ok = require_token(hotkey_implementation, "obs_hotkey_register_source(",
                       "OBS source hotkey registration") &&
         ok;
    ok = require_token(plugin_header, "#define PLUGIN_NAME \"flux-motion\"",
                       "OBS module identity") &&
         ok;
    ok = require_token(plugin_main, "OBS_DECLARE_MODULE()",
                       "OBS module entry point") &&
         ok;
    ok = require_token(plugin_main, "#include <obs-frontend-api.h>",
                       "OBS frontend lifecycle ownership") &&
         ok;
    ok = require_token(plugin_main, "#include \"title-hotkeys.h\"",
                       "plugin lifecycle dependency") &&
         ok;
    ok = require_token(plugin_main, "title_hotkeys_register();",
                       "plugin registration lifecycle") &&
         ok;
    ok = require_token(plugin_main, "title_hotkeys_unregister();",
                       "plugin cleanup lifecycle") &&
         ok;
    ok = require_token(stinger_header, "void stinger_transition_register();",
                       "OBS transition registration API") &&
         ok;
    ok = require_token(stinger_implementation, "#include <obs-module.h>",
                       "OBS transition implementation ownership") &&
         ok;
    ok = require_token(stinger_implementation, "static obs_source_info info",
                       "OBS native transition descriptor") &&
         ok;
    ok = require_token(stinger_implementation, "obs_register_source(&info);",
                       "OBS native transition registration") &&
         ok;
    ok = require_token(plugin_main, "stinger_transition_register();",
                       "plugin transition lifecycle") &&
         ok;
    ok = require_token(audio_header, "#include <obs-module.h>",
                       "OBS audio runtime ownership") &&
         ok;
    ok = require_token(audio_header, "obs_source_t *source_",
                       "OBS source audio binding") &&
         ok;
    ok = require_token(audio_header, "std::unique_ptr<IAudioBackend>",
                       "shared audio output abstraction") &&
         ok;
    ok = require_token(audio_implementation, "\"obs-audio-backend.h\"",
                       "OBS audio backend adapter") &&
         ok;
    ok = require_token(audio_implementation,
                       "std::make_unique<obs_plugin::ObsAudioBackend>",
                       "OBS audio backend construction") &&
         ok;
    ok = require_token(source_header, "#include <obs-module.h>",
                       "OBS source API ownership") &&
         ok;
    ok = require_token(source_header, "void title_source_register();",
                       "OBS source registration API") &&
         ok;
    ok = require_token(source_header, "obs_source_t *source",
                       "OBS source transport API") &&
         ok;
    ok = require_token(source_header, "gs_texture_t *scene_a",
                       "OBS graphics presentation API") &&
         ok;
    ok = require_token(source_implementation, "#include <obs-module.h>",
                       "OBS source implementation ownership") &&
         ok;
    ok = require_token(source_implementation,
                       "void title_source_register()",
                       "OBS source registration implementation") &&
         ok;
    ok = verify_source_modules(source_module_root) && ok;
    ok = reject_token(video_header, "#include <obs",
                      "OBS dependency in Core video API") &&
         ok;
    ok = reject_token(video_implementation, "#include <obs",
                      "OBS dependency in Core video runtime") &&
         ok;
    ok = reject_token(audio_scheduler, "#include <obs",
                      "OBS dependency in Core audio scheduler") &&
         ok;
    ok = reject_token(transport_direction, "#include <obs",
                      "OBS dependency in Core transport direction") &&
         ok;
    ok = require_token(dock_lifecycle,
                       "title_hotkeys_set_program_command_handler(",
                       "Editor-to-OBS command callback wiring") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_PLUGIN_INTEGRATION_SOURCES",
                       "OBS integration source ownership") &&
         ok;
    ok = require_token(cmake, "../../plugins/obs/flux-motion/src/title-hotkeys.cpp",
                       "migrated hotkey source") &&
         ok;
    ok = require_token(cmake, "../../plugins/obs/flux-motion/src/plugin-main.cpp",
                       "migrated plugin entry point") &&
         ok;
    ok = require_token(cmake, "../../plugins/obs/flux-motion/src/stinger-transition.cpp",
                       "migrated OBS transition") &&
         ok;
    ok = require_token(cmake, "../../plugins/obs/flux-motion/src/title-audio-runtime.cpp",
                       "migrated OBS audio runtime") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_PLUGIN_SOURCE_HEADERS",
                       "OBS source header ownership") &&
         ok;
    ok = require_token(cmake, "../../packages/flux-common/Shared/rendering-engine/title-source.h",
                       "migrated OBS source API") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_PLUGIN_SOURCE_SOURCES",
                       "OBS source implementation ownership") &&
         ok;
    ok = require_token(cmake, "../../packages/flux-common/Shared/rendering-engine/title-source.cpp",
                       "migrated OBS source implementation") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_OBS_PLUGIN_SOURCE_MODULES",
                       "OBS source module ownership") &&
         ok;
    ok = require_token(cmake,
                       "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc",
                       "migrated OBS source registration module") &&
         ok;
    ok = require_token(cmake, "OBS_FXM_CORE_PLAYBACK_SOURCES",
                       "Core playback ownership") &&
         ok;
    ok = require_token(cmake, "Core/title-video-runtime.cpp",
                       "Core video runtime") &&
         ok;
    ok = require_token(cmake, "Core/audio-output-scheduler.h",
                       "Core audio scheduler") &&
         ok;
    ok = reject_token(cmake, "src/editor/title-hotkeys.cpp",
                      "legacy Editor hotkey source") &&
         ok;
    ok = reject_token(cmake, "src/obs/plugin-main.cpp",
                      "legacy OBS entry-point source") &&
         ok;
    ok = reject_token(cmake, "src/obs/stinger-transition.cpp",
                      "legacy OBS transition source") &&
         ok;
    ok = reject_token(cmake, "src/obs/title-audio-runtime.cpp",
                      "legacy OBS audio runtime source") &&
         ok;
    ok = reject_token(cmake, "src/obs/title-source.h",
                      "legacy OBS source API") &&
         ok;
    ok = reject_token(cmake, "src/obs/title-source.cpp",
                      "legacy OBS source implementation") &&
         ok;
    ok = reject_token(cmake, "src/obs/title-source/",
                      "legacy OBS source modules") &&
         ok;
    ok = reject_token(cmake, "src/obs/title-video-runtime.cpp",
                      "legacy OBS video runtime") &&
         ok;
    ok = !std::filesystem::exists(legacy_editor_root / "title-hotkeys.cpp") &&
         ok;
    ok = !std::filesystem::exists(legacy_editor_root / "title-hotkeys.h") &&
         ok;
    ok = !std::filesystem::exists(legacy_obs_root / "plugin-main.cpp") && ok;
    ok = !std::filesystem::exists(legacy_obs_root / "plugin-main.h") && ok;
    ok = !std::filesystem::exists(legacy_obs_root / "stinger-transition.cpp") &&
         ok;
    ok = !std::filesystem::exists(legacy_obs_root / "stinger-transition.h") &&
         ok;
    ok = !std::filesystem::exists(legacy_obs_root /
                                  "title-audio-runtime.cpp") &&
         ok;
    ok = !std::filesystem::exists(legacy_obs_root /
                                  "title-audio-runtime.h") &&
         ok;
    ok = !std::filesystem::exists(legacy_obs_root / "title-source.h") && ok;
    ok = !std::filesystem::exists(legacy_obs_root / "title-source.cpp") && ok;
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
