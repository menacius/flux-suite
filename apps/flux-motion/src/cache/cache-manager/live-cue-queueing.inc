        if (cache_ram_frame_contains(ram_cache_, frame_key)) {
            ++cached;
        } else if (disk_cache_.contains(frame_key)) {
            ++cached;
            any_disk = true;
        }
    }

    if (explicit_state == FrameCacheState::Rendering || any_rendering)
        return FrameCacheState::Rendering;
    if (cached >= total)
        return any_disk ? FrameCacheState::CachedDisk : FrameCacheState::CachedRam;
    if (explicit_state == FrameCacheState::Queued || any_queued)
        return FrameCacheState::Queued;
    if (cached > 0)
        return FrameCacheState::Queued;
    if (any_stale)
        return FrameCacheState::Stale;
    return FrameCacheState::NotCached;
}

bool CacheManager::liveCueRowRenderPausedLocked(const QString &title_id, const QString &row_id) const
{
    if (title_id.isEmpty() || row_id.isEmpty())
        return false;
    return live_cue_paused_row_ids_.value(title_id).contains(row_id);
}

bool CacheManager::liveCueStateRenderPausedLocked(const QString &state_key) const
{
    if (state_key.isEmpty())
        return false;

    QString title_id = live_cue_title_ids_.value(state_key);
    if (title_id.isEmpty()) {
        const int marker = state_key.indexOf(QStringLiteral(":live-"));
        if (marker > 0)
            title_id = state_key.left(marker);
    }
    const QSet<QString> paused_rows = live_cue_paused_row_ids_.value(title_id);
    if (paused_rows.isEmpty())
        return false;

    const QString destination_id = live_cue_row_ids_.value(state_key);
    for (const QString &row_id : paused_rows) {
        if (destination_id == row_id ||
            state_key.contains(QStringLiteral(":%1:").arg(row_id)) ||
            state_key.endsWith(QStringLiteral(":%1").arg(row_id)))
            return true;
    }
    return false;
}

