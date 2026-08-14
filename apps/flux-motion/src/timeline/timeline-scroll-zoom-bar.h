#pragma once

#include "timeline-widget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <cmath>

/* Premiere-style timeline navigator. The body pans the visible time range;
 * either end grip changes its size, which is the horizontal zoom operation. */
class TimelineScrollZoomBar final : public QWidget {
public:
    explicit TimelineScrollZoomBar(TimelineWidget *timeline,
                                   QWidget *parent = nullptr)
        : QWidget(parent), timeline_(timeline)
    {
        setObjectName(QStringLiteral("FluxMotionTimelineScrollZoomBar"));
        setMinimumWidth(180);
        setFixedHeight(24);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(QStringLiteral(
            "Drag to scroll. Drag either end to zoom. Double-click to fit."));
        if (timeline_) {
            connect(timeline_, &TimelineWidget::horizontal_view_changed,
                    this, [this](double, double, double) { update(); });
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QPalette pal = palette();
        const QRectF track = trackRect();
        const QRectF thumb = thumbRect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(pal.color(QPalette::Base));
        painter.drawRoundedRect(track, 3.0, 3.0);

        QColor thumb_color = pal.color(QPalette::Mid);
        if (hover_mode_ != DragMode::None || drag_mode_ != DragMode::None)
            thumb_color = pal.color(QPalette::Highlight);
        painter.setBrush(thumb_color);
        painter.drawRoundedRect(thumb, 3.0, 3.0);

        const QColor grip_color =
            (hover_mode_ == DragMode::ResizeLeft ||
             hover_mode_ == DragMode::ResizeRight ||
             drag_mode_ == DragMode::ResizeLeft ||
             drag_mode_ == DragMode::ResizeRight)
                ? pal.color(QPalette::HighlightedText)
                : pal.color(QPalette::ButtonText);
        painter.setPen(QPen(grip_color, 1.0));
        const qreal center_y = thumb.center().y();
        for (qreal x : {thumb.left() + 5.0, thumb.right() - 5.0}) {
            painter.drawLine(QPointF(x - 1.5, center_y - 3.0),
                             QPointF(x - 1.5, center_y + 3.0));
            painter.drawLine(QPointF(x + 1.5, center_y - 3.0),
                             QPointF(x + 1.5, center_y + 3.0));
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!timeline_ || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        const QPointF pos = mousePosition(event);
        drag_mode_ = hitTest(pos);
        if (drag_mode_ == DragMode::None) {
            const double duration = timeline_->horizontal_view_duration();
            const double content = timeline_->horizontal_content_duration();
            const double clicked = timeAtX(pos.x());
            timeline_->set_horizontal_view(
                std::clamp(clicked - duration * 0.5, 0.0,
                           std::max(0.0, content - duration)),
                duration);
            drag_mode_ = DragMode::Move;
        }
        drag_origin_x_ = pos.x();
        drag_start_ = timeline_->horizontal_view_start();
        drag_duration_ = timeline_->horizontal_view_duration();
        setCursor(drag_mode_ == DragMode::Move ? Qt::ClosedHandCursor
                                               : Qt::SizeHorCursor);
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QPointF pos = mousePosition(event);
        if (!timeline_) return;
        if (drag_mode_ == DragMode::None) {
            hover_mode_ = hitTest(pos);
            if (hover_mode_ == DragMode::Move)
                setCursor(Qt::OpenHandCursor);
            else if (hover_mode_ == DragMode::ResizeLeft ||
                     hover_mode_ == DragMode::ResizeRight)
                setCursor(Qt::SizeHorCursor);
            else
                unsetCursor();
            update();
            return;
        }

        const double content = timeline_->horizontal_content_duration();
        const double minimum_duration = std::min(
            content, std::max(1, timeline_->width() - 40) / 1200.0);
        const double delta = (pos.x() - drag_origin_x_) /
                             std::max(1.0, trackRect().width()) * content;
        if (drag_mode_ == DragMode::Move) {
            timeline_->set_horizontal_view(
                std::clamp(drag_start_ + delta, 0.0,
                           std::max(0.0, content - drag_duration_)),
                drag_duration_);
        } else if (drag_mode_ == DragMode::ResizeLeft) {
            const double end = drag_start_ + drag_duration_;
            const double next_start = std::clamp(
                drag_start_ + delta, 0.0,
                std::max(0.0, end - minimum_duration));
            timeline_->set_horizontal_view(next_start, end - next_start);
        } else if (drag_mode_ == DragMode::ResizeRight) {
            timeline_->set_horizontal_view(
                drag_start_,
                std::clamp(drag_duration_ + delta, minimum_duration,
                           std::max(minimum_duration,
                                    content - drag_start_)));
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton &&
            drag_mode_ != DragMode::None) {
            drag_mode_ = DragMode::None;
            hover_mode_ = hitTest(mousePosition(event));
            if (hover_mode_ == DragMode::Move)
                setCursor(Qt::OpenHandCursor);
            else if (hover_mode_ == DragMode::ResizeLeft ||
                     hover_mode_ == DragMode::ResizeRight)
                setCursor(Qt::SizeHorCursor);
            else
                unsetCursor();
            update();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (timeline_ && event->button() == Qt::LeftButton) {
            timeline_->fit_timeline();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void wheelEvent(QWheelEvent *event) override
    {
        if (!timeline_) return;
        const int delta = event->angleDelta().y() != 0
            ? event->angleDelta().y() : event->angleDelta().x();
        if (delta == 0) return;
        const double start = timeline_->horizontal_view_start();
        const double duration = timeline_->horizontal_view_duration();
        const double anchor = timeAtX(wheelPosition(event).x());
        const double next_duration = duration * std::pow(1.0015, -delta);
        const double ratio = duration > 1e-9
            ? std::clamp((anchor - start) / duration, 0.0, 1.0) : 0.5;
        timeline_->set_horizontal_view(anchor - ratio * next_duration,
                                       next_duration);
        event->accept();
    }

    void leaveEvent(QEvent *event) override
    {
        if (drag_mode_ == DragMode::None) {
            hover_mode_ = DragMode::None;
            unsetCursor();
            update();
        }
        QWidget::leaveEvent(event);
    }

private:
    enum class DragMode { None, Move, ResizeLeft, ResizeRight };

    static QPointF mousePosition(const QMouseEvent *event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return event->position();
#else
        return event->localPos();
#endif
    }

    static QPointF wheelPosition(const QWheelEvent *event)
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return event->position();
#else
        return event->posF();
#endif
    }

    QRectF trackRect() const
    {
        return QRectF(3.0, height() * 0.5 - 3.0,
                      std::max(1, width() - 6), 6.0);
    }

    QRectF thumbRect() const
    {
        const QRectF track = trackRect();
        if (!timeline_) return track;
        const double content = std::max(
            1e-9, timeline_->horizontal_content_duration());
        const double start = timeline_->horizontal_view_start();
        const double duration = timeline_->horizontal_view_duration();
        const double natural_left = track.left() + start / content * track.width();
        const double natural_width = duration / content * track.width();
        const double visual_width = std::clamp(natural_width, 24.0,
                                               track.width());
        const double visual_left = std::clamp(
            natural_left - (visual_width - natural_width) * 0.5,
            track.left(), track.right() - visual_width);
        return QRectF(visual_left, height() * 0.5 - 7.0,
                      visual_width, 14.0);
    }

    DragMode hitTest(const QPointF &position) const
    {
        const QRectF thumb = thumbRect();
        if (!thumb.adjusted(-2.0, -3.0, 2.0, 3.0).contains(position))
            return DragMode::None;
        constexpr qreal grip_hit_width = 10.0;
        if (position.x() <= thumb.left() + grip_hit_width)
            return DragMode::ResizeLeft;
        if (position.x() >= thumb.right() - grip_hit_width)
            return DragMode::ResizeRight;
        return DragMode::Move;
    }

    double timeAtX(double x) const
    {
        const QRectF track = trackRect();
        const double ratio = std::clamp(
            (x - track.left()) / std::max(1.0, track.width()), 0.0, 1.0);
        return timeline_ ? ratio * timeline_->horizontal_content_duration()
                         : 0.0;
    }

    TimelineWidget *timeline_ = nullptr;
    DragMode drag_mode_ = DragMode::None;
    DragMode hover_mode_ = DragMode::None;
    double drag_origin_x_ = 0.0;
    double drag_start_ = 0.0;
    double drag_duration_ = 0.0;
};
