from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_development_version_307_identity():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "307")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "307"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=307' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')


def test_render_diagnostics_category_and_public_snapshot():
    logger = read('src/core/title-logger.cpp')
    header = read('../../packages/flux-common/Shared/rendering-engine/title-source.h')
    registration = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc')
    assert 'QStringLiteral("RenderDiagnostics")' in logger
    assert 'struct TitleGpuRenderDiagnostics' in header
    assert 'title_gpu_render_session_get_diagnostics' in header
    assert 'diagnostics_update_serial' in registration
    assert 'pending_raster_count' in registration
    assert 'hardware_depth_run_count' in registration
    assert 'extrusion_pass_count' in registration


def test_transport_canvas_session_and_publication_are_correlated():
    editor = read('src/editor/title-editor/editor-events.inc')
    canvas = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    present = read('src/canvas/canvas-preview/preview-cache-view.inc')
    session_update = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    session_render = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
    assert 'stage=editor-playhead-change' in editor
    assert 'stage=canvas-playhead-request' in present
    assert 'stage=canvas-render-begin' in canvas
    assert 'stage=canvas-render-policy' in canvas
    assert 'stage=canvas-session-updated' in canvas
    assert 'stage=canvas-render-end' in canvas
    assert 'stage=canvas-stall-suspect' in canvas
    assert 'stage=canvas-present' in present
    assert 'stage=session-update-begin' in session_update
    assert 'stage=session-update-end' in session_update
    assert 'stage=session-render-begin' in session_render
    assert 'stage=session-publish' in session_render


def test_extrusion_and_depth_passes_have_diagnostics():
    presentation = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
    properties = read(
        'src/editor/properties-panel/construction-transform-character.inc')
    assert 'stage=extrusion-pass-begin' in presentation
    assert 'stage=extrusion-pass-geometry' in presentation
    assert 'stage=extrusion-pass-end' in presentation
    assert 'stage=depth-run-built' in lifecycle
    assert 'stage=depth-run-summary' in lifecycle
    assert 'stage=depth-run-render' in lifecycle
    assert 'stage=geometry-edit property=extrusion-enabled' in properties
    assert '"extrusion-depth"' in properties
    assert '"bevel-depth"' in properties
