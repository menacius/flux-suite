#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
video_runtime_h = (ROOT / 'Core/title-video-runtime.h').read_text(encoding='utf-8')
video_runtime_cpp = (ROOT / 'Core/title-video-runtime.cpp').read_text(encoding='utf-8')
gpu_primitives = (ROOT / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc').read_text(encoding='utf-8')
gpu_masks = (ROOT / '../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc').read_text(encoding='utf-8')
source_playback = (ROOT / '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc').read_text(encoding='utf-8')

assert 'qint64 requested_frame_number = -1;' in video_runtime_h
assert 'bool exact_requested_frame = false;' in video_runtime_h
assert 'steady_playback_request' in video_runtime_cpp
assert 'prefetch_plan_for_client(client, requested_reverse, steady_playback_request)' in video_runtime_cpp
assert 'plan.forward = 5;' in video_runtime_cpp or 'plan.forward = 12;' in video_runtime_cpp
assert 'result.exact_requested_frame = result.frame_number == requested_frame;' in video_runtime_cpp

assert 'bool video_exact_frame = true;' in gpu_primitives
assert 'qint64 video_requested_frame_number = -1;' in gpu_primitives
assert 'result.video_exact_frame = frame.exact_requested_frame;' in gpu_masks
assert 'result.video_requested_frame_number = frame.requested_frame_number;' in gpu_masks

assert 'Do not\n                 * cache an empty raster' in source_playback
assert 'entry.key.clear();' in source_playback
assert '|video-delivered-frame=' in source_playback
assert '|video-requested-frame=' in source_playback
assert '|video-exact=' in source_playback
print('revision 247 editor video decode cadence contract ok')
