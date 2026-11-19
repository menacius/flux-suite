    sample_time = time;
    add_discrete(title.active_camera);
    add(title.lighting_enabled);
    add(title.default_light_enabled);
    add(quant(title.environment_exposure, 0.001));
    add(TitlePreferences::shadow_map_size_px());
    for (const TitleLight &light : title.lights) {
        add(light.enabled); add((int)light.type); add((int)light.falloff);
        add_vec3(light.position, 0.25); add_vec3(light.target, 0.25);
        add_anim(light.color_a, 1.0 / 255.0);
        add_anim(light.color_r, 1.0 / 255.0);
        add_anim(light.color_g, 1.0 / 255.0);
        add_anim(light.color_b, 1.0 / 255.0);
        add_anim(light.intensity, 0.001);
        add_anim(light.source_size, 0.05);
        add_anim(light.falloff_distance, 0.25);
        add_anim(light.cone_angle, 0.05);
        add_anim(light.cone_feather, 0.05);
        add(light.casts_shadows);
        add_anim(light.shadow_darkness, 0.001);
        add_anim(light.shadow_softness, 0.05);
        add_anim(light.shadow_bias, 0.00001);
        add_anim(light.environment_rotation, 0.05);
    }
    for (const TitleCamera &camera : title.cameras) {
        add_anim(camera.position_x, 0.25); add_anim(camera.position_y, 0.25);
        add_anim(camera.position_z, 0.25);
        if (camera.position_3d_path_enabled)
            add_vec3(camera.position_3d, 0.25);
        add_anim(camera.target_x, 0.25);
        add_anim(camera.target_y, 0.25); add_anim(camera.target_z, 0.25);
        if (camera.target_3d_path_enabled)
            add_vec3(camera.target_3d, 0.25);
        add_anim(camera.orientation_x, 0.05); add_anim(camera.orientation_y, 0.05);
        add_anim(camera.orientation_z, 0.05); add_anim(camera.rotation_x, 0.05);
        add_anim(camera.rotation_y, 0.05); add_anim(camera.rotation_z, 0.05);
        add_anim(camera.focal_length, 0.05); add_anim(camera.field_of_view, 0.01);
        add_anim(camera.zoom, 0.0005); add_anim(camera.near_clip, 0.001);
        add_anim(camera.far_clip, 0.25); add_anim(camera.projection_mode, 1.0);
    }

    for (const auto &layer : title.layers) {
        if (!layer) continue;
        sample_time = cache_layer_sample_time(title, *layer, time);
        add(layer->visible && sample_time >= layer->in_time && sample_time <= layer->out_time);
        add_vec(layer->position, 0.25);
        if (layer->position_3d_path_enabled)
            add_vec3(layer->position_3d, 0.25);
        add_vec(layer->scale, 0.0005);
        add_anim(layer->rotation, 0.05);
        add_anim(layer->position_z, 0.25); add_anim(layer->scale_z, 0.0005);
        add_anim(layer->rotation_x, 0.05); add_anim(layer->rotation_y, 0.05);
        add_anim(layer->anchor_z, 0.25); add_anim(layer->orientation_x, 0.05);
        add_anim(layer->orientation_y, 0.05); add_anim(layer->orientation_z, 0.05);
        add_discrete(layer->camera_assignment);
        add(quant(layer->transform_quad_tl_x, 0.0005)); add(quant(layer->transform_quad_tl_y, 0.0005));
        add(quant(layer->transform_quad_tr_x, 0.0005)); add(quant(layer->transform_quad_tr_y, 0.0005));
        add(quant(layer->transform_quad_br_x, 0.0005)); add(quant(layer->transform_quad_br_y, 0.0005));
        add(quant(layer->transform_quad_bl_x, 0.0005)); add(quant(layer->transform_quad_bl_y, 0.0005));
        add_anim(layer->opacity, 0.002);
        add(layer->material_accepts_lights);
        add(layer->material_casts_shadows);
        add(layer->material_accepts_shadows);
        add(layer->material_appears_in_reflections);
        add_anim(layer->material_ambient, 0.001);
        add_anim(layer->material_diffuse, 0.001);
        add_anim(layer->material_specular, 0.001);
        add_anim(layer->material_shininess, 0.05);
        add_anim(layer->material_metallic, 0.001);
        add_anim(layer->material_roughness, 0.001);
        add_anim(layer->material_reflection_intensity, 0.001);
        add_anim(layer->material_emissive_color_a, 1.0 / 255.0);
        add_anim(layer->material_emissive_color_r, 1.0 / 255.0);
        add_anim(layer->material_emissive_color_g, 1.0 / 255.0);
        add_anim(layer->material_emissive_color_b, 1.0 / 255.0);
        add_anim(layer->material_emissive_intensity, 0.001);
        add_vec(layer->size, 0.25);
        add_vec(layer->image_size, 0.25);
        add_vec(layer->origin_prop, 0.25);
        add_anim(layer->font_size_prop, 0.10);
        add_anim(layer->char_tracking_prop, 0.10);
        add_anim(layer->char_scale_x_prop, 0.0005);
        add_anim(layer->char_scale_y_prop, 0.0005);
        add_anim(layer->baseline_shift_prop, 0.10);
        add_anim(layer->text_color_a, 1.0 / 255.0); add_anim(layer->text_color_r, 1.0 / 255.0);
        add_anim(layer->text_color_g, 1.0 / 255.0); add_anim(layer->text_color_b, 1.0 / 255.0);
        add_anim(layer->paragraph_indent_left_prop, 0.25);
        add_anim(layer->paragraph_indent_right_prop, 0.25);
        add_anim(layer->paragraph_indent_first_line_prop, 0.25);
        add_anim(layer->paragraph_space_before_prop, 0.25);
        add_anim(layer->paragraph_space_after_prop, 0.25);
        add_anim(layer->fill_color_a, 1.0 / 255.0); add_anim(layer->fill_color_r, 1.0 / 255.0);
        add_anim(layer->fill_color_g, 1.0 / 255.0); add_anim(layer->fill_color_b, 1.0 / 255.0);
        add_anim(layer->stroke_color_a, 1.0 / 255.0); add_anim(layer->stroke_color_r, 1.0 / 255.0);
        add_anim(layer->stroke_color_g, 1.0 / 255.0); add_anim(layer->stroke_color_b, 1.0 / 255.0);
        add_anim(layer->stroke_offset_prop, 0.25);
        add_anim(layer->stroke_width_prop, 0.25);
        add_anim(layer->background_enabled_prop, 1.0);
        add_anim(layer->background_opacity_prop, 0.002);
        add_anim(layer->background_padding_x_prop, 0.25);
        add_anim(layer->background_padding_y_prop, 0.25);
        add_anim(layer->background_padding_left_prop, 0.25);
        add_anim(layer->background_padding_right_prop, 0.25);
        add_anim(layer->background_padding_top_prop, 0.25);
        add_anim(layer->background_padding_bottom_prop, 0.25);
        add_anim(layer->background_corner_radius_prop, 0.25);
        add_anim(layer->background_corner_radius_tl_prop, 0.25);
        add_anim(layer->background_corner_radius_tr_prop, 0.25);
        add_anim(layer->background_corner_radius_br_prop, 0.25);
        add_anim(layer->background_corner_radius_bl_prop, 0.25);
        add_anim(layer->background_stroke_width_prop, 0.25);
        add_anim(layer->background_stroke_opacity_prop, 0.002);
        add_anim(layer->background_color_a, 1.0 / 255.0);
        add_anim(layer->background_color_r, 1.0 / 255.0);
        add_anim(layer->background_color_g, 1.0 / 255.0);
        add_anim(layer->background_color_b, 1.0 / 255.0);
        add_anim(layer->background_stroke_color_a, 1.0 / 255.0);
        add_anim(layer->background_stroke_color_r, 1.0 / 255.0);
        add_anim(layer->background_stroke_color_g, 1.0 / 255.0);
        add_anim(layer->background_stroke_color_b, 1.0 / 255.0);
        add_anim(layer->shadow_enabled_prop, 1.0);
        add_anim(layer->shadow_opacity_prop, 0.002);
        add_anim(layer->shadow_distance_prop, 0.25);
        add_anim(layer->shadow_angle_prop, 0.05);
        add_anim(layer->shadow_blur_prop, 0.25);
        add_anim(layer->shadow_spread_prop, 0.25);
        add_anim(layer->shadow_color_a, 1.0 / 255.0);
        add_anim(layer->shadow_color_r, 1.0 / 255.0);
        add_anim(layer->shadow_color_g, 1.0 / 255.0);
        add_anim(layer->shadow_color_b, 1.0 / 255.0);
        for (const auto &effect : layer->effects) add_effect(effect);
        for (const auto &transition : layer->transitions) {
            if (!transition.enabled)
                continue;
            add(quant(layer_transition_progress(transition, layer->in_time, layer->out_time, sample_time), 0.002));
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}

QString CacheManager::temporalStateKey(const CacheFrameKey &key,
                                       const QString &visual_state_hash) const
{
    return QStringLiteral("%1|%2|%3x%4|%5")
        .arg(key.title_id, key.content_hash)
        .arg(key.width).arg(key.height)
        .arg(visual_state_hash);
}

CacheFrameKey CacheManager::keyForFrame(const Title &title, int frame) const
{
    const int clamped_frame = titleHasTimelineChanges(title)
        ? std::clamp(frame, 0, cache_last_frame_for_title(title, effectiveFrameRate()))
        : 0;
    return {QString::fromStdString(title.id), contentHash(title), clamped_frame, title.width, title.height};
}

CacheFrameKey CacheManager::keyForTime(const Title &title, double time) const
{
    return keyForTime(title, time, QString());
}

CacheFrameKey CacheManager::keyForTime(const Title &title, double time,
                                        const QString &known_content_hash) const
{
    const int clamped_frame = frameIndexForTitleTime(title, time);
    return {QString::fromStdString(title.id),
            known_content_hash.isEmpty() ? contentHash(title) : known_content_hash,
            clamped_frame, title.width, title.height};
}

int CacheManager::frameIndexForTitleTime(const Title &title, double time) const
{
    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, title.duration));
    const double frame_rate = effectiveFrameRate();
    return cache_frame_index_for_title_time(
        clamped_time, frame_rate, titleHasTimelineChanges(title),
        cache_last_frame_for_title(title, frame_rate));
}

