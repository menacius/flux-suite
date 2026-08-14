#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_video_decoder_converts_only_selected_frame():
    src = read('Core/title-video-runtime.cpp')
    assert 'convert_current_frame' in src
    assert 'converts only the selected target frame' in src or 'convert exactly the frame that will be uploaded' in src
    assert 'SWS_FAST_BILINEAR' in src
    assert 'codec->thread_count = 0' in src
    old_hot_path = 'best = converted;\n                best_pts = pts;\n                av_frame_unref(frame);'
    assert old_hot_path not in src, 'decoder must not swscale every intermediate frame while chasing target PTS'


def test_layer_list_has_stable_visibility_and_audio_columns():
    src = read('src/layers/layer-stack-widget.cpp')
    assert 'constexpr int kLayerAudioMuteWidth = 20;' in src
    assert 'add_header("", kLayerAudioMuteWidth);' in src
    assert 'Keep switch columns permanent but visually quiet' in src
    assert 'add_empty_switch_column(kLayerAudioMuteWidth)' in src
    assert 'if (media_kinds.audio)' in src
    assert 'OBSTitles.MatteSourceHeader' in src
    assert 'add_header_icon("visibility-normal.svg"' not in src, 'header should not show switch icons anymore'


def test_3d_preview_and_bounds_invalidation_are_not_transform_only():
    src = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    assert 'selected_transform_requires_full_refresh' in src
    assert 'LayerDimensionMode::ThreeD' in src
    assert '!selected_transform_requires_full_refresh' in src
    preview = read('src/canvas/canvas-preview/preview-cache-view.inc')
    assert 'title_gpu_render_session_invalidate_presentation(gpu_render_session_, true);' in preview
    assert 'existing 3D text' in preview or '3D text/content pass' in preview
    assert 'invalidate_canvas_overlay_caches();' in preview


def test_version_229_manifest():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "243"' in read('src/core/build-info.h')
    manifest = read('tests/test-suite-manifest.json')
    assert '"development_version": 243' in manifest
    assert 'video_layer_performance_ui_3d_revision_229_contract_test.py' in manifest

if __name__ == '__main__':
    test_video_decoder_converts_only_selected_frame()
    test_layer_list_has_stable_visibility_and_audio_columns()
    test_3d_preview_and_bounds_invalidation_are_not_transform_only()
    test_version_229_manifest()
    print('video_layer_performance_ui_3d_revision_229_contract_test: PASS')
