static double layer_chain_opacity(const Title &title, const Layer &layer, double title_time, int depth = 0)
{
    if (depth > 64)
        return 1.0;
    const double lt = std::max(0.0, title_time - layer.in_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    double opacity = layer.opacity.evaluate(lt) * transition.opacity;
    if (!layer.parent_id.empty()) {
        if (const Layer *parent = find_layer_by_id(title, layer.parent_id))
            opacity *= layer_chain_opacity(title, *parent, title_time, depth + 1);
    }
    return opacity;
}

/* Resolve opacity relative to the compositor that owns the target layer.
 * A track matte inside a group must include its own opacity (and any nested
 * subgroup opacity), but it must not bake the common container opacity because
 * that container is applied once after its children have been composited.  The
 * old all-the-way-to-root calculation made grouped mattes fade twice, while a
 * Group used as a matte skipped its own opacity entirely. */
static double layer_compositor_opacity(const Title &title, const Layer &layer,
                                       double title_time,
                                       const std::string &compositor_group_id,
                                       int depth = 0)
{
    if (depth > 64)
        return 1.0;
    const double lt = std::max(0.0, title_time - layer.in_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    double opacity = layer.opacity.evaluate(lt) * transition.opacity;
    if (layer.parent_id.empty() || layer.parent_id == compositor_group_id)
        return opacity;
    if (const Layer *parent = find_layer_by_id(title, layer.parent_id))
        opacity *= layer_compositor_opacity(title, *parent, title_time,
                                            compositor_group_id, depth + 1);
    return opacity;
}

/* GPU composition keeps title_time in the main title's clock and resolves
 * every layer independently. This matters at an Asset Layer boundary: the
 * asset's children use the asset clock, while the Asset Layer itself (and any
 * outer parent) still uses its owner's clock. */
static double gpu_layer_chain_opacity(const Title &title, const Layer &layer,
                                      double title_time, int depth = 0)
{
    if (depth > 64)
        return 1.0;
    const double layer_time = gpu_layer_render_time(title, layer, title_time);
    const double lt = std::max(0.0, layer_time - layer.in_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time,
        gpu_layer_transition_time(title, layer, title_time));
    double opacity = layer.opacity.evaluate(lt) * transition.opacity;
    if (!layer.parent_id.empty()) {
        if (const Layer *parent = find_layer_by_id(title, layer.parent_id))
            opacity *= gpu_layer_chain_opacity(
                title, *parent, title_time, depth + 1);
    }
    return opacity;
}

static double gpu_layer_compositor_opacity(
    const Title &title, const Layer &layer, double title_time,
    const std::string &compositor_group_id, int depth = 0)
{
    if (depth > 64)
        return 1.0;
    const double layer_time = gpu_layer_render_time(title, layer, title_time);
    const double lt = std::max(0.0, layer_time - layer.in_time);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time,
        gpu_layer_transition_time(title, layer, title_time));
    double opacity = layer.opacity.evaluate(lt) * transition.opacity;
    if (layer.parent_id.empty() || layer.parent_id == compositor_group_id)
        return opacity;
    if (const Layer *parent = find_layer_by_id(title, layer.parent_id))
        opacity *= gpu_layer_compositor_opacity(
            title, *parent, title_time, compositor_group_id, depth + 1);
    return opacity;
}

static void apply_layer_transition_clip(cairo_t *cr, const Layer &layer, double title_time,
                                        double x, double y, double width, double height)
{
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    if (!transition.active || transition.wipe >= 0.999999 ||
        transition.wipe_softness > 0.000001 || width <= 0.0 || height <= 0.0)
        return;

    double clip_x = x;
    double clip_y = y;
    double clip_w = width;
    double clip_h = height;
    const double reveal = std::clamp(transition.wipe, 0.0, 1.0);
    switch (transition.wipe_direction) {
    case LayerTransitionDirection::Right:
        clip_w = width * reveal;
        clip_x = x + width - clip_w;
        break;
    case LayerTransitionDirection::Up:
        clip_h = height * reveal;
        clip_y = y + height - clip_h;
        break;
    case LayerTransitionDirection::Down:
        clip_h = height * reveal;
        break;
    case LayerTransitionDirection::Left:
    case LayerTransitionDirection::None:
    default:
        clip_w = width * reveal;
        break;
    }
    cairo_rectangle(cr, clip_x, clip_y, std::max(0.0, clip_w), std::max(0.0, clip_h));
    cairo_clip(cr);
}

static void cairo_add_rounded_rect(cairo_t *cr, double x, double y, double w, double h, double r)
{
    r = std::clamp(r, 0.0, std::min(w, h) / 2.0);
    if (r <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + r,     y + r,     r, kPi,       3*kPi/2);
    cairo_arc(cr, x + w - r, y + r,     r, 3*kPi/2,   2*kPi);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,          kPi/2);
    cairo_arc(cr, x + r,     y + h - r, r, kPi/2,     kPi);
    cairo_close_path(cr);
}

static double legacy_corner_type_roundness(CornerType type)
{
    switch (type) {
    case CornerType::Straight:
    case CornerType::Cutout:
        return 0.0;
    case CornerType::Concave:
        return -100.0;
    case CornerType::Round:
    default:
        return 100.0;
    }
}

