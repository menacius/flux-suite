#!/usr/bin/env python3
"""Source contract for Development Version 213 editor stability."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]

def read(relative: str) -> str:
    path = ROOT / relative
    assert path.exists(), f"missing source file: {relative}"
    return path.read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
editor_h = read("src/editor/title-editor.h")
window = read("src/editor/title-editor/window-session.inc")
signals = read("src/editor/title-editor/signal-handlers.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
document = read("src/editor/title-editor/document-shape-editing.inc")
events = read("src/editor/title-editor/editor-events.inc")
canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
canvas_menu = read("src/canvas/canvas-preview/gpu-frame-rendering.inc")
layer_stack = read("src/layers/layer-stack-widget.cpp")
timeline_h = read("src/timeline/timeline-widget.h")
timeline = read("src/timeline/timeline-widget.cpp")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")

# Package build advances, authored data does not.
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)

# The precise monitor timer is interaction-only and dynamic text is bounded.
assert "gui_refresh_timer_->setInterval(16);" in window
assert "const bool pointer_drag = QApplication::mouseButtons() != Qt::NoButton;" in window
assert "gui_refresh_timer_->stop();" in window
assert "if (canvas_ && canvas_->underMouse())" in window
assert "clock_timer_->setInterval(100);" in window
assert "title_has_dynamic_text_layer(title_)" in window
pacing_start = signals.index("void TitleEditor::update_display_refresh_pacing()")
pacing_end = signals.index("\nbool TitleEditor::eventFilter", pacing_start)
assert "gui_refresh_timer_->start()" not in signals[pacing_start:pacing_end]

# Global filtering is fast-gated and editor-owned; unrelated OBS docks cannot pause it.
assert "switch (type)" in signals
assert "const bool watched_in_editor = watched_canvas_window ||" in signals
assert "auto *watched_dock = watched_in_editor" in signals
assert "const bool dock_structure_transition = watched_dock" in signals
assert signals.count("installEventFilter(this)") == 0
assert window.count("qApp->installEventFilter(this)") == 1
assert "dock->installEventFilter(this)" not in read("src/editor/title-editor/panels-colors.inc")

# One canonical selection path owns reflection and panel updates.
assert "void synchronize_layer_selection(" in editor_h
assert re.search(r"bool\s+synchronizing_layer_selection_\s*=\s*false;", editor_h)
assert "QScopedValueRollback<bool> guard(synchronizing_layer_selection_, true);" in events
assert "synchronized_layer_selection_ != normalized" in events
assert "layers_->set_selected_layers(normalized);" in events
assert "canvas_->set_selected_layers(normalized);" in events
assert "timeline_->set_selected_layers(normalized);" in events
assert "connect(layers_, &LayerStack::layer_selected,\n            this, &TitleEditor::on_layer_selected);" in commands
assert "connect(canvas_, &CanvasPreview::layer_clicked,\n            this, &TitleEditor::on_layer_selected);" in document
assert "if (sel_layer_id_ == next && selected_layer_ids_ == visible_ids)" in canvas
assert "if (selected_layer_ids_ == normalized && sel_layer_id_ == next_primary" in timeline
assert "if (current_ids == desired_ids && current_primary == desired_primary)" in layer_stack

# Context actions depend on the actual pointer target, not stale selection.
menu_start = canvas_menu.index("void CanvasPreview::contextMenuEvent")
menu_body = canvas_menu[menu_start:]
assert "std::shared_ptr<Layer> hit = forced_layer_selection" in menu_body
assert "if (forced_layer_selection)" in menu_body and "selection = context_menu_selection_override_;" in menu_body
assert "show_hide->setEnabled(has_selection);" in menu_body
assert "duplicate->setEnabled(has_selection);" in menu_body

# Canvas owns E for Rotate; editor Free Transform remains available elsewhere.
assert "(!canvas_has_focus && key_event->key() == Qt::Key_E)" in signals
assert "Canvas focus reserves E for the 3D Rotate gizmo" in events

# Profiling is aggregated rather than emitted for every paint.
assert "QElapsedTimer paint_profile_window_;" in timeline_h
assert "paint_profile_window_.elapsed() < 1000" in timeline
assert 'FXM_LOG_TRACE("Performance"' in timeline
assert "Editor panel refresh layer=%1 costUs=%2" in events
assert "Editor layout transition object=%1 event=%2" in signals

# Documentation names the package and compatibility boundary.
assert "selection synchronization are more deterministic" in readme
assert "Development Version 213 — Editor Stability and Interaction Consistency" in changelog
assert "selection-synchronization path" in note

print("Development Version 213 editor stability and interaction consistency contract passed")
