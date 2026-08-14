namespace {
constexpr const char *kEditorViewCameraId = "__fxm_editor_3d_view__";

static QVector3D normalized_or(const QVector3D &value, const QVector3D &fallback)
{
    if (value.lengthSquared() < 1.0e-10f)
        return fallback;
    return value.normalized();
}

static QMatrix4x4 gizmo_euler_rotation_matrix(double x, double y, double z)
{
    QMatrix4x4 result;
    result.setToIdentity();
    result.rotate(static_cast<float>(z), 0.0f, 0.0f, 1.0f);
    result.rotate(static_cast<float>(y), 0.0f, 1.0f, 0.0f);
    result.rotate(static_cast<float>(x), 1.0f, 0.0f, 0.0f);
    return result;
}

static QMatrix4x4 gizmo_rotation_basis(const QMatrix4x4 &matrix)
{
    const QVector3D unit_x(1.0f, 0.0f, 0.0f);
    const QVector3D unit_y(0.0f, 1.0f, 0.0f);
    const QVector3D unit_z(0.0f, 0.0f, 1.0f);
    QVector3D x = normalized_or(matrix.mapVector(unit_x), unit_x);
    const QVector3D mapped_y = matrix.mapVector(unit_y);
    const QVector3D mapped_z = matrix.mapVector(unit_z);
    QVector3D y = mapped_y - x * QVector3D::dotProduct(mapped_y, x);
    if (y.lengthSquared() < 1.0e-10f) {
        const QVector3D reference = std::abs(x.z()) < 0.9f ? unit_z : unit_y;
        y = QVector3D::crossProduct(reference, x);
    }
    y = normalized_or(y, unit_y);
    QVector3D z = mapped_z - x * QVector3D::dotProduct(mapped_z, x) -
        y * QVector3D::dotProduct(mapped_z, y);
    if (z.lengthSquared() < 1.0e-10f)
        z = QVector3D::crossProduct(x, y);
    z = normalized_or(z, unit_z);

    QMatrix4x4 result;
    result.setToIdentity();
    result.setColumn(0, QVector4D(x, 0.0f));
    result.setColumn(1, QVector4D(y, 0.0f));
    result.setColumn(2, QVector4D(z, 0.0f));
    return result;
}

static QVector3D gizmo_axis_world_direction(
    TransformAxisSpace space, int axis, const QMatrix4x4 &parent_basis,
    const QMatrix4x4 &orientation, const QMatrix4x4 &rotation)
{
    static const QVector3D canonical_axes[] = {
        QVector3D(1.0f, 0.0f, 0.0f),
        QVector3D(0.0f, 1.0f, 0.0f),
        QVector3D(0.0f, 0.0f, 1.0f),
    };
    axis = std::clamp(axis, 0, 2);
    QVector3D direction = canonical_axes[axis];
    if (space == TransformAxisSpace::Local)
        direction = (parent_basis * orientation * rotation).mapVector(direction);
    else if (space == TransformAxisSpace::Parent)
        direction = parent_basis.mapVector(direction);
    return normalized_or(direction, canonical_axes[axis]);
}

static LayerVector3Value gizmo_decompose_zyx_rotation(
    const QMatrix4x4 &matrix)
{
    const QMatrix4x4 rotation = gizmo_rotation_basis(matrix);
    const double y = std::asin(std::clamp(
        -static_cast<double>(rotation(2, 0)), -1.0, 1.0));
    const double cos_y = std::cos(y);
    double x = 0.0;
    double z = 0.0;
    if (std::abs(cos_y) > 1.0e-7) {
        x = std::atan2(static_cast<double>(rotation(2, 1)),
                       static_cast<double>(rotation(2, 2)));
        z = std::atan2(static_cast<double>(rotation(1, 0)),
                       static_cast<double>(rotation(0, 0)));
    } else {
        x = std::atan2(-static_cast<double>(rotation(1, 2)),
                       static_cast<double>(rotation(1, 1)));
    }
    return {qRadiansToDegrees(x), qRadiansToDegrees(y),
            qRadiansToDegrees(z)};
}

static double point_segment_distance(const QPointF &point, const QPointF &a,
                                     const QPointF &b)
{
    const QPointF ab = b - a;
    const double length2 = QPointF::dotProduct(ab, ab);
    if (length2 < 1.0e-9)
        return QLineF(point, a).length();
    const double t = std::clamp(QPointF::dotProduct(point - a, ab) / length2,
                                0.0, 1.0);
    return QLineF(point, a + ab * t).length();
}

static int gizmo_axis_index(CanvasPreview::GizmoAxis axis)
{
    switch (axis) {
    case CanvasPreview::GizmoAxis::X: return 0;
    case CanvasPreview::GizmoAxis::Y: return 1;
    case CanvasPreview::GizmoAxis::Z: return 2;
    default: return -1;
    }
}

static std::pair<int, int> gizmo_plane_indices(CanvasPreview::GizmoAxis axis)
{
    switch (axis) {
    case CanvasPreview::GizmoAxis::XY: return {0, 1};
    case CanvasPreview::GizmoAxis::XZ: return {0, 2};
    case CanvasPreview::GizmoAxis::YZ: return {1, 2};
    default: return {-1, -1};
    }
}

static QColor gizmo_axis_color(int axis)
{
    if (axis == 0) return QColor(235, 72, 72);
    if (axis == 1) return QColor(76, 205, 105);
    return QColor(78, 135, 245);
}
}

std::shared_ptr<Title> CanvasPreview::projection_title() const
{
    if (!title_)
        return nullptr;
    if (editor_3d_view_ == Editor3DView::ActiveCamera)
        return title_;

    if (!editor_projection_title_)
        editor_projection_title_ = std::make_shared<Title>();
    if (editor_projection_dirty_) {
        /* Keep layer pointers shared with the editable title. The GPU session
         * takes its own immutable deep snapshot when this shallow editor view
         * is submitted. */
        *editor_projection_title_ = *title_;
        editor_projection_title_->render_camera_override_id = kEditorViewCameraId;
        auto &cameras = editor_projection_title_->cameras;
        cameras.erase(std::remove_if(cameras.begin(), cameras.end(),
            [](const TitleCamera &camera) {
                return camera.id == kEditorViewCameraId;
            }), cameras.end());
        cameras.push_back(build_editor_view_camera());
        editor_projection_dirty_ = false;
    }
    return editor_projection_title_;
}

void CanvasPreview::invalidate_editor_projection(bool transform_only)
{
    editor_projection_dirty_ = true;
    editor_camera_transform_only_pending_ =
        editor_camera_transform_only_pending_ || transform_only;
    gpu_model_dirty_ = true;
    prefetched_cache_frame_ = QImage();
    prefetched_cache_time_ = -1.0;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    position_text_editor();
    update();
}

void CanvasPreview::reset_editor_3d_navigation()
{
    const double width = title_ ? std::max(1, title_->width) : 1920.0;
    const double height = title_ ? std::max(1, title_->height) : 1080.0;
    editor_3d_target_ = QVector3D(static_cast<float>(width * 0.5),
                                  static_cast<float>(height * 0.5), 0.0f);
    editor_3d_distance_ = std::max(500.0, std::max(width, height) * 1.25);
    editor_3d_yaw_ = -25.0;
    editor_3d_pitch_ = -18.0;
    editor_3d_ortho_zoom_ = 1.0;
}