void CacheManager::queueLiveCueVariantSet(
    const std::shared_ptr<Title> &title, int row, const QString &state_key,
    const QVector<std::shared_ptr<Title>> &variants, bool urgent,
    bool hydrate_disk_to_ram, int manual_priority_group)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title || state_key.isEmpty() || variants.isEmpty())
        return;

    const QString title_id = QString::fromStdString(title->id);
    const QString row_id = liveCueRowIdentity(title, row);
    if (liveCueRowRenderPausedLocked(title_id, row_id) || liveCueStateRenderPausedLocked(state_key)) {
        FXM_LOG_DEBUG("LiveCue", QStringLiteral("Skipped paused live cue state title=%1 row=%2 stateKey=%3")
                                    .arg(title_id).arg(row).arg(state_key));
        return;
    }
    live_cue_rows_[state_key] = row;
    live_cue_row_ids_[state_key] = row_id;
    live_cue_title_ids_[state_key] = title_id;
    if (state_key.contains(QStringLiteral(":live-transition:")))
        live_cue_transition_state_keys_.insert(state_key);
    else
        live_cue_transition_state_keys_.remove(state_key);

    struct RequiredFrame {
        CacheFrameKey key;
        std::shared_ptr<Title> title;
        int variant_index = 0;
    };
    QVector<RequiredFrame> required_frames;
    QVector<CacheFrameKey> required_keys;
    QSet<CacheFrameKey> required_seen;
    int variant_index = 0;
    for (const auto &variant_title : variants) {
        if (!variant_title) {
            ++variant_index;
            continue;
        }
        for (const CacheFrameKey &frame_key : frameKeysForTitle(*variant_title)) {
            if (required_seen.contains(frame_key))
                continue;
            required_seen.insert(frame_key);
            required_keys.push_back(frame_key);
            required_frames.push_back({frame_key, variant_title, variant_index});
            live_cue_known_keys_.insert(frame_key);
        }
        ++variant_index;
    }
    if (required_keys.isEmpty())
        return;

    const QVector<CacheFrameKey> old_required = live_cue_required_keys_.value(state_key);
    const bool requirements_changed = !live_cue_required_keys_.contains(state_key) || old_required != required_keys;
    live_cue_required_keys_[state_key] = required_keys;
    live_cue_required_total_[state_key] = static_cast<int>(required_keys.size());
    if (urgent) {
        /* Manual refresh owns an independent progress generation. Existing
         * resident frames may still be used by playback, but they must not
         * count as completed work for the new rebuild. */
        live_cue_total_by_key_[state_key] = static_cast<int>(required_keys.size());
        live_cue_done_by_key_[state_key] = 0;
        live_cue_last_emit_states_.remove(state_key);
        live_cue_last_emit_buckets_.remove(state_key);
    }
    if (requirements_changed) {
        live_cue_rendered_since_emit_.remove(state_key);
        live_cue_last_emit_states_.remove(state_key);
        live_cue_last_emit_buckets_.remove(state_key);
    }

    const FrameCacheState existing_state = liveCueStoredState(state_key);
    const int existing_progress = liveCueStoredProgress(state_key);
    const bool busy = existing_state == FrameCacheState::Queued || existing_state == FrameCacheState::Rendering;
    FXM_LOG_DEBUG("LiveCue", QStringLiteral("queueLiveCueState title=%1 row=%2 transition=%3 urgent=%4 state=%5 busy=%6 requiredFrames=%7 progress=%8 stateKey=%9")
                                .arg(title_id).arg(row)
                                .arg(live_cue_transition_state_keys_.contains(state_key))
                                .arg(urgent).arg((int)existing_state).arg(busy)
                                .arg(static_cast<int>(required_keys.size())).arg(existing_progress)
                                .arg(state_key));

    if (!urgent && !hydrate_disk_to_ram && (existing_state == FrameCacheState::CachedRam || existing_state == FrameCacheState::CachedDisk)) {
        live_cue_states_[state_key] = existing_state;
        live_cue_progress_percent_[state_key] = 100;
        return;
    }
    if (!requirements_changed && existing_state == FrameCacheState::Rendering)
        return;

    int queued = 0;
    bool active_or_queued = false;
    QVector<RenderQueueManager::Job> pending_jobs;
    pending_jobs.reserve(required_frames.size());
    for (const RequiredFrame &required : required_frames) {
        const bool in_ram = cache_ram_frame_contains(ram_cache_, required.key);
        const bool on_disk = !in_ram && disk_cache_.contains(required.key);
        if (in_ram)
            continue;
        if (!urgent && !hydrate_disk_to_ram && on_disk)
            continue;
        if (state_tracker_.state(required.key) == FrameCacheState::Rendering ||
            queue_.contains(required.key)) {
            active_or_queued = true;
            if (!urgent && !hydrate_disk_to_ram)
                continue;
        }
        if (urgent)
            queue_.cancelKey(required.key);

        /* Playback hydration outranks all background prerender work, but it is
         * not a forced rebuild: renderJob() will load the existing sparse/LZ4
         * payload from disk and publish it into the shared RAM cache. */
        constexpr qint64 kPlaybackPriorityBase = -2000000000LL;
        constexpr qint64 kManualPriorityBase = -1500000000LL;
        constexpr qint64 kManualRowStride = 1000000LL;
        constexpr qint64 kVariantStride = 100000LL;
        const qint64 variant_priority = static_cast<qint64>(required.variant_index) * kVariantStride +
            std::max(0, required.key.frame);
        const int priority = hydrate_disk_to_ram
            ? clamp_cache_priority(kPlaybackPriorityBase + variant_priority)
            : urgent
                ? clamp_cache_priority(kManualPriorityBase +
                    static_cast<qint64>(std::max(0, manual_priority_group)) * kManualRowStride +
                    variant_priority)
                : clamp_cache_priority(-1000000LL + static_cast<qint64>(std::max(0, row)) * kVariantStride +
                    static_cast<qint64>(required.variant_index) * 1000LL + std::max(0, required.key.frame));
        RenderQueueManager::Job job;
        job.key = required.key;
        job.title = required.title;
        job.time = timeForFrame(required.key.frame);
        job.priority = priority;
        job.live_cue = true;
        job.realtime = hydrate_disk_to_ram;
        job.urgent = urgent || hydrate_disk_to_ram;
        job.force_render = !hydrate_disk_to_ram &&
            (urgent || live_cue_states_.value(state_key, FrameCacheState::NotCached) == FrameCacheState::Stale);
        job.cue_row = row;
        job.cue_state_key = state_key;
        job.cache_epoch = cache_epoch_.load();
        job.title_generation = titleGeneration(required.key.title_id);
        pending_jobs.push_back(job);
    }
    queued = queue_.enqueueMany(pending_jobs);

    if (urgent && queued == 0 && !active_or_queued) {
        /* A manual rebuild can be satisfied entirely by resident/shared frames.
         * Close that progress generation so the row resolves from real cache
         * state instead of staying Queued with no worker job to complete it. */
        live_cue_total_by_key_.remove(state_key);
        live_cue_done_by_key_.remove(state_key);
    }

    const int progress = liveCueStoredProgress(state_key);
    if (queued > 0) {
        ++live_cue_stats_.misses;
        live_cue_states_[state_key] = FrameCacheState::Queued;
        live_cue_progress_percent_[state_key] = progress;
        FXM_LOG_INFO("LiveCue", QStringLiteral("Queued live cue state title=%1 row=%2 transition=%3 queuedFrames=%4 requiredFrames=%5 progress=%6 stateKey=%7")
                                  .arg(title_id).arg(row)
                                  .arg(live_cue_transition_state_keys_.contains(state_key))
                                  .arg(queued).arg(static_cast<int>(required_keys.size()))
                                  .arg(progress).arg(state_key));
        scheduleLiveCueStateChanged(title_id, row);
        scheduleDiagnosticsChanged();
        wakeWorker();
    } else {
        const FrameCacheState resolved_state = liveCueStoredState(state_key);
        live_cue_states_[state_key] = resolved_state;
        live_cue_progress_percent_[state_key] = progress;
        if (shouldEmitLiveCueUpdate(state_key, resolved_state, progress))
            scheduleLiveCueStateChanged(title_id, row);
    }

    if (requirements_changed)
        pruneUnreferencedLiveCueRam(title_id);
}

