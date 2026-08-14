    auto *live_text_settings_menu = new QMenu(btn_live_text_settings_);
    auto *lines_widget = new QWidget(live_text_settings_menu);
    auto *lines_layout = new QHBoxLayout(lines_widget);
    lines_layout->setContentsMargins(8, 4, 8, 4);
    lines_layout->addWidget(new QLabel(fxm_tr("OBSTitles.LiveTextLinesPerRow"), lines_widget));
    spin_live_text_lines_per_row_ = new QSpinBox(lines_widget);
    spin_live_text_lines_per_row_->setRange(kMinLiveTextLinesPerRow, kMaxLiveTextLinesPerRow);
    spin_live_text_lines_per_row_->setValue(live_text_lines_per_row_);
    lines_layout->addWidget(spin_live_text_lines_per_row_);
    auto *lines_action = new QWidgetAction(live_text_settings_menu);
    lines_action->setDefaultWidget(lines_widget);
    live_text_settings_menu->addAction(lines_action);
    live_text_settings_menu->addSeparator();
    act_select_row_before_cue_ = live_text_settings_menu->addAction(
        fxm_tr("OBSTitles.SelectRowBeforeCue"));
    act_select_row_before_cue_->setCheckable(true);
    act_select_row_before_cue_->setToolTip(
        fxm_tr("OBSTitles.SelectRowBeforeCueTooltip"));
    act_show_cue_preview_ = live_text_settings_menu->addAction(
        fxm_tr("OBSTitles.ShowCuePreview"));
    act_show_cue_preview_->setCheckable(true);
    act_show_cue_preview_->setToolTip(
        fxm_tr("OBSTitles.ShowCuePreviewTooltip"));
    act_live_text_columns_window_ = live_text_settings_menu->addAction(
        obs_icon("list-view.svg"),
        fxm_tr("OBSTitles.OpenLiveTextColumnsWindow"));
    act_live_text_columns_window_->setCheckable(true);
    act_live_text_columns_window_->setToolTip(
        fxm_tr("OBSTitles.OpenLiveTextColumnsWindowTooltip"));
    auto *external_data_menu = new QMenu(btn_external_refresh_);
    act_external_data_source_ = external_data_menu->addAction(fxm_tr("OBSTitles.EnableExternalDataSource"),
                                                              this, &TitleDock::on_toggle_external_data_source);
    act_external_data_settings_ = external_data_menu->addAction(fxm_tr("OBSTitles.ExternalDataSourceSettings"),
                                                                this, &TitleDock::on_show_external_data_settings);
    btn_external_refresh_->setMenu(external_data_menu);
    auto *playlist_menu = new QMenu(btn_playlist_);
    act_playlist_loop_ = playlist_menu->addAction(fxm_tr("OBSTitles.PlaylistLoop"));
    act_playlist_loop_->setCheckable(true);
    act_playlist_reverse_ = playlist_menu->addAction(fxm_tr("OBSTitles.PlaylistReverseOrder"));
    act_playlist_reverse_->setCheckable(true);
    act_playlist_restart_on_active_ = playlist_menu->addAction(fxm_tr("OBSTitles.PlaylistRestartOnSourceActive"));
    act_playlist_restart_on_active_->setCheckable(true);
    act_playlist_stop_on_inactive_ = playlist_menu->addAction(fxm_tr("OBSTitles.PlaylistStopOnSourceInactive"));
    act_playlist_stop_on_inactive_->setCheckable(true);
    playlist_menu->addSeparator();
    auto *hold_widget = new QWidget(playlist_menu);
    auto *hold_layout = new QHBoxLayout(hold_widget);
    hold_layout->setContentsMargins(8, 4, 8, 4);
    hold_layout->addWidget(new QLabel(fxm_tr("OBSTitles.PlaylistHoldSeconds"), hold_widget));
    auto *hold_spin = new TimecodeSpinBox(hold_widget);
    hold_spin->setRange(0.0, 3600.0);
    hold_spin->setValue(playlist_hold_seconds_);
    hold_layout->addWidget(hold_spin);
    auto *hold_action = new QWidgetAction(playlist_menu);
    hold_action->setDefaultWidget(hold_widget);
    act_playlist_hold_ = hold_action;
    playlist_menu->addAction(hold_action);
    btn_playlist_->setMenu(playlist_menu);
    btn_live_text_settings_->setMenu(live_text_settings_menu);
    btn_live_text_settings_->setPopupMode(QToolButton::InstantPopup);
    btn_live_text_settings_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{image:none;width:0px;}"));

    connect(btn_row_down_, &QToolButton::clicked, this, &TitleDock::on_move_live_text_row_down);
    connect(btn_external_refresh_, &QToolButton::clicked, this, &TitleDock::on_refresh_external_data);
    connect(btn_playlist_, &QToolButton::toggled, this, &TitleDock::on_toggle_playlist);
    connect(btn_persistence_settings_, &QToolButton::clicked, this, [this]() {
        const auto title = TitleDataStore::instance().get_title(selected_id());
        if (!title || exposed_text_layers(title).empty()) {
            update_persistence_controls();
            return;
        }
        const bool enable_full_persistence = !(background_persistence_ || text_persistence_);
        background_persistence_ = enable_full_persistence;
        text_persistence_ = enable_full_persistence;
        if (act_background_persistence_) {
            QSignalBlocker block(act_background_persistence_);
            act_background_persistence_->setChecked(enable_full_persistence);
        }
        if (act_text_persistence_) {
            QSignalBlocker block(act_text_persistence_);
            act_text_persistence_->setChecked(enable_full_persistence);
        }
        save_dock_settings();
        apply_persistence_settings_to_title(title);
        CacheManager::instance().refreshLiveCueStructure(title);
        update_persistence_controls();
        populate_exposed_text();
    });
    connect(act_playlist_loop_, &QAction::toggled, this, [this](bool checked) {
        playlist_loop_ = checked;
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            title->playlist_loop = checked;
            TitleDataStore::instance().save_async();
        }
        save_dock_settings();
    });
    connect(act_playlist_reverse_, &QAction::toggled, this, [this](bool checked) {
        playlist_reverse_ = checked;
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            title->playlist_reverse = checked;
            TitleDataStore::instance().save_async();
        }
        save_dock_settings();
    });
    connect(act_playlist_restart_on_active_, &QAction::toggled, this, [this](bool checked) {
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            title->playlist_restart_on_source_active = checked;
            TitleDataStore::instance().save_async();
        }
    });
    connect(act_playlist_stop_on_inactive_, &QAction::toggled, this, [this](bool checked) {
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            title->playlist_stop_on_source_inactive = checked;
            TitleDataStore::instance().save_async();
        }
    });
    connect(hold_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        playlist_hold_seconds_ = value;
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            title->playlist_hold_seconds = value;
            TitleDataStore::instance().save_async();
        }
        save_dock_settings();
    });
    connect(spin_live_text_lines_per_row_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &TitleDock::on_live_text_lines_per_row_changed);
    connect(act_live_text_columns_window_, &QAction::triggered,
            this, &TitleDock::open_live_text_columns_window);
    connect(act_select_row_before_cue_, &QAction::toggled,
            this, [this](bool enabled) {
                select_row_before_cue_ = enabled;
                if (act_show_cue_preview_)
                    act_show_cue_preview_->setEnabled(enabled);
                if (!enabled)
                    leavePreview();
                save_dock_settings();
            });
    connect(act_show_cue_preview_, &QAction::toggled,
            this, [this](bool enabled) {
                show_cue_preview_ = enabled;
                sync_live_cue_preview_output_route();
                save_dock_settings();
            });
    connect(act_background_persistence_, &QAction::toggled, this, [this](bool checked) {
        background_persistence_ = checked;
        if (!background_persistence_) {
            text_persistence_ = false;
            if (act_text_persistence_) {
                QSignalBlocker block(act_text_persistence_);
                act_text_persistence_->setChecked(false);
            }
        }
        save_dock_settings();
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            apply_persistence_settings_to_title(title);
            CacheManager::instance().refreshLiveCueStructure(title);
        }
        update_persistence_controls();
        populate_exposed_text();
    });
    connect(act_text_persistence_, &QAction::toggled, this, [this](bool checked) {
        text_persistence_ = background_persistence_ && checked;
        if (act_text_persistence_ && act_text_persistence_->isChecked() != text_persistence_) {
            QSignalBlocker block(act_text_persistence_);
            act_text_persistence_->setChecked(text_persistence_);
        }
        save_dock_settings();
        if (auto title = TitleDataStore::instance().get_title(selected_id())) {
            apply_persistence_settings_to_title(title);
            CacheManager::instance().refreshLiveCueStructure(title);
        }
        update_persistence_controls();
        populate_exposed_text();
    });
    connect(text_table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item && item->column() == 0)
            update_live_text_select_all_state();
    });
    connect(sections_, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!titles_collapsed_ && sections_) {
            const QList<int> sizes = sections_->sizes();
            if (!sizes.isEmpty() &&
                sizes.front() > titles_compact_rail_height(titles_compact_rail_) +
                                    obs_layout_spacing(sections_))
                titles_expanded_splitter_size_ = sizes.front();
        }
        save_dock_settings();
    });
    connect(text_table_->horizontalHeader(), &QHeaderView::sectionMoved,
            this, [this](int, int, int) { save_live_text_header_state(); });
    connect(text_table_->horizontalHeader(), &QHeaderView::sectionResized,
            this, [this](int, int, int) { save_live_text_header_state(); });
    connect(list_, &QListWidget::itemSelectionChanged,
            this, &TitleDock::on_selection_changed);
    connect(list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem *) { on_edit(); });

    playlist_timer_ = new QTimer(this);
    playlist_timer_->setInterval(250);
    connect(playlist_timer_, &QTimer::timeout, this, &TitleDock::on_playlist_tick);

    load_dock_settings();
    if (btn_visibility_filter_) btn_visibility_filter_->setChecked(visibility_filter_active_);
    if (act_playlist_loop_) act_playlist_loop_->setChecked(playlist_loop_);
    if (act_playlist_reverse_) act_playlist_reverse_->setChecked(playlist_reverse_);
    if (act_background_persistence_) act_background_persistence_->setChecked(background_persistence_);
    if (act_text_persistence_) act_text_persistence_->setChecked(text_persistence_);
    if (act_select_row_before_cue_) {
        QSignalBlocker block(act_select_row_before_cue_);
        act_select_row_before_cue_->setChecked(select_row_before_cue_);
    }
    if (act_show_cue_preview_) {
        QSignalBlocker block(act_show_cue_preview_);
        act_show_cue_preview_->setChecked(show_cue_preview_);
        act_show_cue_preview_->setEnabled(select_row_before_cue_);
    }
    hold_spin->setValue(playlist_hold_seconds_);
    if (spin_live_text_lines_per_row_) {
        QSignalBlocker block(spin_live_text_lines_per_row_);
        spin_live_text_lines_per_row_->setValue(live_text_lines_per_row_);
    }
    update_template_view_mode();
    update_playlist_controls();
    update_persistence_controls();
    update_playlist_countdown_label();
    on_selection_changed();
}

