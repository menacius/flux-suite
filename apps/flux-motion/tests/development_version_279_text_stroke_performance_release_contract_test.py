from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding='utf-8')

def test_metadata():
    assert 'VERSION 0.8.12' in read('CMakeLists.txt')
    assert 'OBS_FXM_DEVELOPMENT_VERSION "279"' in read('CMakeLists.txt')
    assert 'PLUGIN_VERSION "0.8.12-alpha"' in read('src/core/build-info.h')
    assert 'FXM_DEVELOPMENT_VERSION "279"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=279' in read('../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc')

def test_stroke_clip():
    s=read('src/rendering/title-gpu-text-renderer.cpp')
    assert 'Quad fill_quad = glyph_quad;' in s
    assert 'Quad stroke_quad = glyph_quad;' in s
    assert 'slice.x0 - clip_guard' in s and 'slice.x1 + clip_guard' in s
    assert 'kCoverageSamplingGuard = 4.0f' in s

def test_adaptive_performance():
    s=read('src/canvas/canvas-preview/keyboard-wheel-events.inc')
    assert 'frame_image_preview_scale_ >= 0.999' in s
    assert 'last_adaptive_render_cost_ms_ = cost_ms;' in s
    assert 'kAdaptiveEditorLowFramerateCadenceMs' not in s
    assert 'editing_cadence_ms = base_editing_cadence_ms' in s
    g=read('../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc')
    assert 'const ImmutableTextLayout layout = cached_text_layout(request);' in g

def test_sdf_performance():
    r=read('src/rendering/title-gpu-text-renderer.cpp')
    assert 'glyph_atlas_coverage_scale' in r
    assert 'atlasPixelsPerLogical : TEXCOORD5' in r
    assert 'sdfSpread * coverageScale - 2.0' in r
    assert 'estimated_extent > 512.0f' in r
    s=read('src/rendering/title-gpu-text-sdf.cpp')
    assert 'thread_local GlyphSdfWorkspace' in s

def test_paint_lookup():
    s=read('src/text/title-text-layout.cpp')
    b=s[s.index('text_layout_cluster_paint_slices'):s.index('text_layout_byte_offset_at')]
    assert 'std::upper_bound' in b and 'run_start >= cluster_end' in b

def test_docs():
    assert 'v0.8.12-alpha` · `Development Version 279' in read('README.md')
    assert 'What changed since v0.8.11-alpha Development Version 239' in read('README.md')
    assert read('docs/CHANGELOG.md').startswith('# v0.8.12-alpha — Development Version 279')
    assert not (ROOT/'docs/TEXT_SYSTEM_AUDIT_276.md').exists()
    assert not (ROOT/'docs/TEXT_SYSTEM_AUDIT_277.md').exists()
    assert not (ROOT/'docs/TEXT_LAYOUT_ALIGNMENT_AUTOSIZE_278.md').exists()
