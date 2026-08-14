#!/usr/bin/env python3
"""Development Version 330 3D-freeze and publication invariants."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


build = read("src/core/build-info.h")
cmake = read("CMakeLists.txt")
manifest = json.loads(read("tests/test-suite-manifest.json"))
model = read("Core/layer-model.h")
serialization = read("src/core/title-data.cpp")
popup = read("src/editor/properties-panel/popup-state.inc")
selection = read("src/editor/properties-panel/selection-refresh.inc")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
renderer = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
session_model = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
registration = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")


assert int(build.split('FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 330
assert int(cmake.split('OBS_FXM_DEVELOPMENT_VERSION "', 1)[1].split('"', 1)[0]) >= 330
assert manifest["development_version"] >= 330

# The old fields remain round-trippable, but cannot be authored or rendered.
for field in (
    "geometry_extrusion_enabled",
    "geometry_extrusion_depth",
    "geometry_bevel_depth",
    "geometry_bevel_segments",
    "geometry_extrusion_segments",
    "geometry_bevel_front",
    "geometry_bevel_back",
):
    assert field in model
    assert f'j["{field}"]' in serialization or f'json_bool(j, "{field}"' in serialization or f'json_int(j, "{field}"' in serialization or f'j.contains("{field}")' in serialization

assert "fxm_add_panel_section(vl, geometry_options_box_" not in popup
assert "geometry_options_box_->setVisible(false)" in popup
assert "geometry_options_box_->setVisible(false)" in selection
assert "tracks.push_back(&layer.geometry_extrusion_depth)" not in hierarchy
assert "tracks.push_back(&layer.geometry_bevel_depth)" not in hierarchy
assert "legacy_slice_extrusion_production_enabled = false" in renderer
assert "legacy_slice_extrusion_production_enabled &&" in renderer

for relative in (
    "Core/asset-runtime.cpp",
    "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc",
    "src/cache/cache-manager/cache-policy-invalidation.inc",
    "src/cache/cache-manager/disk-cache-storage.inc",
):
    text = read(relative)
    assert "geometry_extrusion" not in text
    assert "geometry_bevel" not in text

self_depth_start = session.index(
    "static bool gpu_layer_requires_self_depth_geometry(\n"
    "    const Title &title, const Layer &layer, double title_time)\n{"
)
self_depth_end = session.index("\n}", self_depth_start)
self_depth = session[self_depth_start:self_depth_end]
assert "return false;" in self_depth
assert "geometry_extrusion" not in self_depth

# Presentation targets are private until a complete generation is published.
for state in ("Free", "Rendering", "Complete", "Published"):
    assert state in session_model
assert "presentation_target_generations[2]" in session_model
assert "published_presentation_generation" in session_model
assert "PresentationTargetState::Rendering" in session
assert "PresentationTargetState::Complete" in session
assert "PresentationTargetState::Published" in session
assert "published_presentation_generation = generation" in session
assert "presentation_target_generations[active] ==" in session
assert "gs_texrender_get_texture(session->presentation_targets[active]) ==" in session
assert "gpu_session_has_published_frame_for_current_title(session)" in registration
assert "texture != session->final_texture" in registration
assert "action=reject-unpublished" in registration
assert "v49-legacy-slice-retired-transactional-presentation" in cache_abi

assert "### Development Version 330" in read("README.md")
assert "## Development Version 330" in read("docs/CHANGELOG.md")
