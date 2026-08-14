namespace {

using fxm::live_text::exposed_text_layers;
using fxm::live_text::live_cue_layer_value;
using fxm::live_text::normalize_live_text_rows;

constexpr int TemplateCategoryNameRole = Qt::UserRole + 1;
constexpr int TemplateFilterCategoryRole = Qt::UserRole + 20;
constexpr int TemplateFilterSubcategoryRole = Qt::UserRole + 21;
constexpr int TemplateFilterCollectionRole = Qt::UserRole + 22;
constexpr int TemplateFilterDepthRole = Qt::UserRole + 23;
constexpr const char *kDockSettingsGroup = "TitleDock";
constexpr const char *kDockSplitterStateKey = "sectionSplitterState";
constexpr const char *kTemplateIconViewKey = "templateIconView";
constexpr const char *kVisibilityFilterKey = "visibilityFilterActive";
constexpr const char *kLastSelectedTitleIdKey = "lastSelectedTitleId";
constexpr const char *kPlaylistLoopKey = "playlistLoop";
constexpr const char *kPlaylistReverseKey = "playlistReverse";
constexpr const char *kPlaylistHoldSecondsKey = "playlistHoldSeconds";
constexpr const char *kBackgroundPersistenceKey = "backgroundPersistence";
constexpr const char *kTextPersistenceKey = "textPersistence";
constexpr const char *kLiveTextLinesPerRowKey = "liveTextLinesPerRow";
constexpr const char *kSelectRowBeforeCueKey = "selectRowBeforeCue";
constexpr const char *kShowCuePreviewKey = "showCuePreview";
constexpr const char *kLegacyShowPreviewBeforeCueKey = "showPreviewBeforeCue";
constexpr const char *kTemplateLibrarySettingsGroup = "TemplateLibrary";
constexpr const char *kTemplateLibraryByCollectionKey = "viewByCollection";
constexpr const char *kTemplateLibraryIconViewKey = "iconView";
constexpr const char *kTemplateLibraryGeometryKey = "geometry";
constexpr const char *kTemplateLibrarySplitterKey = "splitterState";
constexpr int kMinLiveTextLinesPerRow = 1;
constexpr int kMaxLiveTextLinesPerRow = 8;
constexpr int kTitleListIconExtent = 16;
constexpr int kTitleIconViewThumbWidth = 144;
constexpr int kTitleIconViewThumbHeight = 82;
constexpr int kTitleIconViewItemWidth = 172;
constexpr int kTitleIconViewItemHeight = 126;
constexpr int kTitleIconViewTextLines = 2;
constexpr int kTitlePlaylistActiveRole = Qt::UserRole + 20;
constexpr int kTitleCueVisualStateRole = Qt::UserRole + 21;
constexpr int kTitleListThumbnailRole = Qt::UserRole + 22;
constexpr int kTitleShowStatusIconRole = Qt::UserRole + 23;
constexpr int kLiveCueColumn = 0;
constexpr int kLiveCacheColumn = 1;
constexpr int kLiveSelectColumn = 2;
constexpr int kLiveFirstValueColumn = 3;
constexpr int kStaticCueColumn = 0;
constexpr int kStaticCacheColumn = 1;
constexpr int kStaticTitleColumn = 2;

static QColor live_cue_state_color(bool current, bool queued, bool ending);

static bool live_cue_row_is_ending(const Title &title, int row)
{
    /* A row is outgoing both for an explicit uncue and for the outro half of
     * a row-to-row hand-off.  pending_cue_row identifies the incoming row,
     * while current_cue_row must stay active until its authored outro has
     * completed.  Keep the outgoing row yellow for that complete interval. */
    return row >= 0 && row == title.current_cue_row &&
        (title.cue_uncue_requested || title.pending_cue_row >= 0);
}

enum class TitleCueVisualState : int {
    Inactive = 0,
    Queued,
    Cued,
    Ending,
};

static TitleCueVisualState title_cue_visual_state(const Title &title,
                                                  bool waiting_for_prerender = false)
{
    /* Outro/uncue has visual priority over every other cue condition. During
     * a cue-to-cue hand-off both current and pending rows can exist, but the
     * thumbnail must remain yellow until the outgoing row actually finishes. */
    if (title.current_cue_row >= 0 &&
        (title.cue_uncue_requested || title.pending_cue_row >= 0))
        return TitleCueVisualState::Ending;
    if (title.current_cue_row >= 0)
        return TitleCueVisualState::Cued;
    if (title.pending_cue_row >= 0 || waiting_for_prerender)
        return TitleCueVisualState::Queued;
    return TitleCueVisualState::Inactive;
}

static QColor title_cue_visual_color(TitleCueVisualState state)
{
    switch (state) {
    case TitleCueVisualState::Ending:
        return live_cue_state_color(false, false, true);
    case TitleCueVisualState::Cued:
        return live_cue_state_color(true, false, false);
    case TitleCueVisualState::Queued:
        return live_cue_state_color(false, true, false);
    case TitleCueVisualState::Inactive:
    default:
        return QColor();
    }
}

static void append_unique_diagnostic(std::vector<std::string> &items, const QString &value)
{
    const std::string utf8 = value.toStdString();
    if (utf8.empty())
        return;
    if (std::find(items.begin(), items.end(), utf8) == items.end())
        items.push_back(utf8);
}

static bool imported_font_family_available(const QString &family, const QStringList &available_families)
{
    const QString candidate = family.trimmed();
    if (candidate.isEmpty())
        return true;
    return std::any_of(available_families.begin(), available_families.end(),
                       [&candidate](const QString &installed) {
                           return installed.compare(candidate, Qt::CaseInsensitive) == 0;
                       });
}

static void complete_import_diagnostics(const Title &title, TitleImportDiagnostics &diagnostics)
{
    const QStringList available_families = QFontDatabase().families();
    QSet<QString> checked_fonts;
    QSet<QString> checked_effects;
    auto &extension_catalog = FxmEffectExtensionCatalog::instance();
    if (extension_catalog.effects().empty())
        extension_catalog.reload();

    auto inspect_font = [&](const std::string &font_family, const std::string &layer_name) {
        const QString family = QString::fromStdString(font_family).trimmed();
        if (family.isEmpty())
            return;
        const QString key = family.toCaseFolded();
        if (checked_fonts.contains(key))
            return;
        checked_fonts.insert(key);
        if (!imported_font_family_available(family, available_families)) {
            append_unique_diagnostic(
                diagnostics.missing_fonts,
                QStringLiteral("%1: %2")
                    .arg(QString::fromStdString(layer_name), family));
        }
    };

    for (const auto &layer : title.layers) {
        if (!layer)
            continue;

        if (layer->type == LayerType::Image && !layer->image_path.empty() &&
            !QFileInfo::exists(QString::fromStdString(layer->image_path))) {
            append_unique_diagnostic(
                diagnostics.missing_images,
                QStringLiteral("%1: %2")
                    .arg(QString::fromStdString(layer->name),
                         QString::fromStdString(layer->image_path)));
        }

        if (layer->type == LayerType::Video && !layer->video_source.empty() &&
            !QFileInfo::exists(QString::fromStdString(layer->video_source))) {
            append_unique_diagnostic(
                diagnostics.missing_images,
                QStringLiteral("%1: %2")
                    .arg(QString::fromStdString(layer->name),
                         QString::fromStdString(layer->video_source)));
        }

        if (layer->type == LayerType::Audio && !layer->audio_source.empty() &&
            !QFileInfo::exists(QString::fromStdString(layer->audio_source))) {
            append_unique_diagnostic(
                diagnostics.missing_audio,
                QStringLiteral("%1: %2")
                    .arg(QString::fromStdString(layer->name),
                         QString::fromStdString(layer->audio_source)));
        }

        if (layer->type == LayerType::Text || layer->type == LayerType::Clock ||
            layer->type == LayerType::Ticker) {
            inspect_font(layer->font_family, layer->name);
            inspect_font(layer->rich_text.default_format.font_family, layer->name);
            if (layer->rich_text.has_typing_format &&
                (layer->rich_text.typing_format_mask & RichTextCharFontFamily)) {
                inspect_font(layer->rich_text.typing_format.font_family, layer->name);
            }
            for (const auto &range : layer->rich_text.ranges) {
                if (range.mask & RichTextCharFontFamily)
                    inspect_font(range.format.font_family, layer->name);
            }
            if (!layer->rich_text.auto_default_style_preset_id.empty())
                inspect_font(layer->rich_text.auto_default_style_cached_format.font_family, layer->name);
            for (const auto &rule : layer->rich_text.auto_style_rules) {
                if (rule.cached_mask & RichTextCharFontFamily)
                    inspect_font(rule.cached_format.font_family, layer->name);
            }
        }

        for (const LayerEffect &effect : layer->effects) {
            const QString stableId = effect.extension_id.empty()
                ? FxmEffectExtensionCatalog::builtInId(effect.type)
                : QString::fromStdString(effect.extension_id);
            const QString diagnosticKey = stableId.isEmpty()
                ? QStringLiteral("legacy:%1").arg(static_cast<int>(effect.type))
                : stableId;
            if (checked_effects.contains(diagnosticKey))
                continue;
            checked_effects.insert(diagnosticKey);

            const auto *extension = stableId.isEmpty()
                ? nullptr : extension_catalog.find(stableId);
            if (!extension || !extension->builtIn) {
                if (!extension || extension->shaderPath.isEmpty() ||
                    !QFileInfo(extension->shaderPath).isReadable()) {
                    append_unique_diagnostic(
                        diagnostics.missing_effects,
                        QStringLiteral("%1: %2")
                            .arg(QString::fromStdString(layer->name), stableId));
                }
                continue;
            }

            const EffectDescriptor *definition =
                effect_descriptor(extension->builtInType);
            if (!definition) {
                append_unique_diagnostic(
                    diagnostics.missing_effects,
                    QStringLiteral("%1: effect type %2")
                        .arg(QString::fromStdString(layer->name))
                        .arg(static_cast<int>(effect.type)));
                continue;
            }

            bool available = definition->has_embedded_fallback;
            const QString resolved_path =
                fxm_asset_path(definition->relative_path);
            if (!resolved_path.isEmpty())
                available = available || QFileInfo(resolved_path).isFile();
            if (!available) {
                append_unique_diagnostic(
                    diagnostics.missing_effects,
                    QStringLiteral("%1: %2")
                        .arg(QString::fromStdString(layer->name),
                             QString::fromUtf8(definition->display_name)));
            }
        }
    }
}

static QString import_diagnostics_text(const QString &title_name, const TitleImportDiagnostics &diagnostics)
{
    QStringList sections;
    sections.push_back(fxm_tr("OBSTitles.ImportMissingResourcesTitleFormat").arg(title_name));

    auto append_section = [&sections](const QString &heading, const std::vector<std::string> &items) {
        if (items.empty())
            return;
        sections.push_back(heading);
        for (const std::string &item : items)
            sections.push_back(QStringLiteral("  • %1").arg(QString::fromStdString(item)));
    };
    append_section(fxm_tr("OBSTitles.MissingEffectsHeading"), diagnostics.missing_effects);
    append_section(fxm_tr("OBSTitles.MissingImagesHeading"), diagnostics.missing_images);
    append_section(fxm_tr("OBSTitles.MissingAudioHeading"), diagnostics.missing_audio);
    append_section(fxm_tr("OBSTitles.MissingFontsHeading"), diagnostics.missing_fonts);
    return sections.join(QLatin1Char('\n'));
}

static void show_import_diagnostics(QWidget *parent, const QStringList &diagnostic_blocks)
{
    if (diagnostic_blocks.isEmpty())
        return;

    const QString details = diagnostic_blocks.join(QStringLiteral("\n\n"));
    QMessageBox dialog(parent);
    dialog.setIcon(QMessageBox::Warning);
    dialog.setWindowTitle(fxm_tr("OBSTitles.ImportMissingResources"));
    dialog.setText(fxm_tr("OBSTitles.ImportMissingResourcesMessage"));
    dialog.setInformativeText(details.left(1800));
    if (details.size() > 1800)
        dialog.setDetailedText(details);
    dialog.setStandardButtons(QMessageBox::Ok);
    dialog.exec();
}


static bool layer_uses_image_cue_value(const std::shared_ptr<Layer> &layer)
{
    return layer && layer->type == LayerType::Image;
}

static void apply_live_cue_layer_value(
    Title &title, const std::shared_ptr<Layer> &target, int cue_row,
    const std::string &value)
{
    if (!target)
        return;
    target->live_cue_hidden_if_empty = target->exposed_hide_if_empty && value.empty();
    apply_live_text_cue_style_to_layer(title, *target, cue_row);
    if (target->type == LayerType::Image) {
        fxm::apply_exposed_image_cue_value(*target, value);
        return;
    }
    if (target->type == LayerType::Chart) {
        fxm::live_text::apply_chart_cue_value(*target, value);
        return;
    }
    if (!target->expose_text)
        return;

    target->text_content = value;
    if (target->rich_text.empty())
        target->rich_text = rich_text_document_from_layer_defaults(*target);
    RichTextCharFormat insertion_format =
        rich_text_effective_typing_format(target->rich_text);
    if (target->expose_fill_color) {
        insertion_format.fill.type = 0;
        insertion_format.fill.color = live_text_cue_effective_color(title, *target, cue_row, false);
    }
    if (target->expose_stroke_color) {
        insertion_format.stroke.enabled = target->outline_enabled || target->stroke_width > 0.0f;
        insertion_format.stroke.width = target->stroke_width;
        insertion_format.stroke.opacity = target->outline_opacity;
        insertion_format.stroke.fill.type = 0;
        insertion_format.stroke.fill.color = live_text_cue_effective_color(title, *target, cue_row, true);
    }
    rich_text_document_replace_text(target->rich_text, target->text_content, insertion_format,
                                        target->rich_text.has_typing_format
                                            ? target->rich_text.typing_format_mask : 0);
}

static QString live_cue_image_file_name(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        return QString();
    const QFileInfo info(trimmed);
    const QString file_name = info.fileName();
    return file_name.isEmpty() ? trimmed : file_name;
}

static QString current_scene_collection_titles_label()
{
    const auto *provider = fxm::host_context_provider();
    QString name = provider
        ? QString::fromStdString(provider->project_scope_name()).trimmed()
        : QString();

    if (name.isEmpty())
        name = fxm_tr("OBSTitles.SceneCollectionFallback");

    return fxm_tr("OBSTitles.SceneCollectionTitlesAndGraphicsFormat").arg(name);
}

static QString live_text_layer_header(const std::shared_ptr<Layer> &layer)
{
    if (!layer) return fxm_tr("OBSTitles.Text");
    QString name = QString::fromStdString(layer->name).trimmed();
    if (!name.isEmpty()) return name;
    if (layer_uses_image_cue_value(layer)) {
        name = live_cue_image_file_name(QString::fromStdString(layer->image_path)).trimmed();
        return name.isEmpty() ? fxm_tr("OBSTitles.Image") : name;
    }
    name = QString::fromStdString(layer->text_content).trimmed();
    return name.isEmpty() ? fxm_tr("OBSTitles.Text") : name;
}

static QString live_cue_cache_text(FrameCacheState state)
{
    switch (state) {
    case FrameCacheState::Queued: return fxm_tr("OBSTitles.Queued");
    case FrameCacheState::Rendering: return fxm_tr("OBSTitles.Caching");
    case FrameCacheState::CachedRam: return fxm_tr("OBSTitles.ReadyRam");
    case FrameCacheState::CachedDisk: return fxm_tr("OBSTitles.ReadyDisk");
    case FrameCacheState::Stale: return fxm_tr("OBSTitles.CacheErrorOrStale");
    case FrameCacheState::Disabled: return fxm_tr("OBSTitles.CachingDisabled");
    case FrameCacheState::NotCached:
    default: return fxm_tr("OBSTitles.NotCached");
    }
}

static QString live_cue_cache_tooltip(FrameCacheState state)
{
    switch (state) {
    case FrameCacheState::Queued:
        return fxm_tr("OBSTitles.LiveCueQueuedTooltip");
    case FrameCacheState::Rendering:
        return fxm_tr("OBSTitles.LiveCueCachingTooltip");
    case FrameCacheState::CachedRam:
        return fxm_tr("OBSTitles.LiveCueCachedRamTooltip");
    case FrameCacheState::CachedDisk:
        return fxm_tr("OBSTitles.LiveCueCachedDiskTooltip");
    case FrameCacheState::Stale:
        return fxm_tr("OBSTitles.LiveCueStaleTooltip");
    case FrameCacheState::Disabled:
        return fxm_tr("OBSTitles.LiveCueCacheDisabledTooltip");
    case FrameCacheState::NotCached:
    default:
        return fxm_tr("OBSTitles.LiveCueNotCachedTooltip");
    }
}

static QColor live_cue_cache_color(FrameCacheState state)
{
    switch (state) {
    /* Queued and Rendering are a single visual progress phase.  Giving them
     * different tints made the same bucket flicker orange/yellow every time
     * ownership moved between the queue and worker.  The four SVG shapes, not
     * the scheduler's transient state, now communicate prerender progress. */
    case FrameCacheState::Queued:
    case FrameCacheState::Rendering: return QColor(255, 202, 74);
    case FrameCacheState::CachedRam: return QColor(39, 186, 103);
    case FrameCacheState::CachedDisk: return QColor(74, 144, 226);
    case FrameCacheState::Stale: return QColor(214, 90, 90);
    case FrameCacheState::Disabled: return QColor(130, 130, 130);
    case FrameCacheState::NotCached:
    default: return QColor(130, 130, 130);
    }
}

static const char *live_cue_cache_progress_icon_name(int progress_percent)
{
    const int progress = std::clamp(progress_percent, 0, 100);
    if (progress >= 100)
        return "cache-ok.svg";
    if (progress >= 75)
        return "cache-progress-75-100.svg";
    if (progress >= 50)
        return "cache-progress-50-75.svg";
    if (progress >= 25)
        return "cache-progress-25-50.svg";
    return "cache-queued-0-25.svg";
}

static const char *live_cue_cache_icon_name(FrameCacheState state, int progress_percent)
{
    switch (state) {
    case FrameCacheState::Queued:
    case FrameCacheState::Rendering:
        return live_cue_cache_progress_icon_name(progress_percent);
    case FrameCacheState::CachedRam:
    case FrameCacheState::CachedDisk:
    case FrameCacheState::Disabled:
        return "cache-ok.svg";
    case FrameCacheState::Stale:
        return "cache-error.svg";
    case FrameCacheState::NotCached:
    default:
        return "cache-queued-0-25.svg";
    }
}

static QIcon obs_icon(const char *file_name);
static QIcon obs_icon(const char *file_name, const QColor &color);
static int obs_toolbar_icon_extent(QWidget *widget);
static int obs_tool_button_extent(QWidget *widget);
static QIcon title_list_status_icon(const Title &title, bool has_exposed_text, bool has_scene_mask);
static bool title_has_external_data_connection(const Title &title);

struct LiveCueAggregateCacheStatus {
    FrameCacheState state = FrameCacheState::NotCached;
    int progress_percent = 0;
    int ready_rows = 0;
    int total_rows = 0;
};

static LiveCueAggregateCacheStatus aggregate_live_cue_cache_status(const std::shared_ptr<Title> &title)
{
    LiveCueAggregateCacheStatus status;
    if (!title || title->live_text_rows.empty()) {
        status.state = CacheManager::instance().cacheEnabled()
            ? FrameCacheState::NotCached
            : FrameCacheState::Disabled;
        return status;
    }

    status.total_rows = static_cast<int>(title->live_text_rows.size());
    if (!CacheManager::instance().cacheEnabled()) {
        status.state = FrameCacheState::Disabled;
        return status;
    }

    for (int row = 0; row < status.total_rows; ++row) {
        const FrameCacheState row_state = CacheManager::instance().liveCueState(title, row);
        if (row_state == FrameCacheState::CachedRam || row_state == FrameCacheState::CachedDisk)
            ++status.ready_rows;
    }

    /* Aggregate progress includes steady row frames and the independent
     * Background Persistence/outro transition-pair states. Row icons remain
     * stable when a new row is added; only the title-level badge reflects the
     * newly required transition work. */
    status.progress_percent = CacheManager::instance().liveCueAggregateProgressPercent(title);
    status.state = CacheManager::instance().liveCueAggregateState(title);
    return status;
}


static QString aggregate_live_cue_cache_tooltip(const LiveCueAggregateCacheStatus &status)
{
    if (status.total_rows <= 0)
        return fxm_tr("OBSTitles.LiveCueNotCachedTooltip");
    return QStringLiteral("%1 (%2%, %3/%4 cues)")
        .arg(live_cue_cache_text(status.state))
        .arg(status.progress_percent)
        .arg(status.ready_rows)
        .arg(status.total_rows);
}

static QIcon title_list_combined_status_icon(const Title &title, bool has_exposed_text, bool has_scene_mask,
                                             const LiveCueAggregateCacheStatus &cache_status,
                                             bool show_cache)
{
    constexpr int gap = 3;
    const bool show_external = title_has_external_data_connection(title);
    const int icon_count = 1 + (show_cache ? 1 : 0) + (show_external ? 1 : 0);
    const int width = kTitleListIconExtent * icon_count + gap * (icon_count - 1);
    QPixmap pixmap(width, kTitleListIconExtent);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    int x = 0;
    if (show_external) {
        const QPixmap globe = obs_icon("globe.svg").pixmap(kTitleListIconExtent, kTitleListIconExtent);
        painter.drawPixmap(x, 0, globe);
        x += kTitleListIconExtent + gap;
    }
    const QPixmap type_pixmap = title_list_status_icon(title, has_exposed_text, has_scene_mask)
                                    .pixmap(kTitleListIconExtent, kTitleListIconExtent);
    painter.drawPixmap(x, 0, type_pixmap);
    x += kTitleListIconExtent + gap;
    if (show_cache) {
        const QPixmap cache_pixmap = obs_icon(
            live_cue_cache_icon_name(cache_status.state, cache_status.progress_percent),
            live_cue_cache_color(cache_status.state)).pixmap(kTitleListIconExtent, kTitleListIconExtent);
        painter.drawPixmap(x, 0, cache_pixmap);
    }
    painter.end();
    return QIcon(pixmap);
}

static QToolButton *centered_cell_tool_button(QTableWidget *table, int row, int col, const char *object_name)
{
    if (!table || row < 0 || row >= table->rowCount() || col < 0 || col >= table->columnCount())
        return nullptr;

    if (auto *cell = table->cellWidget(row, col)) {
        if (auto *button = cell->findChild<QToolButton *>(QString::fromUtf8(object_name)))
            return button;
        if (auto *button = qobject_cast<QToolButton *>(cell))
            return button;
    }

    auto *holder = new QWidget(table);
    auto *layout = new QHBoxLayout(holder);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignCenter);

    auto *button = new QToolButton(holder);
    button->setObjectName(QString::fromUtf8(object_name));
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    const int button_extent = obs_tool_button_extent(table);
    button->setMinimumSize(button_extent, button_extent);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    layout->addWidget(button, 0, Qt::AlignCenter);

    table->setCellWidget(row, col, holder);
    return button;
}

