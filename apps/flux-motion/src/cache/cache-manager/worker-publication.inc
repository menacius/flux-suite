    const QString title_id = QString::fromStdString(title->id);
    if (live_cue_structure_refresh_in_progress_.contains(title_id)) {
        FXM_LOG_TRACE("LiveCue", QStringLiteral("Skipped re-entrant live cue structure refresh title=%1")
                                      .arg(title_id));
        return;
    }

    struct RefreshGuard {
        QSet<QString> &set;
        QString id;
        RefreshGuard(QSet<QString> &target, const QString &title_id) : set(target), id(title_id)
        {
            set.insert(id);
        }
        ~RefreshGuard()
        {
            set.remove(id);
        }
    } refresh_guard(live_cue_structure_refresh_in_progress_, title_id);

    ensure_live_text_row_ids(*title);
    const int row_count = static_cast<int>(title->live_text_rows.size());
    const auto exposed_layers = fxm::live_text::exposed_text_layers(title);
    QSet<QString> current_ids;
    QHash<QString, int> index_by_id;
    QHash<QString, QString> current_fingerprints;
    for (int row = 0; row < row_count; ++row) {
        const QString row_id = liveCueRowIdentity(title, row);
        current_ids.insert(row_id);
        index_by_id.insert(row_id, row);
        QCryptographicHash hash(QCryptographicHash::Sha1);
        for (const std::string &cell : title->live_text_rows[static_cast<size_t>(row)]) {
            const QByteArray bytes = QByteArray::fromStdString(cell);
            hash.addData(bytes);
            hash.addData("\0", 1);
        }
        /* Per-row Fill/Stroke controls affect rendered pixels even when the
         * text/image value stored in live_text_rows is unchanged. Include the
         * effective exposed cue colors in the row fingerprint so cache states
         * and cue-transition variants are invalidated when a row swatch changes
         * or when a single-value swatch changes all rows. */
        for (const auto &layer : exposed_layers) {
            if (!layer)
                continue;
            hash.addData("layer", 5);
            hash.addData(QByteArray::fromStdString(layer->id));
            hash.addData("\0", 1);
            if (layer->expose_fill_color) {
                hash.addData("fill", 4);
                hash.addData(QByteArray::number(
                    static_cast<qulonglong>(live_text_cue_effective_color(*title, *layer, row, false)), 16));
                hash.addData("\0", 1);
            }
            if (layer->expose_stroke_color) {
                hash.addData("stroke", 6);
                hash.addData(QByteArray::number(
                    static_cast<qulonglong>(live_text_cue_effective_color(*title, *layer, row, true)), 16));
                hash.addData("\0", 1);
            }
        }
        current_fingerprints.insert(row_id, QString::fromLatin1(hash.result().toHex()));
    }

    QCryptographicHash signature_hash(QCryptographicHash::Sha1);
    auto add_sig = [&](const QByteArray &value) { signature_hash.addData(value); signature_hash.addData("\0", 1); };
    add_sig(QByteArray::number(title->playback_mode));
    add_sig(QByteArray::number(title->cue_background_persistence));
    add_sig(QByteArray::number(title->cue_text_persistence));
    add_sig(QByteArray::number(title->pause_time, 'g', 17));
    add_sig(QByteArray::number(title->loop_start, 'g', 17));
    add_sig(QByteArray::number(title->loop_end, 'g', 17));
    add_sig(QByteArray::number(title->duration, 'g', 17));
    const QString transition_signature = QString::fromLatin1(signature_hash.result().toHex());

    const QSet<QString> previous_ids = live_cue_structure_row_ids_.value(title_id);
    const QHash<QString, QString> previous_fingerprints = live_cue_row_fingerprints_.value(title_id);
    const bool settings_changed = live_cue_transition_signatures_.value(title_id) != transition_signature;
    const bool first_structure_refresh = previous_ids.isEmpty();

    QSet<QString> added_ids = current_ids;
    for (const QString &id : previous_ids)
        added_ids.remove(id);
    QSet<QString> removed_ids = previous_ids;
    for (const QString &id : current_ids)
        removed_ids.remove(id);
    QSet<QString> changed_ids;
    for (const QString &id : current_ids) {
        if (previous_fingerprints.contains(id) && previous_fingerprints.value(id) != current_fingerprints.value(id))
            changed_ids.insert(id);
    }
    if (settings_changed && !previous_ids.isEmpty())
        changed_ids.unite(current_ids);

    QVector<QString> obsolete_states;
    auto state_involves_any = [&](const QString &state_key, const QSet<QString> &ids) {
        const QString destination_id = live_cue_row_ids_.value(state_key);
        for (const QString &id : ids) {
            if (destination_id == id ||
                state_key.contains(QStringLiteral(":%1:").arg(id)) ||
                state_key.endsWith(QStringLiteral(":%1").arg(id)))
                return true;
        }
        return false;
    };

    for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
        if (it.value() != title_id)
            continue;
        const QString state_key = it.key();
        if (state_involves_any(state_key, removed_ids) ||
            state_involves_any(state_key, changed_ids)) {
            obsolete_states.push_back(state_key);
            continue;
        }
        const QString destination_id = live_cue_row_ids_.value(state_key);
        if (index_by_id.contains(destination_id))
            live_cue_rows_[state_key] = index_by_id.value(destination_id);
    }
    for (const QString &state_key : obsolete_states)
        removeLiveCueState(state_key);

    /* Cancel obsolete queued work and evict orphan RAM before registering new
     * states. Doing this afterwards allowed an old job with the same content
     * key to block the replacement job, then finish against a deleted row. */
    pruneUnreferencedLiveCueRam(title_id);

    QSet<QString> affected_ids = added_ids;
    affected_ids.unite(changed_ids);
    if (first_structure_refresh)
        affected_ids = current_ids;

    if (!first_structure_refresh) {
        for (const QString &row_id : affected_ids) {
            if (!index_by_id.contains(row_id))
                continue;
            queueLiveCue(title, index_by_id.value(row_id));
        }

        /* Transition states are independent from row-ready states. Adding a row
         * creates only the new from/to pairs involving that row; existing rows stay
         * ready and keep their cache icons. Editing a row rebuilds only pairs that
         * actually include that stable row identity. */
        for (const QString &affected_id : affected_ids) {
            if (!index_by_id.contains(affected_id))
                continue;
            const int affected_row = index_by_id.value(affected_id);
            for (int other = 0; other < row_count; ++other) {
                if (other == affected_row)
                    continue;
                queueLiveCueTransition(title, affected_row, other);
                queueLiveCueTransition(title, other, affected_row);
            }
        }
    }

    live_cue_structure_row_ids_[title_id] = current_ids;
    live_cue_row_fingerprints_[title_id] = current_fingerprints;
    live_cue_transition_signatures_[title_id] = transition_signature;
    pruneUnreferencedLiveCueRam(title_id);
    FXM_LOG_INFO("LiveCue", QStringLiteral("Refreshed live cue structure title=%1 rows=%2 added=%3 changed=%4 removed=%5 removedStates=%6")
                              .arg(title_id).arg(row_count)
                              .arg(static_cast<int>(added_ids.size()))
                              .arg(static_cast<int>(changed_ids.size()))
                              .arg(static_cast<int>(removed_ids.size()))
                              .arg(static_cast<int>(obsolete_states.size())));
    scheduleDiagnosticsChanged();
}

