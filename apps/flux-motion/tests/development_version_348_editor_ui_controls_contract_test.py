from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
modern_h = read("Editor/fxm-modern-controls.h")
modern_cpp = read("Editor/fxm-modern-controls.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
properties_header = read("src/editor/properties-panel.h")
camera = read("src/editor/title-properties-panel.cpp")
effects = read("src/effects/effects-panel.cpp")
toolbar = read("src/editor/title-editor/document-shape-editing.inc")
editor_window = read("src/editor/title-editor/window-session.inc")
editor_events = read("src/editor/title-editor/signal-handlers.inc")

cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 348
assert build_version and int(build_version.group(1)) >= 348

# The duplicate inspector header/history row is gone.
assert 'FluxMotionPropertiesHistory' not in properties
assert 'history_label' not in properties

# One canonical asset/state function and one compact navigation wrapper are
# shared by Properties, Camera, Effects and the contextual transform toolbar.
assert 'QIcon fxm_keyframe_diamond_icon(bool active, bool outlined = false)' in modern_h
assert 'class FxmKeyframeControls final' in modern_cpp
assert 'Qt::LeftArrow' in modern_cpp
assert 'Qt::RightArrow' in modern_cpp
assert 'kKeyframeButtonExtent = 18' in modern_cpp
assert 'fxm_make_keyframe_controls' in properties
assert 'fxm_make_keyframe_controls' in camera
assert 'fxm_make_keyframe_controls' in effects
assert 'fxm_make_keyframe_controls' in toolbar
assert 'DeleteAllKeyframes' in camera
assert 'DeleteAllKeyframes' in effects
assert 'DeleteAllKeyframes' in toolbar

# Navigation returns to the editor's canonical playhead path.
assert 'keyframe_navigation_requested' in properties_header
assert '&TitleEditor::on_playhead_changed' in read(
    "src/editor/title-editor/commands-docks.inc"
)

# Camera-style fields are normalized at startup and for dynamically polished
# editor widgets.
assert 'fxm_apply_editor_field_style' in editor_window
assert 'fxm_apply_editor_field_style(watched_widget)' in editor_events
assert 'min-height:18px;max-height:20px' in modern_cpp

# No ordinary editor control is instantiated as a legacy square checkbox.
editor_sources = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in (ROOT / "src" / "editor").rglob("*")
    if path.is_file() and path.suffix in {".cpp", ".h", ".inc"}
)
assert not re.search(r"new\s+QCheckBox\b", editor_sources)
assert 'TransformLockCheckBox' not in editor_sources
assert 'new FxmSwitch' in editor_sources

print("Development Version 348 editor UI controls contract: PASS")
