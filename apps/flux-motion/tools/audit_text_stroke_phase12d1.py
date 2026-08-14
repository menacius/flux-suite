#!/usr/bin/env python3
"""Regression audit for text-only stroke order/alignment in GPU and compatibility paths."""

from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
RENDERER = (ROOT / "src/rendering/title-gpu-text-renderer.cpp").read_text(encoding="utf-8", errors="replace")
SDF = (ROOT / "src/rendering/title-gpu-text-sdf.cpp").read_text(encoding="utf-8", errors="replace")
SOURCE = read_source_bundle(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
INLINE = (ROOT / "src/canvas/canvas-preview-inline-text.cpp").read_text(encoding="utf-8", errors="replace")
CANVAS = read_source_bundle(ROOT / "src/canvas/canvas-preview.cpp")

passes: list[str] = []
errors: list[str] = []

def require(text: str, tokens: tuple[str, ...], label: str) -> None:
    missing = [token for token in tokens if token not in text]
    if missing:
        errors.append(f"{label} misses: {', '.join(missing)}")
    else:
        passes.append(label)

require(
    SDF + RENDERER,
    (
        "case 0: return {width, 0.0f}",
        "case 2: return {0.0f, width}",
        "return {width * 0.5f, width * 0.5f}",
        "strokeOutside",
        "strokeInside",
        "coverageInside(signedDistance + strokeOutside, aa)",
        "coverageInside(signedDistance - strokeInside, aa)",
    ),
    "GPU text SDF has distinct outer/mid/inner coverage",
)

require(
    RENDERER,
    (
        "std::vector<Batch> behind_strokes",
        "std::vector<Batch> fills",
        "std::vector<Batch> front_strokes",
        "text_stroke_draw_phase(stroke.on_front)",
        "behind_strokes.begin()",
        "fills.begin()",
        "front_strokes.begin()",
    ),
    "GPU batches preserve global Behind -> Fill -> Front composition",
)

require(
    SOURCE,
    (
        "rich_text_fill_only_document",
        "format.setTextOutline(QPen(Qt::NoPen))",
        "rich_text_stroke_group_document",
        "render_rich_text_stroke_group",
        "QPainter::CompositionMode_DestinationOut",
        "QPainter::CompositionMode_DestinationIn",
    ),
    "compatibility rich-text path separates fill, stroke and alignment masks",
)

normal = re.search(
    r"if \(has_rich_text\) \{\s*draw_rich_stroke_phase\(false\);\s*draw_text_fill\(\);\s*draw_rich_stroke_phase\(true\);",
    SOURCE,
    re.S,
)
if normal:
    passes.append("normal compatibility text rendering uses Behind -> Fill -> Front")
else:
    errors.append("normal compatibility text rendering lacks explicit Behind -> Fill -> Front order")

transition = re.search(
    r"draw_stroke_phase\(false\);\s*draw_text_fill\(\);\s*draw_stroke_phase\(true\);",
    SOURCE,
    re.S,
)
if transition:
    passes.append("isolated text-transition rendering uses the same stroke order")
else:
    errors.append("isolated text-transition rendering lacks the shared stroke order")

require(
    CANVAS + INLINE,
    (
        "refresh_inline_text_edit(true, true)",
        "doc->idealWidth()",
        "layout->documentSize()",
        "render_to_frame();",
    ),
    "text entry keeps the pre-12D grow-beyond-current-bounds behavior",
)

for item in passes:
    print(f"  PASS: {item}")
if errors:
    for item in errors:
        print(f"  FAIL: {item}")
    print(f"Result: {len(passes)} passed, {len(errors)} failed")
    sys.exit(1)
print(f"Result: {len(passes)} passed, 0 failed")
