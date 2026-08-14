    auto add_rect = [&](const std::string &layer_name,
                        double x, double y, float w, float h,
                        uint32_t color, float radius = 0.0f) {
        auto layer = std::make_shared<Layer>();
        layer->id = TitleDataStore::make_uuid();
        layer->name = layer_name;
        layer->type = LayerType::SolidRect;
        layer->position.static_value.x = x;
        layer->position.static_value.y = y;
        layer->rect_width = w;
        layer->rect_height = h;
        layer->size.static_value.x = w;
        layer->size.static_value.y = h;
        layer->corner_radius = radius;
        layer->corner_radius_tl = radius;
        layer->corner_radius_tr = radius;
        layer->corner_radius_br = radius;
        layer->corner_radius_bl = radius;
        layer->corner_radius_locked = true;
        layer->fill_color = color;
        layer->fill_color_a.static_value = (color >> 24) & 0xFF;
        layer->fill_color_r.static_value = (color >> 16) & 0xFF;
        layer->fill_color_g.static_value = (color >> 8) & 0xFF;
        layer->fill_color_b.static_value = color & 0xFF;
        layer->out_time = title->duration;
        title->layers.push_back(layer);
        return layer;
    };

    auto add_text = [&](const std::string &layer_name,
                        const std::string &text,
                        double x, double y, int size,
                        uint32_t color, bool bold = false,
                        int align_h = 1, int align_v = 1) {
        auto layer = std::make_shared<Layer>();
        layer->id = TitleDataStore::make_uuid();
        layer->name = layer_name;
        layer->type = LayerType::Text;
        layer->text_content = text;
        layer->expose_text = true;
        layer->font_family = "Arial";
        layer->font_size = size;
        layer->font_bold = bold;
        layer->text_color = color;
        layer->text_color_a.static_value = (color >> 24) & 0xFF;
        layer->text_color_r.static_value = (color >> 16) & 0xFF;
        layer->text_color_g.static_value = (color >> 8) & 0xFF;
        layer->text_color_b.static_value = color & 0xFF;
        layer->rect_width = 960.0f;
        layer->rect_height = 160.0f;
        layer->size.static_value.x = layer->rect_width;
        layer->size.static_value.y = layer->rect_height;
        layer->position.static_value.x = x;
        layer->position.static_value.y = y;
        layer->align_h = align_h;
        layer->align_v = align_v;

        /* The GPU text renderer consumes the canonical rich-text document,
         * not the legacy mirror fields above.  Build it only after all font,
         * colour and paragraph defaults have been assigned so canned
         * template text is visible immediately and preserves its styling. */
        layer->rich_text = rich_text_document_from_layer_defaults(*layer);
        rich_text_document_ensure_canonical(*layer);
        rich_text_document_sync_layer_mirrors(*layer);

        layer->out_time = title->duration;
        title->layers.push_back(layer);
        return layer;
    };

    switch (template_id) {
    case 101: { /* Big News Studio — Breaking News Lower Third */
        title->duration = 8.0;
        add_rect("Breaking News Backplate", 690, 860, 1260, 150, 0xF0121720, 8.0f);
        add_rect("Breaking News Accent", 170, 860, 220, 150, 0xFFE5232A, 8.0f);
        add_text("Breaking Label", "BREAKING", 170, 836, 42, 0xFFFFFFFF, true, 1, 1);
        add_text("Headline", name, 770, 828, 58, 0xFFFFFFFF, true, 0, 1);
        add_text("Supporting Line", "Developing story • Add details here", 770, 895, 30, 0xFFD9DEE7, false, 0, 1);
        break;
    }
    case 102: { /* Big News Studio — Headline Strap */
        title->duration = 10.0;
        add_rect("Headline Strap", 960, 985, 1920, 190, 0xF00D1118, 0.0f);
        add_rect("Top Red Rule", 960, 888, 1920, 10, 0xFFE5232A, 0.0f);
        add_rect("News Label Box", 175, 985, 270, 190, 0xFFE5232A, 0.0f);
        add_text("News Label", "BIG NEWS", 175, 970, 46, 0xFFFFFFFF, true, 1, 1);
        add_text("Main Headline", name, 1100, 950, 52, 0xFFFFFFFF, true, 0, 1);
        add_text("Headline Detail", "Short supporting information or developing update", 1100, 1025, 30, 0xFFCBD2DD, false, 0, 1);
        break;
    }
    case 103: { /* Big News Studio — Reporter ID */
        title->duration = 8.0;
        add_rect("Reporter Plate", 560, 858, 980, 142, 0xED101722, 14.0f);
        add_rect("Reporter Accent", 95, 858, 26, 142, 0xFFE5232A, 10.0f);
        add_text("Reporter Name", name, 590, 827, 52, 0xFFFFFFFF, true, 0, 1);
        add_text("Reporter Role", "Senior Correspondent", 590, 890, 30, 0xFFB8C3D3, false, 0, 1);
        break;
    }
    case 104: { /* Big News Studio — Live Location */
        title->duration = 8.0;
        add_rect("Location Plate", 360, 110, 620, 104, 0xF0101620, 10.0f);
        add_rect("Live Box", 102, 110, 180, 104, 0xFFE5232A, 10.0f);
        add_text("Live Label", "LIVE", 102, 110, 42, 0xFFFFFFFF, true, 1, 1);
        add_text("Location", name, 430, 110, 42, 0xFFFFFFFF, true, 0, 1);
        break;
    }
    case 105: { /* Big News Studio — Quote Card */
        title->duration = 9.0;
        add_rect("Quote Background", 960, 540, 1450, 610, 0xF00C1119, 28.0f);
        add_rect("Quote Accent", 310, 540, 18, 450, 0xFFE5232A, 9.0f);
        add_text("Quote Mark", "“", 420, 350, 140, 0xFFE5232A, true, 0, 1);
        auto quote = add_text("Quote", name, 1000, 520, 54, 0xFFFFFFFF, true, 0, 1);
        quote->rect_width = 1120.0f;
        quote->size.static_value.x = quote->rect_width;
        add_text("Attribution", "SOURCE / TITLE", 1000, 740, 30, 0xFFB8C3D3, true, 0, 1);
        break;
    }
    case 106: { /* Big News Studio — Full Screen Bulletin */
        title->duration = 8.0;
        add_rect("Bulletin Background", 960, 540, 1920, 1080, 0xFF0B1018, 0.0f);
        add_rect("Red Header", 960, 165, 1920, 330, 0xFFE5232A, 0.0f);
        add_text("Bulletin Label", "NEWS BULLETIN", 960, 130, 44, 0xFFFFFFFF, true, 1, 1);
        add_text("Bulletin Headline", name, 960, 455, 94, 0xFFFFFFFF, true, 1, 1);
        add_text("Bulletin Summary", "Add a concise summary of the developing story here.", 960, 625, 42, 0xFFD8DEE8, false, 1, 1);
        add_rect("Bottom Rule", 960, 865, 1280, 8, 0xFFE5232A, 4.0f);
        break;
    }
    case 201: { /* Casual Podcast — Host */
        title->duration = 8.0;
        add_rect("Host Card", 500, 850, 900, 150, 0xF6F4E9D8, 28.0f);
        add_rect("Host Accent", 95, 850, 34, 150, 0xFFFF8A5B, 17.0f);
        add_text("Host Name", name, 540, 818, 54, 0xFF262B33, true, 0, 1);
        add_text("Host Role", "HOST", 540, 885, 28, 0xFF6B625D, true, 0, 1);
        break;
    }
    case 202: { /* Casual Podcast — Guest */
        title->duration = 8.0;
        add_rect("Guest Card", 510, 850, 920, 150, 0xF6E6F2F1, 28.0f);
        add_rect("Guest Accent", 100, 850, 36, 150, 0xFF49A6A2, 18.0f);
        add_text("Guest Name", name, 550, 818, 54, 0xFF223033, true, 0, 1);
        add_text("Guest Role", "GUEST • Add role or organization", 550, 885, 28, 0xFF587174, false, 0, 1);
        break;
    }
    case 203: { /* Casual Podcast — Episode Opener */
        title->duration = 7.0;
        add_rect("Episode Background", 960, 540, 1920, 1080, 0xFFF4E9D8, 0.0f);
        add_rect("Episode Blob", 960, 540, 1180, 560, 0xFFFF8A5B, 72.0f);
        add_text("Show Name", "CASUAL PODCAST", 960, 350, 34, 0xFF5F392D, true, 1, 1);
        add_text("Episode Title", name, 960, 515, 88, 0xFFFFFFFF, true, 1, 1);
        add_text("Episode Number", "EPISODE 01 • NEW CONVERSATIONS", 960, 670, 30, 0xFFFFF5EE, true, 1, 1);
        break;
    }
    case 204: { /* Casual Podcast — Topic Card */
        title->duration = 6.0;
        add_rect("Topic Card", 960, 540, 1320, 430, 0xF6E6F2F1, 42.0f);
        add_rect("Topic Dot", 430, 430, 82, 82, 0xFF49A6A2, 41.0f);
        add_text("Topic Label", "NOW TALKING ABOUT", 960, 425, 30, 0xFF4D7374, true, 1, 1);
        add_text("Topic", name, 960, 555, 78, 0xFF223033, true, 1, 1);
        break;
    }
    case 205: { /* Casual Podcast — Social Handle */
        title->duration = 8.0;
        add_rect("Social Card", 410, 930, 730, 112, 0xF6FFFFFF, 56.0f);
        add_rect("Social Icon", 105, 930, 86, 86, 0xFFFF8A5B, 43.0f);
        add_text("Social Symbol", "@", 105, 930, 44, 0xFFFFFFFF, true, 1, 1);
        add_text("Social Handle", name, 455, 913, 42, 0xFF252A31, true, 0, 1);
        add_text("Follow Prompt", "FOLLOW THE CONVERSATION", 455, 958, 22, 0xFF7D7772, true, 0, 1);
        break;
    }
    case 206: { /* Casual Podcast — Break Screen */
        title->duration = 10.0;
        add_rect("Break Background", 960, 540, 1920, 1080, 0xFF223033, 0.0f);
        add_rect("Warm Panel", 960, 540, 1320, 650, 0xFFFF8A5B, 70.0f);
        add_text("Break Message", name, 960, 490, 86, 0xFFFFFFFF, true, 1, 1);
        add_text("Break Detail", "Grab a drink — the conversation continues shortly.", 960, 620, 38, 0xFFFFF2EA, false, 1, 1);
        add_text("Show Name", "CASUAL PODCAST", 960, 785, 28, 0xFF6A3D30, true, 1, 1);
        break;
    }
    default: {
        add_text("Template Title", name, 960, 540, 72, 0xFFFFFFFF, true, 1, 1);
        break;
    }
    }

    for (auto &layer : title->layers)
        layer->out_time = title->duration;

    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    return title;
}

