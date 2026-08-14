#!/usr/bin/env python3
"""Source contract for the Development Version 202/203 hardware-depth core."""
from pathlib import Path
import re
import sys

ROOT = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]

def text(relative: str) -> str:
    path = ROOT / relative
    assert path.exists(), f"missing source file: {relative}"
    return path.read_text(encoding="utf-8")

cmake = text("CMakeLists.txt")
build_info = text("src/core/build-info.h")
schema = text("Core/title-serialization-schema.h")
transform_header = text("src/rendering/layer-transform-3d.h")
transform_source = text("src/rendering/layer-transform-3d.cpp")
resources = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
shader = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
compositor = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
destroy = text("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
serialization = text("src/core/title-data.cpp")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build_info
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)
assert "case 202:" in schema and "case 203:" in schema

# A real color + depth/stencil attachment is owned by the GPU session.
assert "gs_texrender_create(GS_BGRA, GS_Z24_S8)" in resources
assert "gs_texrender_t *depth_target" in resources
assert "destroy_target(session->depth_target)" in destroy
# Z allocation is optional: unsupported backends retain the compatibility path.
assert "if (!session->depth_target ||" not in resources
assert "depth_target_unavailable" in resources
assert "!session->depth_target_unavailable" in resources

# Shared camera/world matrices drive an actual OBS projection/model-view path.
for token in (
    "struct CameraRenderState",
    "camera_render_state",
    "hardware_depth_candidate",
    "hardware_depth_writer",
    "hardware_depth_read_only",
):
    assert token in transform_header
for token in (
    "effective_camera_id",
    "layer.type == LayerType::TransitionInput",
    "layer.blend_mode == EffectBlendMode::Normal",
    "effects_after_projected_mask",
    "LayerEffectSpace::PostTransform",
    "LayerEffectSpace::ScreenSpace",
    "(layer.depth_test || layer.write_to_depth)",
):
    assert token in transform_source
for token in (
    "qmatrix4x4_to_gs",
    "gs_projection_push",
    "gs_perspective",
    "gs_ortho",
    "layer_world_matrix",
    'hardware_depth ? "DepthDraw" : "Draw"',
):
    assert token in presentation

# Per-pixel Z is enabled for compatible planar 3D runs, including padded
# layer-space effects and projected mattes. Transparent planes use a dedicated
# sorted pass; destination-reading/post/screen-space work remains full-frame.
for token in (
    "render_gpu_hardware_depth_run",
    "GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL",
    "gs_enable_depth_test(true)",
    "gs_depth_function(GS_LEQUAL)",
    "gs_depth_function(layer.depth_test ? GS_LEQUAL : GS_ALWAYS)",
    "hardware_depth_runs",
    "run.writer_count >= 1 || run.transparent_count >= 2",
    "hardware_depth_transparent_candidate",
    "std::vector<const Layer *> transparent_layers",
    "rendered_hardware_depth_layers",
    "gpu_hardware_depth_camera_id",
):
    assert token in compositor
assert "composite_gpu_frame_layer" in compositor
assert "render_gpu_group_graph_texture" in compositor

# Cut out fully transparent raster pixels before depth is written.
assert "PSDepthLayer" in shader
assert "clip(color.a - 0.0039215686)" in shader
assert "technique DepthDraw" in shader

# Hardware depth is runtime/render-state only; no new persisted schema field.
for forbidden in ("depth_target", "hardware_depth_run", "hardware_depth_candidate"):
    assert forbidden not in serialization

print("hardware depth pipeline source contract passed")
