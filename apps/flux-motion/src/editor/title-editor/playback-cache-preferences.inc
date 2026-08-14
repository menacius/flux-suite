    connect(boolean_union_button_, &QToolButton::clicked, this, [this]() {
        apply_boolean_shape_operation(BooleanShapeOperation::Union);
    });
    connect(boolean_subtract_button_, &QToolButton::clicked, this, [this]() {
        apply_boolean_shape_operation(BooleanShapeOperation::SubtractFront);
    });
    connect(boolean_intersect_button_, &QToolButton::clicked, this, [this]() {
        apply_boolean_shape_operation(BooleanShapeOperation::Intersect);
    });
    connect(boolean_exclude_button_, &QToolButton::clicked, this, [this]() {
        apply_boolean_shape_operation(BooleanShapeOperation::Exclude);
    });

    connect(point_toolbar_corner_, &QToolButton::clicked, this, [this]() {
        if (!updating_corner_toolbar_ && canvas_)
            canvas_->convert_selected_points_to_corner();
    });
    connect(point_toolbar_smooth_, &QToolButton::clicked, this, [this]() {
        if (!updating_corner_toolbar_ && canvas_)
            canvas_->convert_selected_points_to_smooth();
    });
    connect(point_toolbar_handles_, &QToolButton::toggled, this, [this](bool checked) {
        if (!updating_corner_toolbar_ && canvas_)
            canvas_->set_point_control_show_handles(checked);
    });
    connect(point_toolbar_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (updating_corner_toolbar_ || !canvas_ || value <= point_toolbar_x_->minimum()) return;
                canvas_->set_point_control_x(value);
            });
    connect(point_toolbar_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (updating_corner_toolbar_ || !canvas_ || value <= point_toolbar_y_->minimum()) return;
                canvas_->set_point_control_y(value);
            });

    connect(corner_toolbar_radius_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (updating_corner_toolbar_ || !canvas_ || value < 0.0) return;
                canvas_->set_corner_control_radius(value);
            });
    connect(corner_toolbar_roundness_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (updating_corner_toolbar_ || !canvas_ || value < -100.0) return;
                canvas_->set_corner_control_bevel_roundness(value);
            });
    connect(corner_toolbar_sync_, &QCheckBox::stateChanged,
            this, [this](int state) {
                if (updating_corner_toolbar_ || !canvas_ || state == Qt::PartiallyChecked) return;
                canvas_->set_corner_control_sync(state == Qt::Checked);
            });

    act_live_editing_ = new QAction(fxm_tr("OBSTitles.LiveEditing"), this);
    act_live_editing_->setCheckable(true);
    act_live_editing_->setToolTip(fxm_tr("OBSTitles.LiveEditingTooltip"));
    connect(act_live_editing_, &QAction::toggled, this, &TitleEditor::set_live_editing_enabled);

    auto *live_editing_button = new QToolButton(toolbar_);
    live_editing_button->setDefaultAction(act_live_editing_);
    live_editing_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    live_editing_button->setStyleSheet(QStringLiteral(
        "QToolButton{color:%1;background:transparent;min-width:0;max-width:none;min-height:%2px;max-height:%2px;padding:2px 8px;border:none;}"
        "QToolButton:hover{background:%3;border-radius:3px;}"
        "QToolButton:checked{background:%4;color:%5;border-radius:3px;}")
        .arg(button_text.name(QColor::HexRgb),
             QString::number(kEditorToolbarButtonExtent),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb)));
    toolbar_->addWidget(live_editing_button);

}

void TitleEditor::update_corner_toolbar()
{
    if ((!anchor_toolbar_widget_ && !boolean_toolbar_widget_ &&
         !corner_toolbar_widget_ && !point_toolbar_widget_) || !canvas_)
        return;
    QScopedValueRollback<bool> guard(updating_corner_toolbar_, true);

    const bool transform_available = canvas_->free_transform_controls_available();
    const bool boolean_available = boolean_shape_selection_available();
    bool boolean_enabled = boolean_available;
    if (boolean_enabled && title_) {
        for (const auto &id : selected_layer_ids_for_operation()) {
            const auto layer = title_->find_layer(id);
            if (!layer || layer->locked || layer->shape_type == ShapeType::Line ||
                (layer->shape_type == ShapeType::Path && !layer->path_closed)) {
                boolean_enabled = false;
                break;
            }
        }
    }
    bool anchor_available = false;
    int anchor_index = 4;
    const auto anchor_selection = selected_layer_ids_for_operation();
    if (title_ && anchor_selection.size() == 1) {
        if (const auto anchor_layer = title_->find_layer(anchor_selection.front());
            anchor_layer && !anchor_layer->locked &&
            !layer_type_is_audio(anchor_layer->type)) {
            anchor_available = true;
            anchor_index = anchor_index_from_layer(*anchor_layer);
        }
    }
    const bool point_available = canvas_->point_controls_available();
    const bool corner_available = canvas_->corner_controls_available();
    const bool context_available = transform_available || anchor_available ||
        boolean_available || point_available || corner_available;

    if (transform_toolbar_widget_) transform_toolbar_widget_->setVisible(transform_available);
    if (transform_available) {
        if (transform_toolbar_mode_) transform_toolbar_mode_->setCurrentIndex(canvas_->free_transform_mode());
        if (transform_toolbar_keyframe_) {
            const auto selected = selected_layer_ids_for_operation();
            const auto layer = title_ && selected.size() == 1
                ? title_->find_layer(selected.front()) : nullptr;
            const double local_time = layer
                ? std::clamp(playhead_ - layer->in_time, 0.0,
                             std::max(0.0, layer->out_time - layer->in_time))
                : 0.0;
            bool active = false;
            bool animated = false;
            if (layer) {
                for (const AnimatedVec2Property *property : {&layer->transform_quad_tl,
                                                             &layer->transform_quad_tr,
                                                             &layer->transform_quad_br,
                                                             &layer->transform_quad_bl}) {
                    animated = animated || property->is_animated();
                    active = active || keyframe_at_time(*property, local_time);
                }
            }
            transform_toolbar_keyframe_->setChecked(active);
            transform_toolbar_keyframe_->setProperty("active", active);
            transform_toolbar_keyframe_->setProperty("outlined", animated && !active);
            transform_toolbar_keyframe_->setIcon(
                fxm_keyframe_diamond_icon(active, animated && !active));
            fxm_refresh_keyframe_navigation(transform_toolbar_keyframe_);
        }
    }
    if (anchor_toolbar_widget_)
        anchor_toolbar_widget_->setVisible(anchor_available);
    if (anchor_toolbar_grid_ && anchor_available) {
        anchor_toolbar_grid_->setProperty("active_index", anchor_index);
        anchor_toolbar_grid_->update();
    }
    if (boolean_toolbar_widget_)
        boolean_toolbar_widget_->setVisible(boolean_available);
    if (boolean_union_button_) boolean_union_button_->setEnabled(boolean_enabled);
    if (boolean_subtract_button_) boolean_subtract_button_->setEnabled(boolean_enabled);
    if (boolean_intersect_button_) boolean_intersect_button_->setEnabled(boolean_enabled);
    if (boolean_exclude_button_) boolean_exclude_button_->setEnabled(boolean_enabled);
    if (point_toolbar_widget_)
        point_toolbar_widget_->setVisible(point_available);
    if (corner_toolbar_widget_)
        corner_toolbar_widget_->setVisible(corner_available);

    /*
     * QWidgetAction visibility must be changed directly.  Showing only its
     * default widget is not sufficient after the action has been laid out as
     * hidden/zero-width by QToolBar, particularly with the Windows style.
     */
    if (dynamic_toolbar_action_)
        dynamic_toolbar_action_->setVisible(context_available);
    else if (dynamic_toolbar_widget_)
        dynamic_toolbar_widget_->setVisible(context_available);
    if (dynamic_toolbar_separator_)
        dynamic_toolbar_separator_->setVisible(context_available);

    if (context_available && dynamic_toolbar_widget_) {
        dynamic_toolbar_widget_->adjustSize();
        dynamic_toolbar_widget_->updateGeometry();
    }
    if (toolbar_) {
        toolbar_->updateGeometry();
        toolbar_->update();
    }

    if (point_available) {
        bool x_mixed = false;
        bool y_mixed = false;
        const QPointF position = canvas_->point_control_position(&x_mixed, &y_mixed);
        if (point_toolbar_x_)
            point_toolbar_x_->setValue(x_mixed ? point_toolbar_x_->minimum() : position.x());
        if (point_toolbar_y_)
            point_toolbar_y_->setValue(y_mixed ? point_toolbar_y_->minimum() : position.y());
        bool smooth_mixed = false;
        const bool smooth = canvas_->point_control_smooth(&smooth_mixed);
        if (point_toolbar_corner_)
            point_toolbar_corner_->setChecked(!smooth_mixed && !smooth);
        if (point_toolbar_smooth_)
            point_toolbar_smooth_->setChecked(!smooth_mixed && smooth);
        if (point_toolbar_handles_)
            point_toolbar_handles_->setChecked(canvas_->point_control_show_handles());
    }

    if (!corner_available)
        return;

    bool radius_mixed = false;
    const double radius = canvas_->corner_control_radius(&radius_mixed);
    if (corner_toolbar_radius_)
        corner_toolbar_radius_->setValue(radius_mixed ? -1.0 : radius);

    bool sync_mixed = false;
    const bool sync = canvas_->corner_control_sync(&sync_mixed);
    if (corner_toolbar_sync_) {
        corner_toolbar_sync_->setCheckState(sync_mixed ? Qt::PartiallyChecked
                                                       : (sync ? Qt::Checked : Qt::Unchecked));
    }

    bool roundness_mixed = false;
    const double roundness = canvas_->corner_control_bevel_roundness(&roundness_mixed);
    if (corner_toolbar_roundness_)
        corner_toolbar_roundness_->setValue(roundness_mixed ? -101.0 : roundness);
}