void TitleDock::create_title_from_template(const std::string &default_name,
                                           int template_id)
{
    bool ok = false;
    QString name = QInputDialog::getText(
        this, fxm_tr("OBSTitles.NewTemplateTitle"), fxm_tr("OBSTitles.TitleTextPrompt"), QLineEdit::Normal,
        QString::fromStdString(default_name), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    auto title = create_template_title(name.trimmed().toStdString(), template_id);
    select_title(title->id);
    on_edit();
}

/* ══════════════════════════════════════════════════════════════════
 *  Actions
 * ══════════════════════════════════════════════════════════════════ */
void TitleDock::on_add()
{
    QDialog dialog(this);
    dialog.setWindowTitle(fxm_tr("OBSTitles.NewTitle"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name_edit = new QLineEdit(fxm_tr("OBSTitles.NewTitle"), &dialog);
    form->addRow(fxm_tr("OBSTitles.TitleNamePrompt"), name_edit);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    name_edit->selectAll();
    name_edit->setFocus();

    if (dialog.exec() != QDialog::Accepted || name_edit->text().trimmed().isEmpty())
        return;

    auto title = TitleDataStore::instance().create_title(name_edit->text().trimmed().toStdString());
    /* Blank titles are element-defined. With no exposed text or scene-mask
     * elements yet, the neutral starting type is Graphic; Stinger-specific
     * setup remains available from Graphic Properties after creation. */
    title->graphic_type = TitleGraphicType::Graphic;
    title->preview_screenshot_png_base64 = title_screenshot_png_base64(title_screenshot_image(*title)).toStdString();
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    select_title(title->id);
    on_edit();
}


void TitleDock::on_add_from_templates_library()
{
    const QString root_path = template_library_root_path();
    remove_legacy_canned_templates(root_path);

    const QString canned_version_path = QDir(root_path).filePath(QStringLiteral(".fxm-canned-template-version"));
    QString installed_canned_version;
    {
        QFile version_file(canned_version_path);
        if (version_file.open(QIODevice::ReadOnly | QIODevice::Text))
            installed_canned_version = QString::fromUtf8(version_file.readAll()).trimmed();
    }
    const QString current_canned_version = QStringLiteral("038");
    const bool refresh_builtins = installed_canned_version != current_canned_version;

    for (const auto &fallback_entry : template_library_entries) {
        const CannedTemplateDefinition entry = canned_template_definition(fallback_entry);
        const QString collection_path = QDir(root_path).filePath(entry.collection);
        QDir().mkpath(collection_path);
        const QString template_path = QDir(collection_path).filePath(entry.file_name);
        if (QFileInfo::exists(template_path) && !refresh_builtins)
            continue;
        if (refresh_builtins)
            QFile::remove(template_path);

        auto canned = create_template_title(entry.default_name.toStdString(), entry.id);
        TitleTemplateExportMetadata metadata;
        metadata.title = entry.title.toStdString();
        metadata.description = entry.description.toStdString();
        metadata.creator = "Flux Motion";
        metadata.creation_date = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
        metadata.category = entry.category.toStdString();
        metadata.subcategory = entry.subcategory.toStdString();
        metadata.collection = entry.collection.toStdString();
        const QImage canned_screenshot = title_screenshot_image(*canned);
        metadata.screenshot_png_base64 = title_screenshot_png_base64(canned_screenshot).toStdString();
        canned->preview_screenshot_png_base64 = metadata.screenshot_png_base64;

        std::string error;
        TitleDataStore::instance().export_title(canned->id, template_path.toStdString(), metadata, &error);
        TitleDataStore::instance().delete_title(canned->id);
        TitleDataStore::instance().save();
    }
    if (refresh_builtins) {
        QFile version_file(canned_version_path);
        if (version_file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            version_file.write(current_canned_version.toUtf8());
    }
    refresh();

    auto *window = new QDialog(this);
    fxm_apply_brand_icon(window);
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(fxm_tr("OBSTitles.TemplatesLibrary"));
    window->setModal(false);
    window->resize(900, 520);

    QSettings library_settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    library_settings.beginGroup(QString::fromUtf8(kTemplateLibrarySettingsGroup));
    const bool saved_by_collection = library_settings.value(QString::fromUtf8(kTemplateLibraryByCollectionKey), false).toBool();
    const bool saved_icon_view = library_settings.value(QString::fromUtf8(kTemplateLibraryIconViewKey), true).toBool();
    const QByteArray saved_geometry = library_settings.value(QString::fromUtf8(kTemplateLibraryGeometryKey)).toByteArray();
    const QByteArray saved_splitter = library_settings.value(QString::fromUtf8(kTemplateLibrarySplitterKey)).toByteArray();
    library_settings.endGroup();
    if (!saved_geometry.isEmpty())
        window->restoreGeometry(saved_geometry);

    auto *layout = new QVBoxLayout(window);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(obs_layout_spacing(window));

    auto *intro = new QLabel(fxm_tr("OBSTitles.TemplatesLibraryPrompt"), window);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *view_mode_row = new QHBoxLayout();
    view_mode_row->setContentsMargins(0, 0, 0, 0);
    auto *view_mode_label = new QLabel(fxm_tr("OBSTitles.TemplateViewModeLabel"), window);
    auto *view_mode_toggle = new QToolButton(window);
    view_mode_toggle->setCheckable(true);
    view_mode_toggle->setChecked(saved_by_collection);
    view_mode_toggle->setText(saved_by_collection
        ? fxm_tr("OBSTitles.TemplateViewByCollection")
        : fxm_tr("OBSTitles.TemplateViewByCategory"));
    view_mode_toggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    view_mode_row->addWidget(view_mode_label);
    view_mode_row->addWidget(view_mode_toggle);
    view_mode_row->addStretch(1);
    layout->addLayout(view_mode_row);

    auto *splitter = new QSplitter(Qt::Horizontal, window);
    auto *categories = new TemplateCategoryTree(splitter);
    categories->setAcceptDrops(false);
    categories->viewport()->setAcceptDrops(false);
    categories->setDragDropMode(QAbstractItemView::NoDragDrop);
    categories->setMinimumWidth(180);
    categories->setAlternatingRowColors(true);
    auto records = std::make_shared<QList<TemplateLibraryRecord>>(scan_template_library(root_path));
    populate_dynamic_template_tree(categories, *records, saved_by_collection);

    auto *templates_section = new QWidget(splitter);
    auto *templates_layout = new QVBoxLayout(templates_section);
    templates_layout->setContentsMargins(0, 0, 0, 0);
    templates_layout->setSpacing(obs_layout_spacing(templates_section));

    auto *templates_header = new QHBoxLayout();
    templates_header->setContentsMargins(0, 0, 0, 0);
    auto *templates_label = new QLabel(fxm_tr("OBSTitles.TitlesAndGraphics"), templates_section);
    set_bold_label(templates_label);
    templates_header->addWidget(templates_label);
    templates_header->addStretch(1);
    templates_layout->addLayout(templates_header);

    auto *templates_toolbar = make_obs_dock_toolbar(templates_section);
    auto *templates_view_toggle = make_obs_dock_tool_button(
        templates_toolbar, fxm_tr("OBSTitles.ListView"), "list-view.svg",
        fxm_tr("OBSTitles.ListViewTooltip"));
    templates_toolbar->addWidget(toolbar_spacer(templates_toolbar));
    templates_toolbar->addWidget(templates_view_toggle);
    templates_layout->addWidget(templates_toolbar);

    auto *templates = new TemplateListWidget(templates_section);
    // Use the exact same delegate as the Dock so list/icon cards, thumbnails,
    // selection painting and dimensions remain visually identical.
    templates->setItemDelegate(new TitleIconViewDelegate(templates));
    templates->setViewMode(saved_icon_view ? QListView::IconMode : QListView::ListMode);
    templates->setIconSize(saved_icon_view
        ? QSize(kTitleIconViewThumbWidth, kTitleIconViewThumbHeight)
        : QSize(96, 54));
    templates->setGridSize(saved_icon_view
        ? QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight)
        : QSize());
    templates->setResizeMode(saved_icon_view ? QListView::Adjust : QListView::Fixed);
    templates->setMovement(QListView::Static);
    templates->setSelectionMode(QAbstractItemView::SingleSelection);
    templates->setSpacing(saved_icon_view ? 6 : 0);
    templates->setUniformItemSizes(true);
    templates->setWordWrap(saved_icon_view);
    templates->setTextElideMode(Qt::ElideRight);
    templates->setAlternatingRowColors(true);
    if (!saved_icon_view) {
        templates_view_toggle->setText(fxm_tr("OBSTitles.IconView"));
        templates_view_toggle->setAccessibleName(fxm_tr("OBSTitles.IconView"));
        templates_view_toggle->setToolTip(fxm_tr("OBSTitles.IconViewTooltip"));
        templates_view_toggle->setIcon(obs_icon("icon-view.svg"));
    }
    templates_layout->addWidget(templates, 1);

    auto *metadata_panel = new QWidget(splitter);
    auto *metadata_layout = new QVBoxLayout(metadata_panel);
    metadata_layout->setContentsMargins(0, 0, 0, 0);
    auto *metadata_title = new QLabel(fxm_tr("OBSTitles.TemplateMetadata"), metadata_panel);
    set_bold_label(metadata_title);
    metadata_layout->addWidget(metadata_title);
    auto *preview = new QLabel(metadata_panel);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    preview->setMinimumHeight(140);
    metadata_layout->addWidget(preview);
    auto *details = new QLabel(metadata_panel);
    details->setWordWrap(true);
    details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    metadata_layout->addWidget(details, 1);

    splitter->addWidget(categories);
    splitter->addWidget(templates_section);
    splitter->addWidget(metadata_panel);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    if (!saved_splitter.isEmpty())
        splitter->restoreState(saved_splitter);
    layout->addWidget(splitter, 1);

    auto load_templates_for_group = [templates, categories, records, view_mode_toggle]() {
        templates->clear();
        auto *selected_group = categories->currentItem();
        const bool by_collection = view_mode_toggle->isChecked();
        for (const auto &record : *records) {
            if (!template_matches_tree_item(record, selected_group, by_collection)) continue;
            auto *item = new QListWidgetItem(record.metadata.screenshot_icon, record.metadata.title);
            item->setFlags(item->flags() | Qt::ItemIsDragEnabled);
            item->setData(Qt::UserRole, record.path);
            item->setData(kTitleListThumbnailRole, record.metadata.screenshot_pixmap);
            item->setData(kTitlePlaylistActiveRole, false);
            item->setData(kTitleCueVisualStateRole,
                          static_cast<int>(TitleCueVisualState::Inactive));
            if (templates->viewMode() == QListView::IconMode) {
                item->setSizeHint(QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight));
                item->setTextAlignment(Qt::AlignHCenter);
            } else {
                item->setSizeHint(QSize());
                item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            }
            item->setToolTip(record.metadata.description);
            templates->addItem(item);
        }
    };
    auto reload_library = [categories, records, root_path, view_mode_toggle, load_templates_for_group, templates]() {
        *records = scan_template_library(root_path);
        populate_dynamic_template_tree(categories, *records, view_mode_toggle->isChecked());
        load_templates_for_group();
        if (templates->count() > 0) templates->setCurrentRow(0);
    };
    categories->templates_moved = reload_library;

    auto update_metadata = [templates, preview, details]() {
        auto *item = templates->currentItem();
        if (!item) {
            preview->clear();
            details->setText(fxm_tr("OBSTitles.TemplateNoSelection"));
            return;
        }

        TemplateFileMetadata metadata = read_template_file_metadata(item->data(Qt::UserRole).toString());
        if (!metadata.screenshot_pixmap.isNull()) {
            preview->setPixmap(metadata.screenshot_pixmap.scaled(
                QSize(220, 140), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            preview->setText(fxm_tr("OBSTitles.TemplateScreenshotFailed"));
        }
        details->setText(
            QStringLiteral("<b>%1</b><br><br>%2<br><br><b>%3</b> %4<br><b>%5</b> %6")
                .arg(metadata.title.toHtmlEscaped(),
                     metadata.description.toHtmlEscaped().replace('\n', QStringLiteral("<br>")),
                     fxm_tr("OBSTitles.TemplateCreatorLabel"), metadata.creator.toHtmlEscaped(),
                     fxm_tr("OBSTitles.TemplateCreationDateLabel"), metadata.creation_date.toHtmlEscaped())
                + QStringLiteral("<br><b>%1</b> %2<br><b>%3</b> %4<br><b>%5</b> %6")
                    .arg(fxm_tr("OBSTitles.TemplateCategoryLabel"), template_group_name(metadata.category).toHtmlEscaped(),
                         fxm_tr("OBSTitles.TemplateSubcategoryLabel"), template_group_name(metadata.subcategory).toHtmlEscaped(),
                         fxm_tr("OBSTitles.TemplateCollectionLabel"), template_group_name(metadata.collection).toHtmlEscaped()));
    };

    QObject::connect(categories, &QTreeWidget::currentItemChanged, window,
                     [load_templates_for_group, update_metadata](QTreeWidgetItem *current, QTreeWidgetItem *) {
        if (!current) return;
        load_templates_for_group();
        update_metadata();
    });
    categories->category_delete_requested = nullptr;
    QObject::connect(view_mode_toggle, &QToolButton::toggled, window,
                     [view_mode_toggle, categories, records, load_templates_for_group, templates](bool by_collection) {
        view_mode_toggle->setText(by_collection
            ? fxm_tr("OBSTitles.TemplateViewByCollection")
            : fxm_tr("OBSTitles.TemplateViewByCategory"));
        populate_dynamic_template_tree(categories, *records, by_collection);
        QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
        settings.beginGroup(QString::fromUtf8(kTemplateLibrarySettingsGroup));
        settings.setValue(QString::fromUtf8(kTemplateLibraryByCollectionKey), by_collection);
        settings.endGroup();
        load_templates_for_group();
        if (templates->count() > 0) templates->setCurrentRow(0);
    });
    QObject::connect(templates_view_toggle, &QToolButton::clicked, window,
                     [templates, templates_view_toggle]() {
        const bool use_list = templates->viewMode() == QListView::IconMode;
        if (use_list) {
            templates->setViewMode(QListView::ListMode);
            templates->setIconSize(QSize(kTitleListIconExtent * 3 + 6, kTitleListIconExtent));
            templates->setGridSize(QSize());
            templates->setResizeMode(QListView::Fixed);
            templates->setSpacing(0);
            templates->setWordWrap(false);
            templates_view_toggle->setText(fxm_tr("OBSTitles.IconView"));
            templates_view_toggle->setAccessibleName(fxm_tr("OBSTitles.IconView"));
            templates_view_toggle->setToolTip(fxm_tr("OBSTitles.IconViewTooltip"));
            templates_view_toggle->setIcon(obs_icon("icon-view.svg"));
        } else {
            templates->setViewMode(QListView::IconMode);
            templates->setIconSize(QSize(kTitleIconViewThumbWidth, kTitleIconViewThumbHeight));
            templates->setGridSize(QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight));
            templates->setResizeMode(QListView::Adjust);
            templates->setSpacing(6);
            templates->setWordWrap(true);
            templates_view_toggle->setText(fxm_tr("OBSTitles.ListView"));
            templates_view_toggle->setAccessibleName(fxm_tr("OBSTitles.ListView"));
            templates_view_toggle->setToolTip(fxm_tr("OBSTitles.ListViewTooltip"));
            templates_view_toggle->setIcon(obs_icon("list-view.svg"));
        }
        for (int i = 0; i < templates->count(); ++i) {
            auto *item = templates->item(i);
            if (!item) continue;
            if (templates->viewMode() == QListView::IconMode) {
                item->setSizeHint(QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight));
                item->setTextAlignment(Qt::AlignHCenter);
            } else {
                item->setSizeHint(QSize());
                item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
            }
        }
        templates->viewport()->update();
        QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
        settings.beginGroup(QString::fromUtf8(kTemplateLibrarySettingsGroup));
        settings.setValue(QString::fromUtf8(kTemplateLibraryIconViewKey), templates->viewMode() == QListView::IconMode);
        settings.endGroup();
    });

    QObject::connect(templates, &QListWidget::currentItemChanged, window,
                     [update_metadata](QListWidgetItem *, QListWidgetItem *) { update_metadata(); });
    QObject::connect(window, &QDialog::finished, window, [window, splitter](int) {
        QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
        settings.beginGroup(QString::fromUtf8(kTemplateLibrarySettingsGroup));
        settings.setValue(QString::fromUtf8(kTemplateLibraryGeometryKey), window->saveGeometry());
        settings.setValue(QString::fromUtf8(kTemplateLibrarySplitterKey), splitter->saveState());
        settings.endGroup();
    });

    auto import_selected_template = [this, window, templates]() {
        auto *selected = templates->currentItem();
        if (!selected)
            return;

        std::string error;
        TitleImportDiagnostics diagnostics;
        auto imported = TitleDataStore::instance().import_title(
            selected->data(Qt::UserRole).toString().toStdString(), &error, &diagnostics);
        if (!imported) {
            QMessageBox::warning(window, fxm_tr("OBSTitles.ImportTitleTemplate"), QString::fromStdString(error));
            return;
        }
        complete_import_diagnostics(*imported, diagnostics);
        select_title(imported->id);
        status_lbl_->setText(fxm_tr("OBSTitles.ImportedStatusFormat").arg(QString::fromStdString(imported->name)));
        window->close();
        if (!diagnostics.empty()) {
            show_import_diagnostics(
                this,
                QStringList{import_diagnostics_text(
                    QString::fromStdString(imported->name), diagnostics)});
        }
    };
    QObject::connect(templates, &QListWidget::itemDoubleClicked, window,
                     [import_selected_template](QListWidgetItem *) { import_selected_template(); });

    auto *template_actions = new QHBoxLayout();
    template_actions->setContentsMargins(0, 0, 0, 0);
    auto *edit_template = new QPushButton(fxm_tr("OBSTitles.Edit"), window);
    auto *delete_template = new QPushButton(fxm_tr("OBSTitles.Delete"), window);
    template_actions->addWidget(edit_template);
    template_actions->addWidget(delete_template);
    template_actions->addStretch();
    layout->addLayout(template_actions);

    QObject::connect(edit_template, &QPushButton::clicked, window, [window, templates, reload_library, update_metadata]() {
        auto *selected = templates->currentItem();
        if (!selected) return;
        if (prompt_edit_template_file_metadata(window, selected->data(Qt::UserRole).toString())) {
            reload_library();
            update_metadata();
        }
    });
    QObject::connect(delete_template, &QPushButton::clicked, window, [window, templates, reload_library, update_metadata]() {
        auto *selected = templates->currentItem();
        if (!selected) return;
        const QString path = selected->data(Qt::UserRole).toString();
        const auto reply = QMessageBox::question(
            window, fxm_tr("OBSTitles.DeleteTemplate"),
            fxm_tr("OBSTitles.DeleteTemplateQuestionFormat").arg(selected->text()),
            QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
        if (!QFile::remove(path)) {
            QMessageBox::warning(window, fxm_tr("OBSTitles.DeleteTemplate"),
                                 fxm_tr("OBSTitles.DeleteTemplateFailed"));
            return;
        }
        reload_library();
        update_metadata();
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, window);
    QObject::connect(buttons, &QDialogButtonBox::accepted, window, import_selected_template);
    QObject::connect(buttons, &QDialogButtonBox::rejected, window, &QDialog::close);
    layout->addWidget(buttons);

    load_templates_for_group();
    if (templates->count() > 0) templates->setCurrentRow(0);
    update_metadata();

    window->show();
    window->raise();
    window->activateWindow();
}

void TitleDock::on_duplicate()
{
    auto src = TitleDataStore::instance().get_title(selected_id());
    if (!src) return;

    /* Deep copy by round-tripping through data store */
    auto dup = TitleDataStore::instance().create_title(src->name + obs_text_std("OBSTitles.CopySuffix"));
    dup->description = src->description;
    dup->creator = src->creator;
    dup->creation_date = src->creation_date;
    dup->duration  = src->duration;
    dup->bg_color  = src->bg_color;
    dup->width     = src->width;
    dup->height    = src->height;

    dup->layers.clear();
    for (auto &l : src->layers) {
        auto nl = std::make_shared<Layer>(*l);
        nl->id = TitleDataStore::make_uuid();
        dup->layers.push_back(nl);
    }
    TitleDataStore::instance().notify_change();
    TitleDataStore::instance().save();
    select_title(dup->id);
}

void TitleDock::on_rename()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;

    QImage screenshot = title_screenshot_image(*title);
    const QString screenshot_base64 = title_screenshot_png_base64(screenshot);
    if (!screenshot_base64.isEmpty())
        title->preview_screenshot_png_base64 = screenshot_base64.toStdString();

    TitleTemplateExportMetadata metadata;
    if (!prompt_template_metadata(this, *title, screenshot, metadata, nullptr,
                                  fxm_tr("OBSTitles.EditTemplateMetadata")))
        return;

    TitleDataStore::instance().rename_title(title->id, metadata.title);
    title->description = metadata.description;
    title->creator = metadata.creator;
    title->creation_date = metadata.creation_date;
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    select_title(title->id);
}

void TitleDock::on_export()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;

    QImage screenshot = title_screenshot_image(*title);
    QString screenshot_base64 = title_screenshot_png_base64(screenshot);
    if (screenshot_base64.isEmpty()) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ExportTitleTemplate"),
                             fxm_tr("OBSTitles.TemplateScreenshotFailed"));
        return;
    }

    title->preview_screenshot_png_base64 = screenshot_base64.toStdString();
    TitleDataStore::instance().save();

    TitleTemplateExportMetadata metadata;
    metadata.screenshot_png_base64 = title->preview_screenshot_png_base64;
    bool save_in_template_library = false;
    if (!prompt_template_metadata(this, *title, screenshot, metadata, &save_in_template_library))
        return;

    QString safe_name = QString::fromStdString(metadata.title).trimmed();
    if (safe_name.isEmpty()) safe_name = fxm_tr("OBSTitles.TemplateFileDialogTitle");
    safe_name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));

    QString path;
    QString selected_filter;
    if (save_in_template_library) {
        QStringList hierarchy;
        const QString category = sanitized_template_category_path(QString::fromStdString(metadata.category));
        const QString subcategory = sanitized_template_category_path(QString::fromStdString(metadata.subcategory));
        const QString collection = sanitized_template_category_path(QString::fromStdString(metadata.collection));
        if (!category.isEmpty()) hierarchy << category;
        if (!subcategory.isEmpty()) hierarchy << subcategory;
        if (!collection.isEmpty()) hierarchy << collection;
        QString library_path = template_library_root_path();
        if (!hierarchy.isEmpty()) library_path = QDir(library_path).filePath(hierarchy.join('/'));
        QDir().mkpath(library_path);
        path = QDir(library_path).filePath(safe_name + QStringLiteral(".fxmt"));
    } else {
        path = QFileDialog::getSaveFileName(
            this, fxm_tr("OBSTitles.ExportTitleTemplate"),
            QDir(template_library_root_path()).filePath(safe_name + QStringLiteral(".fxmt")),
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
    if (packed && !prompt_packed_title_options(this, packed_options))
        return;
    std::string error;
    const bool exported = packed
        ? TitleDataStore::instance().export_packed_title(
              title->id, path.toStdString(), metadata, packed_options, &error)
        : TitleDataStore::instance().export_title(
              title->id, path.toStdString(), metadata, &error);
    if (!exported) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ExportTitleTemplate"),
                             QString::fromStdString(error));
        return;
    }

    status_lbl_->setText(fxm_tr("OBSTitles.ExportedStatusFormat").arg(QFileInfo(path).fileName()));
}

