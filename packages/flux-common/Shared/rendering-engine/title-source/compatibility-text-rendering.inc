            const int relative_start = overlap_start - block.position();
            const int relative_end = overlap_end - block.position();
            const qreal x1 = line.cursorToX(relative_start);
            const qreal x2 = line.cursorToX(relative_end);
            const QRectF line_bounds(
                block_rect.left() + std::min(x1, x2),
                block_rect.top() + line.y(),
                std::max<qreal>(1.0, std::abs(x2 - x1)),
                std::max<qreal>(1.0, line.height()));
            local_bounds = has_bounds ? local_bounds.united(line_bounds) : line_bounds;
            has_bounds = true;
        }
    }
    if (!has_bounds)
        return {};

    const QSizeF document_size = doc.size();
    const double horizontal_scale = rich_text_document_horizontal_scale(
        layer, text_rect, document_size, t);
    const QPointF origin = rich_text_document_origin(layer, text_rect, document_size, t, horizontal_scale);
    const qreal scaled_left = local_bounds.left() * horizontal_scale;
    const qreal scaled_width = local_bounds.width() * horizontal_scale;
    local_bounds.setRect(scaled_left, local_bounds.top(),
                         scaled_width, local_bounds.height());
    return local_bounds.translated(origin);
}

static QRect text_transition_unit_render_bounds(const QTextDocument &shaped_document,
                                                const Layer &layer,
                                                const QRectF &text_rect,
                                                double t,
                                                const TextTransitionUnitRange &range,
                                                const ShadowRenderParams &shadow_params,
                                                bool render_shadow,
                                                const QRect &image_bounds)
{
    QRectF bounds = rich_text_document_range_bounds(
        shaped_document, layer, text_rect, t, range);
    if (bounds.isEmpty())
        bounds = text_rect;

    // QTextLine bounds describe advances, not all italic/outline/shadow pixel
    // overhang. Expand from the actual line height, then include the complete
    // shadow envelope. This remains much smaller than a full layer surface.
    const double rich_height = max_rich_text_font_height_hint(layer, t);
    double pad = std::max({8.0, bounds.height() * 0.75,
                           rich_height * 0.75}) +
                 std::max(std::max(0.0, eval_outline_width(layer, t)),
                          max_rich_text_stroke_width(layer, t)) * 2.0 + 6.0;
    if (render_shadow) {
        const double drop_extent = std::max(std::abs(shadow_params.dx),
                                            std::abs(shadow_params.dy)) +
                                   shadow_params.blur * 3.0 +
                                   std::max(0.0, shadow_params.spread);
        const double long_extent = std::max(0.0, shadow_params.long_length) +
                                   std::max(0.0, shadow_params.long_blur) * 3.0;
        pad += std::max(drop_extent, long_extent);
    }

    QRect result = bounds.adjusted(-pad, -pad, pad, pad).toAlignedRect();
    result = result.intersected(image_bounds);
    return result;
}

static QPointF rich_text_transition_unit_kerning_offset(const QTextDocument &shaped_document,
                                                        const QTextDocument &isolated_document,
                                                        const Layer &layer,
                                                        const QRectF &text_rect,
                                                        double t,
                                                        const TextTransitionUnitRange &range)
{
    // Hiding the surrounding characters creates QText format-run boundaries.
    // Qt can no longer apply kerning across those boundaries, even though the
    // hidden characters still reserve their normal advance. Compare the caret
    // position in the fully shaped document with the isolated document and
    // move the independently rendered unit back to its original shaped origin.
    const QPointF shaped = rich_text_document_cursor_position(
        shaped_document, layer, text_rect, t, range.start);
    const QPointF isolated = rich_text_document_cursor_position(
        isolated_document, layer, text_rect, t, range.start);
    return shaped - isolated;
}

static void clear_global_outline_on_local_stroke_ranges(
    QTextDocument &document, const Layer &layer, double t,
    const TextTransitionUnitRange *visible_range)
{
    const RichTextDocument model = rich_text_model_for_source_time(layer, t);
    const QString qplain = document.toPlainText();
    const SourceQtUtf8PositionMap positions(qplain);
    const int visible_start = visible_range ? std::max(0, visible_range->start) : 0;
    const int visible_end = visible_range
        ? visible_start + std::max(0, visible_range->length)
        : std::max(0, document.characterCount() - 1);
    for (const RichTextRange &range : model.ranges) {
        if ((range.mask & RichTextCharStroke) == 0 || range.length == 0)
            continue;
        const int start = std::max(
            visible_start, positions.qpos_from_byte(range.start));
        const int end = std::min(
            visible_end, positions.qpos_from_byte(range.start + range.length));
        if (end <= start)
            continue;
        QTextCursor cursor(&document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        QTextCharFormat no_global_outline;
        no_global_outline.setTextOutline(QPen(Qt::NoPen));
        cursor.mergeCharFormat(no_global_outline);
    }
}


/* v277 exact compatibility raster -------------------------------------------------
 *
 * The editor selection already consumes ImmutableTextLayout. The old visible
 * fallback consumed QTextDocument/QFont instead, encoding V Scale as font size
 * and H Scale as font stretch. DirectWrite may treat that stretch as face
 * selection or metrics only, so selection and visible glyphs diverged. This
 * adapter draws the exact QRawFont outline at each immutable glyph baseline and
 * applies the same per-glyph scale values as the GPU atlas. QTextDocument remains
 * only for color-font/transition compatibility that cannot expose an outline. */
struct ExactRichTextGlyphPiece {
    QPainterPath path;
    QRectF clip;
    size_t paint_index = 0;
    size_t cluster_index = 0;
    bool clip_required = false;
};

struct ExactRichTextRaster {
    RichTextDocument model;
    ImmutableTextLayout layout;
    std::vector<TextLayoutPaintRun> paint_runs;
    std::vector<ExactRichTextGlyphPiece> pieces;
    bool valid = false;
};

static QRawFont exact_rich_text_raw_font(const TextLayoutRun &run)
{
    QRawFont raw = text_layout_registered_raw_font(run.font);
    if (raw.isValid())
        return raw;
    QFont font(QString::fromStdString(run.font.family));
    if (!run.font.style.empty())
        font.setStyleName(QString::fromStdString(run.font.style));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(
        std::max(1.0f, run.font.pixel_size)))));
    raw = QRawFont::fromFont(font);
    if (raw.isValid())
        raw.setPixelSize(std::max(1.0f, run.font.pixel_size));
    return raw;
}