QVector<CacheFrameKey> CacheManager::frameKeysForTitle(const Title &title) const
{
    const bool animated = titleHasTimelineChanges(title);
    const int last_frame = animated ? cache_last_frame_for_title(title, effectiveFrameRate()) : 0;
    const QString title_id = QString::fromStdString(title.id);
    const QString content_hash = contentHash(title);
    QVector<CacheFrameKey> keys;
    keys.reserve(last_frame + 1);
    for (int frame = 0; frame <= last_frame; ++frame)
        keys.push_back({title_id, content_hash, frame, title.width, title.height});
    return keys;
}

bool CacheManager::titleHasTimelineChanges(const Title &title) const
{
    if (std::any_of(title.lights.begin(), title.lights.end(),
                    [](const TitleLight &light) {
                        return title_light_has_authored_keyframes(light);
                    }))
        return true;
    for (const auto &layer : title.layers) {
        if (!layer) continue;
        if (layer->type == LayerType::Clock || layer->type == LayerType::Ticker ||
            layer->type == LayerType::Video || layer->type == LayerType::Chart ||
            fxm::asset_runtime::layer_uses_independent_playback(*layer))
            return true;
        if (std::any_of(layer->transitions.begin(), layer->transitions.end(),
                        [](const LayerTransition &transition) { return transition.enabled; }))
            return true;
        auto animated = [](const auto &prop) { return !prop.keyframes.empty(); };
        if (animated(layer->position) || animated(layer->scale) || animated(layer->rotation) ||
            animated(layer->opacity) || animated(layer->material_ambient) ||
            animated(layer->material_diffuse) || animated(layer->material_specular) ||
            animated(layer->material_shininess) || animated(layer->material_metallic) ||
            animated(layer->material_roughness) ||
            animated(layer->material_reflection_intensity) ||
            animated(layer->material_emissive_color_a) ||
            animated(layer->material_emissive_color_r) ||
            animated(layer->material_emissive_color_g) ||
            animated(layer->material_emissive_color_b) ||
            animated(layer->material_emissive_intensity) ||
            animated(layer->size) || animated(layer->image_size) || animated(layer->origin_prop) ||
            animated(layer->font_size_prop) || animated(layer->char_tracking_prop) ||
            animated(layer->char_scale_x_prop) || animated(layer->char_scale_y_prop) ||
            animated(layer->baseline_shift_prop) || animated(layer->paragraph_indent_left_prop) ||
            animated(layer->paragraph_indent_right_prop) || animated(layer->paragraph_indent_first_line_prop) ||
            animated(layer->paragraph_space_before_prop) || animated(layer->paragraph_space_after_prop) ||
            animated(layer->text_color_a) || animated(layer->text_color_r) ||
            animated(layer->text_color_g) || animated(layer->text_color_b) ||
            animated(layer->fill_color_a) || animated(layer->fill_color_r) ||
            animated(layer->fill_color_g) || animated(layer->fill_color_b) ||
            animated(layer->stroke_width_prop) ||
            animated(layer->stroke_color_a) || animated(layer->stroke_color_r) ||
            animated(layer->stroke_color_g) || animated(layer->stroke_color_b) ||
            animated(layer->chart_animation_progress) ||
            animated(layer->chart_reveal_progress) ||
            animated(layer->chart_value_transition) ||
            animated(layer->chart_label_opacity) ||
            animated(layer->chart_element_opacity) ||
            animated(layer->chart_font_size) ||
            animated(layer->chart_line_thickness) ||
            animated(layer->chart_bar_width) ||
            animated(layer->chart_bar_spacing) ||
            animated(layer->chart_corner_radius) ||
            animated(layer->chart_padding) ||
            animated(layer->chart_margin_left) ||
            animated(layer->chart_margin_top) ||
            animated(layer->chart_margin_right) ||
            animated(layer->chart_margin_bottom) ||
            animated(layer->background_enabled_prop) || animated(layer->background_opacity_prop) ||
            animated(layer->background_padding_x_prop) || animated(layer->background_padding_y_prop) ||
            animated(layer->background_padding_left_prop) || animated(layer->background_padding_right_prop) ||
            animated(layer->background_padding_top_prop) || animated(layer->background_padding_bottom_prop) ||
            animated(layer->background_corner_radius_prop) || animated(layer->background_corner_radius_tl_prop) ||
            animated(layer->background_corner_radius_tr_prop) || animated(layer->background_corner_radius_br_prop) ||
            animated(layer->background_corner_radius_bl_prop) || animated(layer->background_stroke_width_prop) ||
            animated(layer->background_stroke_opacity_prop) || animated(layer->background_color_a) ||
            animated(layer->background_color_r) || animated(layer->background_color_g) ||
            animated(layer->background_color_b) || animated(layer->background_stroke_color_a) ||
            animated(layer->background_stroke_color_r) || animated(layer->background_stroke_color_g) ||
            animated(layer->background_stroke_color_b) || animated(layer->shadow_enabled_prop) ||
            animated(layer->shadow_opacity_prop) || animated(layer->shadow_distance_prop) ||
            animated(layer->shadow_angle_prop) || animated(layer->shadow_blur_prop) ||
            animated(layer->shadow_spread_prop) || animated(layer->shadow_color_a) ||
            animated(layer->shadow_color_r) || animated(layer->shadow_color_g) ||
            animated(layer->shadow_color_b))
            return true;
        for (const LayerEffect &effect : layer->effects) {
            if (effect_is_time_variant(effect) && effect.enabled)
                return true;
        }
        if (layer->in_time > 0.0 || layer->out_time < title.duration)
            return true;
    }
    return false;
}

