#!/usr/bin/env python3
"""Source contract for Development Version 212.3 editor usability and pacing."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    path = ROOT / relative
    assert path.exists(), f"missing source file: {relative}"
    return path.read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
preview_h = read("src/canvas/canvas-preview.h")
preview_view = read("src/canvas/canvas-preview/preview-cache-view.inc")
preview_events = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
motion_path = read("src/canvas/canvas-preview/spatial-bezier-keyframes.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
editor_h = read("src/editor/title-editor.h")
signals = read("src/editor/title-editor/signal-handlers.inc")
playback_tools = read("src/editor/title-editor/layout-template-tools.inc")
footer = read("src/editor/title-editor/editor-audio-preview.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")

# This is a behavior-only patch package; authored data remains Version 212.
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build_info
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)

# A path segment must not steal the layer context menu. Only an explicit
# keyframe vertex is allowed to open spatial-keyframe actions.
menu_start = motion_path.index("bool CanvasPreview::show_position_motion_path_context_menu")
menu_end = motion_path.index("\nbool CanvasPreview::update_position_motion_path_hover", menu_start)
menu_body = motion_path[menu_start:menu_end]
assert "hit_test_position_keyframe_vertices" in menu_body
assert "hit_test_position_motion_path" not in menu_body
assert "if (keyframe_index < 0)\n        return false;" in menu_body

# Ordinary 2D Move/Rotate/Resize gestures receive the same monitor-cadence
# priority already used by 3D gizmos and editor-camera manipulation.
for token in (
    "const bool resize_drag =",
    "const bool canvas_transform_drag = drag_changed_ &&",
    "drag_mode_ == DragMode::Move",
    "drag_mode_ == DragMode::Rotate",
    "editor_camera_transform_only_pending_ || canvas_transform_drag",
):
    assert token in preview_events, f"missing canvas pacing token: {token}"

# FPS is measured from successful project-rate presents, not frame preparation,
# and render-cost statistics use their own timer.
assert "QElapsedTimer diagnostics_window_timer_;" in preview_h
assert "void record_live_playback_present();" in preview_h
assert "if (project_rate_present)\n            record_live_playback_present();" in preview_view
assert "void CanvasPreview::record_live_playback_present()" in preview_view
assert "void CanvasPreview::reset_live_playback_fps_measurement()" in preview_view
assert "CanvasPreview::DiagnosticsSample CanvasPreview::take_diagnostics_sample()" in preview_view
assert "double CanvasPreview::live_playback_fps() const" in preview_view
render_start = preview_events.index("void CanvasPreview::render_to_frame()")
render_end = preview_events.index("\nvoid CanvasPreview::render_dirty_frame_if_due()", render_start)
assert "++live_fps_frame_count_" not in preview_events[render_start:render_end]
assert "render_cost_accumulator_ns_ += cost_ns" in preview_events[render_start:render_end]

# Fractional frame durations alternate floor/ceil intervals instead of fixing
# 60 Hz at 17 ms or 29.97 Hz at 33 ms.
assert "playback_timer_fractional_error_ms_" in editor_h
assert "playback_timer_frame_duration_ms_" in editor_h
assert "void TitleEditor::reset_playback_timer_cadence()" in signals
assert "void TitleEditor::schedule_next_playback_timer_interval()" in signals
assert "std::floor(exact_ms)" in signals
assert "play_timer_->setInterval(interval_ms);" in signals
assert playback_tools.count("reset_playback_timer_cadence();") >= 3

# Frequent layout events from property-panel descendants no longer trigger the
# 90 ms dock-layout suppression path.
assert "const bool main_window_layout_transition = watched == this" in signals
assert "const bool dock_structure_transition = watched_dock" in signals
assert "QWidget *watched_widget = qobject_cast<QWidget *>(watched);" in signals
assert signals.index("QWidget *watched_widget = qobject_cast<QWidget *>(watched);") < signals.index("const bool watched_in_editor = watched_canvas_window")
assert "const bool editor_widget = watched_widget" not in signals

# The 3D controls live in the zoom/adaptive row, immediately after Adaptive,
# rather than consuming a separate canvas row.
adaptive_add = commands.index("canvas_zoom_layout->addWidget(adaptive_rendering);")
separator_add = commands.index("canvas_zoom_layout->addWidget(editor_3d_separator);")
bar_add = commands.index("canvas_zoom_layout->addWidget(editor_3d_bar);")
row_add = commands.index("canvas_layout->addWidget(canvas_zoom_bar);")
assert adaptive_add < separator_add < bar_add < row_add
assert "auto *editor_3d_bar = new QWidget(canvas_zoom_bar);" in commands
assert "canvas_layout->addWidget(editor_3d_bar);" not in commands

# Diagnostic coloring tolerates normal sampling jitter.
assert "fps_measurement_tolerance = std::max(0.5, target_fps * 0.01)" in footer

# Package documentation is explicit and does not claim a schema bump.
assert "editor frame pacing" in readme
assert "Development Version 212.3 — Editor Usability and Frame Pacing" in changelog
assert "direct keyframe handle" in note
assert "Playback cadence uses fractional millisecond accumulation" in note

print("Development Version 212.3 editor usability and frame pacing contract passed")
