int TitleDock::playlist_step_delay_ms(const std::shared_ptr<Title> &title) const
{
    if (!title) return 1000;

    double seconds = title->playlist_hold_seconds;
    if (title->playback_mode == 1)
        seconds += std::clamp(title->loop_end, title->loop_start, title->duration);
    else if (title->playback_mode == 2)
        seconds += std::clamp(title->pause_time, 0.0, title->duration);
    else
        seconds += title->duration;

    return std::max(1, (int)std::round(seconds * 1000.0));
}

int TitleDock::playlist_hold_delay_ms(const std::shared_ptr<Title> &title) const
{
    const double seconds = title ? title->playlist_hold_seconds : playlist_hold_seconds_;
    return std::max(1, (int)std::round(seconds * 1000.0));
}

bool TitleDock::playlist_row_is_terminal(const std::shared_ptr<Title> &title, int row, int row_count) const
{
    if (row < 0 || row_count <= 0) return false;
    const bool reverse = title ? title->playlist_reverse : playlist_reverse_;
    return reverse ? row == 0 : row == row_count - 1;
}

void TitleDock::play_playlist_outro(const std::shared_ptr<Title> &title)
{
    if (!title) return;

    const int row = title->pending_cue_row >= 0 ? title->pending_cue_row : title->current_cue_row;
    if (row >= 0)
        cue_live_text_row_for_title(title, row, true);
    else if (live_text_playlist_row_count(title) == 1)
        cue_live_text_row_for_title(title, 0, true);
}

void TitleDock::stop_playlist()
{
    stop_playlist_for_title(TitleDataStore::instance().get_title(selected_id()));
}

void TitleDock::stop_playlist_for_title(const std::shared_ptr<Title> &title)
{
    if (!title)
        return;
    title->playlist_active = false;
    title->playlist_next_due_ms = 0;
    FXM_LOG_INFO("Playlist", QStringLiteral("Stopped playlist title=%1")
        .arg(QString::fromStdString(title->id)));
    title->playlist_stop_after_due = false;
    TitleDataStore::instance().touch_runtime_change();
    seen_store_revision_ = TitleDataStore::instance().revision();

    bool any_active = false;
    for (const auto &candidate : TitleDataStore::instance().title_snapshots()) {
        if (candidate && candidate->playlist_active) {
            any_active = true;
            break;
        }
    }
    if (!any_active && playlist_timer_)
        playlist_timer_->stop();

    if (QString::fromStdString(title->id) == QString::fromStdString(selected_id())) {
        if (btn_playlist_ && btn_playlist_->isChecked()) {
            QSignalBlocker block(btn_playlist_);
            btn_playlist_->setChecked(false);
        }
        update_playlist_countdown_label();
    }
    update_title_list_cache_icon(QString::fromStdString(title->id));
}

void TitleDock::sync_playlist_runtime_state()
{
    bool any_active = false;
    for (const auto &title : TitleDataStore::instance().title_snapshots()) {
        if (title && title->playlist_active) {
            any_active = true;
            break;
        }
    }

    if (any_active) {
        if (playlist_timer_ && !playlist_timer_->isActive())
            playlist_timer_->start();
    } else if (playlist_timer_ && playlist_timer_->isActive()) {
        playlist_timer_->stop();
    }

    if (list_) {
        for (int i = 0; i < list_->count(); ++i) {
            auto *item = list_->item(i);
            if (!item)
                continue;
            const QString title_id = item->data(Qt::UserRole).toString();
            auto title = TitleDataStore::instance().get_title(title_id.toStdString());
            const bool active = title && title->playlist_active;
            if (item->data(kTitlePlaylistActiveRole).toBool() != active)
                schedule_title_list_cache_icon_update(title_id);
        }
    }

    update_playlist_controls();
}

namespace {
QString format_live_cue_elapsed(double seconds)
{
    const qint64 whole_seconds = static_cast<qint64>(
        std::floor(std::max(0.0, seconds)));
    const qint64 minutes = whole_seconds / 60;
    const int remaining_seconds = static_cast<int>(whole_seconds % 60);
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(remaining_seconds, 2, 10, QChar('0'));
}

QString format_live_cue_countdown(double seconds)
{
    const qint64 total_tenths = static_cast<qint64>(
        std::ceil(std::max(0.0, seconds) * 10.0 - 0.000001));
    const qint64 minutes = total_tenths / 600;
    const int remaining_seconds = static_cast<int>((total_tenths / 10) % 60);
    const int tenths = static_cast<int>(total_tenths % 10);
    if (minutes > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(minutes)
            .arg(remaining_seconds, 2, 10, QChar('0'))
            .arg(tenths);
    }
    return QStringLiteral("%1:%2")
        .arg(remaining_seconds, 2, 10, QChar('0'))
        .arg(tenths);
}
} // namespace

void TitleDock::update_live_cue_timer_label()
{
    if (!live_cue_timer_lbl_)
        return;

    auto title = TitleDataStore::instance().get_title(selected_id());
    const LiveCueRuntimeSnapshot runtime = title
        ? live_cue_runtime_state(title->id)
        : LiveCueRuntimeSnapshot{};
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    const bool fresh_runtime = title && runtime.active &&
        title->current_cue_row >= 0 &&
        runtime.row == title->current_cue_row &&
        runtime.source_token != 0 &&
        runtime.updated_ms > 0 &&
        now_ms - runtime.updated_ms <= 2000;

    if (!fresh_runtime) {
        live_cue_timer_lbl_->clear();
        live_cue_timer_lbl_->setVisible(false);
        return;
    }

    const bool ending = title->cue_uncue_requested ||
        title->pending_cue_row >= 0;
    const bool play_once = title->playback_mode == 0;
    if (ending || play_once) {
        const double remaining = std::max(
            0.0, title->duration - runtime.playhead);
        live_cue_timer_lbl_->setText(format_live_cue_countdown(remaining));
        live_cue_timer_lbl_->setToolTip(fxm_tr(
            ending ? "OBSTitles.LiveCueOutroRemainingTooltip"
                   : "OBSTitles.LiveCuePlayOnceRemainingTooltip"));
    } else {
        live_cue_timer_lbl_->setText(format_live_cue_elapsed(
            runtime.elapsed_seconds));
        live_cue_timer_lbl_->setToolTip(
            fxm_tr("OBSTitles.LiveCueElapsedTooltip"));
    }
    live_cue_timer_lbl_->setVisible(true);
}

void TitleDock::update_playlist_countdown_label()
{
    if (!playlist_countdown_lbl_) return;

    auto title = TitleDataStore::instance().get_title(selected_id());
    const bool active = title && title->playlist_active && title->playlist_next_due_ms > 0;
    if (!active) {
        playlist_countdown_lbl_->setText(QStringLiteral("--"));
        playlist_countdown_lbl_->setVisible(false);
        return;
    }

    const qint64 remaining_ms = std::max<qint64>(0, title->playlist_next_due_ms - QDateTime::currentMSecsSinceEpoch());
    const double remaining_seconds = remaining_ms / 1000.0;
    playlist_countdown_lbl_->setText(QStringLiteral("%1s").arg(remaining_seconds, 0, 'f', remaining_seconds < 10.0 ? 1 : 0));
    playlist_countdown_lbl_->setVisible(true);
}

