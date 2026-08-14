void CanvasPreview::begin_adaptive_interaction()
{
    if (!adaptive_rendering_enabled_)
        return;
    adaptive_interaction_active_ = true;
    if (title_) {
        fxm::ticker_runtime::set_adaptive_pause(title_->id, true);
        FXM_LOG_DEBUG("Ticker", QStringLiteral(
            "action=adaptive-pause title=%1")
            .arg(QString::fromStdString(title_->id)));
    }
    /* The idle timer also releases the ticker auto-pause after keyboard/text
     * edits that do not have a mouse-release boundary. Fixed quality modes keep
     * their selected preview scale; only Auto requests a full-quality settle. */
    if (adaptive_full_quality_timer_)
        adaptive_full_quality_timer_->start();
}

void CanvasPreview::end_adaptive_interaction()
{
    if (adaptive_full_quality_timer_)
        adaptive_full_quality_timer_->stop();

    const bool was_reduced_quality = frame_image_preview_scale_ < 0.999;
    adaptive_interaction_active_ = false;
    if (title_) {
        fxm::ticker_runtime::set_adaptive_pause(title_->id, false);
        FXM_LOG_DEBUG("Ticker", QStringLiteral(
            "action=adaptive-resume title=%1")
            .arg(QString::fromStdString(title_->id)));
    }
    if (adaptive_rendering_enabled_ && adaptive_quality_mode_ == AdaptiveQualityMode::Auto && was_reduced_quality)
        force_live_full_quality_render_ = true;
}

double CanvasPreview::adaptive_preview_scale() const
{
    if (!adaptive_rendering_enabled_)
        return 1.0;
    switch (adaptive_quality_mode_) {
    case AdaptiveQualityMode::Full: return 1.0;
    case AdaptiveQualityMode::Percent75: return 0.75;
    case AdaptiveQualityMode::Percent50: return 0.5;
    case AdaptiveQualityMode::Percent37_5: return 0.375;
    case AdaptiveQualityMode::Percent25: return 0.25;
    case AdaptiveQualityMode::Auto:
    default: break;
    }
    /* Auto quality is governed by complete presented-frame timings. The old
     * implementation used render_to_frame(), which only prepares the session;
     * the expensive GPU compositor and gs_present() run later. Consequently a
     * 3D title could report a cheap "render" and stay at full resolution while
     * playback missed most of its frame deadlines. */
    if (!adaptive_interaction_active_ && !transport_playback_active_)
        return 1.0;

    double scale = transport_playback_active_ ? adaptive_auto_scale_ : 0.5;

    const bool geometry_drag =
        drag_mode_ != DragMode::None && drag_mode_ != DragMode::Marquee &&
        drag_mode_ != DragMode::GuideX && drag_mode_ != DragMode::GuideY;
    if (geometry_drag && title_) {
        const double canvas_area = std::max(1.0, (double)title_->width * (double)title_->height);
        double manipulated_area = drag_start_selection_bounds_.isValid()
            ? drag_start_selection_bounds_.width() * drag_start_selection_bounds_.height()
            : canvas_area;
        if (!std::isfinite(manipulated_area) || manipulated_area <= 0.0)
            manipulated_area = canvas_area;
        const double coverage = std::clamp(manipulated_area / canvas_area, 0.0, 1.0);
        if (manipulated_area >= 6000000.0 || canvas_area >= 8300000.0 || coverage >= 0.70)
            scale = std::min(scale, 0.25);
        else if (manipulated_area >= 3500000.0 || canvas_area >= 5000000.0 || coverage >= 0.45)
            scale = std::min(scale, 0.375);
        else if (manipulated_area >= 1600000.0 || canvas_area >= 2500000.0 || coverage >= 0.25)
            scale = std::min(scale, 0.5);
        else if (manipulated_area >= 900000.0 || coverage >= 0.15)
            scale = std::min(scale, 0.75);
    }

    return scale;
}