static QString editor_template_library_root_path()
{
    auto *provider = fxm::host_context_provider();
    QString root = provider
        ? QString::fromStdString(provider->config_path("template-library"))
        : QDir::homePath();
    QDir().mkpath(root);
    return root;
}

static int editor_dialog_layout_spacing(QWidget *widget)
{
    const int spacing = widget->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, widget);
    return spacing > 0 ? spacing : 6;
}

static QStringList editor_template_library_category_paths(const QString &root_path)
{
    QStringList categories;
    QDirIterator it(root_path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = QDir(root_path).relativeFilePath(it.filePath());
        if (!rel.isEmpty()) categories << rel;
    }
    if (categories.isEmpty()) categories << fxm_tr("OBSTitles.Custom");
    categories.sort(Qt::CaseInsensitive);
    return categories;
}

static QString editor_sanitized_template_category_path(const QString &category)
{
    QString cleaned = QDir::cleanPath(category.trimmed());
    cleaned.replace(QRegularExpression(QStringLiteral("[\\\\:*?\"<>|]")), QStringLiteral("_"));
    while (cleaned.startsWith(QStringLiteral("../")))
        cleaned.remove(0, 3);
    if (cleaned == QStringLiteral(".") || cleaned == QStringLiteral(".."))
        cleaned.clear();
    return cleaned;
}

static bool prompt_editor_template_library_category(QWidget *parent, QString &category)
{
    const QString root_path = editor_template_library_root_path();
    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.TemplateLibraryCategoryTitle"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(editor_dialog_layout_spacing(&dialog));

    auto *prompt = new QLabel(fxm_tr("OBSTitles.TemplateLibraryCategoryPrompt"), &dialog);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);

    auto *combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->addItems(editor_template_library_category_paths(root_path));
    layout->addWidget(combo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString safe_category = editor_sanitized_template_category_path(combo->currentText());
        if (safe_category.isEmpty()) {
            QMessageBox::warning(&dialog, fxm_tr("OBSTitles.TemplateLibraryCategoryTitle"),
                                 fxm_tr("OBSTitles.TemplateLibraryCategoryRequired"));
            return;
        }
        category = safe_category;
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    return dialog.exec() == QDialog::Accepted;
}