void CacheManager::refreshLiveCueStructureAsync(const std::shared_ptr<Title> &title)
{
    if (!title)
        return;
    auto snapshot = clone_title_snapshot(title);
    if (!snapshot)
        return;
    const QString title_id = QString::fromStdString(snapshot->id);
    {
        std::lock_guard<std::mutex> lock(live_cue_refresh_mutex_);
        pending_live_cue_structure_refreshes_[title_id] = std::move(snapshot);
    }
    wakeWorker();
}

bool CacheManager::takePendingLiveCueStructureRefresh(std::shared_ptr<Title> &title)
{
    std::lock_guard<std::mutex> lock(live_cue_refresh_mutex_);
    if (pending_live_cue_structure_refreshes_.isEmpty())
        return false;
    auto it = pending_live_cue_structure_refreshes_.begin();
    title = it.value();
    pending_live_cue_structure_refreshes_.erase(it);
    return title != nullptr;
}

void CacheManager::setLiveCueRowRenderPaused(const std::shared_ptr<Title> &title, int row, bool paused)
{
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return;

    const QString title_id = QString::fromStdString(title->id);
    const QString row_id = liveCueRowIdentity(title, row);
    if (row_id.isEmpty())
        return;

    QVector<QString> affected_states;
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        QSet<QString> &paused_rows = live_cue_paused_row_ids_[title_id];
        const bool changed = paused ? !paused_rows.contains(row_id)
                                    : paused_rows.contains(row_id);
        if (!changed)
            return;

        if (paused)
            paused_rows.insert(row_id);
        else {
            paused_rows.remove(row_id);
            if (paused_rows.isEmpty())
                live_cue_paused_row_ids_.remove(title_id);
        }

        for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
            if (it.value() != title_id)
                continue;
            if (liveCueStateRenderPausedLocked(it.key()))
                affected_states.push_back(it.key());
        }

        if (paused) {
            for (const QString &state_key : affected_states) {
                for (const CacheFrameKey &key : live_cue_required_keys_.value(state_key))
                    queue_.cancelKey(key);
                const FrameCacheState state = liveCueStoredState(state_key);
                live_cue_states_[state_key] = state;
                live_cue_progress_percent_[state_key] = liveCueStoredProgress(state_key);
                live_cue_last_emit_states_.remove(state_key);
                live_cue_last_emit_buckets_.remove(state_key);
            }
        }
    }

    FXM_LOG_DEBUG("LiveCue", QStringLiteral("%1 live cue row render title=%2 row=%3 rowId=%4 states=%5")
                                .arg(paused ? QStringLiteral("Paused") : QStringLiteral("Resumed"))
                                .arg(title_id).arg(row).arg(row_id)
                                .arg(static_cast<int>(affected_states.size())));
    scheduleLiveCueStateChanged(title_id, row);
    scheduleDiagnosticsChanged();

    if (!paused) {
        refreshLiveCueStructure(title);
        /* refreshLiveCueStructure() may already have accepted the edited row's
         * new fingerprint while the row was paused. In that case it sees no new
         * change here and would not requeue the jobs that focus-in cancelled.
         * Explicitly restore the steady row and every transition involving it. */
        queueLiveCue(title, row);
        for (int other = 0; other < static_cast<int>(title->live_text_rows.size()); ++other) {
            if (other == row)
                continue;
            queueLiveCueTransition(title, row, other);
            queueLiveCueTransition(title, other, row);
        }
        wakeWorker();
    }
}

void CacheManager::cancelTitleWork(const QString &title_id)
{
    if (title_id.isEmpty())
        return;
    {
        std::lock_guard<std::mutex> refresh_lock(live_cue_refresh_mutex_);
        pending_live_cue_structure_refreshes_.remove(title_id);
    }
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        bumpTitleGeneration(title_id);
        queue_.cancelTitle(title_id);
        disk_cache_.cancelTitleWrites(title_id);
    }
    resetCancelledWorkState(title_id);
    wakeWorker();
    FXM_LOG_DEBUG("Cache", QStringLiteral("Cancelled pending/in-flight title cache work title=%1").arg(title_id));
}

