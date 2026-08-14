from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def test_version_and_cache_revision_are_277():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "277")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "277"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=277' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')

def test_gpu_bakes_both_axes_into_vector_coverage_once():
    source = read('src/rendering/title-gpu-text-renderer.cpp')
    assert 'quantized_glyph_scale' in source
    assert 'glyph_transform.scale(resolved_scale_x, resolved_scale_y);' in source
    assert 'glyph_path = glyph_transform.map(glyph_path);' in source
    assert 'atlas_glyph.width) * glyph_scale_x' not in source
    assert 'atlas_glyph.height) * glyph_scale_y' not in source
    assert 'atlas entry already contains the anisotropically transformed' in source

def test_compatibility_visible_raster_uses_same_immutable_glyphs_as_selection():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-text-rendering.inc')
    assert 'struct ExactRichTextRaster' in source
    assert 'cached_text_layout(request)' in source
    assert 'text_layout_registered_raw_font' in source
    assert 'raw.pathForGlyph(glyph.glyph_id)' in source
    assert 'transform.scale(std::clamp(glyph.scale_x' in source
    assert 'std::clamp(glyph.scale_y' in source
    assert 'text_layout_cluster_paint_slices' in source

def test_normal_compatibility_layer_prefers_exact_raster_for_fill_shadow_stroke():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    assert 'exact_rich_text_raster_for_layer' in source
    assert 'draw_exact_rich_text_fill(mask_painter' in source
    assert 'draw_exact_rich_text_fill(painter' in source
    assert 'draw_exact_rich_text_strokes' in source
    assert 'if (has_rich_text && !exact_rich_text.valid)' in source

def test_animator_does_not_rebuild_qtextdocument_when_exact_raster_exists():
    source = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    start = source.index('if (use_unified_text_animator)')
    block = source[start:source.index('auto text_surface', start)]
    exact = block[block.index('if (exact_rich_text.valid)'):block.index('} else {', block.index('if (exact_rich_text.valid)'))]
    assert 'apply_unified_text_animator_flattened' in exact
    assert 'apply_unified_text_animator_raster' not in exact

def test_audit_documents_the_actual_split_renderer_root_cause():
    audit = read('docs/TEXT_SYSTEM_AUDIT_277.md')
    assert 'selection overlay consumed `ImmutableTextLayout`' in audit
    assert 'visible compatibility raster consumed `QTextDocument`' in audit
    assert 'One geometry authority' in audit
