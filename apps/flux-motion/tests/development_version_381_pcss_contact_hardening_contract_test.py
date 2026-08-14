from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 381
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 381
assert manifest["development_version"] >= 381
contract = "tests/development_version_381_pcss_contact_hardening_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The fixed square 5x5 filter is gone. A stable Poisson disk performs a
# blocker search followed by an adaptive PCSS filter only where blockers exist.
for token in (
    "float2 shadow_poisson_offset(int sampleIndex)",
    "float2 shadow_sample_rotation(float3 worldPosition, int shadowIndex)",
    "for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)",
    "if (blockerCount < 0.5)\n        return 1.0;",
    "for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)",
    "float physicalPenumbraAngular = sourceRadius *",
    "max(receiverDistance - blockerDistance, 0.0)",
    "float separation = max(receiverLinear - blockerLinear, 0.0);",
):
    assert token in shader
assert "for (int y = -2; y <= 2; ++y)" not in shader[
    shader.index("float planar_shadow_visibility("):
    shader.index("float4 apply_material_lighting(")
]

# Point, Spot and Parallel maps all carry the depth/projection data required
# for physical contact hardening. Point keeps its per-tap receiver-plane test.
for token in (
    "uniform float shadowNear0;",
    "uniform float2 shadowProjectionSpan3;",
    "float shadow_linear_depth(float depth, int shadowKind,",
    "if (shadowKind == 1 || shadowKind == 3)",
    "float receiverPlaneDenominator =",
    "std::array<float, 4> shadow_near_plane",
    "std::array<float, 4> shadow_projection_span_x",
    "std::array<float, 4> shadow_projection_span_y",
):
    assert token in shader or token in session
for token in (
    'std::string("shadowNear") + suffix',
    'std::string("shadowProjectionSpan") + suffix',
    "session->shadow_near_plane[index]",
    "session->shadow_projection_span_x[index]",
    "session->shadow_projection_span_y[index]",
):
    assert token in presentation

# PCSS remains independent of the Point atlas projection. Current builds route quality through the persisted shadowmap preference.
assert "const uint32_t authored_shadow_map_size = static_cast<uint32_t>" in lifecycle
assert "const uint32_t map_size = session->editor_draft" in lifecycle
assert "shadow_light.type == TitleLightType::Parallel ? 3 : 1" in lifecycle
assert "shadow_light.shadow_softness.evaluate(title_time)" in lifecycle
assert "finite_source_softness" not in lifecycle

# Renderer/effect caches must never reuse pixels from the replaced algorithm.
assert "v33-pcss-contact-hardening" in presentation
assert "v60-pcss-contact-hardening" in cache_abi

print("Development Version 381 PCSS contact-hardening shadows contract: PASS")
