from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
read = lambda rel: (root / rel).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
presentation = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
lifecycle = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
registration = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc')
canvas_h = read('src/canvas/canvas-preview.h')
canvas_overlay = read('src/canvas/canvas-preview/canvas-overlay-paint.inc')
canvas_paint = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
text_header = read('src/rendering/title-gpu-text-renderer.h')
text_impl = read('src/rendering/title-gpu-text-renderer.cpp')
registry_h = read('src/rendering/title-effect-registry.h')
registry_cpp = read('src/rendering/title-effect-registry.cpp')
cache_abi = read('src/cache/cache-manager/visual-hash-keying.inc')

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION \"(\d+)\"', cmake).group(1)) >= 388
assert int(re.search(r'FXM_DEVELOPMENT_VERSION \"(\d+)\"', build).group(1)) >= 388

for token in [
    'ShaderCompileJob',
    'shader_compile_thread',
    'shader_compile_cv',
    'queue_shader_compile_job',
    'shader_compile_worker_loop',
    'obs_enter_graphics();',
    'obs_leave_graphics();',
    'ensure_session_shader_effect',
    'session_registry_effect_or_queue',
    'mark_shader_compile_pending',
]:
    assert token in source

for token in [
    '"core:frame-blit"',
    '"core:primitive-shape"',
    '"core:gpu-text"',
    '"Blur effect"',
]:
    assert token in source

for token in [
    '"core:adjustment-coverage"',
    '"core:adjustment-mix"',
    '"core:layer-copy"',
    '"core:temporal-composite"',
    '"core:mask"',
    '"core:external-background"',
    'session_registry_effect_or_queue(',
]:
    assert token in presentation

for token in ['"core:frame-blend"', '"core:shadow-map"']:
    assert token in lifecycle

assert 'title_gpu_render_session_shader_compile_status' in registration
assert '"core:scene-mask"' in registration
assert 'Scene mask matte' in registration
assert 'struct TitleGpuShaderCompileStatus' in read('../../packages/flux-common/Shared/rendering-engine/title-source.h')
assert 'stop_shader_compile_worker(session);' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')

assert 'draw_shader_compile_overlay' in canvas_h
assert 'Compiling Shaders %1 of %2' in canvas_overlay
assert 'gpu_shader_compile_overlay_key_' in canvas_paint
assert 'QTimer::singleShot(display_refresh_interval_ms_' in canvas_paint
assert 'Shader compile pending:' in read('src/canvas/canvas-preview/preview-cache-view.inc')

assert 'bool compile_effect();' in text_header
assert 'bool effect_ready() const;' in text_header
assert 'Renderer::compile_effect()' in text_impl
assert 'Renderer::effect_ready() const' in text_impl

assert 'gs_effect_t *find(LayerEffectType type) const;' in registry_h
assert 'TitleEffectRegistry::find(LayerEffectType type) const' in registry_cpp
assert 'v40-async-shader-compile-progress' in presentation
assert 'v67-async-shader-compile-progress' in cache_abi

# Guard the critical path: these session-local shaders must no longer be compiled inline.
for shader_name in [
    'obs-fxm-gpu-frame-blit.effect',
    'obs-fxm-gpu-layer-copy.effect',
    'obs-fxm-gpu-shadow-map.effect',
    'obs-fxm-gpu-frame-blend.effect',
    'obs-fxm-gpu-mask.effect',
]:
    assert shader_name in (source + presentation + lifecycle)

print('Development Version 388 async shader compile progress contract: PASS')