QImage CacheManager::requestFrame(const std::shared_ptr<Title> &title, double time, bool cached_only)
{
    if (!title) {
        log_cache_playback(QStringLiteral("request kind=editor result=null-title time=%1 cachedOnly=%2")
            .arg(time, 0, 'f', 6).arg(cached_only));
        return QImage();
    }

    const TitleCacheability cacheability = titleCacheability(title);
    if (cacheability == TitleCacheability::NonCacheable ||
        interactive_bypass_.load() || !cache_enabled_.load()) {
        log_cache_playback(QStringLiteral("request kind=editor result=bypass title=%1 time=%2 cachedOnly=%3 cacheability=%4 enabled=%5 interactive=%6")
            .arg(QString::fromStdString(title->id)).arg(time, 0, 'f', 6).arg(cached_only)
            .arg(static_cast<int>(cacheability)).arg(cache_enabled_.load()).arg(interactive_bypass_.load()));
        return QImage();
    }

    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, title->duration));
    const auto finish_cached_frame = [&](const QImage &cached_frame) {
        return cached_frame;
    };

    const QString content_hash = contentHash(*title);
    const CacheFrameKey key = keyForTime(*title, time, content_hash);
    const FrameCacheState initial_state = state_tracker_.state(key);
    log_cache_playback(QStringLiteral("request kind=editor title=%1 time=%2 clamped=%3 frame=%4 state=%5 cachedOnly=%6 key=%7")
        .arg(key.title_id).arg(time, 0, 'f', 6).arg(clamped_time, 0, 'f', 6).arg(key.frame)
        .arg(QString::fromUtf8(cache_playback_state_name(initial_state))).arg(cached_only).arg(key.toString()));
    QImage image;
    if (initial_state != FrameCacheState::Stale && ram_cache_.get(key, image)) {
        state_tracker_.setState(key, FrameCacheState::CachedRam);
        fxm::perf::add(fxm::perf::Counter::CachePlaybackHits);
        log_cache_playback(QStringLiteral("result=ram-hit kind=editor title=%1 frame=%2 cacheKey=%3 size=%4x%5 key=%6")
            .arg(key.title_id).arg(key.frame).arg(image.cacheKey()).arg(image.width()).arg(image.height()).arg(key.toString()));
        return finish_cached_frame(image);
    }

    if (initial_state != FrameCacheState::Stale && disk_cache_.contains(key)) {
        /* Disk decompression and file I/O never run on the editor/UI path. The
         * cache worker promotes the frame to RAM/GPU and coalesces duplicate
         * hydration requests while the uncached renderer remains responsive. */
        queueRealtimeJob(title, clamped_time, false, -1, QString(), content_hash);
        log_cache_playback(QStringLiteral("result=disk-hydration-queued kind=editor title=%1 frame=%2 key=%3")
            .arg(key.title_id).arg(key.frame).arg(key.toString()));
        return QImage();
    }

    fxm::perf::add(fxm::perf::Counter::CachePlaybackMisses);
    queueRealtimeJob(title, clamped_time, false, -1, QString(), content_hash);
    log_cache_playback(QStringLiteral("result=miss-queued kind=editor title=%1 frame=%2 initialState=%3 cachedOnly=%4 key=%5")
        .arg(key.title_id).arg(key.frame).arg(QString::fromUtf8(cache_playback_state_name(initial_state)))
        .arg(cached_only).arg(key.toString()));
    return QImage();
}

QString CacheManager::requestFrameGpuToken(
    const std::shared_ptr<Title> &title, double time,
    bool queue_if_missing, const QString &known_content_hash)
{
    if (!title || !cache_enabled_.load() || interactive_bypass_.load() ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return QString();

    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, title->duration));
    const CacheFrameKey key = keyForTime(
        *title, clamped_time, known_content_hash);
    const FrameCacheState state = state_tracker_.state(key);
    if (state != FrameCacheState::Stale &&
        title_gpu_frame_cache_contains(key.toString().toStdString())) {
        state_tracker_.setState(key, FrameCacheState::CachedRam);
        return key.toString();
    }

    if (queue_if_missing) {
        queueRealtimeJob(title, clamped_time, false, -1, QString(),
                         known_content_hash);
    }
    return QString();
}

