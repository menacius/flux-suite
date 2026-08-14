# Rendering Engine

Target home for the unified OBS GPU compositor, layer materials, textures,
alpha handling, masks, effects, render caches, invalidation, and
performance-critical drawing. Public APIs remain backend-oriented and
independent from editor widgets.

Phase 12C adds a session-local GPU text backend:

- `title-gpu-text-renderer.*` consumes immutable Phase 12B glyph/run/cluster
  layouts and paint runs.
- Missing glyphs are rasterized once through `QRawFont`, converted to an R8
  signed-distance field, and retained in bounded atlas pages.
- Text fill, gradient, inline stroke, underline, and strikethrough are composed
  by an OBS effect shader into double-buffered layer targets.
- Layer-wide text strokes are resolved as the document fallback while sparse
  inline stroke ranges remain independent; Behind/Front order is honored for
  every stroke alignment.
- The editor's canvas gradient handles can target the active rich-text range,
  allowing multiple gradients in one text box without collapsing them into the
  layer default.
- The published layer texture continues through the same masks, effects, blend
  modes, motion blur, preview/program presentation, and final cache readback as
  every other GPU layer.
- Color-font glyphs, ticker output, and active text-unit transitions remain on
  the exact compatibility raster path until their dedicated GPU representation
  is implemented.

`title-gpu-text-sdf.*` is intentionally Qt/OBS-independent so its distance-field
math can be compiled and tested in isolation.

Phase 13 moves masks and track mattes into a recursive GPU mask graph:

- alpha, inverse alpha, luma and inverse luma are resolved in a mask shader;
- parented and nested matte sources render through the normal GPU layer graph;
- effects can run before or after the matte according to the layer contract;
- transformed mattes are retained in a double-buffered GPU texture cache;
- scene masks consume the same GPU matte textures; and
- no Cairo/QPainter alpha-mask compositor remains.

Development Version 220 routes built-in effects through an indexed first-use
shader cache. GPU effect stacks reuse session-owned evaluated-state/pass arrays;
the compatibility bridge reuses a dimension-aware upload texture, ping-pong
render targets, staging surface and transfer buffers until dimensions change.
