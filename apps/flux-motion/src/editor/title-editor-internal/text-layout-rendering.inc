static QColor layer_type_color(LayerType type)
{
    switch (type) {
    case LayerType::Text:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::TextLayer);
    case LayerType::Clock:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ClockLayer);
    case LayerType::Ticker:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::TickerLayer);
    case LayerType::Chart:
        return QColor(QStringLiteral("#38BDF8"));
    case LayerType::SolidRect:
    case LayerType::Shape:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ObjectLayer);
    case LayerType::Image:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ImageLayer);
    case LayerType::Video:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::VideoLayer);
    case LayerType::Audio:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::AudioLayer);
    case LayerType::Empty:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::EmptyLayer);
    case LayerType::Adjustment:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::AdjustmentLayer);
    case LayerType::ColorSolid:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ColorSolidLayer);
    case LayerType::Light:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::LightLayer);
    case LayerType::Group:
    case LayerType::Asset:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::GroupLayer);
    case LayerType::TransitionInput:
        return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ImageLayer);
    }
    return TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::ObjectLayer);
}

static QColor layer_color(const Layer &layer, int /*row*/)
{
    if (layer.custom_ui_color_enabled) {
        QColor custom = QColor::fromRgba(
            static_cast<QRgb>(layer.custom_ui_color));
        custom.setAlpha(255);
        if (custom.isValid())
            return custom;
    }
    return layer_type_color(layer.type);
}

static const char *layer_type_icon_name(LayerType type)
{
    switch (type) {
    case LayerType::Text: return "layer-type-text.svg";
    case LayerType::Clock: return "layer-type-clock.svg";
    case LayerType::Ticker: return "layer-type-ticker.svg";
    case LayerType::Chart: return "layer-type-chart.svg";
    case LayerType::SolidRect:
    case LayerType::Shape: return "layer-type-shape.svg";
    case LayerType::Image: return "layer-type-image.svg";
    case LayerType::Video: return "layer-type-video.svg";
    case LayerType::Audio: return "layer-type-audio.svg";
    case LayerType::Empty: return "layer-type-empty.svg";
    case LayerType::Adjustment: return "layer-type-adjustment.svg";
    case LayerType::ColorSolid: return "layer-type-color-solid.svg";
    case LayerType::Light: return "layer-type-light.svg";
    case LayerType::Asset: return "asset-title.svg";
    case LayerType::Group:
    case LayerType::TransitionInput:
        return nullptr;
    }
    return nullptr;
}

static QString layer_type_short(LayerType type)
{
    switch (type) {
    case LayerType::Group: return "G";
    case LayerType::Asset: return "A";
    case LayerType::TransitionInput: return "A/B";
    default: return QString();
    }
}

static QIcon obs_icon(const char *file_name)
{
    return fxm_icon(file_name);
}

static QIcon layer_type_icon(LayerType type, const QColor &foreground)
{
    const char *file_name = layer_type_icon_name(type);
    if (!file_name)
        return QIcon();

    static QHash<QString, QIcon> cache;
    const QString key = QStringLiteral("%1|%2")
        .arg(QString::fromUtf8(file_name))
        .arg(static_cast<qulonglong>(foreground.rgba()), 8, 16, QLatin1Char('0'));
    const auto found = cache.constFind(key);
    if (found != cache.constEnd())
        return found.value();

    const QIcon icon = fxm_icon(file_name, foreground);
    cache.insert(key, icon);
    return icon;
}

static QIcon layer_type_icon(LayerType type)
{
    return layer_type_icon(type, fxm_icon_color());
}

constexpr double kToolIconPi = 3.14159265358979323846;

