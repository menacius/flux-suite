#!/usr/bin/env python3
"""Dev336: every Rotate ring uses projection-independent screen scrubbing."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
gizmo = read("src/canvas/canvas-preview/editor-3d-tools.inc")
canvas_h = read("src/canvas/canvas-preview.h")

assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 336
assert int(build.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 336
assert manifest["development_version"] >= 336

# Hit-testing still selects the real projected ring, but drag magnitude is a
# linear mouse scrub and therefore cannot become singular when the ring is a
# line on screen.
for token in (
    "rotation_scrub_axis = -1",
    "kScrubActivationPixels = 3.0",
    "kDegreesPerPixel = 0.5",
    "std::abs(screen_delta.x()) >= std::abs(screen_delta.y()) ? 0 : 1",
    "rotation_delta = screen_delta.x() * kDegreesPerPixel",
    "rotation_delta = -screen_delta.y() * kDegreesPerPixel",
    "rotation_delta / 15.0",
    "rotation_scrub_axis < 0",
    "creating an Undo entry for a click/jitter",
):
    assert token in gizmo or token in canvas_h

for obsolete in (
    "rotation_start_world",
    "rotation_screen_tangent",
    "rotation_pixels_per_radian",
    "rotation_screen_fallback_valid",
    "resolved_on_rotation_plane",
    "start_angle",
):
    assert obsolete not in gizmo
    assert obsolete not in canvas_h
assert "canvas_point_on_world_plane(" not in gizmo

# Only angle acquisition changed. Axis-space conjugation and grouped Rotation
# XYZ authoring remain the corrected Dev334 implementation.
for token in (
    "gizmo_axis_world_direction",
    "orientation_inverse * parent_inverse * delta",
    "write_rotation(layer->rotation_x, next.x)",
    "write_rotation(layer->rotation_y, next.y)",
    "write_rotation(layer->rotation, next.z)",
):
    assert token in gizmo

test_name = (
    "tests/development_version_336_projection_independent_rotation_scrub_contract_test.py"
)
for area in ("editor_gui", "rendering_2d_3d", "platform_build"):
    assert test_name in manifest["areas"][area]["python"]

print("Dev336 projection-independent rotation scrub contract passed")