TitleCamera CanvasPreview::build_editor_view_camera() const
{
    TitleCamera camera;
    camera.id = kEditorViewCameraId;
    camera.name = "Editor View";
    camera.use_canvas_default = false;
    camera.projection = editor_3d_view_ == Editor3DView::CustomPerspective
        ? CameraProjection::Perspective
        : CameraProjection::Orthographic;

    QVector3D target = editor_3d_target_;
    QVector3D position = target;
    const double distance = std::max(1.0, editor_3d_distance_);
    switch (editor_3d_view_) {
    case Editor3DView::Front:
        position += QVector3D(0.0f, 0.0f, static_cast<float>(-distance));
        break;
    case Editor3DView::Back:
        position += QVector3D(0.0f, 0.0f, static_cast<float>(distance));
        break;
    case Editor3DView::Left:
        position += QVector3D(static_cast<float>(-distance), 0.0f, 0.0f);
        break;
    case Editor3DView::Right:
        position += QVector3D(static_cast<float>(distance), 0.0f, 0.0f);
        break;
    case Editor3DView::Top:
        position += QVector3D(0.0f, static_cast<float>(-distance), 0.0f);
        break;
    case Editor3DView::Bottom:
        position += QVector3D(0.0f, static_cast<float>(distance), 0.0f);
        break;
    case Editor3DView::CustomPerspective: {
        const double yaw = qDegreesToRadians(editor_3d_yaw_);
        const double pitch = qDegreesToRadians(editor_3d_pitch_);
        const double horizontal = distance * std::cos(pitch);
        position += QVector3D(
            static_cast<float>(horizontal * std::sin(yaw)),
            static_cast<float>(-distance * std::sin(pitch)),
            static_cast<float>(-horizontal * std::cos(yaw)));
        break;
    }
    case Editor3DView::ActiveCamera:
        break;
    }

    camera.position_x.static_value = position.x();
    camera.position_y.static_value = position.y();
    camera.position_z.static_value = position.z();
    camera.target_x.static_value = target.x();
    camera.target_y.static_value = target.y();
    camera.target_z.static_value = target.z();
    camera.rotation_x.static_value = 0.0;
    camera.rotation_y.static_value = 0.0;
    camera.rotation_z.static_value = 0.0;
    camera.field_of_view.static_value = 45.0;
    const double height = title_ ? std::max(1, title_->height) : 1080.0;
    camera.focal_length.static_value =
        height / (2.0 * std::tan(qDegreesToRadians(45.0 * 0.5)));
    camera.zoom.static_value = camera.projection == CameraProjection::Orthographic
        ? std::clamp(editor_3d_ortho_zoom_, 0.001, 1000.0)
        : 1.0;
    camera.near_clip.static_value = 0.01;
    camera.far_clip.static_value = std::max(100000.0, distance * 100.0);
    return camera;
}

QString CanvasPreview::editor_3d_view_name() const
{
    switch (editor_3d_view_) {
    case Editor3DView::ActiveCamera: return QStringLiteral("Active Camera");
    case Editor3DView::Front: return QStringLiteral("Front");
    case Editor3DView::Back: return QStringLiteral("Back");
    case Editor3DView::Left: return QStringLiteral("Left");
    case Editor3DView::Right: return QStringLiteral("Right");
    case Editor3DView::Top: return QStringLiteral("Top");
    case Editor3DView::Bottom: return QStringLiteral("Bottom");
    case Editor3DView::CustomPerspective: return QStringLiteral("Custom Perspective");
    }
    return QStringLiteral("Active Camera");
}

void CanvasPreview::set_editor_3d_view(Editor3DView view)
{
    if (editor_3d_view_ == view)
        return;

    if (view == Editor3DView::CustomPerspective) {
        switch (editor_3d_view_) {
        case Editor3DView::Front: editor_3d_yaw_ = 0.0; editor_3d_pitch_ = 0.0; break;
        case Editor3DView::Back: editor_3d_yaw_ = 180.0; editor_3d_pitch_ = 0.0; break;
        case Editor3DView::Left: editor_3d_yaw_ = -90.0; editor_3d_pitch_ = 0.0; break;
        case Editor3DView::Right: editor_3d_yaw_ = 90.0; editor_3d_pitch_ = 0.0; break;
        case Editor3DView::Top: editor_3d_yaw_ = 0.0; editor_3d_pitch_ = 89.0; break;
        case Editor3DView::Bottom: editor_3d_yaw_ = 0.0; editor_3d_pitch_ = -89.0; break;
        default: break;
        }
    }

    editor_3d_view_ = view;
    editor_3d_navigation_mode_ = Editor3DNavigationMode::None;
    invalidate_editor_projection(true);
    emit editor_3d_view_changed(static_cast<int>(editor_3d_view_));
}

void CanvasPreview::set_3d_gizmo_mode(GizmoMode mode)
{
    if (gizmo_mode_ == mode)
        return;
    gizmo_mode_ = mode;
    gizmo_drag_ = GizmoDragState{};
    gizmo_hover_axis_ = GizmoAxis::None;
    invalidate_canvas_overlay_caches();
    update();
    emit gizmo_mode_changed(static_cast<int>(mode));
}

TransformAxisSpace CanvasPreview::gizmo_space() const
{
    const auto layer = selected_layer();
    return layer ? layer->transform_axis_space : TransformAxisSpace::Local;
}

