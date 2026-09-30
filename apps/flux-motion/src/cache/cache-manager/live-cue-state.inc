    double left = 8.0 + animated_scalar_max_nonnegative(
        layer.stroke_width_prop, layer.stroke_width);
    double right = left;
    double top = left;
    double bottom = left;
    const auto stroke_offset_extents = animated_scalar_extents(layer.stroke_offset_prop);
    const double stroke_offset_extent = std::max({
        std::abs(static_cast<double>(layer.stroke_offset)),
        std::abs(stroke_offset_extents.first),
        std::abs(stroke_offset_extents.second)});
    left += stroke_offset_extent;
    right += stroke_offset_extent;
    top += stroke_offset_extent;
    bottom += stroke_offset_extent;
    const bool background_may_be_enabled = layer.background_enabled ||
        layer.background_enabled_prop.is_animated();
    if (background_may_be_enabled) {
        const double padding_x = animated_scalar_max_nonnegative(
            layer.background_padding_x_prop, layer.background_padding_x);
        const double padding_y = animated_scalar_max_nonnegative(
            layer.background_padding_y_prop, layer.background_padding_y);
        left += std::max(padding_x, animated_scalar_max_nonnegative(
            layer.background_padding_left_prop,
            layer.background_padding_left));
        right += std::max(padding_x, animated_scalar_max_nonnegative(
            layer.background_padding_right_prop,
            layer.background_padding_right));
        top += std::max(padding_y, animated_scalar_max_nonnegative(
            layer.background_padding_top_prop,
            layer.background_padding_top));
        bottom += std::max(padding_y, animated_scalar_max_nonnegative(
            layer.background_padding_bottom_prop,
            layer.background_padding_bottom));
        const double stroke = animated_scalar_max_nonnegative(
            layer.background_stroke_width_prop,
            layer.background_stroke_width);
        left += stroke;
        right += stroke;
        top += stroke;
        bottom += stroke;
    }

    for (const auto &effect : layer.effects) {
        if (!effect.enabled && !effect.enabled_prop.is_animated())
            continue;
        const double effect_size = animated_scalar_max_nonnegative(
            effect.size_prop, effect.effect_size);
        const double spread = animated_scalar_max_nonnegative(
            effect.spread_prop, effect.effect_spread);
        const double distance = animated_scalar_max_nonnegative(
            effect.distance_prop, effect.effect_distance);
        switch (effect.type) {
        case LayerEffectType::BackgroundColor:
            left += animated_scalar_max_nonnegative(
                effect.padding_left_prop, effect.effect_padding_left);
            right += animated_scalar_max_nonnegative(
                effect.padding_right_prop, effect.effect_padding_right);
            top += animated_scalar_max_nonnegative(
                effect.padding_top_prop, effect.effect_padding_top);
            bottom += animated_scalar_max_nonnegative(
                effect.padding_bottom_prop, effect.effect_padding_bottom);
            break;
        case LayerEffectType::Outline: {
            const double outline = animated_scalar_max_nonnegative(
                effect.stroke_width_prop, effect.effect_stroke_width);
            left += outline;
            right += outline;
            top += outline;
            bottom += outline;
            break;
        }
        case LayerEffectType::DropShadow:
        case LayerEffectType::LongShadow: {
            const double halo = effect_size + spread;
            if (effect.angle_prop.is_animated()) {
                /* An animated direction can visit either side of the layer. */
                left += halo + distance;
                right += halo + distance;
                top += halo + distance;
                bottom += halo + distance;
            } else {
                const double angle = effect.angle_prop.is_animated()
                    ? effect.angle_prop.static_value
                    : effect.effect_angle;
                const double radians = angle *
                    3.14159265358979323846 / 180.0;
                const double dx = std::cos(radians) * distance;
                const double dy = std::sin(radians) * distance;
                left += halo + std::max(0.0, -dx);
                right += halo + std::max(0.0, dx);
                top += halo + std::max(0.0, -dy);
                bottom += halo + std::max(0.0, dy);
            }
            break;
        }
        case LayerEffectType::Glow:
        case LayerEffectType::Bloom:
        case LayerEffectType::Blur:
        case LayerEffectType::MotionBlur: {
            const double halo = effect_size + spread +
                (effect.type == LayerEffectType::MotionBlur ? distance : 0.0);
            left += halo;
            right += halo;
            top += halo;
            bottom += halo;
            break;
        }
        case LayerEffectType::Emboss:
            left += effect_size;
            right += effect_size;
            top += effect_size;
            bottom += effect_size;
            break;
        case LayerEffectType::LensFlare:
        case LayerEffectType::Vignette:
        case LayerEffectType::Noise:
        case LayerEffectType::Grain:
        case LayerEffectType::FilmDistortion:
        case LayerEffectType::AnalogDistortion:
        case LayerEffectType::DigitalDistortion:
        case LayerEffectType::RoughenEdges:
            return frame_rect;
        case LayerEffectType::InnerGlow:
        case LayerEffectType::InnerShadow:
        case LayerEffectType::BrightnessContrast:
        case LayerEffectType::Saturation:
        case LayerEffectType::ColorOverlay:
        case LayerEffectType::FourColorGradient:
        case LayerEffectType::Sharpen:
        case LayerEffectType::UnsharpMask:
        case LayerEffectType::HighPass:
        case LayerEffectType::Clarity:
        case LayerEffectType::BilateralSharpen:
        case LayerEffectType::Glare:
        case LayerEffectType::Halation:
        case LayerEffectType::LensDistortion:
        case LayerEffectType::ChromaticAberration:
        case LayerEffectType::DirectionalBlur:
        case LayerEffectType::ZoomBlur:
        case LayerEffectType::RadialBlur:
        case LayerEffectType::Ripple:
        case LayerEffectType::WaveWarp:
        case LayerEffectType::Pixelate:
        case LayerEffectType::EdgeDetect:
        case LayerEffectType::Posterize:
        case LayerEffectType::Threshold:
        case LayerEffectType::Scanlines:
        case LayerEffectType::ChromaKey:
        case LayerEffectType::LumaKey:
        case LayerEffectType::ColorRange:
        case LayerEffectType::SpillSuppression:
        case LayerEffectType::MatteChoker:
        case LayerEffectType::LightWrap:
        case LayerEffectType::DisplacementMap:
            break;
        }
    }

    const AnimatedVec2Extents origin = animated_vec2_extents(layer.origin_prop);
    const double geometry_left = std::max(std::abs(origin.min_x),
                                          std::abs(origin.max_x)) * w;
    const double geometry_right = std::max(std::abs(1.0 - origin.min_x),
                                           std::abs(1.0 - origin.max_x)) * w;
    const double geometry_top = std::max(std::abs(origin.min_y),
                                         std::abs(origin.max_y)) * h;
    const double geometry_bottom = std::max(std::abs(1.0 - origin.min_y),
                                            std::abs(1.0 - origin.max_y)) * h;
    const double local_left = geometry_left + left;
    const double local_right = geometry_right + right;
    const double local_top = geometry_top + top;
    const double local_bottom = geometry_bottom + bottom;

    if (layer.rotation.is_animated()) {
        /* Rotation about an arbitrary animated origin is cheap to invalidate as
         * a full frame and avoids underestimating a swept corner. The readback
         * policy may still choose a region for other layers/titles. */
        return frame_rect;
    }

    double rotated_min_x = -local_left;
    double rotated_max_x = local_right;
    double rotated_min_y = -local_top;
    double rotated_max_y = local_bottom;
    if (std::fmod(std::abs(layer.rotation.static_value), 360.0) > 0.001) {
        const double radians = layer.rotation.static_value *
            3.14159265358979323846 / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        rotated_min_x = std::numeric_limits<double>::max();
        rotated_max_x = std::numeric_limits<double>::lowest();
        rotated_min_y = std::numeric_limits<double>::max();
        rotated_max_y = std::numeric_limits<double>::lowest();
        for (double corner_x : {-local_left, local_right}) {
            for (double corner_y : {-local_top, local_bottom}) {
                const double transformed_x = corner_x * cosine - corner_y * sine;
                const double transformed_y = corner_x * sine + corner_y * cosine;
                rotated_min_x = std::min(rotated_min_x, transformed_x);
                rotated_max_x = std::max(rotated_max_x, transformed_x);
                rotated_min_y = std::min(rotated_min_y, transformed_y);
                rotated_max_y = std::max(rotated_max_y, transformed_y);
            }
        }
    }

    const QRectF rect(
        position.min_x + rotated_min_x,
        position.min_y + rotated_min_y,
        (position.max_x - position.min_x) +
            (rotated_max_x - rotated_min_x),
        (position.max_y - position.min_y) +
            (rotated_max_y - rotated_min_y));
    return rect.toAlignedRect().intersected(frame_rect);
}

