namespace {

double motion_path_point_segment_distance(const QPointF &point,
                                          const QPointF &start,
                                          const QPointF &end,
                                          double *segment_progress)
{
    const QPointF delta = end - start;
    const double length_squared = QPointF::dotProduct(delta, delta);
    double progress = 0.0;
    if (length_squared > 1e-12)
        progress = std::clamp(QPointF::dotProduct(point - start, delta) /
                              length_squared, 0.0, 1.0);
    if (segment_progress)
        *segment_progress = progress;
    return QLineF(point, start + delta * progress).length();
}

QPointF motion_path_arrow_point(const QPointF &origin,
                                const QPointF &direction,
                                double distance,
                                double side)
{
    const double length = std::hypot(direction.x(), direction.y());
    if (length < 1e-9)
        return origin;
    const QPointF unit = direction / length;
    const QPointF normal(-unit.y(), unit.x());
    return origin - unit * distance + normal * side;
}

bool motion_path_layer_depends_on(const std::shared_ptr<Title> &title,
                                  const Layer &layer,
                                  const std::string &ancestor_id)
{
    if (!title || ancestor_id.empty())
        return false;
    std::vector<std::string> pending;
    if (!layer.parent_id.empty()) pending.push_back(layer.parent_id);
    if (!layer.transform_parent_id.empty())
        pending.push_back(layer.transform_parent_id);
    std::set<std::string> visited;
    while (!pending.empty() && visited.size() < 128) {
        const std::string id = pending.back();
        pending.pop_back();
        if (id.empty() || !visited.insert(id).second)
            continue;
        if (id == ancestor_id)
            return true;
        const Layer *parent = editor_find_layer_by_id(title, id);
        if (!parent)
            continue;
        if (!parent->parent_id.empty()) pending.push_back(parent->parent_id);
        if (!parent->transform_parent_id.empty())
            pending.push_back(parent->transform_parent_id);
    }
    return false;
}


bool position_path_is_3d(const Layer &layer)
{
    return layer.dimension_mode == LayerDimensionMode::ThreeD &&
           layer.position_3d_path_enabled;
}

size_t position_path_keyframe_count(const Layer &layer)
{
    return position_path_is_3d(layer)
        ? layer.position_3d.keyframes.size()
        : layer.position.keyframes.size();
}

double position_path_keyframe_time(const Layer &layer, size_t index)
{
    return position_path_is_3d(layer)
        ? layer.position_3d.keyframes[index].time
        : layer.position.keyframes[index].time;
}

SpatialInterpolationMode position_path_spatial_mode(const Layer &layer,
                                                     size_t index)
{
    return position_path_is_3d(layer)
        ? layer.position_3d.keyframes[index].spatial_mode
        : layer.position.keyframes[index].spatial_mode;
}

bool position_path_roving(const Layer &layer, size_t index)
{
    return position_path_is_3d(layer)
        ? layer.position_3d.keyframes[index].rove_across_time
        : layer.position.keyframes[index].rove_across_time;
}

} // namespace

bool CanvasPreview::position_motion_path_visible(const Layer &layer) const
{
    return title_ && active_tool_ == CanvasTool::Selection &&
        selected_layer_ids_.size() == 1 &&
        selected_layer_ids_.front() == layer.id &&
        position_path_keyframe_count(layer) >= 2;
}

