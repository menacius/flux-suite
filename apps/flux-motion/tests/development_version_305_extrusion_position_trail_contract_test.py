from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(path): return (ROOT/path).read_text(encoding='utf-8')
def test_version_305_contract():
    assert '#define FXM_DEVELOPMENT_VERSION "305"' in read('src/core/build-info.h')
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "305")' in read('CMakeLists.txt')
def test_extrusion_extends_behind_front_face():
    src=read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'const double z = -extrusion_depth * t;' in src
    assert 'const double distance_from_front = -z;' in src
    assert 'const double distance_from_back = extrusion_depth + z;' in src
def test_animated_position_bypasses_stale_editor_cache():
    src=read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    assert 'animated_position_requires_live_canvas' in src
    assert 'candidate->position.is_animated()' in src
    assert 'candidate->position_3d.is_animated()' in src
