static QPainterPath apply_vertical_character_scale(const QPainterPath &path, const QRectF &rect,
                                                   Qt::Alignment alignment, const Layer &layer, double t = 0.0)
{
    double scale_y = std::clamp(eval_char_scale_y(layer, t), 0.1, 5.0);
    if (std::abs(scale_y - 1.0) < 0.0001)
        return path;

    QRectF bounds = path.boundingRect();
    double anchor_y = bounds.top();
    if (alignment & Qt::AlignVCenter)
        anchor_y = bounds.center().y();
    else if (alignment & Qt::AlignBottom)
        anchor_y = bounds.bottom();
    else if (!bounds.isEmpty())
        anchor_y = rect.top();

    QTransform xf;
    xf.translate(0.0, anchor_y);
    xf.scale(1.0, scale_y);
    xf.translate(0.0, -anchor_y);
    return xf.map(path);
}

static QRectF text_rect_for_style(const QRectF &rect, const Layer &layer)
{
    if (layer.text_style == 3)
        return rect.adjusted(0.0, 0.0, 0.0, -rect.height() * 0.28);
    if (layer.text_style == 4)
        return rect.adjusted(0.0, rect.height() * 0.28, 0.0, 0.0);
    return rect;
}

static QString overflow_layout_text(const QString &text, const Layer &layer)
{
    if (layer.text_overflow_mode == 2) {
        QString single = text;
        single.replace('\r', ' ');
        single.replace('\n', ' ');
        return single;
    }
    return text;
}

static double eval_text_font_size(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_format.font_size)
        : static_cast<double>(layer.font_size);
    return std::clamp(layer.font_size_prop.is_animated()
                          ? layer.font_size_prop.evaluate(t)
                          : authored,
                      1.0, 512.0);
}

static double eval_char_tracking(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_format.tracking)
        : static_cast<double>(layer.char_tracking);
    return std::clamp(layer.char_tracking_prop.is_animated()
                          ? layer.char_tracking_prop.evaluate(t)
                          : authored,
                      -1000.0, 1000.0);
}

static double eval_char_scale_x(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_format.scale_x)
        : static_cast<double>(layer.char_scale_x);
    return std::clamp(layer.char_scale_x_prop.is_animated()
                          ? layer.char_scale_x_prop.evaluate(t)
                          : authored,
                      0.01, 100.0);
}

static double eval_char_scale_y(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_format.scale_y)
        : static_cast<double>(layer.char_scale_y);
    return std::clamp(layer.char_scale_y_prop.is_animated()
                          ? layer.char_scale_y_prop.evaluate(t)
                          : authored,
                      0.01, 100.0);
}

static double eval_baseline_shift(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_format.baseline_shift)
        : static_cast<double>(layer.baseline_shift);
    return std::clamp(layer.baseline_shift_prop.is_animated()
                          ? layer.baseline_shift_prop.evaluate(t)
                          : authored,
                      -1000.0, 1000.0);
}

static double eval_paragraph_space_before(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_paragraph_format.space_before)
        : static_cast<double>(layer.paragraph_space_before);
    return std::clamp(layer.paragraph_space_before_prop.is_animated()
                          ? layer.paragraph_space_before_prop.evaluate(t)
                          : authored,
                      -10000.0, 10000.0);
}

static double eval_paragraph_space_after(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_paragraph_format.space_after)
        : static_cast<double>(layer.paragraph_space_after);
    return std::clamp(layer.paragraph_space_after_prop.is_animated()
                          ? layer.paragraph_space_after_prop.evaluate(t)
                          : authored,
                      -10000.0, 10000.0);
}

static double eval_paragraph_indent_left(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_paragraph_format.indent_left)
        : static_cast<double>(layer.paragraph_indent_left);
    return std::clamp(layer.paragraph_indent_left_prop.is_animated()
                          ? layer.paragraph_indent_left_prop.evaluate(t)
                          : authored,
                      -10000.0, 10000.0);
}

static double eval_paragraph_indent_right(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_paragraph_format.indent_right)
        : static_cast<double>(layer.paragraph_indent_right);
    return std::clamp(layer.paragraph_indent_right_prop.is_animated()
                          ? layer.paragraph_indent_right_prop.evaluate(t)
                          : authored,
                      -10000.0, 10000.0);
}

static double eval_paragraph_indent_first_line(const Layer &layer, double t)
{
    const double authored = !layer.rich_text.empty()
        ? static_cast<double>(layer.rich_text.default_paragraph_format.indent_first_line)
        : static_cast<double>(layer.paragraph_indent_first_line);
    return std::clamp(layer.paragraph_indent_first_line_prop.is_animated()
                          ? layer.paragraph_indent_first_line_prop.evaluate(t)
                          : authored,
                      -10000.0, 10000.0);
}

static double horizontal_fit_scale(const QFont &font, const QRectF &rect,
                                   const QString &text, const Layer &layer, double)
{
    if (layer.text_overflow_mode != 2) return 1.0;
    QFontMetricsF metrics(font);
    const double text_width = static_cast<double>(metrics.horizontalAdvance(overflow_layout_text(text, layer)));
    double natural_width = std::max(1.0, text_width);
    if (natural_width <= rect.width()) return 1.0;
    return std::clamp(rect.width() / natural_width,
                      std::clamp((double)layer.text_fit_min_scale, 0.05, 1.0),
                      1.0);
}

