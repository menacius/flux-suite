#!/usr/bin/env python3
"""Development Version 226 Effects UI, presets, source effects and animation contract."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
read = lambda rel: (ROOT / rel).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
enum = read("src/effects/layer-effects.h")
runtime = read("src/effects/effect-runtime.cpp")
registry = read("src/rendering/title-effect-registry.cpp")
catalog = read("src/effects/effect-preset-catalog.cpp")
panel = read("src/effects/effects-panel.cpp")
panel_header = read("src/effects/effects-panel.h")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
layer_list = read("src/layers/layer-stack-widget.cpp")
timeline = read("src/timeline/timeline-widget.cpp")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc")
playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
cache = read("src/cache/cache-manager/visual-hash-keying.inc")
loader = read("src/core/title-data.cpp")
shader = read("data/effect-transitions/shaders/source-effects/source-effects.effect")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema and 'case 226:' in schema
assert manifest["development_version"] == 243

assert re.search(r"\bLightWrap\s*=\s*43\b", enum)
assert re.search(r"\bDisplacementMap\s*=\s*44\b", enum)
for token in (
    "effect_source_layer_id", "effect_source_mode", "effect_x_channel",
    "effect_y_channel", "effect_wrap_mode", "effect_mapping_space",
    "effect_alpha_aware",
):
    assert token in enum and token in loader
for stable_id in ("fxm.builtin.light-wrap", "fxm.builtin.displacement-map"):
    assert stable_id in runtime
assert 'LayerEffectSpace::ScreenSpace' in runtime
assert 'LayerEffectType::LightWrap' in catalog
assert 'LayerEffectType::DisplacementMap' in catalog
assert "kEmbeddedSourceEffectsEffect" in registry
assert "case LayerEffectType::LightWrap:" in registry
assert "case LayerEffectType::DisplacementMap: return kEmbeddedSourceEffectsEffect" in registry
assert "embedded-fxm-source-effects.effect" in registry
for preset_path, effect_id, category in (
    ("data/effect-transitions/Light Wrap.fxmeffect", "light-wrap", "Effects/Light and Optical"),
    ("data/effect-transitions/Displacement Map.fxmeffect", "displacement-map", "Effects/Distortion"),
):
    preset = json.loads(read(preset_path))
    assert preset["id"] == effect_id and preset["type"] == effect_id
    assert preset["category"] == category

for uniform in (
    "sourceImage", "sourceEnabled", "sourceIsComposition", "xChannel",
    "yChannel", "wrapMode", "mappingSpace", "alphaAware",
    "inputIsComposition",
):
    assert f"uniform" in shader and uniform in shader
assert "technique LightWrap" in shader and "PSLightWrap" in shader
assert "technique DisplacementMap" in shader and "PSDisplacementMap" in shader
assert "source_luma" in shader and "source_straight" in shader
assert "foreground" not in shader.lower() or "protection" in shader.lower()
assert "GpuMaskGraphPurpose::EffectSource" in gpu
assert "LayerEffectSpace::ScreenSpace" in gpu
assert "input_is_composition" in gpu
assert "Screen-space effects consume the already transformed" in gpu
assert "source_space_uv" in shader
assert "source_effect_visiting" in gpu
assert "effect_source_layer_id" in playback
assert "EffectSource" in session and "ignore_visibility" in gpu
assert "gpu-effects-v21-organic-damage-motion239" in gpu
assert "v44-source-aware-effects" in cache

for category in (
    "Blur and Sharpen", "Color Correction", "Distortion", "Generate",
    "Keying", "Light and Optical", "Noise and Grain", "Stylize",
    "Utility", "Audio", "External Plugins", "Favorites", "Recently used",
):
    assert category in panel
for feature in (
    "Search effects", "effect_browser_thumbnail", "Copy effect", "Paste effect",
    "Copy effect stack", "Save Stack as Preset", "Export Effect Stack Preset",
    "Import Effect Stack Preset", "Replace effect", "Reset Parameter",
    "Reset effect", "DuplicateEffect", "complete effect stack",
):
    assert feature in panel, feature
for badge in ("GPU", "CPU", "HDR", "PLUGIN", "SCREEN", "CACHE"):
    assert badge in panel
assert "application/x-fxm-effect-stack+json" in panel
assert ".fxmstack" in panel
assert "set_title(std::shared_ptr<Title> title)" in panel_header

assert "is_effect_group" in hierarchy and "effect_index" in hierarchy
assert "The stack is the hierarchy authority" in hierarchy
assert "timeline_properties(*layer)" in hierarchy
assert "timeline_row.is_effect_group" in layer_list
assert "entry.is_effect_group" in timeline
assert 'name_group("spill_color"' in hierarchy
for channel in ("horizontal_amount", "vertical_amount", "foreground_luminance_protection"):
    assert channel in hierarchy
assert "spin(-1000000000.0, 1000000000.0" in panel
assert "-360.0, 360.0" not in panel
assert 'OBSTitles.Evolution"), -1000000000.0, 1000000000.0' in panel
assert "evaluate(effect.evolution_prop" in runtime

editor_tests = manifest["areas"]["editor_gui"]["python"]
assert "tests/effects_ui_presets_animation_revision_226_contract_test.py" in editor_tests

print("Development Version 226 Effects UI, presets and animation contract passed")
