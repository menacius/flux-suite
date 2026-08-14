#!/usr/bin/env python3
"""Development Version 248 source contract: editor first-frame raster and playback decode cadence."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8')

build_info = read('src/core/build-info.h')
cmake = read('CMakeLists.txt')
readme = read('README.md')
video_cpp = read('Core/title-video-runtime.cpp')
gpu_lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
source_playback = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')

assert any(f'FXM_DEVELOPMENT_VERSION "{v}"' in build_info for v in range(248, 300))
assert any(f'OBS_FXM_DEVELOPMENT_VERSION "{v}"' in cmake for v in range(248, 300))
assert 'Development Version 248' in readme

# Editor playback must not abort prefetch for every adjacent playhead tick.
assert 'linear_playback_generation_is_safe' in video_cpp
assert 'plan.forward = 12;' in video_cpp
assert 'max_linear_drift' in video_cpp
assert 'latest_cached' in video_cpp
assert 'Seek/scrub requests still' in video_cpp

# Time-remap curves authored as 0-based source-time must work with non-zero trims/ranges.
assert 'video_time_remap_values_look_trim_relative' in video_cpp
assert 'trim_relative_source_values' in video_cpp
assert 'sample.source_time += media_in' in video_cpp
assert 'segment_source_start' in video_cpp and 'segment_source_end' in video_cpp
assert 'non-zero video_in_point' in video_cpp

# First editor frame must not publish blank optional GPU text/primitive rasters.
assert 'Development Version 248: on a fresh editor session' in gpu_lifecycle
assert 'first_editor_text_raster' in source_playback
assert 'VideoDecodeClient::Editor' in source_playback
assert 'all_required_rasters_ready = false;' in gpu_lifecycle
assert 'retained texture' in gpu_lifecycle or 'retainedTexture' in gpu_lifecycle

# Keep the 247 guard for async video initial frame.
assert 'Do not\n                 * cache an empty raster' in source_playback
assert '|video-delivered-frame=' in source_playback

print('revision 248 editor video playback decode contract ok')
