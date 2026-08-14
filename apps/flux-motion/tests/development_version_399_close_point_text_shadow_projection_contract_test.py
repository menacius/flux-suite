#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
destroy = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
visual_hash = read("src/cache/cache-manager/visual-hash-keying.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 399
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 399
assert manifest["development_version"] >= 399
assert "# v0.8.13-alpha — Development Version 399" in changelog
assert "Development Version 399 close Point-light text-shadow projection" in readme

for token in (
    "gs_vertbuffer_t *close_point_text_shadow_grid = nullptr;",
    "triangular gaps reported below roughly 300 px",
):
    assert token in session
for token in (
    "create_close_point_text_shadow_grid()",
    "constexpr int kDivisions = 32;",
    "bool subdivide_close_point_projection = false;",
    "perpendicular_light_distance < 384.0f",
    "projected_extent_ratio > 1.5f",
    "point_shadow &&",
    "text_like_caster &&",
    "gs_load_vertexbuffer(",
    "session->close_point_text_shadow_grid",
    "gs_draw(GS_TRIS, 0, 0);",
    "subdividedTextCasters=%12",
):
    assert token in lifecycle
assert "gs_vertexbuffer_destroy(session->close_point_text_shadow_grid);" in destroy
assert "v70-close-point-text-shadow-projection-grid" in visual_hash
assert "v45-close-point-text-shadow-projection-grid" in presentation

current = "tests/development_version_399_close_point_text_shadow_projection_contract_test.py"
def contains(value):
    if isinstance(value, list):
        return current in value or any(contains(item) for item in value)
    if isinstance(value, dict):
        return any(contains(item) for item in value.values())
    return False
assert contains(manifest)
print("Development Version 399 close Point-light text shadow projection contract: PASS")
