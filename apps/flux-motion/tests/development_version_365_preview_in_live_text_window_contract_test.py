from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
cues = read("src/editor/title-dock/list-selection-cues.inc")

cmake_version = int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
build_version = int(build_info.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0])
assert cmake_version >= 365
assert build_version >= 365
assert manifest["development_version"] >= 365

open_start = cues.index("void TitleDock::open_live_text_columns_window()")
restore_start = cues.index("void TitleDock::restore_live_text_table_to_dock()")
open_window = cues[open_start:restore_start]
restore_end = cues.index("namespace {", restore_start)
restore = cues[restore_start:restore_end]

# The single authoritative Preview widget follows the Live Text editor into the
# external window; its Canvas, status and Take/Cancel children move with it.
assert "live_cue_preview_panel_->setParent(dialog);" in open_window
assert "layout->addWidget(live_cue_preview_panel_, 1);" in open_window
assert "sync_live_cue_preview_output_route();" in open_window
assert "new QFrame" not in open_window

# Closing the external editor returns the same widget to splitter position 2
# and immediately reconciles local-vs-OBS Preview visibility.
for token in (
    "dialog->layout()->removeWidget(live_cue_preview_panel_);",
    "live_cue_preview_panel_->setParent(sections_);",
    "sections_->insertWidget(2, live_cue_preview_panel_);",
    "sections_->setStretchFactor(2, 1);",
    "sync_live_cue_preview_output_route();",
):
    assert token in restore

# If the dock is destroyed with the pop-out still open, the Preview is
# reparented before deleting the dialog and cannot be destroyed twice.
destructor = lifecycle[lifecycle.index("TitleDock::~TitleDock()"):
                       lifecycle.index("void TitleDock::load_dock_settings()")]
assert "live_cue_preview_panel_->setParent(container_);" in destructor
assert destructor.index("live_cue_preview_panel_->setParent(container_);") < destructor.index("delete dialog;")

# The monitor remains non-editable after either reparenting direction.
for token in (
    'setProperty("fxmReadOnly", true)',
    "Qt::WA_TransparentForMouseEvents",
    "Qt::WA_NoMousePropagation",
    "setFocusPolicy(Qt::NoFocus)",
    "setAcceptDrops(false)",
):
    assert token in lifecycle

# Canvas input is rejected independently of Qt mouse transparency, and every
# item in the private OBS duplicate (including group children) is locked.
canvas_header = read("src/canvas/canvas-preview.h")
canvas_events = read("src/canvas/canvas-preview/geometry-selection.inc")
assert "void set_read_only(bool read_only);" in canvas_header
assert "if (read_only_ && ev)" in canvas_events
for event_type in (
    "QEvent::MouseButtonPress",
    "QEvent::MouseMove",
    "QEvent::DragEnter",
    "QEvent::DragMove",
    "QEvent::Drop",
    "QEvent::TouchBegin",
):
    assert event_type in canvas_events
assert "live_cue_preview_canvas_->set_read_only(true);" in lifecycle
assert "obs_scene_enum_items(\n        private_preview_scene, lock_private_obs_preview_item, nullptr);" in cues
assert "obs_sceneitem_group_enum_items(\n            item, lock_private_obs_preview_item, nullptr);" in cues
assert "obs_sceneitem_set_locked(item, true);" in cues

print("Development Version 365 Preview-in-Live-Text-window contract: PASS")