void TitleDock::start_playlist_step()
{
    start_playlist_step_for_title(TitleDataStore::instance().get_title(selected_id()));
}

void TitleDock::start_playlist_step_for_title(const std::shared_ptr<Title> &title)
{
    const int row_count = live_text_playlist_row_count(title);
    if (!title || row_count <= 0) {
        stop_playlist_for_title(title);
        return;
    }

    if (title->playlist_next_row < 0 || title->playlist_next_row >= row_count)
        title->playlist_next_row = title->playlist_reverse ? row_count - 1 : 0;

    const int row = title->playlist_next_row;
    FXM_LOG_DEBUG("Playlist", QStringLiteral(
        "Advancing playlist title=%1 row=%2 rows=%3 reverse=%4 loop=%5")
        .arg(QString::fromStdString(title->id))
        .arg(row).arg(row_count)
        .arg(title->playlist_reverse ? 1 : 0)
        .arg(title->playlist_loop ? 1 : 0));
    const bool already_active = title->pending_cue_row < 0 && title->current_cue_row == row;
    if (!already_active)
        cue_live_text_row_for_title(title, row, false);
    title->playlist_next_row = next_playlist_row(title, row, row_count);
    title->playlist_stop_after_due = !title->playlist_loop && playlist_row_is_terminal(title, row, row_count);

    title->playlist_next_due_ms = QDateTime::currentMSecsSinceEpoch() +
        (already_active ? playlist_hold_delay_ms(title) : playlist_step_delay_ms(title));
    if (playlist_timer_ && !playlist_timer_->isActive())
        playlist_timer_->start();
    if (QString::fromStdString(title->id) == QString::fromStdString(selected_id()))
        update_playlist_countdown_label();
    update_title_list_cache_icon(QString::fromStdString(title->id));
}

void TitleDock::on_playlist_tick()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool any_active = false;
    for (const auto &title : TitleDataStore::instance().titles()) {
        if (!title || !title->playlist_active)
            continue;
        any_active = true;
        if (title->playlist_next_due_ms <= 0 || now < title->playlist_next_due_ms)
            continue;

        if (title->playlist_stop_after_due) {
            play_playlist_outro(title);
            stop_playlist_for_title(title);
        } else {
            start_playlist_step_for_title(title);
        }
    }

    if (!any_active && playlist_timer_)
        playlist_timer_->stop();
    update_playlist_countdown_label();
}

void TitleDock::on_toggle_playlist(bool enabled)
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!enabled) {
        stop_playlist_for_title(title);
        return;
    }

    const int row_count = live_text_playlist_row_count(title);
    if (!title || row_count <= 0) {
        if (btn_playlist_ && btn_playlist_->isChecked()) {
            QSignalBlocker block(btn_playlist_);
            btn_playlist_->setChecked(false);
        }
        update_playlist_countdown_label();
        return;
    }

    title->playlist_active = true;
    title->playlist_loop = playlist_loop_;
    title->playlist_reverse = playlist_reverse_;
    title->playlist_hold_seconds = playlist_hold_seconds_;
    TitleDataStore::instance().touch_runtime_change();
    seen_store_revision_ = TitleDataStore::instance().revision();
    int base = title->pending_cue_row >= 0 ? title->pending_cue_row : title->current_cue_row;
    if (title->pending_cue_row < 0 && base >= 0 && base < row_count) {
        title->playlist_next_row = next_playlist_row(title, base, row_count);
        title->playlist_stop_after_due = !title->playlist_loop && playlist_row_is_terminal(title, base, row_count);
        title->playlist_next_due_ms = QDateTime::currentMSecsSinceEpoch() + playlist_hold_delay_ms(title);
        if (playlist_timer_ && !playlist_timer_->isActive())
            playlist_timer_->start();
        update_playlist_countdown_label();
        update_title_list_cache_icon(QString::fromStdString(title->id));
        return;
    }

    if (base >= 0 && base < row_count)
        title->playlist_next_row = base;
    else
        title->playlist_next_row = title->playlist_reverse ? row_count - 1 : 0;

    start_playlist_step_for_title(title);
}

void TitleDock::update_playlist_controls()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    const bool enabled = title && live_text_playlist_row_count(title) > 0;
    if (title) {
        playlist_loop_ = title->playlist_loop;
        playlist_reverse_ = title->playlist_reverse;
        playlist_hold_seconds_ = title->playlist_hold_seconds;
    }
    if (btn_playlist_) {
        QSignalBlocker block(btn_playlist_);
        btn_playlist_->setEnabled(enabled);
        btn_playlist_->setChecked(enabled && title->playlist_active);
    }
    if (act_playlist_loop_) {
        act_playlist_loop_->setEnabled(enabled);
        QSignalBlocker block(act_playlist_loop_);
        act_playlist_loop_->setChecked(title ? title->playlist_loop : playlist_loop_);
    }
    if (act_playlist_reverse_) {
        act_playlist_reverse_->setEnabled(enabled);
        QSignalBlocker block(act_playlist_reverse_);
        act_playlist_reverse_->setChecked(title ? title->playlist_reverse : playlist_reverse_);
    }
    if (act_playlist_restart_on_active_) {
        act_playlist_restart_on_active_->setEnabled(enabled);
        QSignalBlocker block(act_playlist_restart_on_active_);
        act_playlist_restart_on_active_->setChecked(title && title->playlist_restart_on_source_active);
    }
    if (act_playlist_stop_on_inactive_) {
        act_playlist_stop_on_inactive_->setEnabled(enabled);
        QSignalBlocker block(act_playlist_stop_on_inactive_);
        act_playlist_stop_on_inactive_->setChecked(title && title->playlist_stop_on_source_inactive);
    }
    if (act_playlist_hold_)
        act_playlist_hold_->setEnabled(enabled);
    if (auto *hold_action = qobject_cast<QWidgetAction *>(act_playlist_hold_)) {
        if (auto *widget = hold_action->defaultWidget()) {
            if (auto *spin = widget->findChild<QDoubleSpinBox *>()) {
                QSignalBlocker block(spin);
                spin->setValue(title ? title->playlist_hold_seconds : playlist_hold_seconds_);
            }
        }
    }
    update_playlist_countdown_label();
}

