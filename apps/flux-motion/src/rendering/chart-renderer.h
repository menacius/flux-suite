#pragma once

#include "layer-model.h"

#include <QRectF>

class QPainter;

namespace fxm::chart {

/* Shared vector-command renderer used by both the standalone software session
 * and the OBS/shared raster adapter. All chart layout and geometry lives here;
 * hosts only provide a QPainter target. */
void render(QPainter &painter, const Layer &layer, const QRectF &bounds,
            double local_time);

} // namespace fxm::chart
