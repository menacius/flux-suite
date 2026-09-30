            /* A group can also be a bounded adjustment layer.  Its composed
             * alpha is the coverage silhouette, while the actual group artwork
             * is still composited normally afterwards. */
            for (const LayerEffect &backdrop_effect : layer.effects) {
                if (!eval_effect_enabled(backdrop_effect,
                                         std::max(0.0, layer_time - layer.in_time)) ||
                    !backdrop_effect.affect_layers_behind)
                    continue;
                Layer adjustment_group = layer;
                adjustment_group.effects.clear();
                adjustment_group.effects.push_back(backdrop_effect);
                gs_texture_t *adjusted_background = apply_gpu_layer_effect_stack(
                    session, adjustment_group, layer_time, frame,
                    width, height, nullptr, true, frame, true);
                if (!adjusted_background || adjusted_background == frame)
                    continue;
                gs_texrender_t *backdrop_target = current_is_a
                    ? session->frame_b : session->frame_a;
                gs_texture_t *mixed_background = mix_gpu_adjustment_layer(
                    session, session->title, adjustment_group, session->time,
                    frame, adjusted_background, group_silhouette,
                    backdrop_target, true);
                if (mixed_background && mixed_background != frame) {
                    frame = mixed_background;
                    current_is_a = !current_is_a;
                }
            }

            gs_texrender_t *next = current_is_a ? session->frame_b
                                                : session->frame_a;
            const double opacity = gpu_layer_local_opacity(layer, layer_time);
            if (!composite_gpu_frame_layer(session, frame, foreground,
                                           layer.blend_mode, opacity, next)) {
                if (group_silhouette_target)
                    gs_texrender_destroy(group_silhouette_target);
                continue;
            }
            frame = gs_texrender_get_texture(next);
            current_is_a = !current_is_a;
            if (group_silhouette_target)
                gs_texrender_destroy(group_silhouette_target);
            continue;
        }
        if (layer.type == LayerType::Adjustment) {
            /* The editor canvas destination is a presentation background, not
             * artwork input. AE-style opacity and blend modes may sample it,
             * but adjustment effects must not blur, color-correct, distort or
             * otherwise process the checkerboard/editor underlay. OBS source
             * rendering keeps the full destination-aware adjustment behavior. */
            gs_texture_t *coverage = render_gpu_adjustment_coverage(
                session, session->title, layer, session->time);
            if (!coverage)
                continue;
            gs_texture_t *motion_adjusted = apply_gpu_adjustment_motion_blur(
                session, layer, session->time, frame);
            gs_texture_t *adjusted = apply_gpu_layer_effect_stack(
                session, layer, layer_time, motion_adjusted, width, height,
                nullptr, false, frame, true);
            if (adjusted && adjusted != frame) {
                gs_texrender_t *next = current_is_a ? session->frame_b
                                                    : session->frame_a;
                gs_texture_t *mixed = mix_gpu_adjustment_layer(
                    session, session->title, layer, session->time,
                    frame, adjusted, coverage, next);
                if (mixed && mixed != frame) {
                    frame = mixed;
                    current_is_a = !current_is_a;
                }
            }
            continue;
        }
        const bool has_mask = layer.mask_mode != MaskMode::None &&
                              !layer.mask_source_id.empty();
        const bool effects_after_mask = has_mask &&
                                        layer.effect_stack_respects_masks;
        if (!render_gpu_layer_to_target(session, session->title, layer,
                                        session->time, session->layer_target,
                                        !effects_after_mask, std::string(), 1.0,
                                        std::string(), false, frame))
            continue;
        gs_texture_t *foreground = gs_texrender_get_texture(session->layer_target);
        if (!foreground)
            continue;

        bool has_backdrop_effect = false;
        for (const LayerEffect &effect : layer.effects) {
            if (effect.enabled && effect.affect_layers_behind) {
                has_backdrop_effect = true;
                break;
            }
        }
        if (has_backdrop_effect) {
            /* Build the bounded-adjustment mask from the untouched artwork,
             * before any normal or backdrop effect can expand/change alpha.
             * The artwork remains visible later in the regular foreground pass. */
            gs_texture_t *silhouette = nullptr;
            if (render_gpu_layer_to_target(session, session->title, layer,
                                           session->time,
                                           session->adjustment_coverage_target,
                                           false, std::string(), 1.0)) {
                silhouette = fxm::obs_plugin::native_texture(
                    session->adjustment_coverage_target_resource->texture());
            }
            if (silhouette && has_mask) {
                const Layer *silhouette_mask_layer = find_layer_by_id(
                    session->title, layer.mask_source_id);
                if (silhouette_mask_layer && gpu_layer_chain_active_as_matte(
                        session->title, *silhouette_mask_layer,
                        session->time)) {
                    silhouette = apply_gpu_track_matte_texture(
                        session, session->title, silhouette,
                        *silhouette_mask_layer, session->time, std::string(),
                        layer.parent_id, layer.mask_mode);
                } else if (!mask_mode_is_inverted(layer.mask_mode)) {
                    silhouette = nullptr;
                }
            }

            if (silhouette) {
                /* Evaluate backdrop effects independently. Besides preserving
                 * stack order, this lets every effect own its mask inversion. */
                for (const LayerEffect &backdrop_effect : layer.effects) {
                    if (!eval_effect_enabled(backdrop_effect,
                                             std::max(0.0, layer_time - layer.in_time)) ||
                        !backdrop_effect.affect_layers_behind)
                        continue;
                    Layer adjustment_layer = layer;
                    adjustment_layer.effects.clear();
                    adjustment_layer.effects.push_back(backdrop_effect);
                    gs_texture_t *adjusted_background = apply_gpu_layer_effect_stack(
                        session, adjustment_layer, layer_time, frame,
                        width, height, nullptr, true, frame, true);
                    if (!adjusted_background || adjusted_background == frame)
                        continue;
                    gs_texrender_t *backdrop_target = current_is_a
                        ? session->frame_b : session->frame_a;
                    gs_texture_t *mixed_background = mix_gpu_adjustment_layer(
                        session, session->title, adjustment_layer, session->time,
                        frame, adjusted_background, silhouette,
                        backdrop_target, true);
                    if (mixed_background && mixed_background != frame) {
                        frame = mixed_background;
                        current_is_a = !current_is_a;
                    }
                }
            }
        }

        /* Building a bounded backdrop silhouette may evaluate the same track
         * matte graph before the visible foreground is masked. A Group matte
         * recursively reuses shared layer/effect targets, so restore the
         * consumer foreground after the backdrop pass instead of trusting a
         * texture pointer whose storage may have been repurposed. */
        if (has_backdrop_effect) {
            if (!render_gpu_layer_to_target(
                    session, session->title, layer, session->time,
                    session->layer_target, !effects_after_mask,
                    std::string(), 1.0, std::string(), false, frame))
                continue;
            foreground = gs_texrender_get_texture(session->layer_target);
            if (!foreground)
                continue;
        }

        if (has_mask) {
            const Layer *mask_layer = find_layer_by_id(session->title,
                                                       layer.mask_source_id);
            if (mask_layer &&
                gpu_layer_chain_active_as_matte(session->title, *mask_layer, session->time)) {
                foreground = apply_gpu_track_matte_texture(
                    session, session->title, foreground, *mask_layer,
                    session->time, std::string(), layer.parent_id,
                    layer.mask_mode);
                if (!foreground)
                    continue;
            } else if (!mask_mode_is_inverted(layer.mask_mode)) {
                continue;
            }
        }

        if (effects_after_mask) {
            foreground = apply_gpu_layer_effect_stack(
                session, layer, layer_time, foreground,
                session->width, session->height, nullptr, false, frame, true);
            if (!foreground)
                continue;
        }

        gs_texrender_t *next = current_is_a ? session->frame_b
                                            : session->frame_a;
        const double composite_opacity = std::clamp(
            gpu_layer_chain_opacity(session->title, layer, session->time), 0.0, 1.0);
        if (!composite_gpu_frame_layer(session, frame, foreground,
                                       layer.blend_mode, composite_opacity, next))
            continue;
        frame = gs_texrender_get_texture(next);
        current_is_a = !current_is_a;
    }

    if (destination_composite) {
        if (isolate_editor_background_from_effects) {
            gs_texrender_t *next = current_is_a ? session->frame_b : session->frame_a;
            if (composite_gpu_frame_layer(session, external_background, frame,
                                          EffectBlendMode::Normal, 1.0, next))
                frame = gs_texrender_get_texture(next);
        }
        prune_gpu_mask_texture_cache(session, frame);
        prune_gpu_effect_output_cache(session, frame);
        session->last_error = nullptr;
        return frame;
    }

    gs_texture_t *published = publish_stable_gpu_frame(session, frame);
    if (!published) {
        session->last_error = "Could not publish stable GPU frame.";
        return gpu_session_has_published_frame_for_current_title(session)
            ? session->final_texture : nullptr;
    }
    session->frame_dirty = false;
    prune_gpu_mask_texture_cache(session, published);
    prune_gpu_effect_output_cache(session, published);
    session->last_error = nullptr;
    FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
        "stage=session-render-end session=%1 render=%2 result=published time=%3 revision=%4 publish=%5 dirty=%6 depthRuns=%7 depthLayers=%8 extrusionPasses=%9")
        .arg(reinterpret_cast<quintptr>(session), 0, 16)
        .arg(diagnostic_render_serial)
        .arg(session->time, 0, 'f', 9)
        .arg(session->model_revision)
        .arg(session->diagnostics_publish_serial)
        .arg(session->frame_dirty ? 1 : 0)
        .arg(session->diagnostics_hardware_depth_run_count)
        .arg(session->diagnostics_hardware_depth_layer_count)
        .arg(session->diagnostics_extrusion_pass_count));
    return published;
    } catch (const std::exception &error) {
        /* No C++ exception may cross an OBS render callback or a Qt paint/event
         * handler. Preserve the last transactionally published frame, mark the
         * graph dirty for a later retry and expose the exact failure in the FXM
         * log instead of terminating the host process. */
        session->motion_temporal_depth = 0;
        session->frame_dirty = true;
        session->last_draw_deferred = true;
        session->last_error_storage =
            std::string("GPU compositor exception: ") + error.what();
        session->last_error = session->last_error_storage.c_str();
        FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
            "stage=session-render-exception session=%1 render=%2 title=%3 time=%4 error=%5 fallbackPublished=%6 destination=%7")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(diagnostic_render_serial)
            .arg(QString::fromStdString(session->title.id))
            .arg(session->time, 0, 'f', 9)
            .arg(QString::fromUtf8(error.what()))
            .arg(gpu_session_has_published_frame_for_current_title(session)
                     ? 1 : 0)
            .arg(destination_composite ? 1 : 0));
        return destination_composite
            ? nullptr
            : (gpu_session_has_published_frame_for_current_title(session)
                   ? session->final_texture
                   : nullptr);
    } catch (...) {
        session->motion_temporal_depth = 0;
        session->frame_dirty = true;
        session->last_draw_deferred = true;
        session->last_error_storage = "Unknown GPU compositor exception.";
        session->last_error = session->last_error_storage.c_str();
        FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
            "stage=session-render-exception session=%1 render=%2 title=%3 time=%4 error=unknown fallbackPublished=%5 destination=%6")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(diagnostic_render_serial)
            .arg(QString::fromStdString(session->title.id))
            .arg(session->time, 0, 'f', 9)
            .arg(gpu_session_has_published_frame_for_current_title(session)
                     ? 1 : 0)
            .arg(destination_composite ? 1 : 0));
        return destination_composite
            ? nullptr
            : (gpu_session_has_published_frame_for_current_title(session)
                   ? session->final_texture
                   : nullptr);
    }
}