void CacheManager::queueLiveCue(const std::shared_ptr<Title> &title, int row, bool urgent)
{
    apply_persisted_live_cue_persistence(title);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title ||
        titleCacheability(title) == TitleCacheability::NonCacheable) {
        FXM_LOG_DEBUG("LiveCue", QStringLiteral("Skipped queueLiveCue row=%1 because title caching is unavailable").arg(row));
        return;
    }
    const auto variants = liveCueVariants(title, row);
    if (variants.isEmpty()) {
        FXM_LOG_WARNING("LiveCue", QStringLiteral("Skipped queueLiveCue row=%1 because steady cue variant could not be created").arg(row));
        return;
    }
    queueLiveCueVariantSet(title, row, liveCueStateKey(title, row), variants, urgent);

    /* A cue request must also make its currently reachable transition ready.
     * Previously the dock could wait forever on isLiveCueReady(): it detected a
     * missing active->target transition but queueLiveCue() queued only the
     * steady target state. */
    if (title && title->current_cue_row >= 0 && title->current_cue_row != row &&
        title->current_cue_row < static_cast<int>(title->live_text_rows.size()) &&
        (title->playback_mode == 1 || title->playback_mode == 2 || title->cue_background_persistence)) {
        queueLiveCueTransition(title, title->current_cue_row, row, urgent);
    }
}

void CacheManager::queueLiveCueTransition(const std::shared_ptr<Title> &title,
                                          int from_row, int to_row, bool urgent)
{
    apply_persisted_live_cue_persistence(title);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    const auto variants = liveCueTransitionVariants(title, from_row, to_row);
    if (variants.isEmpty())
        return;
    queueLiveCueVariantSet(title, to_row,
                           liveCueTransitionStateKey(title, from_row, to_row),
                           variants, urgent);
}

void CacheManager::cacheLiveCueNow(const std::shared_ptr<Title> &title, int row)
{
    std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return;

    const QString title_id = QString::fromStdString(title->id);
    const QString row_id = liveCueRowIdentity(title, row);
    if (liveCueRowRenderPausedLocked(title_id, row_id)) {
        /* Manual cache is an explicit user command and must win over the
         * edit-focus pause. A leaked/stale focus pause previously made the
         * cache button a permanent no-op. */
        QSet<QString> &paused_rows = live_cue_paused_row_ids_[title_id];
        paused_rows.remove(row_id);
        if (paused_rows.isEmpty())
            live_cue_paused_row_ids_.remove(title_id);
        FXM_LOG_INFO("LiveCue", QStringLiteral("Manual rebuild force-resumed paused live cue row title=%1 row=%2")
                                   .arg(title_id).arg(row));
    }
    QVector<QString> rebuild_states;
    /* A row's manual refresh must rebuild only its steady state. Transition
     * pairs belong to title-level prerendering and must not drive the status
     * badges of unrelated rows to Queued (100%). They remain valid until the
     * row content actually changes, at which point refreshLiveCueStructure()
     * invalidates only the affected transition pairs. */
    const QString steady_state_key = liveCueStateKey(title, row);
    if (live_cue_title_ids_.value(steady_state_key) == title_id ||
        live_cue_required_keys_.contains(steady_state_key))
        rebuild_states.push_back(steady_state_key);

    QSet<CacheFrameKey> rebuild_keys;
    for (const QString &state_key : rebuild_states) {
        for (const CacheFrameKey &old_key : live_cue_required_keys_.value(state_key))
            rebuild_keys.insert(old_key);
        removeLiveCueState(state_key);
    }

    /* Drop only frames that became unreferenced after removing the selected
     * row's steady/transition states. Content-addressed frames shared by an
     * unaffected state remain valid and must not be destroyed. Manual rebuild
     * still removes unshared disk entries so the selected state is regenerated. */
    int removed_frames = 0;
    for (const CacheFrameKey &frame_key : rebuild_keys) {
        if (isLiveCueKeyReferenced(frame_key))
            continue;
        queue_.cancelKey(frame_key);
        ram_cache_.remove(frame_key);
        title_gpu_frame_cache_remove(frame_key.toString().toStdString());
        disk_cache_.remove(frame_key);
        live_cue_known_keys_.remove(frame_key);
        state_tracker_.setState(frame_key, FrameCacheState::NotCached);
        ++removed_frames;
    }

    FXM_LOG_INFO("LiveCue", QStringLiteral("Manual live cue cache rebuild title=%1 row=%2 removedFrames=%3 candidateFrames=%4 removedStates=%5")
                              .arg(title_id).arg(row)
                              .arg(removed_frames)
                              .arg(static_cast<int>(rebuild_keys.size()))
                              .arg(static_cast<int>(rebuild_states.size())));

    const int manual_priority_group = next_manual_live_cue_priority_group_++;
    if (next_manual_live_cue_priority_group_ > 1000)
        next_manual_live_cue_priority_group_ = 0;
    queueLiveCueVariantSet(title, row, steady_state_key, liveCueVariants(title, row), true, false,
                           manual_priority_group);
    pruneUnreferencedLiveCueRam(title_id);
}


