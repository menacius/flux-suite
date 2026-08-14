from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda p: (root / p).read_text(encoding='utf-8')

compat = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
gpu_masks = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
gpu_session = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-session-lifecycle.inc')
playback = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')
canvas = read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
header = read('../../packages/flux-common/Shared/rendering-engine/title-source.h')
audio = read('../../plugins/obs/flux-motion/src/title-audio-runtime.cpp')
data = read('src/core/title-data.cpp')
props = read('src/editor/properties-panel/construction-transform-character.inc') + read('src/editor/properties-panel/selection-refresh.inc')
source_runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
source_registration = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc')

# Editor-only scene-mask placeholder preview: source/runtime matte rasters do not inherit the grid.
assert 'g_scene_mask_placeholder_preview_enabled' in compat
assert 'ScopedSceneMaskPlaceholderPreview' in compat
assert 'layer.use_as_scene_mask && g_scene_mask_placeholder_preview_enabled' in compat
assert 'bool scene_mask_placeholder_preview_enabled = false;' in gpu_masks
assert 'scene_mask_placeholder_preview = false' in gpu_masks
assert 'layer.use_as_scene_mask && scene_mask_placeholder_preview' in gpu_masks
assert 'session->scene_mask_placeholder_preview_enabled' in gpu_session
assert 'title_gpu_render_session_set_scene_mask_placeholder_preview' in header
assert 'title_gpu_render_session_set_scene_mask_placeholder_preview(\n            gpu_render_session_, true)' in canvas
assert '|scene-mask-placeholder-preview=1' in playback

# Source audio must drop streams that were removed from the editor/title immediately, not after a stale decode finishes.
assert 'requested_ids' in audio
assert 'clips_.erase(\n            std::remove_if(clips_.begin(), clips_.end(),' in audio
assert 'pending_waveforms_.erase' in audio
assert 'output_cv_.notify_all();' in audio
assert 'by_id.find(clip->spec.id) == by_id.end()' in audio

# Video layers are never valid scene-mask layers through load, sync, UI or source discovery.
assert 'l->type != LayerType::Video' in data
assert 'video->use_as_scene_mask = false;' in data
assert 'layer_->type != LayerType::Video' in props
assert 'layer->type != LayerType::Video' in source_runtime
assert 'layer->type == LayerType::Video' in source_registration
assert 'title->layers[i]->type != LayerType::Video' in source_registration