static QStringList editor_template_metadata_values(const QString &field)
{
    QSet<QString> values;
    QDirIterator it(editor_template_library_root_path(), QStringList() << QStringLiteral("*.fxmt"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile file(it.next());
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;
        const QJsonObject root = doc.object();
        const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
        QString value = metadata.value(field).toString().trimmed();
        if (value.isEmpty()) value = root.value(field).toString().trimmed();
        if (!value.isEmpty()) values.insert(value);
    }
    QStringList result = values.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

static QComboBox *make_editor_template_metadata_combo(QWidget *parent, const QString &field,
                                                       const QString &current_value)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItem(fxm_tr("OBSTitles.TemplateMetadataAddNew"));
    combo->insertSeparator(1);
    combo->addItems(editor_template_metadata_values(field));
    const QString current = current_value.trimmed();
    if (!current.isEmpty() && combo->findText(current, Qt::MatchFixedString) < 0)
        combo->addItem(current);
    combo->setEditText(current);
    QObject::connect(combo, QOverload<int>::of(&QComboBox::activated), combo, [combo](int index) {
        if (index != 0) return;
        combo->setCurrentIndex(-1);
        combo->setEditText(QString());
        combo->setFocus(Qt::OtherFocusReason);
        if (combo->lineEdit()) combo->lineEdit()->selectAll();
    });
    return combo;
}

static bool prompt_editor_template_metadata(QWidget *parent, const Title &title,
                                           TitleTemplateExportMetadata &metadata,
                                           const QString &window_title = fxm_tr("OBSTitles.ExportTemplateDetails"))
{
    if (metadata.title.empty()) metadata.title = title.name;
    if (metadata.description.empty()) metadata.description = title.description;
    if (metadata.creator.empty()) metadata.creator = title.creator;
    if (metadata.creation_date.empty()) {
        metadata.creation_date = title.creation_date.empty()
            ? QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString()
            : title.creation_date;
    }
    if (metadata.screenshot_png_base64.empty())
        metadata.screenshot_png_base64 = title.preview_screenshot_png_base64;

    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(window_title);
    dialog.setModal(true);
    dialog.resize(560, 500);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(editor_dialog_layout_spacing(&dialog));

    auto *preview_label = new QLabel(fxm_tr("OBSTitles.TemplateScreenshotPreviewLabel"), &dialog);
    QFont label_font = preview_label->font();
    label_font.setBold(true);
    preview_label->setFont(label_font);
    layout->addWidget(preview_label);

    auto *preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    preview->setMinimumHeight(160);
    QPixmap pixmap;
    const QByteArray png = QByteArray::fromBase64(QByteArray::fromStdString(metadata.screenshot_png_base64));
    if (!png.isEmpty() && pixmap.loadFromData(png, "PNG"))
        preview->setPixmap(pixmap.scaled(QSize(480, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    else
        preview->setText(fxm_tr("OBSTitles.TemplateScreenshotFailed"));
    layout->addWidget(preview);

    auto *form = new QFormLayout();
    auto *title_edit = new QLineEdit(QString::fromStdString(metadata.title), &dialog);
    auto *description_edit = new QTextEdit(&dialog);
    description_edit->setAcceptRichText(false);
    description_edit->setPlainText(QString::fromStdString(metadata.description));
    description_edit->setMinimumHeight(96);
    auto *creator_edit = new QLineEdit(QString::fromStdString(metadata.creator), &dialog);
    auto *date_edit = new QLineEdit(QString::fromStdString(metadata.creation_date), &dialog);
    auto *category_edit = make_editor_template_metadata_combo(&dialog, QStringLiteral("category"), QString::fromStdString(metadata.category));
    auto *subcategory_edit = make_editor_template_metadata_combo(&dialog, QStringLiteral("subcategory"), QString::fromStdString(metadata.subcategory));
    auto *collection_edit = make_editor_template_metadata_combo(&dialog, QStringLiteral("collection"), QString::fromStdString(metadata.collection));

    form->addRow(fxm_tr("OBSTitles.TemplateExportTitleLabel"), title_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateExportDescriptionLabel"), description_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateExportCreatorLabel"), creator_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCreationDateLabel"), date_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCategoryLabel"), category_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateSubcategoryLabel"), subcategory_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCollectionLabel"), collection_edit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (title_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, fxm_tr("OBSTitles.ExportTemplateDetails"),
                                 fxm_tr("OBSTitles.TemplateExportTitleRequired"));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    metadata.title = title_edit->text().trimmed().toStdString();
    metadata.description = description_edit->toPlainText().trimmed().toStdString();
    metadata.creator = creator_edit->text().trimmed().toStdString();
    metadata.creation_date = date_edit->text().trimmed().toStdString();
    metadata.category = category_edit->currentText().trimmed().toStdString();
    metadata.subcategory = subcategory_edit->currentText().trimmed().toStdString();
    metadata.collection = collection_edit->currentText().trimmed().toStdString();
    return true;
}

static bool prompt_editor_packed_title_options(
    QWidget *parent, PackedTitleExportOptions &options)
{
    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.PackedOptionsTitle"));
    dialog.setModal(true);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(editor_dialog_layout_spacing(&dialog));
    auto *description = new QLabel(fxm_tr("OBSTitles.PackedOptionsDescription"), &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *images = new FxmSwitch(fxm_tr("OBSTitles.PackImages"), &dialog);
    auto *media = new FxmSwitch(fxm_tr("OBSTitles.PackMedia"), &dialog);
    auto *fonts = new FxmSwitch(fxm_tr("OBSTitles.PackFonts"), &dialog);
    images->setChecked(options.pack_images);
    media->setChecked(options.pack_media);
    fonts->setChecked(options.pack_fonts);
    fonts->setToolTip(fxm_tr("OBSTitles.PackFontsTooltip"));
    layout->addWidget(images);
    layout->addWidget(media);
    layout->addWidget(fonts);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    options.pack_images = images->isChecked();
    options.pack_media = media->isChecked();
    options.pack_fonts = fonts->isChecked();
    return true;
}

void TitleEditor::copy_title_to_store(const std::shared_ptr<Title> &source,
                                      const std::shared_ptr<Title> &dest) const
{
    if (!source || !dest) return;
    const std::string dest_id = dest->id;
    *dest = *source;
    dest->id = dest_id;
    dest->layers.clear();
    dest->layers.reserve(source->layers.size());
    for (const auto &layer : source->layers) {
        if (layer) dest->layers.push_back(std::make_shared<Layer>(*layer));
    }
}

bool fxm::editor::prompt_standalone_new_title_settings(
    QWidget *parent, StandaloneNewTitleSettings &settings)
{
    QSettings persisted(QStringLiteral("FluxMotion"),
                        QStringLiteral("Standalone"));
    settings.duration = std::clamp(
        persisted.value(QStringLiteral("NewTitle/Duration"),
                        settings.duration).toDouble(), 0.1, 3600.0);
    settings.width = std::clamp(
        persisted.value(QStringLiteral("NewTitle/Width"),
                        settings.width).toInt(), 1, 16384);
    settings.height = std::clamp(
        persisted.value(QStringLiteral("NewTitle/Height"),
                        settings.height).toInt(), 1, 16384);
    settings.frame_rate = std::clamp(
        persisted.value(QStringLiteral("NewTitle/FrameRate"),
                        settings.frame_rate).toDouble(), 1.0, 240.0);

    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("FluxMotionNewTitleDialog"));
    dialog.setWindowTitle(fxm_tr("OBSTitles.NewTitle"));
    dialog.setModal(true);
    dialog.setMinimumWidth(420);

    auto *root = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(
        QStringLiteral("Set the format for the new title or graphic."),
        &dialog);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout();
    auto *length = new TimecodeSpinBox(&dialog);
    length->setRange(0.1, 3600.0);
    length->setValue(settings.duration);
    form->addRow(fxm_tr("OBSTitles.LengthLabel"), length);

    auto *resolution = new QWidget(&dialog);
    auto *resolution_layout = new QHBoxLayout(resolution);
    resolution_layout->setContentsMargins(0, 0, 0, 0);
    resolution_layout->setSpacing(6);
    auto *width = new QSpinBox(resolution);
    width->setRange(1, 16384);
    width->setSuffix(QStringLiteral(" px"));
    width->setValue(settings.width);
    auto *height = new QSpinBox(resolution);
    height->setRange(1, 16384);
    height->setSuffix(QStringLiteral(" px"));
    height->setValue(settings.height);
    resolution_layout->addWidget(width, 1);
    resolution_layout->addWidget(
        new QLabel(QString::fromUtf8("\xC3\x97"), resolution));
    resolution_layout->addWidget(height, 1);
    form->addRow(fxm_tr("OBSTitles.ResolutionLabel"), resolution);

    auto *frame_rate = new QDoubleSpinBox(&dialog);
    frame_rate->setRange(1.0, 240.0);
    frame_rate->setDecimals(3);
    frame_rate->setSingleStep(1.0);
    frame_rate->setSuffix(QStringLiteral(" fps"));
    frame_rate->setValue(settings.frame_rate);
    form->addRow(fxm_tr("OBSTitles.FrameRateLabel"), frame_rate);
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    root->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    settings.duration = length->value();
    settings.width = width->value();
    settings.height = height->value();
    settings.frame_rate = frame_rate->value();
    persisted.setValue(QStringLiteral("NewTitle/Duration"), settings.duration);
    persisted.setValue(QStringLiteral("NewTitle/Width"), settings.width);
    persisted.setValue(QStringLiteral("NewTitle/Height"), settings.height);
    persisted.setValue(QStringLiteral("NewTitle/FrameRate"), settings.frame_rate);
    return true;
}

void fxm::editor::apply_standalone_new_title_settings(
    const std::shared_ptr<Title> &title,
    const StandaloneNewTitleSettings &settings)
{
    if (!title)
        return;
    title->duration = std::clamp(settings.duration, 0.1, 3600.0);
    title->width = std::clamp(settings.width, 1, 16384);
    title->height = std::clamp(settings.height, 1, 16384);
    title->frame_rate = std::clamp(settings.frame_rate, 1.0, 240.0);
    title->loop_start = std::min(title->duration, title->duration * 0.2);
    title->loop_end = std::min(title->duration, title->duration * 0.8);
    for (const auto &layer : title->layers) {
        if (!layer)
            continue;
        layer->out_time = title->duration;
        layer->position.static_value = {
            title->width * 0.5, title->height * 0.5};
        if (layer->type == LayerType::Text) {
            layer->rect_width = std::max(1.0f,
                static_cast<float>(title->width) * 0.5f);
            layer->rect_height = std::max(1.0f, std::min(
                160.0f, static_cast<float>(title->height)));
            layer->size.static_value = {
                layer->rect_width, layer->rect_height};
        }
    }
}

void TitleEditor::new_project()
{
    if (!execution_context_.supports_new_open)
        return;
    if (!confirm_save_before_close())
        return;

    fxm::editor::StandaloneNewTitleSettings settings;
    if (!fxm::editor::prompt_standalone_new_title_settings(this, settings))
        return;
    const auto created = TitleDataStore::instance().create_title("Title 1");
    if (!created) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.New"),
                             fxm_tr("OBSTitles.CreateProjectFailed"));
        return;
    }
    fxm::editor::apply_standalone_new_title_settings(created, settings);
    TitleProject project;
    project.id = TitleDataStore::make_uuid();
    project.name = "Untitled Project";
    project.active_title_id = created->id;
    project.titles = {created};
    TitleDataStore::instance().activate_project(project);
    current_project_ = project;
    project_title_ids_ = {created->id};
    open_title(created->id);
    begin_unsaved_project();
    refresh_project_panel();
}

void TitleEditor::new_title_contents()
{
    new_project();
}

void TitleEditor::open_project()
{
    if (!execution_context_.supports_new_open)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, fxm_tr("OBSTitles.OpenProjectTitle"), QString(),
        fxm_tr("OBSTitles.ProjectFileFilter"));
    if (!path.isEmpty())
        open_project_path(path);
}

bool TitleEditor::open_project_path(const QString &path)
{
    if (!execution_context_.supports_new_open || path.trimmed().isEmpty())
        return false;
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        fxm::editor::RecentProjects::remove(path);
        emit recent_projects_changed();
        QMessageBox::warning(this, fxm_tr("OBSTitles.OpenProjectTitle"),
                             fxm_tr("OBSTitles.ProjectFileMissingFormat")
                                 .arg(QDir::toNativeSeparators(path)));
        return false;
    }
    if (!confirm_save_before_close())
        return false;

    fxm::editor::ProjectLoadingDialog loading(this);
    TitleProject project;
    std::string error;
    const bool loaded = TitleDataStore::instance().read_project(
        info.absoluteFilePath().toStdString(), &project, &error,
        [&loading](const ProjectLoadProgress &state) {
            return loading.updateProgress(state);
        });
    loading.close();
    if (!loaded || !TitleDataStore::instance().activate_project(project)) {
        QMessageBox::warning(
            this, fxm_tr("OBSTitles.OpenProjectTitle"),
            error.empty() ? fxm_tr("OBSTitles.ImportFailed")
                          : QString::fromStdString(error));
        return false;
    }
    TitleDataStore::instance().save_async();
    const std::string active_id = project.active_title_id.empty()
        ? project.titles.front()->id : project.active_title_id;
    open_title(active_id);
    set_project_context(project, info.absoluteFilePath());
    show_editor_activity(fxm_tr("OBSTitles.ProjectOpenedStatusFormat")
                             .arg(info.fileName()));
    return true;
}