static ExactRichTextRaster exact_rich_text_raster_for_layer(
    const Layer &layer, const QRectF &text_rect, double local_time)
{
    ExactRichTextRaster out;
    out.model = rich_text_model_for_source_time(layer, local_time);
    if (layer.text_overflow_mode == 2) {
        const QString fitted = overflow_layout_text(
            QString::fromStdString(out.model.plain_text), layer);
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(out.model);
        rich_text_document_replace_text_canonical(
            out.model, fitted.toUtf8().toStdString(), insertion_format,
            out.model.has_typing_format ? out.model.typing_format_mask : 0);
    }

    TextLayoutRequest request;
    request.document = out.model;
    request.max_width = static_cast<float>(std::max(1.0, text_rect.width()));
    request.max_height = static_cast<float>(std::max(1.0, text_rect.height()));
    request.device_scale = 1.0f;
    request.minimum_horizontal_fit =
        std::clamp(layer.text_fit_min_scale, 0.05f, 1.0f);
    request.overflow_mode = layer.text_overflow_mode;
    out.layout = cached_text_layout(request);
    if (!out.layout || !out.layout->valid)
        return out;

    out.paint_runs = text_layout_paint_runs_canonical(out.model);
    if (out.paint_runs.empty()) {
        TextLayoutPaintRun fallback;
        fallback.byte_start = 0;
        fallback.byte_length = out.model.plain_text.size();
        fallback.style.fill = out.model.default_format.fill;
        fallback.style.stroke = out.model.default_format.stroke;
        fallback.style.underline = out.model.default_format.underline;
        fallback.style.strikethrough = out.model.default_format.strikethrough;
        out.paint_runs.push_back(std::move(fallback));
    }

    std::vector<std::vector<TextLayoutPaintSlice>> cluster_slices;
    cluster_slices.reserve(out.layout->clusters.size());
    for (const TextLayoutCluster &cluster : out.layout->clusters) {
        auto slices = text_layout_cluster_paint_slices(
            *out.layout, cluster, out.paint_runs);
        if (slices.empty())
            slices.push_back({0, cluster.x, cluster.x + cluster.width});
        cluster_slices.push_back(std::move(slices));
    }

    for (const TextLayoutGlyph &glyph : out.layout->glyphs) {
        if (glyph.run_index >= out.layout->runs.size() ||
            glyph.cluster_index >= out.layout->clusters.size())
            continue;
        const TextLayoutRun &run = out.layout->runs[glyph.run_index];
        const QRawFont raw = exact_rich_text_raw_font(run);
        if (!raw.isValid())
            return ExactRichTextRaster{};
        QPainterPath path = raw.pathForGlyph(glyph.glyph_id);
        if (path.isEmpty()) {
            const QRectF bounds = raw.boundingRect(glyph.glyph_id);
            if (!bounds.isEmpty())
                return ExactRichTextRaster{};
            continue;
        }
        QTransform transform;
        transform.translate(text_rect.left() + glyph.x,
                            text_rect.top() + glyph.y);
        transform.scale(std::clamp(glyph.scale_x, 0.01f, 100.0f),
                        std::clamp(glyph.scale_y, 0.01f, 100.0f));
        path = transform.map(path);

        QRectF run_clip(-1000000.0, -1000000.0, 2000000.0, 2000000.0);
        if (run.split_ligature && run.clip_width > 0.0f &&
            run.clip_height > 0.0f) {
            run_clip = QRectF(text_rect.left() + run.clip_x,
                              text_rect.top() + run.clip_y,
                              run.clip_width, run.clip_height);
        }
        const auto &slices = cluster_slices[glyph.cluster_index];
        const bool split_paint_cluster = slices.size() > 1;
        for (const TextLayoutPaintSlice &slice : slices) {
            ExactRichTextGlyphPiece piece;
            piece.path = path;
            piece.paint_index = std::min(
                slice.paint_index, out.paint_runs.size() - 1);
            piece.cluster_index = glyph.cluster_index;
            const QRectF paint_clip(
                text_rect.left() + slice.x0, -1000000.0,
                std::max(0.0f, slice.x1 - slice.x0), 2000000.0);
            /* A normal glyph belongs wholly to one canonical paint run. Do not
             * crop it to the cluster advance: italic bearings, swashes and
             * outside strokes legitimately overlap neighbouring advances.
             * Restrict geometry only for an actual split ligature/paint
             * cluster, where one shaped outline must be divided between two
             * independently styled source ranges. */
            piece.clip_required = run.split_ligature || split_paint_cluster;
            piece.clip = piece.clip_required
                ? run_clip.intersected(paint_clip)
                : run_clip;
            if (!piece.clip_required || !piece.clip.isEmpty())
                out.pieces.push_back(std::move(piece));
        }
    }
    out.valid = true;
    return out;
}

static void draw_exact_rich_text_fill(QPainter &painter,
                                      const ExactRichTextRaster &raster,
                                      const QRectF &text_rect,
                                      bool alpha_mask_only = false,
                                      size_t cluster_begin = 0,
                                      size_t cluster_count =
                                          std::numeric_limits<size_t>::max())
{
    if (!raster.valid)
        return;
    const size_t cluster_end = cluster_count == std::numeric_limits<size_t>::max()
        ? std::numeric_limits<size_t>::max()
        : cluster_begin + cluster_count;
    const auto cluster_selected = [&](size_t cluster_index) {
        return cluster_index >= cluster_begin && cluster_index < cluster_end;
    };
    for (const ExactRichTextGlyphPiece &piece : raster.pieces) {
        if (piece.paint_index >= raster.paint_runs.size())
            continue;
        if (!cluster_selected(piece.cluster_index))
            continue;
        painter.save();
        if (piece.clip_required)
            painter.setClipRect(piece.clip, Qt::IntersectClip);
        painter.setPen(Qt::NoPen);
        painter.setBrush(alpha_mask_only
            ? QBrush(Qt::white)
            : rich_text_fill_brush(
                  raster.paint_runs[piece.paint_index].style.fill, text_rect));
        painter.drawPath(piece.path);
        painter.restore();
    }

    if (alpha_mask_only)
        return;
    /* Decorations share the canonical paint slices and immutable line geometry.
     * Their thickness is intentionally independent from H/V glyph scale, like
     * GPU stroke width and standard DTP character-scale semantics. */
    for (size_t cluster_index = 0;
         cluster_index < raster.layout->clusters.size(); ++cluster_index) {
        if (!cluster_selected(cluster_index))
            continue;
        const TextLayoutCluster &cluster = raster.layout->clusters[cluster_index];
        if (cluster.line_index >= raster.layout->lines.size())
            continue;
        const TextLayoutLine &line = raster.layout->lines[cluster.line_index];
        const auto slices = text_layout_cluster_paint_slices(
            *raster.layout, cluster, raster.paint_runs);
        for (const TextLayoutPaintSlice &slice : slices) {
            if (slice.paint_index >= raster.paint_runs.size())
                continue;
            const TextLayoutPaintRun &paint = raster.paint_runs[slice.paint_index];
            if (!paint.style.underline && !paint.style.strikethrough)
                continue;
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(rich_text_fill_brush(paint.style.fill, text_rect));
            const qreal thickness = std::max<qreal>(
                1.0, (line.ascent + line.descent) / 18.0);
            if (paint.style.underline) {
                const qreal y = text_rect.top() + line.y + line.baseline +
                                std::max<qreal>(1.0, line.descent * 0.2);
                painter.drawRect(QRectF(text_rect.left() + slice.x0, y,
                                        std::max(0.0f, slice.x1 - slice.x0),
                                        thickness));
            }
            if (paint.style.strikethrough) {
                const qreal y = text_rect.top() + line.y + line.baseline -
                                line.ascent * 0.35;
                painter.drawRect(QRectF(text_rect.left() + slice.x0, y,
                                        std::max(0.0f, slice.x1 - slice.x0),
                                        thickness));
            }
            painter.restore();
        }
    }
}


static std::vector<QPainterPath> exact_rich_text_stroke_paths(
    const ExactRichTextRaster &raster, const Layer &layer, double local_time)
{
    std::vector<QPainterPath> sources;
    sources.reserve(raster.pieces.size());
    for (const ExactRichTextGlyphPiece &piece : raster.pieces)
        sources.push_back(piece.path);
    return apply_layer_trim_paths_partitioned(layer, local_time, sources);
}

