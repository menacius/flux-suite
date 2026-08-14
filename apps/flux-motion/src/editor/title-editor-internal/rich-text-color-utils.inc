private:
    int clamp_stop_index(int index) const { return std::clamp(index, 0, std::max(0, stop_count() - 1)); }
    QRectF preview_rect() const { return gradient_line_rect(); }
    QRectF gradient_line_rect() const
    {
        return QRectF(24, 42, std::max(1, width() - 48), 28);
    }
    QPointF stop_point(int index) const
    {
        const int i = clamp_stop_index(index);
        const QRectF line = gradient_line_rect();
        return QPointF(line.left() + positions_[i] * line.width(), line.center().y());
    }
    QRectF stop_handle_rect(int index) const
    {
        QPointF p = stop_point(index);
        return QRectF(p.x() - 10, p.y() - 20, 20, 48);
    }
    int hit_stop(const QPointF &pos) const
    {
        int best = -1;
        double best_distance = std::numeric_limits<double>::max();
        for (int i = 0; i < stop_count(); ++i) {
            if (!stop_handle_rect(i).adjusted(-4, -4, 4, 4).contains(pos))
                continue;
            const double distance = std::abs(pos.x() - stop_point(i).x());
            const bool prefer_selected = (i == selected_stop_ && best != selected_stop_);
            if (prefer_selected || distance < best_distance - 0.5 ||
                (std::abs(distance - best_distance) <= 0.5 &&
                 (best < 0 || (best != selected_stop_ && i < best)))) {
                best = i;
                best_distance = distance;
            }
        }
        return best;
    }

    std::vector<int> sorted_stop_indices() const
    {
        std::vector<int> indices;
        for (int i = 0; i < stop_count(); ++i) indices.push_back(i);
        std::sort(indices.begin(), indices.end(), [&](int a, int b) { return positions_[a] < positions_[b]; });
        return indices;
    }

    QColor interpolated_color(double pos) const
    {
        auto sorted = sorted_stop_indices();
        if (sorted.empty()) return QColor(255, 255, 255, 255);
        int left = sorted.front();
        int right = sorted.back();
        for (int i = 0; i + 1 < (int)sorted.size(); ++i) {
            if (positions_[sorted[i]] <= pos && pos <= positions_[sorted[i + 1]]) {
                left = sorted[i];
                right = sorted[i + 1];
                break;
            }
        }
        const double span = std::max(0.0001, positions_[right] - positions_[left]);
        const double t = std::clamp((pos - positions_[left]) / span, 0.0, 1.0);
        QColor a = colors_[left];
        QColor b = colors_[right];
        QColor out((int)std::round(a.red() + (b.red() - a.red()) * t),
                   (int)std::round(a.green() + (b.green() - a.green()) * t),
                   (int)std::round(a.blue() + (b.blue() - a.blue()) * t),
                   (int)std::round((stop_opacities_[left] + (stop_opacities_[right] - stop_opacities_[left]) * t) * 255.0));
        return out;
    }

    void changed()
    {
        update();
        if (gradient_changed) gradient_changed();
    }

    void set_all_stops(QGradient &gradient) const
    {
        for (int i = 0; i < stop_count(); ++i)
            gradient.setColorAt(std::clamp(positions_[i], 0.0, 1.0), stop_color(i));
    }

    QBrush preview_brush(const QRectF &r) const
    {
        // The editor ramp is always a left-to-right linear preview, like Photoshop.
        // The selected gradient type still controls the actual object/canvas rendering.
        QLinearGradient gradient(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
        set_all_stops(gradient);
        return QBrush(gradient);
    }

    static QString type_name(int type)
    {
        switch (type) {
        case 1: return fxm_tr("OBSTitles.Radial");
        case 2: return fxm_tr("OBSTitles.Conical");
        case 0:
        default: return fxm_tr("OBSTitles.Linear");
        }
    }

    static void draw_checkerboard(QPainter &p, const QRect &rect)
    {
        const int cell = 12;
        for (int y = rect.top(); y < rect.bottom(); y += cell)
            for (int x = rect.left(); x < rect.right(); x += cell)
                p.fillRect(QRect(x, y, cell, cell).intersected(rect),
                           ((x / cell) + (y / cell)) % 2 ? QColor(62, 62, 62) : QColor(82, 82, 82));
    }

    int type_ = 0;
    std::vector<QColor> colors_ = {QColor(75, 110, 168), QColor(27, 27, 27)};
    std::vector<double> positions_ = {0.0, 1.0};
    std::vector<double> stop_opacities_ = {1.0, 1.0};
    double opacity_ = 1.0;
    double angle_ = 0.0;
    double center_x_ = 0.5;
    double center_y_ = 0.5;
    double scale_ = 1.0;
    int selected_stop_ = -1;
    bool dragging_stop_ = false;
    int dragging_stop_index_ = -1;
};



static QColor rich_text_color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t rich_text_argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

enum RichTextFormatProperty {
    RichTextPropFontFamily = QTextFormat::UserProperty + 100,
    RichTextPropFontSize,
    RichTextPropBold,
    RichTextPropItalic,
    RichTextPropUnderline,
    RichTextPropStrikethrough,
    RichTextPropTracking,
    RichTextPropScaleX,
    RichTextPropScaleY,
    RichTextPropBaselineShift,
    RichTextPropFillType,
    RichTextPropFillColor,
    RichTextPropGradientType,
    RichTextPropGradientSpread,
    RichTextPropGradientStartColor,
    RichTextPropGradientEndColor,
    RichTextPropGradientStartPos,
    RichTextPropGradientEndPos,
    RichTextPropGradientStartOpacity,
    RichTextPropGradientEndOpacity,
    RichTextPropGradientOpacity,
    RichTextPropGradientAngle,
    RichTextPropGradientCenterX,
    RichTextPropGradientCenterY,
    RichTextPropGradientScale,
    RichTextPropGradientFocalX,
    RichTextPropGradientFocalY,
    RichTextPropFontStyle,
    RichTextPropKerning,
    RichTextPropKerningMode,
    RichTextPropManualKerning,
    RichTextPropTextStyle,
    RichTextPropLigatures,
    RichTextPropStylisticAlternates,
    RichTextPropFractions,
    RichTextPropOpenTypeFeatures,
    RichTextPropLanguage,
    RichTextPropStrokeEnabled,
    RichTextPropStrokeWidth,
    RichTextPropStrokeOpacity,
    RichTextPropStrokeOnFront,
    RichTextPropStrokeAlignment,
    RichTextPropStrokeAntialias,
    RichTextPropStrokeJoinStyle,
    RichTextPropStrokeFillType,
    RichTextPropStrokeColor,
    RichTextPropStrokeGradientType,
    RichTextPropStrokeGradientSpread,
    RichTextPropStrokeGradientStartColor,
    RichTextPropStrokeGradientEndColor,
    RichTextPropStrokeGradientStartPos,
    RichTextPropStrokeGradientEndPos,
    RichTextPropStrokeGradientStartOpacity,
    RichTextPropStrokeGradientEndOpacity,
    RichTextPropStrokeGradientOpacity,
    RichTextPropStrokeGradientAngle,
    RichTextPropStrokeGradientCenterX,
    RichTextPropStrokeGradientCenterY,
    RichTextPropStrokeGradientScale,
    RichTextPropStrokeGradientFocalX,
    RichTextPropStrokeGradientFocalY,
    RichTextPropGradientStops,
    RichTextPropStrokeGradientStops,
    RichTextPropAutoGenerated,
    RichTextPropManualMask,
    /* Marks formats emitted by the canonical RichTextDocument -> QTextDocument
     * adapter. A fragment without a manual mask but with this marker is an
     * inherited/evaluated visual fragment, not pasted authoring data. */
    RichTextPropCanonicalAdapter,
};