int CanvasPreview::position_keyframe_at_playhead(const Layer &layer) const
{
    const size_t count = position_path_keyframe_count(layer);
    if (count == 0) return -1;
    const double local_time = std::clamp(
        playhead_ - layer.in_time, 0.0,
        std::max(0.0, layer.out_time - layer.in_time));
    constexpr double kTolerance = 1.0 / 240.0;
    int best = -1;
    double best_distance = kTolerance;
    for (int i = 0; i < static_cast<int>(count); ++i) {
        const double distance = std::abs(
            position_path_keyframe_time(layer, static_cast<size_t>(i)) - local_time);
        if (distance <= best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

int CanvasPreview::active_position_keyframe_index(const Layer &layer) const
{
    if (selected_position_keyframe_index_ >= 0 &&
        selected_position_keyframe_index_ <
            static_cast<int>(position_path_keyframe_count(layer)))
        return selected_position_keyframe_index_;
    const int playhead_keyframe = position_keyframe_at_playhead(layer);
    return playhead_keyframe >= 0 ? playhead_keyframe : 0;
}

QPointF CanvasPreview::position_keyframe_canvas_point(
    const Layer &layer, int keyframe_index, const Vec3Value &offset) const
{
    const size_t count = position_path_keyframe_count(layer);
    if (!title_ || keyframe_index < 0 ||
        keyframe_index >= static_cast<int>(count))
        return {};

    if (position_path_is_3d(layer)) {
        const Vector3Keyframe &keyframe =
            layer.position_3d.keyframes[static_cast<size_t>(keyframe_index)];
        const double title_time = layer.in_time + keyframe.time;
        const std::shared_ptr<Title> projected_title = projection_title();
        if (!projected_title) return {};
        const Layer *projected_layer = editor_find_layer_by_id(projected_title, layer.id);
        if (!projected_layer) return {};
        const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time);
        const QVector3D world = parent.map(QVector3D(
            static_cast<float>(keyframe.value.x + offset.x),
            static_cast<float>(keyframe.value.y + offset.y),
            static_cast<float>(keyframe.value.z + offset.z)));
        QPointF canvas;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (fxm::transform3d::project_world_point(
                *projected_title, world, title_time, canvas, nullptr, camera_id))
            return canvas;
        return {};
    }

    const VectorKeyframe &keyframe =
        layer.position.keyframes[static_cast<size_t>(keyframe_index)];
    const double title_time = layer.in_time + keyframe.time;
    const QTransform parent = editor_parent_world_transform(title_, layer, title_time);
    return parent.map(QPointF(keyframe.value.x + offset.x,
                              keyframe.value.y + offset.y));
}

QPointF CanvasPreview::position_segment_canvas_point(
    const Layer &layer, size_t segment_index, double temporal_progress) const
{
    const size_t count = position_path_keyframe_count(layer);
    if (!title_ || segment_index + 1 >= count) return {};
    temporal_progress = std::clamp(temporal_progress, 0.0, 1.0);

    if (position_path_is_3d(layer)) {
        const Vector3Keyframe &start = layer.position_3d.keyframes[segment_index];
        const Vector3Keyframe &end = layer.position_3d.keyframes[segment_index + 1];
        const double local_time = start.time +
            temporal_progress * (end.time - start.time);
        const double spatial_progress =
            layer.position_3d.spatial_progress_for_segment(
                segment_index, temporal_progress);
        const Vec3Value local = layer.position_3d.evaluate_spatial_segment(
            segment_index, spatial_progress);
        const double title_time = layer.in_time + local_time;
        const std::shared_ptr<Title> projected_title = projection_title();
        if (!projected_title) return {};
        const Layer *projected_layer = editor_find_layer_by_id(projected_title, layer.id);
        if (!projected_layer) return {};
        const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time);
        const QVector3D world = parent.map(QVector3D(
            static_cast<float>(local.x), static_cast<float>(local.y),
            static_cast<float>(local.z)));
        QPointF canvas;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (fxm::transform3d::project_world_point(
                *projected_title, world, title_time, canvas, nullptr, camera_id))
            return canvas;
        return {};
    }

    const VectorKeyframe &start = layer.position.keyframes[segment_index];
    const VectorKeyframe &end = layer.position.keyframes[segment_index + 1];
    const double local_time = start.time + temporal_progress * (end.time - start.time);
    const double spatial_progress = layer.position.spatial_progress_for_segment(
        segment_index, temporal_progress);
    const Vec2Value local = layer.position.evaluate_spatial_segment(
        segment_index, spatial_progress);
    const QTransform parent = editor_parent_world_transform(
        title_, layer, layer.in_time + local_time);
    return parent.map(QPointF(local.x, local.y));
}

void CanvasPreview::invalidate_motion_path_sample_cache() const
{
    motion_path_sample_cache_ = MotionPathSampleCache{};
}

const std::vector<std::vector<QPointF>> &
CanvasPreview::motion_path_canvas_samples(const Layer &layer,
                                          int samples_per_segment) const
{
    samples_per_segment = std::max(1, samples_per_segment);
    const std::string title_id = title_ ? title_->id : std::string{};
    if (motion_path_sample_cache_.valid &&
        motion_path_sample_cache_.title_id == title_id &&
        motion_path_sample_cache_.layer_id == layer.id &&
        motion_path_sample_cache_.keyframe_count == position_path_keyframe_count(layer) &&
        motion_path_sample_cache_.samples_per_segment == samples_per_segment) {
        fxm::perf::add(fxm::perf::Counter::MotionPathCacheHits);
        return motion_path_sample_cache_.canvas_segments;
    }

    fxm::perf::add(fxm::perf::Counter::MotionPathCacheMisses);
    MotionPathSampleCache rebuilt;
    rebuilt.title_id = title_id;
    rebuilt.layer_id = layer.id;
    rebuilt.keyframe_count = position_path_keyframe_count(layer);
    rebuilt.samples_per_segment = samples_per_segment;
    if (title_ && position_path_keyframe_count(layer) >= 2) {
        rebuilt.canvas_segments.resize(position_path_keyframe_count(layer) - 1);
        for (size_t segment = 0; segment < rebuilt.canvas_segments.size(); ++segment) {
            auto &points = rebuilt.canvas_segments[segment];
            points.reserve(static_cast<size_t>(samples_per_segment + 1));
            for (int sample = 0; sample <= samples_per_segment; ++sample) {
                points.push_back(position_segment_canvas_point(
                    layer, segment,
                    static_cast<double>(sample) / samples_per_segment));
            }
        }
    }
    rebuilt.valid = true;
    motion_path_sample_cache_ = std::move(rebuilt);
    return motion_path_sample_cache_.canvas_segments;
}

int CanvasPreview::hit_test_position_keyframe_vertices(
    const Layer &layer, const QPointF &view_pt) const
{
    if (!position_motion_path_visible(layer) || !inline_text_layer_id_.empty())
        return -1;
    constexpr double kHitRadius = 8.0;
    int best = -1;
    double best_distance = kHitRadius;
    for (int index = 0;
         index < static_cast<int>(position_path_keyframe_count(layer)); ++index) {
        const QPointF vertex = canvas_to_view(position_keyframe_canvas_point(
            layer, index, {}));
        const double distance = QLineF(view_pt, vertex).length();
        if (distance <= best_distance) {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

CanvasPreview::DragMode CanvasPreview::hit_test_position_tangent_handles(
    const Layer &layer, const QPointF &view_pt, int *keyframe_index) const
{
    if (!position_motion_path_visible(layer) || layer.locked ||
        !inline_text_layer_id_.empty())
        return DragMode::None;
    const int index = active_position_keyframe_index(layer);
    if (index < 0) return DragMode::None;
    const size_t key_index = static_cast<size_t>(index);
    if (position_path_spatial_mode(layer, key_index) ==
        SpatialInterpolationMode::Linear)
        return DragMode::None;

    Vec3Value incoming_offset;
    Vec3Value outgoing_offset;
    if (position_path_is_3d(layer)) {
        incoming_offset = layer.position_3d.resolved_incoming_tangent(key_index);
        outgoing_offset = layer.position_3d.resolved_outgoing_tangent(key_index);
    } else {
        const Vec2Value incoming = layer.position.resolved_incoming_tangent(key_index);
        const Vec2Value outgoing = layer.position.resolved_outgoing_tangent(key_index);
        incoming_offset = {incoming.x, incoming.y, 0.0};
        outgoing_offset = {outgoing.x, outgoing.y, 0.0};
    }
    const QPointF incoming = canvas_to_view(position_keyframe_canvas_point(
        layer, index, incoming_offset));
    const QPointF outgoing = canvas_to_view(position_keyframe_canvas_point(
        layer, index, outgoing_offset));
    constexpr double kHitRadius = 9.0;
    if (QLineF(view_pt, incoming).length() <= kHitRadius) {
        if (keyframe_index) *keyframe_index = index;
        return DragMode::PositionInTangent;
    }
    if (QLineF(view_pt, outgoing).length() <= kHitRadius) {
        if (keyframe_index) *keyframe_index = index;
        return DragMode::PositionOutTangent;
    }
    return DragMode::None;
}

bool CanvasPreview::hit_test_position_motion_path(
    const Layer &layer, const QPointF &view_pt, size_t *segment_index,
    double *temporal_progress, double hit_radius) const
{
    if (!position_motion_path_visible(layer))
        return false;

    double best_distance = hit_radius;
    size_t best_segment = 0;
    double best_progress = 0.0;
    const size_t segment_count = position_path_keyframe_count(layer) - 1;
    const int samples_per_segment = segment_count > 256 ? 4
        : segment_count > 96 ? 8
        : 24;

    const auto &segments = motion_path_canvas_samples(
        layer, samples_per_segment);
    for (size_t segment = 0; segment < segments.size(); ++segment) {
        const auto &points = segments[segment];
        if (points.size() < 2)
            continue;
        QPointF previous = canvas_to_view(points.front());
        for (size_t sample = 1; sample < points.size(); ++sample) {
            const QPointF current = canvas_to_view(points[sample]);
            double line_progress = 0.0;
            const double distance = motion_path_point_segment_distance(
                view_pt, previous, current, &line_progress);
            if (distance <= best_distance) {
                best_distance = distance;
                best_segment = segment;
                best_progress =
                    (static_cast<double>(sample - 1) + line_progress) /
                    samples_per_segment;
            }
            previous = current;
        }
    }

    if (best_distance > hit_radius)
        return false;
    if (segment_index) *segment_index = best_segment;
    if (temporal_progress) *temporal_progress =
        std::clamp(best_progress, 0.0, 1.0);
    return true;
}

void CanvasPreview::draw_position_motion_path(QPainter &p, const Layer &layer)
{
    if (!position_motion_path_visible(layer)) return;

    QColor color = layer_color(layer, 0);
    if (!color.isValid()) color = QColor(255, 181, 46);
    color.setAlpha(220);
    const bool editable_handles = !layer.locked && inline_text_layer_id_.empty();
    const int selected_index = active_position_keyframe_index(layer);
    const size_t key_count = position_path_keyframe_count(layer);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen path_pen(color, motion_path_hover_type_ == MotionPathHoverType::Path
                            ? 2.0 : 1.25, Qt::SolidLine);
    path_pen.setCosmetic(true);
    p.setPen(path_pen);
    p.setBrush(Qt::NoBrush);

    QPainterPath path;
    bool started = false;
    const size_t segment_count = key_count - 1;
    const int samples_per_segment = segment_count > 256 ? 4
        : segment_count > 96 ? 8 : 24;
    const auto &segments = motion_path_canvas_samples(layer, samples_per_segment);
    for (size_t segment = 0; segment < segments.size(); ++segment) {
        const auto &points = segments[segment];
        for (size_t sample = 0; sample < points.size(); ++sample) {
            if (segment > 0 && sample == 0) continue;
            const QPointF view = canvas_to_view(points[sample]);
            if (!started) { path.moveTo(view); started = true; }
            else path.lineTo(view);
        }
    }
    if (started) p.drawPath(path);

    if (editable_handles) {
        for (int index = 0; index < static_cast<int>(key_count); ++index) {
            const QPointF point = canvas_to_view(position_keyframe_canvas_point(
                layer, index, {}));
            const bool selected = index == selected_index;
            const bool hovered = motion_path_hover_type_ == MotionPathHoverType::Vertex &&
                motion_path_hover_keyframe_index_ == index;
            const double radius = selected || hovered ? 5.0 : 4.0;
            QPolygonF diamond;
            diamond << point + QPointF(0.0, -radius)
                    << point + QPointF(radius, 0.0)
                    << point + QPointF(0.0, radius)
                    << point + QPointF(-radius, 0.0);
            p.setPen(QPen(color, selected ? 1.6 : 1.0));
            p.setBrush(selected || hovered ? color : QColor(255, 255, 255, 235));
            p.drawPolygon(diamond);
            if (position_path_roving(layer, static_cast<size_t>(index))) {
                p.setBrush(QColor(255, 255, 255, 235));
                p.setPen(Qt::NoPen);
                p.drawEllipse(point, 1.5, 1.5);
            }
        }

        if (selected_index >= 0 &&
            position_path_spatial_mode(layer, static_cast<size_t>(selected_index)) !=
                SpatialInterpolationMode::Linear) {
            Vec3Value incoming_offset;
            Vec3Value outgoing_offset;
            if (position_path_is_3d(layer)) {
                incoming_offset = layer.position_3d.resolved_incoming_tangent(
                    static_cast<size_t>(selected_index));
                outgoing_offset = layer.position_3d.resolved_outgoing_tangent(
                    static_cast<size_t>(selected_index));
            } else {
                const Vec2Value incoming = layer.position.resolved_incoming_tangent(
                    static_cast<size_t>(selected_index));
                const Vec2Value outgoing = layer.position.resolved_outgoing_tangent(
                    static_cast<size_t>(selected_index));
                incoming_offset = {incoming.x, incoming.y, 0.0};
                outgoing_offset = {outgoing.x, outgoing.y, 0.0};
            }
            const QPointF anchor = canvas_to_view(position_keyframe_canvas_point(
                layer, selected_index, {}));
            const QPointF incoming = canvas_to_view(position_keyframe_canvas_point(
                layer, selected_index, incoming_offset));
            const QPointF outgoing = canvas_to_view(position_keyframe_canvas_point(
                layer, selected_index, outgoing_offset));
            QPen tangent_pen(color, 1.0, Qt::DashLine);
            tangent_pen.setCosmetic(true);
            p.setPen(tangent_pen);
            p.drawLine(anchor, incoming);
            p.drawLine(anchor, outgoing);
            p.setPen(QPen(color, 1.25));
            p.setBrush(QColor(255, 255, 255, 245));
            const bool hover_in = motion_path_hover_type_ == MotionPathHoverType::InTangent;
            const bool hover_out = motion_path_hover_type_ == MotionPathHoverType::OutTangent;
            p.drawEllipse(incoming, hover_in ? 5.5 : 4.5, hover_in ? 5.5 : 4.5);
            p.drawEllipse(outgoing, hover_out ? 5.5 : 4.5, hover_out ? 5.5 : 4.5);
        }
    }

    const double local_duration = std::max(0.0, layer.out_time - layer.in_time);
    const double local_time = std::clamp(playhead_ - layer.in_time, 0.0, local_duration);
    auto evaluated_canvas_point = [&](double time) {
        time = std::clamp(time, 0.0, local_duration);
        if (position_path_is_3d(layer)) {
            const Vec3Value value = layer.position_3d.evaluate(time);
            const std::shared_ptr<Title> projected_title = projection_title();
            if (!projected_title) return QPointF{};
            const Layer *projected_layer = editor_find_layer_by_id(projected_title, layer.id);
            if (!projected_layer) return QPointF{};
            const double title_time = layer.in_time + time;
            const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
                *projected_title, *projected_layer, title_time);
            const QVector3D world = parent.map(QVector3D(
                static_cast<float>(value.x), static_cast<float>(value.y),
                static_cast<float>(value.z)));
            QPointF canvas;
            const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
                *projected_title, *projected_layer, title_time);
            fxm::transform3d::project_world_point(*projected_title, world,
                title_time, canvas, nullptr, camera_id);
            return canvas;
        }
        const Vec2Value value = layer.position.evaluate(time);
        const QTransform parent = editor_parent_world_transform(
            title_, layer, layer.in_time + time);
        return parent.map(QPointF(value.x, value.y));
    };
    const QPointF current = canvas_to_view(evaluated_canvas_point(local_time));
    const double sample_delta = std::max(1.0 / 240.0,
        std::min(1.0 / 30.0, std::max(local_duration, 1.0) / 1000.0));
    const QPointF before = canvas_to_view(evaluated_canvas_point(
        std::max(0.0, local_time - sample_delta)));
    const QPointF after = canvas_to_view(evaluated_canvas_point(
        std::min(local_duration, local_time + sample_delta)));
    const QPointF direction = after - before;
    if (std::hypot(direction.x(), direction.y()) > 0.5) {
        p.setPen(QPen(color, 1.5));
        p.setBrush(color);
        QPolygonF arrow;
        arrow << current
              << motion_path_arrow_point(current, direction, 9.0, 4.0)
              << motion_path_arrow_point(current, direction, 9.0, -4.0);
        p.drawPolygon(arrow);
    }
    p.setPen(QPen(QColor(20, 20, 20, 230), 1.0));
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawEllipse(current, 4.0, 4.0);
    p.restore();
}

bool CanvasPreview::begin_position_tangent_drag(
    const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (active_tool_ != CanvasTool::Selection) return false;
    auto layer = selected_layer();
    if (!layer || layer->locked || selected_layer_ids_.size() != 1 ||
        !inline_text_layer_id_.empty())
        return false;

    int keyframe_index = -1;
    const DragMode mode = hit_test_position_tangent_handles(
        *layer, view_pt, &keyframe_index);
    if (mode == DragMode::None || keyframe_index < 0) return false;

    const size_t index = static_cast<size_t>(keyframe_index);
    spatial_tangent_drag_ = SpatialTangentDragState{};
    spatial_tangent_drag_.active = true;
    spatial_tangent_drag_.layer_id = layer->id;
    spatial_tangent_drag_.keyframe_index = keyframe_index;
    spatial_tangent_drag_.incoming = mode == DragMode::PositionInTangent;
    spatial_tangent_drag_.path_3d = position_path_is_3d(*layer);
    spatial_tangent_drag_.linked = spatial_tangent_drag_.path_3d
        ? layer->position_3d.keyframes[index].spatial_tangents_linked
        : layer->position.keyframes[index].spatial_tangents_linked;
    spatial_tangent_drag_.linked &= !modifiers.testFlag(Qt::AltModifier);

    if (spatial_tangent_drag_.path_3d) {
        const double title_time = layer->in_time +
            layer->position_3d.keyframes[index].time;
        const std::shared_ptr<Title> projected_title = projection_title();
        const Layer *projected_layer = projected_title
            ? editor_find_layer_by_id(projected_title, layer->id) : nullptr;
        if (!projected_layer) {
            spatial_tangent_drag_ = SpatialTangentDragState{};
            return false;
        }
        const Vec3Value value = layer->position_3d.keyframes[index].value;
        const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time);
        spatial_tangent_drag_.plane_point = parent.map(QVector3D(
            static_cast<float>(value.x), static_cast<float>(value.y),
            static_cast<float>(value.z)));
        QVector3D ray_origin, ray_direction;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (!fxm::transform3d::canvas_world_ray(
                *projected_title,
                position_keyframe_canvas_point(*layer, keyframe_index, {}),
                title_time, ray_origin, ray_direction, camera_id)) {
            spatial_tangent_drag_ = SpatialTangentDragState{};
            return false;
        }
        spatial_tangent_drag_.plane_normal = ray_direction;
    }

    selected_position_keyframe_index_ = keyframe_index;
    drag_mode_ = mode;
    drag_changed_ = false;
    drag_start_view_ = view_pt;
    drag_current_view_ = view_pt;
    begin_adaptive_interaction();
    setCursor(Qt::CrossCursor);
    invalidate_selection_overlay_cache();
    gpu_overlay_dirty_ = true;
    update();
    return true;
}

bool CanvasPreview::begin_position_vertex_drag(const QPointF &view_pt)
{
    if (active_tool_ != CanvasTool::Selection) return false;
    auto layer = selected_layer();
    if (!layer || layer->locked || selected_layer_ids_.size() != 1 ||
        !inline_text_layer_id_.empty())
        return false;
    const int keyframe_index = hit_test_position_keyframe_vertices(*layer, view_pt);
    if (keyframe_index < 0) return false;

    const size_t index = static_cast<size_t>(keyframe_index);
    spatial_vertex_drag_ = SpatialVertexDragState{};
    spatial_vertex_drag_.active = true;
    spatial_vertex_drag_.layer_id = layer->id;
    spatial_vertex_drag_.keyframe_index = keyframe_index;
    spatial_vertex_drag_.path_3d = position_path_is_3d(*layer);
    if (spatial_vertex_drag_.path_3d) {
        const Vector3Keyframe &keyframe = layer->position_3d.keyframes[index];
        spatial_vertex_drag_.keyframe_time = keyframe.time;
        spatial_vertex_drag_.start_value = keyframe.value;
        const double title_time = layer->in_time + keyframe.time;
        const std::shared_ptr<Title> projected_title = projection_title();
        const Layer *projected_layer = projected_title
            ? editor_find_layer_by_id(projected_title, layer->id) : nullptr;
        if (!projected_layer) {
            spatial_vertex_drag_ = SpatialVertexDragState{};
            return false;
        }
        const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time);
        spatial_vertex_drag_.plane_point = parent.map(QVector3D(
            static_cast<float>(keyframe.value.x),
            static_cast<float>(keyframe.value.y),
            static_cast<float>(keyframe.value.z)));
        QVector3D ray_origin, ray_direction;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (!fxm::transform3d::canvas_world_ray(
                *projected_title,
                position_keyframe_canvas_point(*layer, keyframe_index, {}),
                title_time, ray_origin, ray_direction, camera_id)) {
            spatial_vertex_drag_ = SpatialVertexDragState{};
            return false;
        }
        spatial_vertex_drag_.plane_normal = ray_direction;
    } else {
        const VectorKeyframe &keyframe = layer->position.keyframes[index];
        spatial_vertex_drag_.keyframe_time = keyframe.time;
        spatial_vertex_drag_.start_value = {
            keyframe.value.x, keyframe.value.y, 0.0};
    }
    selected_position_keyframe_index_ = keyframe_index;
    drag_mode_ = DragMode::PositionVertex;
    drag_changed_ = false;
    drag_start_view_ = view_pt;
    drag_current_view_ = view_pt;
    begin_adaptive_interaction();
    setCursor(Qt::SizeAllCursor);
    invalidate_selection_overlay_cache();
    gpu_overlay_dirty_ = true;
    update();
    return true;
}