static void draw_exact_rich_text_strokes(QPainter &painter,
                                         const ExactRichTextRaster &raster,
                                         const QRectF &text_rect,
                                         const Layer &layer,
                                         double local_time,
                                         bool on_front,
                                         size_t cluster_begin = 0,
                                         size_t cluster_count =
                                             std::numeric_limits<size_t>::max(),
                                         const QPointF &surface_origin = QPointF(),
                                         const std::vector<QPainterPath> *prepared_paths = nullptr)
{
    if (!raster.valid || !painter.device())
        return;
    const QSize surface_size(painter.device()->width(), painter.device()->height());
    if (surface_size.isEmpty())
        return;
    const size_t cluster_end = cluster_count == std::numeric_limits<size_t>::max()
        ? std::numeric_limits<size_t>::max()
        : cluster_begin + cluster_count;
    const auto cluster_selected = [&](size_t cluster_index) {
        return cluster_index >= cluster_begin && cluster_index < cluster_end;
    };

    std::vector<QPainterPath> generated_paths;
    if (!prepared_paths) {
        generated_paths = exact_rich_text_stroke_paths(raster, layer, local_time);
        prepared_paths = &generated_paths;
    }
    const std::vector<QPainterPath> &stroke_piece_paths = *prepared_paths;

    for (size_t paint_index = 0; paint_index < raster.paint_runs.size();
         ++paint_index) {
        const RichTextStroke &stroke =
            raster.paint_runs[paint_index].style.stroke;
        if (!stroke.enabled || stroke.width <= 0.0001f ||
            stroke.on_front != on_front)
            continue;

        QImage stroke_layer(surface_size, QImage::Format_ARGB32_Premultiplied);
        stroke_layer.fill(Qt::transparent);
        {
            QPainter stroke_painter(&stroke_layer);
            stroke_painter.setRenderHint(QPainter::Antialiasing,
                                         stroke.antialias);
            stroke_painter.translate(-surface_origin.x(), -surface_origin.y());
            stroke_painter.setPen(rich_text_stroke_pen(stroke, text_rect));
            stroke_painter.setBrush(Qt::NoBrush);
            QPainterPath continuous_path;
            for (std::size_t piece_index = 0;
                 piece_index < raster.pieces.size(); ++piece_index) {
                const ExactRichTextGlyphPiece &piece = raster.pieces[piece_index];
                if (piece.paint_index != paint_index)
                    continue;
                if (!cluster_selected(piece.cluster_index))
                    continue;
                const QPainterPath &stroke_path =
                    piece_index < stroke_piece_paths.size()
                        ? stroke_piece_paths[piece_index]
                        : piece.path;
                if (stroke_path.isEmpty())
                    continue;
                if (!piece.clip_required) {
                    continuous_path.addPath(stroke_path);
                    continue;
                }
                /* A split ligature still needs a style boundary, but the
                 * boundary must include the complete antialiased stroke
                 * support. Clipping exactly at the logical split recreates the
                 * visible vertical 'invisible box' seam. */
                const qreal stroke_guard = std::max<qreal>(
                    2.0, stroke.alignment == 1
                        ? stroke.width * 0.5 + 2.0
                        : stroke.width + 2.0);
                stroke_painter.save();
                stroke_painter.setClipRect(
                    piece.clip.adjusted(-stroke_guard, -stroke_guard,
                                        stroke_guard, stroke_guard),
                    Qt::IntersectClip);
                stroke_painter.drawPath(stroke_path);
                stroke_painter.restore();
            }
            /* Draw all ordinary glyph subpaths in one operation. This prevents
             * semi-transparent strokes from accumulating darker seams where
             * neighbouring glyph outlines overlap. */
            if (!continuous_path.isEmpty())
                stroke_painter.drawPath(continuous_path);
        }

        if (stroke.alignment != 1) {
            QImage glyph_mask(surface_size,
                              QImage::Format_ARGB32_Premultiplied);
            glyph_mask.fill(Qt::transparent);
            {
                QPainter mask_painter(&glyph_mask);
                mask_painter.setRenderHint(QPainter::Antialiasing,
                                            stroke.antialias);
                mask_painter.translate(-surface_origin.x(), -surface_origin.y());
                mask_painter.setPen(Qt::NoPen);
                mask_painter.setBrush(Qt::white);
                QPainterPath continuous_mask;
                for (const ExactRichTextGlyphPiece &piece : raster.pieces) {
                    if (piece.paint_index != paint_index)
                        continue;
                    if (!cluster_selected(piece.cluster_index))
                        continue;
                    if (!piece.clip_required) {
                        continuous_mask.addPath(piece.path);
                        continue;
                    }
                    mask_painter.save();
                    mask_painter.setClipRect(piece.clip, Qt::IntersectClip);
                    mask_painter.drawPath(piece.path);
                    mask_painter.restore();
                }
                if (!continuous_mask.isEmpty())
                    mask_painter.drawPath(continuous_mask);
            }
            QPainter isolate(&stroke_layer);
            isolate.setCompositionMode(
                stroke.alignment == 0
                    ? QPainter::CompositionMode_DestinationOut
                    : QPainter::CompositionMode_DestinationIn);
            isolate.drawImage(QPointF(0.0, 0.0), glyph_mask);
        }
        painter.drawImage(surface_origin, stroke_layer);
    }
}

static QRect exact_text_transition_unit_render_bounds(
    const ExactRichTextRaster &raster, const Layer &layer,
    const QRectF &text_rect, double local_time, size_t cluster_index,
    const ShadowRenderParams &shadow_params, bool render_shadow,
    const QRect &image_bounds)
{
    if (!raster.valid || cluster_index >= raster.layout->clusters.size())
        return {};

    QRectF bounds;
    bool has_bounds = false;
    for (const ExactRichTextGlyphPiece &piece : raster.pieces) {
        if (piece.cluster_index != cluster_index)
            continue;
        QRectF piece_bounds = piece.path.boundingRect();
        if (piece.clip_required)
            piece_bounds = piece_bounds.intersected(piece.clip);
        if (piece_bounds.isEmpty())
            continue;
        bounds = has_bounds ? bounds.united(piece_bounds) : piece_bounds;
        has_bounds = true;
    }

    const TextLayoutCluster &cluster = raster.layout->clusters[cluster_index];
    const QRectF logical_bounds(text_rect.left() + cluster.x,
                                text_rect.top() + cluster.y,
                                std::max(1.0f, cluster.width),
                                std::max(1.0f, cluster.height));
    bounds = has_bounds ? bounds.united(logical_bounds) : logical_bounds;

    double pad = std::max(
        6.0, max_rich_text_stroke_width(layer, local_time) * 2.0 + 4.0);
    if (render_shadow) {
        const double drop_extent =
            std::max(std::abs(shadow_params.dx), std::abs(shadow_params.dy)) +
            shadow_params.blur * 3.0 + std::max(0.0, shadow_params.spread);
        const double long_extent = std::max(0.0, shadow_params.long_length) +
                                   std::max(0.0, shadow_params.long_blur) * 3.0;
        pad += std::max(drop_extent, long_extent);
    }
    return bounds.adjusted(-pad, -pad, pad, pad)
        .toAlignedRect().intersected(image_bounds);
}

