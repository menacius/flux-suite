from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_development_281_release_notes_are_preserved():
    changelog = read('docs/CHANGELOG.md')
    assert '# v0.8.12-alpha — Development Version 281' in changelog
    assert 'continuous static strokes and restored text transitions' in changelog


def test_static_exact_strokes_are_not_clipped_to_every_cluster_advance():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc')
    assert 'piece.clip_required = run.split_ligature || split_paint_cluster;' in source
    assert 'if (piece.clip_required)' in source
    assert 'continuous_path.addPath(stroke_path);' in source
    assert 'stroke_painter.drawPath(continuous_path);' in source
    assert 'apply_layer_trim_paths_partitioned' in source
    assert 'continuous_mask.addPath(piece.path);' in source


def test_animated_stroked_text_stays_on_unified_gpu_animator_path():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    assert 'const double local_time = std::max(0.0, title_time - layer.in_time);' in source
    assert 'max_rich_text_stroke_width(layer, local_time) > 0.0001 &&' in source
    assert '!text_animator_stack_has_enabled_animators(layer.text_animators)' in source
    assert 'overlapping rectangular unit clips' in source


def test_exact_compatibility_animator_builds_isolated_glyph_units():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc')
    layer_raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    assert 'render_exact_rich_text_transition_unit' in source
    assert 'exact_text_transition_unit_render_bounds' in source
    assert 'const ExactRichTextRaster *exact_raster = nullptr' in source
    assert '&exact_rich_text' in layer_raster
    exact_branch = layer_raster.split('if (exact_rich_text.valid)', 1)[1].split('} else {', 1)[0]
    assert 'apply_unified_text_animator_raster' in exact_branch
    assert 'apply_unified_text_animator_flattened' not in exact_branch


def test_release_notes_describe_both_regressions():
    changelog = read('docs/CHANGELOG.md')
    assert '# v0.8.12-alpha — Development Version 281' in changelog
    assert 'invisible neighbouring character box' in changelog
    assert 'transition-managed and manually animated stroked text' in changelog