QString CacheManager::tileStateKey(const QString &title_id, int frame) const
{
    return title_id + QLatin1Char(':') + QString::number(frame);
}

void CacheManager::markDirtyTiles(const QString &title_id, int first_frame, int last_frame, const QVector<CacheTileRegion> &tiles)
{
    if (!tiles.isEmpty())
        fxm::perf::add(fxm::perf::Counter::DirtyRegionInvalidations,
                       static_cast<std::uint64_t>(tiles.size()));
    if (title_id.isEmpty() || first_frame > last_frame || tiles.isEmpty())
        return;
    QMutexLocker lock(&dirty_tiles_mutex_);
    for (int frame = first_frame; frame <= last_frame; ++frame) {
        QSet<QString> &set = dirty_tiles_by_frame_[tileStateKey(title_id, frame)];
        for (const auto &tile : tiles)
            set.insert(QString::number(tile.tile_x) + QLatin1Char(',') + QString::number(tile.tile_y));
    }
}

QVector<CacheTileRegion> CacheManager::dirtyTilesForKey(const CacheFrameKey &key) const
{
    QVector<CacheTileRegion> regions;
    const QSize frame_size(std::max(1, key.width), std::max(1, key.height));
    QSet<QString> ids;
    {
        QMutexLocker lock(&dirty_tiles_mutex_);
        ids = dirty_tiles_by_frame_.value(tileStateKey(key.title_id, key.frame));
    }
    for (const QString &id : ids) {
        const QStringList parts = id.split(QLatin1Char(','));
        if (parts.size() != 2) continue;
        bool ok_x = false, ok_y = false;
        const int tx = parts[0].toInt(&ok_x);
        const int ty = parts[1].toInt(&ok_y);
        if (!ok_x || !ok_y) continue;
        const QRect tile_rect(tx * kCacheTileSize, ty * kCacheTileSize, kCacheTileSize, kCacheTileSize);
        regions.push_back({tx, ty, tile_rect.intersected(QRect(QPoint(0, 0), frame_size))});
    }
    return regions;
}

