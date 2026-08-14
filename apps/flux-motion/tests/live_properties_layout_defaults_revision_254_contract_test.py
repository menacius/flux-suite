from pathlib import Path
root = Path(__file__).resolve().parents[1]

def read(path):
    return (root / path).read_text(encoding='utf-8')

cmake = read('CMakeLists.txt')
build = read('src/core/build-info.h')
modern = read('Editor/fxm-modern-controls.cpp')
modern_h = read('Editor/fxm-modern-controls.h')
construct = read('src/editor/properties-panel/construction-gradient-image-signals.inc')
sync = read('src/editor/properties-panel/property-synchronization.inc') + read('src/editor/properties-panel/selection-refresh.inc')
transform = read('src/editor/properties-panel/construction-transform-character.inc')
live_utils = read('src/core/live-text-cue-utils.h')
dock = read('src/editor/title-dock/live-text-cache-playlist.inc')
source_runtime = read('../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc')
appearance = read('src/editor/properties-panel/popup-state.inc')

assert 'OBS_FXM_DEVELOPMENT_VERSION "255"' in cmake or 'OBS_FXM_DEVELOPMENT_VERSION "254"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "255"' in build or 'FXM_DEVELOPMENT_VERSION "254"' in build
assert 'QIcon fxm_panel_defaults_icon' in modern_h
assert 'QIcon fxm_panel_defaults_icon' in modern
assert 'new QToolButton(panel)' in modern and 'new QPushButton(QObject::tr("Defaults")' not in modern
assert 'setIcon(fxm_panel_defaults_icon(panel->palette()))' in modern
assert 'SP_BrowserReload' not in construct and 'SP_BrowserReload' not in read('src/editor/properties-panel/popup-state.inc')

assert 'chk_scene_mask_->setText(QString())' in construct
assert 'chk_expose_fill_ = new FxmSwitch(QString(), inner)' in construct
assert 'add_form_row(live_edit_form, QStringLiteral("Expose Fill"), compact_live_field(chk_expose_fill_))' in construct
assert 'add_form_row(live_edit_form, QStringLiteral("Fill single value"), compact_live_field(chk_exposed_fill_single_value_))' in construct
assert 'live_edit_form->setHorizontalSpacing(12)' in construct

assert 'const bool show_fill_expose = (is_text_like || is_rect) && !is_scene_mask_layer;' in sync
assert 'layer_->use_as_scene_mask = v && layer_->type != LayerType::Video && layer_->type != LayerType::Image;' in transform
assert 'layer_->expose_fill_color = false;' in transform
assert 'layer_->expose_fill_color = v && !layer_->use_as_scene_mask;' in transform

assert 'const bool exposes_color = layer->expose_fill_color || layer->expose_stroke_color;' in live_utils
assert 'return layer->expose_text ? layer->text_content : std::string{};' in live_utils
assert 'if (!layer || layer->use_as_scene_mask)' in live_utils
assert 'const bool exposes_value = exposed[col] && exposed[col]->expose_text;' in dock
assert 'if (!edit)' in dock and 'fxm_panel_defaults_icon(palette())' in dock
assert 'if (!layer->expose_text)\n        return;' in source_runtime
assert 'appearance_grid->setHorizontalSpacing(12);' in appearance

assert 'combo->view()->setAutoFillBackground(true);' in modern
assert 'QAbstractItemView::item{min-height:22px;padding:4px 8px;background:%2;color:%1;}' in modern