static QVariantList rich_text_gradient_stops_to_variant(
    const std::vector<GradientStop> &stops)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(stops.size()));
    for (const GradientStop &stop : stops) {
        QVariantMap item;
        item.insert(QStringLiteral("color"), QVariant::fromValue(stop.color));
        item.insert(QStringLiteral("position"), stop.position);
        item.insert(QStringLiteral("opacity"), stop.opacity);
        result.push_back(item);
    }
    return result;
}

static std::vector<GradientStop> rich_text_gradient_stops_from_variant(
    const QVariant &value)
{
    std::vector<GradientStop> result;
    const QVariantList items = value.toList();
    result.reserve(static_cast<size_t>(items.size()));
    for (const QVariant &entry : items) {
        const QVariantMap item = entry.toMap();
        GradientStop stop;
        stop.color = item.value(QStringLiteral("color"),
                                QVariant::fromValue(0xFFFFFFFFu)).toUInt();
        stop.position = std::clamp(
            item.value(QStringLiteral("position"), 0.5).toFloat(), 0.0f, 1.0f);
        stop.opacity = std::clamp(
            item.value(QStringLiteral("opacity"), 1.0).toFloat(), 0.0f, 1.0f);
        result.push_back(stop);
    }
    return result;
}

enum RichTextParagraphFormatProperty {
    RichTextParagraphPropAlignH = QTextFormat::UserProperty + 200,
    RichTextParagraphPropAlignV,
    RichTextParagraphPropIndentLeft,
    RichTextParagraphPropIndentRight,
    RichTextParagraphPropIndentFirstLine,
    RichTextParagraphPropLineSpacing,
    RichTextParagraphPropSpaceBefore,
    RichTextParagraphPropSpaceAfter,
    RichTextParagraphPropHyphenate,
    RichTextParagraphPropManualMask,
};

static void store_rich_text_format_properties(QTextCharFormat &out, const RichTextCharFormat &format)
{
    out.setProperty(RichTextPropCanonicalAdapter, true);
    out.setProperty(RichTextPropFontFamily, QString::fromStdString(format.font_family));
    out.setProperty(RichTextPropFontStyle, QString::fromStdString(format.font_style));
    out.setProperty(RichTextPropFontSize, format.font_size);
    out.setProperty(RichTextPropBold, format.bold);
    out.setProperty(RichTextPropItalic, format.italic);
    out.setProperty(RichTextPropUnderline, format.underline);
    out.setProperty(RichTextPropStrikethrough, format.strikethrough);
    out.setProperty(RichTextPropKerning, format.kerning);
    out.setProperty(RichTextPropKerningMode, format.kerning_mode);
    out.setProperty(RichTextPropManualKerning, (double)format.manual_kerning);
    out.setProperty(RichTextPropTracking, (double)format.tracking);
    out.setProperty(RichTextPropScaleX, (double)format.scale_x);
    out.setProperty(RichTextPropScaleY, (double)format.scale_y);
    out.setProperty(RichTextPropBaselineShift, (double)format.baseline_shift);
    out.setProperty(RichTextPropTextStyle, format.text_style);
    out.setProperty(RichTextPropLigatures, format.ligatures);
    out.setProperty(RichTextPropStylisticAlternates, format.stylistic_alternates);
    out.setProperty(RichTextPropFractions, format.fractions);
    out.setProperty(RichTextPropOpenTypeFeatures, format.opentype_features);
    out.setProperty(RichTextPropLanguage, QString::fromStdString(format.language));
    out.setProperty(RichTextPropFillType, format.fill.type);
    out.setProperty(RichTextPropFillColor, (uint)format.fill.color);
    out.setProperty(RichTextPropGradientType, format.fill.gradient_type);
    out.setProperty(RichTextPropGradientSpread, format.fill.gradient_spread);
    out.setProperty(RichTextPropGradientStartColor, (uint)format.fill.gradient_start_color);
    out.setProperty(RichTextPropGradientEndColor, (uint)format.fill.gradient_end_color);
    out.setProperty(RichTextPropGradientStartPos, (double)format.fill.gradient_start_pos);
    out.setProperty(RichTextPropGradientEndPos, (double)format.fill.gradient_end_pos);
    out.setProperty(RichTextPropGradientStartOpacity, (double)format.fill.gradient_start_opacity);
    out.setProperty(RichTextPropGradientEndOpacity, (double)format.fill.gradient_end_opacity);
    out.setProperty(RichTextPropGradientOpacity, (double)format.fill.gradient_opacity);
    out.setProperty(RichTextPropGradientAngle, (double)format.fill.gradient_angle);
    out.setProperty(RichTextPropGradientCenterX, (double)format.fill.gradient_center_x);
    out.setProperty(RichTextPropGradientCenterY, (double)format.fill.gradient_center_y);
    out.setProperty(RichTextPropGradientScale, (double)format.fill.gradient_scale);
    out.setProperty(RichTextPropGradientFocalX, (double)format.fill.gradient_focal_x);
    out.setProperty(RichTextPropGradientFocalY, (double)format.fill.gradient_focal_y);
    out.setProperty(RichTextPropGradientStops,
                    rich_text_gradient_stops_to_variant(format.fill.gradient_stops));
    out.setProperty(RichTextPropStrokeEnabled, format.stroke.enabled);
    out.setProperty(RichTextPropStrokeWidth, (double)format.stroke.width);
    out.setProperty(RichTextPropStrokeOpacity, (double)format.stroke.opacity);
    out.setProperty(RichTextPropStrokeOnFront, format.stroke.on_front);
    out.setProperty(RichTextPropStrokeAlignment, format.stroke.alignment);
    out.setProperty(RichTextPropStrokeAntialias, format.stroke.antialias);
    out.setProperty(RichTextPropStrokeJoinStyle, format.stroke.join_style);
    out.setProperty(RichTextPropStrokeFillType, format.stroke.fill.type);
    out.setProperty(RichTextPropStrokeColor, (uint)format.stroke.fill.color);
    out.setProperty(RichTextPropStrokeGradientType, format.stroke.fill.gradient_type);
    out.setProperty(RichTextPropStrokeGradientSpread, format.stroke.fill.gradient_spread);
    out.setProperty(RichTextPropStrokeGradientStartColor, (uint)format.stroke.fill.gradient_start_color);
    out.setProperty(RichTextPropStrokeGradientEndColor, (uint)format.stroke.fill.gradient_end_color);
    out.setProperty(RichTextPropStrokeGradientStartPos, (double)format.stroke.fill.gradient_start_pos);
    out.setProperty(RichTextPropStrokeGradientEndPos, (double)format.stroke.fill.gradient_end_pos);
    out.setProperty(RichTextPropStrokeGradientStartOpacity, (double)format.stroke.fill.gradient_start_opacity);
    out.setProperty(RichTextPropStrokeGradientEndOpacity, (double)format.stroke.fill.gradient_end_opacity);
    out.setProperty(RichTextPropStrokeGradientOpacity, (double)format.stroke.fill.gradient_opacity);
    out.setProperty(RichTextPropStrokeGradientAngle, (double)format.stroke.fill.gradient_angle);
    out.setProperty(RichTextPropStrokeGradientCenterX, (double)format.stroke.fill.gradient_center_x);
    out.setProperty(RichTextPropStrokeGradientCenterY, (double)format.stroke.fill.gradient_center_y);
    out.setProperty(RichTextPropStrokeGradientScale, (double)format.stroke.fill.gradient_scale);
    out.setProperty(RichTextPropStrokeGradientFocalX, (double)format.stroke.fill.gradient_focal_x);
    out.setProperty(RichTextPropStrokeGradientFocalY, (double)format.stroke.fill.gradient_focal_y);
    out.setProperty(RichTextPropStrokeGradientStops,
                    rich_text_gradient_stops_to_variant(format.stroke.fill.gradient_stops));
}