void TitleDock::populate_exposed_text()
{
    if (!text_table_) return;
    update_live_text_columns_window_title();
    QSignalBlocker block(text_table_);
    QSignalBlocker header_block(text_table_->horizontalHeader());
    text_table_->clear();
    text_table_->setRowCount(0);
    text_table_->setColumnCount(0);

    auto *header = live_text_cue_header(text_table_);

    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) {
        if (header) header->set_select_all_visible(false);
        text_editor_lbl_->setText(fxm_tr("OBSTitles.LiveTextSelectTitle"));
        text_table_->setEnabled(false);
        if (btn_add_text_row_) btn_add_text_row_->setEnabled(false);
        if (btn_delete_text_row_) btn_delete_text_row_->setEnabled(false);
        if (btn_row_up_) btn_row_up_->setEnabled(false);
        if (btn_row_down_) btn_row_down_->setEnabled(false);
        update_playlist_controls();
        update_persistence_controls();
        update_external_data_controls();
        update_live_text_select_all_state();
        enforce_live_text_cache_column_visibility();
        return;
    }

    const QString current_width_title_id = QString::fromStdString(title->id);
    const bool first_width_fit_for_title = live_text_width_initialized_title_id_ != current_width_title_id;
    if (first_width_fit_for_title)
        live_text_width_initialized_title_id_ = current_width_title_id;

    auto exposed = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed);

    if (auto *cue_table = dynamic_cast<LiveTextCueTable *>(text_table_)) {
        const QString border_title_id = QString::fromStdString(title->id);
        cue_table->cue_row_border_color = [this, border_title_id](int row) -> QColor {
            auto current_title = TitleDataStore::instance().get_title(selected_id());
            if (!current_title || QString::fromStdString(current_title->id) != border_title_id)
                return QColor();
            const bool waiting_for_prerender = cache_waiting_title_id_ == border_title_id &&
                cache_waiting_cue_row_ == row;
            const bool preview = isPreviewReady() &&
                live_cue_preview_title_id_ == border_title_id &&
                live_cue_preview_row_ == row;
            if (row == current_title->current_cue_row)
                return live_cue_state_color(
                    true, preview,
                    live_cue_row_is_ending(*current_title, row) && !preview);
            if (row == current_title->pending_cue_row || waiting_for_prerender || preview)
                return live_cue_state_color(false, true);
            return QColor();
        };
    }

    const bool has_exposed = !exposed.empty();
    text_table_->setEnabled(true);
    if (btn_add_text_row_) btn_add_text_row_->setEnabled(has_exposed);
    if (btn_delete_text_row_) btn_delete_text_row_->setEnabled(has_exposed);
    if (btn_row_up_) btn_row_up_->setEnabled(has_exposed);
    if (btn_row_down_) btn_row_down_->setEnabled(has_exposed);
    text_editor_lbl_->setText(fxm_tr("OBSTitles.LiveTextCues"));
    if (header) header->set_select_all_visible(has_exposed);
    if (!has_exposed) {
        text_table_->setRowCount(1);
        text_table_->setColumnCount(3);
        text_table_->setHorizontalHeaderLabels(QStringList()
                                               << QString()
                                               << QString()
                                               << fxm_tr("OBSTitles.Title"));
        text_table_->setColumnHidden(kStaticCacheColumn, !CacheManager::instance().cacheEnabled());
        text_table_->horizontalHeader()->setSectionsMovable(false);
        text_table_->horizontalHeader()->setSectionResizeMode(kStaticCueColumn, QHeaderView::ResizeToContents);
        text_table_->horizontalHeader()->setSectionResizeMode(kStaticTitleColumn, QHeaderView::Stretch);
        text_table_->horizontalHeader()->setSectionResizeMode(kStaticCacheColumn, QHeaderView::ResizeToContents);
        text_table_->setVerticalHeaderItem(0, new QTableWidgetItem(QStringLiteral("1")));

        auto *title_item = new QTableWidgetItem(QString::fromStdString(title->name));
        title_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        title_item->setToolTip(title_item->text());
        text_table_->setItem(0, kStaticTitleColumn, title_item);

        auto *cue = live_cue_button(text_table_, 0, kStaticCueColumn);
        if (cue) {
            cue->setEnabled(title_has_bound_host_source(title->id));
            const bool current = title->current_cue_row == 0;
            const bool preview = isPreviewReady() &&
                live_cue_preview_title_id_ == QString::fromStdString(title->id) &&
                live_cue_preview_row_ == 0;
            const bool queued = title->pending_cue_row == 0 ||
                (cache_waiting_title_id_ == QString::fromStdString(title->id) && cache_waiting_cue_row_ == 0) ||
                preview;
            cue->setIcon(obs_icon("cue.svg", live_cue_state_color(
                current, queued,
                live_cue_row_is_ending(*title, 0) && !(preview && current))));
            cue->setToolTip(fxm_tr("OBSTitles.PlayCueTooltip"));
            cue->setStyleSheet(QStringLiteral(
                "QToolButton{background:transparent;border:none;padding:0;}"
                "QToolButton:hover{background:transparent;border:none;}"));
            connect(cue, &QToolButton::clicked, this, [this]() { cue_live_text_row(0, true); });
        }
        update_live_text_cache_cell(title, 0);
        update_live_text_select_cell_status(0);
        apply_live_text_row_heights();
        adjust_live_text_table_columns(first_width_fit_for_title);
        update_playlist_controls();
        update_persistence_controls();
        update_external_data_controls();
        update_live_text_select_all_state();
        enforce_live_text_cache_column_visibility();
        return;
    }

    /* Reconcile stable row IDs/cache states before creating row widgets. This
     * prevents a freshly rebuilt table from briefly binding old numeric-row
     * states to the new row order after delete/re-add/reorder operations. */
    CacheManager::instance().refreshLiveCueStructureAsync(title);

    text_table_->setRowCount((int)title->live_text_rows.size());
    text_table_->setColumnCount((int)exposed.size() + 3);

    QStringList headers;
    headers << QString() << QString() << "";
    for (const auto &layer : exposed)
        headers << live_text_layer_header(layer);
    text_table_->setHorizontalHeaderLabels(headers);
    text_table_->setColumnHidden(kLiveCacheColumn, !CacheManager::instance().cacheEnabled());
    for (int col = 0; col < (int)exposed.size(); ++col) {
        if (auto *item = text_table_->horizontalHeaderItem(col + kLiveFirstValueColumn))
            item->setToolTip(live_text_layer_header(exposed[col]));
    }
    text_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    text_table_->horizontalHeader()->setSectionsMovable(true);
    restore_live_text_header_state();
    enforce_live_text_cache_column_visibility();
    auto *table_header = text_table_->horizontalHeader();
    if (table_header) {
        const int cue_visual = table_header->visualIndex(kLiveCueColumn);
        if (cue_visual > 0)
            table_header->moveSection(cue_visual, 0);
        const int cache_visual = table_header->visualIndex(kLiveCacheColumn);
        if (cache_visual != 1)
            table_header->moveSection(cache_visual, 1);
        const int select_visual = table_header->visualIndex(kLiveSelectColumn);
        if (select_visual != 2)
            table_header->moveSection(select_visual, 2);
    }
    adjust_live_text_table_columns(first_width_fit_for_title);

    auto configure_external_cell_binding =
        [this, title](QWidget *widget, int row, int col,
                      const std::shared_ptr<Layer> &layer,
                      const std::string &authored_value) {
            if (!widget || !layer) return;
            const std::string row_id = live_text_row_id(*title, row);
            const LiveTextCueCellState cell_state =
                live_text_cue_cell_state(*title, row_id, layer->id);
            const auto *existing = live_text_external_binding_for_cell(
                *title, row_id, layer->id);
            const bool table_managed =
                cell_state == LiveTextCueCellState::ExternalTableManaged;
            const bool table_detached =
                cell_state == LiveTextCueCellState::DetachedFromTable;
            widget->setProperty("liveCueCellState",
                table_managed ? "external-table-managed"
                : table_detached ? "external-table-detached"
                : existing ? "external-bound" : "authored");
            widget->setProperty("externalDataBound", existing != nullptr);
            widget->setProperty("externalTableManaged", table_managed);
            widget->setProperty("externalTableReadOnly", table_managed);

            if (auto *edit = dynamic_cast<LiveTextCueField *>(widget))
                edit->setReadOnly(table_managed);
            if (auto *image = dynamic_cast<LiveImageCueField *>(widget))
                image->set_read_only(table_managed);

            QFont display_font = widget->font();
            display_font.setItalic(table_managed);
            widget->setFont(display_font);
            const QString base_tip = widget->toolTip();
            if (table_managed) {
                const QString binding_tip = QStringLiteral(
                    "Mapped table data · read-only\n%1 · %2\n"
                    "Use ‘Convert to editable value’ to detach this cell.")
                    .arg(QString::fromStdString(existing ? existing->binding.source_id : std::string{}),
                         QString::fromStdString(existing ? existing->binding.field_path : std::string{}));
                widget->setToolTip(base_tip.isEmpty() ? binding_tip
                                                      : base_tip + QStringLiteral("\n") + binding_tip);
            } else if (table_detached) {
                const QString detached_tip = QStringLiteral(
                    "Editable value detached from its table mapping.");
                widget->setToolTip(base_tip.isEmpty() ? detached_tip
                                                      : base_tip + QStringLiteral("\n") + detached_tip);
            } else if (existing) {
                const QString binding_tip = QStringLiteral("External data: %1 · %2")
                    .arg(QString::fromStdString(existing->binding.source_id),
                         QString::fromStdString(existing->binding.field_path));
                widget->setToolTip(base_tip.isEmpty() ? binding_tip
                                                      : base_tip + QStringLiteral("\n") + binding_tip);
            }
            if (existing) {
                widget->setStyleSheet(widget->styleSheet() + QStringLiteral(
                    "QPlainTextEdit{border:2px solid #f0a000;}"
                    "QWidget[externalDataBound=\"true\"]{border:2px solid #f0a000;}"));
            }
            widget->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(widget, &QWidget::customContextMenuRequested, this,
                    [this, title, row, col, layer, authored_value, widget](const QPoint &point) {
                const std::string row_id = live_text_row_id(*title, row);
                const LiveTextCueCellState state =
                    live_text_cue_cell_state(*title, row_id, layer->id);
                const auto *current = live_text_external_binding_for_cell(
                    *title, row_id, layer->id);

                QMenu menu(widget);
                QAction *edit_action = nullptr;
                QAction *remove_action = nullptr;
                QAction *detach_action = nullptr;
                QAction *restore_action = nullptr;
                if (state == LiveTextCueCellState::ExternalTableManaged) {
                    QAction *managed_label = menu.addAction(
                        QStringLiteral("Externally managed table value"));
                    managed_label->setEnabled(false);
                    menu.addSeparator();
                    detach_action = menu.addAction(
                        QStringLiteral("Convert to editable value"));
                } else {
                    edit_action = menu.addAction(QStringLiteral("Bind external data…"));
                    if (current)
                        remove_action = menu.addAction(QStringLiteral("Remove external binding"));
                    if (state == LiveTextCueCellState::DetachedFromTable)
                        restore_action = menu.addAction(
                            QStringLiteral("Restore table-managed value"));
                }

                QAction *chosen = menu.exec(widget->mapToGlobal(point));
                if (!chosen) return;
                if (chosen == detach_action) {
                    const std::string live_value = effective_live_text_cue_value(
                        *title, row, *layer, authored_value);
                    if (row >= 0 && row < static_cast<int>(title->live_text_rows.size()) &&
                        col >= 0 && col < static_cast<int>(title->live_text_rows[row].size()))
                        title->live_text_rows[row][col] = live_value;
                    if (!detach_live_text_table_cell(*title, row_id, layer->id))
                        return;
                    refresh_active_live_text_row_without_recue(title);
                    TitleDataStore::instance().save();
                    TitleDataStore::instance().touch_runtime_change();
                    CacheManager::instance().refreshLiveCueStructureAsync(title);
                    seen_store_revision_ = TitleDataStore::instance().revision();
                    populate_exposed_text();
                    return;
                }
                if (chosen == restore_action) {
                    if (!restore_live_text_table_cell(*title, row_id, layer->id))
                        return;
                    synchronize_live_text_table_bindings(*title);
                    refresh_active_live_text_row_without_recue(title);
                    TitleDataStore::instance().save();
                    TitleDataStore::instance().touch_runtime_change();
                    CacheManager::instance().refreshLiveCueStructureAsync(title);
                    seen_store_revision_ = TitleDataStore::instance().revision();
                    populate_exposed_text();
                    return;
                }
                if (chosen == remove_action) {
                    remove_live_text_external_binding(*title, row_id, layer->id);
                    synchronize_live_text_table_bindings(*title);
                    refresh_active_live_text_row_without_recue(title);
                    TitleDataStore::instance().save();
                    TitleDataStore::instance().notify_change();
                    populate_exposed_text();
                    return;
                }
                if (chosen != edit_action || !edit_action) return;
                const std::string property = layer->type == LayerType::Image
                    ? ExternalPropertyPaths::ImagePath : ExternalPropertyPaths::TextContent;
                const ExternalDataValue authored = ExternalDataValue::string(
                    authored_value, layer->type == LayerType::Image
                        ? ExternalDataType::FilePath : ExternalDataType::String);
                const ExternalPropertyBinding *binding = current ? &current->binding : nullptr;
                ExternalDataBindingDialog dialog(title, property, authored, binding, this);
                if (dialog.exec() != QDialog::Accepted) return;
                if (dialog.remove_requested()) {
                    remove_live_text_external_binding(*title, row_id, layer->id);
                } else {
                    LiveTextExternalBinding cell;
                    cell.row_id = row_id;
                    cell.layer_id = layer->id;
                    cell.binding = dialog.binding();
                    cell.binding.property_path = property;
                    pin_external_data_field(*title, cell.binding.source_id,
                                            cell.binding.field_path);
                    set_live_text_external_binding(*title, std::move(cell));
                }
                refresh_active_live_text_row_without_recue(title);
                TitleDataStore::instance().save();
                TitleDataStore::instance().notify_change();
                populate_exposed_text();
            });
        };

    for (int row = 0; row < (int)title->live_text_rows.size(); ++row) {
        text_table_->setVerticalHeaderItem(row, new QTableWidgetItem(QString::number(row + 1)));
        auto *select_item = new QTableWidgetItem();
        select_item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        select_item->setCheckState(Qt::Unchecked);
        select_item->setTextAlignment(Qt::AlignCenter);
        text_table_->setItem(row, kLiveSelectColumn, select_item);
        update_live_text_cache_cell(title, row);
        for (int col = 0; col < (int)exposed.size(); ++col) {
            if (exposed[col] && exposed[col]->exposed_single_value && row > 0) {
                auto *single_item = new QTableWidgetItem(QString());
                single_item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                single_item->setToolTip(fxm_tr("OBSTitles.ExposedSingleValue"));
                text_table_->setItem(row, col + kLiveFirstValueColumn, single_item);
                continue;
            }
            const std::string &authored_cell_value = title->live_text_rows[row][col];
            /* Bound cue cells must display the resolved provider value, not the
             * intentionally untouched authored/fallback storage. Table mappings
             * create rows with bindings while leaving authored values empty, so
             * constructing widgets from live_text_rows alone made correctly
             * populated source-managed rows appear blank in the dock. */
            const std::string displayed_cell_value = exposed[col]
                ? effective_live_text_cue_value(
                    *title, row, *exposed[col], authored_cell_value)
                : authored_cell_value;
            if (exposed[col]) {
                const std::string row_id = live_text_row_id(*title, row);
                const LiveTextCueCellState cell_state = live_text_cue_cell_state(
                    *title, row_id, exposed[col]->id);
                ExternalDataLog::write_lazy(
                    cell_state == LiveTextCueCellState::ExternalTableManaged
                        ? ExternalDataLogLevel::Debug
                        : ExternalDataLogLevel::Trace,
                    "DockCell", [&]() {
                        std::ostringstream stream;
                        stream << "populate title=" << ExternalDataLog::sanitize(title->id, 160)
                               << " rowIndex=" << row
                               << " rowId=" << ExternalDataLog::sanitize(row_id, 240)
                               << " column=" << col
                               << " layer=" << ExternalDataLog::sanitize(exposed[col]->id, 160)
                               << " state=" << static_cast<int>(cell_state)
                               << " authoredEmpty=" << (authored_cell_value.empty() ? 1 : 0)
                               << " displayed=" << ExternalDataLog::value_summary(
                                      ExternalDataValue::string(displayed_cell_value));
                        return stream.str();
                    });
            }
            if (layer_uses_image_cue_value(exposed[col])) {
                auto *image = new LiveImageCueField(
                    QString::fromStdString(displayed_cell_value), text_table_);
                image->path_changed = [this, title, row, col](const QString &path) {
                    commit_live_text_cell_edit(title, row, col, path);
                };
                text_table_->setCellWidget(row, col + kLiveFirstValueColumn, image);
                configure_external_cell_binding(image, row, col, exposed[col],
                    authored_cell_value);
            } else {
                const bool exposes_value = exposed[col] &&
                    (exposed[col]->expose_text ||
                     fxm::live_text::chart_has_exposed_values(*exposed[col]));
                const bool exposes_color = exposed[col] &&
                    (exposed[col]->expose_fill_color || exposed[col]->expose_stroke_color);

                LiveTextCueField *edit = nullptr;
                if (exposes_value) {
                    edit = new LiveTextCueField(
                        QString::fromStdString(displayed_cell_value), text_table_);
                    edit->setPlaceholderText(live_text_layer_header(exposed[col]));
                    edit->setStyleSheet(QStringLiteral("QPlainTextEdit{padding:3px;}"));
                    edit->editing_finished = [this, title, row, col](const QString &text) {
                        commit_live_text_cell_edit(title, row, col, text);
                    };
                    if (live_text_cue_cell_state(
                            *title, live_text_row_id(*title, row), exposed[col]->id) !=
                        LiveTextCueCellState::ExternalTableManaged) {
                        QPointer<TitleDock> dock_guard(this);
                        edit->focus_changed = [dock_guard, title, row](bool focused) {
                            if (dock_guard)
                                dock_guard->set_live_text_row_render_paused(title, row, focused);
                        };
                    }
                }

                if (exposes_color) {
                    auto *cell = new QWidget(text_table_);
                    auto *cell_layout = new QVBoxLayout(cell);
                    cell_layout->setContentsMargins(0, 0, 0, 0);
                    cell_layout->setSpacing(2);
                    if (edit)
                        cell_layout->addWidget(edit, 1);
                    auto *color_row = new QWidget(cell);
                    auto *color_layout = new QHBoxLayout(color_row);
                    color_layout->setContentsMargins(3, edit ? 0 : 3, 3, 2);
                    color_layout->setSpacing(5);
                    auto update_chip = [](QToolButton *button, uint32_t argb) {
                        if (!button) return;
                        const QColor c((argb >> 16) & 0xFF, (argb >> 8) & 0xFF,
                                       argb & 0xFF, (argb >> 24) & 0xFF);
                        button->setStyleSheet(QStringLiteral(
                            "QToolButton{background:%1;border:1px solid palette(mid);border-radius:2px;}"
                            "QToolButton:hover{border-color:palette(highlight);}")
                            .arg(c.name(QColor::HexArgb)));
                    };
                    auto commit_color_change = [this, title](QToolButton *target, uint32_t argb,
                                                            bool stroke, const std::shared_ptr<Layer> &layer,
                                                            int cue_row, auto update_chip_fn) {
                        if (!title || !layer)
                            return;
                        set_live_text_cue_color_override(*title, *layer, cue_row, stroke, argb);
                        if (target)
                            update_chip_fn(target, argb);
                        const bool single_value_color = stroke
                            ? layer->exposed_stroke_single_value
                            : layer->exposed_fill_single_value;
                        if (title->current_cue_row >= 0 &&
                            (title->current_cue_row == cue_row || single_value_color))
                            apply_live_cue_layer_value(*title, layer, title->current_cue_row, layer->text_content);
                        TitleDataStore::instance().save();
                        TitleDataStore::instance().touch_runtime_change();
                        CacheManager::instance().refreshLiveCueStructureAsync(title);
                        TitleDataStore::instance().notify_change();
                    };
                    auto clear_color_change = [this, title](QToolButton *target, bool stroke,
                                                           const std::shared_ptr<Layer> &layer,
                                                           int cue_row, auto update_chip_fn) {
                        if (!title || !layer)
                            return;
                        clear_live_text_cue_color_override(*title, *layer, cue_row, stroke);
                        const uint32_t fallback = live_text_cue_effective_color(*title, *layer, cue_row, stroke);
                        if (target)
                            update_chip_fn(target, fallback);
                        const bool single_value_color = stroke
                            ? layer->exposed_stroke_single_value
                            : layer->exposed_fill_single_value;
                        if (title->current_cue_row >= 0 &&
                            (title->current_cue_row == cue_row || single_value_color))
                            apply_live_cue_layer_value(*title, layer, title->current_cue_row, layer->text_content);
                        TitleDataStore::instance().save();
                        TitleDataStore::instance().touch_runtime_change();
                        CacheManager::instance().refreshLiveCueStructureAsync(title);
                        TitleDataStore::instance().notify_change();
                    };
                    auto make_color_button = [&](const QString &label, bool stroke) {
                        auto *button = new QToolButton(color_row);
                        button->setText(QString());
                        button->setToolTip(label);
                        const int button_extent = obs_tool_button_extent(color_row);
                        button->setFixedSize(button_extent, button_extent);
                        update_chip(button, live_text_cue_effective_color(*title, *exposed[col], row, stroke));
                        connect(button, &QToolButton::clicked, this,
                                [this, title, layer = exposed[col], row, button, stroke,
                                 update_chip, commit_color_change]() {
                            if (!title || !layer) return;
                            const uint32_t current = live_text_cue_effective_color(*title, *layer, row, stroke);
                            QColor initial((current >> 16) & 0xFF, (current >> 8) & 0xFF,
                                           current & 0xFF, (current >> 24) & 0xFF);
                            QColor picked = QColorDialog::getColor(initial, this, stroke
                                ? QStringLiteral("Stroke color") : QStringLiteral("Fill color"),
                                QColorDialog::ShowAlphaChannel);
                            if (!picked.isValid()) return;
                            const uint32_t argb = (uint32_t(picked.alpha()) << 24) |
                                                  (uint32_t(picked.red()) << 16) |
                                                  (uint32_t(picked.green()) << 8) |
                                                   uint32_t(picked.blue());
                            commit_color_change(button, argb, stroke, layer, row, update_chip);
                        });
                        return button;
                    };
                    auto make_default_button = [&](const QString &tip, bool stroke, QToolButton *target) {
                        auto *button = new QToolButton(color_row);
                        button->setAutoRaise(true);
                        button->setText(QString());
                        button->setIcon(fxm_panel_defaults_icon(palette()));
                        const int button_extent = obs_tool_button_extent(color_row);
                        button->setToolTip(tip);
                        button->setFixedSize(button_extent, button_extent);
                        button->setStyleSheet(fxm_transform_panel_button_style(palette()));
                        connect(button, &QToolButton::clicked, this,
                                [title, layer = exposed[col], row, stroke, target,
                                 update_chip, clear_color_change]() {
                            clear_color_change(target, stroke, layer, row, update_chip);
                        });
                        return button;
                    };
                    auto add_color_control = [&](const QString &caption_text, bool stroke) {
                        auto *caption = new QLabel(caption_text, color_row);
                        caption->setStyleSheet(QStringLiteral("QLabel{color:palette(text);background:transparent;padding-left:2px;padding-right:2px;}"));
                        color_layout->addWidget(caption, 0, Qt::AlignVCenter);
                        auto *swatch = make_color_button(stroke ? QStringLiteral("Stroke color")
                                                                : QStringLiteral("Fill color"), stroke);
                        color_layout->addWidget(swatch, 0, Qt::AlignVCenter);
                        color_layout->addWidget(make_default_button(
                            stroke ? QStringLiteral("Reset Stroke to default")
                                   : QStringLiteral("Reset Fill to default"),
                            stroke, swatch), 0, Qt::AlignVCenter);
                    };
                    if (exposed[col]->expose_fill_color)
                        add_color_control(QStringLiteral("Fill:"), false);
                    if (exposed[col]->expose_stroke_color)
                        add_color_control(QStringLiteral("Stroke:"), true);
                    color_layout->addStretch(1);
                    cell_layout->addWidget(color_row, edit ? 0 : 1);
                    text_table_->setCellWidget(row, col + kLiveFirstValueColumn, cell);
                } else if (edit) {
                    text_table_->setCellWidget(row, col + kLiveFirstValueColumn, edit);
                }
                if (edit && exposed[col]->type != LayerType::Chart) {
                    configure_external_cell_binding(edit, row, col, exposed[col],
                        authored_cell_value);
                }

            }
        }

        const bool waiting_for_prerender = cache_waiting_title_id_ == QString::fromStdString(title->id) &&
            cache_waiting_cue_row_ == row;
        const bool preview = isPreviewReady() &&
            live_cue_preview_title_id_ == QString::fromStdString(title->id) &&
            live_cue_preview_row_ == row;
        auto *cue = live_cue_button(text_table_, row, kLiveCueColumn);
        if (cue) {
            cue->setEnabled(title_has_bound_host_source(title->id));
            cue->setToolTip(fxm_tr("OBSTitles.PlayCueTooltip"));
            const bool ending = live_cue_row_is_ending(*title, row);
            cue->setIcon(obs_icon("cue.svg", live_cue_state_color(
                row == title->current_cue_row,
                row == title->pending_cue_row || waiting_for_prerender || preview,
                ending && !(preview && row == title->current_cue_row))));
            cue->setStyleSheet(QStringLiteral(
                "QToolButton{background:transparent;border:none;padding:0;}"
                "QToolButton:hover{background:transparent;border:none;}"));
            connect(cue, &QToolButton::clicked, this, [this, row]() { cue_live_text_row(row, true); });
        }
        update_live_text_select_cell_status(row);
    }
    apply_live_text_row_heights();
    adjust_live_text_table_columns(first_width_fit_for_title);
    update_playlist_controls();
    update_persistence_controls();
    update_external_data_controls();
    update_live_text_select_all_state();
    enforce_live_text_cache_column_visibility();
}