static bool stage_gpu_texture_locked(TitleGpuRenderSession *session,
                                     gs_texture_t *texture,
                                     QImage &result)
{
    result = QImage();
    if (!session || !texture || session->width == 0 || session->height == 0)
        return false;
    if (!session->stage || session->stage_width != session->width ||
        session->stage_height != session->height) {
        if (session->stage)
            gs_stagesurface_destroy(session->stage);
        session->stage = gs_stagesurface_create(session->width,
                                                session->height,
                                                GS_BGRA);
        session->stage_width = session->stage ? session->width : 0;
        session->stage_height = session->stage ? session->height : 0;
    }
    if (!session->stage)
        return false;

    gs_stage_texture(session->stage, texture);
    uint8_t *mapped = nullptr;
    uint32_t linesize = 0;
    if (!gs_stagesurface_map(session->stage, &mapped, &linesize) ||
        !mapped || linesize < session->width * 4)
        return false;

    QImage image(static_cast<int>(session->width),
                 static_cast<int>(session->height),
                 QImage::Format_ARGB32_Premultiplied);
    if (!image.isNull()) {
        for (uint32_t y = 0; y < session->height; ++y) {
            std::memcpy(image.scanLine(static_cast<int>(y)),
                        mapped + static_cast<size_t>(y) * linesize,
                        static_cast<size_t>(session->width) * 4);
        }
    }
    gs_stagesurface_unmap(session->stage);
    if (image.isNull())
        return false;
    result = std::move(image);
    return true;
}

static QImage make_ae_checkerboard(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return QImage();
    QImage image(static_cast<int>(width), static_cast<int>(height),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return image;
    const int square = 24;
    const QRgb dark = qRgba(58, 58, 58, 255);
    const QRgb light = qRgba(82, 82, 82, 255);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        const int tile_y = y / square;
        for (int x = 0; x < image.width(); ++x)
            line[x] = (((x / square) + tile_y) & 1) ? light : dark;
    }
    return image;
}

static QImage title_gpu_render_session_readback_over_checkerboard(
    TitleGpuRenderSession *session)
{
    if (!session || session->destroying.load())
        return QImage();

    QImage result;
    obs_enter_graphics();
    std::unique_lock<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        !prepare_external_composite_dimensions_locked(session)) {
        lock.unlock();
        obs_leave_graphics();
        return QImage();
    }

    /* Render the title artwork against transparent black. The checkerboard is
     * editor chrome, not an adjustment-layer input: blur/distort/color effects
     * must sample only actual title pixels. Passing a destination texture here
     * previously made the checkerboard part of the GPU frame and produced
     * incorrect adjustment blur semantics. */
    gs_texture_t *texture = render_gpu_session_locked(
        session, nullptr, true);
    if (texture)
        stage_gpu_texture_locked(session, texture, result);
    lock.unlock();
    obs_leave_graphics();

    if (result.isNull())
        return result;

    /* Composite transparency chrome only after every artwork/effect pass. This
     * keeps the checkerboard sharp while retaining effect-generated alpha such
     * as blur tails, shadows and glows outside layer bounds. */
    QImage background = make_ae_checkerboard(
        static_cast<uint32_t>(result.width()),
        static_cast<uint32_t>(result.height()));
    if (background.isNull())
        return result;
    QPainter painter(&background);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, result);
    painter.end();
    return background;
}

TitleGpuRenderSession *title_gpu_render_session_create()
{
    return new TitleGpuRenderSession();
}

void title_gpu_render_session_invalidate_presentation(
    TitleGpuRenderSession *session, bool discard_model)
{
    if (!session || session->destroying.load())
        return;

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return;
    /* Do not destroy texrender resources here: lifecycle invalidation can be
     * requested from the OBS frontend thread while video_render owns the
     * graphics context. Clearing only the published references is enough to
     * guarantee that the next draw cannot sample a texture from the previous
     * source/title/scene-collection generation. The resources are safely
     * reused or destroyed later on the graphics thread. */
    session->final_texture = nullptr;
    session->published_title_id.clear();
    session->published_model_revision =
        std::numeric_limits<uint64_t>::max();
    session->active_presentation_target = -1;
    session->published_presentation_generation = 0;
    for (int index = 0; index < 2; ++index) {
        session->presentation_target_states[index] =
            TitleGpuRenderSession::PresentationTargetState::Free;
        session->presentation_target_generations[index] = 0;
    }
    session->published_final_serial = 0;

    session->use_submitted_final = false;
    session->use_gpu_cached_final = false;
    session->submitted_gpu_cache_key.clear();
    session->pending_submitted_final = QImage();
    session->submitted_final_pending = false;
    session->submitted_final_image_key = 0;
    session->submitted_final_rect = QRect();

    session->use_base_frame = false;
    session->use_gpu_cached_base = false;
    session->base_gpu_cache_key.clear();
    session->pending_base_frame = QImage();
    session->base_frame_pending = false;
    session->base_frame_image_key = 0;
    session->base_frame_rect = QRect();

    session->frame_dirty = true;
    session->last_error = nullptr;

    if (discard_model) {
        session->has_title = false;
        session->model_revision = std::numeric_limits<uint64_t>::max();
        session->time = 0.0;
        session->first_layer = 0;
        session->last_layer = std::numeric_limits<std::size_t>::max();
        /* Force every layer that survives by ID into a fresh raster-key
         * comparison. Stale non-live entries are retired by update_range and
         * destroyed from render_gpu_session_locked under the graphics lock. */
        for (auto &pair : session->layers) {
            pair.second.key.clear();
            pair.second.effect_cache_key.clear();
        }
        for (auto &pair : session->mask_texture_cache)
            pair.second.key.clear();
    }
}