bool CacheManager::frameReadyForPlayback(
    const std::shared_ptr<Title> &title, double time) const
{
    if (!title || !cache_enabled_.load() || interactive_bypass_.load() ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return false;

    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, title->duration));
    const CacheFrameKey key = keyForTime(
        *title, clamped_time, contentHash(*title));
    const FrameCacheState state = state_tracker_.state(key);
    if (state == FrameCacheState::Stale ||
        state == FrameCacheState::Disabled)
        return false;

    if (title_gpu_frame_cache_contains(key.toString().toStdString()) ||
        ram_cache_.contains(key))
        return true;
    return disk_cache_.contains(key);
}

bool CacheManager::titleFullyReadyForPlayback(
    const std::shared_ptr<Title> &title) const
{
    if (!title || !cache_enabled_.load() || interactive_bypass_.load() ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return false;

    const bool animated = titleHasTimelineChanges(*title);
    const int last_frame = animated
        ? cache_last_frame_for_title(*title, effectiveFrameRate()) : 0;
    const QString title_id = QString::fromStdString(title->id);
    const QString content_hash = contentHash(*title);
    for (int frame = 0; frame <= last_frame; ++frame) {
        const CacheFrameKey key{title_id, content_hash, frame,
                                title->width, title->height};
        const FrameCacheState state = state_tracker_.state(key);
        if (state == FrameCacheState::Stale ||
            state == FrameCacheState::Disabled)
            return false;
        if (!title_gpu_frame_cache_contains(key.toString().toStdString()) &&
            !ram_cache_.contains(key) && !disk_cache_.contains(key))
            return false;
    }
    return true;
}

quint64 CacheManager::ramBytesUsed() const
{
    return static_cast<quint64>(title_gpu_frame_cache_bytes_used()) +
           ram_cache_.bytesUsed();
}

void CacheManager::queueRealtimeJob(const std::shared_ptr<Title> &title, double time,
                                    bool live_cue, int cue_row, const QString &cue_state_key,
                                    const QString &known_content_hash)
{
    if (!title || !cache_enabled_.load() || interactive_bypass_.load() ||
        titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    const CacheFrameKey key = keyForTime(*title, time, known_content_hash);
    const bool force_render = state_tracker_.state(key) == FrameCacheState::Stale;
    RenderQueueManager::Job job;
    job.key = key;
    job.title = snapshotForJob(title, key);
    job.time = timeForFrame(key.frame);
    job.priority = live_cue ? static_cast<int>(RenderQueueManager::PriorityBand::LiveCue) * 100000
                            : static_cast<int>(RenderQueueManager::PriorityBand::Visible) * 100000;
    job.live_cue = live_cue;
    job.realtime = true;
    job.urgent = live_cue;
    job.force_render = force_render;
    job.cue_row = cue_row;
    job.cue_state_key = cue_state_key;
    job.cache_epoch = cache_epoch_.load();
    job.title_generation = titleGeneration(key.title_id);
    if (queue_.enqueue(job)) {
        state_tracker_.setState(key, FrameCacheState::Queued);
        wakeWorker();
    }
}

void CacheManager::rejectFramePayload(const std::shared_ptr<Title> &title,
                                      double time,
                                      const QString &known_content_hash)
{
    if (!title)
        return;

    const CacheFrameKey key = keyForTime(*title, time, known_content_hash);
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        ram_cache_.remove(key);
        title_gpu_frame_cache_remove(key.toString().toStdString());
        disk_cache_.remove(key);
        state_tracker_.setState(key, FrameCacheState::Stale);
    }
    FXM_LOG_WARNING("CachePlayback", QStringLiteral(
        "Rejected invalid cache payload title=%1 frame=%2 key=%3")
        .arg(key.title_id).arg(key.frame).arg(key.toString()));
    queueRealtimeJob(title, time, false, -1, QString(), known_content_hash);
    scheduleDiagnosticsChanged();
}

QImage CacheManager::requestFrameRealtime(const std::shared_ptr<Title> &title, double time,
                                              const QString &known_content_hash)
{
    if (!title || !cache_enabled_.load() || interactive_bypass_.load() ||
        titleCacheability(title) == TitleCacheability::NonCacheable) {
        log_cache_playback(QStringLiteral("request kind=standard result=bypass title=%1 time=%2 enabled=%3 interactive=%4")
            .arg(title ? QString::fromStdString(title->id) : QStringLiteral("<null>"))
            .arg(time, 0, 'f', 6).arg(cache_enabled_.load()).arg(interactive_bypass_.load()));
        return QImage();
    }
    const CacheFrameKey key = keyForTime(*title, time, known_content_hash);
    const FrameCacheState initial_state = state_tracker_.state(key);
    log_cache_playback(QStringLiteral("request kind=standard title=%1 time=%2 frame=%3 state=%4 key=%5")
        .arg(key.title_id).arg(time, 0, 'f', 6).arg(key.frame)
        .arg(QString::fromUtf8(cache_playback_state_name(initial_state)), key.toString()));
    if (initial_state == FrameCacheState::Stale) {
        queueRealtimeJob(title, time, false, -1, QString(), known_content_hash);
        log_cache_playback(QStringLiteral("result=stale-queued title=%1 frame=%2 key=%3")
            .arg(key.title_id).arg(key.frame).arg(key.toString()));
        return QImage();
    }
    QImage image;
    if (ram_cache_.get(key, image)) {
        state_tracker_.setState(key, FrameCacheState::CachedRam);
        fxm::perf::add(fxm::perf::Counter::CachePlaybackHits);
        log_cache_playback(QStringLiteral("result=ram-hit title=%1 frame=%2 cacheKey=%3 size=%4x%5 key=%6")
            .arg(key.title_id).arg(key.frame).arg(image.cacheKey()).arg(image.width()).arg(image.height()).arg(key.toString()));
        return image;
    }

    if (initial_state != FrameCacheState::Stale && disk_cache_.contains(key)) {
        queueRealtimeJob(title, time, false, -1, QString(), known_content_hash);
        log_cache_playback(QStringLiteral("result=disk-hydration-queued title=%1 frame=%2 key=%3")
            .arg(key.title_id).arg(key.frame).arg(key.toString()));
        return QImage();
    }

    fxm::perf::add(fxm::perf::Counter::CachePlaybackMisses);
    queueRealtimeJob(title, time, false, -1, QString(), known_content_hash);
    log_cache_playback(QStringLiteral("result=miss-queued title=%1 frame=%2 initialState=%3 diskAttempt=%4 key=%5")
        .arg(key.title_id).arg(key.frame)
        .arg(QString::fromUtf8(cache_playback_state_name(initial_state)))
        .arg(initial_state == FrameCacheState::CachedDisk).arg(key.toString()));
    return QImage();
}

void CacheManager::restoreDiskStates(const std::shared_ptr<Title> &title)
{
    if (!title || !cache_enabled_.load())
        return;
    if (titleCacheability(title) == TitleCacheability::NonCacheable) {
        /* Clock/ticker titles are dynamic by definition.  Remove stale cache
         * artifacts from older versions instead of publishing them as valid
         * disk states during session restore. */
        removeTitleCache(QString::fromStdString(title->id), true);
        return;
    }
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    const QString title_id = QString::fromStdString(title->id);
    const QString current_hash = contentHash(*title);
    const auto keys = disk_cache_.keysForTitle(title_id);
    for (const CacheFrameKey &key : keys) {
        if (key.content_hash == current_hash && key.width == title->width && key.height == title->height)
            state_tracker_.setState(key, FrameCacheState::CachedDisk);
    }

    ensure_live_text_row_ids(*title);
    for (int row = 0; row < (int)title->live_text_rows.size(); ++row) {
        const QString state_key = liveCueStateKey(title, row);
        const QString row_id = liveCueRowIdentity(title, row);
        QVector<CacheFrameKey> required_keys;
        QSet<CacheFrameKey> seen;
        for (const auto &variant : liveCueVariants(title, row)) {
            if (!variant)
                continue;
            for (const CacheFrameKey &frame_key : frameKeysForTitle(*variant)) {
                if (seen.contains(frame_key))
                    continue;
                seen.insert(frame_key);
                required_keys.push_back(frame_key);
                if (disk_cache_.contains(frame_key))
                    state_tracker_.setState(frame_key, FrameCacheState::CachedDisk);
            }
        }

        if (required_keys.isEmpty())
            continue;

        live_cue_required_keys_[state_key] = required_keys;
        live_cue_required_total_[state_key] = static_cast<int>(required_keys.size());
        live_cue_rows_[state_key] = row;
        live_cue_row_ids_[state_key] = row_id;
        live_cue_title_ids_[state_key] = title_id;
        live_cue_transition_state_keys_.remove(state_key);

        const FrameCacheState restored_state = liveCueStoredState(state_key);
        if (restored_state == FrameCacheState::CachedRam ||
            restored_state == FrameCacheState::CachedDisk) {
            live_cue_states_[state_key] = restored_state;
            live_cue_progress_percent_[state_key] = 100;
        } else {
            live_cue_required_keys_.remove(state_key);
            live_cue_required_total_.remove(state_key);
            live_cue_rows_.remove(state_key);
            live_cue_row_ids_.remove(state_key);
            live_cue_title_ids_.remove(state_key);
        }
    }
    /* Establish the editor's visual baseline even when every frame came from
     * disk. Without this, the first cue-list-only edit looked like an unknown
     * visual change and invalidateAll() discarded every live-cue state. */
    rememberVisualHash(*title, current_hash);
    scheduleDiagnosticsChanged();
}

void CacheManager::queueFrame(const std::shared_ptr<Title> &title, double time,
                              RenderQueueManager::PriorityBand band)
{
    if (!title)
        return;
    queueFrameWithHash(title, time, band, contentHash(*title));
}

void CacheManager::queueFrameWithHash(const std::shared_ptr<Title> &title, double time,
                                      RenderQueueManager::PriorityBand band,
                                      const QString &known_content_hash)
{
    if (!title) return;
    if (interactive_bypass_.load() || !cache_enabled_.load() || titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    if (time < 0.0 || time > std::max(0.0, title->duration))
        return;
    const CacheFrameKey key = keyForTime(*title, time, known_content_hash);
    const FrameCacheState existing_state = state_tracker_.state(key);
    if (existing_state != FrameCacheState::Stale &&
        (cache_ram_frame_contains(ram_cache_, key) ||
         disk_cache_.contains(key)))
        return;
    if (existing_state == FrameCacheState::Rendering || queue_.contains(key))
        return;
    const int band_offset = (int)band * 100000;
    RenderQueueManager::Job job;
    job.key = key;
    job.title = snapshotForJob(title, key);
    job.time = timeForFrame(key.frame);
    job.priority = band_offset + std::abs(key.frame);
    job.force_render = existing_state == FrameCacheState::Stale;
    job.cache_epoch = cache_epoch_.load();
    job.title_generation = titleGeneration(key.title_id);
    if (queue_.enqueue(job)) {
        FXM_LOG_TRACE("Cache", QStringLiteral("Queued frame title=%1 frame=%2 time=%3 band=%4 key=%5")
                                   .arg(key.title_id)
                                   .arg(key.frame)
                                   .arg(timeForFrame(key.frame), 0, 'f', 3)
                                   .arg((int)band)
                                   .arg(key.toString()));
        state_tracker_.setState(key, FrameCacheState::Queued);
        wakeWorker();
    }
}

void CacheManager::queueRange(const std::shared_ptr<Title> &title, double start, double end, RenderQueueManager::PriorityBand band)
{
    if (!title) return;
    if (interactive_bypass_.load() || !cache_enabled_.load() || titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    if (end < start) std::swap(start, end);
    const int max_frame = cache_last_frame_for_title(*title, effectiveFrameRate());
    const int first = std::clamp(frameForTime(std::clamp(start, 0.0, title->duration)), 0, max_frame);
    const int last = std::clamp(frameForTime(std::clamp(end, 0.0, title->duration)), 0, max_frame);
    const QString content_hash = contentHash(*title);
    const QString title_id = QString::fromStdString(title->id);
    const quint64 generation = titleGeneration(title_id);
    const quint64 epoch = cache_epoch_.load();
    const int band_offset = static_cast<int>(band) * 100000;
    QVector<RenderQueueManager::Job> jobs;
    QVector<CacheFrameKey> queued_keys;
    jobs.reserve(last - first + 1);
    queued_keys.reserve(last - first + 1);
    std::shared_ptr<Title> snapshot;
    for (int frame = first; frame <= last; ++frame) {
        const CacheFrameKey key{title_id, content_hash, frame, title->width, title->height};
        const FrameCacheState state = state_tracker_.state(key);
        if (state != FrameCacheState::Stale &&
            (cache_ram_frame_contains(ram_cache_, key) || disk_cache_.contains(key)))
            continue;
        if (state == FrameCacheState::Rendering || queue_.contains(key))
            continue;
        if (!snapshot)
            snapshot = snapshotForJob(title, key);
        if (!snapshot)
            break;
        RenderQueueManager::Job job;
        job.key = key;
        job.title = snapshot;
        job.time = timeForFrame(frame);
        job.priority = band_offset + std::abs(frame);
        job.force_render = state == FrameCacheState::Stale;
        job.cache_epoch = epoch;
        job.title_generation = generation;
        jobs.push_back(job);
        queued_keys.push_back(key);
    }
    if (jobs.isEmpty())
        return;
    queue_.enqueueMany(jobs);
    for (const CacheFrameKey &key : queued_keys) {
        if (queue_.contains(key))
            state_tracker_.setState(key, FrameCacheState::Queued);
    }
    wakeWorker();
}

void CacheManager::queueWholeTimeline(const std::shared_ptr<Title> &title)
{
    if (!title) return;
    if (interactive_bypass_.load() || !cache_enabled_.load() || titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    const int max_frame = cache_last_frame_for_title(*title, effectiveFrameRate());
    const QString title_id = QString::fromStdString(title->id);
    const int start_frame = last_reprioritize_title_id_ == title_id
        ? std::clamp(last_reprioritize_frame_, 0, max_frame)
        : 0;
    queueRange(title, timeForFrame(start_frame), timeForFrame(max_frame),
               RenderQueueManager::PriorityBand::FullTimeline);
    if (start_frame > 0)
        queueRange(title, 0.0, timeForFrame(start_frame - 1),
                   RenderQueueManager::PriorityBand::FullTimeline);
}

void CacheManager::queueWorkArea(const std::shared_ptr<Title> &title)
{
    if (!title) return;
    queueRange(title, title->loop_start, title->loop_end, RenderQueueManager::PriorityBand::WorkArea);
}

void CacheManager::reprioritize(const std::shared_ptr<Title> &title, double current_time)
{
    if (!title) return;
    if (interactive_bypass_.load() || !cache_enabled_.load() || titleCacheability(title) == TitleCacheability::NonCacheable)
        return;
    const int max_frame = cache_last_frame_for_title(*title, effectiveFrameRate());
    const int current = std::clamp(frameForTime(current_time), 0, max_frame);
    const QString title_id = QString::fromStdString(title->id);
    if (last_reprioritize_title_id_ == title_id && last_reprioritize_frame_ == current)
        return;
    last_reprioritize_title_id_ = title_id;
    last_reprioritize_frame_ = current;
    /* Realtime editor/OBS lookups promote the exact requested frame through
     * queueRealtimeJob. This neighbourhood fill must not rewrite and resort the
     * complete title queue on every playback tick. */
    const QString content_hash = contentHash(*title);
    queueFrameWithHash(title, current_time, RenderQueueManager::PriorityBand::Visible, content_hash);
    const int lookahead = std::min(6, std::max(2, (int)std::round(effectiveFrameRate() / 4.0)));
    const int lookbehind = 2;
    for (int i = 1; i <= lookahead; ++i)
        if (current + i <= max_frame)
            queueFrameWithHash(title, timeForFrame(current + i), RenderQueueManager::PriorityBand::AfterCurrent, content_hash);
    for (int i = 1; i <= lookbehind; ++i) {
        if (current - i >= 0)
            queueFrameWithHash(title, timeForFrame(current - i), RenderQueueManager::PriorityBand::BeforeCurrent, content_hash);
    }
}

void CacheManager::resetCancelledWorkState(const QString &title_id)
{
    state_tracker_.resetTransient(title_id);

    QVector<QPair<QString, int>> changed_rows;
    {
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        for (auto it = live_cue_states_.begin(); it != live_cue_states_.end(); ++it) {
            const QString state_key = it.key();
            if (!title_id.isEmpty() && live_cue_title_ids_.value(state_key) != title_id)
                continue;
            if (it.value() != FrameCacheState::Queued && it.value() != FrameCacheState::Rendering)
                continue;
            it.value() = FrameCacheState::NotCached;
            live_cue_progress_percent_[state_key] = liveCueStoredProgress(state_key);
            live_cue_last_emit_states_.remove(state_key);
            live_cue_last_emit_buckets_.remove(state_key);
            changed_rows.push_back({live_cue_title_ids_.value(state_key),
                                    live_cue_rows_.value(state_key, -1)});
        }
    }
    for (const auto &changed : changed_rows) {
        if (!changed.first.isEmpty() && changed.second >= 0)
            scheduleLiveCueStateChanged(changed.first, changed.second);
    }
}

void CacheManager::clearRam()
{
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        cache_epoch_.fetch_add(1);
        queue_.clear();
        ram_cache_.clear();
        QMutexLocker dedup_lock(&temporal_dedup_mutex_);
        temporal_canonical_keys_.clear();
        adaptive_canonical_keys_.clear();
    }
    title_gpu_frame_cache_clear();
    resetCancelledWorkState();
    {
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        live_cue_known_keys_.clear();
    }
    state_tracker_.clear();
    wakeWorker();
    scheduleLiveCueStateChanged(QString(), -1);
    scheduleDiagnosticsChanged();
}

void CacheManager::clearDisk()
{
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        cache_epoch_.fetch_add(1);
        queue_.clear();
        QMutexLocker dedup_lock(&temporal_dedup_mutex_);
        temporal_canonical_keys_.clear();
        adaptive_canonical_keys_.clear();
    }
    disk_cache_.clearFast();
    resetCancelledWorkState();
    state_tracker_.clear();
    wakeWorker();
    scheduleLiveCueStateChanged(QString(), -1);
    scheduleDiagnosticsChanged();
}

void CacheManager::clearAll()
{
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        cache_epoch_.fetch_add(1);
        queue_.clear();
        ram_cache_.clear();
        QMutexLocker dedup_lock(&temporal_dedup_mutex_);
        temporal_canonical_keys_.clear();
        adaptive_canonical_keys_.clear();
    }
    title_gpu_frame_cache_clear();
    disk_cache_.clearFast();
    resetCancelledWorkState();
    state_tracker_.clear();
    {
        std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
        live_cue_states_.clear();
        live_cue_progress_percent_.clear();
        live_cue_last_emit_states_.clear();
        live_cue_last_emit_buckets_.clear();
        live_cue_required_keys_.clear();
        live_cue_required_total_.clear();
        live_cue_rendered_since_emit_.clear();
        live_cue_total_by_key_.clear();
        live_cue_done_by_key_.clear();
        live_cue_rows_.clear();
        live_cue_row_ids_.clear();
        live_cue_title_ids_.clear();
        live_cue_transition_state_keys_.clear();
        live_cue_structure_row_ids_.clear();
        live_cue_row_fingerprints_.clear();
        live_cue_transition_signatures_.clear();
        live_cue_known_keys_.clear();
    }
    wakeWorker();
    scheduleLiveCueStateChanged(QString(), -1);
    scheduleDiagnosticsChanged();
}

void CacheManager::pausePrerender()
{
    paused_.store(true);
    wakeWorker();
}

void CacheManager::setCacheEnabled(bool enabled)
{
    if (cache_enabled_.load() == enabled)
        return;

    /* Invalidate every dequeued snapshot before changing externally visible
     * state. The worker may finish its local render, but jobIsCurrent() prevents
     * it from publishing into RAM/disk or touching live-cue state. */
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        cache_epoch_.fetch_add(1);
        queue_.setAcceptingJobs(false);
        cache_enabled_.store(enabled);
        paused_.store(!enabled);
        if (enabled)
            queue_.setAcceptingJobs(true);
    }
    resetCancelledWorkState();
    FXM_LOG_INFO("Cache", QStringLiteral("Cache enabled changed to %1").arg(enabled));
    TitlePreferences::set_cache_enabled(enabled);

    state_tracker_.clear();
    wakeWorker();
    emit cacheEnabledChanged(enabled);
    scheduleDiagnosticsChanged();
}

