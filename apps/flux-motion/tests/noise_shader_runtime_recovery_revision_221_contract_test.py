#!/usr/bin/env python3
"""Cross-backend Noise shader safety contract retained under the historical 221 test name."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT/p).read_text(encoding="utf-8")
shader=read("data/effect-transitions/shaders/noise/noise.effect")
registry=read("src/rendering/title-effect-registry.cpp")
manifest=json.loads(read("tests/test-suite-manifest.json"))
gpu=read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
cache=read("src/cache/cache-manager/visual-hash-keying.inc")
assert "for (" not in shader and "while (" not in shader and "normalize(" not in shader
marker='static constexpr const char *kEmbeddedNoiseEffect = R"FXMFX('
start=registry.index(marker)+len(marker); end=registry.index(')FXMFX";',start)
assert registry[start:end] == shader
assert "kEmbeddedLegacyNoiseFallbackEffect" not in registry
assert "try_legacy_noise_fallback" not in registry
assert "gpu-effects-v26-3d-lighting-materials-shadows" in gpu
assert "v41-procedural-noise-schema3-detail-core" in cache
assert "tests/noise_shader_runtime_recovery_revision_221_contract_test.py" in manifest["areas"]["rendering_2d_3d"]["python"]
print("Current Noise shader safety contract passed")
