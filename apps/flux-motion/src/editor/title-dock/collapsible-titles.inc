namespace {
constexpr const char *kTitlesCollapsedKey = "titlesCollapsed";
constexpr const char *kTitlesExpandedWidthKey = "titlesExpandedWidth";
constexpr const char *kTitlesExpandedSplitterSizeKey = "titlesExpandedSplitterSize";
constexpr const char *kStoredDockAreaKey = "dockArea";
constexpr const char *kDockSerializationSchemaVersionKey = "serializationSchemaVersion";
constexpr const char *kDockSavedDevelopmentVersionKey = "savedDevelopmentVersion";
constexpr int kTitlesMinimumExpandedWidth = 220;
constexpr int kTitlesDefaultExpandedSplitterSize = 240;

static int titles_compact_rail_height(QWidget *widget)
{
    const int style_extent = obs_dock_toolbar_extent(widget);
    const int text_extent = widget
        ? widget->fontMetrics().height() + 2 * obs_layout_spacing(widget)
        : 0;
    return std::max(style_extent, text_extent);
}

static QString title_cue_state_text(TitleCueVisualState state)
{
    switch (state) {
    case TitleCueVisualState::Queued:
        return fxm_tr("OBSTitles.TitlesRailCueQueued");
    case TitleCueVisualState::Cued:
        return fxm_tr("OBSTitles.TitlesRailCueCued");
    case TitleCueVisualState::Ending:
        return fxm_tr("OBSTitles.TitlesRailCueEnding");
    case TitleCueVisualState::Inactive:
    default:
        return fxm_tr("OBSTitles.TitlesRailCueInactive");
    }
}

static QString title_cue_state_short_text(TitleCueVisualState state)
{
    switch (state) {
    case TitleCueVisualState::Queued:
        return fxm_tr("OBSTitles.TitlesRailCueQueuedShort");
    case TitleCueVisualState::Cued:
        return fxm_tr("OBSTitles.TitlesRailCueCuedShort");
    case TitleCueVisualState::Ending:
        return fxm_tr("OBSTitles.TitlesRailCueEndingShort");
    case TitleCueVisualState::Inactive:
    default:
        return fxm_tr("OBSTitles.TitlesRailCueInactiveShort");
    }
}

}

void TitleDock::update_stored_dock_area(Qt::DockWidgetArea area)
{
    if (area != Qt::NoDockWidgetArea)
        stored_dock_area_ = area;
    update_titles_collapse_direction();
    save_dock_settings();
}

void TitleDock::update_titles_collapse_direction()
{
    if (!btn_titles_collapse_ || !btn_titles_expand_)
        return;

    const bool floating = isFloating();
    const bool right_docked = !floating && stored_dock_area_ == Qt::RightDockWidgetArea;

    if (auto *collapse_caret = qobject_cast<FxmCaretButton *>(btn_titles_collapse_))
        collapse_caret->setCaretState(2);
    if (auto *expand_caret = qobject_cast<FxmCaretButton *>(btn_titles_expand_))
        expand_caret->setCaretState(0);

    if (titles_header_layout_) {
        titles_header_layout_->removeWidget(btn_titles_collapse_);
        if (right_docked || floating)
            titles_header_layout_->addWidget(btn_titles_collapse_);
        else
            titles_header_layout_->insertWidget(0, btn_titles_collapse_);
    }
    if (titles_compact_layout_) {
        titles_compact_layout_->removeWidget(btn_titles_expand_);
        if (right_docked || floating)
            titles_compact_layout_->addWidget(btn_titles_expand_);
        else
            titles_compact_layout_->insertWidget(0, btn_titles_expand_);
    }
}