void CacheManager::removeTitleCache(const QString &title_id, bool remove_disk)
{
    std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (title_id.isEmpty())
        return;
    {
        std::lock_guard<std::mutex> refresh_lock(live_cue_refresh_mutex_);
        pending_live_cue_structure_refreshes_.remove(title_id);
    }
    bumpTitleGeneration(title_id);
    queue_.cancelTitle(title_id);
    disk_cache_.cancelTitleWrites(title_id);

    QVector<QString> state_keys;
    for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
        if (it.value() == title_id)
            state_keys.push_back(it.key());
    }
    for (const QString &state_key : state_keys)
        removeLiveCueState(state_key);

    title_gpu_frame_cache_remove_title(title_id.toStdString());
    const auto ram_keys = ram_cache_.keysForTitle(title_id);
    for (const CacheFrameKey &key : ram_keys) {
        ram_cache_.remove(key);
        title_gpu_frame_cache_remove(key.toString().toStdString());
    }
    if (remove_disk) {
        const auto disk_keys = disk_cache_.keysForTitle(title_id);
        for (const CacheFrameKey &key : disk_keys)
            disk_cache_.remove(key);
    }
    for (auto it = live_cue_known_keys_.begin(); it != live_cue_known_keys_.end();) {
        if ((*it).title_id == title_id)
            it = live_cue_known_keys_.erase(it);
        else
            ++it;
    }
    {
        QMutexLocker dedup_lock(&temporal_dedup_mutex_);
        for (auto it = temporal_canonical_keys_.begin();
             it != temporal_canonical_keys_.end();) {
            if (it.value().title_id == title_id)
                it = temporal_canonical_keys_.erase(it);
            else
                ++it;
        }
        for (auto it = adaptive_canonical_keys_.begin();
             it != adaptive_canonical_keys_.end();) {
            if (it.value().title_id == title_id)
                it = adaptive_canonical_keys_.erase(it);
            else
                ++it;
        }
    }
    {
        std::lock_guard<std::mutex> snapshot_lock(snapshot_mutex_);
        const QString prefix = title_id + QLatin1Char('|');
        for (auto it = job_snapshots_.begin(); it != job_snapshots_.end();) {
            if (it.key().startsWith(prefix))
                it = job_snapshots_.erase(it);
            else
                ++it;
        }
    }
    {
        QMutexLocker dirty_lock(&dirty_tiles_mutex_);
        const QString prefix = title_id + QLatin1Char(':');
        for (auto it = dirty_tiles_by_frame_.begin();
             it != dirty_tiles_by_frame_.end();) {
            if (it.key().startsWith(prefix))
                it = dirty_tiles_by_frame_.erase(it);
            else
                ++it;
        }
    }
    {
        QMutexLocker visual_lock(&visual_hash_mutex_);
        last_visual_hash_by_title_.remove(title_id);
        last_layer_rects_by_title_.remove(title_id);
    }
    state_tracker_.clearTitle(title_id);
    live_cue_structure_row_ids_.remove(title_id);
    live_cue_row_fingerprints_.remove(title_id);
    live_cue_transition_signatures_.remove(title_id);
    live_cue_paused_row_ids_.remove(title_id);
    FXM_LOG_INFO("Cache", QStringLiteral("Removed title cache title=%1 ramFrames=%2 states=%3 disk=%4")
                            .arg(title_id).arg(static_cast<int>(ram_keys.size()))
                            .arg(static_cast<int>(state_keys.size())).arg(remove_disk));
    scheduleDiagnosticsChanged();
}


CachePlaybackSettings CacheManager::playbackSettings() const
{
    QMutexLocker lock(&playback_settings_mutex_);
    return playback_settings_;
}

LiveCueCacheStats CacheManager::liveCueStats() const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    return live_cue_stats_;
}

void CacheManager::setPlaybackSettings(const CachePlaybackSettings &settings)
{
    {
        QMutexLocker lock(&playback_settings_mutex_);
        playback_settings_ = settings;
    }
    emit playbackSettingsChanged();
}

void CacheManager::abandonJobState(const RenderQueueManager::Job &job,
                                         const QString &live_state_key)
{
    if (job.live_cue) {
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        const QString state_key = liveCueStateReferencingKey(job.key, live_state_key);
        if (state_key.isEmpty() ||
            live_cue_states_.value(state_key, FrameCacheState::NotCached) != FrameCacheState::Rendering)
            return;

        bool replacement_queued = false;
        for (const CacheFrameKey &required : live_cue_required_keys_.value(state_key)) {
            if (queue_.contains(required)) {
                replacement_queued = true;
                break;
            }
        }
        const FrameCacheState next_state = replacement_queued
            ? FrameCacheState::Queued
            : FrameCacheState::NotCached;
        const int progress = liveCueStoredProgress(state_key);
        const int signal_row = live_cue_rows_.value(state_key, job.cue_row);
        live_cue_states_[state_key] = next_state;
        live_cue_progress_percent_[state_key] = progress;
        live_cue_last_emit_states_.remove(state_key);
        live_cue_last_emit_buckets_.remove(state_key);
        if (signal_row >= 0)
            scheduleLiveCueStateChanged(job.key.title_id, signal_row);
        return;
    }

    if (state_tracker_.state(job.key) != FrameCacheState::Rendering)
        return;
    state_tracker_.setState(job.key, queue_.contains(job.key)
        ? FrameCacheState::Queued
        : FrameCacheState::NotCached);
}

bool CacheManager::hasPendingGpuReadbacks() const
{
    return worker_pipeline_ && !worker_pipeline_->pending_readbacks.empty();
}

