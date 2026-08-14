#pragma once

#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QPolygonF>
#include <QSlider>
#include <QStyle>
#include <QStyleOptionSlider>
#include <algorithm>

class FluxEncoderZoomablePreview final : public QLabel {
public:
    explicit FluxEncoderZoomablePreview(QWidget *parent = nullptr) : QLabel(parent)
    {
        setAlignment(Qt::AlignCenter);
        setMinimumSize(240, 135);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setPreviewPixmap(const QPixmap &pixmap)
    {
        sourcePixmap_ = pixmap;
        setText(pixmap.isNull() ? text() : QString());
        update();
    }

    void clearPreview(const QString &message = QString())
    {
        sourcePixmap_ = QPixmap();
        setText(message);
        update();
    }

    bool hasPreview() const { return !sourcePixmap_.isNull(); }
    int zoomPercent() const { return zoomPercent_; }
    bool fitToView() const { return fitToView_; }
    int rotationDegrees() const { return rotationDegrees_; }
    const QPixmap &sourcePixmap() const { return sourcePixmap_; }

    void setZoomPercent(int percent)
    {
        zoomPercent_ = std::clamp(percent, 10, 400);
        if (!fitToView_)
            update();
    }

    void setFitToView(bool fit)
    {
        fitToView_ = fit;
        update();
    }

    void setRotationDegrees(int degrees)
    {
        rotationDegrees_ = ((degrees % 360) + 360) % 360;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (sourcePixmap_.isNull()) {
            QLabel::paintEvent(event);
            return;
        }

        QLabel::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRect targetBounds = contentsRect().adjusted(2, 2, -2, -2);
        QSize targetSize;
        if (fitToView_) {
            targetSize = sourcePixmap_.size().scaled(targetBounds.size(), Qt::KeepAspectRatio);
        } else {
            targetSize = QSize(qMax(1, qRound(sourcePixmap_.width() * zoomPercent_ / 100.0)),
                               qMax(1, qRound(sourcePixmap_.height() * zoomPercent_ / 100.0)));
        }
        const QRect target(QPoint(targetBounds.center().x() - targetSize.width() / 2,
                                  targetBounds.center().y() - targetSize.height() / 2), targetSize);
        painter.setClipRect(targetBounds);
        painter.save();
        if (rotationDegrees_ != 0) {
            painter.translate(target.center());
            painter.rotate(rotationDegrees_);
            QRect rotatedTarget(-target.width() / 2, -target.height() / 2, target.width(), target.height());
            painter.drawPixmap(rotatedTarget, sourcePixmap_);
        } else {
            painter.drawPixmap(target, sourcePixmap_);
        }
        painter.restore();
        event->accept();
    }

private:
    QPixmap sourcePixmap_;
    int zoomPercent_ = 100;
    bool fitToView_ = true;
    int rotationDegrees_ = 0;
};

class FluxEncoderRangeSlider final : public QSlider {
public:
    explicit FluxEncoderRangeSlider(Qt::Orientation orientation, QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

    void setInOutValues(int inValue, int outValue)
    {
        inValue_ = std::clamp(inValue, minimum(), maximum());
        outValue_ = std::clamp(outValue, minimum(), maximum());
        if (outValue_ < inValue_)
            std::swap(inValue_, outValue_);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QSlider::paintEvent(event);
        if (orientation() != Qt::Horizontal || maximum() <= minimum())
            return;

        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_Slider, &option,
                                                      QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(QStyle::CC_Slider, &option,
                                                      QStyle::SC_SliderHandle, this);
        const int span = qMax(1, width() - handle.width());
        auto positionFor = [&](int value) {
            return QStyle::sliderPositionFromValue(minimum(), maximum(), value, span,
                                                   option.upsideDown) + handle.width() / 2;
        };

        const int inX = positionFor(inValue_);
        const int outX = positionFor(outValue_);
        const int left = qMin(inX, outX);
        const int right = qMax(inX, outX);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor rangeColor = palette().color(QPalette::Highlight);
        rangeColor.setAlpha(115);
        const QRectF rangeRect(left, groove.center().y() - 3.0, qMax(1, right - left), 6.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(rangeColor);
        painter.drawRoundedRect(rangeRect, 3.0, 3.0);

        QColor markerColor = palette().color(QPalette::Highlight);
        painter.setBrush(markerColor);
        const qreal top = groove.center().y() - 10.0;
        QPolygonF inMarker;
        inMarker << QPointF(inX - 5.0, top) << QPointF(inX + 5.0, top)
                 << QPointF(inX, top + 7.0);
        QPolygonF outMarker;
        outMarker << QPointF(outX - 5.0, top) << QPointF(outX + 5.0, top)
                  << QPointF(outX, top + 7.0);
        painter.drawPolygon(inMarker);
        painter.drawPolygon(outMarker);
    }

private:
    int inValue_ = 0;
    int outValue_ = 1000;
};
