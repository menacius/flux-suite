#!/usr/bin/env python3
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
renderer = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
counters = read("../../packages/flux-common/Shared/performance-counters.h")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
docs_index = read("docs/README.md")
audit = read("docs/VALIDATION_HISTORY.md")
manifest = json.loads(read("tests/test-suite-manifest.json"))

cmake_version = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
build_version = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 396
assert build_version and int(build_version.group(1)) >= 396
assert manifest["development_version"] >= 396
assert "Development Version 396 effects performance audit" in readme
assert "# v0.8.13-alpha — Development Version 396" in changelog
assert "VALIDATION_HISTORY.md" in docs_index

function_start = renderer.index("static gs_texture_t *apply_gpu_layer_effect_stack")
function_end = renderer.index("static gs_texture_t *gpu_transition_matte_texture", function_start)
function = renderer[function_start:function_end]
cache_return = function.index("EffectOutputCacheHits")
pass_collection = function.index("struct GpuEffectPass")
extension_parse = function.index("QJsonDocument::fromJson")
assert cache_return < pass_collection < extension_parse
assert function.count("effect_layer_cache_key(") == 1
assert "gpu_effect_is_provable_noop(resolved)" in function
assert "FxmEffectExtensionCatalog::builtInTypeForId" in function
assert "uses_builtin_semantics" in function
assert "avoiding an additional catalog lookup" in function
assert "EffectNoOpPassesSkipped" in function
assert "has_source_dependency" in function
assert "LayerEffectType::LightWrap" in function
assert "LayerEffectType::DisplacementMap" in function
assert "v42-effect-cache-preflight-noop-elision" in function
assert "gpu-effects-v26-3d-lighting-materials-shadows" in function

noop_start = renderer.index("static bool gpu_effect_is_provable_noop")
noop_end = renderer.index("static gs_texture_t *apply_gpu_layer_effect_stack", noop_start)
noop = renderer[noop_start:noop_end]
for token in (
    "Custom extension shaders own their",
    "LayerEffectType::Blur",
    "LayerEffectType::BrightnessContrast",
    "LayerEffectType::Saturation",
    "LayerEffectType::DisplacementMap",
    "one-pixel sample radius",
    "nine-tap kernel sums to 0.92",
    "Glare uses the RGB tint but not effectColor.a",
):
    assert token in noop

for token in (
    "LayerEffectType::FilmDistortion",
    "LayerEffectType::AnalogDistortion",
    "LayerEffectType::DigitalDistortion",
):
    assert token not in noop

for token in (
    "EffectOutputCacheHits",
    "EffectOutputCacheMisses",
    "EffectNoOpPassesSkipped",
    '"effect_output_cache_hits"',
    '"effect_output_cache_misses"',
    '"effect_noop_passes_skipped"',
):
    assert token in counters

# This audit must not replace or simplify the established temporal renderer.
for token in (
    "append_gpu_temporal_sample",
    "gpu_motion_blur_realtime_sample_cap",
    "use_transform_motion_fast_path",
    "layer_without_motion_blur_effects",
):
    assert token in renderer

current = "tests/development_version_396_effect_performance_regression_contract_test.py"

def contains(value):
    if isinstance(value, list):
        return current in value or any(contains(item) for item in value)
    if isinstance(value, dict):
        return any(contains(item) for item in value.values())
    return False

assert contains(manifest)
assert "Static cache hits occurred too late" in audit
assert "Neutral effects still consumed complete GPU passes" in audit
print("Development Version 396 effects performance regression contract: PASS")