void CacheManager::setInteractiveBypass(bool bypass)
{
    if (interactive_bypass_.load() == bypass)
        return;
    interactive_bypass_.store(bypass);
    FXM_LOG_DEBUG("Cache", QStringLiteral("Interactive bypass changed to %1").arg(bypass));
    /* Keep accepting live-cue work. The worker predicate pauses only normal
     * prerender jobs while the editor is manipulating the canvas. */
    queue_.setAcceptingJobs(cache_enabled_.load());
    wakeWorker();
}

void CacheManager::setEditorPrerenderFocus(const QString &title_id, bool active)
{
    {
        QMutexLocker lock(&editor_focus_mutex_);
        editor_focus_active_ = active && !title_id.isEmpty();
        editor_focus_title_id_ = editor_focus_active_ ? title_id : QString();
    }
    FXM_LOG_DEBUG("Cache", QStringLiteral("Editor prerender focus active=%1 title=%2")
                               .arg(active && !title_id.isEmpty())
                               .arg(title_id));
    wakeWorker();
}


void CacheManager::setRamCacheLimitMb(int megabytes)
{
    const int clamped = std::clamp(megabytes, 64, 32768);
    TitlePreferences::set_cache_ram_limit_mb(clamped);
    const quint64 bytes = (quint64)clamped * 1024ull * 1024ull;
    const quint64 compatibility_ram_budget = std::clamp<quint64>(
        bytes / 8ull,
        16ull * 1024ull * 1024ull,
        64ull * 1024ull * 1024ull);
    ram_cache_.setMaxBytes(compatibility_ram_budget);
    title_gpu_frame_cache_set_budget(
        bytes > compatibility_ram_budget
            ? bytes - compatibility_ram_budget
            : bytes);
    scheduleDiagnosticsChanged();
}