void title_gpu_render_session_destroy(TitleGpuRenderSession *session)
{
    if (!session)
        return;
    bool expected = false;
    if (!session->destroying.compare_exchange_strong(expected, true))
        return;
    stop_shader_compile_worker(session);
    /* Editor presentation and OBS source drawing execute under the graphics
     * lock and then take session->mutex. Taking the mutex first here creates
     * the inverse order (session -> graphics) and can deadlock shutdown against
     * an in-flight draw (graphics -> session). Mark destruction first, wait for
     * graphics presentation to drain, and only then take the session mutex. */
    obs_enter_graphics();
    std::unique_lock<std::mutex> lock(session->mutex);
    for (auto &pair : session->layers) {
        if (pair.second.text_layer)
            release_gpu_text_layer(session, pair.second);
        if (pair.second.primitive_targets[0] || pair.second.primitive_targets[1]) {
            for (gs_texrender_t *target : pair.second.primitive_targets) {
                if (target)
                    gs_texrender_destroy(target);
            }
        } else if (pair.second.texture) {
            gs_texture_destroy(pair.second.texture);
        }
        if (pair.second.effect_cache)
            gs_texrender_destroy(pair.second.effect_cache);
        if (pair.second.transform_grid)
            gs_vertexbuffer_destroy(pair.second.transform_grid);
        pair.second.transform_grid = nullptr;
        pair.second.transform_grid_key.clear();
    }
    clear_runtime_scene_mask_rasters_locked(session);
    session->effect_cache_bytes = 0;
    if (session->text_renderer)
        session->text_renderer->reset();
    auto destroy_target = [](gs_texrender_t *&target) {
        if (target)
            gs_texrender_destroy(target);
        target = nullptr;
    };
    destroy_target(session->frame_a);
    destroy_target(session->frame_b);
    destroy_target(session->depth_target);
    for (gs_texrender_t *&shadow_target : session->shadow_targets)
        destroy_target(shadow_target);
    if (session->close_point_text_shadow_grid)
        gs_vertexbuffer_destroy(session->close_point_text_shadow_grid);
    session->close_point_text_shadow_grid = nullptr;
    destroy_target(session->presentation_targets[0]);
    destroy_target(session->presentation_targets[1]);
    destroy_target(session->layer_target);
    destroy_target(session->mask_target);
    session->masked_target_resource.reset();
    session->masked_target = nullptr;
    session->adjustment_coverage_target_resource.reset();
    session->adjustment_coverage_target = nullptr;
    destroy_target(session->effect_a);
    destroy_target(session->effect_b);
    destroy_target(session->blur_a);
    destroy_target(session->blur_b);
    destroy_target(session->motion_sample_target);
    destroy_target(session->motion_accum_target);
    for (auto &entry : session->adjustment_motion_history) {
        for (gs_texrender_t *&target : entry.second.frames)
            destroy_target(target);
    }
    session->adjustment_motion_history.clear();
    session->external_background_local.reset();
    session->external_background_snapshot_resource.reset();
    session->external_background_snapshot = nullptr;
    if (session->stage)
        gs_stagesurface_destroy(session->stage);
    for (auto &slot : session->readback_slots) {
        if (slot.stage)
            gs_stagesurface_destroy(slot.stage);
        if (slot.crop_target)
            gs_texrender_destroy(slot.crop_target);
        slot = {};
    }
    session->cached_frame_textures.clear();
    session->cached_frame_texture_bytes = 0;
    for (auto &cached : session->mask_texture_cache) {
        for (gs_texrender_t *target : cached.second.targets) {
            if (target)
                gs_texrender_destroy(target);
        }
    }
    session->mask_texture_cache.clear();
    session->mask_texture_cache_bytes = 0;
    for (auto &cached : session->transition_matte_textures) {
        cached.second.texture_resource.reset();
        cached.second.texture = nullptr;
    }
    session->transition_matte_textures.clear();
    session->transition_matte_cache_bytes = 0;
    /* submitted_final_texture/base_frame_texture are borrowed from the pool
     * whenever they have a cacheKey. Unkeyed temporary textures are owned
     * directly by the corresponding field. */
    if (session->submitted_final_texture &&
        session->submitted_final_image_key == 0)
        gs_texture_destroy(session->submitted_final_texture);
    if (session->base_frame_texture && session->base_frame_image_key == 0)
        gs_texture_destroy(session->base_frame_texture);
    if (session->blit_effect)
        gs_effect_destroy(session->blit_effect);
    if (session->copy_effect)
        gs_effect_destroy(session->copy_effect);
    if (session->shadow_effect)
        gs_effect_destroy(session->shadow_effect);
    if (session->motion_composite_effect)
        gs_effect_destroy(session->motion_composite_effect);
    if (session->adjustment_mix_effect)
        gs_effect_destroy(session->adjustment_mix_effect);
    if (session->adjustment_coverage_effect)
        gs_effect_destroy(session->adjustment_coverage_effect);
    if (session->primitive_shape_effect)
        gs_effect_destroy(session->primitive_shape_effect);
    if (session->blend_effect)
        gs_effect_destroy(session->blend_effect);
    if (session->mask_effect)
        gs_effect_destroy(session->mask_effect);
    if (session->external_background_effect)
        gs_effect_destroy(session->external_background_effect);
    if (session->runtime_scene_mask_effect)
        gs_effect_destroy(session->runtime_scene_mask_effect);
    if (session->effect_registry)
        session->effect_registry->reset();
    lock.unlock();
    obs_leave_graphics();
    delete session;
}

