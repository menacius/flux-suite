#pragma once

#include "layer-model.h"

#include <QSize>

#include <string>

namespace fxm {

struct ImageDisplaySize {
    double width = 0.0;
    double height = 0.0;
};

/* Resolve the actual rendered dimensions of an image inside its image box.
 * Auto-fit modes derive the dimensions from the box and the image aspect;
 * manual image sizes are returned unchanged. */
ImageDisplaySize calculate_image_display_size(ImageBoxMode mode, bool auto_fit,
                                               double box_width, double box_height,
                                               double image_width, double image_height);

/* Returns the natural pixel size of a bitmap or SVG asset. Results are cached
 * against the file timestamp/size because live cue variants can query the same
 * image repeatedly while prerendering. */
QSize image_intrinsic_size_for_path(const std::string &path);

/* Apply an exposed image cue value and make the layer's image-size model match
 * the replacement asset. Static image sizes reset to the asset's natural
 * dimensions. Animated image-size tracks are rebased from the old asset's
 * dimensions so their animation is retained with the new aspect ratio. */
bool apply_exposed_image_cue_value(Layer &layer, const std::string &path);

} // namespace fxm