QImage CacheManager::requestLiveCueFrame(const std::shared_ptr<Title> &title, int row, double time, bool queue_if_missing)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return QImage();

    std::shared_ptr<Title> cue_title;
    /* While a row is on air, its already-applied layer values are the
     * authoritative playback snapshot. Editing the row updates only the next
     * cue; it must not replace the text of the current cue or its uncue/outro. */
    const bool exact_runtime_state = title->current_cue_row == row;
    if (exact_runtime_state)
        cue_title = clone_title_snapshot(title);
    else
        cue_title = titleWithCueApplied(title, row);
    if (!cue_title)
        return QImage();

    const double clamped_time = std::clamp(time, 0.0, std::max(0.0, cue_title->duration));
    const CacheFrameKey key = keyForTime(*cue_title, clamped_time);
    QString state_key = liveCueStateKey(title, row);

    /* Runtime transition snapshots are matched by their exact content-addressed
     * key. This avoids relying on mutable row indices or on a remembered
     * previous row after the incoming phase has begun. */
    for (auto it = live_cue_required_keys_.constBegin(); it != live_cue_required_keys_.constEnd(); ++it) {
        if (live_cue_title_ids_.value(it.key()) != key.title_id)
            continue;
        if (std::find(it.value().cbegin(), it.value().cend(), key) != it.value().cend()) {
            state_key = it.key();
            break;
        }
    }

    const FrameCacheState existing_state = live_cue_states_.value(state_key, FrameCacheState::NotCached);
    const bool busy = existing_state == FrameCacheState::Queued || existing_state == FrameCacheState::Rendering;
    QImage image;
    if (existing_state != FrameCacheState::Stale && ram_cache_.get(key, image)) {
        live_cue_known_keys_.insert(key);
        ++live_cue_stats_.hits;
        ++live_cue_stats_.reuses;
        scheduleDiagnosticsChanged();
        return image;
    }
    if (existing_state != FrameCacheState::Stale && disk_cache_.contains(key)) {
        /* Keep tile reads/decompression off the UI and OBS render threads. */
        queueRealtimeJob(cue_title, clamped_time, true, row, state_key);
        FXM_LOG_DEBUG("LiveCue", QStringLiteral(
            "Queued disk hydration title=%1 row=%2 frame=%3 stateKey=%4")
            .arg(key.title_id).arg(row).arg(key.frame).arg(state_key));
        return QImage();
    }

    if (queue_if_missing) {
        if (title->pending_cue_row >= 0 && title->current_cue_row >= 0 &&
            title->current_cue_row != title->pending_cue_row) {
            queueLiveCueTransition(title, title->current_cue_row, title->pending_cue_row);
        } else {
            queueLiveCue(title, row);
        }
    }
    FXM_LOG_DEBUG("LiveCue", QStringLiteral("Live cue cache miss title=%1 row=%2 frame=%3 transition=%4 busy=%5 queue=%6 stateKey=%7")
                                .arg(key.title_id).arg(row).arg(key.frame)
                                .arg(live_cue_transition_state_keys_.contains(state_key))
                                .arg(busy).arg(queue_if_missing).arg(state_key));
    return QImage();
}

