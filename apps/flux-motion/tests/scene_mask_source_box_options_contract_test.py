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
        "bool crop_when_outside_box",
        "double anchor_x",
        "double anchor_y",
    ):
        assert field in RUNTIME
    for suffix in ("box_mode", "crop_when_outside_box", "box_anchor"):
        assert f'scene_mask_key(layer->id, "{suffix}")' in RUNTIME
        assert f'scene_mask_key(layer->id, "{suffix}")' in SOURCE


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
    assert "display_width = fitted.width * cfg.zoom" in SOURCE
    assert "(box_width - display_width) * cfg.anchor_x + cfg.x" in SOURCE
    assert SOURCE.count("scene_mask_box_layout(") >= 3
    assert "sceneScale" in SOURCE
    assert "cropOutsideBox > 0.5" in SOURCE


if __name__ == "__main__":
    test_scene_mask_source_options_are_persistent()
    test_all_requested_fit_modes_are_exposed()
    test_layout_is_shared_by_both_scene_mask_render_paths()
    print("scene mask source box options contract: PASS")