static QPainterPath text_overflow_path(const QFont &font, const QRectF &rect,
                                       Qt::Alignment alignment, const QString &text,
                                       const Layer &layer, double t, double *fit_scale = nullptr)
{
    QPainterPath path;
    QFontMetricsF metrics(font);
    const double indent_left = eval_paragraph_indent_left(layer, t);
    const double indent_right = eval_paragraph_indent_right(layer, t);
    const double first_indent = eval_paragraph_indent_first_line(layer, t);
    const double space_before = eval_paragraph_space_before(layer, t);
    const double space_after = eval_paragraph_space_after(layer, t);
    const double paragraph_left = rect.left() + indent_left;
    const double paragraph_right = rect.right() - indent_right;
    const double paragraph_width = std::max(1.0, paragraph_right - paragraph_left);
    const int align_h = std::clamp(layer.align_h, 0, 6);

    auto aligned_x = [&](double line_left, double line_width, double text_width, int mode) {
        if (mode == 1 || mode == 4)
            return line_left + (line_width - text_width) / 2.0;
        if (mode == 2 || mode == 5)
            return line_left + line_width - text_width;
        return line_left;
    };

    if (layer.text_overflow_mode == 2) {
        QString single = overflow_layout_text(text, layer);
        QRectF bounds = metrics.boundingRect(single);
        QRectF fit_rect(paragraph_left + first_indent, rect.top(),
                        std::max(1.0, paragraph_width - first_indent), rect.height());
        double scale = horizontal_fit_scale(font, fit_rect, text, layer, t);
        if (fit_scale) *fit_scale = scale;
        double visual_width = bounds.width() * scale;
        double x = aligned_x(fit_rect.left(), fit_rect.width(), visual_width, align_h);
        double y = rect.top() - bounds.top();
        if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        else if (alignment & Qt::AlignBottom) y = rect.bottom() - bounds.height() - bounds.top();
        path.addText(QPointF(0, y), font, single);
        QTransform xf;
        xf.translate(x, 0.0);
        xf.scale(scale, 1.0);
        return xf.map(path);
    }
    if (fit_scale) *fit_scale = 1.0;

    struct Line {
        QString text;
        double width = 0.0;
        double ascent = 0.0;
        double height = 0.0;
        int paragraph = 0;
        bool first_in_paragraph = false;
        bool last_in_paragraph = false;
    };
    std::vector<Line> lines;
    const QStringList paragraphs = text.split('\n');
    QTextOption option;
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    for (int paragraph_index = 0; paragraph_index < paragraphs.size(); ++paragraph_index) {
        const QString &paragraph = paragraphs[paragraph_index];
        const size_t first_line_index = lines.size();
        if (paragraph.isEmpty()) {
            lines.push_back({QString(), 0.0, metrics.ascent(), metrics.lineSpacing(), paragraph_index, true, true});
            continue;
        }
        QTextLayout layout(paragraph, font);
        layout.setTextOption(option);
        layout.beginLayout();
        int line_index = 0;
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) break;
            const double line_indent = line_index == 0 ? first_indent : 0.0;
            const double line_width = std::max(1.0, paragraph_width - line_indent);
            line.setLineWidth(layer.text_overflow_mode == 0 ? line_width : 1000000.0);
            int start = line.textStart();
            int len = line.textLength();
            lines.push_back({paragraph.mid(start, len), line.naturalTextWidth(), line.ascent(), line.height(),
                             paragraph_index, line_index == 0, false});
            ++line_index;
            if (layer.text_overflow_mode != 0) break;
        }
        layout.endLayout();
        if (lines.size() > first_line_index)
            lines.back().last_in_paragraph = true;
    }
    double total_height = 0.0;
    const double leading = std::clamp((double)layer.text_leading, -200.0, 500.0);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].first_in_paragraph) total_height += space_before;
        total_height += lines[i].height;
        if (lines[i].last_in_paragraph)
            total_height += space_after;
        else if (i + 1 < lines.size())
            total_height += leading;
    }
    double y = rect.top();
    const bool distribute_vertical = layer.align_v == 3 && lines.size() > 1 && total_height < rect.height();
    const double distributed_gap = distribute_vertical
        ? (rect.height() - total_height) / (static_cast<double>(lines.size()) - 1.0)
        : 0.0;
    if (!distribute_vertical) {
        if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - total_height) / 2.0;
        else if (alignment & Qt::AlignBottom) y = rect.bottom() - total_height;
    }

    auto add_justified_line = [&](const Line &line, double line_left, double line_width, double baseline_y) {
        QStringList words = line.text.simplified().split(' ', Qt::SkipEmptyParts);
        if (words.size() < 2 || line.width >= line_width) {
            path.addText(QPointF(line_left, baseline_y), font, line.text);
            return;
        }
        double words_width = 0.0;
        for (const QString &word : words)
            words_width += metrics.horizontalAdvance(word);
        const double extra_space = std::max(0.0, (line_width - words_width) / (words.size() - 1));
        double word_x = line_left;
        for (int i = 0; i < words.size(); ++i) {
            path.addText(QPointF(word_x, baseline_y), font, words[i]);
            word_x += metrics.horizontalAdvance(words[i]) + extra_space;
        }
    };

    for (const auto &line : lines) {
        if (line.first_in_paragraph) y += space_before;
        const double line_indent = line.first_in_paragraph ? first_indent : 0.0;
        const double line_left = paragraph_left + line_indent;
        const double line_width = std::max(1.0, paragraph_width - line_indent);
        const bool justify_line = align_h == 6 || (align_h >= 3 && align_h <= 5 && !line.last_in_paragraph);
        if (justify_line) {
            add_justified_line(line, line_left, line_width, y + line.ascent);
        } else {
            double x = aligned_x(line_left, line_width, line.width, align_h);
            path.addText(QPointF(x, y + line.ascent), font, line.text);
        }
        y += line.height;
        if (line.last_in_paragraph)
            y += space_after;
        else
            y += leading;
        y += distributed_gap;
    }
    return path;
}


static QStringList ticker_lines(const QString &text, const QFont &font,
                                double available_width, bool wrap)
{
    QString normalized = text;
    normalized.replace('\r', '\n');
    const QStringList paragraphs = normalized.split('\n');
    QStringList lines;

    for (const QString &paragraph : paragraphs) {
        if (!wrap || available_width <= 1.0) {
            if (!paragraph.trimmed().isEmpty())
                lines << paragraph;
            continue;
        }

        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(std::max(1.0, available_width));
            const QString wrapped = paragraph.mid(line.textStart(), line.textLength());
            if (!wrapped.trimmed().isEmpty())
                lines << wrapped;
        }
        layout.endLayout();
    }

    if (lines.isEmpty())
        lines << QString();
    return lines;
}

