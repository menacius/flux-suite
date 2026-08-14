# Text Engine

The text engine owns the canonical rich-text document, evaluated defaults,
immutable shaped layout and paint-run data used by the editor, OBS source and
the GPU renderer.

- `title-rich-text.*` is the single persistent document model. It owns UTF-8
  text, sparse character runs, paragraph blocks, typing state and automatic
  styling rules.
- `title-text-layout.*` owns renderer-neutral glyph, cluster, run and line data,
  shape/geometry keys, the bounded persistent cache, hit testing and paint
  runs.
- `title-text-layout-qt.cpp` is the current CPU shaping backend. It may use
  `QTextLayout` on a cache miss, but it never creates a `QTextDocument`,
  `QImage`, `QPainter` or Cairo surface and stores no Qt objects in the output.

Shaping properties and paint properties are intentionally separate. A fill,
gradient, underline or strikethrough edit updates paint runs without rebuilding
glyph positions. Font, tracking, H/V scale, baseline, OpenType, paragraph or
geometry changes invalidate the immutable layout.

Phase 12C now consumes this output through persistent glyph-atlas textures and
GPU quads. Paint-only edits rebuild material batches without reshaping; the
result is drawn into persistent double-buffered layer targets and then enters
the common GPU compositor for masks, effects, blending, motion blur, clipping,
transitions, preview/program output, and final cache readback.

The compatibility Qt raster adapter remains only for color-font glyphs, ticker
output, and active per-character/word/sentence text transitions.

## Phase 12D editor geometry

The editor canvas reads selection rectangles and caret positions from the same
immutable layout consumed by the GPU text renderer. `QTextEdit` remains the
transparent IME/input bridge and retains its live document-size measurement for
point-text auto-grow, so typing can continue beyond the previous textbox bounds
exactly as before Phase 12D. Cursor-only movement updates the overlay without
rebuilding artwork; actual edits publish the model and any auto-grown geometry
in the established edit transaction.