void title_gpu_render_session_update_range(TitleGpuRenderSession *session,
                                           const Title &title,
                                           double time,
                                           uint64_t model_revision,
                                           std::size_t first_layer,
                                           std::size_t last_layer,
                                           bool transform_only_update)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return;
    const ScopedSourceFrameDuration temporal_scope(
        session->sample_duration_seconds);
    const uint64_t diagnostic_update_serial =
        ++session->diagnostics_update_serial;
    const double previous_session_time = session->time;
    const uint64_t previous_model_revision = session->model_revision;
    const bool previous_frame_dirty = session->frame_dirty;
    const bool was_submitted = session->use_submitted_final ||
                               session->use_gpu_cached_final;
    const bool had_base = session->use_base_frame;
    session->use_submitted_final = false;
    session->use_gpu_cached_final = false;
    session->submitted_gpu_cache_key.clear();
    session->pending_submitted_final = QImage();
    session->submitted_final_pending = false;
    session->use_base_frame = false;
    session->use_gpu_cached_base = false;
    session->base_gpu_cache_key.clear();
    session->pending_base_frame = QImage();
    session->base_frame_pending = false;

    const bool forced_compatibility_rebuild =
        session->force_compatibility_raster_rebuild;
    const bool model_changed = forced_compatibility_rebuild ||
        !session->has_title ||
        session->model_revision != model_revision ||
        session->title.id != title.id;
    session->interactive_transform_update = transform_only_update;
    if (model_changed) {
        bool updated_transform_snapshot = false;
        if (!forced_compatibility_rebuild && transform_only_update &&
            session->has_title &&
            session->title.id == title.id &&
            session->title.layers.size() == title.layers.size()) {
            updated_transform_snapshot = true;
            for (std::size_t i = 0; i < title.layers.size(); ++i) {
                const auto &source_layer = title.layers[i];
                const auto &snapshot_layer = session->title.layers[i];
                if (!source_layer || !snapshot_layer ||
                    source_layer->id != snapshot_layer->id ||
                    source_layer->type != snapshot_layer->type) {
                    updated_transform_snapshot = false;
                    break;
                }
            }
            if (updated_transform_snapshot) {
                session->title.active_camera_id = title.active_camera_id;
                session->title.active_camera = title.active_camera;
                session->title.cameras = title.cameras;
                session->title.render_camera_override_id =
                    title.render_camera_override_id;
                for (std::size_t i = 0; i < title.layers.size(); ++i) {
                    const Layer &source_layer = *title.layers[i];
                    Layer &snapshot_layer = *session->title.layers[i];
                    snapshot_layer.dimension_mode = source_layer.dimension_mode;
                    snapshot_layer.transform_axis_space = source_layer.transform_axis_space;
                    snapshot_layer.position = source_layer.position;
                    snapshot_layer.position_z = source_layer.position_z;
                    snapshot_layer.position_3d = source_layer.position_3d;
                    snapshot_layer.position_3d_path_enabled =
                        source_layer.position_3d_path_enabled;
                    snapshot_layer.scale = source_layer.scale;
                    snapshot_layer.scale_z = source_layer.scale_z;
                    snapshot_layer.rotation = source_layer.rotation;
                    snapshot_layer.rotation_x = source_layer.rotation_x;
                    snapshot_layer.rotation_y = source_layer.rotation_y;
                    snapshot_layer.anchor_z = source_layer.anchor_z;
                    snapshot_layer.orientation_x = source_layer.orientation_x;
                    snapshot_layer.orientation_y = source_layer.orientation_y;
                    snapshot_layer.orientation_z = source_layer.orientation_z;
                    snapshot_layer.camera_id = source_layer.camera_id;
                    snapshot_layer.camera_assignment = source_layer.camera_assignment;
                    snapshot_layer.depth_mode = source_layer.depth_mode;
                    snapshot_layer.depth_test = source_layer.depth_test;
                    snapshot_layer.write_to_depth = source_layer.write_to_depth;
                    snapshot_layer.double_sided = source_layer.double_sided;
                    snapshot_layer.backface_culling = source_layer.backface_culling;
                    snapshot_layer.parent_id = source_layer.parent_id;
                    snapshot_layer.transform_parent_id = source_layer.transform_parent_id;
                    /* During direct resize, present the old raster through a
                     * temporary GPU local-space scale. The settled edit will
                     * replace it with an exact new raster after mouse release. */
                    snapshot_layer.size = source_layer.size;
                    snapshot_layer.origin_prop = source_layer.origin_prop;
                    snapshot_layer.origin_x = source_layer.origin_x;
                    snapshot_layer.origin_y = source_layer.origin_y;
                    snapshot_layer.rect_width = source_layer.rect_width;
                    snapshot_layer.rect_height = source_layer.rect_height;
                    snapshot_layer.image_size = source_layer.image_size;
                    snapshot_layer.image_width = source_layer.image_width;
                    snapshot_layer.image_height = source_layer.image_height;
                }
            }
        }
        if (!updated_transform_snapshot) {
            session->title = clone_title_snapshot(title);
        }
        session->has_title = true;
        session->model_revision = model_revision;
        session->force_compatibility_raster_rebuild = false;
    }

    const std::size_t layer_count = session->title.layers.size();
    const std::size_t resolved_first = std::min(first_layer, layer_count);
    const std::size_t resolved_last = std::max(
        resolved_first, std::min(last_layer, layer_count));
    const bool range_changed = session->first_layer != resolved_first ||
                               session->last_layer != resolved_last;
    session->first_layer = resolved_first;
    session->last_layer = resolved_last;

    const double clamped_time = std::clamp(
        time, 0.0, std::max(0.0, session->title.duration));
    const bool time_changed = std::abs(session->time - clamped_time) > 1e-9;
    /* Independent Asset Layers are sampled from their own steady clock.  The
     * parent title time can remain bit-identical while paused or at a loop
     * boundary, so root-time comparison alone cannot decide that this frame is
     * clean.  Force composition on scheduler updates; unchanged base rasters
     * are still retained by the transform-neutral layer cache. */
    const bool independent_asset_clock =
        fxm::asset_runtime::title_has_independent_playback(session->title);
    session->time = clamped_time;

    FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
        "stage=session-update-begin session=%1 update=%2 title=%3 requestedTime=%4 previousTime=%5 clampedTime=%6 requestedRevision=%7 previousRevision=%8 modelChanged=%9 forcedCompatibility=%10 transformOnly=%11 range=%12..%13 previousDirty=%14 wasSubmitted=%15 hadBase=%16")
        .arg(reinterpret_cast<quintptr>(session), 0, 16)
        .arg(diagnostic_update_serial)
        .arg(QString::fromStdString(title.id))
        .arg(time, 0, 'f', 9)
        .arg(previous_session_time, 0, 'f', 9)
        .arg(clamped_time, 0, 'f', 9)
        .arg(model_revision)
        .arg(previous_model_revision)
        .arg(model_changed ? 1 : 0)
        .arg(forced_compatibility_rebuild ? 1 : 0)
        .arg(transform_only_update ? 1 : 0)
        .arg(resolved_first)
        .arg(resolved_last)
        .arg(previous_frame_dirty ? 1 : 0)
        .arg(was_submitted ? 1 : 0)
        .arg(had_base ? 1 : 0));

    /* Only rasterize the layer range that this graph will composite, plus any
     * track-matte sources it references. Parent layers are evaluated as
     * transforms and do not need their own raster texture. */
    std::unordered_set<std::string> needed_ids;
    needed_ids.reserve((resolved_last - resolved_first) * 3 + 8);
    auto add_effect_source_dependencies = [&](const Layer &owner) {
        for (const LayerEffect &effect : owner.effects) {
            if ((effect.type == LayerEffectType::LightWrap ||
                 effect.type == LayerEffectType::DisplacementMap) &&
                !effect.effect_source_layer_id.empty() &&
                effect.effect_source_layer_id != owner.id) {
                needed_ids.insert(effect.effect_source_layer_id);
            }
        }
    };
    for (std::size_t index = resolved_first; index < resolved_last; ++index) {
        const auto &layer = session->title.layers[index];
        if (!layer)
            continue;
        add_effect_source_dependencies(*layer);
        if (layer->type != LayerType::Adjustment &&
            !layer_type_is_container(layer->type))
            needed_ids.insert(gpu_session_layer_id(*layer));
        if (layer->mask_mode != MaskMode::None &&
            !layer->mask_source_id.empty())
            needed_ids.insert(layer->mask_source_id);
        if (layer_type_is_container(layer->type)) {
            std::vector<std::string> group_ids{layer->id};
            for (std::size_t cursor = 0; cursor < group_ids.size(); ++cursor) {
                for (const auto &candidate : session->title.layers) {
                    if (!candidate || candidate->parent_id != group_ids[cursor])
                        continue;
                    add_effect_source_dependencies(*candidate);
                    if (layer_type_is_container(candidate->type))
                        group_ids.push_back(candidate->id);
                    else if (candidate->type != LayerType::Adjustment)
                        needed_ids.insert(gpu_session_layer_id(*candidate));
                    if (candidate->mask_mode != MaskMode::None &&
                        !candidate->mask_source_id.empty())
                        needed_ids.insert(candidate->mask_source_id);
                }
            }
        }
    }
    /* Expand the complete render dependency graph, not just direct matte IDs.
     * A Group has no raster of its own: when it is referenced only as a track
     * matte, all descendant artwork (including nested Groups and their mattes)
     * must still be rasterized. Previously only the Group ID was added here,
     * so render_gpu_group_graph_texture() found no child textures and produced
     * an empty matte. */
    bool dependency_added = true;
    while (dependency_added) {
        dependency_added = false;
        std::vector<std::string> snapshot(needed_ids.begin(), needed_ids.end());
        for (const std::string &id : snapshot) {
            const Layer *dependency = find_layer_by_id(session->title, id);
            if (!dependency)
                continue;

            add_effect_source_dependencies(*dependency);
            if (layer_type_is_container(dependency->type)) {
                for (const auto &candidate : session->title.layers) {
                    if (!candidate || candidate->parent_id != dependency->id)
                        continue;
                    add_effect_source_dependencies(*candidate);
                    const std::string child_id = layer_type_is_container(candidate->type)
                        ? candidate->id : gpu_session_layer_id(*candidate);
                    if (candidate->type != LayerType::Adjustment)
                        dependency_added = needed_ids.insert(child_id).second ||
                                           dependency_added;
                    if (candidate->mask_mode != MaskMode::None &&
                        !candidate->mask_source_id.empty())
                        dependency_added = needed_ids.insert(
                            candidate->mask_source_id).second || dependency_added;
                }
            }

            if (dependency->mask_mode != MaskMode::None &&
                !dependency->mask_source_id.empty())
                dependency_added = needed_ids.insert(
                    dependency->mask_source_id).second || dependency_added;
        }
    }

    bool raster_changed = false;
    std::unordered_set<std::string> live_ids;
    live_ids.reserve(needed_ids.size());
    const qint64 clock_second = QDateTime::currentSecsSinceEpoch();

    for (const auto &layer_ptr : session->title.layers) {
        if (!layer_ptr)
            continue;
        const Layer &source_layer = *layer_ptr;
        Layer runtime_layer;
        const Layer *runtime_layer_ptr = &source_layer;
        const bool has_text_transition_descriptor = std::any_of(
            source_layer.transitions.begin(), source_layer.transitions.end(),
            [](const LayerTransition &transition) {
                return layer_transition_type_is_text(transition.type);
            });
        if (has_text_transition_descriptor &&
            (source_layer.type == LayerType::Text ||
             source_layer.type == LayerType::Clock ||
             source_layer.type == LayerType::Ticker)) {
            runtime_layer = source_layer;
            runtime_layer.text_animators =
                resolved_text_transition_animator_stack(
                    source_layer.transitions, source_layer.text_animators,
                    source_layer.in_time, source_layer.out_time);
            runtime_layer_ptr = &runtime_layer;
        }
        const Layer &layer = *runtime_layer_ptr;
        const std::string id = gpu_session_layer_id(layer);
        if (needed_ids.find(id) == needed_ids.end())
            continue;
        live_ids.insert(id);
        const double layer_time = gpu_layer_render_time(
            session->title, layer, session->time);
        Layer base_key_layer = layer;
        /* Pixel effects are evaluated after the reusable base raster and must
         * not invalidate that raster. Trim Paths is different: it changes the
         * stroke path geometry before rasterization, so both its current values
         * and its animation state must remain in the base-raster identity.
         * Removing it here caused editor changes to reuse the previous stroke
         * texture until another unrelated property changed the raster key. */
        base_key_layer.effects.erase(
            std::remove_if(
                base_key_layer.effects.begin(), base_key_layer.effects.end(),
                [](const LayerEffect &effect) {
                    return effect.type != LayerEffectType::TrimPaths;
                }),
            base_key_layer.effects.end());
        const double base_local_time = std::max(0.0, layer_time - layer.in_time);
        const bool preserve_effect_surface =
            layer_requires_preserved_effect_surface(layer, base_local_time);
        QRect base_surface_rect =
            (layer.type == LayerType::Text || layer.type == LayerType::Clock)
                ? gpu_text_surface_rect(session->title, layer, base_local_time)
                : clipped_effect_surface_rect(
                      layer, base_local_time,
                      std::max(1, session->title.width),
                      std::max(1, session->title.height));
        const int transition_blur_padding =
            general_transition_blur_padding(layer);
        const bool direct_image = layer_can_use_direct_gpu_image_raster(
            layer, layer_time, transition_blur_padding);
        if (transition_blur_padding > 0) {
            base_surface_rect = base_surface_rect.adjusted(
                -transition_blur_padding, -transition_blur_padding,
                transition_blur_padding, transition_blur_padding);
        }
        std::string key = effect_layer_cache_key(
            nullptr, session->title, base_key_layer, layer_time,
            std::max(1, session->title.width),
            std::max(1, session->title.height), false, true) +
            "|gpu-render-scale=" + std::to_string(gpu_session_raster_scale(session)) +
            "|gpu-base-surface=" + std::to_string(base_surface_rect.x()) + ',' +
            std::to_string(base_surface_rect.y()) + ',' +
            std::to_string(base_surface_rect.width()) + 'x' +
            std::to_string(base_surface_rect.height()) +
            "|gpu-effect-surface=" +
            (preserve_effect_surface ? std::string("preserved")
                                     : std::string("cropped")) +
            "|gpu-direct-image=" +
            (direct_image ? std::string("1") : std::string("0"));
        if (direct_image) {
            /* Direct bitmap/video layers keep their source texture resident
             * while the user scales the image box.  Filtering is now applied
             * by the GPU layer-copy shader, so box geometry and filtering mode
             * must not force a CPU raster regeneration or a texture upload. */
            if (layer.type == LayerType::Image) {
                const std::string effective_image_path = effective_external_string(
                    layer, ExternalPropertyPaths::ImagePath, layer.image_path);
                QFileInfo info(QString::fromStdString(effective_image_path));
                key = std::string("direct-image|src=") + effective_image_path +
                    "|mtime=" + std::to_string(info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0) +
                    "|size=" + std::to_string(info.exists() ? info.size() : -1) +
                    "|svg=" + (image_path_is_svg(QString::fromStdString(effective_image_path)) ? "1" : "0") +
                    "|render-scale=" + std::to_string(gpu_session_raster_scale(session));
            } else if (layer.type == LayerType::Video) {
                key = std::string("direct-video|src=") + layer.video_source +
                    "|stream=" + std::to_string(layer.video_stream_index) +
                    "|astream=" + std::to_string(layer.video_audio_stream_index) +
                    "|streams=" + layer.video_selected_streams_json +
                    "|render-scale=" + std::to_string(gpu_session_raster_scale(session));
            }
        }
        if (layer.type == LayerType::Clock)
            key += "|clock=" + std::to_string(clock_second);
        else if (layer.type == LayerType::Ticker)
            key += "|ticker=" + std::to_string(QDateTime::currentMSecsSinceEpoch());
        else if (layer.type == LayerType::Chart) {
            key += "|chart-data=" + fxm::chart::data_fingerprint(layer);
            if (fxm::chart::transition_active(layer))
                key += "|chart-transition=" + std::to_string(
                    QDateTime::currentMSecsSinceEpoch() / 16);
        }
        if (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
            layer.type == LayerType::Ticker)
            key += "|gpu-text-pipeline=307";
        if (layer.type == LayerType::Video) {
            key += "|" + fxm::video::FrameRuntime::instance()
                .frame_cache_key_for_layer(
                    layer, layer_time, 1.0 / std::max(1e-6, source_frame_duration()));
        }
        if (layer.type == LayerType::TransitionInput)
            key += session->transition_input_preview_enabled
                ? "|transition-input-preview=1"
                : "|transition-input-preview=0";
        /* Non-scaling outlines are rasterized at the inverse of the current
         * local transform scale. Scale changes therefore change the artwork
         * identity even though fill-only layers remain transform-neutral. */
        if (layer.outline_enabled && !layer.scale_stroke_with_shape &&
            (layer_type_uses_shape_geometry(layer.type) ||
             layer_type_is_image_like(layer.type)) &&
            layer.stroke_fill_type != 0) {
            const auto local_scale = layer.scale.evaluate(base_local_time);
            key += "|non-scaling-outline=" +
                std::to_string(local_scale.x) + ',' +
                std::to_string(local_scale.y);
        }
        if (layer.use_as_scene_mask)
            key += session->scene_mask_placeholder_preview_enabled
                ? "|scene-mask-placeholder-preview=1"
                : "|scene-mask-placeholder-preview=0";

        auto &entry = session->layers[id];
        /* Moving/rotating a layer or one of its parents changes only the GPU
         * world matrix. Keep the existing transform-neutral raster and avoid
         * synchronous text/vector/image regeneration on the UI thread. */
        const double current_box_width = std::max(
            0.0001, eval_box_width(layer, layer_time));
        const double current_box_height = std::max(
            0.0001, eval_box_height(layer, layer_time));
        const bool local_geometry_changed =
            std::abs(current_box_width - entry.base_box_width) > 0.0001 ||
            std::abs(current_box_height - entry.base_box_height) > 0.0001;

        if (layer.type == LayerType::TransitionInput &&
            !session->transition_input_preview_enabled) {
            /* Runtime A/B inputs are shape-like boxes whose pixels arrive from
             * obs_transition_video_render(). Do not manufacture a CPU
             * placeholder raster or inherit its cropped geometry. Keeping the
             * exact current box metadata makes Scene A and Scene B map 1:1 to
             * their authored Size at every frame. */
            entry.key = std::move(key);
            entry.gpu_text = false;
            entry.gpu_primitive = false;
            entry.pending_image = QImage();
            entry.pending_upload = false;
            entry.base_box_width = current_box_width;
            entry.base_box_height = current_box_height;
            entry.logical_width = current_box_width;
            entry.logical_height = current_box_height;
            entry.origin = QPointF(
                -eval_origin_x(layer, base_local_time) * current_box_width,
                -eval_origin_y(layer, base_local_time) * current_box_height);
            entry.layer_box_rect = QRectF(0.0, 0.0,
                                          current_box_width, current_box_height);
            entry.image_clip_rect = QRectF();
            raster_changed = raster_changed || local_geometry_changed;
            continue;
        }
        /* During interactive resize, direct image layers keep their decoded
         * texture resident and update only logical box geometry. This works for
         * Stretch/Fit/Fill because layout is metadata; no bitmap rescale, SVG
         * reraster or texture upload is required until the settled edit. */
        if (direct_image &&
            (entry.texture || entry.pending_upload || !entry.pending_image.isNull()) &&
            !entry.key.empty() && entry.key == key) {
            QPointF updated_origin;
            double updated_width = 0.0;
            double updated_height = 0.0;
            if (direct_gpu_image_geometry(layer, layer_time, updated_origin,
                                          updated_width, updated_height)) {
                entry.origin = updated_origin;
                entry.logical_width = updated_width;
                entry.logical_height = updated_height;
                const double local_t = std::max(0.0, layer_time - layer.in_time);
                entry.layer_box_rect = QRectF(
                    -eval_origin_x(layer, local_t) * current_box_width - updated_origin.x(),
                    -eval_origin_y(layer, local_t) * current_box_height - updated_origin.y(),
                    current_box_width, current_box_height);
                const QRectF source_rect(0.0, 0.0, updated_width, updated_height);
                entry.image_clip_rect = layer.image_crop_when_outside_box
                    ? source_rect.intersected(entry.layer_box_rect)
                    : source_rect;
                entry.base_box_width = current_box_width;
                entry.base_box_height = current_box_height;
                raster_changed = raster_changed || local_geometry_changed;
                continue;
            }
        }
        if (transform_only_update && !local_geometry_changed &&
            (entry.texture || entry.pending_upload || !entry.pending_image.isNull()) &&
            !entry.key.empty() && entry.key == key)
            continue;
        const bool dynamic_raster = layer.type != LayerType::TransitionInput &&
            (layer.type == LayerType::Clock ||
             layer.type == LayerType::Ticker ||
             layer.type == LayerType::Video ||
             layer.type == LayerType::Chart ||
             layer_has_non_transform_animation(base_key_layer) ||
             text_animator_stack_is_time_dependent(layer.text_animators));
        const bool entry_has_resolvable_raster =
            entry.texture || !entry.pending_image.isNull() ||
            entry.gpu_text || entry.gpu_primitive;
        if (!model_changed && !dynamic_raster && !local_geometry_changed &&
            !entry.key.empty() && entry.key == key &&
            entry_has_resolvable_raster)
            continue;
        if (entry.key == key && entry_has_resolvable_raster)
            continue;
        /* Phase 12C consumes the immutable Phase 12B glyph layout first.
         * Text Animator transitions use the same per-glyph GPU path. Only an
         * actually unsupported font/glyph or backend failure uses the
         * compatibility raster. */
        const bool scene_mask_placeholder_layer =
            session->scene_mask_placeholder_preview_enabled && layer.use_as_scene_mask;
        const bool gpu_text_candidate = !scene_mask_placeholder_layer &&
            layer_can_use_gpu_text_raster(
                layer, layer_time, !session->text_backend_unavailable);
        const bool gpu_primitive = !gpu_text_candidate &&
            !scene_mask_placeholder_layer &&
            layer_can_use_gpu_primitive_raster(
                layer, layer_time, !session->primitive_backend_unavailable);
        entry.key = key;
        entry.base_box_width = current_box_width;
        entry.base_box_height = current_box_height;

        bool gpu_text_prepared = false;
        if (gpu_text_candidate) {
            std::string text_failure;
            gpu_text_prepared = prepare_gpu_text_raster(
                session, layer, layer_time, base_surface_rect, entry,
                &text_failure);
            if (!gpu_text_prepared && !text_failure.empty() &&
                TitlePreferences::cache_playback_logging_enabled()) {
                FXM_LOG_WARNING("GpuText", QStringLiteral(
                    "stage=phase12c-fallback layer=%1 reason=%2")
                    .arg(QString::fromStdString(layer.id),
                         QString::fromStdString(text_failure)));
            }
        }

        if (gpu_text_prepared) {
            /* Geometry/material batches are now pending in entry.text_layer. */
        } else if (gpu_primitive) {
            const ShapeType primitive_type =
                (layer.type == LayerType::SolidRect || layer.type == LayerType::ColorSolid)
                    ? ShapeType::RoundedRectangle : layer.shape_type;
            const bool has_stroke = layer_has_visible_outline(layer, base_local_time);
            const double padding = gpu_primitive_padding(
                layer, base_local_time);
            entry.gpu_text = false;
            entry.gpu_text_fallback_image = QImage();
            entry.gpu_primitive = true;
            entry.pending_image = QImage();
            entry.primitive_shape_type = static_cast<int>(primitive_type);
            entry.primitive_vertex_count = primitive_type == ShapeType::Star
                ? std::clamp(layer.shape_points, 2, 64)
                : std::clamp(layer.shape_sides, 3, 64);
            entry.primitive_inner_radius = std::clamp(
                layer.shape_inner_radius * 2.0f, 0.0f, 1.0f);
            entry.primitive_outer_radius = std::clamp(
                layer.shape_outer_radius * 2.0f, 0.0001f, 1.0f);
            entry.primitive_stroke_width = has_stroke
                ? static_cast<float>(eval_outline_width(layer, base_local_time))
                : 0.0f;
            entry.primitive_stroke_alignment = eval_outline_alignment(
                layer, base_local_time);
            entry.primitive_stroke_on_front = eval_outline_on_front(
                layer, base_local_time);
            entry.primitive_shape_width = current_box_width;
            entry.primitive_shape_height = current_box_height;
            entry.primitive_padding = padding;
            entry.primitive_fill_color = eval_fill_color(layer, base_local_time);
            entry.primitive_stroke_color = has_stroke
                ? argb_with_multiplied_alpha(
                      eval_outline_color(layer, base_local_time),
                      eval_outline_opacity(layer, base_local_time))
                : 0x00000000u;
            if (primitive_type == ShapeType::Rectangle ||
                primitive_type == ShapeType::RoundedRectangle) {
                entry.primitive_corner_radii = {
                    std::max(0.0f, layer.corner_radius_tl),
                    std::max(0.0f, layer.corner_radius_tr),
                    std::max(0.0f, layer.corner_radius_br),
                    std::max(0.0f, layer.corner_radius_bl)};
            } else {
                entry.primitive_corner_radii = {0.0f, 0.0f, 0.0f, 0.0f};
            }
            entry.logical_width = current_box_width + padding * 2.0;
            entry.logical_height = current_box_height + padding * 2.0;
            entry.origin = QPointF(
                -eval_origin_x(layer, base_local_time) * current_box_width - padding,
                -eval_origin_y(layer, base_local_time) * current_box_height - padding);
            entry.layer_box_rect = QRectF(
                padding, padding, current_box_width, current_box_height);
            entry.image_clip_rect = QRectF();
        } else {
            const double raster_scale = gpu_session_raster_scale(session);
            const MotionBaseRaster raster = render_gpu_layer_base_raster(
                session->title, layer, layer_time, raster_scale,
                session->scene_mask_placeholder_preview_enabled,
                session->video_decode_client);
            entry.gpu_text = false;
            entry.gpu_text_fallback_image = QImage();
            entry.gpu_primitive = false;

            if (layer.type == LayerType::Video) {
                /* Development Version 247: Video decode is asynchronous. Do not
                 * cache an empty raster, and do not store a stale decoded frame
                 * under the requested-frame key.  Older builds could publish an
                 * empty texture when the layer was first added, then consider the
                 * requested key satisfied until the playhead/transform changed.
                 * During playback they could also cache the previous decoded
                 * frame as if it were the current request, producing visibly
                 * stepped video even when the editor FPS counter stayed healthy. */
                if (raster.image.isNull()) {
                    entry.key.clear();
                    if (!entry.texture && !entry.pending_upload && entry.pending_image.isNull()) {
                        entry.origin = QPointF(
                            -eval_origin_x(layer, base_local_time) * current_box_width,
                            -eval_origin_y(layer, base_local_time) * current_box_height);
                        entry.logical_width = current_box_width;
                        entry.logical_height = current_box_height;
                        entry.layer_box_rect = QRectF(0.0, 0.0,
                                                      current_box_width, current_box_height);
                        entry.image_clip_rect = QRectF();
                    }
                    continue;
                }
                entry.key = key + "|video-delivered-frame=" +
                    std::to_string(raster.video_frame_number) +
                    "|video-requested-frame=" +
                    std::to_string(raster.video_requested_frame_number) +
                    "|video-exact=" +
                    (raster.video_exact_frame ? std::string("1") : std::string("0"));
                if (raster.video_loading_progress_percent >= 0)
                    entry.key += "|video-loading-progress=" +
                        std::to_string(raster.video_loading_progress_percent);
            }

            entry.pending_image = raster.image;
            entry.origin = raster.origin;
            entry.logical_width = raster.logical_width > 0.0
                ? raster.logical_width : raster.image.width();
            entry.logical_height = raster.logical_height > 0.0
                ? raster.logical_height : raster.image.height();
            entry.layer_box_rect = raster.layer_box_rect;
            entry.image_clip_rect = raster.image_clip_rect;
        }
        entry.pending_upload = true;
        raster_changed = true;
    }

    for (auto it = session->layers.begin(); it != session->layers.end();) {
        if (is_gpu_scene_mask_raster_id(it->first) ||
            live_ids.find(it->first) != live_ids.end()) {
            ++it;
            continue;
        }
        if (it->second.texture || it->second.text_layer) {
            /* GPU destruction must occur on the graphics thread. */
            it->second.pending_image = QImage();
            it->second.gpu_text = false;
            it->second.gpu_primitive = false;
            it->second.pending_upload = true;
            it->second.key.clear();
            ++it;
        } else {
            if (it->second.transform_grid)
                gs_vertexbuffer_destroy(it->second.transform_grid);
            it = session->layers.erase(it);
        }
        raster_changed = true;
    }

    session->frame_dirty = session->frame_dirty || was_submitted || had_base ||
                           model_changed || range_changed || time_changed ||
                           raster_changed || independent_asset_clock;

    std::size_t pending_rasters = 0;
    std::size_t textured_rasters = 0;
    std::size_t gpu_text_rasters = 0;
    std::size_t gpu_primitive_rasters = 0;
    std::size_t persistence_layers = 0;
    std::size_t persistence_asset_layers = 0;
    for (const auto &pair : session->layers) {
        const auto &entry = pair.second;
        if (entry.pending_upload)
            ++pending_rasters;
        if (entry.texture)
            ++textured_rasters;
        if (entry.gpu_text)
            ++gpu_text_rasters;
        if (entry.gpu_primitive)
            ++gpu_primitive_rasters;
    }
    for (const auto &layer : session->title.layers) {
        if (!layer || !gpu_layer_uses_cue_persistence(session->title, *layer))
            continue;
        ++persistence_layers;
        if (!layer->asset_owner_id.empty())
            ++persistence_asset_layers;
    }
    std::size_t extrusion_layers = 0;
    std::size_t visible_lights = 0;
    for (const auto &layer : session->title.layers) {
        if (!layer)
            continue;
        if (layer->type == LayerType::Light && layer->visible)
            ++visible_lights;
    }
    FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
        "stage=session-update-end session=%1 update=%2 title=%3 time=%4 revision=%5 timeChanged=%6 rangeChanged=%7 rasterChanged=%8 dirty=%9 needed=%10 live=%11 rasters=%12 textured=%13 pending=%14 gpuText=%15 gpuPrimitive=%16 extrusionLayers=%17 lights=%18 persistenceLayers=%19 persistenceAssetLayers=%20")
        .arg(reinterpret_cast<quintptr>(session), 0, 16)
        .arg(diagnostic_update_serial)
        .arg(QString::fromStdString(session->title.id))
        .arg(session->time, 0, 'f', 9)
        .arg(session->model_revision)
        .arg(time_changed ? 1 : 0)
        .arg(range_changed ? 1 : 0)
        .arg(raster_changed ? 1 : 0)
        .arg(session->frame_dirty ? 1 : 0)
        .arg(needed_ids.size())
        .arg(live_ids.size())
        .arg(session->layers.size())
        .arg(textured_rasters)
        .arg(pending_rasters)
        .arg(gpu_text_rasters)
        .arg(gpu_primitive_rasters)
        .arg(extrusion_layers)
        .arg(visible_lights)
        .arg(persistence_layers)
        .arg(persistence_asset_layers));
}

