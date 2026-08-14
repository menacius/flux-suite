from pathlib import Path

root = Path(__file__).resolve().parents[1]
registration = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc').read_text(encoding='utf-8')
runtime = (root / '../../plugins/obs/flux-motion/src/title-audio-runtime.cpp').read_text(encoding='utf-8')
model = (root / '../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')

assert 'OBS_SOURCE_AUDIO' in registration
assert 'OBS_SOURCE_CONTROLLABLE_MEDIA' in registration
for callback in ('media_play_pause', 'media_restart', 'media_stop',
                 'media_get_duration', 'media_get_time', 'media_set_time',
                 'media_get_state'):
    assert callback in registration
assert 'obs_source_output_audio' in runtime
assert 'worker_main' in runtime and 'std::thread' in runtime
assert 'decode_clip' in runtime
assert 'AUDIO_FORMAT_FLOAT_PLANAR' in runtime
assert 'FXM_HAVE_FFMPEG' in runtime
assert 'audio_runtime' in model
assert 'title-audio-runtime.cpp' in cmake
assert 'libavformat libavcodec libavutil libswresample' in cmake