void TitleDock::set_titles_collapsed(bool collapsed, bool persist)
{
    if (!titles_section_ || !titles_expanded_content_ || !titles_compact_rail_)
        return;

    const int compact_height = titles_compact_rail_height(titles_compact_rail_);
    const bool state_changed = titles_collapsed_ != collapsed;
    if (collapsed && state_changed) {
        if (sections_) {
            const QList<int> sizes = sections_->sizes();
            if (!sizes.isEmpty() && sizes.front() > compact_height + obs_layout_spacing(sections_))
                titles_expanded_splitter_size_ = sizes.front();
        }
        if (width() >= kTitlesMinimumExpandedWidth)
            titles_expanded_width_ = width();
    }

    titles_collapsed_ = collapsed;
    update_titles_compact_rail();
    update_titles_collapse_direction();

    if (collapsed) {
        titles_expanded_content_->hide();
        titles_compact_rail_->show();
        titles_section_->setMinimumHeight(compact_height);
        titles_section_->setMaximumHeight(compact_height);
        if (sections_) {
            const QList<int> current = sections_->sizes();
            const int total = std::accumulate(current.begin(), current.end(), 0);
            sections_->setSizes({compact_height,
                                 std::max(1, total - compact_height)});
        }
    } else {
        titles_section_->setMinimumHeight(0);
        titles_section_->setMaximumHeight(QWIDGETSIZE_MAX);
        titles_compact_rail_->hide();
        titles_expanded_content_->show();
        if (isFloating() && titles_expanded_width_ >= kTitlesMinimumExpandedWidth)
            resize(titles_expanded_width_, height());
        if (sections_) {
            const int requested = std::max(kTitlesDefaultExpandedSplitterSize,
                                           titles_expanded_splitter_size_);
            QTimer::singleShot(0, this, [this, requested, compact_height]() {
                if (!sections_ || titles_collapsed_)
                    return;
                const QList<int> current = sections_->sizes();
                const int total = std::accumulate(current.begin(), current.end(), 0);
                if (total <= 0) {
                    sections_->setSizes({requested, 96});
                    return;
                }
                const int minimum_title_size =
                    compact_height + obs_layout_spacing(sections_);
                const int maximum_title_size = std::max(minimum_title_size,
                                                        total - 96);
                const int title_size = std::clamp(requested,
                                                  minimum_title_size,
                                                  maximum_title_size);
                sections_->setSizes({title_size, std::max(1, total - title_size)});
            });
        }
    }

    if (persist)
        save_dock_settings();
}

void TitleDock::update_titles_compact_rail()
{
    if (!titles_collapsed_ || !titles_compact_active_title_ ||
        !titles_compact_cue_state_ || !titles_compact_cache_state_)
        return;

    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) {
        titles_compact_active_title_->setIcon(obs_icon("layer-hidden.svg"));
        titles_compact_active_title_->setText(fxm_tr("OBSTitles.TitlesRailNoTitle"));
        titles_compact_active_title_->setToolTip(fxm_tr("OBSTitles.NoTitleSelected"));
        titles_compact_cue_state_->setIcon(obs_icon("cue.svg"));
        titles_compact_cue_state_->setText(
            fxm_tr("OBSTitles.TitlesRailCueInactiveShort"));
        titles_compact_cue_state_->setToolTip(fxm_tr("OBSTitles.TitlesRailCueInactive"));
        titles_compact_cache_state_->setIcon(QIcon());
        titles_compact_cache_state_->setText(QStringLiteral("0%"));
        titles_compact_cache_state_->setToolTip(fxm_tr("OBSTitles.LiveCueNotCachedTooltip"));
        return;
    }

    const QString title_id = QString::fromStdString(title->id);
    const bool active = active_title_source_ids().contains(title_id);
    const QString title_name = QString::fromStdString(title->name);
    titles_compact_active_title_->setIcon(obs_icon(active ? "layer-visible.svg" : "layer-hidden.svg"));
    titles_compact_active_title_->setText(active
        ? fxm_tr("OBSTitles.TitlesRailActiveTitleFormat").arg(title_name)
        : fxm_tr("OBSTitles.TitlesRailInactiveTitleFormat").arg(title_name));
    titles_compact_active_title_->setToolTip(titles_compact_active_title_->text());

    const bool waiting_for_prerender = cache_waiting_title_id_ == title_id &&
        cache_waiting_cue_row_ >= 0;
    const TitleCueVisualState cue_state = title_cue_visual_state(*title, waiting_for_prerender);
    titles_compact_cue_state_->setIcon(obs_icon("cue.svg", title_cue_visual_color(cue_state)));
    titles_compact_cue_state_->setText(title_cue_state_short_text(cue_state));
    titles_compact_cue_state_->setToolTip(title_cue_state_text(cue_state));

    const LiveCueAggregateCacheStatus cache_status = aggregate_live_cue_cache_status(title);
    if (cache_status.state == FrameCacheState::Disabled)
        titles_compact_cache_state_->setIcon(QIcon());
    else
        titles_compact_cache_state_->setIcon(obs_icon(
            live_cue_cache_icon_name(cache_status.state, cache_status.progress_percent),
            live_cue_cache_color(cache_status.state)));
    titles_compact_cache_state_->setText(cache_status.state == FrameCacheState::Disabled
        ? fxm_tr("OBSTitles.TitlesRailCacheOff")
        : QStringLiteral("%1%").arg(cache_status.progress_percent));
    titles_compact_cache_state_->setToolTip(aggregate_live_cue_cache_tooltip(cache_status));
}

void TitleDock::resizeEvent(QResizeEvent *event)
{
    QDockWidget::resizeEvent(event);
    if (!titles_collapsed_ && event && event->size().width() >= kTitlesMinimumExpandedWidth)
        titles_expanded_width_ = event->size().width();
}
