from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')


def test_scene_mask_eligibility_is_centralized_and_visual_layer_wide():
    model = read('Core/layer-model.h')
    assert 'inline bool layer_type_can_be_scene_mask(LayerType type)' in model
    assert 'type != LayerType::Audio && type != LayerType::Adjustment' in model

    title_data = read('src/core/title-data.cpp')
    assert 'layer_type_can_be_scene_mask(l->type)' in title_data
    assert 'use_as_scene_mask", false) &&\n                           l->type != LayerType::Video' not in title_data
    assert 'layer->use_as_scene_mask && layer->type != LayerType::Video && layer->type != LayerType::Image' not in title_data

    ui = read('src/editor/properties-panel/property-synchronization.inc')
    assert 'const bool show_scene_mask = layer_type_can_be_scene_mask(layer_->type);' in ui
    assert 'layer_->type != LayerType::Image && layer_->type != LayerType::Video' not in ui


def test_obs_scene_mask_runtime_accepts_visual_layers_and_updates_video_masks():
    source_runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
    assert 'layer_type_can_be_scene_mask(layer->type)' in source_runtime
    assert 'layer && layer->use_as_scene_mask && layer->type != LayerType::Video' not in source_runtime

    source_registration = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc')
    assert '!layer_type_can_be_scene_mask(layer->type)' in source_registration
    assert 'layer_type_can_be_scene_mask(title.layers[next]->type)' in source_registration
    assert 'layer_type_can_be_scene_mask(title->layers[i]->type)' in source_registration
    assert 'frame_cache_key_for_layer(\n                    *layer, layer_time,' in source_registration
    assert 'layer->use_as_scene_mask || layer->type == LayerType::Video' not in source_registration


def test_editor_scene_mask_placeholder_uses_layer_alpha_silhouette():
    raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    assert 'render_scene_mask_placeholder_from_layer_alpha' in raster
    assert 'silhouette_layer.use_as_scene_mask = false;' in raster
    assert 'render_layer_unmasked_raw(cr, title, silhouette_layer, title_time,' in raster
    assert 'cairo_mask(cr, silhouette);' in raster
    assert 'render_scene_mask_placeholder(cr, title, layer, title_time,' in raster
    assert 'A rectangular placeholder made text masks look valid' in raster
