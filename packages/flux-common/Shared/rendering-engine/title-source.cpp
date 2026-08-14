/*
 * title-source.cpp
 *
 * Unified GPU compositor for OBS live output, editor preview and final cache
 * readback. Supported Text/Clock layers are composed from persistent SDF glyph
 * atlases and GPU quads; compatibility text raster adapters, vector/image
 * adapters and unsupported color fonts rasterize only when their content changes.
 * Transforms, masks, effects, blending and temporal motion blur remain
 * GPU-resident through presentation.
 *
 * Build dependency: cairo, pango, pangocairo (compatibility raster adapters)
 */

#include "cache-manager.h"
#include "asset-path-provider.h"
#include "cache-frame-payload.h"
#include "cache-tile-payload.h"
#include "title-cache-policy.h"
#include "title-source.h"
#include "title-audio-runtime.h"
#include "title-video-runtime.h"
#include "style-preset-runtime.h"
#include "title-data.h"
#include "external-data.h"
#include "external-data-log.h"
#include "live-text-cue-utils.h"
#include "title-snapshot.h"
#include "plugin-main.h"
#include "title-localization.h"
#include "title-effect-registry.h"
#include "extensions/effect-extension-catalog.h"
#include "effects/effect-animation-utils.h"
#include "effects/effect-runtime.h"
#include "performance-counters.h"
#include "motion-blur-sampling.h"
#include "title-gpu-text-renderer.h"
#include "layer-transform-3d.h"
#include "chart-renderer.h"
#include "chart-data.h"
#include "title-preferences.h"
#include "title-logger.h"
#include "ticker-runtime.h"
#include "asset-runtime.h"
#include "obs-render-backend.h"
#include "image-layer-utils.h"
#include "title-text-layout.h"
#include "title-text-layout-qt-font-registry.h"
#include "text-animator-presets.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <graphics/graphics.h>
#include <graphics/vec2.h>
#include <graphics/vec3.h>
#include <graphics/vec4.h>
#include <graphics/matrix4.h>
#include <graphics/image-file.h>
#include <util/threading.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <QImage>
#include <QImageReader>
#include <QSize>
#include <QSvgRenderer>
#include <QString>
#include <QStringList>
#include <QLocale>
#include <QPointF>
#include <QPainter>
#include <QBrush>
#include <QPainterPath>
#include <QtGlobal>
#include <QFont>
#include <QRawFont>
#include <QFontMetrics>
#include <QFontDatabase>
#include <QTextLayout>
#include <QTextDocument>
#include <QTextBlock>
#include <QAbstractTextDocumentLayout>
#include <QTextOption>
#include <QTextCursor>
#include <QTextBoundaryFinder>
#include <QDateTime>
#include <QFileInfo>
#include <QDir>
#include <QTransform>
#include <QColor>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QConicalGradient>
#include <QCryptographicHash>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QMetaObject>
#include <QObject>
#include <QCoreApplication>

#include <memory>
#include <string>
#include <cstring>
#include <cmath>
#include <chrono>
#include <vector>
#include <algorithm>
#include <atomic>
#include <array>
#include <mutex>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <condition_variable>
#include <deque>
#include <sstream>
#include <iomanip>
#include <functional>
#include <utility>
#include <exception>
#include "path-geometry.h"
#include "stroke-path-geometry.h"

using fxm::live_text::exposed_text_layers;

/* OBS graphics helpers do not accept a null effect handle. Shader creation can
 * legitimately fail while the frontend/graphics subsystem is still settling
 * during startup, so every split title-source module uses this checked lookup.
 * The wrapper is intentionally inline and disappears to the same single API
 * call in the normal non-null path. */
static inline gs_eparam_t *fxm_effect_param(gs_effect_t *effect,
                                             const char *name) noexcept
{
    return effect && name ? gs_effect_get_param_by_name(effect, name) : nullptr;
}

/* Motion Blur accumulates fractional weighted samples. 8-bit UNORM targets
 * quantize each contribution and become visibly banded on smooth fills/images,
 * so exposure and coverage use a floating-point target when the backend allows
 * it. The input sample target remains display-format BGRA; only the running
 * sums need extra precision. */
static gs_texrender_t *fxm_create_motion_blur_sample_target()
{
    return gs_texrender_create(GS_BGRA, GS_ZS_NONE);
}

static gs_texrender_t *fxm_create_motion_blur_accumulation_target()
{
    gs_texrender_t *target = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
    if (!target)
        target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    return target;
}

/* Implemented after all split title-source function continuations have closed.
 * The declaration must remain visible to the cache API in
 * source-lifecycle-playback.inc. */
static bool alias_global_gpu_frame_locked(
    const std::string &cache_key, const std::string &canonical_cache_key);


/* Ordered implementation modules. Keep this list in source order. The shared
 * session implementation includes the explicit extent/time render contract. */
#include "title-source/source-runtime.inc"
#include "title-source/scene-masks-properties.inc"
#include "title-source/layer-evaluation-layout.inc"
#include "title-source/compatibility-text-rendering.inc"
#include "title-source/compatibility-layer-raster.inc"
#include "title-source/compatibility-effects-compositor.inc"
#include "title-source/gpu-resources-primitives.inc"
#include "title-source/gpu-effects-transitions.inc"
#include "title-source/gpu-masks-groups-cache.inc"
#include "title-source/gpu-presentation-readback.inc"
#include "title-source/gpu-session-lifecycle.inc"
#include "title-source/source-lifecycle-playback.inc"
#include "title-source/source-registration.inc"
/* The preceding three files contain split function bodies and must remain
 * contiguous. Definitions inserted between them become illegal block-scope
 * functions on MSVC. */
#include "title-source/gpu-frame-cache-alias.inc"
