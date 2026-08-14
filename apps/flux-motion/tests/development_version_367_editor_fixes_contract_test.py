from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
model = read("Core/layer-model.h")
serialization = read("src/core/title-data.cpp")
stack = read("src/layers/layer-stack-widget.cpp")
creation = read("src/editor/title-editor/panels-colors.inc")
properties_cpp = read("src/editor/properties-panel.cpp")
properties = read("src/editor/properties-panel/popup-state.inc")
property_sync = read("src/editor/properties-panel/property-synchronization.inc")
canvas_header = read("src/canvas/canvas-preview.h")
canvas_transform = read("src/canvas/canvas-preview/transform-snap.inc")
canvas_pointer = read("src/canvas/canvas-preview/pointer-events.inc")
canvas_hit = read("src/canvas/canvas-preview/canvas-overlay-paint.inc")
canvas_paint = read("src/canvas/canvas-preview/keyboard-wheel-events.inc")
canvas_3d = read("src/canvas/canvas-preview/editor-3d-tools.inc")
runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")
gpu = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-presentation-readback.inc")
timeline = read("src/timeline/timeline-widget.cpp")
hierarchy = read("src/editor/title-editor-internal/hierarchy-model.inc")
audio_meter = read("src/editor/title-editor/editor-audio-preview.inc")
locale = read("data/locale/en-US.ini")

cmake_version = int(re.search(
    r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1))
build_version = int(re.search(
    r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1))
assert cmake_version >= 367
assert build_version >= 367

# Audio/Video gain entry is authored in dB, pan keeps a visible sign, and both
# numeric fields synchronize bidirectionally with their sliders.
for token in (
    "kAudioMinimumDb = -96.0",
    "kAudioMaximumDb = 12.0",
    "audio_gain_to_db",
    "audio_db_to_gain",
    "class SignedAudioSpinBox",
    'QStringLiteral("+") + number',
):
    assert token in properties_cpp
assert 'spn_audio_volume_->setSuffix(QStringLiteral(" dB"))' in properties
assert "audio_db_to_slider(db)" in properties
assert "const double db=double(value)/10.0" in properties
assert "sld_audio_pan_->setValue(qRound(v))" in properties
assert "spn_audio_pan_->setValue(v)" in properties

# The collapsible header is the only section title and all raw translation keys
# used by the Audio/Video/Empty UI resolve in the shipped locale.
assert "audio_box_->setTitle(QString())" in property_sync
assert 'fxm_tr("OBSTitles.VideoAudio")' in property_sync
for token in (
    'OBSTitles.AudioLayer="Audio Layer"',
    'OBSTitles.VideoAudio="Video Audio"',
    'OBSTitles.Empty="Empty"',
):
    assert token in locale

# Empty is a stable serialized non-raster type, creatable from the layer menu,
# usable as a transform parent, and represented only by 2D/3D editor axes.
assert "Empty = 14" in model
assert "(int)LayerType::Empty" in serialization
assert "&LayerStack::on_add_empty" in stack
assert "add_layer_requested(LayerType::Empty)" in stack
assert "type == LayerType::Empty" in creation
assert "empty_layer_overlay_points" in canvas_header
assert "void CanvasPreview::draw_empty_layer_overlays" in canvas_3d
assert "const int axis_count = is_3d ? 3 : 2" in canvas_3d
assert "if (layer.type == LayerType::Empty)" in runtime
assert "if (layer.type == LayerType::Empty) return false;" in gpu

# Pure Audio and Empty controls never enter artwork selection geometry; Audio
# hit testing is disabled while Empty uses its explicit axes object.
assert "layer->type == LayerType::Audio ||\n            layer->type == LayerType::Empty" in canvas_transform
assert "if (layer->type == LayerType::Audio)\n        return DragMode::None;" in canvas_pointer
assert "if (layer->type == LayerType::Audio)\n            continue;" in canvas_hit
assert "empty_layer_overlay_contains" in canvas_hit

# The solid grey background, collapsed keyframes, and OBS-native meter
# integration are explicit authoring contracts.
assert "QColor(0x4a, 0x4a, 0x4a), QColor(0x4a, 0x4a, 0x4a)" in canvas_paint
assert "timeline_properties_for_owner" in hierarchy
assert "timeline_owner_keyframe_sections_expanded" in hierarchy
assert "std::map<int, AggregateKeyframe> aggregate" in timeline
for token in (
    "obs_volmeter_create(OBS_FADER_LOG)",
    'candidate->inherits("VolumeMeter")',
    '"backgroundNominalColor"',
    '"foregroundNominalColor"',
    "warning_level_ = meter_type == TRUE_PEAK_METER ? -13.0 : -20.0",
    "error_level_ = meter_type == TRUE_PEAK_METER ? -2.0 : -9.0",
):
    assert token in audio_meter

print("Development Version 367 editor fixes contract: PASS")
