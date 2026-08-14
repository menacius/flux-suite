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
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 380
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 380
assert manifest["development_version"] >= 380
contract = "tests/development_version_380_distance_stable_point_shadows_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The radial minimum comes from the complete finite visual quad rather than
# corners. This is needed when the closest point of a large receiver lies in
# its interior.
for token in (
    "static float point_triangle_distance_squared(",
    "std::vector<std::array<QVector3D, 4>> scene_world_quads;",
    "scene_world_quads.push_back({p00, p10, p11, p01});",
    "point_triangle_distance_squared(\n                    light_position",
    "min_depth * 0.5f - depth_margin",
):
    assert token in lifecycle
assert "point_shadow\n        ? 0.01f" not in lifecycle

# Bias is authored over the depth occupied by the scene and normalized only at
# upload. Its world-space size therefore does not grow with light distance.
for token in (
    "const double occupied_point_depth_span = std::max(",
    "static_cast<double>(max_depth - min_depth)",
    "authored_shadow_bias * occupied_point_depth_span /",
    "static_cast<double>(far_plane)",
):
    assert token in lifecycle

# A cube face's signed W is the dominant vector component, which is always at
# least radial/sqrt(3). The 0.5 radial near bound is consequently safe on all
# six faces while avoiding the old 0.01-to-remote-light precision ratio.
directions = [
    (1.0, 1.0, 1.0), (10.0, -4.0, 3.0),
    (-2.0, 9.0, -5.0), (0.1, 0.2, -8.0),
]
for direction in directions:
    radial = math.sqrt(sum(component * component for component in direction))
    face_depth = max(abs(component) for component in direction)
    assert face_depth >= radial / math.sqrt(3.0) - 1.0e-12
    assert face_depth > radial * 0.5


def depth_contract(light_distance, occupied_span, authored_bias=0.0025):
    margin = max(10.0, occupied_span * 0.1)
    minimum = light_distance
    maximum = light_distance + occupied_span
    near = max(0.01, minimum * 0.5 - margin)
    far = max(near + 1.0, maximum + margin)
    occupied = max(1.0, maximum - minimum + 2.0 * margin)
    normalized_bias = authored_bias * occupied / far
    return near, far, normalized_bias * far


near_10k, far_10k, world_bias_10k = depth_contract(10000.0, 120.0)
near_50k, far_50k, world_bias_50k = depth_contract(50000.0, 120.0)
assert far_10k / near_10k < 2.1
assert far_50k / near_50k < 2.1
assert math.isclose(world_bias_10k, world_bias_50k, rel_tol=1.0e-12)

assert "v32-distance-stable-point-shadows" in presentation
assert "v59-distance-stable-point-shadows" in cache_abi

print("Development Version 380 distance-stable Point shadows contract: PASS")
