static void render_layer_text(cairo_t *cr, const Title &title, const Layer &layer, double title_time,
                               int canvas_w, int canvas_h)
{
    const double t = std::max(0.0, title_time - layer.in_time);
    const bool use_unified_text_animator =
        text_animator_stack_has_enabled_animators(layer.text_animators);
    double alpha = layer_chain_opacity(title, layer, title_time);
    double box_w = eval_box_width(layer, t);
    double box_h = eval_box_height(layer, t);
    if (box_w <= 0.0 || box_h <= 0.0) return;

    QFont font = font_for_layer(layer, t);
    ShadowRenderParams shadow_params = evaluated_shadow_params(layer, t);
    const bool render_shadow = shadow_params.drop_enabled || shadow_params.long_enabled;
    int base_pad = render_shadow
        ? (int)std::ceil(std::max({std::abs(shadow_params.dx), std::abs(shadow_params.dy), shadow_params.long_length}) + shadow_params.blur * 3.0 + shadow_params.spread + 4.0)
        : 0;
    // Reserve the complete painted-ink envelope, not only logical advances.
    // This includes italic bearings, accents, swashes, strokes and the
    // transparent interpolation gutter needed by per-unit transforms.
    base_pad = std::max(
        base_pad, (int)std::ceil(text_raster_ink_gutter(font, layer, t)));
    if (eval_background_enabled(layer, t))
        base_pad += (int)std::ceil(std::max({std::abs(eval_background_padding_left(layer, t)),
                                             std::abs(eval_background_padding_right(layer, t)),
                                             std::abs(eval_background_padding_top(layer, t)),
                                             std::abs(eval_background_padding_bottom(layer, t)),
                                             eval_background_stroke_width(layer, t)}));

    int pad_left = base_pad;
    int pad_right = base_pad;
    int pad_top = base_pad;
    int pad_bottom = base_pad;
    if (use_unified_text_animator) {
        /* The compatibility raster needs the same visual envelope as the SDF
         * path. Bound malicious/extreme values to the maximum surface that can
         * still intersect the current canvas. */
        const double requested_animator_padding =
            text_animator_stack_visual_padding(layer.text_animators, box_w, box_h);
        const double visible_envelope = std::max(
            std::max(1.0, static_cast<double>(canvas_w)) + box_w,
            std::max(1.0, static_cast<double>(canvas_h)) + box_h);
        const int animator_padding = static_cast<int>(std::ceil(std::min(
            std::max(0.0, requested_animator_padding), visible_envelope)));
        pad_left += animator_padding;
        pad_right += animator_padding;
        pad_top += animator_padding;
        pad_bottom += animator_padding;
    }

    const qint64 requested_width = std::max<qint64>(1, (qint64)std::ceil(box_w) + pad_left + pad_right);
    const qint64 requested_height = std::max<qint64>(1, (qint64)std::ceil(box_h) + pad_top + pad_bottom);
    if (requested_width > std::numeric_limits<int>::max() ||
        requested_height > std::numeric_limits<int>::max())
        return;
    int img_w = static_cast<int>(requested_width);
    int img_h = static_cast<int>(requested_height);
    QImage text_image(img_w, img_h, QImage::Format_ARGB32_Premultiplied);
    text_image.fill(Qt::transparent);

    QPainter painter(&text_image);
    const bool previous_shape_aa = painter.testRenderHint(QPainter::Antialiasing);
    const bool previous_text_aa = painter.testRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.setFont(font);

    QRectF base_rect(pad_left, pad_top, box_w, box_h);
    if (eval_background_enabled(layer, t)) {
        const auto *background_effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
        const bool background_gradient = background_effect && background_effect->effect_fill_type == 1;
        const double bg_left = eval_background_padding_left(layer, t);
        const double bg_right = eval_background_padding_right(layer, t);
        const double bg_top = eval_background_padding_top(layer, t);
        const double bg_bottom = eval_background_padding_bottom(layer, t);
        QRectF bg_rect = base_rect.adjusted(-bg_left, -bg_top, bg_right, bg_bottom);
        if (bg_rect.width() <= 0.0 || bg_rect.height() <= 0.0) bg_rect = base_rect;
        QColor bg = evaluated_background_color(layer, t);
        if (bg.alpha() > 0 || background_gradient || eval_background_stroke_width(layer, t) > 0.0) {
            QPainterPath bg_path = painter_rounded_rect_corners(bg_rect,
                eval_background_corner_radius_tl(layer, t), eval_background_corner_radius_tr(layer, t),
                eval_background_corner_radius_br(layer, t), eval_background_corner_radius_bl(layer, t),
                legacy_corner_type_roundness((CornerType)(background_effect ? background_effect->effect_corner_type : 0)));
            painter.setPen(Qt::NoPen);
            painter.setBrush(background_gradient ? background_gradient_fill_brush(layer, bg_rect, eval_background_opacity(layer, t)) : QBrush(bg));
            if (bg.alpha() > 0 || background_gradient)
                painter.fillPath(bg_path, painter.brush());
            const double stroke_w = eval_background_stroke_width(layer, t);
            if (stroke_w > 0.0) {
                QColor stroke = color_from_argb(eval_background_stroke_color(layer, t));
                stroke.setAlphaF(std::clamp(stroke.alphaF() * eval_background_stroke_opacity(layer, t) * alpha, 0.0, 1.0));
                if (stroke.alpha() > 0) {
                    QPen pen(stroke, stroke_w);
                    pen.setJoinStyle(Qt::MiterJoin);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(pen);
                    painter.drawPath(apply_layer_trim_paths(layer, t, bg_path));
                }
            }
        }
    }

    // The isolated per-unit compositor needs a copy of the non-text content,
    // but a full QImage::copy() on every rendered text layer is prohibitively
    // expensive during cue changes and prerendering. Keep the normal path
    // allocation-free and take the snapshot only while a rich-text transition
    // is actually active.
    QImage static_text_background;
    /* Keep the background and animated text on separate surfaces. Besides
     * avoiding background rectangles travelling with glyphs in the bounded
     * flattened fallback, this gives color-font/emoji fallback exactly the
     * same compositing contract as the SDF path. */
    if (use_unified_text_animator) {
        if (eval_background_enabled(layer, t))
            static_text_background = text_image.copy();
        painter.end();
        text_image.fill(Qt::transparent);
        painter.begin(&text_image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
    }

    QRectF text_rect = text_rect_for_style(base_rect, layer);
    QString text = display_text_for_style(layer);
    const bool has_rich_text = layer.type != LayerType::Ticker;
    const ExactRichTextRaster exact_rich_text = has_rich_text
        ? exact_rich_text_raster_for_layer(layer, text_rect, t)
        : ExactRichTextRaster{};
    painter.save();
    const double text_ink_clip_pad = text_raster_ink_gutter(font, layer, t);
    painter.setClipRect(text_rect.adjusted(-text_ink_clip_pad, -text_ink_clip_pad,
                                           text_ink_clip_pad, text_ink_clip_pad));
    Qt::Alignment align = Qt::AlignVCenter | Qt::AlignHCenter;
    if (layer.align_h == 0) align = (align & ~Qt::AlignHorizontal_Mask) | Qt::AlignLeft;
    if (layer.align_h == 2) align = (align & ~Qt::AlignHorizontal_Mask) | Qt::AlignRight;
    if (layer.align_v == 0 || layer.align_v == 3) align = (align & ~Qt::AlignVertical_Mask) | Qt::AlignTop;
    if (layer.align_v == 2) align = (align & ~Qt::AlignVertical_Mask) | Qt::AlignBottom;
    QPainterPath text_path;
    if (!has_rich_text) {
        text_path = layer.type == LayerType::Ticker
            ? ticker_text_path(font, text_rect, align, text, layer, title.id,
                               title.current_cue_row >= 0 || title.pending_cue_row >= 0, t)
            : text_overflow_path(font, text_rect, align, text, layer, t);
        text_path = apply_vertical_character_scale(text_path, text_rect, align, layer, t);
        if (std::abs(eval_baseline_shift(layer, t)) > 0.0001)
            text_path.translate(0.0, -eval_baseline_shift(layer, t));
    }

    if (render_shadow) {
        QImage shadow_mask(img_w, img_h, QImage::Format_ARGB32_Premultiplied);
        shadow_mask.fill(Qt::transparent);
        QPainter mask_painter(&shadow_mask);
        mask_painter.setRenderHint(QPainter::Antialiasing, true);
        mask_painter.setPen(Qt::NoPen);
        mask_painter.setBrush(Qt::white);
        if (has_rich_text && exact_rich_text.valid)
            draw_exact_rich_text_fill(mask_painter, exact_rich_text,
                                      text_rect, true);
        else if (has_rich_text)
            draw_rich_text_document(mask_painter, layer, font, text_rect, t,
                                    nullptr, false, true);
        else
            mask_painter.drawPath(text_path);
        mask_painter.end();
        const QString shape_key = QStringLiteral("text|%1x%2|%3")
            .arg(img_w).arg(img_h).arg(image_content_hash(shadow_mask));
        CachedShadowImage shadow = build_shadow_image(layer, shadow_params, shape_key, shadow_mask);
        painter.save();
        painter.setClipping(false);
        painter.drawImage(shadow.origin, shadow.image);
        painter.restore();
    }

    double outline_width = eval_outline_width(layer, t);
    QColor outline = color_from_argb(eval_outline_color(layer, t));
    outline.setAlphaF(std::clamp((double)outline.alphaF() *
                                 eval_outline_opacity(layer, t),
                                 0.0, 1.0));
    QColor fill = color_from_argb(eval_text_color(layer, t));
    fill.setAlphaF(std::clamp((double)fill.alphaF(), 0.0, 1.0));

    std::unique_ptr<QTextDocument> rich_document;
    RichTextDocument rich_stroke_model;
    std::vector<RichTextStrokeRenderGroup> rich_stroke_groups;
    if (has_rich_text && !exact_rich_text.valid) {
        rich_document = rich_text_document_for_layer(
            layer, font, text_rect, t, nullptr);
        rich_stroke_model = rich_text_model_for_source_time(layer, t);
        rich_stroke_groups =
            rich_text_stroke_render_groups(rich_stroke_model);
    }

    auto draw_text_fill = [&]() {
        if (has_rich_text && exact_rich_text.valid) {
            draw_exact_rich_text_fill(painter, exact_rich_text, text_rect);
            return;
        }
        if (has_rich_text && rich_document) {
            std::unique_ptr<QTextDocument> fill_document =
                rich_text_fill_only_document(*rich_document);
            painter.save();
            painter.setOpacity(fill.alphaF());
            draw_positioned_rich_text_document(
                painter, *fill_document, layer, text_rect, t);
            painter.restore();
            return;
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(layer.fill_type == 1
                             ? gradient_fill_brush(layer, text_rect)
                             : QBrush(fill));
        painter.drawPath(text_path);
    };

    const std::vector<QPainterPath> exact_stroke_paths =
        has_rich_text && exact_rich_text.valid
            ? exact_rich_text_stroke_paths(exact_rich_text, layer, t)
            : std::vector<QPainterPath>{};
    auto draw_rich_stroke_phase = [&](bool on_front) {
        if (has_rich_text && exact_rich_text.valid) {
            draw_exact_rich_text_strokes(
                painter, exact_rich_text, text_rect, layer, t, on_front,
                0, std::numeric_limits<size_t>::max(), QPointF(),
                &exact_stroke_paths);
            return;
        }
        if (!has_rich_text || !rich_document)
            return;
        for (const RichTextStrokeRenderGroup &group : rich_stroke_groups) {
            if (group.on_front != on_front)
                continue;
            const QImage stroke_layer = render_rich_text_stroke_group(
                *rich_document, rich_stroke_model, layer, text_rect, t,
                QSize(img_w, img_h), QPointF(0.0, 0.0), group);
            if (!stroke_layer.isNull())
                painter.drawImage(QPointF(0.0, 0.0), stroke_layer);
        }
    };

    auto draw_plain_text_outline = [&]() {
        if (outline_width <= 0.0) return;
        if (layer.stroke_fill_type == 1 && outline.alpha() <= 0) return;

        const int alignment = eval_outline_alignment(layer, t);
        const qreal painted_width = alignment == 1
            ? outline_width : outline_width * 2.0;
        const QBrush stroke_brush = layer.stroke_fill_type == 2
            ? stroke_gradient_fill_brush(
                  layer, text_rect, eval_outline_opacity(layer, t))
            : QBrush(outline);
        QPen outline_pen(stroke_brush, painted_width, Qt::SolidLine,
                         Qt::RoundCap, outline_pen_join_style(layer));

        QImage stroke_layer(img_w, img_h,
                            QImage::Format_ARGB32_Premultiplied);
        stroke_layer.fill(Qt::transparent);
        QPainter stroke_painter(&stroke_layer);
        stroke_painter.setRenderHint(
            QPainter::Antialiasing, eval_outline_antialias(layer, t));
        stroke_painter.setRenderHint(QPainter::TextAntialiasing, true);
        stroke_painter.setClipRect(text_rect.adjusted(
            -text_ink_clip_pad, -text_ink_clip_pad,
            text_ink_clip_pad, text_ink_clip_pad));
        stroke_painter.setPen(outline_pen);
        stroke_painter.setBrush(Qt::NoBrush);
        stroke_painter.drawPath(apply_layer_trim_paths(layer, t, text_path));
        stroke_painter.end();

        if (alignment != 1) {
            QImage glyph_mask(img_w, img_h,
                              QImage::Format_ARGB32_Premultiplied);
            glyph_mask.fill(Qt::transparent);
            QPainter mask_painter(&glyph_mask);
            mask_painter.setRenderHint(QPainter::Antialiasing, true);
            mask_painter.setRenderHint(QPainter::TextAntialiasing, true);
            mask_painter.setPen(Qt::NoPen);
            mask_painter.setBrush(Qt::white);
            mask_painter.drawPath(text_path);
            mask_painter.end();

            QPainter isolate(&stroke_layer);
            isolate.setCompositionMode(
                alignment == 0
                    ? QPainter::CompositionMode_DestinationOut
                    : QPainter::CompositionMode_DestinationIn);
            isolate.drawImage(QPointF(0.0, 0.0), glyph_mask);
            isolate.end();
        }
        painter.drawImage(QPointF(0.0, 0.0), stroke_layer);
    };

    if (has_rich_text) {
        draw_rich_stroke_phase(false);
        draw_text_fill();
        draw_rich_stroke_phase(true);
    } else {
        if (!eval_outline_on_front(layer, t))
            draw_plain_text_outline();
        draw_text_fill();
        if (eval_outline_on_front(layer, t))
            draw_plain_text_outline();
    }
    painter.setRenderHint(QPainter::TextAntialiasing, previous_text_aa);
    painter.setRenderHint(QPainter::Antialiasing, previous_shape_aa);
    painter.restore();
    painter.end();

    if (use_unified_text_animator) {
        /* Always prefer the shaped isolated-unit compositor. It derives each
         * crop from actual painted alpha and keeps a transparent resampling
         * gutter, so glyph bearings cannot be cut by advance-based clips. The
         * flattened compositor remains only a bounded emergency fallback for
         * unusually large/unsupported layouts. Both consume the same
         * TextAnimatorEvaluation and neither evaluates LayerTransition. */
        bool applied = false;
        if (exact_rich_text.valid) {
            /* Build exact isolated glyph units from ImmutableTextLayout paths.
             * Cropping the complete precomposed bitmap through overlapping
             * cluster rectangles lets neighbouring pixels travel with the
             * wrong transition unit; rebuilding QTextDocuments would instead
             * reintroduce the old H/V Scale divergence. */
            applied = apply_unified_text_animator_raster(
                text_image, static_text_background, layer, text, font, text_rect,
                t, shadow_params, render_shadow, &exact_rich_text);
        } else {
            applied = apply_unified_text_animator_raster(
                text_image, static_text_background, layer, text, font, text_rect,
                t, shadow_params, render_shadow);
            if (!applied) {
                applied = apply_unified_text_animator_flattened(
                    text_image, static_text_background, layer, font, text_rect, t,
                    shadow_params, render_shadow);
            }
        }
        if (!applied && !static_text_background.isNull()) {
            /* Fail safe: unsupported/invalid shaping must not drop the text-box
             * background merely because its foreground was isolated for the
             * animator adapter. */
            QImage combined = static_text_background.copy();
            QPainter combined_painter(&combined);
            combined_painter.drawImage(QPointF(0.0, 0.0), text_image);
            combined_painter.end();
            text_image = std::move(combined);
        }
    }

    auto text_surface = make_image_surface_for_qimage(text_image);
    if (!text_surface) return;

    cairo_save(cr);
    apply_layer_world_transform(cr, title, layer, title_time);
    const double local_x = -eval_origin_x(layer, t) * box_w - pad_left;
    const double local_y = -eval_origin_y(layer, t) * box_h - pad_top;
    apply_layer_transition_clip(cr, layer, title_time, local_x, local_y, img_w, img_h);
    cairo_set_source_surface(cr, text_surface.get(), local_x, local_y);
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
}

static cairo_pattern_t *set_layer_outline_source(cairo_t *cr, const Layer &layer,
                                                 double x, double y, double w, double h,
                                                 double alpha, double t);

static void render_layer_rect(cairo_t *cr, const Title &title, const Layer &layer, double title_time)
{
    const double t = std::max(0.0, title_time - layer.in_time);
    double alpha = layer_chain_opacity(title, layer, title_time);

    double w = eval_box_width(layer, t);
    double h = eval_box_height(layer, t);
    if (w <= 0.0 || h <= 0.0) return;
    double x = -eval_origin_x(layer, t) * w;
    double y = -eval_origin_y(layer, t) * h;

    double fr, fg, fb, fa;
    unpack_color(eval_fill_color(layer, t), fr, fg, fb, fa);

    cairo_save(cr);
    apply_layer_world_transform(cr, title, layer, title_time);
    apply_layer_transition_clip(cr, layer, title_time, x, y, w, h);
    cairo_translate(cr, x, y);

    const QPainterPath shape_path = painter_layer_shape_path(layer, w, h);
    const QPainterPath stroke_shape_path = apply_layer_trim_paths(layer, t, shape_path);

    ShadowRenderParams shadow_params = evaluated_shadow_params(layer, t);
    if (shadow_params.drop_enabled || shadow_params.long_enabled) {
        const int mw = std::max(1, (int)std::ceil(w));
        const int mh = std::max(1, (int)std::ceil(h));
        QImage mask(mw, mh, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        QPainter mp(&mask);
        mp.setRenderHint(QPainter::Antialiasing, true);
        mp.setPen(Qt::NoPen);
        mp.setBrush(Qt::white);
        if (layer.shape_type == ShapeType::Line) {
            QPen pen(Qt::white, std::max(1.0, eval_outline_width(layer, t)), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            mp.setPen(pen);
            mp.setBrush(Qt::NoBrush);
        }
        mp.drawPath(layer.shape_type == ShapeType::Line ? stroke_shape_path : shape_path);
        mp.end();
        const QString shape_key = QStringLiteral("rect|%1|%2x%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
            .arg((int)layer.shape_type).arg(mw).arg(mh)
            .arg(layer.corner_radius_tl, 0, 'f', 2)
            .arg(layer.corner_radius_tr, 0, 'f', 2)
            .arg(layer.corner_radius_br, 0, 'f', 2)
            .arg(layer.corner_radius_bl, 0, 'f', 2)
            .arg(layer.corner_bevel_roundness, 0, 'f', 2)
            .arg(layer.shape_points).arg(layer.shape_sides)
            .arg(layer.shape_inner_radius, 0, 'f', 4)
            .arg(layer.shape_outer_radius, 0, 'f', 4)
            .arg(layer.shape_roundness, 0, 'f', 2)
            .arg(layer.shape_inner_roundness, 0, 'f', 2) + QStringLiteral("|") +
            fxm::path_geometry_signature(layer) + QStringLiteral("|") +
            image_content_hash(mask);
        CachedShadowImage shadow = build_shadow_image(layer, shadow_params, shape_key, mask);
        paint_qimage(cr, shadow.image, shadow.origin.x(), shadow.origin.y(), alpha);
    }

    double outline_width = eval_outline_width(layer, t);
    uint32_t outline_color = eval_outline_color(layer, t);
    bool has_outline = outline_width > 0.0 &&
                       (layer.stroke_fill_type == 2 || ((outline_color >> 24) & 0xFF) > 0);

    auto set_stroke_source = [&]() -> cairo_pattern_t * {
        return set_layer_outline_source(cr, layer, 0.0, 0.0, w, h, alpha, t);
    };

    auto stroke_outline = [&]() {
        const int alignment = eval_outline_alignment(layer, t);
        cairo_save(cr);
        cairo_set_antialias(cr, outline_cairo_antialias(layer));
        cairo_set_line_join(cr, outline_cairo_join_style(layer));

        if (stroke_shape_path.isEmpty()) {
            cairo_restore(cr);
            return;
        }
        if (alignment == 0) {
            // Outer stroke: render a double-width centered stroke into a group,
            // then remove the original object's interior from the group.
            cairo_push_group(cr);
            cairo_append_qpainter_path(cr, stroke_shape_path, false);
            cairo_set_line_width(cr, outline_width * 2.0);
            cairo_pattern_t *pattern = set_stroke_source();
            cairo_stroke(cr);
            if (pattern) cairo_pattern_destroy(pattern);
            cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
            cairo_append_qpainter_path(cr, shape_path, true);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
            cairo_fill(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            cairo_pop_group_to_source(cr);
            cairo_paint(cr);
        } else if (alignment == 2) {
            // Inner stroke: clip to the original object's interior.
            cairo_append_qpainter_path(cr, shape_path, true);
            cairo_clip(cr);
            cairo_append_qpainter_path(cr, stroke_shape_path, false);
            cairo_set_line_width(cr, outline_width * 2.0);
            cairo_pattern_t *pattern = set_stroke_source();
            cairo_stroke(cr);
            if (pattern) cairo_pattern_destroy(pattern);
        } else {
            // Mid stroke: Cairo's native centered stroke.
            cairo_append_qpainter_path(cr, stroke_shape_path, false);
            cairo_set_line_width(cr, outline_width);
            cairo_pattern_t *pattern = set_stroke_source();
            cairo_stroke(cr);
            if (pattern) cairo_pattern_destroy(pattern);
        }
        cairo_restore(cr);
    };

    if (has_outline && !eval_outline_on_front(layer, t))
        stroke_outline();

    cairo_add_layer_shape(cr, layer, w, h);
    cairo_pattern_t *gradient_pattern = nullptr;
    if (layer.fill_type == 1) {
        gradient_pattern = create_fill_gradient_pattern(layer, 0.0, 0.0, w, h, alpha);
        cairo_set_source(cr, gradient_pattern);
    } else {
        cairo_set_source_rgba(cr, fr, fg, fb, fa * alpha);
    }
    cairo_fill(cr);
    if (gradient_pattern) cairo_pattern_destroy(gradient_pattern);

    if (has_outline && eval_outline_on_front(layer, t))
        stroke_outline();
    cairo_restore(cr);
}

struct ImageBoxLayout {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

static ImageBoxLayout image_box_layout_for_layer(const Layer &layer, double box_w, double box_h,
                                                  double image_w, double image_h)
{
    ImageBoxLayout out;
    const fxm::ImageDisplaySize display = fxm::calculate_image_display_size(
        layer.image_box_mode, layer.image_size_auto_fit, box_w, box_h, image_w, image_h);
    out.w = display.width;
    out.h = display.height;
    if (out.w <= 0.0 || out.h <= 0.0)
        return out;
    const double ax = std::clamp((double)layer.image_anchor_x, 0.0, 1.0);
    const double ay = std::clamp((double)layer.image_anchor_y, 0.0, 1.0);
    out.x = (box_w - out.w) * ax;
    out.y = (box_h - out.h) * ay;
    return out;
}

static QRectF image_visible_rect_for_layout(const ImageBoxLayout &layout, double box_w, double box_h,
                                            bool crop_to_box)
{
    QRectF image_rect(layout.x, layout.y, layout.w, layout.h);
    if (!crop_to_box)
        return image_rect;
    return image_rect.intersected(QRectF(0.0, 0.0, box_w, box_h));
}

static QImage image_box_shadow_mask(const QImage &argb, const Layer &layer, double box_w, double box_h,
                                    const ImageBoxLayout &layout)
{
    const int mask_w = std::max(1, (int)std::ceil(box_w));
    const int mask_h = std::max(1, (int)std::ceil(box_h));
    QImage mask(mask_w, mask_h, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    QPainter painter(&mask);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, layer.scale_filter != ImageScaleFilter::Disable);
    const QRectF visible = image_visible_rect_for_layout(layout, box_w, box_h, layer.image_crop_when_outside_box);
    if (visible.isEmpty())
        return mask;
    painter.setClipPath(painter_rounded_rect_corners(visible, layer.corner_radius_tl, layer.corner_radius_tr,
                                                     layer.corner_radius_br, layer.corner_radius_bl,
                                                     layer.corner_bevel_roundness));
    painter.drawImage(QRectF(layout.x, layout.y, layout.w, layout.h), argb);
    return mask;
}

static bool layer_has_visible_outline(const Layer &layer, double t)
{
    const double outline_width = eval_outline_width(layer, t);
    const uint32_t outline_color = eval_outline_color(layer, t);
    return outline_width > 0.0 &&
           (layer.stroke_fill_type == 2 || ((outline_color >> 24) & 0xFF) > 0);
}

static cairo_pattern_t *set_layer_outline_source(cairo_t *cr, const Layer &layer,
                                                 double x, double y, double w, double h,
                                                 double alpha, double t)
{
    if (layer.stroke_fill_type == 2) {
        cairo_pattern_t *pattern = create_stroke_gradient_pattern(
            layer, x, y, w, h, alpha * eval_outline_opacity(layer, t));
        cairo_set_source(cr, pattern);
        return pattern;
    }

    double sr, sg, sb, sa;
    unpack_color(eval_outline_color(layer, t), sr, sg, sb, sa);
    cairo_set_source_rgba(cr, sr, sg, sb, sa * alpha * eval_outline_opacity(layer, t));
    return nullptr;
}

static void stroke_image_visible_outline(cairo_t *cr, const Layer &layer, const QRectF &visible,
                                         double box_x, double box_y, double alpha, double t)
{
    const double outline_width = eval_outline_width(layer, t);
    if (outline_width <= 0.0 || visible.isEmpty())
        return;

    auto add_path = [&]() {
        cairo_add_rounded_rect_corners(cr, box_x + visible.x(), box_y + visible.y(),
                                       visible.width(), visible.height(),
                                       layer.corner_radius_tl, layer.corner_radius_tr,
                                       layer.corner_radius_br, layer.corner_radius_bl,
                                       layer.corner_bevel_roundness);
    };

    const int alignment = eval_outline_alignment(layer, t);
    cairo_save(cr);
    cairo_set_antialias(cr, outline_cairo_antialias(layer));
    cairo_set_line_join(cr, outline_cairo_join_style(layer));

    if (alignment == 0) {
        cairo_push_group(cr);
        add_path();
        cairo_set_line_width(cr, outline_width * 2.0);
        cairo_pattern_t *pattern = set_layer_outline_source(cr, layer, visible.x(), visible.y(),
                                                            visible.width(), visible.height(), alpha, t);
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
        add_path();
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_pop_group_to_source(cr);
        cairo_paint(cr);
    } else if (alignment == 2) {
        add_path();
        cairo_clip(cr);
        add_path();
        cairo_set_line_width(cr, outline_width * 2.0);
        cairo_pattern_t *pattern = set_layer_outline_source(cr, layer, visible.x(), visible.y(),
                                                            visible.width(), visible.height(), alpha, t);
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
    } else {
        add_path();
        cairo_set_line_width(cr, outline_width);
        cairo_pattern_t *pattern = set_layer_outline_source(cr, layer, visible.x(), visible.y(),
                                                            visible.width(), visible.height(), alpha, t);
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
    }

    cairo_restore(cr);
}


static void render_layer_image_pixels(cairo_t *cr, const Title &title,
                                      const Layer &layer, double title_time,
                                      const QImage &source_image,
                                      const QString &shadow_identity)
{
    if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0)
        return;
    const double t = std::max(0.0, title_time - layer.in_time);
    const double alpha = layer_chain_opacity(title, layer, title_time);
    const double w = eval_box_width(layer, t);
    const double h = eval_box_height(layer, t);
    if (w <= 0.0 || h <= 0.0) return;

    double image_w = eval_image_width(layer, t);
    double image_h = eval_image_height(layer, t);
    if (image_w <= 0.0 || image_h <= 0.0) {
        image_w = source_image.width();
        image_h = source_image.height();
    }
    const ImageBoxLayout image_layout = image_box_layout_for_layer(
        layer, w, h, image_w, image_h);
    QImage argb = source_image.format() == QImage::Format_ARGB32_Premultiplied
        ? source_image
        : source_image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    auto img_surface = make_image_surface_for_const_qimage(argb);
    if (!img_surface) return;

    cairo_save(cr);
    apply_layer_world_transform(cr, title, layer, title_time);
    const double origin_x = eval_origin_x(layer, t);
    const double origin_y = eval_origin_y(layer, t);
    const double box_x = -origin_x * w;
    const double box_y = -origin_y * h;
    apply_layer_transition_clip(cr, layer, title_time, box_x, box_y, w, h);
    const ShadowRenderParams shadow_params = evaluated_shadow_params(layer, t);
    if (shadow_params.drop_enabled || shadow_params.long_enabled) {
        QImage mask = image_box_shadow_mask(argb, layer, w, h, image_layout);
        const QString shape_key = QStringLiteral("image-like|%1|%2x%3")
            .arg(shadow_identity).arg(mask.width()).arg(mask.height());
        const CachedShadowImage shadow = build_shadow_image(
            layer, shadow_params, shape_key, mask);
        paint_qimage(cr, shadow.image, box_x + shadow.origin.x(),
                     box_y + shadow.origin.y(), alpha);
    }
    if (eval_background_enabled(layer, t)) {
        const auto *background_effect = find_layer_effect(
            layer, LayerEffectType::BackgroundColor);
        const bool background_gradient = background_effect &&
            background_effect->effect_fill_type == 1;
        const double bg_left = eval_background_padding_left(layer, t);
        const double bg_right = eval_background_padding_right(layer, t);
        const double bg_top = eval_background_padding_top(layer, t);
        const double bg_bottom = eval_background_padding_bottom(layer, t);
        const QColor bg = evaluated_background_color(layer, t);
        const double br = bg.redF();
        const double bgc = bg.greenF();
        const double bb = bg.blueF();
        const double ba = bg.alphaF();
        const double stroke_w = eval_background_stroke_width(layer, t);
        if (ba > 0.0 || background_gradient || stroke_w > 0.0) {
            const double x = box_x - bg_left;
            const double y = box_y - bg_top;
            const double bw = std::max(1.0, w + bg_left + bg_right);
            const double bh = std::max(1.0, h + bg_top + bg_bottom);
            cairo_add_rounded_rect_corners(
                cr, x, y, bw, bh,
                eval_background_corner_radius_tl(layer, t),
                eval_background_corner_radius_tr(layer, t),
                eval_background_corner_radius_br(layer, t),
                eval_background_corner_radius_bl(layer, t),
                legacy_corner_type_roundness((CornerType)(
                    background_effect ? background_effect->effect_corner_type : 0)));
            cairo_pattern_t *gradient_pattern = nullptr;
            if (ba > 0.0 || background_gradient) {
                if (background_gradient) {
                    gradient_pattern = create_background_gradient_pattern(
                        layer, x, y, bw, bh,
                        alpha * eval_background_opacity(layer, t));
                    cairo_set_source(cr, gradient_pattern);
                } else {
                    cairo_set_source_rgba(cr, br, bgc, bb, ba * alpha);
                }
                cairo_fill_preserve(cr);
                if (gradient_pattern) cairo_pattern_destroy(gradient_pattern);
            }
            if (stroke_w > 0.0) {
                const QColor stroke = color_from_argb(
                    eval_background_stroke_color(layer, t));
                cairo_set_line_width(cr, stroke_w);
                cairo_set_source_rgba(
                    cr, stroke.redF(), stroke.greenF(), stroke.blueF(),
                    stroke.alphaF() * alpha *
                        eval_background_stroke_opacity(layer, t));
                cairo_stroke(cr);
            } else {
                cairo_new_path(cr);
            }
        }
    }
    const QRectF visible = image_visible_rect_for_layout(
        image_layout, w, h, layer.image_crop_when_outside_box);
    if (visible.isEmpty()) {
        cairo_restore(cr);
        return;
    }
    const bool has_outline = layer_has_visible_outline(layer, t);
    if (has_outline && !eval_outline_on_front(layer, t))
        stroke_image_visible_outline(cr, layer, visible, box_x, box_y, alpha, t);
    cairo_save(cr);
    cairo_add_rounded_rect_corners(
        cr, box_x + visible.x(), box_y + visible.y(),
        visible.width(), visible.height(),
        layer.corner_radius_tl, layer.corner_radius_tr,
        layer.corner_radius_br, layer.corner_radius_bl,
        layer.corner_bevel_roundness);
    cairo_clip(cr);
    cairo_translate(cr, box_x + image_layout.x, box_y + image_layout.y);
    cairo_scale(cr, image_layout.w / argb.width(), image_layout.h / argb.height());
    cairo_set_source_surface(cr, img_surface.get(), 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cr),
        cairo_filter_for_image_scale_filter(layer.scale_filter));
    cairo_paint_with_alpha(cr, alpha);
    cairo_restore(cr);
    if (has_outline && eval_outline_on_front(layer, t))
        stroke_image_visible_outline(cr, layer, visible, box_x, box_y, alpha, t);
    cairo_restore(cr);
}

static void render_layer_image(cairo_t *cr, const Title &title,
                               const Layer &layer, double title_time)
{
    const std::string effective_image_path = effective_external_string(
        layer, ExternalPropertyPaths::ImagePath, layer.image_path);
    if (effective_image_path.empty()) return;
    const double t = std::max(0.0, title_time - layer.in_time);
    const double w = eval_box_width(layer, t);
    const double h = eval_box_height(layer, t);
    if (w <= 0.0 || h <= 0.0) return;
    const QString image_path = QString::fromStdString(effective_image_path);
    double image_w = eval_image_width(layer, t);
    double image_h = eval_image_height(layer, t);
    if (image_w <= 0.0 || image_h <= 0.0) {
        QSize intrinsic_size = image_intrinsic_size(image_path);
        if (!intrinsic_size.isValid() || intrinsic_size.isEmpty())
            intrinsic_size = QSize(std::max(1, (int)std::ceil(w)),
                                   std::max(1, (int)std::ceil(h)));
        image_w = intrinsic_size.width();
        image_h = intrinsic_size.height();
    }
    const ImageBoxLayout layout = image_box_layout_for_layer(
        layer, w, h, image_w, image_h);
    const int max_sample_dim = std::clamp(
        std::max(title.width, title.height) * 2, 512, 4096);
    const QSize sample_size(
        std::clamp((int)std::ceil(layout.w), 1, max_sample_dim),
        std::clamp((int)std::ceil(layout.h), 1, max_sample_dim));
    const QImage image = load_cached_layer_image(image_path, sample_size);
    const QFileInfo info(image_path);
    const QString identity = QStringLiteral("%1|%2|%3")
        .arg(image_path)
        .arg(info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0)
        .arg(info.exists() ? info.size() : -1);
    render_layer_image_pixels(cr, title, layer, title_time, image, identity);
}

static thread_local fxm::video::VideoDecodeClient g_raster_video_decode_client =
    fxm::video::VideoDecodeClient::LiveOutput;

struct ScopedRasterVideoDecodeClient {
    fxm::video::VideoDecodeClient previous_client;
    explicit ScopedRasterVideoDecodeClient(fxm::video::VideoDecodeClient client)
        : previous_client(g_raster_video_decode_client)
    {
        g_raster_video_decode_client = client;
    }
    ~ScopedRasterVideoDecodeClient()
    {
        g_raster_video_decode_client = previous_client;
    }
};


static QImage make_video_loading_placeholder_raster(
    const Title &title, const Layer &layer, double title_time,
    const fxm::video::VideoFrameLoadingStatus &status)
{
    const double t = std::max(0.0, title_time - layer.in_time);
    const double box_w = std::max(1.0, eval_box_width(layer, t));
    const double box_h = std::max(1.0, eval_box_height(layer, t));
    const int max_sample_dim = std::clamp(std::max(title.width, title.height) * 2,
                                          512, 4096);
    const int w = std::clamp(static_cast<int>(std::ceil(box_w)), 160, max_sample_dim);
    const int h = std::clamp(static_cast<int>(std::ceil(box_h)), 90, max_sample_dim);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(22, 26, 34, 235));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor border(114, 157, 255, 210);
    const QColor fill(55, 68, 92, 210);
    painter.setPen(QPen(border, 2));
    painter.setBrush(fill);
    const QRectF card(8, 8, std::max(1, w - 16), std::max(1, h - 16));
    painter.drawRoundedRect(card, 8, 8);

    const int progress = std::clamp(status.progress_percent, 0, 100);
    const QString headline = QStringLiteral("Loading video…");
    QString detail = QString::fromStdString(status.label);
    if (detail.isEmpty())
        detail = QStringLiteral("Preparing first decoded frame");
    if (progress > 0 && progress < 100 && !detail.contains(QLatin1Char('%')))
        detail += QStringLiteral(" · %1%").arg(progress);

    QFont headline_font = painter.font();
    headline_font.setBold(true);
    headline_font.setPointSize(std::max(9, std::min(18, h / 9)));
    painter.setFont(headline_font);
    painter.setPen(QColor(245, 248, 255));
    painter.drawText(card.adjusted(14, 12, -14, -h / 2),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                     headline);

    QFont detail_font = painter.font();
    detail_font.setBold(false);
    detail_font.setPointSize(std::max(8, std::min(13, h / 13)));
    painter.setFont(detail_font);
    painter.setPen(QColor(210, 218, 235));
    painter.drawText(card.adjusted(14, h / 3, -14, -30),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                     detail);

    const QRectF track(22, h - 24, std::max(8, w - 44), 7);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 24, 31, 230));
    painter.drawRoundedRect(track, 3, 3);
    painter.setBrush(QColor(114, 157, 255, 235));
    painter.drawRoundedRect(QRectF(track.left(), track.top(),
                                   track.width() * progress / 100.0,
                                   track.height()), 3, 3);
    painter.end();
    return image;
}

static void render_layer_video(cairo_t *cr, const Title &title,
                               const Layer &layer, double title_time)
{
    const fxm::video::VideoFrame frame =
        fxm::video::FrameRuntime::instance().frame_for_layer(
            layer, title_time, 1.0 / std::max(1e-6, source_frame_duration()),
            g_raster_video_decode_client);
    if (frame.image.isNull()) {
        const fxm::video::VideoFrameLoadingStatus status =
            fxm::video::FrameRuntime::instance().loading_status_for_layer(layer);
        const QImage placeholder = make_video_loading_placeholder_raster(
            title, layer, title_time, status);
        const QString identity = QStringLiteral("video-loading|%1|%2")
            .arg(QString::fromStdString(layer.id))
            .arg(status.progress_percent);
        render_layer_image_pixels(cr, title, layer, title_time, placeholder, identity);
        return;
    }
    const QString identity = QStringLiteral("video|%1|decoded=%2")
        .arg(QString::fromStdString(layer.id)).arg(frame.frame_number);
    render_layer_image_pixels(cr, title, layer, title_time, frame.image, identity);
}


static thread_local bool g_scene_mask_placeholder_preview_enabled = false;

struct ScopedSceneMaskPlaceholderPreview {
    bool previous = false;
    explicit ScopedSceneMaskPlaceholderPreview(bool enabled)
        : previous(g_scene_mask_placeholder_preview_enabled)
    {
        g_scene_mask_placeholder_preview_enabled = enabled;
    }
    ~ScopedSceneMaskPlaceholderPreview()
    {
        g_scene_mask_placeholder_preview_enabled = previous;
    }
};

static void render_layer_unmasked_raw(cairo_t *cr, const Title &title, const Layer &layer,
                                      double title_time, int canvas_w, int canvas_h);

static void clear_placeholder_rich_text_strokes(RichTextDocument &doc)
{
    doc.default_format.stroke.enabled = false;
    doc.default_format.stroke.width = 0.0f;
    doc.default_format.stroke.opacity = 0.0f;
    if (doc.has_typing_format) {
        doc.typing_format.stroke.enabled = false;
        doc.typing_format.stroke.width = 0.0f;
        doc.typing_format.stroke.opacity = 0.0f;
    }
    for (RichTextRange &range : doc.ranges) {
        range.format.stroke.enabled = false;
        range.format.stroke.width = 0.0f;
        range.format.stroke.opacity = 0.0f;
    }
}

static Layer scene_mask_placeholder_fill_only_layer(const Layer &layer)
{
    Layer fill = layer;
    fill.outline_enabled = false;
    fill.stroke_width = 0.0f;
    fill.outline_opacity = 0.0f;
    fill.stroke_fill_type = 0;
    fill.background_stroke_width = 0.0f;
    fill.background_stroke_opacity = 0.0f;
    fill.background_stroke_width_prop = AnimatedProperty("background_stroke_width", 0.0);
    fill.background_stroke_opacity_prop = AnimatedProperty("background_stroke_opacity", 0.0);
    clear_placeholder_rich_text_strokes(fill.rich_text);
    return fill;
}

static uint32_t scene_mask_placeholder_color(const Layer &layer, double local_time)
{
    switch (layer.type) {
    case LayerType::Text:
    case LayerType::Clock:
    case LayerType::Ticker:
        return eval_text_color(layer, local_time);
    case LayerType::Image:
    case LayerType::Video:
        return 0xFF4B6EA8u;
    default:
        return eval_fill_color(layer, local_time);
    }
}

static void add_scene_mask_placeholder_path(cairo_t *cr, const Layer &layer,
                                                double width, double height)
{
    if (layer_type_uses_shape_geometry(layer.type)) {
        cairo_add_layer_shape(cr, layer, width, height);
        return;
    }

    cairo_add_rounded_rect_corners(cr, 0.0, 0.0, width, height,
                                   layer.corner_radius_tl,
                                   layer.corner_radius_tr,
                                   layer.corner_radius_br,
                                   layer.corner_radius_bl,
                                   layer.corner_bevel_roundness);
}

static bool render_scene_mask_placeholder_from_layer_alpha(
    cairo_t *cr, const Title &title, const Layer &layer, double title_time,
    int canvas_w, int canvas_h, double r, double g, double b, double a)
{
    if (!cr || layer_type_uses_shape_geometry(layer.type) ||
        layer_type_is_container(layer.type))
        return false;

    Layer silhouette_layer = scene_mask_placeholder_fill_only_layer(layer);
    silhouette_layer.use_as_scene_mask = false;

    cairo_push_group(cr);
    {
        ScopedSceneMaskPlaceholderPreview disable_placeholder(false);
        render_layer_unmasked_raw(cr, title, silhouette_layer, title_time,
                                  canvas_w, canvas_h);
    }
    cairo_pattern_t *silhouette = cairo_pop_group(cr);
    if (!silhouette || cairo_pattern_status(silhouette) != CAIRO_STATUS_SUCCESS) {
        if (silhouette)
            cairo_pattern_destroy(silhouette);
        return false;
    }

    const double grid = std::max(
        18.0, std::min(std::max(1, canvas_w), std::max(1, canvas_h)) / 28.0);

    cairo_save(cr);
    cairo_set_source_rgba(cr, r, g, b, std::max(0.24, a * 0.62));
    cairo_mask(cr, silhouette);

    cairo_push_group(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.18);
    for (double px = -grid; px <= canvas_w + grid; px += grid) {
        cairo_move_to(cr, px, 0.0);
        cairo_line_to(cr, px, canvas_h);
    }
    for (double py = -grid; py <= canvas_h + grid; py += grid) {
        cairo_move_to(cr, 0.0, py);
        cairo_line_to(cr, canvas_w, py);
    }
    cairo_stroke(cr);
    cairo_pattern_t *grid_pattern = cairo_pop_group(cr);
    if (grid_pattern && cairo_pattern_status(grid_pattern) == CAIRO_STATUS_SUCCESS) {
        cairo_set_source(cr, grid_pattern);
        cairo_mask(cr, silhouette);
    }
    if (grid_pattern)
        cairo_pattern_destroy(grid_pattern);
    cairo_restore(cr);

    cairo_pattern_destroy(silhouette);
    return true;
}

static void render_scene_mask_placeholder_shape_stroke(cairo_t *cr,
                                                       const Title &title,
                                                       const Layer &layer,
                                                       double title_time,
                                                       double width,
                                                       double height)
{
    if (!cr || !layer_type_uses_shape_geometry(layer.type))
        return;

    const double local_time = std::max(0.0, title_time - layer.in_time);
    const double alpha = layer_chain_opacity(title, layer, title_time);
    const double outline_width = eval_outline_width(layer, local_time);
    const uint32_t outline_color = eval_outline_color(layer, local_time);
    const bool has_outline = outline_width > 0.0 &&
        (layer.stroke_fill_type == 2 || ((outline_color >> 24) & 0xFF) > 0);
    if (!has_outline)
        return;

    auto set_stroke_source = [&]() -> cairo_pattern_t * {
        return set_layer_outline_source(cr, layer, 0.0, 0.0, width, height,
                                        alpha, local_time);
    };

    const int alignment = eval_outline_alignment(layer, local_time);
    cairo_save(cr);
    cairo_set_antialias(cr, outline_cairo_antialias(layer));
    cairo_set_line_join(cr, outline_cairo_join_style(layer));

    if (alignment == 0) {
        cairo_push_group(cr);
        cairo_add_layer_shape(cr, layer, width, height);
        cairo_set_line_width(cr, outline_width * 2.0);
        cairo_pattern_t *pattern = set_stroke_source();
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
        cairo_set_operator(cr, CAIRO_OPERATOR_DEST_OUT);
        cairo_add_layer_shape(cr, layer, width, height);
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
        cairo_fill(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_pop_group_to_source(cr);
        cairo_paint(cr);
    } else if (alignment == 2) {
        cairo_add_layer_shape(cr, layer, width, height);
        cairo_clip(cr);
        cairo_add_layer_shape(cr, layer, width, height);
        cairo_set_line_width(cr, outline_width * 2.0);
        cairo_pattern_t *pattern = set_stroke_source();
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
    } else {
        cairo_add_layer_shape(cr, layer, width, height);
        cairo_set_line_width(cr, outline_width);
        cairo_pattern_t *pattern = set_stroke_source();
        cairo_stroke(cr);
        if (pattern) cairo_pattern_destroy(pattern);
    }

    cairo_restore(cr);
}

static void render_scene_mask_placeholder(cairo_t *cr, const Title &title,
                                          const Layer &layer,
                                          double title_time, int canvas_w,
                                          int canvas_h)
{
    const double local_time = std::max(0.0, title_time - layer.in_time);
    const double width = std::max(1.0, eval_box_width(layer, local_time));
    const double height = std::max(1.0, eval_box_height(layer, local_time));
    const double x = -eval_origin_x(layer, local_time) * width;
    const double y = -eval_origin_y(layer, local_time) * height;
    double r = 0.0, g = 0.0, b = 0.0, a = 1.0;
    unpack_color(scene_mask_placeholder_color(layer, local_time), r, g, b, a);

    /* Text, clocks, tickers, bitmap/video layers and other non-path sources
     * must preview with the same alpha silhouette that the OBS scene-mask
     * pass consumes. A rectangular placeholder made text masks look valid in
     * the editor but produced a different matte at output time. */
    if (render_scene_mask_placeholder_from_layer_alpha(
            cr, title, layer, title_time, canvas_w, canvas_h, r, g, b, a))
        return;

    cairo_save(cr);
    cairo_translate(cr, x, y);

    /* Geometric masks keep their authored path/corner geometry. The normal
     * effect stack is applied after this base raster, exactly like Stinger
     * A/B placeholders. */
    add_scene_mask_placeholder_path(cr, layer, width, height);
    cairo_set_source_rgba(cr, r, g, b, std::max(0.20, a * 0.38));
    cairo_fill_preserve(cr);
    cairo_clip(cr);

    const double grid = std::max(24.0, std::min(width, height) / 12.0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.16);
    for (double px = 0.0; px <= width; px += grid) {
        cairo_move_to(cr, px, 0.0); cairo_line_to(cr, px, height);
    }
    for (double py = 0.0; py <= height; py += grid) {
        cairo_move_to(cr, 0.0, py); cairo_line_to(cr, width, py);
    }
    cairo_stroke(cr);

    cairo_reset_clip(cr);
    /* Development Version 262: the placeholder remains a fill-only editor
     * affordance, but shape strokes are composited back into the same raster.
     * This keeps the authored stroke visible and lets the scene-mask layer
     * effect stack process fill and stroke together. */
    render_scene_mask_placeholder_shape_stroke(cr, title, layer, title_time,
                                               width, height);
    cairo_restore(cr);
}

static void render_transition_input_placeholder(cairo_t *cr, const Layer &layer,
                                                double title_time)
{
    const double local_time = std::max(0.0, title_time - layer.in_time);
    const double width = std::max(1.0, eval_box_width(layer, local_time));
    const double height = std::max(1.0, eval_box_height(layer, local_time));
    const double x = -eval_origin_x(layer, local_time) * width;
    const double y = -eval_origin_y(layer, local_time) * height;
    const bool scene_b = layer.transition_input_slot == 1;

    cairo_save(cr);
    cairo_rectangle(cr, x, y, width, height);
    if (scene_b)
        cairo_set_source_rgb(cr, 0.34, 0.16, 0.08);
    else
        cairo_set_source_rgb(cr, 0.06, 0.20, 0.36);
    cairo_fill(cr);

    const double grid = std::max(24.0, std::min(width, height) / 12.0);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.12);
    for (double px = x; px <= x + width; px += grid) {
        cairo_move_to(cr, px, y);
        cairo_line_to(cr, px, y + height);
    }
    for (double py = y; py <= y + height; py += grid) {
        cairo_move_to(cr, x, py);
        cairo_line_to(cr, x + width, py);
    }
    cairo_stroke(cr);

    const char *label = scene_b ? "B" : "A";
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, std::max(48.0, std::min(width, height) * 0.28));
    cairo_text_extents_t extents{};
    cairo_text_extents(cr, label, &extents);
    cairo_move_to(cr, x + (width - extents.width) * 0.5 - extents.x_bearing,
                  y + (height - extents.height) * 0.5 - extents.y_bearing);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.82);
    cairo_show_text(cr, label);
    cairo_restore(cr);
}

