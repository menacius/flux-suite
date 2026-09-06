static void source_hide(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data)
        return;
    data->shown_on_display.store(false, std::memory_order_release);
    if (data->audio_runtime) data->audio_runtime->stop(true);
    clear_live_cue_runtime_for_source(data, data->title_id);
    data->cue_timer_row = -1;
    data->cue_timer_elapsed_seconds = 0.0;
    FXM_LOG_TRACE("Source", QStringLiteral("Source hidden title=%1")
        .arg(QString::fromStdString(data->title_id)));
    release_active_scene_mask_scenes(data);
    request_source_presentation_reset(data, "source-hidden");
}

static uint32_t source_get_width(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return 1920;
    auto title = source_dimension_title_for_source(data);
    return title ? clamped_source_dimension(title->width) : 1920;
}

static uint32_t source_get_height(void *priv)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data) return 1080;
    auto title = source_dimension_title_for_source(data);
    return title ? clamped_source_dimension(title->height) : 1080;
}

static double obs_output_sample_duration()
{
    struct obs_video_info video_info = {};
    if (obs_get_video_info(&video_info) && video_info.fps_num > 0 &&
        video_info.fps_den > 0) {
        return static_cast<double>(video_info.fps_den) /
               static_cast<double>(video_info.fps_num);
    }
    return 1.0 / 60.0;
}