TitleProject TitleEditor::project_snapshot() const
{
    TitleProject project = current_project_;
    project.titles.clear();
    project.active_title_id = editing_title_id_;
    for (const std::string &id : project_title_ids_) {
        auto title = TitleDataStore::instance().get_title_snapshot(id);
        if (title)
            project.titles.push_back(std::move(title));
    }
    if (project.titles.empty() && title_)
        project.titles.push_back(clone_title(*title_));
    if (project.id.empty())
        project.id = TitleDataStore::make_uuid();
    if (project.name.empty())
        project.name = "Untitled Project";
    return project;
}

void TitleEditor::refresh_project_panel()
{
    if (!project_panel_)
        return;
    std::vector<std::shared_ptr<Title>> titles;
    titles.reserve(project_title_ids_.size());
    for (const std::string &id : project_title_ids_) {
        auto title = TitleDataStore::instance().get_title_snapshot(id);
        if (title) titles.push_back(std::move(title));
    }
    project_panel_->setTitles(titles, editing_title_id_);
}

void TitleEditor::open_project_title(const std::string &title_id)
{
    if (title_id.empty() || title_id == editing_title_id_)
        return;
    if (std::find(project_title_ids_.begin(), project_title_ids_.end(),
                  title_id) == project_title_ids_.end())
        return;
    if (dirty_ && !persist_title_changes(true, false))
        return;
    open_title(title_id);
    refresh_project_panel();
}

void TitleEditor::create_project_title()
{
    if (dirty_ && !persist_title_changes(true, false))
        return;
    fxm::editor::StandaloneNewTitleSettings settings;
    if (!fxm::editor::prompt_standalone_new_title_settings(this, settings))
        return;
    auto created = TitleDataStore::instance().create_title("New Title");
    if (!created)
        return;
    fxm::editor::apply_standalone_new_title_settings(created, settings);
    project_title_ids_.push_back(created->id);
    current_project_.active_title_id = created->id;
    project_dirty_ = true;
    open_title(created->id);
    set_dirty(true);
    refresh_project_panel();
}

void TitleEditor::duplicate_project_titles(
    const std::vector<std::string> &title_ids)
{
    if (title_ids.empty())
        return;
    if (dirty_ && !persist_title_changes(true, false))
        return;
    std::string last_id;
    for (const std::string &id : title_ids) {
        auto duplicate = TitleDataStore::instance().duplicate_title(id);
        if (!duplicate) continue;
        const auto position = std::find(project_title_ids_.begin(),
                                        project_title_ids_.end(), id);
        project_title_ids_.insert(position == project_title_ids_.end()
            ? project_title_ids_.end() : std::next(position), duplicate->id);
        last_id = duplicate->id;
    }
    if (last_id.empty())
        return;
    project_dirty_ = true;
    open_title(last_id);
    set_dirty(true);
    refresh_project_panel();
}

void TitleEditor::rename_project_title(const std::string &title_id)
{
    auto title = TitleDataStore::instance().get_title(title_id);
    if (!title) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("Rename Title"), QStringLiteral("Name:"),
        QLineEdit::Normal, QString::fromStdString(title->name), &accepted).trimmed();
    if (!accepted || name.isEmpty())
        return;
    TitleDataStore::instance().rename_title(title_id, name.toStdString());
    if (title_id == editing_title_id_ && title_)
        title_->name = TitleDataStore::instance().get_title(title_id)->name;
    project_dirty_ = true;
    set_dirty(true);
    update_title_bar();
    refresh_project_panel();
}

void TitleEditor::delete_project_titles(
    const std::vector<std::string> &title_ids)
{
    if (title_ids.empty() || project_title_ids_.size() <= 1)
        return;
    if (QMessageBox::question(
            this, QStringLiteral("Delete Project Items"),
            title_ids.size() == 1
                ? QStringLiteral("Delete the selected title or graphic?")
                : QStringLiteral("Delete %1 selected project items?")
                      .arg(title_ids.size())) != QMessageBox::Yes)
        return;
    const std::set<std::string> deleting(title_ids.begin(), title_ids.end());
    const bool deleting_active = deleting.find(editing_title_id_) != deleting.end();
    project_title_ids_.erase(std::remove_if(
        project_title_ids_.begin(), project_title_ids_.end(),
        [&](const std::string &id) { return deleting.find(id) != deleting.end(); }),
        project_title_ids_.end());
    for (const std::string &id : deleting)
        TitleDataStore::instance().delete_title(id);
    if (project_title_ids_.empty()) {
        auto replacement = TitleDataStore::instance().create_title("Title 1");
        if (replacement) project_title_ids_.push_back(replacement->id);
    }
    project_dirty_ = true;
    if (deleting_active && !project_title_ids_.empty())
        open_title(project_title_ids_.front());
    set_dirty(true);
    refresh_project_panel();
}

void TitleEditor::import_project_titles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Import Title or Graphic"), QString(),
        QStringLiteral("Flux Motion Files (*.fxmt *.fxmp *.fxmproj);;All Files (*)"));
    if (paths.isEmpty()) return;
    std::string last_id;
    for (const QString &path : paths) {
        fxm::editor::ProjectLoadingDialog loading(this);
        TitleProject imported;
        std::string error;
        const bool ok = TitleDataStore::instance().read_project(
            path.toStdString(), &imported, &error,
            [&loading](const ProjectLoadProgress &state) {
                return loading.updateProgress(state);
            });
        loading.close();
        if (!ok) {
            if (error != "Project loading was cancelled.")
                QMessageBox::warning(this, QStringLiteral("Import Title"),
                                     QString::fromStdString(error));
            continue;
        }
        const auto added = TitleDataStore::instance().append_project(imported);
        project_title_ids_.insert(project_title_ids_.end(), added.begin(),
                                  added.end());
        if (!added.empty()) last_id = added.back();
    }
    if (last_id.empty()) return;
    project_dirty_ = true;
    open_title(last_id);
    set_dirty(true);
    refresh_project_panel();
}

void TitleEditor::export_project_titles(
    const std::vector<std::string> &title_ids)
{
    if (title_ids.empty()) return;
    if (dirty_ && !persist_title_changes(true, false))
        return;
    if (title_ids.size() == 1) {
        auto title = TitleDataStore::instance().get_title_snapshot(title_ids.front());
        if (!title) return;
        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export Title"),
            QString::fromStdString(title->name) + QStringLiteral(".fxmt"),
            QStringLiteral("Flux Motion Title (*.fxmt)"));
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".fxmt");
        std::string error;
        if (!TitleDataStore::instance().export_title(
                title->id, path.toStdString(), &error))
            QMessageBox::warning(this, QStringLiteral("Export Title"),
                                 QString::fromStdString(error));
        return;
    }
    TitleProject selection = current_project_;
    selection.id = TitleDataStore::make_uuid();
    selection.name = "Exported Titles";
    selection.titles.clear();
    for (const std::string &id : title_ids) {
        auto title = TitleDataStore::instance().get_title_snapshot(id);
        if (title) selection.titles.push_back(std::move(title));
    }
    if (selection.titles.empty()) return;
    selection.active_title_id = selection.titles.front()->id;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Project Items"),
        QStringLiteral("Exported Titles.fxmproj"),
        QStringLiteral("Flux Motion Project (*.fxmproj)"));
    if (path.isEmpty()) return;
    if (QFileInfo(path).suffix().isEmpty()) path += QStringLiteral(".fxmproj");
    std::string error;
    if (!TitleDataStore::instance().export_project(
            selection, path.toStdString(), &error))
        QMessageBox::warning(this, QStringLiteral("Export Project Items"),
                             QString::fromStdString(error));
}

void TitleEditor::reorder_project_titles(
    const std::vector<std::string> &title_ids)
{
    if (title_ids.empty()) return;
    project_title_ids_ = title_ids;
    TitleDataStore::instance().reorder_titles(title_ids);
    project_dirty_ = true;
    set_dirty(true);
}