static void render_layer_chart(cairo_t *cr, const Layer &layer,
                               double title_time)
{
    const double local_time = std::max(0.0, title_time - layer.in_time);
    const double width = std::max(1.0, eval_box_width(layer, local_time));
    const double height = std::max(1.0, eval_box_height(layer, local_time));
    const int raster_width = std::max(1, static_cast<int>(std::ceil(width)));
    const int raster_height = std::max(1, static_cast<int>(std::ceil(height)));
    QImage image(raster_width, raster_height,
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        fxm::chart::render(painter, layer, QRectF(0.0, 0.0, width, height),
                           local_time);
    }
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        image.bits(), CAIRO_FORMAT_ARGB32, image.width(), image.height(),
        image.bytesPerLine());
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface)
            cairo_surface_destroy(surface);
        return;
    }
    const double x = -eval_origin_x(layer, local_time) * width;
    const double y = -eval_origin_y(layer, local_time) * height;
    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, width / raster_width, height / raster_height);
    cairo_set_source_surface(cr, surface, 0.0, 0.0);
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(surface);
}

static void render_layer_unmasked_raw(cairo_t *cr, const Title &title, const Layer &layer,
                                      double title_time, int canvas_w, int canvas_h)
{
    if (layer.use_as_scene_mask && g_scene_mask_placeholder_preview_enabled) {
        render_scene_mask_placeholder(cr, title, layer, title_time,
                                      canvas_w, canvas_h);
        return;
    }
    switch (layer.type) {
    case LayerType::Text:
    case LayerType::Clock:
    case LayerType::Ticker:
        render_layer_text(cr, title, layer, title_time, canvas_w, canvas_h);
        break;
    case LayerType::SolidRect:
    case LayerType::Shape:
    case LayerType::ColorSolid:
        render_layer_rect(cr, title, layer, title_time);
        break;
    case LayerType::Image:
        render_layer_image(cr, title, layer, title_time);
        break;
    case LayerType::Video:
        render_layer_video(cr, title, layer, title_time);
        break;
    case LayerType::Chart:
        render_layer_chart(cr, layer, title_time);
        break;
    case LayerType::TransitionInput:
        render_transition_input_placeholder(cr, layer, title_time);
        break;
    default:
        break;
    }
}