/* ══════════════════════════════════════════════════════════════════
 *  List population
 * ══════════════════════════════════════════════════════════════════ */
void TitleDock::update_template_view_mode()
{
    if (!list_) return;

    if (template_icon_view_) {
        list_->setViewMode(QListView::IconMode);
        list_->setIconSize(QSize(kTitleIconViewThumbWidth, kTitleIconViewThumbHeight));
        list_->setGridSize(QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight));
        list_->setResizeMode(QListView::Adjust);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        list_->setMovement(QListView::Static);
        list_->setSpacing(6);
        list_->setUniformItemSizes(true);
        list_->setWordWrap(true);
        list_->setTextElideMode(Qt::ElideRight);
        if (btn_view_) {
            btn_view_->setText(fxm_tr("OBSTitles.ListView"));
            btn_view_->setAccessibleName(fxm_tr("OBSTitles.ListView"));
            btn_view_->setToolTip(fxm_tr("OBSTitles.ListViewTooltip"));
            btn_view_->setIcon(obs_icon("list-view.svg"));
        }
    } else {
        list_->setViewMode(QListView::ListMode);
        list_->setIconSize(QSize(kTitleListIconExtent * 3 + 6, kTitleListIconExtent));
        list_->setGridSize(QSize());
        list_->setResizeMode(QListView::Adjust);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list_->setMovement(QListView::Static);
        list_->setSpacing(0);
        list_->setUniformItemSizes(true);
        list_->setWordWrap(false);
        list_->setTextElideMode(Qt::ElideRight);
        if (btn_view_) {
            btn_view_->setText(fxm_tr("OBSTitles.IconView"));
            btn_view_->setAccessibleName(fxm_tr("OBSTitles.IconView"));
            btn_view_->setToolTip(fxm_tr("OBSTitles.IconViewTooltip"));
            btn_view_->setIcon(obs_icon("icon-view.svg"));
        }
    }
}

void TitleDock::on_toggle_template_view()
{
    template_icon_view_ = !template_icon_view_;
    update_template_view_mode();
    populate_list();
    save_dock_settings();
}

void TitleDock::on_toggle_visibility_filter(bool enabled)
{
    visibility_filter_active_ = enabled;
    populate_list();
    save_dock_settings();
}

static bool title_has_bound_host_source(const std::string &title_id)
{
    if (title_id.empty())
        return false;
    const auto *host = fxm::editor_host::editor_host();
    if (!host)
        return false;
    const auto sources = host->title_sources();
    return std::any_of(sources.begin(), sources.end(),
                       [&title_id](const auto &source) {
                           return source.title_id == title_id;
                       });
}

QSet<QString> TitleDock::active_title_source_ids() const
{
    QSet<QString> ids;

    const auto *host = fxm::editor_host::editor_host();
    if (!host)
        return ids;
    for (const auto &source : host->title_sources()) {
        if (!source.active && !source.showing)
            continue;
        const QString id = QString::fromStdString(source.title_id).trimmed();
        if (!id.isEmpty())
            ids.insert(id);
    }

    return ids;
}

bool TitleDock::should_show_title(const std::shared_ptr<Title> &title, const QSet<QString> &active_ids) const
{
    if (!title || title->is_asset) return false;
    if (!visibility_filter_active_) return true;
    return active_ids.contains(QString::fromStdString(title->id));
}