void TitleDock::on_export_live_text_data()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;

    auto exposed = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed);

    QString path = QFileDialog::getSaveFileName(
        this, fxm_tr("OBSTitles.ExportData"), QString(),
        fxm_tr("OBSTitles.JsonFileFilter"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    QJsonArray columns;
    for (const auto &layer : exposed) {
        QJsonObject column;
        column.insert(QStringLiteral("id"), QString::fromStdString(layer->id));
        column.insert(QStringLiteral("name"), live_text_layer_header(layer));
        columns.append(column);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("Flux Motion live text cues"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("title"), QString::fromStdString(title->name));
    root.insert(QStringLiteral("columns"), columns);
    root.insert(QStringLiteral("rows"), live_text_rows_to_json(title->live_text_rows));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ExportData"),
                             fxm_tr("OBSTitles.ExportDataFailed"));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    status_lbl_->setText(fxm_tr("OBSTitles.ExportedStatusFormat").arg(QFileInfo(path).fileName()));
}

void TitleDock::on_import_live_text_data()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;

    const QString path = QFileDialog::getOpenFileName(
        this, fxm_tr("OBSTitles.ImportData"), QString(),
        fxm_tr("OBSTitles.JsonFileFilter"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportData"),
                             fxm_tr("OBSTitles.ImportDataFailed"));
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportData"), parse_error.errorString());
        return;
    }

    std::vector<std::vector<std::string>> imported_rows;
    QString error;
    if (!live_text_rows_from_json(doc, imported_rows, &error)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportData"), error);
        return;
    }

    auto exposed = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed);
    const int existing_rows = (int)title->live_text_rows.size();
    const int imported_row_count = (int)imported_rows.size();
    if (imported_row_count > existing_rows) {
        const auto answer = QMessageBox::question(
            this, fxm_tr("OBSTitles.ImportData"),
            fxm_tr("OBSTitles.ImportDataCropWarning")
                .arg(imported_row_count)
                .arg(existing_rows),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        imported_rows.resize(existing_rows);
    }

    for (auto &row : imported_rows)
        row.resize(exposed.size());
    title->live_text_rows = std::move(imported_rows);
    title->live_text_row_ids.clear();
    normalize_live_text_rows(title, exposed);
    if (title->current_cue_row >= 0)
        title->current_cue_row = std::min(
            title->current_cue_row,
            static_cast<int>(title->live_text_rows.size()) - 1);
    if (title->pending_cue_row >= 0)
        title->pending_cue_row = std::min(
            title->pending_cue_row,
            static_cast<int>(title->live_text_rows.size()) - 1);
    refresh_active_live_text_row_without_recue(title);
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    CacheManager::instance().refreshLiveCueStructure(title);
    populate_exposed_text();
    status_lbl_->setText(fxm_tr("OBSTitles.ImportedStatusFormat").arg(QFileInfo(path).fileName()));
}