static void store_editor_rich_text_format_properties_masked(QTextCharFormat &out, const RichTextCharFormat &format, uint32_t mask)
{
    if (mask & RichTextCharFontFamily)
        out.setProperty(RichTextPropFontFamily, QString::fromStdString(format.font_family));
    if (mask & RichTextCharFontStyle)
        out.setProperty(RichTextPropFontStyle, QString::fromStdString(format.font_style));
    if (mask & RichTextCharFontSize)
        out.setProperty(RichTextPropFontSize, format.font_size);
    if (mask & RichTextCharBold) out.setProperty(RichTextPropBold, format.bold);
    if (mask & RichTextCharItalic) out.setProperty(RichTextPropItalic, format.italic);
    if (mask & RichTextCharUnderline) out.setProperty(RichTextPropUnderline, format.underline);
    if (mask & RichTextCharStrikethrough) out.setProperty(RichTextPropStrikethrough, format.strikethrough);
    if (mask & RichTextCharKerning) {
        out.setProperty(RichTextPropKerning, format.kerning);
        out.setProperty(RichTextPropKerningMode, format.kerning_mode);
        out.setProperty(RichTextPropManualKerning, (double)format.manual_kerning);
    }
    if (mask & RichTextCharTracking) out.setProperty(RichTextPropTracking, (double)format.tracking);
    if (mask & RichTextCharScaleX) out.setProperty(RichTextPropScaleX, (double)format.scale_x);
    if (mask & RichTextCharScaleY) out.setProperty(RichTextPropScaleY, (double)format.scale_y);
    if (mask & RichTextCharBaselineShift) out.setProperty(RichTextPropBaselineShift, (double)format.baseline_shift);
    if (mask & RichTextCharTextStyle) out.setProperty(RichTextPropTextStyle, format.text_style);
    if (mask & RichTextCharLigatures) out.setProperty(RichTextPropLigatures, format.ligatures);
    if (mask & RichTextCharStylisticAlternates)
        out.setProperty(RichTextPropStylisticAlternates, format.stylistic_alternates);
    if (mask & RichTextCharFractions) out.setProperty(RichTextPropFractions, format.fractions);
    if (mask & RichTextCharOpenTypeFeatures) out.setProperty(RichTextPropOpenTypeFeatures, format.opentype_features);
    if (mask & RichTextCharLanguage)
        out.setProperty(RichTextPropLanguage, QString::fromStdString(format.language));
    if (mask & RichTextCharFillColor) {
        out.setProperty(RichTextPropFillType, format.fill.type);
        out.setProperty(RichTextPropFillColor, (uint)format.fill.color);
        out.setProperty(RichTextPropGradientType, format.fill.gradient_type);
        out.setProperty(RichTextPropGradientSpread, format.fill.gradient_spread);
        out.setProperty(RichTextPropGradientStartColor, (uint)format.fill.gradient_start_color);
        out.setProperty(RichTextPropGradientEndColor, (uint)format.fill.gradient_end_color);
        out.setProperty(RichTextPropGradientStartPos, (double)format.fill.gradient_start_pos);
        out.setProperty(RichTextPropGradientEndPos, (double)format.fill.gradient_end_pos);
        out.setProperty(RichTextPropGradientStartOpacity, (double)format.fill.gradient_start_opacity);
        out.setProperty(RichTextPropGradientEndOpacity, (double)format.fill.gradient_end_opacity);
        out.setProperty(RichTextPropGradientOpacity, (double)format.fill.gradient_opacity);
        out.setProperty(RichTextPropGradientAngle, (double)format.fill.gradient_angle);
        out.setProperty(RichTextPropGradientCenterX, (double)format.fill.gradient_center_x);
        out.setProperty(RichTextPropGradientCenterY, (double)format.fill.gradient_center_y);
        out.setProperty(RichTextPropGradientScale, (double)format.fill.gradient_scale);
        out.setProperty(RichTextPropGradientFocalX, (double)format.fill.gradient_focal_x);
        out.setProperty(RichTextPropGradientFocalY, (double)format.fill.gradient_focal_y);
        out.setProperty(RichTextPropGradientStops,
                        rich_text_gradient_stops_to_variant(format.fill.gradient_stops));
    }
    if (mask & RichTextCharStroke) {
        out.setProperty(RichTextPropStrokeEnabled, format.stroke.enabled);
        out.setProperty(RichTextPropStrokeWidth, (double)format.stroke.width);
        out.setProperty(RichTextPropStrokeOpacity, (double)format.stroke.opacity);
        out.setProperty(RichTextPropStrokeOnFront, format.stroke.on_front);
        out.setProperty(RichTextPropStrokeAlignment, format.stroke.alignment);
        out.setProperty(RichTextPropStrokeAntialias, format.stroke.antialias);
        out.setProperty(RichTextPropStrokeJoinStyle, format.stroke.join_style);
        out.setProperty(RichTextPropStrokeFillType, format.stroke.fill.type);
        out.setProperty(RichTextPropStrokeColor, (uint)format.stroke.fill.color);
        out.setProperty(RichTextPropStrokeGradientType, format.stroke.fill.gradient_type);
        out.setProperty(RichTextPropStrokeGradientSpread, format.stroke.fill.gradient_spread);
        out.setProperty(RichTextPropStrokeGradientStartColor, (uint)format.stroke.fill.gradient_start_color);
        out.setProperty(RichTextPropStrokeGradientEndColor, (uint)format.stroke.fill.gradient_end_color);
        out.setProperty(RichTextPropStrokeGradientStartPos, (double)format.stroke.fill.gradient_start_pos);
        out.setProperty(RichTextPropStrokeGradientEndPos, (double)format.stroke.fill.gradient_end_pos);
        out.setProperty(RichTextPropStrokeGradientStartOpacity, (double)format.stroke.fill.gradient_start_opacity);
        out.setProperty(RichTextPropStrokeGradientEndOpacity, (double)format.stroke.fill.gradient_end_opacity);
        out.setProperty(RichTextPropStrokeGradientOpacity, (double)format.stroke.fill.gradient_opacity);
        out.setProperty(RichTextPropStrokeGradientAngle, (double)format.stroke.fill.gradient_angle);
        out.setProperty(RichTextPropStrokeGradientCenterX, (double)format.stroke.fill.gradient_center_x);
        out.setProperty(RichTextPropStrokeGradientCenterY, (double)format.stroke.fill.gradient_center_y);
        out.setProperty(RichTextPropStrokeGradientScale, (double)format.stroke.fill.gradient_scale);
        out.setProperty(RichTextPropStrokeGradientFocalX, (double)format.stroke.fill.gradient_focal_x);
        out.setProperty(RichTextPropStrokeGradientFocalY, (double)format.stroke.fill.gradient_focal_y);
        out.setProperty(RichTextPropStrokeGradientStops,
                        rich_text_gradient_stops_to_variant(format.stroke.fill.gradient_stops));
    }
}

