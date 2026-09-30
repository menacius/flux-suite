static QString auto_style_marker_label(const std::string &marker, size_t offset, const std::string &custom_chars)
{
    QString name;
    if (marker == "text_start") name = "start";
    else if (marker == "text_end") name = "end";
    else if (marker == "character_index" || marker == "character_count") name = "chars";
    else if (marker == "word_count") name = "words";
    else if (marker == "space") name = "space";
    else if (marker == "line_break") name = "line break";
    else if (marker == "newline") name = "new line";
    else if (marker == "paragraph_start") name = "paragraph start";
    else if (marker == "paragraph_end") name = "paragraph end";
    else if (marker == "custom_char") name = QStringLiteral("'%1'").arg(QString::fromStdString(custom_chars));
    else name = QString::fromStdString(marker);
    if (marker == "text_start" || marker == "text_end")
        return name;
    return QStringLiteral("%1[%2]").arg(name).arg((int)offset);
}


static QString auto_style_visible_token(QString token)
{
    token.replace(QStringLiteral("\\r\\n"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\n"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\r"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\t"), QStringLiteral("tab"));
    token.replace(QStringLiteral("\\x20"), QStringLiteral("space"));
    token.replace(QStringLiteral("\\u2028"), QStringLiteral("line break"));
    token.replace(QStringLiteral("\\u2029"), QStringLiteral("paragraph break"));
    token.replace(QRegularExpression(QStringLiteral("\\\\([\\[\\]{}().+*?|^$:-])")), QStringLiteral("\\1"));
    return token.trimmed();
}

static QString auto_style_rule_descriptive_name(const RichTextAutoStyleRule &rule)
{
    if (!rule.display_name.empty())
        return QString::fromStdString(rule.display_name);

    if (rule.condition_type == "regex") {
        const QString pattern = QString::fromStdString(rule.regex_pattern);
        QString beginning = (pattern.startsWith('^') || pattern.contains(QStringLiteral("(?:^|")))
            ? QStringLiteral("Paragraph beginning") : QStringLiteral("Regex match");
        const int lookahead = pattern.lastIndexOf(QStringLiteral("(?="));
        if (lookahead >= 0) {
            const int end = pattern.indexOf(')', lookahead + 3);
            if (end > lookahead) {
                const QString token = auto_style_visible_token(pattern.mid(lookahead + 3, end - lookahead - 3));
                if (!token.isEmpty())
                    return QStringLiteral("%1 to %2").arg(beginning, token);
            }
        }
        return beginning;
    }

    auto start_name = [](const RichTextAutoStyleRule &r) {
        if (r.start_condition == "paragraph_start") return QStringLiteral("Paragraph beginning");
        if (r.start_condition == "text_start") return QStringLiteral("Text beginning");
        if (r.start_condition == "newline" || r.start_condition == "line_break") return QStringLiteral("Line beginning");
        if (r.start_condition == "space") return QStringLiteral("After space");
        if (r.start_condition == "custom_char") return QStringLiteral("After %1").arg(auto_style_visible_token(QString::fromStdString(r.start_custom_chars)));
        if (r.start_condition == "character_index") return QStringLiteral("Character %1").arg((int)r.start_offset);
        if (r.start_condition == "word_count") return QStringLiteral("Word %1").arg((int)r.start_offset);
        return auto_style_marker_label(r.start_condition, r.start_offset, r.start_custom_chars);
    };
    auto end_name = [](const RichTextAutoStyleRule &r) {
        if (r.end_condition == "paragraph_end") return QStringLiteral("paragraph end");
        if (r.end_condition == "text_end") return QStringLiteral("text end");
        if (r.end_condition == "newline" || r.end_condition == "line_break") return QStringLiteral("line break");
        if (r.end_condition == "space") return QStringLiteral("space");
        if (r.end_condition == "custom_char") return auto_style_visible_token(QString::fromStdString(r.end_custom_chars));
        if (r.end_condition == "character_index" || r.end_condition == "character_count") return QStringLiteral("character %1").arg((int)(r.end_offset ? r.end_offset : r.length));
        if (r.end_condition == "word_count") return QStringLiteral("word %1").arg((int)(r.end_offset ? r.end_offset : r.length));
        return auto_style_marker_label(r.end_condition, r.end_offset ? r.end_offset : r.length, r.end_custom_chars);
    };
    return QStringLiteral("%1 to %2").arg(start_name(rule), end_name(rule));
}

static std::vector<std::string> auto_style_split_ids(const QString &text)
{
    std::vector<std::string> ids;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
    for (const QString &part : parts)
        ids.push_back(part.trimmed().toStdString());
    return ids;
}

struct PropertiesColorLibraryColor {
    QString name;
    QColor color;
};

struct PropertiesColorLibrary {
    QString name;
    std::vector<PropertiesColorLibraryColor> colors;
};

static QString properties_color_libraries_path()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/Flux Motion");
    QDir dir(base);
    dir.mkpath(QStringLiteral("palettes"));
    return dir.filePath(QStringLiteral("palettes/user-color-libraries.palette.json"));
}

static bool properties_parse_color_hex(QString text, QColor &color)
{
    text = text.trimmed();
    if (text.startsWith(QStringLiteral("#")))
        text.remove(0, 1);
    if (text.size() != 6 && text.size() != 8)
        return false;
    bool ok = false;
    const uint value = text.toUInt(&ok, 16);
    if (!ok)
        return false;
    color = text.size() == 6
        ? QColor((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF, 255)
        : QColor((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    return color.isValid();
}

static void properties_limit_gradient_stop_color_dialog(color_widgets::ColorDialog *dialog)
{
    if (!dialog)
        return;

    dialog->setAlphaEnabled(true);
    dialog->setButtonMode(color_widgets::ColorDialog::Close);
    const QStringList hidden_names = {
        QStringLiteral("wheel"), QStringLiteral("preview"), QStringLiteral("line"),
        QStringLiteral("label"), QStringLiteral("slide_red"), QStringLiteral("spin_red"),
        QStringLiteral("label_2"), QStringLiteral("slide_green"), QStringLiteral("spin_green"),
        QStringLiteral("label_3"), QStringLiteral("slide_blue"), QStringLiteral("spin_blue")
    };
    for (const QString &name : hidden_names) {
        if (auto *widget = dialog->findChild<QWidget *>(name))
            widget->hide();
    }
    if (auto *buttons = dialog->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"))) {
        buttons->show();
        for (auto *button : buttons->buttons()) {
            const bool is_picker = buttons->buttonRole(button) == QDialogButtonBox::ActionRole;
            button->setVisible(is_picker);
            if (is_picker)
                button->setToolTip(fxm_tr("OBSTitles.PickColor"));
        }
    }
    dialog->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

static std::vector<PropertiesColorLibrary> properties_load_color_libraries()
{
    std::vector<PropertiesColorLibrary> libraries;
    PropertiesColorLibrary open_color;
    open_color.name = QStringLiteral("Open Color");
    for (const auto &entry : fxm_open_color_palette())
        open_color.colors.push_back({entry.first, entry.second});
    libraries.push_back(open_color);

    QFile file(properties_color_libraries_path());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        for (const QJsonValue &value : doc.object().value(QStringLiteral("palettes")).toArray()) {
            const QJsonObject object = value.toObject();
            PropertiesColorLibrary library;
            library.name = object.value(QStringLiteral("name")).toString().trimmed();
            if (library.name.isEmpty())
                continue;
            for (const QJsonValue &color_value : object.value(QStringLiteral("colors")).toArray()) {
                const QJsonObject color_object = color_value.toObject();
                QColor color;
                if (!properties_parse_color_hex(color_object.value(QStringLiteral("hex")).toString(), color))
                    continue;
                library.colors.push_back({color_object.value(QStringLiteral("name")).toString(color.name(QColor::HexRgb)), color});
            }
            libraries.push_back(library);
        }
    }
    return libraries;
}

static QString auto_style_join_ids(const std::vector<std::string> &ids)
{
    QStringList parts;
    for (const auto &id : ids)
        if (!id.empty()) parts << QString::fromStdString(id);
    return parts.join(QStringLiteral(", "));
}

void PropertiesPanel::set_undo_redo_available(bool undo_available, bool redo_available)
{
    if (btn_properties_undo_)
        btn_properties_undo_->setEnabled(undo_available);
    if (btn_properties_redo_)
        btn_properties_redo_->setEnabled(redo_available);
}


void PropertiesPanel::register_keyframe_times(
    QAbstractButton *button, std::function<std::vector<double>()> provider)
{
    if (!button)
        return;
    keyframe_time_providers_[button] = std::move(provider);
    fxm_refresh_keyframe_navigation(button);
}

QWidget *PropertiesPanel::make_keyframe_controls(QAbstractButton *button,
                                                  QWidget *parent)
{
    return fxm_make_keyframe_controls(
        button, parent,
        [this, button]() {
            const auto found = keyframe_time_providers_.find(button);
            return found == keyframe_time_providers_.end()
                ? std::vector<double>{} : found->second();
        },
        [this]() {
            if (!layer_)
                return 0.0;
            return std::clamp(playhead_ - layer_->in_time, 0.0,
                              std::max(0.0, layer_->out_time - layer_->in_time));
        },
        [this](double local_time) {
            if (layer_)
                emit keyframe_navigation_requested(layer_->in_time + local_time);
        });
}


void PropertiesPanel::set_text_style_editor_mode(bool enabled)
{
    text_style_editor_mode_ = enabled;
    apply_text_style_editor_mode_visibility();
}

void PropertiesPanel::apply_text_style_editor_mode_visibility()
{
    if (!text_style_editor_mode_)
        return;

    auto set_section_visible = [](QWidget *section, bool visible) {
        if (!section)
            return;
        section->setVisible(visible);
        if (auto *panel = fxm_panel_for_content(section))
            panel->setVisible(visible);
    };

    /* A text style is edited through the exact same inspector sections and
     * controls as an ordinary text layer. Hide only document/runtime-specific
     * sections; do not maintain a second style-editor implementation. */
    set_section_visible(video_box_, false);
    set_section_visible(live_playback_box_, false);
    set_section_visible(audio_box_, false);
    set_section_visible(asset_box_, false);
    set_section_visible(transform_box_, false);
    set_section_visible(rect_box_, true);
    set_section_visible(image_box_, false);
    set_section_visible(text_animators_box_, false);
    set_section_visible(dynamic_text_box_, true);
    set_section_visible(auto_style_box_, false);
    set_section_visible(live_edit_box_, false);
    set_section_visible(bullets_box_, false);
    set_section_visible(outline_box_, false);
    set_section_visible(shadow_box_, false);
    set_section_visible(background_gradient_box_, false);

    set_section_visible(text_box_, true);
    set_section_visible(type_options_box_, true);
    set_section_visible(paragraph_box_, true);
    set_section_visible(appearance_box_, true);

    /* Keep the shared Text Box panel for its auto-size and maximum-size
     * controls, but remove geometry controls that are layer layout rather than
     * reusable typography. */
    for (QWidget *widget : {static_cast<QWidget *>(btn_kf_width_),
                            shape_size_label_, shape_size_field_w_,
                            shape_size_field_h_, static_cast<QWidget *>(chk_size_lock_),
                            row_shape_scale_options_, row_rect_corners_,
                            static_cast<QWidget *>(chk_corner_lock_),
                            static_cast<QWidget *>(spn_corner_bevel_roundness_),
                            static_cast<QWidget *>(btn_shape_defaults_)}) {
        if (widget)
            widget->setVisible(false);
    }
    if (grp_shape_type_)
        for (auto *button : grp_shape_type_->buttons())
            button->setVisible(false);
    if (rect_box_) {
        if (auto *shape_types = rect_box_->findChild<QWidget *>(
                QStringLiteral("OBSTitlesShapeTypeButtonsRow")))
            shape_types->setVisible(false);
    }

    /* Preview text belongs to the dialog header. Font, type, paragraph,
     * fill/stroke and every NumericDragLabel remain the shared widgets. */
    if (text_box_) {
        if (auto *grid = qobject_cast<QGridLayout *>(text_box_->layout())) {
            for (int column = 0; column < grid->columnCount(); ++column) {
                if (auto *item = grid->itemAtPosition(0, column))
                    if (auto *widget = item->widget())
                        widget->setVisible(false);
            }
        }
    }

    for (QWidget *button : {static_cast<QWidget *>(btn_kf_font_size_),
                            static_cast<QWidget *>(btn_kf_char_scale_x_),
                            static_cast<QWidget *>(btn_kf_char_scale_y_),
                            static_cast<QWidget *>(btn_kf_char_tracking_),
                            static_cast<QWidget *>(btn_kf_baseline_shift_),
                            static_cast<QWidget *>(btn_kf_text_color_),
                            static_cast<QWidget *>(btn_kf_appearance_fill_),
                            static_cast<QWidget *>(btn_kf_appearance_stroke_),
                            static_cast<QWidget *>(btn_kf_appearance_opacity_)}) {
        if (button)
            button->setVisible(false);
    }
}

PropertiesPanel::PropertiesPanel(QWidget *parent) : QScrollArea(parent)
{
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    const QPalette pal = qApp->palette();
    const QColor panel_bg = pal.color(QPalette::Window);
    const QColor panel_text = pal.color(QPalette::WindowText);
    const QColor control_bg = pal.color(QPalette::Base);
    const QColor control_text = pal.color(QPalette::Text);
    const QColor button_bg = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = panel_text.lightness() < 128 ? panel_text.lighter(135) : panel_text.darker(135);
    const QColor section_bg = panel_bg.lightness() < 128 ? panel_bg.lighter(112) : panel_bg.darker(104);
    const QColor hover_bg = button_bg.lightness() < 128 ? button_bg.lighter(122) : button_bg.darker(108);
    const QString panel_bg_name = panel_bg.name(QColor::HexRgb);
    const QString panel_text_name = panel_text.name(QColor::HexRgb);
    const QString control_bg_name = control_bg.name(QColor::HexRgb);
    const QString control_text_name = control_text.name(QColor::HexRgb);
    const QString button_bg_name = button_bg.name(QColor::HexRgb);
    const QString button_text_name = button_text.name(QColor::HexRgb);
    const QString border_name = border.name(QColor::HexRgb);
    const QString highlight_name = highlight.name(QColor::HexRgb);
    const QString highlighted_text_name = highlighted_text.name(QColor::HexRgb);
    const QString subtle_text_name = subtle_text.name(QColor::HexRgb);
    const QString section_bg_name = section_bg.name(QColor::HexRgb);
    const QString hover_bg_name = hover_bg.name(QColor::HexRgb);
    setStyleSheet(QStringLiteral("QScrollArea{background:%1;border:none;}").arg(panel_bg_name));

    auto *inner = new QWidget(this);
    inner->setObjectName(QStringLiteral("FxmLayerPropertiesContent"));
    inner->setProperty("fxmResponsivePropertyFields", true);
    inner->setStyleSheet(QStringLiteral(
        "QWidget#FxmLayerPropertiesContent{background:%1;}"
        "QWidget#FxmLayerPropertiesContent QLabel{color:%2;font-size:10px;background:transparent;}"
        "QWidget#FxmLayerPropertiesContent QDoubleSpinBox,"
        "QWidget#FxmLayerPropertiesContent QSpinBox,"
        "QWidget#FxmLayerPropertiesContent QComboBox,"
        "QWidget#FxmLayerPropertiesContent QLineEdit,"
        "QWidget#FxmLayerPropertiesContent QTextEdit{color:%3;background:%4;"
        "border:1px solid %5;border-radius:2px;padding:1px 3px;"
        "selection-background-color:%6;}"
        "QWidget#FxmLayerPropertiesContent QDoubleSpinBox:focus,"
        "QWidget#FxmLayerPropertiesContent QSpinBox:focus,"
        "QWidget#FxmLayerPropertiesContent QComboBox:focus,"
        "QWidget#FxmLayerPropertiesContent QLineEdit:focus,"
        "QWidget#FxmLayerPropertiesContent QTextEdit:focus{border-color:%6;}")
        .arg(panel_bg_name, subtle_text_name, control_text_name,
             control_bg_name, border_name, highlight_name));
    auto *vl = new QVBoxLayout(inner);
    vl->setContentsMargins(6, 5, 6, 6);
    vl->setSpacing(2);

    const QString section_style =
        QStringLiteral("QGroupBox{color:%1;background:%2;border:none;"
        "border-top:2px solid %3;margin:0;padding-top:4px;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QGroupBox::indicator{width:10px;height:10px;margin-left:2px;}"
        "QLabel{color:%4;font-size:10px;}")
            .arg(panel_text_name)
            .arg(section_bg_name)
            .arg(border_name)
            .arg(subtle_text_name);
    const QString control_style = fxm_transform_panel_control_style(pal);
    const QString menu_style =
        QStringLiteral("QMenu{color:%1;background:%2;border:1px solid %3;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:%4;color:%5;}"
        "QMenu::item:disabled{color:%6;}")
            .arg(panel_text_name, panel_bg_name, border_name, highlight_name, highlighted_text_name, subtle_text_name);
    const QString themed_dialog_style =
        QStringLiteral("QDialog{background:%1;border:1px solid %2;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}"
        "QToolButton{color:%4;background:%5;border:1px solid %2;border-radius:2px;padding:2px 6px;font-size:10px;}"
        "QToolButton:hover{background:%6;}"
        "QToolButton:checked{background:%7;border-color:%7;color:%8;}"
        "QToolButton:disabled{color:%9;background:%5;border-color:%2;}")
            .arg(panel_bg_name, border_name, panel_text_name, button_text_name, button_bg_name,
                 hover_bg_name, highlight_name, highlighted_text_name, subtle_text_name);

    auto style_form = [](QFormLayout *form) {
        form->setContentsMargins(5, 4, 5, 5);
        form->setHorizontalSpacing(4);
        form->setVerticalSpacing(2);
        form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->setFormAlignment(Qt::AlignTop);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    };

    auto add_form_row = [this, subtle_text_name](QFormLayout *form, const QString &label_text, QWidget *field) {
        if (!form || label_text.isEmpty()) {
            if (form) form->addRow(label_text, field);
            return;
        }

        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
        form->addRow(label, field);
    };

    const QString checkbox_style =
        QStringLiteral("QCheckBox{color:%1;font-size:10px;spacing:5px;}"
        "QCheckBox::indicator{width:13px;height:13px;border:1px solid %2;"
        "background:%3;border-radius:2px;}"
        "QCheckBox::indicator:hover{border-color:%2;background:%4;}"
        "QCheckBox::indicator:checked{background:%5;border-color:%5;}")
            .arg(panel_text_name, border_name, button_bg_name, hover_bg_name, highlight_name);
    const QString push_button_style =
        QStringLiteral("QPushButton{color:%1;background:%2;border:1px solid %3;"
        "border-radius:2px;font-size:10px;padding:2px 8px;}"
        "QPushButton:hover{background:%4;border-color:%3;}"
        "QPushButton:pressed{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name, highlight_name, highlighted_text_name);

    auto style_checkbox = [&](QCheckBox *box) {
        box->setFixedHeight(20);
        box->setStyleSheet(checkbox_style);
    };
    auto style_push_button = [&](QPushButton *button) {
        button->setFixedHeight(20);
        button->setStyleSheet(push_button_style);
    };

    auto mk_dspin = [&](double lo, double hi, double step) {
        auto *s = new QDoubleSpinBox(inner);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        s->setFixedHeight(20);
        s->setStyleSheet(control_style);
        return s;
    };

    auto mk_kf_button = [&](const QString &tip) {
        auto *b = new QPushButton(inner);
        b->setIcon(fxm_keyframe_diamond_icon(false));
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setProperty("active", false);
        b->setProperty("outlined", false);
        fxm_style_keyframe_button(b);
        return b;
    };

    auto with_kf = [&](QWidget *field, QPushButton *button) {
        auto *row = new QWidget(inner);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(3);
        field->setSizePolicy(QSizePolicy::Expanding, field->sizePolicy().verticalPolicy());
        hl->addWidget(field, 1);
        hl->addWidget(make_keyframe_controls(button, row), 0, Qt::AlignVCenter);
        return row;
    };


    spn_px_      = mk_dspin(-9999, 9999, 1.0);
    spn_py_      = mk_dspin(-9999, 9999, 1.0);
    spn_scale_x_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_y_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_x_->setSuffix("%");
    spn_scale_y_->setSuffix("%");
    spn_transform_size_w_ = mk_dspin(0.0, 9999.0, 10.0);
    spn_transform_size_h_ = mk_dspin(0.0, 9999.0, 10.0);
    chk_scale_lock_ = new FxmSwitch(inner);
    chk_scale_lock_->setChecked(true);
    chk_transform_size_lock_ = new FxmSwitch(inner);
    chk_transform_size_lock_->setChecked(true);
    spn_rot_     = mk_dspin(-9999,  9999,  0.5);
    spn_opacity_ = mk_dspin(0.0,   1.0,  0.01);
    spn_pz_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_rot_x_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_rot_y_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_scale_z_ = mk_dspin(-10000.0, 10000.0, 1.0);
    spn_scale_z_->setSuffix("%");
    spn_anchor_z_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_orientation_x_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_orientation_y_ = mk_dspin(-99999.0, 99999.0, 0.5);
    spn_orientation_z_ = mk_dspin(-99999.0, 99999.0, 0.5);
    chk_scene_mask_ = new FxmSwitch(fxm_tr("OBSTitles.UseAsSceneMask"), inner);
    chk_scene_mask_->setToolTip(fxm_tr("OBSTitles.SceneMaskRenderTooltip"));
    style_checkbox(chk_scene_mask_);
    chk_shape_scale_stroke_ = new FxmSwitch(fxm_tr("OBSTitles.ScaleStroke"), inner);
    chk_shape_scale_stroke_->setToolTip(fxm_tr("OBSTitles.ScaleStrokeTooltip"));
    style_checkbox(chk_shape_scale_stroke_);
    chk_shape_scale_corners_ = new FxmSwitch(fxm_tr("OBSTitles.ScaleCorners"), inner);
    chk_shape_scale_corners_->setToolTip(fxm_tr("OBSTitles.ScaleCornersTooltip"));
    style_checkbox(chk_shape_scale_corners_);
    spn_origin_x_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_y_ = mk_dspin(0.0, 1.0, 0.05);
    spn_origin_x_->setDecimals(2);
    spn_origin_y_->setDecimals(2);
    spn_origin_x_->setToolTip(fxm_tr("OBSTitles.OriginXTooltip"));
    spn_origin_y_->setToolTip(fxm_tr("OBSTitles.OriginYTooltip"));
    cmb_anchor_ = new QComboBox(inner);
    for (const QString &label : QStringList{fxm_tr("OBSTitles.TopLeft"), fxm_tr("OBSTitles.TopCenter"), fxm_tr("OBSTitles.TopRight"), fxm_tr("OBSTitles.CenterLeft"), fxm_tr("OBSTitles.Center"), fxm_tr("OBSTitles.CenterRight"), fxm_tr("OBSTitles.BottomLeft"), fxm_tr("OBSTitles.BottomCenter"), fxm_tr("OBSTitles.BottomRight")})
        cmb_anchor_->addItem(label);
    cmb_anchor_->setToolTip(fxm_tr("OBSTitles.AnchorChangeTooltip"));
    cmb_anchor_->setFixedHeight(20);
    cmb_anchor_->setStyleSheet(control_style);

    btn_kf_pos_x_ = mk_kf_button(fxm_tr("OBSTitles.ToggleXKeyframe"));
    btn_kf_pos_y_ = mk_kf_button(fxm_tr("OBSTitles.ToggleYKeyframe"));
    btn_kf_scale_x_ = mk_kf_button(fxm_tr("OBSTitles.ToggleScaleXKeyframe"));
    btn_kf_scale_y_ = mk_kf_button(fxm_tr("OBSTitles.ToggleScaleYKeyframe"));
    btn_kf_transform_size_ = mk_kf_button(fxm_tr("OBSTitles.ToggleWidthKeyframe"));
    btn_kf_transform_size_->setToolTip(fxm_tr("OBSTitles.ToggleSizeKeyframe"));
    btn_kf_rotation_ = mk_kf_button(fxm_tr("OBSTitles.ToggleRotationKeyframe"));
    btn_kf_opacity_ = mk_kf_button(fxm_tr("OBSTitles.ToggleOpacityKeyframe"));
    btn_kf_origin_x_ = mk_kf_button(fxm_tr("OBSTitles.ToggleOriginXKeyframe"));
    btn_kf_origin_y_ = mk_kf_button(fxm_tr("OBSTitles.ToggleOriginYKeyframe"));
    btn_kf_position_z_ = mk_kf_button(QStringLiteral("Toggle Position Z keyframe"));
    btn_kf_rotation_x_ = mk_kf_button(QStringLiteral("Toggle Rotation X keyframe"));
    btn_kf_rotation_y_ = mk_kf_button(QStringLiteral("Toggle Rotation Y keyframe"));
    btn_kf_scale_z_ = mk_kf_button(QStringLiteral("Toggle Scale Z keyframe"));
    btn_kf_anchor_z_ = mk_kf_button(QStringLiteral("Toggle Anchor Z keyframe"));
    btn_kf_orientation_x_ = mk_kf_button(QStringLiteral("Toggle Orientation X keyframe"));
    btn_kf_orientation_y_ = mk_kf_button(QStringLiteral("Toggle Orientation Y keyframe"));
    btn_kf_orientation_z_ = mk_kf_button(QStringLiteral("Toggle Orientation Z keyframe"));
    btn_kf_pos_y_->hide();
    btn_kf_scale_y_->hide();
    btn_kf_opacity_->hide();
    btn_kf_origin_y_->hide();
    btn_kf_position_z_->hide();
    btn_kf_rotation_x_->hide();
    btn_kf_rotation_y_->hide();
    btn_kf_scale_z_->hide();
    btn_kf_anchor_z_->hide();
    btn_kf_orientation_y_->hide();
    btn_kf_orientation_z_->hide();
    spn_opacity_->hide();
    chk_scene_mask_->hide();

    /* ── Transform ── */
    transform_box_ = new QWidget(inner);
    transform_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *tform_layout = new QVBoxLayout(transform_box_);
    tform_layout->setContentsMargins(5, 4, 5, 5);
    tform_layout->setSpacing(2);

    btn_transform_defaults_ = new QToolButton(transform_box_);
    btn_transform_defaults_->setObjectName(QStringLiteral("FxmPanelDefaultsButton"));
    btn_transform_defaults_->setAutoRaise(true);
    btn_transform_defaults_->setFixedSize(22, 20);
    btn_transform_defaults_->setIcon(fxm_panel_defaults_icon(palette()));
    btn_transform_defaults_->setIconSize(QSize(14, 14));
    btn_transform_defaults_->setToolTip(fxm_tr("OBSTitles.RestoreTransformDefaults"));
    btn_transform_defaults_->setStyleSheet(fxm_transform_panel_button_style(palette()));

    transform_grid_ = new QGridLayout();
    auto *transform_grid = transform_grid_;
    transform_grid->setContentsMargins(0, 0, 0, 0);
    transform_grid->setHorizontalSpacing(4);
    transform_grid->setVerticalSpacing(2);
    transform_grid->setColumnMinimumWidth(0, 82);
    transform_grid->setColumnMinimumWidth(1, 0);
    transform_grid->setColumnMinimumWidth(2, 0);
    transform_grid->setColumnMinimumWidth(3, 0);
    transform_grid->setColumnMinimumWidth(4, 30);
    transform_grid->setColumnMinimumWidth(5, 0);
    transform_grid->setColumnStretch(1, 1);
    transform_grid->setColumnStretch(2, 1);
    transform_grid->setColumnStretch(3, 1);
    transform_grid->setColumnStretch(5, 0);

    auto style_transform_spin = [&](QDoubleSpinBox *spin) {
        spin->setFixedHeight(20);
        spin->setMinimumWidth(0);
        spin->setMaximumWidth(QWIDGETSIZE_MAX);
        spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        spin->setPrefix(QString());
        spin->setStyleSheet(control_style);
    };
    style_transform_spin(spn_px_);
    style_transform_spin(spn_py_);
    style_transform_spin(spn_scale_x_);
    style_transform_spin(spn_scale_y_);
    style_transform_spin(spn_transform_size_w_);
    style_transform_spin(spn_transform_size_h_);
    style_transform_spin(spn_origin_x_);
    style_transform_spin(spn_origin_y_);
    style_transform_spin(spn_rot_);
    style_transform_spin(spn_pz_);
    style_transform_spin(spn_rot_x_);
    style_transform_spin(spn_rot_y_);
    style_transform_spin(spn_scale_z_);
    style_transform_spin(spn_anchor_z_);
    style_transform_spin(spn_orientation_x_);
    style_transform_spin(spn_orientation_y_);
    style_transform_spin(spn_orientation_z_);
    spn_rot_->setPrefix(QString());

    auto make_transform_field = [&](const QString &label, QDoubleSpinBox *spin, const char *icon_name = nullptr) {
        auto *field = new QWidget(transform_box_);
        field->setObjectName(QStringLiteral("OBSTitlesTransformNumericField"));
        field->setStyleSheet(QStringLiteral(
            "QWidget#OBSTitlesTransformNumericField{background:transparent;border:none;}"));
        auto *field_layout = new QHBoxLayout(field);
        field_layout->setContentsMargins(0, 0, 0, 0);
        field_layout->setSpacing(4);
        auto *field_label = new NumericDragLabel(label, spin, field,
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = true;
                                                     emit property_changed(true);
                                                 },
                                                 [this]() {
                                                     if (loading_values_) return;
                                                     numeric_label_dragging_ = false;
                                                     emit property_changed(true);
                                                 });
        field_label->setFixedWidth(14);
        field_label->setAlignment(Qt::AlignCenter);
        if (icon_name && *icon_name) {
            field_label->setPixmap(obs_icon(icon_name).pixmap(14, 14));
            field_label->setText(QString());
        }
        field_label->setStyleSheet(QStringLiteral("QLabel{color:%1;background:transparent;font-size:10px;padding:0;}").arg(subtle_text_name));
        field_layout->addWidget(field_label);
        field_layout->addWidget(spin, 1);
        field->setMinimumWidth(0);
        field->setMaximumWidth(QWIDGETSIZE_MAX);
        field->setFixedHeight(20);
        field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        return field;
    };

    QWidget *field_pos_x = make_transform_field(fxm_tr("OBSTitles.X"), spn_px_);
    QWidget *field_pos_y = make_transform_field(fxm_tr("OBSTitles.Y"), spn_py_);
    transform_position_field_z_ = make_transform_field(QStringLiteral("Z"), spn_pz_);
    const QString local_position_tooltip = QStringLiteral(
        "Stored and keyframed relative to the effective parent. Root layers "
        "therefore have identical local and world coordinates.");
    spn_px_->setToolTip(local_position_tooltip);
    spn_py_->setToolTip(local_position_tooltip);
    spn_pz_->setToolTip(local_position_tooltip);
    QWidget *field_scale_x = make_transform_field(fxm_tr("OBSTitles.W"), spn_scale_x_);
    QWidget *field_scale_y = make_transform_field(fxm_tr("OBSTitles.H"), spn_scale_y_);
    transform_scale_axis_label_x_ = field_scale_x->findChild<QLabel *>();
    transform_scale_axis_label_y_ = field_scale_y->findChild<QLabel *>();
    transform_scale_field_z_ = make_transform_field(QStringLiteral("Z"), spn_scale_z_);
    QWidget *field_transform_size_w = make_transform_field(fxm_tr("OBSTitles.W"), spn_transform_size_w_);
    QWidget *field_transform_size_h = make_transform_field(fxm_tr("OBSTitles.H"), spn_transform_size_h_);
    QWidget *field_origin_x = make_transform_field(fxm_tr("OBSTitles.X"), spn_origin_x_);
    QWidget *field_origin_y = make_transform_field(fxm_tr("OBSTitles.Y"), spn_origin_y_);
    transform_anchor_field_z_ = make_transform_field(QStringLiteral("Z"), spn_anchor_z_);
    transform_rotation_field_x_ = make_transform_field(QStringLiteral("X"), spn_rot_x_);
    transform_rotation_field_y_ = make_transform_field(QStringLiteral("Y"), spn_rot_y_);
    transform_rotation_field_z_ = make_transform_field(QStringLiteral("Z"), spn_rot_);
    transform_rotation_axis_label_ = transform_rotation_field_z_->findChild<QLabel *>();
    QWidget *field_orientation_x = make_transform_field(QStringLiteral("X"), spn_orientation_x_);
    QWidget *field_orientation_y = make_transform_field(QStringLiteral("Y"), spn_orientation_y_);
    QWidget *field_orientation_z = make_transform_field(QStringLiteral("Z"), spn_orientation_z_);

    /* Anchor preset control lives in the editor dynamic toolbar. */
    btn_anchor_grid_ = nullptr;

    chk_scale_lock_->setText(QStringLiteral("Lock Scale"));
    chk_scale_lock_->setToolTip(fxm_tr("OBSTitles.ScaleLock"));
    chk_transform_size_lock_->setText(QStringLiteral("Lock Scale"));
    chk_transform_size_lock_->setToolTip(fxm_tr("OBSTitles.LockAspectRatio"));

    const QString transform_label_style = QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name);
    auto add_transform_row = [&](int row, QPushButton *kf, const QString &label, QWidget *drag_field, QWidget *first,
                                 QWidget *middle, QWidget *second) {
        auto *text = new NumericDragLabel(label, drag_field, transform_box_,
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = true;
                                              emit property_changed(true);
                                          },
                                          [this]() {
                                              if (loading_values_) return;
                                              numeric_label_dragging_ = false;
                                              emit property_changed(true);
                                          });
        text->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        text->setStyleSheet(transform_label_style);
        transform_grid->addWidget(text, row, 0, Qt::AlignVCenter);
        transform_grid->addWidget(first, row, 1);
        if (middle) transform_grid->addWidget(middle, row, 2);
        if (second) transform_grid->addWidget(second, row, 3);
        transform_grid->addWidget(make_keyframe_controls(kf, transform_box_),
                                  row, 5, Qt::AlignRight | Qt::AlignVCenter);
        return text;
    };

    add_transform_row(0, btn_kf_pos_x_, QStringLiteral("Local Position"), field_pos_x,
                      field_pos_x, field_pos_y, nullptr);
    transform_grid->addWidget(transform_position_field_z_, 0, 3);

    transform_scale_label_ = add_transform_row(1, btn_kf_scale_x_, fxm_tr("OBSTitles.Scale"),
                                               field_scale_x, field_scale_x,
                                               field_scale_y, nullptr);
    transform_scale_field_x_ = field_scale_x;
    transform_scale_field_y_ = field_scale_y;
    transform_grid->addWidget(transform_scale_field_z_, 1, 3);

    transform_size_label_ = add_transform_row(1, btn_kf_transform_size_, fxm_tr("OBSTitles.Size"),
                                              field_transform_size_w, field_transform_size_w,
                                              field_transform_size_h, nullptr);
    transform_size_field_w_ = field_transform_size_w;
    transform_size_field_h_ = field_transform_size_h;

    auto *anchor_label = new NumericDragLabel(fxm_tr("OBSTitles.Anchor"), field_origin_x, transform_box_,
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = true;
                                                  emit property_changed(true);
                                              },
                                              [this]() {
                                                  if (loading_values_) return;
                                                  numeric_label_dragging_ = false;
                                                  emit property_changed(true);
                                              });
    anchor_label->setStyleSheet(transform_label_style);
    anchor_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    transform_grid->addWidget(anchor_label, 2, 0, Qt::AlignVCenter);
    transform_grid->addWidget(field_origin_x, 2, 1);
    transform_grid->addWidget(field_origin_y, 2, 2);
    transform_grid->addWidget(transform_anchor_field_z_, 2, 3);
    transform_grid->addWidget(make_keyframe_controls(btn_kf_origin_x_, transform_box_),
                              2, 5, Qt::AlignRight | Qt::AlignVCenter);

    add_transform_row(3, btn_kf_rotation_, fxm_tr("OBSTitles.Rotation"),
                      transform_rotation_field_z_, transform_rotation_field_x_,
                      transform_rotation_field_y_, nullptr);
    transform_grid->addWidget(transform_rotation_field_z_, 3, 3);

    transform_orientation_row_ = new QWidget(transform_box_);
    transform_orientation_row_->setMinimumWidth(0);
    transform_orientation_row_->setMaximumWidth(QWIDGETSIZE_MAX);
    transform_orientation_row_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    auto *orientation_layout = new QGridLayout(transform_orientation_row_);
    orientation_layout->setContentsMargins(0, 0, 0, 0);
    orientation_layout->setHorizontalSpacing(4);
    orientation_layout->setVerticalSpacing(2);
    orientation_layout->setColumnMinimumWidth(0, 82);
    orientation_layout->setColumnMinimumWidth(1, 0);
    orientation_layout->setColumnMinimumWidth(2, 0);
    orientation_layout->setColumnMinimumWidth(3, 0);
    orientation_layout->setColumnMinimumWidth(4, 30);
    orientation_layout->setColumnMinimumWidth(5, 0);
    orientation_layout->setColumnStretch(1, 1);
    orientation_layout->setColumnStretch(2, 1);
    orientation_layout->setColumnStretch(3, 1);
    orientation_layout->setColumnStretch(5, 0);
    auto *orientation_label = new NumericDragLabel(QStringLiteral("Orientation"), field_orientation_x,
                                                    transform_orientation_row_,
                                                    [this]() {
                                                        if (loading_values_) return;
                                                        numeric_label_dragging_ = true;
                                                        emit property_changed(true);
                                                    },
                                                    [this]() {
                                                        if (loading_values_) return;
                                                        numeric_label_dragging_ = false;
                                                        emit property_changed(true);
                                                    });
    orientation_label->setStyleSheet(transform_label_style);
    orientation_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    orientation_layout->addWidget(orientation_label, 0, 0, Qt::AlignVCenter);
    orientation_layout->addWidget(field_orientation_x, 0, 1);
    orientation_layout->addWidget(field_orientation_y, 0, 2);
    orientation_layout->addWidget(field_orientation_z, 0, 3);
    orientation_layout->addWidget(
        make_keyframe_controls(btn_kf_orientation_x_, transform_orientation_row_),
        0, 5, Qt::AlignRight | Qt::AlignVCenter);

    row_shape_scale_options_ = new QWidget(transform_box_);
    row_shape_scale_options_->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *shape_scale_options_layout = new QHBoxLayout(row_shape_scale_options_);
    shape_scale_options_layout->setContentsMargins(0, 0, 0, 0);
    shape_scale_options_layout->setSpacing(10);
    chk_scale_lock_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    chk_transform_size_lock_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    chk_shape_scale_stroke_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    chk_shape_scale_corners_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    shape_scale_options_layout->addWidget(chk_scale_lock_);
    shape_scale_options_layout->addWidget(chk_transform_size_lock_);
    shape_scale_options_layout->addWidget(chk_shape_scale_stroke_);
    shape_scale_options_layout->addWidget(chk_shape_scale_corners_);
    shape_scale_options_layout->addStretch(1);

    cmb_anchor_->hide();

    /* The AE-style 2D/3D switch lives at the end of every layer-list row.
     * Keep the combo as an internal synchronization control so older code and
     * keyboard/accessibility paths continue to share one dimension-mode signal. */
    auto *dimension_row = new QWidget(transform_box_);
    auto *dimension_layout = new QHBoxLayout(dimension_row);
    dimension_layout->setContentsMargins(0, 0, 0, 0);
    dimension_layout->setSpacing(8);
    auto *dimension_label = new QLabel(QStringLiteral("Layer Mode"), dimension_row);
    dimension_label->setStyleSheet(transform_label_style);
    cmb_dimension_mode_ = new QComboBox(dimension_row);
    cmb_dimension_mode_->addItem(QStringLiteral("2D Layer"), static_cast<int>(LayerDimensionMode::TwoD));
    cmb_dimension_mode_->addItem(QStringLiteral("3D Layer"), static_cast<int>(LayerDimensionMode::ThreeD));
    cmb_dimension_mode_->setFixedHeight(20);
    cmb_dimension_mode_->setStyleSheet(control_style);
    dimension_layout->addWidget(dimension_label);
    dimension_layout->addWidget(cmb_dimension_mode_, 1);
    dimension_row->hide();

    tform_layout->addLayout(transform_grid);
    tform_layout->addWidget(transform_orientation_row_);
    tform_layout->addWidget(row_shape_scale_options_);

    three_d_controls_ = new QWidget(transform_box_);
    auto *three_d_layout = new QGridLayout(three_d_controls_);
    three_d_layout->setContentsMargins(0, 4, 0, 0);
    three_d_layout->setHorizontalSpacing(4);
    three_d_layout->setVerticalSpacing(2);
    three_d_layout->setColumnMinimumWidth(0, 82);
    three_d_layout->setColumnStretch(2, 1);

    cmb_transform_axis_space_ = new QComboBox(three_d_controls_);
    cmb_transform_axis_space_->addItem(QStringLiteral("Local Axes"), static_cast<int>(TransformAxisSpace::Local));
    cmb_transform_axis_space_->addItem(QStringLiteral("Parent Axes"), static_cast<int>(TransformAxisSpace::Parent));
    cmb_transform_axis_space_->addItem(QStringLiteral("World Axes"), static_cast<int>(TransformAxisSpace::World));
    cmb_transform_axis_space_->setFixedHeight(20);
    cmb_transform_axis_space_->setStyleSheet(control_style);
    cmb_transform_axis_space_->setToolTip(QStringLiteral(
        "Controls the 3D gizmo axis orientation only. Transform values above "
        "are always authored in the layer's local transform relative to its "
        "effective parent."));
    auto *axis_space_label = new QLabel(QStringLiteral("Axis Space"), three_d_controls_);
    axis_space_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    three_d_layout->addWidget(axis_space_label, 0, 0);
    three_d_layout->addWidget(cmb_transform_axis_space_, 0, 1, 1, 2);

    cmb_layer_camera_ = new QComboBox(three_d_controls_);
    cmb_layer_camera_->setFixedHeight(20);
    cmb_layer_camera_->setStyleSheet(control_style);
    auto *layer_camera_label = new QLabel(QStringLiteral("Camera"), three_d_controls_);
    layer_camera_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    three_d_layout->addWidget(layer_camera_label, 1, 0);
    three_d_layout->addWidget(cmb_layer_camera_, 1, 1, 1, 2);

    cmb_depth_mode_ = new QComboBox(three_d_controls_);
    cmb_depth_mode_->addItem(fxm_tr("OBSTitles.ScenePositionZ"),
        static_cast<int>(LayerDepthMode::Automatic));
    cmb_depth_mode_->addItem(fxm_tr("OBSTitles.LayerOrder"),
        static_cast<int>(LayerDepthMode::LayerOrder));
    cmb_depth_mode_->setFixedHeight(20);
    cmb_depth_mode_->setStyleSheet(control_style);
    cmb_depth_mode_->setToolTip(fxm_tr("OBSTitles.DepthVisibilityTooltip"));
    auto *depth_mode_label = new QLabel(
        fxm_tr("OBSTitles.DepthVisibility"), three_d_controls_);
    depth_mode_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    three_d_layout->addWidget(depth_mode_label, 2, 0);
    three_d_layout->addWidget(cmb_depth_mode_, 2, 1, 1, 2);

    auto *depth_row = new QWidget(three_d_controls_);
    auto *depth_layout = new QGridLayout(depth_row);
    depth_layout->setContentsMargins(0, 0, 0, 0);
    depth_layout->setHorizontalSpacing(10);
    depth_layout->setVerticalSpacing(4);
    chk_depth_test_ = new FxmSwitch(QStringLiteral("Depth Test"), depth_row);
    chk_depth_test_->setToolTip(QStringLiteral(
        "Compare this layer against depth already written by compatible 3D layers."));
    chk_write_depth_ = new FxmSwitch(QStringLiteral("Write Depth"), depth_row);
    chk_write_depth_->setToolTip(QStringLiteral(
        "Persist this layer's visible pixels in the 3D depth buffer."));
    chk_double_sided_ = new FxmSwitch(QStringLiteral("Double-sided"), depth_row);
    chk_double_sided_->setToolTip(QStringLiteral(
        "Render both projected sides of the planar layer."));
    chk_backface_culling_ = new FxmSwitch(QStringLiteral("Backface Culling"), depth_row);
    chk_backface_culling_->setToolTip(QStringLiteral(
        "Hide the projected back side using editor/OBS-identical winding."));
    for (auto *check : {chk_depth_test_, chk_write_depth_, chk_double_sided_, chk_backface_culling_})
        style_checkbox(check);
    depth_layout->addWidget(chk_depth_test_, 0, 0);
    depth_layout->addWidget(chk_write_depth_, 0, 1);
    depth_layout->addWidget(chk_double_sided_, 1, 0);
    depth_layout->addWidget(chk_backface_culling_, 1, 1);
    three_d_layout->addWidget(depth_row, 3, 0, 1, 3);

    tform_layout->addWidget(three_d_controls_);


    fxm_add_panel_section(vl, transform_box_, fxm_tr("OBSTitles.Transform"), btn_transform_defaults_);

    /* ── Light Source: authored directly on the selected Light layer ── */
    light_options_box_ = new QWidget(inner);
    light_options_box_->setStyleSheet(
        QStringLiteral("background:%1;").arg(section_bg_name));
    auto *light_form = new QFormLayout(light_options_box_);
    style_form(light_form);

    chk_light_enabled_ = new FxmSwitch(fxm_tr("OBSTitles.LightEnabled"),
                                        light_options_box_);
    style_checkbox(chk_light_enabled_);
    add_form_row(light_form, QString(), chk_light_enabled_);

    cmb_light_type_ = new QComboBox(light_options_box_);
    cmb_light_type_->addItem(fxm_tr("OBSTitles.LightAmbient"),
                             static_cast<int>(TitleLightType::Ambient));
    cmb_light_type_->addItem(fxm_tr("OBSTitles.LightPoint"),
                             static_cast<int>(TitleLightType::Point));
    cmb_light_type_->addItem(fxm_tr("OBSTitles.LightSpot"),
                             static_cast<int>(TitleLightType::Spot));
    cmb_light_type_->addItem(fxm_tr("OBSTitles.LightParallel"),
                             static_cast<int>(TitleLightType::Parallel));
    cmb_light_type_->addItem(fxm_tr("OBSTitles.LightEnvironment"),
                             static_cast<int>(TitleLightType::Environment));
    cmb_light_type_->setFixedHeight(20);
    cmb_light_type_->setStyleSheet(control_style);
    add_form_row(light_form, fxm_tr("OBSTitles.LightType"), cmb_light_type_);

    btn_light_color_ = new QPushButton(light_options_box_);
    btn_light_color_->setFixedHeight(20);
    btn_light_color_->setToolTip(fxm_tr("OBSTitles.LightColorTooltip"));
    style_push_button(btn_light_color_);
    add_form_row(light_form, fxm_tr("OBSTitles.LightColor"), btn_light_color_);

    spn_light_intensity_ = mk_dspin(0.0, 100000.0, 1.0);
    spn_light_intensity_->setDecimals(2);
    spn_light_intensity_->setSuffix(QStringLiteral(" %"));
    spn_light_intensity_->setToolTip(
        fxm_tr("OBSTitles.LightBrightnessTooltip"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightBrightness"),
                 spn_light_intensity_);

    spn_light_source_size_ = mk_dspin(0.1, 100000.0, 0.1);
    spn_light_source_size_->setDecimals(2);
    spn_light_source_size_->setSuffix(QStringLiteral(" px"));
    spn_light_source_size_->setToolTip(
        fxm_tr("OBSTitles.LightSourceSizeTooltip"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightSourceSize"),
                 spn_light_source_size_);

    cmb_light_falloff_ = new QComboBox(light_options_box_);
    cmb_light_falloff_->addItem(fxm_tr("OBSTitles.LightFalloffNone"),
                                static_cast<int>(TitleLightFalloff::None));
    cmb_light_falloff_->addItem(fxm_tr("OBSTitles.LightFalloffLinear"),
                                static_cast<int>(TitleLightFalloff::Linear));
    cmb_light_falloff_->addItem(
        fxm_tr("OBSTitles.LightFalloffInverseSquare"),
        static_cast<int>(TitleLightFalloff::InverseSquare));
    cmb_light_falloff_->setFixedHeight(20);
    cmb_light_falloff_->setStyleSheet(control_style);
    add_form_row(light_form, fxm_tr("OBSTitles.LightFalloff"),
                 cmb_light_falloff_);

    spn_light_falloff_start_ = mk_dspin(0.0, 10000000.0, 1.0);
    spn_light_falloff_start_->setDecimals(2);
    spn_light_falloff_start_->setSuffix(QStringLiteral(" px"));
    /* Falloff Start is derived from Source Size and is intentionally not shown. */
    spn_light_falloff_start_->hide();
    spn_light_falloff_distance_ = mk_dspin(0.001, 10000000.0, 10.0);
    spn_light_falloff_distance_->setDecimals(2);
    spn_light_falloff_distance_->setSuffix(QStringLiteral(" px"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightFalloffDistance"),
                 spn_light_falloff_distance_);

    spn_light_cone_angle_ = mk_dspin(0.1, 179.0, 0.5);
    spn_light_cone_angle_->setDecimals(2);
    spn_light_cone_angle_->setSuffix(QStringLiteral("°"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightConeAngle"),
                 spn_light_cone_angle_);
    spn_light_cone_feather_ = mk_dspin(0.0, 100.0, 1.0);
    spn_light_cone_feather_->setDecimals(2);
    spn_light_cone_feather_->setSuffix(QStringLiteral(" %"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightConeFeather"),
                 spn_light_cone_feather_);

    spn_light_target_x_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_light_target_y_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    spn_light_target_z_ = mk_dspin(-1000000.0, 1000000.0, 1.0);
    row_light_target_ = new QWidget(light_options_box_);
    auto *light_target_layout = new QHBoxLayout(row_light_target_);
    light_target_layout->setContentsMargins(0, 0, 0, 0);
    light_target_layout->setSpacing(3);
    const std::array<std::pair<const char *, QDoubleSpinBox *>, 3>
        light_target_components{{
            {"X", spn_light_target_x_}, {"Y", spn_light_target_y_},
            {"Z", spn_light_target_z_}}};
    for (const auto &component : light_target_components) {
        auto *axis = new QLabel(QString::fromLatin1(component.first),
                                row_light_target_);
        axis->setAlignment(Qt::AlignCenter);
        axis->setFixedWidth(10);
        light_target_layout->addWidget(axis);
        light_target_layout->addWidget(component.second, 1);
    }
    add_form_row(light_form, fxm_tr("OBSTitles.LightTarget"),
                 row_light_target_);

    chk_light_casts_shadows_ = new FxmSwitch(
        fxm_tr("OBSTitles.LightCastsShadows"), light_options_box_);
    style_checkbox(chk_light_casts_shadows_);
    add_form_row(light_form, QString(), chk_light_casts_shadows_);
    spn_light_shadow_darkness_ = mk_dspin(0.0, 100.0, 1.0);
    spn_light_shadow_darkness_->setDecimals(2);
    spn_light_shadow_darkness_->setSuffix(QStringLiteral(" %"));
    add_form_row(light_form, fxm_tr("OBSTitles.LightShadowDarkness"),
                 spn_light_shadow_darkness_);
    spn_light_shadow_softness_ = mk_dspin(0.0, 32.0, 0.5);
    spn_light_shadow_softness_->setDecimals(2);
    add_form_row(light_form, fxm_tr("OBSTitles.LightShadowSoftness"),
                 spn_light_shadow_softness_);
    spn_light_shadow_bias_ = mk_dspin(0.0, 0.25, 0.0001);
    spn_light_shadow_bias_->setDecimals(6);
    add_form_row(light_form, fxm_tr("OBSTitles.LightShadowBias"),
                 spn_light_shadow_bias_);

    fxm_add_panel_section(vl, light_options_box_,
                          fxm_tr("OBSTitles.LightProperties"));

    auto light_property_changed = [this](bool commit) {
        if (loading_values_ || !layer_ || layer_->type != LayerType::Light)
            return;
        emit property_changed(commit);
    };
    connect(chk_light_enabled_, &QCheckBox::toggled, this,
            [this, light_property_changed](bool enabled) {
                if (loading_values_ || !layer_ ||
                    layer_->type != LayerType::Light)
                    return;
                layer_->light.enabled = enabled;
                light_property_changed(true);
            });
    connect(cmb_light_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, light_property_changed](int index) {
                if (loading_values_ || !layer_ ||
                    layer_->type != LayerType::Light || index < 0)
                    return;
                layer_->light.type = static_cast<TitleLightType>(
                    cmb_light_type_->itemData(index).toInt());
                load_values();
                light_property_changed(true);
            });
    connect(cmb_light_falloff_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, light_property_changed](int index) {
                if (loading_values_ || !layer_ ||
                    layer_->type != LayerType::Light || index < 0)
                    return;
                layer_->light.falloff = static_cast<TitleLightFalloff>(
                    cmb_light_falloff_->itemData(index).toInt());
                load_values();
                light_property_changed(true);
            });
    connect(chk_light_casts_shadows_, &QCheckBox::toggled, this,
            [this, light_property_changed](bool enabled) {
                if (loading_values_ || !layer_ ||
                    layer_->type != LayerType::Light)
                    return;
                layer_->light.casts_shadows = enabled;
                load_values();
                light_property_changed(true);
            });
    auto connect_light_scalar =
        [this, light_property_changed](
            QDoubleSpinBox *spin, AnimatedProperty TitleLight::*member) {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, member, light_property_changed](double value) {
                        if (loading_values_ || !layer_ ||
                            layer_->type != LayerType::Light)
                            return;
                        set_animated_value(layer_->light.*member, playhead_,
                                           value);
                        light_property_changed(!numeric_label_dragging_);
                    });
        };
    connect_light_scalar(spn_light_intensity_, &TitleLight::intensity);
    connect_light_scalar(spn_light_source_size_, &TitleLight::source_size);
    connect_light_scalar(spn_light_falloff_distance_,
                         &TitleLight::falloff_distance);
    connect_light_scalar(spn_light_cone_angle_, &TitleLight::cone_angle);
    connect_light_scalar(spn_light_cone_feather_, &TitleLight::cone_feather);
    connect_light_scalar(spn_light_shadow_darkness_,
                         &TitleLight::shadow_darkness);
    connect_light_scalar(spn_light_shadow_softness_,
                         &TitleLight::shadow_softness);
    connect_light_scalar(spn_light_shadow_bias_, &TitleLight::shadow_bias);

    auto connect_light_target =
        [this, light_property_changed](QDoubleSpinBox *spin, int component) {
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, component, light_property_changed](double value) {
                        if (loading_values_ || !layer_ ||
                            layer_->type != LayerType::Light)
                            return;
                        Vec3Value target =
                            layer_->light.target.evaluate(playhead_);
                        if (component == 0) target.x = value;
                        else if (component == 1) target.y = value;
                        else target.z = value;
                        set_animated_value(layer_->light.target, playhead_,
                                           target);
                        light_property_changed(!numeric_label_dragging_);
                    });
        };
    connect_light_target(spn_light_target_x_, 0);
    connect_light_target(spn_light_target_y_, 1);
    connect_light_target(spn_light_target_z_, 2);

    connect(btn_light_color_, &QPushButton::clicked, this, [this]() {
        if (loading_values_ || !layer_ || layer_->type != LayerType::Light)
            return;
        const QColor initial = QColor::fromRgba(static_cast<QRgb>(
            evaluated_light_color(layer_->light, playhead_)));
        const QColor selected = QColorDialog::getColor(
            initial, this, fxm_tr("OBSTitles.LightColorDialog"),
            QColorDialog::ShowAlphaChannel |
                QColorDialog::DontUseNativeDialog);
        if (!selected.isValid())
            return;
        set_animated_value(layer_->light.color_a, playhead_, selected.alpha());
        set_animated_value(layer_->light.color_r, playhead_, selected.red());
        set_animated_value(layer_->light.color_g, playhead_, selected.green());
        set_animated_value(layer_->light.color_b, playhead_, selected.blue());
        load_values();
        emit property_changed(true);
    });

    /* ── Material Options: standard property rows, scrubbable labels and keyframes ── */
    material_options_box_ = new QWidget(inner);
    material_options_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *material_form = new QFormLayout(material_options_box_);
    style_form(material_form);

    auto make_option_kf_row = [&](QPushButton *&button, const QString &tip,
                                  QWidget *control, QWidget *parent) {
        auto *row = new QWidget(parent);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        button = mk_kf_button(tip);
        layout->addWidget(control, 1);
        layout->addWidget(make_keyframe_controls(button, row), 0, Qt::AlignVCenter);
        return row;
    };
    auto add_scrubbable_option_row = [&](QFormLayout *form, const QString &label_text,
                                         QWidget *field, QWidget *drag_target) {
        auto *label = new NumericDragLabel(label_text, drag_target, form->parentWidget(),
            [this]() {
                if (loading_values_) return;
                numeric_label_dragging_ = true;
                emit property_changed(true);
            },
            [this]() {
                if (loading_values_) return;
                numeric_label_dragging_ = false;
                emit property_changed(true);
            });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(transform_label_style);
        form->addRow(label, field);
    };

    auto *material_switches = new QWidget(material_options_box_);
    auto *material_switch_layout = new QGridLayout(material_switches);
    material_switch_layout->setContentsMargins(0, 0, 0, 0);
    material_switch_layout->setHorizontalSpacing(10);
    material_switch_layout->setVerticalSpacing(3);
    chk_material_accepts_lights_ = new FxmSwitch(QStringLiteral("Accepts Lights"), material_switches);
    chk_material_casts_shadows_ = new FxmSwitch(QStringLiteral("Casts Shadows"), material_switches);
    chk_material_accepts_shadows_ = new FxmSwitch(QStringLiteral("Accepts Shadows"), material_switches);
    chk_material_reflections_ = new FxmSwitch(QStringLiteral("In Reflections"), material_switches);
    chk_material_accepts_lights_->setToolTip(QStringLiteral("Let scene lights shade this layer."));
    chk_material_casts_shadows_->setToolTip(QStringLiteral("Let this layer cast shadows."));
    chk_material_accepts_shadows_->setToolTip(QStringLiteral("Let this layer receive shadows."));
    chk_material_reflections_->setToolTip(QStringLiteral("Include this layer in environment/reflection lighting."));
    for (auto *check : {chk_material_accepts_lights_, chk_material_casts_shadows_,
                        chk_material_accepts_shadows_, chk_material_reflections_})
        style_checkbox(check);
    material_switch_layout->addWidget(chk_material_accepts_lights_, 0, 0);
    material_switch_layout->addWidget(chk_material_casts_shadows_, 0, 1);
    material_switch_layout->addWidget(chk_material_accepts_shadows_, 1, 0);
    material_switch_layout->addWidget(chk_material_reflections_, 1, 1);
    material_form->addRow(material_switches);

    auto configure_material_spin = [](QDoubleSpinBox *spin, int decimals,
                                      const QString &tooltip) {
        spin->setDecimals(decimals);
        spin->setMinimumWidth(72);
        spin->setToolTip(tooltip);
    };
    spn_material_ambient_ = mk_dspin(0.0, 4.0, 0.05);
    spn_material_diffuse_ = mk_dspin(0.0, 4.0, 0.05);
    spn_material_specular_ = mk_dspin(0.0, 4.0, 0.05);
    spn_material_shininess_ = mk_dspin(1.0, 512.0, 1.0);
    spn_material_metallic_ = mk_dspin(0.0, 1.0, 0.05);
    spn_material_roughness_ = mk_dspin(0.02, 1.0, 0.05);
    spn_material_reflection_ = mk_dspin(0.0, 4.0, 0.05);
    spn_material_emissive_intensity_ = mk_dspin(0.0, 16.0, 0.1);
    configure_material_spin(spn_material_ambient_, 2, QStringLiteral("Ambient response multiplier."));
    configure_material_spin(spn_material_diffuse_, 2, QStringLiteral("Diffuse response multiplier."));
    configure_material_spin(spn_material_specular_, 2, QStringLiteral("Specular response multiplier."));
    configure_material_spin(spn_material_shininess_, 1, QStringLiteral("Specular exponent."));
    configure_material_spin(spn_material_metallic_, 2, QStringLiteral("0 = dielectric, 1 = metallic."));
    configure_material_spin(spn_material_roughness_, 2, QStringLiteral("Micro-surface roughness."));
    configure_material_spin(spn_material_reflection_, 2, QStringLiteral("Environment reflection intensity."));
    configure_material_spin(spn_material_emissive_intensity_, 2, QStringLiteral("Self-illumination intensity."));

    auto add_material_property = [&](const QString &label, QDoubleSpinBox *spin,
                                     QPushButton *&button, const QString &tip) {
        QWidget *row = make_option_kf_row(button, tip, spin, material_options_box_);
        add_scrubbable_option_row(material_form, label, row, spin);
    };
    add_material_property(QStringLiteral("Ambient"), spn_material_ambient_,
                          btn_kf_material_ambient_, QStringLiteral("Toggle Ambient keyframe"));
    add_material_property(QStringLiteral("Diffuse"), spn_material_diffuse_,
                          btn_kf_material_diffuse_, QStringLiteral("Toggle Diffuse keyframe"));
    add_material_property(QStringLiteral("Specular"), spn_material_specular_,
                          btn_kf_material_specular_, QStringLiteral("Toggle Specular keyframe"));
    add_material_property(QStringLiteral("Shininess"), spn_material_shininess_,
                          btn_kf_material_shininess_, QStringLiteral("Toggle Shininess keyframe"));
    add_material_property(QStringLiteral("Metallic"), spn_material_metallic_,
                          btn_kf_material_metallic_, QStringLiteral("Toggle Metallic keyframe"));
    add_material_property(QStringLiteral("Roughness"), spn_material_roughness_,
                          btn_kf_material_roughness_, QStringLiteral("Toggle Roughness keyframe"));
    add_material_property(QStringLiteral("Reflection"), spn_material_reflection_,
                          btn_kf_material_reflection_, QStringLiteral("Toggle Reflection keyframe"));
    fxm_add_panel_section(vl, material_options_box_, QStringLiteral("Material Options"));

    /* ── Geometry Options ── */
    geometry_options_box_ = new QWidget(inner);
    geometry_options_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *geometry_form = new QFormLayout(geometry_options_box_);
    style_form(geometry_form);
    chk_geometry_extrusion_ = new FxmSwitch(QStringLiteral("Extrusion / Bevel"), geometry_options_box_);
    style_checkbox(chk_geometry_extrusion_);
    geometry_form->addRow(chk_geometry_extrusion_);

    spn_geometry_extrusion_depth_ = mk_dspin(0.0, 100000.0, 1.0);
    spn_geometry_extrusion_depth_->setDecimals(2);
    spn_geometry_extrusion_depth_->setSuffix(QStringLiteral(" px"));
    spn_geometry_bevel_depth_ = mk_dspin(0.0, 100000.0, 0.5);
    spn_geometry_bevel_depth_->setDecimals(2);
    spn_geometry_bevel_depth_->setSuffix(QStringLiteral(" px"));
    spn_geometry_bevel_segments_ = new QSpinBox(geometry_options_box_);
    spn_geometry_bevel_segments_->setRange(1, 16);
    spn_geometry_bevel_segments_->setFixedHeight(20);
    spn_geometry_bevel_segments_->setStyleSheet(control_style);
    spn_geometry_extrusion_segments_ = new QSpinBox(geometry_options_box_);
    spn_geometry_extrusion_segments_->setRange(1, 64);
    spn_geometry_extrusion_segments_->setFixedHeight(20);
    spn_geometry_extrusion_segments_->setStyleSheet(control_style);

    QWidget *extrusion_depth_row = make_option_kf_row(
        btn_kf_geometry_extrusion_depth_, QStringLiteral("Toggle Extrusion Depth keyframe"),
        spn_geometry_extrusion_depth_, geometry_options_box_);
    add_scrubbable_option_row(geometry_form, QStringLiteral("Extrusion Depth"),
                              extrusion_depth_row, spn_geometry_extrusion_depth_);
    add_scrubbable_option_row(geometry_form, QStringLiteral("Body Segments"),
                              spn_geometry_extrusion_segments_, spn_geometry_extrusion_segments_);
    QWidget *bevel_depth_row = make_option_kf_row(
        btn_kf_geometry_bevel_depth_, QStringLiteral("Toggle Bevel Depth keyframe"),
        spn_geometry_bevel_depth_, geometry_options_box_);
    add_scrubbable_option_row(geometry_form, QStringLiteral("Bevel Depth"),
                              bevel_depth_row, spn_geometry_bevel_depth_);
    add_scrubbable_option_row(geometry_form, QStringLiteral("Bevel Segments"),
                              spn_geometry_bevel_segments_, spn_geometry_bevel_segments_);
    chk_geometry_bevel_front_ = new FxmSwitch(QStringLiteral("Front Bevel"), geometry_options_box_);
    chk_geometry_bevel_back_ = new FxmSwitch(QStringLiteral("Back Bevel"), geometry_options_box_);
    style_checkbox(chk_geometry_bevel_front_);
    style_checkbox(chk_geometry_bevel_back_);
    auto *bevel_sides = new QWidget(geometry_options_box_);
    auto *bevel_sides_layout = new QHBoxLayout(bevel_sides);
    bevel_sides_layout->setContentsMargins(0, 0, 0, 0);
    bevel_sides_layout->setSpacing(10);
    bevel_sides_layout->addWidget(chk_geometry_bevel_front_);
    bevel_sides_layout->addWidget(chk_geometry_bevel_back_);
    bevel_sides_layout->addStretch(1);
    geometry_form->addRow(bevel_sides);
    /* Development Version 330: retain the widgets only as an internal bridge
     * for loading old documents. The alpha-slice implementation is not real
     * mesh geometry and is no longer user-facing or part of production output.
     * A new Geometry section will return with the retained-mesh renderer. */
    geometry_options_box_->setVisible(false);

    /* ── Video Layer ── */
    video_box_ = new QGroupBox(QStringLiteral("Video Timing & Loading"), inner);
    video_box_->setStyleSheet(section_style);
    auto *video_form = new QFormLayout(video_box_);
    style_form(video_form);
    /* Video Range is intentionally compact: source path, preview copy, set buttons
     * and feedback text are omitted from the inspector. The two spin boxes keep
     * their In/Out labels as in-field prefixes and stay on one row, matching the
     * compact Transform-property style. */
    spn_video_in_ = mk_dspin(0.0, 86400.0, 0.01);
    spn_video_out_ = mk_dspin(0.0, 86400.0, 0.01);
    spn_video_in_->setDecimals(2);
    spn_video_out_->setDecimals(2);
    spn_video_in_->setPrefix(QStringLiteral("In "));
    spn_video_out_->setPrefix(QStringLiteral("Out "));
    spn_video_in_->setSuffix(QStringLiteral(" s"));
    spn_video_out_->setSuffix(QStringLiteral(" s"));
    auto *video_range_row = new QWidget(video_box_);
    auto *video_range_layout = new QHBoxLayout(video_range_row);
    video_range_layout->setContentsMargins(0, 0, 0, 0);
    video_range_layout->setSpacing(5);
    spn_video_in_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spn_video_out_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    video_range_layout->addWidget(spn_video_in_, 1);
    video_range_layout->addWidget(spn_video_out_, 1);
    add_form_row(video_form, QStringLiteral("Range"), video_range_row);

    lbl_video_loading_status_ = new QLabel(QStringLiteral("Waiting for first video frame"), video_box_);
    lbl_video_loading_status_->setWordWrap(true);
    bar_video_loading_ = new QProgressBar(video_box_);
    bar_video_loading_->setRange(0, 100);
    bar_video_loading_->setTextVisible(true);
    bar_video_loading_->setFixedHeight(18);
    add_form_row(video_form, QStringLiteral("Loading"), lbl_video_loading_status_);
    add_form_row(video_form, QString(), bar_video_loading_);

    chk_video_time_remap_ = new FxmSwitch(QStringLiteral("Enable Time Remap"), video_box_);
    add_form_row(video_form, QStringLiteral("Time Remap"), chk_video_time_remap_);

    cmb_video_interpolation_ = new QComboBox(video_box_);
    cmb_video_interpolation_->addItem(QStringLiteral("Nearest frame"), (int)VideoFrameInterpolationMode::NearestFrame);
    cmb_video_interpolation_->addItem(QStringLiteral("Frame blend"), (int)VideoFrameInterpolationMode::FrameBlend);
    cmb_video_interpolation_->addItem(QStringLiteral("Motion compensated"), (int)VideoFrameInterpolationMode::MotionCompensated);
    cmb_video_interpolation_->setFixedHeight(20);
    cmb_video_interpolation_->setStyleSheet(control_style);
    add_form_row(video_form, QStringLiteral("Interpolation"), cmb_video_interpolation_);

    cmb_video_time_remap_audio_ = new QComboBox(video_box_);
    cmb_video_time_remap_audio_->addItem(QStringLiteral("Preserve linear clip audio"), (int)VideoTimeRemapAudioMode::PreserveLinearClipAudio);
    cmb_video_time_remap_audio_->addItem(QStringLiteral("Follow source time"), (int)VideoTimeRemapAudioMode::FollowSourceTime);
    cmb_video_time_remap_audio_->addItem(QStringLiteral("Mute reverse/freeze"), (int)VideoTimeRemapAudioMode::MuteReverseOrFreeze);
    cmb_video_time_remap_audio_->setFixedHeight(20);
    cmb_video_time_remap_audio_->setStyleSheet(control_style);
    add_form_row(video_form, QStringLiteral("Remap Audio"), cmb_video_time_remap_audio_);

    chk_video_optical_flow_ = new FxmSwitch(QStringLiteral("Background optical-flow analysis"), video_box_);
    add_form_row(video_form, QStringLiteral("Optical Flow"), chk_video_optical_flow_);
    lbl_video_optical_flow_status_ = new QLabel(QStringLiteral("Draft preview until analysis is ready"), video_box_);
    lbl_video_optical_flow_status_->setWordWrap(true);
    add_form_row(video_form, QStringLiteral("Analysis"), lbl_video_optical_flow_status_);
    video_box_->setVisible(false);
    fxm_add_panel_section(vl, video_box_);

    /* ── Live Playback transport ──
     * Development Version 250: live/runtime controls must live in one single
     * inspector panel.  Create the Video playback combo early so the existing
     * signal wiring below stays intact, but do not expose a standalone
     * "Live Playback" panel.  The control is reparented into Live Properties
     * when that panel is constructed. */
    live_playback_box_ = nullptr;
    cmb_video_playback_ = new QComboBox(inner);
    cmb_video_playback_->addItem(fxm_tr("OBSTitles.AssetPlaybackSynchronized"), 0);
    cmb_video_playback_->addItem(fxm_tr("OBSTitles.AssetPlaybackIndependent"), 1);
    cmb_video_playback_->setFixedHeight(20);
    cmb_video_playback_->setStyleSheet(control_style);

    auto video_changed = [this](bool commit = false) {
        if (loading_values_ || !title_ || !layer_ || layer_->type != LayerType::Video)
            return;
        synchronize_video_audio_streams(*title_, layer_->id);
        emit audio_property_changed(commit);
        emit property_changed(commit);
    };
    connect(spn_video_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, video_changed](double v) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                set_layer_media_range_in_point(*layer_, v);
                {
                    QSignalBlocker blockIn(spn_video_in_);
                    QSignalBlocker blockOut(spn_video_out_);
                    spn_video_in_->setValue(layer_->video_in_point);
                    spn_video_out_->setValue(layer_->video_out_point);
                }
                video_changed(false);
            });
    connect(spn_video_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, video_changed](double v) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                set_layer_media_range_out_point(*layer_, v);
                {
                    QSignalBlocker blockIn(spn_video_in_);
                    QSignalBlocker blockOut(spn_video_out_);
                    spn_video_in_->setValue(layer_->video_in_point);
                    spn_video_out_->setValue(layer_->video_out_point);
                }
                video_changed(false);
            });
    connect(cmb_video_playback_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, video_changed](int) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                layer_->video_playback_mode = cmb_video_playback_->currentData().toInt();
                layer_->audio_independent = layer_->video_playback_mode == 1;
                if (chk_audio_independent_) {
                    QSignalBlocker block(chk_audio_independent_);
                    chk_audio_independent_->setChecked(layer_->audio_independent);
                }
                video_changed(true);
            });
    connect(chk_video_time_remap_, &QCheckBox::toggled,
            this, [this, video_changed](bool enabled) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                layer_->video_time_remap_enabled = enabled;
                if (enabled && layer_->video_source_time.keyframes.empty()) {
                    layer_->video_source_time.static_value = layer_->video_in_point;
                    layer_->video_source_time.keyframes = {
                        Keyframe{0.0, layer_->video_in_point, EasingType::Linear},
                        Keyframe{std::max(0.0, layer_->video_out_point - layer_->video_in_point),
                                 layer_->video_out_point, EasingType::Linear}
                    };
                }
                layer_->video_time_remap_curve_fingerprint = fxm::video::time_remap_curve_fingerprint_for_layer(*layer_);
                layer_->video_optical_flow_cache_fingerprint.clear();
                fxm::video::FrameRuntime::instance().cancel_optical_flow_analysis_for_layer(layer_->id);
                video_changed(true);
            });
    connect(cmb_video_interpolation_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, video_changed](int) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                layer_->video_frame_interpolation =
                    (VideoFrameInterpolationMode)cmb_video_interpolation_->currentData().toInt();
                if (layer_->video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated &&
                    layer_->video_optical_flow_enabled)
                    fxm::video::FrameRuntime::instance().ensure_optical_flow_analysis_for_layer(*layer_);
                video_changed(true);
            });
    connect(cmb_video_time_remap_audio_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this, video_changed](int) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                layer_->video_time_remap_audio_mode =
                    (VideoTimeRemapAudioMode)cmb_video_time_remap_audio_->currentData().toInt();
                video_changed(true);
            });
    connect(chk_video_optical_flow_, &QCheckBox::toggled,
            this, [this, video_changed](bool enabled) {
                if (!layer_ || loading_values_ || layer_->type != LayerType::Video) return;
                layer_->video_optical_flow_enabled = enabled;
                layer_->video_optical_flow_cache_fingerprint.clear();
                if (enabled && layer_->video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated)
                    fxm::video::FrameRuntime::instance().ensure_optical_flow_analysis_for_layer(*layer_);
                else
                    fxm::video::FrameRuntime::instance().cancel_optical_flow_analysis_for_layer(layer_->id);
                video_changed(true);
            });
    /* ── Audio Layer ── */
    audio_box_ = new QGroupBox(fxm_tr("OBSTitles.AudioLayer"), inner);
    audio_box_->setStyleSheet(section_style);
    auto *audio_form = new QFormLayout(audio_box_);
    style_form(audio_form);
    edt_audio_source_ = new QLineEdit(audio_box_);
    edt_audio_source_->setReadOnly(true);
    edt_audio_source_->setFixedHeight(20);
    edt_audio_source_->setStyleSheet(control_style);
    btn_audio_browse_ = new QToolButton(audio_box_);
    btn_audio_browse_->setText(QStringLiteral("…"));
    btn_audio_browse_->setToolTip(QStringLiteral("Choose replacement audio file"));
    btn_audio_clear_ = new QToolButton(audio_box_);
    btn_audio_clear_->setText(QStringLiteral("×"));
    btn_audio_clear_->setToolTip(QStringLiteral("Clear selected audio file"));
    btn_audio_clear_->setFixedSize(24, 22);
    btn_audio_clear_->setStyleSheet(fxm_transform_panel_button_style(palette()));
    auto *source_row = new QWidget(audio_box_);
    auto *source_layout = new QHBoxLayout(source_row);
    source_layout->setContentsMargins(0,0,0,0); source_layout->setSpacing(4);
    source_layout->addWidget(edt_audio_source_, 1);
    source_layout->addWidget(btn_audio_browse_);
    source_layout->addWidget(btn_audio_clear_);

    spn_audio_in_ = mk_dspin(0.0, 86400.0, 0.01); spn_audio_in_->setSuffix(QStringLiteral(" s"));
    spn_audio_out_ = mk_dspin(0.0, 86400.0, 0.01); spn_audio_out_->setSuffix(QStringLiteral(" s"));
    btn_audio_set_in_ = new QPushButton(QStringLiteral("Set In"), audio_box_);
    btn_audio_set_out_ = new QPushButton(QStringLiteral("Set Out"), audio_box_);
    btn_audio_set_in_->setToolTip(QStringLiteral("Set the audio media In point from the current editor playhead."));
    btn_audio_set_out_->setToolTip(QStringLiteral("Set the audio media Out point from the current editor playhead."));
    lbl_audio_range_preview_ = new QLabel(audio_box_);
    lbl_audio_range_preview_->setWordWrap(true);
    lbl_audio_range_preview_->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(panel_text_name));
    lbl_audio_range_feedback_ = new QLabel(audio_box_);
    lbl_audio_range_feedback_->setWordWrap(true);
    lbl_audio_range_feedback_->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(subtle_text_name));
    auto make_signed_audio_spin = [&](double minimum, double maximum,
                                      double step, int decimals) {
        auto *spin = new SignedAudioSpinBox(audio_box_);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
        spin->setFixedHeight(20);
        /* The inspector's responsive normalizer gives ordinary spin boxes an
         * Ignored horizontal size policy. In a slider row that lets the slider
         * compress the numeric editor to zero pixels. These two authored input
         * fields must remain directly visible and editable at every dock width. */
        spin->setProperty("fxmSkipTransformPanelMetrics", true);
        spin->setFixedWidth(88);
        spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        spin->setAlignment(Qt::AlignRight);
        spin->setStyleSheet(control_style);
        return spin;
    };
    spn_audio_volume_ = make_signed_audio_spin(kAudioMinimumDb,
        kAudioMaximumDb, 0.1, 1);
    spn_audio_volume_->setObjectName(QStringLiteral("audioVolumeDbInput"));
    spn_audio_volume_->setAccessibleName(QStringLiteral("Volume in decibels"));
    spn_audio_volume_->setToolTip(QStringLiteral(
        "Enter Volume from -96.0 dB to +12.0 dB"));
    spn_audio_volume_->setSuffix(QStringLiteral(" dB"));
    spn_audio_pan_ = make_signed_audio_spin(-100.0, 100.0, 1.0, 0);
    spn_audio_pan_->setObjectName(QStringLiteral("audioPanInput"));
    spn_audio_pan_->setAccessibleName(QStringLiteral("Audio pan"));
    spn_audio_pan_->setToolTip(QStringLiteral(
        "Enter Pan from -100 (left) through 0 (center) to +100 (right)"));
    spn_audio_fade_in_ = mk_dspin(0.0, 60.0, 0.01); spn_audio_fade_in_->setSuffix(QStringLiteral(" s"));
    spn_audio_fade_out_ = mk_dspin(0.0, 60.0, 0.01); spn_audio_fade_out_->setSuffix(QStringLiteral(" s"));
    sld_audio_volume_ = new QSlider(Qt::Horizontal, audio_box_);
    sld_audio_volume_->setRange(audio_db_to_slider(kAudioMinimumDb),
                                audio_db_to_slider(kAudioMaximumDb));
    sld_audio_pan_ = new QSlider(Qt::Horizontal, audio_box_); sld_audio_pan_->setRange(-100, 100);
    btn_kf_audio_volume_ = new QToolButton(audio_box_); btn_kf_audio_volume_->setIcon(fxm_keyframe_diamond_icon(false));
    btn_kf_audio_pan_ = new QToolButton(audio_box_); btn_kf_audio_pan_->setIcon(fxm_keyframe_diamond_icon(false));
    for (auto *b : {btn_kf_audio_volume_, btn_kf_audio_pan_}) {
        b->setAutoRaise(true); b->setToolTip(QStringLiteral("Toggle keyframe at current time"));
        fxm_style_keyframe_button(b);
    }
    auto make_audio_pair_row = [this, panel_text_name](const QString &first_label, QDoubleSpinBox *first,
                                                               const QString &second_label, QDoubleSpinBox *second) {
        auto *row = new QWidget(audio_box_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        auto make_drag_label = [this, panel_text_name, row](const QString &text, QDoubleSpinBox *spin) {
            auto *label = new NumericDragLabel(text, spin, row,
                [this]() {
                    if (loading_values_) return;
                    numeric_label_dragging_ = true;
                    emit audio_property_changed(false);
                },
                [this]() {
                    if (loading_values_) return;
                    numeric_label_dragging_ = false;
                    emit audio_property_changed(true);
                });
            label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            label->setStyleSheet(QStringLiteral("color:%1;font-size:11px;background:transparent;").arg(panel_text_name));
            return label;
        };
        layout->addWidget(make_drag_label(first_label, first));
        layout->addWidget(first, 1);
        layout->addWidget(make_drag_label(second_label, second));
        layout->addWidget(second, 1);
        return row;
    };
    auto make_slider_row = [this](QWidget *numeric, QWidget *slider, QAbstractButton *kf) {
        auto *row = new QWidget(audio_box_); auto *l = new QHBoxLayout(row);
        l->setContentsMargins(0,0,0,0); l->setSpacing(5);
        l->addWidget(slider, 1);
        l->addWidget(numeric, 0, Qt::AlignRight | Qt::AlignVCenter);
        l->addWidget(make_keyframe_controls(kf, row)); return row;
    };

    cmb_audio_fade_curve_ = new QComboBox(audio_box_);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Linear"), (int)AudioFadeCurve::Linear);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Smooth"), (int)AudioFadeCurve::Smooth);
    cmb_audio_fade_curve_->addItem(QStringLiteral("Equal Power"), (int)AudioFadeCurve::EqualPower);
    cmb_audio_fade_curve_->setFixedHeight(20);
    cmb_audio_fade_curve_->setStyleSheet(control_style);
    cmb_audio_playback_ = new QComboBox(audio_box_);
    cmb_audio_playback_->addItem(QStringLiteral("Play once"), (int)AudioPlaybackMode::PlayOnce);
    cmb_audio_playback_->addItem(QStringLiteral("Loop"), (int)AudioPlaybackMode::Loop);
    cmb_audio_playback_->addItem(QStringLiteral("Pause at end"), (int)AudioPlaybackMode::PauseAtOut);
    cmb_audio_playback_->setFixedHeight(20);
    cmb_audio_playback_->setStyleSheet(control_style);
    chk_audio_mute_ = new FxmSwitch(QString(), audio_box_);
    chk_audio_solo_ = new FxmSwitch(QString(), audio_box_);
    chk_audio_loop_ = new FxmSwitch(QString(), audio_box_);
    chk_audio_independent_ = new FxmSwitch(QString(), audio_box_);
    for (auto *box : {chk_audio_mute_, chk_audio_solo_, chk_audio_loop_, chk_audio_independent_}) style_checkbox(box);
    row_audio_range_tools_ = new QWidget(audio_box_);
    auto *audio_range_tools = row_audio_range_tools_;
    auto *audio_range_tools_layout = new QHBoxLayout(audio_range_tools);
    audio_range_tools_layout->setContentsMargins(0, 0, 0, 0);
    audio_range_tools_layout->setSpacing(5);
    audio_range_tools_layout->addWidget(btn_audio_set_in_);
    audio_range_tools_layout->addWidget(btn_audio_set_out_);
    audio_range_tools_layout->addWidget(lbl_audio_range_feedback_, 1);
    row_audio_source_ = source_row;
    row_audio_preview_ = lbl_audio_range_preview_;
    row_audio_range_ = make_audio_pair_row(QStringLiteral("In"), spn_audio_in_, QStringLiteral("Out"), spn_audio_out_);
    add_form_row(audio_form, QStringLiteral("Source"), row_audio_source_);
    add_form_row(audio_form, QStringLiteral("Preview"), row_audio_preview_);
    add_form_row(audio_form, QStringLiteral("Range"), row_audio_range_);
    add_form_row(audio_form, QStringLiteral("Range tools"), row_audio_range_tools_);
    lbl_audio_waveform_status_ = new QLabel(QStringLiteral("Waveform not generated"), audio_box_);
    lbl_audio_waveform_status_->setWordWrap(true);
    bar_audio_waveform_ = new QProgressBar(audio_box_);
    bar_audio_waveform_->setRange(0, 100);
    bar_audio_waveform_->setTextVisible(true);
    bar_audio_waveform_->setFixedHeight(18);
    add_form_row(audio_form, QStringLiteral("Waveform"), lbl_audio_waveform_status_);
    add_form_row(audio_form, QString(), bar_audio_waveform_);
    add_form_row(audio_form, QStringLiteral("Volume"), make_slider_row(spn_audio_volume_, sld_audio_volume_, btn_kf_audio_volume_));
    add_form_row(audio_form, QStringLiteral("Pan"), make_slider_row(spn_audio_pan_, sld_audio_pan_, btn_kf_audio_pan_));
    add_form_row(audio_form, QStringLiteral("Fade"), make_audio_pair_row(QStringLiteral("In"), spn_audio_fade_in_, QStringLiteral("Out"), spn_audio_fade_out_));
    add_form_row(audio_form, QStringLiteral("Fade curve"), cmb_audio_fade_curve_);
    add_form_row(audio_form, QStringLiteral("Playback mode"), cmb_audio_playback_);
    add_form_row(audio_form, QStringLiteral("Mute"), chk_audio_mute_);
    add_form_row(audio_form, QStringLiteral("Solo"), chk_audio_solo_);
    add_form_row(audio_form, QStringLiteral("Loop"), chk_audio_loop_);
    add_form_row(audio_form, QStringLiteral("Independent playback"), chk_audio_independent_);
    audio_box_->setVisible(false);
    fxm_add_panel_section(vl, audio_box_);

    auto audio_changed = [this](bool commit = false) {
        if (loading_values_ || !layer_)
            return;
        if (layer_->type == LayerType::Video && title_)
            synchronize_video_audio_streams(*title_, layer_->id);
        if (layer_->type == LayerType::Audio || layer_->type == LayerType::Video) {
            emit audio_property_changed(commit);
            emit property_changed(commit);
        }
    };
    auto install_audio_keyframe_menu =
        [this, audio_changed](QToolButton *button,
                              AnimatedProperty Layer::*member) {
        register_keyframe_times(button, [this, member]() {
            std::vector<double> times;
            if (!layer_) return times;
            for (const Keyframe &key : (layer_.get()->*member).keyframes)
                times.push_back(key.time);
            return times;
        });
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, button, member, audio_changed](const QPoint &position) {
            QMenu menu(button);
            QAction *clear = menu.addAction(
                fxm_tr("OBSTitles.DeleteAllKeyframes"));
            clear->setEnabled(layer_ && !(layer_.get()->*member).keyframes.empty());
            if (menu.exec(button->mapToGlobal(position)) != clear || !layer_)
                return;
            (layer_.get()->*member).keyframes.clear();
            load_values();
            audio_changed(true);
        });
    };
    install_audio_keyframe_menu(btn_kf_audio_volume_, &Layer::audio_volume_prop);
    install_audio_keyframe_menu(btn_kf_audio_pan_, &Layer::audio_pan_prop);
    connect(btn_audio_browse_, &QToolButton::clicked, this, [this, audio_changed]() {
        if (!layer_ || layer_->type != LayerType::Audio) return;
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose audio file"),
            QFileInfo(QString::fromStdString(layer_->audio_source)).absolutePath(),
            QStringLiteral("Audio and video (*.wav *.flac *.mp3 *.aac *.m4a *.mp4 *.mov *.mkv *.webm);;All files (*.*)"));
        if (path.isEmpty()) return;
        layer_->audio_source = path.toStdString();
        layer_->audio_waveform.clear();
        layer_->audio_waveform_duration = 0.0;
        layer_->audio_in_point = 0.0;
        layer_->audio_out_point = std::max(0.0, layer_->out_time - layer_->in_time);
        edt_audio_source_->setText(path);
        {
            QSignalBlocker block_in(spn_audio_in_);
            QSignalBlocker block_out(spn_audio_out_);
            spn_audio_in_->setValue(layer_->audio_in_point);
            spn_audio_out_->setValue(layer_->audio_out_point);
        }
        /* A file replacement changes asset identity, so publish it immediately.
         * The runtime worker will retire the old decode/waveform cache and queue
         * the new asset without waiting for focus loss or editor save. */
        audio_changed(true);
    });
    connect(btn_audio_clear_, &QToolButton::clicked, this, [this, audio_changed]() {
        if (!layer_ || layer_->type != LayerType::Audio || loading_values_) return;
        layer_->audio_source.clear();
        layer_->audio_waveform.clear();
        layer_->audio_waveform_duration = 0.0;
        layer_->audio_waveform_progress_percent = 0;
        layer_->audio_waveform_generating = false;
        layer_->audio_waveform_progress_label.clear();
        layer_->audio_in_point = 0.0;
        layer_->audio_out_point = 0.0;
        if (edt_audio_source_) edt_audio_source_->clear();
        audio_changed(true);
    });
    auto is_audio_control_layer = [this]() {
        return layer_ && (layer_->type == LayerType::Audio || layer_->type == LayerType::Video);
    };
    auto media_in_value = [this]() {
        return layer_ && layer_->type == LayerType::Video ? layer_->video_in_point :
               (layer_ ? layer_->audio_in_point : 0.0);
    };
    auto media_out_value = [this]() {
        return layer_ && layer_->type == LayerType::Video ? layer_->video_out_point :
               (layer_ ? layer_->audio_out_point : 0.0);
    };
    auto refresh_audio_range_spins = [this, media_in_value, media_out_value]() {
        QSignalBlocker blockIn(spn_audio_in_);
        QSignalBlocker blockOut(spn_audio_out_);
        if (spn_audio_in_) spn_audio_in_->setValue(media_in_value());
        if (spn_audio_out_) spn_audio_out_->setValue(media_out_value());
        if (layer_ && layer_->type == LayerType::Video) {
            QSignalBlocker blockVideoIn(spn_video_in_);
            QSignalBlocker blockVideoOut(spn_video_out_);
            if (spn_video_in_) spn_video_in_->setValue(layer_->video_in_point);
            if (spn_video_out_) spn_video_out_->setValue(layer_->video_out_point);
        }
    };
    connect(spn_audio_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed, is_audio_control_layer, refresh_audio_range_spins](double v){
        if (is_audio_control_layer() && !loading_values_) {
            set_layer_media_range_in_point(*layer_, v);
            refresh_audio_range_spins();
            audio_changed(false);
        }
    });
    connect(spn_audio_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed, is_audio_control_layer, refresh_audio_range_spins](double v){
        if (is_audio_control_layer() && !loading_values_) {
            set_layer_media_range_out_point(*layer_, v);
            refresh_audio_range_spins();
            audio_changed(false);
        }
    });
    auto set_audio_range_from_playhead = [this, audio_changed, is_audio_control_layer, media_in_value, refresh_audio_range_spins](bool set_in) {
        if (!is_audio_control_layer() || loading_values_) return;
        const double clip_local = std::clamp(playhead_ - layer_->in_time, 0.0,
            std::max(0.0, layer_->out_time - layer_->in_time));
        const double media_time = std::max(0.0, media_in_value() + clip_local);
        if (set_in)
            set_layer_media_range_in_point(*layer_, media_time);
        else
            set_layer_media_range_out_point(*layer_, media_time);
        refresh_audio_range_spins();
        audio_changed(true);
    };
    connect(btn_audio_set_in_, &QPushButton::clicked, this,
            [set_audio_range_from_playhead]() mutable { set_audio_range_from_playhead(true); });
    connect(btn_audio_set_out_, &QPushButton::clicked, this,
            [set_audio_range_from_playhead]() mutable { set_audio_range_from_playhead(false); });
    connect(spn_audio_volume_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double db){ if (layer_ && !loading_values_) { const double gain=audio_db_to_gain(db); layer_->audio_volume=float(gain); set_animated_value(layer_->audio_volume_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), gain); QSignalBlocker b(sld_audio_volume_); sld_audio_volume_->setValue(audio_db_to_slider(db)); audio_changed(false); }});
    connect(sld_audio_volume_, &QSlider::valueChanged, this, [this, audio_changed](int value){ if (layer_ && !loading_values_) { const double db=double(value)/10.0; const double gain=audio_db_to_gain(db); layer_->audio_volume=float(gain); set_animated_value(layer_->audio_volume_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), gain); QSignalBlocker b(spn_audio_volume_); spn_audio_volume_->setValue(db); audio_changed(false); }});
    connect(spn_audio_pan_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_pan=float(v/100.0); set_animated_value(layer_->audio_pan_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(sld_audio_pan_); sld_audio_pan_->setValue(qRound(v)); audio_changed(false); }});
    connect(sld_audio_pan_, &QSlider::valueChanged, this, [this, audio_changed](int v){ if (layer_ && !loading_values_) { layer_->audio_pan=float(v/100.0); set_animated_value(layer_->audio_pan_prop, std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)), v/100.0); QSignalBlocker b(spn_audio_pan_); spn_audio_pan_->setValue(v); audio_changed(false); }});
    connect(btn_kf_audio_volume_, &QToolButton::clicked, this, [this, audio_changed]() { if (!layer_) return; const double t=std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)); toggle_keyframe(layer_->audio_volume_prop,t,layer_->audio_volume_prop.evaluate(t)); audio_changed(true); });
    connect(btn_kf_audio_pan_, &QToolButton::clicked, this, [this, audio_changed]() { if (!layer_) return; const double t=std::clamp(playhead_-layer_->in_time,0.0,std::max(0.0,layer_->out_time-layer_->in_time)); toggle_keyframe(layer_->audio_pan_prop,t,layer_->audio_pan_prop.evaluate(t)); audio_changed(true); });
    connect(spn_audio_fade_in_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_fade_in=v; audio_changed(false); }});
    connect(spn_audio_fade_out_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, audio_changed](double v){ if (layer_ && !loading_values_) { layer_->audio_fade_out=v; audio_changed(false); }});
    connect(cmb_audio_fade_curve_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, audio_changed](int){ if (layer_ && !loading_values_) { layer_->audio_fade_curve=(AudioFadeCurve)cmb_audio_fade_curve_->currentData().toInt(); audio_changed(true); }});
    connect(cmb_audio_playback_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, audio_changed](int){ if (layer_ && !loading_values_) { layer_->audio_playback_mode=(AudioPlaybackMode)cmb_audio_playback_->currentData().toInt(); if (layer_->type == LayerType::Video) layer_->video_loop = layer_->audio_playback_mode == AudioPlaybackMode::Loop; audio_changed(true); }});
    connect(chk_audio_mute_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_muted=v; audio_changed(true); }});
    connect(chk_audio_solo_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_solo=v; audio_changed(true); }});
    connect(chk_audio_loop_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_loop=v; if (layer_->type == LayerType::Video) layer_->video_loop = v; audio_changed(true); }});
    connect(chk_audio_independent_, &QCheckBox::toggled, this, [this, audio_changed](bool v){ if (layer_ && !loading_values_) { layer_->audio_independent=v; if (layer_->type == LayerType::Video) layer_->video_playback_mode = v ? 1 : 0; if (cmb_video_playback_ && layer_->type == LayerType::Video) { QSignalBlocker block(cmb_video_playback_); const int i = cmb_video_playback_->findData(layer_->video_playback_mode); cmb_video_playback_->setCurrentIndex(i >= 0 ? i : 0); } audio_changed(true); }});
    for (auto *spin : {spn_audio_in_, spn_audio_out_, spn_audio_volume_, spn_audio_pan_, spn_audio_fade_in_, spn_audio_fade_out_})
        connect(spin, &QDoubleSpinBox::editingFinished, this, [audio_changed]() { audio_changed(true); });

    /* ── Asset Layer ── */
    asset_box_ = new QGroupBox(fxm_tr("OBSTitles.AssetLayer"), inner);
    asset_box_->setStyleSheet(section_style);
    auto *asset_form = new QFormLayout(asset_box_);
    style_form(asset_form);
    lbl_asset_name_ = new QLabel(asset_box_);
    lbl_asset_name_->setWordWrap(true);
    cmb_asset_playback_ = new QComboBox(asset_box_);
    cmb_asset_playback_->addItem(fxm_tr("OBSTitles.AssetPlaybackSynchronized"), 0);
    cmb_asset_playback_->addItem(fxm_tr("OBSTitles.AssetPlaybackIndependent"), 1);
    cmb_asset_playback_->setFixedHeight(20);
    cmb_asset_playback_->setStyleSheet(control_style);
    spn_asset_offset_ = new QDoubleSpinBox(asset_box_);
    spn_asset_offset_->setRange(-86400.0, 86400.0);
    spn_asset_offset_->setDecimals(3);
    spn_asset_offset_->setSuffix(QStringLiteral(" s"));
    spn_asset_offset_->setFixedHeight(20);
    spn_asset_offset_->setStyleSheet(control_style);
    spn_asset_pause_duration_ = new TimecodeSpinBox(asset_box_);
    spn_asset_pause_duration_->setRange(0.0, 86400.0);
    spn_asset_pause_duration_->setFixedHeight(20);
    spn_asset_pause_duration_->setStyleSheet(control_style);
    spn_asset_loop_count_ = new QSpinBox(asset_box_);
    spn_asset_loop_count_->setRange(1, 1000000);
    spn_asset_loop_count_->setSuffix(fxm_tr("OBSTitles.AssetLoopTimesSuffix"));
    spn_asset_loop_count_->setFixedHeight(20);
    spn_asset_loop_count_->setStyleSheet(control_style);
    chk_asset_loop_ = new FxmSwitch(fxm_tr("OBSTitles.AssetLoop"), asset_box_);
    chk_asset_loop_->setStyleSheet(checkbox_style);
    btn_asset_overrides_ = new QPushButton(fxm_tr("OBSTitles.EditAssetOverrides"), asset_box_);
    add_form_row(asset_form, fxm_tr("OBSTitles.AssetSource"), lbl_asset_name_);
    add_form_row(asset_form, fxm_tr("OBSTitles.AssetPlayback"), cmb_asset_playback_);
    add_form_row(asset_form, fxm_tr("OBSTitles.AssetPlaybackOffset"), spn_asset_offset_);
    add_form_row(asset_form, fxm_tr("OBSTitles.AssetPauseFor"), spn_asset_pause_duration_);
    add_form_row(asset_form, fxm_tr("OBSTitles.AssetLoopCount"), spn_asset_loop_count_);
    add_form_row(asset_form, QString(), chk_asset_loop_);
    add_form_row(asset_form, QString(), btn_asset_overrides_);
    asset_box_->setVisible(false);
    fxm_add_panel_section(vl, asset_box_);

    connect(cmb_asset_playback_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_playback_mode = cmb_asset_playback_->currentData().toInt();
                fxm::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                update_asset_playback_controls_visibility();
                emit property_changed(true);
            });
    connect(spn_asset_offset_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_playback_offset = value;
                fxm::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(spn_asset_pause_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_pause_duration = value;
                fxm::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(spn_asset_loop_count_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
                if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
                layer_->asset_loop_count = value;
                fxm::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
                emit property_changed(true);
            });
    connect(chk_asset_loop_, &QCheckBox::toggled, this, [this](bool checked) {
        if (loading_values_ || !layer_ || layer_->type != LayerType::Asset) return;
        layer_->asset_loop = checked;
        fxm::asset_runtime::reset_layer(title_ ? title_->id : std::string(), layer_->id);
        emit property_changed(true);
    });
    connect(btn_asset_overrides_, &QPushButton::clicked, this, [this]() {
        if (layer_ && layer_->type == LayerType::Asset)
            emit asset_overrides_requested(layer_->id);
    });

    /* ── Appearance ── */
    appearance_box_ = new QWidget(inner);
    appearance_box_->setStyleSheet(QStringLiteral("background:%1;").arg(section_bg_name));
    auto *appearance_layout = new QVBoxLayout(appearance_box_);
    appearance_layout->setContentsMargins(5, 4, 5, 5);
    appearance_layout->setSpacing(2);

    auto *appearance_grid = new QGridLayout();
    appearance_grid->setContentsMargins(0, 0, 0, 0);
    appearance_grid->setHorizontalSpacing(4);
    appearance_grid->setVerticalSpacing(2);
    appearance_grid->setColumnMinimumWidth(0, 82);
    appearance_grid->setColumnMinimumWidth(1, 42);
    appearance_grid->setColumnMinimumWidth(2, 0);
    appearance_grid->setColumnMinimumWidth(3, 0);
    appearance_grid->setColumnStretch(2, 1);

    auto make_swatch = [&](const QString &tip) {
        auto *button = new QPushButton(appearance_box_);
        button->setFixedSize(42, 20);
        button->setToolTip(tip);
        button->setText(QString());
        return button;
    };
    btn_appearance_fill_color_ = make_swatch(fxm_tr("OBSTitles.FillColorTooltip"));
    btn_appearance_stroke_color_ = make_swatch(fxm_tr("OBSTitles.OutlineColorLabel"));
    btn_material_emissive_color_ = make_swatch(QStringLiteral("Emissive Color"));
    spn_appearance_stroke_width_ = mk_dspin(0.0, 200.0, 1.0);
    spn_appearance_stroke_width_->setFixedSize(86, 24);
    spn_appearance_stroke_width_->setSuffix(QStringLiteral("px"));
    spn_appearance_stroke_width_->setDecimals(0);
    spn_appearance_stroke_width_->setFixedHeight(20);
    spn_appearance_stroke_width_->setStyleSheet(control_style);
    spn_appearance_opacity_ = mk_dspin(0.0, 100.0, 1.0);
    spn_appearance_opacity_->setFixedSize(86, 24);
    spn_appearance_opacity_->setSuffix(QStringLiteral("%"));
    spn_appearance_opacity_->setDecimals(0);
    spn_appearance_opacity_->setFixedHeight(20);
    spn_appearance_opacity_->setStyleSheet(control_style);
    btn_kf_appearance_fill_ = mk_kf_button(fxm_tr("OBSTitles.ToggleFillColorKeyframe"));
    btn_kf_appearance_stroke_ = mk_kf_button(QStringLiteral("Toggle Stroke Color keyframe"));
    btn_kf_appearance_opacity_ = btn_kf_opacity_;
    btn_kf_appearance_opacity_->show();
    btn_kf_material_emissive_color_ = mk_kf_button(QStringLiteral("Toggle Emissive Color keyframe"));
    btn_kf_material_emissive_intensity_ = mk_kf_button(QStringLiteral("Toggle Emissive Intensity keyframe"));
    auto *stroke_options_trigger = new QPushButton(appearance_box_);
    stroke_options_trigger->hide();
    btn_appearance_stroke_label_ = new StrokeOptionsLabel(
        fxm_tr("OBSTitles.Stroke"), spn_appearance_stroke_width_, appearance_box_,
        [stroke_options_trigger]() { stroke_options_trigger->click(); },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = true;
            emit property_changed(true);
        },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = false;
            emit property_changed(true);
        });
    btn_appearance_stroke_label_->setStyleSheet(
        QStringLiteral("QLabel{color:%1;background:transparent;text-decoration:underline;font-size:10px;padding:0;}"
        "QLabel:hover{color:%2;}").arg(panel_text_name, highlighted_text_name));

    auto add_appearance_row = [&](int row, QPushButton *kf, const QString &label, QWidget *primary,
                                  QWidget *secondary = nullptr, QWidget *label_widget = nullptr) -> QWidget * {
        QWidget *text = label_widget;
        if (!text) {
            text = new NumericDragLabel(label, secondary ? secondary : primary, appearance_box_,
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = true;
                                            emit property_changed(true);
                                        },
                                        [this]() {
                                            if (loading_values_) return;
                                            numeric_label_dragging_ = false;
                                            emit property_changed(true);
                                        });
            text->setStyleSheet(transform_label_style);
        }
        if (auto *label_control = qobject_cast<QLabel *>(text))
            label_control->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        appearance_grid->addWidget(text, row, 0, Qt::AlignVCenter);
        if (primary)
            appearance_grid->addWidget(primary, row, 1, Qt::AlignLeft | Qt::AlignVCenter);
        if (secondary)
            appearance_grid->addWidget(secondary, row, 2, Qt::AlignLeft | Qt::AlignVCenter);
        if (kf)
            appearance_grid->addWidget(make_keyframe_controls(kf, appearance_box_),
                                       row, 3, Qt::AlignRight | Qt::AlignVCenter);
        return text;
    };

    add_appearance_row(0, btn_kf_appearance_fill_, fxm_tr("OBSTitles.Fill"), btn_appearance_fill_color_);
    appearance_emissive_color_label_ = add_appearance_row(
        1, btn_kf_material_emissive_color_, QStringLiteral("Emissive Color"),
        btn_material_emissive_color_);
    auto *emissive_intensity_drag_label = new NumericDragLabel(
        QStringLiteral("Emissive Intensity"), spn_material_emissive_intensity_,
        appearance_box_,
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = true;
            emit property_changed(true);
        },
        [this]() {
            if (loading_values_) return;
            numeric_label_dragging_ = false;
            emit property_changed(true);
        });
    emissive_intensity_drag_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    emissive_intensity_drag_label->setStyleSheet(QStringLiteral(
        "font-size:10px;color:%1;background:transparent;"
        "QLabel:hover{color:%2;}").arg(panel_text_name, highlighted_text_name));
    appearance_emissive_intensity_label_ = add_appearance_row(
        2, btn_kf_material_emissive_intensity_, QStringLiteral("Emissive Intensity"),
        nullptr, spn_material_emissive_intensity_, emissive_intensity_drag_label);
    add_appearance_row(3, btn_kf_appearance_stroke_,
                       fxm_tr("OBSTitles.Stroke"),
                       btn_appearance_stroke_color_, spn_appearance_stroke_width_,
                       btn_appearance_stroke_label_);
    add_appearance_row(4, btn_kf_appearance_opacity_, fxm_tr("OBSTitles.Opacity"), nullptr,
                       spn_appearance_opacity_);
    appearance_layout->addLayout(appearance_grid);
    fxm_add_panel_section(vl, appearance_box_, fxm_tr("OBSTitles.Appearance"));

    auto make_property_grid = [&](QWidget *parent_widget) {
        auto *grid = new QGridLayout(parent_widget);
        grid->setContentsMargins(6, 5, 6, 6);
        grid->setHorizontalSpacing(5);
        grid->setVerticalSpacing(3);
        grid->setColumnStretch(1, 1);
        grid->setColumnStretch(3, 1);
        return grid;
    };
    auto grid_label = [&](const QString &text, QWidget *parent_widget, QWidget *field = nullptr) {
        auto *label = new NumericDragLabel(text, field, parent_widget,
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit property_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit property_changed(true);
                                           });
        label->setStyleSheet(QStringLiteral("color:%1;font-size:10px;background:transparent;").arg(subtle_text_name));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return label;
    };
    auto add_grid_field = [&](QGridLayout *grid, int row, int col, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, col * 2);
        grid->addWidget(field, row, col * 2 + 1);
    };
    auto add_full_width_field = [&](QGridLayout *grid, int row, const QString &label_text, QWidget *field) {
        QWidget *parent_widget = grid->parentWidget();
        grid->addWidget(grid_label(label_text, parent_widget, field), row, 0);
        grid->addWidget(field, row, 1, 1, 3);
    };
    auto mk_combo = [&](const QStringList &labels, const QList<int> &values) {
        auto *combo = new QComboBox(inner);
        for (int i = 0; i < labels.size(); ++i)
            combo->addItem(labels[i], i < values.size() ? values[i] : i);
        combo->setFixedHeight(20);
        combo->setStyleSheet(control_style);
        return combo;
    };
    auto mk_type_button = [&](const QString &label, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setText(label);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFixedSize(28, 22);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;"
            "font-size:10px;font-weight:bold;padding:0;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };
    auto mk_type_icon_button = [&](const char *icon_name, const QString &tip) {
        auto *button = mk_type_button(QString(), tip);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setAccessibleName(tip);
        return button;
    };
    auto mk_paragraph_alignment_button = [&](const char *icon_name, const QString &tip) {
        auto *button = new QToolButton(inner);
        button->setIcon(obs_icon(icon_name));
        button->setIconSize(QSize(14, 14));
        button->setToolTip(tip);
        button->setAccessibleName(tip);
        button->setCheckable(true);
        button->setFixedSize(30, 24);
        button->setAutoRaise(false);
        button->setStyleSheet(QStringLiteral(
            "QToolButton{color:%1;background:%2;border:1px solid %3;border-radius:2px;padding:2px;}"
            "QToolButton:hover{background:%4;border-color:%3;}"
            "QToolButton:checked{background:%5;color:%6;border-color:%5;}")
            .arg(button_text_name, button_bg_name, border_name, hover_bg_name,
                 highlight_name, highlighted_text_name));
        return button;
    };

    /* ── Character ── */
    text_box_ = new QGroupBox("Character", inner);
    text_box_->setStyleSheet(QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;margin:0;padding:0;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QLabel{color:%3;font-size:10px;background:transparent;}")
        .arg(panel_text_name, section_bg_name, subtle_text_name));
    auto *char_grid = make_property_grid(text_box_);
    char_grid->setContentsMargins(14, 28, 14, 12);
    char_grid->setHorizontalSpacing(5);
    char_grid->setVerticalSpacing(6);

    txt_content_ = new QTextEdit(inner);
    txt_content_->setAcceptRichText(false);
    txt_content_->setMinimumHeight(72);
    txt_content_->setMaximumHeight(92);
    txt_content_->setPlaceholderText(fxm_tr("OBSTitles.EnterTextPlaceholder"));
    txt_content_->setMinimumHeight(42);
    txt_content_->setStyleSheet(control_style);
    btn_text_external_binding_ = new QToolButton(inner);
    btn_text_external_binding_->setText(QStringLiteral("⛓"));
    btn_text_external_binding_->setCheckable(true);
    btn_text_external_binding_->setAutoRaise(false);
    btn_text_external_binding_->setFixedSize(28, 28);
    btn_text_external_binding_->setToolTip(QStringLiteral("Bind text content to external data"));
    btn_text_external_binding_->setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid palette(mid);border-radius:3px;}"
        "QToolButton:checked{border:2px solid palette(highlight);"
        "background:palette(highlight);color:palette(highlighted-text);}"));

    cmb_clock_preset_ = new QComboBox(inner);
    cmb_clock_preset_->setFixedHeight(20);
    cmb_clock_preset_->setStyleSheet(control_style);
    cmb_clock_preset_->setToolTip(QStringLiteral("Choose a clock date/time preset, or use Custom and edit the format directly."));
    cmb_clock_preset_->addItem(QStringLiteral("Time — HH:MM:SS"), QStringLiteral("H:i:s"));
    cmb_clock_preset_->addItem(QStringLiteral("Time — HH:MM"), QStringLiteral("H:i"));
    cmb_clock_preset_->addItem(QStringLiteral("Date — DD/MM/YYYY"), QStringLiteral("d/m/Y"));
    cmb_clock_preset_->addItem(QStringLiteral("Date — YYYY-MM-DD"), QStringLiteral("Y-m-d"));
    cmb_clock_preset_->addItem(QStringLiteral("Date + Time"), QStringLiteral("d/m/Y H:i"));
    cmb_clock_preset_->addItem(QStringLiteral("Day + Date"), QStringLiteral("D d M Y"));
    cmb_clock_preset_->addItem(QStringLiteral("Full Date"), QStringLiteral("l d F Y"));
    cmb_clock_preset_->addItem(QStringLiteral("Unix Timestamp"), QStringLiteral("U"));
    cmb_clock_preset_->addItem(QStringLiteral("Custom"), QStringLiteral("__custom__"));

    cmb_font_ = new LazyFontComboBox(inner);
    cmb_font_->setFixedHeight(20);
    cmb_font_->setEditable(true);
    cmb_font_->setInsertPolicy(QComboBox::NoInsert);
    cmb_font_->setMaxVisibleItems(24);
    cmb_font_->setStyleSheet(control_style);

    cmb_font_style_ = new LazyFontStyleComboBox(inner);
    cmb_font_style_->setFixedHeight(20);
    cmb_font_style_->setStyleSheet(control_style);

    spn_size_ = new QSpinBox(inner);
    spn_size_->setRange(6, 500);
    spn_size_->setFixedHeight(20);
    spn_size_->setStyleSheet(control_style);
