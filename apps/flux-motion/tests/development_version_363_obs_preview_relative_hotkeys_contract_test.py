from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
dock_header = read("src/editor/title-dock.h")
cues = read("src/editor/title-dock/list-selection-cues.inc")
hotkey_header = read("../../plugins/obs/flux-motion/src/title-hotkeys.h")
hotkeys = read("../../plugins/obs/flux-motion/src/title-hotkeys.cpp")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 363
assert build_version >= 363
assert manifest["development_version"] >= 363

# OBS 31+ stores Studio Mode interface state in user.ini. A migrated global.ini
# value remains a fallback, but must not override an explicit user value.
user_lookup = cues.index("config_t *user_config = obs_frontend_get_user_config();")
user_value = cues.index("config_has_user_value(", user_lookup)
global_lookup = cues.index("obs_frontend_get_global_config()", user_value)
assert user_lookup < user_value < global_lookup
assert '"BasicWindow", "SceneDuplicationMode"' in cues[user_lookup:global_lookup]

# A private reference-copy is installed as the actual OBS Preview scene. This
# works even when OBS initially reports the Program scene as Preview and avoids
# mutating either the Program scene or the persistent scene collection.
for token in (
    "obs_scene_duplicate(",
    "OBS_SCENE_DUP_PRIVATE_REFS",
    "live_cue_obs_private_preview_scene_",
    "live_cue_obs_previous_preview_scene_source_",
    "obs_frontend_set_current_preview_scene(",
    "obs_scene_release(live_cue_obs_private_preview_scene_)",
):
    assert token in cues or token in dock_header
assert "preview_is_program" not in cues
for event in (
    "OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED",
    "OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED",
    "OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED",
):
    assert event in lifecycle

# Next/Previous Cue are marshalled to the dock, where PreviewReady is the
# navigation base while the two-stage row-selection mode is enabled. They wrap without
# starting Program playback; the existing second-Cue/Cue-to-Program path still
# performs Take.
for command in ("NextCue", "PreviousCue"):
    assert command in hotkey_header
    assert f"TitleProgramHotkeyCommand::{command}" in hotkeys
    assert f"TitleProgramHotkeyCommand::{command}" in cues
assert "select_row_before_cue_ && isPreviewReady()" in cues
assert "base = live_cue_preview_row_;" in cues
assert "(base + delta + row_count) % row_count" in cues
relative_start = cues.index("if (command == TitleProgramHotkeyCommand::NextCue ||")
relative_end = cues.index("if (command == TitleProgramHotkeyCommand::Uncue)", relative_start)
relative = cues[relative_start:relative_end]
assert "if (select_row_before_cue_)" in relative
assert "enterPreview(title, row);" in relative
assert "cue_live_text_row_for_title(title, row, true);" in relative

print("Development Version 363 OBS Preview and relative-cue hotkeys contract: PASS")
