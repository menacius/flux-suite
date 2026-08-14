from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
presentation = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")
gpu_text = read("src/rendering/title-gpu-text-renderer.cpp")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 382
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 382
assert manifest["development_version"] >= 382
contract = "tests/development_version_382_shadow_runtime_recovery_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The unverified focused projection that produced empty maps is completely
# removed. Point lights again use the shared signed six-face writer/receiver
# mapping that was runtime-proven in Development Versions 379/380.
for forbidden in (
    "focused_point_shadow",
    "cube_point_shadow",
    "kMaximumFocusedAngle",
    "light_projection.frustum(",
    "shadowKind == 4",
):
    assert forbidden not in lifecycle
    assert forbidden not in shader
for required in (
    "const bool point_shadow = shadow_light.type == TitleLightType::Point;",
    "light_projection.perspective(90.0f, 1.0f, near_plane, far_plane);",
    "session->shadow_map_kind[shadow_slot] = point_shadow ? 2 :",
    "gs_effect_set_int(point_shadow_param, point_shadow ? 1 : 0);",
    "if (point_shadow) {",
):
    assert required in lifecycle

# Interactive transforms retain the last complete shadow maps. The render
# after release clears the flag through the normal non-transform update and
# rebuilds exact shadows once, so overlays never wait for the caster pass.
assert "bool interactive_transform_update = false;" in session
assert "session->interactive_transform_update = transform_only_update;" in playback
assert (
    "if (!session->interactive_transform_update)\n        (void)render_gpu_shadow_maps" in lifecycle
    or (
        "Transform-only editor updates already retain every resident layer" in lifecycle
        and "(void)render_gpu_shadow_maps(session, session->title, session->time);" in lifecycle
    )
)

# OBS' D3D11 backend rejects one-component texture-coordinate vertex streams
# (the attached runtime log reported "Invalid texture vertex size specified").
# Keep scalar shader values packed in supported vec2 streams so GPU text stays
# resident and editor overlay publication does not fall back to CPU rendering.
for token in (
    "float2 opacityData : TEXCOORD2;",
    "float2 atlasScaleData : TEXCOORD5;",
    "o.opacity = v.opacityData.x;",
    "o.atlasPixelsPerLogical = v.atlasScaleData.x;",
    "data->tvarray[2].width = 2;",
    "data->tvarray[5].width = 2;",
    "static_cast<vec2 *>(data->tvarray[2].array)",
    "static_cast<vec2 *>(data->tvarray[5].array)",
):
    assert token in gpu_text
for forbidden in (
    "data->tvarray[2].width = 1;",
    "data->tvarray[5].width = 1;",
):
    assert forbidden not in gpu_text

# Restore the proven responsive shadow budget while retaining PCSS and
# its lit-pixel early-out. Dev389 routes the budget through the Advanced
# shadowmap preference instead of hard-coding 512/1024.
for token in (
    "authored_shadow_map_size",
    "for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)",
    "if (blockerCount < 0.5)\n        return 1.0;",
    "for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)",
):
    assert token in lifecycle or token in shader

assert "v34-shadow-runtime-recovery" in presentation
assert "v61-shadow-runtime-recovery" in cache_abi

print("Development Version 382 shadow runtime recovery contract: PASS")