QString CacheManager::requestLiveCueFrameGpuToken(
    const std::shared_ptr<Title> &title, int row, double time,
    bool queue_if_missing)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title || row < 0 ||
        row >= static_cast<int>(title->live_text_rows.size()))
        return QString();

    std::shared_ptr<Title> cue_title;
    /* While a row is on air, its already-applied layer values are the
     * authoritative playback snapshot. Editing the row updates only the next
     * cue; it must not replace the text of the current cue or its uncue/outro. */
    const bool exact_runtime_state = title->current_cue_row == row;
    if (exact_runtime_state)
        cue_title = clone_title_snapshot(title);
    else
        cue_title = titleWithCueApplied(title, row);
    if (!cue_title)
        return QString();

    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, cue_title->duration));
    const CacheFrameKey key = keyForTime(*cue_title, clamped_time);
    if (title_gpu_frame_cache_contains(key.toString().toStdString())) {
        live_cue_known_keys_.insert(key);
        ++live_cue_stats_.hits;
        ++live_cue_stats_.reuses;
        scheduleDiagnosticsChanged();
        return key.toString();
    }

    if (queue_if_missing) {
        QString state_key = liveCueStateKey(title, row);
        const QString referenced_state =
            liveCueStateReferencingKey(key, state_key);
        if (!referenced_state.isEmpty())
            state_key = referenced_state;
        if (disk_cache_.contains(key)) {
            queueRealtimeJob(cue_title, clamped_time, true, row, state_key);
        } else if (title->pending_cue_row >= 0 && title->current_cue_row >= 0 &&
                   title->current_cue_row != title->pending_cue_row) {
            queueLiveCueTransition(title, title->current_cue_row,
                                   title->pending_cue_row);
        } else {
            queueLiveCue(title, row);
        }
    }
    return QString();
}

QImage CacheManager::requestLiveCueFrameRealtime(const std::shared_ptr<Title> &title, int row, double time,
                                                     const QString &known_content_hash)
{
    if (!cache_enabled_.load() || interactive_bypass_.load() || !title || row < 0 ||
        row >= static_cast<int>(title->live_text_rows.size()))
        return QImage();

    /* Realtime playback must use the exact same runtime title snapshot as the
     * proven uncached source path. Re-applying a row here can discard an
     * in-progress persistence transition and can also make the caller-provided
     * content hash describe a different title than the rendered frame. */
    /* Lookup uses only immutable frame-key fields plus the caller's already
     * validated content hash. Do not deep-copy the complete title graph on
     * every OBS video tick; queueRealtimeJob snapshots only on a true miss. */
    const double clamped_time = std::clamp(time, 0.0, std::max(0.0, title->duration));
    const CacheFrameKey key = keyForTime(*title, clamped_time, known_content_hash);
    const FrameCacheState initial_state = state_tracker_.state(key);
    log_cache_playback(QStringLiteral("request kind=live-cue title=%1 row=%2 time=%3 frame=%4 state=%5 key=%6")
        .arg(key.title_id).arg(row).arg(clamped_time, 0, 'f', 6).arg(key.frame)
        .arg(QString::fromUtf8(cache_playback_state_name(initial_state)), key.toString()));
    QImage image;
    if (initial_state != FrameCacheState::Stale && ram_cache_.get(key, image)) {
        log_cache_playback(QStringLiteral("result=ram-hit kind=live-cue title=%1 row=%2 frame=%3 cacheKey=%4 size=%5x%6 key=%7")
            .arg(key.title_id).arg(row).arg(key.frame).arg(image.cacheKey())
            .arg(image.width()).arg(image.height()).arg(key.toString()));
        return image;
    }
    if (initial_state != FrameCacheState::Stale && disk_cache_.contains(key)) {
        queueRealtimeJob(title, clamped_time, false, row, QString(), known_content_hash);
        log_cache_playback(QStringLiteral("result=disk-hydration-queued kind=live-cue title=%1 row=%2 frame=%3 key=%4")
            .arg(key.title_id).arg(row).arg(key.frame).arg(key.toString()));
        return QImage();
    }

    queueRealtimeJob(title, clamped_time, false, row, QString(), known_content_hash);
    log_cache_playback(QStringLiteral("result=miss-queued kind=live-cue title=%1 row=%2 frame=%3 initialState=%4 key=%5")
        .arg(key.title_id).arg(row).arg(key.frame)
        .arg(QString::fromUtf8(cache_playback_state_name(initial_state)), key.toString()));
    return QImage();
}

QImage CacheManager::requestLiveCueFrame(const std::shared_ptr<Title> &title, int row, bool queue_if_missing)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title)
        return QImage();
    return requestLiveCueFrame(title, row, std::clamp(title->pause_time, 0.0, title->duration), queue_if_missing);
}

void CacheManager::preloadLiveCues(const std::shared_ptr<Title> &title, int current_row, int nearby_count)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title || title->live_text_rows.empty() ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    const int count = static_cast<int>(title->live_text_rows.size());
    const int current = std::clamp(current_row, 0, count - 1);
    queueLiveCue(title, current);
    for (int i = 1; i <= std::max(1, nearby_count); ++i) {
        if (current - i >= 0) queueLiveCue(title, current - i);
        if (current + i < count) queueLiveCue(title, current + i);
    }
}

