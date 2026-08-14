from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
manifest = json.loads(read('tests/test-suite-manifest.json'))
version_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
assert version_match and int(version_match.group(1)) >= 389
build_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert build_match and int(build_match.group(1)) >= 389
assert manifest['development_version'] >= 389

canvas_overlay = read('src/canvas/canvas-preview/canvas-overlay-paint.inc')
canvas_paint = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
gpu_display = read('src/canvas/canvas-preview/preview-cache-view.inc')
assert 'Compiling Shaders %1 of %2' in canvas_overlay
assert 'shader_compile_screen_active' in gpu_display
assert 'if (state.shader_compile_screen_active)' in gpu_display
assert 'draw_shader_compile_overlay(painter, QRectF(rect()))' in canvas_paint
assert 'paint_canvas(painter, CanvasPaintPass::Underlay)' in canvas_paint
assert 'shader-compile|%1|%2x%3|%4|%5' in canvas_paint
assert 'During shader compilation the canvas itself is intentionally hidden' in canvas_paint
assert 'gpu_display_->shader_compile_screen_active = shader_compile_visible;' in canvas_paint

prefs_h = read('src/core/title-preferences.h')
prefs_cpp = read('src/core/title-preferences.cpp')
advanced = read('src/editor/title-editor/signal-handlers.inc')
shadow_render = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
cache_policy = read('src/cache/cache-manager/cache-policy-invalidation.inc')
cache_storage = read('src/cache/cache-manager/disk-cache-storage.inc')
shadow_session_state = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
assert 'int shadow_map_size_px();' in prefs_h
assert 'void set_shadow_map_size_px(int pixels);' in prefs_h
assert 'kShadowMapSizePxKey = "shadowMapSizePx"' in prefs_cpp
assert 'normalize_shadow_map_size_px' in prefs_cpp
assert 'TitlePreferences::shadow_map_size_px()' in advanced
assert 'TitlePreferences::set_shadow_map_size_px(pixels)' in advanced
assert 'Shadowmap Size' in advanced
assert 'TitlePreferences::shadow_map_size_px()' in shadow_render
assert 'authored_shadow_map_size' in shadow_render
assert 'requested_shadow_map_size' in shadow_render
assert 'session->shadow_map_preference_size != requested_shadow_map_size' in shadow_render
assert 'session->shadow_target_unavailable[index] = false;' in shadow_render
assert 'session->shadow_map_width[shadow_slot] = point_shadow\n        ? map_size * 3u : map_size;' in shadow_render
assert 'TitlePreferences::shadow_map_size_px()' in cache_policy
assert cache_storage.count('TitlePreferences::shadow_map_size_px()') >= 2

layer_model = read('Core/layer-model.h')
properties = read('src/editor/properties-panel/popup-state.inc')
title_properties = read('src/editor/title-properties-panel.cpp')
selection_refresh = read('src/editor/properties-panel/selection-refresh.inc')
prop_sync = read('src/editor/properties-panel/property-synchronization.inc')
hierarchy = read('src/editor/title-editor-internal/hierarchy-model.inc')
presentation = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
assert 'AnimatedProperty source_size { "light_source_size", 15.0 };' in layer_model
assert 'AnimatedProperty falloff_distance { "light_falloff_distance", 450.0 };' in layer_model
assert 'Runtime\n     * falloff start is now derived from source_size' in layer_model
assert 'add_form_row(light_form, fxm_tr("OBSTitles.LightFalloffStart")' not in properties
assert 'add_light_row(QStringLiteral("Falloff Start")' not in title_properties
assert 'set_form_row_visible(spn_light_falloff_start_' not in selection_refresh
assert 'set_double(spn_light_falloff_start_' not in prop_sync
assert '{&light.falloff_start, nullptr}' not in hierarchy
assert 'if (name == "light_falloff_start")' not in hierarchy
assert 'slot.falloff_start = slot.source_size;' in presentation

print('Development Version 389 shader screen, shadowmap preference and light falloff contract: PASS')