static QString project_import_graphic_type_name(TitleGraphicType type)
{
    switch (type) {
    case TitleGraphicType::Graphic:
        return fxm_tr("OBSTitles.GraphicTypeGraphic");
    case TitleGraphicType::Mask:
        return fxm_tr("OBSTitles.GraphicTypeMask");
    case TitleGraphicType::Stinger:
        return fxm_tr("OBSTitles.GraphicTypeStinger");
    case TitleGraphicType::Title:
    default:
        return fxm_tr("OBSTitles.GraphicTypeTitle");
    }
}

static bool select_project_titles_for_import(
    QWidget *parent, const TitleProject &project,
    std::vector<std::shared_ptr<Title>> *selected_titles)
{
    if (!selected_titles)
        return false;
    selected_titles->clear();

    if (project.titles.size() <= 1) {
        for (const auto &title : project.titles)
            if (title) selected_titles->push_back(title);
        return !selected_titles->empty();
    }

    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("fxmProjectImportSelectionDialog"));
    dialog.setWindowTitle(fxm_tr("OBSTitles.ImportProjectSelectionTitle"));
    dialog.setMinimumSize(440, 320);

    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        fxm_tr("OBSTitles.ImportProjectSelectionDescription")
            .arg(QString::fromStdString(project.name))
            .arg(static_cast<int>(project.titles.size())),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *items = new QListWidget(&dialog);
    items->setObjectName(QStringLiteral("fxmProjectImportItems"));
    items->setAlternatingRowColors(true);
    items->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(items, 1);

    int project_index = 0;
    for (const auto &title : project.titles) {
        if (!title) {
            ++project_index;
            continue;
        }
        const QString title_name = QString::fromStdString(title->name).trimmed();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  —  %2")
                .arg(title_name.isEmpty() ? fxm_tr("OBSTitles.Untitled") : title_name,
                     project_import_graphic_type_name(title->graphic_type)),
            items);
        item->setData(Qt::UserRole, project_index);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable) &
                       ~Qt::ItemIsSelectable);
        item->setCheckState(Qt::Checked);
        ++project_index;
    }

    auto *selection_actions = new QHBoxLayout();
    auto *select_all = new QPushButton(
        fxm_tr("OBSTitles.ImportProjectSelectAll"), &dialog);
    auto *select_none = new QPushButton(
        fxm_tr("OBSTitles.ImportProjectSelectNone"), &dialog);
    selection_actions->addWidget(select_all);
    selection_actions->addWidget(select_none);
    selection_actions->addStretch();
    layout->addLayout(selection_actions);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *import_button = buttons->button(QDialogButtonBox::Ok);
    import_button->setText(fxm_tr("OBSTitles.ImportSelectedProjectItems"));
    layout->addWidget(buttons);

    const auto update_import_enabled = [items, import_button]() {
        bool any_checked = false;
        for (int row = 0; row < items->count(); ++row) {
            if (items->item(row)->checkState() == Qt::Checked) {
                any_checked = true;
                break;
            }
        }
        import_button->setEnabled(any_checked);
    };
    QObject::connect(items, &QListWidget::itemChanged, &dialog,
                     [update_import_enabled](QListWidgetItem *) {
                         update_import_enabled();
                     });
    QObject::connect(select_all, &QPushButton::clicked, &dialog, [items]() {
        for (int row = 0; row < items->count(); ++row)
            items->item(row)->setCheckState(Qt::Checked);
    });
    QObject::connect(select_none, &QPushButton::clicked, &dialog, [items]() {
        for (int row = 0; row < items->count(); ++row)
            items->item(row)->setCheckState(Qt::Unchecked);
    });
    QObject::connect(buttons, &QDialogButtonBox::accepted,
                     &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected,
                     &dialog, &QDialog::reject);
    update_import_enabled();

    if (dialog.exec() != QDialog::Accepted)
        return false;

    for (int row = 0; row < items->count(); ++row) {
        const auto *item = items->item(row);
        if (item->checkState() != Qt::Checked)
            continue;
        const int index = item->data(Qt::UserRole).toInt();
        if (index >= 0 && index < static_cast<int>(project.titles.size()) &&
            project.titles[static_cast<std::size_t>(index)]) {
            selected_titles->push_back(
                project.titles[static_cast<std::size_t>(index)]);
        }
    }
    return !selected_titles->empty();
}