static bool effect_requires_full_gpu_cache_frame(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::DropShadow:
    case LayerEffectType::LongShadow:
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
    case LayerEffectType::InnerShadow:
    case LayerEffectType::Blur:
    case LayerEffectType::MotionBlur:
    case LayerEffectType::Bloom:
    case LayerEffectType::Emboss:
    case LayerEffectType::LensFlare:
    case LayerEffectType::Vignette:
    case LayerEffectType::Noise:
        case LayerEffectType::Grain:
        case LayerEffectType::FilmDistortion:
        case LayerEffectType::AnalogDistortion:
        case LayerEffectType::DigitalDistortion:
    case LayerEffectType::RoughenEdges:
    case LayerEffectType::FourColorGradient:
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen:
        case LayerEffectType::Glare:
        case LayerEffectType::Halation:
        case LayerEffectType::LensDistortion:
        case LayerEffectType::ChromaticAberration:
        case LayerEffectType::DirectionalBlur:
        case LayerEffectType::ZoomBlur:
        case LayerEffectType::RadialBlur:
        case LayerEffectType::Ripple:
        case LayerEffectType::WaveWarp:
        case LayerEffectType::Pixelate:
        case LayerEffectType::EdgeDetect:
        case LayerEffectType::Posterize:
        case LayerEffectType::Threshold:
        case LayerEffectType::Scanlines:
        case LayerEffectType::ChromaKey:
        case LayerEffectType::LumaKey:
        case LayerEffectType::ColorRange:
        case LayerEffectType::SpillSuppression:
        case LayerEffectType::MatteChoker:
        case LayerEffectType::LightWrap:
        case LayerEffectType::DisplacementMap:
        return true;
    default:
        return false;
    }
}

static bool title_requires_full_gpu_cache_frame(const Title &title)
{
    if (fxm::transform3d::title_camera_has_animation(title))
        return true;
    for (const auto &layer : title.layers) {
        if (!layer)
            continue;
        if (layer->dimension_mode == LayerDimensionMode::ThreeD ||
            layer->type == LayerType::Adjustment ||
            layer->type == LayerType::ColorSolid ||
            !layer->parent_id.empty() ||
            !layer->transform_parent_id.empty() ||
            layer->mask_mode != MaskMode::None ||
            layer->use_as_scene_mask)
            return true;
        for (const LayerEffect &effect : layer->effects) {
            if ((effect.enabled || effect.enabled_prop.is_animated()) &&
                effect_requires_full_gpu_cache_frame(effect.type))
                return true;
        }
        for (const LayerTransition &transition : layer->transitions) {
            if (!transition.enabled || transition.kind != LayerTransitionKind::General)
                continue;
            switch (transition.type) {
            case LayerTransitionType::OpacityBlur:
            case LayerTransitionType::ZoomBlur:
            case LayerTransitionType::BlurSlide:
                if (transition.blur_amount > 0.01)
                    return true;
                break;
            case LayerTransitionType::Blocks:
            case LayerTransitionType::ImageWipe:
            case LayerTransitionType::Clock:
            case LayerTransitionType::Iris:
            case LayerTransitionType::GradientWipe:
                return true;
            default:
                break;
            }
        }
    }
    return false;
}


void CacheManager::rememberVisualHash(const Title &title, const QString &known_hash)
{
    const QString title_id = QString::fromStdString(title.id);
    const QString visual_hash = known_hash.isEmpty() ? contentHash(title) : known_hash;
    QMutexLocker lock(&visual_hash_mutex_);
    if (last_visual_hash_by_title_.value(title_id) == visual_hash &&
        last_layer_rects_by_title_.contains(title_id))
        return;

    last_visual_hash_by_title_[title_id] = visual_hash;
    QHash<QString, QRect> rects;
    for (const auto &layer : title.layers) {
        if (layer)
            rects[QString::fromStdString(layer->id)] = layerDirtyRect(title, *layer);
    }
    last_layer_rects_by_title_[title_id] = rects;
}

bool CacheManager::visualHashUnchanged(const Title &title) const
{
    QMutexLocker lock(&visual_hash_mutex_);
    const QString title_id = QString::fromStdString(title.id);
    return last_visual_hash_by_title_.contains(title_id) &&
           last_visual_hash_by_title_.value(title_id) == contentHash(title);
}

void CacheManager::resumePrerender()
{
    paused_.store(false);
    wakeWorker();
}

