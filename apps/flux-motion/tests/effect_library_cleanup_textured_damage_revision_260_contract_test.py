
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

def test_development_version_synchronized_to_262():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "262")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "262"' in read('src/core/build-info.h')
    schema = read('Core/title-serialization-schema.h')
    assert 'kCurrentDevelopmentVersion = 262' in schema
    for version in range(245, 263):
        assert f'case {version}:' in schema

def test_effect_library_dedupes_and_hides_animation_presets_root():
    panel = read('src/effects/effects-presets-panel.cpp')
    assert 'seen_preset_keys' in panel
    assert 'entry.category_path.value(0).compare(QStringLiteral("Animation Presets")' in panel
    root_block = re.search(r'const QStringList root_categories = \{(?P<body>.*?)\};', panel, re.S).group('body')
    assert 'Animation Presets' not in root_block

def test_damage_effects_use_packaged_artifact_textures():
    shader = read('data/effect-transitions/shaders/damage-distortion/damage-distortion.effect')
    compositor = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
    registry = read('src/rendering/title-effect-registry.cpp')
    assert 'uniform texture2d artifactTexture' in shader
    assert 'sample_artifact_texture' in shader
    for name in ['film-artifacts.png', 'analog-artifacts.png', 'digital-artifacts.png']:
        assert (ROOT / 'data/effect-transitions/textures/damage' / name).exists()
        assert name in compositor
    assert 'fxm_effect_param(gpu_effect, "artifactTexture")' in compositor
    assert 'return nullptr;' in registry and 'Development Version 260: damage effects depend on packaged artifact' in registry
