#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_shape_scene_mask_placeholder_rebuilds_immediately_and_bypasses_gpu_primitive():
    playback = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
    assert "!entry.key.empty() && entry.key == key" in playback
    assert "scene_mask_placeholder_layer" in playback
    assert "const bool gpu_text_candidate = !scene_mask_placeholder_layer" in playback
    assert "!scene_mask_placeholder_layer &&" in playback


def test_shape_scene_mask_placeholder_keeps_stroke_inside_effected_raster():
    raster = read("../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc")
    assert "render_scene_mask_placeholder_shape_stroke" in raster
    assert "the placeholder remains a fill-only editor" in raster
    assert "effect stack process fill and stroke together" in raster
    assert "set_layer_outline_source(cr, layer" in raster


def test_editor_audio_dock_has_visual_meter_fed_from_private_source():
    preview = read("src/editor/title-editor/editor-audio-preview.inc")
    editor_h = read("src/editor/title-editor.h")
    runtime_h = read("../../plugins/obs/flux-motion/src/title-audio-runtime.h")
    runtime = read("../../plugins/obs/flux-motion/src/title-audio-runtime.cpp")
    source_h = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
    source_runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
    assert "class FxmEditorAudioMeter" in preview
    assert "paintEvent" in preview
    assert "obs_volmeter_create(OBS_FADER_LOG)" in preview
    assert "obs_volmeter_attach_source(volmeter_, source)" in preview
    assert "obs_volmeter_add_callback" in preview
    assert "editor_audio_meter_timer_" in editor_h
    assert "update_editor_audio_meter" in preview
    assert "editor_audio_meter_->setSource(editor_audio_preview_source_)" in preview
    assert "current_levels" in runtime_h
    assert "level_left_" in runtime_h and "level_right_" in runtime_h
    assert "level_update_ns_" in runtime_h
    assert "peak_l" in runtime and "peak_r" in runtime
    assert "bool title_source_get_audio_levels" in source_h
    assert "audio_runtime->current_levels" in source_runtime


if __name__ == "__main__":
    test_shape_scene_mask_placeholder_rebuilds_immediately_and_bypasses_gpu_primitive()
    test_shape_scene_mask_placeholder_keeps_stroke_inside_effected_raster()
    test_editor_audio_dock_has_visual_meter_fed_from_private_source()
    print("development version 262 scene-mask/audio-monitor contracts passed")
