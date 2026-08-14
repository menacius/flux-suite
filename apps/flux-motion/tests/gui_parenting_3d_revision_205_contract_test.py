#!/usr/bin/env python3
"""Source contract for Keyframe-safe parenting and grouping GUI and nested 3D parenting."""
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
props_h = text("src/editor/properties-panel.h")
props = text("src/editor/properties-panel/popup-state.inc")
shape_props = text("src/editor/properties-panel/construction-gradient-image-signals.inc")
image_props = text("src/editor/properties-panel/construction-transform-character.inc")
props_actions = text("src/editor/properties-panel/auto-style-and-property-actions.inc")
props_sync = text("src/editor/properties-panel/property-synchronization.inc")
title_props_h = text("src/editor/title-properties-panel.h")
title_props = text("src/editor/title-properties-panel.cpp")
editor_h = text("src/editor/title-editor.h")
toolbar = text("src/editor/title-editor/document-shape-editing.inc")
toolbar_state = text("src/editor/title-editor/playback-cache-preferences.inc")
parenting = text("src/editor/title-editor/window-session.inc")
transform = text("src/rendering/layer-transform-3d.cpp")
overlay_geometry = text("src/canvas/canvas-preview/transform-snap.inc")
overlay_paint = text("src/canvas/canvas-preview/keyboard-wheel-events.inc")
compositor = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
readme = text("README.md")
changelog = text("docs/CHANGELOG.md")
guide = text("docs/EDITOR_WORKFLOW.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)
assert "case 205:" in schema

# Compact property geometry and locks always follow the complete value set.
for token in (
    "spin->setFixedSize(70, 20)",
    "field->setFixedSize(90, 20)",
    "transform_grid->addWidget(transform_scale_field_z_, 1, 4",
    "transform_grid->addWidget(chk_scale_lock_, 1, 5",
):
    assert token in props
assert shape_props.index("shape_grid->addWidget(field_height, 0, 3)") < shape_props.index("shape_grid->addWidget(chk_size_lock_, 0, 4")
assert image_props.index('make_image_box_size_field(fxm_tr("OBSTitles.H")') < image_props.index("image_box_size_grid->addWidget(chk_image_box_size_lock_, 0, 4")

# Anchor presets live in the context-sensitive dynamic toolbar and still route
# through the hidden synchronization combo/keyframe compensation path.
assert "void apply_anchor_preset(int index);" in props_h
assert "void PropertiesPanel::apply_anchor_preset" not in props_actions
assert "void PropertiesPanel::apply_anchor_preset" in props_sync
assert "cmb_anchor_->setCurrentIndex(index)" in props_sync
assert "anchor_toolbar_widget_" in editor_h and "anchor_toolbar_grid_" in editor_h
assert "new AnchorGridButton(anchor_toolbar_widget_)" in toolbar
assert "props_->apply_anchor_preset(index)" in toolbar
assert "anchor_toolbar_widget_->setVisible(anchor_available)" in toolbar_state

# Camera inspector is collapsible and every numeric camera label is draggable.
assert "FxmCollapsiblePanel *camera_panel_" in title_props_h
assert 'new FxmCollapsiblePanel(QStringLiteral("3D Camera")' in title_props
assert 'setPersistenceKey(QStringLiteral("title-properties")' in title_props
assert "new NumericDragLabel" in title_props
for label in ("Position", "Target", "Rotation", "Focal length", "Field of view", "Near clip", "Far clip"):
    assert f'QStringLiteral("{label}")' in title_props

# Projected overlays are stable without changing the rendered layer geometry.
assert "projected_3d" in overlay_geometry
assert "devicePixelRatioF()" in overlay_geometry
assert "std::round(view.x() * overlay_pixel_scale)" in overlay_geometry
assert "projected_3d) ? Qt::SolidLine : Qt::DashLine" in overlay_paint
assert "outline_pen.setCosmetic(true)" in overlay_paint

# 2D children inherit a 3D parent basis but remain strict local XY planes.
for token in (
    "p.z = 0.0",
    "s.z = 1.0",
    "rotation.x = rotation.y = 0.0",
    "anchor.z = 0.0",
):
    assert token in transform

# Every hierarchy mutation can capture a full world matrix and resolve a new
# local XYZ TRS, including a 2D layer entering a 3D destination hierarchy.
for token in (
    "struct EditorWorldTransformSnapshot",
    "bool has_3d_matrix = false",
    "editor_capture_world_transform_for_parenting",
    "destination_uses_3d",
    "parent_inverse * snapshot.world_3d",
    "editor_decompose_local_3d",
    "set_animated_value(layer.position_z",
    "set_animated_value(layer.rotation_x",
    "set_animated_value(layer.rotation_y",
    "set_animated_value(layer.scale_z",
):
    assert token in parenting
assert "Gram-Schmidt" in parenting

# Compatible direct children at every group boundary use the same opaque and
# transparent hardware-depth run before the group result is flattened.
for token in (
    "struct GroupHardwareDepthRun",
    "group_hardware_depth_runs",
    "rendered_group_depth_layers",
    "ordered_group_children",
    "render_gpu_hardware_depth_run(",
    "group.id",
):
    assert token in compositor
assert compositor.index("struct GroupHardwareDepthRun") < compositor.index("const Layer &child = *child_ptr")

assert "Move, Rotate and Scale gizmos" in readme
assert "Development Version 205 — Compact 3D properties, stable overlays, and full group parenting" in changelog
assert "Keyframe-safe parenting and grouping" in guide
print("Keyframe-safe parenting and grouping GUI/parenting/group-depth source contract passed")