static void paint_soft_wipe_mask(cairo_t *cr, const Title &title, const Layer &layer,
                                 double title_time, int canvas_w, int canvas_h,
                                 const LayerTransitionVisualState &transition)
{
    const double t = std::max(0.0, title_time - layer.in_time);
    const double width = std::max(1.0, eval_box_width(layer, t));
    const double height = std::max(1.0, eval_box_height(layer, t));
    const double x = -eval_origin_x(layer, t) * width;
    const double y = -eval_origin_y(layer, t) * height;
    const double reveal = std::clamp(transition.wipe, 0.0, 1.0);
    if (reveal <= 0.000001)
        return;

    const double softness = std::clamp(transition.wipe_softness, 0.0, 1.0);
    const double extent = std::max({width, height, (double)std::max(1, canvas_w),
                                    (double)std::max(1, canvas_h)}) * 4.0 + 64.0;
    cairo_pattern_t *gradient = nullptr;
    switch (transition.wipe_direction) {
    case LayerTransitionDirection::Right: {
        const double boundary = x + width * (1.0 - reveal);
        const double feather = std::max(0.5, width * softness);
        gradient = cairo_pattern_create_linear(boundary, 0.0, boundary + feather, 0.0);
        cairo_pattern_add_color_stop_rgba(gradient, 0.0, 1.0, 1.0, 1.0, 0.0);
        cairo_pattern_add_color_stop_rgba(gradient, 1.0, 1.0, 1.0, 1.0, 1.0);
        break;
    }
    case LayerTransitionDirection::Up: {
        const double boundary = y + height * (1.0 - reveal);
        const double feather = std::max(0.5, height * softness);
        gradient = cairo_pattern_create_linear(0.0, boundary, 0.0, boundary + feather);
        cairo_pattern_add_color_stop_rgba(gradient, 0.0, 1.0, 1.0, 1.0, 0.0);
        cairo_pattern_add_color_stop_rgba(gradient, 1.0, 1.0, 1.0, 1.0, 1.0);
        break;
    }
    case LayerTransitionDirection::Down: {
        const double boundary = y + height * reveal;
        const double feather = std::max(0.5, height * softness);
        gradient = cairo_pattern_create_linear(0.0, boundary - feather, 0.0, boundary);
        cairo_pattern_add_color_stop_rgba(gradient, 0.0, 1.0, 1.0, 1.0, 1.0);
        cairo_pattern_add_color_stop_rgba(gradient, 1.0, 1.0, 1.0, 1.0, 0.0);
        break;
    }
    case LayerTransitionDirection::Left:
    case LayerTransitionDirection::None:
    default: {
        const double boundary = x + width * reveal;
        const double feather = std::max(0.5, width * softness);
        gradient = cairo_pattern_create_linear(boundary - feather, 0.0, boundary, 0.0);
        cairo_pattern_add_color_stop_rgba(gradient, 0.0, 1.0, 1.0, 1.0, 1.0);
        cairo_pattern_add_color_stop_rgba(gradient, 1.0, 1.0, 1.0, 1.0, 0.0);
        break;
    }
    }
    if (!gradient || cairo_pattern_status(gradient) != CAIRO_STATUS_SUCCESS) {
        if (gradient) cairo_pattern_destroy(gradient);
        return;
    }
    cairo_pattern_set_extend(gradient, CAIRO_EXTEND_PAD);
    cairo_save(cr);
    apply_layer_world_transform(cr, title, layer, title_time);
    cairo_rectangle(cr, x - extent, y - extent, width + extent * 2.0, height + extent * 2.0);
    cairo_set_source(cr, gradient);
    cairo_fill(cr);
    cairo_restore(cr);
    cairo_pattern_destroy(gradient);
}

