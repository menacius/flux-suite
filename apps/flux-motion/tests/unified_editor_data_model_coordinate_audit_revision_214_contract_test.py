#!/usr/bin/env python3
"""Source contract for Development Version 214."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
preview_h = read("src/canvas/canvas-preview.h")
preview = read("src/canvas/canvas-preview/preview-cache-view.inc")
keyboard = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
footer = read("src/editor/title-editor/editor-audio-preview.inc")
window = read("src/editor/title-editor/window-session.inc")
layout = read("src/editor/title-editor/layout-template-tools.inc")
properties = read("src/editor/properties-panel/popup-state.inc")
selection = read("src/editor/title-editor/editor-events.inc")
layer_stack = read("src/layers/layer-stack-widget.cpp")
timeline = read("src/timeline/timeline-widget.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")
schema = read("Core/title-serialization-schema.h")
pointer = read("src/canvas/canvas-preview/pointer-events.inc")
geometry = read("src/canvas/canvas-preview/geometry-selection.inc")
overlay = read("src/canvas/canvas-preview/canvas-overlay-paint.inc")
gpu = read("src/canvas/canvas-preview/gpu-frame-rendering.inc")
layer_model = read("Core/layer-model.h")
shape_editing = read("src/editor/title-editor/document-shape-editing.inc")
transform_ui = read("src/editor/properties-panel/construction-transform-character.inc")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build

# One real elapsed-time diagnostics window per footer tick.
assert "struct DiagnosticsSample" in preview_h
assert "DiagnosticsSample take_diagnostics_sample();" in preview_h
assert "diagnostics_timer->setInterval(1000);" in commands
assert "diagnostics_timer->setTimerType(Qt::PreciseTimer);" in commands
assert "sample.elapsed_ms = std::max<qint64>(1, diagnostics_window_timer_.restart())" in preview
assert "1000.0 * double(live_fps_frame_count_) / double(sample.elapsed_ms)" in preview
assert "double(render_cost_accumulator_ns_)" in preview
assert "render_cost_accumulator_ns_ += cost_ns" in keyboard
assert "canvas_->take_diagnostics_sample()" in footer
assert "timeline_->update();" not in footer
assert "250" not in preview[preview.index("double CanvasPreview::live_playback_fps() const"):preview.index("void CanvasPreview::refresh_runtime_dynamic_content()")]

# Local authored values, explicit gizmo-axis space, and whole-track reparenting.
assert "Local Position" in properties
assert "Local Axes" in properties and "Parent Axes" in properties and "World Axes" in properties
assert "gizmo axis orientation only" in properties
assert "editor_reparent_layer_with_parent_bind" in window
assert "editor_store_parent_bind_matrix" in window
assert "editor_store_parent_bind_transform" in window
assert "destination_inverse * source_basis" in window
assert "source_basis * destination_inverse" in window
assert "keyframesBefore=%9 keyframesAfter=%10" in window
assert "duration * project_fps" not in window
assert "kMaximumBakedSamples = 12000" not in window
assert "set_layer_dimension_mode_preserving_position_track" in layer_model
assert "layer.position_3d_path_enabled = false;" in layer_model
assert "set_layer_dimension_mode_preserving_position_track(*layer, mode);" in commands
assert "set_layer_dimension_mode_preserving_position_track(*layer_, mode);" in transform_ui
assert layout.count("editor_reparent_layer_with_parent_bind(") >= 4
assert "editor_restore_world_transform_track_for_parenting(" not in layout
assert "next_parent_id == layer->transform_parent_id" in commands
assert "layer->parent_id == group_id" in layout

# Every canvas-space edit is converted back into the effective parent-local basis.
assert "editor_parent_local_point_to_canvas" in geometry
assert "editor_canvas_point_to_parent_local" in geometry
assert "editor_canvas_delta_to_parent_local" in geometry
assert "editor_evaluated_local_position_xy" in geometry
assert "editor_set_local_position_xy" in geometry
assert "editor_canvas_rotation_delta_to_local" in geometry
assert "layer->position.evaluate(lt).x + dx" not in pointer
assert "editor_set_local_position_xy(*layer, lt, current_local + local_delta);" in pointer
assert "start_canvas_position" in pointer and "next_local_position" in pointer
assert "layer_local_delta" in pointer and "parent_local_delta" in pointer
assert "selected_local_position" in overlay
assert "child_local_position" in overlay
assert "editor_evaluated_local_position_xy(*layer, lt)" in gpu
assert "editor_translate_layer_in_canvas_for_parenting" in window
assert shape_editing.count("editor_translate_layer_in_canvas_for_parenting(") >= 4
assert "editor_layer_canvas_bounds_for_grouping" in shape_editing

# One row model and one guarded selection state across editor surfaces.
assert "const auto shared_timeline_rows = timeline_rows(title_);" in layer_stack
assert "auto rows = timeline_rows(title_);" in timeline
assert 'QString("%1,%2,%3")' in hierarchy
assert 'prop.name() == "position" || prop.name() == "position_3d"' in hierarchy
assert "void TitleEditor::synchronize_layer_selection" in selection
assert "layers_->set_selected_layers(normalized);" in selection
assert "canvas_->set_selected_layers(normalized);" in selection
assert "timeline_->set_selected_layers(normalized);" in selection
assert layout.count("synchronize_layer_selection(") >= 9

assert "static parent-bind matrix" in readme
assert "# Development Version 215 — Timeline and Graph Editor Completion" in changelog
assert "All authored Transform values are stored and keyframed" in note
assert "layer-local coordinates" in note
assert "kCurrentDevelopmentVersion = 243" in schema

print("Development Version 214 unified editor data model and coordinate audit contract passed")