QPointF CanvasPreview::snap_position_keyframe_canvas_point(
    const Layer &layer, int keyframe_index, const QPointF &canvas_point,
    bool allow_snap)
{
    if (!allow_snap || !title_ || !snap_settings_.enabled) {
        clear_snap_feedback();
        return canvas_point;
    }

    struct Target {
        QPointF point;
        QString label;
    };
    std::vector<Target> targets;
    if (snap_settings_.guides) {
        for (double guide : vertical_guides_)
            targets.push_back({QPointF(guide, canvas_point.y()),
                               fxm_tr("OBSTitles.Guide")});
        for (double guide : horizontal_guides_)
            targets.push_back({QPointF(canvas_point.x(), guide),
                               fxm_tr("OBSTitles.Guide")});
    }

    for (const auto &candidate : title_->layers) {
        if (!candidate || !candidate->visible ||
            position_path_keyframe_count(*candidate) == 0)
            continue;
        /* A group/parent motion path must not snap back to artwork carried by
         * that same hierarchy. Only actual keyframe vertices are targets, and
         * descendants of the edited layer are explicitly excluded. */
        if (candidate->id != layer.id &&
            motion_path_layer_depends_on(title_, *candidate, layer.id))
            continue;
        for (int index = 0;
             index < static_cast<int>(position_path_keyframe_count(*candidate)); ++index) {
            if (candidate->id == layer.id && index == keyframe_index)
                continue;
            targets.push_back({position_keyframe_canvas_point(*candidate, index, {}),
                               fxm_tr("OBSTitles.MotionPathVertex")});
        }
    }

    snap_feedback_.clear();
    QPointF snapped = canvas_point;
    const double tolerance = 6.0 / std::max(0.1, view_scale());
    auto snap_axis = [&](bool x_axis) {
        double best_distance = tolerance + 1.0;
        double best_value = x_axis ? canvas_point.x() : canvas_point.y();
        QString best_label;
        for (const Target &target : targets) {
            const double candidate_value = x_axis ? target.point.x() : target.point.y();
            const double distance = std::abs(candidate_value -
                (x_axis ? canvas_point.x() : canvas_point.y()));
            if (distance < best_distance) {
                best_distance = distance;
                best_value = candidate_value;
                best_label = target.label;
            }
        }
        if (best_distance <= tolerance) {
            if (x_axis) snapped.setX(best_value);
            else snapped.setY(best_value);
            add_snap_feedback(x_axis, best_value, best_label);
        }
    };
    snap_axis(true);
    snap_axis(false);
    return snapped;
}