void CanvasPreview::set_3d_depth_debug_visible(bool visible)
{
    if (editor_3d_depth_debug_visible_ == visible)
        return;
    editor_3d_depth_debug_visible_ = visible;
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_3d_normals_visible(bool visible)
{
    if (editor_3d_normals_visible_ == visible)
        return;
    editor_3d_normals_visible_ = visible;
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_3d_gizmo_space(TransformAxisSpace space)
{
    bool changed = false;
    for (const auto &layer : selected_layers()) {
        if (!layer || layer->dimension_mode != LayerDimensionMode::ThreeD ||
            !fxm::transform3d::layer_supports_3d(*layer))
            continue;
        if (layer->transform_axis_space != space) {
            layer->transform_axis_space = space;
            changed = true;
        }
    }
    emit gizmo_space_changed(static_cast<int>(space));
    if (changed) {
        gpu_model_dirty_ = true;
        dirty_ = true;
        invalidate_canvas_overlay_caches();
        emit layer_geometry_changed();
        update();
    }
}

bool CanvasPreview::begin_editor_3d_navigation(QMouseEvent *ev)
{
    if (!ev || !title_ || !ev->modifiers().testFlag(Qt::AltModifier))
        return false;

    /* Alt+Left is the canvas duplication gesture whenever it starts on an
     * object (or one of the selected transform handles). Keep orbit available
     * on empty canvas, while middle/right Alt navigation remains unchanged. */
    if (ev->button() == Qt::LeftButton &&
        (hit_test_selected(ev->position()) != DragMode::None ||
         layer_at_view_pos(ev->position()))) {
        return false;
    }

    Editor3DNavigationMode mode = Editor3DNavigationMode::None;
    if (ev->button() == Qt::LeftButton)
        mode = Editor3DNavigationMode::Orbit;
    else if (ev->button() == Qt::MiddleButton)
        mode = Editor3DNavigationMode::Pan;
    else if (ev->button() == Qt::RightButton)
        mode = Editor3DNavigationMode::Dolly;
    if (mode == Editor3DNavigationMode::None)
        return false;

    if (mode == Editor3DNavigationMode::Orbit &&
        editor_3d_view_ != Editor3DView::CustomPerspective) {
        set_editor_3d_view(Editor3DView::CustomPerspective);
    } else if (editor_3d_view_ == Editor3DView::ActiveCamera) {
        set_editor_3d_view(Editor3DView::CustomPerspective);
    }

    editor_3d_navigation_mode_ = mode;
    editor_3d_navigation_start_view_ = ev->position();
    editor_3d_navigation_start_target_ = editor_3d_target_;
    editor_3d_navigation_start_distance_ = editor_3d_distance_;
    editor_3d_navigation_start_yaw_ = editor_3d_yaw_;
    editor_3d_navigation_start_pitch_ = editor_3d_pitch_;
    editor_3d_navigation_start_ortho_zoom_ = editor_3d_ortho_zoom_;
    setCursor(mode == Editor3DNavigationMode::Orbit ? Qt::SizeAllCursor
              : mode == Editor3DNavigationMode::Pan ? Qt::ClosedHandCursor
                                                    : Qt::SizeVerCursor);
    ev->accept();
    return true;
}

bool CanvasPreview::update_editor_3d_navigation(QMouseEvent *ev)
{
    if (!ev || editor_3d_navigation_mode_ == Editor3DNavigationMode::None)
        return false;
    const QPointF delta = ev->position() - editor_3d_navigation_start_view_;

    if (editor_3d_navigation_mode_ == Editor3DNavigationMode::Orbit) {
        editor_3d_yaw_ = editor_3d_navigation_start_yaw_ + delta.x() * 0.35;
        editor_3d_pitch_ = std::clamp(
            editor_3d_navigation_start_pitch_ - delta.y() * 0.35,
            -89.5, 89.5);
    } else if (editor_3d_navigation_mode_ == Editor3DNavigationMode::Dolly) {
        if (editor_3d_view_ == Editor3DView::CustomPerspective) {
            editor_3d_distance_ = std::clamp(
                editor_3d_navigation_start_distance_ * std::exp(delta.y() * 0.01),
                1.0, 10000000.0);
        } else {
            editor_3d_ortho_zoom_ = std::clamp(
                editor_3d_navigation_start_ortho_zoom_ * std::exp(-delta.y() * 0.01),
                0.001, 1000.0);
        }
    } else if (editor_3d_navigation_mode_ == Editor3DNavigationMode::Pan) {
        const TitleCamera camera = build_editor_view_camera();
        const QVector3D position(
            static_cast<float>(camera.position_x.static_value),
            static_cast<float>(camera.position_y.static_value),
            static_cast<float>(camera.position_z.static_value));
        QVector3D forward = normalized_or(editor_3d_navigation_start_target_ - position,
                                          QVector3D(0.0f, 0.0f, 1.0f));
        QVector3D up(0.0f, -1.0f, 0.0f);
        if (std::abs(QVector3D::dotProduct(forward, up)) > 0.999f)
            up = QVector3D(0.0f, 0.0f, -1.0f);
        const QVector3D right = normalized_or(
            QVector3D::crossProduct(forward, up), QVector3D(1.0f, 0.0f, 0.0f));
        const QVector3D down = normalized_or(
            QVector3D::crossProduct(forward, right), QVector3D(0.0f, 1.0f, 0.0f));
        double world_per_pixel = 1.0 / std::max(0.001, editor_3d_ortho_zoom_);
        if (editor_3d_view_ == Editor3DView::CustomPerspective) {
            const double viewport_height = std::max(1, title_->height);
            world_per_pixel = 2.0 * editor_3d_navigation_start_distance_ *
                std::tan(qDegreesToRadians(45.0 * 0.5)) / viewport_height;
        }
        editor_3d_target_ = editor_3d_navigation_start_target_
            - right * static_cast<float>(delta.x() * world_per_pixel)
            - down * static_cast<float>(delta.y() * world_per_pixel);
    }

    invalidate_editor_projection(true);
    ev->accept();
    return true;
}

bool CanvasPreview::finish_editor_3d_navigation(QMouseEvent *ev)
{
    if (!ev || editor_3d_navigation_mode_ == Editor3DNavigationMode::None)
        return false;
    editor_3d_navigation_mode_ = Editor3DNavigationMode::None;
    unsetCursor();
    ev->accept();
    return true;
}

void CanvasPreview::dolly_editor_3d_view(double delta)
{
    if (editor_3d_view_ == Editor3DView::ActiveCamera)
        set_editor_3d_view(Editor3DView::CustomPerspective);
    if (editor_3d_view_ == Editor3DView::CustomPerspective) {
        editor_3d_distance_ = std::clamp(
            editor_3d_distance_ * std::exp(-delta / 480.0),
            1.0, 10000000.0);
    } else {
        editor_3d_ortho_zoom_ = std::clamp(
            editor_3d_ortho_zoom_ * std::exp(delta / 480.0),
            0.001, 1000.0);
    }
    invalidate_editor_projection(true);
}

bool CanvasPreview::collect_editor_3d_bounds(
    const std::vector<std::shared_ptr<Layer>> &layers,
    QVector3D &minimum, QVector3D &maximum) const
{
    if (!title_)
        return false;
    bool have = false;
    for (const auto &layer : layers) {
        if (!layer || !layer->visible || layer_type_is_audio(layer->type) ||
            !fxm::transform3d::layer_supports_3d(*layer))
            continue;
        const QRectF rect = layer_local_rect(*layer);
        if (!rect.isValid() || rect.isEmpty())
            continue;
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
            *title_, *layer, playhead_);
        const QVector3D corners[] = {
            QVector3D(rect.left(), rect.top(), 0.0f),
            QVector3D(rect.right(), rect.top(), 0.0f),
            QVector3D(rect.right(), rect.bottom(), 0.0f),
            QVector3D(rect.left(), rect.bottom(), 0.0f),
        };
        auto include_point = [&](const QVector3D &point) {
            if (!have) {
                minimum = maximum = point;
                have = true;
            } else {
                minimum.setX(std::min(minimum.x(), point.x()));
                minimum.setY(std::min(minimum.y(), point.y()));
                minimum.setZ(std::min(minimum.z(), point.z()));
                maximum.setX(std::max(maximum.x(), point.x()));
                maximum.setY(std::max(maximum.y(), point.y()));
                maximum.setZ(std::max(maximum.z(), point.z()));
            }
        };
        for (const QVector3D &corner : corners)
            include_point(world.map(corner));

        /* Frame Selected includes the complete authored XYZ motion path, not
         * only the layer's current-frame plane. Path values are parent-space;
         * evaluating the parent matrix at each key produces their world-space
         * positions and keeps parented animation framed correctly. */
        if (layer->position_3d_path_enabled) {
            for (const Vector3Keyframe &key : layer->position_3d.keyframes) {
                const double title_time = layer->in_time + key.time;
                const QMatrix4x4 parent_world =
                    fxm::transform3d::layer_parent_world_matrix(
                        *title_, *layer, title_time);
                include_point(parent_world.map(QVector3D(
                    static_cast<float>(key.value.x),
                    static_cast<float>(key.value.y),
                    static_cast<float>(key.value.z))));
            }
            /* Curved segments can extend beyond every key value. Sample the
             * authored cubic in parent space and include its evaluated world
             * positions so Frame Selected encloses the whole path, not merely
             * its vertices. */
            constexpr int kFramePathSamplesPerSegment = 24;
            for (size_t segment = 0;
                 segment + 1 < layer->position_3d.keyframes.size(); ++segment) {
                const Vector3Keyframe &start = layer->position_3d.keyframes[segment];
                const Vector3Keyframe &end = layer->position_3d.keyframes[segment + 1];
                for (int sample = 1; sample < kFramePathSamplesPerSegment; ++sample) {
                    const double temporal_progress =
                        static_cast<double>(sample) / kFramePathSamplesPerSegment;
                    const double spatial_progress =
                        layer->position_3d.spatial_progress_for_segment(
                            segment, temporal_progress);
                    const Vec3Value local =
                        layer->position_3d.evaluate_spatial_segment(
                            segment, spatial_progress);
                    const double local_time = start.time +
                        temporal_progress * (end.time - start.time);
                    const QMatrix4x4 parent_world =
                        fxm::transform3d::layer_parent_world_matrix(
                            *title_, *layer, layer->in_time + local_time);
                    include_point(parent_world.map(QVector3D(
                        static_cast<float>(local.x),
                        static_cast<float>(local.y),
                        static_cast<float>(local.z))));
                }
            }
        }
    }
    return have;
}

void CanvasPreview::frame_3d_selection()
{
    if (!title_)
        return;
    QVector3D minimum, maximum;
    if (!collect_editor_3d_bounds(selected_layers(), minimum, maximum))
        return;
    if (editor_3d_view_ == Editor3DView::ActiveCamera)
        set_editor_3d_view(Editor3DView::CustomPerspective);
    editor_3d_target_ = (minimum + maximum) * 0.5f;
    const double radius = std::max(25.0, static_cast<double>((maximum - minimum).length()) * 0.5);
    editor_3d_distance_ = std::max(100.0,
        radius / std::tan(qDegreesToRadians(45.0 * 0.5)) * 1.35);
    editor_3d_ortho_zoom_ = std::clamp(
        std::min(title_->width, title_->height) / (radius * 2.7), 0.001, 1000.0);
    invalidate_editor_projection(true);
}

void CanvasPreview::frame_3d_all()
{
    if (!title_)
        return;
    QVector3D minimum, maximum;
    if (!collect_editor_3d_bounds(title_->layers, minimum, maximum))
        return;
    if (editor_3d_view_ == Editor3DView::ActiveCamera)
        set_editor_3d_view(Editor3DView::CustomPerspective);
    editor_3d_target_ = (minimum + maximum) * 0.5f;
    const double radius = std::max(25.0, static_cast<double>((maximum - minimum).length()) * 0.5);
    editor_3d_distance_ = std::max(100.0,
        radius / std::tan(qDegreesToRadians(45.0 * 0.5)) * 1.35);
    editor_3d_ortho_zoom_ = std::clamp(
        std::min(title_->width, title_->height) / (radius * 2.7), 0.001, 1000.0);
    invalidate_editor_projection(true);
}

CanvasPreview::GizmoGeometry CanvasPreview::build_3d_gizmo_geometry() const
{
    if (gizmo_geometry_cache_valid_) {
        fxm::perf::add(fxm::perf::Counter::GizmoGeometryCacheHits);
        return gizmo_geometry_cache_;
    }
    fxm::perf::add(fxm::perf::Counter::GizmoGeometryCacheMisses);
    GizmoGeometry geometry;
    auto cache_result = [this](GizmoGeometry result) {
        gizmo_geometry_cache_ = result;
        gizmo_geometry_cache_valid_ = true;
        return result;
    };
    const auto layer = selected_layer();
    const auto projection = projection_title();
    if (!layer || !projection ||
        layer->dimension_mode != LayerDimensionMode::ThreeD ||
        !fxm::transform3d::layer_supports_3d(*layer))
        return cache_result(std::move(geometry));

    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        *projection, *layer, playhead_);
    const QMatrix4x4 parent = fxm::transform3d::layer_parent_world_matrix(
        *projection, *layer, playhead_);
    const QMatrix4x4 parent_rotation_basis = gizmo_rotation_basis(parent);
    const double gizmo_local_time = std::clamp(
        playhead_ - layer->in_time, 0.0,
        std::max(0.0, layer->out_time - layer->in_time));
    const LayerVector3Value current_orientation =
        evaluated_layer_orientation_3d(*layer, gizmo_local_time);
    const LayerVector3Value current_rotation =
        evaluated_layer_rotation_3d(*layer, gizmo_local_time);
    const QMatrix4x4 orientation = gizmo_euler_rotation_matrix(
        current_orientation.x, current_orientation.y, current_orientation.z);
    const QMatrix4x4 rotation = gizmo_euler_rotation_matrix(
        current_rotation.x, current_rotation.y, current_rotation.z);
    geometry.origin_world = world.map(QVector3D(0.0f, 0.0f, 0.0f));

    for (int axis = 0; axis < 3; ++axis) {
        geometry.axes_world[axis] = gizmo_axis_world_direction(
            layer->transform_axis_space, axis, parent_rotation_basis,
            orientation, rotation);
    }

    const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
        *projection, *layer, playhead_);

    if (editor_3d_view_ == Editor3DView::CustomPerspective)
        geometry.world_length = std::clamp(editor_3d_distance_ * 0.095, 35.0, 5000.0);
    else if (editor_3d_view_ != Editor3DView::ActiveCamera)
        geometry.world_length = std::clamp(120.0 / std::max(0.001, editor_3d_ortho_zoom_),
                                           20.0, 5000.0);
    else if (title_)
        geometry.world_length = std::clamp(std::max(title_->width, title_->height) / 9.0,
                                           35.0, 500.0);

    QPointF origin_canvas;
    if (!fxm::transform3d::project_world_point(
            *projection, geometry.origin_world, playhead_, origin_canvas,
            nullptr, camera_id))
        return cache_result(std::move(geometry));
    geometry.origin_view = canvas_to_view(origin_canvas);

    for (int axis = 0; axis < 3; ++axis) {
        QPointF endpoint_canvas;
        if (!fxm::transform3d::project_world_point(
                *projection,
                geometry.origin_world + geometry.axes_world[axis] *
                    static_cast<float>(geometry.world_length),
                playhead_, endpoint_canvas, nullptr, camera_id))
            continue;
        geometry.axis_end_view[axis] = canvas_to_view(endpoint_canvas);
        geometry.axis_visible[axis] =
            QLineF(geometry.origin_view, geometry.axis_end_view[axis]).length() > 8.0;
    }

    const std::pair<int, int> plane_axes[] = {{0,1}, {0,2}, {1,2}};
    for (int plane = 0; plane < 3; ++plane) {
        const auto [a, b] = plane_axes[plane];
        if (!geometry.axis_visible[a] || !geometry.axis_visible[b])
            continue;
        const float lo = static_cast<float>(geometry.world_length * 0.16);
        const float hi = static_cast<float>(geometry.world_length * 0.34);
        const QVector3D points[] = {
            geometry.origin_world + geometry.axes_world[a] * lo + geometry.axes_world[b] * lo,
            geometry.origin_world + geometry.axes_world[a] * hi + geometry.axes_world[b] * lo,
            geometry.origin_world + geometry.axes_world[a] * hi + geometry.axes_world[b] * hi,
            geometry.origin_world + geometry.axes_world[a] * lo + geometry.axes_world[b] * hi,
        };
        bool valid = true;
        for (const QVector3D &point : points) {
            QPointF canvas;
            if (!fxm::transform3d::project_world_point(
                    *projection, point, playhead_, canvas, nullptr, camera_id)) {
                valid = false;
                break;
            }
            geometry.plane_view[plane] << canvas_to_view(canvas);
        }
        if (!valid)
            geometry.plane_view[plane].clear();
    }

    if (gizmo_mode_ == GizmoMode::Rotate) {
        geometry.rotation_rings.resize(3);
        constexpr int samples = 64;
        for (int axis = 0; axis < 3; ++axis) {
            const int a = (axis + 1) % 3;
            const int b = (axis + 2) % 3;
            QPolygonF &ring = geometry.rotation_rings[axis];
            for (int sample = 0; sample <= samples; ++sample) {
                const double angle = (6.28318530717958647692 * sample) / samples;
                const QVector3D point = geometry.origin_world +
                    geometry.axes_world[a] * static_cast<float>(std::cos(angle) * geometry.world_length * 0.78) +
                    geometry.axes_world[b] * static_cast<float>(std::sin(angle) * geometry.world_length * 0.78);
                QPointF canvas;
                if (fxm::transform3d::project_world_point(
                        *projection, point, playhead_, canvas, nullptr, camera_id))
                    ring << canvas_to_view(canvas);
            }
        }
    }

    geometry.valid = true;
    return cache_result(std::move(geometry));
}

