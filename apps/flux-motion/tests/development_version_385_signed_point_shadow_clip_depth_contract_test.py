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
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 385
assert build_version >= 385
assert manifest["development_version"] >= 385
contract = (
    "tests/development_version_385_signed_point_shadow_clip_depth_"
    "contract_test.py"
)
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The Dev384 runtime log proved target allocation, caster/receiver discovery,
# effect compilation and draw-loop submission all succeeded. The remaining
# writer mixed a Qt perspective-matrix Z with the manually projected cube-face
# W. D3D clips against 0 <= Z <= W, so that mixed convention can discard every
# fragment while the CPU still labels the draw loop successful.
writer = shader[
    shader.index("VertDataOut VSShadow(VertDataIn v)"):
    shader.index("float4 PSShadow(VertDataOut v)")
]
point_branch = writer[
    writer.index("if (pointShadow != 0)"):
    writer.index("} else {")
]
for token in (
    "float3 faceProjection = point_shadow_face_projection(",
    "float faceDepth = clamp(faceProjection.z /",
    "max(pointShadowFar, 0.0001), 0.0, 1.0);",
    "faceDepth * faceProjection.z,",
    "faceProjection.z);",
):
    assert token in point_branch
assert "o.lightClip.z" not in point_branch


def face_projection(direction):
    x, y, z = direction
    axis = tuple(abs(value) for value in direction)
    if axis[0] >= axis[1] and axis[0] >= axis[2]:
        return (-z, -y, x) if x >= 0.0 else (z, -y, -x)
    if axis[1] >= axis[2]:
        return (x, z, y) if y >= 0.0 else (x, -z, -y)
    return (x, -y, z) if z >= 0.0 else (-x, -y, -z)


# For any selected face the dominant signed depth is positive. The new clip
# Z/W is therefore inside D3D's range and increases monotonically along one
# atlas ray, so Z24 keeps the nearest caster.
far_plane = 1000.0
ray = (2.0, -1.0, 5.0)
depths = []
for distance_scale in (10.0, 30.0, 70.0):
    direction = tuple(component * distance_scale for component in ray)
    projected = face_projection(direction)
    assert projected[2] > 0.0
    face_depth = max(0.0, min(projected[2] / far_plane, 1.0))
    clip_z = face_depth * projected[2]
    clip_w = projected[2]
    assert 0.0 <= clip_z <= clip_w
    depths.append(clip_z / clip_w)
assert depths == sorted(depths)
assert all(math.isfinite(value) for value in depths)

# A submitted draw is no longer called a successful map without a GPU pixel
# query. The receiver-side log proves whether the generated map is actually
# bound to an opted-in 3D material and records the comparison inputs needed by
# any future runtime diagnosis.
for token in (
    "result=submitted",
    "writer=%11",
    "signed-face-depth",
    "std::array<bool, 4> shadow_receiver_binding_logged",
    "stage=receiver-bind slot=%1 result=%2 layer=%3",
    "acceptsLights=%4 acceptsShadows=%5",
    "mapValid=%6 texture=%7 kind=%8",
    "darkness=%9 bias=%10 softness=%11 sourceSize=%12",
):
    assert token in lifecycle or token in session or token in presentation

assert "v37-signed-point-shadow-clip-depth" in presentation
assert "v64-signed-point-shadow-clip-depth" in cache_abi

print("Development Version 385 signed Point shadow clip-depth contract: PASS")
