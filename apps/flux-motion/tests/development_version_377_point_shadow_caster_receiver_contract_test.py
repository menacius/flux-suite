from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
resources = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 377
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 377
assert manifest["development_version"] >= 377
contract = "tests/development_version_377_point_shadow_caster_receiver_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Light/control/audio/container layers cannot enter the caster collection.
for token in (
    "layer_type_is_audio(layer->type)",
    "layer->type == LayerType::Adjustment",
    "layer->type == LayerType::Light",
    "layer->type == LayerType::Empty",
    "layer_type_is_container(layer->type)",
    "const bool casts_shadow = layer->material_casts_shadows;",
):
    assert token in lifecycle
# Development Version 377 aligned writer/receiver face projection. A later
# implementation may replace the 24 matrices with one projection function
# shared verbatim by both shaders.
assert "shadow_point_face_view_projections" in resources
matrix_projection = (
    "point_shadow_project_face(" in shader and
    "shadowPointFace0_0" in shader and
    "shadowPointFace3_5" in shader
)
shared_projection = (
    shader.count("float3 point_shadow_face_projection(") >= 2 and
    "pointShadowFace" in shader and
    "gs_effect_set_int(point_face_param" in lifecycle
)
assert matrix_projection or shared_projection
assert "facePosition = float2(-direction.z, direction.y)" not in shader

# Development Version 377 removed single-channel UNORM quantisation with
# RGB24. Later versions may supersede it with a native floating-point target.
rgb24_depth = (
    "float3 encode_shadow_depth(float depth)" in shader and
    "float decode_shadow_depth(float4 packedDepth)" in shader
)
float_depth = (
    "GS_R32F, GS_Z24_S8" in resources and
    ".Sample(pointSampler, uv).r" in shader
)
assert rgb24_depth or float_depth

assert "v29-matrix-exact-rgb24-point-shadows" in presentation
assert "v56-matrix-exact-rgb24-point-shadows" in cache_abi

print("Development Version 377 Point shadow caster/receiver contract: PASS")
