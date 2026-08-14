    /* Color picking is an explicit inspection operation, so it is allowed to
     * perform one final-frame readback. Normal canvas presentation remains
     * GPU-resident and never populates frame_image_. */
    const QImage image = !color_picker_source_image_.isNull()
        ? color_picker_source_image_
        : current_rendered_frame();
    if (image.isNull())
        return false;

    const QPointF canvas = view_to_canvas(view_pt);
    const int x = static_cast<int>(std::floor(canvas.x()));
    const int y = static_cast<int>(std::floor(canvas.y()));
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height()) {
        color = QColor(Qt::transparent);
        return true;
    }

    color = image.pixelColor(x, y);
    return color.isValid();
}

void CanvasPreview::update_color_picker_tooltip(const QPointF &view_pt)
{
    color_picker_tooltip_pos_ = view_pt;
    QColor color;
    color_picker_tooltip_visible_ = sample_color_at_view(view_pt, color);
    if (color_picker_tooltip_visible_)
        color_picker_tooltip_color_ = color;
    gpu_overlay_dirty_ = true;
    update();
}

static QString editor_color_hex(const QColor &color)
{
    if (color.alpha() < 255) {
        return QStringLiteral("#%1%2%3%4")
            .arg(color.red(), 2, 16, QLatin1Char('0'))
            .arg(color.green(), 2, 16, QLatin1Char('0'))
            .arg(color.blue(), 2, 16, QLatin1Char('0'))
            .arg(color.alpha(), 2, 16, QLatin1Char('0'))
            .toUpper();
    }
    return QStringLiteral("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
}


void CanvasPreview::draw_shader_compile_overlay(QPainter &p, const QRectF &canvas_rect)
{
    if (!gpu_render_session_ || canvas_rect.isEmpty())
        return;
    TitleGpuShaderCompileStatus status;
    if (!title_gpu_render_session_shader_compile_status(
            gpu_render_session_, status))
        return;

    const uint32_t total = std::max<uint32_t>(1u, status.total_jobs);
    const uint32_t current = std::clamp<uint32_t>(
        status.completed_jobs + ((status.active || status.queued) ? 1u : 0u),
        1u, total);
    const QString headline = QStringLiteral("Compiling Shaders %1 of %2")
        .arg(current)
        .arg(total);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(canvas_rect, palette().color(QPalette::Window));

    QFont title_font = p.font();
    title_font.setBold(true);
    title_font.setPointSizeF(std::max(13.0, title_font.pointSizeF() + 4.0));
    p.setFont(title_font);
    p.setPen(palette().color(QPalette::WindowText));
    p.drawText(canvas_rect.adjusted(24.0, 24.0, -24.0, -24.0),
               Qt::AlignCenter | Qt::TextSingleLine, headline);
    p.restore();
}

void CanvasPreview::draw_color_picker_tooltip(QPainter &p)
{
    if (active_tool_ != CanvasTool::ColorPicker || !color_picker_tooltip_visible_)
        return;

    const QString hex = editor_color_hex(color_picker_tooltip_color_);
    const QString name = fxm_tr("OBSTitles.ColorPickerTool");
    QFont name_font = font();
    name_font.setPointSizeF(name_font.pointSizeF() * 1.35);
    name_font.setBold(true);
    QFont hex_font = font();
    hex_font.setPointSizeF(hex_font.pointSizeF() * 1.05);
    const QFontMetrics name_fm(name_font);
    const QFontMetrics hex_fm(hex_font);
    const int swatch = 72;
    const int pad = 16;
    const int gap = 14;
    const int text_width = std::max(name_fm.horizontalAdvance(name), hex_fm.horizontalAdvance(hex));
    const int width = pad * 2 + swatch + gap + text_width;
    const int text_height = name_fm.height() + 5 + hex_fm.height();
    const int height = std::max(pad * 2 + swatch, pad * 2 + text_height);
    QPointF pos = color_picker_tooltip_pos_ + QPointF(20.0, 22.0);
    if (pos.x() + width + 4 > rect().right())
        pos.setX(color_picker_tooltip_pos_.x() - width - 20.0);
    if (pos.y() + height + 4 > rect().bottom())
        pos.setY(color_picker_tooltip_pos_.y() - height - 20.0);
    pos.setX(std::max(4.0, pos.x()));
    pos.setY(std::max(4.0, pos.y()));

    QRectF box(pos, QSizeF(width, height));
    QRectF swatch_rect(box.left() + pad, box.top() + (box.height() - swatch) / 2.0, swatch, swatch);
    QRectF text_rect(swatch_rect.right() + gap, box.top() + (box.height() - text_height) / 2.0,
                     width - pad - swatch - gap, text_height);

    const QPalette pal = qApp->palette();
    QColor base = pal.color(QPalette::ToolTipBase);
    QColor text = pal.color(QPalette::ToolTipText);
    QColor border = pal.color(QPalette::Mid);
    QColor subtle = text.lightness() < 128 ? text.lighter(145) : text.darker(145);
    base.setAlpha(242);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(border, 1.0));
    p.setBrush(base);
    p.drawRoundedRect(box, 4.0, 4.0);

    const int cell = 8;
    p.setPen(Qt::NoPen);
    for (int y = (int)swatch_rect.top(); y < (int)swatch_rect.bottom(); y += cell) {
        for (int x = (int)swatch_rect.left(); x < (int)swatch_rect.right(); x += cell) {
            const bool dark = ((x / cell) + (y / cell)) % 2;
            p.fillRect(QRect(x, y, cell, cell).intersected(swatch_rect.toAlignedRect()),
                       dark ? QColor(95, 95, 95) : QColor(165, 165, 165));
        }
    }
    p.fillRect(swatch_rect, color_picker_tooltip_color_);
    p.setPen(QPen(border, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(swatch_rect.adjusted(0.0, 0.0, -1.0, -1.0));

    p.setFont(name_font);
    p.setPen(text);
    p.drawText(QRectF(text_rect.left(), text_rect.top(), text_rect.width(), name_fm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, name);
    p.setFont(hex_font);
    p.setPen(subtle);
    p.drawText(QRectF(text_rect.left(), text_rect.top() + name_fm.height() + 5,
                      text_rect.width(), hex_fm.height()),
               Qt::AlignLeft | Qt::AlignVCenter, hex);
    p.restore();
}

QString CanvasPreview::canvas_drag_tooltip_text() const
{
    if (drawing_shape_) {
        const QRectF rect = shape_draw_current_rect_.isValid()
                                ? shape_draw_current_rect_.normalized()
                                : toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_).normalized();
        if (drawing_shape_changed_ && rect.isValid() && !rect.isEmpty()) {
            return fxm_tr("OBSTitles.SizePixelReadout")
                .arg(std::round(rect.width()), 0, 'f', 0)
                .arg(std::round(rect.height()), 0, 'f', 0);
        }
    }

    if (drag_mode_ == DragMode::None || drag_mode_ == DragMode::Marquee || !drag_changed_)
        return QString();

    auto is_resize_drag = [](DragMode mode) {
        return mode == DragMode::ResizeNW || mode == DragMode::ResizeN || mode == DragMode::ResizeNE ||
               mode == DragMode::ResizeE || mode == DragMode::ResizeSE || mode == DragMode::ResizeS ||
               mode == DragMode::ResizeSW || mode == DragMode::ResizeW;
    };
    auto is_corner_radius_drag = [](DragMode mode) {
        return mode == DragMode::CornerRadius || mode == DragMode::CornerRadiusTL ||
               mode == DragMode::CornerRadiusTR || mode == DragMode::CornerRadiusBR ||
               mode == DragMode::CornerRadiusBL;
    };

    if (drag_mode_ == DragMode::Move) {
        const QPointF canvas = view_to_canvas(drag_current_view_);
        return fxm_tr("OBSTitles.PositionPixelReadout")
            .arg(std::round(canvas.x()), 0, 'f', 0)
            .arg(std::round(canvas.y()), 0, 'f', 0);
    }

    if (is_resize_drag(drag_mode_)) {
        const auto layers = selected_layers();
        double w = 0.0;
        double h = 0.0;
        if (layers.size() > 1) {
            const QRectF bounds = selected_canvas_bounds();
            w = bounds.width();
            h = bounds.height();
        } else if (!layers.empty() && layers.front()) {
            const double lt = std::clamp(playhead_ - layers.front()->in_time, 0.0,
                                         std::max(0.0, layers.front()->out_time - layers.front()->in_time));
            w = eval_box_width(*layers.front(), lt);
            h = eval_box_height(*layers.front(), lt);
        }
        return fxm_tr("OBSTitles.SizePixelReadout")
            .arg(std::round(w), 0, 'f', 0)
            .arg(std::round(h), 0, 'f', 0);
    }

    if (drag_mode_ == DragMode::Rotate) {
        const double radians = degrees_to_radians(drag_current_rotation_delta_);
        return fxm_tr("OBSTitles.RadiansReadout").arg(radians, 0, 'f', 3);
    }

    if (is_corner_radius_drag(drag_mode_)) {
        auto layer = selected_layer();
        if (!layer)
            return QString();
        double radius = drag_mode_ == DragMode::CornerRadius
            ? corner_radius_value(*layer, corner_radius_drag_.point_index)
            : 0.0;
        if (drag_mode_ != DragMode::CornerRadius) {
            switch (drag_mode_) {
            case DragMode::CornerRadiusTL: radius = layer->corner_radius_tl; break;
            case DragMode::CornerRadiusTR: radius = layer->corner_radius_tr; break;
            case DragMode::CornerRadiusBR: radius = layer->corner_radius_br; break;
            case DragMode::CornerRadiusBL: radius = layer->corner_radius_bl; break;
            default: break;
            }
        }
        return fxm_tr("OBSTitles.RadiusPixelReadout").arg(std::round(radius), 0, 'f', 0);
    }

    return QString();
}

void CanvasPreview::draw_canvas_drag_tooltip(QPainter &p)
{
    const QString text = canvas_drag_tooltip_text();
    if (text.isEmpty())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics fm(font());
    const int width = fm.horizontalAdvance(text) + 18;
    const int height = std::max(24, fm.height() + 8);
    QPointF pos = drag_current_view_ + QPointF(14.0, 22.0);
    if (pos.x() + width + 6 > rect().right())
        pos.setX(drag_current_view_.x() - width - 14.0);
    if (pos.y() + height + 6 > rect().bottom())
        pos.setY(drag_current_view_.y() - height - 14.0);
    pos.setX(std::max(6.0, pos.x()));
    pos.setY(std::max(6.0, pos.y()));

    const QRectF box(pos, QSizeF(width, height));
    p.setPen(QPen(QColor(18, 18, 18), 1.0));
    p.setBrush(QColor(48, 48, 48, 235));
    p.drawRoundedRect(box, 4.0, 4.0);
    p.setPen(QColor(245, 245, 245));
    p.drawText(box.adjusted(9.0, 0.0, -9.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, text);
    p.restore();
}


QRect CanvasPreview::snap_cursor_update_rect() const
{
    if (!snap_cursor_visible_)
        return QRect();
    const QPointF view_pt = canvas_to_view(snap_cursor_canvas_);
    return QRectF(view_pt.x() - 12.0, view_pt.y() - 12.0, 24.0, 24.0).toAlignedRect();
}

QRect CanvasPreview::final_snap_cursor_update_rect() const
{
    if (!final_snap_cursor_visible_)
        return QRect();
    const QPointF view_pt = canvas_to_view(final_snap_cursor_canvas_);
    return QRectF(view_pt.x() - 12.0, view_pt.y() - 12.0, 24.0, 24.0).toAlignedRect();
}

QRect CanvasPreview::canvas_drag_tooltip_update_rect() const
{
    const QString text = canvas_drag_tooltip_text();
    if (text.isEmpty())
        return QRect();

    const QFontMetrics fm(font());
    const int width = fm.horizontalAdvance(text) + 18;
    const int height = std::max(24, fm.height() + 8);
    QPointF pos = drag_current_view_ + QPointF(14.0, 22.0);
    if (pos.x() + width + 6 > rect().right())
        pos.setX(drag_current_view_.x() - width - 14.0);
    if (pos.y() + height + 6 > rect().bottom())
        pos.setY(drag_current_view_.y() - height - 14.0);
    pos.setX(std::max(6.0, pos.x()));
    pos.setY(std::max(6.0, pos.y()));
    return QRectF(pos, QSizeF(width, height)).toAlignedRect().adjusted(-3, -3, 3, 3);
}

void CanvasPreview::clear_draw_tool_snap_cursor()
{
    const bool had_cursor = snap_cursor_visible_;
    const bool had_final_cursor = final_snap_cursor_visible_;
    const bool had_feedback = !snap_feedback_.empty();
    const QRect old_rect = snap_cursor_update_rect().united(final_snap_cursor_update_rect());
    snap_cursor_visible_ = false;
    final_snap_cursor_visible_ = false;
    snap_feedback_.clear();
    if (had_cursor || had_final_cursor)
        update(old_rect.adjusted(-2, -2, 2, 2));
    if (had_feedback)
        update();
}

void CanvasPreview::update_draw_tool_snap_cursor(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!title_ || drawing_shape_ ||
        (active_tool_ != CanvasTool::Shape && active_tool_ != CanvasTool::Text &&
         active_tool_ != CanvasTool::Image && active_tool_ != CanvasTool::Pen)) {
        clear_draw_tool_snap_cursor();
        return;
    }

    const bool had_feedback = !snap_feedback_.empty();
    const QRect old_rect = snap_cursor_update_rect();
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    const QPointF snapped = snap_canvas_point(view_to_canvas(view_pt), true, true, allow_snap);
    const bool show = allow_snap && snap_settings_.enabled && !snap_feedback_.empty();
    snap_cursor_visible_ = show;
    snap_cursor_canvas_ = snapped;

    const QRect new_rect = snap_cursor_update_rect();
    QRect dirty = old_rect.united(new_rect);
    if (!dirty.isEmpty())
        update(dirty.adjusted(-4, -4, 4, 4));
    // Snap helpers can span the whole canvas/ruler area, so repaint the full
    // preview when hover feedback appears, disappears, or changes.  This keeps
    // the Adobe-style snapped dot and helper lines visible before mouse press.
    if (had_feedback || !snap_feedback_.empty())
        update();
}

void CanvasPreview::draw_snap_cursor_indicator(QPainter &p)
{
    if (active_tool_ != CanvasTool::Shape && active_tool_ != CanvasTool::Text &&
        active_tool_ != CanvasTool::Image && active_tool_ != CanvasTool::Pen && !drawing_shape_)
        return;
    if (!snap_cursor_visible_ && !final_snap_cursor_visible_)
        return;

    auto role = TitlePreferences::CanvasHelperColorRole::CanvasSnapLines;
    for (const auto &feedback : snap_feedback_) {
        if (snap_feedback_role_from_label(feedback.label) == TitlePreferences::CanvasHelperColorRole::ObjectSnapLines) {
            role = TitlePreferences::CanvasHelperColorRole::ObjectSnapLines;
            break;
        }
    }
    QColor color = editor_canvas_helper_color(role);
    if (!color.isValid())
        color = QColor(0, 210, 110, 220);
    QColor fill = color;
    fill.setAlpha(std::min(80, std::max(32, color.alpha() / 3)));

    auto draw_circle = [&](const QPointF &canvas_pt, double radius, bool filled) {
        const QPointF pt = canvas_to_view(canvas_pt);
        QPen pen(color, 1.5, Qt::SolidLine);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(filled ? fill : Qt::NoBrush);
        p.drawEllipse(pt, radius, radius);
    };

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (snap_cursor_visible_)
        draw_circle(snap_cursor_canvas_, 5.0, true);
    if (final_snap_cursor_visible_)
        draw_circle(final_snap_cursor_canvas_, 5.0, true);
    p.restore();
}

void CanvasPreview::update_shape_drawing(const QPointF &view_pt, Qt::KeyboardModifiers modifiers)
{
    if (!drawing_shape_) return;

    const QRect old_update_rect = last_toolbar_preview_update_rect_
                                      .united(snap_cursor_update_rect())
                                      .united(final_snap_cursor_update_rect())
                                      .united(canvas_drag_tooltip_update_rect());
    drag_current_view_ = view_pt;
    shape_draw_current_canvas_ = view_to_canvas(view_pt);
    shape_draw_modifiers_ = modifiers;
    const bool allow_snap = !modifiers.testFlag(Qt::ControlModifier);
    final_snap_cursor_visible_ = false;
    snap_feedback_.clear();

    const QPointF raw_current = shape_draw_current_canvas_;
    QRectF raw_rect = toolbar_draw_rect(raw_current, shape_draw_modifiers_);
    if (allow_snap && raw_rect.isValid()) {
        const QPointF delta = raw_current - shape_draw_start_canvas_;
        const bool alt = modifiers.testFlag(Qt::AltModifier);
        const bool active_left = alt ? delta.x() < 0.0 : raw_rect.left() < shape_draw_start_canvas_.x();
        const bool active_right = alt ? delta.x() >= 0.0 : raw_rect.right() > shape_draw_start_canvas_.x();
        const bool active_top = alt ? delta.y() < 0.0 : raw_rect.top() < shape_draw_start_canvas_.y();
        const bool active_bottom = alt ? delta.y() >= 0.0 : raw_rect.bottom() > shape_draw_start_canvas_.y();

        auto snap_value = [&](bool x_axis, double value, double *snapped) {
            if (!snapped)
                return false;
            std::vector<double> targets;
            std::vector<QString> labels;
            collect_snap_targets(x_axis, targets, labels);
            collect_spacing_targets(x_axis, targets, labels);
            const double tolerance = 6.0 / std::max(0.1, view_scale());
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
            if (best_distance > tolerance)
                return false;
            *snapped = best_target;
            add_snap_feedback(x_axis, best_target, best_label);
            return true;
        };

        QPointF snapped_current = raw_current;
        double snapped = 0.0;
        if (active_left && snap_value(true, raw_rect.left(), &snapped))
            snapped_current.setX(raw_current.x() + snapped - raw_rect.left());
        else if (active_right && snap_value(true, raw_rect.right(), &snapped))
            snapped_current.setX(raw_current.x() + snapped - raw_rect.right());
        if (active_top && snap_value(false, raw_rect.top(), &snapped))
            snapped_current.setY(raw_current.y() + snapped - raw_rect.top());
        else if (active_bottom && snap_value(false, raw_rect.bottom(), &snapped))
            snapped_current.setY(raw_current.y() + snapped - raw_rect.bottom());

        shape_draw_current_canvas_ = snapped_current;
        final_snap_cursor_visible_ = !snap_feedback_.empty();
        final_snap_cursor_canvas_ = shape_draw_current_canvas_;
        raw_rect = toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
    }
    shape_draw_current_rect_ = raw_rect.normalized();
    drawing_shape_changed_ = shape_draw_current_rect_.width() >= 2.0 || shape_draw_current_rect_.height() >= 2.0;

    last_toolbar_preview_update_rect_ = toolbar_preview_update_rect()
                                           .united(snap_cursor_update_rect())
                                           .united(final_snap_cursor_update_rect())
                                           .united(canvas_drag_tooltip_update_rect());
    /* The GPU canvas keeps editor chrome in a cached overlay texture. The
     * toolbar-drawing preview is overlay-only state, so changing its rectangle
     * must invalidate that texture explicitly; QWidget::update() alone only
     * schedules a paint event and otherwise reuses the previous transparent
     * overlay until mouse release creates the real layer. */
    gpu_overlay_dirty_ = true;
    QRect repaint_rect = old_update_rect.united(last_toolbar_preview_update_rect_);
    if (repaint_rect.isEmpty())
        repaint_rect = this->rect();
    update(repaint_rect.adjusted(-4, -4, 4, 4));
}


std::shared_ptr<Layer> CanvasPreview::layer_at_view_pos(const QPointF &view_pt) const
{
    if (!title_) return nullptr;
    const QPointF canvas = view_to_canvas(view_pt);

    /* Artwork gets first chance while groups are fully expanded.  Otherwise a
     * group's own container rectangle would sit above its children in model
     * order and make the children impossible to select from the canvas. */
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || !layer_is_editor_visible(*layer) ||
            layer_type_is_container(layer->type) || !layer->visible) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (layer->type == LayerType::Audio)
            continue;
        if (layer->type == LayerType::Light) {
            if (!light_layer_overlay_contains(*layer, view_pt))
                continue;
            const Layer *target = editor_canvas_selection_target(title_, *layer);
            if (!target || target->locked)
                continue;
            return title_->find_layer(target->id);
        }
        if (layer->type == LayerType::Empty) {
            if (!empty_layer_overlay_contains(*layer, view_pt))
                continue;
            const Layer *target = editor_canvas_selection_target(title_, *layer);
            if (!target || target->locked)
                continue;
            return title_->find_layer(target->id);
        }
        const QRectF local_rect = layer_local_rect(*layer);
        if (!local_rect.isValid() || local_rect.isEmpty() ||
            !local_rect.contains(canvas_to_layer(*layer, canvas)))
            continue;
        const Layer *target = editor_canvas_selection_target(title_, *layer);
        if (!target || target->locked)
            continue;
        return title_->find_layer(target->id);
    }

    /* Empty space inside an expanded group can still select the group itself,
     * while collapsed groups naturally remain the sole container target. */
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || !layer_is_editor_visible(*layer) ||
            !layer_type_is_container(layer->type) || !layer->visible ||
            layer->locked)
            continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        const QRectF local_rect = layer_local_rect(*layer);
        if (!local_rect.isValid() || local_rect.isEmpty() ||
            !local_rect.contains(canvas_to_layer(*layer, canvas)))
            continue;
        const Layer *target = editor_canvas_selection_target(title_, *layer);
        if (!target || target->locked)
            continue;
        return title_->find_layer(target->id);
    }
    return nullptr;
}

