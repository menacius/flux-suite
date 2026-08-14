from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding='utf-8')

def require(path: str, *tokens: str) -> None:
    text = read(path)
    for token in tokens:
        assert token in text, f"{path}: missing {token!r}"

required_tests = [
    'tests/external_json_path_test.cpp',
    'tests/external_data_runtime_test.cpp',
    'tests/pattern_resource_cache_test.cpp',
    'tests/audio_output_scheduler_test.cpp',
    'tests/audio_transport_waveform_regression_contract_test.py',
    'tests/stinger_native_transition_contract_test.py',
    'tests/asset_runtime_contract_test.cpp',
    'tests/spatial_bezier_keyframes_contract_test.cpp',
    'tests/temporal_graph_editor_contract_test.cpp',
    'tests/serialization_migration_roundtrip_test.cpp',
    'tests/unified_serialization_contract_test.cpp',
]
for test in required_tests:
    assert (root / test).is_file(), f'missing regression test: {test}'

require('../../packages/flux-common/Shared/external-json-path.h', 'parse_json_path', 'resolve_json_path', 'token.index')
require('src/core/external-data.cpp', 'format_external_data_value', 'formatter_config')
require('src/text/pattern-resource-cache.h', 'capacity_ = 256', 'PatternCacheHits', 'PatternCacheEvictions')
require('Core/audio-output-scheduler.h',
        'AudioOutputSchedulerMode::BufferedSource',
        '160000000ULL, 80000000ULL, 20000000ULL',
        'AudioOutputSchedulerMode::RealtimeMonitor',
        '10000000ULL, 0ULL, 20000000ULL, 10000000ULL, 1',
        'timeline_seconds_to_sample')
require('../../plugins/obs/flux-motion/src/title-audio-runtime.cpp', 'output_worker_main', 'AudioOutputUnderruns',
        'AudioTimestampRepairs', 'output_worker_.join()')
require('src/core/title-data.cpp', 'stinger_transition_point_seconds',
        'set_stinger_transition_point_seconds')
require('../../plugins/obs/flux-motion/src/stinger-transition.cpp', 'SafeLiveRender', 'RequireValidProxy')
require('src/cache/cache-manager/disk-cache-storage.inc', 'Source proxy state', 'full-title visual cache')
require('Core/animation.cpp', 'std::upper_bound(', 'evaluate')
require('src/canvas/canvas-preview/spatial-bezier-keyframes.inc',
        'motion_path_canvas_samples', 'invalidate_motion_path_sample_cache')
require('Core/title-serialization-schema.h', 'development_migration_ledger',
        'kCurrentDevelopmentVersion')
require('../../packages/flux-common/Shared/performance-counters.h', 'AudioOutputBlocks',
        'AudioOutputUnderruns', 'BackgroundJobsActive')
require('docs/ARCHITECTURE_AND_BUILD.md', 'Undo/redo', 'Copy/paste',
        'Group/ungroup', 'Windows', 'Linux', 'OBS startup/shutdown',
        'Corrupt proxy cache', 'Dock layout restoration', 'External JSON',
        'Audio layer pause/resume/seek', 'Stinger scene transition', 'Save/reopen')

runtime = read('../../plugins/obs/flux-motion/src/title-audio-runtime.cpp')
pump = runtime.split('void SourceAudioRuntime::pump()', 1)[1].split(
    'int64_t SourceAudioRuntime::duration_ms()', 1)[0]
assert 'mix_block(' not in pump, 'video_tick pump must never mix audio'
assert 'decode_clip(' not in pump, 'video_tick pump must never decode audio'

provider = read('src/core/external-data-provider.cpp')
assert 'QThread thread' in provider and 'Qt::QueuedConnection' in provider
assert 'obs_enter_graphics' not in provider

visual_hash = read('src/cache/cache-manager/disk-cache-storage.inc')
content_hash = visual_hash.split('QString CacheManager::contentHash(', 1)[1].split(
    'QString CacheManager::evaluatedVisualStateHash', 1)[0]
assert 'proxy_metadata' not in content_hash

print('Development Step 100 regression contract: OK')