bool TitleEditor::persist_title_changes(bool update_preview_screenshot, bool show_saved_status,
                                        bool autosave_operation)
{
    if (!title_) return false;

    if (autosave_operation)
        show_editor_activity(fxm_tr("OBSTitles.AutosavingStatus"), 0);
    else if (show_saved_status)
        show_editor_activity(fxm_tr("OBSTitles.SavingStatus"), 0);

    /* Saving can replace/copy the stored Title while a deferred cache render is
     * still holding the same title identity. Cancel only pending work for this
     * title first; cached frames remain available and are selectively
     * invalidated after the save through the normal modification path. */
    const QString cache_title_id = QString::fromStdString(
        editing_title_id_.empty() ? title_->id : editing_title_id_);
    CacheManager::instance().cancelTitleWork(cache_title_id);

    auto stored = TitleDataStore::instance().get_title(editing_title_id_.empty() ? title_->id : editing_title_id_);
    if (!stored) {
        stored = TitleDataStore::instance().create_title(title_->name);
        editing_title_id_ = stored->id;
        title_->id = stored->id;
    }
    copy_title_to_store(title_, stored);
    if (update_preview_screenshot) {
        // Reuse the already rendered editor frame instead of performing another
        // full title render during Save. This removes the largest UI-thread spike
        // for complex titles. Fall back only when no canvas frame exists yet.
        const bool destination_aware =
            title_requires_destination_compositing(*title_);
        const QImage current_preview = (!destination_aware && canvas_)
            ? canvas_->current_rendered_frame() : QImage();
        title_->preview_screenshot_png_base64 = current_preview.isNull()
            ? title_manual_screenshot_png_base64(*title_)
            : title_screenshot_png_base64(current_preview);
        stored->preview_screenshot_png_base64 = title_->preview_screenshot_png_base64;
    }
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save_async();
    emit title_saved(stored->id);
    set_dirty(false);
    const QString saved_time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    if (autosave_operation)
        show_editor_activity(fxm_tr("OBSTitles.AutosavedStatusFormat").arg(saved_time));
    else if (show_saved_status) {
        show_editor_activity(fxm_tr("OBSTitles.SavedStatusFormat").arg(saved_time));
    }
    return true;
}

bool TitleEditor::save_title()
{
    if (execution_context_.host == fxm::editor::EditorHostKind::Standalone) {
        if (current_project_path_.isEmpty() || current_project_requires_save_as_)
            return save_title_as();
        return save_project_to_path(current_project_path_, true);
    }
    return persist_title_changes(true, true);
}

bool TitleEditor::save_title_as()
{
    if (!title_ || !execution_context_.supports_save_as)
        return false;

    QString suggested_name = current_project_path_;
    if (current_project_requires_save_as_ && !suggested_name.isEmpty()) {
        const QFileInfo source(suggested_name);
        suggested_name = QDir(source.absolutePath()).filePath(
            source.completeBaseName() + QStringLiteral(".fxmproj"));
    }
    if (suggested_name.isEmpty()) {
        QString base = QString::fromStdString(title_->name).trimmed();
        if (base.isEmpty() || base.compare(QStringLiteral("Untitled"),
                                           Qt::CaseInsensitive) == 0)
            base = QStringLiteral("Untitled");
        base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                     QStringLiteral("_"));
        suggested_name = QDir(
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                             .filePath(base + QStringLiteral(".fxmproj"));
    }

    QString path = QFileDialog::getSaveFileName(
        this, fxm_tr("OBSTitles.SaveProjectAsTitle"), suggested_name,
        fxm_tr("OBSTitles.SaveProjectFileFilter"));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".fxmproj");

    return save_project_to_path(path, true);
}

bool TitleEditor::save_project_to_path(const QString &path,
                                       bool show_saved_status,
                                       bool autosave_operation)
{
    if (!title_ || path.trimmed().isEmpty())
        return false;

    if (!persist_title_changes(!autosave_operation, false,
                               autosave_operation))
        return false;

    std::string error;
    const QString absolute_path = QFileInfo(path).absoluteFilePath();
    TitleProject project = project_snapshot();
    project.name = QFileInfo(absolute_path).completeBaseName().toStdString();
    project.active_title_id = editing_title_id_;
    if (!TitleDataStore::instance().export_project(
            project, absolute_path.toStdString(), &error)) {
        set_dirty(true);
        QMessageBox::warning(
            this, fxm_tr("OBSTitles.SaveProjectAsTitle"),
            error.empty() ? fxm_tr("OBSTitles.ProjectSaveFailed")
                          : QString::fromStdString(error));
        return false;
    }

    current_project_path_ = absolute_path;
    current_project_requires_save_as_ = false;
    current_project_ = std::move(project);
    fxm::editor::RecentProjects::recordOpened(
        current_project_path_, QFileInfo(current_project_path_).completeBaseName(),
        QByteArray::fromStdString(title_->preview_screenshot_png_base64));
    emit recent_projects_changed();
    project_dirty_ = false;
    set_dirty(false);
    update_title_bar();
    const QString saved_time =
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    if (autosave_operation)
        show_editor_activity(fxm_tr("OBSTitles.AutosavedStatusFormat")
                                 .arg(saved_time));
    else if (show_saved_status)
        show_editor_activity(fxm_tr("OBSTitles.SavedStatusFormat")
                                 .arg(saved_time));
    return true;
}

void TitleEditor::refresh_flux_encoder_export_action()
{
    if (!act_export_media_)
        return;

    const fxm::encoder::Availability availability =
        fxm::encoder::FluxEncoderClient::instance().refreshAvailability();
    const bool encoder_available = availability.available();
    act_export_media_->setEnabled(execution_context_.supports_media_export &&
                                  encoder_available && title_);
    act_export_media_->setText(fxm_tr(
        encoder_available ? "OBSTitles.ExportAsMedia"
                          : "OBSTitles.ExportAsMediaRequiresEncoder"));
    const QString requirement = fxm_tr(
        encoder_available ? "OBSTitles.ExportAsMediaTooltip"
                          : "OBSTitles.FluxEncoderRequiredTooltip");
    act_export_media_->setToolTip(requirement);
    act_export_media_->setStatusTip(requirement);
}

void TitleEditor::export_as_media()
{
    if (!title_)
        return;

    const fxm::encoder::Availability availability =
        fxm::encoder::FluxEncoderClient::instance().refreshAvailability();
    if (!availability.available()) {
        refresh_flux_encoder_export_action();
        QMessageBox::information(
            this, fxm_tr("OBSTitles.ExportAsMedia"),
            fxm_tr("OBSTitles.FluxEncoderNotInstalled"));
        return;
    }

    /* Encoder reads the canonical project store. Persist dirty editor state and
     * flush any earlier asynchronous save before handing over the project ID. */
    if ((dirty_ || (execution_context_.host ==
                        fxm::editor::EditorHostKind::Standalone &&
                    current_project_path_.isEmpty())) &&
        !save_title())
        return;
    TitleDataStore::instance().save();

    fxm::encoder::ExportRequest request;
    request.project_id = QString::fromStdString(title_->id);
    request.project_name = QString::fromStdString(title_->name);
    if (const auto *provider = fxm::host_context_provider()) {
        request.project_store_path =
            QString::fromStdString(provider->config_path(""));
        request.project_scope =
            QString::fromStdString(provider->project_scope_name());
    }

    QString error;
    if (!fxm::encoder::FluxEncoderClient::instance().openExport(request,
                                                                 &error)) {
        QMessageBox::critical(
            this, fxm_tr("OBSTitles.FluxEncoderLaunchFailedTitle"),
            fxm_tr("OBSTitles.FluxEncoderLaunchFailedFormat").arg(error));
        refresh_flux_encoder_export_action();
        return;
    }

    show_editor_activity(fxm_tr("OBSTitles.FluxEncoderOpenedStatus"));
}

void TitleEditor::perform_autosave()
{
    if (shutting_down_)
        return;
    if (!TitlePreferences::autosave_enabled() || !title_ || !dirty_)
        return;

    if (restoring_undo_ || updating_layer_panels_ || QApplication::mouseButtons() != Qt::NoButton) {
        QTimer::singleShot(5000, this, &TitleEditor::perform_autosave);
        return;
    }

    if (execution_context_.host == fxm::editor::EditorHostKind::Standalone) {
        /* Never interrupt an Untitled first-save workflow with a background
         * file picker. It remains visibly unsaved until the user chooses Save. */
        if (current_project_path_.isEmpty() || current_project_requires_save_as_)
            return;
        save_project_to_path(current_project_path_, false, true);
    } else {
        persist_title_changes(false, false, true);
    }
}

void TitleEditor::update_autosave_timer()
{
    if (!autosave_timer_)
        return;

    const int minutes = TitlePreferences::autosave_interval_minutes();
    autosave_timer_->setInterval(minutes * 60 * 1000);
    if (TitlePreferences::autosave_enabled()) {
        if (!autosave_timer_->isActive())
            autosave_timer_->start();
    } else {
        autosave_timer_->stop();
    }
    update_statusbar_autosave_summary();
}

