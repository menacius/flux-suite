bool CanvasPreview::apply_gradient_target_fill(
    Layer &layer, const GradientHandleTarget &target, const RichTextFill &fill)
{
    if (target.kind == GradientHandleTarget::Kind::LayerFill) {
        layer.fill_type = 1;
        layer.gradient_type = fill.gradient_type;
        layer.gradient_center_x = fill.gradient_center_x;
        layer.gradient_center_y = fill.gradient_center_y;
        layer.gradient_focal_x = fill.gradient_focal_x;
        layer.gradient_focal_y = fill.gradient_focal_y;
        layer.gradient_scale = fill.gradient_scale;
        layer.gradient_angle = fill.gradient_angle;
        return true;
    }

    if (!is_canvas_text_layer(layer))
        return false;
    if (target.selection_target)
        return apply_selected_text_gradient_fill(layer, fill);

    if (layer.rich_text.empty())
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
    RichTextCharFormat format;
    format.fill = fill;
    if (target.text_ranges.empty()) {
        layer.rich_text.default_format.fill = fill;
        if (layer.rich_text.has_typing_format) {
            layer.rich_text.typing_format.fill = fill;
            layer.rich_text.typing_format_mask |= RichTextCharFillColor;
        }
    } else {
        for (const auto &[range_start, range_length] : target.text_ranges) {
            if (range_length == 0)
                continue;
            rich_text_document_apply_format(layer.rich_text, range_start, range_length,
                                            format, RichTextCharFillColor);
        }
    }
    rich_text_document_sync_layer_mirrors(layer);
    return true;
}

bool CanvasPreview::apply_gradient_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!gradient_drag_.active)
        return false;
    if (drag_mode_ != DragMode::GradientStart && drag_mode_ != DragMode::GradientEnd &&
        drag_mode_ != DragMode::GradientCenter && drag_mode_ != DragMode::GradientRadius &&
        drag_mode_ != DragMode::GradientFocal)
        return false;

    auto layer = selected_layer();
    if (!layer || layer->locked)
        return false;

    const QRectF box = gradient_drag_.local_rect;
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return false;

    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const QPointF snapped_canvas = snap_canvas_point(view_to_canvas(view_pt), true, true,
                                                     allow_snap);
    const QPointF local = canvas_to_layer(*layer, snapped_canvas);
    auto normalized = [&](const QPointF &pt) {
        return QPointF((pt.x() - box.left()) / box.width(),
                       (pt.y() - box.top()) / box.height());
    };
    RichTextFill fill = gradient_drag_.fill;
    auto assign_center = [&](const QPointF &pt) {
        const QPointF n = normalized(pt);
        fill.gradient_center_x = (float)n.x();
        fill.gradient_center_y = (float)n.y();
    };
    auto assign_focal = [&](const QPointF &pt) {
        const QPointF n = normalized(pt);
        fill.gradient_focal_x = (float)n.x();
        fill.gradient_focal_y = (float)n.y();
    };
    auto assign_axis = [&](const QPointF &a, const QPointF &b) {
        const QPointF c((a.x() + b.x()) * 0.5, (a.y() + b.y()) * 0.5);
        assign_center(c);
        const QPointF delta = b - a;
        const double distance = std::max(1.0, QLineF(a, b).length());
        double angle = radians_to_degrees(std::atan2(delta.y(), delta.x()));
        if (modifiers.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        fill.gradient_angle = (float)normalize_degrees(angle);
        const double base = std::max(1.0, std::hypot(box.width(), box.height()));
        fill.gradient_scale = (float)std::clamp(distance / base, 0.01, 100.0);
    };

    /* Preserve the original unrestricted center/radius editing, but never let
     * the focal point cross the active radial circle. A focal point on or
     * outside the radius can invert the radial equation or produce malformed
     * output in the Qt fallback and GPU shader. */
    auto constrain_radial_fill = [&]() {
        if (!gradient_drag_.radial)
            return;

        QPointF center(box.left() + (double)fill.gradient_center_x * box.width(),
                       box.top() + (double)fill.gradient_center_y * box.height());
        const double base = std::max(1.0, std::max(box.width(), box.height()) * 0.5);
        const double radius = std::max(1.0, base * std::max(0.01, (double)fill.gradient_scale));
        fill.gradient_scale = (float)std::clamp(radius / base, 0.01, 100.0);

        QPointF focal(box.left() + (double)fill.gradient_focal_x * box.width(),
                      box.top() + (double)fill.gradient_focal_y * box.height());
        QPointF focal_delta = focal - center;
        const double focal_distance = std::hypot(focal_delta.x(), focal_delta.y());
        const double maximum_focal_distance = std::max(0.0, radius * 0.98);
        if (focal_distance > maximum_focal_distance && focal_distance > 1e-9)
            focal = center + focal_delta * (maximum_focal_distance / focal_distance);
        assign_focal(focal);
    };

    if (drag_mode_ == DragMode::GradientCenter) {
        const QPointF delta = local - gradient_drag_.center;
        assign_center(gradient_drag_.center + delta);
        if (gradient_drag_.radial)
            assign_focal(gradient_drag_.focal + delta);
    } else if (drag_mode_ == DragMode::GradientFocal) {
        assign_focal(local);
    } else if (drag_mode_ == DragMode::GradientRadius) {
        assign_center(gradient_drag_.center);
        const QPointF delta = local - gradient_drag_.center;
        const double radius = std::max(1.0, QLineF(gradient_drag_.center, local).length());
        const double base = std::max(1.0, std::max(box.width(), box.height()) * 0.5);
        double angle = radians_to_degrees(std::atan2(delta.y(), delta.x()));
        if (modifiers.testFlag(Qt::ShiftModifier))
            angle = std::round(angle / CANVAS_ROTATION_SNAP_DEGREES) * CANVAS_ROTATION_SNAP_DEGREES;
        fill.gradient_angle = (float)normalize_degrees(angle);
        fill.gradient_scale = (float)std::clamp(radius / base, 0.01, 100.0);
    } else if (drag_mode_ == DragMode::GradientStart) {
        assign_axis(local, gradient_drag_.end);
    } else if (drag_mode_ == DragMode::GradientEnd) {
        assign_axis(gradient_drag_.start, local);
    }

    constrain_radial_fill();

    if (!apply_gradient_target_fill(*layer, gradient_drag_.target, fill))
        return false;
    gradient_drag_.fill = fill;
    gradient_drag_.target.fill = fill;

    begin_adaptive_interaction();
    dirty_ = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    invalidate_canvas_overlay_caches();
    update();
    return true;
}