void CanvasPreview::set_snap_enabled(bool enabled)
{
    snap_settings_.enabled = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_guides(bool enabled)
{
    snap_settings_.guides = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_grid(bool enabled)
{
    snap_settings_.grid = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_object_edges(bool enabled)
{
    snap_settings_.object_edges = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_object_centers(bool enabled)
{
    snap_settings_.object_centers = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_canvas_bounds(bool enabled)
{
    snap_settings_.canvas_bounds = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_snap_to_spacing(bool enabled)
{
    snap_settings_.spacing = enabled;
    if (!enabled) clear_snap_feedback();
    update();
}

void CanvasPreview::set_zoom_percent(int percent)
{
    int clamped = std::clamp(percent, 5, 1600);
    if (zoom_percent_ == clamped && !fit_zoom_active_) return;
    zoom_percent_ = clamped;
    fit_zoom_active_ = false;
    invalidate_canvas_overlay_caches();
    emit zoom_percent_changed(zoom_percent_);
    position_text_editor();
    update();
}

int CanvasPreview::zoom_percent() const
{
    return zoom_percent_;
}

void CanvasPreview::set_checkerboard_pattern(int pattern)
{
    checkerboard_pattern_ = std::clamp(pattern, 0, 5);
    invalidate_checkerboard_cache();
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorCanvasTransparencyKey), checkerboard_pattern_);
    settings.endGroup();
    settings.sync();
    update();
}

void CanvasPreview::set_selection_tool_active()
{
    if (pen_path_active_) finish_pen_path(false);
    commit_text_edit(true);
    active_tool_ = CanvasTool::Selection;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    color_picker_source_image_ = QImage();
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    unsetCursor();
    update();
}

void CanvasPreview::set_direct_selection_tool_active()
{
    if (pen_path_active_) finish_pen_path(false);
    commit_text_edit(true);
    active_tool_ = CanvasTool::DirectSelection;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::ArrowCursor);
    update();
}

void CanvasPreview::set_free_transform_tool_active(int mode)
{
    if (pen_path_active_) finish_pen_path(false);
    commit_text_edit(true);
    active_tool_ = CanvasTool::FreeTransform;
    free_transform_mode_ = std::clamp(mode, 0, 2);
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::ArrowCursor);
    update();
}

bool CanvasPreview::free_transform_controls_available() const
{
    const auto layers = selected_layers();
    return active_tool_ == CanvasTool::FreeTransform && layers.size() == 1 && layers.front() && !layers.front()->locked;
}

bool CanvasPreview::free_transform_keyframed() const
{
    const auto layer = selected_layer();
    return layer && (layer->transform_quad_tl.is_animated() || layer->transform_quad_tr.is_animated() ||
                     layer->transform_quad_br.is_animated() || layer->transform_quad_bl.is_animated());
}

void CanvasPreview::toggle_free_transform_keyframe()
{
    auto layer = selected_layer();
    if (!layer || layer->locked) return;
    const double lt = std::clamp(playhead_ - layer->in_time, 0.0, std::max(0.0, layer->out_time - layer->in_time));
    AnimatedVec2Property *props[] = {&layer->transform_quad_tl, &layer->transform_quad_tr, &layer->transform_quad_br, &layer->transform_quad_bl};
    const float legacy[8] = {layer->transform_quad_tl_x, layer->transform_quad_tl_y, layer->transform_quad_tr_x, layer->transform_quad_tr_y,
                             layer->transform_quad_br_x, layer->transform_quad_br_y, layer->transform_quad_bl_x, layer->transform_quad_bl_y};
    const bool remove = free_transform_keyframed();
    for (int i = 0; i < 4; ++i) {
        auto &prop = *props[i];
        auto it = std::find_if(prop.keyframes.begin(), prop.keyframes.end(), [lt](const VectorKeyframe &k) { return std::abs(k.time - lt) < 1e-6; });
        if (remove && it != prop.keyframes.end()) prop.keyframes.erase(it);
        else if (!remove) {
            const Vec2Value value = editor_quad_value(*layer, prop, legacy[i*2], legacy[i*2+1], playhead_);
            prop.keyframes.push_back({lt, value});
            std::sort(prop.keyframes.begin(), prop.keyframes.end(), [](const VectorKeyframe &a, const VectorKeyframe &b){ return a.time < b.time; });
        }
    }
    dirty_ = gpu_model_dirty_ = true;
    emit layer_geometry_changed();
    notify_corner_context_changed();
    refresh_preview();
}

void CanvasPreview::reset_free_transform()
{
    auto layer = selected_layer();
    if (!layer || layer->locked) return;
    editor_set_quad_value(*layer, layer->transform_quad_tl, layer->transform_quad_tl_x, layer->transform_quad_tl_y, playhead_, {0.0,0.0});
    editor_set_quad_value(*layer, layer->transform_quad_tr, layer->transform_quad_tr_x, layer->transform_quad_tr_y, playhead_, {0.0,0.0});
    editor_set_quad_value(*layer, layer->transform_quad_br, layer->transform_quad_br_x, layer->transform_quad_br_y, playhead_, {0.0,0.0});
    editor_set_quad_value(*layer, layer->transform_quad_bl, layer->transform_quad_bl_x, layer->transform_quad_bl_y, playhead_, {0.0,0.0});
    dirty_ = gpu_model_dirty_ = true;
    emit layer_geometry_changed();
    notify_corner_context_changed();
    refresh_preview();
}

void CanvasPreview::set_shape_tool_active(ShapeType shape_type)
{
    if (pen_path_active_) finish_pen_path(false);
    if (shape_type == ShapeType::Line || shape_type == ShapeType::Path)
        shape_type = ShapeType::Rectangle;
    active_tool_ = CanvasTool::Shape;
    active_shape_type_ = shape_type;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::set_pen_tool_active()
{
    commit_text_edit(true);
    active_tool_ = CanvasTool::Pen;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::set_text_tool_active(LayerType type)
{
    if (pen_path_active_) finish_pen_path(false);
    if (type != LayerType::Text && type != LayerType::Clock && type != LayerType::Ticker)
        type = LayerType::Text;
    active_tool_ = CanvasTool::Text;
    resume_inline_text_edit_after_gradient();
    active_text_layer_type_ = type;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::IBeamCursor);
    update();
}

void CanvasPreview::set_image_tool_active()
{
    if (pen_path_active_) finish_pen_path(false);
    commit_text_edit(true);
    active_tool_ = CanvasTool::Image;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::set_color_picker_tool_active()
{
    if (pen_path_active_) finish_pen_path(false);
    commit_text_edit(true);
    active_tool_ = CanvasTool::ColorPicker;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    /* Freeze the frame sampled by the eyedropper. Live preview may recolor the
     * selected object, but subsequent mouse moves must continue sampling the
     * original composite instead of feeding the preview color back into itself. */
    if (dirty_)
        render_to_frame();
    color_picker_source_image_ = current_rendered_frame();
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::set_gradient_tool_active()
{
    if (pen_path_active_) finish_pen_path(false);
    suspend_inline_text_edit_for_gradient();
    active_tool_ = CanvasTool::Gradient;
    drawing_shape_ = false;
    color_picker_tooltip_visible_ = false;
    gradient_tool_dragging_ = false;
    clear_path_point_selection();
    clear_corner_selection();
    invalidate_canvas_overlay_caches();
    setCursor(Qt::CrossCursor);
    update();
}

void CanvasPreview::set_gradient_editor_active(bool active)
{
    if (gradient_editor_active_ == active) return;
    gradient_editor_active_ = active;
    invalidate_canvas_overlay_caches();
    if (!active && active_tool_ != CanvasTool::Gradient) {
        gradient_drag_ = GradientDragState{};
        if (drag_mode_ == DragMode::GradientStart || drag_mode_ == DragMode::GradientEnd ||
            drag_mode_ == DragMode::GradientCenter || drag_mode_ == DragMode::GradientRadius ||
            drag_mode_ == DragMode::GradientFocal)
            drag_mode_ = DragMode::None;
    }
    update();
}

void CanvasPreview::fit_canvas(bool up_to_100)
{
    fit_zoom_active_ = true;
    fit_zoom_up_to_100_ = up_to_100;
    pan_offset_ = QPointF(0, 0);
    invalidate_canvas_overlay_caches();
    double scale = fit_scale();
    if (up_to_100) scale = std::min(scale, 1.0);
    /* A fitted canvas must never round above the available viewport. Even a
     * one-percent overshoot can put an edge underneath the rulers. */
    int next_percent = std::clamp((int)std::floor(scale * 100.0), 5, 1600);
    if (zoom_percent_ != next_percent) {
        zoom_percent_ = next_percent;
        emit zoom_percent_changed(zoom_percent_);
    }
    update();
}

bool CanvasPreview::layer_is_editor_visible(const Layer &layer) const
{
    if (!layer_type_is_transition_input(layer.type))
        return true;
    return title_ && title_->graphic_type == TitleGraphicType::Stinger &&
           title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation;
}

std::shared_ptr<Layer> CanvasPreview::selected_layer() const
{
    auto layer = title_ ? title_->find_layer(sel_layer_id_) : nullptr;
    return layer && layer_is_editor_visible(*layer) ? layer : nullptr;
}

std::vector<std::shared_ptr<Layer>> CanvasPreview::selected_layers() const
{
    std::vector<std::shared_ptr<Layer>> layers;
    if (!title_) return layers;
    if (selected_layer_ids_.empty()) {
        if (auto layer = selected_layer()) layers.push_back(layer);
        return layers;
    }
    std::set<std::string> seen;
    for (const auto &id : selected_layer_ids_) {
        if (!seen.insert(id).second) continue;
        auto layer = title_->find_layer(id);
        if (layer && layer_is_editor_visible(*layer)) layers.push_back(layer);
    }
    return layers;
}

static const Layer *editor_find_layer_by_id(const std::shared_ptr<Title> &title,
                                            const std::string &id);
static QTransform editor_layer_world_transform(const std::shared_ptr<Title> &title,
                                               const Layer &layer, double playhead,
                                               int depth = 0);
static bool editor_layer_has_ancestor(const std::shared_ptr<Title> &title,
                                      const Layer &layer,
                                      const std::string &ancestor_id);
static double editor_asset_resolved_time(const std::shared_ptr<Title> &title,
                                         const Layer &layer, double playhead);

QRectF CanvasPreview::layer_local_rect(const Layer &layer) const
{
    if (layer.type == LayerType::Asset && layer.asset_animated) {
        /* Animated Asset Layers keep one fixed selection envelope: the union
         * of every frame of the source animation captured at insertion time.
         * The artwork animates inside this box, but the box itself never
         * follows the current playhead frame. */
        const double width = std::max(1.0, static_cast<double>(layer.rect_width));
        const double height = std::max(1.0, static_cast<double>(layer.rect_height));
        const double origin_x = std::clamp(
            static_cast<double>(layer.origin_x), 0.0, 1.0);
        const double origin_y = std::clamp(
            static_cast<double>(layer.origin_y), 0.0, 1.0);
        return QRectF(-origin_x * width, -origin_y * height, width, height);
    }

    if (layer_type_is_container(layer.type) && title_) {
        bool invertible = false;
        const QTransform group_world =
            editor_layer_world_transform(projection_title(), layer, playhead_, 0);
        const QTransform canvas_to_group = group_world.inverted(&invertible);
        QRectF group_bounds;
        bool have_bounds = false;
        if (invertible) {
            for (const auto &candidate : title_->layers) {
                if (!candidate || layer_type_is_container(candidate->type) ||
                    !candidate->visible ||
                    !editor_layer_has_ancestor(title_, *candidate, layer.id))
                    continue;
                const double candidate_time = editor_asset_resolved_time(title_, *candidate, playhead_);
                if (candidate_time < candidate->in_time || candidate_time > candidate->out_time)
                    continue;
                const double child_time = candidate_time - candidate->in_time;
                const double width = eval_box_width(*candidate, child_time);
                const double height = eval_box_height(*candidate, child_time);
                if (width <= 0.0 || height <= 0.0)
                    continue;
                const double origin_x = eval_origin_x(*candidate, child_time);
                const double origin_y = eval_origin_y(*candidate, child_time);
                const QRectF child_rect(-origin_x * width, -origin_y * height,
                                        width, height);
                const QTransform child_world =
                    editor_layer_world_transform(projection_title(), *candidate, playhead_, 0);
                QPolygonF polygon;
                polygon << canvas_to_group.map(child_world.map(child_rect.topLeft()))
                        << canvas_to_group.map(child_world.map(child_rect.topRight()))
                        << canvas_to_group.map(child_world.map(child_rect.bottomRight()))
                        << canvas_to_group.map(child_world.map(child_rect.bottomLeft()));
                const QRectF child_bounds = polygon.boundingRect();
                if (!child_bounds.isValid() || child_bounds.isEmpty())
                    continue;
                group_bounds = have_bounds ? group_bounds.united(child_bounds) : child_bounds;
                have_bounds = true;
            }
        }
        if (have_bounds)
            return group_bounds.normalized();
        return QRectF(-16.0, -16.0, 32.0, 32.0);
    }

    double lt = editor_asset_resolved_time(title_, layer, playhead_) - layer.in_time;
    double w = eval_box_width(layer, lt);
    double h = eval_box_height(layer, lt);
    double ox = eval_origin_x(layer, lt);
    double oy = eval_origin_y(layer, lt);
    return QRectF(-ox * w, -oy * h, w, h);
}

double CanvasPreview::fit_scale() const
{
    if (!title_ || title_->width <= 0 || title_->height <= 0) return 1.0;
    const double ruler_inset = rulers_visible_ ? kCanvasRulerThickness : 0.0;
    const double available_width = std::max(1.0, (double)width() - ruler_inset * 2.0);
    const double available_height = std::max(1.0, (double)height() - ruler_inset * 2.0);
    return std::min(available_width / title_->width,
                    available_height / title_->height);
}

double CanvasPreview::view_scale() const
{
    return std::max(0.05, (double)zoom_percent_ / 100.0);
}

QPointF CanvasPreview::centered_view_origin() const
{
    if (!title_) return QPointF(0, 0);
    double scale = view_scale();
    const double ruler_inset = rulers_visible_ ? kCanvasRulerThickness : 0.0;
    const double available_width = std::max(1.0, (double)width() - ruler_inset * 2.0);
    const double available_height = std::max(1.0, (double)height() - ruler_inset * 2.0);
    return QPointF(ruler_inset + (available_width - title_->width * scale) / 2.0,
                   ruler_inset + (available_height - title_->height * scale) / 2.0);
}

QPointF CanvasPreview::view_origin() const
{
    return centered_view_origin() + pan_offset_;
}

QPointF CanvasPreview::view_to_canvas(const QPointF &view_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF((view_pt.x() - origin.x()) / scale,
                   (view_pt.y() - origin.y()) / scale);
}

QPointF CanvasPreview::canvas_to_view(const QPointF &canvas_pt) const
{
    double scale = view_scale();
    QPointF origin = view_origin();
    return QPointF(origin.x() + canvas_pt.x() * scale,
                   origin.y() + canvas_pt.y() * scale);
}

static const Layer *editor_find_layer_by_id(const std::shared_ptr<Title> &title, const std::string &id)
{
    if (!title || id.empty()) return nullptr;
    for (const auto &candidate : title->layers) {
        if (candidate && candidate->id == id) return candidate.get();
    }
    return nullptr;
}

static double editor_asset_resolved_time_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting)
{
    if (!title || layer.asset_owner_id.empty() ||
        !visiting.insert(layer.asset_owner_id).second)
        return playhead;
    const Layer *asset = editor_find_layer_by_id(title, layer.asset_owner_id);
    if (!asset || asset->type != LayerType::Asset) {
        visiting.erase(layer.asset_owner_id);
        return playhead;
    }
    const double owner_time = editor_asset_resolved_time_impl(
        title, *asset, playhead, visiting);
    const double resolved = fxm::asset_runtime::resolve_local_time(
        title->id, *asset, owner_time - asset->in_time);
    visiting.erase(layer.asset_owner_id);
    return resolved;
}

static double editor_asset_resolved_time(const std::shared_ptr<Title> &title,
                                         const Layer &layer, double playhead)
{
    std::set<std::string> visiting;
    return editor_asset_resolved_time_impl(title, layer, playhead, visiting);
}

static QTransform editor_layer_local_transform(const std::shared_ptr<Title> &title,
                                                const Layer &layer, double playhead)
{
    QTransform local;
    const double resolved_time = editor_asset_resolved_time(title, layer, playhead);
    const double lt = std::max(0.0, resolved_time - layer.in_time);
    const LayerVector3Value position = evaluated_layer_position_3d(layer, lt);
    local.translate(position.x, position.y);
    local.rotate(layer.rotation.evaluate(lt));
    local.scale(layer.scale.evaluate(lt).x, layer.scale.evaluate(lt).y);
    return local;
}

static std::vector<std::string> editor_group_chain(
    const std::shared_ptr<Title> &title, const std::string &group_parent_id)
{
    std::vector<std::string> chain;
    std::string cursor = group_parent_id;
    std::set<std::string> visited;
    while (!cursor.empty() && visited.insert(cursor).second) {
        const Layer *group = editor_find_layer_by_id(title, cursor);
        if (!group || !layer_type_is_container(group->type))
            break;
        chain.push_back(cursor);
        cursor = group->parent_id;
    }
    return chain;
}

static std::vector<std::string> editor_group_chain_for_layer(
    const std::shared_ptr<Title> &title, const Layer &layer)
{
    std::vector<std::string> chain;
    if (layer_type_is_container(layer.type))
        chain.push_back(layer.id);
    const auto ancestors = editor_group_chain(title, layer.parent_id);
    chain.insert(chain.end(), ancestors.begin(), ancestors.end());
    return chain;
}

static std::string editor_common_group(
    const std::shared_ptr<Title> &title, const std::string &child_group_parent_id,
    const Layer &transform_parent)
{
    const auto child_groups = editor_group_chain(title, child_group_parent_id);
    const auto parent_groups = editor_group_chain_for_layer(title, transform_parent);
    const std::set<std::string> parent_set(parent_groups.begin(), parent_groups.end());
    for (const auto &id : child_groups)
        if (parent_set.find(id) != parent_set.end()) return id;
    return {};
}

static QTransform editor_layer_world_transform_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting);

static QTransform editor_parent_world_transform_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting)
{
    QTransform group_world;
    if (!layer.parent_id.empty()) {
        if (const Layer *group_parent = editor_find_layer_by_id(title, layer.parent_id);
            group_parent && layer_type_is_container(group_parent->type)) {
            group_world = editor_layer_world_transform_impl(
                title, *group_parent, playhead, visiting);
        }
    }
    if (layer.transform_parent_id.empty())
        return group_world;
    const Layer *transform_parent = editor_find_layer_by_id(title, layer.transform_parent_id);
    if (!transform_parent)
        return group_world;
    const QTransform transform_world = editor_layer_world_transform_impl(
        title, *transform_parent, playhead, visiting);
    const std::string common_id = editor_common_group(title, layer.parent_id, *transform_parent);
    if (common_id.empty())
        return group_world * transform_world;
    const Layer *common_group = editor_find_layer_by_id(title, common_id);
    if (!common_group)
        return group_world * transform_world;
    std::set<std::string> common_visiting;
    const QTransform common_world = editor_layer_world_transform_impl(
        title, *common_group, playhead, common_visiting);
    bool invertible = false;
    const QTransform common_inverse = common_world.inverted(&invertible);
    return invertible ? group_world * common_inverse * transform_world
                      : group_world * transform_world;
}

static QTransform editor_layer_parent_bind_transform(const Layer &layer)
{
    if (!layer.parent_bind_enabled)
        return QTransform();
    const auto &m = layer.parent_bind_matrix;
    return QTransform(
        m[0], m[4], 0.0,
        m[1], m[5], 0.0,
        m[3], m[7], 1.0);
}

static QTransform editor_layer_world_transform_impl(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    std::set<std::string> &visiting)
{
    if (!layer.id.empty() && !visiting.insert(layer.id).second)
        return QTransform();
    const QTransform parent_basis =
        editor_parent_world_transform_impl(title, layer, playhead, visiting);
    /* Preserve the original Group hierarchy order: local artwork transform is
     * applied before the Group/transform-parent basis. */
    const QTransform world = editor_layer_local_transform(title, layer, playhead) *
                             editor_layer_parent_bind_transform(layer) * parent_basis;
    if (!layer.id.empty()) visiting.erase(layer.id);
    return world;
}

static QTransform editor_layer_world_transform(const std::shared_ptr<Title> &title,
                                               const Layer &layer, double playhead, int /*depth*/)
{
    std::set<std::string> visiting;
    const QTransform legacy = editor_layer_world_transform_impl(
        title, layer, playhead, visiting);
    return title ? fxm::transform3d::projected_or_legacy(
                       *title, layer, playhead, legacy)
                 : legacy;
}

static QTransform editor_parent_world_transform(const std::shared_ptr<Title> &title,
                                                const Layer &layer, double playhead)
{
    std::set<std::string> visiting;
    return editor_layer_parent_bind_transform(layer) *
           editor_parent_world_transform_impl(title, layer, playhead, visiting);
}

static QPointF editor_parent_local_point_to_canvas(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const QPointF &parent_local_point)
{
    return editor_parent_world_transform(title, layer, playhead)
        .map(parent_local_point);
}

static QPointF editor_canvas_point_to_parent_local(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const QPointF &canvas_point)
{
    const QTransform parent_world =
        editor_parent_world_transform(title, layer, playhead);
    bool invertible = false;
    const QTransform canvas_to_parent = parent_world.inverted(&invertible);
    return invertible ? canvas_to_parent.map(canvas_point) : canvas_point;
}

static QPointF editor_canvas_delta_to_parent_local(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    const QPointF &canvas_delta)
{
    return editor_canvas_point_to_parent_local(
               title, layer, playhead, canvas_delta) -
           editor_canvas_point_to_parent_local(
               title, layer, playhead, QPointF(0.0, 0.0));
}

static QPointF editor_evaluated_local_position_xy(const Layer &layer,
                                                   double local_time)
{
    const LayerVector3Value value = evaluated_layer_position_3d(
        layer, local_time);
    return QPointF(value.x, value.y);
}

static void editor_set_local_position_xy(Layer &layer, double local_time,
                                         const QPointF &value)
{
    if (layer.dimension_mode == LayerDimensionMode::ThreeD ||
        layer.position_3d_path_enabled) {
        promote_layer_position_to_3d_path(layer);
        set_animated_component(layer.position_3d, local_time, 0, value.x());
        set_animated_component(layer.position_3d, local_time, 1, value.y());
        mirror_position_3d_to_legacy(layer, local_time);
        return;
    }
    set_animated_x(layer.position, local_time, value.x());
    set_animated_y(layer.position, local_time, value.y());
}

static double editor_canvas_rotation_delta_to_local(
    const std::shared_ptr<Title> &title, const Layer &layer, double playhead,
    double canvas_delta_degrees)
{
    /* A mirrored parent reverses the visual direction of a local Z rotation.
     * Ordinary rotation and non-uniform scale retain the same signed delta. */
    const double determinant =
        editor_parent_world_transform(title, layer, playhead).determinant();
    return determinant < 0.0 ? -canvas_delta_degrees : canvas_delta_degrees;
}

static bool editor_layer_has_ancestor(const std::shared_ptr<Title> &title,
                                      const Layer &layer,
                                      const std::string &ancestor_id)
{
    std::string parent_id = layer.parent_id;
    int guard = 0;
    while (!parent_id.empty() && guard++ < 64) {
        if (parent_id == ancestor_id)
            return true;
        const Layer *parent = editor_find_layer_by_id(title, parent_id);
        if (!parent)
            break;
        parent_id = parent->parent_id;
    }
    return false;
}

static const Layer *editor_outermost_group_ancestor(
    const std::shared_ptr<Title> &title, const Layer &layer)
{
    const Layer *result = nullptr;
    std::string parent_id = layer.parent_id;
    int guard = 0;
    while (!parent_id.empty() && guard++ < 64) {
        const Layer *parent = editor_find_layer_by_id(title, parent_id);
        if (!parent)
            break;
        if (layer_type_is_container(parent->type))
            result = parent;
        parent_id = parent->parent_id;
    }
    return result;
}

/* Canvas selection follows the visible hierarchy state.  A child remains the
 * direct editing target only when every containing group is in the full
 * children-visible state.  If one or more ancestors are collapsed (including
 * the keyframes-only caret state), the outermost collapsed container owns the
 * click so the complete group is selected and manipulated as one object. */
static const Layer *editor_canvas_selection_target(
    const std::shared_ptr<Title> &title, const Layer &layer)
{
    const Layer *target = &layer;
    std::string parent_id = layer.parent_id;
    int guard = 0;
    while (!parent_id.empty() && guard++ < 64) {
        const Layer *parent = editor_find_layer_by_id(title, parent_id);
        if (!parent)
            break;
        if (parent->type == LayerType::Asset ||
            (parent->type == LayerType::Group && parent->group_collapsed))
            target = parent;
        parent_id = parent->parent_id;
    }
    return target;
}

QPointF CanvasPreview::canvas_to_layer(const Layer &layer, const QPointF &canvas_pt) const
{
    return editor_layer_world_transform(projection_title(), layer, playhead_).inverted().map(canvas_pt);
}

QPointF CanvasPreview::layer_to_canvas(const Layer &layer, const QPointF &layer_pt) const
{
    return editor_layer_world_transform(projection_title(), layer, playhead_).map(layer_pt);
}

static Vec2Value editor_quad_value(const Layer &layer, const AnimatedVec2Property &prop,
                                   float legacy_x, float legacy_y, double playhead)
{
    const double lt = std::clamp(playhead - layer.in_time, 0.0,
                                 std::max(0.0, layer.out_time - layer.in_time));
    if (prop.is_animated() || prop.static_value.x != 0.0 || prop.static_value.y != 0.0)
        return prop.evaluate(lt);
    return {legacy_x, legacy_y};
}

static void editor_set_quad_value(Layer &layer, AnimatedVec2Property &prop,
                                  float &legacy_x, float &legacy_y, double playhead,
                                  const Vec2Value &value)
{
    const double lt = std::clamp(playhead - layer.in_time, 0.0,
                                 std::max(0.0, layer.out_time - layer.in_time));
    legacy_x = static_cast<float>(value.x);
    legacy_y = static_cast<float>(value.y);
    if (prop.is_animated()) {
        auto it = std::find_if(prop.keyframes.begin(), prop.keyframes.end(),
            [lt](const VectorKeyframe &key) { return std::abs(key.time - lt) < 1e-6; });
        if (it != prop.keyframes.end())
            it->value = value;
        else {
            prop.keyframes.push_back({lt, value});
            std::sort(prop.keyframes.begin(), prop.keyframes.end(),
                      [](const VectorKeyframe &a, const VectorKeyframe &b) { return a.time < b.time; });
        }
    } else {
        prop.static_value = value;
    }
}

static QPointF editor_warped_layer_local_point(
    const Layer &layer, const QRectF &rect, const QPointF &layer_point,
    double playhead)
{
    if (!rect.isValid() || rect.width() == 0.0 || rect.height() == 0.0)
        return layer_point;
    const double u = (layer_point.x() - rect.left()) / rect.width();
    const double v = (layer_point.y() - rect.top()) / rect.height();
    const Vec2Value qtl = editor_quad_value(
        layer, layer.transform_quad_tl, layer.transform_quad_tl_x,
        layer.transform_quad_tl_y, playhead);
    const Vec2Value qtr = editor_quad_value(
        layer, layer.transform_quad_tr, layer.transform_quad_tr_x,
        layer.transform_quad_tr_y, playhead);
    const Vec2Value qbr = editor_quad_value(
        layer, layer.transform_quad_br, layer.transform_quad_br_x,
        layer.transform_quad_br_y, playhead);
    const Vec2Value qbl = editor_quad_value(
        layer, layer.transform_quad_bl, layer.transform_quad_bl_x,
        layer.transform_quad_bl_y, playhead);
    const QPointF tl = rect.topLeft() +
        QPointF(qtl.x * rect.width(), qtl.y * rect.height());
    const QPointF tr = rect.topRight() +
        QPointF(qtr.x * rect.width(), qtr.y * rect.height());
    const QPointF br = rect.bottomRight() +
        QPointF(qbr.x * rect.width(), qbr.y * rect.height());
    const QPointF bl = rect.bottomLeft() +
        QPointF(qbl.x * rect.width(), qbl.y * rect.height());
    const QPointF top = tl * (1.0 - u) + tr * u;
    const QPointF bottom = bl * (1.0 - u) + br * u;
    return top * (1.0 - v) + bottom * v;
}

QPointF CanvasPreview::transformed_layer_point(const Layer &layer,
                                                const QPointF &layer_pt) const
{
    const QRectF rect = layer_local_rect(layer);
    return layer_to_canvas(layer, editor_warped_layer_local_point(
        layer, rect, layer_pt, playhead_));
}


void CanvasPreview::clear_path_point_selection()
{
    const bool changed = !selected_path_point_indices_.empty() || path_marquee_active_;
    selected_path_point_indices_.clear();
    path_marquee_base_selection_.clear();
    path_marquee_layer_id_.clear();
    path_marquee_active_ = false;
    path_drag_point_index_ = -1;
    path_alt_break_active_ = false;
    path_drag_start_points_.clear();
    if (changed)
        notify_corner_context_changed();
}

QPointF CanvasPreview::path_normalized_to_canvas(const Layer &layer, double x, double y) const
{
    const QRectF rect = layer_local_rect(layer);
    return layer_to_canvas(layer, QPointF(rect.left() + x * rect.width(),
                                          rect.top() + y * rect.height()));
}

QPointF CanvasPreview::path_canvas_to_normalized(const Layer &layer, const QPointF &canvas_pt) const
{
    const QRectF rect = layer_local_rect(layer);
    const QPointF local = canvas_to_layer(layer, canvas_pt);
    return QPointF((local.x() - rect.left()) / std::max(1e-9, rect.width()),
                   (local.y() - rect.top()) / std::max(1e-9, rect.height()));
}

QPointF CanvasPreview::constrain_path_direction(const QPointF &anchor, const QPointF &target,
                                                Qt::KeyboardModifiers modifiers) const
{
    QPointF delta = target - anchor;
    if (!modifiers.testFlag(Qt::ShiftModifier) || QLineF(QPointF(), delta).length() < 1e-9)
        return target;
    const double length = std::hypot(delta.x(), delta.y());
    const double angle = std::atan2(delta.y(), delta.x());
    const double step = 3.14159265358979323846 / 4.0;
    const double snapped_angle = std::round(angle / step) * step;
    return anchor + QPointF(std::cos(snapped_angle) * length,
                            std::sin(snapped_angle) * length);
}

bool CanvasPreview::event(QEvent *ev)
{
    if (read_only_ && ev) {
        switch (ev->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ShortcutOverride:
        case QEvent::ContextMenu:
        case QEvent::Wheel:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease:
            ev->accept();
            return true;
        default:
            break;
        }
    }

#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    if (ev && gpu_display_ && gpu_display_->swapchain) {
        if (ev->type() == QEvent::Show) {
            gpu_display_->enabled = true;
            force_present_pending_ = true;
            update();
        } else if (ev->type() == QEvent::Hide) {
            gpu_display_->enabled = false;
        } else if (ev->type() == QEvent::PaletteChange ||
                   ev->type() == QEvent::ApplicationPaletteChange) {
            gpu_underlay_key_.clear();
            force_present_pending_ = true;
            update();
        } else if (ev->type() == QEvent::ScreenChangeInternal) {
            gpu_display_->update_color_space = true;
            force_present_pending_ = true;
            update();
        }
    }
#endif

    /* The editor owns Ctrl+Z/Ctrl+Shift+Z actions with a WidgetWithChildren
     * shortcut context.  Claim those shortcuts while a Pen path is in
     * progress so the unfinished path uses its own lightweight history
     * instead of changing the already committed title. */
    if (ev && ev->type() == QEvent::ShortcutOverride &&
        active_tool_ == CanvasTool::Pen && pen_path_active_) {
        auto *key_ev = static_cast<QKeyEvent *>(ev);
        if (key_ev->key() == Qt::Key_Z &&
            key_ev->modifiers().testFlag(Qt::ControlModifier)) {
            ev->accept();
            return true;
        }
    }
    return QWidget::event(ev);
}

void CanvasPreview::finish_pen_path(bool closed, bool keep)
{
    if (!pen_path_active_)
        return;
    std::vector<BezierPathPoint> points = pen_path_points_;
    const bool valid = keep && points.size() >= 2;
    pen_path_points_.clear();
    pen_redo_points_.clear();
    pen_path_active_ = false;
    pen_close_pending_ = false;
    pen_drag_point_index_ = -1;
    pen_alt_break_active_ = false;
    pen_ctrl_unequal_active_ = false;
    pen_space_reposition_ = false;
    clear_snap_feedback();
    snap_cursor_visible_ = false;
    final_snap_cursor_visible_ = false;
    update();
    if (valid)
        emit pen_path_finished(points, closed);
}

void CanvasPreview::cancel_pen_path()
{
    finish_pen_path(false, false);
}

void CanvasPreview::undo_pen_path_point()
{
    if (!pen_path_active_ || pen_path_points_.empty())
        return;

    pen_redo_points_.push_back(pen_path_points_.back());
    pen_path_points_.pop_back();
    pen_close_pending_ = false;
    pen_drag_point_index_ = -1;
    pen_alt_break_active_ = false;
    pen_ctrl_unequal_active_ = false;
    pen_space_reposition_ = false;
    clear_snap_feedback();
    snap_cursor_visible_ = false;
    final_snap_cursor_visible_ = false;
    update();
}

void CanvasPreview::redo_pen_path_point()
{
    if (!pen_path_active_ || pen_redo_points_.empty())
        return;

    pen_path_points_.push_back(pen_redo_points_.back());
    pen_redo_points_.pop_back();
    pen_close_pending_ = false;
    pen_drag_point_index_ = -1;
    pen_alt_break_active_ = false;
    pen_ctrl_unequal_active_ = false;
    pen_space_reposition_ = false;
    clear_snap_feedback();
    snap_cursor_visible_ = false;
    final_snap_cursor_visible_ = false;
    update();
}

void CanvasPreview::draw_pen_path_preview(QPainter &p)
{
    if (!pen_path_active_ || pen_path_points_.empty())
        return;

    auto to_view = [this](double x, double y) { return canvas_to_view(QPointF(x, y)); };
    QPainterPath path;
    const auto &points = pen_path_points_;
    path.moveTo(to_view(points.front().x, points.front().y));
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const BezierPathPoint &from = points[i];
        const BezierPathPoint &to = points[i + 1];
        const QPointF from_anchor = to_view(from.x, from.y);
        const QPointF to_anchor = to_view(to.x, to.y);
        const QPointF c1 = from.has_out ? to_view(from.out_x, from.out_y) : from_anchor;
        const QPointF c2 = to.has_in ? to_view(to.in_x, to.in_y) : to_anchor;
        if (from.has_out || to.has_in)
            path.cubicTo(c1, c2, to_anchor);
        else
            path.lineTo(to_anchor);
    }

    if (!pen_close_pending_ && mouse_inside_canvas_ && pen_drag_point_index_ < 0) {
        const BezierPathPoint &last = points.back();
        const QPointF from_anchor = to_view(last.x, last.y);
        const QPointF target = canvas_to_view(shape_draw_current_canvas_);
        const QPointF c1 = last.has_out ? to_view(last.out_x, last.out_y) : from_anchor;
        if (last.has_out)
            path.cubicTo(c1, target, target);
        else
            path.lineTo(target);
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor path_color = editor_canvas_helper_color(TitlePreferences::CanvasHelperColorRole::SelectionBoundingBox);
    p.setPen(QPen(path_color, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    for (size_t i = 0; i < points.size(); ++i) {
        const BezierPathPoint &point = points[i];
        const QPointF anchor = to_view(point.x, point.y);
        p.setPen(QPen(path_color, 1.0));
        p.setBrush(i == 0 ? QColor(255, 215, 80) : QColor(255, 255, 255));
        p.drawRect(QRectF(anchor.x() - 3.5, anchor.y() - 3.5, 7.0, 7.0));
        if (point.has_in) {
            const QPointF handle = to_view(point.in_x, point.in_y);
            p.drawLine(anchor, handle);
            p.setBrush(QColor(255, 255, 255));
            p.drawEllipse(handle, 3.0, 3.0);
        }
        if (point.has_out) {
            const QPointF handle = to_view(point.out_x, point.out_y);
            p.drawLine(anchor, handle);
            p.setBrush(QColor(255, 255, 255));
            p.drawEllipse(handle, 3.0, 3.0);
        }
    }
    p.restore();
}

void CanvasPreview::update_pen_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!pen_path_active_ || pen_drag_point_index_ < 0 ||
        (size_t)pen_drag_point_index_ >= pen_path_points_.size())
        return;
    BezierPathPoint &point = pen_path_points_[(size_t)pen_drag_point_index_];
    const QPointF anchor(point.x, point.y);

    /* Decide whether this is a click or a Bézier drag from the real pointer
     * travel in view coordinates.  Measuring after snapping made a one-pixel
     * hand movement jump to a nearby guide/object and create long handles. */
    const double pointer_drag_px = QLineF(canvas_to_view(anchor), view_pt).length();
    if (pointer_drag_px < kPenBezierActivationDistancePx) {
        /* Closing an existing path must not erase handles already belonging
         * to its first anchor just because the mouse moved a few pixels. */
        if (!pen_close_pending_) {
            point.has_in = false;
            point.has_out = false;
            point.in_x = point.out_x = point.x;
            point.in_y = point.out_y = point.y;
            point.smooth = false;
        }
        pen_alt_break_active_ = false;
        pen_ctrl_unequal_active_ = false;
        update();
        return;
    }

    QPointF target = constrain_path_direction(anchor, view_to_canvas(view_pt), modifiers);
    target = snap_canvas_point(target, true, true, !modifiers.testFlag(Qt::ControlModifier));
    if (modifiers.testFlag(Qt::ShiftModifier))
        target = constrain_path_direction(anchor, target, modifiers);

    const QPointF delta = target - anchor;
    const double handle_length = QLineF(anchor, target).length();

    point.has_out = true;
    point.out_x = target.x();
    point.out_y = target.y();

    if (modifiers.testFlag(Qt::AltModifier) || pen_alt_break_active_) {
        /* Illustrator-style Anchor Point modifier: retain the incoming handle
         * at the position where Alt was first pressed and keep the pair broken
         * for the remainder of this mouse gesture. */
        if (!pen_alt_break_active_) {
            pen_alt_fixed_in_canvas_ = point.has_in
                ? QPointF(point.in_x, point.in_y)
                : anchor - delta;
            pen_alt_break_active_ = true;
        }
        point.has_in = QLineF(anchor, pen_alt_fixed_in_canvas_).length() >=
                       1.0 / std::max(0.1, view_scale());
        point.in_x = pen_alt_fixed_in_canvas_.x();
        point.in_y = pen_alt_fixed_in_canvas_.y();
        point.smooth = false;
        pen_ctrl_unequal_active_ = false;
    } else {
        const QPointF unit = delta / std::max(handle_length, 1e-9);
        double incoming_length = handle_length;
        if (modifiers.testFlag(Qt::ControlModifier)) {
            /* Ctrl/Command keeps the handles collinear but allows unequal
             * lengths, matching Illustrator's smooth-point modifier. */
            if (!pen_ctrl_unequal_active_) {
                incoming_length = point.has_in
                    ? QLineF(anchor, QPointF(point.in_x, point.in_y)).length()
                    : handle_length;
                pen_ctrl_fixed_in_length_ = incoming_length;
                pen_ctrl_unequal_active_ = true;
            }
            incoming_length = pen_ctrl_fixed_in_length_;
        } else {
            pen_ctrl_unequal_active_ = false;
        }
        const QPointF incoming = anchor - unit * incoming_length;
        point.has_in = incoming_length >= 1.0 / std::max(0.1, view_scale());
        point.in_x = incoming.x();
        point.in_y = incoming.y();
        point.smooth = true;
    }
    update();
}

bool CanvasPreview::direct_selection_supported(const Layer &layer) const
{
    return layer.type == LayerType::Shape;
}

bool CanvasPreview::ensure_direct_selection_path(const std::shared_ptr<Layer> &layer)
{
    if (!layer || !direct_selection_supported(*layer))
        return false;
    const bool already_path = layer->shape_type == ShapeType::Path;
    if (!fxm::ensure_editable_path(*layer))
        return false;
    if (!already_path) {
        dirty_ = true;
        emit layer_geometry_changed();
    }
    return true;
}

CanvasPreview::PathHit CanvasPreview::hit_test_direct_path(const Layer &layer, const QPointF &view_pt) const
{
    PathHit result;
    if (layer.shape_type != ShapeType::Path)
        return result;
    constexpr double anchor_radius = 7.0;
    constexpr double handle_radius = 6.0;

    for (int index : selected_path_point_indices_) {
        if (index < 0 || (size_t)index >= layer.path_points.size())
            continue;
        const BezierPathPoint &point = layer.path_points[(size_t)index];
        if (point.has_in) {
            const QPointF handle = canvas_to_view(path_normalized_to_canvas(layer, point.in_x, point.in_y));
            if (QLineF(handle, view_pt).length() <= handle_radius)
                return {DragMode::PathInHandle, index};
        }
        if (point.has_out) {
            const QPointF handle = canvas_to_view(path_normalized_to_canvas(layer, point.out_x, point.out_y));
            if (QLineF(handle, view_pt).length() <= handle_radius)
                return {DragMode::PathOutHandle, index};
        }
    }

    double best = anchor_radius + 1.0;
    for (size_t i = 0; i < layer.path_points.size(); ++i) {
        const BezierPathPoint &point = layer.path_points[i];
        const QPointF anchor = canvas_to_view(path_normalized_to_canvas(layer, point.x, point.y));
        const double distance = QLineF(anchor, view_pt).length();
        if (distance <= anchor_radius && distance < best) {
            best = distance;
            result = {DragMode::PathAnchor, (int)i};
        }
    }
    return result;
}
