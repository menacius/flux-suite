from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def test_current_metadata_is_274():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    assert cmake_match and int(cmake_match.group(1)) >= 272
    assert header_match and int(header_match.group(1)) >= 272

def test_horizontal_layout_and_vertical_envelope_are_independent():
    model = read("src/text/title-rich-text.cpp")
    layout = read("src/text/title-text-layout-qt.cpp")
    assert "std::lround(sx * 100.0f)" in model
    assert "sx / sy" not in model
    assert "font.setStretch(100);" in layout
    assert "apply_line_horizontal_character_scale" in layout
    assert "normalize_line_vertical_scale(*data, line);" in layout

def test_scale_reaches_real_gpu_geometry_per_glyph():
    header = read("src/text/title-text-layout.h")
    layout = read("src/text/title-text-layout-qt.cpp")
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "float scale_x = 1.0f;" in header
    assert "float scale_y = 1.0f;" in header
    assert "glyph.scale_x = std::clamp(" in layout
    assert "glyph_format.scale_x" in layout
    assert "glyph.scale_y = std::clamp(" in layout
    assert "glyph_format.scale_y" in layout
    assert "atlas_glyph.width) * glyph_scale_x" in renderer
    assert "atlas_glyph.height) * glyph_scale_y" in renderer

def test_anisotropic_text_uses_gpu_path():
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    assert "layer_has_anisotropic_rich_text_scale" not in routing