static QPainterPath ticker_text_path(const QFont &font, const QRectF &rect,
                                     Qt::Alignment alignment, const QString &text,
                                     const Layer &layer, const std::string &title_id,
                                     bool title_cued, double local_time)
{
    struct TickerLayoutCache { QString key; QPainterPath path; QRectF bounds; };
    static thread_local TickerLayoutCache layout_cache;
    QPainterPath path;
    QFontMetricsF metrics(font);
    const double speed = std::max(1.0, layer.ticker_speed);
    const TickerRuntimeSnapshot ticker_state =
        fxm::ticker_runtime::sample(title_id, layer, title_cued);
    const double now = ticker_state.time_seconds;
    const bool custom_playback = layer.ticker_playback_mode ==
        static_cast<int>(TickerPlaybackMode::CustomPlayback);
    const double completion = std::clamp(
        layer.ticker_completion_prop.is_animated()
            ? layer.ticker_completion_prop.evaluate(local_time)
            : layer.ticker_completion,
        0.0, 100.0) / 100.0;

    if (layer.ticker_style == 0) {
        QString single = text;
        single.replace('\r', ' ');
        single.replace('\n', QStringLiteral("     •     "));
        QRectF bounds = metrics.boundingRect(single);
        const double text_w = std::max(1.0, bounds.width());
        const double travel = rect.width() + text_w;
        const double progress = custom_playback ? completion * travel : std::fmod(now * speed, travel);
        const double x = layer.ticker_direction == 0
            ? rect.left() - text_w + progress
            : rect.right() - progress;
        double y = rect.top() - bounds.top();
        if (alignment & Qt::AlignVCenter) y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        else if (alignment & Qt::AlignBottom) y = rect.bottom() - bounds.height() - bounds.top();
        path.addText(QPointF(x, y), font, single);
        return path;
    }

    const QStringList lines = ticker_lines(
        text, font, rect.width(),
        layer.ticker_style == 2 && layer.text_overflow_mode == 0);
    const int line_count = std::max(1, static_cast<int>(lines.size()));
    const double line_h = std::max(1.0, metrics.lineSpacing() + std::clamp((double)layer.text_leading, -200.0, 500.0));
    if (layer.ticker_style == 1) {
        const double hold = std::max(0.1, layer.ticker_line_hold);
        int idx = custom_playback ? std::min(line_count - 1, (int)std::floor(completion * line_count)) : (int)std::floor(now / hold) % line_count;
        if (layer.ticker_direction == 0) idx = line_count - 1 - idx;
        QString line = lines.at(idx);
        double line_w = metrics.horizontalAdvance(line);
        double x = rect.left();
        if (alignment & Qt::AlignHCenter) x = rect.left() + (rect.width() - line_w) / 2.0;
        else if (alignment & Qt::AlignRight) x = rect.right() - line_w;
        QRectF bounds = metrics.boundingRect(line);
        double y = rect.top() + (rect.height() - bounds.height()) / 2.0 - bounds.top();
        path.addText(QPointF(x, y), font, line);
        return path;
    }

    // Vertical Smooth uses the same paragraph layout engine as regular text.
    // This preserves explicit new lines/empty paragraphs, wrap mode, paragraph
    // indents and spacing, leading, and all justify-last/justify-all modes.
    Layer paragraph_layer = layer;
    paragraph_layer.align_v = 0; // ticker motion owns the vertical placement
    const double layout_height = std::max(
        rect.height(),
        (static_cast<double>(line_count) + 4.0) *
            (line_h + std::abs(eval_paragraph_space_before(layer, local_time)) +
             std::abs(eval_paragraph_space_after(layer, local_time)) + 1.0));
    const QRectF layout_rect(rect.left(), 0.0, rect.width(), layout_height);
    /* Ticker motion must not rebuild paragraph shaping every frame. Cache the
     * immutable layout and animate only its placement. Evaluated paragraph
     * values are part of the key, so animated typography still invalidates the
     * cache exactly when its visual result changes. */
    const QString layout_key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14")
        .arg(text)
        .arg(font.toString())
        .arg(rect.width(), 0, 'f', 3)
        .arg(layout_height, 0, 'f', 3)
        .arg(layer.align_h)
        .arg(layer.text_overflow_mode)
        .arg(eval_paragraph_space_before(layer, local_time), 0, 'f', 3)
        .arg(eval_paragraph_space_after(layer, local_time), 0, 'f', 3)
        .arg(eval_paragraph_indent_left(layer, local_time), 0, 'f', 3)
        .arg(eval_paragraph_indent_right(layer, local_time), 0, 'f', 3)
        .arg(eval_paragraph_indent_first_line(layer, local_time), 0, 'f', 3)
        .arg((double)layer.text_leading, 0, 'f', 3)
        .arg(eval_char_tracking(layer, local_time), 0, 'f', 3)
        .arg(eval_char_scale_x(layer, local_time), 0, 'f', 3);
    if (layout_cache.key != layout_key) {
        layout_cache.path = text_overflow_path(
            font, layout_rect, Qt::AlignLeft | Qt::AlignTop,
            text, paragraph_layer, local_time);
        layout_cache.bounds = layout_cache.path.boundingRect();
        if (layout_cache.bounds.isEmpty())
            layout_cache.bounds = QRectF(rect.left(), 0.0, rect.width(), line_h);
        layout_cache.key = layout_key;
    }
    const QPainterPath &paragraph_path = layout_cache.path;
    const QRectF paragraph_bounds = layout_cache.bounds;

    const double content_h = std::max(1.0, paragraph_bounds.bottom());
    const double travel = rect.height() + content_h;
    const double progress = custom_playback ? completion * travel : std::fmod(now * speed, travel);
    const double target_top = layer.ticker_direction == 0
        ? rect.top() - content_h + progress
        : rect.bottom() - progress;
    QTransform ticker_transform;
    ticker_transform.translate(0.0, target_top);
    return ticker_transform.map(paragraph_path);
}