static QString shape_display_name(ShapeType shape_type)
{
    switch (shape_type) {
    case ShapeType::RoundedRectangle: return fxm_tr("OBSTitles.RoundedRectangle");
    case ShapeType::Ellipse: return fxm_tr("OBSTitles.Ellipse");
    case ShapeType::Triangle: return fxm_tr("OBSTitles.Triangle");
    case ShapeType::Star: return fxm_tr("OBSTitles.Star");
    case ShapeType::Polygon: return fxm_tr("OBSTitles.Polygon");
    case ShapeType::Diamond: return fxm_tr("OBSTitles.Diamond");
    case ShapeType::Line: return fxm_tr("OBSTitles.Line");
    case ShapeType::Path: return fxm_tr("OBSTitles.Path");
    case ShapeType::Rectangle:
    default: return fxm_tr("OBSTitles.Rectangle");
    }
}

static QPainterPath tool_shape_path(ShapeType shape_type, const QRectF &rect)
{
    QPainterPath path;
    switch (shape_type) {
    case ShapeType::RoundedRectangle:
        path.addRoundedRect(rect, 3.0, 3.0);
        break;
    case ShapeType::Ellipse:
        path.addEllipse(rect);
        break;
    case ShapeType::Triangle: {
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.bottom());
        path.lineTo(rect.left(), rect.bottom());
        path.closeSubpath();
        break;
    }
    case ShapeType::Star: {
        const QPointF c = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2 == 0) ? 1.0 : 0.45;
            const double a = -kToolIconPi / 2.0 + kToolIconPi * i / 5.0;
            const QPointF pt(c.x() + std::cos(a) * rx * r, c.y() + std::sin(a) * ry * r);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Polygon: {
        const QPointF c = rect.center();
        const double rx = rect.width() / 2.0;
        const double ry = rect.height() / 2.0;
        for (int i = 0; i < 6; ++i) {
            const double a = -kToolIconPi / 2.0 + 2.0 * kToolIconPi * i / 6.0;
            const QPointF pt(c.x() + std::cos(a) * rx, c.y() + std::sin(a) * ry);
            if (i == 0) path.moveTo(pt); else path.lineTo(pt);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Diamond:
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.center().y());
        path.lineTo(rect.center().x(), rect.bottom());
        path.lineTo(rect.left(), rect.center().y());
        path.closeSubpath();
        break;
    case ShapeType::Line:
        path.moveTo(rect.left(), rect.center().y());
        path.lineTo(rect.right(), rect.center().y());
        break;
    case ShapeType::Path:
        path.moveTo(rect.left(), rect.bottom());
        path.cubicTo(rect.left() + rect.width() * 0.25, rect.top(),
                     rect.left() + rect.width() * 0.75, rect.bottom(),
                     rect.right(), rect.top());
        break;
    case ShapeType::Rectangle:
    default:
        path.addRect(rect);
        break;
    }
    return path;
}

static QPainterPath editor_layer_rounded_rect_path(const Layer &layer, const QRectF &rect);

static QPainterPath editor_scene_mask_shape_path(const Layer &layer, const QRectF &rect)
{
    return fxm::layer_shape_path(layer, rect);
}

static QPainterPath editor_corner_rect_path(const QRectF &rect, double top_left, double top_right,
                                            double bottom_right, double bottom_left, double bevel_roundness)
{
    constexpr double kCubicCircle = 0.5522847498307933984;
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
    auto add_corner = [&](const QPointF &corner, const QPointF &from, const QPointF &to,
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
        add_corner(rect.topRight(), QPointF(rect.right() - tr, rect.top()), QPointF(rect.right(), rect.top() + tr),
                   QPointF(rect.right() - tr, rect.top() + tr), tr);
    path.lineTo(rect.right(), rect.bottom() - br);
    if (br > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.right() - 2.0 * br, rect.bottom() - 2.0 * br, 2.0 * br, 2.0 * br), 0.0, -90.0);
    else if (br > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.right() - br, rect.bottom() - br, 2.0 * br, 2.0 * br), -90.0, 90.0);
    else
        add_corner(rect.bottomRight(), QPointF(rect.right(), rect.bottom() - br), QPointF(rect.right() - br, rect.bottom()),
                   QPointF(rect.right() - br, rect.bottom() - br), br);
    path.lineTo(rect.left() + bl, rect.bottom());
    if (bl > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.left(), rect.bottom() - 2.0 * bl, 2.0 * bl, 2.0 * bl), 270.0, -90.0);
    else if (bl > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.left() - bl, rect.bottom() - bl, 2.0 * bl, 2.0 * bl), 0.0, 90.0);
    else
        add_corner(rect.bottomLeft(), QPointF(rect.left() + bl, rect.bottom()), QPointF(rect.left(), rect.bottom() - bl),
                   QPointF(rect.left() + bl, rect.bottom() - bl), bl);
    path.lineTo(rect.left(), rect.top() + tl);
    if (tl > 0.0 && roundness >= 0.999)
        path.arcTo(QRectF(rect.left(), rect.top(), 2.0 * tl, 2.0 * tl), 180.0, -90.0);
    else if (tl > 0.0 && roundness <= -0.999)
        path.arcTo(QRectF(rect.left() - tl, rect.top() - tl, 2.0 * tl, 2.0 * tl), 90.0, 90.0);
    else
        add_corner(rect.topLeft(), QPointF(rect.left(), rect.top() + tl), QPointF(rect.left() + tl, rect.top()),
                   QPointF(rect.left() + tl, rect.top() + tl), tl);
    path.closeSubpath();
    return path;
}