void CacheManager::setDiskCacheLocation(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty())
        return;
    {
        std::lock_guard<std::recursive_mutex> publication_lock(publication_mutex_);
        cache_epoch_.fetch_add(1);
        queue_.clear();
        disk_cache_.setCacheDirectory(clean);
    }
    TitlePreferences::set_cache_disk_location(clean);
    resetCancelledWorkState();
    FXM_LOG_INFO("Cache", QStringLiteral("Disk cache location changed to %1").arg(clean));
    state_tracker_.clear();
    wakeWorker();
    scheduleDiagnosticsChanged();
}

TitleCacheability CacheManager::titleCacheability(const std::shared_ptr<Title> &title) const
{
    if (!title)
        return TitleCacheability::NonCacheable;
    const TitleDynamicLayerAnalysis analysis = analyze_title_dynamic_layers(*title);
    if (!analysis.has_dynamic_layers)
        return TitleCacheability::Cacheable;
    return analysis.has_cacheable_prefix
        ? TitleCacheability::PartiallyCacheable
        : TitleCacheability::NonCacheable;
}

QString CacheManager::titleCacheabilityMessage(const std::shared_ptr<Title> &title) const
{
    if (!cache_enabled_.load())
        return fxm_tr("OBSTitles.CacheDisabledMessage");
    if (titleCacheability(title) == TitleCacheability::NonCacheable)
        return fxm_tr("OBSTitles.PrerenderDynamicUnavailable");
    return QString();
}