static QToolButton *live_cue_cache_button(QTableWidget *table, int row)
{
    if (!table)
        return nullptr;
    const int col = table->columnCount() <= 3 ? kStaticCacheColumn : kLiveCacheColumn;
    return centered_cell_tool_button(table, row, col, "liveCueCacheButton");
}

static QToolButton *live_cue_button(QTableWidget *table, int row, int col)
{
    auto *button = centered_cell_tool_button(table, row, col, "liveCueButton");
    if (button) {
        /* The cue control owns the complete table cell, while the icon remains
         * centered by QToolButton.  Clicking whitespace in the cue column now
         * triggers the same action as clicking the icon itself. */
        button->setMinimumSize(0, 0);
        button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        if (auto *layout = button->parentWidget() ? button->parentWidget()->layout() : nullptr) {
            layout->setAlignment(Qt::Alignment{});
            layout->setAlignment(button, Qt::Alignment{});
        }
    }
    return button;
}

static QColor live_cue_state_color(bool current, bool queued, bool ending = false)
{
    /* Ending has priority over current: from the instant uncue is requested
     * until the outro reaches the authored end, the active cue is shown in
     * yellow rather than red. */
    if (ending)
        return QColor(255, 202, 74);
    if (current)
        return QColor(208, 48, 48);
    if (queued)
        return QColor(40, 184, 79);
    return QColor(150, 150, 150);
}

