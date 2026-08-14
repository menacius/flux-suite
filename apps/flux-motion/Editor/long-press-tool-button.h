#pragma once

#include <QMouseEvent>
#include <QPoint>
#include <QTimer>
#include <QToolButton>

class LongPressToolButton final : public QToolButton {
public:
    explicit LongPressToolButton(QWidget *parent = nullptr) : QToolButton(parent)
    {
        long_press_timer_.setSingleShot(true);
        long_press_timer_.setInterval(250);
        QObject::connect(&long_press_timer_, &QTimer::timeout, this, [this]() {
            long_press_triggered_ = true;
            if (menu())
                menu()->popup(mapToGlobal(QPoint(0, height())));
        });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            long_press_triggered_ = false;
            long_press_timer_.start();
            setDown(true);
            event->accept();
            return;
        }
        QToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            const bool was_long_press = long_press_triggered_;
            long_press_timer_.stop();
            setDown(false);
            event->accept();
            if (!was_long_press && rect().contains(event->position().toPoint()))
                click();
            return;
        }
        QToolButton::mouseReleaseEvent(event);
    }

private:
    QTimer long_press_timer_;
    bool long_press_triggered_ = false;
};
