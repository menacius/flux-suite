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

static QPainterPath editor_scene_mask_layer_path(const Layer &layer, const QRectF &local_rect,
                                                   double local_time, const std::string &title_id)
{
    if (layer.type == LayerType::Text || layer.type == LayerType::Clock || layer.type == LayerType::Ticker) {
        QFont font = font_for_layer(layer, local_time);
        QRectF text_rect = text_rect_for_style(local_rect, layer);
        Qt::Alignment align = Qt::AlignLeft;
        if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
        else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
        else if (layer.align_h >= 3) align = Qt::AlignJustify;
        if (layer.align_v == 1) align |= Qt::AlignVCenter;
        else if (layer.align_v == 2) align |= Qt::AlignBottom;
        else align |= Qt::AlignTop;

        QString text = display_text_for_style(layer);
        if (layer.type == LayerType::Text && !layer.rich_text.plain_text.empty() &&
            !layer_has_external_binding(layer, ExternalPropertyPaths::TextContent))
            text = QString::fromStdString(layer.rich_text.plain_text);

        QPainterPath text_path = layer.type == LayerType::Ticker
            ? ticker_text_path(font, text_rect, align, text, layer, title_id, local_time)
            : text_overflow_path(font, text_rect, align, text, layer, local_time);
        text_path = apply_vertical_character_scale(text_path, text_rect, align, layer, local_time);
        if (std::abs(eval_baseline_shift(layer, local_time)) > 0.0001)
            text_path.translate(0.0, -eval_baseline_shift(layer, local_time));
        if (!text_path.isEmpty())
            return text_path;
    }

    return editor_scene_mask_shape_path(layer, local_rect);
}

static bool title_has_dynamic_text_layer(const std::shared_ptr<Title> &title)
{
    if (!title) return false;
    const bool dynamic_text = std::any_of(title->layers.begin(), title->layers.end(), [](const std::shared_ptr<Layer> &layer) {
        return layer && (layer->type == LayerType::Clock || layer->type == LayerType::Ticker);
    });
    return dynamic_text ||
           fxm::asset_runtime::title_has_independent_playback(*title);
}

static QColor color_from_argb(uint32_t argb)
{
    return QColor((argb >> 16) & 0xFF,
                  (argb >> 8) & 0xFF,
                  argb & 0xFF,
                  (argb >> 24) & 0xFF);
}

static uint32_t argb_from_color(const QColor &color)
{
    return ((uint32_t)color.alpha() << 24) |
           ((uint32_t)color.red() << 16) |
           ((uint32_t)color.green() << 8) |
           (uint32_t)color.blue();
}

static QPointF anchor_point_from_index(int index)
{
    static const QPointF anchors[] = {
        {0.0, 0.0}, {0.5, 0.0}, {1.0, 0.0},
        {0.0, 0.5}, {0.5, 0.5}, {1.0, 0.5},
        {0.0, 1.0}, {0.5, 1.0}, {1.0, 1.0},
    };
    if (index < 0 || index >= 9) return anchors[4];
    return anchors[index];
}

static int anchor_index_from_layer(const Layer &layer)
{
    int x = layer.origin_x < 0.25f ? 0 : (layer.origin_x > 0.75f ? 2 : 1);
    int y = layer.origin_y < 0.25f ? 0 : (layer.origin_y > 0.75f ? 2 : 1);
    return y * 3 + x;
}

static QPointF rotated_scaled_delta(double dx, double dy, double rot_deg, double sx, double sy)
{
    double rot = rot_deg * 3.14159265358979323846 / 180.0;
    double x = dx * sx;
    double y = dy * sy;
    double c = std::cos(rot);
    double s = std::sin(rot);
    return QPointF(x * c - y * s, x * s + y * c);
}


static bool is_text_box_auto_size_layer(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock;
}

static bool is_canvas_text_layer(const Layer &layer)
{
    return layer.type == LayerType::Text || layer.type == LayerType::Clock || layer.type == LayerType::Ticker;
}

