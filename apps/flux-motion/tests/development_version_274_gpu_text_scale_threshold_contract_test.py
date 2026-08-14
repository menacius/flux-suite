from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_development_version_274_metadata():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    assert cmake_match and int(cmake_match.group(1)) >= 274
    assert header_match and int(header_match.group(1)) >= 274


def test_transition_managed_text_no_longer_switches_renderer():
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    assert "text_animator_stack_has_managed_transition" not in routing
    assert "layer.type == LayerType::Text" in routing
    assert "layer.type == LayerType::Clock" in routing
    assert "layer.type == LayerType::Ticker" in routing


def test_effective_scale_stays_per_glyph_through_gpu_quad():
    layout = read("src/text/title-text-layout-qt.cpp")
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "glyph_format.scale_x" in layout
    assert "glyph_format.scale_y" in layout
    assert "apply_line_horizontal_character_scale" in layout
    assert "normalize_line_vertical_scale" in layout
    assert "(atlas_glyph.offset_x - padding) * glyph_scale_x" in renderer
    assert "(atlas_glyph.offset_y - padding) * glyph_scale_y" in renderer
    assert "static_cast<float>(atlas_glyph.width) * glyph_scale_x" in renderer
    assert "static_cast<float>(atlas_glyph.height) * glyph_scale_y" in renderer


def test_scaled_glyph_ink_is_not_clipped_to_unscaled_text_box():
    layout_helpers = read("../../packages/flux-common/Shared/rendering-engine/title-source/layer-evaluation-layout.inc")
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    assert "max_rich_text_scale_overhang_hint" in layout_helpers
    assert "character_scale_pad" in routing
    assert "max_rich_text_scale_overhang_hint(layer, local_time)" in routing


def test_compatibility_path_has_no_h_over_v_font_stretch_threshold():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/layer-evaluation-layout.inc")
    assert "(scale_x / scale_y) * 100.0" not in source
    assert "font.setStretch(100);" in source
    assert "rich_text_compatibility_x_ratio" in source
    assert "rich_text_document_horizontal_scale" in source


def test_gpu_text_cache_revision_invalidates_old_rasters():
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    assert '"|gpu-text-pipeline=276"' in lifecycle