void CanvasPreview::update_hover_layer(const QPointF &view_pt)
{
    const QPointF previous_mouse_view_pos = last_mouse_view_pos_;
    const bool previous_mouse_inside_canvas = mouse_inside_canvas_;
    last_mouse_view_pos_ = view_pt;
    mouse_inside_canvas_ = title_ && canvas_view_rect().contains(view_pt);
    const bool ruler_indicator_changed =
        previous_mouse_inside_canvas != mouse_inside_canvas_ ||
        previous_mouse_view_pos != last_mouse_view_pos_;

    std::string next_hover;
    if (mouse_inside_canvas_ && drag_mode_ == DragMode::None && !drawing_shape_ &&
        inline_text_layer_id_.empty()) {
        const bool selection_hover = active_tool_ == CanvasTool::Selection;
        const bool draw_tool_hover = active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Text ||
                                     active_tool_ == CanvasTool::Image;
        if (selection_hover || draw_tool_hover) {
            if (auto layer = layer_at_view_pos(view_pt)) {
                if (selection_hover ||
                    (active_tool_ == CanvasTool::Shape &&
                     (layer->type == LayerType::Shape || layer->type == LayerType::SolidRect)) ||
                    (active_tool_ == CanvasTool::Text && is_canvas_text_layer(*layer)) ||
                    (active_tool_ == CanvasTool::Image && layer->type == LayerType::Image)) {
                    next_hover = layer->id;
                }
            }
        }
    }

    const bool hover_changed = hovered_layer_id_ != next_hover;
    if (hover_changed) {
        hovered_layer_id_ = next_hover;
        invalidate_hover_overlay_cache();
        /* Hover chrome is part of the cached GPU overlay. Merely scheduling a
         * QWidget repaint can reuse the previous overlay texture, which makes
         * the hover box appear stuck or disappear entirely. Rebuild the overlay
         * whenever the topmost hovered layer changes. */
        gpu_overlay_dirty_ = true;
        update();
    }

    if (rulers_visible_) {
        /* Rulers are composited into the cached GPU overlay. A QWidget update
         * alone re-presents that cache and leaves the cursor marks at their old
         * coordinates. Rebuild only the overlay (never the artwork frame) when
         * the pointer position or its canvas membership changes. */
        if (ruler_indicator_changed)
            gpu_overlay_dirty_ = true;
        QRect ruler_dirty = ruler_top_rect().toAlignedRect().united(ruler_left_rect().toAlignedRect());
        if (!ruler_dirty.isEmpty()) {
            // Keep ruler cursor indicators in the same backing-store batch as
            // the canvas. Synchronous repaint() can flush a strip before the
            // drag frame and shows up as tearing during fast manipulation.
            update(ruler_dirty.adjusted(-2, -2, 2, 2));
        } else if (!hover_changed) {
            update();
        }
    } else if (!hover_changed) {
        // No repaint is needed for ordinary mouse motion when neither the hover
        // outline nor ruler indicators changed.
    }
}

