#!/usr/bin/env python3
"""Regression contract for the Development Version 220 startup effect crash."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


title_source = read("../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
gpu_session = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
gpu_effects = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
registry_h = read("src/rendering/title-effect-registry.h")
registry_cpp = read("src/rendering/title-effect-registry.cpp")
changelog = read("docs/CHANGELOG.md")

# Every split title-source module must use the null-safe OBS effect lookup.
assert "static inline gs_eparam_t *fxm_effect_param" in title_source
assert "return effect && name ? gs_effect_get_param_by_name(effect, name) : nullptr;" in title_source
for path in (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source").glob("*.inc"):
    text = path.read_text(encoding="utf-8")
    assert "gs_effect_get_param_by_name" not in text, path.name

# Effect pass scratch must be invocation-local and re-entrant. Session-owned
# mutable vectors can be cleared by nested mask/group/scene-mask rendering.
assert "effect_pass_states" not in gpu_session
assert "effect_pass_shaders" not in gpu_session
assert "struct GpuEffectPass" in gpu_effects
assert "kInlineEffectPasses" in gpu_effects
assert "std::array<GpuEffectPass, kInlineEffectPasses>" in gpu_effects
assert "overflow_passes" in gpu_effects
assert "GpuEffectPass &pass = pass_at(index);" in gpu_effects
assert "if (!pass_effect)" in gpu_effects

# Registry creation/cache mutation is serialized without deadlocking the
# stable-id overload when it delegates to the built-in overload.
assert "std::recursive_mutex mutex_" in registry_h
assert registry_cpp.count("std::lock_guard<std::recursive_mutex> lock(mutex_);") >= 3

assert "Startup crash correction" in changelog
print("Development Version 220 startup effect lifetime regression contract passed")