static QColor live_cue_select_cell_color(bool current, bool queued, bool ending = false)
{
    if (ending)
        return QColor(255, 202, 74, 88);
    if (current)
        return QColor(208, 48, 48, 72);
    if (queued)
        return QColor(40, 184, 79, 72);
    return QColor();
}

static const char *obs_native_action_resource(const char *file_name)
{
    struct NativeIcon {
        const char *file_name;
        const char *resource;
    };
    static constexpr NativeIcon native_icons[] = {
        {"add.svg", ":/res/images/plus.svg"},
        {"delete.svg", ":/res/images/trash.svg"},
        {"move-up.svg", ":/res/images/up.svg"},
        {"move-down.svg", ":/res/images/down.svg"},
        {"settings.svg", ":/res/images/settings/general.svg"},
        {"save.svg", ":/res/images/save.svg"},
        {"refresh.svg", ":/res/images/refresh.svg"},
        {"play.svg", ":/res/images/media/media_play.svg"},
        {"pause.svg", ":/res/images/media/media_pause.svg"},
    };

    for (const NativeIcon &native : native_icons) {
        if (std::strcmp(file_name, native.file_name) == 0)
            return native.resource;
    }
    return nullptr;
}

static QIcon obs_icon(const char *file_name)
{
    /* Menus cannot receive OBS' semantic widget classes, so tint the same
     * host resource geometry with the active palette. Non-action names keep
     * the Flux Motion artwork. Graphic-type badges bypass this helper
     * explicitly and always use fxm_icon(). */
    if (const char *resource = obs_native_action_resource(file_name)) {
        const QIcon icon = fxm_tinted_svg_icon(
            QString::fromLatin1(resource), fxm_icon_color());
        if (!icon.isNull())
            return icon;
    }
    return fxm_icon(file_name);
}