static QColor color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static bool eval_background_enabled(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect && eval_effect_enabled(*effect, t);
}

static double eval_background_opacity(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::clamp(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(t) : (double)effect->effect_opacity, 0.0, 1.0) : 0.0;
}

static double eval_background_padding_x(const Layer &layer, double t)
{
    return 0.0;
}

static double eval_background_padding_y(const Layer &layer, double t)
{
    return 0.0;
}

static double eval_background_padding_left(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? (effect->padding_left_prop.is_animated() ? effect->padding_left_prop.evaluate(t) : (double)effect->effect_padding_left) : 0.0;
}

static double eval_background_padding_right(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? (effect->padding_right_prop.is_animated() ? effect->padding_right_prop.evaluate(t) : (double)effect->effect_padding_right) : 0.0;
}

static double eval_background_padding_top(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? (effect->padding_top_prop.is_animated() ? effect->padding_top_prop.evaluate(t) : (double)effect->effect_padding_top) : 0.0;
}

static double eval_background_padding_bottom(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? (effect->padding_bottom_prop.is_animated() ? effect->padding_bottom_prop.evaluate(t) : (double)effect->effect_padding_bottom) : 0.0;
}

static double eval_background_corner_radius(const Layer &layer, double t)
{
    return 0.0;
}

static double eval_background_corner_radius_tl(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::max(0.0, effect->corner_radius_tl_prop.is_animated() ? effect->corner_radius_tl_prop.evaluate(t) : (double)effect->effect_corner_radius_tl) : 0.0;
}

static double eval_background_corner_radius_tr(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::max(0.0, effect->corner_radius_tr_prop.is_animated() ? effect->corner_radius_tr_prop.evaluate(t) : (double)effect->effect_corner_radius_tr) : 0.0;
}

static double eval_background_corner_radius_br(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::max(0.0, effect->corner_radius_br_prop.is_animated() ? effect->corner_radius_br_prop.evaluate(t) : (double)effect->effect_corner_radius_br) : 0.0;
}

static double eval_background_corner_radius_bl(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::max(0.0, effect->corner_radius_bl_prop.is_animated() ? effect->corner_radius_bl_prop.evaluate(t) : (double)effect->effect_corner_radius_bl) : 0.0;
}

static double eval_background_stroke_width(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::max(0.0, effect->stroke_width_prop.is_animated() ? effect->stroke_width_prop.evaluate(t) : (double)effect->effect_stroke_width) : 0.0;
}

static double eval_background_stroke_opacity(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? std::clamp(effect->stroke_opacity_prop.is_animated() ? effect->stroke_opacity_prop.evaluate(t) : (double)effect->effect_stroke_opacity, 0.0, 1.0) : 1.0;
}

static uint32_t eval_background_color(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? eval_effect_color(*effect, t) : 0x00000000;
}

static uint32_t eval_background_stroke_color(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    return effect ? eval_effect_stroke_color(*effect, t) : 0x00000000;
}

static QColor evaluated_background_color(const Layer &layer, double t)
{
    QColor color = color_from_argb(eval_background_color(layer, t));
    color.setAlphaF(std::clamp((double)color.alphaF() * eval_background_opacity(layer, t), 0.0, 1.0));
    return color;
}






static void apply_rich_text_extended_font_properties(QFont &font, const RichTextCharFormat &format)
{
    if (!format.font_style.empty())
        font.setStyleName(QString::fromStdString(format.font_style));
    font.setKerning(format.kerning_mode != 2 && format.kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing,
                          format.tracking + (format.kerning_mode == 2 ? format.manual_kerning : 0.0f));
    /* QTextDocument is only a compatibility raster. Encode V Scale in the
     * font size, but never encode H/V into QFont::stretch: DirectWrite may
     * switch between discrete width faces at a matching threshold (the
     * reported 89%/90% discontinuity). A continuous residual X transform is
     * applied by the positioned-document helpers when all effective runs share
     * the same H/V ratio. Ordinary and multistyle canvas text stays on the GPU
     * per-glyph path. */
    const double scale_y = std::clamp(
        static_cast<double>(format.scale_y), 0.01, 100.0);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(font.pixelSize()) * scale_y))));
    else if (font.pointSizeF() > 0.0)
        font.setPointSizeF(std::max(0.1, font.pointSizeF() * scale_y));
    font.setStretch(100);
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
    /* Preserve shaping for complex scripts. Ligature feature control moves to
     * the Phase 12 HarfBuzz shaping stage instead of disabling shaping. */
}

static void apply_rich_text_extended_char_format(QTextCharFormat &out, const RichTextCharFormat &format)
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

static void apply_rich_text_baseline_delta(QTextCharFormat &out,
                                           const RichTextCharFormat &format,
                                           float default_baseline_shift)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto scale_metrics = rich_text_font_scale_metrics(format.scale_x, format.scale_y);
    const double glyph_height = std::max<double>(
        1.0, static_cast<double>(format.font_size) *
                 static_cast<double>(scale_metrics.vertical_factor));
    out.setBaselineOffset(100.0 * (format.baseline_shift - default_baseline_shift) / glyph_height);
#else
    (void)out;
    (void)format;
    (void)default_baseline_shift;
#endif
}

static QBrush rich_text_fill_brush(const RichTextFill &fill, const QRectF &box)
{
    if (fill.type != 1)
        return QBrush(color_from_argb(fill.color));

    return make_gradient_brush(fill.gradient_type, fill.gradient_spread, box,
                               std::clamp((double)fill.gradient_opacity, 0.0, 1.0),
                               fill.gradient_center_x, fill.gradient_center_y,
                               fill.gradient_focal_x, fill.gradient_focal_y,
                               fill.gradient_scale, fill.gradient_angle,
                               std::clamp((double)fill.gradient_start_pos, 0.0, 1.0),
                               fill.gradient_start_color, fill.gradient_start_opacity,
                               std::clamp((double)fill.gradient_end_pos, 0.0, 1.0),
                               fill.gradient_end_color, fill.gradient_end_opacity,
                               {});
}


