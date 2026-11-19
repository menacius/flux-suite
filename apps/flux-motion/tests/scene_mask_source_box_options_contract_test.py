"""Scene-mask OBS source options share the image-box fit contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc").read_text(
    encoding="utf-8"
)
SOURCE = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc").read_text(
    encoding="utf-8"
)


def test_scene_mask_source_options_are_persistent():
    for field in (
        "ImageBoxMode box_mode",
        "bool keep_within_bounds",
        "double anchor_x",
        "double anchor_y",
    ):
        assert field in RUNTIME
    for suffix in ("box_mode", "box_anchor"):
        assert f'scene_mask_key(layer->id, "{suffix}")' in RUNTIME
        assert f'scene_mask_key(layer->id, "{suffix}")' in SOURCE
    assert 'scene_mask_key(layer->id, "crop_when_outside_box")' not in RUNTIME
    assert 'scene_mask_key(layer->id, "crop_when_outside_box")' not in SOURCE


def test_all_requested_fit_modes_are_exposed():
    properties = SOURCE.split(
        "static void add_scene_mask_properties_for_title", 1
    )[1]
    for mode in (
        "ImageBoxMode::FitImageToBox",
        "ImageBoxMode::FillHorizontal",
        "ImageBoxMode::FillVertical",
        "ImageBoxMode::FitToLongSide",
        "ImageBoxMode::FitToShortSide",
        "ImageBoxMode::StretchToFill",
    ):
        assert mode in properties
    assert "anchor_labels[9]" in properties


def test_layout_is_shared_by_both_scene_mask_render_paths():
    assert "fxm::calculate_image_display_size(" in SOURCE
    assert "display_width = fitted.width * effective.zoom" in SOURCE
    assert "(box_width - display_width) * effective.anchor_x + effective.x" in SOURCE
    assert SOURCE.count("scene_mask_box_layout(") >= 3
    assert "sceneScale" in SOURCE
    assert "cropOutsideBox" not in SOURCE


def test_keep_within_bounds_constrains_persisted_transform_and_render_layout():
    assert "constrain_scene_mask_config_to_box" in RUNTIME
    assert "minimum_zoom" in RUNTIME
    assert "std::max(box_width / fitted.width, box_height / fitted.height)" in RUNTIME
    assert "cfg.zoom = std::clamp(cfg.zoom, minimum_zoom, 8.0)" in RUNTIME
    assert "obs_data_set_double(settings, zoom_key.c_str()" in RUNTIME
    assert "obs_data_set_double(settings, x_key.c_str(), cfg.x)" in RUNTIME
    assert "obs_data_set_double(settings, y_key.c_str(), cfg.y)" in RUNTIME
    assert "TitleSourceData::SceneMaskConfig effective = cfg" in SOURCE
    assert "constrain_scene_mask_config_to_box(" in SOURCE


if __name__ == "__main__":
    test_scene_mask_source_options_are_persistent()
    test_all_requested_fit_modes_are_exposed()
    test_layout_is_shared_by_both_scene_mask_render_paths()
    test_keep_within_bounds_constrains_persisted_transform_and_render_layout()
    print("scene mask source box options contract: PASS")
