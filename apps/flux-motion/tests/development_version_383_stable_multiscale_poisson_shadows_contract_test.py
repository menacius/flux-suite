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

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 383
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 383
assert manifest["development_version"] >= 383
contract = (
    "tests/development_version_383_stable_multiscale_poisson_shadows_"
    "contract_test.py"
)
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# The blocker search is bounded by the apparent emitter size at the receiver,
# never by the near plane. The latter could explode to a cross-face 0.55-radian
# search and dilute valid Point shadows into isolated artifacts.
for token in (
    "float physicalSearchAngular = sourceRadius /",
    "max(receiverDistance, 0.0001);",
    "faceTexel, 0.12);",
    "for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)",
    "if (blockerCount < 0.5)\n        return 1.0;",
):
    assert token in shader
point_filter = shader[
    shader.index("float point_shadow_visibility("):
    shader.index("float shadow_visibility(")
]
assert "receiverDistance * safeNear" not in point_filter
assert "faceTexel, 0.55" not in point_filter

# A compact contact kernel is blended into the wider depth-derived Poisson
# kernel. This preserves a coherent umbra while Source Size and true
# caster/receiver separation progressively broaden the penumbra.
for token in (
    "float coreAngular = min(filterAngular,",
    "float coreOccluded = 0.0;",
    "float wideOccluded = 0.0;",
    "float physicalPenumbraAngular = sourceRadius *",
    "float penumbraMix = clamp(physicalPenumbraAngular /",
):
    assert token in point_filter
assert (
    "float shadow = lerp(coreShadow, wideShadow, penumbraMix);" in point_filter
    or "float shadow = max(lerp(coreShadow, wideShadow, penumbraMix)," in point_filter
)

# The sample pattern has one fixed rotation per light. Per-pixel world hashes
# caused the reported grain/crawling artifacts.
rotation = shader[
    shader.index("float2 shadow_sample_rotation("):
    shader.index("float shadow_linear_depth(")
]
assert "0.61803398875" in rotation
assert "sin(dot(worldPosition" not in rotation

# Keep the complete runtime-proven six-face writer/receiver projection and the
# real-time overlay scheduling fix from Development Version 382.
for token in (
    "const bool point_shadow = shadow_light.type == TitleLightType::Point;",
    "if (point_shadow) {",
    "session->shadow_map_kind[shadow_slot] = point_shadow ? 2 :",
):
    assert token in lifecycle
assert (
    "if (!session->interactive_transform_update)\n        (void)render_gpu_shadow_maps" in lifecycle
    or (
        "Transform-only editor updates already retain every resident layer" in lifecycle
        and "(void)render_gpu_shadow_maps(session, session->title, session->time);" in lifecycle
    )
)

assert "v35-stable-multiscale-poisson-shadows" in presentation
assert "v62-stable-multiscale-poisson-shadows" in cache_abi

print("Development Version 383 stable multiscale Poisson shadows contract: PASS")