FrameCacheState CacheManager::liveCueState(const std::shared_ptr<Title> &title, int row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load())
        return FrameCacheState::Disabled;
    if (title && titleCacheability(title) == TitleCacheability::NonCacheable)
        return FrameCacheState::Disabled;
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return FrameCacheState::NotCached;

    /* Row badges describe only the row's steady prerender state. Transition
     * variants are independent title-level work and are represented by the
     * aggregate cache status. Mixing them here caused every row to display
     * Queued (100%) whenever one row refresh created new transition work. */
    const QString steady_key = liveCueStateKey(title, row);
    return live_cue_required_keys_.contains(steady_key)
        ? liveCueStoredState(steady_key)
        : liveCueVariantsState(liveCueVariants(title, row), steady_key);
}

int CacheManager::liveCueProgressPercent(const std::shared_ptr<Title> &title, int row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() ||
        (title && titleCacheability(title) == TitleCacheability::NonCacheable))
        return 100;
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return 0;

    /* Keep per-row progress isolated from persistence transition pairs. The
     * aggregate progress API continues to include all transition frames. */
    const QString steady_key = liveCueStateKey(title, row);
    if (live_cue_total_by_key_.contains(steady_key))
        return liveCueStoredProgress(steady_key);

    const auto keys = live_cue_required_keys_.value(steady_key);
    if (keys.isEmpty())
        return liveCueVariantsProgress(liveCueVariants(title, row));

    int cached = 0;
    for (const CacheFrameKey &key : keys) {
        if (cache_ram_frame_contains(ram_cache_, key) ||
            disk_cache_.contains(key))
            ++cached;
    }
    return std::clamp((cached * 100) / static_cast<int>(keys.size()), 0, 100);
}

bool CacheManager::liveCueStateFullyResidentInRam(const QString &state_key) const
{
    const QVector<CacheFrameKey> keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return false;
    for (const CacheFrameKey &key : keys) {
        if (!cache_ram_frame_contains(ram_cache_, key))
            return false;
    }
    return true;
}

bool CacheManager::liveCueStateStartResidentInRam(const QString &state_key) const
{
    const QVector<CacheFrameKey> keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return false;

    QHash<QString, CacheFrameKey> first_key_by_content;
    for (const CacheFrameKey &key : keys) {
        const QString content_key = QStringLiteral("%1:%2:%3x%4")
            .arg(key.title_id, key.content_hash)
            .arg(key.width).arg(key.height);
        auto it = first_key_by_content.find(content_key);
        if (it == first_key_by_content.end() || key.frame < it.value().frame)
            first_key_by_content[content_key] = key;
    }

    for (const CacheFrameKey &key : first_key_by_content) {
        if (!cache_ram_frame_contains(ram_cache_, key))
            return false;
    }
    return true;
}

bool CacheManager::liveCueStateRangePlayable(const QString &state_key, int first_frame, int last_frame) const
{
    const QVector<CacheFrameKey> keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return false;

    const int clamped_first = std::max(0, first_frame);
    const int clamped_last = std::max(clamped_first, last_frame);
    bool found_required_frame = false;
    for (const CacheFrameKey &key : keys) {
        if (key.frame < clamped_first || key.frame > clamped_last)
            continue;
        found_required_frame = true;
        if (cache_ram_frame_contains(ram_cache_, key) ||
            disk_cache_.contains(key))
            continue;
        return false;
    }
    return found_required_frame;
}

bool CacheManager::liveCueStateFullyPlayable(const QString &state_key) const
{
    const QVector<CacheFrameKey> keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return false;

    for (const CacheFrameKey &key : keys) {
        if (cache_ram_frame_contains(ram_cache_, key) ||
            disk_cache_.contains(key))
            continue;
        return false;
    }
    return true;
}


bool CacheManager::liveCueUseDiskStreaming() const
{
    constexpr quint64 kMinSystemHeadroom = 512ull * 1024ull * 1024ull;
    constexpr quint64 kMinCacheHeadroom = 96ull * 1024ull * 1024ull;
    const quint64 available = available_system_memory_bytes();
    const quint64 limit = ram_cache_.maxBytes();
    const quint64 used = static_cast<quint64>(
        title_gpu_frame_cache_bytes_used());
    const quint64 cache_headroom = limit > used ? limit - used : 0;
    return available < kMinSystemHeadroom ||
           (used >= (limit * 85ull) / 100ull && cache_headroom < kMinCacheHeadroom);
}

