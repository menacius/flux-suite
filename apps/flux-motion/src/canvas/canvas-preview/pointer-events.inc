    auto layer = layers.front();
    if (!layer || layer->locked) return DragMode::None;
    if (layer->type == LayerType::Audio)
        return DragMode::None;
    if (layer->type == LayerType::Light)
        return light_layer_overlay_contains(*layer, view_pt)
            ? DragMode::Move : DragMode::None;
    if (layer->type == LayerType::Empty)
        return empty_layer_overlay_contains(*layer, view_pt)
            ? DragMode::Move : DragMode::None;

    if (active_tool_ == CanvasTool::FreeTransform) {
        const QRectF box = layer_local_rect(*layer);
        const QPointF local_points[8] = {
            box.topLeft(), QPointF(box.center().x(), box.top()), box.topRight(),
            QPointF(box.right(), box.center().y()), box.bottomRight(),
            QPointF(box.center().x(), box.bottom()), box.bottomLeft(),
            QPointF(box.left(), box.center().y())
        };
        QPointF points[8];
        for (int i = 0; i < 8; ++i) points[i] = canvas_to_view(transformed_layer_point(*layer, local_points[i]));
        const DragMode resize_modes[8] = {DragMode::ResizeNW, DragMode::ResizeN, DragMode::ResizeNE, DragMode::ResizeE,
                                          DragMode::ResizeSE, DragMode::ResizeS, DragMode::ResizeSW, DragMode::ResizeW};
        if (free_transform_mode_ > 0) {
            const int corner_indices[4] = {0,2,4,6};
            for (int i = 0; i < 4; ++i)
                if (QLineF(view_pt, points[corner_indices[i]]).length() <= CANVAS_CONTROL_HIT_RADIUS_PX * 1.5)
                    return static_cast<DragMode>(static_cast<int>(DragMode::TransformCornerTL) + i);
        } else {
            for (int i = 0; i < 8; ++i)
                if (QLineF(view_pt, points[i]).length() <= CANVAS_CONTROL_HIT_RADIUS_PX * 1.5) return resize_modes[i];
        }
        QPolygonF quad; quad << points[0] << points[2] << points[4] << points[6];
        if (free_transform_mode_ == 0) {
            for (int idx : {0,2,4,6}) {
                if (QLineF(view_pt, points[idx]).length() <= CANVAS_ROTATE_HIT_RADIUS_PX &&
                    !quad.containsPoint(view_pt, Qt::OddEvenFill)) return DragMode::Rotate;
            }
        }
        if (quad.containsPoint(view_pt, Qt::OddEvenFill)) return DragMode::Move;
        return DragMode::None;
    }

    DragMode gradient_hit = hit_test_gradient_handles(*layer, view_pt);
    if (gradient_hit != DragMode::None)
        return gradient_hit;
    DragMode corner_radius_hit = hit_test_corner_radius_handles(*layer, view_pt);
    if (corner_radius_hit != DragMode::None)
        return corner_radius_hit;

    QRectF r = layer_local_rect(*layer);
    auto layer_point_to_view = [&](const QPointF &p) {
        return canvas_to_view(layer_to_canvas(*layer, p));
    };
    auto near_pt = [&](const QPointF &p) {
        QPointF view = layer_point_to_view(p);
        return std::abs(view_pt.x() - view.x()) <= CANVAS_CONTROL_HIT_RADIUS_PX &&
               std::abs(view_pt.y() - view.y()) <= CANVAS_CONTROL_HIT_RADIUS_PX;
    };
    QPainterPath layer_path;
    layer_path.moveTo(layer_point_to_view(r.topLeft()));
    layer_path.lineTo(layer_point_to_view(r.topRight()));
    layer_path.lineTo(layer_point_to_view(r.bottomRight()));
    layer_path.lineTo(layer_point_to_view(r.bottomLeft()));
    layer_path.closeSubpath();
    auto near_rotation_corner = [&](const QPointF &p) {
        return QLineF(view_pt, layer_point_to_view(p)).length() <= CANVAS_ROTATE_HIT_RADIUS_PX &&
               !layer_path.contains(view_pt);
    };

    if (near_pt(r.topLeft())) return DragMode::ResizeNW;
    if (near_pt(QPointF(r.center().x(), r.top()))) return DragMode::ResizeN;
    if (near_pt(r.topRight())) return DragMode::ResizeNE;
    if (near_pt(QPointF(r.right(), r.center().y()))) return DragMode::ResizeE;
    if (near_pt(r.bottomRight())) return DragMode::ResizeSE;
    if (near_pt(QPointF(r.center().x(), r.bottom()))) return DragMode::ResizeS;
    if (near_pt(r.bottomLeft())) return DragMode::ResizeSW;
    if (near_pt(QPointF(r.left(), r.center().y()))) return DragMode::ResizeW;
    if (near_rotation_corner(r.topLeft()) || near_rotation_corner(r.topRight()) ||
        near_rotation_corner(r.bottomRight()) || near_rotation_corner(r.bottomLeft()))
        return DragMode::Rotate;
    if (QLineF(view_pt, layer_point_to_view(QPointF(0, 0))).length() <= CANVAS_CONTROL_HIT_RADIUS_PX * 1.25)
        return DragMode::Origin;

    if (layer_path.contains(view_pt)) return DragMode::Move;
    return DragMode::None;
}

bool CanvasPreview::active_draw_tool_can_manipulate_selected() const
{
    if (active_tool_ != CanvasTool::Shape && active_tool_ != CanvasTool::Text &&
        active_tool_ != CanvasTool::Image)
        return false;
    const auto layers = selected_layers();
    if (layers.empty())
        return false;
    for (const auto &layer : layers) {
        if (!layer || layer->locked || !layer->visible)
            return false;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time)
            return false;
        if (active_tool_ == CanvasTool::Shape) {
            if (!(layer->type == LayerType::Shape || layer->type == LayerType::SolidRect))
                return false;
        } else if (active_tool_ == CanvasTool::Text) {
            if (!is_canvas_text_layer(*layer))
                return false;
        } else if (active_tool_ == CanvasTool::Image) {
            if (!layer_type_is_image_like(layer->type))
                return false;
        }
    }
    return true;
}