bool CanvasPreview::begin_gradient_tool_drag(const QPointF &view_pt,
                                             Qt::KeyboardModifiers modifiers)
{
    auto layer = selected_layer();
    if (!layer || layer->locked || !layer->visible)
        return false;
    if (!(layer->type == LayerType::SolidRect || layer->type == LayerType::Shape ||
          is_canvas_text_layer(*layer)))
        return false;

    QRectF box = layer_local_rect(*layer);
    if (is_canvas_text_layer(*layer))
        box = text_rect_for_style(box, *layer);
    if (!box.isValid() || box.width() <= 0.0 || box.height() <= 0.0)
        return false;

    GradientHandleTarget target;
    RichTextFill fill;
    const bool inline_text_fill =
        is_canvas_text_layer(*layer) && inline_text_layer_id_ == layer->id;
    if (is_canvas_text_layer(*layer)) {
        target.kind = GradientHandleTarget::Kind::TextFillRanges;
        if (inline_text_fill) {
            const double local_time = std::max(0.0, playhead_ - layer->in_time);
            const RichTextCharFormatSummary summary =
                summarize_rich_text_char_format(*layer, true, local_time);
            fill = summary.valid ? summary.format.fill
                                 : rich_text_effective_typing_format(layer->rich_text).fill;
            const size_t selection_start = std::min(layer->rich_text.selection.anchor,
                                                    layer->rich_text.selection.head);
            const size_t selection_end = std::max(layer->rich_text.selection.anchor,
                                                  layer->rich_text.selection.head);
            if (selection_end > selection_start)
                target.text_ranges.push_back({selection_start, selection_end - selection_start});
            target.selection_target = true;
        } else {
            RichTextDocument doc = layer->rich_text.empty()
                ? rich_text_document_from_layer_defaults(*layer)
                : layer->rich_text;
            fill = doc.default_format.fill;
            if (!doc.plain_text.empty())
                target.text_ranges.push_back({0, doc.plain_text.size()});
        }
    } else {
        target.kind = GradientHandleTarget::Kind::LayerFill;
        fill.type = layer->fill_type;
        fill.gradient_type = layer->gradient_type;
        fill.gradient_center_x = layer->gradient_center_x;
        fill.gradient_center_y = layer->gradient_center_y;
        fill.gradient_focal_x = layer->gradient_focal_x;
        fill.gradient_focal_y = layer->gradient_focal_y;
        fill.gradient_scale = layer->gradient_scale;
        fill.gradient_angle = layer->gradient_angle;
    }

    fill.type = 1;
    if (fill.gradient_type < 0 || fill.gradient_type > 2)
        fill.gradient_type = 0;
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const QPointF snapped_canvas = snap_canvas_point(view_to_canvas(view_pt), true, true,
                                                     allow_snap);
    const QPointF local = canvas_to_layer(*layer, snapped_canvas);
    gradient_tool_dragging_ = true;
    gradient_tool_start_local_ = local;
    drag_start_selection_bounds_ = selected_canvas_bounds();
    drag_mode_ = fill.gradient_type == 1 ? DragMode::GradientRadius
                                         : DragMode::GradientEnd;

    auto normalized = [&](const QPointF &point) {
        return QPointF((point.x() - box.left()) / box.width(),
                       (point.y() - box.top()) / box.height());
    };
    const QPointF n = normalized(local);
    fill.gradient_center_x = (float)n.x();
    fill.gradient_center_y = (float)n.y();
    fill.gradient_focal_x = (float)n.x();
    fill.gradient_focal_y = (float)n.y();
    fill.gradient_scale = 0.01f;
    fill.gradient_angle = modifiers.testFlag(Qt::ShiftModifier) ? 0.0f
                                                                 : fill.gradient_angle;
    target.fill = fill;
    if (!apply_gradient_target_fill(*layer, target, fill))
        return false;

    begin_gradient_drag(*layer, target);
    begin_adaptive_interaction();
    dirty_ = true;
    drag_changed_ = true;
    invalidate_canvas_overlay_caches();
    update();
    return true;
}

bool CanvasPreview::layer_supports_corner_radius_handles(const Layer &layer) const
{
    if (layer.locked || !layer.visible ||
        (layer.type != LayerType::Shape && layer.type != LayerType::SolidRect))
        return false;
    if (playhead_ < layer.in_time || playhead_ > layer.out_time)
        return false;
    return !fxm::layer_live_corners(layer, layer_local_rect(layer)).empty();
}

void CanvasPreview::notify_corner_context_changed()
{
    emit corner_context_changed();
}

void CanvasPreview::clear_corner_selection(bool notify)
{
    const bool changed = !selected_corner_layer_id_.empty() || !selected_corner_indices_.empty();
    selected_corner_layer_id_.clear();
    selected_corner_indices_.clear();
    if (changed) {
        invalidate_canvas_overlay_caches();
        if (notify)
            notify_corner_context_changed();
        update();
    }
}

std::vector<int> CanvasPreview::corner_indices_for_layer(const Layer &layer, bool selected_only) const
{
    std::vector<int> result;
    const auto corners = fxm::layer_live_corners(layer, layer_local_rect(layer));
    result.reserve(corners.size());
    for (const auto &corner : corners) {
        if (selected_only &&
            (selected_corner_layer_id_ != layer.id ||
             selected_corner_indices_.find(corner.point_index) == selected_corner_indices_.end()))
            continue;
        result.push_back(corner.point_index);
    }
    return result;
}

