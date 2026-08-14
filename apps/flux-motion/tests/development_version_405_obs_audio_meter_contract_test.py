#!/usr/bin/env python3
"""Dev405: the Editor meter follows OBS VolumeMeter behavior and theme."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (
    ROOT / "src/editor/title-editor/editor-audio-preview.inc"
).read_text(encoding="utf-8")
EDITOR = (ROOT / "src/editor/title-editor.cpp").read_text(encoding="utf-8")


for token in (
    "obs_volmeter_create(OBS_FADER_LOG)",
    "obs_volmeter_add_callback",
    "obs_volmeter_attach_source(volmeter_, source)",
    "obs_volmeter_detach_source(volmeter_)",
    "obs_volmeter_set_peak_meter_type",
    "obs_volmeter_remove_callback",
    "obs_volmeter_destroy",
):
    assert token in SOURCE

for native_property in (
    "backgroundNominalColor",
    "backgroundWarningColor",
    "backgroundErrorColor",
    "foregroundNominalColor",
    "foregroundWarningColor",
    "foregroundErrorColor",
    "magnitudeColor",
    "majorTickColor",
):
    assert f'"{native_property}"' in SOURCE

for native_behavior in (
    'candidate->inherits("VolumeMeter")',
    "minimum_level_ = -60.0",
    "kTickDbInterval = 6",
    "peak_decay_rate_ = 11.76",
    "magnitude_integration_time_ = 0.3",
    "peak_hold_duration_ = 20.0",
    "input_peak_hold_duration_ = 1.0",
    "minimum_input_level_ = -50.0",
    "TRUE_PEAK_METER ? -13.0 : -20.0",
    "TRUE_PEAK_METER ? -2.0 : -9.0",
    'config_get_bool(user_config, "Accessibility", "OverrideColors")',
):
    assert native_behavior in SOURCE

assert "editor_audio_meter_timer_->setInterval(16)" in SOURCE
assert "editor_audio_meter_timer_->setTimerType(Qt::PreciseTimer)" in SOURCE
assert "editor_audio_meter_->setSource(nullptr);" in SOURCE
assert SOURCE.index("editor_audio_meter_->setSource(nullptr);") < SOURCE.index(
    "obs_source_release(editor_audio_preview_source_);"
)

assert "#include <obs-audio-controls.h>" in EDITOR
assert "VolumeMeter.hpp" not in EDITOR
assert "OBSApp.hpp" not in EDITOR
assert "QLinearGradient" not in SOURCE
assert "draw_level_bar" not in SOURCE

print("Dev405 OBS-native Editor audio meter contract passed")