void CacheManager::invalidateAll(const std::shared_ptr<Title> &title)
{
    if (!title) return;
    if (titleCacheability(title) == TitleCacheability::NonCacheable) {
        removeTitleCache(QString::fromStdString(title->id), true);
        return;
    }
    if (visualHashUnchanged(*title)) {
        FXM_LOG_TRACE("Cache", QStringLiteral("Skipped frame-cache invalidation for unchanged rendered title=%1; refreshing live-cue structure selectively")
                                      .arg(QString::fromStdString(title->id)));
        /* Structural cue-list edits do not change the title's currently rendered
         * pixels. Clearing every live-cue state here defeated stable row IDs and
         * forced all rows to render again after add/remove/reorder. Let the
         * structure reconciler invalidate only added/removed/edited row states. */
        refreshLiveCueStructure(title);
        return;
    }
    const QString title_id = QString::fromStdString(title->id);
    /* invalidateAll() cancels/marks the previous generation stale. A same-frame
     * reprioritize must therefore not be suppressed by the old UI sentinel. */
    if (last_reprioritize_title_id_ == title_id) {
        last_reprioritize_title_id_.clear();
        last_reprioritize_frame_ = -1;
    }
    const QSize frame_size(std::max(1, title->width), std::max(1, title->height));
    markDirtyTiles(title_id, 0, cache_last_frame_for_title(*title, effectiveFrameRate()),
                   tilesForRect(QRect(QPoint(0, 0), frame_size), frame_size));
    invalidation_.invalidateAll(title);
    invalidateLiveCues(title);
}

void CacheManager::invalidateRange(const std::shared_ptr<Title> &title, double start, double end)
{
    if (!title) return;
    if (visualHashUnchanged(*title)) {
        FXM_LOG_TRACE("Cache", QStringLiteral("Skipped cache range invalidation for unchanged title=%1")
                                      .arg(QString::fromStdString(title->id)));
        return;
    }
    const int max_frame = cache_last_frame_for_title(*title, effectiveFrameRate());
    if (end < start) std::swap(start, end);
    const int first = std::clamp(frameForTime(std::clamp(start, 0.0, title->duration)), 0, max_frame);
    const int last = std::clamp(frameForTime(std::clamp(end, 0.0, title->duration)), 0, max_frame);
    const QSize frame_size(std::max(1, title->width), std::max(1, title->height));
    markDirtyTiles(QString::fromStdString(title->id), first, last,
                   tilesForRect(QRect(QPoint(0, 0), frame_size), frame_size));
    invalidation_.invalidateRange(title, start, end);
}

void CacheManager::invalidateLayer(const std::shared_ptr<Title> &title, const std::string &layer_id)
{
    if (!title) return;
    if (visualHashUnchanged(*title)) {
        FXM_LOG_TRACE("Cache", QStringLiteral("Skipped layer invalidation for unchanged title=%1 layer=%2")
                                      .arg(QString::fromStdString(title->id), QString::fromStdString(layer_id)));
        return;
    }
    if (auto layer = title->find_layer(layer_id)) {
        const int max_frame = cache_last_frame_for_title(*title, effectiveFrameRate());
        const int first = std::clamp(frameForTime(std::clamp(layer->in_time, 0.0, title->duration)), 0, max_frame);
        const int last = std::clamp(frameForTime(std::clamp(layer->out_time, 0.0, title->duration)), 0, max_frame);
        const QSize frame_size(std::max(1, title->width), std::max(1, title->height));
        QRect dirty_rect = layerDirtyRect(*title, *layer);
        {
            QMutexLocker lock(&visual_hash_mutex_);
            dirty_rect = dirty_rect.united(last_layer_rects_by_title_
                .value(QString::fromStdString(title->id))
                .value(QString::fromStdString(layer_id)));
        }
        markDirtyTiles(QString::fromStdString(title->id), first, last, tilesForRect(dirty_rect, frame_size));
    }
    invalidation_.invalidateLayer(title, layer_id);
}

