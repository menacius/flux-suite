#!/usr/bin/env python3
"""Regression contract for Development Version 214.2 animated-group rendering."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
cache = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
types = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
logger = read("src/core/title-logger.cpp")
layout = read("src/editor/title-editor/layout-template-tools.inc")
window = read("src/editor/title-editor/window-session.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
note = read("docs/RENDERING_AND_CACHE.md")

# Normal groups must not pay for a second silhouette render.
assert "const bool has_group_backdrop_effect = std::any_of(" in session
assert "if (has_group_backdrop_effect) {" in session
assert "gpu_note_group_silhouette_pass(session, layer.id);" in session
assert "if (has_group_backdrop_effect && !group_silhouette)" in session

# Group output uses persistent double-buffered targets, not per-frame trio allocation.
assert 'std::string("group-result|") + group.id' in session
assert "for (gs_texrender_t *&target : result_cache.targets)" in session
assert "result_cache.active_target = result_index" in session
assert "unconditional third full-canvas copy" in session
function = session[session.index("static gs_texture_t *render_gpu_group_graph_texture(", session.index("gpu_record_group_render_diagnostics")):session.index("static bool gpu_cached_image_rect")]
assert "gs_texrender_t *published = gs_texrender_create" not in function
assert "gs_texrender_destroy(ping)" not in function
assert "gs_texrender_destroy(pong)" not in function

# One-second aggregate GPU diagnostics and dedicated logger categories.
assert "struct GroupRenderDiagnostics" in types
assert "group_render_diagnostics" in cache
assert "GPU group render group=%1" in session
assert "avgMs=%4 maxMs=%5" in session
assert "targetCreates=%7 silhouettePasses=%8" in session
assert 'QStringLiteral("Grouping")' in logger
assert 'QStringLiteral("Coordinates")' in logger

# Command and coordinate logs distinguish mutation, sampling, refresh and render costs.
for text in (
    "Group command begin", "Group command complete",
    "Ungroup command begin", "Ungroup command complete",
    "Add-to-group command begin", "Add-to-group command complete",
    "Remove-from-group command begin", "Remove-from-group command complete",
):
    assert text in layout
assert "mutationUs=" in layout and "refreshUs=" in layout and "commitUs=" in layout and "totalUs=" in layout
assert "Bind reparent applied" in window
assert "keyframesBefore=%9 keyframesAfter=%10" in window
assert "bind-reparent-failed" in layout

assert "Optimized dense-text and group rendering" in readme
assert "Development Version 214.2 — Animated Group Render Performance and Diagnostics" in changelog
assert "Group rendering reuses persistent ping/pong targets" in note
assert "Threading and lifetime contract" in note

print("Development Version 214.2 animated-group render performance/logging contract passed")
