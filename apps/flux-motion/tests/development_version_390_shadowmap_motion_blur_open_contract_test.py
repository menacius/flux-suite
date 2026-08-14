from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
manifest = json.loads(read('tests/test-suite-manifest.json'))
assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 390
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 390
assert manifest['development_version'] >= 390

prefs_cpp = read('src/core/title-preferences.cpp')
advanced = read('src/editor/title-editor/signal-handlers.inc')
shadow_render = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
assert 'constexpr std::array<int, 5> kAllowedSizes {{256, 512, 1024, 2048, 4096}};' in prefs_cpp
assert 'settings.value(QString::fromUtf8(kShadowMapSizePxKey), 2048).toInt()' in prefs_cpp
assert 'settings.setValue(QString::fromUtf8(kShadowMapSizePxKey),' in prefs_cpp
assert 'settings.sync();' in prefs_cpp[prefs_cpp.index('void set_shadow_map_size_px'):prefs_cpp.index('QString cache_disk_location')]
assert 'for (int size : {256, 512, 1024, 2048, 4096})' in advanced
assert 'shadow_map_size->setCurrentIndex(shadow_map_index >= 0 ? shadow_map_index : 3);' in advanced
assert 'saved permanently for future sessions' in advanced
assert shadow_render.count('std::clamp(TitlePreferences::shadow_map_size_px(), 256, 4096)') >= 2
assert 'shadow_map_preference_size != requested_shadow_map_size' in shadow_render

source_cpp = read('../../packages/flux-common/Shared/rendering-engine/title-source.cpp')
gpu_cache = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
presentation = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
assert 'fxm_create_motion_blur_sample_target()' in source_cpp
assert 'fxm_create_motion_blur_accumulation_target()' in source_cpp
assert 'gs_texrender_create(GS_RGBA16F, GS_ZS_NONE)' in source_cpp
assert 'if (!target)\n        target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);' in source_cpp
assert '!create_motion_sample_target(session->motion_sample_target)' in gpu_cache
assert '!create_motion_precision_target(session->motion_accum_target)' in gpu_cache
assert '!create_motion_precision_target(session->motion_coverage_target)' in gpu_cache
assert presentation.count('fxm_create_motion_blur_accumulation_target()') >= 4
assert presentation.count('fxm_create_motion_blur_sample_target()') >= 2

assert 'entry.gpu_primitive = true;' in gpu_cache[gpu_cache.index('core:primitive-shape'):gpu_cache.index('if (!session->primitive_shape_effect)')]
assert 'session->last_error = "Shader compile pending: GPU primitive shape";' in gpu_cache
playback = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
assert 'const bool entry_has_resolvable_raster =' in playback
assert 'entry.texture || !entry.pending_image.isNull() ||\n            entry.gpu_text || entry.gpu_primitive;' in playback
assert '!entry.key.empty() && entry.key == key &&\n            entry_has_resolvable_raster' in playback
assert 'if (entry.key == key && entry_has_resolvable_raster)' in playback

print('Development Version 390 shadowmap, motion blur precision and editor-open raster recovery contract: PASS')
