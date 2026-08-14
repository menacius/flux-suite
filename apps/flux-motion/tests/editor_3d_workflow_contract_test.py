#!/usr/bin/env python3
"""Source contract for Development Version 203 editor-side 3D workflow."""
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
title_header = text("Core/title-data.h")
title_serialization = text("src/core/title-data.cpp")
preview_header = text("src/canvas/canvas-preview.h")
tools = text("src/canvas/canvas-preview/editor-3d-tools.inc")
render_path = text("src/canvas/canvas-preview/keyboard-wheel-events.inc")
key_path = text("src/canvas/canvas-preview/gpu-frame-rendering.inc")
controls = text("src/editor/title-editor/commands-docks.inc")
transform = text("src/rendering/layer-transform-3d.cpp")
obs_refresh = text("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build_info
assert re.search(r"kCurrentDevelopmentVersion\s*=\s*243", schema)

# The editor camera override must remain runtime-only and outside project serialization.
assert "render_camera_override_id" in title_header
assert 'jt["render_camera_override_id"]' not in title_serialization
assert 'j["render_camera_override_id"]' not in title_serialization
assert 'result["render_camera_override_id"]' not in title_serialization
# Import/export may explicitly clear the runtime override; that is not serialization.
assert '"__fxm_editor_3d_view__"' in tools
assert "render_camera_override_id = kEditorViewCameraId" in tools
assert "render_camera_override_id" in transform
assert "render_camera_override_id" in obs_refresh

# Complete view and gizmo enums/API.
for token in (
    "ActiveCamera", "Front", "Back", "Left", "Right", "Top", "Bottom",
    "CustomPerspective", "GizmoMode", "Move", "Rotate", "Scale",
    "GizmoAxis", "XY", "XZ", "YZ", "set_editor_3d_view",
    "frame_3d_selection", "frame_3d_all",
):
    assert token in preview_header, f"missing preview API token: {token}"

# The projection copy, navigation, framing, and manipulation paths must be implemented.
for token in (
    "CanvasPreview::projection_title",
    "CanvasPreview::begin_editor_3d_navigation",
    "CanvasPreview::update_editor_3d_navigation",
    "CanvasPreview::finish_editor_3d_navigation",
    "Editor3DNavigationMode::Orbit",
    "Editor3DNavigationMode::Pan",
    "Editor3DNavigationMode::Dolly",
    "CanvasPreview::frame_3d_selection",
    "CanvasPreview::frame_3d_all",
    "CanvasPreview::begin_3d_gizmo_drag",
    "CanvasPreview::update_3d_gizmo_drag",
    "CanvasPreview::finish_3d_gizmo_drag",
    "CanvasPreview::draw_3d_gizmo",
    "GizmoAxis::X", "GizmoAxis::Y", "GizmoAxis::Z",
    "GizmoAxis::XY", "GizmoAxis::XZ", "GizmoAxis::YZ",
):
    assert token in tools, f"missing editor 3D implementation token: {token}"

# Final rendering uses an effective preview title without altering the cached title identity.
assert "projection_title()" in render_path
assert "render_title" in render_path
assert "title_gpu_render_session_update" in render_path
assert "title_" in render_path

# Visible controls and keyboard workflow.
for label in (
    'QStringLiteral("Active Camera")',
    'QStringLiteral("Front")',
    'QStringLiteral("Back")',
    'QStringLiteral("Left")',
    'QStringLiteral("Right")',
    'QStringLiteral("Top")',
    'QStringLiteral("Bottom")',
    'QStringLiteral("Custom Perspective")',
    'QStringLiteral("Frame Selected")',
    'QStringLiteral("Frame All")',
):
    assert label in controls, f"missing editor 3D control: {label}"
for key in ("Qt::Key_W", "Qt::Key_E", "Qt::Key_R", "Qt::Key_F"):
    assert key in key_path, f"missing editor 3D shortcut: {key}"
assert "Qt::ShiftModifier" in key_path

print("editor 3D workflow source contract passed")
