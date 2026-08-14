#include "cache-tile-payload.h"

#include <cassert>
#include <iostream>

int main()
{
    constexpr int canvas_width = 1920;
    constexpr int canvas_height = 1080;
    QImage frame(canvas_width, canvas_height, QImage::Format_ARGB32);
    frame.fill(Qt::transparent);

    for (int y = 10; y < 42; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(frame.scanLine(y));
        for (int x = 10; x < 42; ++x)
            row[x] = qRgba(255, 255, 255, 255);
    }
    for (int y = 522; y < 554; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(frame.scanLine(y));
        for (int x = 1034; x < 1066; ++x)
            row[x] = qRgba(255, 255, 255, 255);
    }

    fxm::cache_frame_payload::Placement placement;
    placement.x = 0;
    placement.y = 0;
    placement.canvas_width = canvas_width;
    placement.canvas_height = canvas_height;
    const auto tiles = fxm::cache_tile_payload::extract_nonempty_tiles(
        frame, placement);
    assert(tiles.size() == 2);
    assert(tiles[0].image.width() == 256);
    assert(tiles[0].image.height() == 256);
    assert(tiles[0].digest == tiles[1].digest);

    quint64 stored_pixels = 0;
    for (const auto &tile : tiles)
        stored_pixels += quint64(tile.image.width()) * quint64(tile.image.height());
    assert(stored_pixels * 8 < quint64(canvas_width) * quint64(canvas_height));

    const QImage composed = fxm::cache_tile_payload::compose_sparse_tiles(
        tiles, canvas_width, canvas_height);
    assert(!composed.isNull());
    fxm::cache_frame_payload::Placement composed_placement;
    assert(fxm::cache_frame_payload::read_placement(
        composed, composed_placement));
    assert(composed_placement.x == 0);
    assert(composed_placement.y == 0);
    assert(composed_placement.canvas_width == canvas_width);
    assert(composed_placement.canvas_height == canvas_height);
    assert(qAlpha(composed.pixel(10, 10)) == 255);
    assert(qAlpha(composed.pixel(1034, 522)) == 255);
    assert(qAlpha(composed.pixel(700, 300)) == 0);

    QImage transparent(320, 180, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    placement.canvas_width = 320;
    placement.canvas_height = 180;
    const auto no_tiles = fxm::cache_tile_payload::extract_nonempty_tiles(
        transparent, placement);
    assert(no_tiles.isEmpty());
    const QImage empty = fxm::cache_tile_payload::compose_sparse_tiles(
        no_tiles, 320, 180);
    assert(empty.size() == QSize(1, 1));

    std::cout << "content-addressed tiled cache payload contract passed\n";
    return 0;
}
