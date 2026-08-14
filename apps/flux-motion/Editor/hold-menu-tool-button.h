#pragma once

#include <QTimer>
#include <QToolButton>

class HoldMenuToolButton : public QToolButton {
public:
    explicit HoldMenuToolButton(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void popup_menu_beside_button();

    QTimer hold_timer_;
    bool menu_opened_by_hold_ = false;
};