static void cairo_add_rounded_rect_corners(cairo_t *cr, double x, double y, double w, double h,
                                           double top_left, double top_right,
                                           double bottom_right, double bottom_left,
                                           double bevel_roundness = 100.0)
{
    const double max_radius = std::max(0.0, std::min(w, h) / 2.0);
    const double tl = std::clamp(top_left, 0.0, max_radius);
    const double tr = std::clamp(top_right, 0.0, max_radius);
    const double br = std::clamp(bottom_right, 0.0, max_radius);
    const double bl = std::clamp(bottom_left, 0.0, max_radius);
    if (tl <= 0.0 && tr <= 0.0 && br <= 0.0 && bl <= 0.0) {
        cairo_rectangle(cr, x, y, w, h);
        return;
    }
    const double roundness = std::clamp(bevel_roundness, -100.0, 100.0) / 100.0;
    auto add_corner = [&](double from_x, double from_y, double corner_x, double corner_y, double to_x, double to_y,
                          double cutout_x, double cutout_y, double radius) {
        if (radius <= 0.0) {
            cairo_line_to(cr, corner_x, corner_y);
            return;
        }
        if (std::abs(roundness) <= 1e-6) {
            cairo_line_to(cr, to_x, to_y);
        } else if (roundness < 0.0) {
            const double inverted = -roundness;
            const double flat_c1x = from_x + (to_x - from_x) / 3.0;
            const double flat_c1y = from_y + (to_y - from_y) / 3.0;
            const double flat_c2x = from_x + (to_x - from_x) * (2.0 / 3.0);
            const double flat_c2y = from_y + (to_y - from_y) * (2.0 / 3.0);
            const double arc_c1x = from_x + (cutout_x - to_x) * kCubicCircle;
            const double arc_c1y = from_y + (cutout_y - to_y) * kCubicCircle;
            const double arc_c2x = to_x - (corner_x - from_x) * kCubicCircle;
            const double arc_c2y = to_y - (corner_y - from_y) * kCubicCircle;
            cairo_curve_to(cr,
                           flat_c1x + (arc_c1x - flat_c1x) * inverted,
                           flat_c1y + (arc_c1y - flat_c1y) * inverted,
                           flat_c2x + (arc_c2x - flat_c2x) * inverted,
                           flat_c2y + (arc_c2y - flat_c2y) * inverted,
                           to_x, to_y);
        } else {
            const double c1x = corner_x + (cutout_x - corner_x) * (1.0 - roundness);
            const double c1y = corner_y + (cutout_y - corner_y) * (1.0 - roundness);
            cairo_curve_to(cr, c1x, c1y, c1x, c1y, to_x, to_y);
        }
    };
    cairo_new_sub_path(cr);
    cairo_move_to(cr, x + tl, y);
    cairo_line_to(cr, x + w - tr, y);
    if (tr > 0.0 && roundness >= 0.999)
        cairo_arc(cr, x + w - tr, y + tr, tr, 3 * kPi / 2, 2 * kPi);
    else if (tr > 0.0 && roundness <= -0.999)
        cairo_arc_negative(cr, x + w, y, tr, kPi, kPi / 2);
    else
        add_corner(x + w - tr, y, x + w, y, x + w, y + tr, x + w - tr, y + tr, tr);
    cairo_line_to(cr, x + w, y + h - br);
    if (br > 0.0 && roundness >= 0.999)
        cairo_arc(cr, x + w - br, y + h - br, br, 0, kPi / 2);
    else if (br > 0.0 && roundness <= -0.999)
        cairo_arc_negative(cr, x + w, y + h, br, -kPi / 2, -kPi);
    else
        add_corner(x + w, y + h - br, x + w, y + h, x + w - br, y + h, x + w - br, y + h - br, br);
    cairo_line_to(cr, x + bl, y + h);
    if (bl > 0.0 && roundness >= 0.999)
        cairo_arc(cr, x + bl, y + h - bl, bl, kPi / 2, kPi);
    else if (bl > 0.0 && roundness <= -0.999)
        cairo_arc_negative(cr, x, y + h, bl, 0, -kPi / 2);
    else
        add_corner(x + bl, y + h, x, y + h, x, y + h - bl, x + bl, y + h - bl, bl);
    cairo_line_to(cr, x, y + tl);
    if (tl > 0.0 && roundness >= 0.999)
        cairo_arc(cr, x + tl, y + tl, tl, kPi, 3 * kPi / 2);
    else if (tl > 0.0 && roundness <= -0.999)
        cairo_arc_negative(cr, x, y, tl, kPi / 2, 0);
    else
        add_corner(x, y + tl, x, y, x + tl, y, x + tl, y + tl, tl);
    cairo_close_path(cr);
}

static QPainterPath painter_rounded_rect_corners(const QRectF &rect, double top_left, double top_right,
                                                 double bottom_right, double bottom_left,
                                                 double bevel_roundness = 100.0)
{
    const double max_radius = std::max(0.0, std::min(rect.width(), rect.height()) / 2.0);
    const double tl = std::clamp(top_left, 0.0, max_radius);
    const double tr = std::clamp(top_right, 0.0, max_radius);
    const double br = std::clamp(bottom_right, 0.0, max_radius);
    const double bl = std::clamp(bottom_left, 0.0, max_radius);
    QPainterPath path;
    if (tl <= 0.0 && tr <= 0.0 && br <= 0.0 && bl <= 0.0) {
        path.addRect(rect);
        return path;
    }
    const double roundness = std::clamp(bevel_roundness, -100.0, 100.0) / 100.0;
    auto add_corner = [&](const QPointF &from, const QPointF &corner, const QPointF &to,
                          const QPointF &cutout, double radius) {
        if (radius <= 0.0) {
            path.lineTo(corner);
            return;
        }
        if (std::abs(roundness) <= 1e-6) {
            path.lineTo(to);
        } else if (roundness < 0.0) {
            const double inverted = -roundness;
            const QPointF flat_c1 = from + (to - from) / 3.0;
            const QPointF flat_c2 = from + (to - from) * (2.0 / 3.0);
            const QPointF arc_c1 = from + (cutout - to) * kCubicCircle;
            const QPointF arc_c2 = to - (corner - from) * kCubicCircle;
            path.cubicTo(flat_c1 + (arc_c1 - flat_c1) * inverted,
                         flat_c2 + (arc_c2 - flat_c2) * inverted,
                         to);
        } else {
            const QPointF control = corner + (cutout - corner) * (1.0 - roundness);
            path.cubicTo(control, control, to);
        }
    };
    path.moveTo(rect.left() + tl, rect.top());
    path.lineTo(rect.right() - tr, rect.top());
    if (tr > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.right() - 2.0 * tr, rect.top(), 2.0 * tr, 2.0 * tr), 90.0, -90.0);
    else if (tr > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.right() - tr, rect.top() - tr, 2.0 * tr, 2.0 * tr), 180.0, 90.0);
    else
        add_corner(QPointF(rect.right() - tr, rect.top()), rect.topRight(),
                   QPointF(rect.right(), rect.top() + tr), QPointF(rect.right() - tr, rect.top() + tr), tr);
    path.lineTo(rect.right(), rect.bottom() - br);
    if (br > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.right() - 2.0 * br, rect.bottom() - 2.0 * br, 2.0 * br, 2.0 * br), 0.0, -90.0);
    else if (br > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.right() - br, rect.bottom() - br, 2.0 * br, 2.0 * br), -90.0, 90.0);
    else
        add_corner(QPointF(rect.right(), rect.bottom() - br), rect.bottomRight(),
                   QPointF(rect.right() - br, rect.bottom()), QPointF(rect.right() - br, rect.bottom() - br), br);
    path.lineTo(rect.left() + bl, rect.bottom());
    if (bl > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.left(), rect.bottom() - 2.0 * bl, 2.0 * bl, 2.0 * bl), 270.0, -90.0);
    else if (bl > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.left() - bl, rect.bottom() - bl, 2.0 * bl, 2.0 * bl), 0.0, 90.0);
    else
        add_corner(QPointF(rect.left() + bl, rect.bottom()), rect.bottomLeft(),
                   QPointF(rect.left(), rect.bottom() - bl), QPointF(rect.left() + bl, rect.bottom() - bl), bl);
    path.lineTo(rect.left(), rect.top() + tl);
    if (tl > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.left(), rect.top(), 2.0 * tl, 2.0 * tl), 180.0, -90.0);
    else if (tl > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.left() - tl, rect.top() - tl, 2.0 * tl, 2.0 * tl), 90.0, 90.0);
    else
        add_corner(QPointF(rect.left(), rect.top() + tl), rect.topLeft(),
                   QPointF(rect.left() + tl, rect.top()), QPointF(rect.left() + tl, rect.top() + tl), tl);
    path.closeSubpath();
    return path;
}