static void apply_editor_rich_text_extended_font_properties(QFont &font, const RichTextCharFormat &format)
{
    if (!format.font_style.empty())
        font.setStyleName(QString::fromStdString(format.font_style));
    font.setKerning(format.kerning_mode != 2 && format.kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing,
                          format.tracking + (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
    /* QTextDocument is the compatibility/overlay raster, not the GPU glyph
     * pipeline. Reproduce the same final geometry explicitly: use V Scale as a
     * uniform font-size multiplier, then compensate X with H/V stretch. This
     * keeps the fallback and editor overlays correct when the GPU path is
     * unavailable, while the GPU renderer itself applies per-glyph quad scale. */
    const double scale_x = std::clamp(static_cast<double>(format.scale_x), 0.01, 100.0);
    const double scale_y = std::clamp(static_cast<double>(format.scale_y), 0.01, 100.0);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(font.pixelSize()) * scale_y))));
    else if (font.pointSizeF() > 0.0)
        font.setPointSizeF(std::max(0.1, font.pointSizeF() * scale_y));
    font.setStretch(std::clamp(
        static_cast<int>(std::lround((scale_x / scale_y) * 100.0)), 1, 4000));
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setFeature("kern", format.kerning_mode != 2 && format.kerning ? 1 : 0);
    font.setFeature("liga", format.ligatures ? 1 : 0);
    font.setFeature("clig", format.ligatures ? 1 : 0);
    font.setFeature("salt", format.stylistic_alternates ? 1 : 0);
    font.setFeature("frac", format.fractions ? 1 : 0);
    font.setFeature("calt", format.opentype_features ? 1 : 0);
#endif
    font.setCapitalization(QFont::MixedCase);
    if (format.text_style == 1)
        font.setCapitalization(QFont::AllUppercase);
    else if (format.text_style == 2)
        font.setCapitalization(QFont::SmallCaps);
    if (format.text_style == 3 || format.text_style == 4)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * 0.65)));
    /* Never disable shaping to suppress ligatures: PreferNoShaping breaks
     * Arabic/Indic/complex-script layout. Ligature feature selection belongs
     * in the HarfBuzz shaping stage introduced by Phase 12. */
}