void CacheManager::workerLoop()
{
    enter_cache_worker_background_mode();

    while (!worker_stop_.load()) {
        {
            std::unique_lock<std::mutex> lock(worker_wait_mutex_);
            worker_cv_.wait(lock, [this]() {
                if (worker_stop_.load())
                    return true;
                if (hasPendingGpuReadbacks())
                    return true;
                if (!cache_enabled_.load())
                    return false;
                {
                    std::lock_guard<std::mutex> refresh_lock(live_cue_refresh_mutex_);
                    if (!pending_live_cue_structure_refreshes_.isEmpty())
                        return true;
                }
                const bool urgent_only =
                    paused_.load() || interactive_bypass_.load();
                if (urgent_only)
                    return queue_.hasAvailableJob(true);
                bool editor_focus_active = false;
                QString editor_title_id;
                {
                    QMutexLocker focus_lock(&editor_focus_mutex_);
                    editor_focus_active = editor_focus_active_;
                    editor_title_id = editor_focus_title_id_;
                }
                if (editor_focus_active)
                    return queue_.hasAvailableJob(true) ||
                           queue_.hasAvailableJobForTitle(editor_title_id);
                return queue_.hasAvailableJob(false);
            });
        }
        if (worker_stop_.load())
            break;
        if (!cache_enabled_.load()) {
            if (hasPendingGpuReadbacks())
                resolveOldestGpuReadback(true);
            continue;
        }

        std::shared_ptr<Title> refresh_title;
        if (takePendingLiveCueStructureRefresh(refresh_title)) {
            refreshLiveCueStructure(refresh_title);
            continue;
        }

        RenderQueueManager::Job job;
        const bool urgent_only = paused_.load() || interactive_bypass_.load();
        bool editor_focus_active = false;
        QString editor_title_id;
        {
            QMutexLocker lock(&editor_focus_mutex_);
            editor_focus_active = editor_focus_active_;
            editor_title_id = editor_focus_title_id_;
        }

        bool have_job = false;
        if (urgent_only) {
            have_job = queue_.takeNextUrgent(job);
        } else if (editor_focus_active) {
            have_job = queue_.takeNextUrgent(job);
            if (!have_job)
                have_job = queue_.takeNextForTitle(editor_title_id, job);
        } else {
            /* OBS/live playback requests preempt background prerender work even
             * when the editor is closed. The urgent lane is extracted without
             * resorting the complete background queue. */
            have_job = queue_.takeNextUrgent(job);
            if (!have_job)
                have_job = queue_.takeNext(job);
        }

        if (!have_job) {
            /* No more GPU work is available. Resolve the oldest submitted
             * staging surface rather than sleeping with an incomplete frame. */
            if (hasPendingGpuReadbacks())
                resolveOldestGpuReadback(true);
            continue;
        }

        const bool readback_submitted = renderJob(job);
        if (!readback_submitted)
            queue_.complete(job);

        /* Keep up to three GPU copies in flight. Mapping starts only when the
         * ring is full, allowing frame N readback to overlap rendering N+1/N+2. */
        if (worker_pipeline_->pending_readbacks.size() >= 3)
            resolveOldestGpuReadback(true);

        /* Interactive bypass/editor-focus selection already suspends unrelated
         * background work. Do not add per-frame live-cue sleeps: they made a
         * nominal 50-frame cue take roughly one extra second to prerender. */
        const int cooldown_ms = (job.urgent || job.realtime) ? 0 : 1;
        if (cooldown_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(cooldown_ms));
    }

    while (hasPendingGpuReadbacks())
        resolveOldestGpuReadback(true);
    disk_cache_.flushWrites();
}

bool CacheManager::retryFailedJob(RenderQueueManager::Job job,
                                  const QString &live_state_key,
                                  const char *stage)
{
    constexpr int kMaximumGpuReadbackRetries = 4;
    if (worker_stop_.load() || !cache_enabled_.load() ||
        !jobIsCurrent(job) || job.retry_count >= kMaximumGpuReadbackRetries)
        return false;

    /* The queue suppresses duplicate keys while a job is active. Release the
     * active token before re-enqueuing the exact same content-addressed frame. */
    queue_.complete(job);
    ++job.retry_count;

    if (job.live_cue) {
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        const QString state_key = liveCueStateReferencingKey(job.key, live_state_key);
        if (!state_key.isEmpty()) {
            job.cue_state_key = state_key;
            job.cue_row = live_cue_rows_.value(state_key, job.cue_row);
            live_cue_states_[state_key] = FrameCacheState::Queued;
            live_cue_progress_percent_[state_key] = liveCueStoredProgress(state_key);
            live_cue_last_emit_states_.remove(state_key);
            live_cue_last_emit_buckets_.remove(state_key);
            if (job.cue_row >= 0)
                scheduleLiveCueStateChanged(job.key.title_id, job.cue_row);
        }
    } else {
        state_tracker_.setState(job.key, FrameCacheState::Queued);
    }

    const bool queued = queue_.enqueue(job);
    if (!queued) {
        /* Another producer can fill the small gap between complete() and
         * enqueue(). In that case the retry has already been coalesced into a
         * replacement job and must stay Queued instead of being mislabeled as
         * Stale. If the queue is shutting down, consume the released active
         * token here and publish a terminal state so the caller does not
         * complete the same token twice. */
        const bool replacement_queued = queue_.contains(job.key);
        if (!replacement_queued) {
            if (job.live_cue) {
                std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
                const QString state_key = liveCueStateReferencingKey(job.key, live_state_key);
                if (!state_key.isEmpty()) {
                    const int signal_row = live_cue_rows_.value(state_key, job.cue_row);
                    live_cue_states_[state_key] = FrameCacheState::NotCached;
                    live_cue_progress_percent_[state_key] = liveCueStoredProgress(state_key);
                    live_cue_last_emit_states_.remove(state_key);
                    live_cue_last_emit_buckets_.remove(state_key);
                    if (signal_row >= 0)
                        scheduleLiveCueStateChanged(job.key.title_id, signal_row);
                }
            }
            state_tracker_.setState(job.key, FrameCacheState::Stale);
            FXM_LOG_ERROR("CacheQueue", QStringLiteral(
                "stage=%1 action=retry-enqueue-failed title=%2 frame=%3 attempt=%4 key=%5")
                .arg(QString::fromUtf8(stage ? stage : "gpu-readback"))
                .arg(job.key.title_id)
                .arg(job.key.frame)
                .arg(job.retry_count)
                .arg(job.key.toString()));
        } else {
            FXM_LOG_DEBUG("CacheQueue", QStringLiteral(
                "stage=%1 action=retry-coalesced title=%2 frame=%3 attempt=%4 key=%5")
                .arg(QString::fromUtf8(stage ? stage : "gpu-readback"))
                .arg(job.key.title_id)
                .arg(job.key.frame)
                .arg(job.retry_count)
                .arg(job.key.toString()));
            wakeWorker();
        }
        return true;
    }

    FXM_LOG_WARNING("CacheQueue", QStringLiteral(
        "stage=%1 action=retry title=%2 frame=%3 attempt=%4/%5 key=%6")
        .arg(QString::fromUtf8(stage ? stage : "gpu-readback"))
        .arg(job.key.title_id)
        .arg(job.key.frame)
        .arg(job.retry_count)
        .arg(kMaximumGpuReadbackRetries)
        .arg(job.key.toString()));
    wakeWorker();
    return true;
}