static void source_video_tick(void *priv, float seconds)
{
    auto *data = static_cast<TitleSourceData *>(priv);
    if (!data)
        return;

    const uint64_t diagnostic_tick_serial = ++data->diagnostic_tick_serial;
    const double diagnostic_tick_start_playhead = data->playhead;
    if (seconds < 0.0f || seconds > 0.250f) {
        FXM_LOG_WARNING("SourceTiming", QStringLiteral(
            "event=tick-cadence-anomaly source=%1 title=%2 tick=%3 delta=%4 playhead=%5 playing=%6 visible=%7 shown=%8")
            .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
            .arg(QString::fromStdString(data->title_id))
            .arg(diagnostic_tick_serial)
            .arg(seconds, 0, 'f', 6)
            .arg(data->playhead, 0, 'f', 6)
            .arg(data->playing)
            .arg(data->output_visible.load(std::memory_order_acquire))
            .arg(data->shown_on_display.load(std::memory_order_acquire)));
    }
    FXM_LOG_TRACE("SourceTiming", QStringLiteral(
        "event=tick-begin source=%1 title=%2 tick=%3 delta=%4 playhead=%5 playing=%6 dirty=%7 firstPending=%8 cuePhase=%9")
        .arg(data->source ? QString::fromUtf8(obs_source_get_name(data->source)) : QStringLiteral("<null>"))
        .arg(QString::fromStdString(data->title_id))
        .arg(diagnostic_tick_serial)
        .arg(seconds, 0, 'f', 6)
        .arg(diagnostic_tick_start_playhead, 0, 'f', 6)
        .arg(data->playing)
        .arg(data->dirty)
        .arg(data->first_frame_pending)
        .arg(static_cast<int>(data->cue_phase)));

    if (!source_presentation_generation_is_current(data))
        apply_source_presentation_reset(data, "generation-boundary");

    if (g_source_scene_collection_transition.load(
            std::memory_order_acquire))
        return;

    if (data->title_id.empty()) {
        sync_source_audio_active(data, nullptr, "empty-title-binding");
        return;
    }

    const auto now_project_check = std::chrono::steady_clock::now();
    if (!data->project_path.empty() &&
        (data->last_project_reload_check.time_since_epoch().count() == 0 ||
         now_project_check - data->last_project_reload_check >=
             std::chrono::seconds(1))) {
        data->last_project_reload_check = now_project_check;
        reload_source_project(data, false);
    }
    auto title = source_title_mutable(data);
    if (!title) {
        sync_source_audio_active(data, nullptr, "title-missing");
        clear_live_cue_runtime_for_source(data, data->title_id);
        data->cue_timer_row = -1;
        data->cue_timer_elapsed_seconds = 0.0;
        /* The title store may finish restoring after OBS creates its sources.
         * Keep retrying, but invalidate the old GPU poster immediately. The
         * previous implementation left final_texture valid here, so a source
         * rebound during project/scene-collection loading could retain artwork
         * from a title that no longer existed. */
        if (!data->title_missing) {
            data->title_missing = true;
            request_source_presentation_reset(data, "title-missing");
            apply_source_presentation_reset(data, "title-missing");
            data->visual_identity_hash.clear();
            ++data->visual_model_revision;
        }
        data->dirty = true;
        data->first_frame_pending = true;
        return;
    }

    title_gpu_render_session_set_render_request(
        data->gpu_render_session, TitleGpuRenderRequest{
            clamped_source_dimension(title->width),
            clamped_source_dimension(title->height),
            obs_output_sample_duration()});

    const uint64_t audio_membership_revision =
        source_model_revision(data);
    if (audio_membership_revision !=
        data->seen_audio_membership_revision) {
        sync_source_audio_active(data, title, "title-revision-changed");
        data->seen_audio_membership_revision = audio_membership_revision;
    }

    /* Provider threads never touch GPU state. The atomic revision avoids
     * polling/locking the queue on unchanged video ticks; pending notifications
     * are consumed only after a real external state transition. */
    const uint64_t pending_external_revision =
        ExternalDataManager::instance().revision();
    if (pending_external_revision != data->seen_external_data_revision) {
        const auto external_updates =
            ExternalDataManager::instance().take_render_updates();
        (void)external_updates;
    }

    if (data->title_missing) {
        data->title_missing = false;
        request_source_presentation_reset(data, "title-restored");
        apply_source_presentation_reset(data, "title-restored");
        data->visual_identity_hash.clear();
        ++data->visual_model_revision;
    }

    /* The editor owns its transport clock. Its private monitored source must
     * never run the program cue state machine or advance a second playhead;
     * it only decodes/mixes audio at the exact editor time and direction. */
    if (data->editor_transport_controlled) {
        sync_editor_transport_override(data);
        const uint64_t revision = source_model_revision(data);
        if (data->audio_runtime) {
            data->audio_runtime->bind_title(title, revision);
            data->audio_runtime->transport(
                data->playhead, data->playing,
                obs_source_active(data->source), false,
                data->editor_playback_reverse,
                data->editor_audio_speed.load(std::memory_order_acquire));
            data->audio_runtime->pump();
        }
        return;
    }

    if (data->force_cue_state_sync || title->cue_revision != data->seen_cue_revision) {
        data->playback_state.store(TitlePlaybackState::Queued,
                                   std::memory_order_release);
        const int requested_cue_row = title->pending_cue_row >= 0
            ? title->pending_cue_row
            : title->current_cue_row;
        const bool is_uncue_request = title->cue_uncue_requested &&
            title->pending_cue_row < 0 && title->current_cue_row >= 0;
        bool cache_gate_ready = true;
        if (!is_uncue_request && requested_cue_row >= 0 &&
            requested_cue_row < static_cast<int>(title->live_text_rows.size()) &&
            CacheManager::instance().cacheEnabled() &&
            CacheManager::instance().titleCacheability(title) != TitleCacheability::NonCacheable) {
            const auto now = std::chrono::steady_clock::now();
            const bool target_changed = data->cue_cache_check_title_id != title->id ||
                data->cue_cache_check_revision != title->cue_revision ||
                data->cue_cache_check_row != requested_cue_row;
            const bool retry_due = data->last_cue_cache_check.time_since_epoch().count() == 0 ||
                now - data->last_cue_cache_check >= std::chrono::milliseconds(100);
            if (target_changed || retry_due) {
                data->cue_cache_check_ready =
                    CacheManager::instance().prepareLiveCueForPlayback(title, requested_cue_row);
                data->cue_cache_check_title_id = title->id;
                data->cue_cache_check_revision = title->cue_revision;
                data->cue_cache_check_row = requested_cue_row;
                data->last_cue_cache_check = now;
            }
            cache_gate_ready = data->cue_cache_check_ready;
        }

        if (!cache_gate_ready) {
            /* Keep the current cue/frame running, but do not consume the new cue
             * revision. The request is retried after the required prefix/transition
             * is resident in either RAM or SSD. */
            data->waiting_for_cue = true;
            data->dirty = true;
        } else {
            data->force_cue_state_sync = false;
            double loop_end = std::clamp(title->loop_end, title->loop_start, title->duration);
            double pause_time = std::clamp(title->pause_time, 0.0, title->duration);
            const int cue_row_count = live_text_playlist_row_count(*title);
            bool has_pending = title->pending_cue_row >= 0 &&
                               title->pending_cue_row < cue_row_count;
            bool has_current = title->current_cue_row >= 0 &&
                               title->current_cue_row < cue_row_count;
            bool is_uncue = title->cue_uncue_requested && !has_pending && has_current;
            const bool finalize_uncue_without_restart = !has_pending && !has_current &&
                (data->manual_uncue || data->active_cue_row >= 0 ||
                 data->cue_phase == TitleSourceData::CuePhase::OutroOnly);
            if (finalize_uncue_without_restart) {
                /* A second click on a yellow cue finalizes the pending uncue.
                 * Consume the revision as a terminal state instead of falling
                 * through to the ordinary cue-start branch, which would reset
                 * playhead to 0 and replay the title. */
                data->force_cue_state_sync = false;
                data->seen_cue_revision = title->cue_revision;
                data->playing = false;
                data->playback_state.store(TitlePlaybackState::Stopped,
                                           std::memory_order_release);
                data->cue_phase = TitleSourceData::CuePhase::FreeRun;
                data->active_cue_row = -1;
                data->waiting_for_cue = true;
                data->manual_uncue = false;
                data->manual_uncue_floor = -1.0;
                release_active_scene_mask_scenes(data);
                if (title->cue_end_behavior == 1) {
                    set_source_output_visible(data, false,
                                              "cue-finalized-show-nothing");
                } else {
                    set_source_output_visible(data, true,
                                              "cue-finalized-hold-frame");
                    if (title->cue_end_behavior == 2)
                        data->playhead = 0.0;
                }
                data->dirty = true;
            } else {
            data->manual_uncue = is_uncue;
            if (is_uncue)
                data->manual_uncue_floor = std::clamp(data->playhead, 0.0, title->duration);
            else
                data->manual_uncue_floor = -1.0;
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Debug, "CuePlayback", [&]() {
                    std::ostringstream stream;
                    stream << "consume revision title="
                           << ExternalDataLog::sanitize(title->id, 160)
                           << " revision=" << title->cue_revision
                           << " playbackMode=" << title->playback_mode
                           << " current=" << title->current_cue_row
                           << " pending=" << title->pending_cue_row
                           << " active=" << data->active_cue_row
                           << " hasCurrent=" << (has_current ? 1 : 0)
                           << " hasPending=" << (has_pending ? 1 : 0)
                           << " uncue=" << (is_uncue ? 1 : 0)
                           << " playhead=" << data->playhead;
                    return stream.str();
                });
            if (title->playback_mode == 1) {
                if (has_pending) {
                    data->playhead = loop_end;
                    data->cue_phase = TitleSourceData::CuePhase::OutroThenIntro;
                } else if (is_uncue) {
                    /* Manual uncue releases the title from its current frame.
                     * Do not seek to the configured loop boundary: the outro
                     * must continue naturally from the exact on-air position. */
                    data->cue_phase = TitleSourceData::CuePhase::OutroOnly;
                } else {
                    data->playhead = 0.0;
                    data->cue_phase = TitleSourceData::CuePhase::IntroLoop;
                }
            } else if (title->playback_mode == 2 && (has_pending || is_uncue)) {
                /* Cue-to-cue transitions still start from the authored pause
                 * point. Manual uncue, however, resumes from the current
                 * playhead (normally the held pause frame, or an earlier frame
                 * when toggled during the intro). */
                if (has_pending)
                    data->playhead = pause_time;
                data->cue_phase = has_pending
                    ? TitleSourceData::CuePhase::OutroThenIntro
                    : TitleSourceData::CuePhase::OutroOnly;
            } else if (is_uncue) {
                /* Manual uncue is a terminal transition for every playback
                 * mode. Never return to FreeRun here: that would set
                 * playing=true below and restart/continue the title as normal
                 * playback. OutroOnly preserves the current playhead and
                 * funnels all modes through the single end-behavior
                 * finalization path. */
                data->cue_phase = TitleSourceData::CuePhase::OutroOnly;
            } else {
                if (has_pending) {
                    const int committed_row = title->pending_cue_row;
                    apply_live_text_row(title, committed_row);
                    title->current_cue_row = committed_row;
                    title->pending_cue_row = -1;
                    has_current = true;
                    ExternalDataLog::write_lazy(
                        ExternalDataLogLevel::Info, "CuePlayback", [&]() {
                            std::ostringstream stream;
                            stream << "committed pending row immediately title="
                                   << ExternalDataLog::sanitize(title->id, 160)
                                   << " row=" << committed_row
                                   << " rowId=" << ExternalDataLog::sanitize(
                                          live_text_row_id(*title, committed_row), 240)
                                   << " playbackMode=" << title->playback_mode;
                            return stream.str();
                        });
                    TitleDataStore::instance().touch_runtime_change();
                }
                if (!is_uncue)
                    data->playhead = 0.0;
                data->cue_phase = TitleSourceData::CuePhase::FreeRun;
            }
            ExternalDataLog::write_lazy(
                ExternalDataLogLevel::Info, "CuePlayback", [&]() {
                    std::ostringstream stream;
                    stream << "revision action title="
                           << ExternalDataLog::sanitize(title->id, 160)
                           << " playbackMode=" << title->playback_mode
                           << " phase=" << static_cast<int>(data->cue_phase)
                           << " current=" << title->current_cue_row
                           << " pending=" << title->pending_cue_row
                           << " playhead=" << data->playhead;
                    return stream.str();
                });
            if (has_current)
                data->active_cue_row = title->current_cue_row;
            data->seen_cue_revision = title->cue_revision;
            data->playback_reverse = false;
            data->waiting_for_cue = false;
            data->playing = true;
            data->playback_state.store(TitlePlaybackState::Playing,
                                       std::memory_order_release);
            if (!data->manual_uncue)
                set_source_output_visible(data, true, "cue-started");
            data->dirty = true;
            }
        }
    }

    const bool has_clock_layer = title_has_clock_layer(title);
    const bool has_ticker_layer = title_has_ticker_layer(title);
    const bool has_independent_asset_layer =
        title_has_independent_asset_layer(title);
    const bool has_timeline_animation = title_has_animation(title);
    const bool static_clock_title = has_clock_layer && !has_timeline_animation;

    if (data->playing && (!static_clock_title ||
                          data->cue_phase != TitleSourceData::CuePhase::FreeRun)) {
        double dt = (double)seconds;
        double duration = std::max(0.001, title->duration);
        if (data->manual_uncue && data->manual_uncue_floor >= 0.0 &&
            data->playhead < data->manual_uncue_floor) {
            data->playhead = data->manual_uncue_floor;
        }
        double loop_start = std::clamp(title->loop_start, 0.0, title->duration);
        double loop_end = std::clamp(title->loop_end, loop_start, title->duration);

        const bool ping_pong_loop = title->playback_mode == 1 && title->loop_type == 1 &&
                                    (data->cue_phase == TitleSourceData::CuePhase::FreeRun ||
                                     data->cue_phase == TitleSourceData::CuePhase::IntroLoop);
        if (ping_pong_loop) {
            data->playhead += data->playback_reverse ? -dt : dt;
        } else {
            data->playhead += dt;
        }

        if (data->cue_phase == TitleSourceData::CuePhase::IntroLoop && loop_end > loop_start) {
            double loop_len = std::max(0.001, loop_end - loop_start);
            if (title->loop_type == 1) {
                if (!data->playback_reverse && data->playhead >= loop_end) {
                    clear_cue_persistence_transition(title);
                    data->playhead = loop_end - std::fmod(data->playhead - loop_end, loop_len);
                    data->playback_reverse = true;
                } else if (data->playback_reverse && data->playhead <= loop_start) {
                    data->playhead = loop_start + std::fmod(loop_start - data->playhead, loop_len);
                    data->playback_reverse = false;
                }
            } else if (data->playhead >= loop_end) {
                clear_cue_persistence_transition(title);
                data->playhead = loop_start + std::fmod(data->playhead - loop_start, loop_len);
            }
        } else if ((data->cue_phase == TitleSourceData::CuePhase::OutroThenIntro ||
                    data->cue_phase == TitleSourceData::CuePhase::OutroOnly) &&
                   data->playhead >= title->duration) {
            double next_intro_time = std::max(0.0, data->playhead - title->duration);
            if (data->cue_phase == TitleSourceData::CuePhase::OutroOnly) {
                data->playhead = title->duration;
                data->playing = false;
                data->playback_state.store(TitlePlaybackState::Stopped,
                                           std::memory_order_release);
                data->cue_phase = TitleSourceData::CuePhase::FreeRun;
                /* The row remains cued until this exact point.  Once the
                 * outro has completed, clear the runtime cue status and apply
                 * the title's configured end behavior instead of forcing every
                 * manual uncue to hide the source. */
                title->current_cue_row = -1;
                title->pending_cue_row = -1;
                title->cue_uncue_requested = false;
                title->cue_persistence_transition = false;
                title->cue_persistent_text_columns.clear();
                data->active_cue_row = -1;
                data->waiting_for_cue = true;
                release_active_scene_mask_scenes(data);
                if (title->cue_end_behavior == 1) {
                    set_source_output_visible(data, false,
                                              "cue-ended-show-nothing");
                } else {
                    set_source_output_visible(data, true,
                                              "cue-ended-hold-frame");
                    if (title->cue_end_behavior == 2)
                        data->playhead = 0.0;
                }
                data->manual_uncue = false;
                data->manual_uncue_floor = -1.0;
                TitleDataStore::instance().touch_runtime_change();
            } else {
                if (title->pending_cue_row >= 0 && title->pending_cue_row < (int)title->live_text_rows.size()) {
                    const int committed_row = title->pending_cue_row;
                    apply_live_text_row(title, committed_row);
                    title->current_cue_row = committed_row;
                    title->pending_cue_row = -1;
                    data->active_cue_row = title->current_cue_row;
                    ExternalDataLog::write_lazy(
                        ExternalDataLogLevel::Info, "CuePlayback", [&]() {
                            std::ostringstream stream;
                            stream << "committed pending row after outro title="
                                   << ExternalDataLog::sanitize(title->id, 160)
                                   << " row=" << committed_row
                                   << " rowId=" << ExternalDataLog::sanitize(
                                          live_text_row_id(*title, committed_row), 240)
                                   << " playbackMode=" << title->playback_mode;
                            return stream.str();
                        });
                    TitleDataStore::instance().touch_runtime_change();
                }
                if (title->playback_mode == 1) {
                    if (loop_end > loop_start && next_intro_time >= loop_end) {
                        next_intro_time = loop_start + std::fmod(next_intro_time - loop_start,
                                                                 std::max(0.001, loop_end - loop_start));
                    }
                    data->playhead = std::clamp(next_intro_time, 0.0, title->duration);
                    data->cue_phase = TitleSourceData::CuePhase::IntroLoop;
                } else {
                    data->playhead = 0.0;
                    data->cue_phase = TitleSourceData::CuePhase::FreeRun;
                }
            }
            data->playback_reverse = false;
        } else if (data->cue_phase == TitleSourceData::CuePhase::FreeRun) {
            if (title->playback_mode == 1) {
                double loop_len = std::max(0.001, loop_end - loop_start);
                if (loop_end <= loop_start + 0.0001) {
                    if (data->playhead >= title->duration)
                        data->playhead = std::fmod(data->playhead, duration);
                } else if (title->loop_type == 1) {
                    if (!data->playback_reverse && data->playhead >= loop_end) {
                        data->playhead = loop_end - std::fmod(data->playhead - loop_end, loop_len);
                        data->playback_reverse = true;
                    } else if (data->playback_reverse && data->playhead <= loop_start) {
                        data->playhead = loop_start + std::fmod(loop_start - data->playhead, loop_len);
                        data->playback_reverse = false;
                    }
                } else if (data->playhead >= loop_end) {
                    data->playhead = loop_start + std::fmod(data->playhead - loop_end, loop_len);
                }
            } else if (title->playback_mode == 2) {
                double pause_time = std::clamp(title->pause_time, 0.0, title->duration);
                if (data->playhead >= pause_time) {
                    data->playhead = pause_time;
                    data->playing = false;
                    data->playback_state.store(TitlePlaybackState::Paused,
                                               std::memory_order_release);
                    clear_cue_persistence_transition(title);
                }
            } else if (data->playhead >= title->duration) {
                data->playhead = title->duration;
                data->playing  = false;
                data->playback_state.store(TitlePlaybackState::Stopped,
                                           std::memory_order_release);
                if (title->current_cue_row >= 0 || title->pending_cue_row >= 0 || data->active_cue_row >= 0) {
                    /* Automatic completion is an uncue just like a manual
                     * toggle-off.  Always clear the runtime cue identity, then
                     * keep/hide the source according to When cue ends. */
                    title->current_cue_row = -1;
                    title->pending_cue_row = -1;
                    title->cue_persistence_transition = false;
                    title->cue_persistent_text_columns.clear();
                    data->active_cue_row = -1;
                    data->waiting_for_cue = true;
                    release_active_scene_mask_scenes(data);
                    if (title->cue_end_behavior == 1) {
                        set_source_output_visible(data, false,
                                                  "play-once-show-nothing");
                    } else if (title->cue_end_behavior == 2) {
                        data->playhead = 0.0;
                        set_source_output_visible(data, true,
                                                  "play-once-show-first-frame");
                    } else {
                        set_source_output_visible(data, true,
                                                  "play-once-hold-last-frame");
                    }
                    title->cue_uncue_requested = false;
                    data->manual_uncue = false;
                    data->manual_uncue_floor = -1.0;
                    TitleDataStore::instance().touch_runtime_change();
                }
            }
        }
        data->dirty = true;
    }

    /* Publish the exact source playhead plus a wall-clock cue elapsed timer.
     * This remains active while Pause/Loop titles hold their timeline and is
     * cleared as soon as the source is inactive or the cue actually ends. */
    publish_live_cue_runtime(data, title);

    if (has_ticker_layer || has_independent_asset_layer)
        data->dirty = true;

    if (static_clock_title || (!data->playing && has_clock_layer)) {
        auto now = std::chrono::steady_clock::now();
        if (now - data->last_clock_refresh >= std::chrono::seconds(1)) {
            data->last_clock_refresh = now;
            data->dirty = true;
        }
    }

    const uint64_t revision = source_model_revision(data);
    if (data->audio_runtime) {
        data->audio_runtime->bind_title(title, revision);
        const bool audible = data->editor_audio_preview
            ? obs_source_active(data->source)
            : (data->output_visible.load(std::memory_order_acquire) &&
               data->shown_on_display.load(std::memory_order_acquire) &&
               obs_source_active(data->source));
        data->audio_runtime->transport(data->playhead, data->playing, audible,
                                       false, data->playback_reverse);
        data->audio_runtime->pump();
    }
    const uint64_t external_revision = pending_external_revision;
    const bool store_revision_changed =
        revision != data->seen_store_revision;
    const bool external_revision_changed =
        external_revision != data->seen_external_data_revision;
    if (store_revision_changed || external_revision_changed) {
        data->seen_store_revision = revision;
        data->seen_external_data_revision = external_revision;
        const QString next_visual_identity =
            CacheManager::instance().contentHashForTitle(*title);
        const bool had_visual_identity =
            !data->visual_identity_hash.isEmpty();
        const bool visual_identity_changed =
            had_visual_identity &&
            data->visual_identity_hash != next_visual_identity;
        const bool external_visual_change =
            external_revision_changed &&
            (!had_visual_identity || visual_identity_changed);
        if (visual_identity_changed) {
            ++data->visual_model_revision;
            request_source_presentation_reset(data,
                                              "title-visual-identity-changed");
            apply_source_presentation_reset(data,
                                            "title-visual-identity-changed");
        }
        data->visual_identity_hash = next_visual_identity;
        data->cached_content_hash = next_visual_identity;
        data->cache_hash_revision = revision;

        /* Authored/store changes retain the existing full reconciliation path.
         * External-only changes refresh GPU/cache state solely when the resolved
         * visual value changed; unrelated fields and equivalent fallbacks do no
         * rendering or cache invalidation. */
        if (store_revision_changed && data->source) {
            obs_data_t *settings = obs_source_get_settings(data->source);
            if (settings) {
                const bool keep_scene_masks_foreground =
                    data->scene_mask_foreground_active;
                if (keep_scene_masks_foreground)
                    release_active_scene_mask_scenes(data);
                refresh_scene_mask_configs(data, settings);
                if (keep_scene_masks_foreground)
                    sync_scene_mask_scenes_for_cue(data, title);
                obs_data_release(settings);
            }
        }
        if (store_revision_changed || external_visual_change) {
            data->effect_layer_cache.clear();
            data->dirty = true;
        }
    }

    sync_scene_mask_scenes_for_cue(data, title);

    if (!data->output_visible.load(std::memory_order_acquire)) {
        data->dirty = false;
        return;
    }

    /* A static source may have rendered a live fallback and become clean while
     * the cache worker was still producing the same frame. Wake it when that
     * exact frame arrives so prerendered output is adopted without waiting for
     * an unrelated model or cue change. */
    const int current_cache_frame =
        CacheManager::instance().frameIndexForTitleTime(*title,
                                                        data->playhead);
    if (take_source_cache_wake_frame(data->cache_wake_state, data->title_id,
                                     current_cache_frame)) {
        data->dirty = true;
        if (TitlePreferences::cache_playback_logging_enabled()) {
            FXM_LOG_INFO(
                "CachePlayback",
                QStringLiteral("consumer=source action=frame-ready-wakeup title=%1 time=%2 frame=%3")
                    .arg(QString::fromStdString(data->title_id))
                    .arg(data->playhead, 0, 'f', 6)
                    .arg(current_cache_frame));
        }
    }

    /* Static titles keep their last GPU graph result. Avoid cache lookups,
     * title snapshots and layer-key walks on every OBS video tick when neither
     * the timeline, clock/ticker content nor the model changed. Scene masks are
     * rendered independently in video_render and therefore remain live. */
    if (!data->dirty && !data->first_frame_pending)
        return;

    /* Live rendering stays inside one GPU session. A completed prerender frame
     * may be submitted as the session's final texture; cache misses fall back
     * only to the real-time GPU graph, never to the retired CPU compositor. */
    data->tex_w = clamped_source_dimension(title->width);
    data->tex_h = clamped_source_dimension(title->height);
    if (data->gpu_render_session) {
        bool submitted_cached_frame = false;
        CacheManager &cache = CacheManager::instance();
        const CachePlaybackSettings playback = cache.playbackSettings();
        const TitleCacheability cacheability = cache.titleCacheability(title);
        const bool destination_composite =
            title_requires_destination_compositing(*title);
        /* A flattened cache frame has already lost the destination-dependent
         * layer operations. Screen/Multiply/Overlay/Color and adjustment
         * layers must be evaluated against the actual canvas/scene backdrop. */
        if (!destination_composite && cache.cacheEnabled() &&
            cacheability != TitleCacheability::NonCacheable) {
            if (data->cached_content_hash.isEmpty() ||
                data->cache_hash_revision != revision) {
                data->cached_content_hash = cache.contentHashForTitle(*title);
                data->cache_hash_revision = revision;
            }

            int cached_cue_row = title->current_cue_row;
            if (cached_cue_row < 0 && data->manual_uncue &&
                data->cue_phase == TitleSourceData::CuePhase::OutroOnly)
                cached_cue_row = data->active_cue_row;

            /* Live-cue prerenders use a cue-applied title snapshot and
             * therefore a different content-addressed GPU key from the base
             * title. Asking for the base key here could successfully return a
             * resident frame with the wrong text/persistence state, so choose
             * the same cue-aware key family as the QImage fallback. */
            const QString gpu_cache_token = cached_cue_row >= 0 &&
                    cached_cue_row < static_cast<int>(title->live_text_rows.size())
                ? cache.requestLiveCueFrameGpuToken(
                      title, cached_cue_row, data->playhead, false)
                : cache.requestFrameGpuToken(
                      title, data->playhead, false, data->cached_content_hash);
            QImage cached;
            if (gpu_cache_token.isEmpty()) {
                if (cached_cue_row >= 0 &&
                    cached_cue_row < static_cast<int>(title->live_text_rows.size())) {
                    cached = cache.requestLiveCueFrameRealtime(
                        title, cached_cue_row, data->playhead,
                        data->cached_content_hash);
                } else {
                    cached = cache.requestFrameRealtime(
                        title, data->playhead, data->cached_content_hash);
                }
            }

            if (TitlePreferences::cache_playback_logging_enabled()) {
                FXM_LOG_INFO("CachePlayback", QStringLiteral("consumer=source title=%1 time=%2 cueRow=%3 payload=%4 cacheKey=%5 size=%6x%7 cachedOnly=%8")
                    .arg(QString::fromStdString(title->id)).arg(data->playhead, 0, 'f', 6)
                    .arg(cached_cue_row).arg(cached.isNull() ? QStringLiteral("null") : QStringLiteral("ready"))
                    .arg(cached.isNull() ? 0 : cached.cacheKey()).arg(cached.width()).arg(cached.height())
                    .arg(playback.cached_frames_only));
            }

            if (!gpu_cache_token.isEmpty()) {
                if (cacheability == TitleCacheability::PartiallyCacheable) {
                    const TitleDynamicLayerAnalysis analysis =
                        analyze_title_dynamic_layers(*title);
                    if (analysis.has_cacheable_prefix) {
                        submitted_cached_frame =
                            title_gpu_render_session_submit_gpu_cached_prefix(
                                data->gpu_render_session, *title,
                                gpu_cache_token.toStdString(), data->playhead,
                                analysis.first_dynamic_layer,
                                data->visual_model_revision);
                    }
                } else {
                    submitted_cached_frame =
                        title_gpu_render_session_submit_gpu_cached_frame(
                            data->gpu_render_session, *title,
                            gpu_cache_token.toStdString(),
                            data->visual_model_revision);
                }
                if (TitlePreferences::cache_playback_logging_enabled()) {
                    FXM_LOG_INFO("CachePlayback", QStringLiteral(
                        "consumer=source action=%1 title=%2 time=%3 gpuToken=%4")
                        .arg(submitted_cached_frame
                                 ? QStringLiteral("submit-gpu-ram")
                                 : QStringLiteral("reject-gpu-ram"))
                        .arg(QString::fromStdString(title->id))
                        .arg(data->playhead, 0, 'f', 6)
                        .arg(gpu_cache_token));
                }
            } else if (!cached.isNull()) {
                if (cacheability == TitleCacheability::PartiallyCacheable) {
                    const TitleDynamicLayerAnalysis analysis =
                        analyze_title_dynamic_layers(*title);
                    if (analysis.has_cacheable_prefix) {
                        submitted_cached_frame =
                            title_gpu_render_session_submit_cached_prefix(
                                data->gpu_render_session, *title, cached,
                                data->playhead, analysis.first_dynamic_layer,
                                data->visual_model_revision);
                        if (TitlePreferences::cache_playback_logging_enabled())
                            FXM_LOG_INFO("CachePlayback", QStringLiteral("consumer=source action=%1 title=%2 time=%3 firstDynamicLayer=%4 cacheKey=%5")
                                .arg(submitted_cached_frame ? QStringLiteral("submit-prefix")
                                                           : QStringLiteral("reject-prefix"))
                                .arg(QString::fromStdString(title->id)).arg(data->playhead, 0, 'f', 6)
                                .arg(analysis.first_dynamic_layer).arg(cached.cacheKey()));
                    }
                } else {
                    submitted_cached_frame =
                        title_gpu_render_session_submit_final_frame(
                            data->gpu_render_session, *title, cached,
                            data->visual_model_revision);
                    if (TitlePreferences::cache_playback_logging_enabled())
                        FXM_LOG_INFO("CachePlayback", QStringLiteral("consumer=source action=%1 title=%2 time=%3 cacheKey=%4")
                            .arg(submitted_cached_frame ? QStringLiteral("submit-final")
                                                       : QStringLiteral("reject-final"))
                            .arg(QString::fromStdString(title->id)).arg(data->playhead, 0, 'f', 6).arg(cached.cacheKey()));
                }
                if (!submitted_cached_frame) {
                    cache.rejectFramePayload(title, data->playhead,
                                             data->cached_content_hash);
                    if (playback.cached_frames_only &&
                        !data->first_frame_pending) {
                        /* Once a valid frame has been published, cached-only
                         * playback holds it until the repaired payload arrives. */
                        data->dirty = true;
                        return;
                    }
                    /* A source with no published texture must never remain
                     * permanently transparent. Bootstrap one live poster frame;
                     * subsequent cache misses still hold the published result. */
                }
            } else if (playback.cached_frames_only &&
                       !data->first_frame_pending) {
                /* Keep the last valid GPU texture while the worker prepares the
                 * requested frame. Do not synchronously render on video_tick. */
                data->dirty = true;
                return;
            } else if (playback.cached_frames_only &&
                       data->first_frame_pending &&
                       TitlePreferences::cache_playback_logging_enabled()) {
                FXM_LOG_WARNING("CachePlayback", QStringLiteral(
                    "consumer=source action=bootstrap-live-poster title=%1 time=%2 reason=no-published-cache-frame")
                    .arg(QString::fromStdString(title->id))
                    .arg(data->playhead, 0, 'f', 6));
            }
        }

        if (!submitted_cached_frame) {
            title_gpu_render_session_update(data->gpu_render_session, *title,
                                            data->playhead,
                                            data->visual_model_revision);
        }

        /* Scene-mask layers are auxiliary GPU inputs. They must remain
         * available even when the visible title is supplied by a completed
         * cached frame, but they never force the cached artwork itself to be
         * recomposited. */
        if (!data->scene_masks.empty()) {
            std::vector<std::string> mask_layer_ids;
            mask_layer_ids.reserve(data->scene_masks.size());
            for (const auto &cfg : data->scene_masks) {
                if (!cfg.layer_id.empty())
                    mask_layer_ids.push_back(cfg.layer_id);
            }
            title_gpu_render_session_prepare_auxiliary_layers(
                data->gpu_render_session, *title, data->playhead,
                data->visual_model_revision,
                mask_layer_ids);
        }
        /* video_tick only prepares the GPU graph. The source becomes clean
         * after video_render proves that the session actually produced and
         * drew a texture. This closes the startup/cache race where a failed
         * first render was marked clean forever. */
        data->dirty = data->first_frame_pending;
    }
}



