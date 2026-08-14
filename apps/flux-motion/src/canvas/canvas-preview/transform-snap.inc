void CanvasPreview::draw_direct_selection_path(QPainter &p, const Layer &layer)
{
    if (active_tool_ != CanvasTool::DirectSelection || layer.shape_type != ShapeType::Path)
        return;
    const QRectF local_rect = layer_local_rect(layer);
    QPainterPath local_path = fxm::layer_shape_path(layer, local_rect);
    QPainterPath canvas_path = editor_layer_world_transform(projection_title(), layer, playhead_).map(local_path);
    QTransform canvas_to_view_transform;
    canvas_to_view_transform.translate(view_origin().x(), view_origin().y());
    canvas_to_view_transform.scale(view_scale(), view_scale());
    const QPainterPath view_path = canvas_to_view_transform.map(canvas_path);

    QColor path_color = editor_canvas_helper_color(TitlePreferences::CanvasHelperColorRole::SelectionBoundingBox);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(path_color, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawPath(view_path);

    for (size_t i = 0; i < layer.path_points.size(); ++i) {
        const BezierPathPoint &point = layer.path_points[i];
        const QPointF anchor = canvas_to_view(path_normalized_to_canvas(layer, point.x, point.y));
        const bool selected = selected_path_point_indices_.find((int)i) != selected_path_point_indices_.end();
        if (selected && (show_selected_path_handles_ || selected_path_point_indices_.size() == 1)) {
            p.setPen(QPen(path_color, 1.0));
            if (point.has_in) {
                const QPointF handle = canvas_to_view(path_normalized_to_canvas(layer, point.in_x, point.in_y));
                p.drawLine(anchor, handle);
                p.setBrush(QColor(255, 255, 255));
                p.drawEllipse(handle, 3.2, 3.2);
            }
            if (point.has_out) {
                const QPointF handle = canvas_to_view(path_normalized_to_canvas(layer, point.out_x, point.out_y));
                p.drawLine(anchor, handle);
                p.setBrush(QColor(255, 255, 255));
                p.drawEllipse(handle, 3.2, 3.2);
            }
        }
        p.setPen(QPen(path_color, 1.0));
        p.setBrush(selected ? path_color : QColor(255, 255, 255));
        p.drawRect(QRectF(anchor.x() - 3.5, anchor.y() - 3.5, 7.0, 7.0));
    }
    p.restore();
}

bool CanvasPreview::select_direct_object_at(const QPointF &view_pt)
{
    /* Direct Selection obeys the same hierarchy visibility contract as the
     * regular cursor. A path inside a collapsed/keyframes-only group must not
     * bypass its container; select the group instead and expose path points
     * only after the group reaches the full children-visible state. */
    if (auto target = layer_at_view_pos(view_pt)) {
        emit layers_selected({target->id});
        selected_layer_ids_ = {target->id};
        sel_layer_id_ = target->id;
        clear_path_point_selection();
        clear_corner_selection();
        if (direct_selection_supported(*target))
            ensure_direct_selection_path(target);
        invalidate_canvas_overlay_caches();
        notify_corner_context_changed();
        update();
        return true;
    }

    clear_path_point_selection();
    clear_corner_selection();
    emit layers_selected({});
    selected_layer_ids_.clear();
    sel_layer_id_.clear();
    invalidate_canvas_overlay_caches();
    notify_corner_context_changed();
    update();
    return true;
}

void CanvasPreview::begin_path_point_marquee(const QPointF &view_pt,
                                             Qt::KeyboardModifiers modifiers)
{
    auto layer = selected_layer();
    if (!layer || !ensure_direct_selection_path(layer) || layer->shape_type != ShapeType::Path)
        return;
    drag_mode_ = DragMode::PathMarquee;
    path_marquee_active_ = true;
    path_marquee_layer_id_ = layer->id;
    path_marquee_base_selection_ = modifiers.testFlag(Qt::ShiftModifier)
        ? selected_path_point_indices_
        : std::set<int>{};
    drag_start_view_ = view_pt;
    drag_current_view_ = view_pt;
    drag_changed_ = false;
    update();
}

void CanvasPreview::update_path_point_marquee(const QPointF &view_pt,
                                              Qt::KeyboardModifiers modifiers)
{
    if (!path_marquee_active_ || drag_mode_ != DragMode::PathMarquee)
        return;
    auto layer = title_ ? title_->find_layer(path_marquee_layer_id_) : nullptr;
    if (!layer || layer->shape_type != ShapeType::Path)
        return;

    drag_current_view_ = view_pt;
    drag_changed_ = QLineF(drag_start_view_, drag_current_view_).length() >= 3.0;
    if (!drag_changed_) {
        selected_path_point_indices_ = path_marquee_base_selection_;
        update();
        return;
    }

    const QRectF marquee(drag_start_view_, drag_current_view_);
    const QRectF normalized = marquee.normalized();
    std::set<int> next = modifiers.testFlag(Qt::ShiftModifier)
        ? path_marquee_base_selection_
        : std::set<int>{};
    for (size_t i = 0; i < layer->path_points.size(); ++i) {
        const BezierPathPoint &point = layer->path_points[i];
        const QPointF anchor_view = canvas_to_view(path_normalized_to_canvas(*layer, point.x, point.y));
        if (normalized.contains(anchor_view))
            next.insert((int)i);
    }
    selected_path_point_indices_ = std::move(next);
    clear_corner_selection(false);
    selected_corner_layer_id_ = layer->id;
    const auto corners = fxm::layer_live_corners(*layer, layer_local_rect(*layer));
    for (const auto &corner : corners) {
        if (selected_path_point_indices_.find(corner.point_index) != selected_path_point_indices_.end())
            selected_corner_indices_.insert(corner.point_index);
    }
    if (selected_corner_indices_.empty())
        selected_corner_layer_id_.clear();
    invalidate_canvas_overlay_caches();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::finish_path_point_marquee(const QPointF &view_pt,
                                              Qt::KeyboardModifiers modifiers)
{
    if (!path_marquee_active_)
        return;
    update_path_point_marquee(view_pt, modifiers);
    const bool moved = drag_changed_;
    path_marquee_active_ = false;
    path_marquee_layer_id_.clear();
    path_marquee_base_selection_.clear();
    drag_mode_ = DragMode::None;
    drag_changed_ = false;
    if (!moved)
        select_direct_object_at(view_pt);
    else
        notify_corner_context_changed();
    update();
}

bool CanvasPreview::begin_direct_selection(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    auto layer = selected_layer();
    if (layer && direct_selection_supported(*layer)) {
        ensure_direct_selection_path(layer);
        const PathHit hit = hit_test_direct_path(*layer, view_pt);
        if (hit.mode != DragMode::None) {
            clear_corner_selection();
            if (hit.mode == DragMode::PathAnchor) {
                if (modifiers.testFlag(Qt::ShiftModifier)) {
                    auto it = selected_path_point_indices_.find(hit.point_index);
                    if (it == selected_path_point_indices_.end())
                        selected_path_point_indices_.insert(hit.point_index);
                    else
                        selected_path_point_indices_.erase(it);
                } else if (selected_path_point_indices_.find(hit.point_index) == selected_path_point_indices_.end()) {
                    selected_path_point_indices_.clear();
                    selected_path_point_indices_.insert(hit.point_index);
                }
            } else {
                selected_path_point_indices_.insert(hit.point_index);
            }
            if (hit.mode == DragMode::PathAnchor) {
                selected_corner_layer_id_ = layer->id;
                selected_corner_indices_.clear();
                const auto live_corners = fxm::layer_live_corners(*layer, layer_local_rect(*layer));
                for (const auto &corner : live_corners) {
                    if (selected_path_point_indices_.find(corner.point_index) != selected_path_point_indices_.end())
                        selected_corner_indices_.insert(corner.point_index);
                }
                if (selected_corner_indices_.empty())
                    selected_corner_layer_id_.clear();
            }
            notify_corner_context_changed();
            drag_mode_ = hit.mode;
            path_drag_point_index_ = hit.point_index;
            path_alt_break_active_ = false;
            path_drag_start_points_ = layer->path_points;
            drag_start_view_ = view_pt;
            drag_current_view_ = view_pt;
            drag_start_canvas_ = view_to_canvas(view_pt);
            drag_changed_ = false;
            begin_adaptive_interaction();
            update();
            return true;
        }

        /* Illustrator-style Direct Selection marquee. Once a path is selected,
         * dragging from empty canvas selects any anchor points enclosed by the
         * rectangle. Shift-drag adds to the existing point selection. */
        begin_path_point_marquee(view_pt, modifiers);
        if (path_marquee_active_)
            return true;
    }
    return select_direct_object_at(view_pt);
}

void CanvasPreview::apply_direct_path_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    auto layer = selected_layer();
    if (!layer || layer->shape_type != ShapeType::Path || path_drag_point_index_ < 0 ||
        path_drag_start_points_.size() != layer->path_points.size())
        return;

    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    if (drag_mode_ == DragMode::PathAnchor) {
        const BezierPathPoint &active_start = path_drag_start_points_[(size_t)path_drag_point_index_];
        const QPointF active_anchor = path_normalized_to_canvas(*layer, active_start.x, active_start.y);
        QPointF target = active_anchor + (view_to_canvas(view_pt) - drag_start_canvas_);
        target = constrain_path_direction(active_anchor, target, modifiers);
        target = snap_canvas_point(target, true, true, allow_snap);
        if (modifiers.testFlag(Qt::ShiftModifier))
            target = constrain_path_direction(active_anchor, target, modifiers);
        QPointF delta = target - active_anchor;

        layer->path_points = path_drag_start_points_;
        for (int index : selected_path_point_indices_) {
            if (index < 0 || (size_t)index >= layer->path_points.size()) continue;
            const BezierPathPoint &start = path_drag_start_points_[(size_t)index];
            BezierPathPoint &point = layer->path_points[(size_t)index];
            const QPointF anchor = path_canvas_to_normalized(*layer,
                path_normalized_to_canvas(*layer, start.x, start.y) + delta);
            point.x = anchor.x(); point.y = anchor.y();
            if (start.has_in) {
                const QPointF handle = path_canvas_to_normalized(*layer,
                    path_normalized_to_canvas(*layer, start.in_x, start.in_y) + delta);
                point.in_x = handle.x(); point.in_y = handle.y();
            }
            if (start.has_out) {
                const QPointF handle = path_canvas_to_normalized(*layer,
                    path_normalized_to_canvas(*layer, start.out_x, start.out_y) + delta);
                point.out_x = handle.x(); point.out_y = handle.y();
            }
        }
    } else {
        layer->path_points = path_drag_start_points_;
        BezierPathPoint &point = layer->path_points[(size_t)path_drag_point_index_];
        const BezierPathPoint &start = path_drag_start_points_[(size_t)path_drag_point_index_];
        const QPointF anchor_canvas = path_normalized_to_canvas(*layer, start.x, start.y);
        QPointF target_canvas = constrain_path_direction(anchor_canvas, view_to_canvas(view_pt), modifiers);
        target_canvas = snap_canvas_point(target_canvas, true, true, allow_snap);
        if (modifiers.testFlag(Qt::ShiftModifier))
            target_canvas = constrain_path_direction(anchor_canvas, target_canvas, modifiers);
        const QPointF normalized = path_canvas_to_normalized(*layer, target_canvas);
        const bool dragging_in = drag_mode_ == DragMode::PathInHandle;
        if (dragging_in) {
            point.has_in = true;
            point.in_x = normalized.x(); point.in_y = normalized.y();
        } else {
            point.has_out = true;
            point.out_x = normalized.x(); point.out_y = normalized.y();
        }

        if (modifiers.testFlag(Qt::AltModifier))
            path_alt_break_active_ = true;
        if (start.smooth && !path_alt_break_active_) {
            const QPointF vector = target_canvas - anchor_canvas;
            const double length = std::hypot(vector.x(), vector.y());
            if (length > 1e-9) {
                const QPointF unit = vector / length;
                const QPointF opposite_start = dragging_in
                    ? path_normalized_to_canvas(*layer, start.out_x, start.out_y)
                    : path_normalized_to_canvas(*layer, start.in_x, start.in_y);
                const double opposite_length = QLineF(anchor_canvas, opposite_start).length();
                const QPointF opposite_canvas = anchor_canvas - unit * opposite_length;
                const QPointF opposite = path_canvas_to_normalized(*layer, opposite_canvas);
                if (dragging_in && start.has_out) {
                    point.out_x = opposite.x(); point.out_y = opposite.y();
                } else if (!dragging_in && start.has_in) {
                    point.in_x = opposite.x(); point.in_y = opposite.y();
                }
                point.smooth = true;
            }
        } else if (path_alt_break_active_) {
            point.smooth = false;
        }
    }

    drag_changed_ = true;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::reframe_custom_path(Layer &layer, const std::vector<QPointF> &anchors_local,
                                        const std::vector<QPointF> &in_handles_local,
                                        const std::vector<QPointF> &out_handles_local,
                                        const std::vector<bool> &has_in,
                                        const std::vector<bool> &has_out)
{
    if (anchors_local.size() < 2 || anchors_local.size() != layer.path_points.size() ||
        in_handles_local.size() != anchors_local.size() ||
        out_handles_local.size() != anchors_local.size() ||
        has_in.size() != anchors_local.size() || has_out.size() != anchors_local.size())
        return;
    QPainterPath geometry;
    geometry.setFillRule(Qt::OddEvenFill);
    const auto subpaths = fxm::path_subpath_ranges(layer);
    for (const auto &[start, end] : subpaths) {
        if (end <= start)
            continue;
        geometry.moveTo(anchors_local[start]);
        const size_t point_count = end - start;
        const size_t segment_count = layer.path_closed ? point_count : point_count - 1;
        for (size_t local_index = 0; local_index < segment_count; ++local_index) {
            const size_t i = start + local_index;
            const size_t next_index = start + ((local_index + 1) % point_count);
            const QPointF control1 = has_out[i] ? out_handles_local[i] : anchors_local[i];
            const QPointF control2 = has_in[next_index] ? in_handles_local[next_index] : anchors_local[next_index];
            if (has_out[i] || has_in[next_index])
                geometry.cubicTo(control1, control2, anchors_local[next_index]);
            else
                geometry.lineTo(anchors_local[next_index]);
        }
        if (layer.path_closed)
            geometry.closeSubpath();
    }

    QRectF bounds = geometry.boundingRect();
    if (bounds.width() < 1.0) {
        const double center_x = bounds.center().x();
        bounds.setLeft(center_x - 0.5);
        bounds.setWidth(1.0);
    }
    if (bounds.height() < 1.0) {
        const double center_y = bounds.center().y();
        bounds.setTop(center_y - 0.5);
        bounds.setHeight(1.0);
    }
    const double width = bounds.width();
    const double height = bounds.height();

    const QTransform old_world = editor_layer_world_transform(projection_title(), layer, playhead_);
    const QPointF center_canvas = old_world.map(bounds.center());
    const QTransform parent_world = editor_parent_world_transform(title_, layer, playhead_);
    const QPointF new_position = parent_world.inverted().map(center_canvas);
    const double lt = std::clamp(playhead_ - layer.in_time, 0.0,
                                 std::max(0.0, layer.out_time - layer.in_time));
    if (layer.dimension_mode == LayerDimensionMode::ThreeD ||
        layer.position_3d_path_enabled) {
        promote_layer_position_to_3d_path(layer);
        const Vec3Value current = layer.position_3d.evaluate(lt);
        set_animated_value(layer.position_3d, lt,
                           {new_position.x(), new_position.y(), current.z});
        mirror_position_3d_to_legacy(layer, lt);
    } else {
        Vec2Value next_position = layer.position.evaluate(lt);
        next_position.x = new_position.x();
        next_position.y = new_position.y();
        set_animated_value(layer.position, lt, next_position);
    }
    Vec2Value next_size = layer.size.evaluate(lt);
    next_size.x = width;
    next_size.y = height;
    set_animated_value(layer.size, lt, next_size);
    layer.rect_width = (float)width;
    layer.rect_height = (float)height;
    layer.origin_x = 0.5f;
    layer.origin_y = 0.5f;
    Vec2Value next_origin = layer.origin_prop.evaluate(lt);
    next_origin.x = 0.5;
    next_origin.y = 0.5;
    set_animated_value(layer.origin_prop, lt, next_origin);

    for (size_t i = 0; i < layer.path_points.size(); ++i) {
        BezierPathPoint &point = layer.path_points[i];
        auto normalize = [&](const QPointF &value) {
            return QPointF((value.x() - bounds.left()) / width,
                           (value.y() - bounds.top()) / height);
        };
        const QPointF anchor = normalize(anchors_local[i]);
        point.x = anchor.x(); point.y = anchor.y();
        if (has_in[i]) {
            const QPointF handle = normalize(in_handles_local[i]);
            point.in_x = handle.x(); point.in_y = handle.y();
        } else {
            point.in_x = point.x; point.in_y = point.y;
        }
        if (has_out[i]) {
            const QPointF handle = normalize(out_handles_local[i]);
            point.out_x = handle.x(); point.out_y = handle.y();
        } else {
            point.out_x = point.x; point.out_y = point.y;
        }
    }
}

bool CanvasPreview::nudge_selected_path_points(double dx, double dy)
{
    auto layer = selected_layer();
    if (!layer || layer->shape_type != ShapeType::Path || selected_path_point_indices_.empty())
        return false;

    const QRectF rect = layer_local_rect(*layer);
    std::vector<QPointF> anchors, in_handles, out_handles;
    std::vector<bool> has_in, has_out;
    anchors.reserve(layer->path_points.size());
    in_handles.reserve(layer->path_points.size());
    out_handles.reserve(layer->path_points.size());
    has_in.reserve(layer->path_points.size());
    has_out.reserve(layer->path_points.size());

    const QPointF canvas_delta(dx, dy);
    for (size_t i = 0; i < layer->path_points.size(); ++i) {
        const BezierPathPoint &point = layer->path_points[i];
        QPointF anchor = fxm::path_point_to_local(point, rect);
        QPointF in_handle = fxm::path_in_handle_to_local(point, rect);
        QPointF out_handle = fxm::path_out_handle_to_local(point, rect);
        if (selected_path_point_indices_.find((int)i) != selected_path_point_indices_.end()) {
            anchor = canvas_to_layer(*layer, layer_to_canvas(*layer, anchor) + canvas_delta);
            if (point.has_in)
                in_handle = canvas_to_layer(*layer, layer_to_canvas(*layer, in_handle) + canvas_delta);
            if (point.has_out)
                out_handle = canvas_to_layer(*layer, layer_to_canvas(*layer, out_handle) + canvas_delta);
        }
        anchors.push_back(anchor);
        in_handles.push_back(in_handle);
        out_handles.push_back(out_handle);
        has_in.push_back(point.has_in);
        has_out.push_back(point.has_out);
    }

    reframe_custom_path(*layer, anchors, in_handles, out_handles, has_in, has_out);
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    update();
    return true;
}

void CanvasPreview::delete_selected_path_points()
{
    auto layer = selected_layer();
    if (!layer || layer->shape_type != ShapeType::Path || selected_path_point_indices_.empty())
        return;

    std::vector<BezierPathPoint> remaining;
    remaining.reserve(layer->path_points.size());
    const size_t minimum = layer->path_closed ? 3 : 2;
    for (const auto &[start, end] : fxm::path_subpath_ranges(*layer)) {
        const size_t contour_start = remaining.size();
        for (size_t i = start; i < end; ++i) {
            if (selected_path_point_indices_.find((int)i) == selected_path_point_indices_.end())
                remaining.push_back(layer->path_points[i]);
        }
        const size_t contour_size = remaining.size() - contour_start;
        if (contour_size < minimum) {
            remaining.resize(contour_start);
            continue;
        }
        remaining[contour_start].starts_subpath = contour_start != 0;
    }

    if (remaining.empty()) {
        const std::string id = layer->id;
        title_->remove_layer(id);
        selected_layer_ids_.clear();
        sel_layer_id_.clear();
        clear_path_point_selection();
        emit layers_selected({});
        emit layer_structure_changed();
    } else {
        layer->path_points = std::move(remaining);
        const QRectF rect = layer_local_rect(*layer);
        std::vector<QPointF> anchors, in_handles, out_handles;
        std::vector<bool> has_in, has_out;
        anchors.reserve(layer->path_points.size());
        in_handles.reserve(layer->path_points.size());
        out_handles.reserve(layer->path_points.size());
        has_in.reserve(layer->path_points.size());
        has_out.reserve(layer->path_points.size());
        for (const auto &point : layer->path_points) {
            anchors.push_back(fxm::path_point_to_local(point, rect));
            in_handles.push_back(fxm::path_in_handle_to_local(point, rect));
            out_handles.push_back(fxm::path_out_handle_to_local(point, rect));
            has_in.push_back(point.has_in);
            has_out.push_back(point.has_out);
        }
        reframe_custom_path(*layer, anchors, in_handles, out_handles, has_in, has_out);
        clear_path_point_selection();
        dirty_ = true;
        emit layer_geometry_changed();
    }
    invalidate_canvas_overlay_caches();
    update();
}

QRectF CanvasPreview::layer_canvas_bounds(const Layer &layer) const
{
    QRectF r = layer_local_rect(layer);
    const QPointF corners[] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};

    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();

    for (const QPointF &corner : corners) {
        QPointF canvas = layer_to_canvas(layer, corner);
        min_x = std::min(min_x, canvas.x());
        min_y = std::min(min_y, canvas.y());
        max_x = std::max(max_x, canvas.x());
        max_y = std::max(max_y, canvas.y());
    }

    if (!std::isfinite(min_x) || !std::isfinite(min_y) ||
        !std::isfinite(max_x) || !std::isfinite(max_y))
        return QRectF();

    return QRectF(QPointF(min_x, min_y), QPointF(max_x, max_y)).normalized();
}

QRectF CanvasPreview::selected_canvas_bounds() const
{
    QRectF bounds;
    bool have_bounds = false;
    for (auto &layer : selected_layers()) {
        if (!layer || !layer->visible || layer->type == LayerType::Audio ||
            layer->type == LayerType::Empty)
            continue;
        QRectF layer_bounds = layer_canvas_bounds(*layer);
        if (!layer_bounds.isValid()) continue;
        if (!have_bounds) {
            bounds = layer_bounds;
            have_bounds = true;
        } else {
            bounds = bounds.united(layer_bounds);
        }
    }
    return bounds.normalized();
}

void CanvasPreview::invalidate_selection_overlay_cache() const
{
    selection_overlay_cache_valid_ = false;
    selection_overlay_cache_ = SelectionOverlayGeometry{};
}

void CanvasPreview::invalidate_hover_overlay_cache() const
{
    hover_overlay_cache_valid_ = false;
    hover_overlay_cache_ = HoverOverlayGeometry{};
}

void CanvasPreview::invalidate_canvas_overlay_caches() const
{
    invalidate_selection_overlay_cache();
    invalidate_hover_overlay_cache();
    gizmo_geometry_cache_valid_ = false;
    gizmo_geometry_cache_ = GizmoGeometry{};
    gpu_overlay_dirty_ = true;
}

bool CanvasPreview::layer_overlay_changes_with_playhead(const Layer &layer) const
{
    auto transform_is_animated = [](const Layer &candidate) {
        return candidate.position.is_animated() || candidate.position_z.is_animated() ||
            candidate.scale.is_animated() || candidate.scale_z.is_animated() ||
            candidate.rotation.is_animated() || candidate.rotation_x.is_animated() ||
            candidate.rotation_y.is_animated() || candidate.anchor_z.is_animated() ||
            candidate.orientation_x.is_animated() || candidate.orientation_y.is_animated() ||
            candidate.orientation_z.is_animated() || candidate.size.is_animated() ||
            candidate.origin_prop.is_animated();
    };
    if ((title_ && fxm::transform3d::title_camera_has_animation(*title_)) ||
        transform_is_animated(layer))
        return true;

    if (layer_type_is_container(layer.type) && title_) {
        for (const auto &candidate : title_->layers) {
            if (!candidate || !editor_layer_has_ancestor(title_, *candidate, layer.id))
                continue;
            if (transform_is_animated(*candidate))
                return true;
        }
    }

    std::vector<std::string> pending;
    if (!layer.parent_id.empty()) pending.push_back(layer.parent_id);
    if (!layer.transform_parent_id.empty()) pending.push_back(layer.transform_parent_id);
    std::set<std::string> visited_parents;
    while (!pending.empty()) {
        const std::string parent_id = pending.back();
        pending.pop_back();
        if (parent_id.empty() || !visited_parents.insert(parent_id).second)
            continue;
        const Layer *parent = editor_find_layer_by_id(title_, parent_id);
        if (!parent) continue;
        if (transform_is_animated(*parent))
            return true;
        if (!parent->parent_id.empty()) pending.push_back(parent->parent_id);
        if (!parent->transform_parent_id.empty()) pending.push_back(parent->transform_parent_id);
    }

    return false;
}

bool CanvasPreview::overlay_visibility_crosses_playhead_boundary(const Layer &layer,
                                                                 double old_playhead,
                                                                 double new_playhead) const
{
    const bool was_active = old_playhead >= layer.in_time && old_playhead <= layer.out_time;
    const bool is_active = new_playhead >= layer.in_time && new_playhead <= layer.out_time;
    return was_active != is_active;
}

bool CanvasPreview::canvas_overlay_changes_with_playhead(double old_playhead, double new_playhead) const
{
    if (!title_)
        return false;

    for (const auto &layer : selected_layers()) {
        if (!layer)
            continue;
        if (overlay_visibility_crosses_playhead_boundary(*layer, old_playhead, new_playhead) ||
            layer_overlay_changes_with_playhead(*layer))
            return true;
    }

    if (!hovered_layer_id_.empty()) {
        if (const Layer *hovered = editor_find_layer_by_id(title_, hovered_layer_id_)) {
            if (overlay_visibility_crosses_playhead_boundary(*hovered, old_playhead, new_playhead) ||
                layer_overlay_changes_with_playhead(*hovered))
                return true;
        } else {
            return true;
        }
    }

    return false;
}

const CanvasPreview::SelectionOverlayGeometry &CanvasPreview::selection_overlay_geometry() const
{
    if (selection_overlay_cache_valid_)
        return selection_overlay_cache_;

    SelectionOverlayGeometry geometry;
    const auto layers = selected_layers();
    geometry.valid = true;
    geometry.layers.reserve(layers.size());

    auto make_geometry = [&](const Layer &layer, bool editing_text_layer) {
        SelectionOverlayLayerGeometry layer_geometry;
        layer_geometry.layer = &layer;
        layer_geometry.editing_text_layer = editing_text_layer;
        const QRectF box = layer_local_rect(layer);
        const auto projected_title = projection_title();
        const bool projected_3d = projected_title &&
            fxm::transform3d::layer_or_ancestor_uses_3d(
                *projected_title, layer);
        const qreal overlay_pixel_scale =
            std::max<qreal>(1.0, devicePixelRatioF());
        auto quantize_overlay = [&](QPointF view) {
            if (projected_3d) {
                view.setX(std::round(view.x() * overlay_pixel_scale) /
                          overlay_pixel_scale);
                view.setY(std::round(view.y() * overlay_pixel_scale) /
                          overlay_pixel_scale);
            }
            return view;
        };
        auto legacy_layer_point_to_view = [&](const QPointF &pt) {
            return quantize_overlay(canvas_to_view(
                transformed_layer_point(layer, pt)));
        };
        auto projected_layer_point_to_view = [&](const QPointF &pt,
                                                  QPointF &view) {
            if (!projected_3d || !projected_title) {
                view = legacy_layer_point_to_view(pt);
                return true;
            }
            const QPointF warped = editor_warped_layer_local_point(
                layer, box, pt, playhead_);
            QVector4D clip;
            if (!fxm::transform3d::layer_local_clip_point(
                    *projected_title, layer, playhead_,
                    QVector3D(static_cast<float>(warped.x()),
                              static_cast<float>(warped.y()), 0.0f), clip))
                return false;
            const double w = static_cast<double>(clip.w());
            if (w <= 1.0e-8)
                return false;
            const double ndc_x = static_cast<double>(clip.x()) / w;
            const double ndc_y = static_cast<double>(clip.y()) / w;
            const double ndc_z = static_cast<double>(clip.z()) / w;
            if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) ||
                !std::isfinite(ndc_z) || ndc_x < -1.000001 ||
                ndc_x > 1.000001 || ndc_y < -1.000001 ||
                ndc_y > 1.000001 || ndc_z < -1.000001 ||
                ndc_z > 1.000001)
                return false;
            const QPointF canvas_point(
                (ndc_x + 1.0) * 0.5 *
                    std::max(1, projected_title->width),
                (1.0 - ndc_y) * 0.5 *
                    std::max(1, projected_title->height));
            view = quantize_overlay(canvas_to_view(canvas_point));
            return true;
        };

        if (projected_3d && projected_title) {
            QPolygonF warped_local_polygon;
            warped_local_polygon
                << editor_warped_layer_local_point(
                       layer, box, box.topLeft(), playhead_)
                << editor_warped_layer_local_point(
                       layer, box, box.topRight(), playhead_)
                << editor_warped_layer_local_point(
                       layer, box, box.bottomRight(), playhead_)
                << editor_warped_layer_local_point(
                       layer, box, box.bottomLeft(), playhead_);
            QRectF clipped_bounds;
            QPolygonF clipped_canvas_polygon;
            if (fxm::transform3d::projected_local_polygon(
                    *projected_title, layer, playhead_,
                    warped_local_polygon, clipped_bounds,
                    &clipped_canvas_polygon)) {
                for (const QPointF &point : clipped_canvas_polygon)
                    layer_geometry.outline <<
                        quantize_overlay(canvas_to_view(point));
            }
        } else {
            layer_geometry.outline
                << legacy_layer_point_to_view(box.topLeft())
                << legacy_layer_point_to_view(box.topRight())
                << legacy_layer_point_to_view(box.bottomRight())
                << legacy_layer_point_to_view(box.bottomLeft());
        }

        const QPointF handle_points[] = {
            box.topLeft(), QPointF(box.center().x(), box.top()),
            box.topRight(), QPointF(box.right(), box.center().y()),
            box.bottomRight(), QPointF(box.center().x(), box.bottom()),
            box.bottomLeft(), QPointF(box.left(), box.center().y())
        };
        for (int index = 0; index < 8; ++index) {
            layer_geometry.handle_visible[index] =
                projected_layer_point_to_view(
                    handle_points[index], layer_geometry.handles[index]);
        }
        layer_geometry.origin_visible = projected_layer_point_to_view(
            QPointF(0.0, 0.0), layer_geometry.origin);
        if (layer.type == LayerType::Light) {
            /* Light layers are spatial objects, not resizable artwork. Keep
             * their projected object hit area for selection, but expose no
             * bounding-box handles or artwork origin control. */
            for (bool &visible : layer_geometry.handle_visible) visible = false;
            layer_geometry.origin_visible = false;
        }
        return layer_geometry;
    };

    std::set<std::string> selected_ids;
    for (const auto &layer : layers) {
        if (!layer || layer->type == LayerType::Audio ||
            layer->type == LayerType::Empty)
            continue;
        selected_ids.insert(layer->id);
        geometry.layers.push_back(make_geometry(
            *layer, !inline_text_layer_id_.empty() &&
                        inline_text_layer_id_ == layer->id &&
                        is_canvas_text_layer(*layer)));
    }

    /* When drilling into a group, retain the immediate container outline as
     * context but never expose its transform handles. This mirrors AE's group
     * context while making it clear which child is currently editable. */
    std::set<std::string> context_group_ids;
    for (const auto &layer : layers) {
        if (!layer || layer->parent_id.empty())
            continue;
        const auto parent = title_ ? title_->find_layer(layer->parent_id) : nullptr;
        if (!parent || !layer_type_is_container(parent->type) ||
            selected_ids.find(parent->id) != selected_ids.end() ||
            !context_group_ids.insert(parent->id).second)
            continue;
        geometry.group_contexts.push_back(make_geometry(*parent, false));
    }

    if (geometry.layers.size() > 1) {
        QRectF view_bounds;
        bool have_view_bounds = false;
        for (const SelectionOverlayLayerGeometry &layer_geometry :
             geometry.layers) {
            if (!layer_geometry.layer ||
                layer_geometry.layer->type == LayerType::Light ||
                layer_geometry.outline.size() < 3)
                continue;
            const QRectF candidate =
                layer_geometry.outline.boundingRect().normalized();
            view_bounds = have_view_bounds
                ? view_bounds.united(candidate) : candidate;
            have_view_bounds = true;
        }
        if (have_view_bounds && view_bounds.isValid() &&
            !view_bounds.isEmpty()) {
            geometry.multi_bounds_view = view_bounds;
            geometry.multi_handles[0] = view_bounds.topLeft();
            geometry.multi_handles[1] = QPointF(
                view_bounds.center().x(), view_bounds.top());
            geometry.multi_handles[2] = view_bounds.topRight();
            geometry.multi_handles[3] = QPointF(
                view_bounds.right(), view_bounds.center().y());
            geometry.multi_handles[4] = view_bounds.bottomRight();
            geometry.multi_handles[5] = QPointF(
                view_bounds.center().x(), view_bounds.bottom());
            geometry.multi_handles[6] = view_bounds.bottomLeft();
            geometry.multi_handles[7] = QPointF(
                view_bounds.left(), view_bounds.center().y());
        }
    }

    selection_overlay_cache_ = geometry;
    selection_overlay_cache_valid_ = true;
    return selection_overlay_cache_;
}

