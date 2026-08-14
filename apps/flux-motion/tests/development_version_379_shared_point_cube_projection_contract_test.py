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
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 379
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 379
assert manifest["development_version"] >= 379
contract = "tests/development_version_379_shared_point_cube_projection_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The Point writer and receiver contain the same face projection body. This is
# the central invariant: no CPU-uploaded face matrices or second UV convention
# can map a receiver ray into another face of the 3x2 atlas.
pattern = (
    r"float3 point_shadow_face_projection\(int faceIndex, float3 direction\)"
    r"\s*\{(.*?)\n\}"
)
projection_bodies = re.findall(pattern, shader, re.DOTALL)
assert len(projection_bodies) == 2
assert projection_bodies[0] == projection_bodies[1]
for token in (
    "uniform int pointShadowFace;",
    "float2 point_shadow_atlas_uv(float3 direction, float2 atlasTexelSize)",
    "faceProjection.xy /",
    "max(faceProjection.z, 0.000001)",
    'fxm_effect_param(\n        session->shadow_effect, "pointShadowFace")',
    "gs_effect_set_int(point_face_param, static_cast<int>(face));",
):
    assert token in shader or token in lifecycle
assert (
    "o.pos = float4(faceProjection.xy, o.lightClip.z, faceProjection.z);"
    in shader
    or "faceDepth * faceProjection.z," in shader
)
assert "shadowPointFace0_0" not in shader
assert 'std::string("shadowPointFace") + suffix + "_"' not in presentation

# CPU reference for the exact shader contract. Every selected cube face has a
# positive signed depth and coordinates inside that face; cardinal directions
# land at the centres of the six distinct atlas tiles.
def face_index(d):
    x, y, z = d
    ax, ay, az = abs(x), abs(y), abs(z)
    if ax >= ay and ax >= az:
        return 0 if x >= 0 else 1
    if ay >= az:
        return 2 if y >= 0 else 3
    return 4 if z >= 0 else 5


def face_projection(face, d):
    x, y, z = d
    return (
        (-z, -y, x), (z, -y, -x), (x, z, y),
        (x, -z, -y), (x, -y, z), (-x, -y, -z),
    )[face]


directions = [
    (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0), (0.0, -1.0, 0.0),
    (0.0, 0.0, 1.0), (0.0, 0.0, -1.0),
    (69.523, -200.260, 115.0),
    (142.6, -411.0, 236.0),
]
for direction in directions:
    face = face_index(direction)
    px, py, depth = face_projection(face, direction)
    assert depth > 0.0
    assert abs(px) <= depth + 1.0e-9
    assert abs(py) <= depth + 1.0e-9

for face, direction in enumerate(directions[:6]):
    px, py, depth = face_projection(face, direction)
    assert math.isclose(px / depth, 0.0, abs_tol=1.0e-12)
    assert math.isclose(py / depth, 0.0, abs_tol=1.0e-12)

assert "v31-shared-point-cube-projection" in presentation
assert "v58-shared-point-cube-projection" in cache_abi

print("Development Version 379 shared Point cube projection contract: PASS")