void CanvasPreview::set_cursor_for_drag_mode(DragMode mode, bool dragging)
{
    if (mode == DragMode::Move) setCursor(dragging ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
    else if (mode == DragMode::Origin) setCursor(Qt::CrossCursor);
    else if (mode == DragMode::Rotate) setCursor(canvas_rotation_cursor());
    else if (mode == DragMode::GradientCenter || mode == DragMode::GradientFocal) setCursor(Qt::CrossCursor);
    else if (mode == DragMode::GradientStart || mode == DragMode::GradientEnd ||
             mode == DragMode::GradientRadius) setCursor(Qt::SizeAllCursor);
    else if (mode == DragMode::CornerRadius || mode == DragMode::CornerRadiusTL ||
             mode == DragMode::CornerRadiusTR || mode == DragMode::CornerRadiusBR ||
             mode == DragMode::CornerRadiusBL) setCursor(Qt::SizeAllCursor);
    else if (mode == DragMode::ResizeN || mode == DragMode::ResizeS) setCursor(Qt::SizeVerCursor);
    else if (mode == DragMode::ResizeE || mode == DragMode::ResizeW) setCursor(Qt::SizeHorCursor);
    else if (mode == DragMode::ResizeNE || mode == DragMode::ResizeSW) setCursor(Qt::SizeBDiagCursor);
    else if (mode != DragMode::None) setCursor(Qt::SizeFDiagCursor);
    else unsetCursor();
}

void CanvasPreview::begin_marquee(const QPointF &view_pt, Qt::KeyboardModifiers)
{
    drag_mode_ = DragMode::Marquee;
    marquee_active_ = false;
    drag_start_view_ = view_pt;
    drag_current_view_ = view_pt;
    marquee_base_selection_ = selected_layer_ids_;
    drag_changed_ = false;
    clear_snap_feedback();
}

void CanvasPreview::update_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!title_) return;
    drag_current_view_ = view_pt;
    if ((drag_current_view_ - drag_start_view_).manhattanLength() < QApplication::startDragDistance()) {
        update();
        return;
    }

    marquee_active_ = true;
    QRectF view_rect(drag_start_view_, drag_current_view_);
    view_rect = view_rect.normalized();
    QRectF canvas_rect(view_to_canvas(view_rect.topLeft()), view_to_canvas(view_rect.bottomRight()));
    canvas_rect = canvas_rect.normalized();
    auto intersects_or_touches = [](const QRectF &a, const QRectF &b) {
        if (!a.isValid() || !b.isValid()) return false;
        return a.left() <= b.right() && a.right() >= b.left() &&
               a.top() <= b.bottom() && a.bottom() >= b.top();
    };

    std::set<std::string> selected;
    if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier))
        selected.insert(marquee_base_selection_.begin(), marquee_base_selection_.end());

    std::vector<std::string> hits;
    std::set<std::string> hit_ids;
    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible || layer->locked) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (layer->type == LayerType::Audio) continue;
        bool object_hit = false;
        if (layer->type == LayerType::Empty) {
            std::array<QPointF, 4> axes;
            bool is_3d = false;
            if (!empty_layer_overlay_points(*layer, axes, is_3d) ||
                !view_rect.contains(axes[0]))
                continue;
            object_hit = true;
        }
        if (!object_hit) {
            QRectF bounds = layer_canvas_bounds(*layer);
            if (!intersects_or_touches(canvas_rect, bounds))
                continue;
        }
        if (layer->type == LayerType::Group && !layer->group_collapsed)
            continue;
        const Layer *target = editor_canvas_selection_target(title_, *layer);
        if (!target || target->locked)
            continue;
        const std::string hit_id = target->id;
        if (hit_ids.insert(hit_id).second)
            hits.push_back(hit_id);
    }

    if (modifiers & Qt::ControlModifier) {
        for (const auto &id : hits) {
            auto it = selected.find(id);
            if (it == selected.end()) selected.insert(id);
            else selected.erase(it);
        }
    } else {
        selected.insert(hits.begin(), hits.end());
    }

    selected_layer_ids_.clear();
    for (const auto &layer : title_->layers) {
        if (layer && selected.find(layer->id) != selected.end())
            selected_layer_ids_.push_back(layer->id);
    }
    sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
    invalidate_canvas_overlay_caches();
    emit layers_selected(selected_layer_ids_);
    update();
}

bool CanvasPreview::duplicate_selected_layers_for_drag()
{
    if (!title_ || selected_layer_ids_.empty()) return false;

    /* Alt-dragging a group must duplicate the complete container, not only
     * the synthetic group layer. Expand the requested canvas selection with
     * every nested group descendant before cloning, while keeping the visible
     * drag selection limited to the originally selected roots. */
    std::set<std::string> requested_ids;
    for (const auto &id : selected_layer_ids_) {
        auto layer = title_->find_layer(id);
        if (layer && !layer->locked)
            requested_ids.insert(id);
    }
    if (requested_ids.empty()) return false;

    std::set<std::string> selected_ids = requested_ids;
    bool added_descendant = true;
    while (added_descendant) {
        added_descendant = false;
        for (const auto &layer : title_->layers) {
            if (!layer || layer->parent_id.empty() ||
                selected_ids.find(layer->id) != selected_ids.end())
                continue;
            auto parent = title_->find_layer(layer->parent_id);
            const bool regular_child = parent && layer_type_is_container(parent->type);
            const bool linked_video_stream =
                parent && parent->type == LayerType::Video &&
                layer->type == LayerType::Audio && layer->linked_media_stream &&
                layer->linked_media_layer_id == parent->id;
            if ((regular_child || linked_video_stream) &&
                selected_ids.find(parent->id) != selected_ids.end()) {
                selected_ids.insert(layer->id);
                added_descendant = true;
            }
        }
    }

    std::map<std::string, std::string> cloned_ids_by_original;
    std::map<std::string, std::shared_ptr<Layer>> clones_by_original;
    std::vector<std::shared_ptr<Layer>> clones;
    std::set<std::string> reserved_names;

    for (const auto &layer : title_->layers) {
        if (!layer || selected_ids.find(layer->id) == selected_ids.end())
            continue;

        auto clone = std::make_shared<Layer>(*layer);
        clone->id = TitleDataStore::make_uuid();
        if (clone->type == LayerType::TransitionInput)
            clone->transition_input_required = false;
        clone->name = clone->name.empty() ? editor_text_std("OBSTitles.LayerCopy") :
                                            clone->name + editor_text_std("OBSTitles.CopySuffix");
        clone->name = unique_canvas_layer_name(*title_, clone->name, {}, &reserved_names);
        cloned_ids_by_original[layer->id] = clone->id;
        clones_by_original[layer->id] = clone;
        clones.push_back(clone);
    }

    if (clones.empty()) return false;

    for (auto &clone : clones) {
        auto parent_clone = cloned_ids_by_original.find(clone->parent_id);
        if (parent_clone != cloned_ids_by_original.end()) {
            clone->parent_id = parent_clone->second;
        } else if (!clone->parent_id.empty() && !title_->find_layer(clone->parent_id)) {
            clone->parent_id.clear();
        }
        auto transform_parent_clone = cloned_ids_by_original.find(clone->transform_parent_id);
        if (transform_parent_clone != cloned_ids_by_original.end()) {
            clone->transform_parent_id = transform_parent_clone->second;
        } else if (!clone->transform_parent_id.empty() &&
                   !title_->find_layer(clone->transform_parent_id)) {
            clone->transform_parent_id.clear();
        }
        auto mask_clone = cloned_ids_by_original.find(clone->mask_source_id);
        if (mask_clone != cloned_ids_by_original.end()) {
            clone->mask_source_id = mask_clone->second;
        } else if (!clone->mask_source_id.empty() && !title_->find_layer(clone->mask_source_id)) {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }
        auto media_owner_clone = cloned_ids_by_original.find(clone->linked_media_layer_id);
        if (media_owner_clone != cloned_ids_by_original.end()) {
            clone->linked_media_layer_id = media_owner_clone->second;
        } else if (clone->linked_media_stream) {
            clone->linked_media_layer_id.clear();
            clone->linked_media_stream = false;
            clone->parent_id.clear();
        }
    }

    std::vector<std::shared_ptr<Layer>> next_layers;
    next_layers.reserve(title_->layers.size() + clones.size());
    for (const auto &layer : title_->layers) {
        next_layers.push_back(layer);
        if (!layer) continue;
        auto clone = clones_by_original.find(layer->id);
        /* LayerStack displays title_->layers in reverse order. Inserting the
         * clone after the source in model order places it directly above the
         * original in the visible layer list and render stack. */
        if (clone != clones_by_original.end())
            next_layers.push_back(clone->second);
    }
    title_->layers = std::move(next_layers);
    synchronize_video_audio_streams(*title_);

    selected_layer_ids_.clear();
    drag_layer_states_.clear();
    for (const auto &layer : title_->layers) {
        if (!layer) continue;
        const auto original_id = std::find_if(
            clones_by_original.begin(), clones_by_original.end(),
            [&](const auto &entry) { return entry.second && entry.second->id == layer->id; });
        if (original_id == clones_by_original.end() ||
            requested_ids.find(original_id->first) == requested_ids.end())
            continue;
        const auto &clone = layer;
        selected_layer_ids_.push_back(clone->id);
        double lt = std::clamp(playhead_ - clone->in_time, 0.0,
                               std::max(0.0, clone->out_time - clone->in_time));
        const QRectF clone_local_bounds = layer_local_rect(*clone);
        const QPointF clone_local_position =
            editor_evaluated_local_position_xy(*clone, lt);
        drag_layer_states_.push_back({clone->id,
                                      {},
                                      clone_local_position.x(),
                                      clone_local_position.y(),
                                      static_cast<float>(clone_local_bounds.width()),
                                      static_cast<float>(clone_local_bounds.height()),
                                      clone_local_bounds.left(),
                                      clone_local_bounds.top(),
                                      clone->scale.evaluate(lt).x,
                                      clone->scale.evaluate(lt).y,
                                      clone->rotation.evaluate(lt),
                                      clone->stroke_width,
                                      clone->corner_radius_tl,
                                      clone->corner_radius_tr,
                                      clone->corner_radius_br,
                                      clone->corner_radius_bl,
                                      clone->shape_roundness,
                                      clone->shape_inner_roundness,
                                      capture_path_corner_radii(*clone)});
    }
    sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
    invalidate_canvas_overlay_caches();
    drag_start_selection_bounds_ = selected_canvas_bounds();

    emit layer_structure_changed();
    emit layers_selected(selected_layer_ids_);
    dirty_ = true;
    update();
    return true;
}

