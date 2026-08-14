from pathlib import Path
import json
import math
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")


cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake
).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build
).group(1))
assert cmake_version >= 384
assert build_version >= 384
assert manifest["development_version"] >= 384
contract = (
    "tests/development_version_384_restored_3d_shadow_visibility_"
    "contract_test.py"
)
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Development 381 sent every light through a sparse blocker search. A
# punctual light has no blocker-dependent penumbra, so a missed/sparse search
# could return fully lit even though the centre ray was behind a caster. Point,
# Spot and Parallel paths now resolve those shadows directly with a stable
# 16-tap Poisson disk. This is also cheaper than the former 25-tap square PCF.
planar = shader[
    shader.index("float planar_shadow_visibility("):
    shader.index("int point_shadow_face_index(")
]
point = shader[
    shader.index("float point_shadow_visibility("):
    shader.index("float shadow_visibility(")
]
for visibility in (planar, point):
    assert "if (sourceRadius <= 0.0001)" in visibility
    branch_start = visibility.index("if (sourceRadius <= 0.0001)")
    direct = visibility[
        branch_start:
        visibility.index("physicalSearch", branch_start)
    ]
    assert "for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)" in direct
    assert "if (blockerCount < 0.5)" not in direct
    assert "occluded / 16.0" in direct

# Manual softness belongs to the visibility filter, not blocker discovery.
# Finite Source Size still drives blocker/receiver PCSS separation, while the
# exact centre ray anchors the umbra and prevents valid shadows being diluted
# into isolated specks.
for token in (
    "float manualRadius = max(softness, 0.0) * faceTexel * 2.0;",
    "float2 searchRadius = clamp(max(minimumSearch, physicalSearch),",
    "float centerDepth = sample_planar_shadow_depth(shadowIndex, uv);",
    "float coreShadow = max(centerShadow, coreOccluded / 8.0);",
    "float manualAngular = max(softness, 0.0) * faceTexel * 2.0;",
    "float searchAngular = clamp(max(faceTexel, physicalSearchAngular),",
    "float shadow = max(lerp(coreShadow, wideShadow, penumbraMix),",
):
    assert token in shader
assert (
    "float centerUvDepth = sample_shadow_depth(" in shader
    or "float centerShadow = point_shadow_bilinear_occlusion(" in shader
    or (
        "float2 centerDepths = point_shadow_point_sample_depths(" in shader
        and "float centerShadow = step(centerDepths.x + bias, centerDepths.y);" in shader
    )
)
assert "sample_planar_shadow_depth" in planar
assert "return 1.0;" in shader[
    shader.index("float sample_planar_shadow_depth("):
    shader.index("float2 shadow_poisson_offset(")
]

# R32F remains the high-precision primary target. If an OBS backend cannot
# begin that color/depth combination, the same numeric R-channel contract is
# retried in RGBA16F instead of permanently setting shadowEnabled=0.
for token in (
    "std::array<bool, 4> shadow_target_uses_rgba16f",
    "GS_R32F, GS_Z24_S8",
    "GS_RGBA16F, GS_Z24_S8",
    "auto begin_shadow_target = [&]()",
    "primary=R32F fallback=RGBA16F result=success",
    "stage=map-build slot=%1 result=",
    'QStringLiteral("target-unavailable")',
    "reason=%2 light=%3 type=%4",
):
    assert token in session or token in lifecycle

# Geometry from the supplied 3d test title. The Point light/caster projection
# overlaps a visible band at the top of the white receiver; a fully lit result
# is therefore not a valid interpretation of this fixture.
light = (646.0, 735.0, -188.0)
caster = (715.5234582747621, 534.7395771784636, -73.0)
receiver_z = 48.0
caster_size = (145.54054260253906, 139.73423767089844)
receiver_center_y = 604.5358209570807
receiver_height = 405.4053955078125
projection = (receiver_z - light[2]) / (caster[2] - light[2])
shadow_y = light[1] + projection * (caster[1] - light[1])
shadow_half_height = projection * caster_size[1] * 0.5
receiver_top = receiver_center_y - receiver_height * 0.5
overlap_height = shadow_y + shadow_half_height - receiver_top
assert math.isclose(projection, 236.0 / 115.0, rel_tol=1.0e-9)
assert overlap_height > 60.0

assert "v36-restored-3d-shadow-visibility" in presentation
assert "v63-restored-3d-shadow-visibility" in cache_abi

print("Development Version 384 restored 3D shadow visibility contract: PASS")