const CanvasPreview::HoverOverlayGeometry &CanvasPreview::hover_overlay_geometry() const
{
    if (hover_overlay_cache_valid_)
        return hover_overlay_cache_;

    HoverOverlayGeometry geometry;
    if (!title_ || hovered_layer_id_.empty() || drag_mode_ != DragMode::None || drawing_shape_) {
        hover_overlay_cache_ = geometry;
        hover_overlay_cache_valid_ = true;
        return hover_overlay_cache_;
    }

    auto it = std::find_if(title_->layers.begin(), title_->layers.end(), [this](const std::shared_ptr<Layer> &layer) {
        return layer && layer->id == hovered_layer_id_;
    });
    if (it != title_->layers.end() && *it) {
        const Layer &layer = **it;
        if (layer.type != LayerType::Light &&
            layer.type != LayerType::Audio &&
            layer.type != LayerType::Empty &&
            layer_is_editor_visible(layer) && layer.visible && !layer.locked &&
            playhead_ >= layer.in_time && playhead_ <= layer.out_time) {
            const QRectF box = layer_local_rect(layer);
            if (box.isValid() && !box.isEmpty()) {
                const auto projected_title = projection_title();
                const bool projected_3d = projected_title &&
                    fxm::transform3d::layer_or_ancestor_uses_3d(
                        *projected_title, layer);
                const qreal overlay_pixel_scale =
                    std::max<qreal>(1.0, devicePixelRatioF());
                auto quantize_overlay = [&](QPointF view) {
                    if (projected_3d) {
                        view.setX(std::round(view.x() * overlay_pixel_scale) /
                                  overlay_pixel_scale);
                        view.setY(std::round(view.y() * overlay_pixel_scale) /
                                  overlay_pixel_scale);
                    }
                    return view;
                };
                geometry.layer = &layer;
                geometry.hovered_is_selected =
                    std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), hovered_layer_id_) != selected_layer_ids_.end() ||
                    sel_layer_id_ == hovered_layer_id_;
                if (projected_3d) {
                    QPolygonF warped_local_polygon;
                    warped_local_polygon
                        << editor_warped_layer_local_point(
                               layer, box, box.topLeft(), playhead_)
                        << editor_warped_layer_local_point(
                               layer, box, box.topRight(), playhead_)
                        << editor_warped_layer_local_point(
                               layer, box, box.bottomRight(), playhead_)
                        << editor_warped_layer_local_point(
                               layer, box, box.bottomLeft(), playhead_);
                    QRectF clipped_bounds;
                    QPolygonF clipped_canvas_polygon;
                    if (fxm::transform3d::projected_local_polygon(
                            *projected_title, layer, playhead_,
                            warped_local_polygon, clipped_bounds,
                            &clipped_canvas_polygon)) {
                        for (const QPointF &point : clipped_canvas_polygon)
                            geometry.outline << quantize_overlay(
                                canvas_to_view(point));
                    }
                } else {
                    geometry.outline
                        << canvas_to_view(transformed_layer_point(
                               layer, box.topLeft()))
                        << canvas_to_view(transformed_layer_point(
                               layer, box.topRight()))
                        << canvas_to_view(transformed_layer_point(
                               layer, box.bottomRight()))
                        << canvas_to_view(transformed_layer_point(
                               layer, box.bottomLeft()));
                }
            }
        }
    }

    hover_overlay_cache_ = geometry;
    hover_overlay_cache_valid_ = true;
    return hover_overlay_cache_;
}