static QPainterPath painter_layer_rounded_rect_path(const Layer &layer, const QRectF &rect)
{
    return painter_rounded_rect_corners(rect, layer.corner_radius_tl, layer.corner_radius_tr,
                                        layer.corner_radius_br, layer.corner_radius_bl,
                                        layer.corner_bevel_roundness);
}

static void cairo_add_regular_polygon(cairo_t *cr, double cx, double cy, double rx, double ry,
                                      int count, double start_angle)
{
    count = std::max(3, count);
    for (int i = 0; i < count; ++i) {
        const double a = start_angle + 2.0 * kPi * i / count;
        const double x = cx + std::cos(a) * rx;
        const double y = cy + std::sin(a) * ry;
        if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
}

static void cairo_add_star(cairo_t *cr, double cx, double cy, double rx, double ry,
                           int points, double inner_radius, double outer_radius)
{
    points = std::clamp(points, 3, 64);
    inner_radius = std::clamp(inner_radius, 0.0, 1.0);
    outer_radius = std::clamp(outer_radius, 0.0, 1.0);
    if (outer_radius <= 0.0) outer_radius = 0.5;
    for (int i = 0; i < points * 2; ++i) {
        const bool outer = (i % 2) == 0;
        const double factor = outer ? outer_radius * 2.0 : inner_radius * 2.0;
        const double a = -kPi / 2.0 + kPi * i / points;
        const double x = cx + std::cos(a) * rx * factor;
        const double y = cy + std::sin(a) * ry * factor;
        if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
}


static QPainterPath painter_layer_shape_path(const Layer &layer, double w, double h)
{
    return fxm::layer_shape_path(layer, QRectF(0.0, 0.0, w, h));
}

static void cairo_append_qpainter_path(cairo_t *cr, const QPainterPath &path,
                                         bool close_subpaths)
{
    if (!cr || path.isEmpty())
        return;
    bool have_subpath = false;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            if (have_subpath && close_subpaths)
                cairo_close_path(cr);
            cairo_move_to(cr, element.x, element.y);
            have_subpath = true;
            break;
        case QPainterPath::LineToElement:
            cairo_line_to(cr, element.x, element.y);
            break;
        case QPainterPath::CurveToElement:
            if (i + 2 < path.elementCount()) {
                const QPainterPath::Element control2 = path.elementAt(i + 1);
                const QPainterPath::Element endpoint = path.elementAt(i + 2);
                cairo_curve_to(cr, element.x, element.y,
                               control2.x, control2.y,
                               endpoint.x, endpoint.y);
                i += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            /* Curve data is consumed together with CurveToElement above. */
            break;
        }
    }
    if (have_subpath && close_subpaths)
        cairo_close_path(cr);
}

static void cairo_add_layer_shape(cairo_t *cr, const Layer &layer, double w, double h)
{
    const QPainterPath path = painter_layer_shape_path(layer, w, h);
    const ShapeType shape_type = layer.type == LayerType::Shape
        ? layer.shape_type
        : ShapeType::RoundedRectangle;
    const bool closed = shape_type != ShapeType::Line &&
                        (shape_type != ShapeType::Path || layer.path_closed);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_append_qpainter_path(cr, path, closed);
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
    QColor color((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
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

static int normalized_gradient_type(int gradient_type)
{
    switch (std::clamp(gradient_type, 0, 4)) {
    case 1: return 1;
    case 2: return 2;
    case 4: return 1;
    case 0:
    case 3:
    default: return 0;
    }
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

static cairo_extend_t cairo_gradient_extend(int gradient_spread, int gradient_type)
{
    switch (normalized_gradient_spread(gradient_spread, gradient_type)) {
    case 1: return CAIRO_EXTEND_REFLECT;
    case 2: return CAIRO_EXTEND_REPEAT;
    case 0:
    default: return CAIRO_EXTEND_PAD;
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
    const double angle = angle_degrees * kPi / 180.0;
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
                               layer.gradient_scale, layer.gradient_angle,
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
                               effect->effect_gradient_scale, effect->effect_gradient_angle,
                               std::clamp((double)effect->effect_gradient_start_pos, 0.0, 1.0),
                               effect->effect_gradient_start_color, effect->effect_gradient_start_opacity,
                               std::clamp((double)effect->effect_gradient_end_pos, 0.0, 1.0),
                               effect->effect_gradient_end_color, effect->effect_gradient_end_opacity,
                               {});
}

static QBrush stroke_gradient_fill_brush(const Layer &layer, const QRectF &box, double layer_opacity = 1.0)
{
    return make_gradient_brush(layer.stroke_gradient_type, layer.stroke_gradient_spread, box,
                               std::clamp((double)layer.stroke_gradient_opacity * layer_opacity, 0.0, 1.0),
                               layer.stroke_gradient_center_x, layer.stroke_gradient_center_y,
                               layer.stroke_gradient_focal_x, layer.stroke_gradient_focal_y,
                               layer.stroke_gradient_scale, layer.stroke_gradient_angle,
                               std::clamp((double)layer.stroke_gradient_start_pos, 0.0, 1.0),
                               layer.stroke_gradient_start_color, layer.stroke_gradient_start_opacity,
                               std::clamp((double)layer.stroke_gradient_end_pos, 0.0, 1.0),
                               layer.stroke_gradient_end_color, layer.stroke_gradient_end_opacity,
                               layer.stroke_gradient_stops);
}

static cairo_pattern_t *create_fill_gradient_pattern(const Layer &layer, double x, double y, double w, double h, double layer_alpha)
{
    const double opacity = std::clamp((double)layer.gradient_opacity * layer_alpha, 0.0, 1.0);
    const double cx = x + (double)layer.gradient_center_x * w;
    const double cy = y + (double)layer.gradient_center_y * h;
    const double scale = std::clamp((double)layer.gradient_scale, 0.01, 100.0);
    const double start_pos = std::clamp((double)layer.gradient_start_pos, 0.0, 1.0);
    const double end_pos = std::clamp((double)layer.gradient_end_pos, 0.0, 1.0);
    cairo_pattern_t *pattern = nullptr;
    const int type = normalized_gradient_type(layer.gradient_type);
    if (type == 1) {
        const double radius = std::max(w, h) * 0.5 * scale;
        const double fx = x + (double)layer.gradient_focal_x * w;
        const double fy = y + (double)layer.gradient_focal_y * h;
        pattern = cairo_pattern_create_radial(fx, fy, 0.0, cx, cy, std::max(1.0, radius));
    } else {
        const double length = std::hypot(w, h) * 0.5 * scale;
        const double angle = layer.gradient_angle * kPi / 180.0;
        const double dx = std::cos(angle) * length;
        const double dy = std::sin(angle) * length;
        pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy);
    }
    auto add_stop = [&](double pos, uint32_t argb, double stop_opacity) {
        QColor color = gradient_color_with_opacity(argb, opacity, stop_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, pos, color.redF(), color.greenF(), color.blueF(), color.alphaF());
    };
    add_stop(start_pos, layer.gradient_start_color, layer.gradient_start_opacity);
    add_stop(end_pos, layer.gradient_end_color, layer.gradient_end_opacity);
    for (const auto &stop : layer.gradient_stops)
        add_stop(stop.position, stop.color, stop.opacity);
    cairo_pattern_set_extend(pattern, cairo_gradient_extend(layer.gradient_spread, layer.gradient_type));
    return pattern;
}

static cairo_pattern_t *create_stroke_gradient_pattern(const Layer &layer, double x, double y, double w, double h, double layer_alpha)
{
    const double opacity = std::clamp((double)layer.stroke_gradient_opacity * layer_alpha, 0.0, 1.0);
    const double cx = x + (double)layer.stroke_gradient_center_x * w;
    const double cy = y + (double)layer.stroke_gradient_center_y * h;
    const double scale = std::clamp((double)layer.stroke_gradient_scale, 0.01, 100.0);
    const double start_pos = std::clamp((double)layer.stroke_gradient_start_pos, 0.0, 1.0);
    const double end_pos = std::clamp((double)layer.stroke_gradient_end_pos, 0.0, 1.0);
    cairo_pattern_t *pattern = nullptr;
    const int type = normalized_gradient_type(layer.stroke_gradient_type);
    if (type == 1) {
        const double radius = std::max(w, h) * 0.5 * scale;
        const double fx = x + (double)layer.stroke_gradient_focal_x * w;
        const double fy = y + (double)layer.stroke_gradient_focal_y * h;
        pattern = cairo_pattern_create_radial(fx, fy, 0.0, cx, cy, std::max(1.0, radius));
    } else {
        const double length = std::hypot(w, h) * 0.5 * scale;
        const double angle = layer.stroke_gradient_angle * kPi / 180.0;
        const double dx = std::cos(angle) * length;
        const double dy = std::sin(angle) * length;
        pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy);
    }
    auto add_stop = [&](double pos, uint32_t argb, double stop_opacity) {
        QColor color = gradient_color_with_opacity(argb, opacity, stop_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, pos, color.redF(), color.greenF(), color.blueF(), color.alphaF());
    };
    add_stop(start_pos, layer.stroke_gradient_start_color, layer.stroke_gradient_start_opacity);
    add_stop(end_pos, layer.stroke_gradient_end_color, layer.stroke_gradient_end_opacity);
    for (const auto &stop : layer.stroke_gradient_stops)
        add_stop(stop.position, stop.color, stop.opacity);
    cairo_pattern_set_extend(pattern, cairo_gradient_extend(layer.stroke_gradient_spread, layer.stroke_gradient_type));
    return pattern;
}

static cairo_pattern_t *create_background_gradient_pattern_for_effect(const LayerEffect &effect, double x, double y, double w, double h, double layer_alpha);

static cairo_pattern_t *create_background_gradient_pattern(const Layer &layer, double x, double y, double w, double h, double layer_alpha)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::BackgroundColor);
    if (!effect) return nullptr;
    return create_background_gradient_pattern_for_effect(*effect, x, y, w, h, layer_alpha);
}

static cairo_pattern_t *create_background_gradient_pattern_for_effect(const LayerEffect &effect, double x, double y, double w, double h, double layer_alpha)
{
    const double opacity = std::clamp((double)effect.effect_gradient_opacity * layer_alpha, 0.0, 1.0);
    const double cx = x + (double)effect.effect_gradient_center_x * w;
    const double cy = y + (double)effect.effect_gradient_center_y * h;
    const double scale = std::clamp((double)effect.effect_gradient_scale, 0.01, 100.0);
    const double start_pos = std::clamp((double)effect.effect_gradient_start_pos, 0.0, 1.0);
    const double end_pos = std::clamp((double)effect.effect_gradient_end_pos, 0.0, 1.0);
    cairo_pattern_t *pattern = nullptr;
    const int type = normalized_gradient_type(effect.effect_gradient_type);
    if (type == 1) {
        const double radius = std::max(w, h) * 0.5 * scale;
        const double fx = x + (double)effect.effect_gradient_focal_x * w;
        const double fy = y + (double)effect.effect_gradient_focal_y * h;
        pattern = cairo_pattern_create_radial(fx, fy, 0.0, cx, cy, std::max(1.0, radius));
    } else {
        const double length = std::hypot(w, h) * 0.5 * scale;
        const double angle = effect.effect_gradient_angle * kPi / 180.0;
        const double dx = std::cos(angle) * length;
        const double dy = std::sin(angle) * length;
        pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy);
    }
    auto add_stop = [&](double pos, uint32_t argb, double stop_opacity) {
        QColor color = gradient_color_with_opacity(argb, opacity, stop_opacity);
        cairo_pattern_add_color_stop_rgba(pattern, pos, color.redF(), color.greenF(), color.blueF(), color.alphaF());
    };
    add_stop(start_pos, effect.effect_gradient_start_color, effect.effect_gradient_start_opacity);
    add_stop(end_pos, effect.effect_gradient_end_color, effect.effect_gradient_end_opacity);
    cairo_pattern_set_extend(pattern, cairo_gradient_extend(effect.effect_gradient_spread, effect.effect_gradient_type));
    return pattern;
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

static double eval_layer_stroke_offset(const Layer &layer, double local_time)
{
    const double value = layer.stroke_offset_prop.is_animated()
        ? layer.stroke_offset_prop.evaluate(local_time)
        : (double)layer.stroke_offset;
    return std::clamp(std::isfinite(value) ? value : 0.0, -16384.0, 16384.0);
}

static bool layer_has_stroke_geometry_modifiers(const Layer &layer,
                                                double local_time)
{
    // Keep animated offsets on the geometry-aware path even when the current
    // sample happens to be zero.  Otherwise the renderer can switch between
    // the primitive fast path and the compatibility path at zero crossings,
    // leaving stale cached stroke geometry for the following frame.
    if (layer.stroke_offset_prop.is_animated() ||
        std::abs(eval_layer_stroke_offset(layer, local_time)) > 1.0e-7)
        return true;
    for (const LayerEffect &effect : layer.effects) {
        if (effect.type == LayerEffectType::TrimPaths &&
            resolve_layer_effect(effect, local_time).enabled)
            return true;
    }
    return false;
}

static std::vector<QPainterPath> apply_layer_trim_paths_partitioned(
    const Layer &layer, double local_time,
    const std::vector<QPainterPath> &sources)
{
    std::vector<QPainterPath> results =
        fxm::apply_stroke_offset_geometry_partitioned(
            sources, eval_layer_stroke_offset(layer, local_time));
    for (const LayerEffect &effect : layer.effects) {
        if (effect.type != LayerEffectType::TrimPaths)
            continue;
        const ResolvedLayerEffect resolved = resolve_layer_effect(effect, local_time);
        if (!resolved.enabled)
            continue;
        fxm::TrimPathsGeometryOptions options;
        options.start_percent = resolved.effect_trim_start;
        options.end_percent = resolved.effect_trim_end;
        options.trim_offset_degrees = resolved.effect_trim_offset;
        options.individually = resolved.effect_trim_multiple_shapes != 0;
        results = fxm::apply_trim_paths_geometry_partitioned(results, options);
        const bool all_empty = std::all_of(
            results.begin(), results.end(),
            [](const QPainterPath &path) { return path.isEmpty(); });
        if (all_empty)
            break;
    }
    return results;
}

static QPainterPath apply_layer_trim_paths(const Layer &layer, double local_time,
                                            const QPainterPath &source)
{
    const std::vector<QPainterPath> results =
        apply_layer_trim_paths_partitioned(layer, local_time, {source});
    return results.empty() ? QPainterPath{} : results.front();
}

static uint32_t eval_effect_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.color_a, (effect.effect_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.color_r, (effect.effect_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.color_g, (effect.effect_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.color_b, effect.effect_color & 0xFF, t);
}

static uint32_t eval_effect_secondary_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.secondary_color_a, (effect.effect_secondary_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.secondary_color_r, (effect.effect_secondary_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.secondary_color_g, (effect.effect_secondary_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.secondary_color_b, effect.effect_secondary_color & 0xFF, t);
}

static uint32_t eval_effect_stroke_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.stroke_color_a, (effect.effect_stroke_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.stroke_color_r, (effect.effect_stroke_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.stroke_color_g, (effect.effect_stroke_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.stroke_color_b, effect.effect_stroke_color & 0xFF, t);
}

static uint32_t eval_effect_gradient_start_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.gradient_start_color_a, (effect.effect_gradient_start_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.gradient_start_color_r, (effect.effect_gradient_start_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.gradient_start_color_g, (effect.effect_gradient_start_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.gradient_start_color_b, effect.effect_gradient_start_color & 0xFF, t);
}

static uint32_t eval_effect_gradient_end_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.gradient_end_color_a, (effect.effect_gradient_end_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.gradient_end_color_r, (effect.effect_gradient_end_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.gradient_end_color_g, (effect.effect_gradient_end_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.gradient_end_color_b, effect.effect_gradient_end_color & 0xFF, t);
}

static bool eval_outline_enabled(const Layer &layer, double)
{
    return layer.outline_enabled && layer.stroke_fill_type != 0 && layer.stroke_width > 0.0f;
}

static uint32_t eval_outline_color(const Layer &layer, double t)
{
    return eval_outline_enabled(layer, t) ? eval_stroke_color(layer, t) : 0x00000000;
}

static double eval_outline_width(const Layer &layer, double)
{
    return eval_outline_enabled(layer, 0.0) ? std::max(0.0f, layer.stroke_width) : 0.0;
}

static double eval_outline_opacity(const Layer &layer, double)
{
    return eval_outline_enabled(layer, 0.0) ? std::clamp((double)layer.outline_opacity, 0.0, 1.0) : 1.0;
}

static bool eval_outline_on_front(const Layer &layer, double)
{
    return layer.outline_on_front;
}

static int eval_outline_alignment(const Layer &layer, double)
{
    return std::clamp(layer.outline_alignment, 0, 2);
}

static bool eval_outline_antialias(const Layer &layer, double)
{
    return layer.outline_antialias;
}

static cairo_antialias_t outline_cairo_antialias(const Layer &layer)
{
    return layer.outline_antialias ? CAIRO_ANTIALIAS_DEFAULT : CAIRO_ANTIALIAS_NONE;
}

static cairo_line_join_t outline_cairo_join_style(const Layer &layer)
{
    switch (layer.outline_join_style) {
    case 0: return CAIRO_LINE_JOIN_MITER;
    case 2: return CAIRO_LINE_JOIN_BEVEL;
    case 1:
    default: return CAIRO_LINE_JOIN_ROUND;
    }
}

static Qt::PenJoinStyle outline_pen_join_style(const Layer &layer)
{
    switch (layer.outline_join_style) {
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

static bool eval_long_shadow_enabled(const Layer &layer, double t)
{
    const auto *effect = find_layer_effect(layer, LayerEffectType::LongShadow);
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
    double radians = eval_shadow_angle(layer, t) * kPi / 180.0;
    double distance = eval_shadow_distance(layer, t);
    return QPointF(std::cos(radians) * distance,
                   std::sin(radians) * distance);
}

static QColor color_from_argb(uint32_t argb);

struct ShadowRenderParams {
    double dx = 0.0;
    double dy = 0.0;
    double blur = 0.0;
    double spread = 0.0;
    int blur_type = (int)ShadowBlurType::StackFast;
    bool drop_enabled = false;
    uint32_t color = 0x99000000;
    double opacity = 1.0;
    bool long_enabled = false;
    uint32_t long_color = 0x99000000;
    double long_opacity = 0.0;
    double long_length = 0.0;
    double long_angle = 0.0;
    double long_falloff = 1.0;
    int long_blur_type = (int)LongShadowBlurType::None;
    double long_blur = 0.0;
};

struct CachedShadowImage {
    QImage image;
    QPointF origin;
};

struct CachedShadowEntry {
    CachedShadowImage image;
    uint64_t last_used = 0;
};

static QString image_content_hash(const QImage &image)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(reinterpret_cast<const char *>(image.constBits()), image.bytesPerLine() * image.height());
    return QString::fromLatin1(hash.result().toHex());
}

static QString shadow_param_key(const Layer &layer, const ShadowRenderParams &p, const QString &shape_key)
{
    return QStringLiteral("%1|%2|%3|%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18")
        .arg(QString::fromStdString(layer.id), shape_key)
        .arg(p.dx, 0, 'f', 2).arg(p.dy, 0, 'f', 2).arg(p.blur, 0, 'f', 2)
        .arg(p.spread, 0, 'f', 2).arg(p.blur_type).arg(p.drop_enabled ? 1 : 0)
        .arg(p.color).arg(p.opacity, 0, 'f', 3)
        .arg(p.long_enabled ? 1 : 0).arg(p.long_color).arg(p.long_opacity, 0, 'f', 3)
        .arg(p.long_length, 0, 'f', 2).arg(p.long_angle, 0, 'f', 2).arg(p.long_falloff, 0, 'f', 3)
        .arg(p.long_blur_type).arg(p.long_blur, 0, 'f', 2);
}

static ShadowRenderParams evaluated_shadow_params(const Layer &layer, double t)
{
    ShadowRenderParams p;
    p.drop_enabled = eval_shadow_enabled(layer, t);
    if (p.drop_enabled) {
        QPointF off = shadow_offset(layer, t);
        p.dx = off.x();
        p.dy = off.y();
        p.blur = eval_shadow_blur(layer, t);
        p.spread = eval_shadow_spread(layer, t);
        if (const auto *effect = find_layer_effect(layer, LayerEffectType::DropShadow))
            p.blur_type = effect->effect_blur_type;
        p.color = eval_shadow_color(layer, t);
        p.opacity = eval_shadow_opacity(layer, t) * (((p.color >> 24) & 0xFF) / 255.0);
    }
    const auto *long_effect = find_layer_effect(layer, LayerEffectType::LongShadow);
    const double long_length = long_effect ? std::max(0.0, long_effect->distance_prop.is_animated() ? long_effect->distance_prop.evaluate(t) : (double)long_effect->effect_distance) : 0.0;
    const double long_opacity = long_effect ? std::clamp(long_effect->opacity_prop.is_animated() ? long_effect->opacity_prop.evaluate(t) : (double)long_effect->effect_opacity, 0.0, 1.0) : 0.0;
    p.long_enabled = long_effect && eval_effect_enabled(*long_effect, t) && long_length > 0.0 && long_opacity > 0.0;
    if (p.long_enabled) {
        p.long_color = eval_effect_color(*long_effect, t);
        p.long_opacity = long_opacity * (((p.long_color >> 24) & 0xFF) / 255.0);
        p.long_length = long_length;
        p.long_angle = long_effect->angle_prop.is_animated() ? long_effect->angle_prop.evaluate(t) : (double)long_effect->effect_angle;
        p.long_falloff = std::clamp(long_effect->falloff_prop.is_animated() ? long_effect->falloff_prop.evaluate(t) : (double)long_effect->effect_falloff, 0.0, 8.0);
        p.long_blur_type = long_effect->effect_blur_type;
        p.long_blur = p.long_blur_type == (int)LongShadowBlurType::None
                          ? 0.0
                          : std::max(0.0, long_effect->size_prop.is_animated() ? long_effect->size_prop.evaluate(t) : (double)long_effect->effect_size);
    }
    return p;
}

static void box_blur_alpha(std::vector<uint8_t> &alpha, int w, int h, int radius)
{
    if (radius <= 0 || w <= 0 || h <= 0) return;
    std::vector<uint8_t> tmp(alpha.size());
    const int window = radius * 2 + 1;
    for (int y = 0; y < h; ++y) {
        int sum = 0;
        for (int x = -radius; x <= radius; ++x)
            sum += alpha[y * w + std::clamp(x, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            tmp[y * w + x] = static_cast<uint8_t>(sum / window);
            sum -= alpha[y * w + std::clamp(x - radius, 0, w - 1)];
            sum += alpha[y * w + std::clamp(x + radius + 1, 0, w - 1)];
        }
    }
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -radius; y <= radius; ++y)
            sum += tmp[std::clamp(y, 0, h - 1) * w + x];
        for (int y = 0; y < h; ++y) {
            alpha[y * w + x] = static_cast<uint8_t>(sum / window);
            sum -= tmp[std::clamp(y - radius, 0, h - 1) * w + x];
            sum += tmp[std::clamp(y + radius + 1, 0, h - 1) * w + x];
        }
    }
}

static std::vector<int> blur_pass_radii(double blur, int blur_type)
{
    const int radius = (int)std::round(std::max(0.0, blur));
    if (radius <= 0)
        return {};

    switch ((ShadowBlurType)std::clamp(blur_type, 0, (int)ShadowBlurType::DualKawase)) {
    case ShadowBlurType::Box:
        return {radius};
    case ShadowBlurType::Gaussian: {
        const int r = std::max(1, (int)std::round(radius * 0.58));
        return {r, r, r};
    }
    case ShadowBlurType::Triangular: {
        const int r = std::max(1, (int)std::round(radius * 0.5));
        return {r, r};
    }
    case ShadowBlurType::DualKawase:
        return {std::max(1, radius / 3), std::max(1, radius / 4), std::max(1, radius / 5)};
    case ShadowBlurType::AlphaMask:
        return {std::max(1, radius / 2)};
    case ShadowBlurType::StackFast:
    default:
        return {std::max(1, radius / 2), std::max(1, radius / 3)};
    }
}

static int blur_support_radius_for_type(double blur, int blur_type)
{
    int support = 0;
    for (int radius : blur_pass_radii(blur, blur_type))
        support += std::max(0, radius);
    return support;
}

/* Shared premultiplied-color blur backend. Text animators and the ordinary
 * Blur effect must use this exact helper so their radius response and blur
 * type never drift into separate implementations again. */
static void blur_premultiplied_pixels_for_type(
    std::vector<uint8_t> &pixels, int w, int h, double blur, int blur_type)
{
    for (int radius : blur_pass_radii(blur, blur_type))
        box_blur_pixels(pixels, w, h, radius);
}

static void blur_alpha_for_type(std::vector<uint8_t> &alpha, int w, int h, double blur, int blur_type)
{
    for (int radius : blur_pass_radii(blur, blur_type))
        box_blur_alpha(alpha, w, h, radius);
}

static int shadow_blur_type_for_long_shadow(int blur_type)
{
    switch ((LongShadowBlurType)std::clamp(blur_type, 0, (int)LongShadowBlurType::StackFast)) {
    case LongShadowBlurType::Box: return (int)ShadowBlurType::Box;
    case LongShadowBlurType::Gaussian: return (int)ShadowBlurType::Gaussian;
    case LongShadowBlurType::StackFast: return (int)ShadowBlurType::StackFast;
    case LongShadowBlurType::None:
    default: return -1;
    }
}

static void composite_mask_alpha(std::vector<uint8_t> &dst, int dw, int dh, const QImage &mask,
                                 int ox, int oy, double opacity)
{
    if (opacity <= 0.0) return;
    const QImage src = mask.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < src.height(); ++y) {
        int dy = y + oy;
        if (dy < 0 || dy >= dh) continue;
        const QRgb *line = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            int dx = x + ox;
            if (dx < 0 || dx >= dw) continue;
            int a = (int)std::round(qAlpha(line[x]) * opacity);
            if (a <= 0) continue;
            uint8_t &d = dst[dy * dw + dx];
            d = (uint8_t)std::min(255, (int)d + a - ((int)d * a) / 255);
        }
    }
}

static CachedShadowImage build_shadow_image(const Layer &layer, const ShadowRenderParams &p,
                                            const QString &shape_key, const QImage &mask)
{
    static std::mutex cache_mutex;
    static std::unordered_map<std::string, CachedShadowEntry> cache;
    static uint64_t cache_tick = 0;
    static qsizetype cache_bytes = 0;
    const QString key = shadow_param_key(layer, p, shape_key);
    const std::string skey = key.toStdString();
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto it = cache.find(skey);
        if (it != cache.end()) {
            it->second.last_used = ++cache_tick;
            return it->second.image;
        }
    }

    const double long_rad = p.long_angle * kPi / 180.0;
    const double long_dx = p.long_enabled ? std::cos(long_rad) * p.long_length : 0.0;
    const double long_dy = p.long_enabled ? std::sin(long_rad) * p.long_length : 0.0;
    const int blur_pad = (int)std::ceil(std::max(0.0, p.blur) * 3.0 + p.spread + 2.0);
    const double min_x = std::min({0.0, p.dx, long_dx}) - blur_pad;
    const double min_y = std::min({0.0, p.dy, long_dy}) - blur_pad;
    const double max_x = std::max({0.0, p.dx, long_dx}) + mask.width() + blur_pad;
    const double max_y = std::max({0.0, p.dy, long_dy}) + mask.height() + blur_pad;
    const int w = std::max(1, (int)std::ceil(max_x - min_x));
    const int h = std::max(1, (int)std::ceil(max_y - min_y));
    std::vector<uint8_t> drop_alpha((size_t)w * (size_t)h, 0);
    std::vector<uint8_t> long_alpha((size_t)w * (size_t)h, 0);

    auto add_alpha = [&](std::vector<uint8_t> &target, double opacity, double x, double y) {
        composite_mask_alpha(target, w, h, mask,
                             (int)std::round(x - min_x), (int)std::round(y - min_y), opacity);
        if (p.spread > 0.0) {
            const int s = (int)std::ceil(p.spread);
            for (int oy : {-s, 0, s})
                for (int ox : {-s, 0, s})
                    if (ox || oy) composite_mask_alpha(target, w, h, mask,
                        (int)std::round(x + ox - min_x), (int)std::round(y + oy - min_y), opacity * 0.55);
        }
    };

    if (p.long_enabled) {
        /* Sharp long shadows are a cheap directional extrusion by default.
         * Use a coarse, adaptive number of stamps so editing length/angle remains responsive;
         * optional long-shadow blur is applied only when explicitly selected.
         */
        const int steps = std::clamp((int)std::ceil(p.long_length / 16.0), 1, 32);
        for (int i = steps; i >= 1; --i) {
            const double u = (double)i / steps;
            const double fade = std::pow(1.0 - u, p.long_falloff);
            add_alpha(long_alpha, p.long_opacity * fade, long_dx * u, long_dy * u);
        }
        const int mapped_long_blur = shadow_blur_type_for_long_shadow(p.long_blur_type);
        if (mapped_long_blur >= 0 && p.long_blur > 0.0)
            blur_alpha_for_type(long_alpha, w, h, p.long_blur, mapped_long_blur);
    }
    if (p.drop_enabled && p.opacity > 0.0) {
        add_alpha(drop_alpha, p.opacity, p.dx, p.dy);
        blur_alpha_for_type(drop_alpha, w, h, p.blur, p.blur_type);
    }

    QColor dc = color_from_argb(p.color);
    QColor lc = color_from_argb(p.long_color);
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            int la = long_alpha[y * w + x];
            int da = drop_alpha[y * w + x];
            int out_a = la + da - (la * da) / 255;
            if (out_a <= 0) { line[x] = qRgba(0, 0, 0, 0); continue; }
            int pr = (lc.red() * la + dc.red() * da * (255 - la) / 255) / 255;
            int pg = (lc.green() * la + dc.green() * da * (255 - la) / 255) / 255;
            int pb = (lc.blue() * la + dc.blue() * da * (255 - la) / 255) / 255;
            pr = std::clamp(pr, 0, 255);
            pg = std::clamp(pg, 0, 255);
            pb = std::clamp(pb, 0, 255);
            line[x] = qRgba(pr, pg, pb, out_a);
        }
    }
    CachedShadowImage out{image, QPointF(min_x, min_y)};
    constexpr qsizetype kMaxShadowCacheBytes = 128 * 1024 * 1024;
    // A single pathological shadow surface must not become a permanent static
    // allocation. It can still be returned for the current frame, but is not
    // retained in the process-wide cache.
    if (out.image.sizeInBytes() > kMaxShadowCacheBytes)
        return out;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (auto existing = cache.find(skey); existing != cache.end()) {
            cache_bytes -= existing->second.image.image.sizeInBytes();
            cache.erase(existing);
        }
        cache.emplace(skey, CachedShadowEntry{out, ++cache_tick});
        cache_bytes += out.image.sizeInBytes();

        constexpr size_t kMaxShadowCacheEntries = 128;
        while ((cache.size() > kMaxShadowCacheEntries ||
                cache_bytes > kMaxShadowCacheBytes) &&
               cache.size() > 1) {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->first == skey)
                    continue;
                if (oldest == cache.end() || it->second.last_used < oldest->second.last_used)
                    oldest = it;
            }
            if (oldest == cache.end())
                break;
            cache_bytes -= oldest->second.image.image.sizeInBytes();
            cache.erase(oldest);
        }
    }
    return out;
}