void TitleDock::import_title_paths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    int imported_count = 0;
    QStringList failures;
    QStringList diagnostic_blocks;
    std::shared_ptr<Title> last_imported;
    bool selection_cancelled = false;
    bool project_titles_appended = false;

    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile() || !info.isReadable())
            continue;
        const QString suffix = info.suffix().toLower();
        if (suffix != QStringLiteral("fxmp") && suffix != QStringLiteral("fxmt") &&
            suffix != QStringLiteral("fxmproj") && suffix != QStringLiteral("json"))
            continue;

        std::string error;
        if (suffix == QStringLiteral("fxmproj")) {
            TitleProject project;
            if (!TitleDataStore::instance().read_project(
                    info.absoluteFilePath().toStdString(), &project, &error)) {
                const QString message = error.empty()
                    ? fxm_tr("OBSTitles.ImportFailed")
                    : QString::fromStdString(error);
                failures.push_back(
                    QStringLiteral("%1 — %2").arg(info.fileName(), message));
                continue;
            }

            std::vector<std::shared_ptr<Title>> selected_titles;
            if (!select_project_titles_for_import(
                    this, project, &selected_titles)) {
                selection_cancelled = true;
                continue;
            }

            TitleProject selection = project;
            selection.titles = selected_titles;
            selection.active_title_id = selected_titles.front()->id;
            const auto added_ids =
                TitleDataStore::instance().append_project(selection);
            if (added_ids.empty()) {
                failures.push_back(
                    QStringLiteral("%1 — %2")
                        .arg(info.fileName(), fxm_tr("OBSTitles.ImportFailed")));
                continue;
            }

            project_titles_appended = true;
            imported_count += static_cast<int>(added_ids.size());
            for (const std::string &added_id : added_ids) {
                auto imported = TitleDataStore::instance().get_title(added_id);
                if (!imported)
                    continue;
                TitleImportDiagnostics diagnostics;
                complete_import_diagnostics(*imported, diagnostics);
                if (!diagnostics.empty()) {
                    diagnostic_blocks.push_back(import_diagnostics_text(
                        QString::fromStdString(imported->name), diagnostics));
                }
                last_imported = imported;
            }
            continue;
        }

        TitleImportDiagnostics diagnostics;
        auto imported = TitleDataStore::instance().import_title(
            info.absoluteFilePath().toStdString(), &error, &diagnostics);
        if (!imported) {
            const QString message = error.empty() ? fxm_tr("OBSTitles.ImportFailed") : QString::fromStdString(error);
            failures.push_back(QStringLiteral("%1 — %2").arg(info.fileName(), message));
            continue;
        }

        complete_import_diagnostics(*imported, diagnostics);
        if (!diagnostics.empty()) {
            diagnostic_blocks.push_back(import_diagnostics_text(
                QString::fromStdString(imported->name), diagnostics));
        }
        last_imported = imported;
        ++imported_count;
    }

    if (project_titles_appended)
        TitleDataStore::instance().save();

    if (imported_count > 0)
        refresh();

    if (last_imported)
        select_title(last_imported->id);

    if (imported_count == 1 && last_imported) {
        status_lbl_->setText(fxm_tr("OBSTitles.ImportedStatusFormat").arg(QString::fromStdString(last_imported->name)));
    } else if (imported_count > 1) {
        status_lbl_->setText(fxm_tr("OBSTitles.ImportedTitleTemplates").arg(imported_count));
    }

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportTitleTemplate"),
                             fxm_tr("OBSTitles.SomeTemplatesImportFailed")
                                 .arg(failures.join(QStringLiteral("\n"))));
    } else if (imported_count == 0 && !selection_cancelled) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportTitleTemplate"),
                             fxm_tr("OBSTitles.NoSupportedTemplatesImported"));
    }

    show_import_diagnostics(this, diagnostic_blocks);
}