void TitleDock::on_import_append_live_text_data()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;

    const QString path = QFileDialog::getOpenFileName(
        this, fxm_tr("OBSTitles.ImportAppendData"), QString(),
        fxm_tr("OBSTitles.JsonFileFilter"));
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportAppendData"),
                             fxm_tr("OBSTitles.ImportDataFailed"));
        return;
    }

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportAppendData"), parse_error.errorString());
        return;
    }

    std::vector<std::vector<std::string>> imported_rows;
    QString error;
    if (!live_text_rows_from_json(doc, imported_rows, &error)) {
        QMessageBox::warning(this, fxm_tr("OBSTitles.ImportAppendData"), error);
        return;
    }

    auto exposed = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed);
    for (auto &row : imported_rows) {
        row.resize(exposed.size());
        title->live_text_rows.push_back(std::move(row));
    }
    normalize_live_text_rows(title, exposed);
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    CacheManager::instance().refreshLiveCueStructure(title);
    populate_exposed_text();
    status_lbl_->setText(fxm_tr("OBSTitles.ImportedStatusFormat").arg(QFileInfo(path).fileName()));
}

void TitleDock::on_toggle_external_data_source()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;
    title->external_data_enabled = !title->external_data_enabled;
    refresh_active_live_text_row_without_recue(title);
    TitleDataStore::instance().save();
    TitleDataStore::instance().touch_runtime_change();
    seen_store_revision_ = TitleDataStore::instance().revision();
    update_external_data_controls();
}