static void render_layer_unmasked(cairo_t *cr, const Title &title, const Layer &layer,
                                  double title_time, int canvas_w, int canvas_h)
{
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    const bool soft_wipe = transition.active && transition.wipe < 0.999999 &&
                           transition.wipe_softness > 0.000001;
    if (!soft_wipe) {
        render_layer_unmasked_raw(cr, title, layer, title_time, canvas_w, canvas_h);
        return;
    }

    cairo_push_group(cr);
    render_layer_unmasked_raw(cr, title, layer, title_time, canvas_w, canvas_h);
    cairo_pattern_t *content = cairo_pop_group(cr);

    cairo_push_group(cr);
    paint_soft_wipe_mask(cr, title, layer, title_time, canvas_w, canvas_h, transition);
    cairo_pattern_t *mask = cairo_pop_group(cr);

    if (content && mask && cairo_pattern_status(content) == CAIRO_STATUS_SUCCESS &&
        cairo_pattern_status(mask) == CAIRO_STATUS_SUCCESS) {
        cairo_set_source(cr, content);
        cairo_mask(cr, mask);
    }
    if (content) cairo_pattern_destroy(content);
    if (mask) cairo_pattern_destroy(mask);
}



static bool layer_effect_requires_stack_surface(const LayerEffect &effect)
{
    if (!effect.enabled) return false;
    switch (effect.type) {
    case LayerEffectType::BackgroundColor:
    case LayerEffectType::Outline:
    case LayerEffectType::Bloom:
    case LayerEffectType::Emboss:
    case LayerEffectType::DropShadow:
    case LayerEffectType::LongShadow:
    case LayerEffectType::ColorOverlay:
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
    case LayerEffectType::InnerShadow:
    case LayerEffectType::Blur:
    case LayerEffectType::LensFlare:
    case LayerEffectType::Vignette:
    case LayerEffectType::Noise:
        case LayerEffectType::Grain:
        case LayerEffectType::FilmDistortion:
        case LayerEffectType::AnalogDistortion:
        case LayerEffectType::DigitalDistortion:
    case LayerEffectType::RoughenEdges:
    case LayerEffectType::FourColorGradient:
        return true;
    case LayerEffectType::MotionBlur:
        /* Motion blur is temporal, not a post-render image-space filter.
         * Treating it as a stack-surface effect makes static bitmap/SVG
         * layers look like multiple overlaid copies even when they do not
         * move, because some code paths used a cached raster and applied a
         * directional smear.  Keep it out of the stack-surface pipeline and
         * let render_motion_blurred_layer() handle it consistently. */
        return false;
    case LayerEffectType::BrightnessContrast:
        return std::abs(effect.brightness) > 0.0001f || std::abs(effect.contrast - 1.0f) > 0.0001f;
    case LayerEffectType::Saturation:
        return std::abs(effect.saturation - 1.0f) > 0.0001f;
    default:
        return false;
    }
}