bool CanvasPreview::nudge_selected_layers(double dx, double dy)
{
    auto layers = selected_layers();
    if (!title_ || layers.empty()) return false;

    auto selected_contains = [&](const std::string &id) {
        return std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), id) != selected_layer_ids_.end();
    };
    auto has_selected_parent = [&](const Layer &layer) {
        std::vector<std::string> pending;
        if (!layer.parent_id.empty()) pending.push_back(layer.parent_id);
        if (!layer.transform_parent_id.empty()) pending.push_back(layer.transform_parent_id);
        std::set<std::string> visited;
        while (!pending.empty()) {
            const std::string parent_id = pending.back();
            pending.pop_back();
            if (parent_id.empty() || !visited.insert(parent_id).second)
                continue;
            if (selected_contains(parent_id))
                return true;
            const Layer *parent = editor_find_layer_by_id(title_, parent_id);
            if (!parent) continue;
            if (!parent->parent_id.empty()) pending.push_back(parent->parent_id);
            if (!parent->transform_parent_id.empty()) pending.push_back(parent->transform_parent_id);
        }
        return false;
    };

    bool changed = false;
    for (const auto &layer : layers) {
        if (!layer || layer->locked || layer_has_fixed_canvas_geometry(*layer) ||
            has_selected_parent(*layer))
            continue;
        double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                               std::max(0.0, layer->out_time - layer->in_time));
        const QPointF current_local =
            editor_evaluated_local_position_xy(*layer, lt);
        const QPointF local_delta = editor_canvas_delta_to_parent_local(
            title_, *layer, playhead_, QPointF(dx, dy));
        editor_set_local_position_xy(*layer, lt, current_local + local_delta);
        changed = true;
    }

    if (!changed) return false;

    begin_adaptive_interaction();
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    update();
    emit layer_geometry_changed();
    return true;
}


void CanvasPreview::clear_snap_feedback()
{
    if (snap_feedback_.empty()) return;
    snap_feedback_.clear();
    /* Snap guides and labels are baked into gpu_overlay_cache_. Repainting
     * without invalidation only uploads the old overlay again. */
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::add_snap_feedback(bool x_axis, double value, const QString &label)
{
    snap_feedback_.push_back({x_axis, value, label});
}

static std::set<std::string> editor_snap_excluded_layer_ids(
    const std::shared_ptr<Title> &title,
    const std::vector<std::string> &selected_layer_ids,
    const std::string &primary_layer_id)
{
    std::set<std::string> excluded(selected_layer_ids.begin(), selected_layer_ids.end());
    if (!primary_layer_id.empty())
        excluded.insert(primary_layer_id);
    if (!title)
        return excluded;

    const std::vector<std::string> selected(excluded.begin(), excluded.end());
    for (const auto &id : selected) {
        auto layer = title->find_layer(id);
        std::set<std::string> visited;
        while (layer && !layer->parent_id.empty() &&
               visited.insert(layer->parent_id).second) {
            auto parent = title->find_layer(layer->parent_id);
            if (!parent || !layer_type_is_container(parent->type))
                break;
            /* A child must never magnetically snap to the container whose
             * transform already defines its local coordinate space. */
            excluded.insert(parent->id);
            layer = parent;
        }
    }

    auto world_transform_depends_on = [&](const Layer &candidate,
                                          const std::string &ancestor_id) {
        std::vector<std::string> pending;
        if (!candidate.parent_id.empty())
            pending.push_back(candidate.parent_id);
        if (!candidate.transform_parent_id.empty())
            pending.push_back(candidate.transform_parent_id);
        std::set<std::string> visited;
        while (!pending.empty()) {
            const std::string dependency_id = pending.back();
            pending.pop_back();
            if (dependency_id.empty() || !visited.insert(dependency_id).second)
                continue;
            if (dependency_id == ancestor_id)
                return true;
            const auto dependency = title->find_layer(dependency_id);
            if (!dependency)
                continue;
            if (!dependency->parent_id.empty())
                pending.push_back(dependency->parent_id);
            if (!dependency->transform_parent_id.empty())
                pending.push_back(dependency->transform_parent_id);
        }
        return false;
    };

    for (const auto &selected_id : selected) {
        const auto selected_layer = title->find_layer(selected_id);
        if (!selected_layer) continue;
        for (const auto &candidate : title->layers) {
            if (!candidate || candidate->id == selected_id)
                continue;
            /* Moving a transform parent must not snap to any of its children,
             * including descendants contained by a parented Group.
             * Moving a Group must not snap to artwork contained by the Group. */
            if (world_transform_depends_on(*candidate, selected_id))
                excluded.insert(candidate->id);
        }
    }
    return excluded;
}

void CanvasPreview::collect_snap_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const
{
    if (!title_) return;
    auto add = [&](double value, const QString &label) {
        if (!std::isfinite(value)) return;
        targets.push_back(value);
        labels.push_back(label);
    };

    if (snap_settings_.canvas_bounds) {
        const double size = x_axis ? title_->width : title_->height;
        add(0.0, fxm_tr("OBSTitles.Canvas"));
        add(size * 0.5, fxm_tr("OBSTitles.CanvasCenter"));
        add(size, fxm_tr("OBSTitles.Canvas"));
    }

    if (snap_settings_.guides) {
        const double size = x_axis ? title_->width : title_->height;
        add(size * OBS_ACTION_SAFE_PERCENT, fxm_tr("OBSTitles.ActionSafe"));
        add(size * (1.0 - OBS_ACTION_SAFE_PERCENT), fxm_tr("OBSTitles.ActionSafe"));
        add(size * OBS_GRAPHICS_SAFE_PERCENT, fxm_tr("OBSTitles.TitleSafe"));
        add(size * (1.0 - OBS_GRAPHICS_SAFE_PERCENT), fxm_tr("OBSTitles.TitleSafe"));
        const auto &user_guides = x_axis ? vertical_guides_ : horizontal_guides_;
        for (double guide : user_guides)
            add(guide, fxm_tr("OBSTitles.Guide"));
    }

    if (snap_settings_.grid) {
        const double size = x_axis ? title_->width : title_->height;
        constexpr double grid = 10.0;
        for (double v = 0.0; v <= size + 0.01; v += grid)
            add(v, fxm_tr("OBSTitles.Grid"));
    }

    if (!snap_settings_.object_edges && !snap_settings_.object_centers) return;

    const std::set<std::string> excluded_ids = editor_snap_excluded_layer_ids(
        title_, selected_layer_ids_, sel_layer_id_);

    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (excluded_ids.find(layer->id) != excluded_ids.end())
            continue;
        QRectF bounds = layer_canvas_bounds(*layer);
        if (!bounds.isValid() || bounds.isEmpty()) continue;
        if (snap_settings_.object_edges) {
            add(x_axis ? bounds.left() : bounds.top(), fxm_tr("OBSTitles.ObjectEdge"));
            add(x_axis ? bounds.right() : bounds.bottom(), fxm_tr("OBSTitles.ObjectEdge"));
        }
        if (snap_settings_.object_centers)
            add(x_axis ? bounds.center().x() : bounds.center().y(), fxm_tr("OBSTitles.ObjectCenter"));
    }
}

