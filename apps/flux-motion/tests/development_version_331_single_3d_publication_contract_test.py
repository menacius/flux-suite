#!/usr/bin/env python3
"""Dev331: single-plane 3D parity and exact stable-frame publication."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
present = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 331
assert int(build.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 331
assert manifest["development_version"] >= 331

# Stable publication is an exact resource replacement. A shader draw can
# inherit a stale 3D transform, and alpha composition cannot erase old pixels.
replace_start = present.index("static bool replace_full_canvas_gpu_texture(")
replace_end = present.index("\n}\n", replace_start)
replacement = present[replace_start:replace_end]
for token in (
    "gs_texture_get_width(texture) != session->width",
    "gs_texture_get_height(texture) != session->height",
    "gs_texture_get_color_format(destination) !=",
    "gs_copy_texture(destination, texture);",
):
    assert token in replacement
assert "gs_draw_sprite" not in replacement

publish_start = session.index("static gs_texture_t *publish_stable_gpu_frame(")
publish_end = session.index("\nclass ScopedGpuCompositorState", publish_start)
publish = session[publish_start:publish_end]
assert "replace_full_canvas_gpu_texture(session, frame, target)" in publish
assert "copy_full_canvas_gpu_texture(session, frame, target)" not in publish

# Both group-local and root single-plane runs stay on the same native camera
# path that was already used when an unrelated second layer was present.
for marker in (
    "group_hardware_depth_runs.emplace(run.layers.front()",
    "hardware_depth_runs.emplace(run.indices.front()",
):
    position = session.index(marker)
    decision = session[position - 700:position + 120]
    assert "if (!run.layers.empty())" in decision
    assert "run.layers.size() >= 2" not in decision

# Preserve Dev330's one-time canvas-Y/libobs conversion. Dev331 removes the
# one-layer homography fork instead of adding another axis sign inversion.
assert "qt_to_gs_camera.scale(1.0f, -1.0f, 1.0f);" in present
assert "const QMatrix4x4 model_view = gpu_canvas_camera_model_view(" in present
assert "v51-single-3d-exact-publication" in cache_abi

print("Dev331 single-3D publication contract passed")