std::shared_ptr<Title> CacheManager::titleWithCueApplied(const std::shared_ptr<Title> &title, int row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title || row < 0 || row >= static_cast<int>(title->live_text_rows.size()))
        return nullptr;

    auto cue_title = clone_title_snapshot(title);
    if (!cue_title)
        return nullptr;

    std::vector<std::shared_ptr<Layer>> exposed;
    for (const auto &layer : cue_title->layers) {
        if (!layer) continue;
        if (fxm::live_text::is_exposed_cue_layer(layer))
            exposed.push_back(layer);
    }
    if (!cue_title->live_text_column_order.empty()) {
        std::vector<std::shared_ptr<Layer>> ordered;
        ordered.reserve(exposed.size());
        for (const auto &layer_id : cue_title->live_text_column_order) {
            auto it = std::find_if(exposed.begin(), exposed.end(),
                                   [&](const std::shared_ptr<Layer> &layer) {
                                       return layer && layer->id == layer_id;
                                   });
            if (it != exposed.end())
                ordered.push_back(*it);
        }
        for (const auto &layer : exposed) {
            auto it = std::find_if(ordered.begin(), ordered.end(),
                                   [&](const std::shared_ptr<Layer> &candidate) {
                                       return candidate && layer && candidate->id == layer->id;
                                   });
            if (it == ordered.end())
                ordered.push_back(layer);
        }
        exposed = std::move(ordered);
    }

    for (int col = 0; col < static_cast<int>(exposed.size()) &&
         col < static_cast<int>(cue_title->live_text_rows[row].size()); ++col) {
        auto &target = exposed[col];
        if (!target)
            continue;
        const int value_row = target->exposed_single_value ? 0 : row;
        if (value_row < 0 || value_row >= static_cast<int>(cue_title->live_text_rows.size()) ||
            col >= static_cast<int>(cue_title->live_text_rows[value_row].size()))
            continue;
        const std::string cue_value = cue_title->live_text_rows[value_row][col];
        target->live_cue_hidden_if_empty = target->exposed_hide_if_empty && cue_value.empty();
        apply_live_text_cue_style_to_layer(*cue_title, *target, row);
        if (target->type == LayerType::Image) {
            fxm::apply_exposed_image_cue_value(*target, cue_value);
            continue;
        }
        if (target->type == LayerType::Chart) {
            fxm::live_text::apply_chart_cue_value(*target, cue_value);
            continue;
        }
        target->text_content = cue_value;
        if (target->rich_text.empty())
            target->rich_text = rich_text_document_from_layer_defaults(*target);
        RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(target->rich_text);
        if (target->expose_fill_color) {
            insertion_format.fill.type = 0;
            insertion_format.fill.color = live_text_cue_effective_color(*cue_title, *target, row, false);
        }
        if (target->expose_stroke_color) {
            insertion_format.stroke.enabled = target->outline_enabled || target->stroke_width > 0.0f;
            insertion_format.stroke.width = target->stroke_width;
            insertion_format.stroke.opacity = target->outline_opacity;
            insertion_format.stroke.fill.type = 0;
            insertion_format.stroke.fill.color = live_text_cue_effective_color(*cue_title, *target, row, true);
        }
        rich_text_document_replace_text(target->rich_text, cue_value, insertion_format,
                                        target->rich_text.has_typing_format
                                            ? target->rich_text.typing_format_mask : 0);
    }

    /* Steady cue titles are deterministic. Runtime current/pending state must
     * never leak into their cache identity, otherwise adding/removing a row
     * changes every existing row's key. Stateful persistence transitions are
     * represented by separate from->to cache states below. */
    cue_title->current_cue_row = row;
    cue_title->pending_cue_row = -1;
    cue_title->cue_persistence_transition = false;
    cue_title->cue_persistent_text_columns.clear();
    return cue_title;
}


QVector<std::shared_ptr<Title>> CacheManager::liveCueVariants(const std::shared_ptr<Title> &title, int row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    QVector<std::shared_ptr<Title>> variants;
    if (auto steady = titleWithCueApplied(title, row))
        variants.push_back(steady);
    return variants;
}

QVector<std::shared_ptr<Title>> CacheManager::liveCueTransitionVariants(
    const std::shared_ptr<Title> &title, int from_row, int to_row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    QVector<std::shared_ptr<Title>> variants;
    if (!title || from_row < 0 || to_row < 0 || from_row == to_row ||
        from_row >= static_cast<int>(title->live_text_rows.size()) ||
        to_row >= static_cast<int>(title->live_text_rows.size()))
        return variants;

    if (title->playback_mode != 1 && title->playback_mode != 2 && !title->cue_background_persistence)
        return variants;

    auto persistent_columns = [&](const std::shared_ptr<Title> &candidate) {
        std::vector<bool> result;
        if (!candidate || !candidate->cue_text_persistence)
            return result;
        std::vector<std::shared_ptr<Layer>> exposed;
        for (const auto &layer : candidate->layers) {
            if (fxm::live_text::is_exposed_cue_layer(layer))
                exposed.push_back(layer);
        }
        if (!candidate->live_text_column_order.empty()) {
            std::vector<std::shared_ptr<Layer>> ordered;
            ordered.reserve(exposed.size());
            for (const auto &layer_id : candidate->live_text_column_order) {
                auto it = std::find_if(exposed.begin(), exposed.end(),
                                       [&](const std::shared_ptr<Layer> &layer) {
                                           return layer && layer->id == layer_id;
                                       });
                if (it != exposed.end())
                    ordered.push_back(*it);
            }
            for (const auto &layer : exposed) {
                auto it = std::find_if(ordered.begin(), ordered.end(),
                                       [&](const std::shared_ptr<Layer> &candidate_layer) {
                                           return candidate_layer && layer && candidate_layer->id == layer->id;
                                       });
                if (it == ordered.end())
                    ordered.push_back(layer);
            }
            exposed = std::move(ordered);
        }
        result.assign(exposed.size(), false);
        for (size_t col = 0; col < result.size(); ++col) {
            const auto &layer = exposed[col];
            const int from_value_row = layer && layer->exposed_single_value ? 0 : from_row;
            const int to_value_row = layer && layer->exposed_single_value ? 0 : to_row;
            if (from_value_row < 0 || to_value_row < 0 ||
                from_value_row >= static_cast<int>(title->live_text_rows.size()) ||
                to_value_row >= static_cast<int>(title->live_text_rows.size()) ||
                col >= title->live_text_rows[static_cast<size_t>(from_value_row)].size() ||
                col >= title->live_text_rows[static_cast<size_t>(to_value_row)].size())
                continue;
            result[col] = title->live_text_rows[static_cast<size_t>(from_value_row)][col] ==
                          title->live_text_rows[static_cast<size_t>(to_value_row)][col];
        }
        return result;
    };

    /* Outgoing phase: the previous cue remains applied while the title plays
     * its outro. pending_cue_row is control state only; rendered identity comes
     * from the applied previous-row pixels and persistence column mask. */
    if (auto outgoing = titleWithCueApplied(title, from_row)) {
        outgoing->current_cue_row = from_row;
        outgoing->pending_cue_row = to_row;
        outgoing->cue_persistence_transition = title->cue_background_persistence;
        outgoing->cue_persistent_text_columns = persistent_columns(outgoing);
        variants.push_back(outgoing);
    }

    /* Incoming phase: target cue text is applied, while persistent background
     * layers (and unchanged text columns) are held exactly as in live playback. */
    if (auto incoming = titleWithCueApplied(title, to_row)) {
        incoming->current_cue_row = to_row;
        incoming->pending_cue_row = -1;
        incoming->cue_persistence_transition = title->cue_background_persistence;
        incoming->cue_persistent_text_columns = persistent_columns(incoming);
        variants.push_back(incoming);
    }
    return variants;
}


