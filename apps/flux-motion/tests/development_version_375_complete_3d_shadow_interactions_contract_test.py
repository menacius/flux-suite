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
properties_load = read("src/editor/properties-panel/selection-refresh.inc")
legacy_properties = read("src/editor/title-properties-panel.cpp")
cache_abi = read("src/cache/cache-manager/visual-hash-keying.inc")


assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 375
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 375
assert manifest["development_version"] >= 375
contract = "tests/development_version_375_complete_3d_shadow_interactions_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Shadow participation is strictly opt-in on directly authored 3D raster
# layers. Casting is independent of lighting; receiving requires both lighting
# and shadow acceptance. Audio/control/container rows never enter this pass.
for token in (
    "layer->dimension_mode != LayerDimensionMode::ThreeD",
    "const bool casts_shadow = layer->material_casts_shadows;",
    "const bool receives_shadow = layer->material_accepts_lights &&",
    "layer->material_accepts_shadows;",
    "if (!casts_shadow && !receives_shadow)",
):
    assert token in lifecycle
for token in (
    "layer.dimension_mode == LayerDimensionMode::ThreeD",
    "layer.material_accepts_shadows",
    "const bool enabled = title.lighting_enabled &&",
    "layer.material_accepts_lights",
):
    assert token in presentation
assert "const bool material_supported = is_three_d_transform" in properties_load

# Point lights expose the same shadow controls as Spot/Parallel lights in both
# inspector implementations and render six 90-degree faces into one 3x2 atlas.
assert properties_load.count("const bool shadow_capable = distance_falloff ||") == 1
assert legacy_properties.count("const bool shadow_capable = distance_falloff ||") == 1
for token in (
    "shadow_light.type != TitleLightType::Point",
    "const bool point_shadow = shadow_light.type == TitleLightType::Point;",
    "light_projection.perspective(90.0f, 1.0f, near_plane, far_plane);",
    "? map_size * 3u : map_size",
    "? map_size * 2u : map_size",
    "const std::array<QVector3D, 6> face_directions",
    "const std::array<QVector3D, 6> face_ups",
    "gs_set_viewport(static_cast<int>((face % 3u) * map_size)",
):
    assert token in lifecycle

# Point maps store radial distance, select the matching atlas face in the
# receiver shader and derive PCF penumbra from Source Size at receiver distance.
for token in (
    "uniform int pointShadow;",
    "length(v.worldPosition - pointLightPosition) /",
    "float2 point_shadow_atlas_uv(",
    "float point_shadow_visibility(",
    "float sourceRadius = max(sourceSize, 0.0) * 0.5;",
    "max(receiverDistance - blockerDistance, 0.0)",
    "if (shadowKind == 2)",
):
    assert token in shader
for index in range(4):
    for prefix in (
        "shadowKind", "shadowLightPosition", "shadowFar",
    ):
        assert f"{prefix}{index}" in shader

# Each of the four light slots carries its own map kind, Point position and far
# distance from the shadow pass through to the material effect.
for token in (
    "std::array<int, 4> shadow_map_kind",
    "std::array<QVector3D, 4> shadow_light_positions",
    "std::array<float, 4> shadow_far_plane",
):
    assert token in resources
for token in (
    'std::string("shadowKind") + suffix',
    'std::string("shadowLightPosition") + suffix',
    'std::string("shadowFar") + suffix',
    "session->shadow_map_kind[index]",
    "session->shadow_light_positions[index]",
    "session->shadow_far_plane[index]",
):
    assert token in presentation

assert "v28-point-shadow-atlas-complete-3d-interactions" in presentation
assert "v55-complete-3d-point-shadows" in cache_abi

print("Development Version 375 complete 3D shadow interactions contract: PASS")
