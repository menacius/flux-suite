from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_trim_paths_remains_a_stable_builtin_effect():
    model = read('src/effects/layer-effects.h')
    runtime = read('src/effects/effect-runtime.cpp')
    catalog = read('src/effects/effect-preset-catalog.cpp')
    assert 'TrimPaths = 49' in model
    for field in ('effect_trim_start', 'effect_trim_end', 'effect_trim_offset',
                  'effect_trim_multiple_shapes'):
        assert field in model
        assert field in runtime
    assert 'fxm.builtin.trim-paths' in runtime
    assert 'EffectExecutionBackend::Cpu' in runtime
    assert 'case LayerEffectType::TrimPaths: return QStringLiteral("trim-paths")' in catalog


def test_geometry_stage_trims_by_measured_path_length():
    header = read('src/layers/stroke-path-geometry.h')
    source = read('src/layers/stroke-path-geometry.cpp')
    assert 'apply_trim_paths_geometry' in header
    assert 'flatten_cubic' in source
    assert 'polyline_length' in source
    assert 'normalized_intervals' in source
    assert 'append_polyline_range' in source
    assert 'apply_trim_paths_geometry_partitioned' in header
    assert 'flatten_sources' in source


def test_renderer_uses_generated_stroke_geometry_and_preserves_fill_geometry():
    properties = read('../../packages/flux-common/Shared/rendering-engine/title-source/scene-masks-properties.inc')
    raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    rich = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc')
    assert 'apply_layer_trim_paths' in properties
    assert 'stroke_shape_path = apply_layer_trim_paths' in raster
    assert 'cairo_append_qpainter_path(cr, stroke_shape_path, false)' in raster
    assert 'cairo_append_qpainter_path(cr, shape_path, true)' in raster
    assert 'apply_layer_trim_paths(layer, t, text_path)' in raster
    assert 'apply_layer_trim_paths_partitioned(layer, local_time' in rich
    assert 'continuous_path.addPath(stroke_path);' in rich
    assert 'exact_rich_text_stroke_paths' in rich
    compositor = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
    assert 'apply_layer_trim_paths(layer, t, background_path)' in compositor


def test_trim_paths_ui_and_serialization_remain_wired():
    ui = read('src/effects/effects-panel.cpp')
    data = read('src/core/title-data.cpp')
    for label in ('Start', 'End', 'Trim Offset', 'Trim Multiple Shapes'):
        assert label in ui
    for key in ('effect_trim_start', 'effect_trim_end', 'effect_trim_offset',
                'effect_trim_multiple_shapes', 'trim_start_prop',
                'trim_end_prop', 'trim_offset_prop'):
        assert key in data


def test_gpu_paths_skip_geometry_modifiers():
    gpu = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc')
    compat = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-effects-compositor.inc')
    eligibility = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    assert 'effect_config.type == LayerEffectType::TrimPaths' in gpu
    assert 'effect.type == LayerEffectType::TrimPaths' in compat
    assert 'layer_has_stroke_geometry_modifiers(layer, local_time)' in eligibility