CanvasPreview::GizmoAxis CanvasPreview::hit_test_3d_gizmo(
    const QPointF &view_point) const
{
    const GizmoGeometry geometry = build_3d_gizmo_geometry();
    if (!geometry.valid)
        return GizmoAxis::None;

    if (gizmo_mode_ == GizmoMode::Rotate) {
        for (int axis = 2; axis >= 0; --axis) {
            const QPolygonF &ring = geometry.rotation_rings[axis];
            for (int i = 1; i < ring.size(); ++i) {
                if (point_segment_distance(view_point, ring[i - 1], ring[i]) <= 7.0)
                    return axis == 0 ? GizmoAxis::X
                         : axis == 1 ? GizmoAxis::Y : GizmoAxis::Z;
            }
        }
        return GizmoAxis::None;
    }

    const GizmoAxis planes[] = {GizmoAxis::XY, GizmoAxis::XZ, GizmoAxis::YZ};
    for (int plane = 0; plane < 3; ++plane) {
        if (geometry.plane_view[plane].size() >= 3 &&
            geometry.plane_view[plane].containsPoint(view_point, Qt::OddEvenFill))
            return planes[plane];
    }
    for (int axis = 2; axis >= 0; --axis) {
        if (geometry.axis_visible[axis] &&
            point_segment_distance(view_point, geometry.origin_view,
                                   geometry.axis_end_view[axis]) <= 8.0)
            return axis == 0 ? GizmoAxis::X
                 : axis == 1 ? GizmoAxis::Y : GizmoAxis::Z;
    }
    return GizmoAxis::None;
}

void CanvasPreview::update_3d_gizmo_hover(const QPointF &view_point)
{
    GizmoAxis next = GizmoAxis::None;
    if (!gizmo_drag_.active &&
        editor_3d_navigation_mode_ == Editor3DNavigationMode::None)
        next = hit_test_3d_gizmo(view_point);
    if (next == gizmo_hover_axis_)
        return;
    gizmo_hover_axis_ = next;
    gpu_overlay_dirty_ = true;
    update();
}

