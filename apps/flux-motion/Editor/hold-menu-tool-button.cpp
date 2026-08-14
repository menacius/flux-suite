#include "hold-menu-tool-button.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPolygon>
#include <QScreen>

#include <algorithm>

HoldMenuToolButton::HoldMenuToolButton(QWidget *parent)
    : QToolButton(parent)
{
    hold_timer_.setSingleShot(true);
    hold_timer_.setInterval(250);
    QObject::connect(&hold_timer_, &QTimer::timeout, this, [this]() {
        if (!menu() || !isDown() || !underMouse())
            return;
        menu_opened_by_hold_ = true;
        popup_menu_beside_button();
    });
}

void HoldMenuToolButton::mousePressEvent(QMouseEvent *event)
{
    menu_opened_by_hold_ = false;
    if (event->button() == Qt::LeftButton && menu())
        hold_timer_.start();
    QToolButton::mousePressEvent(event);
}

void HoldMenuToolButton::mouseReleaseEvent(QMouseEvent *event)
{
    hold_timer_.stop();
    if (menu_opened_by_hold_) {
        setDown(false);
        event->accept();
        return;
    }
    QToolButton::mouseReleaseEvent(event);
}

void HoldMenuToolButton::leaveEvent(QEvent *event)
{
    if (!isDown())
        hold_timer_.stop();
    QToolButton::leaveEvent(event);
}

void HoldMenuToolButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (!menu())
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    QColor marker = palette().color(QPalette::ButtonText);
    marker.setAlpha(190);
    painter.setPen(Qt::NoPen);
    painter.setBrush(marker);
    const int inset = 3;
    const int size = 4;
    const QPoint corner(width() - inset, height() - inset);
    QPolygon triangle;
    triangle << QPoint(corner.x() - size, corner.y())
             << QPoint(corner.x(), corner.y())
             << QPoint(corner.x(), corner.y() - size);
    painter.drawPolygon(triangle);
}

void HoldMenuToolButton::popup_menu_beside_button()
{
    QMenu *popup = menu();
    if (!popup)
        return;

    popup->ensurePolished();
    const QSize popup_size = popup->sizeHint();
    const QPoint global_top_left = mapToGlobal(QPoint(0, 0));
    const QPoint global_center = mapToGlobal(rect().center());
    QScreen *screen = QGuiApplication::screenAt(global_center);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const QRect available = screen ? screen->availableGeometry()
                                   : QRect(0, 0, 1920, 1080);

    const int gap = 2;
    const int right_x = global_top_left.x() + width() + gap;
    const int left_x = global_top_left.x() - popup_size.width() - gap;
    const int right_space =
        available.right() - (global_top_left.x() + width());
    const int left_space = global_top_left.x() - available.left();

    bool open_left = global_center.x() >= available.center().x();
    if (open_left && left_space < popup_size.width() + gap &&
        right_space > left_space) {
        open_left = false;
    } else if (!open_left && right_space < popup_size.width() + gap &&
               left_space > right_space) {
        open_left = true;
    }

    const int max_x = std::max(
        available.left(), available.right() - popup_size.width() + 1);
    const int x = std::clamp(open_left ? left_x : right_x,
                             available.left(), max_x);
    const int max_y = std::max(
        available.top(), available.bottom() - popup_size.height() + 1);
    const int y = std::clamp(global_top_left.y(), available.top(), max_y);
    popup->popup(QPoint(x, y));
}