std::vector<int> CanvasPreview::mapped_corner_indices(const Layer &source, const Layer &target,
                                                       const std::vector<int> &source_indices) const
{
    const auto source_corners = fxm::layer_live_corners(source, layer_local_rect(source));
    const auto target_corners = fxm::layer_live_corners(target, layer_local_rect(target));
    std::vector<int> result;
    if (source_corners.empty() || target_corners.empty() || source_indices.empty())
        return result;

    for (int source_index : source_indices) {
        auto source_it = std::find_if(source_corners.begin(), source_corners.end(),
            [source_index](const fxm::LiveCornerGeometry &corner) {
                return corner.point_index == source_index;
            });
        if (source_it == source_corners.end())
            continue;
        const size_t source_position = (size_t)std::distance(source_corners.begin(), source_it);

        auto exact = std::find_if(target_corners.begin(), target_corners.end(),
            [source_index](const fxm::LiveCornerGeometry &corner) {
                return corner.point_index == source_index;
            });
        if (source_corners.size() == target_corners.size() && exact != target_corners.end()) {
            result.push_back(exact->point_index);
            continue;
        }

        const double fraction = source_corners.size() <= 1
            ? 0.0
            : (double)source_position / (double)(source_corners.size() - 1);
        const size_t target_position = (size_t)std::clamp(
            (long long)std::llround(fraction * (double)(target_corners.size() - 1)),
            0LL, (long long)target_corners.size() - 1);
        result.push_back(target_corners[target_position].point_index);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool CanvasPreview::corner_controls_available() const
{
    if (active_tool_ == CanvasTool::DirectSelection && !selected_path_point_indices_.empty())
        return !selected_corner_layer_id_.empty() && !selected_corner_indices_.empty();
    for (const auto &layer : selected_layers()) {
        if (layer && layer_supports_corner_radius_handles(*layer))
            return true;
    }
    return false;
}

double CanvasPreview::corner_control_radius(bool *mixed) const
{
    if (mixed) *mixed = false;
    auto layers = selected_layers();
    if (layers.empty())
        return 0.0;

    std::shared_ptr<Layer> source;
    if (!selected_corner_layer_id_.empty() && title_)
        source = title_->find_layer(selected_corner_layer_id_);
    if (!source)
        source = selected_layer();
    if (!source)
        source = layers.back();
    if (!source || !layer_supports_corner_radius_handles(*source))
        return 0.0;

    const bool selected_only = selected_corner_layer_id_ == source->id && !selected_corner_indices_.empty();
    const std::vector<int> source_indices = corner_indices_for_layer(*source, selected_only);
    bool have_value = false;
    double value = 0.0;
    bool is_mixed = false;
    for (const auto &layer : layers) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        const std::vector<int> indices = selected_only
            ? mapped_corner_indices(*source, *layer, source_indices)
            : corner_indices_for_layer(*layer, false);
        for (int index : indices) {
            const double current = corner_radius_value(*layer, index);
            if (!have_value) {
                value = current;
                have_value = true;
            } else if (std::abs(current - value) > 1e-4) {
                is_mixed = true;
            }
        }
    }
    if (mixed) *mixed = is_mixed;
    return have_value ? value : 0.0;
}

bool CanvasPreview::corner_control_sync(bool *mixed) const
{
    if (mixed) *mixed = false;
    bool have_value = false;
    bool value = true;
    bool is_mixed = false;
    for (const auto &layer : selected_layers()) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        if (!have_value) {
            value = layer->corner_radius_locked;
            have_value = true;
        } else if (layer->corner_radius_locked != value) {
            is_mixed = true;
        }
    }
    if (mixed) *mixed = is_mixed;
    return have_value ? value : true;
}

double CanvasPreview::corner_control_bevel_roundness(bool *mixed) const
{
    if (mixed) *mixed = false;
    bool have_value = false;
    double value = 100.0;
    bool is_mixed = false;
    for (const auto &layer : selected_layers()) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        if (!have_value) {
            value = layer->corner_bevel_roundness;
            have_value = true;
        } else if (std::abs(layer->corner_bevel_roundness - value) > 1e-4) {
            is_mixed = true;
        }
    }
    if (mixed) *mixed = is_mixed;
    return have_value ? value : 100.0;
}

