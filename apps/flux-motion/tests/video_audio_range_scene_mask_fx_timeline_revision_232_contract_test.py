from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
compat = (ROOT / '../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc').read_text()
hierarchy = (ROOT / 'src/editor/title-editor-internal/hierarchy-model.inc').read_text()
props_h = (ROOT / 'src/editor/properties-panel.h').read_text()
popup = (ROOT / 'src/editor/properties-panel/popup-state.inc').read_text()
sync = (ROOT / 'src/editor/properties-panel/property-synchronization.inc').read_text()
schema = (ROOT / 'Core/title-serialization-schema.h').read_text()
cmake = (ROOT / 'CMakeLists.txt').read_text()
manifest = (ROOT / 'tests/test-suite-manifest.json').read_text()

# Development Version 243 metadata and migration continuity.
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'case 232:' in schema
assert '"development_version": 243' in manifest

# Scene-mask placeholders are editor-only base rasters shaped like their layer content, not overlay rectangles.
assert 'add_scene_mask_placeholder_path' in compat
assert 'layer_type_uses_shape_geometry(layer.type)' in compat
assert 'cairo_add_layer_shape(cr, layer, width, height)' in compat
assert 'cairo_add_rounded_rect_corners(cr, 0.0, 0.0, width, height' in compat
assert 'normal effect' in compat and 'stack is applied after this base raster' in compat
# The old rectangle-overlay placeholder must not remain in the mask renderer.
placeholder_block = compat[compat.index('static void render_scene_mask_placeholder'):compat.index('static void render_transition_input_placeholder')]
assert 'cairo_rectangle(cr, x, y, width, height)' not in placeholder_block
assert 'cairo_clip(cr)' in placeholder_block

# FX strips/properties appear only when that effect property has authored keyframes.
assert 'FX strips are a keyframe navigation surface' in hierarchy
assert 'if (prop.is_animated() && valid_index && effect_index >= 0' in hierarchy
assert 'effect_properties[effect_index].push_back(prop)' in hierarchy

# Video layers have a compact properties range editor; Audio retains playhead actions and feedback.
assert 'QGroupBox       *video_box_' in props_h
assert 'QDoubleSpinBox  *spn_video_in_' in props_h
assert 'QDoubleSpinBox  *spn_video_out_' in props_h
assert 'QLineEdit       *edt_video_source_' not in props_h
assert 'QPushButton     *btn_video_set_in_' not in props_h
assert 'QPushButton     *btn_video_set_out_' not in props_h
assert 'QLabel          *lbl_video_range_preview_' not in props_h
assert 'QLabel          *lbl_video_range_feedback_' not in props_h
assert 'add_form_row(video_form, QStringLiteral("Range"), video_range_row);' in popup
assert 'add_form_row(video_form, QStringLiteral("Source")' not in popup
assert 'video_range_layout->addWidget(spn_video_in_, 1);' in popup
assert 'video_range_layout->addWidget(spn_video_out_, 1);' in popup
assert 'QPushButton     *btn_audio_set_in_' in props_h
assert 'QPushButton     *btn_audio_set_out_' in props_h
assert 'QLabel          *lbl_audio_range_feedback_' in props_h
assert 'video_box_ = new QGroupBox(QStringLiteral("Video Layer")' in popup
assert 'btn_audio_set_in_ = new QPushButton(QStringLiteral("Set In")' in popup
assert 'btn_audio_set_out_ = new QPushButton(QStringLiteral("Set Out")' in popup
assert 'set_audio_range_from_playhead' in popup
assert 'synchronize_video_audio_streams(*title_, layer_->id)' in popup
assert 'emit audio_property_changed(commit)' in popup
assert 'emit property_changed(commit)' in popup
assert 'QStringLiteral("Inside")' in sync and 'QStringLiteral("Outside")' in sync
assert 'range length follows strip' in sync and 'media %2 s' in sync
assert 'lbl_audio_range_feedback_' in sync
assert 'if (video_box_) video_box_->setVisible(is_video)' in sync

print('Development Version 243 video/audio range, scene-mask placeholder and FX-strip contract: PASS')
