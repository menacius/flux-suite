    double lt = std::clamp(playhead_ - layer->in_time, 0.0,
                           std::max(0.0, layer->out_time - layer->in_time));
    const QPointF local_position =
        editor_evaluated_local_position_xy(*layer, lt);
    drag_start_x_ = local_position.x();
    drag_start_y_ = local_position.y();
    if (layer_type_is_container(layer->type)) {
        const QRectF group_rect = layer_local_rect(*layer);
        drag_start_w_ = static_cast<float>(std::max(1.0, group_rect.width()));
        drag_start_h_ = static_cast<float>(std::max(1.0, group_rect.height()));
        drag_start_origin_x_ = static_cast<float>(-group_rect.left() / drag_start_w_);
        drag_start_origin_y_ = static_cast<float>(-group_rect.top() / drag_start_h_);
    } else {
        drag_start_w_ = (float)eval_box_width(*layer, lt);
        drag_start_h_ = (float)eval_box_height(*layer, lt);
        /* Capture the origin evaluated at the drag-start playhead. Using the
         * legacy static fields here desynchronizes resize geometry whenever
         * origin is animated and makes the handle jump away from the pointer. */
        drag_start_origin_x_ = static_cast<float>(eval_origin_x(*layer, lt));
        drag_start_origin_y_ = static_cast<float>(eval_origin_y(*layer, lt));
    }
    if (drag_mode_ == DragMode::GradientStart || drag_mode_ == DragMode::GradientEnd ||
        drag_mode_ == DragMode::GradientCenter || drag_mode_ == DragMode::GradientRadius ||
        drag_mode_ == DragMode::GradientFocal) {
        GradientHandleTarget gradient_target;
        if (hit_test_gradient_handles(*layer, drag_start_view_, &gradient_target) != DragMode::None)
            begin_gradient_drag(*layer, gradient_target);
    } else {
        gradient_drag_ = GradientDragState{};
    }
    begin_corner_radius_drag(*layer);
    drag_rotation_pivot_canvas_ = layers.size() > 1
        ? drag_start_selection_bounds_.center()
        : layer_to_canvas(*layer, QPointF(0, 0));
    QPointF pivot_view = canvas_to_view(drag_rotation_pivot_canvas_);
    drag_start_rotation_angle_ = radians_to_degrees(std::atan2(drag_start_view_.y() - pivot_view.y(),
                                                               drag_start_view_.x() - pivot_view.x()));
    drag_current_rotation_delta_ = 0.0;
    auto set_cursor_for_mode = [this](DragMode mode) {
        if (mode == DragMode::Move) setCursor(Qt::ClosedHandCursor);
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
        else setCursor(Qt::SizeFDiagCursor);
    };
    set_cursor_for_mode(drag_mode_);
    ev->accept();
}