static QPainterPath editor_layer_rounded_rect_path(const Layer &layer, const QRectF &rect)
{
    return editor_corner_rect_path(rect, layer.corner_radius_tl, layer.corner_radius_tr,
                                   layer.corner_radius_br, layer.corner_radius_bl, layer.corner_bevel_roundness);
}

static void set_layer_corner_radii(Layer &layer, float top_left, float top_right,
                                   float bottom_right, float bottom_left)
{
    layer.corner_radius_tl = std::max(0.0f, top_left);
    layer.corner_radius_tr = std::max(0.0f, top_right);
    layer.corner_radius_br = std::max(0.0f, bottom_right);
    layer.corner_radius_bl = std::max(0.0f, bottom_left);
    layer.corner_radius = layer.corner_radius_tl;
    if (layer.corner_radius_tl == layer.corner_radius_tr &&
        layer.corner_radius_tl == layer.corner_radius_br &&
        layer.corner_radius_tl == layer.corner_radius_bl)
        layer.shape_roundness = layer.corner_radius_tl;
}

static void set_layer_all_corner_radii(Layer &layer, float radius)
{
    set_layer_corner_radii(layer, radius, radius, radius, radius);
}

static QBrush scene_mask_hatch_brush()
{
    QPixmap pattern(12, 12);
    pattern.fill(Qt::transparent);
    QPainter painter(&pattern);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(255, 0, 200, 180), 2.0));
    painter.drawLine(QPointF(-2.0, 12.0), QPointF(12.0, -2.0));
    painter.drawLine(QPointF(4.0, 14.0), QPointF(14.0, 4.0));
    painter.end();
    return QBrush(pattern);
}

static QIcon shape_tool_icon(ShapeType shape_type)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 220, 220), shape_type == ShapeType::Line ? 2.4 : 1.8));
    painter.setBrush(shape_type == ShapeType::Line ? Qt::NoBrush : QBrush(QColor(120, 120, 120, 60)));
    painter.drawPath(tool_shape_path(shape_type, QRectF(5, 5, 14, 14)));
    return QIcon(pixmap);
}

static QIcon cursor_tool_icon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.moveTo(6, 4);
    path.lineTo(6, 19);
    path.lineTo(10, 15);
    path.lineTo(13, 21);
    path.lineTo(16, 19);
    path.lineTo(13, 13);
    path.lineTo(18, 13);
    path.closeSubpath();
    painter.setPen(QPen(QColor(230, 230, 230), 1.4));
    painter.setBrush(QColor(65, 65, 65));
    painter.drawPath(path);
    return QIcon(pixmap);
}

