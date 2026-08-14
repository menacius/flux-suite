from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
construction = read("src/editor/properties-panel/popup-state.inc")
sync = read("src/editor/properties-panel/property-synchronization.inc")
manifest = json.loads(read("tests/test-suite-manifest.json"))

cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 369
assert build_version >= 369
assert manifest["development_version"] >= 369
assert (
    "tests/development_version_369_audio_properties_rows_inputs_contract_test.py"
    in manifest["areas"]["editor_gui"]["python"]
)

# The two shared Audio/Video Audio numeric editors cannot be collapsed by the
# responsive inspector and are deliberately placed after their sliders.
for token in (
    'spin->setProperty("fxmSkipTransformPanelMetrics", true);',
    "spin->setFixedWidth(88);",
    "spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);",
    'spn_audio_volume_->setObjectName(QStringLiteral("audioVolumeDbInput"));',
    'spn_audio_pan_->setObjectName(QStringLiteral("audioPanInput"));',
    'spn_audio_volume_->setSuffix(QStringLiteral(" dB"));',
    'l->addWidget(slider, 1);',
    'l->addWidget(numeric, 0, Qt::AlignRight | Qt::AlignVCenter);',
):
    assert token in construction

# Pure Audio no longer exposes these diagnostic/convenience rows. Video Audio
# may retain waveform progress for its linked decode stream.
assert "set_form_row_visible(row_audio_preview_, false);" in sync
assert "set_form_row_visible(row_audio_range_tools_, false);" in sync
assert "const bool show_audio_waveform_rows = is_video;" in sync
assert "set_form_row_visible(row_audio_range_, !is_video);" in sync
assert "set_form_row_visible(lbl_audio_waveform_status_," in sync
assert "set_form_row_visible(\n                bar_audio_waveform_," in sync

# Both directions update the model, animated property and paired control.
for token in (
    "audio_db_to_gain(db)",
    "sld_audio_volume_->setValue(audio_db_to_slider(db))",
    "spn_audio_volume_->setValue(db)",
    "sld_audio_pan_->setValue(qRound(v))",
    "spn_audio_pan_->setValue(v)",
):
    assert token in construction

print("Development Version 369 audio properties rows/inputs contract: PASS")
