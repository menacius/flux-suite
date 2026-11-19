#pragma once

#include <QPainter>
#include <QPolygonF>
#include <QToolButton>
#include <algorithm>

namespace fxm::ui {
inline void initialize_caret_button(QToolButton &button)
{
    button.setAutoRaise(true);
    button.setCursor(Qt::PointingHandCursor);
    button.setFocusPolicy(Qt::NoFocus);
    button.setFixedSize(18, 20);
    button.setToolButtonStyle(Qt::ToolButtonIconOnly);
}

inline void draw_caret(QPainter &painter, const QRectF &rect, int state, const QColor &color)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    const QPointF center = rect.center();
    QPolygonF triangle;
    if (state > 0) {
        triangle << QPointF(center.x() - 4.0, center.y() - 2.0)
                 << QPointF(center.x() + 4.0, center.y() - 2.0)
                 << QPointF(center.x(), center.y() + 3.0);
    } else {
        triangle << QPointF(center.x() - 2.0, center.y() - 4.0)
                 << QPointF(center.x() - 2.0, center.y() + 4.0)
                 << QPointF(center.x() + 3.0, center.y());
    }
    painter.drawPolygon(triangle);
    if (state == 1) {
        QColor marker = color;
        marker.setAlpha(std::min(255, color.alpha() + 35));
        painter.setBrush(marker);
        painter.drawEllipse(QPointF(center.x() + 6.0, center.y() + 5.0), 1.45, 1.45);
    }
    painter.restore();
}

inline void paint_caret_button(QToolButton &button, int state)
{
    QPainter painter(&button);
    if (button.underMouse() && button.isEnabled()) {
        QColor hover = button.palette().color(QPalette::Button);
        hover.setAlpha(90);
        painter.fillRect(button.rect().adjusted(1, 1, -1, -1), hover);
    }
    QColor caret = button.palette().color(
        button.isEnabled() ? QPalette::Active : QPalette::Disabled, QPalette::WindowText);
    caret.setAlpha(button.isEnabled() ? 175 : 85);
    draw_caret(painter, button.rect(), state, caret);
}
} // namespace fxm::ui