class SourceQtUtf8PositionMap {
public:
    explicit SourceQtUtf8PositionMap(const QString &text)
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

static Qt::Alignment rich_text_qt_alignment_source(int align_h)
{
    if (align_h == 1 || align_h == 4) return Qt::AlignHCenter;
    if (align_h == 2 || align_h == 5) return Qt::AlignRight;
    if (align_h >= 3) return Qt::AlignJustify;
    return Qt::AlignLeft;
}

static QTextBlockFormat text_block_format_from_rich_format(
    const RichTextParagraphFormat &format)
{
    QTextBlockFormat out;
    out.setAlignment(rich_text_qt_alignment_source(format.align_h));
    out.setLeftMargin(std::max(0.0f, format.indent_left));
    out.setRightMargin(std::max(0.0f, format.indent_right));
    out.setTextIndent(format.indent_first_line);
    out.setTopMargin(std::max(0.0f, format.space_before));
    out.setBottomMargin(std::max(0.0f, format.space_after));
    if (std::abs(format.line_spacing) >= 0.0001f)
        out.setLineHeight(format.line_spacing, QTextBlockFormat::LineDistanceHeight);
    else
        out.setLineHeight(0.0, QTextBlockFormat::SingleHeight);
    return out;
}

static void apply_rich_text_paragraph_blocks(QTextDocument &doc,
                                             const RichTextDocument &model)
{
    const QString qplain = doc.toPlainText();
    const SourceQtUtf8PositionMap positions(qplain);
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const size_t start = positions.byte_from_qpos(block.position());
        RichTextParagraphFormat effective = model.default_paragraph_format;
        for (const auto &rich_block : model.blocks) {
            if (rich_block.start != start)
                continue;
            rich_text_merge_paragraph_format(effective, rich_block.format,
                                             rich_block.mask);
            break;
        }
        QTextCursor cursor(block);
        cursor.setBlockFormat(text_block_format_from_rich_format(effective));
    }
}

static Qt::PenJoinStyle rich_text_stroke_join_style(int join_style)
{
    switch (join_style) {
    case 1: return Qt::RoundJoin;
    case 2: return Qt::BevelJoin;
    default: return Qt::MiterJoin;
    }
}

static QPen rich_text_stroke_pen(const RichTextStroke &stroke,
                                 const QRectF &text_rect)
{
    if (!stroke.enabled || stroke.width <= 0.0f)
        return QPen(Qt::NoPen);
    RichTextFill fill = stroke.fill;
    if (fill.type == 0) {
        QColor color = color_from_argb(fill.color);
        color.setAlphaF(std::clamp((double)color.alphaF() *
                                   (double)stroke.opacity, 0.0, 1.0));
        fill.color = ((uint32_t)color.alpha() << 24) |
                     ((uint32_t)color.red() << 16) |
                     ((uint32_t)color.green() << 8) |
                     (uint32_t)color.blue();
    } else {
        fill.gradient_opacity = (float)std::clamp(
            (double)fill.gradient_opacity * (double)stroke.opacity, 0.0, 1.0);
    }
    const qreal painted_width = stroke.alignment == 1
        ? std::max(0.0f, stroke.width)
        : std::max(0.0f, stroke.width) * 2.0;
    return QPen(rich_text_fill_brush(fill, text_rect), painted_width,
                Qt::SolidLine, Qt::RoundCap,
                rich_text_stroke_join_style(stroke.join_style));
}

static QTextCharFormat text_format_from_rich_format(const RichTextCharFormat &format, const QFont &fallback_font,
                                                     const QRectF &text_rect)
{
    QTextCharFormat out;
    QFont font = fallback_font;
    if (!format.font_family.empty()) font.setFamily(QString::fromStdString(format.font_family));
    font.setPixelSize(std::max(1, format.font_size));
    font.setBold(format.bold);
    font.setItalic(format.italic);
    font.setUnderline(format.underline);
    font.setStrikeOut(format.strikethrough);
    apply_rich_text_extended_font_properties(font, format);
    out.setFont(font);
    out.setFontUnderline(format.underline);
    out.setFontStrikeOut(format.strikethrough);
    apply_rich_text_extended_char_format(out, format);
    out.setForeground(rich_text_fill_brush(format.fill, text_rect));
    out.setTextOutline(rich_text_stroke_pen(format.stroke, text_rect));
    return out;
}

static bool resolve_auto_text_style_preset(const std::string &preset_id, RichTextCharFormat &format, uint32_t &mask)
{
    return fxm::style_presets::resolve_text_preset(preset_id, format, mask);
}

static RichTextDocument rich_text_model_for_source_time(const Layer &layer,
                                                        double local_time)
{
    RichTextDocument canonical = rich_text_document_canonical_copy(layer);
    const bool external_text_bound =
        layer_has_external_binding(layer, ExternalPropertyPaths::TextContent);
    if (layer.type == LayerType::Clock || layer.type == LayerType::Ticker ||
        external_text_bound) {
        const std::string display_text =
            display_text_for_style(layer).toUtf8().toStdString();
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(canonical);
        rich_text_document_replace_text_canonical(
            canonical, display_text, insertion_format,
            canonical.has_typing_format
                ? canonical.typing_format_mask
                : 0);
    }

    RichTextEvaluatedDefaults defaults;
    defaults.font_size = std::max(
        1, static_cast<int>(std::round(eval_text_font_size(layer, local_time))));
    defaults.tracking =
        static_cast<float>(eval_char_tracking(layer, local_time));
    defaults.scale_x =
        static_cast<float>(eval_char_scale_x(layer, local_time));
    defaults.scale_y =
        static_cast<float>(eval_char_scale_y(layer, local_time));
    defaults.baseline_shift =
        static_cast<float>(eval_baseline_shift(layer, local_time));
    defaults.solid_fill_color = eval_text_color(layer, local_time);
    defaults.align_h = canonical.default_paragraph_format.align_h;
    defaults.align_v = canonical.default_paragraph_format.align_v;
    defaults.indent_left =
        static_cast<float>(eval_paragraph_indent_left(layer, local_time));
    defaults.indent_right =
        static_cast<float>(eval_paragraph_indent_right(layer, local_time));
    defaults.indent_first_line = static_cast<float>(
        eval_paragraph_indent_first_line(layer, local_time));
    defaults.line_spacing = canonical.default_paragraph_format.line_spacing;
    defaults.space_before =
        static_cast<float>(eval_paragraph_space_before(layer, local_time));
    defaults.space_after =
        static_cast<float>(eval_paragraph_space_after(layer, local_time));
    defaults.hyphenate = canonical.default_paragraph_format.hyphenate;
    RichTextDocument model =
        rich_text_document_with_evaluated_defaults_canonical(
            std::move(canonical), defaults);
    /* Static default and inline stroke are already part of the canonical
     * document. Do not merge compatibility layer mirrors back into it. */
    return rich_text_document_with_auto_styles_canonical(
        std::move(model), resolve_auto_text_style_preset);
}

