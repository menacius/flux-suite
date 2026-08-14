#!/usr/bin/env python3
"""Source contract for Development Version 220 unified effects runtime."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
runtime_h = read("src/effects/effect-runtime.h")
runtime_cpp = read("src/effects/effect-runtime.cpp")
registry_h = read("src/rendering/title-effect-registry.h")
registry_cpp = read("src/rendering/title-effect-registry.cpp")
gpu_session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
gpu_effects = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
title_source = read("../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
compat = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc")
cache = read("src/cache/cache-manager/cache-policy-invalidation.inc")
counters = read("../../packages/flux-common/Shared/performance-counters.h")
catalog = read("src/extensions/effect-extension-catalog.cpp")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert "kCurrentDevelopmentVersion = 243" in schema
assert "case 220:" in schema
assert "effect-runtime.h" in cmake and "effect-runtime.cpp" in cmake

# The split title-source implementation uses fxm::perf directly. It must not
# rely on transitive includes, because MSVC compiles the .inc files as part of
# title-source.cpp and otherwise sees only namespace fxm.
assert '#include "performance-counters.h"' in title_source
assert title_source.index('#include "performance-counters.h"') < title_source.index(
    '#include "title-source/compatibility-effects-compositor.inc"'
)

for token in (
    "struct EffectDescriptor",
    "schema_version",
    "execution_space",
    "EffectExecutionBackend",
    "EffectColorContract",
    "EffectAlphaContract",
    "minimum_render_passes",
    "supports_hdr",
    "expands_bounds",
    "cacheable_when_static",
    "struct ResolvedLayerEffect",
    "EffectDirtyScope",
    "EffectBoundsExpansion",
):
    assert token in runtime_h, token

assert runtime_cpp.count('"fxm.builtin.') >= 19
assert "resolve_layer_effect" in runtime_cpp
assert "effect_is_time_variant" in runtime_cpp
assert "effect_dirty_scope" in runtime_cpp
assert "effect_bounds_expansion" in runtime_cpp
assert "EffectParameterResolutionNanoseconds" in runtime_cpp
assert "LayerEffect resolved = effect" not in runtime_cpp

assert "std::array<gs_effect_t *, kBuiltInEffectCount>" in registry_h
assert "std::unordered_map<std::string, gs_effect_t *>" in registry_h
assert "EffectShaderCacheHits" in registry_cpp
assert "EffectShaderCacheMisses" in registry_cpp
assert "builtin_effect_descriptors()" in registry_cpp

assert "effect_pass_states" not in gpu_session
assert "effect_pass_shaders" not in gpu_session
assert "kInlineEffectPasses" in gpu_effects
assert "std::array<GpuEffectPass, kInlineEffectPasses>" in gpu_effects
assert "overflow_passes" in gpu_effects
assert "pass_count" in gpu_effects
assert "EffectEmptyStackFastPaths" in gpu_effects
assert "EffectPassNanoseconds" in gpu_effects

for token in (
    "struct CompatibilityEffectSurfacePool",
    "gs_texture_set_image",
    "EffectSurfacePoolHits",
    "EffectSurfacePoolMisses",
    "reset_compatibility_effect_surface_pool_locked",
):
    assert token in compat, token
assert "gs_texture_create" in compat and "gs_stagesurface_create" in compat
assert compat.count("gs_texture_create") == 1
assert compat.count("gs_stagesurface_create") == 1

assert "effect_is_time_variant(effect)" in cache
for token in (
    "EffectParameterResolutions",
    "EffectShaderCacheHits",
    "EffectBoundsEvaluations",
    "EffectPasses",
    "EffectSurfacePoolHits",
):
    assert token in counters, token

release_timer_prefix = counters.split("class ScopedTimer", 1)[1].split("~ScopedTimer", 1)[0]
assert "#ifndef NDEBUG" in release_timer_prefix
assert "steady_clock::now()" in release_timer_prefix
assert "#ifdef NDEBUG" in release_timer_prefix

for token in (
    "meta.schema_version",
    'QStringLiteral("executionSpace")',
    'QStringLiteral("colorContract")',
    'QStringLiteral("alphaContract")',
    "meta.parameter_count",
):
    assert token in catalog, token

assert "Development Version 243" in readme
assert changelog.startswith("# v0.8.11-alpha — Development Version 243")
assert "Unified Effects Runtime and Render Performance Baseline" in changelog

print("Development Version 220 unified effects runtime contract passed")