bool CanvasPreview::apply_position_vertex_drag(
    const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!spatial_vertex_drag_.active || !title_) return false;
    auto layer = title_->find_layer(spatial_vertex_drag_.layer_id);
    if (!layer || layer->locked) return false;
    const int index = spatial_vertex_drag_.keyframe_index;
    const size_t count = position_path_keyframe_count(*layer);
    if (index < 0 || index >= static_cast<int>(count)) return false;

    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const QPointF canvas_target = snap_position_keyframe_canvas_point(
        *layer, index, view_to_canvas(view_pt), allow_snap);
    const double title_time = layer->in_time + spatial_vertex_drag_.keyframe_time;

    if (spatial_vertex_drag_.path_3d) {
        const std::shared_ptr<Title> projected_title = projection_title();
        const Layer *projected_layer = projected_title
            ? editor_find_layer_by_id(projected_title, layer->id) : nullptr;
        if (!projected_layer) return false;
        QVector3D world_target;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (!fxm::transform3d::canvas_point_on_world_plane(
                *projected_title, canvas_target, title_time,
                spatial_vertex_drag_.plane_point,
                spatial_vertex_drag_.plane_normal,
                world_target, camera_id))
            return false;
        bool invertible = false;
        const QMatrix4x4 inverse = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time).inverted(&invertible);
        if (!invertible) return false;
        const QVector3D local = inverse.map(world_target);
        Vector3Keyframe &keyframe =
            layer->position_3d.keyframes[static_cast<size_t>(index)];
        const Vec3Value next{local.x(), local.y(), local.z()};
        const Vec3Value delta{next.x - keyframe.value.x,
                              next.y - keyframe.value.y,
                              next.z - keyframe.value.z};
        if (std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) < 1e-9)
            return false;
        keyframe.value = next;
        mirror_position_3d_to_legacy(*layer, keyframe.time);
    } else {
        const QTransform parent = editor_parent_world_transform(title_, *layer, title_time);
        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (!invertible) return false;
        const QPointF local_target = inverse.map(canvas_target);
        VectorKeyframe &keyframe = layer->position.keyframes[static_cast<size_t>(index)];
        const Vec2Value next{local_target.x(), local_target.y()};
        if (std::hypot(next.x - keyframe.value.x,
                       next.y - keyframe.value.y) < 1e-9)
            return false;
        keyframe.value = next;
    }

    spatial_vertex_drag_.moved = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    dirty_ = true;
    gpu_model_dirty_ = true;
    invalidate_canvas_overlay_caches();
    refresh_preview();
    update();
    return true;
}