static QIcon direct_selection_tool_icon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.moveTo(6, 4);
    path.lineTo(6, 19);
    path.lineTo(10, 15);
    path.lineTo(13, 21);
    path.lineTo(16, 19);
    path.lineTo(13, 13);
    path.lineTo(18, 13);
    path.closeSubpath();
    painter.setPen(QPen(QColor(230, 230, 230), 1.4));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.setBrush(QColor(230, 230, 230));
    painter.drawRect(QRectF(3.5, 3.5, 4.0, 4.0));
    return QIcon(pixmap);
}

static QIcon pen_tool_icon()
{
    return obs_icon("pen-nib.svg");
}

static QIcon gradient_tool_icon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QRectF r(4, 5, 16, 14);
    QLinearGradient g(r.topLeft(), r.topRight());
    g.setColorAt(0.0, QColor(255, 80, 80));
    g.setColorAt(0.5, QColor(255, 230, 80));
    g.setColorAt(1.0, QColor(80, 160, 255));
    painter.setBrush(g);
    painter.setPen(QPen(QColor(230, 230, 230), 1.2));
    painter.drawRoundedRect(r, 2, 2);
    painter.setPen(QPen(QColor(230, 230, 230), 1.5));
    painter.drawLine(QPointF(5, 20), QPointF(19, 20));
    painter.setBrush(QColor(230, 230, 230));
    painter.drawEllipse(QPointF(5, 20), 2.0, 2.0);
    painter.drawEllipse(QPointF(19, 20), 2.0, 2.0);
    return QIcon(pixmap);
}

static QString text_tool_display_name(LayerType type)
{
    if (type == LayerType::Clock) return fxm_tr("OBSTitles.Clock");
    if (type == LayerType::Ticker) return fxm_tr("OBSTitles.Ticker");
    return fxm_tr("OBSTitles.Text");
}

static QIcon text_tool_icon(LayerType type)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(230, 230, 230), 1.7));
    painter.setBrush(Qt::NoBrush);

    if (type == LayerType::Clock) {
        painter.drawEllipse(QRectF(5.0, 5.0, 14.0, 14.0));
        painter.drawLine(QPointF(12.0, 12.0), QPointF(12.0, 7.5));
        painter.drawLine(QPointF(12.0, 12.0), QPointF(15.8, 14.2));
    } else if (type == LayerType::Ticker) {
        painter.setPen(QPen(QColor(230, 230, 230), 1.6));
        painter.drawText(QRectF(4.0, 2.0, 16.0, 12.0), Qt::AlignCenter, QStringLiteral("T"));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(20.0, 15.0));
        painter.drawLine(QPointF(7.0, 19.0), QPointF(20.0, 19.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 12.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 18.0));
    } else {
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(18);
        painter.setFont(font);
        painter.drawText(QRectF(3.0, 2.0, 18.0, 20.0), Qt::AlignCenter, QStringLiteral("T"));
    }
    return QIcon(pixmap);
}

static std::string editor_text_std(const char *key)
{
    return fxm_tr(key).toStdString();
}

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

static QFont font_for_layer(const Layer &layer, double t = 0.0)
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
    font.setKerning(layer.font_kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing, eval_char_tracking(layer, t));
    font.setStretch(std::clamp((int)std::round(eval_char_scale_x(layer, t) * 100.0), 1, 4000));
    apply_text_style_to_font(font, layer);
    return font;
}

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
                                     const Layer &layer, const std::string &title_id, double local_time)
{
    struct TickerLayoutCache { QString key; QPainterPath path; QRectF bounds; };
    static thread_local TickerLayoutCache layout_cache;
    QPainterPath path;
    QFontMetricsF metrics(font);
    const double speed = std::max(1.0, layer.ticker_speed);
    const TickerRuntimeSnapshot ticker_state =
        fxm::ticker_runtime::sample(title_id, layer, false);
    const double now = ticker_state.time_seconds;
    const bool custom_playback = layer.ticker_playback_mode ==
        static_cast<int>(TickerPlaybackMode::CustomPlayback);
    const double completion = std::clamp(
        layer.ticker_completion_prop.is_animated()
            ? layer.ticker_completion_prop.evaluate(local_time)
            : layer.ticker_completion,
        0.0, 100.0) / 100.0;
