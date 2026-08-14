from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_and_manifest_are_294_or_newer():
    assert 'OBS_FXM_DEVELOPMENT_VERSION "299"' in read('CMakeLists.txt')
    assert 'FXM_DEVELOPMENT_VERSION "299"' in read('src/core/build-info.h')
    assert json.loads(read('tests/test-suite-manifest.json'))['development_version'] >= 294
    assert '|gpu-text-pipeline=299' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')


def test_temporal_resolve_blends_effect_color_without_alpha_union():
    gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-effects-transitions.inc')
    cpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    assert 'float4 result = lerp(currentFrame, wet, mixAmount)' in gpu
    assert 'max(sharpAlpha' not in gpu
    assert 'float4 over = sharp + trail * (1.0 - sharp.a)' not in gpu
    assert 'const double dry = 1.0 - mix;' in cpu
    assert 'current_alpha * dry + wet_alpha * mix' in cpu
    assert 'std::max(sa, ta * mix)' not in cpu


def test_transform_sampler_restores_distance_adaptive_minimum_quality():
    gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    compat = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc')
    assert 'motion_blur_quality_sample_count(' in gpu
    assert 'std::max(authored, adaptive)' in compat
    assert 'image_or_video_motion ? 96 : 24' in gpu
    assert 'image_or_video_motion ? 64 : 14' in gpu
    assert 'std::min(configured, adaptive)' not in gpu
    assert 'std::min(configured_samples, adaptive_samples)' not in compat


def test_all_procedural_effects_and_properties_are_temporal_dependencies():
    runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
    assert 'return effect_is_time_variant(effect);' in runtime
    assert 'if (effect.effect_animated)' in runtime
    for token in (
        '&effect.brightness_prop', '&effect.contrast_prop',
        '&effect.saturation_prop', '&effect.stroke_offset_prop',
        '&effect.gradient_opacity_prop', '&effect.gradient_start_color_a',
        '&effect.gradient_end_color_b'):
        assert token in runtime



def _reference_temporal_resolve(current_rgba, exposure_rgba, coverage_alpha, mix):
    mix = max(0.0, min(1.0, mix))
    dry = 1.0 - mix
    exposure_alpha = exposure_rgba[3]
    occupancy = max(0.0, min(1.0, exposure_alpha / coverage_alpha)) if coverage_alpha else 0.0
    wet_alpha = coverage_alpha * (1.0 - (1.0 - occupancy) ** 4.0)
    straight = tuple((exposure_rgba[i] / exposure_alpha) if exposure_alpha else 0.0
                     for i in range(3))
    wet = tuple(c * wet_alpha for c in straight) + (wet_alpha,)
    return tuple(current_rgba[i] * dry + wet[i] * mix for i in range(4))

def test_reference_resolve_blurs_internal_detail_and_preserves_alpha_ceiling():
    # Full overlap keeps opaque internal detail fully temporal and opaque.
    opaque = _reference_temporal_resolve(
        (1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 1.0, 1.0), 1.0, 1.0)
    assert opaque == (0.0, 0.0, 1.0, 1.0)

    # Sparse translucent shadow history remains below authored coverage.
    translucent = _reference_temporal_resolve(
        (0.16, 0.08, 0.04, 0.4), (0.01, 0.03, 0.04, 0.1), 0.4, 1.0)
    assert 0.0 < translucent[3] < 0.4

def test_motion_blur_still_wraps_complete_effect_pipeline():
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    assert 'Layer temporal_layer = layer_without_motion_blur_effects(layer);' in renderer
    assert 'render_gpu_layer_to_target(\n                    session, title, temporal_layer, sample_time' in renderer
    assert 'LayerEffectSpace::LayerSpace' in renderer
    assert 'LayerEffectSpace::ScreenSpace' in renderer
    assert 'effect_config.type == LayerEffectType::MotionBlur' in renderer
    assert '|gpu-effects-v26-3d-lighting-materials-shadows|' in renderer


def test_static_noise_and_effects_on_either_side_of_motion_blur_feed_the_exposure():
    renderer = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    # The temporal layer strips Motion Blur only; all other effects remain in
    # their authored order, including effects listed after Motion Blur.
    assert """base_layer.effects.erase(std::remove_if(base_layer.effects.begin(), base_layer.effects.end(),
                                            effect_is_motion_blur)""" in raster
    # The complete layer-space stack is resolved before the transform-only
    # exposure reuses local_texture, so static Noise is blurred with the layer.
    effect_index = renderer.index('gs_texture_t *local_texture = apply_pixel_effects')
    fast_index = renderer.index('if (use_transform_motion_fast_path', effect_index)
    assert effect_index < fast_index
    assert """draw_gpu_layer_texture(
                    session, &entry, title, layer, local_texture""" in renderer[fast_index:]