bool CanvasPreview::begin_3d_gizmo_drag(QMouseEvent *ev)
{
    if (!ev || ev->button() != Qt::LeftButton ||
        ev->modifiers().testFlag(Qt::AltModifier))
        return false;
    const GizmoAxis axis = hit_test_3d_gizmo(ev->position());
    if (axis == GizmoAxis::None)
        return false;
    const GizmoGeometry geometry = build_3d_gizmo_geometry();
    if (!geometry.valid)
        return false;

    gizmo_drag_ = GizmoDragState{};
    gizmo_drag_.active = true;
    gizmo_drag_.axis = axis;
    gizmo_hover_axis_ = axis;
    gizmo_drag_.start_view = ev->position();
    gizmo_drag_.world_length = geometry.world_length;
    for (int i = 0; i < 3; ++i) {
        gizmo_drag_.axes_world[i] = geometry.axes_world[i];
        gizmo_drag_.axis_screen[i] = geometry.axis_end_view[i] - geometry.origin_view;
    }

    for (const auto &layer : selected_layers()) {
        if (!layer || layer->dimension_mode != LayerDimensionMode::ThreeD ||
            !fxm::transform3d::layer_supports_3d(*layer))
            continue;
        const double local_time = std::clamp(
            playhead_ - layer->in_time, 0.0,
            std::max(0.0, layer->out_time - layer->in_time));
        GizmoLayerStart start;
        start.id = layer->id;
        const LayerVector3Value evaluated_position =
            evaluated_layer_position_3d(*layer, local_time);
        start.position = {evaluated_position.x, evaluated_position.y};
        start.position_z = evaluated_position.z;
        start.scale = layer->scale.evaluate(local_time);
        start.scale_z = layer->scale_z.evaluate(local_time);
        start.rotation_x = layer->rotation_x.evaluate(local_time);
        start.rotation_y = layer->rotation_y.evaluate(local_time);
        start.rotation_z = layer->rotation.evaluate(local_time);
        start.orientation_x = layer->orientation_x.evaluate(local_time);
        start.orientation_y = layer->orientation_y.evaluate(local_time);
        start.orientation_z = layer->orientation_z.evaluate(local_time);
        gizmo_drag_.layers.push_back(start);
    }
    if (gizmo_drag_.layers.empty()) {
        gizmo_drag_ = GizmoDragState{};
        return false;
    }

    begin_adaptive_interaction();
    setCursor(gizmo_mode_ == GizmoMode::Rotate ? Qt::CrossCursor
                                                : Qt::ClosedHandCursor);
    ev->accept();
    return true;
}

bool CanvasPreview::update_3d_gizmo_drag(QMouseEvent *ev)
{
    if (!ev || !gizmo_drag_.active || !title_)
        return false;
    /* Mouse/tablet drivers can deliver repeated coordinates. A no-op event
     * must not create another model revision or another queued presentation. */
    if (gizmo_drag_.has_last_view &&
        QLineF(gizmo_drag_.last_view, ev->position()).length() < 0.01) {
        ev->accept();
        return true;
    }
    gizmo_drag_.last_view = ev->position();
    gizmo_drag_.has_last_view = true;
    const QPointF screen_delta = ev->position() - gizmo_drag_.start_view;
    const bool snapping = snap_settings_.enabled &&
        !ev->modifiers().testFlag(Qt::ControlModifier);

    double components[3] = {0.0, 0.0, 0.0};
    const int axis_index = gizmo_axis_index(gizmo_drag_.axis);
    if (axis_index >= 0) {
        const QPointF basis = gizmo_drag_.axis_screen[axis_index];
        const double length2 = QPointF::dotProduct(basis, basis);
        if (length2 > 1.0e-8)
            components[axis_index] = QPointF::dotProduct(screen_delta, basis) /
                length2 * gizmo_drag_.world_length;
    } else {
        const auto [a, b] = gizmo_plane_indices(gizmo_drag_.axis);
        if (a >= 0 && b >= 0) {
            const QPointF va = gizmo_drag_.axis_screen[a];
            const QPointF vb = gizmo_drag_.axis_screen[b];
            const double determinant = va.x() * vb.y() - va.y() * vb.x();
            if (std::abs(determinant) > 1.0e-8) {
                components[a] = (screen_delta.x() * vb.y() - screen_delta.y() * vb.x()) /
                    determinant * gizmo_drag_.world_length;
                components[b] = (va.x() * screen_delta.y() - va.y() * screen_delta.x()) /
                    determinant * gizmo_drag_.world_length;
            }
        }
    }

    if (gizmo_mode_ == GizmoMode::Move && snapping) {
        const double step = ev->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
        for (double &component : components)
            component = std::round(component / step) * step;
    }

    double rotation_delta = 0.0;
    if (gizmo_mode_ == GizmoMode::Rotate) {
        /* Rotation is a linear screen-space scrub after the ring has been hit.
         * The first deliberate mouse movement locks horizontal or vertical
         * control for the rest of the drag. This remains well-defined when a
         * 3D ring is exactly edge-on and its projected circle has collapsed to
         * a line, where both ray-plane and polar-angle drags are singular. */
        constexpr double kScrubActivationPixels = 3.0;
        constexpr double kDegreesPerPixel = 0.5;
        if (gizmo_drag_.rotation_scrub_axis < 0 &&
            (std::abs(screen_delta.x()) >= kScrubActivationPixels ||
             std::abs(screen_delta.y()) >= kScrubActivationPixels)) {
            gizmo_drag_.rotation_scrub_axis =
                std::abs(screen_delta.x()) >= std::abs(screen_delta.y()) ? 0 : 1;
        }
        if (gizmo_drag_.rotation_scrub_axis == 0)
            rotation_delta = screen_delta.x() * kDegreesPerPixel;
        else if (gizmo_drag_.rotation_scrub_axis == 1)
            rotation_delta = -screen_delta.y() * kDegreesPerPixel;
        if (snapping && ev->modifiers().testFlag(Qt::ShiftModifier))
            rotation_delta = std::round(rotation_delta / 15.0) * 15.0;
    }
    if (gizmo_mode_ == GizmoMode::Rotate &&
        gizmo_drag_.rotation_scrub_axis < 0) {
        /* Stay below the activation threshold without modifying the model or
         * creating an Undo entry for a click/jitter that never became a drag. */
        ev->accept();
        return true;
    }

    double scale_factors[3] = {1.0, 1.0, 1.0};
    if (gizmo_mode_ == GizmoMode::Scale) {
        for (int i = 0; i < 3; ++i) {
            if (std::abs(components[i]) > 1.0e-9)
                scale_factors[i] = std::exp(
                    components[i] / std::max(1.0, gizmo_drag_.world_length) * 0.7);
        }
        if (snapping && ev->modifiers().testFlag(Qt::ShiftModifier)) {
            for (double &factor : scale_factors)
                factor = std::max(0.01, std::round(factor * 10.0) / 10.0);
        }
    }

    const QVector3D world_delta =
        gizmo_drag_.axes_world[0] * static_cast<float>(components[0]) +
        gizmo_drag_.axes_world[1] * static_cast<float>(components[1]) +
        gizmo_drag_.axes_world[2] * static_cast<float>(components[2]);

    for (const GizmoLayerStart &start : gizmo_drag_.layers) {
        const auto layer = title_->find_layer(start.id);
        if (!layer)
            continue;
        const double local_time = std::clamp(
            playhead_ - layer->in_time, 0.0,
            std::max(0.0, layer->out_time - layer->in_time));

        if (gizmo_mode_ == GizmoMode::Move) {
            const QVector3D local_delta =
                fxm::transform3d::world_delta_to_parent_space(
                    *title_, *layer, playhead_, world_delta);
            promote_layer_position_to_3d_path(*layer);
            set_animated_value(layer->position_3d, local_time,
                {start.position.x + local_delta.x(),
                 start.position.y + local_delta.y(),
                 start.position_z + local_delta.z()});
            mirror_position_3d_to_legacy(*layer, local_time);
        } else if (gizmo_mode_ == GizmoMode::Rotate && axis_index >= 0) {
            const QMatrix4x4 parent_basis = gizmo_rotation_basis(
                fxm::transform3d::layer_parent_world_matrix(
                    *title_, *layer, playhead_));
            const QMatrix4x4 orientation = gizmo_euler_rotation_matrix(
                start.orientation_x, start.orientation_y,
                start.orientation_z);
            const QMatrix4x4 rotation = gizmo_euler_rotation_matrix(
                start.rotation_x, start.rotation_y, start.rotation_z);
            const QVector3D axis_world = gizmo_axis_world_direction(
                layer->transform_axis_space, axis_index, parent_basis,
                orientation, rotation);
            QMatrix4x4 delta;
            delta.setToIdentity();
            delta.rotate(static_cast<float>(rotation_delta), axis_world);
            bool parent_invertible = false;
            bool orientation_invertible = false;
            const QMatrix4x4 parent_inverse =
                parent_basis.inverted(&parent_invertible);
            const QMatrix4x4 orientation_inverse =
                orientation.inverted(&orientation_invertible);
            if (parent_invertible && orientation_invertible) {
                const LayerVector3Value next = gizmo_decompose_zyx_rotation(
                    orientation_inverse * parent_inverse * delta *
                    parent_basis * orientation * rotation);
                const bool animated = layer->rotation_x.is_animated() ||
                    layer->rotation_y.is_animated() ||
                    layer->rotation.is_animated();
                auto write_rotation = [&](AnimatedProperty &property,
                                          double value) {
                    if (animated)
                        add_or_replace_keyframe(property, local_time, value);
                    else
                        property.static_value = value;
                };
                write_rotation(layer->rotation_x, next.x);
                write_rotation(layer->rotation_y, next.y);
                write_rotation(layer->rotation, next.z);
            }
        } else if (gizmo_mode_ == GizmoMode::Scale) {
            const double next_scale_z = start.scale_z * scale_factors[2];
            set_animated_value(layer->scale, local_time,
                {start.scale.x * scale_factors[0],
                 start.scale.y * scale_factors[1],
                 next_scale_z});
            set_animated_value(layer->scale_z, local_time, next_scale_z);
        }
    }

    gizmo_drag_.changed = true;
    drag_changed_ = true;
    gpu_model_dirty_ = true;
    interactive_transform_pending_ = true;
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    update();
    ev->accept();
    return true;
}