static void apply_editor_rich_text_extended_char_format(QTextCharFormat &out, const RichTextCharFormat &format)
{
    if (format.text_style == 3)
        out.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    else if (format.text_style == 4)
        out.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    else
        out.setVerticalAlignment(QTextCharFormat::AlignNormal);
    out.setFontKerning(format.kerning_mode != 2 && format.kerning);
    out.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    out.setFontLetterSpacing(format.tracking + (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
}

static void apply_editor_rich_text_baseline_delta(QTextCharFormat &out,
                                                  const RichTextCharFormat &format,
                                                  float default_baseline_shift,
                                                  double visual_scale)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const double delta_px = (format.baseline_shift - default_baseline_shift) * visual_scale;
    const double glyph_height = std::max(1.0, format.font_size *
                                              rich_text_font_scale_metrics(format.scale_x, format.scale_y).vertical_factor *
                                              visual_scale);
    out.setBaselineOffset(100.0 * delta_px / glyph_height);
#else
    (void)out;
    (void)format;
    (void)default_baseline_shift;
    (void)visual_scale;
#endif
}

static RichTextCharFormat rich_text_format_from_qtext_format(const QTextCharFormat &fmt,
                                                             const RichTextCharFormat &fallback,
                                                             double visual_scale)
{
    RichTextCharFormat out = fallback;
    QFont font = fmt.font();
    out.font_family = fmt.property(RichTextPropFontFamily).toString().isEmpty()
                          ? (!font.family().isEmpty() ? font.family().toStdString() : out.font_family)
                          : fmt.property(RichTextPropFontFamily).toString().toStdString();
    if (fmt.hasProperty(RichTextPropFontStyle))
        out.font_style = fmt.property(RichTextPropFontStyle).toString().toStdString();
    else if (!font.styleName().isEmpty())
        out.font_style = font.styleName().toStdString();
    int px = font.pixelSize();
    if (px <= 0 && font.pointSizeF() > 0.0) px = (int)std::round(font.pointSizeF());
    out.font_size = fmt.hasProperty(RichTextPropFontSize)
                        ? fmt.property(RichTextPropFontSize).toInt()
                        : (px > 0 ? std::max(1, (int)std::round(px / std::max(0.0001, visual_scale))) : out.font_size);
    out.bold = fmt.hasProperty(RichTextPropBold) ? fmt.property(RichTextPropBold).toBool() : (fmt.fontWeight() >= QFont::Bold);
    out.italic = fmt.hasProperty(RichTextPropItalic) ? fmt.property(RichTextPropItalic).toBool() : fmt.fontItalic();
    out.underline = fmt.hasProperty(RichTextPropUnderline) ? fmt.property(RichTextPropUnderline).toBool() : fmt.fontUnderline();
    out.strikethrough = fmt.hasProperty(RichTextPropStrikethrough) ? fmt.property(RichTextPropStrikethrough).toBool() : fmt.fontStrikeOut();
    if (fmt.hasProperty(RichTextPropKerning)) out.kerning = fmt.property(RichTextPropKerning).toBool();
    else out.kerning = font.kerning();
    if (fmt.hasProperty(RichTextPropKerningMode)) out.kerning_mode = fmt.property(RichTextPropKerningMode).toInt();
    if (fmt.hasProperty(RichTextPropManualKerning)) out.manual_kerning = (float)fmt.property(RichTextPropManualKerning).toDouble();
    if (fmt.hasProperty(RichTextPropTracking)) out.tracking = (float)fmt.property(RichTextPropTracking).toDouble();
    if (fmt.hasProperty(RichTextPropScaleX)) out.scale_x = (float)fmt.property(RichTextPropScaleX).toDouble();
    if (fmt.hasProperty(RichTextPropScaleY)) out.scale_y = (float)fmt.property(RichTextPropScaleY).toDouble();
    if (fmt.hasProperty(RichTextPropBaselineShift)) out.baseline_shift = (float)fmt.property(RichTextPropBaselineShift).toDouble();
    if (fmt.hasProperty(RichTextPropTextStyle)) out.text_style = fmt.property(RichTextPropTextStyle).toInt();
    else if (font.capitalization() == QFont::AllUppercase) out.text_style = 1;
    else if (font.capitalization() == QFont::SmallCaps) out.text_style = 2;
    else if (fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript) out.text_style = 3;
    else if (fmt.verticalAlignment() == QTextCharFormat::AlignSubScript) out.text_style = 4;
    if (fmt.hasProperty(RichTextPropLigatures)) out.ligatures = fmt.property(RichTextPropLigatures).toBool();
    if (fmt.hasProperty(RichTextPropStylisticAlternates)) out.stylistic_alternates = fmt.property(RichTextPropStylisticAlternates).toBool();
    if (fmt.hasProperty(RichTextPropFractions)) out.fractions = fmt.property(RichTextPropFractions).toBool();
    if (fmt.hasProperty(RichTextPropOpenTypeFeatures)) out.opentype_features = fmt.property(RichTextPropOpenTypeFeatures).toBool();
    if (fmt.hasProperty(RichTextPropLanguage)) out.language = fmt.property(RichTextPropLanguage).toString().toStdString();
    if (fmt.hasProperty(RichTextPropFillType)) out.fill.type = fmt.property(RichTextPropFillType).toInt();
    if (fmt.hasProperty(RichTextPropFillColor)) out.fill.color = fmt.property(RichTextPropFillColor).toUInt();
    else if (fmt.foreground().style() != Qt::NoBrush) {
        const QColor c = fmt.foreground().color();
        if (c.isValid() && c.alpha() > 0)
            out.fill.color = rich_text_argb_from_color(c);
    }
    if (fmt.hasProperty(RichTextPropGradientType)) out.fill.gradient_type = fmt.property(RichTextPropGradientType).toInt();
    if (fmt.hasProperty(RichTextPropGradientSpread)) out.fill.gradient_spread = fmt.property(RichTextPropGradientSpread).toInt();
    if (fmt.hasProperty(RichTextPropGradientStartColor)) out.fill.gradient_start_color = fmt.property(RichTextPropGradientStartColor).toUInt();
    if (fmt.hasProperty(RichTextPropGradientEndColor)) out.fill.gradient_end_color = fmt.property(RichTextPropGradientEndColor).toUInt();
    if (fmt.hasProperty(RichTextPropGradientStartPos)) out.fill.gradient_start_pos = (float)fmt.property(RichTextPropGradientStartPos).toDouble();
    if (fmt.hasProperty(RichTextPropGradientEndPos)) out.fill.gradient_end_pos = (float)fmt.property(RichTextPropGradientEndPos).toDouble();
    if (fmt.hasProperty(RichTextPropGradientStartOpacity)) out.fill.gradient_start_opacity = (float)fmt.property(RichTextPropGradientStartOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientEndOpacity)) out.fill.gradient_end_opacity = (float)fmt.property(RichTextPropGradientEndOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientOpacity)) out.fill.gradient_opacity = (float)fmt.property(RichTextPropGradientOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropGradientAngle)) out.fill.gradient_angle = (float)fmt.property(RichTextPropGradientAngle).toDouble();
    if (fmt.hasProperty(RichTextPropGradientCenterX)) out.fill.gradient_center_x = (float)fmt.property(RichTextPropGradientCenterX).toDouble();
    if (fmt.hasProperty(RichTextPropGradientCenterY)) out.fill.gradient_center_y = (float)fmt.property(RichTextPropGradientCenterY).toDouble();
    if (fmt.hasProperty(RichTextPropGradientScale)) out.fill.gradient_scale = (float)fmt.property(RichTextPropGradientScale).toDouble();
    if (fmt.hasProperty(RichTextPropGradientFocalX)) out.fill.gradient_focal_x = (float)fmt.property(RichTextPropGradientFocalX).toDouble();
    if (fmt.hasProperty(RichTextPropGradientFocalY)) out.fill.gradient_focal_y = (float)fmt.property(RichTextPropGradientFocalY).toDouble();
    if (fmt.hasProperty(RichTextPropGradientStops))
        out.fill.gradient_stops = rich_text_gradient_stops_from_variant(
            fmt.property(RichTextPropGradientStops));
    if (fmt.hasProperty(RichTextPropStrokeEnabled)) out.stroke.enabled = fmt.property(RichTextPropStrokeEnabled).toBool();
    if (fmt.hasProperty(RichTextPropStrokeWidth)) out.stroke.width = (float)fmt.property(RichTextPropStrokeWidth).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeOpacity)) out.stroke.opacity = (float)fmt.property(RichTextPropStrokeOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeOnFront)) out.stroke.on_front = fmt.property(RichTextPropStrokeOnFront).toBool();
    if (fmt.hasProperty(RichTextPropStrokeAlignment)) out.stroke.alignment = fmt.property(RichTextPropStrokeAlignment).toInt();
    if (fmt.hasProperty(RichTextPropStrokeAntialias)) out.stroke.antialias = fmt.property(RichTextPropStrokeAntialias).toBool();
    if (fmt.hasProperty(RichTextPropStrokeJoinStyle)) out.stroke.join_style = fmt.property(RichTextPropStrokeJoinStyle).toInt();
    if (fmt.hasProperty(RichTextPropStrokeFillType)) out.stroke.fill.type = fmt.property(RichTextPropStrokeFillType).toInt();
    if (fmt.hasProperty(RichTextPropStrokeColor)) out.stroke.fill.color = fmt.property(RichTextPropStrokeColor).toUInt();
    if (fmt.hasProperty(RichTextPropStrokeGradientType)) out.stroke.fill.gradient_type = fmt.property(RichTextPropStrokeGradientType).toInt();
    if (fmt.hasProperty(RichTextPropStrokeGradientSpread)) out.stroke.fill.gradient_spread = fmt.property(RichTextPropStrokeGradientSpread).toInt();
    if (fmt.hasProperty(RichTextPropStrokeGradientStartColor)) out.stroke.fill.gradient_start_color = fmt.property(RichTextPropStrokeGradientStartColor).toUInt();
    if (fmt.hasProperty(RichTextPropStrokeGradientEndColor)) out.stroke.fill.gradient_end_color = fmt.property(RichTextPropStrokeGradientEndColor).toUInt();
    if (fmt.hasProperty(RichTextPropStrokeGradientStartPos)) out.stroke.fill.gradient_start_pos = (float)fmt.property(RichTextPropStrokeGradientStartPos).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientEndPos)) out.stroke.fill.gradient_end_pos = (float)fmt.property(RichTextPropStrokeGradientEndPos).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientStartOpacity)) out.stroke.fill.gradient_start_opacity = (float)fmt.property(RichTextPropStrokeGradientStartOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientEndOpacity)) out.stroke.fill.gradient_end_opacity = (float)fmt.property(RichTextPropStrokeGradientEndOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientOpacity)) out.stroke.fill.gradient_opacity = (float)fmt.property(RichTextPropStrokeGradientOpacity).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientAngle)) out.stroke.fill.gradient_angle = (float)fmt.property(RichTextPropStrokeGradientAngle).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientCenterX)) out.stroke.fill.gradient_center_x = (float)fmt.property(RichTextPropStrokeGradientCenterX).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientCenterY)) out.stroke.fill.gradient_center_y = (float)fmt.property(RichTextPropStrokeGradientCenterY).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientScale)) out.stroke.fill.gradient_scale = (float)fmt.property(RichTextPropStrokeGradientScale).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientFocalX)) out.stroke.fill.gradient_focal_x = (float)fmt.property(RichTextPropStrokeGradientFocalX).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientFocalY)) out.stroke.fill.gradient_focal_y = (float)fmt.property(RichTextPropStrokeGradientFocalY).toDouble();
    if (fmt.hasProperty(RichTextPropStrokeGradientStops))
        out.stroke.fill.gradient_stops = rich_text_gradient_stops_from_variant(
            fmt.property(RichTextPropStrokeGradientStops));
    return out;
}

