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
resources = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 378
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 378
assert manifest["development_version"] >= 378
contract = "tests/development_version_378_point_shadow_receiver_plane_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Shadow maps contain numeric depth in a linear floating-point channel.  They
# must not pass through a BGRA/sRGB target or colour-channel packing.
assert "GS_R32F, GS_Z24_S8" in resources
for token in (
    "float3 encode_shadow_depth(float depth)",
    "float decode_shadow_depth(float4 packedDepth)",
    "return float4(encode_shadow_depth(depth), 1.0);",
):
    assert token not in shader
assert ".Sample(pointSampler, uv).r" in shader
assert "return float4(clamp(depth, 0.0, 1.0), 0.0, 0.0, 1.0);" in shader

# Soft Point-light taps compare against the depth where each neighbouring ray
# intersects the current receiver plane.  This lets one visual layer cast and
# receive without producing a wedge-shaped self-shadow.
for token in (
    "float3 receiverNormal = normalize(worldNormal);",
    "float receiverPlaneNumerator = dot(fromLight, receiverNormal);",
    "dot(sampleDirection, receiverNormal)",
    "float planeDistance = receiverPlaneNumerator /",
    "float sampleReceiverDepth = sampleReceiverDistance / farPlane;",
):
    assert token in shader
assert (
    "step(casterDepth + bias, sampleReceiverDepth)" in shader
    or (
        "float3 texelDirection = point_shadow_face_direction(" in shader
        and (
            "step(casterDepth + bias, texelReceiverDepth)" in shader
            or "step(sampleDepths.x + bias, sampleDepths.y)" in shader
        )
    )
)
point_visibility = shader[
    shader.index("float point_shadow_visibility("):
    shader.index("float shadow_visibility(")
]
assert "step(casterDepth + bias, receiverDepth)" not in point_visibility

assert "v30-float32-receiver-plane-point-shadows" in presentation
assert "v57-float32-receiver-plane-point-shadows" in cache_abi

# Geometry copied from the supplied 3d test title.  The real red caster's
# projection meets only the top of the white receiver; the former large black
# wedge cannot be its shadow and is covered by the receiver-plane correction.
light = (646.0, 735.0, -188.0)
caster = (715.523, 534.740, -73.0)
receiver_z = 48.0
projection = (receiver_z - light[2]) / (caster[2] - light[2])
shadow_y = light[1] + projection * (caster[1] - light[1])
shadow_half_height = projection * 139.734 * 0.5
receiver_top = 604.536 - 405.405 * 0.5
assert math.isclose(projection, 236.0 / 115.0, rel_tol=1e-9)
assert shadow_y - shadow_half_height < receiver_top
assert shadow_y + shadow_half_height > receiver_top

print("Development Version 378 Point shadow receiver-plane contract: PASS")
