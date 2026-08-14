from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

def test_current_metadata_is_274():
    match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    assert match and int(match.group(1)) >= 271

def test_effective_multistyle_scale_detection_model_is_preserved():
    model = read("src/text/title-rich-text.cpp")
    assert "boundaries.reserve(doc.ranges.size() * 2 + 1)" in model
    assert "rich_text_format_at(doc, offset)" in model

def test_current_renderer_applies_scale_after_atlas_lookup():
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "raw.pathForGlyph(glyph_id)" in renderer
    assert "glyph_scale_x" in renderer
    assert "glyph_scale_y" in renderer
