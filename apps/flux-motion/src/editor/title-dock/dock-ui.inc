struct DeleteTitleConfirmationResult {
    QMessageBox::StandardButton button = QMessageBox::No;
    bool delete_sources = false;
};

static DeleteTitleConfirmationResult confirm_delete_single_title(QWidget *parent, const Title &title)
{
    QMessageBox box(QMessageBox::Question,
                    fxm_tr("OBSTitles.DeleteTitle"),
                    fxm_tr("OBSTitles.DeleteTitleQuestionFormat").arg(QString::fromStdString(title.name)),
                    QMessageBox::Yes | QMessageBox::No,
                    parent);

    const QPixmap preview = title_preview_pixmap(title, QSize(240, 135));
    if (!preview.isNull())
        box.setIconPixmap(preview);

    auto *delete_sources_checkbox = new FxmSwitch(fxm_tr("OBSTitles.DeleteSourcesContainingTitle"), &box);
    delete_sources_checkbox->setToolTip(fxm_tr("OBSTitles.DeleteSourcesContainingTitleTooltip"));
    delete_sources_checkbox->setChecked(false);
    box.setCheckBox(delete_sources_checkbox);

    box.setDefaultButton(QMessageBox::No);

    DeleteTitleConfirmationResult result;
    result.button = static_cast<QMessageBox::StandardButton>(box.exec());
    result.delete_sources = delete_sources_checkbox->isChecked();
    return result;
}

static DeleteTitleConfirmationResult confirm_delete_multiple_titles(QWidget *parent, int count)
{
    QMessageBox box(QMessageBox::Question,
                    fxm_tr("OBSTitles.DeleteTitle"),
                    fxm_tr("OBSTitles.DeleteSelectedTitlesQuestionFormat").arg(count),
                    QMessageBox::Yes | QMessageBox::No,
                    parent);

    auto *delete_sources_checkbox = new FxmSwitch(fxm_tr("OBSTitles.DeleteSourcesContainingTitles"), &box);
    delete_sources_checkbox->setToolTip(fxm_tr("OBSTitles.DeleteSourcesContainingTitleTooltip"));
    delete_sources_checkbox->setChecked(false);
    box.setCheckBox(delete_sources_checkbox);
    box.setDefaultButton(QMessageBox::No);

    DeleteTitleConfirmationResult result;
    result.button = static_cast<QMessageBox::StandardButton>(box.exec());
    result.delete_sources = delete_sources_checkbox->isChecked();
    return result;
}

static int delete_title_sources_for_ids(const std::vector<std::string> &title_ids)
{
    auto *host = fxm::editor_host::editor_host();
    return host ? host->remove_title_sources(title_ids) : 0;
}

static QString title_screenshot_png_base64(const QImage &screenshot)
{
    if (screenshot.isNull())
        return QString();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!screenshot.save(&buffer, "PNG"))
        return QString();
    return QString::fromLatin1(png.toBase64());
}

static void populate_metadata_from_title(const Title &title, TitleTemplateExportMetadata &metadata)
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
}

static QString template_library_root_path();