bool CacheManager::prepareLiveCueForPlayback(const std::shared_ptr<Title> &title, int row)
{
    apply_persisted_live_cue_persistence(title);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title || row < 0 ||
        row >= static_cast<int>(title->live_text_rows.size()) ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return false;

    const QString steady_key = liveCueStateKey(title, row);
    const auto steady_variants = liveCueVariants(title, row);
    const bool disk_streaming = liveCueUseDiskStreaming();
    auto ensure_playback_state = [&](const QString &state_key,
                                     const QVector<std::shared_ptr<Title>> &variants) {
        const FrameCacheState state = live_cue_states_.value(state_key, FrameCacheState::NotCached);
        if (state == FrameCacheState::Queued || state == FrameCacheState::Rendering)
            return;
        queueLiveCueVariantSet(title, row, state_key, variants, false, !disk_streaming);
    };
    ensure_playback_state(steady_key, steady_variants);

    const double duration = std::max(0.0, title->duration);
    double steady_end_time = duration;
    if (title->playback_mode == 1) {
        const double loop_start = std::clamp(title->loop_start, 0.0, duration);
        steady_end_time = std::clamp(title->loop_end, loop_start, duration);
    } else if (title->playback_mode == 2) {
        steady_end_time = std::clamp(title->pause_time, 0.0, duration);
    }
    const int steady_last_frame = titleHasTimelineChanges(*title)
        ? std::clamp(frameForTime(steady_end_time), 0,
                     cache_last_frame_for_title(*title, effectiveFrameRate()))
        : 0;

    bool ready = liveCueStateRangePlayable(steady_key, 0, steady_last_frame);
    int required_state_count = 1;
    const int active_row = title->current_cue_row;
    if (active_row >= 0 && active_row != row &&
        active_row < static_cast<int>(title->live_text_rows.size()) &&
        (title->playback_mode == 1 || title->playback_mode == 2 || title->cue_background_persistence)) {
        const QString transition_key = liveCueTransitionStateKey(title, active_row, row);
        const auto transition_variants = liveCueTransitionVariants(title, active_row, row);
        ensure_playback_state(transition_key, transition_variants);
        /* A row-to-row cue starts on the outgoing suffix and then enters the
         * incoming prefix. Require the complete transition state so playback
         * can never fall through to uncached rendering between those phases. */
        ready = ready && liveCueStateFullyPlayable(transition_key);
        ++required_state_count;
    }

    FXM_LOG_INFO("LiveCue", QStringLiteral("Playback cache gate title=%1 row=%2 states=%3 ready=%4 requiredEndFrame=%5 storage=ram-or-ssd hydrate=%6")
                              .arg(QString::fromStdString(title->id)).arg(row)
                              .arg(required_state_count).arg(ready)
                              .arg(steady_last_frame)
                              .arg(disk_streaming ? QStringLiteral("ssd-stream") : QStringLiteral("ram-prefetch")));
    return ready;
}


bool CacheManager::isLiveCueReady(const std::shared_ptr<Title> &title, int row)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return false;

    const QString steady_key = liveCueStateKey(title, row);
    const FrameCacheState steady = live_cue_required_keys_.contains(steady_key)
        ? liveCueStoredState(steady_key) : FrameCacheState::NotCached;
    bool ready = steady == FrameCacheState::CachedRam || steady == FrameCacheState::CachedDisk;

    const int active_row = title->current_cue_row;
    if (ready && active_row >= 0 && active_row != row &&
        active_row < static_cast<int>(title->live_text_rows.size()) &&
        (title->playback_mode == 1 || title->playback_mode == 2 || title->cue_background_persistence)) {
        const QString transition_key = liveCueTransitionStateKey(title, active_row, row);
        const FrameCacheState transition = live_cue_required_keys_.contains(transition_key)
            ? liveCueStoredState(transition_key) : FrameCacheState::NotCached;
        ready = transition == FrameCacheState::CachedRam || transition == FrameCacheState::CachedDisk;
    }

    if (ready) {
        ++live_cue_stats_.hits;
        ++live_cue_stats_.reuses;
        scheduleDiagnosticsChanged();
    }
    return ready;
}

FrameCacheState CacheManager::liveCueAggregateState(const std::shared_ptr<Title> &title) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load())
        return FrameCacheState::Disabled;
    if (!title)
        return FrameCacheState::NotCached;
    const QString title_id = QString::fromStdString(title->id);
    bool any = false, queued = false, rendering = false, stale = false, disk = false;
    bool missing_rows = false;
    for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
        if (it.value() != title_id)
            continue;
        any = true;
        const FrameCacheState state = liveCueStoredState(it.key());
        stale |= state == FrameCacheState::Stale;
        rendering |= state == FrameCacheState::Rendering;
        queued |= state == FrameCacheState::Queued || state == FrameCacheState::NotCached;
        disk |= state == FrameCacheState::CachedDisk;
    }
    for (int row = 0; row < static_cast<int>(title->live_text_rows.size()); ++row) {
        const QString state_key = liveCueStateKey(title, row);
        if (live_cue_required_keys_.contains(state_key))
            continue;
        const FrameCacheState state = liveCueVariantsState(liveCueVariants(title, row), state_key);
        if (state == FrameCacheState::NotCached) {
            missing_rows = true;
            continue;
        }
        any = true;
        stale |= state == FrameCacheState::Stale;
        rendering |= state == FrameCacheState::Rendering;
        queued |= state == FrameCacheState::Queued;
        disk |= state == FrameCacheState::CachedDisk;
    }
    if (!any) return FrameCacheState::NotCached;
    queued |= missing_rows;
    if (stale) return FrameCacheState::Stale;
    if (rendering) return FrameCacheState::Rendering;
    if (queued) return FrameCacheState::Queued;
    return disk ? FrameCacheState::CachedDisk : FrameCacheState::CachedRam;
}