int CacheManager::liveCueVariantsProgress(const QVector<std::shared_ptr<Title>> &variants) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (variants.isEmpty())
        return 0;
    int total_frames = 0;
    int cached_frames = 0;
    QSet<CacheFrameKey> seen;
    for (const auto &variant : variants) {
        if (!variant)
            continue;
        for (const CacheFrameKey &frame_key : frameKeysForTitle(*variant)) {
            if (seen.contains(frame_key))
                continue;
            seen.insert(frame_key);
            ++total_frames;
            if (cache_ram_frame_contains(ram_cache_, frame_key) ||
                disk_cache_.contains(frame_key))
                ++cached_frames;
        }
    }
    if (total_frames <= 0)
        return 0;
    return std::clamp((cached_frames * 100) / total_frames, 0, 100);
}


int CacheManager::liveCueStoredProgress(const QString &state_key) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    /* A manual rebuild is a new rendering generation even when its
     * content-addressed frames are still resident from the previous one.
     * Report generation completion, not old RAM/SSD residency, otherwise the
     * badge stays at 100% and oscillates between Rendering and CachedRam. */
    if (live_cue_total_by_key_.contains(state_key)) {
        const int total = std::max(1, live_cue_total_by_key_.value(state_key));
        const int done = std::clamp(live_cue_done_by_key_.value(state_key, 0), 0, total);
        return std::clamp((done * 100) / total, 0, 100);
    }
    const auto keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return live_cue_progress_percent_.value(state_key, 0);

    int cached = 0;
    for (const CacheFrameKey &frame_key : keys) {
        if (cache_ram_frame_contains(ram_cache_, frame_key) ||
            disk_cache_.contains(frame_key))
            ++cached;
    }
    const int total = std::max(1, static_cast<int>(keys.size()));
    return std::clamp((cached * 100) / total, 0, 100);
}

FrameCacheState CacheManager::liveCueStoredState(const QString &state_key) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    const auto keys = live_cue_required_keys_.value(state_key);
    if (keys.isEmpty())
        return live_cue_states_.value(state_key, FrameCacheState::NotCached);

    const FrameCacheState explicit_state = live_cue_states_.value(state_key, FrameCacheState::NotCached);
    if (live_cue_total_by_key_.contains(state_key) &&
        live_cue_done_by_key_.value(state_key, 0) < live_cue_total_by_key_.value(state_key)) {
        return explicit_state == FrameCacheState::Rendering
            ? FrameCacheState::Rendering
            : FrameCacheState::Queued;
    }
    bool any_disk = false;
    bool any_rendering = explicit_state == FrameCacheState::Rendering;
    bool any_queued = explicit_state == FrameCacheState::Queued;
    bool any_stale = explicit_state == FrameCacheState::Stale;
    int cached = 0;

    for (const CacheFrameKey &frame_key : keys) {
        const FrameCacheState frame_state = state_tracker_.state(frame_key);
        if (frame_state == FrameCacheState::Rendering)
            any_rendering = true;
        if (frame_state == FrameCacheState::Queued || queue_.contains(frame_key))
            any_queued = true;
        if (frame_state == FrameCacheState::Stale)
            any_stale = true;
        if (cache_ram_frame_contains(ram_cache_, frame_key)) {
            ++cached;
        } else if (disk_cache_.contains(frame_key)) {
            ++cached;
            any_disk = true;
        }
    }

    if (any_rendering) return FrameCacheState::Rendering;
    /* Shared content-addressed frames can complete through another cue state.
     * Full residency is authoritative even if this state's advisory Queued flag
     * has not yet received its own completion callback. */
    if (cached >= static_cast<int>(keys.size())) return any_disk ? FrameCacheState::CachedDisk : FrameCacheState::CachedRam;
    if (any_queued) return FrameCacheState::Queued;
    if (cached > 0) return any_stale ? FrameCacheState::Stale : FrameCacheState::NotCached;
    if (any_stale) return FrameCacheState::Stale;
    return FrameCacheState::NotCached;
}