bool CanvasPreview::finish_3d_gizmo_drag(QMouseEvent *ev)
{
    if (!ev || !gizmo_drag_.active || ev->button() != Qt::LeftButton)
        return false;
    const bool changed = gizmo_drag_.changed;
    gizmo_drag_ = GizmoDragState{};
    gizmo_hover_axis_ = hit_test_3d_gizmo(ev->position());
    drag_changed_ = false;
    /* The release boundary is a model-authority boundary. In particular,
     * set_animated_value() may have created or modified keyframes. Never carry
     * transform-only state across layer_geometry_changed()/refresh_preview(). */
    interactive_transform_pending_ = false;
    interactive_settle_pending_ = changed;
    if (changed) {
        gpu_model_dirty_ = true;
        full_gpu_model_refresh_pending_ = true;
        dirty_ = true;
    }
    end_adaptive_interaction();
    unsetCursor();
    invalidate_canvas_overlay_caches();
    if (changed)
        emit layer_geometry_changed();
    update();
    ev->accept();
    return true;
}

void CanvasPreview::draw_editor_3d_view_overlay(QPainter &p)
{
    if (!title_ || editor_3d_view_ == Editor3DView::ActiveCamera)
        return;
    const QRectF canvas_rect = canvas_view_rect();
    if (!canvas_rect.isValid())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QString label = editor_3d_view_name();
    const QFontMetrics metrics(p.font());
    const QRectF label_rect(canvas_rect.left() + 10.0, canvas_rect.top() + 10.0,
                            metrics.horizontalAdvance(label) + 18.0,
                            metrics.height() + 10.0);
    p.setPen(QPen(QColor(10, 10, 10, 190), 1.0));
    p.setBrush(QColor(35, 35, 35, 205));
    p.drawRoundedRect(label_rect, 4.0, 4.0);
    p.setPen(QColor(245, 245, 245));
    p.drawText(label_rect, Qt::AlignCenter, label);
    p.restore();
}

bool CanvasPreview::light_layer_overlay_contains(
    const Layer &layer, const QPointF &view_point) const
{
    if (!title_ || layer.type != LayerType::Light || !layer.visible ||
        !layer.light.enabled)
        return false;
    const auto projection = projection_title();
    if (!projection)
        return false;
    const auto projected_layer = projection->find_layer(layer.id);
    const Layer &evaluated_layer = projected_layer ? *projected_layer : layer;
    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        *projection, evaluated_layer, playhead_);
    const Vec3Value local_position =
        evaluated_layer.light.position.evaluate(playhead_);
    const QVector3D world_position = world.map(QVector3D(
        local_position.x, local_position.y, local_position.z));
    QPointF canvas_position;
    const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
        *projection, evaluated_layer, playhead_);
    if (!fxm::transform3d::project_world_point(
            *projection, world_position, playhead_, canvas_position, nullptr,
            camera_id))
        return false;
    return QLineF(view_point, canvas_to_view(canvas_position)).length() <= 18.0;
}

void CanvasPreview::draw_light_layer_overlays(QPainter &p)
{
    if (!title_)
        return;
    const auto projection = projection_title();
    if (!projection)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto &layer_ptr : projection->layers) {
        if (!layer_ptr || layer_ptr->type != LayerType::Light || !layer_ptr->visible)
            continue;
        const Layer &layer = *layer_ptr;
        const TitleLight &light = layer.light;
        if (!light.enabled)
            continue;
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(*projection, layer, playhead_);
        const Vec3Value lp = light.position.evaluate(playhead_);
        const Vec3Value lt = light.target.evaluate(playhead_);
        const QVector3D world_pos = world.map(QVector3D(lp.x, lp.y, lp.z));
        const QVector3D world_target = world.map(QVector3D(lt.x, lt.y, lt.z));
        QPointF canvas_pos, canvas_target;
        double depth = 0.0;
        const std::string camera_id = fxm::transform3d::resolved_layer_camera_id(
            *projection, layer, playhead_);
        if (!fxm::transform3d::project_world_point(*projection, world_pos, playhead_,
                canvas_pos, &depth, camera_id))
            continue;
        const bool has_target = light.type == TitleLightType::Spot ||
                                light.type == TitleLightType::Parallel;
        bool target_ok = has_target && fxm::transform3d::project_world_point(
            *projection, world_target, playhead_, canvas_target, nullptr, camera_id);
        const QPointF view_pos = canvas_to_view(canvas_pos);
        const QPointF view_target = target_ok ? canvas_to_view(canvas_target) : view_pos;
        const bool selected = std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(),
                                        layer.id) != selected_layer_ids_.end();
        const QColor color = selected ? QColor(255, 190, 55) : QColor(255, 225, 120, 210);
        QPen pen(color, selected ? 2.0 : 1.25);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(QColor(color.red(), color.green(), color.blue(), selected ? 90 : 45));

        const bool finite_source = light.type == TitleLightType::Point ||
                                   light.type == TitleLightType::Spot ||
                                   light.type == TitleLightType::Parallel;
        const double authored_source_size = std::max(
            0.1, light.source_size.evaluate(playhead_));
        if (selected && finite_source && authored_source_size > 0.0001) {
            const double projection_width = std::max(
                1.0, static_cast<double>(projection->width));
            const double projection_height = std::max(
                1.0, static_cast<double>(projection->height));
            const double view_scale = std::max(
                static_cast<double>(canvas_view_rect().width()) /
                    projection_width,
                static_cast<double>(canvas_view_rect().height()) /
                    projection_height);
            const double source_radius = std::clamp(
                authored_source_size * view_scale, 4.0, 240.0);
            QPen source_pen(color, 1.0, Qt::DotLine);
            source_pen.setCosmetic(true);
            p.setPen(source_pen);
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(view_pos, source_radius, source_radius);
            p.setPen(pen);
            p.setBrush(QColor(color.red(), color.green(), color.blue(), 90));
        }

        if (light.type == TitleLightType::Ambient || light.type == TitleLightType::Environment) {
            p.drawEllipse(view_pos, 9.0, 9.0);
            p.drawEllipse(view_pos, 4.0, 4.0);
        } else if (light.type == TitleLightType::Point) {
            p.drawEllipse(view_pos, 7.0, 7.0);
            for (int i = 0; i < 8; ++i) {
                const double a = i * 3.14159265358979323846 / 4.0;
                p.drawLine(view_pos + QPointF(std::cos(a) * 10.0, std::sin(a) * 10.0),
                           view_pos + QPointF(std::cos(a) * 16.0, std::sin(a) * 16.0));
            }
            if (selected && light.falloff != TitleLightFalloff::None) {
                const double projection_width = std::max(1.0, static_cast<double>(projection->width));
                const double projection_height = std::max(1.0, static_cast<double>(projection->height));
                const double view_scale = std::max(
                    static_cast<double>(canvas_view_rect().width()) / projection_width,
                    static_cast<double>(canvas_view_rect().height()) / projection_height);
                const double radius = std::clamp(
                    static_cast<double>(light.falloff_distance.evaluate(playhead_)) * view_scale,
                    20.0, 500.0);
                QPen range_pen(color, 1.0, Qt::DashLine); range_pen.setCosmetic(true);
                p.setPen(range_pen); p.setBrush(Qt::NoBrush); p.drawEllipse(view_pos, radius, radius);
            }
        } else {
            if (target_ok) {
                p.drawLine(view_pos, view_target);
                p.setBrush(color);
                p.drawEllipse(view_target, selected ? 5.0 : 3.5, selected ? 5.0 : 3.5);
            }
            if (light.type == TitleLightType::Spot && target_ok) {
                const QLineF axis(view_pos, view_target);
                const double length = std::max(24.0, axis.length());
                const double half = std::tan(std::clamp(light.cone_angle.evaluate(playhead_), 1.0, 179.0) *
                    0.5 * 3.14159265358979323846 / 180.0) * length;
                const QPointF normal(-axis.dy() / length, axis.dx() / length);
                p.drawLine(view_pos, view_target + normal * half);
                p.drawLine(view_pos, view_target - normal * half);
                p.drawLine(view_target + normal * half, view_target - normal * half);
            } else {
                p.drawRect(QRectF(view_pos.x() - 6.0, view_pos.y() - 6.0, 12.0, 12.0));
            }
        }
        if (selected) {
            p.setPen(color);
            p.drawText(QRectF(view_pos.x() + 18.0, view_pos.y() - 12.0, 220.0, 24.0),
                       QString::fromStdString(layer.name));
        }
    }
    p.restore();
}