static QTextCharFormat qtext_format_from_rich_text_format(const RichTextCharFormat &format, double visual_scale)
{
    QTextCharFormat out;
    QFont font(QString::fromStdString(format.font_family));
    font.setPixelSize(std::max(1, (int)std::round(format.font_size * visual_scale)));
    font.setBold(format.bold);
    font.setItalic(format.italic);
    font.setUnderline(format.underline);
    font.setStrikeOut(format.strikethrough);
    apply_editor_rich_text_extended_font_properties(font, format);
    out.setFont(font);
    out.setFontUnderline(format.underline);
    out.setFontStrikeOut(format.strikethrough);
    apply_editor_rich_text_extended_char_format(out, format);
    store_rich_text_format_properties(out, format);
    QColor color = rich_text_color_from_argb(format.fill.color);
    color.setAlpha(0);
    out.setForeground(color);
    return out;
}

static bool resolve_editor_auto_text_style_preset(const std::string &preset_id, RichTextCharFormat &format, uint32_t &mask)
{
    return fxm::style_presets::resolve_text_preset(preset_id, format, mask);
}


class EditorQtUtf8PositionMap {
public:
    explicit EditorQtUtf8PositionMap(const QString &text)
    {
        byte_at_qpos_.assign(static_cast<size_t>(text.size()) + 1, 0);
        qpos_boundaries_.reserve(static_cast<size_t>(text.size()) + 1);
        byte_boundaries_.reserve(static_cast<size_t>(text.size()) + 1);
        qpos_boundaries_.push_back(0);
        byte_boundaries_.push_back(0);

        size_t bytes = 0;
        const int size = static_cast<int>(text.size());
        int qpos = 0;
        while (qpos < size) {
            const ushort first = text.at(qpos).unicode();
            const bool surrogate_pair = first >= 0xD800 && first <= 0xDBFF &&
                                        qpos + 1 < size &&
                                        text.at(qpos + 1).unicode() >= 0xDC00 &&
                                        text.at(qpos + 1).unicode() <= 0xDFFF;
            const int units = surrogate_pair ? 2 : 1;
            byte_at_qpos_[static_cast<size_t>(qpos)] = bytes;
            if (surrogate_pair)
                byte_at_qpos_[static_cast<size_t>(qpos + 1)] = bytes;
            const size_t encoded_bytes = surrogate_pair
                ? 4u
                : (first < 0x80u ? 1u : (first < 0x800u ? 2u : 3u));
            bytes += encoded_bytes;
            qpos += units;
            byte_at_qpos_[static_cast<size_t>(qpos)] = bytes;
            qpos_boundaries_.push_back(qpos);
            byte_boundaries_.push_back(bytes);
        }
        total_bytes_ = bytes;
    }

    int qpos_from_byte(size_t byte_offset) const
    {
        const size_t clamped = std::min(byte_offset, total_bytes_);
        const auto upper = std::upper_bound(byte_boundaries_.begin(),
                                            byte_boundaries_.end(), clamped);
        if (upper == byte_boundaries_.begin())
            return 0;
        const size_t index = static_cast<size_t>(std::prev(upper) -
                                                 byte_boundaries_.begin());
        return qpos_boundaries_[index];
    }

    size_t byte_from_qpos(int qpos) const
    {
        if (byte_at_qpos_.empty())
            return 0;
        qpos = std::clamp(qpos, 0,
                          static_cast<int>(byte_at_qpos_.size()) - 1);
        return byte_at_qpos_[static_cast<size_t>(qpos)];
    }

private:
    std::vector<size_t> byte_at_qpos_;
    std::vector<int> qpos_boundaries_;
    std::vector<size_t> byte_boundaries_;
    size_t total_bytes_ = 0;
};

static Qt::Alignment rich_text_qt_alignment(int align_h)
{
    if (align_h == 1 || align_h == 4) return Qt::AlignHCenter;
    if (align_h == 2 || align_h == 5) return Qt::AlignRight;
    if (align_h >= 3) return Qt::AlignJustify;
    return Qt::AlignLeft;
}