static QIcon obs_action_icon(const char *file_name)
{
    if (const char *resource = obs_native_action_resource(file_name)) {
        const QIcon icon(QString::fromLatin1(resource));
        if (!icon.isNull())
            return icon;
    }
    return fxm_icon(file_name);
}

static QIcon obs_icon(const char *file_name, const QColor &color)
{
    return fxm_icon(file_name, color);
}

static std::string obs_text_std(const char *key)
{
    return fxm_tr(key).toStdString();
}

static int obs_toolbar_icon_extent(QWidget *widget)
{
    QStyle *style = widget ? widget->style() : (qApp ? qApp->style() : nullptr);
    int size = style ? style->pixelMetric(QStyle::PM_SmallIconSize, nullptr, widget) : 0;
    return size > 0 ? size : 16;
}

static QSize obs_native_dock_icon_size()
{
    /* Read the authoritative value from OBS' own Scenes/Sources toolbars.
     * OBSBasic.ui initializes both to 16x16, while querying the live widgets
     * also preserves any host-side DPI/theme adjustment made at runtime. */
    if (qApp) {
        const QStringList native_toolbar_names = {
            QStringLiteral("scenesToolbar"),
            QStringLiteral("sourcesToolbar"),
        };
        const QWidgetList widgets = QApplication::allWidgets();
        for (const QString &name : native_toolbar_names) {
            for (QWidget *candidate : widgets) {
                auto *toolbar = qobject_cast<QToolBar *>(candidate);
                if (!toolbar || toolbar->objectName() != name)
                    continue;
                QSize size;
                const QList<QToolButton *> native_buttons =
                    toolbar->findChildren<QToolButton *>();
                for (QToolButton *button : native_buttons) {
                    if (button && !button->icon().isNull()) {
                        size = button->iconSize();
                        break;
                    }
                }
                if (!size.isValid())
                    size = toolbar->iconSize();
                if (size.width() >= 8 && size.height() >= 8 &&
                    size.width() <= 64 && size.height() <= 64) {
                    return size;
                }
            }
        }
    }
    return QSize(16, 16);
}

