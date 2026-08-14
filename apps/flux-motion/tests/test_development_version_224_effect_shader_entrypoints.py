from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

ASSETS = {
    "kEmbeddedNoiseEffect": "data/effect-transitions/shaders/noise/noise.effect",
    "kEmbeddedDetailEffect": "data/effect-transitions/shaders/detail/detail.effect",
    "kEmbeddedGlareEffect": "data/effect-transitions/shaders/glare/glare.effect",
    "kEmbeddedHalationEffect": "data/effect-transitions/shaders/halation/halation.effect",
    "kEmbeddedFinishingEffect": "data/effect-transitions/shaders/finishing/finishing.effect",
}


def _pixel_functions(shader: str):
    return re.findall(r"float4\s+(PS\w+)\s*\(VertDataOut\s+(\w+)\)\s*:\s*TARGET", shader)


def _pixel_invocations(shader: str):
    return re.findall(r"pixel_shader\s*=\s*(PS\w+)\((\w+)\)\s*;", shader)


def test_new_effect_shaders_use_obs_compatible_pixel_entrypoints():
    for rel in ASSETS.values():
        shader = (ROOT / rel).read_text(encoding="utf-8")
        declared = dict(_pixel_functions(shader))
        invoked = _pixel_invocations(shader)
        assert declared, rel
        assert invoked, rel
        for function, argument in invoked:
            assert function in declared, (rel, function)
            assert declared[function] == "v_in", (rel, function, declared[function])
            assert argument == "v_in", (rel, function, argument)
        assert "VertDataOut i)" not in shader
        assert "i.uv" not in shader


def test_embedded_effects_are_byte_identical_to_installed_assets():
    registry = (ROOT / "src/rendering/title-effect-registry.cpp").read_text(encoding="utf-8")
    for constant, rel in ASSETS.items():
        match = re.search(
            rf"{constant}\s*=\s*R\"FXMFX\((.*?)\)FXMFX\";",
            registry,
            re.S,
        )
        assert match, constant
        assert match.group(1) == (ROOT / rel).read_text(encoding="utf-8"), rel


def test_effect_cache_abi_invalidates_noop_shader_frames():
    gpu = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc").read_text(encoding="utf-8")
    cache = (ROOT / "src/cache/cache-manager/visual-hash-keying.inc").read_text(encoding="utf-8")
    assert "gpu-effects-v26-3d-lighting-materials-shadows" in gpu
    assert "v48-3d-lighting-materials" in cache
