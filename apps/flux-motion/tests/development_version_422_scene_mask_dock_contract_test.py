#!/usr/bin/env python3
"""Development Version 422 Scene Mask dock and lifecycle contract."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[1]


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> None:
    cmake = read(ROOT / "CMakeLists.txt")
    version = read(ROOT / "VERSION.txt").strip()
    plugin_version = read(REPO / "plugins/obs/flux-motion/VERSION.txt").strip()
    runtime = read(
        REPO
        / "packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc"
    )
    source_header = read(
        REPO / "packages/flux-common/Shared/rendering-engine/title-source.h"
    )
    source_properties = read(
        REPO
        / "packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc"
    )
    hotkeys = read(REPO / "plugins/obs/flux-motion/src/title-hotkeys.cpp")
    dock = read(REPO / "plugins/obs/flux-motion/src/scene-mask-dock.cpp")
    plugin = read(REPO / "plugins/obs/flux-motion/src/plugin-main.cpp")
    integration = read(
        REPO / "plugins/obs/flux-motion/cmake/FluxMotionObsPlugin.cmake"
    )
    manifest = json.loads(read(ROOT / "tests/test-suite-manifest.json"))

    assert version == "2026 - v0.8.20-alpha"
    assert plugin_version == version
    assert "project(flux-motion VERSION 0.8.20)" in cmake
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "422")' in cmake
    assert manifest["development_version"] == 422

    assert "bool active_ref = false;" in runtime
    assert "const bool active_ref = data->scene_mask_foreground_active;" in runtime
    assert "if (active_ref)\n            obs_source_inc_active(scene);" in runtime
    assert "if (active.active_ref)\n                    obs_source_dec_active" in runtime
    assert "active.active_ref != requires_active_ref" in runtime

    assert 'kHotkeyBackupFile = "dock-hotkeys.json"' in hotkeys
    assert '"hotkey_bindings_changed"' in hotkeys
    assert "QSaveFile file(path);" in hotkeys
    assert "QTimer::singleShot(0, QCoreApplication::instance()" in hotkeys

    assert "class SceneMaskJoystick" in dock
    assert "obs_frontend_get_current_preview_scene" in dock
    assert "obs_source_active(source)" in dock
    assert 'scene_mask_key(layer_id, "zoom_percent")' in dock
    assert "W" in dock and "T" in dock
    assert 'QStringLiteral("MONITOR")' in dock
    assert 'QStringLiteral("PREVIEW")' in dock
    assert 'QStringLiteral("Smooth motion")' in dock
    assert 'scene_mask_key(card_ptr->layer_id, "smooth_motion")' in dock
    assert 'scene_mask_key(card_ptr->layer_id, "smoothness")' in dock
    assert "new QSlider(Qt::Horizontal" in dock
    assert "tau_ms" in dock
    assert "1.0 - std::exp(-16.0 / tau_ms)" in dock
    assert "set_card_motion_target(card_ptr, 0.0, 0.0" in dock
    assert "card_ptr->target_y, 100.0" in dock
    assert "QDoubleSpinBox" in dock
    assert "open_scene_mask_source_controls" in dock
    assert 'PROP_SCENE_MASK_CLIP_TO_BOUNDS "scene_masks_clip_to_bounds"' in source_header
    assert "cfg.keep_within_bounds = clip_all_masks" in runtime
    assert "constrain_scene_mask_config_to_box" in runtime
    assert 'scene_mask_key(layer->id, "crop_when_outside_box")' not in runtime
    assert 'scene_mask_key(layer->id, "crop_when_outside_box")' not in source_properties
    assert '"Open Joystick / Dial Controls..."' in source_properties
    assert "obs_properties_add_float(layer_group" in source_properties
    assert "obs_properties_add_float_slider(layer_group" not in source_properties
    assert 'obs_frontend_add_custom_qdock("flux-motion-scene-masks-dock"' in plugin
    assert "title_source_set_scene_mask_controls_opener" in plugin
    assert 'scene-mask-dock.cpp"' in integration

    editor_tests = manifest["areas"]["editor_gui"]["python"]
    assert "tests/development_version_422_scene_mask_dock_contract_test.py" in editor_tests
    print("Development Version 422 Scene Mask dock contract passed")


if __name__ == "__main__":
    main()