bool CanvasPreview::corner_radius_handle_view_geometry(
    const Layer &layer, const fxm::LiveCornerGeometry &corner,
    QPointF &anchor_view, QPointF &handle_view) const
{
    constexpr double kCornerRadiusHandleOffsetPx = 20.0;
    const QPointF toward_previous = corner.previous - corner.anchor;
    const QPointF toward_next = corner.next - corner.anchor;
    const double previous_length = std::hypot(toward_previous.x(), toward_previous.y());
    const double next_length = std::hypot(toward_next.x(), toward_next.y());

    const QRectF local_rect = layer_local_rect(layer);
    const auto projected_title = projection_title();
    const bool projected_3d = projected_title &&
        fxm::transform3d::layer_or_ancestor_uses_3d(*projected_title, layer);
    const qreal overlay_pixel_scale = std::max<qreal>(1.0, devicePixelRatioF());

    auto local_to_view = [&](const QPointF &local_point, QPointF &view_point) {
        if (!projected_3d || !projected_title) {
            view_point = canvas_to_view(layer_to_canvas(layer, local_point));
            return std::isfinite(view_point.x()) && std::isfinite(view_point.y());
        }

        const QPointF warped = editor_warped_layer_local_point(
            layer, local_rect, local_point, playhead_);
        QVector4D clip;
        if (!fxm::transform3d::layer_local_clip_point(
                *projected_title, layer, playhead_,
                QVector3D(static_cast<float>(warped.x()),
                          static_cast<float>(warped.y()), 0.0f), clip))
            return false;
        const double w = static_cast<double>(clip.w());
        if (!std::isfinite(w) || w <= 1.0e-8)
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
            (ndc_x + 1.0) * 0.5 * std::max(1, projected_title->width),
            (1.0 - ndc_y) * 0.5 * std::max(1, projected_title->height));
        view_point = canvas_to_view(canvas_point);
        view_point.setX(std::round(view_point.x() * overlay_pixel_scale) /
                        overlay_pixel_scale);
        view_point.setY(std::round(view_point.y() * overlay_pixel_scale) /
                        overlay_pixel_scale);
        return std::isfinite(view_point.x()) && std::isfinite(view_point.y());
    };

    if (!local_to_view(corner.anchor, anchor_view))
        return false;
    if (previous_length <= 1e-9 || next_length <= 1e-9) {
        handle_view = anchor_view;
        return true;
    }

    const QPointF previous_unit = toward_previous / previous_length;
    const QPointF next_unit = toward_next / next_length;
    QPointF inward = previous_unit + next_unit;
    const double inward_length = std::hypot(inward.x(), inward.y());
    if (inward_length <= 1e-9) {
        handle_view = anchor_view;
        return true;
    }
    inward /= inward_length;

    const double cosine = std::clamp(
        QPointF::dotProduct(previous_unit, next_unit), -1.0, 1.0);
    const double sin_half_angle = std::max(
        1e-6, std::sqrt(std::max(0.0, (1.0 - cosine) * 0.5)));
    const double center_distance = corner.radius / sin_half_angle;
    const QPointF geometric_local = corner.anchor + inward * center_distance;
    QPointF geometric_view;
    QPointF inward_view_probe;
    if (!local_to_view(geometric_local, geometric_view) ||
        !local_to_view(corner.anchor + inward, inward_view_probe))
        return false;

    const QPointF inward_view_delta = inward_view_probe - anchor_view;
    const double inward_view_length = std::hypot(
        inward_view_delta.x(), inward_view_delta.y());
    const QPointF inward_view = inward_view_length > 1e-9
        ? inward_view_delta / inward_view_length
        : QPointF();
    const double radius_fraction = corner.max_radius > 1e-9
        ? std::clamp(corner.radius / corner.max_radius, 0.0, 1.0)
        : 0.0;
    const double offset_px = kCornerRadiusHandleOffsetPx *
        (1.0 - 2.0 * radius_fraction);
    handle_view = geometric_view + inward_view * offset_px;
    if (!std::isfinite(handle_view.x()) || !std::isfinite(handle_view.y()))
        return false;

    /* Corner controls are optional editor chrome. Hide them once their
     * projected anchors leave the viewport instead of feeding QPainter huge
     * near-plane coordinates, which caused the remaining 3D overlay flicker. */
    if (projected_3d) {
        const QRectF padded_view = QRectF(rect()).adjusted(-24.0, -24.0,
                                                        24.0, 24.0);
        if (!padded_view.contains(anchor_view) ||
            !padded_view.contains(handle_view))
            return false;
    }
    return true;
}

QPointF CanvasPreview::corner_radius_handle_view_pos(
    const Layer &layer, const fxm::LiveCornerGeometry &corner) const
{
    QPointF anchor_view;
    QPointF handle_view;
    if (!corner_radius_handle_view_geometry(
            layer, corner, anchor_view, handle_view))
        return QPointF(std::numeric_limits<qreal>::quiet_NaN(),
                       std::numeric_limits<qreal>::quiet_NaN());
    return handle_view;
}

int CanvasPreview::hit_test_corner_radius_handle_index(const Layer &layer, const QPointF &view_pt) const
{
    if (!layer_supports_corner_radius_handles(layer))
        return -1;
    const auto corners = fxm::layer_live_corners(layer, layer_local_rect(layer));
    double best_distance = CANVAS_CONTROL_HIT_RADIUS_PX * 1.5;
    int best_index = -1;
    for (const auto &corner : corners) {
        QPointF anchor_view;
        QPointF handle_view;
        if (!corner_radius_handle_view_geometry(
                layer, corner, anchor_view, handle_view))
            continue;
        const double distance = QLineF(view_pt, handle_view).length();
        if (distance <= best_distance) {
            best_distance = distance;
            best_index = corner.point_index;
        }
    }
    return best_index;
}

std::shared_ptr<Layer> CanvasPreview::hit_test_selected_corner_layer(const QPointF &view_pt,
                                                                     int &point_index) const
{
    point_index = -1;
    auto layers = selected_layers();
    if (layers.empty())
        return nullptr;

    if (auto primary = selected_layer()) {
        point_index = hit_test_corner_radius_handle_index(*primary, view_pt);
        if (point_index >= 0)
            return primary;
    }
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || (selected_layer() && layer->id == selected_layer()->id))
            continue;
        point_index = hit_test_corner_radius_handle_index(*layer, view_pt);
        if (point_index >= 0)
            return layer;
    }
    return nullptr;
}

CanvasPreview::DragMode CanvasPreview::hit_test_corner_radius_handles(const Layer &layer,
                                                                      const QPointF &view_pt) const
{
    return hit_test_corner_radius_handle_index(layer, view_pt) >= 0
        ? DragMode::CornerRadius
        : DragMode::None;
}

double CanvasPreview::corner_radius_value(const Layer &layer, int point_index) const
{
    if (layer.shape_type == ShapeType::Path) {
        if (point_index >= 0 && (size_t)point_index < layer.path_points.size())
            return std::max(0.0, layer.path_points[(size_t)point_index].corner_radius);
        return 0.0;
    }
    if (layer.shape_type == ShapeType::Rectangle || layer.shape_type == ShapeType::RoundedRectangle) {
        switch (point_index) {
        case 0: return layer.corner_radius_tl;
        case 1: return layer.corner_radius_tr;
        case 2: return layer.corner_radius_br;
        case 3: return layer.corner_radius_bl;
        default: return 0.0;
        }
    }
    if (layer.shape_type == ShapeType::Star && point_index % 2 != 0)
        return std::max(0.0, (double)layer.shape_inner_roundness);
    return std::max(0.0, (double)layer.shape_roundness);
}