bool CanvasPreview::apply_position_tangent_drag(
    const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!spatial_tangent_drag_.active || !title_) return false;
    auto layer = title_->find_layer(spatial_tangent_drag_.layer_id);
    if (!layer || layer->locked) return false;
    const int index = spatial_tangent_drag_.keyframe_index;
    const size_t count = position_path_keyframe_count(*layer);
    if (index < 0 || index >= static_cast<int>(count)) return false;
    const size_t key_index = static_cast<size_t>(index);
    const double key_time = position_path_keyframe_time(*layer, key_index);
    const double title_time = layer->in_time + key_time;

    if (spatial_tangent_drag_.path_3d) {
        Vector3Keyframe &keyframe = layer->position_3d.keyframes[key_index];
        QPointF canvas_target = view_to_canvas(view_pt);
        const QPointF anchor_canvas = position_keyframe_canvas_point(*layer, index, {});
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            const QPointF delta = canvas_target - anchor_canvas;
            const double angle = std::atan2(delta.y(), delta.x());
            const double tangent_length = std::hypot(delta.x(), delta.y());
            const double step = std::acos(-1.0) / 4.0;
            const double snapped = std::round(angle / step) * step;
            canvas_target = anchor_canvas + QPointF(
                std::cos(snapped) * tangent_length,
                std::sin(snapped) * tangent_length);
        }
        const std::shared_ptr<Title> projected_title = projection_title();
        const Layer *projected_layer = projected_title
            ? editor_find_layer_by_id(projected_title, layer->id) : nullptr;
        if (!projected_layer) return false;
        QVector3D world_target;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projected_title, *projected_layer, title_time);
        if (!fxm::transform3d::canvas_point_on_world_plane(
                *projected_title, canvas_target, title_time,
                spatial_tangent_drag_.plane_point,
                spatial_tangent_drag_.plane_normal,
                world_target, camera_id))
            return false;
        bool invertible = false;
        const QMatrix4x4 inverse = fxm::transform3d::layer_parent_world_matrix(
            *projected_title, *projected_layer, title_time).inverted(&invertible);
        if (!invertible) return false;
        const QVector3D local_target = inverse.map(world_target);
        Vec3Value tangent{local_target.x() - keyframe.value.x,
                          local_target.y() - keyframe.value.y,
                          local_target.z() - keyframe.value.z};
        if (modifiers.testFlag(Qt::AltModifier))
            spatial_tangent_drag_.linked = false;
        const bool linked = spatial_tangent_drag_.linked;
        const Vec3Value opposite_before = spatial_tangent_drag_.incoming
            ? layer->position_3d.resolved_outgoing_tangent(key_index)
            : layer->position_3d.resolved_incoming_tangent(key_index);
        const auto magnitude = [](const Vec3Value &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        };
        double opposite_length = magnitude(opposite_before);
        const double dragged_length = magnitude(tangent);
        if (opposite_length < 1e-9) opposite_length = dragged_length;
        Vec3Value opposite{};
        if (linked && dragged_length >= 1e-9) {
            const double factor = -opposite_length / dragged_length;
            opposite = {tangent.x * factor, tangent.y * factor, tangent.z * factor};
        }
        if (spatial_tangent_drag_.incoming) {
            keyframe.incoming_tangent = tangent;
            if (linked) keyframe.outgoing_tangent = opposite;
        } else {
            keyframe.outgoing_tangent = tangent;
            if (linked) keyframe.incoming_tangent = opposite;
        }
        keyframe.spatial_tangents_linked = linked;
        keyframe.spatial_mode = linked
            ? SpatialInterpolationMode::ContinuousBezier
            : SpatialInterpolationMode::ManualBezier;
    } else {
        VectorKeyframe &keyframe = layer->position.keyframes[key_index];
        const QTransform parent = editor_parent_world_transform(title_, *layer, title_time);
        bool invertible = false;
        const QTransform inverse = parent.inverted(&invertible);
        if (!invertible) return false;
        const QPointF canvas_anchor = parent.map(
            QPointF(keyframe.value.x, keyframe.value.y));
        QPointF canvas_target = view_to_canvas(view_pt);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            const QPointF delta = canvas_target - canvas_anchor;
            const double angle = std::atan2(delta.y(), delta.x());
            const double tangent_length = std::hypot(delta.x(), delta.y());
            const double step = std::acos(-1.0) / 4.0;
            const double snapped = std::round(angle / step) * step;
            canvas_target = canvas_anchor + QPointF(
                std::cos(snapped) * tangent_length,
                std::sin(snapped) * tangent_length);
        }
        const QPointF local_target = inverse.map(canvas_target);
        Vec2Value tangent{local_target.x() - keyframe.value.x,
                          local_target.y() - keyframe.value.y};
        if (modifiers.testFlag(Qt::AltModifier))
            spatial_tangent_drag_.linked = false;
        const bool linked = spatial_tangent_drag_.linked;
        const Vec2Value opposite_before = spatial_tangent_drag_.incoming
            ? layer->position.resolved_outgoing_tangent(key_index)
            : layer->position.resolved_incoming_tangent(key_index);
        double opposite_length = std::hypot(opposite_before.x, opposite_before.y);
        const double dragged_length = std::hypot(tangent.x, tangent.y);
        if (opposite_length < 1e-9) opposite_length = dragged_length;
        Vec2Value opposite{};
        if (linked && dragged_length >= 1e-9) {
            opposite = {-tangent.x * opposite_length / dragged_length,
                        -tangent.y * opposite_length / dragged_length};
        }
        if (spatial_tangent_drag_.incoming) {
            keyframe.incoming_tangent = tangent;
            if (linked) keyframe.outgoing_tangent = opposite;
        } else {
            keyframe.outgoing_tangent = tangent;
            if (linked) keyframe.incoming_tangent = opposite;
        }
        keyframe.spatial_tangents_linked = linked;
        keyframe.spatial_mode = linked
            ? SpatialInterpolationMode::ContinuousBezier
            : SpatialInterpolationMode::ManualBezier;
    }

    spatial_tangent_drag_.moved = true;
    drag_changed_ = true;
    drag_current_view_ = view_pt;
    dirty_ = true;
    gpu_model_dirty_ = true;
    invalidate_canvas_overlay_caches();
    refresh_preview();
    update();
    return true;
}

