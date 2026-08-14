#pragma once

#include <QPainterPath>

#include <vector>

namespace fxm {

struct TrimPathsGeometryOptions {
    double start_percent = 0.0;
    double end_percent = 100.0;
    double trim_offset_degrees = 0.0;
    bool individually = false;
    double flatten_tolerance = 0.25;
};

/* Applies the layer's general Stroke Offset to the path centreline before
 * stroking. Positive values move closed contours outward and open paths to the
 * left of their authored direction; negative values reverse that direction. */
QPainterPath apply_stroke_offset_geometry(const QPainterPath &source,
                                          double offset,
                                          double flatten_tolerance = 0.25);

std::vector<QPainterPath> apply_stroke_offset_geometry_partitioned(
    const std::vector<QPainterPath> &sources,
    double offset,
    double flatten_tolerance = 0.25);

/* Applies arc-length based Trim Paths to already offset path geometry. The
 * returned path is intended to be stroked; the caller retains the original
 * path for fills and inside/outside clipping. */
QPainterPath apply_trim_paths_geometry(const QPainterPath &source,
                                       const TrimPathsGeometryOptions &options);

/* Applies one shared Trim Paths operation to an ordered set of paths while
 * preserving ownership of every generated fragment. This is used by rich text
 * so Simultaneously measures the complete text box once, even when glyphs use
 * different stroke styles, and then returns each fragment to its source style. */
std::vector<QPainterPath> apply_trim_paths_geometry_partitioned(
    const std::vector<QPainterPath> &sources,
    const TrimPathsGeometryOptions &options);

} // namespace fxm