void TitleEditor::update_statusbar_autosave_summary()
{
    if (!status_activity_label_)
        return;

    if (TitlePreferences::autosave_enabled()) {
        status_activity_label_->setText(
            fxm_tr("OBSTitles.AutosaveEnabledStatusFormat")
                .arg(TitlePreferences::autosave_interval_minutes()));
    } else {
        status_activity_label_->setText(fxm_tr("OBSTitles.AutosaveDisabledStatus"));
    }
}

void TitleEditor::show_editor_activity(const QString &message, int timeout_ms)
{
    if (!status_activity_label_)
        return;

    status_activity_label_->setText(message);
    status_activity_label_->repaint();
    if (status_activity_timer_) {
        status_activity_timer_->stop();
        if (timeout_ms > 0)
            status_activity_timer_->start(timeout_ms);
    }
}

void TitleEditor::set_live_editing_enabled(bool enabled)
{
    live_editing_ = enabled;
    if (act_live_editing_ && act_live_editing_->isChecked() != enabled) {
        QSignalBlocker blocker(act_live_editing_);
        act_live_editing_->setChecked(enabled);
    }
    if (live_editing_ && dirty_)
        save_live_edit();
}

void TitleEditor::save_live_edit()
{
    if (!live_editing_ || !title_) return;
    persist_title_changes(false, false);
}

void TitleEditor::save_title_as_new()
{
    if (!title_) return;

    Title temp = *title_;
    temp.preview_screenshot_png_base64 = title_manual_screenshot_png_base64(temp);
    TitleTemplateExportMetadata metadata;
    metadata.screenshot_png_base64 = temp.preview_screenshot_png_base64;
    if (!prompt_editor_template_metadata(this, temp, metadata, fxm_tr("OBSTitles.SaveAsNew")))
        return;

    auto created = TitleDataStore::instance().create_title(metadata.title);
    title_->name = metadata.title;
    title_->description = metadata.description;
    title_->creator = metadata.creator;
    title_->creation_date = metadata.creation_date;
    title_->preview_screenshot_png_base64 = metadata.screenshot_png_base64;
    copy_title_to_store(title_, created);
    created->name = metadata.title;
    created->description = metadata.description;
    created->creator = metadata.creator;
    created->creation_date = metadata.creation_date;
    created->preview_screenshot_png_base64 = metadata.screenshot_png_base64;
    editing_title_id_ = created->id;
    title_->id = created->id;
    update_title_bar();
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save_async();
    emit title_saved(created->id);
    set_dirty(false);
}


void TitleEditor::save_title_as_asset()
{
    if (!title_)
        return;

    QDialog dialog(this);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.SaveAsAsset"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *name = new QLineEdit(QString::fromStdString(title_->name), &dialog);
    auto *category = new QLineEdit(QStringLiteral("Default"), &dialog);
    auto *tags = new QLineEdit(&dialog);
    tags->setPlaceholderText(fxm_tr("OBSTitles.AssetTagsPlaceholder"));
    form->addRow(fxm_tr("OBSTitles.AssetName"), name);
    form->addRow(fxm_tr("OBSTitles.Category"), category);
    form->addRow(fxm_tr("OBSTitles.TagsOptional"), tags);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        if (name->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, fxm_tr("OBSTitles.SaveAsAsset"),
                                 fxm_tr("OBSTitles.AssetNameRequired"));
            return;
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto stored = TitleDataStore::instance().create_title(name->text().trimmed().toStdString());
    copy_title_to_store(title_, stored);
    stored->name = name->text().trimmed().toStdString();
    stored->is_asset = true;
    stored->asset_animated = fxm::asset_runtime::title_has_timeline_animation(*stored);
    stored->asset_category = category->text().trimmed().isEmpty()
        ? std::string("Default") : category->text().trimmed().toStdString();
    const QStringList entered_tags = tags->text().split(
        QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    QSet<QString> unique_tags;
    for (const QString &entered : entered_tags) {
        const QString tag = entered.trimmed();
        const QString identity = tag.toCaseFolded();
        if (!tag.isEmpty() && !unique_tags.contains(identity)) {
            unique_tags.insert(identity);
            stored->asset_tags.push_back(tag.toStdString());
        }
    }
    stored->preview_screenshot_png_base64 = title_manual_screenshot_png_base64(*title_);
    stored->current_cue_row = -1;
    stored->pending_cue_row = -1;
    stored->playlist_active = false;
    stored->cue_persistence_transition = false;
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save_async();
    show_editor_activity(fxm_tr("OBSTitles.AssetSavedStatusFormat")
        .arg(QString::fromStdString(stored->name)));
}

void TitleEditor::export_title_template(bool save_in_library)
{
    if (!title_) return;

    Title temp = *title_;
    temp.preview_screenshot_png_base64 = title_manual_screenshot_png_base64(temp);
    TitleTemplateExportMetadata metadata;
    metadata.screenshot_png_base64 = temp.preview_screenshot_png_base64;
    if (!prompt_editor_template_metadata(this, temp, metadata))
        return;

    QString safe_name = QString::fromStdString(metadata.title).trimmed();
    if (safe_name.isEmpty()) safe_name = fxm_tr("OBSTitles.TemplateFileDialogTitle");
    safe_name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));

    QString path;
    QString selected_filter;
    if (save_in_library) {
        QStringList hierarchy;
        const QString category = editor_sanitized_template_category_path(QString::fromStdString(metadata.category));
        const QString subcategory = editor_sanitized_template_category_path(QString::fromStdString(metadata.subcategory));
        const QString collection = editor_sanitized_template_category_path(QString::fromStdString(metadata.collection));
        if (!category.isEmpty()) hierarchy << category;
        if (!subcategory.isEmpty()) hierarchy << subcategory;
        if (!collection.isEmpty()) hierarchy << collection;
        QString library_path = editor_template_library_root_path();
        if (!hierarchy.isEmpty()) library_path = QDir(library_path).filePath(hierarchy.join('/'));
        QDir().mkpath(library_path);
        path = QDir(library_path).filePath(safe_name + QStringLiteral(".fxmt"));
    } else {
        path = QFileDialog::getSaveFileName(this, fxm_tr("OBSTitles.ExportTitleTemplate"),
                                            QDir(editor_template_library_root_path()).filePath(safe_name + QStringLiteral(".fxmt")),
                                            fxm_tr("OBSTitles.TemplateFileFilter"), &selected_filter);
        if (path.isEmpty()) return;
        if (QFileInfo(path).suffix().isEmpty()) {
            path += selected_filter.contains(QStringLiteral("*.fxmp"))
                ? QStringLiteral(".fxmp") : QStringLiteral(".fxmt");
        }
    }

    const bool packed = QFileInfo(path).suffix().compare(
        QStringLiteral("fxmp"), Qt::CaseInsensitive) == 0;
    PackedTitleExportOptions packed_options;
    if (packed && !prompt_editor_packed_title_options(this, packed_options))
        return;

    auto stored = TitleDataStore::instance().create_title(metadata.title);
    copy_title_to_store(title_, stored);
    stored->name = metadata.title;
    stored->description = metadata.description;
    stored->creator = metadata.creator;
    stored->creation_date = metadata.creation_date;
    stored->preview_screenshot_png_base64 = metadata.screenshot_png_base64;

    std::string error;
    const bool exported = packed
        ? TitleDataStore::instance().export_packed_title(
              stored->id, path.toStdString(), metadata, packed_options, &error)
        : TitleDataStore::instance().export_title(
              stored->id, path.toStdString(), metadata, &error);
    if (!exported) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ExportTitleTemplate"), QString::fromStdString(error));
    } else {
        if (save_in_library) {
            QSettings library_state(QStringLiteral("FluxMotion"),
                                    QStringLiteral("Library"));
            library_state.setValue(
                QStringLiteral("contentRevision"),
                QDateTime::currentDateTimeUtc().toMSecsSinceEpoch());
            library_state.sync();
            /* In-process Library and Asset surfaces subscribe to the shared
             * store change channel; the persistent revision lets other Flux
             * Motion windows discover the publish without an application
             * restart. */
            TitleDataStore::instance().notify_change();
        }
        QMessageBox::information(this, fxm_tr("OBSTitles.ExportTitleTemplate"),
                                 fxm_tr("OBSTitles.ExportedStatusFormat").arg(QFileInfo(path).fileName()));
    }
    TitleDataStore::instance().delete_title(stored->id);
}

