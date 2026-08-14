#include "image-layer-utils.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>

namespace {

struct CachedIntrinsicSize {
    QSize size;
    qint64 last_modified_msecs = 0;
    qint64 file_size = -1;
    uint64_t last_used = 0;
};

bool is_svg_path(const QString &path)
{
    return path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) ||
           path.endsWith(QStringLiteral(".svgz"), Qt::CaseInsensitive);
}

QSize read_intrinsic_size(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};

    if (is_svg_path(path)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid())
            return {};
        QSize size = renderer.defaultSize();
        if (!size.isValid() || size.isEmpty())
            size = renderer.viewBox().size();
        return size;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && !size.isEmpty())
        return size;

    const QImage image = reader.read();
    return image.isNull() ? QSize() : image.size();
}

double positive_dimension_or(double value, double fallback)
{
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

double finite_nonnegative_or(double value, double fallback)
{
    return std::isfinite(value) ? std::max(0.0, value) : fallback;
}

} // namespace

namespace fxm {

ImageDisplaySize calculate_image_display_size(ImageBoxMode mode, bool auto_fit,
                                               double box_width, double box_height,
                                               double image_width, double image_height)
{
    ImageDisplaySize out;
    if (!std::isfinite(box_width) || !std::isfinite(box_height) ||
        !std::isfinite(image_width) || !std::isfinite(image_height) ||
        box_width <= 0.0 || box_height <= 0.0 ||
        image_width <= 0.0 || image_height <= 0.0) {
        return out;
    }

    if (!auto_fit) {
        out.width = image_width;
        out.height = image_height;
        return out;
    }

    if (mode == ImageBoxMode::StretchToFill) {
        out.width = box_width;
        out.height = box_height;
        return out;
    }

    const double scale_x = box_width / image_width;
    const double scale_y = box_height / image_height;
    double scale = 1.0;
    switch (mode) {
    case ImageBoxMode::FitImageToBox:
        scale = std::min(scale_x, scale_y);
        break;
    case ImageBoxMode::FillHorizontal:
    case ImageBoxMode::LegacyFitHorizontalCrop:
        scale = scale_x;
        break;
    case ImageBoxMode::FillVertical:
    case ImageBoxMode::LegacyFitVerticalCrop:
        scale = scale_y;
        break;
    case ImageBoxMode::FitToLongSide:
        // Fit the image's own longest side to the matching box dimension.
        scale = image_width >= image_height ? scale_x : scale_y;
        break;
    case ImageBoxMode::FitToShortSide:
        // Fit the image's own shortest side to the matching box dimension.
        scale = image_width >= image_height ? scale_y : scale_x;
        break;
    case ImageBoxMode::StretchToFill:
        break;
    }

    out.width = std::max(1.0, image_width * scale);
    out.height = std::max(1.0, image_height * scale);
    return out;
}

QSize image_intrinsic_size_for_path(const std::string &path_value)
{
    const QString path = QString::fromStdString(path_value).trimmed();
    if (path.isEmpty())
        return {};

    const QFileInfo info(path);
    const qint64 modified = info.exists() ? info.lastModified().toMSecsSinceEpoch() : 0;
    const qint64 bytes = info.exists() ? info.size() : -1;
    const std::string key = info.exists()
        ? info.absoluteFilePath().toStdString()
        : path.toStdString();

    static std::mutex cache_mutex;
    static std::unordered_map<std::string, CachedIntrinsicSize> cache;
    static uint64_t cache_tick = 0;
    constexpr std::size_t kMaxIntrinsicSizeCacheEntries = 256;

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        const auto it = cache.find(key);
        if (it != cache.end() &&
            it->second.last_modified_msecs == modified &&
            it->second.file_size == bytes) {
            it->second.last_used = ++cache_tick;
            return it->second.size;
        }
    }

    const QSize size = read_intrinsic_size(path);
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache[key] = CachedIntrinsicSize{size, modified, bytes, ++cache_tick};
        while (cache.size() > kMaxIntrinsicSizeCacheEntries) {
            auto oldest = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->first == key)
                    continue;
                if (oldest == cache.end() ||
                    it->second.last_used < oldest->second.last_used)
                    oldest = it;
            }
            if (oldest == cache.end())
                break;
            cache.erase(oldest);
        }
    }
    return size;
}

bool apply_exposed_image_cue_value(Layer &layer, const std::string &path)
{
    const QSize old_intrinsic = image_intrinsic_size_for_path(layer.image_path);
    const QSize new_intrinsic = image_intrinsic_size_for_path(path);

    layer.image_path = path;
    if (!new_intrinsic.isValid() || new_intrinsic.isEmpty())
        return false;

    const double new_width = std::max(1, new_intrinsic.width());
    const double new_height = std::max(1, new_intrinsic.height());

    if (layer.image_size.is_animated()) {
        const double old_width = old_intrinsic.isValid() && !old_intrinsic.isEmpty()
            ? std::max(1, old_intrinsic.width())
            : positive_dimension_or(layer.image_width,
                                     positive_dimension_or(layer.image_size.static_value.x, new_width));
        const double old_height = old_intrinsic.isValid() && !old_intrinsic.isEmpty()
            ? std::max(1, old_intrinsic.height())
            : positive_dimension_or(layer.image_height,
                                     positive_dimension_or(layer.image_size.static_value.y, new_height));
        const double width_ratio = new_width / old_width;
        const double height_ratio = new_height / old_height;

        layer.image_size.static_value.x = finite_nonnegative_or(
            layer.image_size.static_value.x, old_width) * width_ratio;
        layer.image_size.static_value.y = finite_nonnegative_or(
            layer.image_size.static_value.y, old_height) * height_ratio;
        for (auto &keyframe : layer.image_size.keyframes) {
            keyframe.value.x = finite_nonnegative_or(
                keyframe.value.x, old_width) * width_ratio;
            keyframe.value.y = finite_nonnegative_or(
                keyframe.value.y, old_height) * height_ratio;
        }
    } else {
        layer.image_size.static_value.x = new_width;
        layer.image_size.static_value.y = new_height;
    }

    layer.image_width = static_cast<float>(new_width);
    layer.image_height = static_cast<float>(new_height);
    if (layer.image_box_mode == ImageBoxMode::StretchToFill)
        layer.lock_aspect_ratio = false;
    return true;
}

} // namespace fxm
