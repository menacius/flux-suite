#!/usr/bin/env python3
"""Source contract for 3D depth and material interaction depth/culling semantics."""
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
header = text("src/rendering/layer-transform-3d.h")
transform = text("src/rendering/layer-transform-3d.cpp")
compositor = text("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
canvas_header = text("src/canvas/canvas-preview.h")
canvas_tools = text("src/canvas/canvas-preview/editor-3d-tools.inc")
canvas_paint = text("src/canvas/canvas-preview/keyboard-wheel-events.inc")
view_state = text("src/canvas/canvas-preview/preview-cache-view.inc")
editor_controls = text("src/editor/title-editor/commands-docks.inc")
properties = text("src/editor/properties-panel/popup-state.inc")
readme = text("README.md")
changelog = text("docs/CHANGELOG.md")
guide = text("docs/EFFECTS_AND_EXTENSIONS.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)
assert "case 203:" in schema

# Depth Test and Write Depth are independently classified.
for token in ("hardware_depth_writer", "hardware_depth_read_only"):
    assert token in header and token in transform
assert "(layer.depth_test || layer.write_to_depth)" in transform
assert "layer.write_to_depth &&" in transform
assert "layer.depth_test && !layer.write_to_depth" in transform

# Persistent writers and isolated read-only evaluations are distinct.
for token in (
    "std::vector<const Layer *> persistent_writers",
    "gs_depth_function(layer.depth_test ? GS_LEQUAL : GS_ALWAYS)",
    "draw_depth_layer(*persistent, false)",
    "persistent_writers",
    "run.writer_count >= 1 || run.transparent_count >= 2",
):
    assert token in compositor

# Culling is based on final screen-space winding and exposes a world normal.
for token in ("world_normal", "projected_winding"):
    assert token in header and token in transform
assert "result.front_facing = result.projected_winding >= -kEpsilon" in transform
assert "QVector3D::crossProduct" in transform

# Runtime-only editor diagnostics must not leak into title serialization.
for token in (
    "editor_3d_depth_debug_visible",
    "editor_3d_normals_visible",
    "set_3d_depth_debug_visible",
    "set_3d_normals_visible",
    "draw_3d_material_debug_overlay",
):
    assert token in canvas_header
assert "draw_3d_material_debug_overlay(p);" in canvas_paint
assert "Test / Read-only" in canvas_tools
assert "Always + Write" in canvas_tools
assert "world_normal" in canvas_tools
assert "editor_3d_depth_debug_visible" in view_state
assert "editor_3d_normals_visible" in view_state
assert 'QStringLiteral("Depth")' in editor_controls
assert 'QStringLiteral("Normals")' in editor_controls

for tooltip in (
    "Compare this layer against depth already written",
    "Persist this layer's visible pixels in the 3D depth buffer",
    "Render both projected sides",
    "Hide the projected back side",
):
    assert tooltip in properties

assert "hardware Z-buffer compositing" in readme
assert "Development Version 203 — Depth, culling, and material semantics" in changelog
assert "3D depth and material interaction" in guide
assert "Test enabled, Write enabled" in guide
assert "Test enabled, Write disabled" in guide
assert "Test disabled, Write enabled" in guide
assert "Test disabled, Write disabled" in guide

print("depth, culling, and material semantics source contract passed")