static void apply_layer_world_transform_gs(const Title &title, const Layer &layer,
                                           double title_time, int /*depth*/ = 0)
{
    const QTransform transform = layer_world_transform_qt(title, layer, title_time);

    /* OBS stores affine basis vectors in matrix4::x/y and translation in
     * matrix4::t.  Apply the complete QTransform in one multiplication rather
     * than decomposing it into translate/rotate/scale: nested non-uniform
     * parent transforms can legitimately produce shear, which a decomposition
     * would silently discard. */
    struct matrix4 affine;
    matrix4_identity(&affine);
    vec4_set(&affine.x,
             static_cast<float>(transform.m11()),
             static_cast<float>(transform.m12()),
             0.0f, static_cast<float>(transform.m13()));
    vec4_set(&affine.y,
             static_cast<float>(transform.m21()),
             static_cast<float>(transform.m22()),
             0.0f, static_cast<float>(transform.m23()));
    vec4_set(&affine.t,
             static_cast<float>(transform.dx()),
             static_cast<float>(transform.dy()),
             0.0f, static_cast<float>(transform.m33()));
    gs_matrix_mul(&affine);
}


/* ══════════════════════════════════════════════════════════════════
 *  GPU-only title compositor
 *
 *  CPU work is limited to producing reusable, transform-neutral layer
 *  rasters (text glyph coverage, vector tessellation and decoded images).
 *  Full-frame composition, transforms, masks, blend modes, stack effects and
 *  temporal accumulation remain in OBS graphics resources.  Editor and live
 *  output share this exact session implementation.
 * ══════════════════════════════════════════════════════════════════ */

static constexpr const char *kGpuFrameBlitEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v)
{
    VertDataOut o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.uv = v.uv;
    return o;
}
float4 PSBlit(VertDataOut v) : TARGET
{
    return image.Sample(textureSampler, v.uv);
}
technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v);
        pixel_shader = PSBlit(v);
    }
}
)";

/* The temporal resolve uses the normalized premultiplied shutter exposure
 * directly. A weighted-average alpha is inherently bounded by the maximum
 * sample alpha, so a separate max-coverage texture cannot alter this resolve.
 * Avoiding that redundant texture/pass cuts temporal compositing work without
 * changing the authored shutter exposure. */
static constexpr const char *kGpuTemporalCompositeEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d accumulatedImage;
uniform texture2d sampleImage;
uniform float sampleWeight;
uniform int hasAccumulated;

sampler_state textureSampler {
    Filter = Linear;
    AddressU = Clamp;
    AddressV = Clamp;
};

struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };

VertDataOut VSDefault(VertDataIn v)
{
    VertDataOut o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.uv = v.uv;
    return o;
}

float4 PSTemporalComposite(VertDataOut v) : TARGET
{
    float4 result = hasAccumulated != 0
        ? accumulatedImage.Sample(textureSampler, v.uv)
        : float4(0.0, 0.0, 0.0, 0.0);
    result += sampleImage.Sample(textureSampler, v.uv) * sampleWeight;
    result = saturate(result);
    result.rgb = min(result.rgb, float3(result.a, result.a, result.a));
    return result;
}

float4 PSTemporalResolve(VertDataOut v) : TARGET
{
    float4 exposure = accumulatedImage.Sample(textureSampler, v.uv);
    float4 currentFrame = sampleImage.Sample(textureSampler, v.uv);

    /* A shutter exposure is a time average, not alpha-composited history. */
    float exposureAlpha = saturate(exposure.a);
    float wetAlpha = exposureAlpha;
    float3 exposureStraight = exposureAlpha > 0.00001
        ? saturate(exposure.rgb / exposureAlpha)
        : float3(0.0, 0.0, 0.0);
    float4 wet = float4(exposureStraight * wetAlpha, wetAlpha);
    float mixAmount = saturate(sampleWeight);
    float4 result = lerp(currentFrame, wet, mixAmount);
    result = saturate(result);
    result.rgb = min(result.rgb, float3(result.a, result.a, result.a));
    return result;
}

float4 PSTemporalAccumulate(VertDataOut v) : TARGET
{
    float4 result = sampleImage.Sample(textureSampler, v.uv) * sampleWeight;
    result = saturate(result);
    result.rgb = min(result.rgb, float3(result.a, result.a, result.a));
    return result;
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v);
        pixel_shader = PSTemporalComposite(v);
    }
}

technique Resolve
{
    pass
    {
        vertex_shader = VSDefault(v);
        pixel_shader = PSTemporalResolve(v);
    }
}

technique Accumulate
{
    pass
    {
        vertex_shader = VSDefault(v);
        pixel_shader = PSTemporalAccumulate(v);
    }
}
)";

/* Keep the overwhelmingly common unlit path deliberately small.  Compiling the
 * lighting/shadow implementation as part of every layer copy made D3DCompiler
 * spend minutes optimizing code that ordinary 2D titles never execute. */
static constexpr const char *kGpuLayerCopyEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d transitionMatte;
uniform float weight;
uniform float wipeProgress;
uniform float wipeSoftness;
uniform int wipeDirection;
uniform int transitionType;
uniform int blocksColumns;
uniform int blocksRows;
uniform float randomSeed;
uniform int transitionMatteEnabled;
uniform int imageChannel;
uniform int transitionInvert;
uniform int transitionClockwise;
uniform float2 transitionCenter;
uniform float transitionRotation;
uniform float transitionAspect;
uniform int transitionProfile;
uniform int imageClipEnabled;
uniform float4 imageClipRect;
uniform float4 imageCornerRadii;
uniform float2 imageLogicalSize;
uniform float2 sourceTexelSize;
uniform int imageScaleFilter;
uniform texture2d postProjectionMask;
uniform int postProjectionMaskEnabled;
uniform int postProjectionMaskMode;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 clipPos : TEXCOORD1; };
VertDataOut VSDefault(VertDataIn v)
{
    VertDataOut o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.clipPos = o.pos;
    o.uv = v.uv;
    return o;
}
float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32 + randomSeed);
    return frac(p.x * p.y);
}
float directional_wipe(float2 uv, float reveal, float feather)
{
    if (wipeDirection == 2) {
        float edge = 1.0 - reveal;
        return wipeSoftness > 0.000001 ? smoothstep(edge, edge + feather, uv.x) : step(edge, uv.x);
    }
    if (wipeDirection == 3) {
        float edge = 1.0 - reveal;
        return wipeSoftness > 0.000001 ? smoothstep(edge, edge + feather, uv.y) : step(edge, uv.y);
    }
    if (wipeDirection == 4)
        return wipeSoftness > 0.000001 ? 1.0 - smoothstep(reveal - feather, reveal, uv.y) : 1.0 - step(reveal, uv.y);
    return wipeSoftness > 0.000001 ? 1.0 - smoothstep(reveal - feather, reveal, uv.x) : 1.0 - step(reveal, uv.x);
}
float transition_alpha(float2 uv)
{
    float reveal = clamp(wipeProgress, 0.0, 1.0);
    if (reveal >= 0.999999)
        return 1.0;
    float feather = max(wipeSoftness, 0.000001);
    float alpha = 1.0;
    if (transitionType == 14) {
        float2 grid = float2(max(blocksColumns, 1), max(blocksRows, 1));
        float threshold = hash21(floor(uv * grid));
        if (transitionInvert != 0) threshold = 1.0 - threshold;
        alpha = wipeSoftness > 0.000001 ? smoothstep(threshold - feather, threshold + feather, reveal) : step(threshold, reveal);
    } else if (transitionType == 15) {
        float matte = uv.x;
        if (transitionMatteEnabled != 0) {
            float4 m = transitionMatte.Sample(textureSampler, uv);
            if (imageChannel == 1) matte = m.a;
            else if (imageChannel == 2) matte = m.r;
            else if (imageChannel == 3) matte = m.g;
            else if (imageChannel == 4) matte = m.b;
            else matte = dot(m.rgb, float3(0.2126, 0.7152, 0.0722));
        }
        if (transitionInvert != 0) matte = 1.0 - matte;
        alpha = smoothstep(matte - feather, matte + feather, reveal);
    } else if (transitionType == 16) {
        float2 p = uv - transitionCenter;
        float angle = frac(atan2(p.y, p.x) / 6.28318530718 + 1.5 + transitionRotation / 360.0);
        if (transitionClockwise == 0) angle = 1.0 - angle;
        if (transitionInvert != 0) angle = 1.0 - angle;
        alpha = smoothstep(angle - feather, angle + feather, reveal);
    } else if (transitionType == 17) {
        float2 p = (uv - transitionCenter) * 2.0;
        p.x *= max(transitionAspect, 0.01);
        float d = length(p) / 1.41421356;
        if (transitionProfile == 1) d = max(abs(p.x), abs(p.y));
        else if (transitionProfile == 2) d = (abs(p.x) + abs(p.y)) * 0.5;
        d = clamp(d, 0.0, 1.0);
        if (transitionInvert != 0) d = 1.0 - d;
        alpha = smoothstep(d - feather, d + feather, reveal);
    } else if (transitionType == 18) {
        float2 p = uv - transitionCenter;
        float radians = transitionRotation * 0.01745329252;
        float2 q = float2(p.x * cos(radians) - p.y * sin(radians), p.x * sin(radians) + p.y * cos(radians));
        q.x *= max(transitionAspect, 0.01);
        float matte = q.x + 0.5;
        if (transitionProfile == 1) matte = length(q) * 1.41421356;
        else if (transitionProfile == 2) matte = abs(q.x) + abs(q.y);
        matte = clamp(matte, 0.0, 1.0);
        if (transitionInvert != 0) matte = 1.0 - matte;
        alpha = smoothstep(matte - feather, matte + feather, reveal);
    } else if (wipeDirection != 0) {
        alpha = directional_wipe(uv, reveal, feather);
    }
    return clamp(alpha, 0.0, 1.0);
}
float rounded_box_alpha(float2 p, float4 rect, float4 radii)
{
    float2 rectMin = rect.xy;
    float2 rectMax = rect.xy + rect.zw;
    if (p.x < rectMin.x || p.y < rectMin.y || p.x > rectMax.x || p.y > rectMax.y)
        return 0.0;
    float2 local = p - rectMin;
    float2 size = max(rect.zw, float2(0.0001, 0.0001));
    float maxRadius = min(size.x, size.y) * 0.5;
    float4 r = clamp(radii, 0.0, maxRadius);
    float radius = 0.0;
    float2 center = float2(0.0, 0.0);
    if (local.x <= r.x && local.y <= r.x) { radius = r.x; center = float2(radius, radius); }
    else if (local.x >= size.x - r.y && local.y <= r.y) { radius = r.y; center = float2(size.x - radius, radius); }
    else if (local.x >= size.x - r.z && local.y >= size.y - r.z) { radius = r.z; center = float2(size.x - radius, size.y - radius); }
    else if (local.x <= r.w && local.y >= size.y - r.w) { radius = r.w; center = float2(radius, size.y - radius); }
    else return 1.0;
    if (radius <= 0.0001) return 1.0;
    return 1.0 - smoothstep(max(0.0, radius - 1.0), radius + 1.0, length(local - center));
}
float projected_mask_alpha(VertDataOut v)
{
    if (postProjectionMaskEnabled == 0 || abs(v.clipPos.w) <= 0.000001)
        return 1.0;
    float2 ndc = v.clipPos.xy / v.clipPos.w;
    float2 screenUv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    float4 m = postProjectionMask.Sample(textureSampler, clamp(screenUv, 0.0, 1.0));
    float value = m.a;
    if (postProjectionMaskMode == 3 || postProjectionMaskMode == 4) {
        float3 straight = m.a > 0.000001 ? m.rgb / m.a : float3(0.0, 0.0, 0.0);
        value = dot(straight, float3(0.2126, 0.7152, 0.0722)) * m.a;
    }
    if (postProjectionMaskMode == 2 || postProjectionMaskMode == 4 || postProjectionMaskMode == 6)
        value = 1.0 - value;
    return clamp(value, 0.0, 1.0);
}
float cubic_weight(float x)
{
    x = abs(x);
    float x2 = x * x;
    float x3 = x2 * x;
    if (x <= 1.0) return 1.5 * x3 - 2.5 * x2 + 1.0;
    if (x < 2.0) return -0.5 * x3 + 2.5 * x2 - 4.0 * x + 2.0;
    return 0.0;
}
float sinc_approx(float x)
{
    x = abs(x);
    if (x < 0.0001) return 1.0;
    float pix = 3.14159265359 * x;
    return sin(pix) / pix;
}
float lanczos_weight(float x)
{
    x = abs(x);
    if (x >= 3.0) return 0.0;
    return sinc_approx(x) * sinc_approx(x / 3.0);
}
float4 sample_cubic(float2 uv)
{
    float2 texel = max(sourceTexelSize, float2(0.000001, 0.000001));
    float2 size = 1.0 / texel;
    float2 pixel = uv * size - 0.5;
    float2 base = floor(pixel);
    float2 f = pixel - base;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float total = 0.0;
    for (int y = -1; y <= 2; ++y) {
        float wy = cubic_weight(float(y) - f.y);
        for (int x = -1; x <= 2; ++x) {
            float w = cubic_weight(float(x) - f.x) * wy;
            sum += image.Sample(pointSampler, clamp((base + float2(x, y) + 0.5) * texel, 0.0, 1.0)) * w;
            total += w;
        }
    }
    return total > 0.000001 ? sum / total : image.Sample(textureSampler, uv);
}
float4 sample_lanczos(float2 uv)
{
    float2 texel = max(sourceTexelSize, float2(0.000001, 0.000001));
    float2 size = 1.0 / texel;
    float2 pixel = uv * size - 0.5;
    float2 base = floor(pixel);
    float2 f = pixel - base;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float total = 0.0;
    for (int y = -2; y <= 3; ++y) {
        float wy = lanczos_weight(float(y) - f.y);
        for (int x = -2; x <= 3; ++x) {
            float w = lanczos_weight(float(x) - f.x) * wy;
            sum += image.Sample(pointSampler, clamp((base + float2(x, y) + 0.5) * texel, 0.0, 1.0)) * w;
            total += w;
        }
    }
    return total > 0.000001 ? sum / total : image.Sample(textureSampler, uv);
}
float4 sample_scaled_image(float2 uv)
{
    if (imageScaleFilter == 0) return image.Sample(pointSampler, uv);
    if (imageScaleFilter == 2) return sample_cubic(uv);
    if (imageScaleFilter == 3) return sample_lanczos(uv);
    return image.Sample(textureSampler, uv);
}
float4 layer_color(VertDataOut v)
{
    float clipAlpha = 1.0;
    if (imageClipEnabled != 0)
        clipAlpha = rounded_box_alpha(v.uv * imageLogicalSize, imageClipRect, imageCornerRadii);
    return sample_scaled_image(v.uv) *
        (weight * transition_alpha(v.uv) * clipAlpha * projected_mask_alpha(v));
}
float4 PSLayer(VertDataOut v) : TARGET { return layer_color(v); }
float4 PSDepthLayer(VertDataOut v) : TARGET
{
    float4 color = layer_color(v);
    clip(color.a - 0.0039215686);
    return color;
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSLayer(v); } }
technique DepthDraw { pass { vertex_shader = VSDefault(v); pixel_shader = PSDepthLayer(v); } }
)";

static constexpr const char *kGpuLitLayerCopyEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d transitionMatte;
uniform float weight;
uniform float wipeProgress;
uniform float wipeSoftness;
uniform int wipeDirection;
uniform int transitionType;
uniform int blocksColumns;
uniform int blocksRows;
uniform float randomSeed;
uniform int transitionMatteEnabled;
uniform int imageChannel;
uniform int transitionInvert;
uniform int transitionClockwise;
uniform float2 transitionCenter;
uniform float transitionRotation;
uniform float transitionAspect;
uniform int transitionProfile;
uniform int imageClipEnabled;
uniform float4 imageClipRect;
uniform float4 imageCornerRadii;
uniform float2 imageLogicalSize;
uniform float2 sourceTexelSize;
uniform int imageScaleFilter;
uniform texture2d postProjectionMask;
uniform int postProjectionMaskEnabled;
uniform int postProjectionMaskMode;

/* Development Version 298: opt-in planar PBR lighting.  The four explicit
 * light slots keep the effect portable across OBS' D3D11, OpenGL and Metal
 * shader translators, which have historically differed in dynamic uniform
 * array handling. */
uniform int lightingEnabled;
uniform float3 planeOrigin;
uniform float3 planeAxisU;
uniform float3 planeAxisV;
uniform float3 worldNormal;
uniform float3 cameraWorldPosition;
uniform float4 materialPrimary;       /* ambient, diffuse, specular, shininess */
uniform float4 materialPbr;           /* metallic, roughness, reflection, emissive intensity */
uniform float4 materialEmissive;      /* straight RGBA; alpha scales emission */
uniform float environmentExposure;
uniform texture2d shadowMap0;
uniform int shadowEnabled0;
uniform int shadowKind0;
uniform float4x4 shadowViewProj0;
uniform float3 shadowLightPosition0;
uniform float4 shadowBasisRightCenterX0;
uniform float4 shadowBasisUpCenterY0;
uniform float3 shadowBasisForward0;
uniform float shadowNear0;
uniform float shadowFar0;
uniform float2 shadowProjectionSpan0;
uniform float shadowDarkness0;
uniform float shadowBias0;
uniform float shadowSoftness0;
uniform float2 shadowTexelSize0;
uniform texture2d shadowMap1;
uniform int shadowEnabled1;
uniform int shadowKind1;
uniform float4x4 shadowViewProj1;
uniform float3 shadowLightPosition1;
uniform float4 shadowBasisRightCenterX1;
uniform float4 shadowBasisUpCenterY1;
uniform float3 shadowBasisForward1;
uniform float shadowNear1;
uniform float shadowFar1;
uniform float2 shadowProjectionSpan1;
uniform float shadowDarkness1;
uniform float shadowBias1;
uniform float shadowSoftness1;
uniform float2 shadowTexelSize1;
uniform texture2d shadowMap2;
uniform int shadowEnabled2;
uniform int shadowKind2;
uniform float4x4 shadowViewProj2;
uniform float3 shadowLightPosition2;
uniform float4 shadowBasisRightCenterX2;
uniform float4 shadowBasisUpCenterY2;
uniform float3 shadowBasisForward2;
uniform float shadowNear2;
uniform float shadowFar2;
uniform float2 shadowProjectionSpan2;
uniform float shadowDarkness2;
uniform float shadowBias2;
uniform float shadowSoftness2;
uniform float2 shadowTexelSize2;
uniform texture2d shadowMap3;
uniform int shadowEnabled3;
uniform int shadowKind3;
uniform float4x4 shadowViewProj3;
uniform float3 shadowLightPosition3;
uniform float4 shadowBasisRightCenterX3;
uniform float4 shadowBasisUpCenterY3;
uniform float3 shadowBasisForward3;
uniform float shadowNear3;
uniform float shadowFar3;
uniform float2 shadowProjectionSpan3;
uniform float shadowDarkness3;
uniform float shadowBias3;
uniform float shadowSoftness3;
uniform float2 shadowTexelSize3;
uniform float4 lightPositionType0;
uniform float4 lightDirectionIntensity0;
uniform float4 lightColorEnabled0;
uniform float4 lightParams0;
uniform float lightSourceSize0;
uniform int lightFalloff0;
uniform float4 lightPositionType1;
uniform float4 lightDirectionIntensity1;
uniform float4 lightColorEnabled1;
uniform float4 lightParams1;
uniform float lightSourceSize1;
uniform int lightFalloff1;
uniform float4 lightPositionType2;
uniform float4 lightDirectionIntensity2;
uniform float4 lightColorEnabled2;
uniform float4 lightParams2;
uniform float lightSourceSize2;
uniform int lightFalloff2;
uniform float4 lightPositionType3;
uniform float4 lightDirectionIntensity3;
uniform float4 lightColorEnabled3;
uniform float4 lightParams3;
uniform float lightSourceSize3;
uniform int lightFalloff3;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
sampler_state pointSampler { Filter = Point; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 clipPos : TEXCOORD1; };
VertDataOut VSDefault(VertDataIn v)
{
    VertDataOut o;
    o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj);
    o.clipPos = o.pos;
    o.uv = v.uv;
    return o;
}
float hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32 + randomSeed);
    return frac(p.x * p.y);
}
float directional_wipe(float2 uv, float reveal, float feather)
{
    if (wipeDirection == 2) {
        float edge = 1.0 - reveal;
        return wipeSoftness > 0.000001 ? smoothstep(edge, edge + feather, uv.x) : step(edge, uv.x);
    }
    if (wipeDirection == 3) {
        float edge = 1.0 - reveal;
        return wipeSoftness > 0.000001 ? smoothstep(edge, edge + feather, uv.y) : step(edge, uv.y);
    }
    if (wipeDirection == 4)
        return wipeSoftness > 0.000001 ? 1.0 - smoothstep(reveal - feather, reveal, uv.y) : 1.0 - step(reveal, uv.y);
    return wipeSoftness > 0.000001 ? 1.0 - smoothstep(reveal - feather, reveal, uv.x) : 1.0 - step(reveal, uv.x);
}
float transition_alpha(float2 uv)
{
    float reveal = clamp(wipeProgress, 0.0, 1.0);
    if (reveal >= 0.999999)
        return 1.0;
    float feather = max(wipeSoftness, 0.000001);
    float alpha = 1.0;
    if (transitionType == 14) {
        float2 grid = float2(max(blocksColumns, 1), max(blocksRows, 1));
        float2 cell = floor(uv * grid);
        float threshold = hash21(cell);
        if (transitionInvert != 0) threshold = 1.0 - threshold;
        alpha = wipeSoftness > 0.000001 ? smoothstep(threshold - feather, threshold + feather, reveal) : step(threshold, reveal);
    } else if (transitionType == 15) {
        float matte = uv.x;
        if (transitionMatteEnabled != 0) {
            float4 m = transitionMatte.Sample(textureSampler, uv);
            if (imageChannel == 1) matte = m.a;
            else if (imageChannel == 2) matte = m.r;
            else if (imageChannel == 3) matte = m.g;
            else if (imageChannel == 4) matte = m.b;
            else matte = dot(m.rgb, float3(0.2126, 0.7152, 0.0722));
        }
        if (transitionInvert != 0) matte = 1.0 - matte;
        alpha = smoothstep(matte - feather, matte + feather, reveal);
    } else if (transitionType == 16) {
        float2 p = uv - transitionCenter;
        float angle = atan2(p.y, p.x) / 6.28318530718 + 0.5 + transitionRotation / 360.0;
        angle = frac(angle + 1.0);
        if (transitionClockwise == 0) angle = 1.0 - angle;
        if (transitionInvert != 0) angle = 1.0 - angle;
        alpha = smoothstep(angle - feather, angle + feather, reveal);
    } else if (transitionType == 17) {
        float2 p = (uv - transitionCenter) * 2.0;
        p.x *= max(transitionAspect, 0.01);
        float distanceValue = length(p) / 1.41421356;
        if (transitionProfile == 1) distanceValue = max(abs(p.x), abs(p.y));
        else if (transitionProfile == 2) distanceValue = (abs(p.x) + abs(p.y)) * 0.5;
        distanceValue = clamp(distanceValue, 0.0, 1.0);
        if (transitionInvert != 0) distanceValue = 1.0 - distanceValue;
        alpha = smoothstep(distanceValue - feather, distanceValue + feather, reveal);
    } else if (transitionType == 18) {
        float2 p = uv - transitionCenter;
        float radians = transitionRotation * 0.01745329252;
        float2 q = float2(p.x * cos(radians) - p.y * sin(radians), p.x * sin(radians) + p.y * cos(radians));
        q.x *= max(transitionAspect, 0.01);
        float matte = q.x + 0.5;
        if (transitionProfile == 1) matte = length(q) * 1.41421356;
        else if (transitionProfile == 2) matte = (abs(q.x) + abs(q.y));
        matte = clamp(matte, 0.0, 1.0);
        if (transitionInvert != 0) matte = 1.0 - matte;
        alpha = smoothstep(matte - feather, matte + feather, reveal);
    } else if (wipeDirection != 0) {
        alpha = directional_wipe(uv, reveal, feather);
    }
    return clamp(alpha, 0.0, 1.0);
}
float rounded_box_alpha(float2 p, float4 rect, float4 radii)
{
    float2 rectMin = rect.xy;
    float2 rectMax = rect.xy + rect.zw;
    if (p.x < rectMin.x || p.y < rectMin.y || p.x > rectMax.x || p.y > rectMax.y)
        return 0.0;
    float2 local = p - rectMin;
    float2 size = max(rect.zw, float2(0.0001, 0.0001));
    float maxRadius = min(size.x, size.y) * 0.5;
    float4 clampedRadii = clamp(radii, 0.0, maxRadius);
    float radius = 0.0;
    float2 center = float2(0.0, 0.0);
    if (local.x <= clampedRadii.x && local.y <= clampedRadii.x) { radius = clampedRadii.x; center = float2(radius, radius); }
    else if (local.x >= size.x - clampedRadii.y && local.y <= clampedRadii.y) { radius = clampedRadii.y; center = float2(size.x - radius, radius); }
    else if (local.x >= size.x - clampedRadii.z && local.y >= size.y - clampedRadii.z) { radius = clampedRadii.z; center = float2(size.x - radius, size.y - radius); }
    else if (local.x <= clampedRadii.w && local.y >= size.y - clampedRadii.w) { radius = clampedRadii.w; center = float2(radius, size.y - radius); }
    else return 1.0;
    if (radius <= 0.0001) return 1.0;
    return 1.0 - smoothstep(max(0.0, radius - 1.0), radius + 1.0, length(local - center));
}
float projected_mask_alpha(VertDataOut v)
{
    if (postProjectionMaskEnabled == 0 || abs(v.clipPos.w) <= 0.000001)
        return 1.0;
    float2 ndc = v.clipPos.xy / v.clipPos.w;
    float2 screenUv = float2(ndc.x * 0.5 + 0.5, 1.0 - (ndc.y * 0.5 + 0.5));
    float4 m = postProjectionMask.Sample(textureSampler, clamp(screenUv, 0.0, 1.0));
    float value = m.a;
    if (postProjectionMaskMode == 3 || postProjectionMaskMode == 4) {
        float3 straight = m.a > 0.000001 ? m.rgb / m.a : float3(0.0, 0.0, 0.0);
        value = dot(straight, float3(0.2126, 0.7152, 0.0722)) * m.a;
    }
    if (postProjectionMaskMode == 2 || postProjectionMaskMode == 4 ||
        postProjectionMaskMode == 6)
        value = 1.0 - value;
    return clamp(value, 0.0, 1.0);
}
float cubic_weight(float x)
{
    x = abs(x);
    float x2 = x * x;
    float x3 = x2 * x;
    /* Catmull-Rom cubic.  It is sharp enough for editor/live previews and
     * stays fully on the GPU, avoiding the old CPU QImage/Cairo resample path
     * during interactive box scaling. */
    if (x <= 1.0)
        return 1.5 * x3 - 2.5 * x2 + 1.0;
    if (x < 2.0)
        return -0.5 * x3 + 2.5 * x2 - 4.0 * x + 2.0;
    return 0.0;
}
float sinc_approx(float x)
{
    x = abs(x);
    if (x < 0.0001)
        return 1.0;
    float pix = 3.14159265359 * x;
    return sin(pix) / pix;
}
float lanczos_weight(float x)
{
    x = abs(x);
    if (x >= 3.0)
        return 0.0;
    return sinc_approx(x) * sinc_approx(x / 3.0);
}
float4 sample_cubic(float2 uv)
{
    float2 texel = max(sourceTexelSize, float2(0.000001, 0.000001));
    float2 size = 1.0 / texel;
    float2 pixel = uv * size - 0.5;
    float2 base = floor(pixel);
    float2 f = pixel - base;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float total = 0.0;
    for (int y = -1; y <= 2; ++y) {
        float wy = cubic_weight(float(y) - f.y);
        for (int x = -1; x <= 2; ++x) {
            float wx = cubic_weight(float(x) - f.x);
            float w = wx * wy;
            float2 tap = (base + float2(x, y) + 0.5) * texel;
            sum += image.Sample(pointSampler, clamp(tap, 0.0, 1.0)) * w;
            total += w;
        }
    }
    return total > 0.000001 ? sum / total : image.Sample(textureSampler, uv);
}
float4 sample_lanczos(float2 uv)
{
    float2 texel = max(sourceTexelSize, float2(0.000001, 0.000001));
    float2 size = 1.0 / texel;
    float2 pixel = uv * size - 0.5;
    float2 base = floor(pixel);
    float2 f = pixel - base;
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float total = 0.0;
    for (int y = -2; y <= 3; ++y) {
        float wy = lanczos_weight(float(y) - f.y);
        for (int x = -2; x <= 3; ++x) {
            float wx = lanczos_weight(float(x) - f.x);
            float w = wx * wy;
            float2 tap = (base + float2(x, y) + 0.5) * texel;
            sum += image.Sample(pointSampler, clamp(tap, 0.0, 1.0)) * w;
            total += w;
        }
    }
    return total > 0.000001 ? sum / total : image.Sample(textureSampler, uv);
}
float4 sample_scaled_image(float2 uv)
{
    if (imageScaleFilter == 0)
        return image.Sample(pointSampler, uv);
    if (imageScaleFilter == 2)
        return sample_cubic(uv);
    if (imageScaleFilter == 3)
        return sample_lanczos(uv);
    return image.Sample(textureSampler, uv);
}
)"
R"(float light_distance_attenuation(float distanceValue, float startDistance,
                                 float falloffDistance, int falloffMode)
{
    if (falloffMode == 0)
        return 1.0;
    float normalizedDistance = max(distanceValue - max(startDistance, 0.0), 0.0) /
        max(falloffDistance, 0.0001);
    if (falloffMode == 1)
        return clamp(1.0 - normalizedDistance, 0.0, 1.0);
    /* A normalized inverse-square curve is physically shaped while remaining
     * useful in title-sized coordinate systems where true candela units would
     * make legacy pixel distances impractical. */
    return 1.0 / (1.0 + 4.0 * normalizedDistance * normalizedDistance);
}
void accumulate_light(float4 positionType, float4 directionIntensity,
                      float4 colorEnabled, float4 lightParams,
                      float sourceSize, int falloffMode, float lightVisibility,
                      float3 worldPosition, float3 normal, float3 viewDirection,
                      float3 baseColor, float metallic, float roughness,
                      float diffuseAmount, float specularAmount, float shininess,
                      inout float3 diffuseLight, inout float3 specularLight,
                      inout float3 ambientLight, inout float3 environmentLight)
{
    if (colorEnabled.a < 0.5 || directionIntensity.a <= 0.000001)
        return;
    int lightType = (int)(positionType.w + 0.5);
    float3 lightColor = max(colorEnabled.rgb, float3(0.0, 0.0, 0.0));
    float intensity = max(directionIntensity.a, 0.0);
    if (lightType == 0) {
        ambientLight += lightColor * intensity;
        return;
    }
    if (lightType == 4) {
        environmentLight += lightColor * intensity * environmentExposure;
        return;
    }

    float3 toLight = positionType.xyz - worldPosition;
    float distanceValue = max(length(toLight), 0.0001);
    float3 lightDirection = toLight / distanceValue;
    float attenuation = 1.0;
    if (lightType == 3) {
        /* Parallel light direction stores the direction in which rays travel. */
        lightDirection = normalize(-directionIntensity.xyz);
    } else {
        attenuation = light_distance_attenuation(
            distanceValue, lightParams.x, lightParams.y, falloffMode);
    }
    if (lightType == 2) {
        float3 fromLight = normalize(worldPosition - positionType.xyz);
        float coneCosine = dot(normalize(directionIntensity.xyz), fromLight);
        attenuation *= smoothstep(lightParams.z, lightParams.w, coneCosine);
    }
    /* A finite emitter broadens the lighting terminator and specular lobe.
     * Point/spot size is expressed in scene units; parallel lights use the
     * same control as a small angular extent. Zero exactly preserves the
     * historical punctual-light response. */
    float sourceAngular = lightType == 3
        ? clamp(max(sourceSize, 0.0) / 1000.0, 0.0, 1.0)
        : clamp(max(sourceSize, 0.0) / distanceValue, 0.0, 1.0);
    float rawNDotL = dot(normal, lightDirection);
    float nDotL = sourceAngular > 0.000001
        ? smoothstep(-sourceAngular, sourceAngular, rawNDotL)
        : max(rawNDotL, 0.0);
    if (nDotL <= 0.000001 || attenuation <= 0.000001)
        return;

    float3 radiance = lightColor * intensity * attenuation *
        clamp(lightVisibility, 0.0, 1.0);
    diffuseLight += radiance * nDotL * diffuseAmount;
    float3 halfVector = normalize(lightDirection + viewDirection);
    float authoredPower = max(shininess, 1.0);
    float effectiveRoughness = clamp(roughness + sourceAngular, 0.02, 1.0);
    float roughPower = lerp(256.0, 2.0,
                            effectiveRoughness * effectiveRoughness);
    float specularPower = max(1.0, min(authoredPower, roughPower));
    float specularLobe = pow(max(dot(normal, halfVector), 0.0), specularPower);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float fresnel = pow(1.0 - max(dot(viewDirection, halfVector), 0.0), 5.0);
    float3 fresnelColor = f0 + (1.0 - f0) * fresnel;
    specularLight += radiance * specularLobe * specularAmount * fresnelColor;
}
)"
R"(float sample_shadow_depth(int shadowIndex, float2 uv)
{
    if (shadowIndex == 0)
        return shadowMap0.Sample(pointSampler, uv).r;
    if (shadowIndex == 1)
        return shadowMap1.Sample(pointSampler, uv).r;
    if (shadowIndex == 2)
        return shadowMap2.Sample(pointSampler, uv).r;
    return shadowMap3.Sample(pointSampler, uv).r;
}
float sample_planar_shadow_depth(int shadowIndex, float2 uv)
{
    /* Samples outside a Spot/Parallel map see the cleared far plane. Clamping
     * them to an occupied edge texel stretches isolated dark bands across the
     * receiver when a soft kernel crosses the fitted projection bounds. */
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0)
        return 1.0;
    return sample_shadow_depth(shadowIndex, uv);
}
/* A fixed low-discrepancy disk avoids the square bands of the old 5x5 grid.
 * Entry zero intentionally samples the centre so sub-texel blockers cannot
 * fall between every blocker-search tap. */