void CanvasPreview::collect_spacing_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const
{
    if (!title_ || !snap_settings_.spacing) return;

    struct Span { double start; double end; };
    std::vector<Span> spans;
    const std::set<std::string> excluded_ids = editor_snap_excluded_layer_ids(
        title_, selected_layer_ids_, sel_layer_id_);

    for (const auto &layer : title_->layers) {
        if (!layer || !layer->visible) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (excluded_ids.find(layer->id) != excluded_ids.end())
            continue;
        QRectF bounds = layer_canvas_bounds(*layer);
        if (!bounds.isValid() || bounds.isEmpty()) continue;
        spans.push_back({x_axis ? bounds.left() : bounds.top(), x_axis ? bounds.right() : bounds.bottom()});
    }
    if (spans.empty()) return;
    std::sort(spans.begin(), spans.end(), [](const Span &a, const Span &b) { return a.start < b.start; });

    std::vector<double> gaps;
    for (size_t i = 1; i < spans.size(); ++i) {
        double gap = spans[i].start - spans[i - 1].end;
        if (gap >= 0.0) gaps.push_back(gap);
    }
    if (gaps.empty()) gaps.push_back(0.0);

    auto add = [&](double value) {
        if (!std::isfinite(value)) return;
        targets.push_back(value);
        labels.push_back(fxm_tr("OBSTitles.Spacing"));
    };
    for (const Span &span : spans) {
        for (double gap : gaps) {
            add(span.start - gap);
            add(span.end + gap);
        }
    }
}

QPointF CanvasPreview::snap_delta_for_bounds(const QRectF &start_bounds, const QPointF &delta, bool snap_x, bool snap_y,
                                             bool allow_snap)
{
    if (!allow_snap || !title_ || !snap_settings_.enabled || !start_bounds.isValid()) {
        clear_snap_feedback();
        return delta;
    }

    snap_feedback_.clear();
    QPointF snapped_delta = delta;
    const double tolerance = 6.0 / std::max(0.1, view_scale());

    auto snap_axis = [&](bool x_axis) {
        std::vector<double> targets;
        std::vector<QString> labels;
        collect_snap_targets(x_axis, targets, labels);
        collect_spacing_targets(x_axis, targets, labels);
        if (targets.empty()) return;

        const double offset = x_axis ? snapped_delta.x() : snapped_delta.y();
        const double start_min = x_axis ? start_bounds.left() : start_bounds.top();
        const double start_center = x_axis ? start_bounds.center().x() : start_bounds.center().y();
        const double start_max = x_axis ? start_bounds.right() : start_bounds.bottom();
        const double points[] = {start_min + offset, start_center + offset, start_max + offset};
        double best_adjust = 0.0;
        double best_distance = tolerance + 1.0;
        double best_target = 0.0;
        QString best_label;
        for (size_t i = 0; i < targets.size(); ++i) {
            for (double point : points) {
                double adjust = targets[i] - point;
                double distance = std::abs(adjust);
                if (distance < best_distance) {
                    best_distance = distance;
                    best_adjust = adjust;
                    best_target = targets[i];
                    best_label = labels[i];
                }
            }
        }
        if (best_distance <= tolerance) {
            if (x_axis) snapped_delta.setX(snapped_delta.x() + best_adjust);
            else snapped_delta.setY(snapped_delta.y() + best_adjust);
            add_snap_feedback(x_axis, best_target, best_label);
        }
    };

    if (snap_x) snap_axis(true);
    if (snap_y) snap_axis(false);
    return snapped_delta;
}

QPointF CanvasPreview::snap_canvas_point(const QPointF &canvas_pt, bool snap_x, bool snap_y, bool allow_snap)
{
    // Point snapping is used by draw-tool hover/start feedback.  Do not route it
    // through snap_delta_for_bounds(): a zero-size QRectF is invalid in Qt, so
    // that path clears the feedback and returns the raw point.  Resolve each
    // axis directly so hover can show the Adobe-style snap dot + helper lines
    // before the first mouse press.
    if (!allow_snap || !title_ || !snap_settings_.enabled) {
        clear_snap_feedback();
        return canvas_pt;
    }

    snap_feedback_.clear();
    QPointF snapped = canvas_pt;
    const double tolerance = 6.0 / std::max(0.1, view_scale());

    auto snap_axis = [&](bool x_axis) {
        std::vector<double> targets;
        std::vector<QString> labels;
        collect_snap_targets(x_axis, targets, labels);
        collect_spacing_targets(x_axis, targets, labels);
        if (targets.empty())
            return;

        const double value = x_axis ? canvas_pt.x() : canvas_pt.y();
        double best_distance = tolerance + 1.0;
        double best_target = value;
        QString best_label;
        for (size_t i = 0; i < targets.size(); ++i) {
            const double distance = std::abs(targets[i] - value);
            if (distance < best_distance) {
                best_distance = distance;
                best_target = targets[i];
                best_label = labels[i];
            }
        }

        if (best_distance <= tolerance) {
            if (x_axis)
                snapped.setX(best_target);
            else
                snapped.setY(best_target);
            add_snap_feedback(x_axis, best_target, best_label);
        }
    };

    if (snap_x)
        snap_axis(true);
    if (snap_y)
        snap_axis(false);
    return snapped;
}