bool CanvasPreview::gradient_handles_visible() const
{
    return active_tool_ == CanvasTool::Gradient || gradient_editor_active_;
}

bool CanvasPreview::selected_text_fill(const Layer &layer, RichTextFill &fill) const
{
    if (!is_canvas_text_layer(layer) || inline_text_layer_id_ != layer.id)
        return false;

    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    const RichTextCharFormatSummary summary =
        summarize_rich_text_char_format(layer, true, local_time);
    if (!summary.valid || (summary.mixed & RichTextCharFillColor) != 0)
        return false;
    fill = summary.format.fill;
    return true;
}

bool CanvasPreview::apply_selected_text_gradient_fill(
    Layer &layer, const RichTextFill &fill)
{
    if (!is_canvas_text_layer(layer) || inline_text_layer_id_ != layer.id)
        return false;

    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    RichTextCharFormatSummary summary =
        summarize_rich_text_char_format(layer, true, local_time);
    RichTextCharFormat format = summary.valid
        ? summary.format
        : rich_text_effective_typing_format(layer.rich_text);
    format.fill = fill;
    apply_rich_text_format_to_layer_range(
        layer, format, RichTextCharFillColor, true);
    apply_active_text_char_format(
        layer.id, format, RichTextCharFillColor);
    return true;
}