float2 shadow_poisson_offset(int sampleIndex)
{
    if (sampleIndex == 0) return float2( 0.0000,  0.0000);
    if (sampleIndex == 1) return float2(-0.6134,  0.6175);
    if (sampleIndex == 2) return float2( 0.1700, -0.0403);
    if (sampleIndex == 3) return float2(-0.2994, -0.7919);
    if (sampleIndex == 4) return float2( 0.6457,  0.4932);
    if (sampleIndex == 5) return float2(-0.6518, -0.2132);
    if (sampleIndex == 6) return float2( 0.4210, -0.7407);
    if (sampleIndex == 7) return float2( 0.8841, -0.1757);
    if (sampleIndex == 8) return float2(-0.1012,  0.2969);
    if (sampleIndex == 9) return float2( 0.3102,  0.7910);
    if (sampleIndex == 10) return float2(-0.8762,  0.0577);
    if (sampleIndex == 11) return float2(-0.2798,  0.8992);
    if (sampleIndex == 12) return float2( 0.1428, -0.9442);
    if (sampleIndex == 13) return float2( 0.6754, -0.4491);
    if (sampleIndex == 14) return float2(-0.9410, -0.3261);
    return float2(0.9480, 0.3172);
}
float2 rotate_shadow_offset(float2 offset, float2 rotation)
{
    return float2(offset.x * rotation.x - offset.y * rotation.y,
                  offset.x * rotation.y + offset.y * rotation.x);
}
float2 shadow_sample_rotation(float3 worldPosition, int shadowIndex)
{
    /* Keep one deterministic rotation per light. Hashing every world-space
     * pixel independently turned the low-sample kernel into visible grain and
     * crawling artifacts as geometry moved through the scene. The Poisson
     * distribution is already non-grid-shaped, so spatial randomization is
     * unnecessary. */
    float phase = frac(((float)shadowIndex + 1.0) * 0.61803398875) *
        6.28318530718 + worldPosition.x * 0.0;
    return float2(cos(phase), sin(phase));
}
float shadow_linear_depth(float depth, int shadowKind,
                          float nearPlane, float farPlane)
{
    float safeNear = max(nearPlane, 0.0001);
    float safeFar = max(farPlane, safeNear + 0.0001);
    return safeNear + depth * (safeFar - safeNear);
}
float planar_shadow_visibility(int shadowIndex, float3 worldPosition,
                               int shadowKind, float3 lightPosition,
                               float4 basisRightCenterX,
                               float4 basisUpCenterY,
                               float3 basisForward,
                               float nearPlane, float farPlane,
                               float2 projectionSpan, float darkness,
                               float bias, float softness, float sourceSize,
                               float2 texelSize)
{
    float3 relative = worldPosition - lightPosition;
    float depth = dot(relative, basisForward);
    if (depth <= 0.000001)
        return 1.0;
    float2 localPoint = float2(dot(relative, basisRightCenterX.xyz),
                               dot(relative, basisUpCenterY.xyz));
    float2 projectionCenter = float2(basisRightCenterX.w,
                                     basisUpCenterY.w);
    float2 halfSpan = max(projectionSpan * 0.5,
                          float2(0.0001, 0.0001));
    float2 ndcXY = shadowKind == 1
        ? (localPoint - projectionCenter) / max(halfSpan * depth,
            float2(0.0001, 0.0001))
        : (localPoint - projectionCenter) / halfSpan;
    float2 uv = float2(ndcXY.x * 0.5 + 0.5,
                       1.0 - (ndcXY.y * 0.5 + 0.5));
    float receiverDepth = (depth - nearPlane) /
        max(farPlane - nearPlane, 0.0001);
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0 ||
        receiverDepth <= 0.0 || receiverDepth >= 1.0)
        return 1.0;
    float receiverLinear = shadow_linear_depth(
        receiverDepth, shadowKind, nearPlane, farPlane);
    float sourceRadius = max(sourceSize, 0.0) * 0.5;
    float2 safeSpan = max(projectionSpan, float2(0.0001, 0.0001));
    float faceTexel = max(texelSize.x, texelSize.y);
    /* Preserve the perceptual range of the pre-PCSS control: its old 5x5
     * kernel reached one complete authored radius from the centre. */
    float manualRadius = max(softness, 0.0) * faceTexel * 2.0;
    float2 rotation = shadow_sample_rotation(worldPosition, shadowIndex);
    if (sourceRadius <= 0.0001) {
        /* A punctual emitter has no blocker-dependent penumbra to estimate.
         * Running it through the sparse blocker search was the Development
         * 381 regression: valid hard/manual-soft shadows could early-out as
         * fully lit. A direct low-discrepancy PCF resolve is deterministic,
         * non-blocky and cheaper than the former 25-tap square grid. */
        float2 filterRadius = max(
            float2(faceTexel * 0.75, faceTexel * 0.75),
            float2(manualRadius, manualRadius));
        float occluded = 0.0;
        for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
            float2 disk = rotate_shadow_offset(
                shadow_poisson_offset(sampleIndex), rotation);
            float casterDepth = sample_planar_shadow_depth(
                shadowIndex, uv + disk * filterRadius);
            occluded += step(casterDepth + bias, receiverDepth);
        }
        float shadow = occluded / 16.0;
        return 1.0 - clamp(darkness, 0.0, 1.0) * shadow;
    }
    float2 physicalSearch = float2(0.0, 0.0);
    if (shadowKind == 1) {
        physicalSearch = sourceRadius /
            max(safeSpan * receiverLinear, float2(0.0001, 0.0001));
    } else {
        physicalSearch = sourceRadius / safeSpan;
    }
    /* Blocker search follows emitter extent only. Manual softness belongs to
     * the final filter; mixing it into the search can jump over a real caster
     * and produce holes or isolated artifacts. */
    float2 minimumSearch = float2(faceTexel, faceTexel);
    float2 searchRadius = clamp(max(minimumSearch, physicalSearch),
        float2(faceTexel, faceTexel), float2(0.10, 0.10));
    float blockerDepthSum = 0.0;
    float blockerCount = 0.0;
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), rotation);
        float2 sampleUv = uv + disk * searchRadius;
        float casterDepth = sample_planar_shadow_depth(
            shadowIndex, sampleUv);
        if (casterDepth + bias < receiverDepth) {
            blockerDepthSum += shadow_linear_depth(
                casterDepth, shadowKind, nearPlane, farPlane);
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5)
        return 1.0;
    float blockerLinear = blockerDepthSum / blockerCount;
    float separation = max(receiverLinear - blockerLinear, 0.0);
    float2 physicalPenumbra = shadowKind == 1
        ? sourceRadius / safeSpan * separation /
            max(receiverLinear * blockerLinear, 0.0001)
        : sourceRadius / safeSpan * separation /
            max(blockerLinear, 1.0);
    float2 minimumFilter = float2(faceTexel * 0.75, faceTexel * 0.75);
    float2 filterRadius = clamp(max(minimumFilter,
        float2(manualRadius, manualRadius) + physicalPenumbra),
        minimumFilter, float2(0.16, 0.16));
    /* A compact contact kernel guarantees a stable umbra. Blend toward the
     * wider physical kernel only as blocker/receiver separation creates a
     * real penumbra; this prevents a few far-away samples from diluting a
     * valid shadow into near invisibility. */
    float2 coreRadius = min(filterRadius,
        max(minimumFilter * 2.0,
            float2(manualRadius, manualRadius) * 0.5));
    float coreOccluded = 0.0;
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), rotation);
        float casterDepth = sample_planar_shadow_depth(
            shadowIndex, uv + disk * coreRadius);
        coreOccluded += step(casterDepth + bias, receiverDepth);
    }
    float wideOccluded = 0.0;
    float2 filterRotation = float2(
        rotation.x * -0.30901699 - rotation.y * 0.95105652,
        rotation.x *  0.95105652 + rotation.y * -0.30901699);
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), filterRotation);
        float casterDepth = sample_planar_shadow_depth(
            shadowIndex, uv + disk * filterRadius);
        wideOccluded += step(casterDepth + bias, receiverDepth);
    }
    float centerDepth = sample_planar_shadow_depth(shadowIndex, uv);
    float centerShadow = step(centerDepth + bias, receiverDepth);
    float coreShadow = max(centerShadow, coreOccluded / 8.0);
    float wideShadow = wideOccluded / 16.0;
    float penumbraAmount = max(physicalPenumbra.x, physicalPenumbra.y);
    float contactScale = max(faceTexel * 6.0, manualRadius * 0.5);
    float penumbraMix = clamp(penumbraAmount /
        max(contactScale, 0.0001), 0.0, 1.0);
    float shadow = max(lerp(coreShadow, wideShadow, penumbraMix),
                       coreShadow * 0.25);
    return 1.0 - clamp(darkness, 0.0, 1.0) * shadow;
}
)"
R"(int point_shadow_face_index(float3 direction)
{
    float3 axis = abs(direction);
    if (axis.x >= axis.y && axis.x >= axis.z)
        return direction.x >= 0.0 ? 0 : 1;
    if (axis.y >= axis.z)
        return direction.y >= 0.0 ? 2 : 3;
    return direction.z >= 0.0 ? 4 : 5;
}
/* xy are face-local clip coordinates; z is signed distance along the face.
 * The writer uses the identical projection so the atlas never depends on a
 * second matrix convention in the receiver shader. */
