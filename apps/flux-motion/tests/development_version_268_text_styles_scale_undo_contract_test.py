#!/usr/bin/env python3
"""Development Version 268 contracts: editable text styles, glyph scale and Properties history."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_development_version_is_268_or_newer():
    cmake = read("CMakeLists.txt")
    header = read("src/core/build-info.h")
    import re
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake)
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', header)
    assert cmake_match and int(cmake_match.group(1)) >= 268
    assert header_match and int(header_match.group(1)) >= 268


def test_text_styles_store_stroke_and_have_an_editor_with_preview():
    cpp = read("src/editor/style-presets.cpp")
    header = read("src/editor/style-presets.h")

    assert "kStylePresetFileVersion = 4" in cpp
    assert 'o[QStringLiteral("stroke")] = strokePayloadFromLayer(source);' in cpp
    assert "applyStrokePayloadToLayer" in cpp
    assert "strokeFormatFromPayload" in cpp
    assert "class TextStylePreview final" in cpp
    assert "class TextStyleEditDialog final" in cpp
    assert "void StylePresetPanel::editSelectedPreset()" in cpp
    assert "edit_button_" in header
    assert "textPresetToParagraphFormat" in header
    assert "RichTextCharStroke" in cpp
    assert "RichTextParagraphAll" in cpp


def test_gpu_character_scale_changes_raster_geometry_not_tracking():
    renderer = read("src/rendering/title-gpu-text-renderer.cpp")
    rich_text = read("src/text/title-rich-text.cpp")

    assert "glyph_scale_x = std::clamp(glyph.scale_x" in renderer
    assert "glyph_scale_y = std::clamp(glyph.scale_y" in renderer
    assert ("glyph_origin_x + (raw_x0 - glyph_origin_x) * raster_scale_x" in renderer or
            "glyph_transform.scale(raster_scale_x, 1.0);" in renderer or
            "glyph_transform.scale(raster_scale_x, raster_scale_y);" in renderer or
            "gray.scaled(scaled_width, gray.height()" in renderer or
            "transform.scale(static_cast<qreal>(horizontal_factor), 1.0)" in renderer or
            "atlas_glyph.width) * glyph_scale_x" in renderer)
    routing = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-masks-groups-cache.inc")
    assert ("H Scale changes advances but not" in renderer or
            "Rasterize the glyph at its real independent character scale" in renderer or
            "resample its coverage explicitly" in renderer or
            "raw.pathForGlyph(glyph_id)" in renderer or
            "layer_has_anisotropic_rich_text_scale" in routing)
    assert "f.stroke.enabled = layer.outline_enabled" in rich_text


def test_properties_panel_exposes_canonical_undo_redo_stack():
    header = read("src/editor/properties-panel.h")
    panel = read("src/editor/properties-panel/popup-state.inc")
    docks = read("src/editor/title-editor/commands-docks.inc")
    history = read("src/editor/title-editor/layout-template-tools.inc")

    assert "void undo_requested();" in header
    assert "void redo_requested();" in header
    assert "FluxMotionPropertiesHistory" in panel
    assert "btn_properties_undo_" in panel
    assert "btn_properties_redo_" in panel
    assert "restore_undo_snapshot(undo_index_ - 1)" in docks
    assert "restore_undo_snapshot(undo_index_ + 1)" in docks
    assert "props_->set_undo_redo_available(undo_available, redo_available)" in history


def test_msvc_clamp_operands_are_explicitly_double():
    cpp = read("src/editor/style-presets.cpp")
    assert cpp.count("static_cast<double>(stroke_color.alphaF())") >= 2
    assert "static_cast<double>(layer_->outline_opacity)" in cpp
    assert "static_cast<double>(preview_layer.outline_opacity)" in cpp
    assert "stroke_color.alphaF() * layer_->outline_opacity" not in cpp
    assert "stroke_color.alphaF() * preview_layer.outline_opacity" not in cpp


if __name__ == "__main__":
    test_development_version_is_268_or_newer()
    test_text_styles_store_stroke_and_have_an_editor_with_preview()
    test_gpu_character_scale_changes_raster_geometry_not_tracking()
    test_properties_panel_exposes_canonical_undo_redo_stack()
    test_msvc_clamp_operands_are_explicitly_double()
    print("development version 268 text-style/scale/undo contract passed")
