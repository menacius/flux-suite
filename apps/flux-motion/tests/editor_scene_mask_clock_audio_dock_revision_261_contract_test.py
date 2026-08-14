from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def test_revision_264_development_metadata_is_synchronized():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "264")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "264"' in read('src/core/build-info.h')
    schema = read('Core/title-serialization-schema.h')
    assert 'kCurrentDevelopmentVersion = 264' in schema
    assert 'case 264:' in schema
    assert 'Development Version 264' in read('docs/CHANGELOG.md')


def test_editor_scene_mask_placeholder_is_fill_only():
    raster = read('../../packages/flux-common/Shared/rendering-engine/title-source/compatibility-layer-raster.inc')
    assert 'scene_mask_placeholder_fill_only_layer' in raster
    assert 'clear_placeholder_rich_text_strokes(fill.rich_text)' in raster
    assert 'silhouette_layer.use_as_scene_mask = false' in raster
    assert 'render_scene_mask_placeholder_shape_stroke' in raster
    assert 'the placeholder remains a fill-only editor' in raster
    assert 'cairo_show_text(cr, label)' not in raster.split('static void render_scene_mask_placeholder(')[1].split('static void render_transition_input_placeholder')[0]


def test_scene_mask_stroke_goes_through_scene_mask_effect_stack():
    src = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-registration.inc')
    assert 'bool apply_chain_opacity = true' in src
    assert 'render_scene_mask_stroke_over_current_target(\n                    data, title, *layer, title_time, false);' in src
    segment = src.split('gs_texrender_reset(data->scene_mask_matted_texrender);')[1].split('gs_effect_t *present')[0]
    assert segment.find('render_scene_mask_stroke_over_current_target') < segment.find('apply_gpu_layer_effect_stack')
    assert 'Opacity\n                 * is applied later to the combined texture' in src


def test_blank_title_creation_no_longer_exposes_type_choice():
    actions = read('src/editor/title-dock/title-actions.inc')
    on_add = actions.split('void TitleDock::on_add()')[1].split('void TitleDock::on_add_from_templates_library()')[0]
    assert 'QComboBox' not in on_add
    assert 'type_combo' not in on_add
    assert 'title->graphic_type = TitleGraphicType::Graphic;' in on_add
    assert 'element-defined' in on_add


def test_clock_presets_and_character_header_visibility():
    popup = read('src/editor/properties-panel/popup-state.inc')
    sync = read('src/editor/properties-panel/property-synchronization.inc')
    signals = read('src/editor/properties-panel/construction-transform-character.inc')
    assert 'cmb_clock_preset_' in read('src/editor/properties-panel.h')
    for fmt in ['H:i:s', 'H:i', 'd/m/Y', 'Y-m-d', 'd/m/Y H:i', 'U', '__custom__']:
        assert fmt in popup
    assert 'text_box_->setTitle(is_text ? QStringLiteral("Character") : QString())' in sync
    assert 'label->setVisible(is_clock)' in sync
    assert 'cmb_clock_preset_->setVisible(is_clock)' in sync
    assert 'layer_->clock_format = format.toStdString()' in signals


def test_canvas_background_button_shows_selected_background():
    commands = read('src/editor/title-editor/commands-docks.inc')
    assert 'checkerboard->setText(QStringLiteral("Background: %1").arg(selected->text()))' in commands
    assert 'Canvas background: %1' in commands


def test_editor_audio_monitor_dock_uses_obs_monitoring_and_icon_only_headphones():
    header = read('src/editor/title-editor.h')
    helpers = read('src/editor/title-editor-internal/widget-property-helpers.inc')
    commands = read('src/editor/title-editor/commands-docks.inc')
    panels = read('src/editor/title-editor/panels-colors.inc')
    audio = read('src/editor/title-editor/editor-audio-preview.inc')
    assert 'kEditorLayoutVersion = 4' in helpers
    assert 'kEditorAudioDockObjectName' in helpers
    assert 'editor_audio_dock_' in header
    assert 'create_editor_audio_panel()' in audio
    assert 'setToolButtonStyle(Qt::ToolButtonIconOnly)' in audio
    assert 'obs_icon("headphones.svg")' in audio
    assert 'OBS_MONITORING_TYPE_MONITOR_ONLY' in audio
    assert 'OBS_MONITORING_TYPE_NONE' in audio
    assert 'obs_source_set_monitoring_type' in audio
    assert 'create_editor_dock(QString::fromUtf8(kEditorAudioDockObjectName)' in commands
    assert 'act_editor_audio_visible_' in panels
    assert (ROOT / 'data/icons/headphones.svg').exists()
