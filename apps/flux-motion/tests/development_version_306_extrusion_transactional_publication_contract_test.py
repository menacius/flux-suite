from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_and_gpu_text_identity():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "306")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "306"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=306' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')


def test_extruded_text_uses_stable_compatibility_raster():
    src = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    assert 'if (layer.geometry_extrusion_enabled)' in src
    assert 'gpu_text_fallback_image' in src
    assert 'activate_gpu_text_cpu_fallback' in src
    assert 'immediateCpuFallback=%3' in src


def test_single_extruded_layer_gets_hardware_depth_pass():
    src = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
    assert 'gpu_layer_requires_self_depth_geometry' in src
    assert src.count('self_depth_geometry_count') >= 6
    assert src.count('needs_solo_geometry_pass') >= 4
    assert 'if (!run.layers.empty() &&' in src
    geometry = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'front_face_scale' in geometry
    assert 'shrinking only the face' in geometry


def test_deferred_publication_is_visible_to_canvas_recovery():
    session = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
    model = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    header = read('../../packages/flux-common/Shared/rendering-engine/title-source.h')
    canvas = read('src/canvas/canvas-preview/preview-cache-view.inc')
    assert 'last_draw_deferred = true' in session
    assert 'bool last_draw_deferred = false;' in model
    assert 'title_gpu_render_session_last_draw_deferred' in header
    assert 'draw_deferred' in canvas
    assert 'full_gpu_model_refresh_pending_ = true' in canvas
    assert 'stage=optional-raster-defer' in session
    assert 'canvas->refresh_preview();' in canvas
