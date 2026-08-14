from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(rel):
    return (ROOT / rel).read_text(encoding='utf-8')

def test_effect_defaults_live_in_effect_header_menu_only():
    effects = read('src/effects/effects-panel.cpp')
    assert 'box->setProperty("fxmPanelDefaultsDisabled", true);' in effects
    assert 'panel->style()->standardIcon(QStyle::SP_BrowserReload), tr("Defaults")' in effects

def test_header_defaults_are_icon_tool_buttons_and_have_handlers():
    header = read('src/editor/properties-panel.h')
    assert 'QToolButton     *btn_transform_defaults_' in header
    assert 'QToolButton     *btn_shape_defaults_' in header
    popup = read('src/editor/properties-panel/popup-state.inc')
    shape = read('src/editor/properties-panel/construction-gradient-image-signals.inc')
    assert 'new QToolButton(transform_box_)' in popup
    assert 'new QToolButton(rect_box_)' in shape
    assert 'FxmPanelDefaultsButton' in popup and 'FxmPanelDefaultsButton' in shape
    defaults = read('src/editor/properties-panel/panel-defaults.inc')
    assert 'void PropertiesPanel::reset_panel_defaults' in defaults
    assert 'expose_fill_color = defaults.expose_fill_color' in defaults


def test_image_layers_have_no_scene_mask_ui_or_loaded_scene_mask():
    sync = read('src/editor/properties-panel/property-synchronization.inc')
    data = read('src/core/title-data.cpp')
    assert 'layer_->type != LayerType::Image' in sync
    assert 'l->type != LayerType::Video && l->type != LayerType::Image' in data


def test_external_media_clear_controls_exist():
    header = read('src/editor/properties-panel.h')
    assert 'btn_clear_image_' in header
    assert 'btn_audio_clear_' in header
    construction = read('src/editor/properties-panel/construction-transform-character.inc')
    popup = read('src/editor/properties-panel/popup-state.inc')
    actions = read('src/editor/properties-panel/auto-style-and-property-actions.inc')
    live_image = read('src/editor/title-dock/template-library-helpers.inc') + read('src/editor/title-dock/import-export-helpers.inc')
    assert 'Clear selected media' in construction
    assert 'Clear selected audio file' in popup
    assert 'layer_->video_source.clear()' in actions
    assert 'path_changed(QString())' in live_image


def test_fill_and_stroke_exposure_controls_and_live_color_rows():
    model = read('Core/layer-model.h')
    for token in ['expose_fill_color', 'exposed_fill_single_value', 'expose_stroke_color', 'exposed_stroke_single_value']:
        assert token in model
    props = read('src/editor/properties-panel/construction-gradient-image-signals.inc')
    assert 'Expose Fill' in props and 'Expose Stroke' in props
    actions = read('src/editor/properties-panel/auto-style-and-property-actions.inc')
    appearance = read('src/editor/properties-panel/color-gradient-editing.inc')
    assert 'Expose Fill to Dock' in actions
    assert 'Expose Stroke to Dock' in appearance
    live = read('src/editor/title-dock/live-text-cache-playlist.inc')
    assert 'QColorDialog::getColor' in live
    assert 'Reset Fill to default' in live
    assert 'Reset Stroke to default' in live


def test_combo_popup_style_and_guide_live_drag():
    controls = read('Editor/fxm-modern-controls.cpp')
    assert 'QComboBox QAbstractItemView' in controls
    assert 'setMaxVisibleItems(12)' in controls
    guides = read('src/canvas/canvas-preview/gpu-frame-rendering.inc')
    snippet = 'dragging_guide_value_ = snap_guide_value_to_objects'
    assert snippet in guides
    after = guides.split(snippet, 1)[1]
    assert 'invalidate_canvas_overlay_caches();' in after.split('return;', 1)[0]


def test_libraries_dock_stable_width_contract():
    panels = read('src/editor/title-editor/panels-colors.inc')
    commands = read('src/editor/title-editor/commands-docks.inc')
    assert 'fxmStableDockedWidth' in panels
    assert 'resizeDocks({styles_dock_, graphic_props_dock_}' in commands

def test_live_fill_stroke_visibility_does_not_depend_on_local_live_form_scope():
    sync = read('src/editor/properties-panel/property-synchronization.inc')
    forbidden = [
        'live_form->labelForField(chk_expose_fill_',
        'live_form->labelForField(chk_exposed_fill_single_value_',
        'live_form->labelForField(chk_expose_stroke_',
        'live_form->labelForField(chk_exposed_stroke_single_value_',
    ]
    for token in forbidden:
        assert token not in sync
    assert 'set_form_row_visible(chk_expose_fill_, show_fill_expose);' in sync
    assert 'set_form_row_visible(chk_exposed_fill_single_value_, show_fill_single_value);' in sync
    assert 'set_form_row_visible(chk_expose_stroke_, show_stroke_expose);' in sync
    assert 'set_form_row_visible(chk_exposed_stroke_single_value_, show_stroke_single_value);' in sync
    assert 'for (QWidget *cursor = field; cursor; cursor = cursor->parentWidget())' in sync
