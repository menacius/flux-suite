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
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
visual_hash = read("src/cache/cache-manager/visual-hash-keying.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 398
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 398
assert manifest["development_version"] >= 398
assert "## Development Version 398 — explicit planar shadows and continuous text coverage" in changelog
assert "Development Version 398 planar shadows and text continuity" in readme

# Planar maps carry an explicit basis and fitted projection centre, then bind
# exactly the same values to the receiver shader.
for token in (
    "shadow_light_rights",
    "shadow_light_ups",
    "shadow_light_forwards",
    "shadow_projection_center_x",
    "shadow_projection_center_y",
):
    assert token in session
for token in (
    "QVector3D::crossProduct(forward, up)",
    "QVector3D::dotProduct(relative, light_right)",
    "QVector3D::dotProduct(relative, light_up)",
    "QVector3D::dotProduct(relative, forward)",
    "projection_center_x = (min_x + max_x) * 0.5f;",
    'session->shadow_effect, "shadowRight"',
    'session->shadow_effect, "shadowForward"',
):
    assert token in lifecycle
for token in (
    "uniform float3 shadowRight;",
    "uniform float3 shadowUp;",
    "uniform float3 shadowForward;",
    "float depth = dot(relative, shadowForward);",
    "float normalizedDepth = (depth - shadowNear)",
    "float depth = dot(relative, basisForward);",
    "float receiverDepth = (depth - nearPlane)",
):
    assert token in shader
for token in (
    "shadowBasisRightCenterX",
    "shadowBasisUpCenterY",
    "shadowBasisForward",
    "session->shadow_light_rights[index]",
    "session->shadow_projection_center_x[index]",
):
    assert token in presentation
assert "if (shadowKind == 1 || shadowKind == 3)" in shader
assert "shadow_light.type == TitleLightType::Parallel ? 3 : 1" in lifecycle

# Text-like casters use complete 4x4 coverage only in the shadow writer.
for token in (
    "for (int sampleY = 0; sampleY < 4; ++sampleY)",
    "for (int sampleX = 0; sampleX < 4; ++sampleX)",
    "footprint * 0.5 + sourceTexelSize * 0.5",
    "min(alphaCutoff, 0.0005)",
    "caster.conservative_alpha ? 1 : 0",
):
    assert token in shader or token in lifecycle
assert "const bool text_like_caster =" in lifecycle

assert "v69-explicit-planar-basis-text-footprint" in visual_hash
assert "v44-explicit-planar-basis-text-footprint" in presentation

current = "tests/development_version_398_planar_shadow_text_continuity_contract_test.py"
def contains(value):
    if isinstance(value, list):
        return current in value or any(contains(item) for item in value)
    if isinstance(value, dict):
        return any(contains(item) for item in value.values())
    return False
assert contains(manifest)
print("Development Version 398 planar shadow and text continuity contract: PASS")
