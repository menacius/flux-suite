from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def test_current_metadata_is_274():
    match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    assert match and int(match.group(1)) >= 270

def test_multistyle_runs_keep_independent_scale_values():
    layout_header = read("src/text/title-text-layout.h")
    layout_source = read("src/text/title-text-layout-qt.cpp")
    assert "RichTextCharScaleX | RichTextCharScaleY" in layout_header
    assert "glyph.scale_x" in layout_source
    assert "glyph.scale_y" in layout_source

def test_canvas_scales_glyph_quad_not_only_positions():
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "atlas_glyph.width) * glyph_scale_x" in renderer
    assert "atlas_glyph.height) * glyph_scale_y" in renderer