static bool layer_has_stackable_pixel_effects(const Layer &layer)
{
    for (const auto &effect : layer.effects) {
        if (layer_effect_requires_stack_surface(effect))
            return true;
    }
    return false;
}

static bool effect_is_motion_blur(const LayerEffect &effect)
{
    return effect.enabled && effect.type == LayerEffectType::MotionBlur;
}

static const LayerEffect *layer_motion_blur_effect(const Layer &layer)
{
    for (auto it = layer.effects.rbegin(); it != layer.effects.rend(); ++it) {
        if (effect_is_motion_blur(*it))
            return &*it;
    }
    return nullptr;
}

static bool layer_has_motion_blur(const Layer &layer)
{
    return layer_motion_blur_effect(layer) != nullptr;
}

static bool gpu_effects_requested_for_source(const TitleSourceData *data)
{
    (void)data;
    return true;
}

static Layer layer_without_stackable_pixel_effects(const Layer &layer)
{
    Layer base_layer = layer;
    base_layer.effects.erase(std::remove_if(base_layer.effects.begin(), base_layer.effects.end(),
                                            layer_effect_requires_stack_surface),
                             base_layer.effects.end());
    return base_layer;
}

static Layer layer_without_motion_blur_effects(const Layer &layer)
{
    Layer base_layer = layer;
    base_layer.effects.erase(std::remove_if(base_layer.effects.begin(), base_layer.effects.end(),
                                            effect_is_motion_blur),
                             base_layer.effects.end());
    return base_layer;
}