float3 point_shadow_face_projection(int faceIndex, float3 direction)
{
    if (faceIndex == 0) return float3(-direction.z, -direction.y,  direction.x);
    if (faceIndex == 1) return float3( direction.z, -direction.y, -direction.x);
    if (faceIndex == 2) return float3( direction.x,  direction.z,  direction.y);
    if (faceIndex == 3) return float3( direction.x, -direction.z, -direction.y);
    if (faceIndex == 4) return float3( direction.x, -direction.y,  direction.z);
    return float3(-direction.x, -direction.y, -direction.z);
}
float2 point_shadow_face_local_uv(int faceIndex, float3 direction,
                                  float2 faceTexelSize)
{
    float3 faceProjection = point_shadow_face_projection(
        faceIndex, direction);
    float2 facePosition = faceProjection.xy /
        max(faceProjection.z, 0.000001);
    float2 localUv = float2(facePosition.x * 0.5 + 0.5,
                            1.0 - (facePosition.y * 0.5 + 0.5));
    return clamp(localUv, faceTexelSize * 0.5,
                  1.0 - faceTexelSize * 0.5);
}
float3 point_shadow_face_direction(int faceIndex, float2 localUv)
{
    float2 facePosition = float2(localUv.x * 2.0 - 1.0,
                                 1.0 - localUv.y * 2.0);
    if (faceIndex == 0)
        return normalize(float3(1.0, -facePosition.y, -facePosition.x));
    if (faceIndex == 1)
        return normalize(float3(-1.0, -facePosition.y, facePosition.x));
    if (faceIndex == 2)
        return normalize(float3(facePosition.x, 1.0, facePosition.y));
    if (faceIndex == 3)
        return normalize(float3(facePosition.x, -1.0, -facePosition.y));
    if (faceIndex == 4)
        return normalize(float3(facePosition.x, -facePosition.y, 1.0));
    return normalize(float3(-facePosition.x, -facePosition.y, -1.0));
}
float2 point_shadow_atlas_uv(float3 direction, float2 atlasTexelSize)
{
    int faceIndex = point_shadow_face_index(direction);
    float2 tile = faceIndex <= 2
        ? float2((float)faceIndex, 0.0)
        : float2((float)(faceIndex - 3), 1.0);
    float2 faceTexelSize = atlasTexelSize * float2(3.0, 2.0);
    float2 localUv = point_shadow_face_local_uv(
        faceIndex, direction, faceTexelSize);
    return (tile + localUv) / float2(3.0, 2.0);
}
float point_shadow_receiver_depth_for_ray(
    float3 sampleDirection, float receiverDistance, float3 receiverNormal,
    float receiverPlaneNumerator, float farPlane)
{
    float sampleReceiverDistance = receiverDistance;
    float receiverPlaneDenominator = dot(sampleDirection, receiverNormal);
    if (abs(receiverPlaneDenominator) > 0.000001) {
        float planeDistance = receiverPlaneNumerator /
            receiverPlaneDenominator;
        if (planeDistance > 0.0)
            sampleReceiverDistance = planeDistance;
    }
    float sampleReceiverDepth = sampleReceiverDistance / farPlane;
    return sampleReceiverDepth;
}
float2 point_shadow_point_sample_depths(
    int shadowIndex, float3 sampleDirection, float receiverDistance,
    float3 receiverNormal, float receiverPlaneNumerator, float farPlane,
    float2 atlasTexelSize)
{
    int faceIndex = point_shadow_face_index(sampleDirection);
    float2 tile = faceIndex <= 2
        ? float2((float)faceIndex, 0.0)
        : float2((float)(faceIndex - 3), 1.0);
    float2 faceTexelSize = atlasTexelSize * float2(3.0, 2.0);
    float2 localUv = point_shadow_face_local_uv(
        faceIndex, sampleDirection, faceTexelSize);
    float2 halfTexel = faceTexelSize * 0.5;
    float2 texelCenter = clamp(
        (floor(localUv / faceTexelSize) + 0.5) * faceTexelSize,
        halfTexel, 1.0 - halfTexel);
    float casterDepth = sample_shadow_depth(
        shadowIndex, (tile + texelCenter) / float2(3.0, 2.0));
    float3 texelDirection = point_shadow_face_direction(
        faceIndex, texelCenter);
    float receiverDepth = point_shadow_receiver_depth_for_ray(
        texelDirection, receiverDistance, receiverNormal,
        receiverPlaneNumerator, farPlane);
    return float2(casterDepth, receiverDepth);
}
float point_shadow_visibility(int shadowIndex, float3 worldPosition,
                              float3 lightPosition, float nearPlane,
                              float farPlane, float darkness, float bias,
                              float softness, float sourceSize,
                              float2 atlasTexelSize)
{
    float3 fromLight = worldPosition - lightPosition;
    float receiverDistance = length(fromLight);
    if (receiverDistance <= 0.000001 || farPlane <= 0.000001)
        return 1.0;
    float receiverDepth = receiverDistance / farPlane;
    if (receiverDepth <= 0.0 || receiverDepth >= 1.0)
        return 1.0;
    float3 direction = fromLight / receiverDistance;
    float3 receiverNormal = normalize(worldNormal);
    float receiverPlaneNumerator = dot(fromLight, receiverNormal);
    float3 tangentSeed = abs(direction.y) < 0.999
        ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(tangentSeed, direction));
    float3 bitangent = normalize(cross(direction, tangent));
    float faceTexel = max(atlasTexelSize.x * 3.0,
                          atlasTexelSize.y * 2.0);
    float sourceRadius = max(sourceSize, 0.0) * 0.5;
    float manualAngular = max(softness, 0.0) * faceTexel * 2.0;
    float2 rotation = shadow_sample_rotation(worldPosition, shadowIndex);
    if (sourceRadius <= 0.0001) {
        /* Punctual/manual-soft Point shadows use the same direct Poisson PCF
         * contract as the last runtime-proven path. Every tap still intersects
         * the receiver plane, so self-shadow wedges stay fixed without the
         * fragile blocker-search early-out introduced in Development 381. */
        float filterAngular = max(faceTexel * 0.75, manualAngular);
        float occluded = 0.0;
        for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
            float2 disk = rotate_shadow_offset(
                shadow_poisson_offset(sampleIndex), rotation);
            float3 sampleDirection = normalize(direction +
                (tangent * disk.x + bitangent * disk.y) * filterAngular);
            float2 sampleDepths = point_shadow_point_sample_depths(
                shadowIndex, sampleDirection, receiverDistance,
                receiverNormal, receiverPlaneNumerator, farPlane,
                atlasTexelSize);
            occluded += step(sampleDepths.x + bias, sampleDepths.y);
        }
        float shadow = occluded / 16.0;
        return 1.0 - clamp(darkness, 0.0, 1.0) * shadow;
    }
    /* Search only the emitter's apparent angular radius at the receiver.
     * The previous near-plane divisor exploded when a light approached any
     * object, scattering blocker taps across unrelated cube faces. */
    float physicalSearchAngular = sourceRadius /
        max(receiverDistance, 0.0001);
    float searchAngular = clamp(max(faceTexel, physicalSearchAngular),
                                faceTexel, 0.12);
    float blockerDistanceSum = 0.0;
    float blockerCount = 0.0;
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), rotation);
        float3 sampleDirection = normalize(direction +
            (tangent * disk.x + bitangent * disk.y) * searchAngular);
        float2 sampleDepths = point_shadow_point_sample_depths(
            shadowIndex, sampleDirection, receiverDistance, receiverNormal,
            receiverPlaneNumerator, farPlane, atlasTexelSize);
        if (sampleDepths.x + bias < sampleDepths.y) {
            blockerDistanceSum += sampleDepths.x * farPlane;
            blockerCount += 1.0;
        }
    }
    /* Most visible pixels have no blocker and stop after 8 taps. The costly
     * filter runs only inside an actual shadow. */
    if (blockerCount < 0.5)
        return 1.0;
    float blockerDistance = blockerDistanceSum / blockerCount;
    float physicalPenumbraAngular = sourceRadius *
        max(receiverDistance - blockerDistance, 0.0) /
        max(blockerDistance * receiverDistance, 0.0001);
    float filterAngular = clamp(max(faceTexel * 0.75,
        manualAngular + physicalPenumbraAngular),
        faceTexel * 0.75, 0.18);
    float2 filterRotation = float2(
        rotation.x * -0.30901699 - rotation.y * 0.95105652,
        rotation.x *  0.95105652 + rotation.y * -0.30901699);
    float coreAngular = min(filterAngular,
        max(faceTexel * 1.5, manualAngular * 0.5));
    float coreOccluded = 0.0;
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), rotation);
        float3 sampleDirection = normalize(direction +
            (tangent * disk.x + bitangent * disk.y) * coreAngular);
        float2 sampleDepths = point_shadow_point_sample_depths(
            shadowIndex, sampleDirection, receiverDistance,
            receiverNormal, receiverPlaneNumerator, farPlane,
            atlasTexelSize);
        coreOccluded += step(sampleDepths.x + bias, sampleDepths.y);
    }
    float wideOccluded = 0.0;
    for (int sampleIndex = 0; sampleIndex < 16; ++sampleIndex) {
        float2 disk = rotate_shadow_offset(
            shadow_poisson_offset(sampleIndex), filterRotation);
        float3 sampleDirection = normalize(direction +
            (tangent * disk.x + bitangent * disk.y) * filterAngular);
        /* Each tap compares the stored texel-centre ray against its matching
         * receiver-plane intersection without expanding shader work 4x. */
        float2 sampleDepths = point_shadow_point_sample_depths(
            shadowIndex, sampleDirection, receiverDistance,
            receiverNormal, receiverPlaneNumerator, farPlane,
            atlasTexelSize);
        wideOccluded += step(sampleDepths.x + bias, sampleDepths.y);
    }
    float2 centerDepths = point_shadow_point_sample_depths(
        shadowIndex, direction, receiverDistance, receiverNormal,
        receiverPlaneNumerator, farPlane, atlasTexelSize);
    float centerShadow = step(centerDepths.x + bias, centerDepths.y);
    float coreShadow = max(centerShadow, coreOccluded / 8.0);
    float wideShadow = wideOccluded / 16.0;
    float contactScale = max(faceTexel * 6.0, manualAngular * 0.5);
    float penumbraMix = clamp(physicalPenumbraAngular /
        max(contactScale, 0.0001), 0.0, 1.0);
    float shadow = max(lerp(coreShadow, wideShadow, penumbraMix),
                       coreShadow * 0.25);
    return 1.0 - clamp(darkness, 0.0, 1.0) * shadow;
}
float shadow_visibility(int shadowIndex, float3 worldPosition,
                        float4x4 shadowMatrix, int enabled, int shadowKind,
                        float3 shadowLightPosition,
                        float4 shadowBasisRightCenterX,
                        float4 shadowBasisUpCenterY,
                        float3 shadowBasisForward, float shadowNear,
                        float shadowFar, float2 shadowProjectionSpan,
                        float darkness, float bias, float softness,
                        float sourceSize, float2 texelSize)
{
    if (enabled == 0)
        return 1.0;
    if (shadowKind == 2)
        return point_shadow_visibility(
            shadowIndex, worldPosition, shadowLightPosition, shadowNear,
            shadowFar, darkness, bias, softness, sourceSize, texelSize);
    if (shadowKind == 1 || shadowKind == 3)
        return planar_shadow_visibility(
            shadowIndex, worldPosition, shadowKind, shadowLightPosition,
            shadowBasisRightCenterX, shadowBasisUpCenterY,
            shadowBasisForward, shadowNear, shadowFar,
            shadowProjectionSpan, darkness, bias, softness, sourceSize,
            texelSize);
    return 1.0;
}
)"
R"(
float4 apply_material_lighting(float4 premultipliedColor, float2 uv)
{
    if (lightingEnabled == 0 || premultipliedColor.a <= 0.000001)
        return premultipliedColor;
    float alpha = clamp(premultipliedColor.a, 0.0, 1.0);
    float3 baseColor = max(premultipliedColor.rgb / max(alpha, 0.000001),
                           float3(0.0, 0.0, 0.0));
    float3 worldPosition = planeOrigin + planeAxisU * uv.x + planeAxisV * uv.y;
    float3 normal = normalize(worldNormal);
    float3 viewDirection = normalize(cameraWorldPosition - worldPosition);
    /* Double-sided planar artwork should remain illuminable from the visible
     * side after negative scale or a camera crossing. */
    if (dot(normal, viewDirection) < 0.0)
        normal = -normal;

    float ambientAmount = max(materialPrimary.x, 0.0);
    float diffuseAmount = max(materialPrimary.y, 0.0);
    float specularAmount = max(materialPrimary.z, 0.0);
    float shininess = max(materialPrimary.w, 1.0);
    float metallic = clamp(materialPbr.x, 0.0, 1.0);
    float roughness = clamp(materialPbr.y, 0.02, 1.0);
    float reflectionAmount = max(materialPbr.z, 0.0);
    float emissiveAmount = max(materialPbr.w, 0.0);
    float3 diffuseLight = float3(0.0, 0.0, 0.0);
    float3 specularLight = float3(0.0, 0.0, 0.0);
    float3 ambientLight = float3(0.0, 0.0, 0.0);
    float3 environmentLight = float3(0.0, 0.0, 0.0);

    float shadowVisibility0 = shadow_visibility(0, worldPosition,
        shadowViewProj0, shadowEnabled0, shadowKind0, shadowLightPosition0,
        shadowBasisRightCenterX0, shadowBasisUpCenterY0,
        shadowBasisForward0, shadowNear0, shadowFar0,
        shadowProjectionSpan0, shadowDarkness0,
        shadowBias0, shadowSoftness0, lightSourceSize0, shadowTexelSize0);
    float shadowVisibility1 = shadow_visibility(1, worldPosition,
        shadowViewProj1, shadowEnabled1, shadowKind1, shadowLightPosition1,
        shadowBasisRightCenterX1, shadowBasisUpCenterY1,
        shadowBasisForward1, shadowNear1, shadowFar1,
        shadowProjectionSpan1, shadowDarkness1,
        shadowBias1, shadowSoftness1, lightSourceSize1, shadowTexelSize1);
    float shadowVisibility2 = shadow_visibility(2, worldPosition,
        shadowViewProj2, shadowEnabled2, shadowKind2, shadowLightPosition2,
        shadowBasisRightCenterX2, shadowBasisUpCenterY2,
        shadowBasisForward2, shadowNear2, shadowFar2,
        shadowProjectionSpan2, shadowDarkness2,
        shadowBias2, shadowSoftness2, lightSourceSize2, shadowTexelSize2);
    float shadowVisibility3 = shadow_visibility(3, worldPosition,
        shadowViewProj3, shadowEnabled3, shadowKind3, shadowLightPosition3,
        shadowBasisRightCenterX3, shadowBasisUpCenterY3,
        shadowBasisForward3, shadowNear3, shadowFar3,
        shadowProjectionSpan3, shadowDarkness3,
        shadowBias3, shadowSoftness3, lightSourceSize3, shadowTexelSize3);

    accumulate_light(lightPositionType0, lightDirectionIntensity0,
        lightColorEnabled0, lightParams0, lightSourceSize0, lightFalloff0,
        shadowVisibility0, worldPosition, normal,
        viewDirection, baseColor, metallic, roughness, diffuseAmount,
        specularAmount, shininess, diffuseLight, specularLight, ambientLight,
        environmentLight);
    accumulate_light(lightPositionType1, lightDirectionIntensity1,
        lightColorEnabled1, lightParams1, lightSourceSize1, lightFalloff1,
        shadowVisibility1, worldPosition, normal,
        viewDirection, baseColor, metallic, roughness, diffuseAmount,
        specularAmount, shininess, diffuseLight, specularLight, ambientLight,
        environmentLight);
    accumulate_light(lightPositionType2, lightDirectionIntensity2,
        lightColorEnabled2, lightParams2, lightSourceSize2, lightFalloff2,
        shadowVisibility2, worldPosition, normal,
        viewDirection, baseColor, metallic, roughness, diffuseAmount,
        specularAmount, shininess, diffuseLight, specularLight, ambientLight,
        environmentLight);
    accumulate_light(lightPositionType3, lightDirectionIntensity3,
        lightColorEnabled3, lightParams3, lightSourceSize3, lightFalloff3,
        shadowVisibility3, worldPosition, normal,
        viewDirection, baseColor, metallic, roughness, diffuseAmount,
        specularAmount, shininess, diffuseLight, specularLight, ambientLight,
        environmentLight);

    float nDotV = max(dot(normal, viewDirection), 0.0);
    float fresnelView = pow(1.0 - nDotV, 5.0);
    float3 environmentContribution = environmentLight *
        (baseColor * ambientAmount + reflectionAmount *
         lerp(float3(0.04, 0.04, 0.04), baseColor, metallic) *
         (0.2 + 0.8 * fresnelView));
    float3 litColor = baseColor * (ambientLight * ambientAmount +
        diffuseLight * (1.0 - metallic)) + specularLight +
        environmentContribution + materialEmissive.rgb * emissiveAmount * materialEmissive.a;
    return float4(max(litColor, float3(0.0, 0.0, 0.0)) * alpha, alpha);
}
float4 layer_color(VertDataOut v)
{
    float clipAlpha = 1.0;
    if (imageClipEnabled != 0) {
        float2 logicalPoint = v.uv * imageLogicalSize;
        clipAlpha = rounded_box_alpha(logicalPoint, imageClipRect, imageCornerRadii);
    }
    float4 color = sample_scaled_image(v.uv) *
        (weight * transition_alpha(v.uv) * clipAlpha * projected_mask_alpha(v));
    return apply_material_lighting(color, v.uv);
}
float4 PSLayer(VertDataOut v) : TARGET
{
    return layer_color(v);
}
float4 PSDepthLayer(VertDataOut v) : TARGET
{
    float4 color = layer_color(v);
    clip(color.a - 0.0039215686);
    return color;
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSLayer(v); } }
technique DepthDraw { pass { vertex_shader = VSDefault(v); pixel_shader = PSDepthLayer(v); } }
)";

