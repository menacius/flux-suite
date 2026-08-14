#!/usr/bin/env python3
"""Source contract for 3D depth and material interaction AE-style 3D authoring and transparency."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]

def text(relative: str) -> str:
    path = ROOT / relative
    assert path.exists(), f"missing source file: {relative}"
    return path.read_text(encoding="utf-8")

cmake = text("CMakeLists.txt")
build = text("src/core/build-info.h")
schema = text("Core/title-serialization-schema.h")
model = text("Core/layer-model.h")
stack_h = text("src/layers/layer-stack-widget.h")
stack = text("src/layers/layer-stack-widget.cpp")
editor = text("src/editor/title-editor/commands-docks.inc")
props_h = text("src/editor/properties-panel.h")
props_ui = text("src/editor/properties-panel/popup-state.inc")
props_refresh = text("src/editor/properties-panel/selection-refresh.inc")
props_actions = text("src/editor/properties-panel/auto-style-and-property-actions.inc")
props_context = text("src/editor/properties-panel/construction-transform-character.inc")
transform_h = text("src/rendering/layer-transform-3d.h")
transform = text("src/rendering/layer-transform-3d.cpp")
compositor = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
readme = text("README.md")
changelog = text("docs/CHANGELOG.md")
guide = text("docs/EFFECTS_AND_EXTENSIONS.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)
assert "case 204:" in schema

# The final layer-list column owns the visible switch and emits one model event.
assert "layer_dimension_mode_changed" in stack_h
assert 'add_header(QStringLiteral("2D/3D"), kLayerDimensionWidth)' in stack
assert "auto *dimension_toggle = new QToolButton(row_widget)" in stack
assert "hl->addWidget(dimension_toggle);" in stack
assert stack.index("hl->addWidget(dimension_toggle);") > stack.index("hl->addWidget(matte_invert)")
assert "LayerDimensionMode::ThreeD" in stack
assert "QTimer::singleShot(0, layers_" in editor

# 3D expands the existing rows in place, with Z in the final numeric column.
for member in (
    "transform_position_field_z_", "transform_scale_field_z_",
    "transform_anchor_field_z_", "transform_rotation_field_x_",
    "transform_rotation_field_y_", "transform_rotation_field_z_",
    "transform_orientation_row_", "transform_grid_",
    "transform_scale_axis_label_x_", "transform_scale_axis_label_y_",
):
    assert member in props_h
for token in (
    "transform_grid->addWidget(transform_position_field_z_, 0, 4",
    "transform_grid->addWidget(transform_scale_field_z_, 1, 4",
    "transform_grid->addWidget(transform_anchor_field_z_, 2, 4",
    "transform_grid->addWidget(transform_rotation_field_z_, 3, 4",
    "orientation_layout->addWidget(field_orientation_z)",
    "dimension_row->hide()",
):
    assert token in props_ui
assert "update_transform_dimension_ui" in props_refresh
assert "show_3d ? 4 : 2" in props_refresh
assert 'show_3d ? QStringLiteral("X")' in props_refresh
assert 'show_3d ? QStringLiteral("Y")' in props_refresh
assert "set_vec3_kf_icon" in props_refresh
assert "toggle_vec3_keyframe" in props_actions
assert "toggle_scalar3_keyframe" in props_actions
assert "install_vec3_delete_all" in props_context

# Renderer-facing code consumes one migration-safe Vector3 facade while JSON
# remains the existing XY animated property plus separate Z channels.
for token in (
    "struct LayerVector3Value", "evaluated_layer_position_3d",
    "evaluated_layer_scale_3d", "evaluated_layer_anchor_3d",
    "evaluated_layer_rotation_3d", "evaluated_layer_orientation_3d",
):
    assert token in model
assert "LayerVector3Value p = evaluated_layer_position_3d" in transform
assert "LayerVector3Value s = evaluated_layer_scale_3d" in transform

# Transparent simple planes render after opaque planes, far-to-near, and keep
# authored order as the deterministic equal-depth tie-break.
assert "hardware_depth_transparent_candidate" in transform_h
assert "simple_transparent_3d_candidate" in transform
for token in (
    "std::vector<const Layer *> opaque_layers",
    "std::vector<const Layer *> transparent_layers",
    "left_depth > right_depth",
    "authored_index(left) < authored_index(right)",
    "for (const Layer *layer : opaque_layers)",
    "for (const Layer *layer : transparent_layers)",
    "run.transparent_count >= 2",
    "draw_depth_layer(*persistent, false)",
):
    assert token in compositor

assert "transparent depth ordering" in readme
assert "Development Version 204 — AE-style 3D layer UI and transparent compositing" in changelog
assert "3D depth and material interaction" in guide
assert "2D/3D" in guide and "far-to-near" in guide

print("3D depth and material interaction 3D GUI/transparency source contract passed")