void TitleDock::on_show_external_data_settings()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title)
        return;
    ExternalDataSettingsDialog dialog(title, this);
    if (dialog.exec() == QDialog::Accepted) {
        refresh_active_live_text_row_without_recue(title);
        TitleDataStore::instance().save();
        TitleDataStore::instance().touch_runtime_change();
        seen_store_revision_ = TitleDataStore::instance().revision();
    }
    update_external_data_controls();
    populate_exposed_text();
}

void TitleDock::on_refresh_external_data()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title)
        return;
    for (const auto &source : title->external_data_sources)
        ExternalDataProviderService::instance().refresh_source(source.id);
    status_lbl_->setText(fxm_tr("OBSTitles.ExternalDataRefreshed"));
}

void TitleDock::on_add_live_text_row()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title) return;
    auto exposed = exposed_text_layers(title);
    if (exposed.empty()) return;

    auto selected_rows = selected_live_text_rows();
    std::vector<std::string> row(exposed.size());
    if (selected_rows.size() == 1) {
        const int source_row = selected_rows.front();
        if (source_row >= 0 && source_row < (int)title->live_text_rows.size())
            row = title->live_text_rows[source_row];
    }
    row.resize(exposed.size());

    title->live_text_rows.push_back(std::move(row));
    title->live_text_row_ids.push_back(TitleDataStore::make_uuid());
    const int added_row = (int)title->live_text_rows.size() - 1;
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();

    /* Adding a row creates only the transition variants to/from that row.
     * Refresh all requirement sets so those new states are queued, while stable
     * content-addressed keys reuse every previously rendered frame. */
    CacheManager::instance().refreshLiveCueStructure(title);
    populate_exposed_text();
    apply_live_text_row_selection({added_row}, false);
    update_live_text_cache_cell(title, added_row);
}

