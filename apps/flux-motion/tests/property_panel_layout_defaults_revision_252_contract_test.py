from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding='utf-8')


def test_generated_defaults_buttons_on_collapsible_panels():
    src = read('Editor/fxm-modern-controls.cpp')
    assert 'FxmPanelDefaultsButton' in src
    assert 'Restore this panel to defaults' in src
    assert 'reset_panel_defaults' in src
    assert 'QMetaObject::invokeMethod' in src
    assert 'fxmPanelDefaultsDisabled' in src


def test_properties_panel_has_invokable_defaults_handler():
    header = read('src/editor/properties-panel.h')
    body = read('src/editor/properties-panel/panel-defaults.inc')
    assert 'Q_INVOKABLE void reset_panel_defaults(const QString &panel_key);' in header
    assert 'void PropertiesPanel::reset_panel_defaults' in body
    for token in ['character', 'paragraph', 'live', 'image', 'video', 'audio', 'asset', 'appearance']:
        assert token in body


def test_image_source_layout_keeps_box_size_inside_form_not_third_column():
    src = read('src/editor/properties-panel/construction-transform-character.inc')
    assert 'add_form_row(image_form, QStringLiteral("Box size"), image_box_size_box_);' in src
    assert 'image_content_layout->addWidget(image_box_size_box_)' not in src
    assert 'auto *image_box_size_label = new QLabel(QStringLiteral("Box size")' not in src
    assert src.index('add_form_row(image_form, QStringLiteral("Box size"), image_box_size_box_);') < src.index('fxm_add_panel_section(vl, image_box_')


def test_live_properties_switches_are_compact_left_aligned():
    src = read('src/editor/properties-panel/construction-gradient-image-signals.inc')
    assert 'auto compact_live_field = [this](QWidget *field)' in src
    assert 'layout->addWidget(field, 0, Qt::AlignLeft | Qt::AlignVCenter);' in src
    assert 'add_form_row(live_edit_form, fxm_tr("OBSTitles.DockEditing"), compact_live_field(chk_expose_text_));' in src
