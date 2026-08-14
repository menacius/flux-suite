from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
dock_header = read("src/editor/title-dock.h")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
cues = read("src/editor/title-dock/list-selection-cues.inc")
population = read("src/editor/title-dock/live-text-cache-playlist.inc")
source_header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
hotkeys = read("../../plugins/obs/flux-motion/src/title-hotkeys.cpp")
locale = read("data/locale/en-US.ini")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 360
assert build_version >= 360
assert manifest["development_version"] >= 360

assert "bool enterPreview(const std::shared_ptr<Title> &title, int row);" in dock_header
assert "void leavePreview();" in dock_header
assert "bool isPreviewReady() const;" in dock_header
assert "select_row_before_cue_ = false" in dock_header
assert "live_cue_preview_canvas_" in dock_header

assert "act_live_text_columns_window_ = live_text_settings_menu->addAction" in cues
assert "btn_live_text_columns_window_" not in dock_header
assert "SelectRowBeforeCue" in cues
assert "kLegacyShowPreviewBeforeCueKey" in lifecycle
assert "settings.value(legacy_preview_key, false).toBool()" in lifecycle

pause = cues.index("if (title.playback_mode == 2)")
loop = cues.index("if (title.playback_mode == 1", pause)
frame_zero = cues.index("return 0.0;", loop)
assert pause < loop < frame_zero
assert "clone_title_snapshot(title)" in cues
assert "apply_live_text_runtime_binding" in cues
assert "effective_live_text_cue_value" in cues
assert "set_playback_active(false)" in cues
assert "prepareLiveCueForPlayback(title, row)" in cues
assert "preloadLiveCues(title, row, 2)" in cues

assert "enum class TitlePlaybackState" in source_header
for state in ("Idle", "PreviewReady", "Queued", "Playing", "Paused", "Stopped"):
    assert state in source_header
assert "bool enterPreview(obs_source_t *source" in source_header
assert "void leavePreview(obs_source_t *source)" in source_header
assert "bool isPreviewReady(obs_source_t *source)" in source_header
assert "data->playing = false;" in source_runtime
assert "TitlePlaybackState::PreviewReady" in source_runtime

assert "preview && current" in lifecycle
assert "waiting_for_prerender || preview" in lifecycle
assert "live_cue_row_is_ending(*current_title, row) && !preview" in population

for action in ("CueToProgram", "Uncue", "CueLast"):
    assert action in hotkeys
for key in (
    "OBSTitles.SelectRowBeforeCue",
    "OBSTitles.CueToProgramHotkey",
    "OBSTitles.UncueHotkey",
    "OBSTitles.CueLastHotkey",
):
    assert key in locale

print("Development Version 360 preview-before-cue and Program hotkeys contract: PASS")