void TitleDock::on_delete_live_text_rows()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title || !text_table_) return;

    auto rows = selected_live_text_rows();
    if (rows.empty()) return;

    updating_exposed_text_ = true;
    int next_row = rows.front();
    const bool had_current_cue = title->current_cue_row >= 0;
    const bool had_pending_cue = title->pending_cue_row >= 0;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        const int row = *it;
        if (row < 0 || row >= (int)title->live_text_rows.size())
            continue;
        title->live_text_rows.erase(title->live_text_rows.begin() + row);
        if (row < static_cast<int>(title->live_text_row_ids.size()))
            title->live_text_row_ids.erase(title->live_text_row_ids.begin() + row);
        if (title->current_cue_row == row)
            title->current_cue_row = -1;
        else if (title->current_cue_row > row)
            --title->current_cue_row;
        if (title->pending_cue_row == row)
            title->pending_cue_row = -1;
        else if (title->pending_cue_row > row)
            --title->pending_cue_row;
    }

    auto exposed_now = exposed_text_layers(title);
    normalize_live_text_rows(title, exposed_now);
    if (had_current_cue && title->current_cue_row < 0 &&
        !title->live_text_rows.empty())
        title->current_cue_row = std::min(
            next_row, static_cast<int>(title->live_text_rows.size()) - 1);
    if (had_pending_cue && title->pending_cue_row < 0 &&
        !title->live_text_rows.empty())
        title->pending_cue_row = std::min(
            next_row, static_cast<int>(title->live_text_rows.size()) - 1);
    refresh_active_live_text_row_without_recue(title);
    TitleDataStore::instance().save();
    TitleDataStore::instance().notify_change();
    CacheManager::instance().refreshLiveCueStructure(title);
    updating_exposed_text_ = false;
    populate_exposed_text();
    if (!title->live_text_rows.empty())
        text_table_->selectRow(std::min(next_row, (int)title->live_text_rows.size() - 1));
}

