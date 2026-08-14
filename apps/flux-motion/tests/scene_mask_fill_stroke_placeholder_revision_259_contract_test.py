from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src" / "obs" / "title-source" / "source-registration.inc"
SEL = ROOT / "src" / "editor" / "properties-panel" / "selection-refresh.inc"
HIER = ROOT / "src" / "editor" / "title-editor-internal" / "hierarchy-model.inc"

def test_scene_mask_matte_uses_fill_only_contract():
    text = SRC.read_text(encoding="utf-8")
    assert "scene_mask_fill_matte_layer" in text
    assert "fill.outline_enabled = false" in text
    assert "make_rich_text_strokes_enabled(fill.rich_text, false)" in text
    assert "|gpu-scene-mask-fill-only-v259" in text
    assert "render_gpu_layer_base_raster(\n            session->title, fill_matte_layer" in text


def test_scene_mask_stroke_is_composited_after_matted_scene():
    text = SRC.read_text(encoding="utf-8")
    assert "scene_mask_stroke_composite_layer" in text
    assert "make_rich_text_fill_transparent(stroke.rich_text)" in text
    assert "render_scene_mask_stroke_over_current_target" in text
    segment = text.split("gs_texrender_reset(data->scene_mask_matted_texrender);")[1].split("gs_effect_t *present")[0]
    assert "apply_chain_opacity = true" in text
    assert "render_scene_mask_stroke_over_current_target(\n                    data, title, *layer, title_time, false);" in segment
    assert segment.find("render_scene_mask_stroke_over_current_target") < segment.find("apply_gpu_layer_effect_stack")


def test_scene_mask_placeholder_styles_in_inactive_fill_swatches():
    hierarchy = HIER.read_text(encoding="utf-8")
    selection = SEL.read_text(encoding="utf-8")
    assert "style_scene_mask_placeholder_button" in hierarchy
    assert "buttonFillKind" in hierarchy
    assert "scene-mask-placeholder" in hierarchy
    assert "if (is_scene_mask_layer) {\n            style_scene_mask_placeholder_button(btn_appearance_fill_color_);" in selection
    assert "if (is_scene_mask_layer)\n        style_scene_mask_placeholder_button(btn_fill_color_);" in selection
