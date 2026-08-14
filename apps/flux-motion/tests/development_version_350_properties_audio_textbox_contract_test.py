from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
modern = read("Editor/fxm-modern-controls.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
property_sync = read("src/editor/properties-panel/property-synchronization.inc")
shape = read("src/editor/properties-panel/construction-gradient-image-signals.inc")
audio = read("src/editor/title-editor/editor-audio-preview.inc")
model = read("Core/layer-model.h")
serialization = read("src/core/title-data.cpp")
text_controls = read("src/editor/properties-panel/construction-transform-character.inc")
canvas_resize = read("src/canvas/canvas-preview/pointer-events.inc")

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 350
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 350

# Shared diamonds/carets are compact, borderless, and the field is inserted
# before its navigation group so the complete control sits on the right.
assert "QToolButton{background:transparent;border:none;padding:0;}" in modern
assert "QPushButton,QToolButton{background:transparent;border:none" in modern
assert properties.index("hl->addWidget(field, 1);") < properties.index(
    "hl->addWidget(make_keyframe_controls(button, row)")
assert "row, 5, Qt::AlignRight | Qt::AlignVCenter" in properties
assert "row, 3, Qt::AlignRight | Qt::AlignVCenter" in properties
assert "0, 4, Qt::AlignRight | Qt::AlignVCenter" in shape

# Properties use the same compact form geometry and late-widget normalizer as
# the Camera inspector.
for token in (
    "form->setContentsMargins(5, 4, 5, 5)",
    "form->setHorizontalSpacing(4)",
    "form->setVerticalSpacing(2)",
    "form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter)",
):
    assert token in modern and token in properties
assert "fxm_apply_transform_panel_widget_style(inner);" in property_sync

# Stereo geometry and metering remain OBS-native. Only the explicit monitor
# mute X uses a semantic red; meter colors come from the host VolumeMeter
# properties and accessibility configuration.
assert "class FxmEditorAudioMonitorButton" in audio
assert audio.index("editor_audio_meter_ = new FxmEditorAudioMeter") < audio.index(
    "editor_audio_monitor_button_ = new FxmEditorAudioMonitorButton")
assert "obs_volmeter_create(OBS_FADER_LOG)" in audio
assert 'candidate->inherits("VolumeMeter")' in audio
assert '"foregroundNominalColor"' in audio
assert "db >= static_cast<int>(minimum_level_)" in audio
assert "display_peak_hold_" in audio and "display_input_peak_hold_" in audio
assert "if (isChecked())" in audio and "QColor(220, 45, 45)" in audio
assert "QColor(34, 154, 58)" not in audio

# Text maxima follow Size until an explicitly different maximum is authored,
# and that distinction survives serialization, presets and canvas resize.
for flag in (
    "max_text_box_width_overridden",
    "max_text_box_height_overridden",
):
    assert flag in model
    assert flag in serialization
    assert flag in text_controls
    assert flag in canvas_resize
assert "std::abs(v - eval_box_width(*layer_, local_time())) > 0.01" in text_controls
assert "std::abs(v - eval_box_height(*layer_, local_time())) > 0.01" in text_controls

# No ordinary checkbox is directly instantiated; all editor check controls use
# the shared switch subclass while keeping signal-compatible QCheckBox pointers.
source_text = "\n".join(
    path.read_text(encoding="utf-8", errors="ignore")
    for path in (ROOT / "src").rglob("*")
    if path.is_file() and path.suffix in {".cpp", ".h", ".inc"}
)
assert "new QCheckBox" not in source_text

print("Development Version 350 properties/audio/text-box contract: PASS")
