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
model = read("Core/layer-model.h")
serialization = read("src/core/title-data.cpp")
transform = read("src/rendering/layer-transform-3d.cpp")
properties_h = read("src/editor/properties-panel.h")
properties_ui = read("src/editor/properties-panel/popup-state.inc")
properties_load = read("src/editor/properties-panel/selection-refresh.inc")
properties_connections = read(
    "src/editor/properties-panel/construction-transform-character.inc")
locale = read("data/locale/en-US.ini")


assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 374
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 374
assert manifest["development_version"] >= 374
contract = "tests/development_version_374_multi_light_shadows_scene_depth_contract_test.py"
assert contract in manifest["areas"]["editor_gui"]["python"]
assert contract in manifest["areas"]["rendering_2d_3d"]["python"]

# Four shadow resources stay index-aligned with the four explicit light slots,
# are allocated independently, and are released through the common lifecycle.
for token in (
    "std::array<gs_texrender_t *, 4> shadow_targets",
    "std::array<bool, 4> shadow_map_valid",
    "std::array<QMatrix4x4, 4> shadow_view_projections",
    "session->shadow_targets[index] = gs_texrender_create(",
    "for (gs_texrender_t *&shadow_target : session->shadow_targets)",
):
    assert token in resources or token in read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")

for token in (
    "static bool render_gpu_shadow_maps(",
    "const std::size_t current_slot = light_slot++;",
    "enabled non-shadow lights do, preserving 1:1 map/light alignment",
    "render_gpu_shadow_map(",
    "render_gpu_shadow_maps(session, session->title, session->time)",
):
    assert token in lifecycle

# Source Size reaches the shader for PCSS projection from blocker/receiver
# depth. The per-slot visibility enters accumulate_light radiance instead of
# multiplying the final aggregate lighting result.
for index in range(4):
    for prefix in (
        "shadowMap", "shadowEnabled", "shadowViewProj", "shadowDarkness",
        "shadowBias", "shadowSoftness", "shadowTexelSize",
    ):
        assert f"{prefix}{index}" in shader
assert "float sourceRadius = max(sourceSize, 0.0) * 0.5;" in shader
assert "shadow_light.shadow_softness.evaluate(title_time)" in lifecycle
assert "for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)" in shader
assert "for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex)" in shader
assert "float sourceSize, int falloffMode, float lightVisibility" in shader
assert "clamp(lightVisibility, 0.0, 1.0)" in shader
assert "float visibility = shadow_visibility(worldPosition);" not in shader
for token in (
    'std::string("shadowMap") + suffix',
    "session->shadow_map_valid[index]",
    "session->shadow_view_projections[index]",
):
    assert token in presentation

# Automatic remains backwards-compatible and uses scene/camera depth. The UI
# gives users a clear per-layer escape hatch to authored layer-list order.
assert "Automatic = 0" in model and "LayerOrder = 1" in model
assert "LayerDepthMode depth_mode = LayerDepthMode::Automatic;" in model
assert 'json_int(j, "depth_mode", static_cast<int>(LayerDepthMode::Automatic))' in serialization
assert transform.count("layer.depth_mode != LayerDepthMode::Automatic") >= 2
assert "QComboBox       *cmb_depth_mode_ = nullptr;" in properties_h
for token in (
    'fxm_tr("OBSTitles.ScenePositionZ")',
    'fxm_tr("OBSTitles.LayerOrder")',
    "static_cast<int>(LayerDepthMode::Automatic)",
    "static_cast<int>(LayerDepthMode::LayerOrder)",
):
    assert token in properties_ui
assert "static_cast<int>(layer_->depth_mode)" in properties_load
assert "layer_->depth_mode = static_cast<LayerDepthMode>" in properties_connections
assert 'OBSTitles.DepthVisibility="Z / Visibility"' in locale
assert 'OBSTitles.ScenePositionZ="Scene Position (Z)"' in locale