bool CacheManager::renderJob(RenderQueueManager::Job job)
{
    if (!job.title || !jobIsCurrent(job))
        return false;
    fxm::perf::BackgroundJobScope performance_job_scope;
    fxm::perf::ScopedTimer performance_render_timer(
        fxm::perf::Counter::CacheRenderNanoseconds);
    fxm::perf::add(fxm::perf::Counter::CacheRenderJobs);

    QString live_state_key = job.cue_state_key;
    bool was_stale = false;
    if (job.live_cue) {
        std::lock_guard<std::recursive_mutex> lock(live_cue_mutex_);
        const QString rebound_state = liveCueStateReferencingKey(job.key, live_state_key);
        if (rebound_state.isEmpty())
            return false;
        if (liveCueStateRenderPausedLocked(rebound_state))
            return false;
        live_state_key = rebound_state;
        job.cue_state_key = rebound_state;
        job.cue_row = live_cue_rows_.value(rebound_state, job.cue_row);
        was_stale = job.force_render ||
            live_cue_states_.value(rebound_state, FrameCacheState::NotCached) ==
                FrameCacheState::Stale;
        if (live_cue_states_.value(rebound_state, FrameCacheState::NotCached) !=
            FrameCacheState::Rendering) {
            live_cue_states_[rebound_state] = FrameCacheState::Rendering;
            live_cue_progress_percent_[rebound_state] = liveCueStoredProgress(rebound_state);
            if (shouldEmitLiveCueUpdate(
                    rebound_state, FrameCacheState::Rendering,
                    live_cue_progress_percent_.value(rebound_state)))
                scheduleLiveCueStateChanged(job.key.title_id, job.cue_row);
        }
    } else {
        was_stale = job.force_render ||
            state_tracker_.state(job.key) == FrameCacheState::Stale;
        state_tracker_.setState(job.key, FrameCacheState::Rendering);
    }

    const bool has_animated_asset = std::any_of(
        job.title->layers.cbegin(), job.title->layers.cend(),
        [](const std::shared_ptr<Layer> &layer) {
            return layer && layer->type == LayerType::Asset &&
                   layer->asset_animated;
        });
    const bool temporal_dedup_allowed =
        !job.live_cue && !job.realtime && !has_animated_asset &&
        titleCacheability(job.title) == TitleCacheability::Cacheable;
    const QString temporal_state_key = temporal_dedup_allowed
        ? temporalStateKey(
              job.key, evaluatedVisualStateHash(
                           *job.title, job.time, job.key.content_hash))
        : QString();

    /* A CPU/disk-resident frame does not need to be rerendered merely to become
     * a GPU token. Hydrate its sparse tiled payload on the cache worker and
     * publish it into the shared GPU tile cache. This also prevents each
     * live/editor session from accumulating its own 2D fallback textures after
     * an application restart. */
    if (!was_stale) {
        QImage resident_image;
        const bool found_in_ram = ram_cache_.get(job.key, resident_image);
        const bool found_on_disk = !found_in_ram &&
            disk_cache_.get(job.key, resident_image);
        const bool found_resident = found_in_ram || found_on_disk;
        if (found_on_disk)
            ram_cache_.put(job.key, resident_image);
        if (found_resident &&
            title_gpu_frame_cache_store_image(
                job.key.toString().toStdString(), resident_image,
                static_cast<uint32_t>(std::max(1, job.key.width)),
                static_cast<uint32_t>(std::max(1, job.key.height)))) {
            std::unique_lock<std::recursive_mutex> publication_lock(
                publication_mutex_);
            if (!jobIsCurrent(job)) {
                publication_lock.unlock();
                title_gpu_frame_cache_remove(
                    job.key.toString().toStdString());
                abandonJobState(job, live_state_key);
                return false;
            }

            state_tracker_.setState(job.key, FrameCacheState::CachedRam);
            if (!temporal_state_key.isEmpty()) {
                QMutexLocker dedup_lock(&temporal_dedup_mutex_);
                if (!temporal_canonical_keys_.contains(temporal_state_key))
                    temporal_canonical_keys_[temporal_state_key] = job.key;
            }
            if (job.live_cue) {
                std::lock_guard<std::recursive_mutex> live_lock(
                    live_cue_mutex_);
                live_cue_known_keys_.insert(job.key);
                live_cue_states_[live_state_key] =
                    FrameCacheState::NotCached;
                const FrameCacheState next_state =
                    liveCueStoredState(live_state_key);
                const int progress = liveCueStoredProgress(live_state_key);
                live_cue_states_[live_state_key] = next_state;
                live_cue_progress_percent_[live_state_key] = progress;
                if (job.cue_row >= 0 && shouldEmitLiveCueUpdate(
                        live_state_key, next_state, progress))
                    scheduleLiveCueStateChanged(job.key.title_id,
                                             job.cue_row);
            } else {
                scheduleFrameReady(job.key.title_id, job.key.frame);
            }
            publication_lock.unlock();
            scheduleDiagnosticsChanged();
            return false;
        }
    }

    /* Equal evaluated renderer input must not enter the GPU render/readback
     * pipeline again. If the first sample of this state is still in flight,
     * resolve it now so the following samples can alias its shared GPU/disk
     * tiles instead of producing up to three redundant readbacks. */
    if (!temporal_state_key.isEmpty()) {
        bool matching_readback_pending = true;
        while (matching_readback_pending && hasPendingGpuReadbacks()) {
            matching_readback_pending = std::any_of(
                worker_pipeline_->pending_readbacks.cbegin(),
                worker_pipeline_->pending_readbacks.cend(),
                [&](const WorkerPipeline::PendingReadback &pending) {
                    return pending.temporal_state_key == temporal_state_key;
                });
            if (matching_readback_pending)
                resolveOldestGpuReadback(true);
        }

        CacheFrameKey canonical_key;
        bool have_canonical = false;
        {
            QMutexLocker dedup_lock(&temporal_dedup_mutex_);
            const auto canonical =
                temporal_canonical_keys_.constFind(temporal_state_key);
            if (canonical != temporal_canonical_keys_.cend()) {
                canonical_key = canonical.value();
                have_canonical = canonical_key == job.key ||
                    canonical_key.content_hash == job.key.content_hash;
            }
        }

        if (have_canonical && !(canonical_key == job.key)) {
            const std::string alias_token =
                job.key.toString().toStdString();
            const std::string canonical_token =
                canonical_key.toString().toStdString();
            bool aliased = title_gpu_frame_cache_alias(
                alias_token, canonical_token);

            /* A canonical GPU entry may have been evicted while its SSD frame
             * remains valid. Hydrate the canonical sparse payload directly
             * under the alias token; this is still far cheaper than running
             * layout, effects, compositing and GPU readback again. */
            if (!aliased) {
                QImage canonical_image;
                if ((ram_cache_.get(canonical_key, canonical_image) ||
                     disk_cache_.get(canonical_key, canonical_image)) &&
                    title_gpu_frame_cache_store_image(
                        alias_token, canonical_image,
                        static_cast<uint32_t>(std::max(1, job.key.width)),
                        static_cast<uint32_t>(std::max(1, job.key.height))))
                    aliased = true;
            }

            if (aliased) {
                disk_cache_.enqueueAlias(job.key, canonical_key);
                std::unique_lock<std::recursive_mutex> publication_lock(
                    publication_mutex_);
                if (!jobIsCurrent(job)) {
                    publication_lock.unlock();
                    title_gpu_frame_cache_remove(alias_token);
                    abandonJobState(job, live_state_key);
                    return false;
                }
                state_tracker_.setState(job.key,
                                        FrameCacheState::CachedRam);
                scheduleFrameReady(job.key.title_id, job.key.frame);
                publication_lock.unlock();
                FXM_LOG_TRACE("Prerender", QStringLiteral(
                    "Reused temporal visual state title=%1 frame=%2 canonicalFrame=%3")
                    .arg(job.key.title_id).arg(job.key.frame)
                    .arg(canonical_key.frame));
                scheduleDiagnosticsChanged();
                return false;
            }

            /* The canonical payload was cleared/evicted from every tier.
             * Remove only the still-matching stale index entry and let this
             * frame become the replacement canonical after a normal render. */
            QMutexLocker dedup_lock(&temporal_dedup_mutex_);
            const auto current =
                temporal_canonical_keys_.find(temporal_state_key);
            if (current != temporal_canonical_keys_.end() &&
                current.value() == canonical_key)
                temporal_canonical_keys_.erase(current);
        }
    }

    QImage previous;
    bool had_previous = ram_cache_.get(job.key, previous);
    if (!had_previous)
        had_previous = disk_cache_.get(job.key, previous);

    /* A visual edit produces a new content-addressed key. Looking up only the
     * new key made dirty-region regeneration degenerate into a full readback,
     * because the previous pixels still live under the last published visual
     * hash. Resolve that exact predecessor before deciding whether a GPU patch
     * is worthwhile. Live-cue variants keep their independent state keys and
     * conservatively use the full-frame path. */
    if (!had_previous && was_stale && !job.live_cue) {
        QString previous_hash;
        {
            QMutexLocker lock(&visual_hash_mutex_);
            previous_hash = last_visual_hash_by_title_.value(job.key.title_id);
        }
        if (!previous_hash.isEmpty() &&
            previous_hash != job.key.content_hash) {
            CacheFrameKey previous_key = job.key;
            previous_key.content_hash = previous_hash;
            had_previous = ram_cache_.get(previous_key, previous);
            if (!had_previous)
                had_previous = disk_cache_.get(previous_key, previous);
        }
    }

    QRect readback_region(0, 0, std::max(1, job.key.width),
                          std::max(1, job.key.height));
    if (was_stale && had_previous &&
        !title_requires_full_gpu_cache_frame(*job.title)) {
        const QVector<CacheTileRegion> dirty_tiles = dirtyTilesForKey(job.key);
        QRect dirty_union;
        for (const auto &tile : dirty_tiles)
            dirty_union = dirty_union.united(tile.rect);
        const qint64 full_pixels = qint64(readback_region.width()) *
                                   qint64(readback_region.height());
        const qint64 dirty_pixels = qint64(dirty_union.width()) *
                                    qint64(dirty_union.height());
        /* A bounded dirty union is staged as a GPU crop. Large or fragmented
         * updates are cheaper and safer as one full-frame staging copy. */
        if (!dirty_union.isEmpty() && full_pixels > 0 &&
            dirty_pixels * 100 < full_pixels * 60) {
            readback_region = dirty_union.intersected(readback_region);
        }
    }

    while (worker_pipeline_->pending_readbacks.size() >= 3)
        resolveOldestGpuReadback(true);

    TitleGpuReadbackTicket ticket;
    bool submitted = render_title_gpu_cache_submit_readback(
        *job.title, job.time, gpu_cache_model_revision(job.key),
        job.key.toString().toStdString(), readback_region, ticket);
    if (!submitted && hasPendingGpuReadbacks()) {
        resolveOldestGpuReadback(true);
        submitted = render_title_gpu_cache_submit_readback(
            *job.title, job.time, gpu_cache_model_revision(job.key),
            job.key.toString().toStdString(), readback_region, ticket);
    }
    if (!submitted) {
        if (retryFailedJob(job, live_state_key, "submit"))
            return true; // active token was completed and the job was requeued
        abandonJobState(job, live_state_key);
        state_tracker_.setState(job.key, FrameCacheState::Stale);
        FXM_LOG_ERROR("Prerender", QStringLiteral(
            "stage=submit action=failed-final title=%1 frame=%2 retries=%3")
            .arg(job.key.title_id).arg(job.key.frame).arg(job.retry_count));
        return false;
    }

    WorkerPipeline::PendingReadback pending;
    pending.job = std::move(job);
    pending.ticket = ticket;
    pending.previous = std::move(previous);
    pending.was_stale = was_stale;
    pending.had_previous = had_previous;
    pending.live_state_key = live_state_key;
    pending.temporal_state_key = temporal_state_key;
    worker_pipeline_->pending_readbacks.push_back(std::move(pending));
    return true;
}