void TitleDock::on_import()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, fxm_tr("OBSTitles.ImportTitleTemplate"), QString(),
        fxm_tr("OBSTitles.ImportFileFilter"));
    if (paths.isEmpty()) return;

    import_title_paths(paths);
}

void TitleDock::on_delete()
{
    const auto ids = selected_title_ids();
    if (ids.empty()) return;

    int target_row = -1;
    if (list_) {
        int first_selected_row = list_->count();
        for (int i = 0; i < list_->count(); ++i) {
            auto *item = list_->item(i);
            if (item && item->isSelected())
                first_selected_row = std::min(first_selected_row, i);
        }
        if (first_selected_row < list_->count())
            target_row = first_selected_row;
    }

    DeleteTitleConfirmationResult confirmation;
    if (ids.size() == 1) {
        auto title = TitleDataStore::instance().get_title(ids.front());
        if (!title) return;
        confirmation = confirm_delete_single_title(this, *title);
    } else {
        confirmation = confirm_delete_multiple_titles(this, (int)ids.size());
    }

    if (confirmation.button == QMessageBox::Yes) {
        if (confirmation.delete_sources)
            delete_title_sources_for_ids(ids);

        for (const auto &id : ids) {
            CacheManager::instance().removeTitleCache(QString::fromStdString(id), false);
            TitleDataStore::instance().delete_title(id);
        }
        TitleDataStore::instance().save();
        populate_list();
        if (list_ && list_->count() > 0)
            list_->setCurrentRow(std::clamp(target_row, 0, list_->count() - 1));
        else if (list_)
            list_->clearSelection();
    }
}