bool CanvasPreview::add_position_keyframe_at_path(const QPointF &view_pt)
{
    auto layer = selected_layer();
    if (!layer || layer->locked || !position_motion_path_visible(*layer) ||
        !inline_text_layer_id_.empty())
        return false;
    size_t segment = 0;
    double temporal_progress = 0.0;
    if (!hit_test_position_motion_path(*layer, view_pt, &segment,
                                       &temporal_progress, 7.0))
        return false;
    size_t inserted = 0;
    size_t count = 0;
    if (position_path_is_3d(*layer)) {
        const double spatial_progress =
            layer->position_3d.spatial_progress_for_segment(
                segment, temporal_progress);
        inserted = layer->position_3d.split_spatial_segment(
            segment, temporal_progress, spatial_progress);
        count = layer->position_3d.keyframes.size();
    } else {
        const double spatial_progress = layer->position.spatial_progress_for_segment(
            segment, temporal_progress);
        inserted = layer->position.split_spatial_segment(
            segment, temporal_progress, spatial_progress);
        count = layer->position.keyframes.size();
    }
    if (inserted >= count) return false;
    selected_position_keyframe_index_ = static_cast<int>(inserted);
    dirty_ = true;
    gpu_model_dirty_ = true;
    invalidate_canvas_overlay_caches();
    emit layer_geometry_changed();
    update();
    return true;
}