FrameCacheState CacheManager::displayStateForFrame(const std::shared_ptr<Title> &title, int frame) const
{
    if (!title)
        return FrameCacheState::NotCached;
    const QString title_id = QString::fromStdString(title->id);
    const int display_frame = titleHasTimelineChanges(*title)
        ? std::clamp(frame, 0, cache_last_frame_for_title(*title, effectiveFrameRate()))
        : 0;
    return state_tracker_.stateForFrame(title_id, display_frame);
}

QHash<int, FrameCacheState> CacheManager::displayStatesForRange(
    const std::shared_ptr<Title> &title, int first_frame, int last_frame) const
{
    QHash<int, FrameCacheState> result;
    if (!title)
        return result;
    if (last_frame < first_frame)
        std::swap(first_frame, last_frame);

    const QString title_id = QString::fromStdString(title->id);
    const bool animated = titleHasTimelineChanges(*title);
    const int final_frame = animated
        ? cache_last_frame_for_title(*title, effectiveFrameRate()) : 0;
    result.reserve(last_frame - first_frame + 1);
    const int indexed_first = animated
        ? std::clamp(first_frame, 0, final_frame) : 0;
    const int indexed_last = animated
        ? std::clamp(last_frame, indexed_first, final_frame) : 0;
    const QHash<int, FrameCacheState> indexed = state_tracker_.statesForRange(
        title_id, indexed_first, indexed_last);
    for (int frame = first_frame; frame <= last_frame; ++frame) {
        const int display_frame = animated
            ? std::clamp(frame, 0, final_frame) : 0;
        result.insert(frame, indexed.value(display_frame,
                                           FrameCacheState::NotCached));
    }
    fxm::perf::add(fxm::perf::Counter::TimelineCacheFramesInspected,
                   static_cast<std::uint64_t>(result.size()));
    return result;
}

QHash<int, bool> CacheManager::displayStaticFramesForRange(
    const std::shared_ptr<Title> &title, int first_frame, int last_frame) const
{
    QHash<int, bool> result;
    if (!title)
        return result;
    if (last_frame < first_frame)
        std::swap(first_frame, last_frame);
    result.reserve(last_frame - first_frame + 1);

    if (!titleHasTimelineChanges(*title)) {
        for (int frame = first_frame; frame <= last_frame; ++frame)
            result.insert(frame, true);
        return result;
    }

    const double fps = std::max(1.0, effectiveFrameRate());
    const int final_frame = cache_last_frame_for_title(*title, fps);
    const int clamped_first = std::clamp(first_frame, 0, final_frame);
    const int clamped_last = std::clamp(last_frame, clamped_first, final_frame);
    const int hash_first = std::max(0, clamped_first - 1);
    const int hash_last = std::min(final_frame, clamped_last + 1);
    const QString content_hash = contentHash(*title);

    /* Timeline paint previously called contentHash() and evaluated the same
     * neighbouring visual states up to three times for every visible frame.
     * Build one contiguous hash window per paint so the cost is O(N), not
     * O(N * full-title-hash), and keep the cache strip entirely read-only. */
    QHash<int, QString> visual_hashes;
    visual_hashes.reserve(hash_last - hash_first + 1);
    for (int frame = hash_first; frame <= hash_last; ++frame) {
        visual_hashes.insert(frame, adaptiveVisualStateHash(
            *title, static_cast<double>(frame) / fps, content_hash));
    }
    for (int frame = first_frame; frame <= last_frame; ++frame) {
        const int f = std::clamp(frame, 0, final_frame);
        const QString current = visual_hashes.value(f);
        const bool matches_previous = f > 0 &&
            visual_hashes.value(f - 1) == current;
        const bool matches_next = f < final_frame &&
            visual_hashes.value(f + 1) == current;
        result.insert(frame, matches_previous || matches_next);
    }
    return result;
}

