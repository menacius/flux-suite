"""Development Version 231 Video frame-accurate playback and cue audit contract."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8')


def test_version_230_manifest():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "243"' in read('src/core/build-info.h')
    schema = read('Core/title-serialization-schema.h')
    assert 'kCurrentDevelopmentVersion = 243' in schema
    assert 'case 230:' in schema and 'deterministic timeline-frame mapper' in schema
    assert '"development_version": 243' in read('tests/test-suite-manifest.json')


def test_video_frame_mapping_uses_project_timeline_frames():
    runtime = read('Core/title-video-runtime.cpp')
    header = read('Core/title-video-runtime.h')
    assert 'project_frame_rate_or_default' in runtime
    assert 'FrameRequest' in runtime
    assert 'frame_request_for_layer' in runtime
    assert 'timeline frame N selects floor(N * media_fps / project_fps)' in runtime
    assert 'timeline_frame_number = static_cast<qint64>(std::llround' in runtime
    assert 'media_offset_frames = static_cast<qint64>(std::floor' in runtime
    assert 'layer.video_loop' in runtime and 'span_frames' in runtime
    assert 'frame_for_layer(const Layer &layer, double title_time,' in header
    assert 'frame_cache_key_for_layer(const Layer &layer, double title_time,' in header


def test_video_render_paths_pass_project_fps():
    raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    playback = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
    for text in (raster, gpu, playback):
        assert '1.0 / std::max(1e-6, source_frame_duration())' in text
    assert 'video-frame-map=timeline:' in read('Core/title-video-runtime.cpp')


def test_yellow_uncue_does_not_restart_title():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    assert 'finalize_uncue_without_restart' in source
    assert 'cue-finalized-show-nothing' in source
    assert 'cue-finalized-hold-frame' in source
    assert 'would reset\n                 * playhead to 0 and replay the title' in source
    assert 'data->playing = false;' in source
    assert 'data->seen_cue_revision = title->cue_revision;' in source


def test_readme_and_changelog_document_audit():
    readme = read('README.md')
    changelog = read('docs/CHANGELOG.md')
    assert 'Development Version 243' in readme
    assert 'different FPS is handled through deterministic source-frame duplicate/drop mapping' in readme
    assert changelog.startswith('# v0.8.11-alpha — Development Version 243')
    assert 'Frame-Accurate Video Playback Audit' in changelog


if __name__ == '__main__':
    test_version_230_manifest()
    test_video_frame_mapping_uses_project_timeline_frames()
    test_video_render_paths_pass_project_fps()
    test_yellow_uncue_does_not_restart_title()
    test_readme_and_changelog_document_audit()
    print('video_frame_accurate_playback_audit_revision_230_contract_test: PASS')