void title_gpu_render_session_set_render_request(
    TitleGpuRenderSession *session, const TitleGpuRenderRequest &request)
{
    if (!session || session->destroying.load())
        return;
    const uint32_t output_width = request.output_width == 0
        ? 0 : clamped_source_dimension(request.output_width);
    const uint32_t output_height = request.output_height == 0
        ? 0 : clamped_source_dimension(request.output_height);
    const double sample_duration =
        std::isfinite(request.sample_duration_seconds) &&
            request.sample_duration_seconds > 0.0
        ? std::clamp(request.sample_duration_seconds, 1.0 / 1000.0, 1.0)
        : 1.0 / 60.0;

    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        (session->requested_output_width == output_width &&
         session->requested_output_height == output_height &&
         std::abs(session->sample_duration_seconds - sample_duration) < 1e-12))
        return;

    session->requested_output_width = output_width;
    session->requested_output_height = output_height;
    session->sample_duration_seconds = sample_duration;
    session->frame_dirty = true;
    for (auto &pair : session->layers) {
        pair.second.effect_cache_key.clear();
        pair.second.key.clear();
    }
}

void title_gpu_render_session_set_preview_quality(TitleGpuRenderSession *session,
                                                   double scale, bool editor_draft)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return;
    const float resolved_scale = static_cast<float>(std::clamp(scale, 0.25, 1.0));
    if (std::abs(session->preview_quality_scale - resolved_scale) > 0.0001f ||
        session->editor_draft != editor_draft) {
        session->preview_quality_scale = resolved_scale;
        session->editor_draft = editor_draft;
        session->frame_dirty = true;
        for (auto &pair : session->layers) {
            pair.second.effect_cache_key.clear();
            pair.second.key.clear();
        }
    }
}

