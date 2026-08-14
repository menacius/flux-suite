from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

transition = read("../../plugins/obs/flux-motion/src/stinger-transition.cpp")
cache_h = read("src/cache/cache-manager.h")
cache_cpp = read("src/cache/cache-manager/cache-policy-invalidation.inc")
plugin = read("../../plugins/obs/flux-motion/src/plugin-main.cpp")
locale = read("data/locale/en-US.ini")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")

# Native OBS transition registration and A/B composition.  A scene source or
# frontend scene mutation would be an overlay simulation, not a transition.
for token in (
    'info.type = OBS_SOURCE_TYPE_TRANSITION',
    'obs_transition_enable_fixed',
    'obs_transition_get_time',
    'obs_transition_video_render_direct',
    'obs_transition_audio_render',
    'info.transition_start = stinger_transition_start',
    'info.transition_stop = stinger_transition_stop',
    'stinger_transition_register();',
):
    assert token in transition or token in plugin, token
assert 'obs_frontend_set_current_scene' not in transition
assert 'obs_scene_create' not in transition
assert 'scene-source overlay' in transition

# Exactly one visual switch per transition run and deterministic reset.
for token in (
    'bool scene_switched = false',
    'update_scene_switch_latch',
    'if (!data || data->scene_switched',
    'data->scene_switched = true',
    'data->scene_switched = false',
    '++data->transition_run',
):
    assert token in transition, token

# Progress drives pre-roll, title playback synchronisation and post-roll hold.
for token in (
    'data->total_duration',
    'data->pre_roll',
    'data->post_roll',
    'obs_source_media_get_time',
    'obs_source_media_set_time',
    'hold_child_last_frame',
):
    assert token in transition, token

# Four requested audio behaviours plus an independently configurable point.
for token in (
    'StingerAudioOnly',
    'CrossfadeSceneAudio',
    'CutSceneAudio',
    'CustomTransitionCurve',
    'audio_transition_point',
    'custom_curve_progress',
    'custom_scene_audio_blend',
    'scene_mix_a',
    'scene_mix_b',
):
    assert token in transition, token

# Proxy preference uses current content-addressed cache entries.  Missing or
# stale proxies can either render live safely or block the overlay explicitly.
for token in (
    'frameReadyForPlayback',
    'inspect_proxy',
    'SafeLiveRender',
    'RequireValidProxy',
    'queueWholeTimeline',
    'runtime_graphic_allowed',
):
    assert token in transition, token
assert 'frameReadyForPlayback' in cache_h
for token in (
    'contentHash(*title)',
    'FrameCacheState::Stale',
    'title_gpu_frame_cache_contains',
    'ram_cache_.contains',
    'disk_cache_.contains',
):
    assert token in cache_cpp, token

# Alpha and child audio are mixed over the native transition result.
for token in (
    'data->alpha_output',
    'obs_source_video_render(data->graphic_source)',
    'obs_source_get_audio_mix',
    'child_audio.output',
):
    assert token in transition, token

# The properties panel exposes document selection, audio/proxy controls,
# validation and an explicit hint for OBS's native Preview Transition button.
for key in (
    'OBSTitles.StingerAudioBehavior=',
    'OBSTitles.StingerAudioOnly=',
    'OBSTitles.StingerAudioCrossfade=',
    'OBSTitles.StingerAudioCut=',
    'OBSTitles.StingerAudioCustom=',
    'OBSTitles.StingerAudioTransitionPoint=',
    'OBSTitles.StingerProxyFallback=',
    'OBSTitles.StingerProxySafeLive=',
    'OBSTitles.StingerProxyRequire=',
    'OBSTitles.StingerNativePreviewHint=',
):
    assert key in locale, key
assert 'OBS_TEXT_INFO' in transition
assert 'OBS_PROPERTIES_DEFER_UPDATE' in transition

assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info
print("Native FXM Stinger transition contract passed")