void TitleDock::populate_list()
{
    QString prev_id = QString::fromStdString(selected_id());
    if (prev_id.isEmpty())
        prev_id = last_selected_title_id_;
    list_->blockSignals(true);
    list_->clear();

    const QSet<QString> active_ids = visibility_filter_active_ ? active_title_source_ids() : QSet<QString>();

    for (auto &t : TitleDataStore::instance().titles()) {
        if (!should_show_title(t, active_ids))
            continue;
        const QString title_id = QString::fromStdString(t->id);
        auto exposed = exposed_text_layers(t);
        normalize_live_text_rows(t, exposed);
        CacheManager::instance().restoreDiskStates(t);

        auto *item = new QListWidgetItem(QString::fromStdString(t->name));
        const bool has_exposed_text = !exposed.empty();
        const bool has_scene_mask = title_has_scene_mask(t);
        const LiveCueAggregateCacheStatus cache_status = aggregate_live_cue_cache_status(t);
        if (template_icon_view_) {
            item->setIcon(title_icon_view_icon(*t, has_exposed_text, has_scene_mask, cache_status, t->playlist_active, CacheManager::instance().cacheEnabled()));
            item->setSizeHint(QSize(kTitleIconViewItemWidth, kTitleIconViewItemHeight));
            item->setTextAlignment(Qt::AlignHCenter);
        } else {
            item->setIcon(title_list_combined_status_icon(*t, has_exposed_text, has_scene_mask, cache_status, CacheManager::instance().cacheEnabled()));
            item->setSizeHint(QSize());
            item->setTextAlignment(Qt::AlignVCenter | Qt::AlignLeft);
        }
        item->setData(Qt::UserRole, title_id);
        item->setData(kTitleListThumbnailRole, title_preview_pixmap(*t, QSize(96, 54)));
        item->setData(kTitleShowStatusIconRole, true);
        item->setData(kTitlePlaylistActiveRole, t->playlist_active);
        const bool waiting_for_prerender = cache_waiting_title_id_ == title_id &&
            cache_waiting_cue_row_ >= 0;
        item->setData(kTitleCueVisualStateRole,
                      static_cast<int>(title_cue_visual_state(*t, waiting_for_prerender)));
        // Layer count hint as tooltip
        item->setToolTip(QStringLiteral("%1\n%2")
            .arg(fxm_tr("OBSTitles.LayerCountTooltipFormat").arg(t->layers.size()).arg(t->duration),
                 aggregate_live_cue_cache_tooltip(cache_status)));
        list_->addItem(item);
    }

    /* Restore selection, or select the first title when the dock/list loads. */
    bool restored_selection = false;
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString() == prev_id) {
            list_->setCurrentRow(i);
            restored_selection = true;
            break;
        }
    }
    if (!restored_selection && list_->count() > 0)
        list_->setCurrentRow(0);

    list_->blockSignals(false);
    on_selection_changed();
}

void TitleDock::refresh()
{
    populate_list();
    populate_exposed_text();
}

/* ══════════════════════════════════════════════════════════════════
 *  Selection helper
 * ══════════════════════════════════════════════════════════════════ */
std::string TitleDock::selected_id() const
{
    auto *item = list_->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString().toStdString();
}

std::vector<std::string> TitleDock::selected_title_ids() const
{
    std::vector<std::string> ids;
    if (!list_) return ids;

    const auto items = list_->selectedItems();
    ids.reserve((size_t)items.size());
    for (const auto *item : items) {
        if (!item) continue;
        const std::string id = item->data(Qt::UserRole).toString().toStdString();
        if (!id.empty())
            ids.push_back(id);
    }

    return ids;
}

void TitleDock::on_selection_changed()
{
    const auto ids = selected_title_ids();
    const int selected_count = (int)ids.size();
    const bool has = selected_count > 0;
    const bool single = selected_count == 1;
    const QString current_title_id = QString::fromStdString(selected_id());
    if (!current_title_id.isEmpty()) {
        last_selected_title_id_ = current_title_id;
        save_dock_settings();
    }
    FXM_LOG_DEBUG("Dock", QStringLiteral(
        "Title selection changed count=%1 current=%2")
        .arg(selected_count)
        .arg(current_title_id.isEmpty() ? QStringLiteral("<none>")
                                        : current_title_id));

    btn_dup_->setEnabled(single);
    btn_rename_->setEnabled(single);
    btn_del_->setEnabled(has);
    btn_export_->setEnabled(single);
    btn_edit_->setEnabled(single);
    btn_scene_->setEnabled(single);

    if (single) {
        auto t = TitleDataStore::instance().get_title(ids.front());
        if (t)
            status_lbl_->setText(
                fxm_tr("OBSTitles.StatusLayerCountFormat")
                    .arg(t->layers.size())
                    .arg(t->duration, 0, 'f', 1));
    } else if (has) {
        status_lbl_->setText(fxm_tr("OBSTitles.SelectedTitlesStatusFormat").arg(selected_count));
    } else {
        status_lbl_->setText(list_->count() == 0
            ? fxm_tr("OBSTitles.UseAddHint")
            : fxm_tr("OBSTitles.NoTitleSelected"));
    }
    populate_exposed_text();
    update_titles_compact_rail();
}

void TitleDock::save_live_text_header_state()
{
    if (!text_table_ || text_table_->columnCount() <= 0) return;
    const QByteArray state = text_table_->horizontalHeader()->saveState();
    live_text_header_states_[text_table_->columnCount()] = state;

    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;
    const QByteArray encoded = state.toBase64();
    title->live_text_header_state.assign(encoded.constData(), (size_t)encoded.size());
    TitleDataStore::instance().save();
}

bool TitleDock::restore_live_text_header_state()
{
    if (!text_table_ || text_table_->columnCount() <= 0) return false;

    bool restored = false;
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (title && !title->live_text_header_state.empty()) {
        const QByteArray encoded(title->live_text_header_state.data(), (int)title->live_text_header_state.size());
        const QByteArray state = QByteArray::fromBase64(encoded);
        if (!state.isEmpty() && text_table_->horizontalHeader()->restoreState(state)) {
            live_text_header_states_[text_table_->columnCount()] = state;
            restored = true;
        }
    }

    if (!restored) {
        auto it = live_text_header_states_.find(text_table_->columnCount());
        if (it != live_text_header_states_.end())
            restored = text_table_->horizontalHeader()->restoreState(it->second);
    }

    /* A persisted QHeaderView state also contains hidden-section flags. It
     * must never resurrect Cache Status while the global cache is disabled. */
    enforce_live_text_cache_column_visibility();
    return restored;
}

bool TitleDock::has_checked_live_text_rows() const
{
    if (!text_table_) return false;
    for (int row = 0; row < text_table_->rowCount(); ++row) {
        auto *item = text_table_->item(row, kLiveSelectColumn);
        if (item && item->checkState() == Qt::Checked)
            return true;
    }
    return false;
}