static void apply_obs_native_toolbar_icon_size(QToolBar *toolbar)
{
    if (!toolbar)
        return;
    const QSize size = obs_native_dock_icon_size();
    const int style_extent = std::min(size.width(), size.height());
    const QString style = QStringLiteral(
        "QToolButton { icon-size: %1px; }")
                              .arg(style_extent);
    if (toolbar->property("fxmObsIconSize").toSize() != size ||
        toolbar->styleSheet() != style) {
        toolbar->setProperty("fxmObsIconSize", size);
        /* A local rule is required because the application-level generic
         * QToolButton rule otherwise overrides setIconSize on custom docks. */
        toolbar->setStyleSheet(style);
    }
    toolbar->setIconSize(size);
    const QList<QToolButton *> buttons =
        toolbar->findChildren<QToolButton *>();
    for (QToolButton *button : buttons)
        button->setIconSize(size);
}

static int obs_layout_spacing(QWidget *widget)
{
    QStyle *style = widget ? widget->style() : (qApp ? qApp->style() : nullptr);
    int spacing = style ? style->pixelMetric(QStyle::PM_LayoutVerticalSpacing, nullptr, widget) : -1;
    return spacing >= 0 ? spacing : 4;
}

static int obs_tool_button_extent(QWidget *widget)
{
    QStyle *style = widget ? widget->style() : (qApp ? qApp->style() : nullptr);
    const int icon_extent = obs_toolbar_icon_extent(widget);
    const int button_margin = style
        ? std::max(0, style->pixelMetric(QStyle::PM_ButtonMargin, nullptr, widget))
        : 6;
    return icon_extent + button_margin * 2;
}