std::vector<CanvasPreview::GradientHandleTarget>
CanvasPreview::gradient_handle_targets(const Layer &layer) const
{
    std::vector<GradientHandleTarget> targets;
    if (!gradient_handles_visible() || layer.locked || !layer.visible)
        return targets;
    if (playhead_ < layer.in_time || playhead_ > layer.out_time)
        return targets;
    if (!(layer.type == LayerType::SolidRect || layer.type == LayerType::Shape ||
          is_canvas_text_layer(layer)))
        return targets;

    if (!is_canvas_text_layer(layer)) {
        if (layer.fill_type != 1)
            return targets;
        GradientHandleTarget target;
        target.kind = GradientHandleTarget::Kind::LayerFill;
        target.fill.type = layer.fill_type;
        target.fill.gradient_type = layer.gradient_type;
        target.fill.gradient_center_x = layer.gradient_center_x;
        target.fill.gradient_center_y = layer.gradient_center_y;
        target.fill.gradient_focal_x = layer.gradient_focal_x;
        target.fill.gradient_focal_y = layer.gradient_focal_y;
        target.fill.gradient_scale = layer.gradient_scale;
        target.fill.gradient_angle = layer.gradient_angle;
        targets.push_back(std::move(target));
        return targets;
    }

    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    RichTextDocument source = layer.rich_text.empty()
        ? rich_text_document_from_layer_defaults(layer)
        : layer.rich_text;
    RichTextDocument effective = rich_text_document_for_editor_time(layer, local_time);
    effective = rich_text_effective_document_for_editor(effective);
    effective.normalize();

    size_t selection_start = 0;
    size_t selection_end = 0;
    bool isolate_uniform_selection = false;
    if (inline_text_layer_id_ == layer.id) {
        selection_start = std::min(source.selection.anchor, source.selection.head);
        selection_end = std::max(source.selection.anchor, source.selection.head);
        selection_start = std::min(selection_start, effective.plain_text.size());
        selection_end = std::min(selection_end, effective.plain_text.size());
        if (selection_end > selection_start) {
            RichTextFill selected_fill;
            if (selected_text_fill(layer, selected_fill) && selected_fill.type == 1) {
                GradientHandleTarget target;
                target.kind = GradientHandleTarget::Kind::TextFillRanges;
                target.fill = selected_fill;
                target.text_ranges.push_back({selection_start, selection_end - selection_start});
                target.selection_target = true;
                targets.push_back(std::move(target));
                isolate_uniform_selection = true;
            }
        }
    }

    const size_t text_len = effective.plain_text.size();
    if (text_len == 0)
        return targets;

    std::vector<size_t> boundaries{0, text_len};
    for (const RichTextRange &range : effective.ranges) {
        const size_t range_start = std::min(range.start, text_len);
        const size_t range_end = std::min(text_len, range_start +
            std::min(range.length, text_len - range_start));
        boundaries.push_back(range_start);
        boundaries.push_back(range_end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    auto append_range = [&](size_t range_start, size_t range_end,
                            const RichTextFill &fill) {
        if (fill.type != 1 || range_end <= range_start)
            return;
        for (GradientHandleTarget &existing : targets) {
            if (existing.selection_target ||
                existing.kind != GradientHandleTarget::Kind::TextFillRanges)
                continue;
            if (rich_text_fills_equal(existing.fill, fill)) {
                existing.text_ranges.push_back({range_start, range_end - range_start});
                return;
            }
        }
        GradientHandleTarget target;
        target.kind = GradientHandleTarget::Kind::TextFillRanges;
        target.fill = fill;
        target.text_ranges.push_back({range_start, range_end - range_start});
        targets.push_back(std::move(target));
    };

    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const size_t segment_start = boundaries[i];
        const size_t segment_end = boundaries[i + 1];
        if (segment_end <= segment_start)
            continue;
        const RichTextFill fill = rich_text_format_at(effective, segment_start).fill;
        if (isolate_uniform_selection && segment_start < selection_end && segment_end > selection_start) {
            append_range(segment_start, std::min(segment_end, selection_start), fill);
            append_range(std::max(segment_start, selection_end), segment_end, fill);
        } else {
            append_range(segment_start, segment_end, fill);
        }
    }
    for (size_t i = 0; i < targets.size(); ++i)
        targets[i].display_index = i;
    return targets;
}

bool CanvasPreview::layer_supports_gradient_handles(const Layer &layer) const
{
    return !gradient_handle_targets(layer).empty();
}

CanvasPreview::GradientHandleGeometry CanvasPreview::gradient_handle_geometry(
    const Layer &layer, const GradientHandleTarget &target) const
{
    GradientHandleGeometry g;
    if (!gradient_handles_visible() || layer.locked || !layer.visible)
        return g;
    if (playhead_ < layer.in_time || playhead_ > layer.out_time)
        return g;

    QRectF box = layer_local_rect(layer);
    if (is_canvas_text_layer(layer))
        box = text_rect_for_style(box, layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return g;

    const RichTextFill &fill = target.fill;
    if (fill.type != 1)
        return g;

    g.valid = true;
    g.target = target;
    g.radial = fill.gradient_type == 1;
    g.local_rect = box;
    g.center = QPointF(box.left() + (double)fill.gradient_center_x * box.width(),
                       box.top() + (double)fill.gradient_center_y * box.height());

    const double scale = std::clamp((double)fill.gradient_scale, 0.01, 100.0);
    const double angle = degrees_to_radians(fill.gradient_angle);
    const QPointF axis(std::cos(angle), std::sin(angle));
    if (g.radial) {
        const double radius = std::max(box.width(), box.height()) * 0.5 * scale;
        g.radius = g.center + axis * std::max(1.0, radius);
        g.focal = QPointF(box.left() + (double)fill.gradient_focal_x * box.width(),
                          box.top() + (double)fill.gradient_focal_y * box.height());
        g.start = g.center;
        g.end = g.radius;
    } else {
        const double length = std::hypot(box.width(), box.height()) * 0.5 * scale;
        const QPointF delta = axis * std::max(1.0, length);
        g.start = g.center - delta;
        g.end = g.center + delta;
        g.radius = g.end;
        g.focal = g.center;
    }
    return g;
}

CanvasPreview::DragMode CanvasPreview::hit_test_gradient_handles(
    const Layer &layer, const QPointF &view_pt, GradientHandleTarget *target) const
{
    const std::vector<GradientHandleTarget> targets = gradient_handle_targets(layer);
    DragMode best_mode = DragMode::None;
    double best_score = std::numeric_limits<double>::max();
    GradientHandleTarget best_target;

    auto consider = [&](const GradientHandleGeometry &g, const QPointF &local,
                        double radius, DragMode mode) {
        const QPointF view = canvas_to_view(layer_to_canvas(layer, local));
        const double distance = QLineF(view_pt, view).length();
        const size_t display_index = g.target.display_index;
        const double visual_radius = radius + (double)display_index * 1.75;
        if (distance > visual_radius + 2.5)
            return;
        /* Multiple text gradients commonly start with identical geometry. Their
         * handles are drawn as concentric rings; scoring against each ring lets
         * the user select an otherwise perfectly overlapping controller. */
        const double score = display_index == 0
            ? distance
            : std::min(distance, std::abs(distance - visual_radius) + 0.25);
        if (score < best_score) {
            best_score = score;
            best_mode = mode;
            best_target = g.target;
        }
    };

    for (size_t target_index = 0; target_index < targets.size(); ++target_index) {
        const GradientHandleTarget &candidate = targets[target_index];
        const GradientHandleGeometry g = gradient_handle_geometry(layer, candidate);
        if (!g.valid)
            continue;
        if (g.radial) {
            consider(g, g.focal, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2,
                     DragMode::GradientFocal);
            consider(g, g.radius, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2,
                     DragMode::GradientRadius);
            consider(g, g.center, CANVAS_CONTROL_HIT_RADIUS_PX * 1.35,
                     DragMode::GradientCenter);
        } else {
            consider(g, g.start, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2,
                     DragMode::GradientStart);
            consider(g, g.end, CANVAS_CONTROL_HIT_RADIUS_PX * 1.2,
                     DragMode::GradientEnd);
            consider(g, g.center, CANVAS_CONTROL_HIT_RADIUS_PX * 1.35,
                     DragMode::GradientCenter);
        }
    }

    if (target && best_mode != DragMode::None)
        *target = std::move(best_target);
    return best_mode;
}

void CanvasPreview::draw_gradient_handles(QPainter &p, const Layer &layer)
{
    std::vector<GradientHandleTarget> targets;
    if (gradient_drag_.active && gradient_drag_.layer_id == layer.id) {
        GradientHandleTarget active_target = gradient_drag_.target;
        active_target.fill = gradient_drag_.fill;
        targets.push_back(std::move(active_target));
    } else {
        targets = gradient_handle_targets(layer);
    }
    if (targets.empty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (size_t draw_index = targets.size(); draw_index-- > 0;) {
        const size_t target_index = draw_index;
        const GradientHandleGeometry g = gradient_handle_geometry(layer, targets[target_index]);
        if (!g.valid)
            continue;

        auto to_view = [&](const QPointF &local) {
            return canvas_to_view(layer_to_canvas(layer, local));
        };
        const size_t display_index = targets[target_index].display_index;
        const QColor controller_color = display_index == 0
            ? QColor(0, 145, 255)
            : QColor::fromHsv((205 + (int)display_index * 67) % 360, 190, 245);
        const double handle_radius_offset = (double)display_index * 1.75;
        auto draw_handle = [&](const QPointF &view_point, const QColor &fill,
                               double radius = CANVAS_GRADIENT_HANDLE_RADIUS_PX) {
            radius += handle_radius_offset;
            p.setPen(QPen(controller_color, 2.5));
            p.setBrush(fill);
            p.drawEllipse(view_point, radius, radius);
            p.setPen(QPen(QColor(255, 255, 255, 235), 1.0));
            p.drawEllipse(view_point, radius, radius);
        };

        const QPointF center = to_view(g.center);
        QPen guide_pen(controller_color, 1.5, Qt::SolidLine);
        guide_pen.setCosmetic(true);
        p.setPen(guide_pen);
        p.setBrush(Qt::NoBrush);

        if (g.radial) {
            const QPointF radius = to_view(g.radius);
            const QPointF focal = to_view(g.focal);
            p.drawLine(center, radius);
            QPen focal_pen(controller_color.lighter(125), 1.2, Qt::DashLine);
            focal_pen.setCosmetic(true);
            p.setPen(focal_pen);
            p.drawLine(center, focal);
            draw_handle(center, controller_color);
            draw_handle(radius, QColor(255, 255, 255));
            draw_handle(focal, controller_color.lighter(135),
                        CANVAS_GRADIENT_HANDLE_RADIUS_PX - 0.5);
        } else {
            const QPointF start = to_view(g.start);
            const QPointF end = to_view(g.end);
            p.drawLine(start, end);
            draw_handle(start, QColor(255, 255, 255));
            draw_handle(end, QColor(255, 255, 255));
            draw_handle(center, controller_color);
        }
    }
    p.restore();
}

void CanvasPreview::begin_gradient_drag(const Layer &layer,
                                        const GradientHandleTarget &target)
{
    const GradientHandleGeometry g = gradient_handle_geometry(layer, target);
    gradient_drag_ = GradientDragState{};
    if (!g.valid)
        return;

    gradient_drag_.active = true;
    gradient_drag_.layer_id = layer.id;
    gradient_drag_.target = target;
    gradient_drag_.fill = target.fill;
    gradient_drag_.radial = g.radial;
    gradient_drag_.local_rect = g.local_rect;
    gradient_drag_.center = g.center;
    gradient_drag_.start = g.start;
    gradient_drag_.end = g.end;
    gradient_drag_.radius = g.radius;
    gradient_drag_.focal = g.focal;
    gradient_drag_.center_x = gradient_drag_.fill.gradient_center_x;
    gradient_drag_.center_y = gradient_drag_.fill.gradient_center_y;
    gradient_drag_.focal_x = gradient_drag_.fill.gradient_focal_x;
    gradient_drag_.focal_y = gradient_drag_.fill.gradient_focal_y;
    gradient_drag_.scale = gradient_drag_.fill.gradient_scale;
    gradient_drag_.angle = gradient_drag_.fill.gradient_angle;
}