void TitleDock::apply_live_text_row_selection(const std::vector<int> &rows, bool checked)
{
    if (!text_table_) return;

    QSignalBlocker block(text_table_);
    text_table_->clearSelection();
    for (int row = 0; row < text_table_->rowCount(); ++row) {
        auto *item = text_table_->item(row, kLiveSelectColumn);
        if (item)
            item->setCheckState(Qt::Unchecked);
    }

    auto *selection_model = text_table_->selectionModel();
    for (int row : rows) {
        if (row < 0 || row >= text_table_->rowCount()) continue;
        if (checked) {
            auto *item = text_table_->item(row, kLiveSelectColumn);
            if (item)
                item->setCheckState(Qt::Checked);
        }
        if (selection_model && text_table_->columnCount() > 0) {
            const QModelIndex left = text_table_->model()->index(row, 0);
            const QModelIndex right = text_table_->model()->index(row, text_table_->columnCount() - 1);
            selection_model->select(QItemSelection(left, right),
                                    QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
    if (!rows.empty() && selection_model)
        selection_model->setCurrentIndex(text_table_->model()->index(rows.front(), kLiveSelectColumn), QItemSelectionModel::NoUpdate);
    update_live_text_select_all_state();
}

void TitleDock::set_all_live_text_rows_checked(bool checked)
{
    if (!text_table_) return;

    QSignalBlocker block(text_table_);
    for (int row = 0; row < text_table_->rowCount(); ++row) {
        auto *item = text_table_->item(row, kLiveSelectColumn);
        if (item)
            item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
    update_live_text_select_all_state();
}

void TitleDock::update_live_text_select_all_state()
{
    auto *header = live_text_cue_header(text_table_);
    if (!header || !text_table_) return;

    const int row_count = text_table_->rowCount();
    bool all_checked = row_count > 0;
    for (int row = 0; row < row_count; ++row) {
        auto *item = text_table_->item(row, kLiveSelectColumn);
        if (!item || item->checkState() != Qt::Checked) {
            all_checked = false;
            break;
        }
    }
    header->set_select_all_checked(all_checked);
}

std::vector<int> TitleDock::selected_live_text_rows() const
{
    std::vector<int> rows;
    if (!text_table_) return rows;

    for (int row = 0; row < text_table_->rowCount(); ++row) {
        auto *item = text_table_->item(row, kLiveSelectColumn);
        if (item && item->checkState() == Qt::Checked)
            rows.push_back(row);
    }

    if (rows.empty()) {
        if (auto *selection = text_table_->selectionModel()) {
            for (const QModelIndex &index : selection->selectedRows()) {
                const int row = index.row();
                if (std::find(rows.begin(), rows.end(), row) == rows.end())
                    rows.push_back(row);
            }
        }
    }

    if (rows.empty()) {
        for (const auto *item : text_table_->selectedItems()) {
            if (!item) continue;
            int row = item->row();
            if (std::find(rows.begin(), rows.end(), row) == rows.end())
                rows.push_back(row);
        }
    }

    std::sort(rows.begin(), rows.end());
    return rows;
}

void TitleDock::commit_live_text_cell_edit(const std::shared_ptr<Title> &title,
                                           int row, int col, const QString &text)
{
    if (!title || !text_table_) return;
    if (row < 0 || row >= (int)title->live_text_rows.size()) return;
    if (col < 0 || col >= (int)title->live_text_rows[row].size()) return;

    auto exposed = exposed_text_layers(title);
    const std::string edited_layer_id =
        col >= 0 && col < static_cast<int>(exposed.size()) && exposed[col]
            ? exposed[col]->id : std::string{};
    if (!edited_layer_id.empty() &&
        live_text_cue_cell_state(*title, live_text_row_id(*title, row),
                                 edited_layer_id) ==
            LiveTextCueCellState::ExternalTableManaged)
        return;

    const bool single_value_column = col >= 0 && col < (int)exposed.size() &&
        exposed[col] && exposed[col]->exposed_single_value;
    std::vector<int> target_rows = selected_live_text_rows();
    if (single_value_column) {
        target_rows.clear();
        target_rows.reserve(title->live_text_rows.size());
        for (int i = 0; i < (int)title->live_text_rows.size(); ++i)
            target_rows.push_back(i);
    } else if (target_rows.size() <= 1 ||
        std::find(target_rows.begin(), target_rows.end(), row) == target_rows.end()) {
        target_rows = {row};
    }

    const std::string new_value = text.toStdString();
    bool changed = false;
    for (int target_row : target_rows) {
        if (target_row < 0 || target_row >= (int)title->live_text_rows.size())
            continue;
        auto &live_row = title->live_text_rows[target_row];
        if (col < 0 || col >= (int)live_row.size())
            continue;
        if (!edited_layer_id.empty() &&
            live_text_cue_cell_state(*title, live_text_row_id(*title, target_row),
                                     edited_layer_id) ==
                LiveTextCueCellState::ExternalTableManaged)
            continue;
        if (live_row[col] == new_value)
            continue;

        live_row[col] = new_value;
        changed = true;
    }
    if (!changed) return;

    updating_exposed_text_ = true;
    for (int target_row : target_rows) {
        if (target_row < 0 || target_row >= text_table_->rowCount())
            continue;
        QWidget *widget = text_table_->cellWidget(target_row, col + kLiveFirstValueColumn);
        if (auto *edit = dynamic_cast<LiveTextCueField *>(widget)) {
            if (edit->toPlainText() == text)
                continue;
            QSignalBlocker block(edit);
            edit->setPlainText(text);
            edit->update_tooltip(text);
        } else if (auto *image = dynamic_cast<LiveImageCueField *>(widget)) {
            if (image->path() == text)
                continue;
            image->set_path(text);
        }
    }

    TitleDataStore::instance().save();
    TitleDataStore::instance().touch_runtime_change();
    /* A row can also be the previous/outgoing side of another row's persistence
     * transition, so refresh the pairwise requirement sets once. Stable keys
     * ensure that only variants affected by the edited text are regenerated. */
    CacheManager::instance().refreshLiveCueStructureAsync(title);
    seen_store_revision_ = TitleDataStore::instance().revision();
    updating_exposed_text_ = false;
}

void TitleDock::set_live_text_row_render_paused(const std::shared_ptr<Title> &title, int row, bool paused)
{
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return;
    ensure_live_text_row_ids(*title);
    const QString row_key = QStringLiteral("%1:%2")
        .arg(QString::fromStdString(title->id),
             QString::fromStdString(live_text_row_id(*title, row)));
    if (row_key.endsWith(QStringLiteral(":")))
        return;

    if (paused) {
        int &count = focused_live_text_row_render_counts_[row_key];
        ++count;
        if (count == 1)
            CacheManager::instance().setLiveCueRowRenderPaused(title, row, true);
        return;
    }

    auto it = focused_live_text_row_render_counts_.find(row_key);
    if (it == focused_live_text_row_render_counts_.end())
        return;
    it.value() = std::max(0, it.value() - 1);
    if (it.value() > 0)
        return;
    focused_live_text_row_render_counts_.erase(it);
    CacheManager::instance().setLiveCueRowRenderPaused(title, row, false);
}

void TitleDock::apply_persistence_settings_to_title(const std::shared_ptr<Title> &title)
{
    if (!title) return;
    auto exposed = exposed_text_layers(title);
    const bool has_exposed = !exposed.empty();
    title->cue_background_persistence = background_persistence_ && has_exposed;
    title->cue_text_persistence = title->cue_background_persistence && text_persistence_;
    if (!title->cue_background_persistence)
        title->cue_persistence_transition = false;
    if (!title->cue_text_persistence)
        title->cue_persistent_text_columns.clear();
}

void TitleDock::update_persistence_controls()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    const bool has_exposed = title && !exposed_text_layers(title).empty();
    if (btn_persistence_settings_) {
        btn_persistence_settings_->setEnabled(has_exposed);
        QSignalBlocker block(btn_persistence_settings_);
        const bool background_only = has_exposed && background_persistence_ && !text_persistence_;
        const bool full_persistence = has_exposed && background_persistence_ && text_persistence_;
        btn_persistence_settings_->setProperty("persistenceState",
                                               background_only ? QStringLiteral("background") : QString());
        btn_persistence_settings_->style()->unpolish(btn_persistence_settings_);
        btn_persistence_settings_->style()->polish(btn_persistence_settings_);
        btn_persistence_settings_->setChecked(full_persistence);
    }
    if (act_background_persistence_)
        act_background_persistence_->setEnabled(has_exposed);
    if (act_text_persistence_) {
        act_text_persistence_->setEnabled(has_exposed && background_persistence_);
        if (!background_persistence_ && act_text_persistence_->isChecked()) {
            QSignalBlocker block(act_text_persistence_);
            act_text_persistence_->setChecked(false);
        }
    }
    if (title)
        apply_persistence_settings_to_title(title);
}

int TitleDock::live_text_row_height() const
{
    const QWidget *font_source = text_table_ ? text_table_->viewport() : nullptr;
    const QFontMetrics metrics(font_source ? font_source->font() : font());
    const int line_count = std::clamp(live_text_lines_per_row_,
                                      kMinLiveTextLinesPerRow,
                                      kMaxLiveTextLinesPerRow);
    const int text_height = metrics.lineSpacing() * line_count;
    const int padding = 2 * obs_layout_spacing(text_table_);
    return std::max(obs_tool_button_extent(text_table_), text_height + padding);
}

void TitleDock::apply_live_text_row_heights()
{
    if (!text_table_) return;

    const int height = live_text_row_height();
    text_table_->verticalHeader()->setDefaultSectionSize(height);
    for (int row = 0; row < text_table_->rowCount(); ++row)
        text_table_->setRowHeight(row, height);
}

void TitleDock::on_live_text_lines_per_row_changed(int lines)
{
    const int clamped_lines = std::clamp(lines, kMinLiveTextLinesPerRow, kMaxLiveTextLinesPerRow);
    if (spin_live_text_lines_per_row_ && spin_live_text_lines_per_row_->value() != clamped_lines) {
        QSignalBlocker block(spin_live_text_lines_per_row_);
        spin_live_text_lines_per_row_->setValue(clamped_lines);
    }

    if (live_text_lines_per_row_ == clamped_lines)
        return;

    live_text_lines_per_row_ = clamped_lines;
    apply_live_text_row_heights();
    save_dock_settings();
}

void TitleDock::update_external_data_controls()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    const bool has_title = (bool)title;
    const bool has_exposed = title && !exposed_text_layers(title).empty();
    const bool has_chart_consumer = title && std::any_of(
        title->layers.begin(), title->layers.end(),
        [](const std::shared_ptr<Layer> &layer) {
            return layer && layer->type == LayerType::Chart &&
                   layer->chart_data_mode == ChartDataMode::ExternalData;
        });
    const bool has_data_consumer = has_exposed || has_chart_consumer;
    const bool external_enabled = title && title->external_data_enabled;

    if (btn_data_sources_)
        btn_data_sources_->setEnabled(has_title && has_data_consumer);
    if (act_live_text_columns_window_)
        act_live_text_columns_window_->setEnabled(has_title && has_exposed);
    if (btn_live_text_settings_)
        btn_live_text_settings_->setEnabled(has_title);
    if (btn_external_refresh_) {
        btn_external_refresh_->setEnabled(has_title && has_data_consumer);
        btn_external_refresh_->setIcon(globe_status_icon(external_enabled));
        QSignalBlocker block(btn_external_refresh_);
        btn_external_refresh_->setChecked(external_enabled);
        btn_external_refresh_->setToolTip(external_enabled
            ? fxm_tr("OBSTitles.RefreshExternalDataEnabledTooltip")
            : fxm_tr("OBSTitles.RefreshExternalDataTooltip"));
    }

    if (btn_data_sources_ && btn_data_sources_->menu()) {
        auto actions = btn_data_sources_->menu()->actions();
        for (QAction *action : actions)
            action->setEnabled(has_title && has_data_consumer);
    }
    if (act_external_data_source_) {
        act_external_data_source_->setEnabled(has_title && has_data_consumer);
        act_external_data_source_->setText(external_enabled
            ? fxm_tr("OBSTitles.DisableExternalDataSource")
            : fxm_tr("OBSTitles.EnableExternalDataSource"));
    }
    if (act_external_data_settings_) {
        act_external_data_settings_->setEnabled(has_title && has_data_consumer);
    }
}

void TitleDock::enforce_live_text_cache_column_visibility()
{
    if (!text_table_ || text_table_->columnCount() <= kLiveCacheColumn)
        return;
    text_table_->setColumnHidden(
        kLiveCacheColumn, !CacheManager::instance().cacheEnabled());
}

void TitleDock::update_live_text_columns_window_title()
{
    if (!live_text_columns_dialog_)
        return;
    auto title = TitleDataStore::instance().get_title(selected_id());
    live_text_columns_dialog_->setWindowTitle(title
        ? fxm_tr("OBSTitles.LiveTextColumnsWindowFormat")
              .arg(QString::fromStdString(title->name))
        : fxm_tr("OBSTitles.LiveTextColumnsWindow"));
}

void TitleDock::open_live_text_columns_window()
{
    if (!text_table_ || !live_text_section_ || !live_text_section_layout_ ||
        !live_text_header_widget_ || !live_text_toolbar_ ||
        !live_text_table_host_layout_ || !live_text_table_placeholder_ ||
        !live_cue_preview_panel_ || !sections_)
        return;

    if (live_text_columns_dialog_) {
        if (act_live_text_columns_window_) {
            QSignalBlocker block(act_live_text_columns_window_);
            act_live_text_columns_window_->setChecked(true);
        }
        live_text_columns_dialog_->show();
        live_text_columns_dialog_->raise();
        live_text_columns_dialog_->activateWindow();
        return;
    }

    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title || exposed_text_layers(title).empty()) {
        if (act_live_text_columns_window_) {
            QSignalBlocker block(act_live_text_columns_window_);
            act_live_text_columns_window_->setChecked(false);
        }
        return;
    }

    auto *host = fxm::editor_host::editor_host();
    QWidget *main_window = static_cast<QWidget *>(
        host ? host->main_window_handle() : nullptr);
    auto *dialog = new QDialog(main_window, Qt::Window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    dialog->resize(std::max(900, width() * 2), std::max(520, height()));
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    live_text_columns_dialog_ = dialog;
    update_live_text_columns_window_title();

    live_text_section_layout_->removeWidget(live_text_header_widget_);
    live_text_header_widget_->setParent(dialog);
    layout->addWidget(live_text_header_widget_);

    live_text_table_host_layout_->removeWidget(text_table_);
    text_table_->setParent(dialog);
    layout->addWidget(text_table_, 1);
    text_table_->show();

    live_text_section_layout_->removeWidget(live_text_toolbar_);
    live_text_toolbar_->setParent(dialog);
    layout->addWidget(live_text_toolbar_);

    /* Move the one authoritative read-only Preview with the Live Text Cues
     * controls. Reparenting preserves its CanvasPreview/source state, so the
     * dock and external window can never drift or render duplicate previews. */
    live_cue_preview_panel_->setParent(dialog);
    layout->addWidget(live_cue_preview_panel_, 1);
    live_text_header_widget_->show();
    live_text_toolbar_->show();
    live_text_table_placeholder_->show();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    connect(dialog, &QDialog::finished, this, [this, dialog](int) {
        if (live_text_columns_dialog_ != dialog)
            return;
        restore_live_text_table_to_dock();
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog]() {
        if (live_text_columns_dialog_ == dialog)
            live_text_columns_dialog_ = nullptr;
    });

    dialog->show();
    sync_live_cue_preview_output_route();
    dialog->raise();
    dialog->activateWindow();
    adjust_live_text_table_columns(true);
}

void TitleDock::restore_live_text_table_to_dock()
{
    QDialog *dialog = live_text_columns_dialog_;
    if (!dialog)
        return;

    live_text_columns_dialog_ = nullptr;
    disconnect(dialog, nullptr, this, nullptr);
    if (dialog->layout()) {
        dialog->layout()->removeWidget(live_text_header_widget_);
        dialog->layout()->removeWidget(text_table_);
        dialog->layout()->removeWidget(live_text_toolbar_);
        dialog->layout()->removeWidget(live_cue_preview_panel_);
    }
    live_text_header_widget_->setParent(live_text_section_);
    live_text_section_layout_->insertWidget(0, live_text_header_widget_);
    text_table_->setParent(live_text_table_host_);
    live_text_table_host_layout_->insertWidget(0, text_table_, 1);
    live_text_toolbar_->setParent(live_text_section_);
    live_text_section_layout_->insertWidget(2, live_text_toolbar_);
    live_cue_preview_panel_->setParent(sections_);
    sections_->insertWidget(2, live_cue_preview_panel_);
    sections_->setStretchFactor(2, 1);
    live_text_header_widget_->show();
    text_table_->show();
    live_text_toolbar_->show();
    live_text_table_placeholder_->hide();
    if (act_live_text_columns_window_) {
        QSignalBlocker block(act_live_text_columns_window_);
        act_live_text_columns_window_->setChecked(false);
    }
    dialog->deleteLater();
    sync_live_cue_preview_output_route();
    enforce_live_text_cache_column_visibility();
    adjust_live_text_table_columns(true);
}

namespace {

double deterministic_live_cue_preview_time(const Title &title)
{
    if (title.playback_mode == 2)
        return std::clamp(title.pause_time, 0.0, title.duration);
    if (title.playback_mode == 1 && title.loop_end > title.loop_start)
        return std::clamp(title.loop_start, 0.0, title.duration);
    return 0.0;
}

QString deterministic_live_cue_preview_position_label(const Title &title)
{
    if (title.playback_mode == 2)
        return fxm_tr("OBSTitles.PreviewPauseFrame");
    if (title.playback_mode == 1 && title.loop_end > title.loop_start)
        return fxm_tr("OBSTitles.PreviewLoopStartFrame");
    return fxm_tr("OBSTitles.PreviewTimelineStartFrame");
}

} // namespace

bool TitleDock::attach_live_cue_preview_to_host()
{
    if (!live_cue_preview_session_)
        return false;
    live_cue_preview_uses_host_ =
        live_cue_preview_session_->attach_to_host_preview();
    return live_cue_preview_uses_host_;
}

void TitleDock::detach_live_cue_preview_from_host()
{
    live_cue_preview_uses_host_ = false;
    if (live_cue_preview_session_)
        live_cue_preview_session_->detach_from_host_preview();
}

void TitleDock::sync_live_cue_preview_output_route()
{
    if (!isPreviewReady() || !show_cue_preview_) {
        detach_live_cue_preview_from_host();
        if (live_cue_preview_panel_)
            live_cue_preview_panel_->hide();
        return;
    }

    if (attach_live_cue_preview_to_host()) {
        if (live_cue_preview_panel_)
            live_cue_preview_panel_->hide();
        return;
    }

    detach_live_cue_preview_from_host();
    if (live_cue_preview_panel_)
        live_cue_preview_panel_->show();
    if (live_cue_preview_canvas_)
        live_cue_preview_canvas_->repaint();
}

bool TitleDock::enterPreview(const std::shared_ptr<Title> &title, int row)
{
    if (!title || !live_cue_preview_canvas_ || !live_cue_preview_panel_)
        return false;

    auto preview_title = clone_title_snapshot(title);
    if (!preview_title)
        return false;
    /* Isolate ticker/media runtime keys from the on-air title while retaining
     * the authored hierarchy and asset references in the deep snapshot. */
    preview_title->id = title->id + "::cue-preview";

    auto exposed = exposed_text_layers(preview_title);
    normalize_live_text_rows(preview_title, exposed);
    if (exposed.empty()) {
        row = 0;
    } else if (row < 0 || row >= static_cast<int>(preview_title->live_text_rows.size())) {
        return false;
    }

    for (int col = 0; col < static_cast<int>(exposed.size()); ++col) {
        auto &target = exposed[col];
        if (!target)
            continue;
        const int value_row = target->exposed_single_value ? 0 : row;
        if (value_row < 0 ||
            value_row >= static_cast<int>(preview_title->live_text_rows.size()) ||
            col >= static_cast<int>(preview_title->live_text_rows[value_row].size()))
            continue;
        apply_live_text_runtime_binding(*preview_title, value_row, *target);
        const std::string cue_value = effective_live_text_cue_value(
            *preview_title, value_row, *target,
            preview_title->live_text_rows[value_row][col]);
        apply_live_cue_layer_value(*preview_title, target, row, cue_value);
    }

    preview_title->current_cue_row = row;
    preview_title->pending_cue_row = -1;
    preview_title->cue_uncue_requested = false;
    preview_title->cue_persistence_transition = false;
    preview_title->cue_persistent_text_columns.clear();

    const QString previous_title_id = live_cue_preview_title_id_;
    const int previous_row = live_cue_preview_row_;
    live_cue_preview_title_ = preview_title;
    live_cue_preview_title_id_ = QString::fromStdString(title->id);
    live_cue_preview_row_ = row;
    live_cue_preview_time_ = deterministic_live_cue_preview_time(*preview_title);

    if (!live_cue_preview_session_) {
        if (auto *host = fxm::editor_host::editor_host())
            live_cue_preview_session_ =
                host->create_live_cue_preview_session();
    }
    if (live_cue_preview_session_)
        live_cue_preview_session_->enter(preview_title,
                                         live_cue_preview_time_);

    live_cue_preview_canvas_->set_playback_active(false);
    live_cue_preview_canvas_->set_title(preview_title, false, false);
    live_cue_preview_canvas_->set_playhead(live_cue_preview_time_, false);
    live_cue_preview_status_->setText(
        fxm_tr("OBSTitles.PreviewReadyFormat")
            .arg(row + 1)
            .arg(deterministic_live_cue_preview_position_label(*preview_title))
            .arg(live_cue_preview_time_, 0, 'f', 3));
    sync_live_cue_preview_output_route();

    /* Prime the exact cue/cache path while the operator inspects the frozen
     * frame. The CanvasPreview session retains GPU textures/shaders/glyphs and
     * the cache manager hydrates the Program-ready transition concurrently. */
    CacheManager::instance().prepareLiveCueForPlayback(title, row);
    CacheManager::instance().preloadLiveCues(title, row, 2);

    if (!previous_title_id.isEmpty()) {
        auto previous_title = TitleDataStore::instance().get_title(
            previous_title_id.toStdString());
        if (previous_title && previous_title_id == QString::fromStdString(selected_id()))
            update_live_text_runtime_status_fast(previous_title, previous_row);
    }
    if (live_cue_preview_title_id_ == QString::fromStdString(selected_id()))
        update_live_text_runtime_status_fast(title, row);
    return true;
}

void TitleDock::leavePreview()
{
    const QString previous_title_id = live_cue_preview_title_id_;
    const int previous_row = live_cue_preview_row_;
    detach_live_cue_preview_from_host();
    if (live_cue_preview_session_)
        live_cue_preview_session_->leave();
    live_cue_preview_title_.reset();
    live_cue_preview_title_id_.clear();
    live_cue_preview_row_ = -1;
    live_cue_preview_time_ = 0.0;
    if (live_cue_preview_canvas_) {
        live_cue_preview_canvas_->set_playback_active(false);
        live_cue_preview_canvas_->set_title(nullptr, false, false);
    }
    if (live_cue_preview_panel_)
        live_cue_preview_panel_->hide();

    auto previous_title = TitleDataStore::instance().get_title(
        previous_title_id.toStdString());
    if (previous_title && previous_title_id == QString::fromStdString(selected_id()))
        update_live_text_runtime_status_fast(previous_title, previous_row);
}

bool TitleDock::isPreviewReady() const
{
    if (!live_cue_preview_title_ || live_cue_preview_row_ < 0)
        return false;
    return !live_cue_preview_session_ || live_cue_preview_session_->ready();
}

void TitleDock::refresh_live_cue_preview()
{
    if (!isPreviewReady())
        return;
    auto title = TitleDataStore::instance().get_title(
        live_cue_preview_title_id_.toStdString());
    const int row = live_cue_preview_row_;
    if (title)
        enterPreview(title, row);
    else
        leavePreview();
}

void TitleDock::release_live_cue_preview_source()
{
    detach_live_cue_preview_from_host();
    live_cue_preview_session_.reset();
}

int TitleDock::program_hotkey_target_row(const std::shared_ptr<Title> &title,
                                         bool prefer_last) const
{
    if (!title)
        return -1;
    const int row_count = std::max(1, live_text_playlist_row_count(title));
    auto valid_row = [row_count](int candidate) {
        return candidate >= 0 && candidate < row_count ? candidate : -1;
    };

    if (prefer_last) {
        int previous = valid_row(title->last_cue_row);
        if (previous < 0)
            previous = valid_row(last_cued_rows_.value(
                QString::fromStdString(title->id), -1));
        if (previous >= 0)
            return previous;
    }
    if (QString::fromStdString(title->id) == QString::fromStdString(selected_id()) &&
        text_table_) {
        const int selected_row = valid_row(text_table_->currentRow());
        if (selected_row >= 0)
            return selected_row;
    }
    const int pending = valid_row(title->pending_cue_row);
    if (pending >= 0)
        return pending;
    const int current = valid_row(title->current_cue_row);
    if (current >= 0)
        return current;
    return 0;
}

void TitleDock::handle_program_command(fxm::editor_host::ProgramCommand command,
                                       const std::string &title_id)
{
    auto title = TitleDataStore::instance().get_title(title_id);
    if (!title)
        return;

    if (command == fxm::editor_host::ProgramCommand::NextCue ||
        command == fxm::editor_host::ProgramCommand::PreviousCue) {
        const int row_count = live_text_playlist_row_count(title);
        if (row_count <= 0)
            return;
        const int delta = command == fxm::editor_host::ProgramCommand::NextCue
            ? 1 : -1;
        int base = -1;
        if (select_row_before_cue_ && isPreviewReady() &&
            live_cue_preview_title_id_ == QString::fromStdString(title_id)) {
            base = live_cue_preview_row_;
        } else if (title->pending_cue_row >= 0) {
            base = title->pending_cue_row;
        } else {
            base = title->current_cue_row;
        }
        const int row = base >= 0 && base < row_count
            ? (base + delta + row_count) % row_count
            : (delta < 0 ? row_count - 1 : 0);
        if (select_row_before_cue_) {
            /* Relative navigation is Preview-only, including when wrapping to
             * the row that is already in Program. Do not let the ordinary
             * active-row toggle interpret that case as an Uncue. */
            if (title_has_bound_host_source(title->id))
                enterPreview(title, row);
        } else {
            cue_live_text_row_for_title(title, row, true);
        }
        return;
    }

    if (command == fxm::editor_host::ProgramCommand::Uncue) {
        const int row = title->pending_cue_row >= 0
            ? title->pending_cue_row : title->current_cue_row;
        if (row >= 0)
            cue_live_text_row_for_title(title, row, true);
        return;
    }

    int row = -1;
    if (command == fxm::editor_host::ProgramCommand::CueToProgram &&
        isPreviewReady() &&
        live_cue_preview_title_id_ == QString::fromStdString(title_id)) {
        row = live_cue_preview_row_;
    } else {
        row = program_hotkey_target_row(
            title, command == fxm::editor_host::ProgramCommand::CueLast);
    }
    if (row < 0)
        return;

    /* These three commands explicitly target Program. Temporarily bypass the
     * optional first-click row-selection gate without changing its persisted
     * setting. */
    const bool preview_setting = select_row_before_cue_;
    select_row_before_cue_ = false;
    cue_live_text_row_for_title(title, row, false);
    select_row_before_cue_ = preview_setting;
}

bool TitleDock::cue_live_text_row(int row, bool allow_uncue)
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    return cue_live_text_row_for_title(title, row, allow_uncue);
}

bool TitleDock::cue_live_text_row_for_title(const std::shared_ptr<Title> &title, int row, bool allow_uncue, bool force_restart)
{
    if (!title) return false;
    if (!title_has_bound_host_source(title->id)) {
        FXM_LOG_INFO("CueControl", QStringLiteral("Rejected cue for unbound title=%1")
                                      .arg(QString::fromStdString(title->id)));
        return false;
    }
    const QString title_id = QString::fromStdString(title->id);
    const bool selected_title = title_id == QString::fromStdString(selected_id());

    auto exposed_now = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed_now);

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "CueControl", [&]() {
            std::ostringstream stream;
            stream << "request title=" << ExternalDataLog::sanitize(title->id, 160)
                   << " row=" << row
                   << " rowId=" << ExternalDataLog::sanitize(live_text_row_id(*title, row), 240)
                   << " current=" << title->current_cue_row
                   << " pending=" << title->pending_cue_row
                   << " playbackMode=" << title->playback_mode
                   << " allowUncue=" << (allow_uncue ? 1 : 0)
                   << " forceRestart=" << (force_restart ? 1 : 0)
                   << " exposedColumns=" << exposed_now.size();
            return stream.str();
        });

    for (const auto &source : title->external_data_sources) {
        if (source.provider.enabled &&
            source.provider.refresh_mode == ExternalDataRefreshMode::RefreshOnCue) {
            ExternalDataProviderService::instance().configure_source(source);
            ExternalDataProviderService::instance().connect_source(source.id);
            ExternalDataProviderService::instance().refresh_source(source.id);
        }
    }

    const bool row_is_program_active = title->current_cue_row == row ||
        title->pending_cue_row == row;
    const bool same_preview_ready = isPreviewReady() &&
        live_cue_preview_title_id_ == title_id &&
        live_cue_preview_row_ == row;
    if (allow_uncue && row_is_program_active) {
        /* Program uncue does not clear Preview output. The frozen row remains
         * available and becomes green again after Program completes its
         * outro. While it exists on both buses, the row is red. */
    } else if (select_row_before_cue_ && allow_uncue && !force_restart) {
        if (same_preview_ready) {
            /* Second Cue/Take consumes the already prepared snapshot and now
             * enters the unchanged Program cue path below. Keep the private
             * preview frozen so Preview+Program is represented by one red row. */
        } else {
            if (isPreviewReady())
                leavePreview();
            return enterPreview(title, row);
        }
    }

    if (exposed_now.empty()) {
        updating_exposed_text_ = true;
        cache_waiting_cue_row_ = -1;
        cache_waiting_title_id_.clear();
        const bool row_active = title->current_cue_row == 0 || title->pending_cue_row == 0;
        const bool immediate_finalize = allow_uncue && title->current_cue_row == 0 &&
            title->cue_uncue_requested;
        const bool uncue_active = allow_uncue && row_active && !immediate_finalize;
        /* Titles without exposed fields still have one synthetic cue row. The
         * first click requests the outro; a second click while yellow clears
         * the cue immediately and lets the source apply When cue ends. */
        title->current_cue_row = immediate_finalize ? -1 : 0;
        title->pending_cue_row = -1;
        title->cue_uncue_requested = uncue_active;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
        if (!uncue_active && !immediate_finalize) {
            last_cued_rows_[title_id] = 0;
            title->last_cue_row = 0;
        }
        ++title->cue_revision;
        TitleDataStore::instance().touch_runtime_change();
        seen_store_revision_ = TitleDataStore::instance().revision();
        updating_exposed_text_ = false;
        if (selected_title)
            update_live_text_runtime_status_fast(title, 0);
        schedule_title_list_cache_icon_update(title_id);
        return true;
    }

    if (row < 0 || row >= (int)title->live_text_rows.size())
        return false;

    /* An uncue releases the row that is already on Program; it never needs a
     * new prerender. Sending it through the cue cache gate can leave the outro
     * armed indefinitely (or turn the click into a delayed cue restart). */
    const bool program_uncue_requested =
        allow_uncue && row_is_program_active;
    const bool require_cached_cue = !program_uncue_requested &&
        CacheManager::instance().cacheEnabled() &&
        CacheManager::instance().titleCacheability(title) != TitleCacheability::NonCacheable;
    if (require_cached_cue && cache_waiting_title_id_ == title_id && cache_waiting_cue_row_ == row) {
        if (selected_title)
            update_live_text_runtime_status_fast(title, row);
        else
            schedule_title_list_cache_icon_update(title_id);
        return false;
    }
    if (require_cached_cue && !CacheManager::instance().prepareLiveCueForPlayback(title, row)) {
        cache_waiting_cue_row_ = row;
        cache_waiting_title_id_ = QString::fromStdString(title->id);
        FXM_LOG_INFO("LiveCueUI", QStringLiteral("Armed cue row waiting for prerender title=%1 row=%2")
                                    .arg(cache_waiting_title_id_).arg(row));
        /* prepareLiveCueForPlayback() has already queued high-priority disk
         * hydration (or missing renders) for the exact steady/transition state.
         * Nearby rows remain ordinary background work. */
        CacheManager::instance().preloadLiveCues(title, row, 2);
        if (selected_title)
            update_live_text_runtime_status_fast(title, row);
        else
            schedule_title_list_cache_icon_update(title_id);
        QTimer::singleShot(0, this, [this, title, row]() {
            const QString title_id = QString::fromStdString(title->id);
            const bool selected_title = title_id == QString::fromStdString(selected_id());
            if (selected_title)
                update_live_text_runtime_status_fast(title, row);
            else
                schedule_title_list_cache_icon_update(title_id);
            if (cache_waiting_title_id_ == title_id && cache_waiting_cue_row_ == row &&
                CacheManager::instance().prepareLiveCueForPlayback(title, row)) {
                cache_waiting_cue_row_ = -1;
                cache_waiting_title_id_.clear();
                cue_live_text_row_for_title(title, row, false, true);
            }
        });
        return false;
    }

    updating_exposed_text_ = true;
    apply_persistence_settings_to_title(title);
    const bool is_active_cue = !force_restart && title->current_cue_row == row;
    const bool is_pending_cue = !force_restart && title->pending_cue_row == row;
    const int previous_row = title->current_cue_row >= 0 ? title->current_cue_row : title->pending_cue_row;
    const bool can_persist_transition = title->cue_background_persistence &&
        (title->playback_mode == 1 || title->playback_mode == 2) &&
        previous_row >= 0 && previous_row != row;
    const bool needs_outro_before_cue =
        (title->playback_mode == 1 || title->playback_mode == 2) &&
        title->current_cue_row >= 0 && title->current_cue_row != row;
    const char *cue_decision = "no-op";

    title->cue_persistence_transition = false;
    title->cue_persistent_text_columns.assign(exposed_now.size(), false);
    if (!(allow_uncue && (is_active_cue || is_pending_cue)))
        title->cue_uncue_requested = false;

    if (allow_uncue && is_active_cue && title->cue_uncue_requested) {
        cache_waiting_cue_row_ = -1;
        cache_waiting_title_id_.clear();
        title->current_cue_row = -1;
        title->pending_cue_row = -1;
        title->cue_uncue_requested = false;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
        ++title->cue_revision;
        TitleDataStore::instance().touch_runtime_change();
        updating_exposed_text_ = false;
        if (selected_title)
            update_live_text_runtime_status_fast(title, row);
        schedule_title_list_cache_icon_update(title_id);
        return true;
    } else if (allow_uncue && (is_active_cue || is_pending_cue)) {
        cache_waiting_cue_row_ = -1;
        cache_waiting_title_id_.clear();
        /* Keep current_cue_row active while the source plays the outro.  This
         * preserves the cued status in the dock and gives cached playback the
         * exact outgoing row until the last frame. */
        title->pending_cue_row = -1;
        title->cue_uncue_requested = true;
        title->cue_persistence_transition = false;
        title->cue_persistent_text_columns.clear();
        cue_decision = "uncue-outro";
    } else if (can_persist_transition) {
        for (int col = 0; col < (int)exposed_now.size(); ++col) {
            auto &target = exposed_now[col];
            const int previous_value_row = target && target->exposed_single_value ? 0 : previous_row;
            const int next_value_row = target && target->exposed_single_value ? 0 : row;
            if (title->cue_text_persistence &&
                previous_value_row >= 0 && previous_value_row < (int)title->live_text_rows.size() &&
                next_value_row >= 0 && next_value_row < (int)title->live_text_rows.size() &&
                col < (int)title->live_text_rows[previous_value_row].size() &&
                col < (int)title->live_text_rows[next_value_row].size() &&
                effective_live_text_cue_value(*title, previous_value_row, *target,
                    title->live_text_rows[previous_value_row][col]) ==
                effective_live_text_cue_value(*title, next_value_row, *target,
                    title->live_text_rows[next_value_row][col]))
                title->cue_persistent_text_columns[col] = true;
        }
        title->pending_cue_row = row;
        title->cue_persistence_transition = true;
        cue_decision = "pending-persistence-outro";
    } else if (needs_outro_before_cue) {
        title->pending_cue_row = row;
        cue_decision = "pending-outro";
    } else if (force_restart || !is_active_cue || title->pending_cue_row >= 0) {
        for (int col = 0; col < (int)exposed_now.size(); ++col) {
            auto &target = exposed_now[col];
            const int value_row = target && target->exposed_single_value ? 0 : row;
            if (value_row < 0 || value_row >= (int)title->live_text_rows.size() ||
                col >= (int)title->live_text_rows[value_row].size())
                continue;
            apply_live_text_runtime_binding(*title, value_row, *target);
            const std::string cue_value = effective_live_text_cue_value(
                *title, value_row, *target, title->live_text_rows[value_row][col]);
            apply_live_cue_layer_value(*title, target, row, cue_value);
        }
        title->current_cue_row = row;
        title->pending_cue_row = -1;
        cue_decision = "apply-immediately";
    }

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "CueControl", [&]() {
            std::ostringstream stream;
            stream << "decision title=" << ExternalDataLog::sanitize(title->id, 160)
                   << " row=" << row
                   << " decision=" << cue_decision
                   << " current=" << title->current_cue_row
                   << " pending=" << title->pending_cue_row
                   << " playbackMode=" << title->playback_mode
                   << " persistence=" << (title->cue_persistence_transition ? 1 : 0);
            return stream.str();
        });

    ++title->cue_revision;
    cache_waiting_cue_row_ = -1;
    cache_waiting_title_id_.clear();
    TitleDataStore::instance().touch_runtime_change();
    seen_store_revision_ = TitleDataStore::instance().revision();
    CacheManager::instance().preloadLiveCues(title, row, 2);
    if (title->current_cue_row >= 0 || title->pending_cue_row >= 0) {
        last_cued_rows_[title_id] = row;
        title->last_cue_row = row;
    }
    updating_exposed_text_ = false;
    if (selected_title) {
        update_live_text_runtime_status_fast(title, row, previous_row);
        QTimer::singleShot(0, this, [this, title, row, previous_row]() { update_live_text_runtime_status_fast(title, row, previous_row); });
    } else {
        schedule_title_list_cache_icon_update(title_id);
    }
    return true;
}

int TitleDock::live_text_playlist_row_count(const std::shared_ptr<Title> &title) const
{
    if (!title) return 0;
    auto exposed = exposed_text_layers(title);
    if (exposed.empty())
        return 1;
    return (int)title->live_text_rows.size();
}

int TitleDock::next_playlist_row(const std::shared_ptr<Title> &title, int current_row, int row_count) const
{
    if (row_count <= 0) return 0;
    const bool reverse = title ? title->playlist_reverse : playlist_reverse_;
    return (current_row + (reverse ? -1 : 1) + row_count) % row_count;
}