void TitleDock::on_move_live_text_row_up()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title || !text_table_) return;

    auto rows = selected_live_text_rows();
    if (rows.empty()) return;

    const bool restore_checked = has_checked_live_text_rows();
    const int row_count = (int)title->live_text_rows.size();
    std::vector<bool> selected(row_count, false);
    for (int row : rows) {
        if (row >= 0 && row < row_count)
            selected[row] = true;
    }

    std::vector<int> order(row_count);
    std::iota(order.begin(), order.end(), 0);
    bool moved = false;
    for (int visual = 1; visual < row_count; ++visual) {
        if (selected[order[visual]] && !selected[order[visual - 1]]) {
            std::swap(order[visual], order[visual - 1]);
            moved = true;
        }
    }
    if (!moved) return;

    ensure_live_text_row_ids(*title);
    std::vector<std::vector<std::string>> reordered;
    std::vector<std::string> reordered_ids;
    reordered.reserve(title->live_text_rows.size());
    reordered_ids.reserve(title->live_text_row_ids.size());
    std::vector<int> new_index(row_count, -1);
    for (int visual = 0; visual < row_count; ++visual) {
        new_index[order[visual]] = visual;
        reordered.push_back(std::move(title->live_text_rows[order[visual]]));
        reordered_ids.push_back(std::move(title->live_text_row_ids[order[visual]]));
    }
    title->live_text_rows = std::move(reordered);
    title->live_text_row_ids = std::move(reordered_ids);
    if (title->current_cue_row >= 0 && title->current_cue_row < row_count)
        title->current_cue_row = new_index[title->current_cue_row];
    if (title->pending_cue_row >= 0 && title->pending_cue_row < row_count)
        title->pending_cue_row = new_index[title->pending_cue_row];

    std::vector<int> moved_rows;
    for (int row : rows) {
        if (row >= 0 && row < row_count)
            moved_rows.push_back(new_index[row]);
    }
    std::sort(moved_rows.begin(), moved_rows.end());

    TitleDataStore::instance().save();
    TitleDataStore::instance().touch_runtime_change();
    CacheManager::instance().refreshLiveCueStructure(title);
    seen_store_revision_ = TitleDataStore::instance().revision();
    populate_exposed_text();
    apply_live_text_row_selection(moved_rows, restore_checked);
}

void TitleDock::on_move_live_text_row_down()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title || !text_table_) return;

    auto rows = selected_live_text_rows();
    if (rows.empty()) return;

    const bool restore_checked = has_checked_live_text_rows();
    const int row_count = (int)title->live_text_rows.size();
    std::vector<bool> selected(row_count, false);
    for (int row : rows) {
        if (row >= 0 && row < row_count)
            selected[row] = true;
    }

    std::vector<int> order(row_count);
    std::iota(order.begin(), order.end(), 0);
    bool moved = false;
    for (int visual = row_count - 2; visual >= 0; --visual) {
        if (selected[order[visual]] && !selected[order[visual + 1]]) {
            std::swap(order[visual], order[visual + 1]);
            moved = true;
        }
    }
    if (!moved) return;

    ensure_live_text_row_ids(*title);
    std::vector<std::vector<std::string>> reordered;
    std::vector<std::string> reordered_ids;
    reordered.reserve(title->live_text_rows.size());
    reordered_ids.reserve(title->live_text_row_ids.size());
    std::vector<int> new_index(row_count, -1);
    for (int visual = 0; visual < row_count; ++visual) {
        new_index[order[visual]] = visual;
        reordered.push_back(std::move(title->live_text_rows[order[visual]]));
        reordered_ids.push_back(std::move(title->live_text_row_ids[order[visual]]));
    }
    title->live_text_rows = std::move(reordered);
    title->live_text_row_ids = std::move(reordered_ids);
    if (title->current_cue_row >= 0 && title->current_cue_row < row_count)
        title->current_cue_row = new_index[title->current_cue_row];
    if (title->pending_cue_row >= 0 && title->pending_cue_row < row_count)
        title->pending_cue_row = new_index[title->pending_cue_row];

    std::vector<int> moved_rows;
    for (int row : rows) {
        if (row >= 0 && row < row_count)
            moved_rows.push_back(new_index[row]);
    }
    std::sort(moved_rows.begin(), moved_rows.end());

    TitleDataStore::instance().save();
    TitleDataStore::instance().touch_runtime_change();
    CacheManager::instance().refreshLiveCueStructure(title);
    seen_store_revision_ = TitleDataStore::instance().revision();
    populate_exposed_text();
    apply_live_text_row_selection(moved_rows, restore_checked);
}


void TitleDock::select_title(const std::string &id)
{
    populate_list();
    for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole).toString().toStdString() == id) {
            list_->setCurrentRow(i);
            break;
        }
    }
}

void TitleDock::on_map_external_table()
{
    auto title = TitleDataStore::instance().get_title(selected_id());
    if (!title)
        return;
    auto exposed = exposed_text_layers(title);
    if (exposed.empty()) {
        QMessageBox::information(
            this, QStringLiteral("External table mapping"),
            QStringLiteral("Expose at least one text, ticker, or image layer to Live Text Cues first."));
        return;
    }
    if (title->external_data_sources.empty()) {
        QMessageBox::information(
            this, QStringLiteral("External table mapping"),
            QStringLiteral("Create an external data source and refresh it before mapping a table."));
        return;
    }

    normalize_live_text_rows(title, exposed);
    const LiveTextTableBinding *existing = title->live_text_table_bindings.empty()
        ? nullptr : &title->live_text_table_bindings.front();
    ExternalDataTableMappingDialog dialog(title, existing, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    ExternalDataLog::write_lazy(
        ExternalDataLogLevel::Info, "Dock", [&]() {
            const LiveTextTableBinding mapping = dialog.mapping();
            std::ostringstream stream;
            stream << (dialog.remove_requested() ? "remove table mapping" : "apply table mapping")
                   << " title=" << ExternalDataLog::sanitize(title->id, 160)
                   << " id=" << ExternalDataLog::sanitize(mapping.id, 160)
                   << " source=" << ExternalDataLog::sanitize(mapping.source_id, 160)
                   << " table=" << ExternalDataLog::sanitize(mapping.table_path, 320)
                   << " columns=" << mapping.columns.size();
            return stream.str();
        });
    bool changed = false;
    if (dialog.remove_requested()) {
        if (existing)
            changed = remove_live_text_table_binding(*title, existing->id, true);
    } else {
        LiveTextTableBinding mapping = dialog.mapping();
        auto current = std::find_if(
            title->live_text_table_bindings.begin(),
            title->live_text_table_bindings.end(),
            [&mapping](const LiveTextTableBinding &candidate) {
                return candidate.id == mapping.id;
            });
        if (current == title->live_text_table_bindings.end())
            title->live_text_table_bindings.push_back(std::move(mapping));
        else
            *current = std::move(mapping);
        changed = true;
        changed = synchronize_live_text_table_bindings(*title) || changed;
    }

    if (!changed)
        return;
    refresh_active_live_text_row_without_recue(title);
    TitleDataStore::instance().save();
    TitleDataStore::instance().touch_runtime_change();
    CacheManager::instance().refreshLiveCueStructureAsync(title);
    seen_store_revision_ = TitleDataStore::instance().revision();
    populate_exposed_text();
    update_external_data_controls();
}


std::shared_ptr<Title> TitleDock::create_template_title(const std::string &name,
                                                         int template_id)
{
    auto title = TitleDataStore::instance().create_title(name);
    title->layers.clear();
    title->bg_color = 0x00000000;
    title->duration = 7.0;