void title_gpu_render_session_set_realtime_output(
    TitleGpuRenderSession *session, bool realtime_output)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        session->realtime_output.load(std::memory_order_relaxed) ==
            realtime_output)
        return;
    session->realtime_output.store(
        realtime_output, std::memory_order_relaxed);
    session->frame_dirty = true;
    for (auto &pair : session->layers) {
        pair.second.effect_cache_key.clear();
        pair.second.key.clear();
    }
}

bool title_gpu_render_session_is_realtime_output(
    const TitleGpuRenderSession *session)
{
    return session && !session->destroying.load(std::memory_order_relaxed) &&
        session->realtime_output.load(std::memory_order_relaxed);
}

void title_gpu_render_session_set_editor_video_decode_client(
    TitleGpuRenderSession *session, bool editor_client)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load())
        return;
    const fxm::video::VideoDecodeClient next = editor_client
        ? fxm::video::VideoDecodeClient::Editor
        : fxm::video::VideoDecodeClient::LiveOutput;
    if (session->video_decode_client == next)
        return;
    session->video_decode_client = next;
    session->frame_dirty = true;
    for (auto &pair : session->layers) {
        pair.second.effect_cache_key.clear();
        pair.second.key.clear();
    }
}

void title_gpu_render_session_set_transition_input_preview(
    TitleGpuRenderSession *session, bool enabled)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        session->transition_input_preview_enabled == enabled)
        return;
    session->transition_input_preview_enabled = enabled;
    session->frame_dirty = true;
}


