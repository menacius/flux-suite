#include "editor-tool-icons.h"

#include "title-assets.h"
#include "title-localization.h"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>

#include <cmath>

namespace fxm::editor::tool_icons {
namespace {

constexpr double kToolIconPi = 3.14159265358979323846;

QPainterPath tool_shape_path(ShapeType shape_type, const QRectF &rect)
{
    QPainterPath path;
    switch (shape_type) {
    case ShapeType::RoundedRectangle:
        path.addRoundedRect(rect, 3.0, 3.0);
        break;
    case ShapeType::Ellipse:
        path.addEllipse(rect);
        break;
    case ShapeType::Triangle:
        path.moveTo(rect.center().x(), rect.top());
        path.lineTo(rect.right(), rect.bottom());
        path.lineTo(rect.left(), rect.bottom());
        path.closeSubpath();
        break;
    case ShapeType::Star: {
        const QPointF center = rect.center();
        const double radius_x = rect.width() / 2.0;
        const double radius_y = rect.height() / 2.0;
        for (int index = 0; index < 10; ++index) {
            const double radius = index % 2 == 0 ? 1.0 : 0.45;
            const double angle = -kToolIconPi / 2.0 +
                                 kToolIconPi * index / 5.0;
            const QPointF point(center.x() + std::cos(angle) * radius_x * radius,
                                center.y() + std::sin(angle) * radius_y * radius);
            if (index == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        path.closeSubpath();
        break;
    }
    case ShapeType::Polygon: {
        const QPointF center = rect.center();
        const double radius_x = rect.width() / 2.0;
        const double radius_y = rect.height() / 2.0;
        for (int index = 0; index < 6; ++index) {
            const double angle = -kToolIconPi / 2.0 +
                                 2.0 * kToolIconPi * index / 6.0;
            const QPointF point(center.x() + std::cos(angle) * radius_x,
                                center.y() + std::sin(angle) * radius_y);
            if (index == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
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

} // namespace

QString shape_display_name(ShapeType shape_type)
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

QIcon shape_tool_icon(ShapeType shape_type)
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(220, 220, 220),
                        shape_type == ShapeType::Line ? 2.4 : 1.8));
    painter.setBrush(shape_type == ShapeType::Line
                         ? Qt::NoBrush
                         : QBrush(QColor(120, 120, 120, 60)));
    painter.drawPath(tool_shape_path(shape_type, QRectF(5, 5, 14, 14)));
    return QIcon(pixmap);
}

QIcon cursor_tool_icon()
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

QIcon direct_selection_tool_icon()
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

QIcon pen_tool_icon()
{
    return fxm_icon("pen-nib.svg");
}

QIcon gradient_tool_icon()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rect(4, 5, 16, 14);
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, QColor(255, 80, 80));
    gradient.setColorAt(0.5, QColor(255, 230, 80));
    gradient.setColorAt(1.0, QColor(80, 160, 255));
    painter.setBrush(gradient);
    painter.setPen(QPen(QColor(230, 230, 230), 1.2));
    painter.drawRoundedRect(rect, 2, 2);
    painter.setPen(QPen(QColor(230, 230, 230), 1.5));
    painter.drawLine(QPointF(5, 20), QPointF(19, 20));
    painter.setBrush(QColor(230, 230, 230));
    painter.drawEllipse(QPointF(5, 20), 2.0, 2.0);
    painter.drawEllipse(QPointF(19, 20), 2.0, 2.0);
    return QIcon(pixmap);
}

QString text_tool_display_name(LayerType type)
{
    if (type == LayerType::Clock)
        return fxm_tr("OBSTitles.Clock");
    if (type == LayerType::Ticker)
        return fxm_tr("OBSTitles.Ticker");
    return fxm_tr("OBSTitles.Text");
}

QIcon text_tool_icon(LayerType type)
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
        painter.drawText(QRectF(4.0, 2.0, 16.0, 12.0), Qt::AlignCenter,
                         QStringLiteral("T"));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(20.0, 15.0));
        painter.drawLine(QPointF(7.0, 19.0), QPointF(20.0, 19.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 12.0));
        painter.drawLine(QPointF(4.0, 15.0), QPointF(7.0, 18.0));
    } else {
        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(18);
        painter.setFont(font);
        painter.drawText(QRectF(3.0, 2.0, 18.0, 20.0), Qt::AlignCenter,
                         QStringLiteral("T"));
    }
    return QIcon(pixmap);
}

} // namespace fxm::editor::tool_icons
