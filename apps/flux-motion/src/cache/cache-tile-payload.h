#pragma once

#include "cache-frame-payload.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QImage>
#include <QRect>
#include <QVector>

#include <algorithm>
#include <cstring>

namespace fxm::cache_tile_payload {

inline constexpr int kTileSize = 256;

struct Tile {
    QRect rect;
    QImage image;
    QByteArray digest;
};

inline bool has_visible_alpha(const QImage &image)
{
    if (image.isNull())
        return false;
    const QImage argb = image.format() == QImage::Format_ARGB32
        ? image : image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) != 0)
                return true;
        }
    }
    return false;
}

inline QByteArray raw_bgra_bytes(const QImage &image)
{
    if (image.isNull())
        return QByteArray();
    const QImage argb = image.format() == QImage::Format_ARGB32
        ? image : image.convertToFormat(QImage::Format_ARGB32);
    const qsizetype row_bytes = qsizetype(argb.width()) * 4;
    QByteArray raw;
    raw.resize(int(row_bytes * argb.height()));
    for (int y = 0; y < argb.height(); ++y)
        std::memcpy(raw.data() + y * row_bytes, argb.constScanLine(y), size_t(row_bytes));
    return raw;
}

inline QByteArray digest_for_tile(const QImage &image)
{
    if (image.isNull())
        return QByteArray();
    const QByteArray raw = raw_bgra_bytes(image);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const quint32 width = quint32(image.width());
    const quint32 height = quint32(image.height());
    hash.addData(reinterpret_cast<const char *>(&width), sizeof(width));
    hash.addData(reinterpret_cast<const char *>(&height), sizeof(height));
    hash.addData(raw);
    return hash.result();
}

inline QVector<Tile> extract_nonempty_tiles(
    const QImage &sparse_image,
    const cache_frame_payload::Placement &placement,
    int tile_size = kTileSize)
{
    QVector<Tile> tiles;
    if (sparse_image.isNull() || tile_size <= 0 ||
        placement.canvas_width <= 0 || placement.canvas_height <= 0)
        return tiles;

    const QImage source = sparse_image.format() == QImage::Format_ARGB32
        ? sparse_image : sparse_image.convertToFormat(QImage::Format_ARGB32);
    const QRect canvas_rect(0, 0, placement.canvas_width, placement.canvas_height);
    const QRect source_rect(placement.x, placement.y, source.width(), source.height());
    const QRect clipped_source = source_rect.intersected(canvas_rect);
    if (clipped_source.isEmpty())
        return tiles;

    const int first_tx = clipped_source.left() / tile_size;
    const int last_tx = clipped_source.right() / tile_size;
    const int first_ty = clipped_source.top() / tile_size;
    const int last_ty = clipped_source.bottom() / tile_size;

    for (int ty = first_ty; ty <= last_ty; ++ty) {
        for (int tx = first_tx; tx <= last_tx; ++tx) {
            const QRect tile_rect(tx * tile_size, ty * tile_size,
                                  tile_size, tile_size);
            const QRect clipped_tile = tile_rect.intersected(canvas_rect);
            const QRect overlap = clipped_tile.intersected(source_rect);
            if (overlap.isEmpty())
                continue;

            QImage tile(clipped_tile.size(), QImage::Format_ARGB32);
            tile.fill(Qt::transparent);
            const int source_x = overlap.x() - source_rect.x();
            const int source_y = overlap.y() - source_rect.y();
            const int tile_x = overlap.x() - clipped_tile.x();
            const int tile_y = overlap.y() - clipped_tile.y();
            const qsizetype row_bytes = qsizetype(overlap.width()) * 4;
            for (int row = 0; row < overlap.height(); ++row) {
                std::memcpy(tile.scanLine(tile_y + row) + qsizetype(tile_x) * 4,
                            source.constScanLine(source_y + row) + qsizetype(source_x) * 4,
                            size_t(row_bytes));
            }
            if (!has_visible_alpha(tile))
                continue;

            Tile result;
            result.rect = clipped_tile;
            result.image = std::move(tile);
            result.digest = digest_for_tile(result.image);
            if (!result.digest.isEmpty())
                tiles.push_back(std::move(result));
        }
    }
    return tiles;
}

inline QImage compose_sparse_tiles(const QVector<Tile> &tiles,
                                   int canvas_width,
                                   int canvas_height)
{
    if (canvas_width <= 0 || canvas_height <= 0)
        return QImage();
    if (tiles.isEmpty()) {
        QImage empty(1, 1, QImage::Format_ARGB32);
        empty.fill(Qt::transparent);
        cache_frame_payload::set_placement(empty, 0, 0,
                                           canvas_width, canvas_height);
        return empty;
    }

    const QRect canvas_rect(0, 0, canvas_width, canvas_height);
    QRect union_rect;
    for (const Tile &tile : tiles) {
        if (tile.image.isNull() || tile.rect.size() != tile.image.size() ||
            !canvas_rect.contains(tile.rect))
            return QImage();
        union_rect = union_rect.united(tile.rect);
    }
    if (union_rect.isEmpty())
        return QImage();

    QImage sparse(union_rect.size(), QImage::Format_ARGB32);
    sparse.fill(Qt::transparent);
    for (const Tile &tile : tiles) {
        const QImage source = tile.image.format() == QImage::Format_ARGB32
            ? tile.image : tile.image.convertToFormat(QImage::Format_ARGB32);
        const int destination_x = tile.rect.x() - union_rect.x();
        const int destination_y = tile.rect.y() - union_rect.y();
        const qsizetype row_bytes = qsizetype(source.width()) * 4;
        for (int row = 0; row < source.height(); ++row) {
            std::memcpy(sparse.scanLine(destination_y + row) +
                            qsizetype(destination_x) * 4,
                        source.constScanLine(row), size_t(row_bytes));
        }
    }
    cache_frame_payload::set_placement(sparse, union_rect.x(), union_rect.y(),
                                       canvas_width, canvas_height);
    return sparse;
}

} // namespace fxm::cache_tile_payload