static int rich_text_align_h_from_qt(Qt::Alignment alignment, int fallback)
{
    if (alignment.testFlag(Qt::AlignJustify)) return 3;
    if (alignment.testFlag(Qt::AlignHCenter)) return 1;
    if (alignment.testFlag(Qt::AlignRight)) return 2;
    if (alignment.testFlag(Qt::AlignLeft)) return 0;
    return fallback;
}

static QTextBlockFormat qtext_block_format_from_rich_text_paragraph(
    const RichTextParagraphFormat &format, double visual_scale, uint32_t source_mask)
{
    QTextBlockFormat out;
    out.setAlignment(rich_text_qt_alignment(format.align_h));
    out.setLeftMargin(std::max(0.0f, format.indent_left) * visual_scale);
    out.setRightMargin(std::max(0.0f, format.indent_right) * visual_scale);
    out.setTextIndent(format.indent_first_line * visual_scale);
    out.setTopMargin(std::max(0.0f, format.space_before) * visual_scale);
    out.setBottomMargin(std::max(0.0f, format.space_after) * visual_scale);
    if (std::abs(format.line_spacing) >= 0.0001f)
        out.setLineHeight(format.line_spacing * visual_scale, QTextBlockFormat::LineDistanceHeight);
    else
        out.setLineHeight(0.0, QTextBlockFormat::SingleHeight);

    out.setProperty(RichTextParagraphPropAlignH, format.align_h);
    out.setProperty(RichTextParagraphPropAlignV, format.align_v);
    out.setProperty(RichTextParagraphPropIndentLeft, (double)format.indent_left);
    out.setProperty(RichTextParagraphPropIndentRight, (double)format.indent_right);
    out.setProperty(RichTextParagraphPropIndentFirstLine, (double)format.indent_first_line);
    out.setProperty(RichTextParagraphPropLineSpacing, (double)format.line_spacing);
    out.setProperty(RichTextParagraphPropSpaceBefore, (double)format.space_before);
    out.setProperty(RichTextParagraphPropSpaceAfter, (double)format.space_after);
    out.setProperty(RichTextParagraphPropHyphenate, format.hyphenate);
    out.setProperty(RichTextParagraphPropManualMask, (uint)(source_mask & RichTextParagraphAll));
    return out;
}

static RichTextParagraphFormat rich_text_paragraph_from_qtext_block_format(
    const QTextBlockFormat &format, const RichTextParagraphFormat &fallback,
    double visual_scale)
{
    RichTextParagraphFormat out = fallback;
    const double safe_scale = std::max(0.0001, visual_scale);
    out.align_h = format.hasProperty(RichTextParagraphPropAlignH)
        ? format.property(RichTextParagraphPropAlignH).toInt()
        : rich_text_align_h_from_qt(format.alignment(), fallback.align_h);
    if (format.hasProperty(RichTextParagraphPropAlignV))
        out.align_v = format.property(RichTextParagraphPropAlignV).toInt();
    out.indent_left = format.hasProperty(RichTextParagraphPropIndentLeft)
        ? (float)format.property(RichTextParagraphPropIndentLeft).toDouble()
        : (float)(format.leftMargin() / safe_scale);
    out.indent_right = format.hasProperty(RichTextParagraphPropIndentRight)
        ? (float)format.property(RichTextParagraphPropIndentRight).toDouble()
        : (float)(format.rightMargin() / safe_scale);
    out.indent_first_line = format.hasProperty(RichTextParagraphPropIndentFirstLine)
        ? (float)format.property(RichTextParagraphPropIndentFirstLine).toDouble()
        : (float)(format.textIndent() / safe_scale);
    out.line_spacing = format.hasProperty(RichTextParagraphPropLineSpacing)
        ? (float)format.property(RichTextParagraphPropLineSpacing).toDouble()
        : (format.lineHeightType() == QTextBlockFormat::LineDistanceHeight
               ? (float)(format.lineHeight() / safe_scale)
               : 0.0f);
    out.space_before = format.hasProperty(RichTextParagraphPropSpaceBefore)
        ? (float)format.property(RichTextParagraphPropSpaceBefore).toDouble()
        : (float)(format.topMargin() / safe_scale);
    out.space_after = format.hasProperty(RichTextParagraphPropSpaceAfter)
        ? (float)format.property(RichTextParagraphPropSpaceAfter).toDouble()
        : (float)(format.bottomMargin() / safe_scale);
    if (format.hasProperty(RichTextParagraphPropHyphenate))
        out.hyphenate = format.property(RichTextParagraphPropHyphenate).toBool();
    return out;
}

static uint32_t rich_text_manual_paragraph_mask_at(const RichTextDocument &model,
                                                   size_t paragraph_start)
{
    for (const auto &block : model.blocks) {
        if (block.start == paragraph_start)
            return block.mask & RichTextParagraphAll;
    }
    return 0;
}

static RichTextParagraphFormat rich_text_paragraph_with_base(
    const RichTextDocument &model, size_t paragraph_start,
    const RichTextParagraphFormat &base)
{
    RichTextParagraphFormat out = base;
    for (const auto &block : model.blocks) {
        if (block.start != paragraph_start)
            continue;
        rich_text_merge_paragraph_format(out, block.format, block.mask);
        break;
    }
    return out;
}

static void apply_rich_text_paragraph_blocks_to_qtext_document(
    QTextDocument *doc, const RichTextDocument &model,
    const RichTextParagraphFormat &base, double visual_scale)
{
    if (!doc) return;
    const QString qplain = doc->toPlainText();
    const EditorQtUtf8PositionMap positions(qplain);
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const size_t start = positions.byte_from_qpos(block.position());
        const uint32_t mask = rich_text_manual_paragraph_mask_at(model, start);
        const RichTextParagraphFormat effective = rich_text_paragraph_with_base(model, start, base);
        QTextCursor cursor(block);
        cursor.setBlockFormat(qtext_block_format_from_rich_text_paragraph(effective,
                                                                          visual_scale,
                                                                          mask));
    }
}

static void set_qtext_range_from_rich_range(
    QTextCursor &cursor, const QString &text,
    const EditorQtUtf8PositionMap &positions, size_t start, size_t length)
{
    const int qstart = positions.qpos_from_byte(start);
    const int qend = positions.qpos_from_byte(start + length);
    cursor.setPosition(std::clamp(qstart, 0, static_cast<int>(text.size())));
    cursor.setPosition(std::clamp(qend, 0, static_cast<int>(text.size())),
                       QTextCursor::KeepAnchor);
}

static RichTextDocument rich_text_effective_document_for_editor(const RichTextDocument &source_model)
{
    return rich_text_document_with_auto_styles(source_model, resolve_editor_auto_text_style_preset);
}