bool CacheManager::displayFrameIsStatic(const std::shared_ptr<Title> &title,
                                        int frame) const
{
    return displayStaticFramesForRange(title, frame, frame).value(frame, false);
}

QVector<CacheTileRegion> CacheManager::tilesForRect(const QRect &rect, const QSize &frame_size) const
{
    QVector<CacheTileRegion> tiles;
    if (rect.isEmpty() || frame_size.isEmpty())
        return tiles;
    const QRect frame_rect(QPoint(0, 0), frame_size);
    const QRect clipped = rect.intersected(frame_rect);
    if (clipped.isEmpty())
        return tiles;
    const int first_x = clipped.left() / kCacheTileSize;
    const int last_x = clipped.right() / kCacheTileSize;
    const int first_y = clipped.top() / kCacheTileSize;
    const int last_y = clipped.bottom() / kCacheTileSize;
    for (int ty = first_y; ty <= last_y; ++ty) {
        for (int tx = first_x; tx <= last_x; ++tx) {
            const QRect tile_rect(tx * kCacheTileSize, ty * kCacheTileSize, kCacheTileSize, kCacheTileSize);
            tiles.push_back({tx, ty, tile_rect.intersected(frame_rect)});
        }
    }
    return tiles;
}

static std::pair<double, double> animated_scalar_extents(
    const AnimatedProperty &property)
{
    double minimum = property.static_value;
    double maximum = property.static_value;
    for (const Keyframe &keyframe : property.keyframes) {
        minimum = std::min(minimum, keyframe.value);
        maximum = std::max(maximum, keyframe.value);
    }
    return {minimum, maximum};
}

static double animated_scalar_max_nonnegative(
    const AnimatedProperty &property, double fallback)
{
    const auto extents = animated_scalar_extents(property);
    return std::max({0.0, fallback, extents.second});
}

struct AnimatedVec2Extents {
    double min_x = 0.0;
    double max_x = 0.0;
    double min_y = 0.0;
    double max_y = 0.0;
};

static AnimatedVec2Extents animated_vec2_extents(
    const AnimatedVec2Property &property)
{
    AnimatedVec2Extents extents {
        property.static_value.x, property.static_value.x,
        property.static_value.y, property.static_value.y};
    auto include = [&](const Vec2Value &value) {
        extents.min_x = std::min(extents.min_x, value.x);
        extents.max_x = std::max(extents.max_x, value.x);
        extents.min_y = std::min(extents.min_y, value.y);
        extents.max_y = std::max(extents.max_y, value.y);
    };
    for (const VectorKeyframe &keyframe : property.keyframes)
        include(keyframe.value);

    /* A cubic Bezier is contained by the convex hull of its four control
     * points. Including those points gives a deterministic conservative dirty
     * envelope for manual/continuous/auto spatial curves without frame
     * sampling, so prerender tiles cannot clip curved position motion. */
    for (size_t segment = 0; segment + 1 < property.keyframes.size(); ++segment) {
        const VectorKeyframe &start = property.keyframes[segment];
        const VectorKeyframe &end = property.keyframes[segment + 1];
        const Vec2Value outgoing = property.resolved_outgoing_tangent(segment);
        const Vec2Value incoming = property.resolved_incoming_tangent(segment + 1);
        include({start.value.x + outgoing.x, start.value.y + outgoing.y});
        include({end.value.x + incoming.x, end.value.y + incoming.y});
    }
    return extents;
}

QRect CacheManager::layerDirtyRect(const Title &title, const Layer &layer) const
{
    const QSize frame_size(std::max(1, title.width), std::max(1, title.height));
    const QRect frame_rect(QPoint(0, 0), frame_size);
    if (!layer.visible || layer.live_cue_hidden_if_empty)
        return QRect();
    if (layer.type == LayerType::Adjustment || layer.type == LayerType::ColorSolid)
        return frame_rect;

    // Perspective projection, 3D parenting and camera motion can move a planar
    // layer anywhere on the output canvas. Keep dirty-region invalidation
    // conservative for 3D titles; purely 2D titles retain the legacy bounds
    // calculation and its tile-level performance characteristics.
    const bool title_uses_3d = std::any_of(
        title.layers.begin(), title.layers.end(),
        [](const std::shared_ptr<Layer> &candidate) {
            return candidate && candidate->dimension_mode == LayerDimensionMode::ThreeD;
        });
    if (title_uses_3d || fxm::transform3d::title_camera_has_animation(title))
        return frame_rect;

    /* Dirty tiles are shared by every invalidated frame in the layer range, so
     * their bounds must cover the whole animated envelope rather than only the
     * static/default values. Scalar temporal easing remains within keyframe
     * extrema; vector spatial curves additionally include their cubic control
     * hull in animated_vec2_extents(). */
    const AnimatedVec2Extents position = animated_vec2_extents(layer.position);
    const AnimatedVec2Extents scale = animated_vec2_extents(layer.scale);
    const AnimatedVec2Extents size = animated_vec2_extents(layer.size);
    const double sx = std::max({0.01, std::abs(scale.min_x),
                                std::abs(scale.max_x)});
    const double sy = std::max({0.01, std::abs(scale.min_y),
                                std::abs(scale.max_y)});
    double w = std::max({1.0, std::abs(size.min_x),
                         std::abs(size.max_x)}) * sx;
    double h = std::max({1.0, std::abs(size.min_y),
                         std::abs(size.max_y)}) * sy;

    if (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
        layer.type == LayerType::Ticker) {
        const double font_size = animated_scalar_max_nonnegative(
            layer.font_size_prop, std::max(1, layer.font_size));
        const std::string effective_text = effective_external_string(
            layer, ExternalPropertyPaths::TextContent, layer.text_content);
        w = std::max(w, font_size *
                         std::max<size_t>(1, effective_text.size()) *
                         0.75 * sx);
        h = std::max(h, font_size * 1.6 * sy);
    } else if (layer_type_is_image_like(layer.type) && w <= 1.0 && h <= 1.0) {
        w = frame_size.width();
        h = frame_size.height();
    }