/* Alpha-aware shadow depth writer shared by planar maps and the six viewports
 * of a Point-light 3x2 atlas. The real Z attachment chooses the nearest
 * caster; Point maps store normalized radial distance for direction-based
 * atlas lookup in the lighting pass. */
static constexpr const char *kGpuShadowMapEffect = R"(
uniform texture2d image;
uniform float4x4 shadowViewProj;
uniform float3 planeOrigin;
uniform float3 planeAxisU;
uniform float3 planeAxisV;
uniform float opacity;
uniform float alphaCutoff;
uniform int conservativeAlpha;
uniform float2 sourceTexelSize;
uniform int pointShadow;
uniform int pointShadowFace;
uniform float3 pointLightPosition;
uniform float pointShadowFar;
uniform int shadowKind;
uniform float3 shadowRight;
uniform float3 shadowUp;
uniform float3 shadowForward;
uniform float2 shadowProjectionCenter;
uniform float2 shadowProjectionSpan;
uniform float shadowNear;
uniform float shadowFar;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; float4 lightClip : TEXCOORD1; float3 worldPosition : TEXCOORD2; };
float3 point_shadow_face_projection(int faceIndex, float3 direction)
{
    if (faceIndex == 0) return float3(-direction.z, -direction.y,  direction.x);
    if (faceIndex == 1) return float3( direction.z, -direction.y, -direction.x);
    if (faceIndex == 2) return float3( direction.x,  direction.z,  direction.y);
    if (faceIndex == 3) return float3( direction.x, -direction.z, -direction.y);
    if (faceIndex == 4) return float3( direction.x, -direction.y,  direction.z);
    return float3(-direction.x, -direction.y, -direction.z);
}
VertDataOut VSShadow(VertDataIn v)
{
    VertDataOut o;
    float3 worldPosition = planeOrigin + planeAxisU * v.uv.x + planeAxisV * v.uv.y;
    o.lightClip = mul(float4(worldPosition, 1.0), shadowViewProj);
    if (pointShadow != 0) {
        float3 faceProjection = point_shadow_face_projection(
            pointShadowFace, worldPosition - pointLightPosition);
        /* Point-atlas XY/W is authored directly from the signed cube face.
         * Its clip Z must come from that same convention. Combining the Qt
         * perspective-matrix Z with the manual face W can place every vertex
         * outside D3D's 0..W clip range while the CPU draw loop still reports
         * success. Face depth is monotonic with radial distance for one ray,
         * so it also gives the Z24 attachment the correct nearest-caster
         * ordering without a second matrix convention. */
        float faceDepth = clamp(faceProjection.z /
            max(pointShadowFar, 0.0001), 0.0, 1.0);
        o.pos = float4(faceProjection.xy,
                       faceDepth * faceProjection.z,
                       faceProjection.z);
    } else {
        /* Spot and Parallel maps are projected explicitly in the same
         * light-space basis used by the receiver. This avoids backend-specific
         * matrix packing and OpenGL-vs-D3D clip conventions entirely. */
        float3 relative = worldPosition - pointLightPosition;
        float depth = dot(relative, shadowForward);
        float2 localPoint = float2(dot(relative, shadowRight),
                                   dot(relative, shadowUp));
        float2 halfSpan = max(shadowProjectionSpan * 0.5,
                              float2(0.0001, 0.0001));
        float normalizedDepth = (depth - shadowNear) /
            max(shadowFar - shadowNear, 0.0001);
        float2 ndcXY;
        if (shadowKind == 1) {
            float safeDepth = max(depth, 0.000001);
            ndcXY = (localPoint - shadowProjectionCenter) /
                max(halfSpan * safeDepth, float2(0.0001, 0.0001));
            o.pos = float4(ndcXY * safeDepth,
                           normalizedDepth * safeDepth, safeDepth);
        } else {
            ndcXY = (localPoint - shadowProjectionCenter) / halfSpan;
            o.pos = float4(ndcXY, normalizedDepth, 1.0);
        }
        o.lightClip = float4(ndcXY, normalizedDepth, 1.0);
    }
    o.uv = v.uv;
    o.worldPosition = worldPosition;
    return o;
}
float4 PSShadow(VertDataOut v) : TARGET
{
    float alpha = image.Sample(textureSampler, v.uv).a;
    if (conservativeAlpha != 0) {
        /* Text/Clock/Ticker are alpha-cutout planes. Under strong shadow-map
         * minification a five-point quincunx can still miss a thin diagonal or
         * one-pixel stem between taps. Resolve a full 4x4 projected footprint
         * only for text-like casters and retain the maximum alpha. The extra
         * half-texel dilation closes coverage gaps without changing ordinary
         * Shape/Image/Video shadow cost. */
        float2 footprint = max(
            float2(fwidth(v.uv.x), fwidth(v.uv.y)), sourceTexelSize);
        float2 radius = footprint * 0.5 + sourceTexelSize * 0.5;
        for (int sampleY = 0; sampleY < 4; ++sampleY) {
            float offsetY = ((float)sampleY / 3.0 * 2.0 - 1.0) * radius.y;
            for (int sampleX = 0; sampleX < 4; ++sampleX) {
                float offsetX = ((float)sampleX / 3.0 * 2.0 - 1.0) * radius.x;
                alpha = max(alpha, image.Sample(textureSampler,
                    v.uv + float2(offsetX, offsetY)).a);
            }
        }
    }
    alpha *= opacity;
    float effectiveCutoff = conservativeAlpha != 0
        ? min(alphaCutoff, 0.0005) : alphaCutoff;
    clip(alpha - effectiveCutoff);
    float depth = pointShadow != 0
        ? length(v.worldPosition - pointLightPosition) /
            max(pointShadowFar, 0.0001)
        : v.lightClip.z;
    return float4(clamp(depth, 0.0, 1.0), 0.0, 0.0, 1.0);
}
technique Draw { pass { vertex_shader = VSShadow(v); pixel_shader = PSShadow(v); } }
)";


static constexpr const char *kGpuAdjustmentMixEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d originalImage;
uniform texture2d effectedImage;
uniform texture2d coverageImage;
uniform texture2d transitionMatte;
uniform float amount;
uniform int blendMode;
uniform int coverageInvert;
uniform float wipeProgress;
uniform float wipeSoftness;
uniform int wipeDirection;
uniform int transitionType;
uniform int blocksColumns;
uniform int blocksRows;
uniform float randomSeed;
uniform int transitionMatteEnabled;
uniform int imageChannel;
uniform int transitionInvert;
uniform int transitionClockwise;
uniform float2 transitionCenter;
uniform float transitionRotation;
uniform float transitionAspect;
uniform int transitionProfile;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v){VertDataOut o;o.pos=mul(float4(v.pos.xyz,1.0),ViewProj);o.uv=v.uv;return o;}
float hash21(float2 p){p=frac(p*float2(123.34,456.21));p+=dot(p,p+45.32+randomSeed);return frac(p.x*p.y);}
float transition_alpha(float2 uv)
{
 float r=clamp(wipeProgress,0.0,1.0);if(r>=0.999999)return 1.0;float f=max(wipeSoftness,0.000001);float a=1.0;
 if(transitionType==14){float2 c=floor(uv*float2(max(blocksColumns,1),max(blocksRows,1)));float t=hash21(c);if(transitionInvert!=0)t=1.0-t;a=smoothstep(t-f,t+f,r);}
 else if(transitionType==15){float m=uv.x;if(transitionMatteEnabled!=0){float4 x=transitionMatte.Sample(textureSampler,uv);m=imageChannel==1?x.a:imageChannel==2?x.r:imageChannel==3?x.g:imageChannel==4?x.b:dot(x.rgb,float3(0.2126,0.7152,0.0722));}if(transitionInvert!=0)m=1.0-m;a=smoothstep(m-f,m+f,r);}
 else if(transitionType==16){float2 p=uv-transitionCenter;float x=frac(atan2(p.y,p.x)/6.28318530718+0.5+transitionRotation/360.0+1.0);if(transitionClockwise==0)x=1.0-x;if(transitionInvert!=0)x=1.0-x;a=smoothstep(x-f,x+f,r);}
 else if(transitionType==17){float2 p=(uv-transitionCenter)*2.0;p.x*=max(transitionAspect,0.01);float d=length(p)/1.41421356;if(transitionProfile==1)d=max(abs(p.x),abs(p.y));else if(transitionProfile==2)d=(abs(p.x)+abs(p.y))*0.5;d=clamp(d,0.0,1.0);if(transitionInvert!=0)d=1.0-d;a=smoothstep(d-f,d+f,r);}
 else if(transitionType==18){float2 p=uv-transitionCenter;float z=transitionRotation*0.01745329252;float2 q=float2(p.x*cos(z)-p.y*sin(z),p.x*sin(z)+p.y*cos(z));q.x*=max(transitionAspect,0.01);float m=q.x+0.5;if(transitionProfile==1)m=length(q)*1.41421356;else if(transitionProfile==2)m=abs(q.x)+abs(q.y);m=clamp(m,0.0,1.0);if(transitionInvert!=0)m=1.0-m;a=smoothstep(m-f,m+f,r);}
 else if(wipeDirection!=0){if(wipeDirection==2){float e=1.0-r;a=smoothstep(e,e+f,uv.x);}else if(wipeDirection==3){float e=1.0-r;a=smoothstep(e,e+f,uv.y);}else if(wipeDirection==4)a=1.0-smoothstep(r-f,r,uv.y);else a=1.0-smoothstep(r-f,r,uv.x);}
 return clamp(a,0.0,1.0);
}
float mix_lum(float3 c){return dot(c,float3(0.30,0.59,0.11));}
float mix_sat(float3 c){return max(c.r,max(c.g,c.b))-min(c.r,min(c.g,c.b));}
float3 mix_clip(float3 c){float l=mix_lum(c);float n=min(c.r,min(c.g,c.b));float x=max(c.r,max(c.g,c.b));if(n<0.0)c=l+((c-l)*l)/max(l-n,0.000001);if(x>1.0)c=l+((c-l)*(1.0-l))/max(x-l,0.000001);return clamp(c,0.0,1.0);}
float3 mix_set_lum(float3 c,float l){return mix_clip(c+(l-mix_lum(c)));}
float3 mix_set_sat(float3 c,float sat){float n=min(c.r,min(c.g,c.b));float x=max(c.r,max(c.g,c.b));return x>n?(c-n)*(sat/(x-n)):float3(0.0,0.0,0.0);}
float3 adjustment_blend(float3 base,float3 src){if(blendMode==1)return base*src;if(blendMode==2)return min(base+src,1.0);if(blendMode==3)return 1.0-(1.0-base)*(1.0-src);if(blendMode==4)return lerp(2.0*base*src,1.0-2.0*(1.0-base)*(1.0-src),step(0.5,base));if(blendMode==5)return mix_set_lum(mix_set_sat(src,mix_sat(base)),mix_lum(base));return src;}
float4 PSMix(VertDataOut v):TARGET{float4 a=originalImage.Sample(textureSampler,v.uv);float4 b=effectedImage.Sample(textureSampler,v.uv);float coverage=step(0.5,clamp(coverageImage.Sample(textureSampler,v.uv).a,0.0,1.0));if(coverageInvert!=0)coverage=1.0-coverage;float aa=clamp(a.a,0.0,1.0);float ba=clamp(b.a,0.0,1.0);float3 ac=aa>0.000001?a.rgb/aa:float3(0.0,0.0,0.0);float3 bc=ba>0.000001?b.rgb/ba:float3(0.0,0.0,0.0);float3 blended=adjustment_blend(ac,bc);/* Process the existing artwork only inside the adjustment coverage. The editor checkerboard is not part of either texture, so it cannot be blurred or color-corrected. *//* The effect is evaluated from the complete lower composite. Inside the
coverage use the effected alpha as well as its color, otherwise blur kernels
are clipped back to the original per-pixel alpha and crossing text/shapes only
blur partially. Color-only effects keep the same alpha naturally. */float4 inside=float4(blended*ba,ba);float t=clamp(amount*coverage*transition_alpha(v.uv),0.0,1.0);float4 result=lerp(a,inside,t);/* Preserve effect-generated pixels that expand beyond the adjustment bounds (drop shadow, glow, bloom, blur tails). Only the alpha newly created by the effect is admitted outside coverage, so opaque background pixels outside the adjustment region are not recolored. */float halo=0.0;return lerp(result,b,halo);}
technique Draw{pass{vertex_shader=VSDefault(v);pixel_shader=PSMix(v);}}
)";

static constexpr const char *kGpuAdjustmentCoverageEffect = R"(
uniform float4x4 ViewProj;
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v){VertDataOut o;o.pos=mul(float4(v.pos.xyz,1.0),ViewProj);o.uv=v.uv;return o;}
float4 PSCoverage(VertDataOut v):TARGET{return float4(1.0,1.0,1.0,1.0);}
technique Draw{pass{vertex_shader=VSDefault(v);pixel_shader=PSCoverage(v);}}
)";

static constexpr const char *kGpuPrimitiveShapeEffect = R"(
uniform float4x4 ViewProj;
uniform int shapeType;
uniform float2 surfaceSize;
uniform float2 shapeSize;
uniform float2 shapeOffset;
uniform float4 cornerRadii;
uniform float4 fillColor;
uniform float4 strokeColor;
uniform float strokeWidth;
uniform int strokeAlignment;
uniform int strokeOnFront;
uniform float starInnerRadius;
uniform float starOuterRadius;
uniform int vertexCount;
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v) { VertDataOut o; o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj); o.uv = v.uv; return o; }
float coverage(float distanceValue, float aa) { return 1.0 - smoothstep(-aa, aa, distanceValue); }
float sdRoundBox(float2 p, float2 b, float4 radii) {
    float radius = p.x < 0.0
        ? (p.y < 0.0 ? radii.x : radii.w)
        : (p.y < 0.0 ? radii.y : radii.z);
    radius = min(max(radius, 0.0), min(b.x, b.y));
    float2 q = abs(p) - b + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}
