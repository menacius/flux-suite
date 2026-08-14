#pragma once

#include <QImage>
#include <QRect>
#include <QString>

#include <algorithm>

namespace fxm::cache_frame_payload {

inline constexpr const char *kCanvasX = "obs_fxm_canvas_x";
inline constexpr const char *kCanvasY = "obs_fxm_canvas_y";
inline constexpr const char *kCanvasWidth = "obs_fxm_canvas_width";
inline constexpr const char *kCanvasHeight = "obs_fxm_canvas_height";

struct Placement {
    int x = 0;
    int y = 0;
    int canvas_width = 0;
    int canvas_height = 0;

    QRect image_rect(const QImage &image) const
    {
        return QRect(x, y, image.width(), image.height());
    }
};

inline void set_placement(QImage &image, int x, int y,
                          int canvas_width, int canvas_height)
{
    image.setText(QString::fromLatin1(kCanvasX), QString::number(x));
    image.setText(QString::fromLatin1(kCanvasY), QString::number(y));
    image.setText(QString::fromLatin1(kCanvasWidth), QString::number(canvas_width));
    image.setText(QString::fromLatin1(kCanvasHeight), QString::number(canvas_height));
}

inline bool read_placement(const QImage &image, Placement &placement)
{
    if (image.isNull())
        return false;

    bool ok_x = false;
    bool ok_y = false;
    bool ok_width = false;
    bool ok_height = false;
    placement.x = image.text(QString::fromLatin1(kCanvasX)).toInt(&ok_x);
    placement.y = image.text(QString::fromLatin1(kCanvasY)).toInt(&ok_y);
    placement.canvas_width = image.text(QString::fromLatin1(kCanvasWidth)).toInt(&ok_width);
    placement.canvas_height = image.text(QString::fromLatin1(kCanvasHeight)).toInt(&ok_height);

    return ok_x && ok_y && ok_width && ok_height &&
           placement.x >= 0 && placement.y >= 0 &&
           placement.canvas_width > 0 && placement.canvas_height > 0 &&
           image.width() <= placement.canvas_width &&
           image.height() <= placement.canvas_height &&
           placement.x <= placement.canvas_width - image.width() &&
           placement.y <= placement.canvas_height - image.height();
}

/* Resolve a cache image to an exact destination on the requested canvas.
 * Current sparse payloads must carry valid placement metadata. A metadata-free
 * image is accepted only when it is already a full-canvas frame. This prevents
 * a damaged or stale crop from being stretched across the whole title. */
inline bool resolve_placement(const QImage &image, int expected_canvas_width,
                              int expected_canvas_height, QRect &destination)
{
    const int canvas_width = std::max(1, expected_canvas_width);
    const int canvas_height = std::max(1, expected_canvas_height);

    Placement placement;
    if (read_placement(image, placement)) {
        if (placement.canvas_width != canvas_width ||
            placement.canvas_height != canvas_height)
            return false;
        destination = placement.image_rect(image);
        return true;
    }

    if (image.width() == canvas_width && image.height() == canvas_height) {
        destination = QRect(0, 0, canvas_width, canvas_height);
        return true;
    }

    destination = QRect();
    return false;
}

} // namespace fxm::cache_frame_payload
