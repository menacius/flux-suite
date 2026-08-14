from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def test_development_version_is_299():
    assert '#define FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "299")' in read('CMakeLists.txt')
    assert '|gpu-text-pipeline=299' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')


def test_qt_slots_macro_does_not_collide_with_local_array():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'std::array<EvaluatedLightSlot, 4> light_slots {};' in source
    assert not re.search(r'\bslots\s*(?:\{|\[|\.|;)', source)


def test_material_lambda_captures_properties_panel_instance():
    source = read('src/editor/properties-panel/popup-state.inc')
    assert 'auto make_material_field = [this, subtle_text_name](' in source
    assert '[three_d_controls_, subtle_text_name]' not in source


def test_large_effect_is_split_and_has_single_clip_alpha_declaration():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    start = source.index('static constexpr const char *kGpuLayerCopyEffect')
    end = source.index('static constexpr const char *kGpuShadowMapEffect')
    effect = source[start:end]
    assert ')"\nR"(float shadow_visibility' in effect
    assert effect.count('float clipAlpha = 1.0;') == 1


def test_each_raw_literal_token_stays_below_msvc_limit():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    # C2026 occurs around 16 KiB for a single compiler string token.
    tokens = re.findall(r'R"\((.*?)\)"', source, flags=re.S)
    assert tokens
    assert max(map(len, tokens)) < 16000


def test_changelog_mentions_windows_build_fixes():
    changelog = read('docs/CHANGELOG.md')
    assert changelog.startswith('# v0.8.12-alpha — Development Version 299')
    assert 'Qt\'s `slots` preprocessor macro' in changelog
    assert 'MSVC no longer raises C2026' in changelog