static void paint_qimage(cairo_t *cr, const QImage &image, double x, double y, double alpha)
{
    if (!cr || image.isNull() || image.width() <= 0 || image.height() <= 0 || alpha <= 0.0) return;
    auto surface = make_image_surface_for_const_qimage(image);
    if (!surface) return;
    cairo_set_source_surface(cr, surface.get(), x, y);
    cairo_paint_with_alpha(cr, std::clamp(alpha, 0.0, 1.0));
}

/* ══════════════════════════════════════════════════════════════════
 *  Cairo rendering
 * ══════════════════════════════════════════════════════════════════ */


static QLocale locale_for_text_transform(const QString &text)
{
    QLocale locale;
    for (const QChar ch : text) {
        uint u = ch.unicode();
        if (u >= 0x0370 && u <= 0x03FF)
            return QLocale(QLocale::Greek, QLocale::Greece);
        if (QStringLiteral("ıİşŞğĞçÇ").contains(ch))
            return QLocale(QLocale::Turkish, QLocale::Turkey);
        if (ch == QChar(0x00DF))
            return QLocale(QLocale::German, QLocale::Germany);
    }
    return locale;
}


static QString php_date_format(const QString &format, const QDateTime &date_time)
{
    QString out;
    const QDate date = date_time.date();
    const QTime time = date_time.time();
    for (int i = 0; i < format.size(); ++i) {
        const QChar token = format.at(i);
        if (token == QLatin1Char('\\') && i + 1 < format.size()) {
            out.append(format.at(++i));
            continue;
        }
        switch (token.unicode()) {
        case 'd': out += QString("%1").arg(date.day(), 2, 10, QChar('0')); break;
        case 'D': out += date_time.toString("ddd"); break;
        case 'j': out += QString::number(date.day()); break;
        case 'l': out += date_time.toString("dddd"); break;
        case 'F': out += date_time.toString("MMMM"); break;
        case 'm': out += QString("%1").arg(date.month(), 2, 10, QChar('0')); break;
        case 'M': out += date_time.toString("MMM"); break;
        case 'n': out += QString::number(date.month()); break;
        case 'Y': out += QString::number(date.year()); break;
        case 'y': out += QString("%1").arg(date.year() % 100, 2, 10, QChar('0')); break;
        case 'a': out += (time.hour() < 12 ? "am" : "pm"); break;
        case 'A': out += (time.hour() < 12 ? "AM" : "PM"); break;
        case 'g': { int h = time.hour() % 12; out += QString::number(h == 0 ? 12 : h); break; }
        case 'G': out += QString::number(time.hour()); break;
        case 'h': { int h = time.hour() % 12; out += QString("%1").arg(h == 0 ? 12 : h, 2, 10, QChar('0')); break; }
        case 'H': out += QString("%1").arg(time.hour(), 2, 10, QChar('0')); break;
        case 'i': out += QString("%1").arg(time.minute(), 2, 10, QChar('0')); break;
        case 's': out += QString("%1").arg(time.second(), 2, 10, QChar('0')); break;
        case 'U': out += QString::number(date_time.toSecsSinceEpoch()); break;
        default: out.append(token); break;
        }
    }
    return out;
}