static uint32_t rich_text_manual_char_mask_at(const RichTextDocument &model, size_t byte_offset)
{
    uint32_t mask = 0;
    for (const auto &range : model.ranges) {
        const size_t end = range.length > model.plain_text.size() - std::min(range.start, model.plain_text.size())
            ? model.plain_text.size()
            : range.start + range.length;
        if (byte_offset >= range.start && byte_offset < end)
            mask |= range.mask;
    }
    return mask & RichTextCharAll;
}

static void tag_qtext_manual_char_masks(QTextDocument *doc,
                                        const RichTextDocument &source_model)
{
    if (!doc) return;
    const QString qtext = doc->toPlainText();
    const EditorQtUtf8PositionMap positions(qtext);
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    QTextCharFormat inherited;
    inherited.setProperty(RichTextPropAutoGenerated,
                          source_model.auto_style_enabled);
    inherited.setProperty(RichTextPropManualMask, (uint)0);
    all.mergeCharFormat(inherited);

    for (const auto &manual : source_model.ranges) {
        if (manual.length == 0 || manual.start >= source_model.plain_text.size())
            continue;
        QTextCursor cursor(doc);
        set_qtext_range_from_rich_range(cursor, qtext, positions, manual.start, manual.length);
        QTextCharFormat metadata;
        metadata.setProperty(RichTextPropAutoGenerated, false);
        metadata.setProperty(RichTextPropManualMask,
                             (uint)(manual.mask & RichTextCharAll));
        cursor.mergeCharFormat(metadata);
    }
}

static RichTextParagraphFormat editor_paragraph_format_for_block(
    const RichTextDocument &model, const EditorQtUtf8PositionMap &positions,
    const QTextBlock &block)
{
    RichTextParagraphFormat effective = model.default_paragraph_format;
    const size_t start = positions.byte_from_qpos(block.position());
    for (const auto &rich_block : model.blocks) {
        if (rich_block.start != start)
            continue;
        rich_text_merge_paragraph_format(effective, rich_block.format, rich_block.mask);
        break;
    }
    return effective;
}

static void apply_editor_last_line_justification(QTextDocument &doc,
                                                 const RichTextDocument &model)
{
    const QString plain_text = doc.toPlainText();
    const EditorQtUtf8PositionMap positions(plain_text);
    (void)doc.size();
    bool relayout_needed = false;

    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextLayout *layout = block.layout();
        if (!layout || layout->lineCount() <= 0)
            continue;
        const RichTextParagraphFormat format =
            editor_paragraph_format_for_block(model, positions, block);
        if (format.align_h != 6)
            continue;
        const QTextLine last = layout->lineAt(layout->lineCount() - 1);
        const QString line_text = block.text().mid(last.textStart(), last.textLength());
        const int spaces = line_text.count(QLatin1Char(' '));
        const double available = last.width();
        const double natural = last.naturalTextWidth();
        if (spaces <= 0 || available <= natural + 0.01)
            continue;
        QTextCursor cursor(&doc);
        const int start = block.position() + last.textStart();
        cursor.setPosition(start);
        cursor.setPosition(start + last.textLength(), QTextCursor::KeepAnchor);
        QTextCharFormat spacing;
        spacing.setFontWordSpacing((available - natural) / static_cast<double>(spaces));
        cursor.mergeCharFormat(spacing);
        relayout_needed = true;
    }
    if (relayout_needed)
        (void)doc.size();

    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextLayout *layout = block.layout();
        if (!layout || layout->lineCount() <= 0)
            continue;
        const RichTextParagraphFormat format =
            editor_paragraph_format_for_block(model, positions, block);
        if (format.align_h != 4 && format.align_h != 5)
            continue;
        QTextLine last = layout->lineAt(layout->lineCount() - 1);
        QPointF position = last.position();
        const double spare = std::max(0.0, last.width() - last.naturalTextWidth());
        position.setX(format.align_h == 4 ? spare * 0.5 : spare);
        last.setPosition(position);
    }
}

static void populate_qtext_document_from_rich_text(QTextDocument *doc, const RichTextDocument &source_model, double visual_scale)
{
    if (!doc) return;
    /* Illustrator/After Effects behaviour: the editor displays the effective
     * text appearance (manual styles + automatic character styles), but auto
     * generated spans are tagged so committing the QTextEdit does not turn them
     * into persistent manual inline ranges. */
    const RichTextDocument model = rich_text_effective_document_for_editor(source_model);
    const QString qtext = QString::fromUtf8(model.plain_text.data(),
                                                    static_cast<int>(model.plain_text.size()));
    QSignalBlocker blocker(doc);
    doc->setPlainText(qtext);
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    QTextCharFormat default_fmt = qtext_format_from_rich_text_format(model.default_format, visual_scale);
    default_fmt.setProperty(RichTextPropAutoGenerated, source_model.auto_style_enabled);
    default_fmt.setProperty(RichTextPropManualMask, (uint)0);
    all.mergeCharFormat(default_fmt);
    const EditorQtUtf8PositionMap positions(qtext);
    for (const auto &range : model.ranges) {
        if (range.length == 0 || range.start >= model.plain_text.size()) continue;
        QTextCursor cursor(doc);
        set_qtext_range_from_rich_range(cursor, qtext, positions,
                                        range.start, range.length);
        const RichTextCharFormat effective = rich_text_format_at(model, range.start);
        QTextCharFormat fmt = qtext_format_from_rich_text_format(effective, visual_scale);
        apply_editor_rich_text_baseline_delta(fmt, effective,
                                              model.default_format.baseline_shift, visual_scale);
        const uint32_t manual_mask = rich_text_manual_char_mask_at(source_model, range.start);
        fmt.setProperty(RichTextPropAutoGenerated, manual_mask == 0);
        fmt.setProperty(RichTextPropManualMask, (uint)manual_mask);
        cursor.mergeCharFormat(fmt);
    }
    /* Keep explicit sparse masks even when their current value happens to equal
     * the document default. Visual formatting alone cannot represent that
     * authoring intent, so attach the mask as non-visual QText metadata. */
    tag_qtext_manual_char_masks(doc, source_model);
    apply_rich_text_paragraph_blocks_to_qtext_document(doc, model,
                                                        model.default_paragraph_format,
                                                        visual_scale);
    apply_editor_last_line_justification(*doc, model);
}

static void populate_qtext_document_from_plain_layer_text(QTextDocument *doc, const std::string &text,
                                                          const RichTextCharFormat &format, double visual_scale)
{
    if (!doc) return;
    QSignalBlocker blocker(doc);
    doc->setPlainText(QString::fromStdString(text));
    QTextCursor all(doc);
    all.select(QTextCursor::Document);
    all.mergeCharFormat(qtext_format_from_rich_text_format(format, visual_scale));
}