static int obs_dock_toolbar_extent(QWidget *widget)
{
    QStyle *style = widget ? widget->style() : (qApp ? qApp->style() : nullptr);
    const int item_margin = style
        ? std::max(0, style->pixelMetric(QStyle::PM_ToolBarItemMargin, nullptr, widget))
        : 2;
    const int frame_width = style
        ? std::max(0, style->pixelMetric(QStyle::PM_ToolBarFrameWidth, nullptr, widget))
        : 1;
    return std::max(obs_tool_button_extent(widget),
                    obs_toolbar_icon_extent(widget) + 2 * (item_margin + frame_width));
}

static const char *obs_icon_style_class(const char *file_name)
{
    struct IconClass {
        const char *file_name;
        const char *style_class;
    };
    static constexpr IconClass icon_classes[] = {
        {"add.svg", "icon-plus"},
        {"delete.svg", "icon-trash"},
        {"move-up.svg", "icon-up"},
        {"move-down.svg", "icon-down"},
        {"settings.svg", "icon-gear"},
        {"save.svg", "icon-save"},
        {"refresh.svg", "icon-refresh"},
        {"play.svg", "icon-media-play"},
        {"pause.svg", "icon-media-pause"},
    };
    for (const IconClass &entry : icon_classes) {
        if (std::strcmp(file_name, entry.file_name) == 0)
            return entry.style_class;
    }
    return nullptr;
}

static void apply_obs_icon_style_class(QWidget *widget, const char *file_name)
{
    if (!widget)
        return;

    widget->setProperty("fxmIconName", QString::fromUtf8(file_name));
    QStringList classes = widget->property("class").toString()
                              .split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (!classes.contains(QStringLiteral("btn-tool")))
        classes.push_back(QStringLiteral("btn-tool"));
    if (const char *style_class = obs_icon_style_class(file_name)) {
        const QString semantic_class = QString::fromLatin1(style_class);
        if (!classes.contains(semantic_class))
            classes.push_back(semantic_class);
    }
    widget->setProperty("class", classes.join(QLatin1Char(' ')));
}