void CanvasPreview::set_corner_radius_value(Layer &layer, int point_index, double radius, bool affect_group)
{
    radius = std::max(0.0, radius);
    if (layer.shape_type == ShapeType::Path) {
        if (affect_group) {
            const auto corners = fxm::layer_live_corners(layer, layer_local_rect(layer));
            for (const auto &corner : corners) {
                if (corner.point_index >= 0 && (size_t)corner.point_index < layer.path_points.size())
                    layer.path_points[(size_t)corner.point_index].corner_radius = radius;
            }
        } else if (point_index >= 0 && (size_t)point_index < layer.path_points.size()) {
            layer.path_points[(size_t)point_index].corner_radius = radius;
        }
        return;
    }

    if (layer.shape_type == ShapeType::Rectangle || layer.shape_type == ShapeType::RoundedRectangle) {
        if (affect_group) {
            set_layer_all_corner_radii(layer, (float)radius);
        } else {
            switch (point_index) {
            case 0: layer.corner_radius_tl = (float)radius; break;
            case 1: layer.corner_radius_tr = (float)radius; break;
            case 2: layer.corner_radius_br = (float)radius; break;
            case 3: layer.corner_radius_bl = (float)radius; break;
            default: break;
            }
            set_layer_corner_radii(layer, layer.corner_radius_tl, layer.corner_radius_tr,
                                   layer.corner_radius_br, layer.corner_radius_bl);
        }
        return;
    }

    if (layer.shape_type == ShapeType::Star && point_index % 2 != 0)
        layer.shape_inner_roundness = (float)radius;
    else
        layer.shape_roundness = (float)radius;
}