static QImage render_exact_rich_text_transition_unit(
    const ExactRichTextRaster &raster, const Layer &layer,
    const QRectF &text_rect, double local_time, const QRect &render_bounds,
    const ShadowRenderParams &shadow_params, bool render_shadow,
    size_t cluster_index,
    const std::vector<QPainterPath> *prepared_stroke_paths)
{
    if (!raster.valid || render_bounds.isEmpty())
        return {};

    QImage unit_image(render_bounds.size(), QImage::Format_ARGB32_Premultiplied);
    unit_image.fill(Qt::transparent);
    QPainter painter(&unit_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.translate(-render_bounds.x(), -render_bounds.y());
    painter.setClipRect(QRectF(render_bounds));

    if (render_shadow) {
        QImage shadow_mask(render_bounds.size(),
                           QImage::Format_ARGB32_Premultiplied);
        shadow_mask.fill(Qt::transparent);
        QPainter mask_painter(&shadow_mask);
        mask_painter.setRenderHint(QPainter::Antialiasing, true);
        mask_painter.setRenderHint(QPainter::TextAntialiasing, true);
        mask_painter.translate(-render_bounds.x(), -render_bounds.y());
        draw_exact_rich_text_fill(mask_painter, raster, text_rect, true,
                                  cluster_index, 1);
        mask_painter.end();
        const QString shape_key = QStringLiteral("exact-text-transition|%1,%2,%3x%4|%5")
            .arg(render_bounds.x()).arg(render_bounds.y())
            .arg(render_bounds.width()).arg(render_bounds.height())
            .arg(image_content_hash(shadow_mask));
        CachedShadowImage shadow = build_shadow_image(
            layer, shadow_params, shape_key, shadow_mask);
        painter.save();
        painter.setClipping(false);
        painter.drawImage(QPointF(render_bounds.topLeft()) + shadow.origin,
                          shadow.image);
        painter.restore();
    }

    draw_exact_rich_text_strokes(
        painter, raster, text_rect, layer, local_time, false, cluster_index, 1,
        render_bounds.topLeft(), prepared_stroke_paths);
    draw_exact_rich_text_fill(
        painter, raster, text_rect, false, cluster_index, 1);
    draw_exact_rich_text_strokes(
        painter, raster, text_rect, layer, local_time, true, cluster_index, 1,
        render_bounds.topLeft(), prepared_stroke_paths);
    painter.end();
    return unit_image;
}

static void draw_rich_text_document(QPainter &painter, const Layer &layer, const QFont &font,
                                    const QRectF &text_rect, double t,
                                    const QPen *text_outline = nullptr,
                                    bool outline_only = false,
                                    bool alpha_mask_only = false,
                                    const TextTransitionUnitRange *visible_range = nullptr)
{
    auto doc = rich_text_document_for_layer(layer, font, text_rect, t, visible_range);
    if (text_outline || alpha_mask_only) {
        QTextCursor cursor(doc.get());
        if (visible_range) {
            const int text_length = std::max(0, doc->characterCount() - 1);
            const int start = std::clamp(visible_range->start, 0, text_length);
            const int end = std::clamp(start + std::max(0, visible_range->length), start, text_length);
            cursor.setPosition(start);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
        } else {
            cursor.select(QTextCursor::Document);
        }
        QTextCharFormat format;
        if (text_outline)
            format.setTextOutline(*text_outline);
        if (outline_only)
            format.setForeground(QBrush(Qt::transparent));
        else if (alpha_mask_only) {
            // A stroke-alignment mask must not inherit inline fill colours,
            // gradients or opacity.  Use one opaque white glyph silhouette so
            // DestinationIn/DestinationOut isolate the exact inner/outer half.
            format.setForeground(QBrush(Qt::white));
            format.setTextOutline(QPen(Qt::NoPen));
        }
        cursor.mergeCharFormat(format);
        if (text_outline)
            clear_global_outline_on_local_stroke_ranges(*doc, layer, t, visible_range);
    }
    QSizeF doc_size = doc->size();
    const double scale = rich_text_document_horizontal_scale(
        layer, text_rect, doc_size, t);
    const QPointF origin = rich_text_document_origin(layer, text_rect, doc_size, t, scale);
    if (std::abs(scale - 1.0) > 0.0001) {
        painter.save();
        painter.translate(origin);
        painter.scale(scale, 1.0);
        doc->drawContents(&painter, QRectF(QPointF(0, 0), doc_size));
        painter.restore();
        return;
    }
    painter.save();
    painter.translate(origin);
    doc->drawContents(&painter, QRectF(QPointF(0, 0), doc_size));
    painter.restore();
}

static void draw_prepared_rich_text_document(QPainter &painter,
                                                const QTextDocument &source_document,
                                                const Layer &layer,
                                                const QRectF &text_rect,
                                                double t,
                                                const TextTransitionUnitRange &visible_range,
                                                const QPen *text_outline = nullptr,
                                                bool outline_only = false,
                                                bool alpha_mask_only = false)
{
    std::unique_ptr<QTextDocument> modified_document;
    QTextDocument *document = const_cast<QTextDocument *>(&source_document);
    if (text_outline || alpha_mask_only) {
        modified_document.reset(source_document.clone());
        document = modified_document.get();
        const int text_length = std::max(0, document->characterCount() - 1);
        const int start = std::clamp(visible_range.start, 0, text_length);
        const int end = std::clamp(start + std::max(0, visible_range.length), start, text_length);
        QTextCursor cursor(document);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        if (text_outline)
            format.setTextOutline(*text_outline);
        if (outline_only)
            format.setForeground(QBrush(Qt::transparent));
        else if (alpha_mask_only) {
            format.setForeground(QBrush(Qt::white));
            format.setTextOutline(QPen(Qt::NoPen));
        }
        cursor.mergeCharFormat(format);
        if (text_outline)
            clear_global_outline_on_local_stroke_ranges(*document, layer, t, &visible_range);
    }

    const QSizeF document_size = document->size();
    const double scale = rich_text_document_horizontal_scale(
        layer, text_rect, document_size, t);
    const QPointF origin = rich_text_document_origin(layer, text_rect, document_size, t, scale);
    if (std::abs(scale - 1.0) > 0.0001) {
        painter.save();
        painter.translate(origin);
        painter.scale(scale, 1.0);
        document->drawContents(&painter, QRectF(QPointF(0, 0), document_size));
        painter.restore();
        return;
    }

    painter.save();
    painter.translate(origin);
    document->drawContents(&painter, QRectF(QPointF(0, 0), document_size));
    painter.restore();
}


struct RichTextStrokeRenderGroup {
    bool on_front = false;
    int alignment = 0;
    bool antialias = true;
};

static bool same_rich_text_stroke_group(const RichTextStrokeRenderGroup &a,
                                        const RichTextStrokeRenderGroup &b)
{
    return a.on_front == b.on_front && a.alignment == b.alignment &&
           a.antialias == b.antialias;
}

static std::vector<RichTextStrokeRenderGroup>
rich_text_stroke_render_groups(const RichTextDocument &model)
{
    std::vector<RichTextStrokeRenderGroup> groups;
    for (const TextLayoutPaintRun &run : text_layout_paint_runs(model)) {
        const RichTextStroke &stroke = run.style.stroke;
        if (!stroke.enabled || stroke.width <= 0.0001f)
            continue;
        RichTextStrokeRenderGroup group;
        group.on_front = stroke.on_front;
        group.alignment = std::clamp(stroke.alignment, 0, 2);
        group.antialias = stroke.antialias;
        if (std::none_of(groups.begin(), groups.end(),
                         [&](const RichTextStrokeRenderGroup &existing) {
                             return same_rich_text_stroke_group(existing, group);
                         }))
            groups.push_back(group);
    }
    return groups;
}

static void draw_positioned_rich_text_document(QPainter &painter,
                                                QTextDocument &document,
                                                const Layer &layer,
                                                const QRectF &text_rect,
                                                double t)
{
    const QSizeF document_size = document.size();
    const double scale = rich_text_document_horizontal_scale(
        layer, text_rect, document_size, t);
    const QPointF origin = rich_text_document_origin(
        layer, text_rect, document_size, t, scale);
    painter.save();
    painter.translate(origin);
    if (std::abs(scale - 1.0) > 0.0001)
        painter.scale(scale, 1.0);
    document.drawContents(&painter,
                          QRectF(QPointF(0, 0), document_size));
    painter.restore();
}

static std::unique_ptr<QTextDocument>
rich_text_fill_only_document(const QTextDocument &source)
{
    std::unique_ptr<QTextDocument> document(source.clone());
    QTextCursor cursor(document.get());
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    format.setTextOutline(QPen(Qt::NoPen));
    cursor.mergeCharFormat(format);
    return document;
}

static std::unique_ptr<QTextDocument> rich_text_stroke_group_document(
    const QTextDocument &source, const RichTextDocument &model,
    const QRectF &text_rect, const RichTextStrokeRenderGroup &group,
    bool glyph_mask_only, const TextTransitionUnitRange *visible_range)
{
    std::unique_ptr<QTextDocument> document(source.clone());
    QTextCursor all(document.get());
    all.select(QTextCursor::Document);
    QTextCharFormat hidden;
    hidden.setForeground(QBrush(Qt::transparent));
    hidden.setTextOutline(QPen(Qt::NoPen));
    hidden.setFontUnderline(false);
    hidden.setFontStrikeOut(false);
    all.mergeCharFormat(hidden);

    const QString qplain = document->toPlainText();
    const SourceQtUtf8PositionMap positions(qplain);
    const int text_length = std::max(0, document->characterCount() - 1);
    const int visible_start = visible_range
        ? std::clamp(visible_range->start, 0, text_length)
        : 0;
    const int visible_end = visible_range
        ? std::clamp(visible_start + std::max(0, visible_range->length),
                     visible_start, text_length)
        : text_length;

    for (const TextLayoutPaintRun &run : text_layout_paint_runs(model)) {
        const RichTextStroke &stroke = run.style.stroke;
        if (!stroke.enabled || stroke.width <= 0.0001f ||
            stroke.on_front != group.on_front ||
            std::clamp(stroke.alignment, 0, 2) != group.alignment ||
            stroke.antialias != group.antialias)
            continue;

        int start = positions.qpos_from_byte(run.byte_start);
        int end = positions.qpos_from_byte(run.byte_start + run.byte_length);
        start = std::max(start, visible_start);
        end = std::min(end, visible_end);
        if (end <= start)
            continue;

        QTextCursor cursor(document.get());
        cursor.setPosition(std::clamp(start, 0, text_length));
        cursor.setPosition(std::clamp(end, 0, text_length),
                           QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setFontUnderline(false);
        format.setFontStrikeOut(false);
        if (glyph_mask_only) {
            format.setForeground(QBrush(Qt::white));
            format.setTextOutline(QPen(Qt::NoPen));
        } else {
            format.setForeground(QBrush(Qt::transparent));
            format.setTextOutline(rich_text_stroke_pen(stroke, text_rect));
        }
        cursor.mergeCharFormat(format);
    }
    return document;
}

static QImage render_rich_text_stroke_group(
    const QTextDocument &source, const RichTextDocument &model,
    const Layer &layer, const QRectF &text_rect, double t,
    const QSize &surface_size, const QPointF &surface_origin,
    const RichTextStrokeRenderGroup &group,
    const TextTransitionUnitRange *visible_range = nullptr)
{
    if (surface_size.isEmpty())
        return {};

    QImage stroke_layer(surface_size,
                        QImage::Format_ARGB32_Premultiplied);
    stroke_layer.fill(Qt::transparent);
    {
        std::unique_ptr<QTextDocument> stroke_document =
            rich_text_stroke_group_document(
                source, model, text_rect, group, false, visible_range);
        QPainter stroke_painter(&stroke_layer);
        stroke_painter.setRenderHint(QPainter::Antialiasing,
                                     group.antialias);
        stroke_painter.setRenderHint(QPainter::TextAntialiasing,
                                     group.antialias);
        stroke_painter.translate(-surface_origin.x(), -surface_origin.y());
        draw_positioned_rich_text_document(
            stroke_painter, *stroke_document, layer, text_rect, t);
    }

    if (group.alignment != 1) {
        QImage glyph_mask(surface_size,
                          QImage::Format_ARGB32_Premultiplied);
        glyph_mask.fill(Qt::transparent);
        {
            std::unique_ptr<QTextDocument> mask_document =
                rich_text_stroke_group_document(
                    source, model, text_rect, group, true, visible_range);
            QPainter mask_painter(&glyph_mask);
            mask_painter.setRenderHint(QPainter::Antialiasing,
                                       group.antialias);
            mask_painter.setRenderHint(QPainter::TextAntialiasing,
                                       group.antialias);
            mask_painter.translate(-surface_origin.x(), -surface_origin.y());
            draw_positioned_rich_text_document(
                mask_painter, *mask_document, layer, text_rect, t);
        }

        QPainter isolate(&stroke_layer);
        isolate.setCompositionMode(
            group.alignment == 0
                ? QPainter::CompositionMode_DestinationOut
                : QPainter::CompositionMode_DestinationIn);
        isolate.drawImage(QPointF(0.0, 0.0), glyph_mask);
    }
    return stroke_layer;
}

static constexpr int kTextAnimatorBlurType =
    static_cast<int>(ShadowBlurType::StackFast);

/* Text-transition blur is not a private approximation. It is an image adapter
 * over the same premultiplied-color backend used by the built-in Blur effect. */
static QImage integrated_text_animator_blur_image(const QImage &source,
                                                  double radius)
{
    if (source.isNull() || radius <= 0.0)
        return source;
    QImage blurred = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width = blurred.width();
    const int height = blurred.height();
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::memcpy(pixels.data() + static_cast<size_t>(y) * width * 4,
                    blurred.constScanLine(y), static_cast<size_t>(width) * 4);
    blur_premultiplied_pixels_for_type(
        pixels, width, height, std::clamp(radius, 0.0, 512.0),
        kTextAnimatorBlurType);
    for (int y = 0; y < height; ++y)
        std::memcpy(blurred.scanLine(y),
                    pixels.data() + static_cast<size_t>(y) * width * 4,
                    static_cast<size_t>(width) * 4);
    return blurred;
}

static double text_raster_ink_gutter(const QFont &font,
                                      const Layer &layer, double t)
{
    const QFontMetricsF metrics(font);
    const double stroke = std::max(
        std::max(0.0, eval_outline_width(layer, t)),
        max_rich_text_stroke_width(layer, t));
    /* QTextLine exposes advances, not the complete painted ink. Half a line
     * height safely covers italic bearings, swashes, combining marks and
     * antialiasing while remaining bounded for large text. */
    const double rich_height = max_rich_text_font_height_hint(layer, t);
    return std::ceil(std::max({4.0, metrics.height() * 0.5,
                               rich_height * 0.75}) +
                     stroke * 2.0 + 6.0);
}

static QRect transition_image_alpha_bounds(const QImage &image)
{
    if (image.isNull())
        return QRect();

    int min_x = image.width();
    int min_y = image.height();
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) == 0)
                continue;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }
    if (max_x < min_x || max_y < min_y)
        return QRect();
    return QRect(QPoint(min_x, min_y), QPoint(max_x, max_y));
}

