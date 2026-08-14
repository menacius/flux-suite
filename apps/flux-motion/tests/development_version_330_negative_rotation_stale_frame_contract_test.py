#!/usr/bin/env python3
"""Dev330 hotfix: negative transforms cannot retain or move old pixels."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


targets = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")
present = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
compat = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
primitives = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc")
gpu_text = read("src/rendering/title-gpu-text-renderer.cpp")
canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
registration = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
cache_policy = read("src/cache/title-cache-policy.h")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
transform3d = read("src/rendering/layer-transform-3d.cpp")

# Clear operations must run with a canonical colour/depth/cull state. In
# particular, colour writes are restored before gs_clear; otherwise the old
# upright raster can survive when the replacement is moved or flipped.
begin_start = targets.index("static bool begin_gpu_target(")
begin_end = targets.index("\n}\n", begin_start)
begin = targets[begin_start:begin_end]
for token in (
    "gs_enable_color(true, true, true, true);",
    "gs_enable_depth_test(false);",
    "gs_depth_function(GS_LEQUAL);",
    "gs_set_cull_mode(GS_NEITHER);",
    "gs_enable_blending(false);",
    "gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);",
):
    assert token in begin
assert begin.index("gs_enable_color(true, true, true, true);") < begin.index(
    "gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);"
)

# A complete compositor frame is copied with replacement semantics. It must
# never be alpha-composited over a presentation target from an older revision.
publish_start = session.index("static gs_texture_t *publish_stable_gpu_frame(")
publish_end = session.index("\nclass ScopedGpuCompositorState", publish_start)
publish = session[publish_start:publish_end]
assert "replace_full_canvas_gpu_texture(session, frame, target)" in publish
assert "draw_gpu_cached_image(session, frame" not in publish

# OBS live output may retain its last complete frame while a raster is pending,
# but the editor must match the exact model revision used by its overlays.
guard_start = session.index(
    "static bool gpu_session_has_published_frame_for_current_title(\n"
    "    const TitleGpuRenderSession *session)\n{",
    session.index("static bool gpu_session_final_matches_model"),
)
guard_end = session.index("\n}\n", guard_start)
guard = session[guard_start:guard_end]
assert "session->realtime_output.load(std::memory_order_relaxed) ||" in guard
assert "session->published_model_revision == session->model_revision" in guard
assert "return exact_editor_revision &&" in guard
assert "v50-negative-rotation-target-reset" in cache_abi

# Every manual reusable target family restores colour/depth/cull state before
# its clear, including depth, shadows, effects, primitives, glyphs and the
# editor swapchain. Scene-mask surfaces use the canonical helper.
for source in (session, present, compat, primitives, gpu_text, canvas):
    assert "gs_enable_color(true, true, true, true);" in source
    assert "gs_set_cull_mode(GS_NEITHER);" in source
assert registration.count("begin_gpu_target(") >= 2

# Ping/pong composition may never sample the texture owned by its output.
assert "ScopedAliasSnapshots" in session
assert "input != target_texture" in session

# 3D camera-depth ordering cannot safely cross a flattened cached prefix.
assert "title_has_3d_compositing" in cache_policy
assert "!title_has_3d_compositing(title)" in cache_policy
assert playback.count("title_has_3d_compositing(title)") >= 2

# Qt's Y-up NDC conversion and libobs' Y-down viewport are reconciled exactly
# once at the native hardware-depth API boundary. Authored positions stay in
# canvas Y-down space, so a downward Y drag renders downward.
assert "gpu_canvas_camera_model_view" in present
assert "qt_to_gs_camera.scale(1.0f, -1.0f, 1.0f);" in present
assert "const QMatrix4x4 model_view = gpu_canvas_camera_model_view(" in present

# Dev334 supersedes the temporary front-only typography rule: authored
# Double-sided now governs Text/Clock/Ticker like every other planar layer.
assert "const bool readable_front_only" not in transform3d
assert "including Text/Clock/Ticker" in transform3d


print("Dev330 negative-rotation stale-frame contract passed")
