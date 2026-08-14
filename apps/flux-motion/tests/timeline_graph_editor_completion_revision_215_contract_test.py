#!/usr/bin/env python3
"""Source contract for Development Version 215 Timeline/Graph completion."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
header = read("src/timeline/timeline-widget.h")
timeline = read("src/timeline/timeline-widget.cpp")
graph = read("src/timeline/temporal-graph-editor.inc")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
layer_stack = read("src/layers/layer-stack-widget.cpp")
toolbar = read("src/editor/title-editor/commands-docks.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/EDITOR_WORKFLOW.md")
schema = read("Core/title-serialization-schema.h")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build

# Sub-frame graph time and explicit snap modifier.
assert "graph_x_to_time(ev->pos().x())" in graph
assert "graph_x_to_time(graph_drag_start_.x())" in graph
assert "Qt::ControlModifier | Qt::MetaModifier" in graph
assert "time_delta = snap_time(desired_global)" in graph
assert "x_to_time(ev->pos().x()) - x_to_time(graph_drag_start_.x())" not in graph

# Channel switching is a view change and four-channel groups are complete.
channel_switch = graph[graph.index("void TimelineWidget::set_graph_channel_mode"):
                       graph.index("void TimelineWidget::select_graph_property")]
assert "selected_keyframes_.clear()" not in channel_switch
assert "Fourth = 4" in header
assert "std::array<double, 4> channel_values" in header
assert "std::min<size_t>(4, scalar_group.size())" in hierarchy
assert 'scalar_group_timeline_property("text_color"' in hierarchy
assert 'scalar_group_timeline_property("fill_color"' in hierarchy
assert "timeline_property_channel_label" in hierarchy
assert 'argb_names[] = {"A", "R", "G", "B"}' in hierarchy
assert "std::min(4, property.graph_channel_count())" in hierarchy
assert "graph_channel_fourth" in toolbar
assert "timeline_->graph_channel_label(3)" in toolbar
assert "keyframeValueW" in layer_stack
assert "graph_mode_for_property_channel" in layer_stack

# Clipboard retargeting, vector compatibility and collision replacement.
assert "clipboard_target_property" in header and "clipboard_target_property" in timeline
assert "clipboard_property_compatible" in timeline
assert "vector3_keyframe_from_legacy" in timeline
assert "legacy_keyframe_from_vector3" in timeline
assert "erase_keyframes_at(prop, local_time)" in timeline
assert "replaced_keyframes" in timeline
assert "redirect_single_track" in timeline
assert "prop.is_hold_only()" in timeline
assert "TemporalInterpolationMode::Hold" in timeline

# Complete editing affordances in both timeline modes.
assert "OBSTitles.Copy" in graph
assert "OBSTitles.Cut" in graph
assert "OBSTitles.Paste" in graph
assert "OBSTitles.Delete" in graph
assert "void TimelineWidget::mouseDoubleClickEvent" in timeline
assert "add_keyframe_at" in timeline
assert "show_temporal_velocity_dialog" in timeline
assert "1.0 / 240.0 + 1e-9" in timeline
assert "paste_keyframes_at(" in graph and "false" in graph
assert "emit keyframe_structure_changed()" in timeline
assert "emit keyframe_easing_changed()" in timeline

assert "sub-frame keyframe movement" in readme
assert "# Development Version 215 — Timeline and Graph Editor Completion" in changelog
assert "sub-frame" in note
assert "Camera switching" in note
assert "It is serialized" in note
assert "kCurrentDevelopmentVersion = 243" in schema

print("Development Version 215 Timeline and Graph Editor completion contract passed")