static ImmutableTextLayout editor_text_layout_for_metrics(const Layer &layer,
                                                           double local_time,
                                                           double width,
                                                           int overflow_mode,
                                                           double height = 0.0)
{
    RichTextDocument model = rich_text_document_for_editor_time(layer, local_time);
    QString display_text = display_text_for_style(layer);
    if (overflow_mode == 2)
        display_text = overflow_layout_text(display_text, layer);
    const std::string display_utf8 = display_text.toUtf8().toStdString();
    if (model.plain_text != display_utf8) {
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(model);
        rich_text_document_replace_text_canonical(
            model, display_utf8, insertion_format,
            model.has_typing_format ? model.typing_format_mask : 0);
    }
    model = rich_text_document_with_auto_styles_canonical(
        std::move(model), resolve_editor_auto_text_style_preset);

    TextLayoutRequest request;
    request.document = std::move(model);
    request.max_width = static_cast<float>(std::max(0.0, width));
    request.max_height = static_cast<float>(std::max(0.0, height));
    request.device_scale = 1.0f;
    request.minimum_horizontal_fit =
        std::clamp(layer.text_fit_min_scale, 0.05f, 1.0f);
    request.overflow_mode = overflow_mode;
    return cached_text_layout(request);
}

static double natural_text_width(const Layer &layer, double local_time = 0.0)
{
    if (!is_text_box_auto_size_layer(layer)) return 1.0;
    const ImmutableTextLayout layout =
        editor_text_layout_for_metrics(layer, local_time, 0.0, 1);
    return layout && layout->valid
               ? std::ceil(std::max(1.0f, layout->natural_width))
               : 1.0;
}

static double natural_text_height(const Layer &layer, double width,
                                  double local_time = 0.0)
{
    if (!is_text_box_auto_size_layer(layer)) return 1.0;
    const ImmutableTextLayout layout = editor_text_layout_for_metrics(
        layer, local_time, width, layer.text_overflow_mode);
    return layout && layout->valid
               ? std::ceil(std::max(1.0f, layout->natural_height))
               : 1.0;
}

static double eval_box_width(const Layer &layer, double t)
{
    double width = layer.size.is_animated()
        ? layer.size.evaluate(t).x
        : static_cast<double>(layer.rect_width);
    if (layer.text_box_width_to_text && is_text_box_auto_size_layer(layer))
        width = std::min(natural_text_width(layer, t), std::max(1.0, (double)layer.max_text_box_width));
    return std::max(0.0, width);
}

static double eval_box_height(const Layer &layer, double t)
{
    double height = layer.size.is_animated()
        ? layer.size.evaluate(t).y
        : static_cast<double>(layer.rect_height);
    if (layer.text_box_height_to_text && is_text_box_auto_size_layer(layer)) {
        const double width = eval_box_width(layer, t);
        height = std::min(natural_text_height(layer, width, t), std::max(1.0, (double)layer.max_text_box_height));
    }
    return std::max(0.0, height);
}

static double eval_image_width(const Layer &layer, double t)
{
    const double width = layer.image_size.is_animated()
        ? layer.image_size.evaluate(t).x
        : static_cast<double>(layer.image_width);
    return std::max(0.0, width);
}

static double eval_image_height(const Layer &layer, double t)
{
    const double height = layer.image_size.is_animated()
        ? layer.image_size.evaluate(t).y
        : static_cast<double>(layer.image_height);
    return std::max(0.0, height);
}

static int shadow_pass_count(double blur)
{
    const int passes = static_cast<int>(std::ceil(blur / 3.0));
    return passes < 1 ? 1 : passes;
}

static double eval_origin_x(const Layer &layer, double t)
{
    return std::clamp(layer.origin_prop.is_animated()
                          ? layer.origin_prop.evaluate(t).x
                          : (double)layer.origin_x,
                      0.0, 1.0);
}