static QImage render_rich_text_transition_unit(const QTextDocument &isolated_document,
                                               const Layer &layer,
                                               double t,
                                               const QFont &font,
                                               const QRectF &text_rect,
                                               const QRect &render_bounds,
                                               const ShadowRenderParams &shadow_params,
                                               bool render_shadow,
                                               const TextTransitionUnitRange &range)
{
    if (render_bounds.isEmpty())
        return {};

    QImage unit_image(render_bounds.size(), QImage::Format_ARGB32_Premultiplied);
    unit_image.fill(Qt::transparent);

    QPainter painter(&unit_image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setFont(font);
    painter.translate(-render_bounds.x(), -render_bounds.y());

    painter.save();
    /* The isolated surface is already bounded to this shaped unit's generous
     * ink envelope. Clipping again to the logical text box cuts italic
     * bearings, accents, swashes and stroke pixels before animation starts. */
    painter.setClipRect(QRectF(render_bounds));

    if (render_shadow) {
        QImage shadow_mask(render_bounds.size(),
                           QImage::Format_ARGB32_Premultiplied);
        shadow_mask.fill(Qt::transparent);
        QPainter mask_painter(&shadow_mask);
        mask_painter.setRenderHint(QPainter::Antialiasing, true);
        mask_painter.setRenderHint(QPainter::TextAntialiasing, true);
        mask_painter.setPen(Qt::NoPen);
        mask_painter.setBrush(Qt::white);
        mask_painter.translate(-render_bounds.x(), -render_bounds.y());
        draw_prepared_rich_text_document(mask_painter, isolated_document,
                                         layer, text_rect, t, range);
        mask_painter.end();
        const QString shape_key =
            QStringLiteral("text-transition|%1,%2,%3x%4|%5")
                .arg(render_bounds.x()).arg(render_bounds.y())
                .arg(render_bounds.width()).arg(render_bounds.height())
                .arg(image_content_hash(shadow_mask));
        CachedShadowImage shadow = build_shadow_image(
            layer, shadow_params, shape_key, shadow_mask);
        painter.save();
        painter.setClipping(false);
        painter.drawImage(QPointF(render_bounds.topLeft()) + shadow.origin,
                          shadow.image);
        painter.restore();
    }

    QColor fill = color_from_argb(eval_text_color(layer, t));
    fill.setAlphaF(std::clamp((double)fill.alphaF(), 0.0, 1.0));
    const RichTextDocument stroke_model =
        rich_text_model_for_source_time(layer, t);
    const std::vector<RichTextStrokeRenderGroup> stroke_groups =
        rich_text_stroke_render_groups(stroke_model);

    auto draw_text_fill = [&]() {
        std::unique_ptr<QTextDocument> fill_document =
            rich_text_fill_only_document(isolated_document);
        painter.save();
        painter.setOpacity(fill.alphaF());
        draw_positioned_rich_text_document(
            painter, *fill_document, layer, text_rect, t);
        painter.restore();
    };
    auto draw_stroke_phase = [&](bool on_front) {
        for (const RichTextStrokeRenderGroup &group : stroke_groups) {
            if (group.on_front != on_front)
                continue;
            const QImage stroke_layer = render_rich_text_stroke_group(
                isolated_document, stroke_model, layer, text_rect, t,
                render_bounds.size(), render_bounds.topLeft(), group, &range);
            if (!stroke_layer.isNull())
                painter.drawImage(QPointF(render_bounds.topLeft()),
                                  stroke_layer);
        }
    };

    draw_stroke_phase(false);
    draw_text_fill();
    draw_stroke_phase(true);

    painter.restore();
    painter.end();
    return unit_image;
}

static bool layer_has_non_transform_animation(const Layer &layer);

struct IsolatedTextUnitCache {
    struct BlurredVariant {
        int radius = 0;
        QImage crop;
        QRect bounds;
    };

    QString key;
    QString rejected_key;
    // Store only each unit's non-transparent crop. Keeping full layer-sized
    // images per character can retain hundreds of megabytes.
    QVector<TextTransitionUnitRange> ranges;
    QVector<QImage> source_crops;
    QVector<QRect> source_bounds;
    QVector<QPointF> placement_offsets;
    QVector<QVector<BlurredVariant>> blurred_variants;
    qsizetype source_bytes = 0;
    qsizetype blurred_bytes = 0;
    BlurredVariant scratch_variant;
    uint64_t last_used = 0;
};

static double source_frame_duration();

struct IsolatedTextUnitCachePool {
    std::unordered_map<std::string, IsolatedTextUnitCache> entries;
    uint64_t tick = 0;
};

static qsizetype isolated_text_unit_cache_bytes(const IsolatedTextUnitCache &cache)
{
    return cache.source_bytes + cache.blurred_bytes + cache.scratch_variant.crop.sizeInBytes();
}

static void evict_isolated_text_unit_caches(IsolatedTextUnitCachePool &pool,
                                             const std::string &protected_cache_id)
{
    constexpr size_t kMaxCachedTextLayers = 8;
    constexpr qsizetype kMaxTextTransitionCacheBytes = 128 * 1024 * 1024;
    qsizetype total_bytes = 0;
    for (const auto &entry : pool.entries)
        total_bytes += isolated_text_unit_cache_bytes(entry.second);

    while ((pool.entries.size() > kMaxCachedTextLayers ||
            total_bytes > kMaxTextTransitionCacheBytes) &&
           pool.entries.size() > 1) {
        auto oldest = pool.entries.end();
        for (auto it = pool.entries.begin(); it != pool.entries.end(); ++it) {
            if (it->first == protected_cache_id)
                continue;
            if (oldest == pool.entries.end() ||
                it->second.last_used < oldest->second.last_used)
                oldest = it;
        }
        if (oldest == pool.entries.end())
            break;
        total_bytes -= isolated_text_unit_cache_bytes(oldest->second);
        pool.entries.erase(oldest);
    }
}

/* Generic compatibility-raster adapter for the unified TextAnimator model.
 *
 * The persistent SDF renderer is the primary path. Color fonts, unusually
 * large glyphs and backend alpha-map failures intentionally fall back to Qt's
 * exact text rasterizer. That fallback must still consume the same shaped
 * cluster map and TextAnimatorEvaluation; reviving LayerTransition-specific
 * drawing here would create a second runtime animation engine.
 */
static bool apply_unified_text_animator_raster(
    QImage &image, const QImage &static_background, const Layer &layer,
    const QString &text, const QFont &font, const QRectF &text_rect,
    double local_time, const ShadowRenderParams &shadow_params,
    bool render_shadow,
    const ExactRichTextRaster *exact_raster = nullptr)
{
    if (image.isNull() || layer.type == LayerType::Ticker ||
        !text_animator_stack_has_enabled_animators(layer.text_animators))
        return false;

    const ImmutableTextLayout layout = source_text_layout_for_metrics(
        layer, local_time, text_rect.width(), text_rect.height(),
        layer.text_overflow_mode);
    if (!layout || !layout->valid || layout->clusters.empty())
        return false;

    const TextAnimatorEvaluation evaluation = evaluate_text_animators(
        layer.text_animators, *layout, local_time);
    if (evaluation.clusters.size() != layout->clusters.size())
        return false;

    static thread_local IsolatedTextUnitCachePool cache_pool;
    const std::string cache_id = layer.id.empty()
        ? std::string("__anonymous_unified_text_animator")
        : layer.id + "|unified";
    auto [cache_it, inserted] = cache_pool.entries.try_emplace(cache_id);
    (void)inserted;
    IsolatedTextUnitCache &cache = cache_it->second;
    cache.last_used = ++cache_pool.tick;
    cache.scratch_variant = {};
    evict_isolated_text_unit_caches(cache_pool, cache_id);

    const bool time_varying_source = layer_has_non_transform_animation(layer);
    const double frame_duration = std::max(1.0 / 240.0, source_frame_duration());
    const qlonglong evaluated_frame = time_varying_source
        ? static_cast<qlonglong>(std::llround(local_time / frame_duration)) : 0;
    const QByteArray text_digest = QCryptographicHash::hash(
        text.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString cache_key = QStringLiteral(
        "%1|unified|exact=%2|rev=%3|layout=%4:%5|%6x%7|text=%8|font=%9|"
        "rect=%10,%11,%12,%13|shadow=%14|frame=%15")
        .arg(QString::fromStdString(layer.id))
        .arg(exact_raster && exact_raster->valid ? 1 : 0)
        .arg(static_cast<qulonglong>(TitleDataStore::instance().revision()))
        .arg(static_cast<qulonglong>(layout->key.content))
        .arg(static_cast<qulonglong>(layout->key.geometry))
        .arg(image.width()).arg(image.height())
        .arg(QString::fromLatin1(text_digest))
        .arg(font.toString())
        .arg(text_rect.x(), 0, 'f', 3)
        .arg(text_rect.y(), 0, 'f', 3)
        .arg(text_rect.width(), 0, 'f', 3)
        .arg(text_rect.height(), 0, 'f', 3)
        .arg(render_shadow ? 1 : 0)
        .arg(evaluated_frame);

    constexpr qsizetype kMaxIsolatedTextClusters = 512;
    constexpr qsizetype kMaxSourceCropBytes = 48 * 1024 * 1024;
    if (cache.rejected_key == cache_key)
        return false;

    if (cache.key != cache_key ||
        cache.source_crops.size() != static_cast<qsizetype>(layout->clusters.size())) {
        cache.key.clear();
        cache.ranges.clear();
        cache.source_crops.clear();
        cache.source_bounds.clear();
        cache.placement_offsets.clear();
        cache.blurred_variants.clear();
        cache.source_bytes = 0;
        cache.blurred_bytes = 0;

        if (layout->clusters.size() > static_cast<size_t>(kMaxIsolatedTextClusters)) {
            cache.rejected_key = cache_key;
            evict_isolated_text_unit_caches(cache_pool, cache_id);
            return false;
        }

        const bool use_exact_units = exact_raster && exact_raster->valid;
        const std::vector<QPainterPath> exact_stroke_paths = use_exact_units
            ? exact_rich_text_stroke_paths(*exact_raster, layer, local_time)
            : std::vector<QPainterPath>{};
        std::unique_ptr<QTextDocument> shaped_document;
        if (!use_exact_units) {
            shaped_document = rich_text_document_for_layer(
                layer, font, text_rect, local_time, nullptr);
        }
        if (!use_exact_units && !shaped_document) {
            cache.rejected_key = cache_key;
            return false;
        }
        const QString canonical_text = QString::fromUtf8(
            layout->text.data(), static_cast<int>(layout->text.size()));
        const SourceQtUtf8PositionMap positions(canonical_text);

        cache.ranges.reserve(static_cast<qsizetype>(layout->clusters.size()));
        cache.source_crops.reserve(static_cast<qsizetype>(layout->clusters.size()));
        cache.source_bounds.reserve(static_cast<qsizetype>(layout->clusters.size()));
        cache.placement_offsets.reserve(static_cast<qsizetype>(layout->clusters.size()));
        qsizetype source_crop_bytes = 0;

        for (size_t cluster_index = 0;
             cluster_index < layout->clusters.size(); ++cluster_index) {
            const TextLayoutCluster &cluster = layout->clusters[cluster_index];
            const int start = positions.qpos_from_byte(cluster.byte_start);
            const int end = positions.qpos_from_byte(
                cluster.byte_start + cluster.byte_length);
            const TextTransitionUnitRange range{
                start, std::max(0, end - start)};
            cache.ranges.push_back(range);

            const std::string cluster_text = layout->text.substr(
                cluster.byte_start, cluster.byte_length);
            const bool visible_cluster = !QString::fromUtf8(
                cluster_text.data(), static_cast<int>(cluster_text.size()))
                .trimmed().isEmpty();
            if (!visible_cluster || range.length <= 0) {
                cache.source_crops.push_back(QImage());
                cache.source_bounds.push_back(QRect());
                cache.placement_offsets.push_back(QPointF());
                continue;
            }

            std::unique_ptr<QTextDocument> isolated_document;
            QPointF placement_offset;
            QRect render_bounds;
            QImage unit_image;
            if (use_exact_units) {
                render_bounds = exact_text_transition_unit_render_bounds(
                    *exact_raster, layer, text_rect, local_time, cluster_index,
                    shadow_params, render_shadow, image.rect());
                unit_image = render_exact_rich_text_transition_unit(
                    *exact_raster, layer, text_rect, local_time, render_bounds,
                    shadow_params, render_shadow, cluster_index,
                    &exact_stroke_paths);
            } else {
                isolated_document = rich_text_document_for_layer(
                    layer, font, text_rect, local_time, &range);
                placement_offset = isolated_document
                    ? rich_text_transition_unit_kerning_offset(
                          *shaped_document, *isolated_document, layer, text_rect,
                          local_time, range)
                    : QPointF();
                render_bounds = text_transition_unit_render_bounds(
                    *shaped_document, layer, text_rect, local_time, range,
                    shadow_params, render_shadow, image.rect());
                unit_image = isolated_document
                    ? render_rich_text_transition_unit(
                          *isolated_document, layer, local_time, font, text_rect,
                          render_bounds, shadow_params, render_shadow, range)
                    : QImage();
            }
            const QRect ink_bounds = transition_image_alpha_bounds(unit_image)
                .intersected(unit_image.rect());
            constexpr int kTextAnimatorResamplingGutter = 4;
            const QRect local_bounds = ink_bounds.isEmpty()
                ? QRect()
                : ink_bounds.adjusted(-kTextAnimatorResamplingGutter,
                                       -kTextAnimatorResamplingGutter,
                                       kTextAnimatorResamplingGutter,
                                       kTextAnimatorResamplingGutter)
                      .intersected(unit_image.rect());
            const QRect bounds = local_bounds.isEmpty()
                ? QRect()
                : local_bounds.translated(render_bounds.topLeft());
            QImage source_crop = local_bounds.isEmpty()
                ? QImage() : unit_image.copy(local_bounds);
            source_crop_bytes += source_crop.sizeInBytes();
            if (source_crop_bytes > kMaxSourceCropBytes) {
                cache.key.clear();
                cache.rejected_key = cache_key;
                cache.ranges.clear();
                cache.source_crops.clear();
                cache.source_bounds.clear();
                cache.placement_offsets.clear();
                cache.blurred_variants.clear();
                cache.source_bytes = 0;
                cache.blurred_bytes = 0;
                evict_isolated_text_unit_caches(cache_pool, cache_id);
                return false;
            }
            cache.source_bounds.push_back(bounds);
            cache.placement_offsets.push_back(placement_offset);
            cache.source_crops.push_back(std::move(source_crop));
        }
        cache.source_bytes = source_crop_bytes;
        cache.key = cache_key;
        cache.rejected_key.clear();
        cache.blurred_variants.resize(cache.source_crops.size());
        cache.scratch_variant = {};
        evict_isolated_text_unit_caches(cache_pool, cache_id);
    }

    auto blurred_variant_for = [&image, &cache, &cache_id](int index, int radius)
        -> const IsolatedTextUnitCache::BlurredVariant * {
        radius = std::clamp(radius, 0, 64);
        if (radius <= 0 || index < 0 || index >= cache.source_crops.size())
            return nullptr;
        QVector<IsolatedTextUnitCache::BlurredVariant> &variants =
            cache.blurred_variants[index];
        for (const auto &variant : variants) {
            if (variant.radius == radius)
                return &variant;
        }
        const QImage &source_crop = cache.source_crops[index];
        const QRect source_bounds = cache.source_bounds[index];
        if (source_crop.isNull() || source_bounds.isEmpty())
            return nullptr;

        const int blur_pad =
            blur_support_radius_for_type(radius, kTextAnimatorBlurType) + 4;
        QImage padded(source_crop.width() + blur_pad * 2,
                      source_crop.height() + blur_pad * 2,
                      QImage::Format_ARGB32_Premultiplied);
        padded.fill(Qt::transparent);
        {
            QPainter padded_painter(&padded);
            padded_painter.drawImage(QPoint(blur_pad, blur_pad), source_crop);
        }
        QImage blurred = integrated_text_animator_blur_image(padded, radius);
        const QRect local_bounds = transition_image_alpha_bounds(blurred);
        if (local_bounds.isEmpty())
            return nullptr;
        const QPoint padded_origin = source_bounds.topLeft() -
                                     QPoint(blur_pad, blur_pad);
        const QRect global_bounds(padded_origin + local_bounds.topLeft(),
                                  local_bounds.size());
        const QRect clipped_bounds = global_bounds.intersected(image.rect());
        if (clipped_bounds.isEmpty())
            return nullptr;
        const QRect local_clip(
            clipped_bounds.topLeft() - global_bounds.topLeft(),
            clipped_bounds.size());
        const QRect blurred_crop_rect(
            local_bounds.topLeft() + local_clip.topLeft(), local_clip.size());
        QImage cropped_blur = blurred.copy(blurred_crop_rect);
        const qsizetype variant_bytes = cropped_blur.sizeInBytes();

        constexpr int kMaxBlurVariantsPerUnit = 4;
        constexpr qsizetype kMaxBlurCacheBytes = 48 * 1024 * 1024;
        if (variant_bytes > kMaxBlurCacheBytes) {
            cache.scratch_variant = {
                radius, std::move(cropped_blur), clipped_bounds};
            return &cache.scratch_variant;
        }
        if (cache.blurred_bytes + variant_bytes > kMaxBlurCacheBytes) {
            for (auto &unit_variants : cache.blurred_variants)
                unit_variants.clear();
            cache.blurred_bytes = 0;
        }
        while (variants.size() >= kMaxBlurVariantsPerUnit) {
            cache.blurred_bytes -= variants.front().crop.sizeInBytes();
            variants.removeAt(0);
        }
        variants.push_back({radius, std::move(cropped_blur), clipped_bounds});
        cache.blurred_bytes += variants.back().crop.sizeInBytes();
        evict_isolated_text_unit_caches(cache_pool, cache_id);
        return &variants.back();
    };

    if (static_background.isNull())
        image.fill(Qt::transparent);
    else
        image = static_background.copy();

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    for (int index = 0; index < cache.source_crops.size(); ++index) {
        const QImage &unit_crop = cache.source_crops[index];
        const QRect source_bounds = cache.source_bounds[index];
        if (unit_crop.isNull() || source_bounds.isEmpty())
            continue;
        const TextAnimatorClusterState &state =
            evaluation.clusters[static_cast<size_t>(index)];
        const double reveal_opacity =
            state.reveal_direction == TextRevealDirection::None
                ? state.reveal : 1.0;
        const double opacity = std::clamp(
            state.opacity * state.visibility * reveal_opacity, 0.0, 1.0);
        if (opacity <= 0.0001)
            continue;

        const QPointF placement_offset = cache.placement_offsets.value(index);
        const QPointF source_origin = QPointF(source_bounds.topLeft()) +
                                      placement_offset;
        const TextLayoutCluster &cluster =
            layout->clusters[static_cast<size_t>(index)];
        const QPointF base_center = state.has_transform_origin
            ? QPointF(text_rect.left() + state.transform_origin_x,
                      text_rect.top() + state.transform_origin_y)
            : QRectF(text_rect.left() + cluster.x,
                     text_rect.top() + cluster.y,
                     std::max(1.0f, cluster.width),
                     std::max(1.0f, cluster.height)).center();
        const QPointF center = base_center +
                               QPointF(state.anchor_x, state.anchor_y);
        const double sx = state.scale_x * state.horizontal_scale;
        const double sy = state.scale_y * state.vertical_scale;
        const double rotation = state.rotation + state.character_rotation;
        const double shear = std::tan(std::clamp(
            state.skew * 3.14159265358979323846 / 180.0,
            -1.553343, 1.553343));

        painter.save();
        if (state.has_unit_clip_bounds) {
            painter.setClipRect(QRectF(
                text_rect.left() + state.unit_clip_x0,
                text_rect.top() + state.unit_clip_y0,
                state.unit_clip_x1 - state.unit_clip_x0,
                state.unit_clip_y1 - state.unit_clip_y0));
        }
        painter.translate(center.x() + state.position_x,
                          center.y() + state.position_y -
                              state.baseline_shift);
        painter.rotate(rotation);
        painter.rotate(state.skew_axis);
        painter.shear(shear, 0.0);
        painter.rotate(-state.skew_axis);
        painter.scale(sx, sy);
        painter.translate(-center.x(), -center.y());

        if (state.has_reveal_bounds &&
            state.reveal_direction != TextRevealDirection::None) {
            const double reveal = std::clamp(state.reveal, 0.0, 1.0);
            constexpr double margin = 1.5;
            QRectF visible(text_rect.left() + state.reveal_x0 - margin,
                           text_rect.top() + state.reveal_y0 - margin,
                           state.reveal_x1 - state.reveal_x0 + margin * 2.0,
                           state.reveal_y1 - state.reveal_y0 + margin * 2.0);
            switch (state.reveal_direction) {
            case TextRevealDirection::Right:
                visible.setLeft(visible.right() - visible.width() * reveal);
                break;
            case TextRevealDirection::Up:
                visible.setTop(visible.bottom() - visible.height() * reveal);
                break;
            case TextRevealDirection::Down:
                visible.setHeight(visible.height() * reveal);
                break;
            case TextRevealDirection::Left:
                visible.setWidth(visible.width() * reveal);
                break;
            case TextRevealDirection::None:
                break;
            }
            painter.setClipRect(visible, Qt::IntersectClip);
        }

        painter.setOpacity(opacity);
        const int blur_radius = std::clamp(
            static_cast<int>(std::lround(std::max(0.0, state.blur))), 0, 64);
        const IsolatedTextUnitCache::BlurredVariant *blurred_variant =
            blurred_variant_for(index, blur_radius);
        if (blurred_variant && !blurred_variant->crop.isNull()) {
            painter.drawImage(
                QPointF(blurred_variant->bounds.topLeft()) + placement_offset,
                blurred_variant->crop);
        } else {
            painter.drawImage(source_origin, unit_crop);
        }
        painter.restore();
    }
    painter.end();
    cache.scratch_variant = {};
    return true;
}


static bool apply_unified_text_animator_flattened(
    QImage &image, const QImage &static_background, const Layer &layer,
    const QFont &font, const QRectF &text_rect, double local_time,
    const ShadowRenderParams &shadow_params, bool render_shadow)
{
    if (image.isNull() ||
        !text_animator_stack_has_enabled_animators(layer.text_animators))
        return false;
    const ImmutableTextLayout layout = source_text_layout_for_metrics(
        layer, local_time, text_rect.width(), text_rect.height(),
        layer.text_overflow_mode);
    if (!layout || !layout->valid || layout->clusters.empty())
        return false;
    const TextAnimatorEvaluation evaluation = evaluate_text_animators(
        layer.text_animators, *layout, local_time);
    if (evaluation.clusters.size() != layout->clusters.size())
        return false;

    struct RasterUnit {
        std::vector<size_t> cluster_indices;
    };
    std::vector<RasterUnit> units;
    constexpr size_t kMaxFlattenedUnits = 512;
    if (layout->clusters.size() <= kMaxFlattenedUnits) {
        units.reserve(layout->clusters.size());
        for (size_t index = 0; index < layout->clusters.size(); ++index)
            units.push_back({{index}});
    } else {
        const TextAnimatorUnitMap unit_map = build_text_animator_unit_map(*layout);
        TextAnimatorUnit preferred = TextAnimatorUnit::Word;
        for (const TextAnimator &animator : layer.text_animators.animators) {
            if (animator.enabled) {
                preferred = animator.granularity;
                break;
            }
        }
        const std::vector<TextAnimatorUnit> candidates = {
            preferred, TextAnimatorUnit::Word, TextAnimatorUnit::Line,
            TextAnimatorUnit::WholeLayer};
        const std::vector<TextAnimatorUnitRange> *chosen = nullptr;
        for (TextAnimatorUnit candidate : candidates) {
            const auto &candidate_units = unit_map.units(candidate);
            if (!candidate_units.empty() &&
                candidate_units.size() <= kMaxFlattenedUnits) {
                chosen = &candidate_units;
                break;
            }
        }
        if (!chosen)
            return false;
        units.reserve(chosen->size());
        for (const TextAnimatorUnitRange &unit : *chosen) {
            RasterUnit raster_unit;
            if (!unit.cluster_indices.empty()) {
                for (size_t index : unit.cluster_indices) {
                    if (index < layout->clusters.size())
                        raster_unit.cluster_indices.push_back(index);
                }
            } else {
                const size_t begin =
                    std::min(unit.cluster_begin, layout->clusters.size());
                const size_t end = std::min(
                    begin + unit.cluster_count, layout->clusters.size());
                for (size_t index = begin; index < end; ++index)
                    raster_unit.cluster_indices.push_back(index);
            }
            units.push_back(std::move(raster_unit));
        }
    }

    const QImage source = image.copy();
    if (source.isNull())
        return false;
    if (static_background.isNull())
        image.fill(Qt::transparent);
    else
        image = static_background.copy();

    double clip_padding = std::max(
        text_raster_ink_gutter(font, layer, local_time),
        std::max(
            std::max(0.0, eval_outline_width(layer, local_time)),
            max_rich_text_stroke_width(layer, local_time)) * 2.0 + 6.0);
    if (render_shadow) {
        clip_padding += std::max(
            std::max(std::abs(shadow_params.dx), std::abs(shadow_params.dy)) +
                shadow_params.blur * 3.0 + std::max(0.0, shadow_params.spread),
            std::max(0.0, shadow_params.long_length) +
                std::max(0.0, shadow_params.long_blur) * 3.0);
    }

    std::unordered_map<int, QImage> blurred_sources;
    auto source_for_radius = [&](int radius) -> const QImage & {
        radius = std::clamp(radius, 0, 64);
        if (radius <= 0)
            return source;
        const auto found = blurred_sources.find(radius);
        if (found != blurred_sources.end())
            return found->second;
        auto [inserted, ok] = blurred_sources.emplace(
            radius, integrated_text_animator_blur_image(source, radius));
        (void)ok;
        return inserted->second;
    };

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const RasterUnit &unit : units) {
        if (unit.cluster_indices.empty())
            continue;
        double x0 = std::numeric_limits<double>::max();
        double y0 = std::numeric_limits<double>::max();
        double x1 = std::numeric_limits<double>::lowest();
        double y1 = std::numeric_limits<double>::lowest();
        for (size_t index : unit.cluster_indices) {
            if (index >= layout->clusters.size())
                continue;
            const TextLayoutCluster &cluster = layout->clusters[index];
            x0 = std::min(x0, static_cast<double>(cluster.x));
            y0 = std::min(y0, static_cast<double>(cluster.y));
            x1 = std::max(x1, static_cast<double>(cluster.x + cluster.width));
            y1 = std::max(y1, static_cast<double>(cluster.y + cluster.height));
        }
        if (!(x1 > x0 && y1 > y0))
            continue;
        const TextAnimatorClusterState &state =
            evaluation.clusters[unit.cluster_indices.front()];
        const double reveal_opacity =
            state.reveal_direction == TextRevealDirection::None
                ? state.reveal : 1.0;
        const double opacity = std::clamp(
            state.opacity * state.visibility * reveal_opacity, 0.0, 1.0);
        if (opacity <= 0.0001)
            continue;

        QRectF unit_bounds(text_rect.left() + x0, text_rect.top() + y0,
                           x1 - x0, y1 - y0);
        QRectF unit_clip = unit_bounds.adjusted(
            -clip_padding, -clip_padding, clip_padding, clip_padding);
        const QPointF base_center = state.has_transform_origin
            ? QPointF(text_rect.left() + state.transform_origin_x,
                      text_rect.top() + state.transform_origin_y)
            : unit_bounds.center();
        const QPointF center = base_center +
                               QPointF(state.anchor_x, state.anchor_y);
        const double sx = state.scale_x * state.horizontal_scale;
        const double sy = state.scale_y * state.vertical_scale;
        const double rotation = state.rotation + state.character_rotation;
        const double shear = std::tan(std::clamp(
            state.skew * 3.14159265358979323846 / 180.0,
            -1.553343, 1.553343));

        painter.save();
        if (state.has_unit_clip_bounds) {
            painter.setClipRect(QRectF(
                text_rect.left() + state.unit_clip_x0,
                text_rect.top() + state.unit_clip_y0,
                state.unit_clip_x1 - state.unit_clip_x0,
                state.unit_clip_y1 - state.unit_clip_y0));
        }
        painter.translate(center.x() + state.position_x,
                          center.y() + state.position_y - state.baseline_shift);
        painter.rotate(rotation);
        painter.rotate(state.skew_axis);
        painter.shear(shear, 0.0);
        painter.rotate(-state.skew_axis);
        painter.scale(sx, sy);
        painter.translate(-center.x(), -center.y());
        painter.setClipRect(unit_clip, Qt::IntersectClip);

        if (state.has_reveal_bounds &&
            state.reveal_direction != TextRevealDirection::None) {
            const double reveal = std::clamp(state.reveal, 0.0, 1.0);
            constexpr double margin = 1.5;
            QRectF visible(text_rect.left() + state.reveal_x0 - margin,
                           text_rect.top() + state.reveal_y0 - margin,
                           state.reveal_x1 - state.reveal_x0 + margin * 2.0,
                           state.reveal_y1 - state.reveal_y0 + margin * 2.0);
            switch (state.reveal_direction) {
            case TextRevealDirection::Right:
                visible.setLeft(visible.right() - visible.width() * reveal);
                break;
            case TextRevealDirection::Up:
                visible.setTop(visible.bottom() - visible.height() * reveal);
                break;
            case TextRevealDirection::Down:
                visible.setHeight(visible.height() * reveal);
                break;
            case TextRevealDirection::Left:
                visible.setWidth(visible.width() * reveal);
                break;
            case TextRevealDirection::None:
                break;
            }
            painter.setClipRect(visible, Qt::IntersectClip);
        }
        painter.setOpacity(opacity);
        const int blur_radius = std::clamp(
            static_cast<int>(std::lround(std::max(0.0, state.blur))), 0, 64);
        painter.drawImage(QPointF(0.0, 0.0), source_for_radius(blur_radius));
        painter.restore();
    }
    painter.end();
    return true;
}