/* ── open_title ──────────────────────────────────────────────────── */
void TitleEditor::open_title(const std::string &tid)
{
    std::unique_ptr<fxm::editor::ProjectLoadingDialog> editor_loading;
    if (isVisible()) {
        editor_loading =
            std::make_unique<fxm::editor::ProjectLoadingDialog>(this);
        editor_loading->setWindowTitle(
            QStringLiteral("Preparing Flux Motion Editor"));
        editor_loading->setCancellable(false);
        editor_loading->updateEditorProgress(
            QStringLiteral("Loading title"), 10);
    }
    const auto report_editor_progress =
        [&editor_loading](const QString &step, int percent) {
            if (editor_loading)
                editor_loading->updateEditorProgress(step, percent);
        };

    show_editor_activity(fxm_tr("OBSTitles.LoadingTitleStatus"), 0);
    play_timer_->stop();
    release_editor_audio_preview();
    if (cache_invalidation_timer_)
        cache_invalidation_timer_->stop();
    CacheManager::instance().setInteractiveBypass(false);
    playing_ = false;
    pending_play_after_prerender_ = false;
    if (canvas_) canvas_->set_playback_active(false);
    act_play_->setText("▶");
    act_play_->setIcon(obs_icon("play.svg"));
    playhead_ = 0.0;
    playback_reverse_ = false;
    manual_reverse_playback_ = false;
    full_loop_playback_ = false;

    auto stored_title = TitleDataStore::instance().get_title_snapshot(tid);
    if (!stored_title) {
        if (editor_loading)
            editor_loading->close();
        show_editor_activity(fxm_tr("OBSTitles.LoadTitleFailedStatus"));
        return;
    }
    report_editor_progress(QStringLiteral("Preparing title data"), 28);
    CacheManager::instance().setEditorPrerenderFocus(QString::fromStdString(tid), true);
    if (execution_context_.host == fxm::editor::EditorHostKind::Standalone &&
        project_title_ids_.empty())
        project_title_ids_.push_back(tid);
    editing_title_id_ = tid;
    title_ = clone_title(*stored_title);
    if (execution_context_.host == fxm::editor::EditorHostKind::ObsPlugin) {
        const double authored_frame_rate = title_->frame_rate;
        if (execution_context_.host_canvas_width > 0 &&
            execution_context_.host_canvas_height > 0) {
            title_->width = std::clamp(
                execution_context_.host_canvas_width, 1, 16384);
            title_->height = std::clamp(
                execution_context_.host_canvas_height, 1, 16384);
        }
        if (std::isfinite(execution_context_.host_frame_rate) &&
            execution_context_.host_frame_rate > 0.0) {
            const double project_frame_rate = std::clamp(
                execution_context_.host_frame_rate, 1.0, 240.0);
            if (std::isfinite(authored_frame_rate) &&
                authored_frame_rate > 0.0 &&
                std::abs(authored_frame_rate - project_frame_rate) > 1.0e-6) {
                quantize_title_keyframes_to_frame_rate(
                    *title_, project_frame_rate);
            }
            title_->frame_rate = project_frame_rate;
        }
    }
    fxm::set_document_frame_rate(title_->frame_rate);
    reset_playback_timer_cadence();
    ensure_editor_audio_preview();
    external_data_visual_hash_ =
        CacheManager::instance().contentHashForTitle(*title_);
    load_new_layer_defaults();

    report_editor_progress(QStringLiteral("Populating editor panels"), 52);
    update_title_bar();
    canvas_->set_title(title_);
    layers_->set_title(title_);
    layers_->set_layer_clipboard_available(!layer_clipboard_.empty());
    timeline_->set_title(title_);
    props_->set_title(title_);
    title_props_->set_title(title_);
    refresh_color_swatches_panel();
    if (prerender_panel_) prerender_panel_->setTitle(title_);
    CacheManager::instance().restoreDiskStates(title_);
    CacheManager::instance().reprioritize(title_, playhead_);
    // Opening the editor is an explicit request to make this title responsive.
    // Queue the remaining timeline immediately; the cache scheduler will suspend
    // non-urgent live-cue/background jobs while this editor focus is active.
    CacheManager::instance().queueWholeTimeline(title_);

    report_editor_progress(QStringLiteral("Preparing timeline and preview"), 78);
    if (!title_->layers.empty())
        on_layer_selected(title_->layers.back()->id);
    else
        update_layer_panels(nullptr, playhead_);

    const auto settle_loaded_title_layout = [this, tid]() {
        if (!title_ || editing_title_id_ != tid)
            return;
        ensurePolished();
        if (layout())
            layout()->activate();
        if (canvas_) {
            canvas_->fit_canvas(true);
            canvas_->refresh_preview();
        }
        if (timeline_)
            timeline_->fit_timeline();
    };
    /* Title switches can happen while the editor page is hidden or while dock
     * restoration is still resizing the central widget. Settle immediately and
     * once more after the native window/layout transaction has completed. */
    QTimer::singleShot(0, this, settle_loaded_title_layout);
    QTimer::singleShot(120, this, settle_loaded_title_layout);

    undo_stack_.clear();
    undo_index_ = -1;
    push_undo_snapshot();
    update_undo_redo_actions();

    on_playhead_changed(0.0);
    set_dirty(false);
    if (project_panel_)
        project_panel_->setActiveTitle(tid);
    refresh_file_commands();
    if (editor_loading) {
        ensurePolished();
        if (layout())
            layout()->activate();
        editor_loading->updateEditorProgress(
            QStringLiteral("Editor ready"), 100);
        editor_loading->close();
    }
    show_editor_activity(
        fxm_tr("OBSTitles.LoadedTitleStatusFormat")
            .arg(QString::fromStdString(title_->name)));
}

std::shared_ptr<Title> TitleEditor::clone_title(const Title &title) const
{
    auto clone = std::make_shared<Title>(title);
    clone->layers.clear();
    clone->layers.reserve(title.layers.size());
    for (const auto &layer : title.layers) {
        if (layer) clone->layers.push_back(std::make_shared<Layer>(*layer));
    }
    return clone;
}


std::shared_ptr<Layer> TitleEditor::clone_layer_for_insert(const Layer &layer, bool suffix_name) const
{
    auto clone = std::make_shared<Layer>(layer);
    clone->id = TitleDataStore::make_uuid();
    if (suffix_name)
        clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy") : clone->name + editor_text_std("OBSTitles.CopySuffix");
    clone->name = unique_layer_name(clone->name);
    if (!clone->parent_id.empty() && (!title_ || !title_->find_layer(clone->parent_id)))
        clone->parent_id.clear();
    if (!clone->transform_parent_id.empty() &&
        (!title_ || !title_->find_layer(clone->transform_parent_id)))
        clone->transform_parent_id.clear();
    if (!clone->mask_source_id.empty() && (!title_ || !title_->find_layer(clone->mask_source_id))) {
        clone->mask_source_id.clear();
        clone->mask_mode = MaskMode::None;
    }
    return clone;
}

std::string TitleEditor::unique_layer_name(const std::string &base_name,
                                           const std::set<std::string> &exclude_ids,
                                           std::set<std::string> *reserved_names) const
{
    std::string base = QString::fromStdString(base_name).trimmed().toStdString();
    if (base.empty())
        base = editor_text_std("OBSTitles.Layer");

    std::set<std::string> used;
    if (title_) {
        for (const auto &layer : title_->layers) {
            if (!layer || exclude_ids.find(layer->id) != exclude_ids.end()) continue;
            used.insert(layer->name);
        }
    }
    if (reserved_names)
        used.insert(reserved_names->begin(), reserved_names->end());

    auto available = [&](const std::string &candidate) {
        return used.find(candidate) == used.end();
    };
    if (available(base)) {
        if (reserved_names) reserved_names->insert(base);
        return base;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2")
            .arg(QString::fromStdString(base))
            .arg(suffix, 2, 10, QChar('0'));
        const std::string value = candidate.toStdString();
        if (!available(value)) continue;
        if (reserved_names) reserved_names->insert(value);
        return value;
    }

    const std::string fallback = base + " " + TitleDataStore::make_uuid().substr(0, 8);
    if (reserved_names) reserved_names->insert(fallback);
    return fallback;
}

void TitleEditor::insert_layer_above(const std::string &anchor_id, std::shared_ptr<Layer> layer)
{
    if (!title_ || !layer) return;

    auto it = std::find_if(title_->layers.begin(), title_->layers.end(),
                           [&](const auto &candidate) {
                               return candidate && candidate->id == anchor_id;
                           });
    if (it == title_->layers.end())
        title_->layers.push_back(layer);
    else
        title_->layers.insert(it + 1, layer);
}