bool CacheManager::shouldEmitLiveCueUpdate(const QString &state_key, FrameCacheState state, int progress) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    const int bucket = progress >= 100 ? 100 : (progress / 25) * 25;

    /* Queue-to-worker handoff must not be treated as a visible state change.
     * Both states use the same four-stage progress presentation; emitting on
     * every Queued <-> Rendering transition caused needless icon replacement
     * and orange/yellow flicker while the percentage stayed in one bucket. */
    const FrameCacheState visual_state =
        (state == FrameCacheState::Queued || state == FrameCacheState::Rendering)
            ? FrameCacheState::Rendering
            : state;
    const bool changed = !live_cue_last_emit_states_.contains(state_key) ||
        live_cue_last_emit_states_.value(state_key) != visual_state ||
        live_cue_last_emit_buckets_.value(state_key, -1) != bucket;
    if (changed) {
        live_cue_last_emit_states_[state_key] = visual_state;
        live_cue_last_emit_buckets_[state_key] = bucket;
    }
    return changed;
}

bool CacheManager::isLiveCueKeyReferenced(const CacheFrameKey &key) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    return !liveCueStateReferencingKey(key).isEmpty();
}

QString CacheManager::liveCueStateReferencingKey(const CacheFrameKey &key,
                                                  const QString &preferred_state) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    auto contains_key = [&](const QString &state_key) {
        const QVector<CacheFrameKey> required = live_cue_required_keys_.value(state_key);
        return !required.isEmpty() &&
            std::find(required.cbegin(), required.cend(), key) != required.cend();
    };
    if (!preferred_state.isEmpty() && contains_key(preferred_state))
        return preferred_state;
    for (auto it = live_cue_required_keys_.constBegin(); it != live_cue_required_keys_.constEnd(); ++it) {
        if (live_cue_title_ids_.value(it.key()) != key.title_id)
            continue;
        if (std::find(it.value().cbegin(), it.value().cend(), key) != it.value().cend())
            return it.key();
    }
    return {};
}

void CacheManager::removeLiveCueState(const QString &state_key)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    live_cue_states_.remove(state_key);
    live_cue_progress_percent_.remove(state_key);
    live_cue_last_emit_states_.remove(state_key);
    live_cue_last_emit_buckets_.remove(state_key);
    live_cue_required_keys_.remove(state_key);
    live_cue_required_total_.remove(state_key);
    live_cue_rendered_since_emit_.remove(state_key);
    live_cue_total_by_key_.remove(state_key);
    live_cue_done_by_key_.remove(state_key);
    live_cue_rows_.remove(state_key);
    live_cue_row_ids_.remove(state_key);
    live_cue_title_ids_.remove(state_key);
    live_cue_transition_state_keys_.remove(state_key);
}

void CacheManager::pruneUnreferencedLiveCueRam(const QString &title_id)
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    QSet<CacheFrameKey> referenced;
    for (auto it = live_cue_required_keys_.constBegin(); it != live_cue_required_keys_.constEnd(); ++it) {
        if (live_cue_title_ids_.value(it.key()) != title_id)
            continue;
        for (const CacheFrameKey &key : it.value())
            referenced.insert(key);
    }

    /* Inspect every key ever registered by the live-cue cache, not just frames
     * currently resident in RAM. Otherwise obsolete queued jobs continue to
     * render after add/remove/edit, then repopulate RAM with superseded frames. */
    QSet<CacheFrameKey> candidates;
    for (const CacheFrameKey &key : live_cue_known_keys_) {
        if (key.title_id == title_id)
            candidates.insert(key);
    }
    for (const CacheFrameKey &key : ram_cache_.keysForTitle(title_id))
        candidates.insert(key);

    int cancelled_jobs = 0;
    int removed_ram = 0;
    int removed_keys = 0;
    for (const CacheFrameKey &key : candidates) {
        if (referenced.contains(key))
            continue;
        if (queue_.cancelKey(key))
            ++cancelled_jobs;
        const bool gpu_resident = gpu_ram_frame_contains(key);
        QImage resident;
        const bool cpu_resident = ram_cache_.get(key, resident);
        if (gpu_resident)
            title_gpu_frame_cache_remove(key.toString().toStdString());
        if (cpu_resident)
            ram_cache_.remove(key);
        if (gpu_resident || cpu_resident)
            ++removed_ram;
        if (disk_cache_.contains(key))
            state_tracker_.setState(key, FrameCacheState::CachedDisk);
        else
            state_tracker_.setState(key, FrameCacheState::NotCached);
        live_cue_known_keys_.remove(key);
        ++removed_keys;
    }

    if (removed_keys > 0) {
        FXM_LOG_INFO("LiveCue", QStringLiteral("Pruned superseded live cue work title=%1 keys=%2 ramFrames=%3 queuedJobs=%4")
                                  .arg(title_id)
                                  .arg(removed_keys)
                                  .arg(removed_ram)
                                  .arg(cancelled_jobs));
        scheduleDiagnosticsChanged();
    }
}