float sdEllipse(float2 p, float2 ab) {
    float2 a = max(ab, float2(0.0001, 0.0001));
    return (length(p / a) - 1.0) * min(a.x, a.y);
}
float sdPolygonUnit(float2 p, int n) {
    float halfSector = 3.14159265359 / max((float)n, 3.0);
    float fullSector = halfSector * 2.0;
    float angleFromTop = atan2(p.y, p.x) + 1.57079632679;
    angleFromTop -= floor(angleFromTop / fullSector) * fullSector;
    float sideNormalDelta = angleFromTop - halfSector;
    return length(p) * cos(sideNormalDelta) - cos(halfSector);
}
float sdStarUnit(float2 p, float innerRadius, float outerRadius, int points) {
    float count = max((float)points, 2.0);
    float sector = 3.14159265359 / count;
    float a = atan2(p.y, p.x) + 1.57079632679;
    float k = floor(0.5 + a / sector);
    float local = k * sector - a;
    float radius = fmod(abs(k), 2.0) < 0.5 ? outerRadius : innerRadius;
    return cos(local) * length(p) - radius;
}
float shapeDistance(float2 p, float2 halfSize) {
    if (shapeType == 2)
        return sdEllipse(p, halfSize);
    float minHalf = max(min(halfSize.x, halfSize.y), 0.0001);
    float2 normalized = p / max(halfSize, float2(0.0001, 0.0001));
    if (shapeType == 3)
        return sdPolygonUnit(normalized, 3) * minHalf;
    if (shapeType == 4)
        return sdStarUnit(normalized,
                          clamp(starInnerRadius, 0.0, 1.0),
                          clamp(starOuterRadius, 0.0001, 1.0),
                          max(vertexCount, 2)) * minHalf;
    if (shapeType == 5)
        return sdPolygonUnit(normalized, max(vertexCount, 3)) * minHalf;
    if (shapeType == 6)
        return sdPolygonUnit(normalized, 4) * minHalf;
    return sdRoundBox(p, halfSize, cornerRadii);
}
float4 premultiplied(float4 color, float alphaCoverage) {
    float alpha = color.a * alphaCoverage;
    return float4(color.rgb * alpha, alpha);
}
float4 over(float4 foreground, float4 background) {
    return foreground + background * (1.0 - foreground.a);
}
float4 PSShape(VertDataOut v) : TARGET {
    float2 surfacePoint = v.uv * surfaceSize;
    float2 p = surfacePoint - shapeOffset - shapeSize * 0.5;
    float2 halfSize = max(shapeSize * 0.5, float2(0.0001, 0.0001));
    float d = shapeDistance(p, halfSize);
    float aa = max(fwidth(d), 0.75);
    float fillA = coverage(d, aa);
    float strokeA = 0.0;
    if (strokeWidth > 0.0001) {
        if (strokeAlignment == 0) {
            strokeA = clamp(coverage(d - strokeWidth, aa) - coverage(d, aa), 0.0, 1.0);
        } else if (strokeAlignment == 2) {
            strokeA = clamp(coverage(d, aa) - coverage(d + strokeWidth, aa), 0.0, 1.0);
        } else {
            strokeA = clamp(coverage(d - strokeWidth * 0.5, aa) -
                            coverage(d + strokeWidth * 0.5, aa), 0.0, 1.0);
        }
    }

    float4 fill = premultiplied(fillColor, fillA);
    float4 stroke = premultiplied(strokeColor, strokeA);
    return strokeOnFront != 0 ? over(stroke, fill) : over(fill, stroke);
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSShape(v); } }
)";

static constexpr const char *kGpuFrameBlendEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d background;
uniform texture2d foreground;
uniform int blendMode;
uniform float opacity;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v) { VertDataOut o; o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj); o.uv = v.uv; return o; }
float blend_lum(float3 c) { return dot(c, float3(0.30, 0.59, 0.11)); }
float blend_sat(float3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
float3 blend_clip_color(float3 c)
{
    float l = blend_lum(c);
    float n = min(c.r, min(c.g, c.b));
    float x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + ((c - l) * l) / max(l - n, 0.000001);
    if (x > 1.0) c = l + ((c - l) * (1.0 - l)) / max(x - l, 0.000001);
    return clamp(c, 0.0, 1.0);
}
float3 blend_set_lum(float3 c, float l)
{
    return blend_clip_color(c + (l - blend_lum(c)));
}
float3 blend_set_sat(float3 c, float s)
{
    float n = min(c.r, min(c.g, c.b));
    float x = max(c.r, max(c.g, c.b));
    return x > n ? (c - n) * (s / (x - n)) : float3(0.0, 0.0, 0.0);
}
float3 blend_color(float3 cb, float3 cs, int mode)
{
    if (mode == 1) return cb * cs;
    if (mode == 2) return min(cb + cs, 1.0);
    if (mode == 3) return 1.0 - (1.0 - cb) * (1.0 - cs);
    if (mode == 4) return lerp(2.0 * cb * cs, 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs), step(0.5, cb));
    if (mode == 5) return blend_set_lum(blend_set_sat(cs, blend_sat(cb)), blend_lum(cb));
    return cs;
}
float4 PSBlend(VertDataOut v) : TARGET
{
    float4 dst = background.Sample(textureSampler, v.uv);
    float4 src = foreground.Sample(textureSampler, v.uv);
    float da = clamp(dst.a, 0.0, 1.0);
    float sourceAlpha = clamp(src.a, 0.0, 1.0);
    float layerOpacity = clamp(opacity, 0.0, 1.0);
    float sa = sourceAlpha * layerOpacity;
    float3 cb = da > 0.000001 ? dst.rgb / da : float3(0.0, 0.0, 0.0);
    float3 cs = sourceAlpha > 0.000001 ? src.rgb / sourceAlpha : float3(0.0, 0.0, 0.0);
    float3 blended = blend_color(cb, cs, blendMode);
    float outA = sa + da * (1.0 - sa);
    // AE-style source-over composition in premultiplied-alpha space.
    // The foreground texture is already premultiplied by sourceAlpha, so its
    // RGB contribution must also be scaled by the animated layer opacity.
    // Without this, lowering opacity only changed alpha while RGB stayed at
    // full strength, making Screen/Add layers look opaque in Preview/Program
    // and preventing the canvas checkerboard from showing through correctly.
    float3 effectiveSrc = src.rgb * layerOpacity;
    float3 outRGB = dst.rgb * (1.0 - sa) +
                    effectiveSrc * (1.0 - da) +
                    blended * (sa * da);
    return float4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSBlend(v); } }
)";

static constexpr const char *kGpuExternalBackgroundEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d background;
uniform float4x4 localToBackground;
uniform float2 backgroundSize;
uniform float2 logicalSize;
uniform float4 backgroundRect;
uniform float4 titleBackground;
uniform int mappingMode;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v) { VertDataOut o; o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj); o.uv = v.uv; return o; }
float4 PSBackground(VertDataOut v) : TARGET
{
    float2 pixel;
    if (mappingMode == 0) {
        pixel = backgroundRect.xy + v.uv * backgroundRect.zw;
    } else {
        float4 mapped = mul(float4(v.uv * logicalSize, 0.0, 1.0), localToBackground);
        pixel = mapped.xy;
    }
    float2 size = max(backgroundSize, float2(1.0, 1.0));
    float2 uv = pixel / size;
    float inside = step(0.0, uv.x) * step(0.0, uv.y) *
                   step(uv.x, 1.0) * step(uv.y, 1.0);
    float4 dst = background.Sample(textureSampler, clamp(uv, 0.0, 1.0)) * inside;
    return titleBackground + dst * (1.0 - titleBackground.a);
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSBackground(v); } }
)";

static constexpr const char *kGpuMaskEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform texture2d maskImage;
uniform int maskMode;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertDataIn { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VertDataOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertDataOut VSDefault(VertDataIn v) { VertDataOut o; o.pos = mul(float4(v.pos.xyz, 1.0), ViewProj); o.uv = v.uv; return o; }
float4 PSMask(VertDataOut v) : TARGET
{
    float4 src = image.Sample(textureSampler, v.uv);
    float4 m = maskImage.Sample(textureSampler, v.uv);
    float maskValue = m.a;
    if (maskMode == 3 || maskMode == 4) {
        float3 straight = m.a > 0.000001 ? m.rgb / m.a : float3(0.0, 0.0, 0.0);
        maskValue = dot(straight, float3(0.2126, 0.7152, 0.0722)) * m.a;
    }
    /* Clipping mattes consume geometric artwork coverage. Their mask graph is
     * rendered at full compositor opacity, so modes 5/6 remain channel-
     * compatible with Alpha/Inverse Alpha here while ignoring the source
     * layer's animated opacity. Intrinsic image transparency and anti-aliased
     * shape/text edges remain part of the coverage. Visible-base artwork is
     * rendered separately with its normal opacity by composite_clipping_base(). */
    if (maskMode == 2 || maskMode == 4 || maskMode == 6)
        maskValue = 1.0 - maskValue;
    return src * clamp(maskValue, 0.0, 1.0);
}
technique Draw { pass { vertex_shader = VSDefault(v); pixel_shader = PSMask(v); } }
)";


struct ExtensionImageTexture {
    gs_image_file_t image{};
    bool initialized = false;
    qint64 modified_msecs = 0;
    qint64 file_size = -1;
    uint64_t last_used = 0;
    ~ExtensionImageTexture()
    {
        if (initialized)
            gs_image_file_free(&image);
    }
};

static std::mutex g_extension_image_texture_mutex;
static std::unordered_map<std::string, std::unique_ptr<ExtensionImageTexture>>
    g_extension_image_textures;
static uint64_t g_extension_image_texture_tick = 0;
static constexpr std::size_t kMaxExtensionImageTextures = 64;

static void clear_extension_image_textures_locked()
{
    g_extension_image_textures.clear();
    g_extension_image_texture_tick = 0;
}

static void clear_extension_image_textures()
{
    std::lock_guard<std::mutex> lock(g_extension_image_texture_mutex);
    clear_extension_image_textures_locked();
}

static gs_texture_t *extension_image_texture(const QString &absolute_path)
{
    if (absolute_path.trimmed().isEmpty())
        return nullptr;
    const QFileInfo info(absolute_path);
    if (!info.isFile() || !info.isReadable())
        return nullptr;
    const QString canonical = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath() : info.canonicalFilePath();
    const std::string key = canonical.toUtf8().constData();
    const qint64 modified = info.lastModified().toMSecsSinceEpoch();
    const qint64 fileSize = info.size();

    std::lock_guard<std::mutex> lock(g_extension_image_texture_mutex);
    auto found = g_extension_image_textures.find(key);
    if (found != g_extension_image_textures.end()) {
        if (found->second->modified_msecs == modified &&
            found->second->file_size == fileSize) {
            found->second->last_used = ++g_extension_image_texture_tick;
            return found->second->image.texture;
        }
        g_extension_image_textures.erase(found);
    }

    auto entry = std::make_unique<ExtensionImageTexture>();
    gs_image_file_init(&entry->image, key.c_str());
    entry->initialized = true;
    if (!entry->image.loaded)
        return nullptr;
    gs_image_file_init_texture(&entry->image);
    if (!entry->image.texture)
        return nullptr;
    entry->modified_msecs = modified;
    entry->file_size = fileSize;
    entry->last_used = ++g_extension_image_texture_tick;
    gs_texture_t *texture = entry->image.texture;
    g_extension_image_textures.emplace(key, std::move(entry));

    while (g_extension_image_textures.size() > kMaxExtensionImageTextures) {
        auto oldest = g_extension_image_textures.end();
        for (auto it = g_extension_image_textures.begin();
             it != g_extension_image_textures.end(); ++it) {
            if (it->first == key)
                continue;
            if (oldest == g_extension_image_textures.end() ||
                it->second->last_used < oldest->second->last_used)
                oldest = it;
        }
        if (oldest == g_extension_image_textures.end())
            break;
        g_extension_image_textures.erase(oldest);
    }
    return texture;
}

struct TitleGpuRenderSession {
    struct LayerRaster {
        std::string key;
        std::string effect_cache_key;
        QImage pending_image;
        /* First-publication safety net for GPU text. The compatibility raster
         * is prepared on the model thread only while no prior texture exists,
         * so a transient glyph-buffer failure can publish the exact current
         * frame immediately instead of retaining an older compositor frame. */
        QImage gpu_text_fallback_image;
        QPointF gpu_text_fallback_origin;
        double gpu_text_fallback_logical_width = 0.0;
        double gpu_text_fallback_logical_height = 0.0;
        QRectF gpu_text_fallback_layer_box_rect;
        QRectF gpu_text_fallback_image_clip_rect;
        QPointF origin;
        double logical_width = 0.0;
        double logical_height = 0.0;
        double base_box_width = 1.0;
        double base_box_height = 1.0;
        QRectF layer_box_rect;
        QRectF image_clip_rect;
        bool pending_upload = false;
        bool gpu_text = false;
        std::unique_ptr<fxm::gpu_text::Layer> text_layer;
        /* Dynamic text revisions keep only shaped-layout references and a
         * cluster/UTF-8 remap. Glyph indices from an old cue/clock value are
         * never reused by the new layout. */
        ImmutableTextLayout text_animation_layout;
        TextAnimatorContentChange text_animation_change;
        double text_animation_change_local_time = 0.0;
        bool gpu_primitive = false;
        int primitive_shape_type = 0;
        int primitive_vertex_count = 0;
        float primitive_inner_radius = 0.2f;
        float primitive_outer_radius = 0.5f;
        float primitive_stroke_width = 0.0f;
        int primitive_stroke_alignment = 1;
        bool primitive_stroke_on_front = true;
        double primitive_shape_width = 0.0;
        double primitive_shape_height = 0.0;
        double primitive_padding = 0.0;
        uint32_t primitive_fill_color = 0xFFFFFFFFu;
        uint32_t primitive_stroke_color = 0x00000000u;
        std::array<float, 4> primitive_corner_radii {0.0f, 0.0f, 0.0f, 0.0f};
        gs_texture_t *texture = nullptr;
        gs_texrender_t *primitive_targets[2] = {nullptr, nullptr};
        int primitive_active_target = -1;
        gs_texrender_t *effect_cache = nullptr;
        uint64_t effect_cache_last_used = 0;
        /* Free-transform geometry is immutable for ordinary ticker playback.
         * Keep it GPU-resident and rebuild only when the quad, raster bounds or
         * evaluated transform actually changes. */
        gs_vertbuffer_t *transform_grid = nullptr;
        std::string transform_grid_key;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct CachedFrameTexture {
        std::unique_ptr<fxm::rendering::ITexture> texture_resource;
        gs_texture_t *texture = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        quint64 bytes = 0;
        uint64_t last_used = 0;
    };

    struct RuntimeSceneMaskRaster {
        LayerRaster raster;
        gs_texrender_t *target = nullptr;
    };

    struct MaskTextureCacheEntry {
        std::string key;
        gs_texrender_t *targets[2] = {nullptr, nullptr};
        int active_target = -1;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t last_used = 0;
    };

    struct GroupRenderDiagnostics {
        std::chrono::steady_clock::time_point window_started {};
        uint64_t calls = 0;
        uint64_t total_us = 0;
        uint64_t max_us = 0;
        uint64_t child_samples = 0;
        uint64_t target_creations = 0;
        uint64_t silhouette_passes = 0;
        uint64_t slow_frames = 0;
        std::size_t max_child_transform_keyframes = 0;
        std::string max_child_id;
        std::string max_child_name;
        bool initialized = false;
    };

    struct TransitionMatteTexture {
        std::unique_ptr<fxm::rendering::ITexture> texture_resource;
        gs_texture_t *texture = nullptr;
        qint64 modified_msecs = 0;
        qint64 file_size = -1;
        uint32_t width = 0;
        uint32_t height = 0;
        quint64 bytes = 0;
        uint64_t last_used = 0;
    };

    struct AsyncReadbackSlot {
        gs_stagesurf_t *stage = nullptr;
        gs_texrender_t *crop_target = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        QRect region;
        uint64_t serial = 0;
        bool pending = false;
    };

    struct ShaderCompileJob {
        std::string id;
        std::string label;
        std::function<bool()> run;
    };