void TitleEditor::select_after_layer_list_mutation(const std::string &layer_id)
{
    layers_->refresh();
    timeline_->set_title(title_);
    on_layer_selected(layer_id);
}


std::vector<std::string> TitleEditor::selected_layer_ids_for_operation() const
{
    std::vector<std::string> requested = layers_ ? layers_->selected_ids() : std::vector<std::string>{};
    if (requested.empty() && !sel_layer_id_.empty())
        requested.push_back(sel_layer_id_);

    std::set<std::string> requested_set(requested.begin(), requested.end());
    std::vector<std::string> ordered_ids;
    if (!title_ || requested_set.empty()) return ordered_ids;

    for (const auto &layer : title_->layers) {
        if (layer && requested_set.find(layer->id) != requested_set.end())
            ordered_ids.push_back(layer->id);
    }
    return ordered_ids;
}

bool TitleEditor::boolean_shape_selection_available() const
{
    const auto ids = selected_layer_ids_for_operation();
    if (!title_ || ids.size() < 2)
        return false;
    for (const auto &id : ids) {
        const auto layer = title_->find_layer(id);
        if (!layer || layer->type != LayerType::Shape)
            return false;
    }
    return true;
}

void TitleEditor::apply_boolean_shape_operation(BooleanShapeOperation operation)
{
    if (!title_ || !boolean_shape_selection_available())
        return;

    const auto ids = selected_layer_ids_for_operation();
    std::set<std::string> selected_ids(ids.begin(), ids.end());
    std::vector<std::shared_ptr<Layer>> shapes;
    shapes.reserve(ids.size());
    for (const auto &layer : title_->layers) {
        if (!layer || selected_ids.find(layer->id) == selected_ids.end())
            continue;
        if (layer->locked || layer->shape_type == ShapeType::Line ||
            (layer->shape_type == ShapeType::Path && !layer->path_closed))
            return;
        shapes.push_back(layer);
    }
    if (shapes.size() < 2)
        return;

    std::vector<QPainterPath> canvas_paths;
    std::vector<QRectF> local_rects;
    std::vector<QTransform> world_transforms;
    canvas_paths.reserve(shapes.size());
    local_rects.reserve(shapes.size());
    world_transforms.reserve(shapes.size());
    for (const auto &layer : shapes) {
        const double local_time = std::clamp(playhead_ - layer->in_time, 0.0,
                                             std::max(0.0, layer->out_time - layer->in_time));
        const double width = std::max(1e-6, eval_box_width(*layer, local_time));
        const double height = std::max(1e-6, eval_box_height(*layer, local_time));
        const double origin_x = eval_origin_x(*layer, local_time);
        const double origin_y = eval_origin_y(*layer, local_time);
        const QRectF local_rect(-origin_x * width, -origin_y * height, width, height);
        const QTransform world_transform =
            editor_layer_world_transform_for_parenting(title_, *layer, playhead_);
        QPainterPath path = fxm::layer_shape_path(*layer, local_rect);
        path.setFillRule(Qt::OddEvenFill);
        path = world_transform.map(path);
        path.setFillRule(Qt::OddEvenFill);
        if (path.isEmpty())
            return;
        canvas_paths.push_back(path);
        local_rects.push_back(local_rect);
        world_transforms.push_back(world_transform);
    }

    QPainterPath result = canvas_paths.front();
    switch (operation) {
    case BooleanShapeOperation::Union:
        for (size_t i = 1; i < canvas_paths.size(); ++i)
            result = result.united(canvas_paths[i]);
        break;
    case BooleanShapeOperation::SubtractFront: {
        QPainterPath front = canvas_paths[1];
        for (size_t i = 2; i < canvas_paths.size(); ++i)
            front = front.united(canvas_paths[i]);
        result = result.subtracted(front);
        break;
    }
    case BooleanShapeOperation::Intersect:
        for (size_t i = 1; i < canvas_paths.size(); ++i)
            result = result.intersected(canvas_paths[i]);
        break;
    case BooleanShapeOperation::Exclude:
        for (size_t i = 1; i < canvas_paths.size(); ++i) {
            const QPainterPath overlap = result.intersected(canvas_paths[i]);
            result = result.united(canvas_paths[i]).subtracted(overlap);
        }
        break;
    }
    /* Do not call QPainterPath::simplified() here. It explicitly flattens
     * Bézier curves, which destroys rounded corners produced by primitive
     * shapes. The boolean result is already a valid filled path. */
    result = fxm::restore_boolean_path_curves(result, canvas_paths);
    result.setFillRule(Qt::OddEvenFill);

    const size_t style_source_index = operation == BooleanShapeOperation::SubtractFront
        ? 0
        : shapes.size() - 1;
    const auto style_source = shapes[style_source_index];
    const QRectF style_source_rect = local_rects[style_source_index];
    const QTransform style_source_world = world_transforms[style_source_index];
    const std::string result_id = TitleDataStore::make_uuid();
    std::shared_ptr<Layer> result_layer;

    QString operation_name;
    switch (operation) {
    case BooleanShapeOperation::Union:
        operation_name = fxm_tr("OBSTitles.BooleanUnion");
        break;
    case BooleanShapeOperation::SubtractFront:
        operation_name = fxm_tr("OBSTitles.BooleanSubtractFront");
        break;
    case BooleanShapeOperation::Intersect:
        operation_name = fxm_tr("OBSTitles.BooleanIntersect");
        break;
    case BooleanShapeOperation::Exclude:
        operation_name = fxm_tr("OBSTitles.BooleanExclude");
        break;
    }

    if (!result.isEmpty()) {
        const bool source_dependencies_survive =
            selected_ids.find(style_source->parent_id) == selected_ids.end() &&
            selected_ids.find(style_source->transform_parent_id) == selected_ids.end() &&
            selected_ids.find(style_source->mask_source_id) == selected_ids.end();
        const bool preserve_source_geometry = source_dependencies_survive &&
            editor_paths_geometrically_equal(result, canvas_paths[style_source_index]);

        if (preserve_source_geometry) {
            /* When the operation leaves the appearance-source shape unchanged
             * (for example, union with a fully contained shape), keep its
             * parametric corner radii and transform instead of baking it. */
            result_layer = std::make_shared<Layer>(*style_source);
            result_layer->id = result_id;
            result_layer->name = unique_layer_name(operation_name.toStdString(), selected_ids);
        }

        QRectF bounds = result.boundingRect();
        if (bounds.width() < 1.0) {
            const double center = bounds.center().x();
            bounds.setLeft(center - 0.5);
            bounds.setWidth(1.0);
        }
        if (bounds.height() < 1.0) {
            const double center = bounds.center().y();
            bounds.setTop(center - 0.5);
            bounds.setHeight(1.0);
        }

        std::vector<BezierPathPoint> points;
        if (!result_layer)
            points = fxm::painter_path_to_bezier_points(result, bounds, true);
        if (!result_layer && points.size() >= 3) {
            result_layer = std::make_shared<Layer>(*style_source);
            result_layer->id = result_id;
            result_layer->name = unique_layer_name(operation_name.toStdString(), selected_ids);
            result_layer->type = LayerType::Shape;
            result_layer->shape_type = ShapeType::Path;
            result_layer->path_points = std::move(points);
            result_layer->path_closed = true;
            result_layer->parent_id.clear();
            result_layer->transform_parent_id.clear();
            result_layer->mask_source_id.clear();
            result_layer->mask_mode = MaskMode::None;
            result_layer->position = AnimatedVec2Property{
                "position", {bounds.center().x(), bounds.center().y()}, {}};
            result_layer->scale = AnimatedVec2Property{"scale", {1.0, 1.0, 1.0}, {}};
            result_layer->rotation = AnimatedProperty{"rotation", 0.0, {}};
            result_layer->size = AnimatedVec2Property{
                "size", {bounds.width(), bounds.height()}, {}};
            result_layer->rect_width = (float)bounds.width();
            result_layer->rect_height = (float)bounds.height();
            result_layer->origin_x = 0.5f;
            result_layer->origin_y = 0.5f;
            result_layer->origin_prop = AnimatedVec2Property{"origin", {0.5, 0.5}, {}};
            result_layer->corner_radius = 0.0f;
            result_layer->corner_radius_tl = 0.0f;
            result_layer->corner_radius_tr = 0.0f;
            result_layer->corner_radius_br = 0.0f;
            result_layer->corner_radius_bl = 0.0f;
            result_layer->shape_roundness = 0.0f;
            result_layer->shape_inner_roundness = 0.0f;
