#!/usr/bin/env python3
"""Structural acceptance checks for the Phase 11 analytic GPU shape renderer."""

from __future__ import annotations

from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = read_source_bundle(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
PROPERTIES = read_source_bundle(ROOT / "src/editor/properties-panel.cpp")
TOOLS_SIDEBAR = (ROOT / "src/editor/tools-sidebar.cpp").read_text(encoding="utf-8", errors="replace")
CANVAS = read_source_bundle(ROOT / "src/canvas/canvas-preview.cpp")
TITLE_EDITOR = read_source_bundle(ROOT / "src/editor/title-editor.cpp")
PATH_GEOMETRY = (ROOT / "src/layers/path-geometry.cpp").read_text(encoding="utf-8", errors="replace")


def block(start: str, end: str) -> str:
    begin = SOURCE.find(start)
    if begin < 0:
        return ""
    finish = SOURCE.find(end, begin + len(start))
    return SOURCE[begin:] if finish < 0 else SOURCE[begin:finish]


errors: list[str] = []
passes: list[str] = []

for path in (ROOT / "src").rglob("*"):
    if path.is_file() and path.suffix in {".cpp", ".h", ".hpp", ".c"}:
        for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if re.match(r"^(<<<<<<<|=======|>>>>>>>)", line):
                errors.append(f"merge marker in {path.relative_to(ROOT)}:{number}")

selector = block("static bool layer_can_use_gpu_primitive_raster(",
                 "static double gpu_primitive_padding(")
if not selector:
    errors.append("primitive eligibility helper is missing")
else:
    required_fallbacks = (
        "ShapeType::Path", "shape_type == ShapeType::Star",
        "shape_type == ShapeType::Line", "layer.fill_type != 0",
        "layer.stroke_fill_type != 1", "corner_bevel_roundness",
        "shape_type == ShapeType::Triangle", "shape_type == ShapeType::Diamond",
        "shape_roundness",
    )
    missing = [token for token in required_fallbacks if token not in selector]
    if missing:
        errors.append("eligibility helper misses exact-render fallbacks: " + ", ".join(missing))
    else:
        passes.append("unsupported paths, gradients, bevels, legacy lines and rounded polygons retain exact fallback")

if ("shape_type == ShapeType::Triangle" not in selector or
        "shape_type == ShapeType::Diamond" not in selector or
        "std::abs(layer.shape_roundness) > 0.001f" not in selector):
    errors.append("rounded triangle/diamond can still enter the sharp analytic polygon shader")
else:
    passes.append("rounded triangle and diamond use the exact editable-path corner renderer")

if ("case ShapeType::Triangle:" not in PATH_GEOMETRY or
        "case ShapeType::Diamond:" not in PATH_GEOMETRY or
        "primitive_corner_radii(layer, vertices.size())" not in PATH_GEOMETRY or
        "layer.shape_roundness" not in PATH_GEOMETRY):
    errors.append("exact fallback does not build rounded triangle/diamond geometry from shape_roundness")
else:
    passes.append("exact fallback constructs triangle/diamond corners from the live roundness value")

update = block("void title_gpu_render_session_update_range(",
               "void title_gpu_render_session_set_preview_quality(")
if "const bool gpu_primitive = false" in update:
    errors.append("Phase 11 is still hard-disabled")
elif "layer_can_use_gpu_primitive_raster(" not in update:
    errors.append("session update does not select the Phase 11 eligibility contract")
else:
    passes.append("eligible built-in shapes select the analytic GPU path")

if "!model_changed && !dynamic_raster && !local_geometry_changed" not in update:
    errors.append("interactive local geometry changes can still be skipped")
else:
    passes.append("interactive resize invalidates the primitive raster without requiring a model revision")

if ("primitive_type == ShapeType::Rectangle ||" not in update or
        "primitive_type == ShapeType::RoundedRectangle" not in update or
        "std::max(0.0f, layer.corner_radius_tl)" not in update or
        "std::max(0.0f, layer.corner_radius_br)" not in update):
    errors.append("rectangle GPU upload does not publish the four live corner radii")
elif re.search(r"primitive_type == ShapeType::Rectangle\s*\)\s*\{\s*entry\.primitive_corner_radii\s*=\s*\{0\.0f", update):
    errors.append("rectangle GPU upload still hard-resets corner radii to zero")
else:
    passes.append("rectangle and rounded-rectangle GPU uploads preserve all four corner radii")

session = block("struct TitleGpuRenderSession", "static std::string gpu_session_layer_id")
for token in ("primitive_targets[2]", "primitive_active_target", "primitive_backend_unavailable"):
    if token not in session:
        errors.append(f"GPU session lacks {token}")
if not any(token not in session for token in ("primitive_targets[2]", "primitive_active_target", "primitive_backend_unavailable")):
    passes.append("primitive lifetime has double-buffered targets and a permanent backend fallback gate")

primitive = block("static bool render_gpu_primitive_raster(",
                  "static bool upload_gpu_layer_raster")
if "entry.primitive_active_target == 0 ? 1 : 0" not in primitive:
    errors.append("primitive redraw does not use the inactive target")
elif primitive.find("gs_texrender_get_texture(render_target)") > primitive.find("entry.primitive_active_target = render_index"):
    errors.append("primitive target is committed before successful texture publication")
else:
    passes.append("primitive target is committed only after a successful inactive-target render")

if "primitive_backend_unavailable = true" not in primitive or "entry.key.clear()" not in primitive:
    errors.append("shader compilation failure cannot escape to the stable CPU base-raster path")
else:
    passes.append("shader compilation failure schedules stable CPU fallback instead of a blank retry loop")

for token in ("surfaceSize", "shapeSize", "shapeOffset", "primitive_padding"):
    if token not in SOURCE:
        errors.append(f"padded primitive contract misses {token}")
if not any(f"padded primitive contract misses {token}" in errors for token in ("surfaceSize", "shapeSize", "shapeOffset", "primitive_padding")):
    passes.append("shape coverage is separated from its padded GPU surface")

shader = block('static constexpr const char *kGpuPrimitiveShapeEffect = R"(',
               'static constexpr const char *kGpuFrameBlendEffect')
for token in ("strokeAlignment", "strokeOnFront", "p / max(halfSize",
              "cos(halfSector)"):
    if token not in shader:
        errors.append(f"primitive shader misses {token}")
if not any("primitive shader misses" in error for error in errors):
    passes.append("shader preserves stroke placement/order and exact non-square convex primitive boundaries")

if "entry.effect_cache_key.clear();" not in primitive:
    errors.append("primitive publication can reuse an effected texture from the previous generation")
else:
    passes.append("every primitive generation invalidates its GPU effect cache")

if "owned_cpu_texture" not in primitive or "gs_texture_destroy(owned_cpu_texture)" not in primitive:
    errors.append("CPU-to-primitive promotion leaks the previous owned texture")
else:
    passes.append("CPU-to-primitive promotion releases the replaced owned texture")

render = block("static gs_texture_t *render_gpu_session_locked(",
               "TitleGpuRenderSession *title_gpu_render_session_create")
if "all_required_rasters_ready" not in render or "frame publication deferred" not in render:
    errors.append("incomplete primitive replacement can publish a mixed/black compositor frame")
else:
    passes.append("incomplete primitive replacements preserve the last complete presentation frame")

state_scope = render.find("ScopedGpuCompositorState state;")
upload_loop = render.find("for (auto &pair : session->layers)")
if state_scope < 0 or upload_loop < 0 or state_scope > upload_loop:
    errors.append("primitive uploads can inherit the editor display matrix/projection before GPU state isolation")
else:
    passes.append("GPU state isolation begins before primitive uploads and resize redraws")

if "gpu_session_has_published_frame_for_current_title(session)" not in render:
    errors.append("interactive replacement rejects the previous same-title frame after model revision advances")
else:
    passes.append("same-title presentation survives a transient replacement revision without cross-title retention")

auxiliary = block("static gs_texture_t *title_gpu_render_session_render_auxiliary_layer(",
                  "bool title_gpu_render_session_draw(")
aux_state = auxiliary.find("ScopedGpuCompositorState state;")
aux_upload = auxiliary.find("upload_gpu_layer_raster(session")
if aux_state < 0 or aux_upload < 0 or aux_state > aux_upload:
    errors.append("auxiliary primitive uploads are not isolated from caller GPU transforms")
else:
    passes.append("auxiliary/mask primitive uploads are isolated before raster publication")

if "publish_stable_gpu_frame(session, frame)" not in render:
    errors.append("layer textures are exposed directly instead of copied into stable presentation storage")
else:
    passes.append("completed frames are copied into independent stable presentation targets")

shape_menu = TOOLS_SIDEBAR[TOOLS_SIDEBAR.find("void ToolsSidebar::rebuild_shape_menu()"):]
shape_menu = shape_menu[:shape_menu.find("void ToolsSidebar::set_selected_text_layer_type")]
shape_buttons = PROPERTIES[PROPERTIES.find("for (ShapeType shape_type : {"):]
shape_buttons = shape_buttons[:shape_buttons.find("shape_types_layout->addStretch")]
if ("ShapeType::Line," in shape_menu or "ShapeType::Line})" in shape_buttons or
        'addItem("Line", (int)ShapeType::Line)' in PROPERTIES):
    errors.append("Line is still exposed through a shape creation UI")
else:
    passes.append("Line is removed from the toolbar menu and shape properties selector")

if ("shape_type == ShapeType::Line || shape_type == ShapeType::Path" not in TOOLS_SIDEBAR or
        "shape_type == ShapeType::Line || shape_type == ShapeType::Path" not in CANVAS or
        "shape_type == ShapeType::Line || shape_type == ShapeType::Path" not in TITLE_EDITOR):
    errors.append("stale Line shape activations can still create a shape layer")
else:
    passes.append("stale Line/Path activations normalize safely to Rectangle before layer creation")

if "shapeType == 7" in shader:
    errors.append("legacy Line rendering remains in the Phase 11 primitive shader")
else:
    passes.append("legacy Line rendering is removed from the Phase 11 shader")

print("Phase 11 GPU shape renderer structural audit")
for message in passes:
    print(f"  PASS: {message}")
for message in errors:
    print(f"  FAIL: {message}")
print(f"Result: {len(passes)} passed, {len(errors)} failed")
sys.exit(1 if errors else 0)
