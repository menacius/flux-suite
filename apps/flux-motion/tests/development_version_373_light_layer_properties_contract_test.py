from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))
model = read("Core/layer-model.h")
data = read("src/core/title-data.cpp")
properties_h = read("src/editor/properties-panel.h")
properties_ui = read("src/editor/properties-panel/popup-state.inc")
properties_load = read("src/editor/properties-panel/selection-refresh.inc")
properties_sync = read("src/editor/properties-panel/property-synchronization.inc")
title_properties_h = read("src/editor/title-properties-panel.h")
title_properties = read("src/editor/title-properties-panel.cpp")
shader = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc")
renderer = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
shadow = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
canvas = read("src/canvas/canvas-preview/editor-3d-tools.inc")
timeline = read("src/editor/title-editor-internal/hierarchy-model.inc")
cache_policy = read("src/cache/cache-manager/cache-policy-invalidation.inc")
cache_storage = read("src/cache/cache-manager/disk-cache-storage.inc")
locale = read("data/locale/en-US.ini")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 373
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 373
assert manifest["development_version"] >= 373
assert (
    "tests/development_version_373_light_layer_properties_contract_test.py"
    in manifest["areas"]["editor_gui"]["python"]
)

# Source size is a canonical animated light property with the current finite
# emitter default, bounded JSON persistence and animation/cache participation.
for token in (
    'AnimatedProperty source_size { "light_source_size", 15.0 };',
    "l.source_size.is_animated()",
):
    assert token in model
for token in (
    '{"source_size", aprop_to_json(light.source_size)},',
    'read_prop("source_size", light.source_size, 0.1, 100000.0);',
):
    assert token in data
assert "add_anim(light.source_size, 0.05);" in cache_policy
assert cache_storage.count("add_anim(light.source_size)") >= 3

# The selected Light layer owns its inspector. It exposes the complete useful
# light surface and conditionally presents controls by light type.
for token in (
    "QWidget         *light_options_box_ = nullptr;",
    "QPushButton     *btn_light_color_ = nullptr;",
    "QDoubleSpinBox  *spn_light_intensity_ = nullptr;",
    "QDoubleSpinBox  *spn_light_source_size_ = nullptr;",
    "QComboBox       *cmb_light_falloff_ = nullptr;",
    "QDoubleSpinBox  *spn_light_cone_angle_ = nullptr;",
    "QCheckBox       *chk_light_casts_shadows_ = nullptr;",
):
    assert token in properties_h
for token in (
    'fxm_add_panel_section(vl, light_options_box_,',
    'fxm_tr("OBSTitles.LightProperties")',
    "&TitleLight::intensity",
    "&TitleLight::source_size",
    "&TitleLight::falloff_distance",
    "&TitleLight::cone_angle",
    "&TitleLight::cone_feather",
    "&TitleLight::shadow_darkness",
    "&TitleLight::shadow_softness",
    "&TitleLight::shadow_bias",
    "set_animated_value(layer_->light.target, playhead_,",
    "QColorDialog::DontUseNativeDialog",
):
    assert token in properties_ui
for token in (
    "const bool distance_falloff",
    "const bool finite_source",
    "const bool spot",
    "const bool targeted",
    "const bool shadow_capable",
    "set_form_row_visible(spn_light_source_size_, finite_source);",
    "set_form_row_visible(row_light_target_, targeted);",
):
    assert token in properties_load
assert "light.source_size.evaluate(playhead_)" in properties_sync

# The title-wide compatibility inspector exposes the same new source-size
# property, avoiding two divergent editing paths.
assert "QDoubleSpinBox *spn_light_source_size_ = nullptr;" in title_properties_h
for token in (
    'add_light_row(QStringLiteral("Source Size"), spn_light_source_size_);',
    "connect_light_property(spn_light_source_size_, &TitleLight::source_size);",
    "light->source_size.evaluate(playhead_)",
):
    assert token in title_properties

# Source size reaches the live GPU light slots, broadens finite-source shading,
# contributes to shadow softness and is represented by the canvas helper.
for index in range(4):
    assert f"uniform float lightSourceSize{index};" in shader
    assert f"lightSourceSize{index}, lightFalloff{index}" in shader
for token in (
    "float sourceAngular",
    "smoothstep(-sourceAngular, sourceAngular, rawNDotL)",
    "effectiveRoughness = clamp(roughness + sourceAngular",
):
    assert token in shader
for token in (
    "float source_size = 0.0f;",
    "light.source_size.evaluate(title_time)",
    'std::string("lightSourceSize") + suffix',
    "gs_effect_set_float(source_size, slot.source_size);",
):
    assert token in renderer
assert "float physicalPenumbraAngular = sourceRadius *" in shader
assert "authored_source_size * view_scale" in canvas

# Light animation rows include source size for layer-owned and migrated
# title-owned lights.
assert timeline.count("{&light.source_size, nullptr}") >= 2
assert 'if (name == "light_source_size")' in timeline

for key in (
    "OBSTitles.LightProperties=",
    "OBSTitles.LightColor=",
    "OBSTitles.LightBrightness=",
    "OBSTitles.LightSourceSize=",
    "OBSTitles.LightFalloff=",
    "OBSTitles.LightConeAngle=",
    "OBSTitles.LightTarget=",
    "OBSTitles.LightCastsShadows=",
):
    assert key in locale

print("Development Version 373 light layer properties contract: PASS")