void CanvasPreview::set_corner_control_radius(double radius)
{
    radius = std::max(0.0, radius);
    auto layers = selected_layers();
    if (layers.empty())
        return;
    std::shared_ptr<Layer> source = !selected_corner_layer_id_.empty() && title_
        ? title_->find_layer(selected_corner_layer_id_) : selected_layer();
    if (!source) source = layers.back();
    if (!source) return;

    const bool selected_only = selected_corner_layer_id_ == source->id && !selected_corner_indices_.empty();
    const std::vector<int> source_indices = corner_indices_for_layer(*source, selected_only);
    bool changed = false;
    for (auto &layer : layers) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        std::vector<int> indices = selected_only
            ? mapped_corner_indices(*source, *layer, source_indices)
            : corner_indices_for_layer(*layer, false);
        if (selected_only && layer->shape_type != ShapeType::Path &&
            layer->shape_type != ShapeType::Rectangle &&
            layer->shape_type != ShapeType::RoundedRectangle) {
            if (fxm::ensure_editable_path(*layer))
                indices = mapped_corner_indices(*source, *layer, source_indices);
        }
        for (int index : indices)
            set_corner_radius_value(*layer, index, radius, false);
        if (selected_only)
            layer->corner_radius_locked = false;
        changed = changed || !indices.empty();
    }
    if (!changed) return;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::set_corner_control_sync(bool enabled)
{
    bool changed = false;
    for (auto &layer : selected_layers()) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        layer->corner_radius_locked = enabled;
        if (enabled) {
            const auto corners = fxm::layer_live_corners(*layer, layer_local_rect(*layer));
            if (!corners.empty()) {
                const double radius = corner_radius_value(*layer, corners.front().point_index);
                for (const auto &corner : corners)
                    set_corner_radius_value(*layer, corner.point_index, radius, false);
            }
        }
        changed = true;
    }
    if (!changed) return;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::set_corner_control_bevel_roundness(double roundness)
{
    roundness = std::clamp(roundness, -100.0, 100.0);
    bool changed = false;
    for (auto &layer : selected_layers()) {
        if (!layer || !layer_supports_corner_radius_handles(*layer))
            continue;
        if (std::abs(layer->corner_bevel_roundness - roundness) <= 1e-4)
            continue;
        layer->corner_bevel_roundness = (float)roundness;
        changed = true;
    }
    if (!changed) return;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

bool CanvasPreview::point_controls_available() const
{
    const auto layer = selected_layer();
    return active_tool_ == CanvasTool::DirectSelection && layer &&
           layer->shape_type == ShapeType::Path && !selected_path_point_indices_.empty();
}

QPointF CanvasPreview::point_control_position(bool *x_mixed, bool *y_mixed) const
{
    if (x_mixed) *x_mixed = false;
    if (y_mixed) *y_mixed = false;
    const auto layer = selected_layer();
    if (!layer || layer->shape_type != ShapeType::Path || selected_path_point_indices_.empty())
        return QPointF();

    bool have = false;
    QPointF value;
    for (int index : selected_path_point_indices_) {
        if (index < 0 || (size_t)index >= layer->path_points.size())
            continue;
        const BezierPathPoint &point = layer->path_points[(size_t)index];
        const QPointF canvas = path_normalized_to_canvas(*layer, point.x, point.y);
        if (!have) {
            value = canvas;
            have = true;
        } else {
            if (x_mixed && std::abs(canvas.x() - value.x()) > 0.01) *x_mixed = true;
            if (y_mixed && std::abs(canvas.y() - value.y()) > 0.01) *y_mixed = true;
        }
    }
    return value;
}

bool CanvasPreview::point_control_smooth(bool *mixed) const
{
    if (mixed) *mixed = false;
    const auto layer = selected_layer();
    if (!layer || layer->shape_type != ShapeType::Path || selected_path_point_indices_.empty())
        return false;
    bool have = false;
    bool value = false;
    for (int index : selected_path_point_indices_) {
        if (index < 0 || (size_t)index >= layer->path_points.size())
            continue;
        const bool smooth = layer->path_points[(size_t)index].smooth;
        if (!have) {
            value = smooth;
            have = true;
        } else if (smooth != value && mixed) {
            *mixed = true;
        }
    }
    return value;
}

namespace {
void collect_path_local_geometry(const Layer &layer, const QRectF &rect,
                                 std::vector<QPointF> &anchors,
                                 std::vector<QPointF> &in_handles,
                                 std::vector<QPointF> &out_handles,
                                 std::vector<bool> &has_in,
                                 std::vector<bool> &has_out)
{
    anchors.clear(); in_handles.clear(); out_handles.clear(); has_in.clear(); has_out.clear();
    anchors.reserve(layer.path_points.size());
    in_handles.reserve(layer.path_points.size());
    out_handles.reserve(layer.path_points.size());
    has_in.reserve(layer.path_points.size());
    has_out.reserve(layer.path_points.size());
    for (const BezierPathPoint &point : layer.path_points) {
        anchors.push_back(fxm::path_point_to_local(point, rect));
        in_handles.push_back(fxm::path_in_handle_to_local(point, rect));
        out_handles.push_back(fxm::path_out_handle_to_local(point, rect));
        has_in.push_back(point.has_in);
        has_out.push_back(point.has_out);
    }
}
}

void CanvasPreview::set_point_control_x(double x)
{
    auto layer = selected_layer();
    if (!point_controls_available() || !layer)
        return;
    const QRectF rect = layer_local_rect(*layer);
    std::vector<QPointF> anchors, in_handles, out_handles;
    std::vector<bool> has_in, has_out;
    collect_path_local_geometry(*layer, rect, anchors, in_handles, out_handles, has_in, has_out);
    for (int index : selected_path_point_indices_) {
        if (index < 0 || (size_t)index >= anchors.size()) continue;
        const QPointF anchor_canvas = layer_to_canvas(*layer, anchors[(size_t)index]);
        const QPointF delta(x - anchor_canvas.x(), 0.0);
        anchors[(size_t)index] = canvas_to_layer(*layer, anchor_canvas + delta);
        if (has_in[(size_t)index])
            in_handles[(size_t)index] = canvas_to_layer(*layer, layer_to_canvas(*layer, in_handles[(size_t)index]) + delta);
        if (has_out[(size_t)index])
            out_handles[(size_t)index] = canvas_to_layer(*layer, layer_to_canvas(*layer, out_handles[(size_t)index]) + delta);
    }
    reframe_custom_path(*layer, anchors, in_handles, out_handles, has_in, has_out);
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::set_point_control_y(double y)
{
    auto layer = selected_layer();
    if (!point_controls_available() || !layer)
        return;
    const QRectF rect = layer_local_rect(*layer);
    std::vector<QPointF> anchors, in_handles, out_handles;
    std::vector<bool> has_in, has_out;
    collect_path_local_geometry(*layer, rect, anchors, in_handles, out_handles, has_in, has_out);
    for (int index : selected_path_point_indices_) {
        if (index < 0 || (size_t)index >= anchors.size()) continue;
        const QPointF anchor_canvas = layer_to_canvas(*layer, anchors[(size_t)index]);
        const QPointF delta(0.0, y - anchor_canvas.y());
        anchors[(size_t)index] = canvas_to_layer(*layer, anchor_canvas + delta);
        if (has_in[(size_t)index])
            in_handles[(size_t)index] = canvas_to_layer(*layer, layer_to_canvas(*layer, in_handles[(size_t)index]) + delta);
        if (has_out[(size_t)index])
            out_handles[(size_t)index] = canvas_to_layer(*layer, layer_to_canvas(*layer, out_handles[(size_t)index]) + delta);
    }
    reframe_custom_path(*layer, anchors, in_handles, out_handles, has_in, has_out);
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::convert_selected_points_to_corner()
{
    auto layer = selected_layer();
    if (!point_controls_available() || !layer)
        return;
    for (int index : selected_path_point_indices_) {
        if (index >= 0 && (size_t)index < layer->path_points.size()) {
            BezierPathPoint &point = layer->path_points[(size_t)index];
            point.smooth = false;
            point.has_in = false;
            point.has_out = false;
            point.in_x = point.out_x = point.x;
            point.in_y = point.out_y = point.y;
        }
    }
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::convert_selected_points_to_smooth()
{
    auto layer = selected_layer();
    if (!point_controls_available() || !layer || layer->path_points.size() < 2)
        return;
    const QRectF rect = layer_local_rect(*layer);
    const auto subpaths = fxm::path_subpath_ranges(*layer);
    for (int selected : selected_path_point_indices_) {
        if (selected < 0 || (size_t)selected >= layer->path_points.size()) continue;
        const size_t i = (size_t)selected;
        auto range_it = std::find_if(subpaths.begin(), subpaths.end(), [i](const auto &range) {
            return i >= range.first && i < range.second;
        });
        if (range_it == subpaths.end())
            continue;
        const size_t start_index = range_it->first;
        const size_t end_index = range_it->second;
        const size_t count = end_index - start_index;
        if (count < 2)
            continue;
        const size_t local_index = i - start_index;
        BezierPathPoint &point = layer->path_points[i];
        const bool has_previous = layer->path_closed || local_index > 0;
        const bool has_next = layer->path_closed || local_index + 1 < count;
        const size_t previous = start_index + ((local_index + count - 1) % count);
        const size_t next = start_index + ((local_index + 1) % count);
        const QPointF anchor = fxm::path_point_to_local(point, rect);
        QPointF direction;
        if (has_previous && has_next) {
            direction = fxm::path_point_to_local(layer->path_points[next], rect) -
                        fxm::path_point_to_local(layer->path_points[previous], rect);
        } else if (has_next) {
            direction = fxm::path_point_to_local(layer->path_points[next], rect) - anchor;
        } else if (has_previous) {
            direction = anchor - fxm::path_point_to_local(layer->path_points[previous], rect);
        }
        const double direction_length = std::hypot(direction.x(), direction.y());
        if (direction_length <= 1e-9)
            continue;
        const QPointF unit = direction / direction_length;
        double in_length = point.has_in ? QLineF(anchor, fxm::path_in_handle_to_local(point, rect)).length() : 0.0;
        double out_length = point.has_out ? QLineF(anchor, fxm::path_out_handle_to_local(point, rect)).length() : 0.0;
        if (has_previous && in_length <= 1e-6)
            in_length = QLineF(anchor, fxm::path_point_to_local(layer->path_points[previous], rect)).length() / 3.0;
        if (has_next && out_length <= 1e-6)
            out_length = QLineF(anchor, fxm::path_point_to_local(layer->path_points[next], rect)).length() / 3.0;
        auto normalize = [&](const QPointF &local) {
            return QPointF((local.x() - rect.left()) / std::max(1e-9, rect.width()),
                           (local.y() - rect.top()) / std::max(1e-9, rect.height()));
        };
        if (has_previous) {
            const QPointF in = normalize(anchor - unit * in_length);
            point.has_in = true; point.in_x = in.x(); point.in_y = in.y();
        }
        if (has_next) {
            const QPointF out = normalize(anchor + unit * out_length);
            point.has_out = true; point.out_x = out.x(); point.out_y = out.y();
        }
        point.smooth = true;
    }
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::set_point_control_show_handles(bool show)
{
    if (show_selected_path_handles_ == show)
        return;
    show_selected_path_handles_ = show;
    invalidate_canvas_overlay_caches();
    notify_corner_context_changed();
    update();
}

void CanvasPreview::draw_corner_radius_handles(QPainter &p, const Layer &layer)
{
    if (!layer_supports_corner_radius_handles(layer))
        return;
    const auto corners = fxm::layer_live_corners(layer, layer_local_rect(layer));
    if (corners.empty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen guide_pen(QColor(255, 255, 255, 170), 1.0, Qt::DashLine);
    guide_pen.setCosmetic(true);
    for (const auto &corner : corners) {
        QPointF anchor_view;
        QPointF handle_view;
        if (!corner_radius_handle_view_geometry(
                layer, corner, anchor_view, handle_view))
            continue;
        const bool selected = selected_corner_layer_id_ == layer.id &&
            selected_corner_indices_.find(corner.point_index) != selected_corner_indices_.end();
        p.setPen(guide_pen);
        p.setBrush(Qt::NoBrush);
        p.drawLine(anchor_view, handle_view);
        const bool at_limit = corner.max_radius > 0.0 && corner.radius >= corner.max_radius - 0.25;
        const QColor outline = at_limit ? QColor(235, 55, 55, 255) : QColor(0, 120, 255, 255);
        p.setPen(QPen(QColor(25, 25, 25, 230), selected ? 3.5 : 3.0));
        p.setBrush(selected ? outline : QColor(255, 255, 255, 245));
        p.drawEllipse(handle_view, selected ? 5.8 : 5.0, selected ? 5.8 : 5.0);
        p.setPen(QPen(outline, selected ? 1.8 : 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(handle_view, selected ? 5.8 : 5.0, selected ? 5.8 : 5.0);
    }
    p.restore();
}

void CanvasPreview::begin_corner_radius_drag(const Layer &layer)
{
    corner_radius_drag_ = CornerRadiusDragState{};
    if (!layer_supports_corner_radius_handles(layer))
        return;
    const int point_index = hit_test_corner_radius_handle_index(layer, drag_start_view_);
    if (point_index < 0)
        return;

    const bool preselected = selected_corner_layer_id_ == layer.id &&
        selected_corner_indices_.find(point_index) != selected_corner_indices_.end();
    corner_radius_drag_.active = true;
    corner_radius_drag_.primary_layer_id = layer.id;
    corner_radius_drag_.point_index = point_index;
    corner_radius_drag_.primary_start_radius = corner_radius_value(layer, point_index);
    corner_radius_drag_.isolated = preselected;

    std::vector<int> source_targets;
    if (preselected) {
        source_targets.assign(selected_corner_indices_.begin(), selected_corner_indices_.end());
    } else {
        const auto source_corners = fxm::layer_live_corners(layer, layer_local_rect(layer));
        for (const auto &corner : source_corners) {
            if (layer.shape_type == ShapeType::Star &&
                (corner.point_index & 1) != (point_index & 1))
                continue;
            source_targets.push_back(corner.point_index);
        }
    }

    for (auto &target_layer : selected_layers()) {
        if (!target_layer || !layer_supports_corner_radius_handles(*target_layer))
            continue;
        CornerLayerDragState state;
        state.id = target_layer->id;
        state.sync = target_layer->corner_radius_locked;
        const auto corners = fxm::layer_live_corners(*target_layer, layer_local_rect(*target_layer));
        for (const auto &corner : corners)
            state.points.push_back({corner.point_index, corner_radius_value(*target_layer, corner.point_index)});

        if (preselected) {
            state.target_indices = mapped_corner_indices(layer, *target_layer, source_targets);
        } else if (layer.shape_type == ShapeType::Star && target_layer->shape_type == ShapeType::Star) {
            for (const auto &corner : corners) {
                if ((corner.point_index & 1) == (point_index & 1))
                    state.target_indices.push_back(corner.point_index);
            }
        } else {
            for (const auto &corner : corners)
                state.target_indices.push_back(corner.point_index);
        }
        corner_radius_drag_.layers.push_back(std::move(state));
    }
}

bool CanvasPreview::apply_corner_radius_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!corner_radius_drag_.active || drag_mode_ != DragMode::CornerRadius || !title_)
        return false;
    auto layer = title_->find_layer(corner_radius_drag_.primary_layer_id);
    if (!layer || layer->locked)
        return false;

    const auto corners = fxm::layer_live_corners(*layer, layer_local_rect(*layer));
    const auto it = std::find_if(corners.begin(), corners.end(), [&](const fxm::LiveCornerGeometry &corner) {
        return corner.point_index == corner_radius_drag_.point_index;
    });
    if (it == corners.end())
        return false;
    const fxm::LiveCornerGeometry &corner = *it;

    const QPointF previous_vector = corner.previous - corner.anchor;
    const QPointF next_vector = corner.next - corner.anchor;
    const double previous_length = std::hypot(previous_vector.x(), previous_vector.y());
    const double next_length = std::hypot(next_vector.x(), next_vector.y());
    if (previous_length <= 1e-9 || next_length <= 1e-9)
        return false;
    const QPointF previous_unit = previous_vector / previous_length;
    const QPointF next_unit = next_vector / next_length;
    QPointF inward = previous_unit + next_unit;
    const double inward_length = std::hypot(inward.x(), inward.y());
    if (inward_length <= 1e-9)
        return false;
    inward /= inward_length;

    const QPointF anchor_view = canvas_to_view(layer_to_canvas(*layer, corner.anchor));
    const QPointF probe_view = canvas_to_view(layer_to_canvas(*layer, corner.anchor + inward));
    const QPointF probe_delta = probe_view - anchor_view;
    const double pixels_per_local = std::hypot(probe_delta.x(), probe_delta.y());
    if (pixels_per_local <= 1e-9)
        return false;
    const QPointF inward_view = probe_delta / pixels_per_local;
    constexpr double kCornerRadiusHandleOffsetPx = 20.0;
    const double pointer_projection_px = QPointF::dotProduct(view_pt - anchor_view, inward_view);
    const double cosine = std::clamp(QPointF::dotProduct(previous_unit, next_unit), -1.0, 1.0);
    const double sin_half_angle = std::max(1e-6, std::sqrt(std::max(0.0, (1.0 - cosine) * 0.5)));
    double radius = std::clamp(corner_radius_drag_.primary_start_radius, 0.0, corner.max_radius);
    for (int iteration = 0; iteration < 6; ++iteration) {
        const double fraction = corner.max_radius > 1e-9
            ? std::clamp(radius / corner.max_radius, 0.0, 1.0) : 0.0;
        const double offset_px = kCornerRadiusHandleOffsetPx * (1.0 - 2.0 * fraction);
        const double center_distance_local = std::max(0.0,
            (pointer_projection_px - offset_px) / pixels_per_local);
        const double next_radius = std::clamp(center_distance_local * sin_half_angle,
                                              0.0, corner.max_radius);
        if (std::abs(next_radius - radius) <= 1e-4) {
            radius = next_radius;
            break;
        }
        radius = next_radius;
    }
    if (modifiers.testFlag(Qt::ShiftModifier))
        radius = std::round(radius);
    const double delta = radius - corner_radius_drag_.primary_start_radius;
    const double drag_distance = QLineF(view_pt, drag_start_view_).length();
    if (drag_distance < 2.0 && !corner_radius_drag_.moved)
        return true;
    corner_radius_drag_.moved = true;
    corner_radius_drag_.value_changed = std::abs(delta) > 1e-5;

    for (const CornerLayerDragState &state : corner_radius_drag_.layers) {
        auto target_layer = title_->find_layer(state.id);
        if (!target_layer)
            continue;
        if (corner_radius_drag_.isolated && target_layer->shape_type != ShapeType::Path &&
            target_layer->shape_type != ShapeType::Rectangle &&
            target_layer->shape_type != ShapeType::RoundedRectangle)
            fxm::ensure_editable_path(*target_layer);
        for (const CornerPointDragState &point : state.points)
            set_corner_radius_value(*target_layer, point.point_index, point.radius, false);

        const auto current_corners = fxm::layer_live_corners(*target_layer, layer_local_rect(*target_layer));
        for (int target_index : state.target_indices) {
            auto start = std::find_if(state.points.begin(), state.points.end(),
                [target_index](const CornerPointDragState &point) {
                    return point.point_index == target_index;
                });
            if (start == state.points.end())
                continue;
            auto geometry = std::find_if(current_corners.begin(), current_corners.end(),
                [target_index](const fxm::LiveCornerGeometry &item) {
                    return item.point_index == target_index;
                });
            const double maximum = geometry == current_corners.end() ? 9999.0 : geometry->max_radius;
            set_corner_radius_value(*target_layer, target_index,
                                    std::clamp(start->radius + delta, 0.0, maximum), false);
        }
        target_layer->corner_radius_locked =
            (corner_radius_drag_.isolated && corner_radius_drag_.value_changed) ? false : state.sync;
    }

    clear_snap_feedback();
    begin_adaptive_interaction();
    dirty_ = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    invalidate_canvas_overlay_caches();
    notify_corner_context_changed();
    update();
    return true;
}

CanvasPreview::DragMode CanvasPreview::hit_test_selected(const QPointF &view_pt) const
{
    auto layers = selected_layers();
    if (layers.empty()) return DragMode::None;
    if (std::any_of(layers.begin(), layers.end(), [](const auto &layer) {
            return layer && layer_has_fixed_canvas_geometry(*layer);
        }))
        return DragMode::None;

    if (layers.size() > 1) {
        QRectF r = selected_canvas_bounds();
        if (!r.isValid() || r.isEmpty()) return DragMode::None;
        auto canvas_handle_to_view = [&](const QPointF &p) { return canvas_to_view(p); };
        auto near_pt = [&](const QPointF &p) {
            QPointF view = canvas_handle_to_view(p);
            return std::abs(view_pt.x() - view.x()) <= CANVAS_CONTROL_HIT_RADIUS_PX &&
                   std::abs(view_pt.y() - view.y()) <= CANVAS_CONTROL_HIT_RADIUS_PX;
        };
        QRectF view_bounds(canvas_to_view(r.topLeft()), canvas_to_view(r.bottomRight()));
        view_bounds = view_bounds.normalized();
        auto near_rotation_corner = [&](const QPointF &p) {
            return QLineF(view_pt, canvas_handle_to_view(p)).length() <= CANVAS_ROTATE_HIT_RADIUS_PX &&
                   !view_bounds.adjusted(-CANVAS_CONTROL_SIZE_PX, -CANVAS_CONTROL_SIZE_PX,
                                         CANVAS_CONTROL_SIZE_PX, CANVAS_CONTROL_SIZE_PX).contains(view_pt);
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
        QPointF canvas = view_to_canvas(view_pt);
        for (const auto &layer : layers) {
            if (!layer || layer->locked) continue;
            QPointF local = canvas_to_layer(*layer, canvas);
            if (layer_local_rect(*layer).contains(local))
                return DragMode::Move;
        }
        return DragMode::None;
    }

