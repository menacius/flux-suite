#include "cache-manager.h"
#include "title-serialization-schema.h"
#include "cache-frame-payload.h"
#include "cache-tile-payload.h"
#include "cache-time.h"
#include "title-cache-policy.h"
#include "title-snapshot.h"
#include "title-localization.h"
#include "title-render-session.h"
#include "title-video-runtime.h"
#include "title-preferences.h"
#include "title-logger.h"
#include "external-data.h"
#include "chart-data.h"
#include "image-layer-utils.h"
#include "live-text-cue-utils.h"
#include "layer-transform-3d.h"
#include "performance-counters.h"
#include "frame-rate-provider.h"
#include "effects/effect-runtime.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QBuffer>
#include <QDataStream>
#include <QMutexLocker>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QMetaObject>
#include <QTimer>

#ifdef OBS_FXM_HAVE_LZ4
#include <lz4.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <chrono>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif


/* Ordered implementation modules. Keep this list in source order. */
#include "cache-manager/visual-hash-keying.inc"
#include "cache-manager/disk-cache-storage.inc"
#include "cache-manager/cache-policy-invalidation.inc"
#include "cache-manager/live-cue-state.inc"
#include "cache-manager/live-cue-queueing.inc"
#include "cache-manager/worker-publication.inc"