static double eval_origin_y(const Layer &layer, double t)
{
    return std::clamp(layer.origin_prop.is_animated()
                          ? layer.origin_prop.evaluate(t).y
                          : (double)layer.origin_y,
                      0.0, 1.0);
}

static int eval_channel(const AnimatedProperty &prop, double fallback, double t)
{
    return (int)std::clamp(std::round(prop.is_animated() ? prop.evaluate(t) : fallback),
                           0.0, 255.0);
}

static uint32_t eval_text_color(const Layer &layer, double t)
{
    const uint32_t authored = !layer.rich_text.empty()
        ? layer.rich_text.default_format.fill.color
        : layer.text_color;
    return ((uint32_t)eval_channel(layer.text_color_a, (authored >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.text_color_r, (authored >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.text_color_g, (authored >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.text_color_b, authored & 0xFF, t);
}

static uint32_t eval_fill_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.fill_color_a, (layer.fill_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.fill_color_r, (layer.fill_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.fill_color_g, (layer.fill_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.fill_color_b, layer.fill_color & 0xFF, t);
}

static uint32_t eval_stroke_color(const Layer &layer, double t)
{
    return ((uint32_t)eval_channel(layer.stroke_color_a, (layer.stroke_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(layer.stroke_color_r, (layer.stroke_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(layer.stroke_color_g, (layer.stroke_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(layer.stroke_color_b, layer.stroke_color & 0xFF, t);
}

static QColor gradient_color_with_opacity(uint32_t argb, double gradient_opacity, double stop_opacity)
{
    QColor color = color_from_argb(argb);
    color.setAlphaF(std::clamp((double)color.alphaF() * gradient_opacity * stop_opacity, 0.0, 1.0));
    return color;
}


static void apply_extra_gradient_stops(QGradient &gradient, const std::vector<GradientStop> &stops, double gradient_opacity)
{
    for (const auto &stop : stops) {
        const double pos = std::clamp((double)stop.position, 0.0, 1.0);
        gradient.setColorAt(pos, gradient_color_with_opacity(stop.color, gradient_opacity, stop.opacity));
    }
}

static void apply_base_gradient_stops(QGradient &gradient,
                                      double start_pos, uint32_t start_color, double start_opacity,
                                      double end_pos, uint32_t end_color, double end_opacity,
                                      const std::vector<GradientStop> &extra_stops,
                                      double opacity)
{
    gradient.setColorAt(start_pos, gradient_color_with_opacity(start_color, opacity, start_opacity));
    gradient.setColorAt(end_pos, gradient_color_with_opacity(end_color, opacity, end_opacity));
    apply_extra_gradient_stops(gradient, extra_stops, opacity);
}

static int normalized_gradient_spread(int gradient_spread, int gradient_type)
{
    if (gradient_spread == 1 || gradient_spread == 2)
        return gradient_spread;
    return std::clamp(gradient_type, 0, 4) == 3 ? 1 : 0;
}

static QGradient::Spread qt_gradient_spread(int gradient_spread, int gradient_type)
{
    switch (normalized_gradient_spread(gradient_spread, gradient_type)) {
    case 1: return QGradient::ReflectSpread;
    case 2: return QGradient::RepeatSpread;
    case 0:
    default: return QGradient::PadSpread;
    }
}

static QBrush make_gradient_brush(int gradient_type,
                                  int gradient_spread,
                                  const QRectF &box,
                                  double opacity,
                                  double center_x, double center_y,
                                  double focal_x, double focal_y,
                                  double scale, double angle_degrees,
                                  double start_pos, uint32_t start_color, double start_opacity,
                                  double end_pos, uint32_t end_color, double end_opacity,
                                  const std::vector<GradientStop> &extra_stops)
{
    const int type = normalized_gradient_type(gradient_type);
    const QGradient::Spread spread = qt_gradient_spread(gradient_spread, gradient_type);
    const double cx = box.left() + center_x * box.width();
    const double cy = box.top() + center_y * box.height();
    const double safe_scale = std::clamp(scale, 0.01, 100.0);
    const double length = std::max(1.0, std::hypot(box.width(), box.height()) * 0.5 * safe_scale);
    const double angle = angle_degrees * std::acos(-1.0) / 180.0;
    const double dx = std::cos(angle) * length;
    const double dy = std::sin(angle) * length;

    if (type == 1) {
        const double radius = std::max(box.width(), box.height()) * 0.5 * safe_scale;
        QRadialGradient gradient(QPointF(cx, cy), std::max(1.0, radius),
                                 QPointF(box.left() + focal_x * box.width(),
                                         box.top() + focal_y * box.height()));
        gradient.setSpread(spread);
        apply_base_gradient_stops(gradient, start_pos, start_color, start_opacity,
                                  end_pos, end_color, end_opacity, extra_stops, opacity);
        return QBrush(gradient);
    }

    if (type == 2) {
        QConicalGradient gradient(QPointF(cx, cy), -angle_degrees);
        apply_base_gradient_stops(gradient, start_pos, start_color, start_opacity,
                                  end_pos, end_color, end_opacity, extra_stops, opacity);
        return QBrush(gradient);
    }

    QLinearGradient gradient(QPointF(cx - dx, cy - dy), QPointF(cx + dx, cy + dy));
    gradient.setSpread(spread);
    apply_base_gradient_stops(gradient, start_pos, start_color, start_opacity,
                              end_pos, end_color, end_opacity, extra_stops, opacity);
    return QBrush(gradient);
}

static QBrush gradient_fill_brush(const Layer &layer, const QRectF &box, double layer_opacity = 1.0)
{
    return make_gradient_brush(layer.gradient_type, layer.gradient_spread, box,
                               std::clamp((double)layer.gradient_opacity * layer_opacity, 0.0, 1.0),
                               layer.gradient_center_x, layer.gradient_center_y,
                               layer.gradient_focal_x, layer.gradient_focal_y,
                               std::clamp((double)layer.gradient_scale, 0.01, 100.0),
                               layer.gradient_angle,
                               std::clamp((double)layer.gradient_start_pos, 0.0, 1.0),
                               layer.gradient_start_color, layer.gradient_start_opacity,
                               std::clamp((double)layer.gradient_end_pos, 0.0, 1.0),
                               layer.gradient_end_color, layer.gradient_end_opacity,
                               layer.gradient_stops);
}

static const LayerEffect *find_layer_effect(const Layer &layer, LayerEffectType type);

static QBrush background_gradient_fill_brush(const Layer &layer, const QRectF &box, double layer_opacity = 1.0)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    if (!effect) return QBrush(Qt::NoBrush);
    return make_gradient_brush(effect->effect_gradient_type, effect->effect_gradient_spread, box,
                               std::clamp((double)effect->effect_gradient_opacity * layer_opacity, 0.0, 1.0),
                               effect->effect_gradient_center_x, effect->effect_gradient_center_y,
                               effect->effect_gradient_focal_x, effect->effect_gradient_focal_y,
                               std::clamp((double)effect->effect_gradient_scale, 0.01, 100.0),
                               effect->effect_gradient_angle,
                               std::clamp((double)effect->effect_gradient_start_pos, 0.0, 1.0),
                               effect->effect_gradient_start_color, effect->effect_gradient_start_opacity,
                               std::clamp((double)effect->effect_gradient_end_pos, 0.0, 1.0),
                               effect->effect_gradient_end_color, effect->effect_gradient_end_opacity,
                               {});
}

static QString effect_type_name(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::BackgroundColor: return fxm_tr("OBSTitles.BackgroundColor");
    case LayerEffectType::Outline: return fxm_tr("OBSTitles.Outline");
    case LayerEffectType::DropShadow: return fxm_tr("OBSTitles.DropShadow");
    case LayerEffectType::LongShadow: return fxm_tr("OBSTitles.LongShadow");
    case LayerEffectType::BrightnessContrast: return fxm_tr("OBSTitles.BrightnessContrast");
    case LayerEffectType::Saturation: return fxm_tr("OBSTitles.Saturation");
    case LayerEffectType::ColorOverlay: return fxm_tr("OBSTitles.ColorOverlay");
    case LayerEffectType::Glow: return fxm_tr("OBSTitles.Glow");
    case LayerEffectType::InnerGlow: return fxm_tr("OBSTitles.InnerGlow");
    case LayerEffectType::InnerShadow: return fxm_tr("OBSTitles.InnerShadow");
    case LayerEffectType::Blur: return fxm_tr("OBSTitles.Blur");
    case LayerEffectType::MotionBlur: return fxm_tr("OBSTitles.MotionBlur");
    case LayerEffectType::Bloom: return fxm_tr("OBSTitles.Bloom");
    case LayerEffectType::Emboss: return fxm_tr("OBSTitles.Emboss");
    case LayerEffectType::LensFlare: return fxm_tr("OBSTitles.LensFlare");
    case LayerEffectType::Vignette: return fxm_tr("OBSTitles.Vignette");
    case LayerEffectType::Noise: return fxm_tr("OBSTitles.Noise");
    case LayerEffectType::Grain: return QStringLiteral("Grain");
    case LayerEffectType::FilmDistortion: return QStringLiteral("Film Distortion");
    case LayerEffectType::AnalogDistortion: return QStringLiteral("Analog Distortion");
    case LayerEffectType::DigitalDistortion: return QStringLiteral("Digital Distortion");
    case LayerEffectType::TrimPaths: return QStringLiteral("Trim Paths");
    case LayerEffectType::RoughenEdges: return fxm_tr("OBSTitles.RoughenEdges");
    case LayerEffectType::FourColorGradient: return QStringLiteral("4-Color Gradient");
    case LayerEffectType::Sharpen: return QStringLiteral("Sharpen");
    case LayerEffectType::UnsharpMask: return QStringLiteral("Unsharp Mask");
    case LayerEffectType::HighPass: return QStringLiteral("High Pass");
    case LayerEffectType::Clarity: return QStringLiteral("Clarity / Local Contrast");
    case LayerEffectType::BilateralSharpen: return QStringLiteral("Bilateral Sharpen");
    case LayerEffectType::Glare: return QStringLiteral("Real Glare");
    case LayerEffectType::Halation: return QStringLiteral("Halation");
    case LayerEffectType::LensDistortion: return QStringLiteral("Lens Distortion");
    case LayerEffectType::ChromaticAberration: return QStringLiteral("Chromatic Aberration");
    case LayerEffectType::DirectionalBlur: return QStringLiteral("Directional Blur");
    case LayerEffectType::ZoomBlur: return QStringLiteral("Zoom Blur");
    case LayerEffectType::RadialBlur: return QStringLiteral("Radial Blur");
    case LayerEffectType::Ripple: return QStringLiteral("Ripple");
    case LayerEffectType::WaveWarp: return QStringLiteral("Wave Warp");
    case LayerEffectType::Pixelate: return QStringLiteral("Pixelate / Mosaic");
    case LayerEffectType::EdgeDetect: return QStringLiteral("Edge Detect");
    case LayerEffectType::Posterize: return QStringLiteral("Posterize");
    case LayerEffectType::Threshold: return QStringLiteral("Threshold");
    case LayerEffectType::Scanlines: return QStringLiteral("Scanlines");
    case LayerEffectType::ChromaKey: return QStringLiteral("Chroma Key");
    case LayerEffectType::LumaKey: return QStringLiteral("Luma Key");
    case LayerEffectType::ColorRange: return QStringLiteral("Color Range");
    case LayerEffectType::SpillSuppression: return QStringLiteral("Spill Suppression");
    case LayerEffectType::MatteChoker: return QStringLiteral("Matte Choker");
    case LayerEffectType::LightWrap: return QStringLiteral("Light Wrap");
    case LayerEffectType::DisplacementMap: return QStringLiteral("Displacement Map");
    }
    return fxm_tr("OBSTitles.Effect");
}

static void add_shadow_blur_items(QComboBox *combo)
{
    if (!combo) return;
    combo->addItem(fxm_tr("OBSTitles.BoxBlur"), (int)ShadowBlurType::Box);
    combo->addItem(fxm_tr("OBSTitles.GaussianBlur"), (int)ShadowBlurType::Gaussian);
    combo->addItem(fxm_tr("OBSTitles.StackFastBlur"), (int)ShadowBlurType::StackFast);
    combo->addItem(fxm_tr("OBSTitles.TriangularBlur"), (int)ShadowBlurType::Triangular);
    combo->addItem(fxm_tr("OBSTitles.DualKawaseBlur"), (int)ShadowBlurType::DualKawase);
    combo->addItem(fxm_tr("OBSTitles.AlphaMaskBlur"), (int)ShadowBlurType::AlphaMask);
}

static void add_blend_mode_items(QComboBox *combo)
{
    if (!combo) return;
    combo->addItem(fxm_tr("OBSTitles.BlendModeNormal"), (int)EffectBlendMode::Normal);
    combo->addItem(fxm_tr("OBSTitles.BlendModeMultiply"), (int)EffectBlendMode::Multiply);
    combo->addItem(fxm_tr("OBSTitles.BlendModeAdditive"), (int)EffectBlendMode::Additive);
    combo->addItem(fxm_tr("OBSTitles.BlendModeScreen"), (int)EffectBlendMode::Screen);
    combo->addItem(fxm_tr("OBSTitles.BlendModeOverlay"), (int)EffectBlendMode::Overlay);
    combo->addItem(fxm_tr("OBSTitles.BlendModeColor"), (int)EffectBlendMode::Color);
}

static const LayerEffect *find_layer_effect(const Layer &layer, LayerEffectType type)
{
    for (const auto &effect : layer.effects) {
        if (effect.type == type)
            return &effect;
    }
    return nullptr;
}

static bool eval_effect_enabled(const LayerEffect &effect, double t)
{
    return effect.enabled && (!effect.enabled_prop.is_animated() || effect.enabled_prop.evaluate(t) >= 0.5);
}

static uint32_t eval_effect_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.color_a, (effect.effect_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.color_r, (effect.effect_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.color_g, (effect.effect_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.color_b, effect.effect_color & 0xFF, t);
}

static uint32_t eval_effect_stroke_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.stroke_color_a, (effect.effect_stroke_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.stroke_color_r, (effect.effect_stroke_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.stroke_color_g, (effect.effect_stroke_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.stroke_color_b, effect.effect_stroke_color & 0xFF, t);
}

static bool eval_outline_enabled(const Layer &layer, double t)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return true;
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect && effect->effect_fill_type != 0 && eval_effect_enabled(*effect, t);
}

static uint32_t eval_outline_color(const Layer &layer, double t)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return eval_stroke_color(layer, t);
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect ? eval_effect_color(*effect, t) : 0x00000000;
}

static double eval_outline_width(const Layer &layer, double t)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return std::max(0.0f, layer.stroke_width);
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect && eval_outline_enabled(layer, t)
               ? std::max(0.0, effect->size_prop.is_animated() ? effect->size_prop.evaluate(t) : (double)effect->effect_size)
               : 0.0;
}

static double eval_outline_opacity(const Layer &layer, double t)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return std::clamp((double)layer.outline_opacity, 0.0, 1.0);
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect ? std::clamp(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(t) : (double)effect->effect_opacity, 0.0, 1.0) : 1.0;
}

static bool eval_outline_on_front(const Layer &layer, double)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return layer.outline_on_front;
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect ? effect->effect_on_front : true;
}

static int eval_outline_alignment(const Layer &layer, double)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return std::clamp(layer.outline_alignment, 0, 2);
    return 0;
}

static bool eval_outline_antialias(const Layer &layer, double)
{
    if (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
        return layer.outline_antialias;
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    return effect ? effect->effect_antialias : true;
}

static Qt::PenJoinStyle outline_pen_join_style(const Layer &layer)
{
    const int join_style = (layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
                               ? layer.outline_join_style
                               : 1;
    const auto *effect = find_layer_effect(layer, LayerEffectType::Outline);
    switch (effect && !(layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f)
                ? effect->effect_join_style
                : join_style) {
    case 0: return Qt::MiterJoin;
    case 2: return Qt::BevelJoin;
    case 1:
    default: return Qt::RoundJoin;
    }
}

static bool eval_shadow_enabled(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect && eval_effect_enabled(*effect, t);
}

static double eval_shadow_opacity(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? std::clamp(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(t) : (double)effect->effect_opacity, 0.0, 1.0) : 0.0;
}

static double eval_shadow_distance(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? std::max(0.0, effect->distance_prop.is_animated() ? effect->distance_prop.evaluate(t) : (double)effect->effect_distance) : 0.0;
}

static double eval_shadow_angle(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? (effect->angle_prop.is_animated() ? effect->angle_prop.evaluate(t) : (double)effect->effect_angle) : 0.0;
}

static double eval_shadow_blur(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? std::max(0.0, effect->size_prop.is_animated() ? effect->size_prop.evaluate(t) : (double)effect->effect_size) : 0.0;
}

static double eval_shadow_spread(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? std::max(0.0, effect->spread_prop.is_animated() ? effect->spread_prop.evaluate(t) : (double)effect->effect_spread) : 0.0;
}

static uint32_t eval_shadow_color(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow);
    return effect ? eval_effect_color(*effect, t) : 0x00000000;
}

static QPointF shadow_offset(const Layer &layer, double t)
{
    double radians = eval_shadow_angle(layer, t) * 3.14159265358979323846 / 180.0;
    double distance = eval_shadow_distance(layer, t);
    return QPointF(std::cos(radians) * distance, std::sin(radians) * distance);
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

static void set_channel_statics(Layer &layer, bool text, uint32_t argb)
{
    auto &a = text ? layer.text_color_a : layer.fill_color_a;
    auto &r = text ? layer.text_color_r : layer.fill_color_r;
    auto &g = text ? layer.text_color_g : layer.fill_color_g;
    auto &b = text ? layer.text_color_b : layer.fill_color_b;
    a.static_value = (argb >> 24) & 0xFF;
    r.static_value = (argb >> 16) & 0xFF;
    g.static_value = (argb >> 8) & 0xFF;
    b.static_value = argb & 0xFF;
}

static void apply_easing_preset(Keyframe &keyframe, EasingType easing)
{
    keyframe.easing = easing;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    switch (easing) {
    case EasingType::Bezier:
        keyframe.cx1 = 0.33f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.67f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseIn:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 1.0f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseOut:
        keyframe.cx1 = 0.0f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseInOut:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::Linear:
    case EasingType::Hold:
    default:
        keyframe.cx1 = 0.333f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.667f; keyframe.cy2 = 1.0f;
        break;
    }
}

static void apply_easing_preset(VectorKeyframe &keyframe, EasingType easing)
{
    keyframe.easing = easing;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    switch (easing) {
    case EasingType::Bezier:
        keyframe.cx1 = 0.33f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.67f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseIn:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 1.0f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseOut:
        keyframe.cx1 = 0.0f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseInOut:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::Linear:
    case EasingType::Hold:
    default:
        keyframe.cx1 = 0.333f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.667f; keyframe.cy2 = 1.0f;
        break;
    }
}

static void apply_easing_preset(Vector3Keyframe &keyframe, EasingType easing)
{
    keyframe.easing = easing;
    keyframe.temporal_velocity_explicit = false;
    keyframe.temporal_tangents_linked = true;
    switch (easing) {
    case EasingType::Bezier:
        keyframe.cx1 = 0.33f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.67f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseIn:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 1.0f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseOut:
        keyframe.cx1 = 0.0f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::EaseInOut:
        keyframe.cx1 = 0.42f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.58f; keyframe.cy2 = 1.0f;
        break;
    case EasingType::Linear:
    case EasingType::Hold:
    default:
        keyframe.cx1 = 0.333f; keyframe.cy1 = 0.0f;
        keyframe.cx2 = 0.667f; keyframe.cy2 = 1.0f;
        break;
    }
}

static void add_or_replace_keyframe(AnimatedProperty &prop, double time, double value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.static_value = value;
    for (auto &kf : prop.keyframes) {
        if (std::abs(kf.time - time) <= kEpsilon) {
            kf.time = time;
            kf.value = value;
            return;
        }
    }
    Keyframe kf;
    kf.time = time;
    kf.value = value;
    apply_easing_preset(kf, EasingType::Linear);
    prop.keyframes.push_back(kf);
    std::sort(prop.keyframes.begin(), prop.keyframes.end(),
              [](const Keyframe &a, const Keyframe &b) { return a.time < b.time; });
}

static void add_or_replace_keyframe(AnimatedVec2Property &prop, double time, Vec2Value value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.static_value = value;
    for (auto &kf : prop.keyframes) {
        if (std::abs(kf.time - time) <= kEpsilon) {
            kf.time = time;
            kf.value = value;
            return;
        }
    }
    VectorKeyframe kf;
    kf.time = time;
    kf.value = value;
    apply_easing_preset(kf, EasingType::Linear);
    prop.keyframes.push_back(kf);
    std::sort(prop.keyframes.begin(), prop.keyframes.end(),
              [](const VectorKeyframe &a, const VectorKeyframe &b) { return a.time < b.time; });
}

static void add_or_replace_keyframe(AnimatedVec3Property &prop, double time, Vec3Value value)
{
    constexpr double kEpsilon = 1.0 / 240.0;
    prop.static_value = value;
    for (auto &kf : prop.keyframes) {
        if (std::abs(kf.time - time) <= kEpsilon) {
            kf.time = time;
            kf.value = value;
            return;
        }
    }
    Vector3Keyframe kf;
    kf.time = time;
    kf.value = value;
    apply_easing_preset(kf, EasingType::Linear);
    kf.temporal_mode = TemporalInterpolationMode::Linear;
    prop.keyframes.push_back(kf);
    std::sort(prop.keyframes.begin(), prop.keyframes.end(),
              [](const Vector3Keyframe &a, const Vector3Keyframe &b) { return a.time < b.time; });
    prop.recalculate_rove_times();
}

static void set_animated_value(AnimatedProperty &prop, double time, double value)
{
    if (prop.is_animated())
        add_or_replace_keyframe(prop, time, value);
    else
        prop.static_value = value;
}

static void set_animated_value(AnimatedVec2Property &prop, double time, Vec2Value value)
{
    if (prop.is_animated())
        add_or_replace_keyframe(prop, time, value);
    else
        prop.static_value = value;
}

static void set_animated_value(AnimatedVec3Property &prop, double time, Vec3Value value)
{
    if (prop.is_animated())
        add_or_replace_keyframe(prop, time, value);
    else
        prop.static_value = value;
}

static void set_animated_component(AnimatedVec3Property &prop, double time,
                                   int component, double value)
{
    Vec3Value next = prop.evaluate(time);
    if (component == 0) next.x = value;
    else if (component == 1) next.y = value;
    else next.z = value;
    set_animated_value(prop, time, next);
}

static void set_animated_x(AnimatedVec2Property &prop, double time, double value)
{
    Vec2Value next = prop.evaluate(time);
    next.x = value;
    set_animated_value(prop, time, next);
}