bool CanvasPreview::empty_layer_overlay_points(
    const Layer &source_layer, std::array<QPointF, 4> &view_points,
    bool &is_3d) const
{
    view_points.fill(QPointF());
    is_3d = false;
    if (!title_ || source_layer.type != LayerType::Empty ||
        !source_layer.visible || !layer_is_editor_visible(source_layer) ||
        playhead_ < source_layer.in_time ||
        playhead_ > source_layer.out_time)
        return false;

    const auto projection = projection_title();
    if (!projection)
        return false;
    const auto projected_layer = projection->find_layer(source_layer.id);
    const Layer &layer = projected_layer ? *projected_layer : source_layer;
    is_3d = fxm::transform3d::layer_or_ancestor_uses_3d(*projection, layer);
    const double logical_axis = std::max(
        48.0, std::min(projection->width, projection->height) * 0.075);

    std::array<QPointF, 4> projected;
    if (is_3d) {
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
            *projection, layer, playhead_);
        const QVector3D local_points[] = {
            QVector3D(0.0f, 0.0f, 0.0f),
            QVector3D(static_cast<float>(logical_axis), 0.0f, 0.0f),
            QVector3D(0.0f, static_cast<float>(logical_axis), 0.0f),
            QVector3D(0.0f, 0.0f, static_cast<float>(logical_axis)),
        };
        const std::string camera_id =
            fxm::transform3d::resolved_layer_camera_id(
                *projection, layer, playhead_);
        for (int index = 0; index < 4; ++index) {
            QPointF canvas_point;
            if (!fxm::transform3d::project_world_point(
                    *projection, world.map(local_points[index]), playhead_,
                    canvas_point, nullptr, camera_id)) {
                if (index == 0)
                    return false;
                projected[index] = projected[0];
                continue;
            }
            projected[index] = canvas_to_view(canvas_point);
        }
    } else {
        projected[0] = canvas_to_view(layer_to_canvas(layer, QPointF(0.0, 0.0)));
        projected[1] = canvas_to_view(layer_to_canvas(layer, QPointF(logical_axis, 0.0)));
        projected[2] = canvas_to_view(layer_to_canvas(layer, QPointF(0.0, logical_axis)));
        projected[3] = projected[0];
    }

    view_points[0] = projected[0];
    const double axis_pixels = is_3d ? 18.0 : 28.0;
    for (int index = 1; index < 4; ++index) {
        const QLineF axis(projected[0], projected[index]);
        if (axis.length() < 0.5) {
            view_points[index] = projected[0];
            continue;
        }
        view_points[index] = projected[0] +
            (projected[index] - projected[0]) * (axis_pixels / axis.length());
    }
    return true;
}

bool CanvasPreview::empty_layer_overlay_contains(
    const Layer &layer, const QPointF &view_point) const
{
    std::array<QPointF, 4> points;
    bool is_3d = false;
    if (!empty_layer_overlay_points(layer, points, is_3d))
        return false;
    if (QLineF(view_point, points[0]).length() <= 8.0)
        return true;
    const int axis_count = is_3d ? 3 : 2;
    for (int axis = 1; axis <= axis_count; ++axis) {
        if (QLineF(points[axis], points[0]).length() < 0.5)
            continue;
        const QPointF start = is_3d
            ? points[0]
            : points[0] - (points[axis] - points[0]);
        if (point_segment_distance(view_point, start, points[axis]) <= 7.0)
            return true;
    }
    return false;
}

void CanvasPreview::draw_empty_layer_overlays(QPainter &p)
{
    if (!title_)
        return;
    const auto projection = projection_title();
    if (!projection)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const auto &layer_ptr : projection->layers) {
        if (!layer_ptr || layer_ptr->type != LayerType::Empty)
            continue;
        const Layer &layer = *layer_ptr;
        std::array<QPointF, 4> points;
        bool is_3d = false;
        if (!empty_layer_overlay_points(layer, points, is_3d))
            continue;
        const bool selected = std::find(
            selected_layer_ids_.begin(), selected_layer_ids_.end(), layer.id) !=
            selected_layer_ids_.end();
        const bool hovered = hovered_layer_id_ == layer.id;
        const int axis_count = is_3d ? 3 : 2;
        for (int axis = 1; axis <= axis_count; ++axis) {
            if (QLineF(points[axis], points[0]).length() < 0.5)
                continue;
            QColor color;
            if (is_3d)
                color = gizmo_axis_color(axis - 1);
            else
                color = selected ? QColor(255, 190, 55)
                                 : QColor(225, 225, 225, hovered ? 245 : 195);
            if (!selected && is_3d)
                color.setAlpha(hovered ? 235 : 185);
            QPen pen(color, selected ? 2.0 : 1.25,
                     Qt::SolidLine, Qt::RoundCap);
            pen.setCosmetic(true);
            p.setPen(pen);
            const QPointF start = is_3d
                ? points[0]
                : points[0] - (points[axis] - points[0]);
            p.drawLine(start, points[axis]);
        }
        const QColor center_color = selected
            ? QColor(255, 220, 90)
            : QColor(235, 235, 235, hovered ? 250 : 205);
        p.setPen(QPen(QColor(25, 25, 25, 220), 1.0));
        p.setBrush(center_color);
        const double center_size = is_3d ? 5.0 : 7.0;
        p.drawRect(QRectF(points[0].x() - center_size * 0.5,
                          points[0].y() - center_size * 0.5,
                          center_size, center_size));
    }
    p.restore();
}