static QToolBar *make_obs_dock_toolbar(QWidget *parent)
{
    auto *toolbar = new QToolBar(parent);
    toolbar->setObjectName(QStringLiteral("FluxMotionDockToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setOrientation(Qt::Horizontal);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    apply_obs_native_toolbar_icon_size(toolbar);
    return toolbar;
}

static QToolButton *make_obs_dock_tool_button(QWidget *parent, const QString &text,
                                              const QIcon &icon, const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setAccessibleName(text);
    button->setToolTip(tooltip);
    button->setIcon(icon);
    button->setIconSize(obs_native_dock_icon_size());
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return button;
}

static QToolButton *make_obs_dock_tool_button(QWidget *parent, const QString &text,
                                              const char *icon_name, const QString &tooltip)
{
    QToolButton *button = make_obs_dock_tool_button(
        parent, text, obs_action_icon(icon_name), tooltip);
    apply_obs_icon_style_class(button, icon_name);
    return button;
}

static QString dock_highlight_button_style(bool hide_menu_indicator = false,
                                           bool include_persistence_state = false)
{
    QString style;
    if (include_persistence_state) {
        style += QStringLiteral(
            "QToolButton[persistenceState=\"background\"]{"
            "background:palette(midlight);color:palette(text);"
            "border:1px solid palette(highlight);}"
            "QToolButton[persistenceState=\"background\"]:hover{background:palette(mid);}");
    }
    style += QStringLiteral(
        "QToolButton:checked{"
        "background:palette(highlight);color:palette(highlighted-text);"
        "border:1px solid palette(highlight);}"
        "QToolButton:checked:hover{background:palette(highlight);color:palette(highlighted-text);}");
    if (hide_menu_indicator)
        style += QStringLiteral("QToolButton::menu-indicator{image:none;width:0px;}");
    return style;
}

static LongPressToolButton *make_obs_dock_long_press_tool_button(QWidget *parent, const QString &text,
                                                                 const QIcon &icon, const QString &tooltip)
{
    auto *button = new LongPressToolButton(parent);
    button->setText(text);
    button->setAccessibleName(text);
    button->setToolTip(tooltip);
    button->setIcon(icon);
    button->setIconSize(obs_native_dock_icon_size());
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    button->setPopupMode(QToolButton::DelayedPopup);
    return button;
}

static LongPressToolButton *make_obs_dock_long_press_tool_button(
    QWidget *parent, const QString &text, const char *icon_name,
    const QString &tooltip)
{
    LongPressToolButton *button = make_obs_dock_long_press_tool_button(
        parent, text, obs_action_icon(icon_name), tooltip);
    apply_obs_icon_style_class(button, icon_name);
    return button;
}


static QWidget *toolbar_spacer(QWidget *parent)
{
    auto *spacer = new QWidget(parent);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return spacer;
}

static QIcon globe_status_icon(bool enabled)
{
    const QColor color = enabled ? QColor(38, 184, 79) : QColor(145, 145, 145);
    return obs_icon("globe.svg", color);
}

static QJsonArray live_text_rows_to_json(const std::vector<std::vector<std::string>> &rows)
{
    QJsonArray json_rows;
    for (const auto &row : rows) {
        QJsonArray json_row;
        for (const auto &cell : row)
            json_row.append(QString::fromStdString(cell));
        json_rows.append(json_row);
    }
    return json_rows;
}

static bool live_text_rows_from_json(const QJsonDocument &doc,
                                     std::vector<std::vector<std::string>> &rows,
                                     QString *error)
{
    QJsonArray json_rows;
    if (doc.isArray()) {
        json_rows = doc.array();
    } else if (doc.isObject()) {
        const QJsonObject root = doc.object();
        const QJsonValue rows_value = root.value(QStringLiteral("rows"));
        const QJsonValue live_rows_value = root.value(QStringLiteral("live_text_rows"));
        if (rows_value.isArray())
            json_rows = rows_value.toArray();
        else if (live_rows_value.isArray())
            json_rows = live_rows_value.toArray();
        else {
            if (error) *error = fxm_tr("OBSTitles.LiveCueJsonRowsError");
            return false;
        }
    } else {
        if (error) *error = fxm_tr("OBSTitles.LiveCueJsonInvalidError");
        return false;
    }

    rows.clear();
    rows.reserve(json_rows.size());
    for (const QJsonValue &row_value : json_rows) {
        if (!row_value.isArray()) {
            if (error) *error = fxm_tr("OBSTitles.LiveCueJsonRowArrayError");
            return false;
        }
        std::vector<std::string> row;
        const QJsonArray json_row = row_value.toArray();
        row.reserve(json_row.size());
        for (const QJsonValue &cell_value : json_row)
            row.push_back(cell_value.toVariant().toString().toStdString());
        rows.push_back(std::move(row));
    }
    return true;
}

static void set_bold_label(QLabel *label)
{
    if (!label) return;
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
}


class LiveTextCueField : public QPlainTextEdit {
public:
    explicit LiveTextCueField(const QString &text, QWidget *parent = nullptr)
        : QPlainTextEdit(parent)
    {
        setPlainText(text);
        update_tooltip(text);
        setLineWrapMode(QPlainTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTabChangesFocus(true);
        document()->setDocumentMargin(0.0);
        setFrameShape(QFrame::NoFrame);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        connect(this, &QPlainTextEdit::textChanged, this, [this]() {
            update_tooltip(toPlainText());
        });
    }

    void update_tooltip(const QString &text)
    {
        setToolTip(text);
        if (viewport())
            viewport()->setToolTip(text);
    }

    std::function<void(const QString &)> editing_finished;
    std::function<void(bool)> focus_changed;

protected:
    void focusInEvent(QFocusEvent *event) override
    {
        QPlainTextEdit::focusInEvent(event);
        if (focus_changed)
            focus_changed(true);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if (event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (editing_finished)
                editing_finished(toPlainText());
            focusNextChild();
            event->accept();
            return;
        }

        // A literal newline is intentionally available only through Shift+Enter.
        QPlainTextEdit::keyPressEvent(event);
    }

    void focusOutEvent(QFocusEvent *event) override
    {
        QPlainTextEdit::focusOutEvent(event);
        if (editing_finished)
            editing_finished(toPlainText());
        if (focus_changed) {
            const auto callback = focus_changed;
            QTimer::singleShot(0, qApp, [callback]() { callback(false); });
        }
    }
};

class LiveImageCueField : public QWidget {
public:
    explicit LiveImageCueField(const QString &path, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 2, 3, 2);
        layout->setSpacing(4);

        label_ = new QLabel(this);
        label_->setTextInteractionFlags(Qt::NoTextInteraction);
        label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        label_->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

        button_ = new QToolButton(this);
        button_->setAutoRaise(true);
        button_->setCursor(Qt::ArrowCursor);
        button_->setIcon(obs_icon("import.svg"));
        button_->setToolTip(fxm_tr("OBSTitles.Browse"));
        button_->setAccessibleName(fxm_tr("OBSTitles.Browse"));
        button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        clear_button_ = new QToolButton(this);
        clear_button_->setAutoRaise(true);
        clear_button_->setCursor(Qt::ArrowCursor);
        clear_button_->setIcon(obs_action_icon("delete.svg"));
        apply_obs_icon_style_class(clear_button_, "delete.svg");
        clear_button_->setToolTip(QStringLiteral("Clear image"));
        clear_button_->setAccessibleName(QStringLiteral("Clear image"));
        clear_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        layout->addWidget(label_, 1);
        layout->addWidget(button_, 0);
        layout->addWidget(clear_button_, 0);

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        label_->installEventFilter(this);
        button_->installEventFilter(this);
        clear_button_->installEventFilter(this);
        set_path(path);
        connect(button_, &QToolButton::clicked, this, [this]() {
            const QString initial_dir = path_.isEmpty()
                ? QString()
                : QFileInfo(path_).absolutePath();
            const QString selected = QFileDialog::getOpenFileName(
                this, fxm_tr("OBSTitles.ChooseImage"), initial_dir,
                fxm_tr("OBSTitles.ImageFileFilter"));
            if (selected.isEmpty())
                return;
            set_path(selected);
            if (path_changed)
                path_changed(selected);
        });
        connect(clear_button_, &QToolButton::clicked, this, [this]() {
            set_path(QString());
            if (path_changed)
                path_changed(QString());
        });
    }

    QString path() const { return path_; }

    void set_read_only(bool read_only)
    {
        read_only_ = read_only;
        if (button_)
            button_->setVisible(!read_only_);
        if (clear_button_)
            clear_button_->setVisible(!read_only_);
        setProperty("externalTableReadOnly", read_only_);
    }

    bool is_read_only() const { return read_only_; }

    void set_path(const QString &path)
    {
        path_ = path;
        const QString file_name = live_cue_image_file_name(path_);
        label_->setText(file_name.isEmpty() ? QStringLiteral("-") : file_name);
        preview_tooltip_ = path_.isEmpty() ? QString() : path_.toHtmlEscaped();
        preview_tooltip_dirty_ = true;
        setToolTip(preview_tooltip_);
        label_->setToolTip(preview_tooltip_);
    }

    std::function<void(const QString &)> path_changed;

protected:
    bool event(QEvent *event) override
    {
        if (event && event->type() == QEvent::ToolTip)
            return show_preview_tooltip(static_cast<QHelpEvent *>(event));
        return QWidget::event(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if ((watched == label_ || watched == button_ || watched == clear_button_) &&
            event && event->type() == QEvent::ToolTip)
            return show_preview_tooltip(static_cast<QHelpEvent *>(event));
        return QWidget::eventFilter(watched, event);
    }

private:
    bool show_preview_tooltip(QHelpEvent *event)
    {
        ensure_preview_tooltip();
        if (!event || preview_tooltip_.isEmpty())
            return false;
        QToolTip::showText(event->globalPos(), preview_tooltip_, this);
        return true;
    }

    void ensure_preview_tooltip()
    {
        if (!preview_tooltip_dirty_)
            return;
        preview_tooltip_dirty_ = false;

        const QFileInfo info(path_);
        if (!info.exists() || !info.isFile()) {
            preview_tooltip_ = path_.isEmpty() ? QString() : path_.toHtmlEscaped();
            setToolTip(preview_tooltip_);
            if (label_) label_->setToolTip(preview_tooltip_);
            return;
        }

        QImage image(path_);
        if (image.isNull()) {
            preview_tooltip_ = path_.toHtmlEscaped();
            setToolTip(preview_tooltip_);
            if (label_) label_->setToolTip(preview_tooltip_);
            return;
        }
