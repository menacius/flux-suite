#!/usr/bin/env python3
"""Dependency-free structural checks for the unified GPU presentation contract."""

from __future__ import annotations

from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return read_source_bundle(ROOT / path)


def block(text: str, start: str, end: str) -> str:
    begin = text.find(start)
    if begin < 0:
        return ""
    finish = text.find(end, begin + len(start))
    return text[begin:] if finish < 0 else text[begin:finish]


errors: list[str] = []
passes: list[str] = []
source = read("../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
canvas = read("src/canvas/canvas-preview.cpp")

for path in (ROOT / "src").rglob("*"):
    if not path.is_file() or path.suffix not in {".cpp", ".h", ".hpp", ".c"}:
        continue
    for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        if re.match(r"^(<<<<<<<|=======|>>>>>>>)", line):
            errors.append(f"merge marker in {path.relative_to(ROOT)}:{number}")

if source.count("static constexpr const char *kGpuFrameBlitEffect") != 1:
    errors.append("dedicated frame-blit shader is not defined exactly once")
else:
    passes.append("dedicated frame-blit shader has one definition")

cached_draw = block(source, "static bool draw_gpu_cached_image(", "static gs_texture_t *publish_stable_gpu_frame")
if not cached_draw:
    errors.append("draw_gpu_cached_image implementation not found")
else:
    if "session->blit_effect" not in cached_draw:
        errors.append("cached/final frames do not use the isolated blit effect")
    if "session->copy_effect" in cached_draw or '"wipeProgress"' in cached_draw or '"imageClipRect"' in cached_draw:
        errors.append("cached/final frame presentation still shares layer wipe/crop shader state")
    if "gs_effect_set_float" in cached_draw:
        errors.append("frame blit unexpectedly depends on mutable layer scalar uniforms")
    if not errors:
        passes.append("cached/final frame blits are isolated from layer transition uniforms")

layer_draw = block(source, "static bool draw_gpu_layer_texture(", "static bool render_gpu_layer_to_target")
transition_binder = block(source, "static void set_gpu_transition_effect_params(",
                          "static bool draw_gpu_layer_texture(")
if ("session->copy_effect" not in layer_draw or
        "set_gpu_transition_effect_params" not in layer_draw or
        '"wipeProgress"' not in transition_binder):
    errors.append("layer copy shader no longer owns its transition/crop parameters")
else:
    passes.append("layer-only shader retains wipe/crop ownership through the transition parameter binder")

ensure = block(source, "static bool ensure_gpu_session_objects(", "static bool render_gpu_primitive_raster")
if "session->blit_effect" not in ensure or "!session->blit_effect" not in ensure:
    errors.append("mandatory GPU object creation does not compile/validate the blit shader")
else:
    passes.append("blit shader participates in mandatory GPU object validation")

if "gs_effect_create(kGpuPrimitiveShapeEffect" in ensure or \
        "!session->primitive_shape_effect" in ensure:
    errors.append("optional primitive-shape shader still gates mandatory compositor startup")
else:
    primitive = block(source, "static bool render_gpu_primitive_raster(", "static bool upload_gpu_layer_raster")
    if "gs_effect_create(" not in primitive or "primitive_backend_unavailable" not in primitive:
        errors.append("primitive-shape shader is neither lazy nor failure-isolated")
    else:
        passes.append("optional primitive shader is lazy and failure-isolated from cache presentation")

for shader_token in ("kGpuLayerCopyEffect", "kGpuFrameBlendEffect", "kGpuMaskEffect"):
    if f"gs_effect_create({shader_token}" in ensure:
        errors.append(f"{shader_token} still gates cache-only frame presentation")
if not any(f"{token} still gates" in error for token in ("kGpuLayerCopyEffect", "kGpuFrameBlendEffect", "kGpuMaskEffect") for error in errors):
    passes.append("cache-only presentation is independent of live layer/blend/mask shader compilation")

session_struct = block(source, "struct TitleGpuRenderSession", "static std::string gpu_session_layer_id")
if "state_transaction_pending" not in session_struct:
    errors.append("GPU session lacks an atomic prefix-submission transaction guard")

render_locked = block(source, "static gs_texture_t *render_gpu_session_locked(", "TitleGpuRenderSession *title_gpu_render_session_create")
if "state_transaction_pending.load" not in render_locked:
    errors.append("draw/readback can observe an intermediate cached-prefix state")
else:
    passes.append("draw/readback preserves the last complete frame during prefix submission")

prefix = block(source, "bool title_gpu_render_session_submit_cached_prefix(", "static void title_gpu_render_session_prepare_auxiliary_layers")
if "PrefixSubmissionTransaction" not in prefix or "state_transaction_pending.store(true" not in prefix:
    errors.append("cached-prefix update and base attachment are not transactionally guarded")
else:
    passes.append("cached-prefix update and base attachment form one presentation transaction")

quality = block(source, "void title_gpu_render_session_set_preview_quality(", "void title_gpu_render_session_update(")
if "std::lock_guard<std::mutex> lock(session->mutex)" not in quality:
    errors.append("preview-quality mutation races the OBS display thread")
else:
    passes.append("preview-quality changes are serialized with GPU draws")

readback = block(source, "QImage title_gpu_render_session_readback(", "QImage render_title_gpu_frame_readback")
enter_pos = readback.find("obs_enter_graphics();")
lock_pos = readback.find("std::unique_lock<std::mutex> lock(session->mutex);")
leave_pos = readback.rfind("obs_leave_graphics();")
unlock_pos = readback.rfind("lock.unlock();")
if min(enter_pos, lock_pos, leave_pos, unlock_pos) < 0 or not (enter_pos < lock_pos < unlock_pos < leave_pos):
    errors.append("GPU readback lock order is not graphics -> session -> unlock -> leave graphics")
else:
    passes.append("readback, display, and destruction share one graphics/session lock order")


scene_masks = block(source, "static void render_scene_masks_gpu(", "static void source_video_render")
masked_draw = scene_masks[scene_masks.rfind("gs_effect_set_texture(image, scene_texture)"):]
if "gs_enable_blending(true);" not in masked_draw or \
        "gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);" not in masked_draw:
    errors.append("scene-mask overlay can overwrite the title because blending is not explicitly enabled")
else:
    passes.append("scene-mask composition explicitly uses premultiplied-alpha blending")

source_tick = block(source, "static void source_video_tick(", "static void apply_layer_world_transform_gs")
if "bootstrap-live-poster" not in source_tick:
    errors.append("cached-only startup can remain permanently transparent on its first miss")
elif "playback.cached_frames_only &&\n                       !data->first_frame_pending" not in source_tick:
    errors.append("cached-only hold does not distinguish an existing frame from blank startup")
else:
    passes.append("cached-only startup bootstraps one visible frame, then holds valid frames on misses")

mouse_release = block(canvas, "void CanvasPreview::mouseReleaseEvent(", "void CanvasPreview::contextMenuEvent")
normal_release = mouse_release[mouse_release.rfind("bool changed = drag_changed_;"):]
if "drag_mode_ = DragMode::None;" not in normal_release:
    errors.append("normal canvas drag release block not found")
elif "invalidate_canvas_overlay_caches();" not in normal_release or "update();" not in normal_release:
    errors.append("normal drag release leaves the cached tooltip/overlay visible")
else:
    passes.append("normal drag release invalidates and repaints the GPU overlay")

clear_snap = block(canvas, "void CanvasPreview::clear_snap_feedback()", "void CanvasPreview::add_snap_feedback")
if "invalidate_canvas_overlay_caches();" not in clear_snap:
    errors.append("clearing snap labels does not invalidate the cached GPU overlay")
else:
    passes.append("snap labels and guides invalidate the cached GPU overlay")

# Make sure the frame blit shader is not accidentally reintroduced through the
# layer-copy object by future edits.
copy_effect_use_count = source.count("session->copy_effect")
if copy_effect_use_count < 10:
    errors.append("unexpected copy-effect usage count; audit assumptions need review")
if "gs_effect_destroy(session->blit_effect)" not in source:
    errors.append("dedicated blit shader leaks during GPU session destruction")
else:
    passes.append("dedicated blit shader has explicit lifetime management")

print("Unified GPU pipeline structural audit")
for message in passes:
    print(f"  PASS: {message}")
for message in errors:
    print(f"  FAIL: {message}")
print(f"Result: {len(passes)} passed, {len(errors)} failed")
sys.exit(1 if errors else 0)
