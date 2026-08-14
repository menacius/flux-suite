#!/usr/bin/env python3
"""Structural acceptance audit for the Phase 12C persistent GPU text renderer."""

from __future__ import annotations

from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
RENDERER = (ROOT / "src/rendering/title-gpu-text-renderer.cpp").read_text(
    encoding="utf-8", errors="replace")
HEADER = (ROOT / "src/rendering/title-gpu-text-renderer.h").read_text(
    encoding="utf-8", errors="replace")
SDF = (ROOT / "src/rendering/title-gpu-text-sdf.cpp").read_text(
    encoding="utf-8", errors="replace")
SOURCE = read_source_bundle(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
CACHE = read_source_bundle(ROOT / "src/cache/cache-manager.cpp")
TEXT_README = (ROOT / "src/text/README.md").read_text(
    encoding="utf-8", errors="replace")
DOC = (ROOT / "docs/RENDERING_AND_CACHE.md").read_text(
    encoding="utf-8", errors="replace")
LAYOUT_HEADER = (ROOT / "src/text/title-text-layout.h").read_text(
    encoding="utf-8", errors="replace")
LAYOUT_CPP = (ROOT / "src/text/title-text-layout.cpp").read_text(
    encoding="utf-8", errors="replace")
LAYOUT_QT = (ROOT / "src/text/title-text-layout-qt.cpp").read_text(
    encoding="utf-8", errors="replace")
CANVAS = read_source_bundle(ROOT / "src/canvas/canvas-preview.cpp")
INLINE_CANVAS = (ROOT / "src/canvas/canvas-preview-inline-text.cpp").read_text(
    encoding="utf-8", errors="replace")
EDITOR = read_source_bundle(ROOT / "src/editor/title-editor.cpp")
EDITOR_INTERNAL = read_source_bundle(ROOT / "src/editor/title-editor-internal.h")

errors: list[str] = []
passes: list[str] = []


def require(text: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in text]
    if missing:
        errors.append(f"{label} misses: {', '.join(missing)}")
    else:
        passes.append(label)


for path in (ROOT / "src").rglob("*"):
    if path.is_file() and path.suffix in {".cpp", ".h", ".hpp", ".c"}:
        for number, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if re.match(r"^(<<<<<<<|=======|>>>>>>>)", line):
                errors.append(f"merge marker in {path.relative_to(ROOT)}:{number}")

require(
    CMAKE,
    (
        "src/rendering/title-gpu-text-renderer.cpp",
        "src/rendering/title-gpu-text-renderer.h",
        "src/rendering/title-gpu-text-sdf.cpp",
        "src/rendering/title-gpu-text-sdf.h",
    ),
    "CMake includes the renderer and independently testable SDF translation unit",
)

require(
    RENDERER,
    (
        "constexpr int kAtlasSize = 1024",
        "constexpr int kAtlasMaxPages = 32",
        "constexpr int kSdfSpread = 32",
        "GS_R8",
        "GS_DYNAMIC",
        "gs_texture_map",
        "for (int y = 0; y < kAtlasSize; ++y)",
        "std::unordered_map<GlyphKey, AtlasGlyph",
    ),
    "bounded persistent R8 glyph atlas is present",
)

require(
    RENDERER,
    (
        "shaping_variant_fingerprint",
        "text_layout_registered_raw_font(font_key)",
        "reconstructed_raw_font_for_run(run)",
        "raw_font_fingerprint(raw) == key.fingerprint",
        "database.styles(family)",
    ),
    "atlas identity reconstructs the exact shaped face and style variant",
)

require(
    RENDERER,
    (
        "build_glyph_sdf(",
        "alphaMapForGlyph",
        "QRawFont::PixelAntialiasing",
        "raw_font_has_color_glyph_tables",
        'font.fontTable("COLR")',
        'font.fontTable("SVG ")',
    ),
    "QRawFont populates only missing monochrome SDF glyphs with color-font fallback",
)

for forbidden in (
    "#include <QTextDocument",
    "#include <QPainter",
    "QPainter painter",
    "drawContents(",
    "cairo_t *",
    "pango_layout_",
):
    if forbidden in RENDERER:
        errors.append(f"new GPU renderer contains forbidden full text-raster API: {forbidden}")
if not any("forbidden full text-raster" in item for item in errors):
    passes.append("new GPU renderer contains no QTextDocument/QPainter/Cairo/Pango text surface")

require(
    RENDERER,
    (
        "gs_vbdata_create",
        "data->num_tex = 2",
        "gs_vertexbuffer_create",
        "gs_draw(GS_TRIS",
        "glyphAtlas.Sample",
        "fillGradientType",
        "strokeOutside",
        "strokeInside",
        "coverageMode",
    ),
    "glyph and decoration quads use the shader material path",
)

require(
    RENDERER,
    (
        "Merge only adjacent compatible quads inside the same global paint",
        "last.paint_index == paint_index",
        "last.page == encoded_page",
        "last.solid_geometry == solid",
        "last.draw_part == draw_part",
    ),
    "glyph batching preserves draw order inside each global paint phase",
)
if "std::map<std::pair<int, size_t>" in RENDERER:
    errors.append("GPU text globally groups batches and can reorder overlapping glyphs")

require(
    RENDERER,
    (
        "gs_texrender_t *targets[2]",
        "state.active_target == 0 ? 1 : 0",
        "gs_texrender_get_texture(target)",
        "state.active_target = render_index",
    ),
    "text layer publication is double-buffered and transactional",
)
commit_texture = RENDERER.find("gs_texture_t *rendered = gs_texrender_get_texture(target)")
commit_target = RENDERER.find("state.active_target = render_index", commit_texture)
if commit_texture < 0 or commit_target < commit_texture:
    errors.append("text layer target is committed before a successful texture is available")
else:
    passes.append("previous text target remains published until replacement succeeds")

require(
    RENDERER,
    (
        "strokeWidth",
        "strokeOutside",
        "strokeInside",
        "text_stroke_coverage_extents",
        "DrawPart::BehindStroke",
        "DrawPart::Fill",
        "DrawPart::FrontStroke",
        "text_stroke_draw_phase(stroke.on_front)",
        "fillStartColor",
        "fillEndColor",
        "gradientPosition",
        "normalized_gradient_type",
        "normalized_gradient_spread",
        "style.underline",
        "style.strikethrough",
    ),
    "inline fill/gradient/stroke and vector decorations are represented",
)

require(
    SOURCE + RENDERER,
    (
        "rich_text_layer_stroke_for_source_time",
        "model.default_format.stroke =",
        "text_stroke_draw_phase(stroke.on_front)",
        "if (!eval_outline_on_front(layer, t))",
        "if (eval_outline_on_front(layer, t))",
    ),
    "layer-wide and inline text strokes share one explicit order contract",
)
if "stroke.on_front || stroke.alignment == 2" in SOURCE + RENDERER:
    errors.append("inner text stroke still bypasses the requested Behind/Front order")

require(
    EDITOR_INTERNAL,
    (
        "static RichTextStroke layer_stroke_for_editor(const Layer &layer);",
        "model.default_format.stroke = layer_stroke_for_editor(layer);",
        "Sparse RichTextCharStroke ranges remain independent inline overrides.",
    ),
    "editor-time GPU text model inherits the live layer-wide stroke order",
)

require(
    CANVAS + INLINE_CANVAS + EDITOR,
    (
        "suspend_inline_text_edit_for_gradient",
        "resume_inline_text_edit_after_gradient",
        "selected_text_fill",
        "apply_selected_text_gradient_fill",
        "apply_rich_text_format_to_layer_range(",
        "RichTextCharFillColor",
        "inline_text_suspended_for_gradient_",
        "CanvasPreview suspends (rather than commits) the inline QTextEdit",
    ),
    "inline gradient selection survives popup edits and canvas-tool drags",
)

PROPERTIES = read_source_bundle(ROOT / "src/editor/properties-panel.cpp")
require(
    PROPERTIES,
    (
        "Qt::Tool | Qt::FramelessWindowHint",
        "popup.setModal(false)",
        "QEventLoop popup_loop",
        "popup.show()",
        "popup_loop.exec()",
        "gradient_model_refresh_requested",
        "current_gradient_fill",
        "syncing_gradient_model",
    ),
    "gradient panel remains open and resynchronizes after canvas drags",
)

gradient_tool_start = CANVAS.find("void CanvasPreview::set_gradient_tool_active()")
gradient_tool_end = CANVAS.find("void CanvasPreview::set_gradient_editor_active", gradient_tool_start)
gradient_tool = CANVAS[gradient_tool_start:gradient_tool_end]
if "commit_text_edit(true)" in gradient_tool:
    errors.append("Gradient tool still commits and clears the active rich-text selection")
else:
    passes.append("Gradient tool no longer converts an inline gradient edit to object level")

require(
    LAYOUT_HEADER + LAYOUT_CPP + LAYOUT_QT + RENDERER,
    (
        "TextLayoutCursorBoundary",
        "TextLayoutPaintSlice",
        "cursor_boundaries",
        "qline.cursorToX(boundary_qpos)",
        "text_layout_cluster_paint_slices",
        "split_paint_cluster",
    ),
    "cached clusters expose exact cursor boundaries for multiple inline paints",
)

behind = RENDERER.find("std::make_move_iterator(behind_strokes.begin())")
fills = RENDERER.find("std::make_move_iterator(fills.begin())", behind)
front = RENDERER.find("std::make_move_iterator(front_strokes.begin())", fills)
if behind < 0 or fills < behind or front < fills:
    errors.append("stroke/fill batches are not published in Behind -> Fill -> Front order")
else:
    passes.append("stroke order is enforced globally as Behind -> Fill -> Front")

require(
    SDF,
    (
        "distance_transform_1d",
        "squared_distance_field",
        "std::sqrt(distance_outside[i])",
        "std::sqrt(distance_inside[i])",
        "std::clamp",
        "int64_t padding64",
        "kMaximumGlyphPixels",
        "qf * qf",
    ),
    "SDF builder uses deterministic overflow-bounded exact squared distances",
)
if any(token in SDF for token in ("QImage", "gs_texture", "QRawFont", "obs_")):
    errors.append("SDF math translation unit is not independent from Qt/OBS")
else:
    passes.append("SDF math translation unit remains Qt/OBS independent")

require(
    SOURCE,
    (
        '#include "title-gpu-text-renderer.h"',
        "std::unique_ptr<fxm::gpu_text::Layer> text_layer",
        "std::unique_ptr<fxm::gpu_text::Renderer> text_renderer",
        "prepare_gpu_text_raster(",
        "render_gpu_text_raster(",
        "release_gpu_text_layer(",
        "gpu_text_surface_rect(",
    ),
    "title source owns and integrates Phase 12C text resources",
)

require(
    SOURCE,
    (
        "layer.type != LayerType::Text && layer.type != LayerType::Clock",
        "active_text_layer_transition(layer, title_time) == nullptr",
        "text_layout_paint_runs_canonical(request.document)",
        "build_text_layout(request)",
        "cached_text_layout(request)",
        "session->text_backend_unavailable",
    ),
    "eligibility consumes immutable layout and preserves compatibility boundaries",
)

upload_start = SOURCE.find("static bool upload_gpu_layer_raster(")
upload_end = SOURCE.find("static void set_effect_float_param", upload_start)
upload = SOURCE[upload_start:upload_end] if upload_start >= 0 else ""
require(
    upload,
    (
        "entry.gpu_text && entry.pending_upload",
        "render_gpu_text_raster(session, entry)",
        "release_gpu_text_layer(session, entry)",
        "entry.gpu_primitive",
    ),
    "layer upload switches text/primitive/compatibility ownership explicitly",
)

require(
    SOURCE,
    (
        "session->text_renderer->owns_texture",
        "session->text_renderer->release_layer",
        "session->text_renderer->reset()",
        "pair.second.text_layer",
        "obs_enter_graphics()",
    ),
    "session destruction releases text targets and atlas under graphics ownership",
)

require(
    SOURCE,
    (
        "apply_gpu_layer_effect_stack",
        "draw_gpu_layer_texture",
        "apply_gpu_mask",
        "layer_motion_blur_effect",
        "publish_stable_gpu_frame",
    ),
    "GPU text texture remains in the unified effects/mask/motion/presentation compositor",
)

if "gpu-renderer-v31-lens-flare-dx11-keyword-fix" not in CACHE:
    errors.append("cache ABI does not supersede the Phase 12D text pixels/editor geometry generation")
else:
    passes.append("Transactional renderer ABI invalidates incomplete and older frame payloads")

require(
    HEADER,
    (
        "bool prepare(",
        "bool render(",
        "void release_layer(",
        "void reset()",
        "bool owns_texture(",
        "backend_available() const",
    ),
    "renderer API exposes explicit preparation, rendering, ownership and lifecycle",
)

if "Phase 12C now consumes" not in TEXT_README or "compatibility boundaries" not in DOC:
    errors.append("Phase 12C architecture/compatibility documentation is incomplete")
else:
    passes.append("Phase 12C architecture and compatibility boundaries are documented")

print("Phase 12C GPU text renderer structural audit")
for message in passes:
    print(f"  PASS: {message}")
for message in errors:
    print(f"  FAIL: {message}")
print(f"Result: {len(passes)} passed, {len(errors)} failed")
sys.exit(1 if errors else 0)