void title_gpu_render_session_set_scene_mask_placeholder_preview(
    TitleGpuRenderSession *session, bool enabled)
{
    if (!session || session->destroying.load())
        return;
    std::lock_guard<std::mutex> lock(session->mutex);
    if (session->destroying.load() ||
        session->scene_mask_placeholder_preview_enabled == enabled)
        return;
    session->scene_mask_placeholder_preview_enabled = enabled;
    session->frame_dirty = true;
    for (auto &pair : session->layers) {
        if (pair.second.key.find("|scene-mask-placeholder-preview=") != std::string::npos)
            pair.second.key.clear();
    }
}

void title_gpu_render_session_update(TitleGpuRenderSession *session,
                                     const Title &title,
                                     double time,
                                     uint64_t model_revision,
                                     bool transform_only_update)
{
    title_gpu_render_session_update_range(session, title, time, model_revision,
                                          0, title.layers.size(),
                                          transform_only_update);
}

bool title_gpu_frame_cache_contains(const std::string &cache_key)
{
    if (cache_key.empty())
        return false;
    std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
    return g_gpu_frame_cache.find(cache_key) != g_gpu_frame_cache.end();
}

bool title_gpu_frame_cache_alias(
    const std::string &cache_key,
    const std::string &canonical_cache_key)
{
    if (cache_key.empty() || canonical_cache_key.empty())
        return false;
    obs_enter_graphics();
    bool aliased = false;
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        aliased = alias_global_gpu_frame_locked(
            cache_key, canonical_cache_key);
    }
    obs_leave_graphics();
    return aliased;
}

bool title_gpu_frame_cache_store_image(
    const std::string &cache_key, const QImage &sparse_image,
    uint32_t canvas_width, uint32_t canvas_height)
{
    if (cache_key.empty() || sparse_image.isNull() ||
        canvas_width == 0 || canvas_height == 0)
        return false;

    fxm::cache_frame_payload::Placement placement;
    if (!fxm::cache_frame_payload::read_placement(sparse_image, placement)) {
        if (sparse_image.width() != static_cast<int>(canvas_width) ||
            sparse_image.height() != static_cast<int>(canvas_height))
            return false;
        placement.x = 0;
        placement.y = 0;
        placement.canvas_width = static_cast<int>(canvas_width);
        placement.canvas_height = static_cast<int>(canvas_height);
    }
    if (placement.canvas_width != static_cast<int>(canvas_width) ||
        placement.canvas_height != static_cast<int>(canvas_height))
        return false;

    /* Alpha scanning and hashing are CPU work. Complete them before taking the
     * OBS graphics context or the global cache mutex, otherwise a large frame
     * can stall live/editor presentation while its tiles are being analyzed. */
    const QVector<fxm::cache_tile_payload::Tile> extracted =
        fxm::cache_tile_payload::extract_nonempty_tiles(
            sparse_image, placement, kGpuRamTileSize);

    obs_enter_graphics();
    bool stored = false;
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        stored = store_global_gpu_frame_tiles_locked(
            cache_key, extracted, canvas_width, canvas_height);
    }
    obs_leave_graphics();
    return stored;
}

