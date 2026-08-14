from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_current_development_version_metadata():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    assert cmake_match and int(cmake_match.group(1)) >= 269
    assert header_match and int(header_match.group(1)) >= 269


def test_text_style_editor_reuses_the_real_properties_panel():
    source = read("src/editor/style-presets.cpp")
    assert '#include "properties-panel.h"' in source
    assert "properties_ = new PropertiesPanel" in source
    assert "properties_->set_text_style_editor_mode(true)" in source
    assert "NumericDragLabel" not in source  # no second control implementation
    assert "new QTabWidget" not in source
    assert "preview_->setMinimumSize(560, 480)" in source
    assert "28.0, 144.0" in source


def test_properties_panel_exposes_a_text_style_mode():
    header = read("src/editor/properties-panel.h")
    source = read("src/editor/properties-panel/popup-state.inc")
    assert "void set_text_style_editor_mode(bool enabled);" in header
    assert "void PropertiesPanel::apply_text_style_editor_mode_visibility()" in source
    for section in ("text_box_", "type_options_box_", "paragraph_box_", "dynamic_text_box_", "rect_box_", "appearance_box_"):
        assert f"set_section_visible({section}, true);" in source
    assert "btn_kf_char_scale_x_" in source
    assert "btn_kf_char_scale_y_" in source


def test_style_editor_uses_shared_undo_redo_and_commit_semantics():
    source = read("src/editor/style-presets.cpp")
    assert "&PropertiesPanel::property_changed" in source
    assert "&PropertiesPanel::undo_requested" in source
    assert "&PropertiesPanel::redo_requested" in source
    assert "if (push_undo_snapshot)" in source
    assert "properties_->set_undo_redo_available" in source


def test_hv_scale_uses_the_shared_per_run_glyph_geometry():
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    layout = read("src/text/title-text-layout-qt.cpp")
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    assert "layer_has_anisotropic_rich_text_scale" not in routing
    assert "run.shaping_style = shaping_style(run_format);" in layout
    assert "atlas_glyph.width) * glyph_scale_x" in renderer
    assert "atlas_glyph.height) * glyph_scale_y" in renderer