static QStringList template_metadata_values(const QString &field)
{
    QSet<QString> values;
    QDirIterator it(template_library_root_path(), QStringList() << QStringLiteral("*.fxmt"),
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

static QComboBox *make_template_metadata_combo(QWidget *parent, const QString &field,
                                                const QString &current_value)
{
    auto *combo = new QComboBox(parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->addItem(fxm_tr("OBSTitles.TemplateMetadataAddNew"));
    combo->insertSeparator(1);
    combo->addItems(template_metadata_values(field));
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

static bool prompt_template_metadata(QWidget *parent, const Title &title,
                                     const QImage &screenshot,
                                     TitleTemplateExportMetadata &metadata,
                                     bool *save_in_template_library = nullptr,
                                     const QString &window_title = fxm_tr("OBSTitles.ExportTemplateDetails"))
{
    populate_metadata_from_title(title, metadata);

    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(window_title);
    dialog.setModal(true);
    dialog.resize(560, 500);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(obs_layout_spacing(&dialog));

    auto *preview_label = new QLabel(fxm_tr("OBSTitles.TemplateScreenshotPreviewLabel"), &dialog);
    set_bold_label(preview_label);
    layout->addWidget(preview_label);

    auto *preview = new QLabel(&dialog);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    preview->setMinimumHeight(160);
    if (!screenshot.isNull()) {
        preview->setPixmap(QPixmap::fromImage(screenshot).scaled(
            QSize(480, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else if (!metadata.screenshot_png_base64.empty()) {
        QPixmap pixmap;
        const QByteArray png = QByteArray::fromBase64(QByteArray::fromStdString(metadata.screenshot_png_base64));
        if (pixmap.loadFromData(png, "PNG"))
            preview->setPixmap(pixmap.scaled(QSize(480, 180), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
            preview->setText(fxm_tr("OBSTitles.TemplateScreenshotFailed"));
    } else {
        preview->setText(fxm_tr("OBSTitles.TemplateScreenshotFailed"));
    }
    layout->addWidget(preview);

    auto *form = new QFormLayout();
    auto *title_edit = new QLineEdit(QString::fromStdString(metadata.title), &dialog);
    auto *description_edit = new QTextEdit(&dialog);
    description_edit->setAcceptRichText(false);
    description_edit->setPlainText(QString::fromStdString(metadata.description));
    description_edit->setMinimumHeight(96);
    auto *creator_edit = new QLineEdit(QString::fromStdString(metadata.creator), &dialog);
    auto *date_edit = new QLineEdit(QString::fromStdString(metadata.creation_date), &dialog);
    auto *category_edit = make_template_metadata_combo(&dialog, QStringLiteral("category"), QString::fromStdString(metadata.category));
    auto *subcategory_edit = make_template_metadata_combo(&dialog, QStringLiteral("subcategory"), QString::fromStdString(metadata.subcategory));
    auto *collection_edit = make_template_metadata_combo(&dialog, QStringLiteral("collection"), QString::fromStdString(metadata.collection));

    form->addRow(fxm_tr("OBSTitles.TemplateExportTitleLabel"), title_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateExportDescriptionLabel"), description_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateExportCreatorLabel"), creator_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCreationDateLabel"), date_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCategoryLabel"), category_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateSubcategoryLabel"), subcategory_edit);
    form->addRow(fxm_tr("OBSTitles.TemplateCollectionLabel"), collection_edit);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    auto validate_and_accept = [&dialog, title_edit, save_in_template_library](bool save_to_library) {
        if (title_edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, fxm_tr("OBSTitles.ExportTemplateDetails"),
                                 fxm_tr("OBSTitles.TemplateExportTitleRequired"));
            return;
        }
        if (save_in_template_library)
            *save_in_template_library = save_to_library;
        dialog.accept();
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     [&validate_and_accept]() { validate_and_accept(false); });
    if (save_in_template_library) {
        auto *library_button = buttons->addButton(fxm_tr("OBSTitles.SaveInTemplateLibrary"), QDialogButtonBox::ActionRole);
        QObject::connect(library_button, &QPushButton::clicked, &dialog,
                         [&validate_and_accept]() { validate_and_accept(true); });
    }
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

struct TemplateLibraryEntry {
    int id;
    const char *title;
    const char *description;
    const char *default_name;
    const char *category;
    const char *subcategory;
    const char *collection;
    const char *file_name;
};

struct TemplateFileMetadata {
    QString title;
    QString description;
    QString creator;
    QString creation_date;
    QString category;
    QString subcategory;
    QString collection;
    QIcon screenshot_icon;
    QPixmap screenshot_pixmap;
    TitleGraphicType graphic_type = TitleGraphicType::Title;
};

static const std::array<const char *, 0> template_library_category_folders{};

static const std::array<TemplateLibraryEntry, 12> template_library_entries{{
    {101, "Breaking News Lower Third", "Bold breaking-news identification with headline and supporting line.",
     "BREAKING NEWS", "News", "Lower Thirds", "Big News Studio", "breaking-news-lower-third.fxmt"},
    {102, "Headline Strap", "Full-width headline strap for major developing stories.",
     "Major story develops", "News", "Straps", "Big News Studio", "headline-strap.fxmt"},
    {103, "Reporter ID", "On-air reporter identification with role and organization.",
     "REPORTER NAME", "News", "Lower Thirds", "Big News Studio", "reporter-id.fxmt"},
    {104, "Live Location", "Live location identifier with prominent LIVE status.",
     "ATHENS", "News", "Location IDs", "Big News Studio", "live-location.fxmt"},
    {105, "News Quote Card", "Editorial quote card for statements and key excerpts.",
     "A key statement from the story goes here.", "News", "Quote Cards", "Big News Studio", "news-quote-card.fxmt"},
    {106, "Full Screen Bulletin", "Full-screen bulletin for urgent headlines and summaries.",
     "BREAKING NEWS", "News", "Full Screen", "Big News Studio", "full-screen-bulletin.fxmt"},

    {201, "Podcast Host", "Friendly lower third for the podcast host.",
     "HOST NAME", "Podcast", "Lower Thirds", "Casual Podcast", "podcast-host.fxmt"},
    {202, "Podcast Guest", "Relaxed guest identification with a short role line.",
     "GUEST NAME", "Podcast", "Lower Thirds", "Casual Podcast", "podcast-guest.fxmt"},
    {203, "Episode Opener", "Centered episode title card with show and episode information.",
     "EPISODE TITLE", "Podcast", "Openers", "Casual Podcast", "episode-opener.fxmt"},
    {204, "Topic Card", "Simple discussion-topic card for chapter changes.",
     "TODAY'S TOPIC", "Podcast", "Topic Cards", "Casual Podcast", "topic-card.fxmt"},
    {205, "Social Handle", "Compact social-media handle and follow prompt.",
     "@yourhandle", "Podcast", "Social", "Casual Podcast", "social-handle.fxmt"},
    {206, "Podcast Break Screen", "Full-screen intermission graphic for short breaks.",
     "WE'LL BE RIGHT BACK", "Podcast", "Full Screen", "Casual Podcast", "podcast-break-screen.fxmt"},
}};


struct CannedTemplateDefinition {
    int id = 0;
    QString title;
    QString description;
    QString default_name;
    QString category;
    QString subcategory;
    QString collection;
    QString file_name;
};

static CannedTemplateDefinition canned_template_definition(const TemplateLibraryEntry &fallback)
{
    CannedTemplateDefinition result;
    result.id = fallback.id;
    result.title = QString::fromUtf8(fallback.title);
    result.description = QString::fromUtf8(fallback.description);
    result.default_name = QString::fromUtf8(fallback.default_name);
    result.category = QString::fromUtf8(fallback.category);
    result.subcategory = QString::fromUtf8(fallback.subcategory);
    result.collection = QString::fromUtf8(fallback.collection);
    result.file_name = QString::fromUtf8(fallback.file_name);

    const QString definition_name = QFileInfo(result.file_name).completeBaseName() + QStringLiteral(".json");
    const QString definition_path = fxm_asset_path(
        (QStringLiteral("canned-templates/") + definition_name)
            .toUtf8().constData());
    QFile file(definition_path);
    if (!definition_path.isEmpty() && file.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        if (!object.isEmpty()) {
            result.id = object.value(QStringLiteral("id")).toInt(result.id);
            result.title = object.value(QStringLiteral("title")).toString(result.title);
            result.description = object.value(QStringLiteral("description")).toString(result.description);
            result.default_name = object.value(QStringLiteral("default_name")).toString(result.default_name);
            result.category = object.value(QStringLiteral("category")).toString(result.category);
            result.subcategory = object.value(QStringLiteral("subcategory")).toString(result.subcategory);
            result.collection = object.value(QStringLiteral("collection")).toString(result.collection);
            result.file_name = object.value(QStringLiteral("file_name")).toString(result.file_name);
        }
    }
    return result;
}

static void remove_legacy_canned_templates(const QString &root_path)
{
    static const std::array<std::pair<const char *, const char *>, 5> legacy_templates{{
        {"Lower Thirds", "lower-third.fxmt"},
        {"Tickers", "ticker-strap.fxmt"},
        {"Clocks", "clock.fxmt"},
        {"Centered", "centered-title.fxmt"},
        {"Full Screen", "full-screen.fxmt"},
    }};
    for (const auto &legacy : legacy_templates)
        QFile::remove(QDir(QDir(root_path).filePath(QString::fromUtf8(legacy.first)))
                          .filePath(QString::fromUtf8(legacy.second)));
}

static const TemplateLibraryEntry *template_library_entry_by_id(int id)
{
    for (const auto &entry : template_library_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

static QString template_library_root_path()
{
    const auto *provider = fxm::host_context_provider();
    QString root = provider
        ? QString::fromStdString(provider->config_path("template-library"))
        : QDir::homePath() + QStringLiteral("/Flux Motion Templates");
    QDir().mkpath(root);
    return root;
}

static QString template_file_filter()
{
    return QStringLiteral("*.fxmp *.fxmt *.json");
}

static bool is_title_template_file_info(const QFileInfo &info)
{
    if (!info.exists() || !info.isFile() || !info.isReadable())
        return false;

    const QString suffix = info.suffix().toLower();
    return suffix == QStringLiteral("fxmp") ||
           suffix == QStringLiteral("fxmt") ||
           suffix == QStringLiteral("fxmproj") ||
           suffix == QStringLiteral("json");
}

static void append_title_template_path(QStringList &paths, const QString &raw_path)
{
    QString candidate = raw_path.trimmed();
    if (candidate.isEmpty())
        return;

    if (candidate.startsWith(QStringLiteral("file:"))) {
        const QUrl url(candidate);
        if (url.isLocalFile())
            candidate = url.toLocalFile();
    }

    candidate.remove(QChar('"'));
    const QFileInfo info(candidate);
    if (is_title_template_file_info(info))
        paths.push_back(info.absoluteFilePath());
}

static QStringList title_template_paths_from_mime(const QMimeData *mime)
{
    QStringList paths;
    if (!mime)
        return paths;

    if (mime->hasUrls()) {
        const auto urls = mime->urls();
        for (const QUrl &url : urls) {
            if (url.isLocalFile())
                append_title_template_path(paths, url.toLocalFile());
        }
    }

    // Fallback for sources that expose newline-separated paths or file:// URLs as text.
    if (mime->hasText()) {
        const QStringList lines = mime->text().split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            append_title_template_path(paths, line);
    }

    paths.removeDuplicates();
    return paths;
}

static bool mime_has_title_templates(const QMimeData *mime)
{
    return !title_template_paths_from_mime(mime).isEmpty();
}

static TemplateFileMetadata read_template_file_metadata(const QString &path)
{
    TemplateFileMetadata metadata;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        metadata.title = QFileInfo(path).completeBaseName();
        return metadata;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = doc.object();
    const QJsonObject meta = root.value(QStringLiteral("metadata")).toObject();
    metadata.title = meta.value(QStringLiteral("title")).toString(
        root.value(QStringLiteral("template_title")).toString(QFileInfo(path).completeBaseName()));
    metadata.description = meta.value(QStringLiteral("description")).toString(root.value(QStringLiteral("description")).toString());
    metadata.creator = meta.value(QStringLiteral("creator")).toString(root.value(QStringLiteral("creator")).toString());
    metadata.creation_date = meta.value(QStringLiteral("creation_date")).toString(root.value(QStringLiteral("creation_date")).toString());
    metadata.category = meta.value(QStringLiteral("category")).toString(root.value(QStringLiteral("category")).toString()).trimmed();
    metadata.subcategory = meta.value(QStringLiteral("subcategory")).toString(root.value(QStringLiteral("subcategory")).toString()).trimmed();
    metadata.collection = meta.value(QStringLiteral("collection")).toString(root.value(QStringLiteral("collection")).toString()).trimmed();
    const QJsonObject title_object = root.value(QStringLiteral("title")).toObject();
    metadata.graphic_type = static_cast<TitleGraphicType>(std::clamp(
        title_object.value(QStringLiteral("graphic_type")).toInt(0), 0, 3));

    QJsonObject screenshot = meta.value(QStringLiteral("screenshot")).toObject();
    if (screenshot.isEmpty())
        screenshot = root.value(QStringLiteral("screenshot")).toObject();
    const QByteArray png = QByteArray::fromBase64(screenshot.value(QStringLiteral("data_base64")).toString().toLatin1());
    QPixmap pixmap;
    if (!png.isEmpty() && pixmap.loadFromData(png, "PNG")) {
        metadata.screenshot_pixmap = pixmap;
        QPixmap composed = pixmap.scaled(QSize(96, 54), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (metadata.graphic_type == TitleGraphicType::Stinger) {
            QPainter painter(&composed);
            const QPixmap badge = obs_icon("stinger.svg", QColor(255, 255, 255)).pixmap(QSize(18, 18));
            painter.setBrush(QColor(0, 0, 0, 170));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(QRect(3, 3, 24, 24), 4, 4);
            painter.drawPixmap(6, 6, badge);
        }
        metadata.screenshot_icon = QIcon(composed);
    } else if (metadata.graphic_type == TitleGraphicType::Stinger) {
        metadata.screenshot_icon = obs_icon("stinger.svg", QColor(205, 205, 205));
    }
    return metadata;
}

static bool write_template_file_metadata(const QString &path, const TemplateFileMetadata &metadata, QString *error = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = fxm_tr("OBSTitles.TemplateEditReadFailed");
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        if (error) *error = fxm_tr("OBSTitles.TemplateEditReadFailed");
        return false;
    }

    QJsonObject root = doc.object();
    QJsonObject meta = root.value(QStringLiteral("metadata")).toObject();
    meta[QStringLiteral("title")] = metadata.title;
    meta[QStringLiteral("description")] = metadata.description;
    meta[QStringLiteral("creator")] = metadata.creator;
    meta[QStringLiteral("creation_date")] = metadata.creation_date;
    meta[QStringLiteral("category")] = metadata.category;
    meta[QStringLiteral("subcategory")] = metadata.subcategory;
    meta[QStringLiteral("collection")] = metadata.collection;
    if (!root.value(QStringLiteral("screenshot")).isUndefined())
        meta[QStringLiteral("screenshot")] = root.value(QStringLiteral("screenshot"));

    root[QStringLiteral("template_title")] = metadata.title;
    root[QStringLiteral("description")] = metadata.description;
    root[QStringLiteral("creator")] = metadata.creator;
    root[QStringLiteral("creation_date")] = metadata.creation_date;
    root[QStringLiteral("category")] = metadata.category;
    root[QStringLiteral("subcategory")] = metadata.subcategory;
    root[QStringLiteral("collection")] = metadata.collection;
    root[QStringLiteral("metadata")] = meta;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = fxm_tr("OBSTitles.TemplateEditWriteFailed");
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.flush()) {
        if (error) *error = fxm_tr("OBSTitles.TemplateEditWriteFailed");
        return false;
    }
    return true;
}

static bool prompt_edit_template_file_metadata(QWidget *parent, const QString &path)
{
    TemplateFileMetadata metadata = read_template_file_metadata(path);

    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.EditTemplateMetadata"));
    dialog.setModal(true);
    dialog.resize(520, 360);

    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout();
    auto *title_edit = new QLineEdit(metadata.title, &dialog);
    auto *description_edit = new QTextEdit(&dialog);
    description_edit->setAcceptRichText(false);
    description_edit->setPlainText(metadata.description);
    description_edit->setMinimumHeight(110);
    auto *creator_edit = new QLineEdit(metadata.creator, &dialog);
    auto *date_edit = new QLineEdit(metadata.creation_date, &dialog);
    auto *category_edit = make_template_metadata_combo(&dialog, QStringLiteral("category"), metadata.category);
    auto *subcategory_edit = make_template_metadata_combo(&dialog, QStringLiteral("subcategory"), metadata.subcategory);
    auto *collection_edit = make_template_metadata_combo(&dialog, QStringLiteral("collection"), metadata.collection);

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
            QMessageBox::warning(&dialog, fxm_tr("OBSTitles.EditTemplateMetadata"),
                                 fxm_tr("OBSTitles.TemplateExportTitleRequired"));
            return;
        }
        dialog.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    metadata.title = title_edit->text().trimmed();
    metadata.description = description_edit->toPlainText().trimmed();
    metadata.creator = creator_edit->text().trimmed();
    metadata.creation_date = date_edit->text().trimmed();
    metadata.category = category_edit->currentText().trimmed();
    metadata.subcategory = subcategory_edit->currentText().trimmed();
    metadata.collection = collection_edit->currentText().trimmed();

    QString error;
    if (!write_template_file_metadata(path, metadata, &error)) {
        QMessageBox::warning(parent, fxm_tr("OBSTitles.EditTemplateMetadata"), error);
        return false;
    }
    return true;
}


struct TemplateLibraryRecord {
    QString path;
    TemplateFileMetadata metadata;
};

static QList<TemplateLibraryRecord> scan_template_library(const QString &root_path)
{
    QList<TemplateLibraryRecord> records;
    QDirIterator it(root_path, template_file_filter().split(' ', Qt::SkipEmptyParts),
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        TemplateFileMetadata metadata = read_template_file_metadata(path);
        // Backward compatibility: old libraries used the first physical folder as category.
        if (metadata.category.isEmpty()) {
            const QString relative = QDir(root_path).relativeFilePath(path);
            const QStringList parts = relative.split('/', Qt::SkipEmptyParts);
            if (parts.size() > 1) metadata.category = parts.first();
        }
        records.push_back({path, metadata});
    }
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.metadata.title, b.metadata.title, Qt::CaseInsensitive) < 0;
    });
    return records;
}

static QString template_group_name(const QString &value)
{
    return value.trimmed().isEmpty() ? fxm_tr("OBSTitles.TemplateUncategorized") : value.trimmed();
}

static QTreeWidgetItem *ensure_template_group(QTreeWidget *tree, QTreeWidgetItem *parent,
                                               QHash<QString, QTreeWidgetItem *> &items,
                                               const QString &key, const QString &label)
{
    if (items.contains(key)) return items.value(key);
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, label);
    item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    items.insert(key, item);
    return item;
}

static void populate_dynamic_template_tree(QTreeWidget *tree,
                                           const QList<TemplateLibraryRecord> &records,
                                           bool by_collection)
{
    tree->clear();
    tree->setColumnCount(1);
    tree->setHeaderHidden(true);
    QHash<QString, QTreeWidgetItem *> items;
    for (const auto &record : records) {
        const QString category = template_group_name(record.metadata.category);
        const QString subcategory = template_group_name(record.metadata.subcategory);
        const QString collection = template_group_name(record.metadata.collection);
        const QStringList labels = by_collection
            ? QStringList{collection, category, subcategory}
            : QStringList{category, subcategory, collection};
        QTreeWidgetItem *parent = nullptr;
        QString key;
        for (int depth = 0; depth < labels.size(); ++depth) {
            if (!key.isEmpty()) key += QLatin1Char('/');
            key += labels[depth].toCaseFolded();
            parent = ensure_template_group(tree, parent, items, key, labels[depth]);
            parent->setData(0, TemplateFilterCategoryRole, record.metadata.category);
            parent->setData(0, TemplateFilterSubcategoryRole, record.metadata.subcategory);
            parent->setData(0, TemplateFilterCollectionRole, record.metadata.collection);
            parent->setData(0, TemplateFilterDepthRole, depth + 1);
        }
    }
    tree->expandAll();
    if (tree->topLevelItemCount() > 0) tree->setCurrentItem(tree->topLevelItem(0));
}

static bool template_matches_tree_item(const TemplateLibraryRecord &record,
                                       const QTreeWidgetItem *item, bool by_collection)
{
    if (!item) return false;
    QStringList selected;
    for (auto *cursor = item; cursor; cursor = cursor->parent()) selected.prepend(cursor->text(0));
    const QStringList actual = by_collection
        ? QStringList{template_group_name(record.metadata.collection),
                      template_group_name(record.metadata.category),
                      template_group_name(record.metadata.subcategory)}
        : QStringList{template_group_name(record.metadata.category),
                      template_group_name(record.metadata.subcategory),
                      template_group_name(record.metadata.collection)};
    if (selected.size() > actual.size()) return false;
    for (int i = 0; i < selected.size(); ++i)
        if (selected[i].compare(actual[i], Qt::CaseInsensitive) != 0) return false;
    return true;
}
static QTreeWidgetItem *add_template_category_item(QTreeWidget *tree, QTreeWidgetItem *parent,
                                                   const QFileInfo &dir_info)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
    item->setText(0, QStringLiteral("×  ") + dir_info.fileName());
    item->setToolTip(0, dir_info.absoluteFilePath());
    item->setData(0, Qt::UserRole, dir_info.absoluteFilePath());
    item->setData(0, TemplateCategoryNameRole, dir_info.fileName());
    item->setFlags(item->flags() | Qt::ItemIsDropEnabled);

    QDir dir(dir_info.absoluteFilePath());
    const auto children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &child : children)
        add_template_category_item(tree, item, child);
    return item;
}

static void populate_template_categories(QTreeWidget *tree, const QString &root_path)
{
    tree->clear();
    tree->setColumnCount(1);
    tree->setHeaderHidden(true);
    for (const char *folder : template_library_category_folders)
        QDir(root_path).mkpath(QString::fromUtf8(folder));

    QDir root(root_path);
    std::vector<QString> known;
    known.reserve(template_library_category_folders.size());
    for (const char *folder : template_library_category_folders) {
        const QString name = QString::fromUtf8(folder);
        known.push_back(name);
        add_template_category_item(tree, nullptr, QFileInfo(root.filePath(name)));
    }
    for (const QFileInfo &dir : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name)) {
        if (std::find(known.begin(), known.end(), dir.fileName()) == known.end())
            add_template_category_item(tree, nullptr, dir);
    }

    tree->expandAll();
    if (tree->topLevelItemCount() > 0)
        tree->setCurrentItem(tree->topLevelItem(0));
}

static void collect_template_category_paths(const QDir &root, const QString &relative_dir, QStringList &categories)
{
    const QString scan_path = relative_dir.isEmpty() ? root.absolutePath() : root.filePath(relative_dir);
    QDir dir(scan_path);
    const auto children = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &child : children) {
        const QString relative_child = relative_dir.isEmpty()
            ? child.fileName()
            : relative_dir + QStringLiteral("/") + child.fileName();
        categories << relative_child;
        collect_template_category_paths(root, relative_child, categories);
    }
}

static QStringList template_library_category_paths(const QString &root_path)
{
    for (const char *folder : template_library_category_folders)
        QDir(root_path).mkpath(QString::fromUtf8(folder));

    QStringList categories;
    QDir root(root_path);
    collect_template_category_paths(root, QString(), categories);
    categories.removeDuplicates();
    categories.sort(Qt::CaseInsensitive);
    return categories;
}

static QString sanitized_template_category_path(QString category)
{
    category.replace('\\', '/');
    QStringList safe_parts;
    for (QString part : category.split('/', Qt::SkipEmptyParts)) {
        part = part.trimmed();
        part.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
        if (!part.isEmpty() && part != QStringLiteral(".") && part != QStringLiteral(".."))
            safe_parts << part;
    }
    return safe_parts.join(QStringLiteral("/"));
}

static bool rename_template_category(QWidget *parent, QTreeWidgetItem *item)
{
    if (!item) return false;

    const QString category_path = item->data(0, Qt::UserRole).toString();
    if (category_path.isEmpty()) return false;

    QFileInfo category_info(category_path);
    bool ok = false;
    const QString current_name = item->data(0, TemplateCategoryNameRole).toString().isEmpty()
        ? category_info.fileName()
        : item->data(0, TemplateCategoryNameRole).toString();
    QString name = QInputDialog::getText(
        parent, fxm_tr("OBSTitles.RenameCategory"), fxm_tr("OBSTitles.CategoryNamePrompt"),
        QLineEdit::Normal, current_name, &ok);
    if (!ok) return false;

    name = sanitized_template_category_path(name);
    if (name.isEmpty() || name.contains('/')) {
        QMessageBox::warning(parent, fxm_tr("OBSTitles.RenameCategory"),
                             fxm_tr("OBSTitles.TemplateLibraryCategoryRequired"));
        return false;
    }

    const QString parent_path = category_info.dir().absolutePath();
    const QString new_path = QDir(parent_path).filePath(name);
    if (QDir::cleanPath(new_path) == QDir::cleanPath(category_path))
        return false;

    if (QFileInfo::exists(new_path) || !QDir(parent_path).rename(category_info.fileName(), name)) {
        QMessageBox::warning(parent, fxm_tr("OBSTitles.RenameCategory"),
                             fxm_tr("OBSTitles.RenameCategoryFailed"));
        return false;
    }

    return true;
}

static bool delete_template_category(QWidget *parent, QTreeWidgetItem *item, const QString &root_path)
{
    if (!item) return false;

    const QString category_path = item->data(0, Qt::UserRole).toString();
    if (category_path.isEmpty()) return false;
    if (QDir::cleanPath(category_path) == QDir::cleanPath(root_path)) return false;

    const auto reply = QMessageBox::question(
        parent, fxm_tr("OBSTitles.DeleteCategory"),
        fxm_tr("OBSTitles.DeleteCategoryQuestionFormat").arg(item->data(0, TemplateCategoryNameRole).toString()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return false;

    if (!QDir(category_path).removeRecursively()) {
        QMessageBox::warning(parent, fxm_tr("OBSTitles.DeleteCategory"),
                             fxm_tr("OBSTitles.DeleteCategoryFailed"));
        return false;
    }

    return true;
}

static bool prompt_template_library_category(QWidget *parent, QString &category)
{
    const QString root_path = template_library_root_path();
    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.TemplateLibraryCategoryTitle"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(obs_layout_spacing(&dialog));

    auto *prompt = new QLabel(fxm_tr("OBSTitles.TemplateLibraryCategoryPrompt"), &dialog);
    prompt->setWordWrap(true);
    layout->addWidget(prompt);

    auto *combo = new QComboBox(&dialog);
    combo->setEditable(true);
    combo->addItems(template_library_category_paths(root_path));
    layout->addWidget(combo);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString safe_category = sanitized_template_category_path(combo->currentText());
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

static bool prompt_packed_title_options(QWidget *parent,
                                        PackedTitleExportOptions &options)
{
    QDialog dialog(parent);
    fxm_apply_brand_icon(&dialog);
    dialog.setWindowTitle(fxm_tr("OBSTitles.PackedOptionsTitle"));
    dialog.setModal(true);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(obs_layout_spacing(&dialog));
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

} // namespace