void CanvasPreview::draw_3d_material_debug_overlay(QPainter &p)
{
    if ((!editor_3d_depth_debug_visible_ && !editor_3d_normals_visible_) ||
        !title_)
        return;
    const std::shared_ptr<Title> projection = projection_title();
    if (!projection)
        return;
    const QRectF canvas_rect = canvas_view_rect();
    if (!canvas_rect.isValid() || canvas_rect.isEmpty())
        return;

    p.save();
    p.setClipRect(canvas_rect);
    p.setRenderHint(QPainter::Antialiasing, true);

    const double normal_length = std::max(
        48.0, std::min(projection->width, projection->height) * 0.08);
    for (const auto &layer_ptr : projection->layers) {
        if (!layer_ptr)
            continue;
        const Layer &layer = *layer_ptr;
        if (!layer.visible || layer_type_is_audio(layer.type) ||
            layer.type == LayerType::Empty ||
            layer_type_is_container(layer.type) ||
            !fxm::transform3d::layer_supports_3d(layer) ||
            !fxm::transform3d::layer_or_ancestor_uses_3d(
                *projection, layer))
            continue;

        const QRectF local_rect = layer_local_rect(layer);
        if (!local_rect.isValid() || local_rect.isEmpty())
            continue;
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
            *projection, layer, playhead_);
        const QPointF local_points[] = {
            local_rect.topLeft(), local_rect.topRight(),
            local_rect.bottomRight(), local_rect.bottomLeft(),
        };
        QPolygonF polygon;
        double average_depth = 0.0;
        bool projectable = true;
        const std::string render_camera_id =
            fxm::transform3d::resolved_layer_camera_id(
                *projection, layer, playhead_);
        for (const QPointF &local : local_points) {
            const QVector3D world_point = world.map(QVector3D(
                static_cast<float>(local.x()),
                static_cast<float>(local.y()), 0.0f));
            QPointF canvas_point;
            double depth = 0.0;
            if (!fxm::transform3d::project_world_point(
                    *projection, world_point, playhead_, canvas_point,
                    &depth, render_camera_id)) {
                projectable = false;
                break;
            }
            polygon << canvas_to_view(canvas_point);
            average_depth += depth;
        }
        if (!projectable || polygon.size() != 4)
            continue;
        average_depth *= 0.25;

        const fxm::transform3d::EvaluatedTransform evaluated =
            fxm::transform3d::evaluate(*projection, layer, playhead_);
        if (editor_3d_depth_debug_visible_) {
            const double normalized = std::clamp(
                (average_depth + 1.0) * 0.5, 0.0, 1.0);
            const int shade = static_cast<int>(
                std::round(235.0 - normalized * 175.0));
            QColor fill(shade, shade, shade, 78);
            QColor outline = evaluated.front_facing
                ? QColor(80, 220, 140, 220)
                : QColor(245, 90, 90, 220);
            QPen pen(outline, 1.5);
            pen.setCosmetic(true);
            if (layer.backface_culling && !layer.double_sided &&
                !evaluated.front_facing)
                pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.setBrush(fill);
            p.drawPolygon(polygon);

            QString state;
            if (layer.depth_test && layer.write_to_depth)
                state = QStringLiteral("Test + Write");
            else if (layer.depth_test)
                state = QStringLiteral("Test / Read-only");
            else if (layer.write_to_depth)
                state = QStringLiteral("Always + Write");
            else
                state = QStringLiteral("Depth Off");
            state += evaluated.front_facing
                ? QStringLiteral(" · Front") : QStringLiteral(" · Back");
            state += QStringLiteral(" · z=%1").arg(average_depth, 0, 'f', 3);
            const QPointF label_origin = polygon.boundingRect().topLeft() +
                QPointF(4.0, 4.0);
            const QFontMetrics metrics(p.font());
            const QRectF label_rect(
                label_origin,
                QSizeF(metrics.horizontalAdvance(state) + 10.0,
                       metrics.height() + 6.0));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(20, 20, 20, 205));
            p.drawRoundedRect(label_rect, 3.0, 3.0);
            p.setPen(QColor(245, 245, 245));
            p.drawText(label_rect, Qt::AlignCenter, state);
        }

        if (editor_3d_normals_visible_ && evaluated.projectable) {
            const QPointF center_local = local_rect.center();
            const QVector3D center_world = world.map(QVector3D(
                static_cast<float>(center_local.x()),
                static_cast<float>(center_local.y()), 0.0f));
            const QVector3D normal_end_world = center_world +
                evaluated.world_normal * static_cast<float>(normal_length);
            QPointF center_canvas;
            QPointF end_canvas;
            if (!fxm::transform3d::project_world_point(
                    *projection, center_world, playhead_, center_canvas,
                    nullptr, render_camera_id) ||
                !fxm::transform3d::project_world_point(
                    *projection, normal_end_world, playhead_, end_canvas,
                    nullptr, render_camera_id))
                continue;
            const QPointF center_view = canvas_to_view(center_canvas);
            const QPointF end_view = canvas_to_view(end_canvas);
            const QPointF vector = end_view - center_view;
            const double length = std::hypot(vector.x(), vector.y());
            if (length < 2.0)
                continue;
            const QPointF direction = vector / length;
            const QPointF side(-direction.y(), direction.x());
            const QColor normal_color = evaluated.front_facing
                ? QColor(90, 230, 155) : QColor(250, 100, 100);
            QPen normal_pen(normal_color, 2.0);
            normal_pen.setCosmetic(true);
            p.setPen(normal_pen);
            p.setBrush(normal_color);
            p.drawLine(center_view, end_view);
            QPolygonF arrow;
            arrow << end_view
                  << end_view - direction * 10.0 + side * 4.0
                  << end_view - direction * 10.0 - side * 4.0;
            p.drawPolygon(arrow);
        }
    }
    p.restore();
}

void CanvasPreview::draw_3d_gizmo(QPainter &p)
{
    const GizmoGeometry geometry = build_3d_gizmo_geometry();
    if (!geometry.valid || editor_3d_navigation_mode_ != Editor3DNavigationMode::None)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor active_color(255, 214, 64);

    if (gizmo_mode_ != GizmoMode::Rotate) {
        const GizmoAxis planes[] = {GizmoAxis::XY, GizmoAxis::XZ, GizmoAxis::YZ};
        const std::pair<int,int> indices[] = {{0,1}, {0,2}, {1,2}};
        for (int plane = 0; plane < 3; ++plane) {
            if (geometry.plane_view[plane].size() < 3)
                continue;
            QColor fill = gizmo_axis_color(indices[plane].first);
            fill = QColor((fill.red() + gizmo_axis_color(indices[plane].second).red()) / 2,
                          (fill.green() + gizmo_axis_color(indices[plane].second).green()) / 2,
                          (fill.blue() + gizmo_axis_color(indices[plane].second).blue()) / 2,
                          70);
            const bool active = gizmo_drag_.active && gizmo_drag_.axis == planes[plane];
            const bool hovered = !gizmo_drag_.active && gizmo_hover_axis_ == planes[plane];
            const QColor plane_outline = active ? active_color
                : hovered ? fill.lighter(190) : fill.lighter(145);
            const QColor plane_fill = active ? QColor(255, 214, 64, 100)
                : hovered ? QColor(fill.red(), fill.green(), fill.blue(), 135) : fill;
            p.setPen(QPen(plane_outline, hovered || active ? 2.0 : 1.0));
            p.setBrush(plane_fill);
            p.drawPolygon(geometry.plane_view[plane]);
        }

        for (int axis = 0; axis < 3; ++axis) {
            if (!geometry.axis_visible[axis])
                continue;
            const GizmoAxis axis_id = axis == 0 ? GizmoAxis::X
                : axis == 1 ? GizmoAxis::Y : GizmoAxis::Z;
            const bool active = gizmo_drag_.active && gizmo_drag_.axis == axis_id;
            const bool hovered = !gizmo_drag_.active && gizmo_hover_axis_ == axis_id;
            const QColor base_color = gizmo_axis_color(axis);
            const QColor color = active ? active_color
                : hovered ? base_color.lighter(165) : base_color;
            QPen pen(color, active ? 3.5 : hovered ? 3.4 : 2.5);
            pen.setCosmetic(true);
            p.setPen(pen);
            p.setBrush(color);
            p.drawLine(geometry.origin_view, geometry.axis_end_view[axis]);
            if (gizmo_mode_ == GizmoMode::Move) {
                const QPointF line = geometry.axis_end_view[axis] - geometry.origin_view;
                const double length = std::max(1.0, std::hypot(line.x(), line.y()));
                const QPointF direction = line / length;
                const QPointF normal(-direction.y(), direction.x());
                QPolygonF arrow;
                arrow << geometry.axis_end_view[axis]
                      << geometry.axis_end_view[axis] - direction * 11.0 + normal * 5.0
                      << geometry.axis_end_view[axis] - direction * 11.0 - normal * 5.0;
                p.drawPolygon(arrow);
            } else {
                p.drawRect(QRectF(geometry.axis_end_view[axis] - QPointF(4.0, 4.0),
                                  QSizeF(8.0, 8.0)));
            }
        }
    } else {
        for (int axis = 0; axis < 3 && axis < static_cast<int>(geometry.rotation_rings.size()); ++axis) {
            const GizmoAxis axis_id = axis == 0 ? GizmoAxis::X
                : axis == 1 ? GizmoAxis::Y : GizmoAxis::Z;
            const bool active = gizmo_drag_.active && gizmo_drag_.axis == axis_id;
            const bool hovered = !gizmo_drag_.active && gizmo_hover_axis_ == axis_id;
            const QColor base_color = gizmo_axis_color(axis);
            QPen pen(active ? active_color : hovered ? base_color.lighter(165) : base_color,
                     active ? 3.5 : hovered ? 3.2 : 2.0);
            pen.setCosmetic(true);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(geometry.rotation_rings[axis]);
        }
    }

    p.setPen(QPen(QColor(20, 20, 20), 1.0));
    p.setBrush(QColor(250, 250, 250));
    p.drawEllipse(geometry.origin_view, 4.0, 4.0);
    p.restore();
}
