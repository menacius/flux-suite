from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_current_development_version_metadata():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    assert cmake_match and int(cmake_match.group(1)) >= 273
    assert header_match and int(header_match.group(1)) >= 273


def test_effective_scale_is_carried_per_glyph_not_only_per_qt_run():
    header = read("src/text/title-text-layout.h")
    layout = read("src/text/title-text-layout-qt.cpp")
    assert "float scale_x = 1.0f;" in header
    assert "float scale_y = 1.0f;" in header
    assert "const auto string_indexes = glyph_run.stringIndexes();" in layout
    assert "mapped.formats.push_back(" in layout
    assert "rich_text_format_at(request.document, cluster_byte)" in layout
    assert "glyph_format.scale_x" in layout
    assert "glyph_format.scale_y" in layout


def test_atlas_is_authored_size_and_scale_is_applied_to_gpu_quad():
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "raw.pathForGlyph(glyph_id)" in renderer
    assert "glyph_transform.scale(raster_scale_x, raster_scale_y)" not in renderer
    assert "(atlas_glyph.offset_x - padding) * glyph_scale_x" in renderer
    assert "(atlas_glyph.offset_y - padding) * glyph_scale_y" in renderer
    assert "static_cast<float>(atlas_glyph.width) * glyph_scale_x" in renderer
    assert "static_cast<float>(atlas_glyph.height) * glyph_scale_y" in renderer


def test_scaled_sdf_padding_is_cropped_independently_per_axis():
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "crop_quad_padding(Quad &quad, float inset_x, float inset_y)" in renderer
    assert "padding * glyph_scale_x - required_padding" in renderer
    assert "padding * glyph_scale_y - required_padding" in renderer


def test_anisotropic_and_multistyle_text_remain_on_gpu_path():
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    assert "layer_has_anisotropic_rich_text_scale" not in routing
    assert "layer_can_use_gpu_text_raster(" in lifecycle


def test_qtextdocument_source_fallback_avoids_width_face_threshold():
    source = read("../../packages/flux-common/Shared/rendering-engine/title-source/layer-evaluation-layout.inc")
    assert "static_cast<double>(font.pixelSize()) * scale_y" in source
    assert "(scale_x / scale_y) * 100.0" not in source
    assert "font.setStretch(100);" in source
    assert "rich_text_compatibility_x_ratio" in source
