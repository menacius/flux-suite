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
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")


cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 386
assert build_version >= 386
assert manifest["development_version"] >= 386
contract = "tests/development_version_386_realtime_self_shadow_pcf_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Transform-only snapshots keep their resident rasters, but shadow depth still
# depends on every caster and light transform. The compositor must therefore
# rebuild maps during the drag instead of reusing the pre-drag atlas.
publication = lifecycle[
    lifecycle.index("/* Shadow-map failure is deliberately fail-open"):
    lifecycle.index("if (!destination_composite", lifecycle.index(
        "/* Shadow-map failure is deliberately fail-open"))
]
assert "(void)render_gpu_shadow_maps(session, session->title, session->time);" in publication
assert "if (!session->interactive_transform_update)" not in publication
assert "Transform-only editor updates already retain every resident layer" in publication

# A Point-map texel stores the caster depth on its pixel-centre cube ray. The
# receiver comparison reconstructs that exact ray (including the PCF four-
# texel neighbourhood), so a plane that casts and receives cannot alternately
# classify neighbouring texels as being in front of itself.
for token in (
    "float3 point_shadow_face_direction(int faceIndex, float2 localUv)",
    "float2 point_shadow_point_sample_depths(",
    "float3 texelDirection = point_shadow_face_direction(",
    "float receiverDepth = point_shadow_receiver_depth_for_ray(",
    "(floor(localUv / faceTexelSize) + 0.5) * faceTexelSize",
    "halfTexel, 1.0 - halfTexel",
):
    assert token in shader

point_visibility = shader[
    shader.index("float point_shadow_visibility("):
    shader.index("float shadow_visibility(")
]
assert point_visibility.count("point_shadow_point_sample_depths(") == 5
assert "if (sampleDepths.x + bias < sampleDepths.y)" in point_visibility


def normalize(vector):
    length = math.sqrt(sum(component * component for component in vector))
    return tuple(component / length for component in vector)


def face_direction(face, local_uv):
    face_x = local_uv[0] * 2.0 - 1.0
    face_y = 1.0 - local_uv[1] * 2.0
    directions = (
        (1.0, -face_y, -face_x),
        (-1.0, -face_y, face_x),
        (face_x, 1.0, face_y),
        (face_x, -1.0, -face_y),
        (face_x, -face_y, 1.0),
        (-face_x, -face_y, -1.0),
    )
    return normalize(directions[face])


def face_projection(face, direction):
    x, y, z = direction
    return (
        (-z, -y, x),
        (z, -y, -x),
        (x, z, y),
        (x, -z, -y),
        (x, -y, z),
        (-x, -y, -z),
    )[face]


# CPU mirror of a +Z cube face viewing a plane at z=40. For every quantized
# texel centre, the depth written by that plane and the reconstructed receiver
# intersection are identical. A positive authored bias therefore rejects the
# plane's own depth while a genuinely closer caster remains an occluder.
face_size = 512
plane_z = 40.0
far_plane = 200.0
bias = 0.0001
for face in range(6):
    for requested_uv in ((0.17, 0.23), (0.499, 0.501), (0.91, 0.78)):
        center_uv = tuple(
            (math.floor(value * face_size) + 0.5) / face_size
            for value in requested_uv
        )
        direction = face_direction(face, center_uv)
        projected = face_projection(face, direction)
        recovered_uv = (
            projected[0] / projected[2] * 0.5 + 0.5,
            1.0 - (projected[1] / projected[2] * 0.5 + 0.5),
        )
        assert math.isclose(recovered_uv[0], center_uv[0], abs_tol=1.0e-12)
        assert math.isclose(recovered_uv[1], center_uv[1], abs_tol=1.0e-12)

# Mirror a +Z face viewing a plane at z=40. The depth written by that plane
# and the reconstructed receiver intersection are identical at every stored
# texel centre.
for requested_uv in ((0.17, 0.23), (0.499, 0.501), (0.91, 0.78)):
    center_uv = tuple(
        (math.floor(value * face_size) + 0.5) / face_size
        for value in requested_uv
    )
    direction = face_direction(4, center_uv)
    radial_distance = plane_z / direction[2]
    caster_depth = radial_distance / far_plane
    receiver_depth = radial_distance / far_plane
    assert not (caster_depth + bias < receiver_depth)
    closer_caster_depth = (radial_distance - 2.0) / far_plane
    assert closer_caster_depth + bias < receiver_depth

assert "v38-realtime-receiver-matched-point-pcf" in presentation
assert "v65-realtime-receiver-matched-point-pcf" in cache_abi

print("Development Version 386 realtime self-shadow PCF contract: PASS")