void title_gpu_frame_cache_remove(const std::string &cache_key)
{
    if (cache_key.empty())
        return;
    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        auto found = g_gpu_frame_cache.find(cache_key);
        if (found != g_gpu_frame_cache.end()) {
            release_gpu_ram_frame_locked(found->second);
            g_gpu_frame_cache.erase(found);
        }
    }
    obs_leave_graphics();
}

void title_gpu_frame_cache_remove_title(const std::string &title_id)
{
    if (title_id.empty())
        return;
    const std::string prefix = title_id + "-";
    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        for (auto it = g_gpu_frame_cache.begin();
             it != g_gpu_frame_cache.end();) {
            if (it->first.compare(0, prefix.size(), prefix) != 0) {
                ++it;
                continue;
            }
            release_gpu_ram_frame_locked(it->second);
            it = g_gpu_frame_cache.erase(it);
        }
    }
    obs_leave_graphics();
}

void title_gpu_frame_cache_clear()
{
    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        for (auto &entry : g_gpu_frame_cache)
            release_gpu_ram_frame_locked(entry.second);
        g_gpu_frame_cache.clear();
        /* Defensive cleanup for a failed/aborted insertion. Normally every
         * shared tile reaches zero references through frame release. */
        g_gpu_ram_tiles.clear();
        g_gpu_frame_cache_bytes = 0;
    }
    obs_leave_graphics();
}

void title_gpu_frame_cache_set_budget(uint64_t bytes)
{
    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
        g_gpu_frame_cache_budget = std::max<uint64_t>(
            16ull * 1024ull * 1024ull, bytes);
        evict_global_gpu_frame_cache_locked();
    }
    obs_leave_graphics();
}

uint64_t title_gpu_frame_cache_bytes_used()
{
    std::lock_guard<std::mutex> lock(g_gpu_frame_cache_mutex);
    return g_gpu_frame_cache_bytes;
}

bool title_gpu_render_session_submit_gpu_cached_frame(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, uint64_t model_revision)
{
    if (!session || session->destroying.load() || cache_key.empty() ||
        !title_gpu_frame_cache_contains(cache_key) ||
        title_has_3d_compositing(title))
        return false;

    std::lock_guard<std::mutex> lock(session->mutex);
    const bool model_changed = !session->has_title ||
        session->model_revision != model_revision ||
        session->title.id != title.id;
    if (model_changed) {
        session->title = clone_title_snapshot(title);
        session->has_title = true;
        session->model_revision = model_revision;
    }

    session->use_submitted_final = false;
    session->pending_submitted_final = QImage();
    session->submitted_final_pending = false;
    session->use_gpu_cached_final = true;
    session->submitted_gpu_cache_key = cache_key;
    session->use_base_frame = false;
    session->use_gpu_cached_base = false;
    session->base_gpu_cache_key.clear();
    session->pending_base_frame = QImage();
    session->base_frame_pending = false;
    session->first_layer = 0;
    session->last_layer = 0;
    session->frame_dirty = true;
    return true;
}

bool title_gpu_render_session_submit_gpu_cached_prefix(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, double time,
    std::size_t first_dynamic_layer, uint64_t model_revision)
{
    if (!session || session->destroying.load() || cache_key.empty() ||
        !title_gpu_frame_cache_contains(cache_key))
        return false;

    struct PrefixSubmissionTransaction {
        TitleGpuRenderSession *session = nullptr;
        explicit PrefixSubmissionTransaction(TitleGpuRenderSession *value)
            : session(value)
        {
            session->state_transaction_pending.store(true,
                                                     std::memory_order_release);
        }
        ~PrefixSubmissionTransaction()
        {
            session->state_transaction_pending.store(false,
                                                     std::memory_order_release);
        }
    } transaction(session);

    title_gpu_render_session_update_range(
        session, title, time, model_revision, first_dynamic_layer,
        title.layers.size());

    std::lock_guard<std::mutex> lock(session->mutex);
    session->use_submitted_final = false;
    session->use_gpu_cached_final = false;
    session->submitted_gpu_cache_key.clear();
    session->pending_submitted_final = QImage();
    session->submitted_final_pending = false;
    session->use_base_frame = true;
    session->use_gpu_cached_base = true;
    session->base_gpu_cache_key = cache_key;
    session->pending_base_frame = QImage();
    session->base_frame_pending = false;
    session->frame_dirty = true;
    return true;
}

bool title_gpu_render_session_submit_final_frame(TitleGpuRenderSession *session,
                                                 const Title &title,
                                                 const QImage &image,
                                                 uint64_t model_revision)
{
    if (!session || session->destroying.load() || image.isNull())
        return false;

    QRect destination;
    if (!gpu_cached_image_rect(image, title, destination)) {
        if (TitlePreferences::cache_playback_logging_enabled()) {
            FXM_LOG_WARNING("CachePlayback", QStringLiteral(
                "stage=gpu-reject-final title=%1 image=%2x%3 canvas=%4x%5 reason=invalid-placement")
                .arg(QString::fromStdString(title.id))
                .arg(image.width()).arg(image.height())
                .arg(std::max(1, title.width)).arg(std::max(1, title.height)));
        }
        return false;
    }

    /* QImage::cacheKey() is only an implicit-sharing identifier, not a
     * durable content identity. Reusing GPU textures by that value could show
     * a stale/invalid texture on later playback loops. Keep a session-owned
     * upload for correctness until cache frames carry an explicit immutable
     * frame identity. */
    const qint64 image_key = 0;
    std::lock_guard<std::mutex> lock(session->mutex);
    const bool model_changed = !session->has_title ||
        session->model_revision != model_revision || session->title.id != title.id;
    if (model_changed) {
        session->title = clone_title_snapshot(title);
        session->has_title = true;
        session->model_revision = model_revision;
    }
    if (image_key != 0 && session->use_submitted_final &&
        session->submitted_final_image_key == image_key &&
        session->submitted_final_rect == destination && !model_changed)
        return true;

    session->use_base_frame = false;
    session->use_gpu_cached_base = false;
    session->base_gpu_cache_key.clear();
    session->pending_base_frame = QImage();
    session->base_frame_pending = false;
    session->use_gpu_cached_final = false;
    session->submitted_gpu_cache_key.clear();
    session->use_submitted_final = true;
    session->pending_submitted_final = image;
    session->submitted_final_rect = destination;
    session->submitted_final_image_key = image_key;
    session->submitted_final_pending = true;
    const uint64_t submission_serial = ++session->submitted_final_serial;
    if (TitlePreferences::cache_playback_logging_enabled()) {
        FXM_LOG_INFO("CachePlayback", QStringLiteral(
            "stage=gpu-submit-final serial=%1 title=%2 modelRevision=%3 imageCacheKey=%4 size=%5x%6 rect=%7,%8,%9x%10")
            .arg(submission_serial)
            .arg(QString::fromStdString(title.id))
            .arg(model_revision)
            .arg(image.cacheKey())
            .arg(image.width()).arg(image.height())
            .arg(destination.x()).arg(destination.y())
            .arg(destination.width()).arg(destination.height()));
    }
    session->first_layer = 0;
    session->last_layer = 0;
    session->frame_dirty = true;
    return true;
}

bool title_gpu_render_session_submit_cached_prefix(
    TitleGpuRenderSession *session, const Title &title,
    const QImage &cached_prefix, double time,
    std::size_t first_dynamic_layer, uint64_t model_revision)
{
    if (!session || session->destroying.load() || cached_prefix.isNull() ||
        title_has_3d_compositing(title))
        return false;

    QRect destination;
    if (!gpu_cached_image_rect(cached_prefix, title, destination)) {
        if (TitlePreferences::cache_playback_logging_enabled()) {
            FXM_LOG_WARNING("CachePlayback", QStringLiteral(
                "stage=gpu-reject-prefix title=%1 image=%2x%3 canvas=%4x%5 reason=invalid-placement")
                .arg(QString::fromStdString(title.id))
                .arg(cached_prefix.width()).arg(cached_prefix.height())
                .arg(std::max(1, title.width)).arg(std::max(1, title.height)));
        }
        return false;
    }

    struct PrefixSubmissionTransaction {
        TitleGpuRenderSession *session = nullptr;
        explicit PrefixSubmissionTransaction(TitleGpuRenderSession *value)
            : session(value)
        {
            session->state_transaction_pending.store(true,
                                                     std::memory_order_release);
        }
        ~PrefixSubmissionTransaction()
        {
            session->state_transaction_pending.store(false,
                                                     std::memory_order_release);
        }
    } transaction(session);