static QRectF editor_modifier_rect(const QPointF &anchor, const QPointF &current,
                                   Qt::KeyboardModifiers modifiers, double aspect,
                                   double fixed_width = 0.0, double fixed_height = 0.0);

void CanvasPreview::apply_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (drag_mode_ == DragMode::Marquee) {
        update_marquee(view_pt, modifiers);
        return;
    }

    if (apply_gradient_drag(view_pt, modifiers))
        return;
    if (apply_corner_radius_drag(view_pt, modifiers))
        return;

    if (drag_mode_ == DragMode::Move && alt_duplicate_pending_ && !alt_duplicate_done_) {
        alt_duplicate_done_ = true;
        alt_duplicate_active_ = duplicate_selected_layers_for_drag();
    }

    /* After Alt+drag creates copies, drive the rest of the pointer gesture from
     * the freshly captured clone drag states rather than from widget selection.
     * Layer/timeline refreshes are synchronous and may temporarily replay a
     * selection update; the active gesture must nevertheless remain attached
     * to the duplicated layers until mouse release. */
    std::vector<std::shared_ptr<Layer>> layers;
    /* Once Alt+drag has duplicated the selection, drag_layer_states_ is the
     * authoritative gesture target for every subsequent mouse-move event.
     * Do not fall back to selected_layers(): synchronous layer-list/timeline
     * refreshes may briefly replay the original selection and would make the
     * gesture jump back to, and move, the source artwork. */
    if (alt_duplicate_active_) {
        std::set<std::string> seen_drag_ids;
        for (const auto &state : drag_layer_states_) {
            if (!seen_drag_ids.insert(state.id).second)
                continue;
            if (auto duplicated_layer = title_->find_layer(state.id))
                layers.push_back(duplicated_layer);
        }
    } else {
        layers = selected_layers();
    }
    if (layers.empty() || drag_mode_ == DragMode::None) return;

    const int transform_corner = static_cast<int>(drag_mode_) - static_cast<int>(DragMode::TransformCornerTL);
    if (transform_corner >= 0 && transform_corner < 4 && layers.size() == 1) {
        auto layer = layers.front();
        if (!layer || layer->locked || drag_layer_states_.empty()) return;
        const auto &state = drag_layer_states_.front();
        const QRectF box = layer_local_rect(*layer);
        if (box.width() <= 0.0 || box.height() <= 0.0) return;
        QPointF target_canvas = view_to_canvas(view_pt);
        const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
        target_canvas = snap_canvas_point(target_canvas, true, true, allow_snap);
        QPointF local_delta = canvas_to_layer(*layer, target_canvas) - canvas_to_layer(*layer, drag_start_canvas_);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            if (std::abs(local_delta.x()) >= std::abs(local_delta.y())) local_delta.setY(0.0);
            else local_delta.setX(0.0);
        }
        float q[8]; for (int i = 0; i < 8; ++i) q[i] = state.quad[i];
        const float dx = static_cast<float>(local_delta.x() / box.width());
        const float dy = static_cast<float>(local_delta.y() / box.height());
        q[transform_corner * 2] += dx; q[transform_corner * 2 + 1] += dy;
        if (free_transform_mode_ == 1) {
            const int mate = transform_corner ^ 3;
            q[mate * 2] -= dx; q[mate * 2 + 1] += dy;
        }
        if (modifiers.testFlag(Qt::AltModifier)) {
            const int opposite = transform_corner ^ 2;
            q[opposite * 2] -= dx; q[opposite * 2 + 1] -= dy;
        }
        editor_set_quad_value(*layer, layer->transform_quad_tl, layer->transform_quad_tl_x, layer->transform_quad_tl_y, playhead_, {q[0],q[1]});
        editor_set_quad_value(*layer, layer->transform_quad_tr, layer->transform_quad_tr_x, layer->transform_quad_tr_y, playhead_, {q[2],q[3]});
        editor_set_quad_value(*layer, layer->transform_quad_br, layer->transform_quad_br_x, layer->transform_quad_br_y, playhead_, {q[4],q[5]});
        editor_set_quad_value(*layer, layer->transform_quad_bl, layer->transform_quad_bl_x, layer->transform_quad_bl_y, playhead_, {q[6],q[7]});
        drag_changed_ = true; dirty_ = true; gpu_model_dirty_ = true;
        invalidate_canvas_overlay_caches();
        emit layer_geometry_changed();
        refresh_preview();
        return;
    }

    begin_adaptive_interaction();
    drag_current_view_ = view_pt;
    QPointF canvas = view_to_canvas(view_pt);
    QPointF delta = canvas - drag_start_canvas_;
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const double snap_tolerance = 6.0 / std::max(0.1, view_scale());
    auto snap_coordinate = [&](bool x_axis, double value, double *snapped) {
        if (!allow_snap || !snapped)
            return false;

        std::vector<double> targets;
        std::vector<QString> labels;
        collect_snap_targets(x_axis, targets, labels);
        collect_spacing_targets(x_axis, targets, labels);
        if (targets.empty())
            return false;

        double best_distance = snap_tolerance + 1.0;
        double best_target = value;
        QString best_label;
        for (size_t i = 0; i < targets.size(); ++i) {
            const double distance = std::abs(targets[i] - value);
            if (distance < best_distance) {
                best_distance = distance;
                best_target = targets[i];
                best_label = labels[i];
            }
        }
        if (best_distance > snap_tolerance)
            return false;

        *snapped = best_target;
        add_snap_feedback(x_axis, best_target, best_label);
        return true;
    };

    std::set<std::string> active_drag_ids;
    if (alt_duplicate_active_) {
        for (const auto &state : drag_layer_states_)
            active_drag_ids.insert(state.id);
    }
    auto selected_contains = [&](const std::string &id) {
        if (!active_drag_ids.empty())
            return active_drag_ids.find(id) != active_drag_ids.end();
        return std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), id) != selected_layer_ids_.end();
    };
    auto has_selected_parent = [&](const Layer &layer) {
        std::vector<std::string> pending;
        if (!layer.parent_id.empty()) pending.push_back(layer.parent_id);
        if (!layer.transform_parent_id.empty()) pending.push_back(layer.transform_parent_id);
        std::set<std::string> visited;
        while (!pending.empty()) {
            const std::string parent_id = pending.back();
            pending.pop_back();
            if (parent_id.empty() || !visited.insert(parent_id).second)
                continue;
            if (selected_contains(parent_id))
                return true;
            const Layer *parent = editor_find_layer_by_id(title_, parent_id);
            if (!parent) continue;
            if (!parent->parent_id.empty()) pending.push_back(parent->parent_id);
            if (!parent->transform_parent_id.empty()) pending.push_back(parent->transform_parent_id);
        }
        return false;
    };
    auto disable_text_auto_size_for_manual_resize = [](
        Layer &layer, bool width_changed, bool height_changed) {
        if (!is_canvas_text_layer(layer))
            return;
        /* A manual bounding-box dimension becomes authoritative immediately.
         * Disable only the matching auto-size axis; the other axis may continue
         * following its text content. This model change participates in the same
         * geometry undo transaction as the resize itself. */
        if (width_changed && layer.text_box_width_to_text)
            layer.text_box_width_to_text = false;
        if (height_changed && layer.text_box_height_to_text)
            layer.text_box_height_to_text = false;
    };

    if (drag_mode_ == DragMode::Rotate) {
        clear_snap_feedback();
        QPointF pivot_view = canvas_to_view(drag_rotation_pivot_canvas_);
        double current_angle = radians_to_degrees(std::atan2(view_pt.y() - pivot_view.y(),
                                                             view_pt.x() - pivot_view.x()));
        double rotation_delta = normalize_degrees(current_angle - drag_start_rotation_angle_);
        if (modifiers & Qt::ShiftModifier)
            rotation_delta = std::round(rotation_delta / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        drag_current_rotation_delta_ = rotation_delta;

        for (const auto &state : drag_layer_states_) {
            auto layer = title_->find_layer(state.id);
            if (!layer || layer->locked || has_selected_parent(*layer)) continue;
            double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                   std::max(0.0, layer->out_time - layer->in_time));
            if (layers.size() > 1) {
                const QPointF start_canvas_position =
                    editor_parent_local_point_to_canvas(
                        title_, *layer, playhead_, QPointF(state.x, state.y));
                const QPointF next_canvas_position = rotate_point_around(
                    start_canvas_position, drag_rotation_pivot_canvas_,
                    rotation_delta);
                const QPointF next_local_position =
                    editor_canvas_point_to_parent_local(
                        title_, *layer, playhead_, next_canvas_position);
                editor_set_local_position_xy(*layer, lt, next_local_position);
            }
            const double local_rotation_delta =
                editor_canvas_rotation_delta_to_local(
                    title_, *layer, playhead_, rotation_delta);
            set_animated_value(layer->rotation, lt,
                               state.rotation + local_rotation_delta);
        }
        dirty_ = true;
        drag_changed_ = true;
        refresh_preview();
        return;
    }

    if (layers.size() > 1) {
        if (drag_mode_ == DragMode::Move) {
            if (modifiers & Qt::ShiftModifier) {
                if (std::abs(delta.x()) >= std::abs(delta.y())) delta.setY(0.0);
                else delta.setX(0.0);
            }
            delta = snap_delta_for_bounds(drag_start_selection_bounds_, delta, true, true, allow_snap);
            for (const auto &state : drag_layer_states_) {
                auto layer = title_->find_layer(state.id);
                if (!layer || layer->locked || has_selected_parent(*layer)) continue;
                double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                       std::max(0.0, layer->out_time - layer->in_time));
                const QPointF local_delta = editor_canvas_delta_to_parent_local(
                    title_, *layer, playhead_, delta);
                editor_set_local_position_xy(
                    *layer, lt, QPointF(state.x, state.y) + local_delta);
            }
        } else {
            QRectF start = drag_start_selection_bounds_;
            if (!start.isValid() || start.width() <= 0.0 || start.height() <= 0.0) return;
            if (allow_snap)
                snap_feedback_.clear();
            QRectF next = start;
            bool resize_left = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeW;
            bool resize_right = drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeE;
            bool resize_top = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeN;
            bool resize_bottom = drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeS;
            if (resize_left) next.setLeft(std::min(canvas.x(), start.right()));
            if (resize_right) next.setRight(std::max(canvas.x(), start.left()));
            if (resize_top) next.setTop(std::min(canvas.y(), start.bottom()));
            if (resize_bottom) next.setBottom(std::max(canvas.y(), start.top()));
            double sx = next.width() / start.width();
            double sy = next.height() / start.height();
            if (modifiers & Qt::ShiftModifier) {
                double uniform = std::abs(sx) >= std::abs(sy) ? sx : sy;
                sx = sy = uniform;
            }
            next = QRectF(start.topLeft(), QSizeF(start.width() * sx, start.height() * sy)).normalized();
            if (resize_left)
                next.moveRight(start.right());
            else
                next.moveLeft(start.left());
            if (resize_top)
                next.moveBottom(start.bottom());
            else
                next.moveTop(start.top());

            double snapped_value = 0.0;
            if (resize_left && snap_coordinate(true, next.left(), &snapped_value))
                next.setLeft(std::min(snapped_value, next.right()));
            else if (resize_right && snap_coordinate(true, next.right(), &snapped_value))
                next.setRight(std::max(snapped_value, next.left()));
            if (resize_top && snap_coordinate(false, next.top(), &snapped_value))
                next.setTop(std::min(snapped_value, next.bottom()));
            else if (resize_bottom && snap_coordinate(false, next.bottom(), &snapped_value))
                next.setBottom(std::max(snapped_value, next.top()));

            sx = next.width() / start.width();
            sy = next.height() / start.height();
            for (const auto &state : drag_layer_states_) {
                auto layer = title_->find_layer(state.id);
                if (!layer || layer->locked || has_selected_parent(*layer)) continue;
                double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                                       std::max(0.0, layer->out_time - layer->in_time));
                const QPointF start_canvas_position =
                    editor_parent_local_point_to_canvas(
                        title_, *layer, playhead_, QPointF(state.x, state.y));
                const double rx =
                    (start_canvas_position.x() - start.left()) / start.width();
                const double ry =
                    (start_canvas_position.y() - start.top()) / start.height();
                const QPointF next_canvas_position(
                    next.left() + rx * next.width(),
                    next.top() + ry * next.height());
                const QPointF next_local_position =
                    editor_canvas_point_to_parent_local(
                        title_, *layer, playhead_, next_canvas_position);
                editor_set_local_position_xy(*layer, lt, next_local_position);
                const bool scale_text_object = layer_type_is_container(layer->type) ||
                    (drag_text_object_scaling_ && is_canvas_text_layer(*layer));
                if (scale_text_object) {
                    set_animated_x(layer->scale, lt, state.scale_x * sx);
                    set_animated_y(layer->scale, lt, state.scale_y * sy);
                } else {
                    const float resized_width =
                        std::max(0.0f, static_cast<float>(state.w * sx));
                    const float resized_height =
                        std::max(0.0f, static_cast<float>(state.h * sy));
                    disable_text_auto_size_for_manual_resize(
                        *layer, std::abs(resized_width - state.w) > 0.01f,
                        std::abs(resized_height - state.h) > 0.01f);
                    layer->rect_width = resized_width;
                    layer->rect_height = resized_height;
                    if (is_canvas_text_layer(*layer)) {
                        if (!layer->max_text_box_width_overridden)
                            layer->max_text_box_width = std::max(1.0f, resized_width);
                        if (!layer->max_text_box_height_overridden)
                            layer->max_text_box_height = std::max(1.0f, resized_height);
                    }
                    set_animated_x(layer->size, lt, layer->rect_width);
                    set_animated_y(layer->size, lt, layer->rect_height);
                    apply_shape_resize_metrics_from_state(*layer, state.w, state.h,
                                                          state.stroke_width,
                                                          state.corner_radius_tl,
                                                          state.corner_radius_tr,
                                                          state.corner_radius_br,
                                                          state.corner_radius_bl,
                                                          state.shape_roundness,
                                                          state.shape_inner_roundness,
                                                          state.path_corner_radii,
                                                          layer->rect_width,
                                                          layer->rect_height);
                }
            }
        }
        dirty_ = true;
        drag_changed_ = true;
        refresh_preview();
        return;
    }

    auto layer = layers.front();
    if (!layer) return;
    double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                           std::max(0.0, layer->out_time - layer->in_time));

    if (drag_mode_ == DragMode::Move) {
        if (modifiers & Qt::ShiftModifier) {
            if (std::abs(delta.x()) >= std::abs(delta.y()))
                delta.setY(0.0);
            else
                delta.setX(0.0);
        }
        delta = snap_delta_for_bounds(drag_start_selection_bounds_, delta, true, true, allow_snap);
        const QPointF local_delta = editor_canvas_delta_to_parent_local(
            title_, *layer, playhead_, delta);
        editor_set_local_position_xy(
            *layer, lt, QPointF(drag_start_x_, drag_start_y_) + local_delta);
    } else if (drag_mode_ == DragMode::Origin) {
        clear_snap_feedback();
        const double w = std::max(1.0f, drag_start_w_);
        const double h = std::max(1.0f, drag_start_h_);
        QPointF layer_local_delta;
        bool have_layer_local_delta = false;
        if (!drag_layer_states_.empty()) {
            const LayerDragState &state = drag_layer_states_.front();
            auto non_zero_scale = [](double value) {
                if (std::abs(value) >= 0.0001) return value;
                return value < 0.0 ? -0.0001 : 0.0001;
            };
            QTransform start_xf = editor_parent_world_transform(
                title_, *layer, playhead_);
            start_xf.translate(state.x, state.y);
            start_xf.rotate(state.rotation);
            start_xf.scale(non_zero_scale(state.scale_x),
                           non_zero_scale(state.scale_y));
            bool invertible = false;
            const QTransform canvas_to_start_layer =
                start_xf.inverted(&invertible);
            if (invertible) {
                layer_local_delta =
                    canvas_to_start_layer.map(drag_start_canvas_ + delta) -
                    canvas_to_start_layer.map(drag_start_canvas_);
                have_layer_local_delta = true;
            }
        }
        if (!have_layer_local_delta) {
            layer_local_delta =
                canvas_to_layer(*layer, drag_start_canvas_ + delta) -
                canvas_to_layer(*layer, drag_start_canvas_);
        }
        layer->origin_x = static_cast<float>(std::clamp(
            drag_start_origin_x_ + layer_local_delta.x() / w, 0.0, 1.0));
        layer->origin_y = static_cast<float>(std::clamp(
            drag_start_origin_y_ + layer_local_delta.y() / h, 0.0, 1.0));
        set_animated_x(layer->origin_prop, lt, layer->origin_x);
        set_animated_y(layer->origin_prop, lt, layer->origin_y);
        const QPointF parent_local_delta =
            editor_canvas_delta_to_parent_local(
                title_, *layer, playhead_, delta);
        editor_set_local_position_xy(
            *layer, lt,
            QPointF(drag_start_x_, drag_start_y_) + parent_local_delta);
    } else {
        bool resize_left = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeW;
        bool resize_right = drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeE;
        bool resize_top = drag_mode_ == DragMode::ResizeNW || drag_mode_ == DragMode::ResizeNE || drag_mode_ == DragMode::ResizeN;
        bool resize_bottom = drag_mode_ == DragMode::ResizeSW || drag_mode_ == DragMode::ResizeSE || drag_mode_ == DragMode::ResizeS;
        if (allow_snap)
            snap_feedback_.clear();
        const bool scale_text_object = layer_type_is_container(layer->type) ||
            (drag_text_object_scaling_ && is_canvas_text_layer(*layer));
        const LayerDragState *start_state = drag_layer_states_.empty() ? nullptr : &drag_layer_states_.front();
        auto non_zero_scale = [](double value) {
            if (std::abs(value) >= 0.0001) return value;
            return value < 0.0 ? -0.0001 : 0.0001;
        };
        auto start_canvas_to_layer = [&](const QPointF &canvas_pt) {
            QTransform start_xf = editor_parent_world_transform(
                title_, *layer, playhead_);
            start_xf.translate(start_state->x, start_state->y);
            start_xf.rotate(start_state->rotation);
            start_xf.scale(non_zero_scale(start_state->scale_x),
                           non_zero_scale(start_state->scale_y));
            return start_xf.inverted().map(canvas_pt);
        };
        const bool lock_aspect_resize =
            ((layer_type_is_image_like(layer->type) && layer->image_box_lock_aspect_ratio) ||
             (layer_is_shape_sized(*layer) && layer->lock_aspect_ratio)) &&
            drag_start_h_ > 0.0f;
        Qt::KeyboardModifiers resize_modifiers = modifiers;
        if (lock_aspect_resize)
            resize_modifiers |= Qt::ShiftModifier;
        QPointF local = start_state ? start_canvas_to_layer(canvas)
                                    : canvas_to_layer(*layer, canvas);
        const double left = start_state ? start_state->local_left
                                        : -drag_start_origin_x_ * drag_start_w_;
        const double top = start_state ? start_state->local_top
                                       : -drag_start_origin_y_ * drag_start_h_;
        const double right = left + drag_start_w_;
        const double bottom = top + drag_start_h_;
        /* Use the exact same local bounds that produced the visible canvas
         * outline. This is essential for Groups, whose bounds are derived from
         * their composited descendants and are not necessarily centred on the
         * Group origin. */
        const QRectF start_rect(QPointF(left, top), QPointF(right, bottom));
        const bool corner_resize = (resize_left || resize_right) && (resize_top || resize_bottom);
        const double aspect = drag_start_h_ > 0.0f ? (double)drag_start_w_ / drag_start_h_ : 1.0;

        QPointF anchor = start_rect.center();
        double fixed_w = 0.0;
        double fixed_h = 0.0;
        if (resize_modifiers.testFlag(Qt::AltModifier)) {
            // Alt-resize is center-based. Side handles must stay single-axis:
            // E/W only change width and N/S only change height. The modifier
            // helper doubles dimensions around the anchor for Alt, so pass half
            // the unchanged dimension to preserve the original opposite axis.
            if (!corner_resize) {
                if (resize_left || resize_right)
                    fixed_h = start_rect.height() * 0.5;
                else
                    fixed_w = start_rect.width() * 0.5;
            }
        } else {
            if (corner_resize) {
                anchor = QPointF(resize_left ? start_rect.right() : start_rect.left(),
                                 resize_top ? start_rect.bottom() : start_rect.top());
            } else if (resize_left || resize_right) {
                anchor = QPointF(resize_left ? start_rect.right() : start_rect.left(),
                                 start_rect.center().y());
                fixed_h = start_rect.height();
            } else {
                anchor = QPointF(start_rect.center().x(),
                                 resize_top ? start_rect.bottom() : start_rect.top());
                fixed_w = start_rect.width();
            }
        }

        const QRectF resized_rect = editor_modifier_rect(anchor, local, resize_modifiers, aspect, fixed_w, fixed_h);
        QTransform start_xf = editor_parent_world_transform(
            title_, *layer, playhead_);
        if (start_state) {
            start_xf.translate(start_state->x, start_state->y);
            start_xf.rotate(start_state->rotation);
            start_xf.scale(non_zero_scale(start_state->scale_x),
                           non_zero_scale(start_state->scale_y));
        }
        QRectF final_rect = resized_rect;
        if (allow_snap && start_state) {
            QPointF handle_local = local;
            if (resize_left || resize_right)
                handle_local.setX(resize_left ? resized_rect.left() : resized_rect.right());
            if (resize_top || resize_bottom)
                handle_local.setY(resize_top ? resized_rect.top() : resized_rect.bottom());

            QPointF handle_canvas = start_xf.map(handle_local);
            bool snapped_any = false;
            double snapped_value = 0.0;
            QPolygonF proposed_poly;
            proposed_poly << start_xf.map(resized_rect.topLeft())
                          << start_xf.map(resized_rect.topRight())
                          << start_xf.map(resized_rect.bottomRight())
                          << start_xf.map(resized_rect.bottomLeft());
            const QRectF proposed_bounds = proposed_poly.boundingRect();
            if (resize_left && snap_coordinate(true, proposed_bounds.left(), &snapped_value)) {
                handle_canvas.setX(handle_canvas.x() + snapped_value - proposed_bounds.left());
                snapped_any = true;
            } else if (resize_right && snap_coordinate(true, proposed_bounds.right(), &snapped_value)) {
                handle_canvas.setX(handle_canvas.x() + snapped_value - proposed_bounds.right());
                snapped_any = true;
            }
            if (resize_top && snap_coordinate(false, proposed_bounds.top(), &snapped_value)) {
                handle_canvas.setY(handle_canvas.y() + snapped_value - proposed_bounds.top());
                snapped_any = true;
            } else if (resize_bottom && snap_coordinate(false, proposed_bounds.bottom(), &snapped_value)) {
                handle_canvas.setY(handle_canvas.y() + snapped_value - proposed_bounds.bottom());
                snapped_any = true;
            }
            if (snapped_any) {
                const QPointF snapped_local = start_xf.inverted().map(handle_canvas);
                final_rect = editor_modifier_rect(anchor, snapped_local, resize_modifiers, aspect, fixed_w, fixed_h);
            }
        }
        const double new_w = std::max(1.0, final_rect.width());
        const double new_h = std::max(1.0, final_rect.height());
        if (scale_text_object) {
            const double sx = drag_start_w_ > 0.0f ? new_w / drag_start_w_ : 1.0;
            const double sy = drag_start_h_ > 0.0f ? new_h / drag_start_h_ : 1.0;
            const double start_scale_x = start_state ? start_state->scale_x
                                                     : layer->scale.evaluate(lt).x;
            const double start_scale_y = start_state ? start_state->scale_y
                                                     : layer->scale.evaluate(lt).y;
            set_animated_x(layer->scale, lt, start_scale_x * sx);
            set_animated_y(layer->scale, lt, start_scale_y * sy);

            if (start_state) {
                /* final_rect is expressed in the original layer-local basis.
                 * Scaling changes the artwork around the layer origin, which is
                 * not necessarily the fixed opposite handle. Translate after
                 * scaling so the visual rectangle lands exactly on final_rect.
                 * This is the same cursor/anchor contract used by size-backed
                 * shapes and also covers Groups and Asset Layers whose bounds
                 * can be offset from their own origin. */
                const QPointF desired_center_canvas = start_xf.map(final_rect.center());
                QTransform next_xf = editor_parent_world_transform(
                    title_, *layer, playhead_);
                next_xf.translate(start_state->x, start_state->y);
                next_xf.rotate(start_state->rotation);
                next_xf.scale(non_zero_scale(start_scale_x * sx),
                              non_zero_scale(start_scale_y * sy));
                const QPointF current_center_canvas = next_xf.map(start_rect.center());
                const QPointF canvas_shift = desired_center_canvas - current_center_canvas;
                const QPointF parent_local_shift = editor_canvas_delta_to_parent_local(
                    title_, *layer, playhead_, canvas_shift);
                editor_set_local_position_xy(
                    *layer, lt,
                    QPointF(start_state->x, start_state->y) +
                        parent_local_shift);
            }
        } else {
            const float start_width = start_state ? start_state->w
                                                   : drag_start_w_;
            const float start_height = start_state ? start_state->h
                                                    : drag_start_h_;
            disable_text_auto_size_for_manual_resize(
                *layer, std::abs(new_w - start_width) > 0.01,
                std::abs(new_h - start_height) > 0.01);
            layer->rect_width = static_cast<float>(new_w);
            layer->rect_height = static_cast<float>(new_h);
            if (is_canvas_text_layer(*layer)) {
                if (!layer->max_text_box_width_overridden)
                    layer->max_text_box_width = std::max(1.0f, layer->rect_width);
                if (!layer->max_text_box_height_overridden)
                    layer->max_text_box_height = std::max(1.0f, layer->rect_height);
            }
            set_animated_x(layer->size, lt, new_w);
            set_animated_y(layer->size, lt, new_h);
            if (start_state) {
                apply_shape_resize_metrics_from_state(*layer, start_state->w, start_state->h,
                                                      start_state->stroke_width,
                                                      start_state->corner_radius_tl,
                                                      start_state->corner_radius_tr,
                                                      start_state->corner_radius_br,
                                                      start_state->corner_radius_bl,
                                                      start_state->shape_roundness,
                                                      start_state->shape_inner_roundness,
                                                      start_state->path_corner_radii,
                                                      new_w,
                                                      new_h);

                /* Size-backed layers must preserve their origin as a fraction
                 * of the resized dimensions. Reusing local_left/local_top as
                 * absolute coordinates makes the visual centre drift and the
                 * grabbed handle run away from the mouse. */
                const double origin_x = start_state->w > 0.0f
                    ? -start_state->local_left / start_state->w
                    : drag_start_origin_x_;
                const double origin_y = start_state->h > 0.0f
                    ? -start_state->local_top / start_state->h
                    : drag_start_origin_y_;
                const QRectF actual_rect(-origin_x * new_w,
                                         -origin_y * new_h,
                                         new_w, new_h);
                const QPointF desired_center_canvas = start_xf.map(final_rect.center());
                const QPointF current_center_canvas = start_xf.map(actual_rect.center());
                const QPointF canvas_shift = desired_center_canvas - current_center_canvas;
                const QPointF parent_local_shift = editor_canvas_delta_to_parent_local(
                    title_, *layer, playhead_, canvas_shift);
                editor_set_local_position_xy(
                    *layer, lt,
                    QPointF(start_state->x, start_state->y) +
                        parent_local_shift);
            }
        }
    }
