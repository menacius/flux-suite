from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
header = read("src/editor/title-dock.h")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
cues = read("src/editor/title-dock/list-selection-cues.inc")
source_dimensions = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
source_registration = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
locale = read("data/locale/en-US.ini")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 361
assert build_version >= 361
assert manifest["development_version"] >= 361

# Duplicate Scene is meaningful only while Preview/Program (Studio Mode) is
# active. New OBS versions store it in user.ini; global.ini remains fallback.
assert "obs_frontend_preview_program_mode_active()" in cues
assert "obs_frontend_get_user_config()" in cues
assert 'config_has_user_value(\n            user_config, "BasicWindow", "SceneDuplicationMode")' in cues
assert 'config_get_bool(\n        global_config, "BasicWindow", "SceneDuplicationMode")' in cues

# The fallback monitor is a distinct read-only splitter section while docked.
# Its authoritative widget may move to the Live Text pop-out without becoming
# editable or being duplicated.
assert "live_cue_preview_panel_ = new QFrame(sections_);" in lifecycle
assert "sections_->addWidget(live_cue_preview_panel_);" in lifecycle
assert "live_layout->addWidget(live_cue_preview_panel_);" not in lifecycle
assert 'setProperty("fxmReadOnly", true)' in lifecycle
assert "Qt::WA_TransparentForMouseEvents" in lifecycle
assert "Qt::WA_NoMousePropagation" in lifecycle
assert "setFocusPolicy(Qt::NoFocus)" in lifecycle
assert "setAcceptDrops(false)" in lifecycle

# With Duplicate Scene enabled the frozen private source is attached only to
# OBS Preview, fills the canvas, remains locked, and is removed on route exit.
for token in (
    "obs_frontend_get_current_preview_scene()",
    "obs_scene_duplicate(",
    "OBS_SCENE_DUP_PRIVATE_REFS",
    "obs_frontend_set_current_preview_scene(",
    "obs_scene_add(",
    "obs_sceneitem_set_bounds_type(preview_item, OBS_BOUNDS_STRETCH)",
    "obs_sceneitem_set_locked(preview_item, true)",
    "obs_sceneitem_set_order(preview_item, OBS_ORDER_MOVE_TOP)",
    "obs_sceneitem_remove(live_cue_obs_preview_item_)",
    "sync_live_cue_preview_output_route();",
):
    assert token in cues or token in lifecycle
assert "live_cue_preview_panel_->hide();" in cues
assert "live_cue_preview_panel_->show();" in cues

# The OBS-rendered private source must use the immutable preview snapshot for
# dimensions, render, duration and seeking rather than the mutable title store.
assert source_dimensions.count("source_dimension_title_for_source(data)") >= 2
assert "static std::shared_ptr<Title> source_dimension_title_for_source(" in source_runtime
assert "? editor_title_snapshot_for_source(data)" in source_runtime
assert source_registration.count("editor_title_snapshot_for_source(data)") >= 4

assert 'OBSTitles.CuePreviewReadOnly="Cue Preview (read-only)"' in locale
assert "CuePreviewReadOnlyTooltip" in locale

print("Development Version 361 OBS Duplicate Scene preview routing contract: PASS")
