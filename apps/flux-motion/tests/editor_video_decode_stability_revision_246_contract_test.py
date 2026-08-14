#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8')

video_runtime = read('Core/title-video-runtime.cpp')
video_header = read('Core/title-video-runtime.h')
preview = read('src/canvas/canvas-preview/preview-cache-view.inc')
gpu_cache = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
compat = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
source_lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
source_header = read('../../packages/flux-common/Shared/rendering-engine/title-source.h')
readme = read('README.md')
changelog = read('docs/CHANGELOG.md')
build_info = read('src/core/build-info.h')

assert any(f'FXM_DEVELOPMENT_VERSION "{v}"' in build_info for v in range(246, 299))
assert any(f'Development Version {v}' in readme for v in range(246, 299))
assert 'Development Version 246 — Editor Video Decode Stability Fix' in changelog

# 245 regression must not remain: editor preview may not force low-quality,
# preview-sized decoding or use a decode-size-dependent cache identity.
for forbidden in [
    'bounded_decode_size',
    'requested_max_decode_width',
    'requested_max_decode_height',
    'max_decode_width',
    'max_decode_height',
]:
    assert forbidden not in video_runtime
    assert forbidden not in video_header

# Editor preview must explicitly select the Editor client, but preserve full
# raster quality by passing only the client, not a target decode size.
assert 'title_gpu_render_session_set_editor_video_decode_client' in source_header
assert 'title_gpu_render_session_set_editor_video_decode_client(\n        gpu_render_session_, true);' in preview
assert 'fxm::video::VideoDecodeClient video_decode_client' in gpu_cache
assert 'ScopedRasterVideoDecodeClient video_decode_scope(video_decode_client);' in gpu_cache
assert 'g_raster_video_decode_client' in compat
assert 'frame_for_layer(\n            layer, title_time, 1.0 / std::max(1e-6, source_frame_duration()),\n            g_raster_video_decode_client)' in compat
assert 'frame_for_layer(\n                layer, title_time, 1.0 / std::max(1e-6, source_frame_duration()),\n                video_decode_client)' in gpu_cache

# Hardware decode remains available for live output, but Auto hardware decode is
# not used for the editor QImage path where hardware frames require CPU readback.
assert 'client == VideoDecodeClient::Editor' in video_runtime
assert 'backend == HardwareDecodeBackend::Auto' in video_runtime
assert 'backend = HardwareDecodeBackend::SoftwareOnly;' in video_runtime
assert 'live output keeps the hardware Auto path' in changelog

# Editor prefetch must stay small for scrubbing, but 247 may restore a larger
# steady-playback look-ahead to fix visibly stepped decode cadence.
assert 'client == VideoDecodeClient::Editor' in video_runtime
assert ('plan.forward = moving_backward ? 0 : 1;' in video_runtime) or ('plan.forward = 1;' in video_runtime) or ('plan.forward = 2;' in video_runtime)
assert ('plan.reverse = moving_backward ? 1 : 0;' in video_runtime) or ('plan.reverse = 0;' in video_runtime) or ('plan.reverse = steady_playback ? 4 : 1;' in video_runtime)
assert 'plan.forward = moving_backward ? 2 : 6;' in video_runtime
assert 'plan.reverse = moving_backward ? 3 : 1;' in video_runtime
if 'Development Version 247' in readme or 'Development Version 248' in readme:
    assert 'steady_playback_request' in video_runtime
    assert 'plan.forward = 5;' in video_runtime or 'plan.forward = 12;' in video_runtime

# The 244 serialization audit remains intact.
assert 'Development Version 244 serialization/migration audit' in readme
assert 'migrate_serialization_audit_244' in read('Core/title-serialization-schema.h')

print('Development Version 246 editor video decode stability contract OK')