static QString clock_text_for_layer(const Layer &layer)
{
    QString format = QString::fromStdString(layer.clock_format);
    if (format.isEmpty()) format = QStringLiteral("H:i:s");
    return php_date_format(format, QDateTime::currentDateTime());
}

static QString display_text_for_style(const Layer &layer)
{
    QString text = layer.type == LayerType::Clock
        ? clock_text_for_layer(layer)
        : QString::fromStdString(effective_external_string(
              layer, ExternalPropertyPaths::TextContent, layer.text_content));
    if (layer.text_style == 1)
        return locale_for_text_transform(text).toUpper(text);
    return text;
}

static void apply_text_style_to_font(QFont &font, const Layer &layer)
{
    if (layer.text_style == 2)
        font.setCapitalization(QFont::SmallCaps);
    if (layer.text_style == 3 || layer.text_style == 4)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * 0.65)));
}

static QFont font_for_layer(const Layer &layer, double t)
{
    const QString family = QString::fromStdString(layer.font_family);
    const QString style = QString::fromStdString(layer.font_style);
    QFontDatabase fdb;
    QFont font = !style.isEmpty()
        ? fdb.font(family, style, (int)std::round(eval_text_font_size(layer, t)))
        : QFont(family);
    font.setFamily(family);
    font.setPixelSize((int)std::round(eval_text_font_size(layer, t)));
    if (!style.isEmpty())
        font.setStyleName(style);
    font.setBold(layer.font_bold);
    font.setItalic(layer.font_italic);
    font.setUnderline(layer.text_underline);
    font.setStrikeOut(layer.text_strikethrough);
    font.setKerning(layer.kerning_mode != 2 && layer.font_kerning);
    const float effective_tracking = (float)eval_char_tracking(layer, t) + (layer.kerning_mode == 2 ? layer.manual_kerning : 0.0f);
    font.setLetterSpacing(QFont::AbsoluteSpacing, effective_tracking);
    font.setStretch(std::clamp((int)std::round(eval_char_scale_x(layer, t) * 100.0), 1, 4000));
    apply_text_style_to_font(font, layer);
    return font;
}