void TitleDock::on_edit()
{
    std::string id = selected_id();
    if (id.empty()) return;

    auto *host = fxm::editor_host::editor_host();
    std::string error;
    if (!host || !host->open_title_in_editor(id, &error)) {
        QMessageBox::warning(
            this, fxm_tr("OBSTitles.EditorWindowTitle"),
            error.empty()
                ? QStringLiteral("Flux Motion is unavailable.")
                : QString::fromStdString(error));
        return;
    }

    const QFileInfo title_store_file(
        QString::fromStdString(TitleDataStore::data_path()));
    title_store_last_modified_ = title_store_file.exists()
        ? title_store_file.lastModified() : QDateTime();
    title_store_last_size_ = title_store_file.exists()
        ? title_store_file.size() : -1;
    title_store_poll_ticks_ = 0;
    external_editor_launched_ = true;
}

void TitleDock::on_add_to_scene()
{
    std::string id = selected_id();
    if (id.empty()) return;

    auto t = TitleDataStore::instance().get_title(id);
    if (!t) return;

    auto *host = fxm::editor_host::editor_host();
    if (!host) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.NoScene"),
                             fxm_tr("OBSTitles.NoActiveScene"));
        return;
    }
    std::string error;
    if (host->add_title_to_current_scene(id, t->name, &error)) {
        status_lbl_->setText(fxm_tr("OBSTitles.AddedToScene"));
    } else {
        QMessageBox::warning(this, fxm_tr("OBSTitles.AddTitleSource"),
                             error.empty()
                                 ? fxm_tr("OBSTitles.CreateSourceFailed")
                                 : QString::fromStdString(error));
    }
}