void CanvasPreview::mouseMoveEvent(QMouseEvent *ev)
{
    if (inline_text_mouse_selecting_) {
        if ((ev->buttons() & Qt::LeftButton) &&
            !inline_text_suspended_for_gradient_) {
            set_inline_text_cursor_from_view(ev->position(), true);
            ev->accept();
            return;
        }
        inline_text_mouse_selecting_ = false;
    }

    if (update_editor_3d_navigation(ev) || update_3d_gizmo_drag(ev))
        return;

    if (ev->buttons() == Qt::NoButton)
        update_3d_gizmo_hover(ev->position());
    else if (gizmo_hover_axis_ != GizmoAxis::None) {
        gizmo_hover_axis_ = GizmoAxis::None;
        gpu_overlay_dirty_ = true;
        update();
    }
    update_hover_layer(ev->pos());

    if (drag_mode_ == DragMode::ExtensionPoint && active_extension_handle_ >= 0 &&
        active_extension_handle_ < extension_canvas_handles_.size() &&
        (ev->buttons() & Qt::LeftButton) && title_) {
        const QPointF canvas_point = view_to_canvas(ev->pos());
        QJsonObject handle = extension_canvas_handles_.at(active_extension_handle_).toObject();
        QPointF normalized;
        if (handle.value(QStringLiteral("space")).toString() == QStringLiteral("layer")) {
            const auto layer = selected_layer();
            if (!layer) return;
            const QRectF rect = layer_local_rect(*layer);
            const QPointF local = canvas_to_layer(*layer, canvas_point);
            normalized = QPointF(
                rect.width() != 0.0 ? (local.x() - rect.left()) / rect.width() : 0.5,
                rect.height() != 0.0 ? (local.y() - rect.top()) / rect.height() : 0.5);
        } else {
            normalized = QPointF(
                canvas_point.x() / std::max(1.0, static_cast<double>(title_->width)),
                canvas_point.y() / std::max(1.0, static_cast<double>(title_->height)));
        }
        normalized.setX(std::clamp(normalized.x(),
            handle.value(QStringLiteral("minX")).toDouble(0.0),
            handle.value(QStringLiteral("maxX")).toDouble(1.0)));
        normalized.setY(std::clamp(normalized.y(),
            handle.value(QStringLiteral("minY")).toDouble(0.0),
            handle.value(QStringLiteral("maxY")).toDouble(1.0)));
        handle.insert(QStringLiteral("value"), QJsonArray{normalized.x(), normalized.y()});
        extension_canvas_handles_.replace(active_extension_handle_, handle);
        emit extension_canvas_handle_moved(
            handle.value(QStringLiteral("path")).toString(), normalized, false);
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }

    if (panning_ && (ev->buttons() & Qt::MiddleButton)) {
        pan_offset_ = pan_start_offset_ + (QPointF(ev->pos()) - pan_start_view_);
        fit_zoom_active_ = false;
        invalidate_canvas_overlay_caches();
        position_text_editor();
        update();
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Pen) {
        if ((ev->buttons() & Qt::LeftButton) && pen_drag_point_index_ >= 0) {
            if (pen_space_reposition_ && (size_t)pen_drag_point_index_ < pen_path_points_.size()) {
                BezierPathPoint &point = pen_path_points_[(size_t)pen_drag_point_index_];
                const bool allow_snap = !ev->modifiers().testFlag(Qt::ControlModifier);
                QPointF target = snap_canvas_point(view_to_canvas(ev->pos()), true, true, allow_snap);
                const QPointF current(point.x, point.y);
                const QPointF delta = target - current;
                point.x += delta.x(); point.y += delta.y();
                point.in_x += delta.x(); point.in_y += delta.y();
                point.out_x += delta.x(); point.out_y += delta.y();
                pen_press_canvas_ += delta;
                pen_last_drag_canvas_ = target;
                shape_draw_current_canvas_ = target;
                update();
            } else {
                update_pen_drag(ev->pos(), ev->modifiers());
                pen_last_drag_canvas_ = view_to_canvas(ev->pos());
            }
            ev->accept();
            return;
        }
        if (!(ev->buttons() & Qt::LeftButton)) {
            update_draw_tool_snap_cursor(ev->pos(), ev->modifiers());
            shape_draw_current_canvas_ = snap_cursor_visible_
                ? snap_cursor_canvas_
                : view_to_canvas(ev->pos());
            if (pen_path_active_ && !pen_path_points_.empty() &&
                ev->modifiers().testFlag(Qt::ShiftModifier)) {
                const QPointF previous(pen_path_points_.back().x, pen_path_points_.back().y);
                shape_draw_current_canvas_ = constrain_path_direction(previous, shape_draw_current_canvas_,
                                                                       ev->modifiers());
            }
            bool close_cursor = false;
            if (pen_path_active_ && pen_path_points_.size() >= 2) {
                const QPointF first_view = canvas_to_view(QPointF(pen_path_points_.front().x,
                                                                  pen_path_points_.front().y));
                close_cursor = QLineF(first_view, ev->pos()).length() <= 8.0;
            }
            setCursor(close_cursor ? Qt::PointingHandCursor : Qt::CrossCursor);
            update();
            return;
        }
    }

    if ((drag_mode_ == DragMode::PositionInTangent ||
         drag_mode_ == DragMode::PositionOutTangent) &&
        (ev->buttons() & Qt::LeftButton)) {
        apply_position_tangent_drag(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (drag_mode_ == DragMode::PositionVertex &&
        (ev->buttons() & Qt::LeftButton)) {
        apply_position_vertex_drag(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (drag_mode_ == DragMode::PathMarquee && (ev->buttons() & Qt::LeftButton)) {
        update_path_point_marquee(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if ((drag_mode_ == DragMode::PathAnchor || drag_mode_ == DragMode::PathInHandle ||
         drag_mode_ == DragMode::PathOutHandle) && (ev->buttons() & Qt::LeftButton)) {
        apply_direct_path_drag(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (drawing_shape_ && (ev->buttons() & Qt::LeftButton)) {
        update_shape_drawing(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if ((drag_mode_ == DragMode::GuideX || drag_mode_ == DragMode::GuideY) && (ev->buttons() & Qt::LeftButton)) {
        const QPointF canvas_pt = view_to_canvas(ev->pos());
        dragging_guide_value_ = drag_mode_ == DragMode::GuideX ? canvas_pt.x() : canvas_pt.y();
        if (title_) {
            if (drag_mode_ == DragMode::GuideX)
                dragging_guide_value_ = std::clamp(dragging_guide_value_, -10000.0, title_->width + 10000.0);
            else
                dragging_guide_value_ = std::clamp(dragging_guide_value_, -10000.0, title_->height + 10000.0);
        }
        dragging_guide_value_ = snap_guide_value_to_objects(drag_mode_ == DragMode::GuideX, dragging_guide_value_);
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }

    if (drag_mode_ != DragMode::None && (ev->buttons() & Qt::LeftButton)) {
        apply_drag(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::DirectSelection) {
        clear_draw_tool_snap_cursor();
        if (auto layer = selected_layer()) {
            if (hit_test_corner_radius_handle_index(*layer, ev->pos()) >= 0) {
                setCursor(Qt::SizeAllCursor);
            } else {
                const PathHit hit = hit_test_direct_path(*layer, ev->pos());
                if (hit.mode == DragMode::PathAnchor)
                    setCursor(Qt::SizeAllCursor);
                else if (hit.mode == DragMode::PathInHandle || hit.mode == DragMode::PathOutHandle)
                    setCursor(Qt::CrossCursor);
                else
                    setCursor(Qt::ArrowCursor);
            }
        } else {
            setCursor(Qt::ArrowCursor);
        }
        return;
    }

    if (active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Text ||
        active_tool_ == CanvasTool::Image) {
        if (active_draw_tool_can_manipulate_selected()) {
            DragMode mode = hit_test_selected(ev->pos());

            // Shape/Image tools reserve body drags for drawing a new box; only
            // explicit transform handles switch away from the draw cursor.
            if ((active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Image) &&
                mode == DragMode::Move)
                mode = DragMode::None;

            if (mode != DragMode::None) {
                clear_draw_tool_snap_cursor();
                set_cursor_for_drag_mode(mode, false);
                return;
            }
        }
        update_draw_tool_snap_cursor(ev->pos(), ev->modifiers());
        setCursor(active_tool_ == CanvasTool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
        return;
    }
    if (active_tool_ == CanvasTool::ColorPicker) {
        clear_draw_tool_snap_cursor();
        update_color_picker_tooltip(ev->pos());
        if (color_picker_tooltip_visible_)
            emit color_picker_previewed(color_picker_tooltip_color_);
        setCursor(Qt::CrossCursor);
        return;
    }
    if (active_tool_ == CanvasTool::Gradient || gradient_handles_visible()) {
        clear_draw_tool_snap_cursor();
        if (auto layer = selected_layer()) {
            DragMode mode = hit_test_gradient_handles(*layer, ev->pos());
            if (mode == DragMode::GradientCenter || mode == DragMode::GradientFocal) setCursor(Qt::CrossCursor);
            else if (mode == DragMode::GradientStart || mode == DragMode::GradientEnd ||
                     mode == DragMode::GradientRadius) setCursor(Qt::SizeAllCursor);
            else setCursor(active_tool_ == CanvasTool::Gradient ? Qt::CrossCursor : Qt::ArrowCursor);
        } else {
            setCursor(active_tool_ == CanvasTool::Gradient ? Qt::CrossCursor : Qt::ArrowCursor);
        }
        return;
    }

    clear_draw_tool_snap_cursor();

    if (active_tool_ == CanvasTool::Selection &&
        update_position_motion_path_hover(ev->pos()))
        return;

    bool hover_x_axis = true;
    if (guide_hit_test(ev->pos(), hover_x_axis) >= 0) {
        setCursor(hover_x_axis ? Qt::SplitHCursor : Qt::SplitVCursor);
        return;
    }
    bool hover_vertical_guide = true;
    if (ruler_hit_test(ev->pos(), hover_vertical_guide)) {
        setCursor(hover_vertical_guide ? Qt::SplitHCursor : Qt::SplitVCursor);
        return;
    }

    DragMode mode = hit_test_selected(ev->pos());
    if (mode == DragMode::Move) setCursor(Qt::ArrowCursor);
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



static bool external_canvas_file_maybe_importable(const QString &path)
{
    /* The editor's unified dispatcher asks Qt and FFmpeg what they can decode.
     * Accept real local files here instead of duplicating a format allow-list;
     * this keeps drag/drop and paste in sync with newly installed decoders. */
    return !path.trimmed().isEmpty() && QFileInfo(path).isFile();
}

bool CanvasPreview::mime_has_external_canvas_content(const QMimeData *mime) const
{
    if (!mime)
        return false;
    if (mime->hasFormat(QStringLiteral("application/x-flux-motion-asset")))
        return true;
    if (mime->hasImage())
        return true;
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            if (external_canvas_file_maybe_importable(url.toLocalFile()))
                return true;
        }
    }
    return mime->hasText() && !mime->text().trimmed().isEmpty();
}

bool CanvasPreview::handle_external_canvas_mime(const QMimeData *mime, const QPointF &canvas_pt)
{
    if (!mime || !title_)
        return false;

    if (mime->hasFormat(QStringLiteral("application/x-flux-motion-asset"))) {
        const QString asset_id = QString::fromUtf8(
            mime->data(QStringLiteral("application/x-flux-motion-asset"))).trimmed();
        if (!asset_id.isEmpty()) {
            emit asset_layer_requested(asset_id, canvas_pt);
            return true;
        }
    }

    bool imported_files = false;
    if (mime->hasUrls()) {
        for (const QUrl &url : mime->urls()) {
            if (!url.isLocalFile())
                continue;
            const QString path = url.toLocalFile();
            if (!external_canvas_file_maybe_importable(path))
                continue;
            emit external_file_import_requested(path, canvas_pt);
            imported_files = true;
        }
        if (imported_files)
            return true;
    }

    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            QString base_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
            if (base_dir.isEmpty())
                base_dir = QDir::tempPath();
            QDir dir(base_dir + QStringLiteral("/obs-fxm-pasted-assets"));
            if (!dir.exists())
                dir.mkpath(QStringLiteral("."));
            const QString path = dir.filePath(QStringLiteral("pasted-image-%1.png")
                .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-hhmmss-zzz"))));
            if (image.save(path, "PNG")) {
                emit external_image_layer_requested(path, canvas_pt);
                return true;
            }
        }
    }

    if (mime->hasText()) {
        const QString text = mime->text().trimmed();
        if (!text.isEmpty()) {
            emit external_text_layer_requested(text, canvas_pt);
            return true;
        }
    }

    return false;
}

void CanvasPreview::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev && fxm::effects::mime_has_effect_preset(ev->mimeData())) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
        return;
    }
    if (mime_has_external_canvas_content(ev ? ev->mimeData() : nullptr)) {
        ev->acceptProposedAction();
        return;
    }
    QWidget::dragEnterEvent(ev);
}

void CanvasPreview::dragMoveEvent(QDragMoveEvent *ev)
{
    if (ev && fxm::effects::mime_has_effect_preset(ev->mimeData())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF view_pt = ev->position();
#else
        const QPointF view_pt = ev->pos();
#endif
        const auto layer = layer_at_view_pos(view_pt);
        if (layer && !layer->locked) {
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            update_hover_layer(view_pt);
            return;
        }
        ev->ignore();
        return;
    }
    if (mime_has_external_canvas_content(ev ? ev->mimeData() : nullptr)) {
        ev->acceptProposedAction();
        return;
    }
    QWidget::dragMoveEvent(ev);
}

void CanvasPreview::dropEvent(QDropEvent *ev)
{
    if (!ev) {
        QWidget::dropEvent(ev);
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF view_pt = ev->position();
#else
    const QPointF view_pt = ev->pos();
#endif
    if (fxm::effects::mime_has_effect_preset(ev->mimeData())) {
        const auto layer = layer_at_view_pos(view_pt);
        const QString file_path = fxm::effects::effect_preset_path_from_mime(ev->mimeData());
        if (layer && !layer->locked && !file_path.isEmpty()) {
            emit effect_preset_dropped(file_path, layer->id);
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            setFocus(Qt::MouseFocusReason);
            return;
        }
        ev->ignore();
        return;
    }
    if (handle_external_canvas_mime(ev->mimeData(), view_to_canvas(view_pt))) {
        ev->acceptProposedAction();
        setFocus(Qt::MouseFocusReason);
        return;
    }
    QWidget::dropEvent(ev);
}

void CanvasPreview::leaveEvent(QEvent *ev)
{
    bool needs_update = false;
    if (color_picker_tooltip_visible_) {
        color_picker_tooltip_visible_ = false;
        gpu_overlay_dirty_ = true;
        needs_update = true;
    }
    if (mouse_inside_canvas_ || !hovered_layer_id_.empty()) {
        mouse_inside_canvas_ = false;
        hovered_layer_id_.clear();
        invalidate_hover_overlay_cache();
        gpu_overlay_dirty_ = true;
        needs_update = true;
    }
    if (motion_path_hover_type_ != MotionPathHoverType::None) {
        motion_path_hover_type_ = MotionPathHoverType::None;
        motion_path_hover_keyframe_index_ = -1;
        invalidate_selection_overlay_cache();
        gpu_overlay_dirty_ = true;
        needs_update = true;
    }
    if (gizmo_hover_axis_ != GizmoAxis::None) {
        gizmo_hover_axis_ = GizmoAxis::None;
        gpu_overlay_dirty_ = true;
        needs_update = true;
    }
    if (snap_cursor_visible_ || final_snap_cursor_visible_) {
        snap_cursor_visible_ = false;
        final_snap_cursor_visible_ = false;
        needs_update = true;
    }
    if (needs_update)
        update();
    QWidget::leaveEvent(ev);
}

void CanvasPreview::keyPressEvent(QKeyEvent *ev)
{
    if (ev && inline_text_layer_id_.empty() &&
        !ev->modifiers().testFlag(Qt::ControlModifier) &&
        !ev->modifiers().testFlag(Qt::AltModifier)) {
        if (ev->key() == Qt::Key_W) {
            set_3d_gizmo_mode(GizmoMode::Move);
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_E) {
            set_3d_gizmo_mode(GizmoMode::Rotate);
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_R) {
            set_3d_gizmo_mode(GizmoMode::Scale);
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_F) {
            if (ev->modifiers().testFlag(Qt::ShiftModifier))
                frame_3d_all();
            else
                frame_3d_selection();
            ev->accept();
            return;
        }
    }

    if (ev && inline_text_layer_id_.empty() && ev->matches(QKeySequence::Paste)) {
        const QPointF paste_pt = view_to_canvas(QPointF(width() * 0.5, height() * 0.5));
        if (handle_external_canvas_mime(QApplication::clipboard() ? QApplication::clipboard()->mimeData() : nullptr, paste_pt)) {
            ev->accept();
            return;
        }
    }

    if (!inline_text_layer_id_.empty() && ev->key() == Qt::Key_Escape) {
        commit_text_edit(true);
        ev->accept();
        return;
    }

    if (corner_radius_drag_.active && drag_mode_ == DragMode::CornerRadius &&
        (ev->key() == Qt::Key_Up || ev->key() == Qt::Key_Down)) {
        if (title_) {
            auto layer = title_->find_layer(corner_radius_drag_.primary_layer_id);
            if (layer) {
                const bool forward = ev->key() == Qt::Key_Up;
                const double current = layer->corner_bevel_roundness;
                const double next = forward
                    ? (current > 50.0 ? -100.0 : (current < -50.0 ? 0.0 : 100.0))
                    : (current > 50.0 ? 0.0 : (current < -50.0 ? 100.0 : -100.0));
                for (auto &selected : selected_layers())
                    if (selected && layer_supports_corner_radius_handles(*selected))
                        selected->corner_bevel_roundness = (float)next;
                dirty_ = true;
                drag_changed_ = true;
                invalidate_canvas_overlay_caches();
                notify_corner_context_changed();
                update();
            }
        }
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Pen && pen_path_active_) {
        if (ev->key() == Qt::Key_Z &&
            ev->modifiers().testFlag(Qt::ControlModifier)) {
            if (ev->modifiers().testFlag(Qt::ShiftModifier))
                redo_pen_path_point();
            else
                undo_pen_path_point();
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_Space && pen_drag_point_index_ >= 0) {
            pen_space_reposition_ = true;
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_Return || ev->key() == Qt::Key_Enter || ev->key() == Qt::Key_Escape) {
            finish_pen_path(false);
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_Backspace || ev->key() == Qt::Key_Delete) {
            undo_pen_path_point();
            ev->accept();
            return;
        }
    }

    if (active_tool_ == CanvasTool::DirectSelection) {
        if (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) {
            delete_selected_path_points();
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_Escape) {
            clear_path_point_selection();
            update();
            ev->accept();
            return;
        }
    }

    double dx = 0.0;
    double dy = 0.0;
    const double amount = ev->modifiers().testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;

    switch (ev->key()) {
    case Qt::Key_Left:
        dx = -amount;
        break;
    case Qt::Key_Right:
        dx = amount;
        break;
    case Qt::Key_Up:
        dy = -amount;
        break;
    case Qt::Key_Down:
        dy = amount;
        break;
    default:
        QWidget::keyPressEvent(ev);
        return;
    }

    if (active_tool_ == CanvasTool::DirectSelection && nudge_selected_path_points(dx, dy))
        ev->accept();
    else if (nudge_selected_layers(dx, dy))
        ev->accept();
    else
        QWidget::keyPressEvent(ev);
}

void CanvasPreview::keyReleaseEvent(QKeyEvent *ev)
{
    if (ev && ev->key() == Qt::Key_Space && pen_space_reposition_) {
        pen_space_reposition_ = false;
        ev->accept();
        return;
    }
    QWidget::keyReleaseEvent(ev);
}

void CanvasPreview::mouseReleaseEvent(QMouseEvent *ev)
{
    if (inline_text_mouse_selecting_ && ev->button() == Qt::LeftButton) {
        set_inline_text_cursor_from_view(ev->position(), true);
        inline_text_mouse_selecting_ = false;
        ev->accept();
        return;
    }

    if (finish_editor_3d_navigation(ev) || finish_3d_gizmo_drag(ev))
        return;

    if (ev->button() == Qt::LeftButton && drag_mode_ == DragMode::ExtensionPoint) {
        if (active_extension_handle_ >= 0 && active_extension_handle_ < extension_canvas_handles_.size()) {
            const QJsonObject handle = extension_canvas_handles_.at(active_extension_handle_).toObject();
            const QJsonArray value = handle.value(QStringLiteral("value")).toArray();
            if (value.size() >= 2)
                emit extension_canvas_handle_moved(handle.value(QStringLiteral("path")).toString(),
                                                   QPointF(value.at(0).toDouble(), value.at(1).toDouble()), true);
        }
        active_extension_handle_ = -1;
        drag_mode_ = DragMode::None;
        unsetCursor();
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }
    if (ev->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        unsetCursor();
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton && active_tool_ == CanvasTool::Pen) {
        if (pen_close_pending_) {
            const QPointF release_canvas = view_to_canvas(ev->pos());
            if (pen_drag_point_index_ == 0 && !pen_space_reposition_ &&
                QLineF(pen_press_canvas_, release_canvas).length() >= 1.0 / std::max(0.1, view_scale()))
                update_pen_drag(ev->pos(), ev->modifiers());
            finish_pen_path(true);
        } else if (pen_path_active_) {
            if (pen_drag_point_index_ >= 0 && !pen_space_reposition_)
                update_pen_drag(ev->pos(), ev->modifiers());
            pen_drag_point_index_ = -1;
            pen_alt_break_active_ = false;
            pen_ctrl_unequal_active_ = false;
            pen_space_reposition_ = false;
            clear_snap_feedback();
            snap_cursor_visible_ = false;
            final_snap_cursor_visible_ = false;
            invalidate_canvas_overlay_caches();
            update();
        }
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton &&
        drag_mode_ == DragMode::PositionVertex) {
        if (spatial_vertex_drag_.moved ||
            QLineF(drag_start_view_, ev->pos()).length() > 0.5)
            apply_position_vertex_drag(ev->pos(), ev->modifiers());
        const bool changed = spatial_vertex_drag_.moved;
        if (changed && title_) {
            if (auto layer = title_->find_layer(spatial_vertex_drag_.layer_id))
                layer->position.recalculate_rove_times();
        }
        spatial_vertex_drag_ = SpatialVertexDragState{};
        drag_mode_ = DragMode::None;
        drag_changed_ = false;
        end_adaptive_interaction();
        clear_snap_feedback();
        unsetCursor();
        invalidate_canvas_overlay_caches();
        if (changed)
            emit layer_geometry_changed();
        update();
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton &&
        (drag_mode_ == DragMode::PositionInTangent ||
         drag_mode_ == DragMode::PositionOutTangent)) {
        /* A click on a handle must not silently convert Auto/Linear state or
         * create an undo entry. Mouse moves already apply live changes. */
        if (spatial_tangent_drag_.moved ||
            QLineF(drag_start_view_, ev->pos()).length() > 0.5)
            apply_position_tangent_drag(ev->pos(), ev->modifiers());
        const bool changed = spatial_tangent_drag_.moved;
        if (changed && title_) {
            if (auto layer = title_->find_layer(spatial_tangent_drag_.layer_id))
                layer->position.recalculate_rove_times();
        }
        spatial_tangent_drag_ = SpatialTangentDragState{};
        drag_mode_ = DragMode::None;
        drag_changed_ = false;
        end_adaptive_interaction();
        unsetCursor();
        invalidate_canvas_overlay_caches();
        if (changed)
            emit layer_geometry_changed();
        update();
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton && drag_mode_ == DragMode::PathMarquee && path_marquee_active_) {
        finish_path_point_marquee(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton &&
        (drag_mode_ == DragMode::PathAnchor || drag_mode_ == DragMode::PathInHandle ||
         drag_mode_ == DragMode::PathOutHandle)) {
        apply_direct_path_drag(ev->pos(), ev->modifiers());
        if (auto layer = selected_layer()) {
            if (layer->shape_type == ShapeType::Path && layer->path_points.size() >= 2) {
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
            }
        }
        end_adaptive_interaction();
        drag_mode_ = DragMode::None;
        path_drag_point_index_ = -1;
        path_alt_break_active_ = false;
        path_drag_start_points_.clear();
        clear_snap_feedback();
        dirty_ = true;
        invalidate_canvas_overlay_caches();
        emit layer_geometry_changed();
        update();
        ev->accept();
        return;
    }

    if (ev->button() == Qt::LeftButton && drawing_shape_) {
        const QRect old_update_rect = last_toolbar_preview_update_rect_;
        const QPointF release_canvas = view_to_canvas(ev->pos());
        const bool dragged_far_enough = QLineF(shape_draw_start_canvas_, release_canvas).length() >= 2.0;
        if (drawing_shape_changed_ || dragged_far_enough)
            update_shape_drawing(ev->pos(), ev->modifiers());

        const QRect repaint_rect = old_update_rect.united(last_toolbar_preview_update_rect_);
        const QRectF final_rect = shape_draw_current_rect_.isValid()
                                    ? shape_draw_current_rect_.normalized()
                                    : toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
        const bool has_drawn_size = drawing_shape_changed_ || dragged_far_enough;
        const bool was_text_tool = active_tool_ == CanvasTool::Text;
        const bool was_image_tool = active_tool_ == CanvasTool::Image;
        const ShapeType final_shape_type = active_shape_type_;
        const LayerType final_text_type = active_text_layer_type_;
        const QPointF start_canvas = shape_draw_start_canvas_;

        drawing_shape_ = false;
        drawing_shape_changed_ = false;
        shape_draw_current_rect_ = QRectF();
        snap_cursor_visible_ = false;
        final_snap_cursor_visible_ = false;
        last_toolbar_preview_update_rect_ = QRect();
        clear_snap_feedback();
        gpu_overlay_dirty_ = true;
        update(repaint_rect.isEmpty() ? rect() : repaint_rect);

        if (was_text_tool)
            emit text_drawing_started(final_text_type, start_canvas);
        else if (was_image_tool)
            emit image_drawing_started(start_canvas);
        else
            emit shape_drawing_started(final_shape_type, start_canvas);
        if (has_drawn_size)
            emit shape_drawing_changed(final_rect);
        emit shape_drawing_finished(true);

        setCursor(active_tool_ == CanvasTool::Text ? Qt::IBeamCursor :
                  (active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Image ||
                   active_tool_ == CanvasTool::ColorPicker || active_tool_ == CanvasTool::Gradient
                       ? Qt::CrossCursor : Qt::ArrowCursor));
        ev->accept();
        return;
    }

    if (ev->button() != Qt::LeftButton || drag_mode_ == DragMode::None) return;

    if (gradient_tool_dragging_) {
        apply_gradient_drag(ev->pos(), ev->modifiers());
        end_adaptive_interaction();
        gradient_tool_dragging_ = false;
        gradient_drag_ = GradientDragState{};
        drag_mode_ = DragMode::None;
        clear_snap_feedback();
        emit layer_geometry_changed();
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }

    if (drag_mode_ == DragMode::CornerRadius && corner_radius_drag_.active) {
        const bool moved = corner_radius_drag_.moved;
        const bool value_changed = corner_radius_drag_.value_changed;
        const std::string primary_layer_id = corner_radius_drag_.primary_layer_id;
        const int point_index = corner_radius_drag_.point_index;
        if (!moved) {
            if (ev->modifiers().testFlag(Qt::ShiftModifier) &&
                selected_corner_layer_id_ == primary_layer_id) {
                auto it = selected_corner_indices_.find(point_index);
                if (it == selected_corner_indices_.end())
                    selected_corner_indices_.insert(point_index);
                else
                    selected_corner_indices_.erase(it);
                if (selected_corner_indices_.empty())
                    selected_corner_layer_id_.clear();
            } else {
                selected_corner_layer_id_ = primary_layer_id;
                selected_corner_indices_.clear();
                selected_corner_indices_.insert(point_index);
            }
        } else {
            clear_corner_selection(false);
        }
        drag_mode_ = DragMode::None;
        drag_changed_ = false;
        corner_radius_drag_ = CornerRadiusDragState{};
        clear_snap_feedback();
        unsetCursor();
        if (moved)
            end_adaptive_interaction();
        if (value_changed)
            emit layer_geometry_changed();
        invalidate_canvas_overlay_caches();
        notify_corner_context_changed();
        update();
        ev->accept();
        return;
    }

    if (drag_mode_ == DragMode::GuideX || drag_mode_ == DragMode::GuideY) {
        const bool x_axis = drag_mode_ == DragMode::GuideX;
        const QRectF canvas_rect = canvas_view_rect();
        const bool inside_canvas_band = x_axis
            ? (ev->pos().x() >= canvas_rect.left() - 12.0 && ev->pos().x() <= canvas_rect.right() + 12.0)
            : (ev->pos().y() >= canvas_rect.top() - 12.0 && ev->pos().y() <= canvas_rect.bottom() + 12.0);
        auto &guides = x_axis ? vertical_guides_ : horizontal_guides_;
        if (inside_canvas_band) {
            if (dragging_guide_index_ >= 0 && (size_t)dragging_guide_index_ < guides.size())
                guides[(size_t)dragging_guide_index_] = dragging_guide_value_;
            else
                guides.push_back(dragging_guide_value_);
            std::sort(guides.begin(), guides.end());
        } else if (dragging_guide_index_ >= 0 && (size_t)dragging_guide_index_ < guides.size()) {
            guides.erase(guides.begin() + dragging_guide_index_);
        }
        dragging_new_guide_ = false;
        dragging_guide_index_ = -1;
        drag_mode_ = DragMode::None;
        clear_snap_feedback();
        save_ruler_guide_settings();
        unsetCursor();
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }

    if (drag_mode_ == DragMode::Marquee) {
        update_marquee(ev->pos(), ev->modifiers());
        if (!marquee_active_)
            emit layers_selected(std::vector<std::string>{});
        drag_mode_ = DragMode::None;
        marquee_active_ = false;
        drag_changed_ = false;
        alt_duplicate_pending_ = false;
        alt_duplicate_done_ = false;
        alt_duplicate_active_ = false;
        drag_text_object_scaling_ = false;
        gradient_drag_ = GradientDragState{};
        corner_radius_drag_ = CornerRadiusDragState{};
        clear_snap_feedback();
        unsetCursor();
        invalidate_canvas_overlay_caches();
        update();
        ev->accept();
        return;
    }

    bool changed = drag_changed_;
    /* Keep the duplicated objects selected at the end of the gesture even if
     * a synchronous UI refresh replayed the pre-duplication selection while
     * the mouse was down. The drag-state IDs are the authoritative clone IDs. */
    if (alt_duplicate_active_ && !drag_layer_states_.empty()) {
        selected_layer_ids_.clear();
        for (const auto &state : drag_layer_states_) {
            if (title_ && title_->find_layer(state.id))
                selected_layer_ids_.push_back(state.id);
        }
        sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
        emit layers_selected(selected_layer_ids_);
    }
    drag_mode_ = DragMode::None;
    drag_changed_ = false;
    alt_duplicate_pending_ = false;
    alt_duplicate_done_ = false;
    alt_duplicate_active_ = false;
    drag_text_object_scaling_ = false;
    gradient_drag_ = GradientDragState{};
    corner_radius_drag_ = CornerRadiusDragState{};
    drag_layer_states_.clear();
    clear_snap_feedback();
    unsetCursor();
    if (changed) {
        end_adaptive_interaction();
        emit layer_geometry_changed();
    }
    /* Drag tooltips and snap labels are part of the cached GPU overlay. The
     * drag state is already cleared here, so invalidate before repainting or
     * the last interaction overlay can remain indefinitely on the canvas. */
    invalidate_canvas_overlay_caches();
    update();
    ev->accept();
}


void CanvasPreview::show_selected_layers_context_menu(const QPoint &global_pos)
{
    if (!title_ || selected_layer_ids_.empty())
        return;

    context_menu_selection_override_ = selected_layer_ids_;
    QContextMenuEvent synthetic_event(QContextMenuEvent::Other,
                                      mapFromGlobal(global_pos), global_pos);
    contextMenuEvent(&synthetic_event);
    context_menu_selection_override_.clear();
}


void CanvasPreview::contextMenuEvent(QContextMenuEvent *ev)
{
    const bool forced_layer_selection =
        !context_menu_selection_override_.empty();
    if (!forced_layer_selection &&
        active_tool_ == CanvasTool::Pen && pen_path_active_) {
        ev->accept();
        return;
    }

    bool x_axis = true;
    const int guide_index = forced_layer_selection
        ? -1 : guide_hit_test(ev->pos(), x_axis, true);
    if (guide_index >= 0) {
        QMenu guide_menu(this);
        QAction *delete_guide = guide_menu.addAction(fxm_tr("OBSTitles.DeleteGuide"));
        delete_guide->setEnabled(!guides_locked_);
        QAction *chosen = guide_menu.exec(ev->globalPos());
        if (chosen == delete_guide && !guides_locked_) {
            auto &guides = x_axis ? vertical_guides_ : horizontal_guides_;
            if ((size_t)guide_index < guides.size()) {
                guides.erase(guides.begin() + guide_index);
                clear_snap_feedback();
                save_ruler_guide_settings();
                invalidate_canvas_overlay_caches();
                update();
            }
        }
        ev->accept();
        return;
    }

    if (!title_) {
        QWidget::contextMenuEvent(ev);
        return;
    }

    if (!forced_layer_selection &&
        show_position_motion_path_context_menu(ev))
        return;

    std::shared_ptr<Layer> hit = forced_layer_selection
        ? nullptr : layer_at_view_pos(ev->pos());
    const bool clicked_current_selection = hit &&
        std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), hit->id) !=
            selected_layer_ids_.end();

    if (!forced_layer_selection && !clicked_current_selection) {
        if (hit) {
            selected_layer_ids_ = {hit->id};
            sel_layer_id_ = hit->id;
            emit layers_selected(selected_layer_ids_);
            invalidate_canvas_overlay_caches();
            update();
        }
    }

    /* Context actions are scoped to the object under the pointer. A right-click
     * on empty canvas must never act on a stale selection elsewhere. Keep the
     * visual selection unchanged, but expose only canvas-level actions. */
    std::vector<std::string> selection;
    if (forced_layer_selection) {
        selection = context_menu_selection_override_;
    } else if (hit) {
        selection = selected_layer_ids_;
        if (selection.empty() && !sel_layer_id_.empty())
            selection.push_back(sel_layer_id_);
    }
    const bool has_selection = !selection.empty();

    auto selected_layer_is_group = [this](const std::string &layer_id) {
        auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
        return layer && layer->type == LayerType::Group;
    };
    auto parent_is_group = [this](const std::string &layer_id) {
        auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
        auto parent = layer && !layer->parent_id.empty()
            ? title_->find_layer(layer->parent_id) : nullptr;
        return parent && parent->type == LayerType::Group;
    };
    auto would_cycle = [this](const std::string &child_id,
                              const std::string &group_id) {
        if (child_id.empty() || group_id.empty() || child_id == group_id)
            return true;
        const auto group = title_ ? title_->find_layer(group_id) : nullptr;
        return group && editor_layer_has_ancestor(title_, *group, child_id);
    };

    bool all_locked = has_selection;
    bool all_hidden = has_selection;
    for (const auto &id : selection) {
        const auto layer = title_->find_layer(id);
        if (!layer)
            continue;
        all_locked = all_locked && layer->locked;
        all_hidden = all_hidden && !layer->visible;
    }

    std::shared_ptr<Layer> selected_asset;
    if (selection.size() == 1) {
        const auto candidate = title_->find_layer(selection.front());
        if (candidate && candidate->type == LayerType::Asset)
            selected_asset = candidate;
    }

    QMenu menu(this);
    const QPalette pal = palette();
    const QString menu_style = QStringLiteral(
        "QMenu{color:%1;background:%2;border:1px solid %3;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:%4;color:%5;}"
        "QMenu::item:disabled{color:%6;}")
        .arg(pal.color(QPalette::Text).name(QColor::HexRgb),
             pal.color(QPalette::Base).name(QColor::HexRgb),
             pal.color(QPalette::Mid).name(QColor::HexRgb),
             pal.color(QPalette::Highlight).name(QColor::HexRgb),
             pal.color(QPalette::HighlightedText).name(QColor::HexRgb),
             pal.color(QPalette::Disabled, QPalette::Text).name(QColor::HexRgb));
    menu.setStyleSheet(menu_style);

    QAction *edit_asset = nullptr;
    if (selected_asset) {
        edit_asset = menu.addAction(fxm_tr("OBSTitles.EditAsset"));
        edit_asset->setEnabled(!selected_asset->asset_title_id.empty());
        menu.addSeparator();
    }

    QAction *show_hide = menu.addAction(all_hidden
        ? fxm_tr("OBSTitles.ShowLayers")
        : fxm_tr("OBSTitles.HideLayers"));
    show_hide->setEnabled(has_selection);
    QAction *lock_unlock = menu.addAction(all_locked
        ? fxm_tr("OBSTitles.UnlockLayers")
        : fxm_tr("OBSTitles.LockLayers"));
    lock_unlock->setEnabled(has_selection);

    menu.addSeparator();
    QAction *duplicate = menu.addAction(fxm_tr("OBSTitles.CloneLayer"));
    duplicate->setEnabled(has_selection);
    QAction *cut = menu.addAction(fxm_tr("OBSTitles.CutLayer"));
    cut->setEnabled(has_selection);
    QAction *copy = menu.addAction(fxm_tr("OBSTitles.CopyLayer"));
    copy->setEnabled(has_selection);
    QAction *paste = menu.addAction(fxm_tr("OBSTitles.PasteLayer"));
    QAction *del = menu.addAction(fxm_tr("OBSTitles.DeleteLayer"));
    del->setEnabled(has_selection);

    menu.addSeparator();
    QAction *group_layers = menu.addAction(fxm_tr("OBSTitles.GroupLayers"));
    group_layers->setEnabled(selection.size() >= 2);
    QAction *ungroup_layers = menu.addAction(fxm_tr("OBSTitles.UngroupLayers"));
    ungroup_layers->setEnabled(std::any_of(selection.begin(), selection.end(),
                                           selected_layer_is_group));

    QMenu *add_to_group = menu.addMenu(fxm_tr("OBSTitles.AddToGroup"));
    add_to_group->setStyleSheet(menu_style);
    std::map<QAction *, std::string> group_targets;
    bool has_available_group = false;
    for (const auto &candidate : title_->layers) {
        if (!candidate || candidate->type != LayerType::Group || candidate->locked)
            continue;
        bool valid = false;
        for (const auto &selected_id : selection) {
            const auto selected_layer = title_->find_layer(selected_id);
            if (!selected_layer || selected_layer->locked ||
                selected_id == candidate->id ||
                selected_layer->parent_id == candidate->id ||
                would_cycle(selected_id, candidate->id))
                continue;
            valid = true;
            break;
        }
        if (!valid)
            continue;
        QAction *target = add_to_group->addAction(
            QString::fromStdString(candidate->name));
        group_targets[target] = candidate->id;
        has_available_group = true;
    }
    if (!has_available_group) {
        QAction *none = add_to_group->addAction(
            fxm_tr("OBSTitles.NoAvailableGroups"));
        none->setEnabled(false);
    }
    QAction *remove_from_group = menu.addAction(
        fxm_tr("OBSTitles.RemoveFromGroup"));
    remove_from_group->setEnabled(std::any_of(selection.begin(), selection.end(),
                                               parent_is_group));

    menu.addSeparator();
    QMenu *transform = menu.addMenu(fxm_tr("OBSTitles.Transform"));
    transform->setStyleSheet(menu_style);
    QAction *fit_screen = transform->addAction(
        fxm_tr("OBSTitles.FitScreen"));
    QAction *fill_screen = transform->addAction(
        fxm_tr("OBSTitles.FillScreen"));
    transform->addSeparator();
    QAction *flip_horizontal = transform->addAction(
        fxm_tr("OBSTitles.FlipHorizontal"));
    QAction *flip_vertical = transform->addAction(
        fxm_tr("OBSTitles.FlipVertical"));
    transform->addSeparator();
    QAction *rotate_left = transform->addAction(
        fxm_tr("OBSTitles.RotateLeft90"));
    QAction *rotate_right = transform->addAction(
        fxm_tr("OBSTitles.RotateRight90"));
    transform->setEnabled(has_selection);

    QMenu *arrange = menu.addMenu(fxm_tr("OBSTitles.LayerOrder"));
    arrange->setStyleSheet(menu_style);
    QAction *bring_front = arrange->addAction(
        fxm_tr("OBSTitles.BringToFront"));
    QAction *bring_forward = arrange->addAction(
        fxm_tr("OBSTitles.BringForward"));
    QAction *send_backward = arrange->addAction(
        fxm_tr("OBSTitles.SendBackward"));
    QAction *send_back = arrange->addAction(
        fxm_tr("OBSTitles.SendToBack"));
    arrange->setEnabled(has_selection);

    QAction *chosen = menu.exec(ev->globalPos());
    if (chosen == edit_asset && selected_asset)
        emit edit_asset_requested(
            QString::fromStdString(selected_asset->asset_title_id));
    else if (chosen == show_hide)
        emit set_layers_visible_requested(all_hidden);
    else if (chosen == lock_unlock)
        emit set_layers_locked_requested(!all_locked);
    else if (chosen == duplicate)
        emit duplicate_layers_requested();
    else if (chosen == cut)
        emit cut_layers_requested();
    else if (chosen == copy)
        emit copy_layers_requested();
    else if (chosen == paste)
        emit paste_layers_requested();
    else if (chosen == del)
        emit delete_layers_requested();
    else if (chosen == group_layers)
        emit group_layers_requested();
    else if (chosen == ungroup_layers)
        emit ungroup_layers_requested();
    else if (chosen == remove_from_group)
        emit remove_layers_from_group_requested();
    else if (chosen && group_targets.find(chosen) != group_targets.end())
        emit add_layers_to_group_requested(group_targets.at(chosen));
    else if (chosen == fit_screen)
        emit fit_fill_layers_requested(false);
    else if (chosen == fill_screen)
        emit fit_fill_layers_requested(true);
    else if (chosen == flip_horizontal)
        emit flip_layers_requested(true);
    else if (chosen == flip_vertical)
        emit flip_layers_requested(false);
    else if (chosen == rotate_left)
        emit rotate_layers_requested(-90.0);
    else if (chosen == rotate_right)
        emit rotate_layers_requested(90.0);
    else if (chosen == bring_front)
        emit layer_order_requested(2);
    else if (chosen == bring_forward)
        emit layer_order_requested(1);
    else if (chosen == send_backward)
        emit layer_order_requested(-1);
    else if (chosen == send_back)
        emit layer_order_requested(-2);

    ev->accept();
}

void CanvasPreview::wheelEvent(QWheelEvent *ev)
{
    if (!title_) {
        ev->ignore();
        return;
    }

    QPointF wheel_delta = ev->pixelDelta();
    if (wheel_delta.isNull()) {
        constexpr double kWheelPanStepPx = 80.0;
        wheel_delta = QPointF(ev->angleDelta()) / 120.0 * kWheelPanStepPx;
    }

    const Qt::KeyboardModifiers mods = ev->modifiers();
    if (editor_3d_view_ != Editor3DView::ActiveCamera &&
        mods.testFlag(Qt::AltModifier)) {
        const double dolly_delta = !qFuzzyIsNull(wheel_delta.y())
            ? wheel_delta.y() : wheel_delta.x();
        if (!qFuzzyIsNull(dolly_delta))
            dolly_editor_3d_view(dolly_delta);
    } else if (mods.testFlag(Qt::AltModifier)) {
        const double zoom_delta = !qFuzzyIsNull(wheel_delta.y()) ? wheel_delta.y() : wheel_delta.x();
        if (qFuzzyIsNull(zoom_delta)) {
            ev->accept();
            return;
        }

        const QPointF anchor_canvas = view_to_canvas(ev->position());
        const double factor = std::pow(1.1, zoom_delta / 80.0);
        const int next = (int)std::round((double)zoom_percent_ * factor);
        zoom_percent_ = std::clamp(next, 5, 1600);
        fit_zoom_active_ = false;
        const QPointF origin_without_pan = centered_view_origin();
        const double scale = view_scale();
        pan_offset_ = ev->position() - origin_without_pan -
                      QPointF(anchor_canvas.x() * scale, anchor_canvas.y() * scale);
        invalidate_canvas_overlay_caches();
        emit zoom_percent_changed(zoom_percent_);
    } else if (mods.testFlag(Qt::ControlModifier)) {
        const double horizontal_delta = !qFuzzyIsNull(wheel_delta.y()) ? wheel_delta.y() : wheel_delta.x();
        pan_offset_.rx() += horizontal_delta;
        fit_zoom_active_ = false;
        invalidate_canvas_overlay_caches();
    } else {
        const double vertical_delta = !qFuzzyIsNull(wheel_delta.y()) ? wheel_delta.y() : wheel_delta.x();
        pan_offset_.ry() += vertical_delta;
        fit_zoom_active_ = false;
        invalidate_canvas_overlay_caches();
    }

    position_text_editor();
    update();
    ev->accept();
}

void CanvasPreview::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    dirty_ = true;
    invalidate_canvas_overlay_caches();
    if (fit_zoom_active_) fit_canvas(fit_zoom_up_to_100_);
    position_text_editor();
    gpu_underlay_key_.clear();
    /* The OBS swapchain keeps the artwork destination rectangle separately
     * from QWidget geometry. Refresh it even when no overlay texture changes,
     * otherwise a title opened during dock restoration can retain the hidden
     * page's old top/left offset until the next user interaction. */
    update_gpu_display_textures(nullptr, nullptr, nullptr);
    update();
}

/* ══════════════════════════════════════════════════════════════════
 *  LayerStack
 * ══════════════════════════════════════════════════════════════════ */