static std::vector<uint8_t> surface_alpha(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    uint8_t *data = cairo_image_surface_get_data(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    std::vector<uint8_t> alpha((size_t)std::max(0, width) * (size_t)std::max(0, height), 0);
    if (!data || width <= 0 || height <= 0 || stride <= 0)
        return alpha;
    for (int y = 0; y < height; ++y) {
        const uint8_t *row = data + y * stride;
        for (int x = 0; x < width; ++x)
            alpha[y * width + x] = row[x * 4 + 3];
    }
    return alpha;
}

static std::vector<uint8_t> surface_pixels(cairo_surface_t *surface)
{
    cairo_surface_flush(surface);
    uint8_t *data = cairo_image_surface_get_data(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    std::vector<uint8_t> pixels((size_t)std::max(0, width) * (size_t)std::max(0, height) * 4, 0);
    if (!data || width <= 0 || height <= 0 || stride <= 0)
        return pixels;
    for (int y = 0; y < height; ++y)
        std::copy(data + y * stride, data + y * stride + width * 4, pixels.begin() + (size_t)y * width * 4);
    return pixels;
}

static void write_surface_pixels(cairo_surface_t *surface, const std::vector<uint8_t> &pixels)
{
    uint8_t *data = cairo_image_surface_get_data(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    if (!data || width <= 0 || height <= 0 || stride <= 0 || pixels.size() < (size_t)width * (size_t)height * 4)
        return;
    for (int y = 0; y < height; ++y)
        std::copy(pixels.begin() + (size_t)y * width * 4,
                  pixels.begin() + (size_t)(y + 1) * width * 4,
                  data + y * stride);
    cairo_surface_mark_dirty(surface);
}

static void clear_surface(cairo_t *cr)
{
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_restore(cr);
}

static void composite_premultiplied_over(std::vector<uint8_t> &dst, const std::vector<uint8_t> &src)
{
    const size_t count = std::min(dst.size(), src.size()) / 4;
    for (size_t i = 0; i < count; ++i) {
        uint8_t *d = dst.data() + i * 4;
        const uint8_t *s = src.data() + i * 4;
        const int inv_a = 255 - s[3];
        d[0] = (uint8_t)std::min(255, (int)s[0] + ((int)d[0] * inv_a + 127) / 255);
        d[1] = (uint8_t)std::min(255, (int)s[1] + ((int)d[1] * inv_a + 127) / 255);
        d[2] = (uint8_t)std::min(255, (int)s[2] + ((int)d[2] * inv_a + 127) / 255);
        d[3] = (uint8_t)std::min(255, (int)s[3] + ((int)d[3] * inv_a + 127) / 255);
    }
}

static void box_blur_pixels(std::vector<uint8_t> &pixels, int w, int h, int radius)
{
    if (radius <= 0 || w <= 0 || h <= 0 || pixels.size() < (size_t)w * (size_t)h * 4)
        return;

    std::vector<uint8_t> tmp(pixels.size());
    const int window = radius * 2 + 1;

    for (int y = 0; y < h; ++y) {
        int sum[4] = {0, 0, 0, 0};
        for (int x = -radius; x <= radius; ++x) {
            const uint8_t *px = &pixels[((size_t)y * w + std::clamp(x, 0, w - 1)) * 4];
            for (int c = 0; c < 4; ++c) sum[c] += px[c];
        }
        for (int x = 0; x < w; ++x) {
            uint8_t *dst = &tmp[((size_t)y * w + x) * 4];
            for (int c = 0; c < 4; ++c) dst[c] = (uint8_t)(sum[c] / window);
            const uint8_t *remove = &pixels[((size_t)y * w + std::clamp(x - radius, 0, w - 1)) * 4];
            const uint8_t *add = &pixels[((size_t)y * w + std::clamp(x + radius + 1, 0, w - 1)) * 4];
            for (int c = 0; c < 4; ++c) sum[c] += add[c] - remove[c];
        }
    }

    for (int x = 0; x < w; ++x) {
        int sum[4] = {0, 0, 0, 0};
        for (int y = -radius; y <= radius; ++y) {
            const uint8_t *px = &tmp[((size_t)std::clamp(y, 0, h - 1) * w + x) * 4];
            for (int c = 0; c < 4; ++c) sum[c] += px[c];
        }
        for (int y = 0; y < h; ++y) {
            uint8_t *dst = &pixels[((size_t)y * w + x) * 4];
            for (int c = 0; c < 4; ++c) dst[c] = (uint8_t)(sum[c] / window);
            const uint8_t *remove = &tmp[((size_t)std::clamp(y - radius, 0, h - 1) * w + x) * 4];
            const uint8_t *add = &tmp[((size_t)std::clamp(y + radius + 1, 0, h - 1) * w + x) * 4];
            for (int c = 0; c < 4; ++c) sum[c] += add[c] - remove[c];
        }
    }
}

static void blur_surface_for_type(cairo_surface_t *surface, double blur, int blur_type, double amount)
{
    cairo_surface_flush(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    if (width <= 0 || height <= 0)
        return;

    const auto passes = blur_pass_radii(blur, blur_type);
    if (passes.empty())
        return;

    std::vector<uint8_t> original = surface_pixels(surface);
    std::vector<uint8_t> blurred = original;
    blur_premultiplied_pixels_for_type(
        blurred, width, height, blur, blur_type);

    const double mix = std::clamp(amount, 0.0, 1.0);
    if (mix < 1.0) {
        for (size_t i = 0; i < blurred.size(); ++i)
            blurred[i] = (uint8_t)std::lround(original[i] * (1.0 - mix) + blurred[i] * mix);
    }
    write_surface_pixels(surface, blurred);
}

static void offset_alpha(const std::vector<uint8_t> &src, std::vector<uint8_t> &dst,
                         int w, int h, double dx, double dy, double opacity = 1.0)
{
    if (opacity <= 0.0 || w <= 0 || h <= 0) return;
    const int ox = (int)std::round(dx);
    const int oy = (int)std::round(dy);
    for (int y = 0; y < h; ++y) {
        const int sy = y - oy;
        if (sy < 0 || sy >= h) continue;
        for (int x = 0; x < w; ++x) {
            const int sx = x - ox;
            if (sx < 0 || sx >= w) continue;
            const int a = (int)std::round(src[sy * w + sx] * opacity);
            if (a <= 0) continue;
            uint8_t &d = dst[y * w + x];
            d = (uint8_t)std::min(255, (int)d + a - ((int)d * a) / 255);
        }
    }
}

static double blend_channel(double base, double src, EffectBlendMode mode)
{
    switch (mode) {
    case EffectBlendMode::Multiply: return base * src;
    case EffectBlendMode::Additive: return std::min(1.0, base + src);
    case EffectBlendMode::Screen: return 1.0 - (1.0 - base) * (1.0 - src);
    case EffectBlendMode::Overlay: return base < 0.5 ? 2.0 * base * src : 1.0 - 2.0 * (1.0 - base) * (1.0 - src);
    case EffectBlendMode::Color:
    case EffectBlendMode::Normal:
    default: return src;
    }
}

static void blend_color_mode(double br, double bg, double bb, double sr, double sg, double sb,
                             double &rr, double &rg, double &rb)
{
    QColor base = QColor::fromRgbF(std::clamp(br, 0.0, 1.0), std::clamp(bg, 0.0, 1.0), std::clamp(bb, 0.0, 1.0));
    QColor src = QColor::fromRgbF(std::clamp(sr, 0.0, 1.0), std::clamp(sg, 0.0, 1.0), std::clamp(sb, 0.0, 1.0));
    float bh, bs, bl, ba;
    float sh, ss, sl, sa;
    base.getHslF(&bh, &bs, &bl, &ba);
    src.getHslF(&sh, &ss, &sl, &sa);
    QColor out;
    out.setHslF(sh < 0.0 ? bh : sh, ss, bl, ba);
    rr = out.redF();
    rg = out.greenF();
    rb = out.blueF();
}

static void composite_solid_alpha(cairo_surface_t *surface, const std::vector<uint8_t> &mask,
                                  uint32_t argb, double opacity, EffectBlendMode mode,
                                  bool preserve_destination_alpha)
{
    cairo_surface_flush(surface);
    uint8_t *data = cairo_image_surface_get_data(surface);
    const int width = cairo_image_surface_get_width(surface);
    const int height = cairo_image_surface_get_height(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    if (!data || width <= 0 || height <= 0 || stride <= 0 || mask.size() < (size_t)width * (size_t)height)
        return;

    const double src_a_base = std::clamp(opacity, 0.0, 1.0) * (((argb >> 24) & 0xFF) / 255.0);
    if (src_a_base <= 0.0) return;
    const double sr = ((argb >> 16) & 0xFF) / 255.0;
    const double sg = ((argb >> 8) & 0xFF) / 255.0;
    const double sb = (argb & 0xFF) / 255.0;

    for (int y = 0; y < height; ++y) {
        uint8_t *row = data + y * stride;
        for (int x = 0; x < width; ++x) {
            const double sa = src_a_base * (mask[y * width + x] / 255.0);
            if (sa <= 0.0) continue;
            uint8_t *px = row + x * 4;
            const double da = px[3] / 255.0;
            const double br = da > 0.0 ? (px[2] / 255.0) / da : 0.0;
            const double bg = da > 0.0 ? (px[1] / 255.0) / da : 0.0;
            const double bb = da > 0.0 ? (px[0] / 255.0) / da : 0.0;

            double rr, rg, rb;
            if (mode == EffectBlendMode::Color)
                blend_color_mode(br, bg, bb, sr, sg, sb, rr, rg, rb);
            else {
                rr = blend_channel(br, sr, mode);
                rg = blend_channel(bg, sg, mode);
                rb = blend_channel(bb, sb, mode);
            }