FrameCacheState CacheManager::liveCueVariantsState(const QVector<std::shared_ptr<Title>> &variants, const QString &state_key) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (variants.isEmpty())
        return FrameCacheState::NotCached;

    const FrameCacheState explicit_state = live_cue_states_.value(state_key, FrameCacheState::NotCached);
    bool any_disk = false;
    bool any_rendering = explicit_state == FrameCacheState::Rendering;
    bool any_queued = explicit_state == FrameCacheState::Queued;
    bool any_stale = explicit_state == FrameCacheState::Stale;
    int total = 0;
    int cached = 0;

    QSet<CacheFrameKey> seen;
    for (const auto &variant : variants) {
        if (!variant)
            continue;
        for (const CacheFrameKey &frame_key : frameKeysForTitle(*variant)) {
            if (seen.contains(frame_key))
                continue;
            seen.insert(frame_key);
            ++total;
            const FrameCacheState frame_state = state_tracker_.state(frame_key);
            if (frame_state == FrameCacheState::Rendering)
                any_rendering = true;
            if (frame_state == FrameCacheState::Queued || queue_.contains(frame_key))
                any_queued = true;
            if (frame_state == FrameCacheState::Stale)
                any_stale = true;
            if (cache_ram_frame_contains(ram_cache_, frame_key)) {
                ++cached;
            } else if (disk_cache_.contains(frame_key)) {
                ++cached;
                any_disk = true;
            }
        }
    }

    if (any_rendering)
        return FrameCacheState::Rendering;
    if (total > 0 && cached >= total)
        return any_disk ? FrameCacheState::CachedDisk : FrameCacheState::CachedRam;
    if (any_queued)
        return FrameCacheState::Queued;
    if (cached > 0)
        return any_stale ? FrameCacheState::Stale : FrameCacheState::NotCached;
    if (any_stale)
        return FrameCacheState::Stale;
    return FrameCacheState::NotCached;
}

CacheFrameKey CacheManager::liveCueKey(const std::shared_ptr<Title> &title, int row) const
{
    auto cue_title = titleWithCueApplied(title, row);
    if (!cue_title) return {};
    return keyForTime(*cue_title, std::clamp(cue_title->pause_time, 0.0, cue_title->duration));
}

QString CacheManager::liveCueRowIdentity(const std::shared_ptr<Title> &title, int row) const
{
    if (!title)
        return {};
    return QString::fromStdString(live_text_row_id(*title, row));
}

QString CacheManager::liveCueStateKey(const std::shared_ptr<Title> &title, int row) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!title)
        return {};
    return QStringLiteral("%1:live-cue:%2")
        .arg(QString::fromStdString(title->id))
        .arg(liveCueRowIdentity(title, row));
}

QString CacheManager::liveCueTransitionStateKey(const std::shared_ptr<Title> &title,
                                                int from_row, int to_row) const
{
    if (!title)
        return {};
    return QStringLiteral("%1:live-transition:%2:%3")
        .arg(QString::fromStdString(title->id))
        .arg(liveCueRowIdentity(title, from_row))
        .arg(liveCueRowIdentity(title, to_row));
}

int CacheManager::liveCueRangeProgress(const std::shared_ptr<Title> &cue_title) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cue_title)
        return 0;
    const auto keys = frameKeysForTitle(*cue_title);
    const int total = std::max(1, static_cast<int>(keys.size()));
    int cached = 0;
    for (const CacheFrameKey &frame_key : keys) {
        if (cache_ram_frame_contains(ram_cache_, frame_key) ||
            disk_cache_.contains(frame_key))
            ++cached;
    }
    return std::clamp((cached * 100) / total, 0, 100);
}

FrameCacheState CacheManager::liveCueRangeState(const std::shared_ptr<Title> &cue_title, const QString &state_key) const
{
    std::lock_guard<std::recursive_mutex> live_lock(live_cue_mutex_);
    if (!cue_title)
        return FrameCacheState::NotCached;

    const FrameCacheState explicit_state = live_cue_states_.value(state_key, FrameCacheState::NotCached);
    const auto keys = frameKeysForTitle(*cue_title);
    const int total = std::max(1, static_cast<int>(keys.size()));
    int cached = 0;
    bool any_disk = false;
    bool any_rendering = false;
    bool any_queued = false;
    bool any_stale = explicit_state == FrameCacheState::Stale;

    for (const CacheFrameKey &frame_key : keys) {
        const FrameCacheState frame_state = state_tracker_.state(frame_key);
        if (frame_state == FrameCacheState::Rendering)
            any_rendering = true;
        if (frame_state == FrameCacheState::Queued || queue_.contains(frame_key))
            any_queued = true;
        if (frame_state == FrameCacheState::Stale)
            any_stale = true;