void CanvasPreview::draw_hover_layer_box(QPainter &p)
{
    const HoverOverlayGeometry &hover_overlay = hover_overlay_geometry();
    if (!hover_overlay.layer || hover_overlay.outline.size() < 3)
        return;
    const Layer &layer = *hover_overlay.layer;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    if (!hover_overlay.hovered_is_selected) {
        QColor hover_color = layer_color(layer, 0);
        if (hover_color.alpha() > 190)
            hover_color.setAlpha(190);
        QPen pen(hover_color, 1.0, Qt::SolidLine);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(hover_overlay.outline);
    }
    if (!hover_overlay.hovered_is_selected && layer_supports_corner_radius_handles(layer))
        draw_corner_radius_handles(p, layer);
    p.restore();
}

void CanvasPreview::draw_canvas_border(QPainter &p, const QRectF &canvas_rect)
{
    if (!canvas_border_visible_ || !canvas_rect.isValid() || canvas_rect.isEmpty()) return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(editor_canvas_helper_color(TitlePreferences::CanvasHelperColorRole::CanvasBorder), 1.0, Qt::SolidLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(canvas_rect.adjusted(0.5, 0.5, -0.5, -0.5));
    p.restore();
}

void CanvasPreview::draw_ruler_mouse_indicators(QPainter &p, const QRectF &canvas_rect)
{
    if (!rulers_visible_ || !title_ || !mouse_inside_canvas_) return;
    if (!canvas_rect.contains(last_mouse_view_pos_)) return;

    const double x = std::clamp(last_mouse_view_pos_.x(), canvas_rect.left(), canvas_rect.right());
    const double y = std::clamp(last_mouse_view_pos_.y(), canvas_rect.top(), canvas_rect.bottom());
    const QRectF top = ruler_top_rect();
    const QRectF left = ruler_left_rect();
    if (!top.isValid() || !left.isValid()) return;

    QColor accent = editor_canvas_helper_color(TitlePreferences::CanvasHelperColorRole::RulerMouseIndicator);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(accent, 1.0, Qt::SolidLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    // Illustrator-style cursor indicators: simple guide lines only, without
    // numeric labels.  The ruler repaint is forced from mouseMoveEvent so these
    // lines track the pointer immediately instead of waiting for a deferred
    // widget update.
    p.drawLine(QPointF(x, top.top()), QPointF(x, top.bottom()));
    p.drawLine(QPointF(left.left(), y), QPointF(left.right(), y));
    p.restore();
}


void CanvasPreview::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (ev->button() == Qt::LeftButton &&
        !inline_text_layer_id_.empty() &&
        !inline_text_suspended_for_gradient_ &&
        inline_text_view_point_hits_document(ev->position()) &&
        set_inline_text_cursor_from_view(ev->position(), false, true)) {
        inline_text_mouse_selecting_ = false;
        ev->accept();
        return;
    }
    if (ev->button() == Qt::LeftButton && active_tool_ == CanvasTool::Pen && pen_path_active_) {
        if (pen_path_points_.size() >= 2) {
            const auto &last = pen_path_points_.back();
            const auto &previous = pen_path_points_[pen_path_points_.size() - 2];
            if (QLineF(QPointF(last.x, last.y), QPointF(previous.x, previous.y)).length() <
                2.0 / std::max(0.1, view_scale()))
                pen_path_points_.pop_back();
        }
        finish_pen_path(false);
        ev->accept();
        return;
    }
    if (ev->button() == Qt::LeftButton && title_) {
        if (active_tool_ == CanvasTool::Selection &&
            add_position_keyframe_at_path(ev->pos())) {
            ev->accept();
            return;
        }
        const QPointF canvas = view_to_canvas(ev->pos());
        std::shared_ptr<Layer> hit_child;
        for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
            const auto &layer = *it;
            if (!layer || !layer_is_editor_visible(*layer) ||
                layer_type_is_container(layer->type) || !layer->visible || layer->locked)
                continue;
            if (playhead_ < layer->in_time || playhead_ > layer->out_time)
                continue;
            const QRectF local_rect = layer_local_rect(*layer);
            if (local_rect.isValid() && !local_rect.isEmpty() &&
                local_rect.contains(canvas_to_layer(*layer, canvas))) {
                hit_child = layer;
                break;
            }
        }

        const Layer *outer_container = hit_child
            ? editor_outermost_group_ancestor(title_, *hit_child) : nullptr;
        if (hit_child && outer_container) {
            /* Asset Layers are intentionally opaque containers. Their internal
             * snapshot is edited only through exposed overrides, never by
             * drilling into hidden instance layers on the canvas. */
            if (outer_container->type == LayerType::Asset) {
                emit layer_clicked(outer_container->id);
                ev->accept();
                return;
            }
            /* Double-click drills into ordinary grouped artwork. Expand the
             * ancestor chain so the selected child is visible in Layers/timeline,
             * then enter inline editing immediately when that child is text. */
            bool hierarchy_changed = false;
            std::string parent_id = hit_child->parent_id;
            std::set<std::string> visited;
            while (!parent_id.empty() && visited.insert(parent_id).second) {
                auto parent = title_->find_layer(parent_id);
                if (!parent || parent->type != LayerType::Group)
                    break;
                if (parent->group_collapsed || !parent->properties_expanded) {
                    parent->group_collapsed = false;
                    parent->properties_expanded = true;
                    hierarchy_changed = true;
                }
                parent_id = parent->parent_id;
            }
            if (hierarchy_changed)
                emit layer_structure_changed();
            emit layer_clicked(hit_child->id);
            if (hierarchy_changed)
                emit layer_geometry_changed();
            if (hit_child->type == LayerType::Text)
                begin_text_edit(hit_child);
            ev->accept();
            return;
        }

        auto text_layer = text_layer_at_view_pos(ev->pos());
        if (text_layer) {
            emit layer_clicked(text_layer->id);
            begin_text_edit(text_layer);
            ev->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(ev);
}

void CanvasPreview::mousePressEvent(QMouseEvent *ev)
{
    if (!title_) return;

    if (begin_editor_3d_navigation(ev))
        return;

    if (ev->button() == Qt::LeftButton && !extension_canvas_handles_.isEmpty()) {
        const auto handle_layer = selected_layer();
        for (int i = extension_canvas_handles_.size() - 1; i >= 0; --i) {
            const QJsonObject handle = extension_canvas_handles_.at(i).toObject();
            const QJsonArray point = handle.value(QStringLiteral("value")).toArray();
            if (point.size() < 2) continue;
            QPointF canvas_point;
            if (handle.value(QStringLiteral("space")).toString() == QStringLiteral("layer") &&
                handle_layer) {
                const QRectF rect = layer_local_rect(*handle_layer);
                const QPointF local(rect.left() + point.at(0).toDouble() * rect.width(),
                                    rect.top() + point.at(1).toDouble() * rect.height());
                canvas_point = layer_to_canvas(*handle_layer, local);
            } else {
                canvas_point = QPointF(point.at(0).toDouble() * title_->width,
                                       point.at(1).toDouble() * title_->height);
            }
            const QPointF viewPoint = canvas_to_view(canvas_point);
            if (QLineF(viewPoint, ev->pos()).length() <= 14.0) {
                active_extension_handle_ = i;
                drag_mode_ = DragMode::ExtensionPoint;
                setCursor(Qt::SizeAllCursor);
                ev->accept();
                return;
            }
        }
    }

    if (!inline_text_layer_id_.empty() && !inline_text_suspended_for_gradient_) {
        if (ev->button() == Qt::LeftButton &&
            inline_text_view_point_hits_document(ev->position()) &&
            set_inline_text_cursor_from_view(
                ev->position(),
                ev->modifiers().testFlag(Qt::ShiftModifier))) {
            inline_text_mouse_selecting_ = true;
            ev->accept();
            return;
        }
        commit_text_edit(true);
        ev->accept();
        return;
    }

    if (begin_3d_gizmo_drag(ev))
        return;

    /*
     * Gradient handles are an editor overlay, not normal layer geometry.
     * When the Gradient tab is open (or the Gradient tool is active), the
     * handles are visible and must get first chance at the mouse press.
     *
     * This is intentionally before setFocus(), guide hit-testing, selection,
     * marquee, and normal transform handles. Otherwise a click on a gradient
     * handle can be interpreted as a dock/panel focus change or a normal canvas
     * selection operation, which makes the Gradient tab lose the interaction
     * and feels like the user is dragging the window instead of the handle.
     */
    if (ev->button() == Qt::LeftButton && gradient_handles_visible()) {
        if (auto layer = selected_layer()) {
            GradientHandleTarget gradient_target;
            DragMode gradient_hit = hit_test_gradient_handles(*layer, ev->pos(), &gradient_target);
            if (gradient_hit != DragMode::None) {
                drag_mode_ = gradient_hit;
                drag_changed_ = false;
                drag_start_view_ = ev->pos();
                drag_current_view_ = ev->pos();
                drag_start_canvas_ = view_to_canvas(ev->pos());
                drag_start_selection_bounds_ = selected_canvas_bounds();
                drag_layer_states_.clear();
                gradient_drag_ = GradientDragState{};
                corner_radius_drag_ = CornerRadiusDragState{};
                begin_gradient_drag(*layer, gradient_target);
                invalidate_canvas_overlay_caches();
                update();
                if (drag_mode_ == DragMode::GradientCenter || drag_mode_ == DragMode::GradientFocal)
                    setCursor(Qt::CrossCursor);
                else
                    setCursor(Qt::SizeAllCursor);
                ev->accept();
                return;
            }
        }
    }

    setFocus(Qt::MouseFocusReason);

    if (ev->button() == Qt::LeftButton) {
        bool vertical_guide = true;
        if (ruler_hit_test(ev->pos(), vertical_guide)) {
            dragging_new_guide_ = true;
            dragging_guide_x_axis_ = vertical_guide;
            dragging_guide_index_ = -1;
            const QPointF canvas_pt = view_to_canvas(ev->pos());
            dragging_guide_value_ = vertical_guide ? canvas_pt.x() : canvas_pt.y();
            drag_mode_ = vertical_guide ? DragMode::GuideX : DragMode::GuideY;
            setCursor(vertical_guide ? Qt::SplitHCursor : Qt::SplitVCursor);
            update();
            ev->accept();
            return;
        }
        bool x_axis = true;
        const int guide_index = guide_hit_test(ev->pos(), x_axis);
        if (guide_index >= 0) {
            dragging_new_guide_ = false;
            dragging_guide_x_axis_ = x_axis;
            dragging_guide_index_ = guide_index;
            dragging_guide_value_ = x_axis ? vertical_guides_[(size_t)guide_index] : horizontal_guides_[(size_t)guide_index];
            drag_mode_ = x_axis ? DragMode::GuideX : DragMode::GuideY;
            setCursor(x_axis ? Qt::SplitHCursor : Qt::SplitVCursor);
            update();
            ev->accept();
            return;
        }
    }

    if (ev->button() == Qt::MiddleButton) {
        panning_ = true;
        pan_start_view_ = QPointF(ev->pos());
        pan_start_offset_ = pan_offset_;
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Pen && pen_path_active_ &&
        ev->button() == Qt::RightButton) {
        undo_pen_path_point();
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::ColorPicker) {
        if (ev->button() != Qt::LeftButton && ev->button() != Qt::RightButton)
            return;
        clear_draw_tool_snap_cursor();
        update_color_picker_tooltip(ev->pos());
        if (color_picker_tooltip_visible_)
            emit color_picked(color_picker_tooltip_color_, ev->button() == Qt::LeftButton);
        ev->accept();
        return;
    }

    if (ev->button() != Qt::LeftButton) return;

    if (active_tool_ == CanvasTool::Pen) {
        if (pen_path_active_ && pen_path_points_.size() >= 2 &&
            ev->modifiers().testFlag(Qt::ControlModifier)) {
            const QPointF first_view = canvas_to_view(QPointF(pen_path_points_.front().x,
                                                              pen_path_points_.front().y));
            if (QLineF(first_view, ev->pos()).length() > 8.0) {
                finish_pen_path(false);
                ev->accept();
                return;
            }
        }
        const bool allow_snap = !ev->modifiers().testFlag(Qt::ControlModifier);
        QPointF anchor = snap_canvas_point(view_to_canvas(ev->pos()), true, true, allow_snap);
        if (pen_path_active_ && !pen_path_points_.empty() && ev->modifiers().testFlag(Qt::ShiftModifier)) {
            const QPointF previous(pen_path_points_.back().x, pen_path_points_.back().y);
            anchor = constrain_path_direction(previous, anchor, ev->modifiers());
        }
        if (pen_path_active_ && pen_path_points_.size() >= 2) {
            const QPointF first_view = canvas_to_view(QPointF(pen_path_points_.front().x,
                                                              pen_path_points_.front().y));
            if (QLineF(first_view, ev->pos()).length() <= 8.0) {
                pen_close_pending_ = true;
                pen_drag_point_index_ = 0;
                pen_press_canvas_ = QPointF(pen_path_points_.front().x,
                                            pen_path_points_.front().y);
                pen_last_drag_canvas_ = pen_press_canvas_;
                pen_alt_break_active_ = false;
                pen_ctrl_unequal_active_ = false;
                pen_space_reposition_ = false;
                shape_draw_current_canvas_ = pen_press_canvas_;
                update();
                ev->accept();
                return;
            }
        }

        BezierPathPoint point;
        point.x = point.in_x = point.out_x = anchor.x();
        point.y = point.in_y = point.out_y = anchor.y();
        pen_redo_points_.clear();
        pen_path_points_.push_back(point);
        pen_path_active_ = true;
        pen_close_pending_ = false;
        pen_drag_point_index_ = (int)pen_path_points_.size() - 1;
        pen_press_canvas_ = anchor;
        pen_last_drag_canvas_ = anchor;
        pen_alt_break_active_ = false;
        pen_ctrl_unequal_active_ = false;
        pen_space_reposition_ = false;
        shape_draw_current_canvas_ = anchor;
        snap_cursor_visible_ = !snap_feedback_.empty();
        snap_cursor_canvas_ = anchor;
        update();
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Selection &&
        begin_position_tangent_drag(ev->pos(), ev->modifiers())) {
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Selection &&
        begin_position_vertex_drag(ev->pos())) {
        ev->accept();
        return;
    }

    if ((active_tool_ == CanvasTool::Selection ||
         active_tool_ == CanvasTool::DirectSelection ||
         active_tool_ == CanvasTool::Shape)) {
        int corner_index = -1;
        auto corner_layer = hit_test_selected_corner_layer(ev->pos(), corner_index);
        if (corner_layer && corner_index >= 0) {
            if (ev->modifiers().testFlag(Qt::AltModifier)) {
                const double current = corner_layer->corner_bevel_roundness;
                const double next = current > 50.0 ? -100.0 : (current < -50.0 ? 0.0 : 100.0);
                set_corner_control_bevel_roundness(next);
                ev->accept();
                return;
            }
            drag_mode_ = DragMode::CornerRadius;
            drag_changed_ = false;
            drag_start_view_ = ev->pos();
            drag_current_view_ = ev->pos();
            drag_start_canvas_ = view_to_canvas(ev->pos());
            drag_start_selection_bounds_ = selected_canvas_bounds();
            drag_layer_states_.clear();
            gradient_drag_ = GradientDragState{};
            corner_radius_drag_ = CornerRadiusDragState{};
            begin_corner_radius_drag(*corner_layer);
            setCursor(Qt::SizeAllCursor);
            ev->accept();
            return;
        }
    }

    /* A click away from a live-corner widget returns the widgets to their
     * neutral state. This keeps the next direct drag linked, matching the
     * Illustrator interaction model after an isolated corner operation. */
    clear_corner_selection();

    if (active_tool_ == CanvasTool::DirectSelection) {
        begin_direct_selection(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    if (active_tool_ == CanvasTool::Gradient) {
        if (begin_gradient_tool_drag(ev->pos(), ev->modifiers())) {
            ev->accept();
            return;
        }
    }

    bool begin_draw_tool_layer_manipulation = false;
    if ((active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Text ||
         active_tool_ == CanvasTool::Image) && active_draw_tool_can_manipulate_selected()) {
        drag_mode_ = hit_test_selected(ev->pos());

        // With the Shape or Image tool active, dragging the body of a matching
        // existing layer remains reserved for drawing a new box. Explicit handles
        // still manipulate the selected layer; Text keeps body-move behavior.
        if ((active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Image) &&
            drag_mode_ == DragMode::Move)
            drag_mode_ = DragMode::None;

        if (drag_mode_ != DragMode::None) {
            begin_draw_tool_layer_manipulation = true;
            clear_draw_tool_snap_cursor();
        }
    }

    if (!begin_draw_tool_layer_manipulation && active_tool_ == CanvasTool::Text) {
        if (auto layer = text_layer_at_view_pos(ev->pos())) {
            emit layer_clicked(layer->id);
            begin_text_edit(layer);
            ev->accept();
            return;
        }
    }

    if (!begin_draw_tool_layer_manipulation &&
        (active_tool_ == CanvasTool::Shape || active_tool_ == CanvasTool::Text ||
         active_tool_ == CanvasTool::Image)) {
        drawing_shape_ = true;
        drawing_shape_changed_ = false;
        drag_mode_ = DragMode::None;
        selected_layer_ids_.clear();
        sel_layer_id_.clear();
        invalidate_canvas_overlay_caches();
        drag_current_view_ = ev->pos();
        shape_draw_start_canvas_ = snap_canvas_point(view_to_canvas(ev->pos()), true, true,
                                                     !ev->modifiers().testFlag(Qt::ControlModifier));
        shape_draw_current_canvas_ = shape_draw_start_canvas_;
        snap_cursor_visible_ = !snap_feedback_.empty();
        snap_cursor_canvas_ = shape_draw_start_canvas_;
        final_snap_cursor_visible_ = false;
        shape_draw_modifiers_ = ev->modifiers();
        shape_draw_current_rect_ = toolbar_draw_rect(shape_draw_current_canvas_, shape_draw_modifiers_);
        last_toolbar_preview_update_rect_ = toolbar_preview_update_rect();
        update(last_toolbar_preview_update_rect_.isEmpty() ? rect() : last_toolbar_preview_update_rect_);
        ev->accept();
        return;
    }

    if (!begin_draw_tool_layer_manipulation) {
        drag_mode_ = hit_test_selected(ev->pos());

        /* Transform handles always keep priority, but a selected layer body must
         * not hide a visually higher layer under the pointer. Resolve the
         * topmost selectable target first; only keep Move when that target is
         * already part of the current selection. */
        if (drag_mode_ == DragMode::Move) {
            std::string topmost_target_id;
            if (auto selection_target = layer_at_view_pos(ev->pos()))
                topmost_target_id = selection_target->id;
            if (!topmost_target_id.empty() &&
                std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), topmost_target_id) == selected_layer_ids_.end())
                drag_mode_ = DragMode::None;
        }
    }
    if (drag_mode_ == DragMode::None) {
        if (auto selection_target = layer_at_view_pos(ev->pos())) {
            const std::string target_id = selection_target->id;
            std::vector<std::string> next_ids;
            if (ev->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
                next_ids = selected_layer_ids_;
            auto existing = std::find(next_ids.begin(), next_ids.end(), target_id);
            if (ev->modifiers() & Qt::ControlModifier) {
                if (existing == next_ids.end()) next_ids.push_back(target_id);
                else next_ids.erase(existing);
            } else if (existing == next_ids.end()) {
                if (!(ev->modifiers() & Qt::ShiftModifier)) next_ids.clear();
                next_ids.push_back(target_id);
            }
            emit layers_selected(next_ids);
            selected_layer_ids_ = next_ids;
            sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
            invalidate_canvas_overlay_caches();
            drag_mode_ = DragMode::Move;
        }
    }

    if (drag_mode_ == DragMode::None) {
        begin_marquee(ev->pos(), ev->modifiers());
        ev->accept();
        return;
    }

    drag_changed_ = false;
    alt_duplicate_pending_ = (drag_mode_ == DragMode::Move) && ev->modifiers().testFlag(Qt::AltModifier);
    alt_duplicate_done_ = false;
    alt_duplicate_active_ = false;
    drag_start_view_ = ev->pos();
    drag_current_view_ = ev->pos();
    drag_start_canvas_ = view_to_canvas(ev->pos());
    drag_layer_states_.clear();
    drag_child_layer_states_.clear();
    gradient_drag_ = GradientDragState{};
    corner_radius_drag_ = CornerRadiusDragState{};
    drag_start_selection_bounds_ = selected_canvas_bounds();

    auto layers = selected_layers();
    drag_text_object_scaling_ = false;
    auto layer = selected_layer();
    if (!layer && !layers.empty()) layer = layers.front();
    if (!layer) return;

    auto is_resize_drag = [](DragMode mode) {
        return mode == DragMode::ResizeNW || mode == DragMode::ResizeN || mode == DragMode::ResizeNE ||
               mode == DragMode::ResizeE || mode == DragMode::ResizeSE || mode == DragMode::ResizeS ||
               mode == DragMode::ResizeSW || mode == DragMode::ResizeW;
    };
    drag_text_object_scaling_ = is_resize_drag(drag_mode_) && ev->modifiers().testFlag(Qt::AltModifier) &&
        std::any_of(layers.begin(), layers.end(), [](const std::shared_ptr<Layer> &selected) {
            return selected && is_canvas_text_layer(*selected);
        });

    for (const auto &selected : layers) {
        if (!selected || selected->locked) continue;
        double lt = std::clamp(playhead_ - selected->in_time, 0.0,
                               std::max(0.0, selected->out_time - selected->in_time));
        const QRectF selected_local_bounds = layer_local_rect(*selected);
        const float selected_width = layer_type_is_container(selected->type)
            ? static_cast<float>(selected_local_bounds.width())
            : static_cast<float>(eval_box_width(*selected, lt));
        const float selected_height = layer_type_is_container(selected->type)
            ? static_cast<float>(selected_local_bounds.height())
            : static_cast<float>(eval_box_height(*selected, lt));
        const QPointF selected_local_position =
            editor_evaluated_local_position_xy(*selected, lt);
        drag_layer_states_.push_back({selected->id,
                                      {},
                                      selected_local_position.x(),
                                      selected_local_position.y(),
                                      selected_width,
                                      selected_height,
                                      selected_local_bounds.left(),
                                      selected_local_bounds.top(),
                                      selected->scale.evaluate(lt).x,
                                      selected->scale.evaluate(lt).y,
                                      selected->rotation.evaluate(lt),
                                      selected->stroke_width,
                                      selected->corner_radius_tl,
                                      selected->corner_radius_tr,
                                      selected->corner_radius_br,
                                      selected->corner_radius_bl,
                                      selected->shape_roundness,
                                      selected->shape_inner_roundness,
                                      capture_path_corner_radii(*selected)});
        auto &saved = drag_layer_states_.back();
        const Vec2Value qtl = editor_quad_value(*selected, selected->transform_quad_tl, selected->transform_quad_tl_x, selected->transform_quad_tl_y, playhead_);
        const Vec2Value qtr = editor_quad_value(*selected, selected->transform_quad_tr, selected->transform_quad_tr_x, selected->transform_quad_tr_y, playhead_);
        const Vec2Value qbr = editor_quad_value(*selected, selected->transform_quad_br, selected->transform_quad_br_x, selected->transform_quad_br_y, playhead_);
        const Vec2Value qbl = editor_quad_value(*selected, selected->transform_quad_bl, selected->transform_quad_bl_x, selected->transform_quad_bl_y, playhead_);
        saved.quad[0] = qtl.x; saved.quad[1] = qtl.y; saved.quad[2] = qtr.x; saved.quad[3] = qtr.y;
        saved.quad[4] = qbr.x; saved.quad[5] = qbr.y; saved.quad[6] = qbl.x; saved.quad[7] = qbl.y;
    }

    auto selected_contains = [&](const std::string &id) {
        return std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), id) != selected_layer_ids_.end();
    };
    if (is_resize_drag(drag_mode_)) {
        for (const auto &candidate : title_->layers) {
            if (!candidate || candidate->locked || selected_contains(candidate->id))
                continue;
            std::string resize_root_id;
            for (const auto &selected : layers) {
                if (selected && editor_layer_has_ancestor(title_, *candidate, selected->id)) {
                    resize_root_id = selected->id;
                    break;
                }
            }
            if (resize_root_id.empty())
                continue;
            double child_lt = std::clamp(playhead_ - candidate->in_time, 0.0,
                                         std::max(0.0, candidate->out_time - candidate->in_time));
            const QRectF child_local_bounds = layer_local_rect(*candidate);
            const QPointF child_local_position =
                editor_evaluated_local_position_xy(*candidate, child_lt);
            drag_child_layer_states_.push_back({candidate->id,
                                                resize_root_id,
                                                child_local_position.x(),
                                                child_local_position.y(),
                                                (float)eval_box_width(*candidate, child_lt),
                                                (float)eval_box_height(*candidate, child_lt),
                                                child_local_bounds.left(),
                                                child_local_bounds.top(),
                                                candidate->scale.evaluate(child_lt).x,
                                                candidate->scale.evaluate(child_lt).y,
                                                candidate->rotation.evaluate(child_lt),
                                                candidate->stroke_width,
                                                candidate->corner_radius_tl,
                                                candidate->corner_radius_tr,
                                                candidate->corner_radius_br,
                                                candidate->corner_radius_bl,
                                                candidate->shape_roundness,
                                                candidate->shape_inner_roundness,
                                                capture_path_corner_radii(*candidate)});
        }
    }