bool CanvasPreview::show_position_motion_path_context_menu(QContextMenuEvent *ev)
{
    auto layer = selected_layer();
    if (!ev || !layer || layer->locked || !position_motion_path_visible(*layer) ||
        !inline_text_layer_id_.empty())
        return false;

    /* A motion-path segment is only a visual/editor guide. Let it fall through
     * to the normal layer context menu; otherwise right-clicking the same layer
     * appears to choose a random menu depending on whether the path happens to
     * cross that pixel. Keyframe actions are intentionally scoped to the
     * explicit keyframe vertex handle. */
    const int keyframe_index =
        hit_test_position_keyframe_vertices(*layer, ev->pos());
    if (keyframe_index < 0)
        return false;
    const size_t count = position_path_keyframe_count(*layer);
    if (keyframe_index >= static_cast<int>(count))
        return false;

    selected_position_keyframe_index_ = keyframe_index;
    const size_t index = static_cast<size_t>(keyframe_index);
    const SpatialInterpolationMode current_mode =
        position_path_spatial_mode(*layer, index);
    const bool current_rove = position_path_roving(*layer, index);
    const bool linked = position_path_is_3d(*layer)
        ? layer->position_3d.keyframes[index].spatial_tangents_linked
        : layer->position.keyframes[index].spatial_tangents_linked;

    QMenu menu(this);
    auto *mode_group = new QActionGroup(&menu);
    mode_group->setExclusive(true);
    std::map<QAction *, SpatialInterpolationMode> modes;
    for (auto [label, mode] : std::initializer_list<
             std::pair<QString, SpatialInterpolationMode>>{
             {fxm_tr("OBSTitles.SpatialLinear"), SpatialInterpolationMode::Linear},
             {fxm_tr("OBSTitles.SpatialAutoBezier"), SpatialInterpolationMode::AutoBezier},
             {fxm_tr("OBSTitles.SpatialContinuousBezier"), SpatialInterpolationMode::ContinuousBezier},
             {fxm_tr("OBSTitles.SpatialManualBezier"), SpatialInterpolationMode::ManualBezier},
         }) {
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setActionGroup(mode_group);
        action->setChecked(current_mode == mode);
        modes[action] = mode;
    }
    menu.addSeparator();
    QAction *rove = menu.addAction(fxm_tr("OBSTitles.RoveAcrossTime"));
    rove->setCheckable(true);
    rove->setChecked(current_rove);
    rove->setEnabled(keyframe_index > 0 &&
        keyframe_index + 1 < static_cast<int>(count));
    menu.addSeparator();
    QAction *break_tangents = menu.addAction(
        fxm_tr("OBSTitles.BreakSpatialTangents"));
    QAction *join_tangents = menu.addAction(
        fxm_tr("OBSTitles.JoinSpatialTangents"));
    break_tangents->setEnabled(linked);
    join_tangents->setEnabled(!linked);

    QAction *chosen = menu.exec(ev->globalPos());
    if (!chosen) { ev->accept(); return true; }

    bool changed = false;
    auto apply_mode = [&](SpatialInterpolationMode mode) {
        if (position_path_is_3d(*layer))
            layer->position_3d.set_spatial_mode(index, mode);
        else
            layer->position.set_spatial_mode(index, mode);
    };
    auto apply_rove = [&](bool value) {
        if (position_path_is_3d(*layer))
            layer->position_3d.set_rove_across_time(index, value);
        else
            layer->position.set_rove_across_time(index, value);
    };
    auto apply_link = [&](bool value) {
        if (position_path_is_3d(*layer))
            layer->position_3d.set_spatial_tangents_linked(index, value);
        else
            layer->position.set_spatial_tangents_linked(index, value);
    };

    const auto mode_it = modes.find(chosen);
    if (mode_it != modes.end()) {
        if (current_mode != mode_it->second) {
            apply_mode(mode_it->second);
            changed = true;
        }
    } else if (chosen == rove) {
        apply_rove(!current_rove);
        changed = true;
    } else if (chosen == break_tangents) {
        apply_link(false);
        changed = true;
    } else if (chosen == join_tangents) {
        apply_link(true);
        changed = true;
    }

    if (changed) {
        dirty_ = true;
        gpu_model_dirty_ = true;
        invalidate_motion_path_sample_cache();
        invalidate_canvas_overlay_caches();
        emit layer_geometry_changed();
        update();
    }
    ev->accept();
    return true;
}

