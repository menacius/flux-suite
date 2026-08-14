#!/usr/bin/env python3
"""Source contract for Development Version 206 masks/blends/effects in 3D."""
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
effects = text("src/effects/layer-effects.h")
effect_runtime = text("src/effects/effect-runtime.cpp")
effect_contract = effects + effect_runtime
transform_h = text("src/rendering/layer-transform-3d.h")
transform = text("src/rendering/layer-transform-3d.cpp")
shader = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
compositor = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
scene_masks = text("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
canvas_h = text("src/canvas/canvas-preview.h")
overlay_geometry = text("src/canvas/canvas-preview/transform-snap.inc")
overlay_paint = text("src/canvas/canvas-preview/canvas-overlay-paint.inc")
corner_tools = text("src/canvas/canvas-preview/path-gradient-tools.inc")
readme = text("README.md")
changelog = text("docs/CHANGELOG.md")
guide = text("docs/EFFECTS_AND_EXTENSIONS.md")
effects_guide = text("docs/EFFECTS_AND_EXTENSIONS.md")
serialization = text("src/core/title-data.cpp")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)

# Effect placement is an explicit runtime contract derived from existing data,
# so old projects need no new serialized field.
for token in (
    "enum class LayerEffectSpace",
    "LayerSpace = 0",
    "PostTransform = 1",
    "ScreenSpace = 2",
    "layer_effect_execution_space",
    "effect.affect_layers_behind",
    "LayerEffectType::MotionBlur",
    "LayerEffectSpace::PostTransform",
):
    assert token in effect_contract
for forbidden in ("effect_execution_space", "layer_effect_space"):
    assert forbidden not in serialization

# Layer-space effects and projected mattes may share the hardware depth draw;
# temporal and screen-space passes stay on the destination compositor.
for token in (
    "depth_compositor_surface_candidate",
    "effects_after_projected_mask",
    "LayerEffectSpace::PostTransform",
    "LayerEffectSpace::ScreenSpace",
    "layer.blend_mode == EffectBlendMode::Normal",
    "Non-Normal blends must read the destination color",
):
    assert token in transform
assert "simple_planar_3d_candidate(title, layer, title_time)" in transform

# Track-matte coverage is sampled in screen space by the same depth shader.
for token in (
    "postProjectionMask",
    "postProjectionMaskEnabled",
    "postProjectionMaskMode",
    "clipPos : TEXCOORD1",
    "projected_mask_alpha",
    "PSDepthLayer",
    "clip(color.a - 0.0039215686)",
):
    assert token in shader
for token in (
    "post_projection_mask",
    "post_projection_mask_mode",
    "projected_mask_enabled",
    "projected_mask_mode_param",
):
    assert token in presentation

# Nested texrenders are resolved before the Z target begins. The complete
# padded effect raster and mask graph are then supplied to the projected draw.
for token in (
    "struct PreparedDepthSurface",
    "Resolve all effect and matte graph textures before depth_target begins",
    "apply_gpu_layer_effect_stack(",
    "render_gpu_mask_graph_texture(",
    "GpuMaskGraphPurpose::ClippingShape",
    "GpuMaskGraphPurpose::MatteArtwork",
    "prepared.projected_mask",
    "prepared.projected_mask_mode",
    "prepared.texture",
    "render_gpu_hardware_depth_run(",
    "compositor_group_id",
):
    assert token in compositor
assert compositor.index("Resolve all effect and matte graph textures") < compositor.index("bool success = begin_target")
assert "prepared.projected_mask, prepared.projected_mask_mode" in compositor

# Expanded local effect bounds are projected/clipped with the exact camera
# matrices. They are used by renderer culling and by stable editor overlays.
for token in (
    "projected_local_bounds",
    "QPolygonF *clipped_polygon",
    "local_to_clip",
    "Clip in homogeneous coordinates",
    "p.z() + p.w()",
    "p.w() - p.z()",
):
    assert token in transform_h or token in transform
for token in (
    "padded_local_surface",
    "entry.origin.x() * box_sx",
    "entry.logical_width * box_sx",
    "prepared.projected_bounds",
):
    assert token in compositor
for token in (
    "bool handle_visible[8]",
    "bool origin_visible",
):
    assert token in canvas_h
assert overlay_geometry.count("projected_local_polygon(") >= 2
assert "clipped_canvas_polygon" in overlay_geometry
assert "editor_warped_layer_local_point" in overlay_geometry
assert "layer_local_clip_point(" in overlay_geometry
assert "hover_overlay.outline.size() < 3" in overlay_paint
for token in (
    "corner_radius_handle_view_geometry",
    "layer_local_clip_point(",
    "padded_view.contains(anchor_view)",
    "remaining 3D overlay flicker",
):
    assert token in corner_tools

# Destination-reading blend modes remain ping/pong full-frame passes, but
# planar fallback runs are camera-depth ordered. Groups are flattened only
# after their internal child depth/mask/effect graph is complete.
for token in (
    "sort_candidate_runs",
    "camera_depth",
    "composite_gpu_frame_layer",
    "EffectBlendMode mode",
    "render_gpu_group_graph_texture",
    "group_hardware_depth_runs",
):
    assert token in transform or token in compositor
assert "layer.blend_mode, child_opacity" in compositor or "child.blend_mode" in compositor

# Scene masks remain post-projection/output operations at their real stack
# positions and can apply a full-canvas effect stack to the matted scene.
for token in (
    "render_scene_masks_gpu",
    "title_gpu_render_session_render_auxiliary_layer",
    "apply_gpu_layer_effect_stack(",
    "draw_gpu_layer_range_over_current_target",
    "preserving the real layer order",
):
    assert token in scene_masks

assert "Masks, mattes, blend modes, groups and effects now follow a defined 3D execution contract" in readme
assert "Development Version 206 — Masks, blend modes, and effects in 3D" in changelog
assert "three stable effect execution spaces" in guide
assert "## 3D execution spaces" in effects_guide

print("Development Version 206 masks/blends/effects 3D source contract passed")