int CacheManager::liveCueAggregateProgressPercent(const std::shared_ptr<Title> &title) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load())
        return 100;
    if (!title)
        return 0;
    const QString title_id = QString::fromStdString(title->id);
    QSet<CacheFrameKey> keys;
    for (auto it = live_cue_required_keys_.constBegin(); it != live_cue_required_keys_.constEnd(); ++it) {
        if (live_cue_title_ids_.value(it.key()) != title_id)
            continue;
        for (const CacheFrameKey &key : it.value())
            keys.insert(key);
    }
    for (int row = 0; row < static_cast<int>(title->live_text_rows.size()); ++row) {
        const QString state_key = liveCueStateKey(title, row);
        if (live_cue_required_keys_.contains(state_key))
            continue;
        for (const auto &variant : liveCueVariants(title, row)) {
            if (!variant)
                continue;
            for (const CacheFrameKey &key : frameKeysForTitle(*variant))
                keys.insert(key);
        }
    }
    if (keys.isEmpty())
        return 0;
    int cached = 0;
    for (const CacheFrameKey &key : keys) {
        if (cache_ram_frame_contains(ram_cache_, key) ||
            disk_cache_.contains(key))
            ++cached;
    }
    return std::clamp((cached * 100) / static_cast<int>(keys.size()), 0, 100);
}


void CacheManager::invalidateLiveCue(const std::shared_ptr<Title> &title, int row)
{
    std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return;

    ensure_live_text_row_ids(*title);
    const QString title_id = QString::fromStdString(title->id);
    const QString row_id = liveCueRowIdentity(title, row);
    QVector<QString> affected_states;
    for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
        if (it.value() != title_id)
            continue;
        const QString state_key = it.key();
        if (live_cue_row_ids_.value(state_key) == row_id ||
            state_key.contains(QStringLiteral(":%1:").arg(row_id)) ||
            state_key.endsWith(QStringLiteral(":%1").arg(row_id)))
            affected_states.push_back(state_key);
    }
    for (const QString &state_key : affected_states)
        removeLiveCueState(state_key);

    ++live_cue_stats_.invalidations;
    pruneUnreferencedLiveCueRam(title_id);
    queueLiveCue(title, row);
    for (int other = 0; other < static_cast<int>(title->live_text_rows.size()); ++other) {
        if (other == row)
            continue;
        queueLiveCueTransition(title, row, other);
        queueLiveCueTransition(title, other, row);
    }
    FXM_LOG_INFO("LiveCue", QStringLiteral("Invalidated live cue identity title=%1 row=%2 rowId=%3 removedStates=%4")
                              .arg(title_id).arg(row).arg(row_id)
                              .arg(static_cast<int>(affected_states.size())));
    scheduleDiagnosticsChanged();
    scheduleLiveCueStateChanged(title_id, row);
}

void CacheManager::invalidateLiveCues(const std::shared_ptr<Title> &title)
{
    std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title)
        return;
    const QString title_id = QString::fromStdString(title->id);
    QVector<QString> state_keys;
    for (auto it = live_cue_title_ids_.constBegin(); it != live_cue_title_ids_.constEnd(); ++it) {
        if (it.value() == title_id)
            state_keys.push_back(it.key());
    }
    for (const QString &state_key : state_keys) {
        const int row = live_cue_rows_.value(state_key, -1);
        removeLiveCueState(state_key);
        if (row >= 0)
            scheduleLiveCueStateChanged(title_id, row);
    }
    live_cue_structure_row_ids_.remove(title_id);
    live_cue_row_fingerprints_.remove(title_id);
    live_cue_transition_signatures_.remove(title_id);
    live_cue_paused_row_ids_.remove(title_id);
    ++live_cue_stats_.invalidations;
    pruneUnreferencedLiveCueRam(title_id);
    scheduleDiagnosticsChanged();
}

void CacheManager::refreshLiveCueStructure(const std::shared_ptr<Title> &title)
{
    apply_persisted_live_cue_persistence(title);
    if (!title)
        return;
    if (titleCacheability(title) == TitleCacheability::NonCacheable) {
        removeTitleCache(QString::fromStdString(title->id), true);
        scheduleLiveCueStateChanged(QString::fromStdString(title->id), -1);
        return;
    }
    std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cache_enabled_.load() || !title)
        return;

