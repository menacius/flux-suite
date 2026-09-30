#!/usr/bin/env python3
"""Dock text input and scene-mask Preview lifecycle regression contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def test_live_text_requires_explicit_edit_focus():
    helpers = read("src/editor/title-dock/template-library-helpers.inc")
    field = helpers.split("class LiveTextCueField", 1)[1].split(
        "class LiveImageCueField", 1
    )[0]
    assert "edit_focus_armed_" in field
    assert "setFocusPolicy(Qt::ClickFocus);" in field
    assert "void mousePressEvent(QMouseEvent *event) override" in field
    assert "edit_focus_armed_ = true;" in field
    assert "if (!edit_focus_armed_)" in field
    assert "event->ignore();" in field
    assert "ancestor->setFocus(Qt::OtherFocusReason);" in field
    assert "edit_focus_armed_ = false;" in field

    lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
    assert "qApp->installEventFilter(this);" in lifecycle
    assert "event->type() == QEvent::MouseButtonPress" in lifecycle
    assert "active_field->isAncestorOf(pressed_widget)" in lifecycle
    assert "active_field->clearFocus();" in lifecycle
    assert "qApp->removeEventFilter(this);" in lifecycle


def test_scene_masks_render_while_showing_in_obs_preview():
    runtime = read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc"
    )
    registration = read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
    )
    resources = read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-resources-primitives.inc"
    )

    activate = runtime.split("static void activate_scene_mask_scenes", 1)[1].split(
        "static void sync_scene_mask_scenes_for_cue", 1
    )[0]
    assert "shown_on_display.load" in activate
    assert "const bool active_ref = data->scene_mask_foreground_active;" in activate
    assert "if (active_ref)" in activate

    sync = runtime.split("static void sync_scene_mask_scenes_for_cue", 1)[1].split(
        "static int live_text_playlist_row_count", 1
    )[0]
    assert "shown_on_display.load" in sync
    assert "scene_mask_foreground_active" not in sync

    prepare = registration.split(
        "static bool prepare_runtime_scene_mask_rasters", 1
    )[1].split("static void render_scene_masks_gpu", 1)[0]
    render = registration.split("static void render_scene_masks_gpu", 1)[1].split(
        "static void source_video_render", 1
    )[0]
    assert "shown_on_display.load" in prepare
    assert "shown_on_display.load" in render

    deactivate = resources.split("static void source_deactivate", 1)[1].split(
        "static void source_show", 1
    )[0]
    assert "sync_scene_mask_scenes_for_cue" in deactivate


if __name__ == "__main__":
    test_live_text_requires_explicit_edit_focus()
    test_scene_masks_render_while_showing_in_obs_preview()
    print("dock focus and scene-mask Preview contract: PASS")