bool CacheManager::resolveOldestGpuReadback(bool force)
{
    Q_UNUSED(force);
    if (!hasPendingGpuReadbacks())
        return false;
    fxm::perf::ScopedTimer performance_readback_timer(
        fxm::perf::Counter::GpuReadbackNanoseconds);
    fxm::perf::add(fxm::perf::Counter::GpuReadbackJobs);

    WorkerPipeline::PendingReadback pending =
        std::move(worker_pipeline_->pending_readbacks.front());
    worker_pipeline_->pending_readbacks.pop_front();

    RenderQueueManager::Job job = std::move(pending.job);
    if (!jobIsCurrent(job)) {
        title_gpu_render_session_cancel_readback(pending.ticket);
        queue_.complete(job);
        abandonJobState(job, pending.live_state_key);
        return false;
    }

    fxm::perf::BackgroundJobScope performance_job_scope;
    QImage staged;
    const bool resolved = title_gpu_render_session_resolve_readback(
        pending.ticket, staged);
    struct QueueCompletionGuard {
        RenderQueueManager &queue;
        RenderQueueManager::Job &job;
        bool armed = true;
        ~QueueCompletionGuard() { if (armed) queue.complete(job); }
        void disarm() { armed = false; }
    } completion_guard {queue_, job};
    QString live_state_key = pending.live_state_key;

    if (!resolved || staged.isNull()) {
        title_gpu_frame_cache_remove(job.key.toString().toStdString());
        if (retryFailedJob(job, live_state_key, "resolve")) {
            completion_guard.disarm();
            scheduleDiagnosticsChanged();
            return false;
        }
        abandonJobState(job, live_state_key);
        state_tracker_.setState(job.key, FrameCacheState::Stale);
        FXM_LOG_ERROR("Prerender", QStringLiteral(
            "stage=resolve action=failed-final title=%1 frame=%2 retries=%3")
            .arg(job.key.title_id).arg(job.key.frame).arg(job.retry_count));
        scheduleDiagnosticsChanged();
        return false;
    }

    QImage image;
    const QRect full_rect(0, 0, std::max(1, job.key.width),
                          std::max(1, job.key.height));
    if (pending.was_stale && pending.had_previous &&
        pending.ticket.region != full_rect) {
        QImage merged = expand_sparse_frame(
            pending.previous, job.key.width, job.key.height);
        fxm::cache_frame_payload::Placement placement;
        const QImage patch = staged.format() == kDiskFrameFormat
            ? staged : staged.convertToFormat(kDiskFrameFormat);
        if (!merged.isNull() && !patch.isNull() &&
            fxm::cache_frame_payload::read_placement(patch, placement) &&
            placement.canvas_width == job.key.width &&
            placement.canvas_height == job.key.height) {
            const qsizetype row_bytes = qsizetype(patch.width()) * 4;
            for (int y = 0; y < patch.height(); ++y) {
                std::memcpy(
                    merged.scanLine(placement.y + y) +
                        qsizetype(placement.x) * 4,
                    patch.constScanLine(y), row_bytes);
            }
            image = make_sparse_frame(merged, job.key.width, job.key.height);
        }
    } else {
        image = make_sparse_frame(staged, job.key.width, job.key.height);
    }

    if (image.isNull()) {
        title_gpu_frame_cache_remove(job.key.toString().toStdString());
        if (retryFailedJob(job, live_state_key, "payload")) {
            completion_guard.disarm();
            scheduleDiagnosticsChanged();
            return false;
        }
        abandonJobState(job, live_state_key);
        state_tracker_.setState(job.key, FrameCacheState::Stale);
        FXM_LOG_ERROR("Prerender", QStringLiteral(
            "stage=payload action=failed-final title=%1 frame=%2 retries=%3")
            .arg(job.key.title_id).arg(job.key.frame).arg(job.retry_count));
        scheduleDiagnosticsChanged();
        return false;
    }

    /* Publish the final sparse payload into the shared GPU RAM cache only
     * after readback resolution/dirty-patch merge. The cache decomposes it into
     * aligned content-addressed tiles, skips transparent tiles and reuses
     * unchanged tile textures across frames. */
    const bool gpu_ram_stored = title_gpu_frame_cache_store_image(
        job.key.toString().toStdString(), image,
        static_cast<uint32_t>(std::max(1, job.key.width)),
        static_cast<uint32_t>(std::max(1, job.key.height)));

    {
        QMutexLocker lock(&dirty_tiles_mutex_);
        dirty_tiles_by_frame_.remove(tileStateKey(job.key.title_id, job.key.frame));
    }

    std::unique_lock<std::recursive_mutex> publication_lock(publication_mutex_);
    if (!jobIsCurrent(job)) {
        publication_lock.unlock();
        title_gpu_frame_cache_remove(job.key.toString().toStdString());
        abandonJobState(job, live_state_key);
        return false;
    }

    std::unique_lock<std::recursive_mutex> live_publish_lock;
    QVector<QString> publish_live_states;
    if (job.live_cue) {
        live_publish_lock = std::unique_lock<std::recursive_mutex>(live_cue_mutex_);
        const QString rebound_state = liveCueStateReferencingKey(job.key, live_state_key);
        if (rebound_state.isEmpty()) {
            publication_lock.unlock();
            title_gpu_frame_cache_remove(job.key.toString().toStdString());
            return false;
        }
        live_state_key = rebound_state;
        job.cue_state_key = rebound_state;
        job.cue_row = live_cue_rows_.value(rebound_state, job.cue_row);
        publish_live_states.push_back(rebound_state);
        for (auto it = live_cue_required_keys_.cbegin();
             it != live_cue_required_keys_.cend(); ++it) {
            if (it.key() == rebound_state ||
                live_cue_title_ids_.value(it.key()) != job.key.title_id)
                continue;
            if (std::find(it.value().cbegin(), it.value().cend(), job.key) !=
                it.value().cend())
                publish_live_states.push_back(it.key());
        }
    }

    /* Disk persistence is best-effort and nonblocking. If both the sparse GPU
     * cache and the bounded writer reject this frame, keep the already-rendered
     * payload in RAM rather than publishing a false CachedDisk state or forcing
     * the frame to be rendered again immediately. */
    const bool disk_write_queued = disk_cache_.enqueuePut(job.key, image);
    bool resident_frame_stored = gpu_ram_stored;
    if (!resident_frame_stored && !disk_write_queued) {
        ram_cache_.put(job.key, image);
        resident_frame_stored = ram_cache_.contains(job.key);
    }

    if (!job.live_cue && !job.realtime) {
        if (!pending.temporal_state_key.isEmpty()) {
            QMutexLocker dedup_lock(&temporal_dedup_mutex_);
            if (!temporal_canonical_keys_.contains(
                    pending.temporal_state_key))
                temporal_canonical_keys_[pending.temporal_state_key] =
                    job.key;
        }
        rememberVisualHash(*job.title, job.key.content_hash);
    }

    if (job.live_cue) {
        live_cue_known_keys_.insert(job.key);
        for (const QString &state_key : publish_live_states) {
            const int signal_row = live_cue_rows_.value(state_key, job.cue_row);
            if (job.force_render && live_cue_total_by_key_.contains(state_key)) {
                const int total = std::max(1, live_cue_total_by_key_.value(state_key));
                live_cue_done_by_key_[state_key] = std::min(
                    total, live_cue_done_by_key_.value(state_key, 0) + 1);
            }
            const int progress = liveCueStoredProgress(state_key);
            live_cue_states_[state_key] = FrameCacheState::NotCached;
            FrameCacheState next_state = liveCueStoredState(state_key);
            const bool manual_rebuild_active =
                live_cue_total_by_key_.contains(state_key) &&
                live_cue_done_by_key_.value(state_key, 0) <
                    live_cue_total_by_key_.value(state_key);
            if (manual_rebuild_active) {
                next_state = FrameCacheState::Rendering;
            } else if (live_cue_total_by_key_.contains(state_key)) {
                live_cue_total_by_key_.remove(state_key);
                live_cue_done_by_key_.remove(state_key);
                next_state = liveCueStoredState(state_key);
            }
            live_cue_states_[state_key] = next_state;
            live_cue_progress_percent_[state_key] = progress;
            if (signal_row >= 0 &&
                shouldEmitLiveCueUpdate(state_key, next_state, progress))
                scheduleLiveCueStateChanged(job.key.title_id, signal_row);
        }
    } else {
        state_tracker_.setState(
            job.key, resident_frame_stored
                ? FrameCacheState::CachedRam
                : FrameCacheState::CachedDisk);
        scheduleFrameReady(job.key.title_id, job.key.frame);
    }
    scheduleDiagnosticsChanged();
    return true;
}