bool CanvasPreview::update_position_motion_path_hover(const QPointF &view_pt)
{
    MotionPathHoverType next_type = MotionPathHoverType::None;
    int next_keyframe = -1;
    auto layer = selected_layer();
    if (layer && !layer->locked && inline_text_layer_id_.empty() &&
        position_motion_path_visible(*layer) && drag_mode_ == DragMode::None) {
        int tangent_index = -1;
        const DragMode tangent = hit_test_position_tangent_handles(
            *layer, view_pt, &tangent_index);
        if (tangent == DragMode::PositionInTangent) {
            next_type = MotionPathHoverType::InTangent;
            next_keyframe = tangent_index;
        } else if (tangent == DragMode::PositionOutTangent) {
            next_type = MotionPathHoverType::OutTangent;
            next_keyframe = tangent_index;
        } else {
            next_keyframe = hit_test_position_keyframe_vertices(*layer, view_pt);
            if (next_keyframe >= 0) {
                next_type = MotionPathHoverType::Vertex;
            } else if (hit_test_position_motion_path(
                           *layer, view_pt, nullptr, nullptr, 6.0)) {
                next_type = MotionPathHoverType::Path;
            }
        }
    }

    const bool changed = next_type != motion_path_hover_type_ ||
        next_keyframe != motion_path_hover_keyframe_index_;
    motion_path_hover_type_ = next_type;
    motion_path_hover_keyframe_index_ = next_keyframe;
    if (changed) {
        /* Hover is overlay-only. Never dirty the title model or request a
         * rendered canvas frame merely to highlight a handle. */
        invalidate_selection_overlay_cache();
        gpu_overlay_dirty_ = true;
        update();
    }

    if (next_type == MotionPathHoverType::InTangent ||
        next_type == MotionPathHoverType::OutTangent)
        setCursor(Qt::CrossCursor);
    else if (next_type == MotionPathHoverType::Vertex)
        setCursor(Qt::SizeAllCursor);
    else if (next_type == MotionPathHoverType::Path)
        setCursor(Qt::PointingHandCursor);
    return next_type != MotionPathHoverType::None;
}
