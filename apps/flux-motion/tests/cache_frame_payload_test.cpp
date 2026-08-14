#include "cache-frame-payload.h"

#include <QImage>
#include <QRect>

#include <cassert>
#include <iostream>

int main()
{
    QImage sparse(40, 20, QImage::Format_ARGB32);
    sparse.fill(Qt::transparent);
    fxm::cache_frame_payload::set_placement(sparse, 100, 50, 1920, 1080);

    fxm::cache_frame_payload::Placement placement;
    assert(fxm::cache_frame_payload::read_placement(sparse, placement));
    assert(placement.x == 100 && placement.y == 50);
    assert(placement.canvas_width == 1920 && placement.canvas_height == 1080);

    QRect destination;
    assert(fxm::cache_frame_payload::resolve_placement(sparse, 1920, 1080,
                                                       destination));
    assert(destination == QRect(100, 50, 40, 20));
    assert(!fxm::cache_frame_payload::resolve_placement(sparse, 1280, 720,
                                                        destination));

    QImage full(1920, 1080, QImage::Format_ARGB32);
    full.fill(Qt::transparent);
    assert(fxm::cache_frame_payload::resolve_placement(full, 1920, 1080,
                                                       destination));
    assert(destination == QRect(0, 0, 1920, 1080));

    QImage unplaced_crop(40, 20, QImage::Format_ARGB32);
    unplaced_crop.fill(Qt::transparent);
    assert(!fxm::cache_frame_payload::resolve_placement(
        unplaced_crop, 1920, 1080, destination));

    QImage out_of_bounds(40, 20, QImage::Format_ARGB32);
    out_of_bounds.fill(Qt::transparent);
    fxm::cache_frame_payload::set_placement(
        out_of_bounds, 1900, 1070, 1920, 1080);
    assert(!fxm::cache_frame_payload::resolve_placement(
        out_of_bounds, 1920, 1080, destination));

    std::cout << "strict sparse cache payload placement contract passed\n";
    return 0;
}