/* QTextDocument can reproduce anisotropic scale continuously with a painter
 * transform when every effective style run has the same H/V ratio. This covers
 * layer-wide H/V Scale and proportional multistyle runs without asking the
 * platform font matcher to synthesize or substitute a width face. */
static double rich_text_compatibility_x_ratio(const Layer &layer, double t)
{
    const RichTextDocument model = rich_text_model_for_source_time(layer, t);
    const auto ratio_for = [](const RichTextCharFormat &format) {
        const double sx = std::clamp(
            static_cast<double>(format.scale_x), 0.01, 100.0);
        const double sy = std::clamp(
            static_cast<double>(format.scale_y), 0.01, 100.0);
        return sx / sy;
    };

    const double reference = ratio_for(model.default_format);
    const size_t text_size = model.plain_text.size();
    std::vector<size_t> boundaries;
    boundaries.reserve(model.ranges.size() * 2 + 1);
    boundaries.push_back(0);
    for (const RichTextRange &range : model.ranges) {
        const size_t start = std::min(range.start, text_size);
        const size_t end = start + std::min(range.length, text_size - start);
        boundaries.push_back(start);
        boundaries.push_back(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    for (size_t offset : boundaries) {
        const double ratio = ratio_for(
            rich_text_format_at(model, std::min(offset, text_size)));
        if (std::abs(ratio - reference) > 0.0001)
            return 1.0;
    }
    return std::clamp(reference, 0.01, 100.0);
}

static double max_rich_text_stroke_width(const Layer &layer, double t)
{
    const RichTextDocument model = rich_text_model_for_source_time(layer, t);
    double maximum = 0.0;
    for (const TextLayoutPaintRun &run :
         text_layout_paint_runs_canonical(model)) {
        const RichTextStroke &stroke = run.style.stroke;
        if (stroke.enabled)
            maximum = std::max(maximum,
                               static_cast<double>(std::max(0.0f, stroke.width)));
    }
    return maximum;
}

static double max_rich_text_font_height_hint(const Layer &layer, double t)
{
    const RichTextDocument model = rich_text_model_for_source_time(layer, t);
    const auto format_height = [](const RichTextCharFormat &format) {
        const double font_size =
            static_cast<double>(std::max(1, format.font_size));
        const double scale_y = std::max(
            0.01, std::abs(static_cast<double>(format.scale_y)));
        return font_size * scale_y;
    };

    double maximum = std::max(1.0, format_height(model.default_format));

    /* TextLayoutPaintStyle deliberately contains paint-only state. Font size
     * and vertical scale are shaping properties, so derive the envelope from
     * the effective rich-text character formats at every range boundary. This
     * keeps mixed-size/scale text safe without coupling paint runs to shaping
     * data or relying on fields that do not exist on TextLayoutPaintStyle. */
    std::vector<size_t> boundaries;
    boundaries.reserve(model.ranges.size() * 2 + 1);
    boundaries.push_back(0);
    const size_t text_size = model.plain_text.size();
    for (const RichTextRange &range : model.ranges) {
        const size_t start = std::min(range.start, text_size);
        const size_t remaining = text_size - start;
        const size_t end = start + std::min(range.length, remaining);
        boundaries.push_back(start);
        boundaries.push_back(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());

    for (size_t offset : boundaries) {
        if (offset >= text_size && text_size != 0)
            offset = rich_text_utf8_previous_boundary(model.plain_text,
                                                       text_size - 1);
        maximum = std::max(maximum,
                           format_height(rich_text_format_at(model, offset)));
    }
    return maximum;
}


/* Conservative extra ink envelope required by anisotropic character scaling.
 * The immutable layout applies scale around each glyph baseline, so values
 * above 100% may extend outside the authored text box even when line wrapping
 * remains inside it. This hint is used only for raster allocation/clipping; it
 * does not change shaping, line breaks or alignment. */
static double max_rich_text_scale_overhang_hint(const Layer &layer, double t)
{
    const RichTextDocument model = rich_text_model_for_source_time(layer, t);
    const auto format_overhang = [](const RichTextCharFormat &format) {
        const double font_size = static_cast<double>(std::max(1, format.font_size));
        const double scale_x = std::clamp(
            std::abs(static_cast<double>(format.scale_x)), 0.01, 100.0);
        const double scale_y = std::clamp(
            std::abs(static_cast<double>(format.scale_y)), 0.01, 100.0);
        return font_size * std::max({0.0, scale_x - 1.0, scale_y - 1.0});
    };

    double maximum = format_overhang(model.default_format);
    const size_t text_size = model.plain_text.size();
    std::vector<size_t> boundaries;
    boundaries.reserve(model.ranges.size() * 2 + 1);
    boundaries.push_back(0);
    for (const RichTextRange &range : model.ranges) {
        const size_t start = std::min(range.start, text_size);
        const size_t end = start + std::min(range.length, text_size - start);
        boundaries.push_back(start);
        boundaries.push_back(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    for (size_t offset : boundaries)
        maximum = std::max(maximum, format_overhang(
            rich_text_format_at(model, std::min(offset, text_size))));
    return maximum;
}

static RichTextParagraphFormat source_paragraph_format_for_block(
    const RichTextDocument &model, const SourceQtUtf8PositionMap &positions,
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

/* Qt's AlignJustify always leaves the final visual line left aligned and has
 * no native equivalents for "justify with last line centered/right" or
 * "justify all". Preserve Qt shaping/wrapping for the preceding lines, then
 * apply the missing final-line semantics directly to the laid-out block. */
static void apply_rich_text_last_line_justification(QTextDocument &doc,
                                                    const RichTextDocument &model)
{
    const QString plain_text = doc.toPlainText();
    const SourceQtUtf8PositionMap positions(plain_text);
    (void)doc.size(); // force QTextDocument/QTextLayout line creation

    bool relayout_needed = false;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextLayout *layout = block.layout();
        if (!layout || layout->lineCount() <= 0)
            continue;
        const RichTextParagraphFormat format =
            source_paragraph_format_for_block(model, positions, block);
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
            source_paragraph_format_for_block(model, positions, block);
        if (format.align_h != 4 && format.align_h != 5)
            continue;

        QTextLine last = layout->lineAt(layout->lineCount() - 1);
        QPointF position = last.position();
        const double spare = std::max(0.0, last.width() - last.naturalTextWidth());
        position.setX(format.align_h == 4 ? spare * 0.5 : spare);
        last.setPosition(position);
    }
}

static void apply_rich_text_ranges(QTextDocument &doc, const Layer &layer, const QFont &font,
                                   const QRectF &text_rect, double t)
{
    const RichTextDocument model =
        rich_text_model_for_source_time(layer, t);
    QTextCursor all(&doc);
    all.select(QTextCursor::Document);
    all.mergeCharFormat(text_format_from_rich_format(model.default_format, font, text_rect));
    const QString qplain = QString::fromStdString(model.plain_text);
    const SourceQtUtf8PositionMap positions(qplain);
    for (const auto &range : model.ranges) {
        if (range.length == 0 || range.start >= model.plain_text.size()) continue;
        QTextCursor cursor(&doc);
        const int qstart = positions.qpos_from_byte(range.start);
        const int qend = positions.qpos_from_byte(range.start + range.length);
        cursor.setPosition(std::clamp(qstart, 0, static_cast<int>(qplain.size())));
        cursor.setPosition(std::clamp(qend, 0, static_cast<int>(qplain.size())), QTextCursor::KeepAnchor);
        const RichTextCharFormat effective = rich_text_format_at(model, range.start);
        QTextCharFormat format = text_format_from_rich_format(effective, font, text_rect);
        apply_rich_text_baseline_delta(format, effective, model.default_format.baseline_shift);
        cursor.mergeCharFormat(format);
    }
    apply_rich_text_paragraph_blocks(doc, model);
}

struct TextTransitionUnitRange {
    int start = 0;
    int length = 0;
};

static void hide_rich_text_outside_range(QTextDocument &doc,
                                         const TextTransitionUnitRange *visible_range)
{
    if (!visible_range)
        return;

    const int text_length = std::max(0, doc.characterCount() - 1);
    const int start = std::clamp(visible_range->start, 0, text_length);
    const int end = std::clamp(start + std::max(0, visible_range->length), start, text_length);

    QTextCharFormat hidden;
    hidden.setForeground(QBrush(Qt::transparent));
    hidden.setTextOutline(QPen(Qt::NoPen));
    hidden.setFontUnderline(false);
    hidden.setFontStrikeOut(false);

    auto hide_range = [&](int from, int to) {
        if (to <= from)
            return;
        QTextCursor cursor(&doc);
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(hidden);
    };

    hide_range(0, start);
    hide_range(end, text_length);
}

static int rich_text_document_visual_line_count(const QTextDocument &doc)
{
    int count = 0;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (layout)
            count += layout->lineCount();
    }
    return count;
}

static void apply_rich_text_vertical_distribute(QTextDocument &doc, const Layer &layer, const QRectF &text_rect)
{
    if (layer.align_v != 3 || layer.text_overflow_mode == 2)
        return;

    const QSizeF natural_size = doc.size();
    const int line_count = rich_text_document_visual_line_count(doc);
    if (line_count <= 1 || natural_size.height() >= text_rect.height())
        return;

    const double extra_gap = (text_rect.height() - natural_size.height()) /
                             (static_cast<double>(line_count) - 1.0);
    if (extra_gap <= 0.0)
        return;

    QTextBlockFormat block_format;
    block_format.setLineHeight(extra_gap, QTextBlockFormat::LineDistanceHeight);
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(block_format);
}

static std::unique_ptr<QTextDocument> rich_text_document_for_layer(const Layer &layer, const QFont &font,
                                                  const QRectF &text_rect, double t,
                                                  const TextTransitionUnitRange *visible_range = nullptr)
{
    auto doc = std::make_unique<QTextDocument>();
    doc->setDocumentMargin(0.0);
    doc->setDefaultFont(font);
    const QColor default_color = color_from_argb(eval_text_color(layer, t));
    doc->setDefaultStyleSheet(QStringLiteral("body{color:%1;}").arg(default_color.name(QColor::HexRgb)));
    QTextOption option = doc->defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    Qt::Alignment align = Qt::AlignLeft;
    if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
    else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
    else if (layer.align_h >= 3) align = Qt::AlignJustify;
    option.setAlignment(align);
    doc->setDefaultTextOption(option);
    doc->setTextWidth(layer.text_overflow_mode == 2 ? -1.0 : std::max(1.0, text_rect.width()));
    const RichTextDocument render_model =
        rich_text_model_for_source_time(layer, t);
    doc->setPlainText(QString::fromStdString(render_model.plain_text));
    apply_rich_text_ranges(*doc, layer, font,
                           QRectF(0.0, 0.0, text_rect.width(), text_rect.height()), t);
    apply_rich_text_last_line_justification(*doc, render_model);

    hide_rich_text_outside_range(*doc, visible_range);
    apply_rich_text_vertical_distribute(*doc, layer, text_rect);
    return doc;
}

static double rich_text_horizontal_fit_scale(const Layer &layer,
                                             const QRectF &text_rect,
                                             const QSizeF &document_size)
{
    if (layer.text_overflow_mode != 2 || document_size.width() <= text_rect.width())
        return 1.0;

    return std::clamp(text_rect.width() / std::max(1.0, document_size.width()),
                      std::clamp(static_cast<double>(layer.text_fit_min_scale), 0.05, 1.0),
                      1.0);
}

static double rich_text_document_horizontal_scale(
    const Layer &layer, const QRectF &text_rect,
    const QSizeF &document_size, double t)
{
    const double ratio = rich_text_compatibility_x_ratio(layer, t);
    const QSizeF ratio_size(document_size.width() * ratio,
                            document_size.height());
    return ratio * rich_text_horizontal_fit_scale(
        layer, text_rect, ratio_size);
}

static QPointF rich_text_document_origin(const Layer &layer,
                                         const QRectF &text_rect,
                                         const QSizeF &document_size,
                                         double t,
                                         double horizontal_scale)
{
    QPointF origin = text_rect.topLeft();

    if (layer.text_overflow_mode == 2) {
        const double visual_width = document_size.width() * horizontal_scale;
        if (layer.align_h == 1 || layer.align_h == 4)
            origin.setX(text_rect.left() + (text_rect.width() - visual_width) / 2.0);
        else if (layer.align_h == 2 || layer.align_h == 5)
            origin.setX(text_rect.right() - visual_width);
    }

    if (layer.align_v == 1)
        origin.setY(text_rect.top() + (text_rect.height() - document_size.height()) / 2.0);
    else if (layer.align_v == 2)
        origin.setY(text_rect.bottom() - document_size.height());
    if (std::abs(eval_baseline_shift(layer, t)) > 0.0001)
        origin.setY(origin.y() - eval_baseline_shift(layer, t));

    return origin;
}

static QPointF rich_text_document_cursor_position(const QTextDocument &doc,
                                                  const Layer &layer,
                                                  const QRectF &text_rect,
                                                  double t,
                                                  int document_position)
{
    const QAbstractTextDocumentLayout *document_layout = doc.documentLayout();
    if (!document_layout)
        return text_rect.topLeft();

    const int text_length = std::max(0, doc.characterCount() - 1);
    const int clamped_position = std::clamp(document_position, 0, text_length);
    QTextBlock block = doc.findBlock(clamped_position);
    if (!block.isValid())
        block = doc.lastBlock();
    if (!block.isValid() || !block.layout())
        return text_rect.topLeft();

    const QTextLayout *layout = block.layout();
    const int relative_position = std::clamp(
        clamped_position - block.position(), 0, std::max(0, block.length() - 1));
    QTextLine line = layout->lineForTextPosition(relative_position);
    if (!line.isValid() && layout->lineCount() > 0)
        line = layout->lineAt(layout->lineCount() - 1);
    if (!line.isValid())
        return text_rect.topLeft();

    const QRectF block_rect = document_layout->blockBoundingRect(block);
    QPointF local(block_rect.left() + line.cursorToX(relative_position),
                  block_rect.top() + line.y());

    const QSizeF document_size = doc.size();
    const double horizontal_scale = rich_text_document_horizontal_scale(
        layer, text_rect, document_size, t);
    const QPointF origin = rich_text_document_origin(layer, text_rect, document_size, t, horizontal_scale);
    local.setX(local.x() * horizontal_scale);
    return origin + local;
}

static QRectF rich_text_document_range_bounds(const QTextDocument &doc,
                                               const Layer &layer,
                                               const QRectF &text_rect,
                                               double t,
                                               const TextTransitionUnitRange &range)
{
    const QAbstractTextDocumentLayout *document_layout = doc.documentLayout();
    if (!document_layout)
        return {};

    const int text_length = std::max(0, doc.characterCount() - 1);
    const int range_start = std::clamp(range.start, 0, text_length);
    const int range_end = std::clamp(range_start + std::max(0, range.length),
                                     range_start, text_length);
    if (range_end <= range_start)
        return {};

    /* Phase 12B: transition units consume the same immutable glyph/cluster
     * layout that will feed the GPU renderer. QTextDocument remains only as
     * the temporary raster adapter until Phase 12C. */
    const ImmutableTextLayout shaped_layout = source_text_layout_for_metrics(
        layer, t, text_rect.width(), text_rect.height(),
        layer.text_overflow_mode);
    if (shaped_layout && shaped_layout->valid) {
        const QString plain_text = doc.toPlainText();
        const SourceQtUtf8PositionMap positions(plain_text);
        const size_t byte_start = positions.byte_from_qpos(range_start);
        const size_t byte_end = positions.byte_from_qpos(range_end);
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        if (text_layout_range_bounds(*shaped_layout, byte_start,
                                     byte_end - byte_start, x, y,
                                     width, height)) {
            return QRectF(text_rect.left() + x, text_rect.top() + y,
                          std::max(1.0f, width),
                          std::max(1.0f, height));
        }
    }

    QRectF local_bounds;
    bool has_bounds = false;
    for (QTextBlock block = doc.findBlock(range_start);
         block.isValid() && block.position() < range_end;
         block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (!layout)
            continue;
        const QRectF block_rect = document_layout->blockBoundingRect(block);
        for (int line_index = 0; line_index < layout->lineCount(); ++line_index) {
            const QTextLine line = layout->lineAt(line_index);
            if (!line.isValid())
                continue;
            const int line_start = block.position() + line.textStart();
            const int line_end = line_start + line.textLength();
            const int overlap_start = std::max(range_start, line_start);
            const int overlap_end = std::min(range_end, line_end);
            if (overlap_end <= overlap_start)
                continue;
