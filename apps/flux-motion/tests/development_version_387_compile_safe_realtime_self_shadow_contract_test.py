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
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")


cmake_dev = int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "([0-9]+)"', cmake).group(1))
build_dev = int(re.search(r'FXM_DEVELOPMENT_VERSION "([0-9]+)"', build).group(1))
assert cmake_dev >= 387
assert build_dev >= 387
assert manifest["development_version"] >= 387
contract = (
    "tests/development_version_387_compile_safe_realtime_self_shadow_"
    "contract_test.py"
)
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The Dev386 runtime log stopped after depth-run-summary even for a zero-light
# title. Its nested four-neighbour comparison expanded every sample in the
# common layer shader and blocked the D3D11 graphics thread during first-use
# compilation. Keep one texture lookup per Poisson tap.
point = shader[
    shader.index("float2 point_shadow_point_sample_depths("):
    shader.index("float shadow_visibility(")
]
for forbidden in (
    "point_shadow_bilinear_occlusion",
    "point_shadow_compare_at_face_texel",
    "compare00",
    "compare10",
    "compare01",
    "compare11",
    "blendWeight",
):
    assert forbidden not in point
sample_helper = point[
    point.index("float2 point_shadow_point_sample_depths("):
    point.index("float point_shadow_visibility(")
]
assert sample_helper.count("sample_shadow_depth(") == 1
assert "float3 texelDirection = point_shadow_face_direction(" in sample_helper
assert "float receiverDepth = point_shadow_receiver_depth_for_ray(" in sample_helper

visibility = point[point.index("float point_shadow_visibility("):]
assert visibility.count("point_shadow_point_sample_depths(") == 5
assert visibility.count("step(sampleDepths.x + bias, sampleDepths.y)") == 3
assert "step(centerDepths.x + bias, centerDepths.y)" in visibility

# Real-time drag still rebuilds depth atlases; only the compile-explosive
# visibility expansion was removed.
publication = lifecycle[
    lifecycle.index("/* Shadow-map failure is deliberately fail-open"):
    lifecycle.index("if (!destination_composite", lifecycle.index(
        "/* Shadow-map failure is deliberately fail-open"))
]
assert "(void)render_gpu_shadow_maps(session, session->title, session->time);" in publication
assert "if (!session->interactive_transform_update)" not in publication

# Dev388 moved the same first-use layer-copy compile out of the draw path.
# Keep either the old explicit diagnostic bracket or the new async queue marker
# so regressions remain attributable instead of ending after depth-run-summary.
assert (
    "stage=effect-compile-begin effect=gpu-layer-copy revision=%1" in presentation
    or '"core:layer-copy"' in presentation
)

assert "v39-compile-safe-receiver-matched-point-pcf" in presentation
assert "v66-compile-safe-receiver-matched-point-pcf" in cache_abi

print("Development Version 387 compile-safe realtime self-shadow contract: PASS")
