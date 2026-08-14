namespace {

static const char *cache_playback_state_name(FrameCacheState state)
{
    switch (state) {
    case FrameCacheState::NotCached: return "NotCached";
    case FrameCacheState::Queued: return "Queued";
    case FrameCacheState::Rendering: return "Rendering";
    case FrameCacheState::CachedRam: return "CachedRam";
    case FrameCacheState::CachedDisk: return "CachedDisk";
    case FrameCacheState::Stale: return "Stale";
    case FrameCacheState::Disabled: return "Disabled";
    }
    return "Unknown";
}

static void log_cache_playback(const QString &message)
{
    if (TitlePreferences::cache_playback_logging_enabled())
        FXM_LOG_INFO("CachePlayback", message);
}
double cache_obs_frame_rate()
{
    return fxm::current_frame_rate();
}

int cache_last_frame_for_title(const Title &title, double fps)
{
    return std::max(0, (int)std::ceil(std::max(0.0, title.duration) * std::max(1.0, fps)) - 1);
}

constexpr quint32 kRawFrameMagic = 0x4f475346; // FXMF
constexpr quint16 kRawFrameVersion = 5;
constexpr quint32 kTilePayloadMagic = 0x4f475354; // FXMT
constexpr quint16 kTilePayloadVersion = 1;
constexpr quint16 kFrameCodecRaw = 0;
constexpr quint16 kFrameCodecLz4 = 1;
/* Cache payloads are straight-alpha BGRA, exactly as OBS expects for GS_BGRA.
 * Premultiplied Cairo frames are converted once on the worker, never during
 * realtime playback. */
constexpr QImage::Format kDiskFrameFormat = QImage::Format_ARGB32;
constexpr int kCacheTileSize = fxm::cache_tile_payload::kTileSize;
/* Bump whenever the GPU render graph, effect compositing, alpha contract,
 * bounds expansion, or cache/readback semantics change. This is part of the
 * visual cache identity and prevents stale prerenders from surviving renderer
 * fixes. */
constexpr const char *kGpuRendererCacheAbi =
    "gpu-renderer-v31-lens-flare-dx11-keyword-fix-v32-asset-layers-v33-effects-cleanup-4-color-gradient-v34-linux-alpha8-text-temporal-frame-alias-v35-layer-space-effects-stack-v36-text-transition-ink-gutter-shared-blur-v37-live-cue-transition-persistence-v38-camera-timeline-animation-static-compatible-v39-parent-bind-matrix-3d-pipeline-completion-v41-procedural-noise-schema3-detail-core-v43-obs-effect-entrypoint-fix-v44-source-aware-effects-v48-3d-lighting-materials-v49-legacy-slice-retired-transactional-presentation-v50-negative-rotation-target-reset-v51-single-3d-exact-publication-v52-light-control-no-artwork-v53-axis-space-double-sided-v54-per-light-source-size-shadows-scene-depth-v55-complete-3d-point-shadows-v56-matrix-exact-rgb24-point-shadows-v57-float32-receiver-plane-point-shadows-v58-shared-point-cube-projection-v59-distance-stable-point-shadows-v60-pcss-contact-hardening-v61-shadow-runtime-recovery-v62-stable-multiscale-poisson-shadows-v63-restored-3d-shadow-visibility-v64-signed-point-shadow-clip-depth-v65-realtime-receiver-matched-point-pcf-v66-compile-safe-receiver-matched-point-pcf-v67-async-shader-compile-progress-v68-conservative-text-shadow-alpha-min-source-size-v69-explicit-planar-basis-text-footprint-v70-close-point-text-shadow-projection-grid-v71-native-chart-layer";

static bool gpu_ram_frame_contains(const CacheFrameKey &key)
{
    return title_gpu_frame_cache_contains(key.toString().toStdString());
}

static bool cache_ram_frame_contains(const RamFrameCache &cache,
                                     const CacheFrameKey &key)
{
    if (gpu_ram_frame_contains(key))
        return true;
    return cache.contains(key);
}

static uint64_t gpu_cache_model_revision(const CacheFrameKey &key)
{
    bool ok = false;
    uint64_t revision = key.content_hash.left(16).toULongLong(&ok, 16);
    if (!ok || revision == 0)
        revision = static_cast<uint64_t>(qHash(key.content_hash));
    revision ^= static_cast<uint64_t>(static_cast<uint32_t>(key.width)) << 32;
    revision ^= static_cast<uint64_t>(static_cast<uint32_t>(key.height));
    return revision == 0 ? 1 : revision;
}

static void set_sparse_frame_metadata(QImage &image, int x, int y,
                                      int canvas_width, int canvas_height)
{
    fxm::cache_frame_payload::set_placement(image, x, y,
                                            canvas_width, canvas_height);
}

static QRect alpha_bounds(const QImage &image)
{
    if (image.isNull())
        return QRect();
    const QImage argb = image.format() == QImage::Format_ARGB32
        ? image
        : image.convertToFormat(QImage::Format_ARGB32);
    int left = argb.width();
    int top = argb.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(y));
        int row_left = argb.width();
        int row_right = -1;
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) != 0) {
                row_left = x;
                break;
            }
        }
        if (row_left == argb.width())
            continue;
        for (int x = argb.width() - 1; x >= row_left; --x) {
            if (qAlpha(row[x]) != 0) {
                row_right = x;
                break;
            }
        }
        left = std::min(left, row_left);
        right = std::max(right, row_right);
        top = std::min(top, y);
        bottom = y;
    }
    return right >= left && bottom >= top
        ? QRect(left, top, right - left + 1, bottom - top + 1)
        : QRect();
}

static bool sparse_frame_metadata(const QImage &image, int &x, int &y,
                                  int &canvas_width, int &canvas_height)
{
    fxm::cache_frame_payload::Placement placement;
    if (!fxm::cache_frame_payload::read_placement(image, placement))
        return false;
    x = placement.x;
    y = placement.y;
    canvas_width = placement.canvas_width;
    canvas_height = placement.canvas_height;
    return true;
}

static QImage expand_sparse_frame(const QImage &image, int expected_width, int expected_height)
{
    int x = 0, y = 0, canvas_width = 0, canvas_height = 0;
    if (!sparse_frame_metadata(image, x, y, canvas_width, canvas_height)) {
        return image.width() == expected_width && image.height() == expected_height
            ? image
            : QImage();
    }
    if (canvas_width != expected_width || canvas_height != expected_height)
        return QImage();
    QImage full(canvas_width, canvas_height, kDiskFrameFormat);
    full.fill(Qt::transparent);
    const QImage source = image.format() == kDiskFrameFormat
        ? image : image.convertToFormat(kDiskFrameFormat);
    if (source.isNull() || x < 0 || y < 0 ||
        x + source.width() > full.width() ||
        y + source.height() > full.height())
        return QImage();
    const qsizetype row_bytes = qsizetype(source.width()) * 4;
    for (int row = 0; row < source.height(); ++row) {
        std::memcpy(full.scanLine(y + row) + qsizetype(x) * 4,
                    source.constScanLine(row), row_bytes);
    }
    return full;
}

static QImage make_sparse_frame(const QImage &full_frame, int canvas_width, int canvas_height)
{
    if (full_frame.isNull())
        return QImage();
    QImage straight = full_frame.format() == kDiskFrameFormat
        ? full_frame
        : full_frame.convertToFormat(kDiskFrameFormat);
    const QRect bounds = alpha_bounds(straight);
    if (bounds.isEmpty()) {
        QImage empty(1, 1, kDiskFrameFormat);
        empty.fill(Qt::transparent);
        set_sparse_frame_metadata(empty, 0, 0, canvas_width, canvas_height);
        return empty;
    }
    QImage cropped = straight.copy(bounds);
    set_sparse_frame_metadata(cropped, bounds.x(), bounds.y(), canvas_width, canvas_height);
    return cropped;
}

static quint64 cache_image_bytes(const QImage &image)
{
    return image.isNull() ? 0 : (quint64)image.bytesPerLine() * (quint64)image.height();
}

static void apply_persisted_live_cue_persistence(const std::shared_ptr<Title> &title)
{
    if (!title)
        return;

    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QStringLiteral("TitleDock"));
    const bool background = settings.value(QStringLiteral("backgroundPersistence"), false).toBool();
    const bool text = background && settings.value(QStringLiteral("textPersistence"), false).toBool();
    settings.endGroup();

    bool has_exposed = false;
    for (const auto &layer : title->layers) {
        if (layer && (layer->type == LayerType::Text || layer->type == LayerType::Ticker || layer->type == LayerType::Image) &&
            layer->expose_text) {
            has_exposed = true;
            break;
        }
    }

    title->cue_background_persistence = background && has_exposed;
    title->cue_text_persistence = title->cue_background_persistence && text;
    if (!title->cue_background_persistence)
        title->cue_persistence_transition = false;
    if (!title->cue_text_persistence)
        title->cue_persistent_text_columns.clear();
}

static double cache_cue_persistence_hold_time(const Title &title)
{
    if (title.playback_mode == 1)
        return std::clamp(title.loop_end, title.loop_start, title.duration);
    if (title.playback_mode == 2)
        return std::clamp(title.pause_time, 0.0, title.duration);
    return std::clamp(title.duration, 0.0, title.duration);
}

static bool cache_layer_uses_cue_persistence(const Title &title,
                                             const Layer &layer)
{
    const bool persistence = title.cue_background_persistence &&
        title.cue_persistence_transition && title.current_cue_row >= 0 &&
        !title.live_text_rows.empty() && !layer.ignore_persistence;
    if (!persistence)
        return false;

    const auto exposed = fxm::live_text::exposed_text_layers(title);
    int exposed_index = -1;
    for (std::size_t i = 0; i < exposed.size(); ++i) {
        if (exposed[i] && (exposed[i].get() == &layer ||
            (!layer.id.empty() && exposed[i]->id == layer.id))) {
            exposed_index = static_cast<int>(i);
            break;
        }
    }
    const bool persistent_text = exposed_index >= 0 && title.cue_text_persistence &&
        exposed_index < static_cast<int>(title.cue_persistent_text_columns.size()) &&
        title.cue_persistent_text_columns[exposed_index];
    return exposed_index < 0 || persistent_text;
}

static double cache_layer_sample_time(const Title &title, const Layer &layer,
                                      double title_time)
{
    return cache_layer_uses_cue_persistence(title, layer)
        ? cache_cue_persistence_hold_time(title)
        : title_time;
}

static void enter_cache_worker_background_mode()
{
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
#endif
}

static int clamp_cache_priority(qint64 priority)
{
    return static_cast<int>(std::clamp<qint64>(
        priority,
        std::numeric_limits<int>::min() + 1,
        std::numeric_limits<int>::max()));
}

static quint64 available_system_memory_bytes()
{
#ifdef _WIN32
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
        return static_cast<quint64>(status.ullAvailPhys);
#endif
    return std::numeric_limits<quint64>::max();
}
}



DiskFrameCache::DiskFrameCache(QObject *parent) : QObject(parent)
{
    cache_dir_ = TitlePreferences::cache_disk_location();
    QDir().mkpath(cache_dir_);
    rebuildIndex();
    bytes_used_ = scanBytesUsed();
    publishBytesUsedLocked();
    const QFileInfo active_info(cache_dir_);
    QDir cache_parent(active_info.absolutePath());
    const QString obsolete_pattern =
        active_info.fileName() + QStringLiteral(".obsolete-*");
    for (const QFileInfo &obsolete : cache_parent.entryInfoList(
             QStringList() << obsolete_pattern,
             QDir::Dirs | QDir::NoDotAndDotDot))
        cleanup_queue_.push_back(obsolete.absoluteFilePath());
    writer_thread_ = std::thread(&DiskFrameCache::writerLoop, this);
    if (!cleanup_queue_.empty())
        writer_cv_.notify_one();
}

DiskFrameCache::~DiskFrameCache()
{
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        writer_stop_ = true;
    }
    writer_cv_.notify_all();
    if (writer_thread_.joinable())
        writer_thread_.join();
}

bool DiskFrameCache::enqueuePut(const CacheFrameKey &key, const QImage &image)
{
    if (image.isNull())
        return false;
    WriteJob job;
    job.kind = WriteJob::Kind::Put;
    job.key = key;
    job.image = image;
    job.generation = writer_generation_.load(std::memory_order_acquire);
    job.title_generation = titleWriteGeneration(key.title_id);
    job.bytes = static_cast<quint64>(
        std::max<qsizetype>(0, image.sizeInBytes()));
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        /* Disk persistence is best-effort and must never apply backpressure to
         * the cache/render worker. When compression or storage cannot keep up,
         * retain the completed frame in GPU/RAM and skip this disk write rather
         * than blocking realtime playback, UI work, or urgent hydration jobs. */
        const std::size_t in_flight = writer_queue_.size() +
            (writer_active_ ? 1u : 0u);
        const bool byte_room = writer_pending_bytes_ == 0 ||
            (job.bytes <= writer_queue_budget_ &&
             writer_pending_bytes_ <= writer_queue_budget_ - job.bytes);
        if (writer_stop_ || in_flight >= writer_queue_limit_ || !byte_room)
            return false;
        writer_pending_bytes_ += job.bytes;
        writer_queue_.push_back(std::move(job));
    }
    writer_cv_.notify_one();
    return true;
}

void DiskFrameCache::enqueueAlias(
    const CacheFrameKey &key, const CacheFrameKey &canonical_key)
{
    if (key == canonical_key || key.title_id.isEmpty() ||
        canonical_key.title_id.isEmpty() || key.width <= 0 ||
        key.height <= 0 || key.width != canonical_key.width ||
        key.height != canonical_key.height)
        return;

    WriteJob job;
    job.kind = WriteJob::Kind::Alias;
    job.key = key;
    job.canonical_key = canonical_key;
    job.generation = writer_generation_.load(std::memory_order_acquire);
    job.title_generation = titleWriteGeneration(key.title_id);
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        const std::size_t in_flight = writer_queue_.size() +
            (writer_active_ ? 1u : 0u);
        if (writer_stop_ || in_flight >= writer_queue_limit_)
            return;
        writer_queue_.push_back(std::move(job));
    }
    writer_cv_.notify_one();
}

quint64 DiskFrameCache::titleWriteGeneration(const QString &title_id) const
{
    std::lock_guard<std::mutex> lock(writer_title_generation_mutex_);
    return writer_title_generations_.value(title_id, 0);
}

bool DiskFrameCache::writeJobCurrent(const WriteJob &job) const
{
    return job.generation == writer_generation_.load(std::memory_order_acquire) &&
           job.title_generation == titleWriteGeneration(job.key.title_id);
}

void DiskFrameCache::cancelTitleWrites(const QString &title_id)
{
    if (title_id.isEmpty())
        return;
    {
        std::lock_guard<std::mutex> generation_lock(
            writer_title_generation_mutex_);
        writer_title_generations_[title_id] =
            writer_title_generations_.value(title_id, 0) + 1;
    }
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        for (auto it = writer_queue_.begin(); it != writer_queue_.end();) {
            if (it->key.title_id == title_id) {
                writer_pending_bytes_ -= std::min(
                    writer_pending_bytes_, it->bytes);
                it = writer_queue_.erase(it);
            } else {
                ++it;
            }
        }
        if (!writer_active_ && writer_queue_.empty() && cleanup_queue_.empty())
            writer_idle_cv_.notify_all();
    }
}

void DiskFrameCache::flushWrites()
{
    std::unique_lock<std::mutex> lock(writer_mutex_);
    writer_idle_cv_.wait(lock, [this]() {
        return writer_queue_.empty() && cleanup_queue_.empty() &&
               !writer_active_;
    });
}

void DiskFrameCache::cancelQueuedWrites()
{
    writer_generation_.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> generation_lock(
            writer_title_generation_mutex_);
        writer_title_generations_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        for (const WriteJob &job : writer_queue_)
            writer_pending_bytes_ -= std::min(writer_pending_bytes_, job.bytes);
        writer_queue_.clear();
        if (!writer_active_)
            writer_idle_cv_.notify_all();
    }
}

void DiskFrameCache::writerLoop()
{
    for (;;) {
        WriteJob job;
        QString cleanup_dir;
        bool write_job = false;
        {
            std::unique_lock<std::mutex> lock(writer_mutex_);
            writer_cv_.wait(lock, [this]() {
                return writer_stop_ || !writer_queue_.empty() ||
                       !cleanup_queue_.empty();
            });
            if (writer_stop_ && writer_queue_.empty() &&
                cleanup_queue_.empty())
                break;
            if (!cleanup_queue_.empty()) {
                cleanup_dir = std::move(cleanup_queue_.front());
                cleanup_queue_.pop_front();
            } else {
                job = std::move(writer_queue_.front());
                writer_queue_.pop_front();
                write_job = true;
            }
            writer_active_ = true;
        }

        if (write_job) {
            if (job.kind == WriteJob::Kind::Alias)
                aliasForGeneration(job.key, job.canonical_key,
                                   job.generation, job.title_generation);
            else
                putForGeneration(job.key, job.image, job.generation,
                                 job.title_generation);
        }
        else if (!cleanup_dir.isEmpty())
            QDir(cleanup_dir).removeRecursively();

        {
            std::lock_guard<std::mutex> lock(writer_mutex_);
            if (write_job)
                writer_pending_bytes_ -= std::min(
                    writer_pending_bytes_, job.bytes);
            writer_active_ = false;
            if (writer_queue_.empty() && cleanup_queue_.empty())
                writer_idle_cv_.notify_all();
        }
    }

    {
        std::lock_guard<std::mutex> lock(writer_mutex_);
        writer_active_ = false;
        writer_idle_cv_.notify_all();
    }
}

QString DiskFrameCache::pathForKey(const CacheFrameKey &key) const
{
    const QByteArray digest = QCryptographicHash::hash(
        key.toString().toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(cache_dir_).filePath(
        QString::fromLatin1(digest) + QStringLiteral(".ogsf"));
}

QString DiskFrameCache::pathForTileDigest(const QByteArray &digest) const
{
    return QDir(cache_dir_).filePath(
        QString::fromLatin1(digest.toHex()) + QStringLiteral(".ogst"));
}

bool DiskFrameCache::contains(const CacheFrameKey &key) const
{
    /* Membership has its own short-lived read lock. Disk compression, manifest
     * writes and tile I/O continue under mutex_, but realtime/UI callers never
     * wait behind those operations merely to ask whether a key exists. */
    QReadLocker lock(&membership_mutex_);
    return indexed_membership_.contains(key.toString());
}

void DiskFrameCache::setIndexedMembershipLocked(const QString &indexed_key,
                                                 bool present)
{
    QWriteLocker lock(&membership_mutex_);
    if (present)
        indexed_membership_.insert(indexed_key);
    else
        indexed_membership_.remove(indexed_key);
}

void DiskFrameCache::rebuildIndexedMembershipLocked()
{
    QWriteLocker lock(&membership_mutex_);
    indexed_membership_.clear();
    indexed_membership_.reserve(indexed_keys_.size());
    for (auto it = indexed_keys_.cbegin(); it != indexed_keys_.cend(); ++it)
        indexed_membership_.insert(it.key());
}

void DiskFrameCache::publishBytesUsedLocked()
{
    bytes_used_snapshot_.store(bytes_used_, std::memory_order_release);
}

bool DiskFrameCache::readFrameTileRefsLocked(
    const CacheFrameKey &key, QVector<TileRef> &refs) const
{
    refs.clear();
    QFile file(pathForKey(key));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint16 version = 0;
    quint16 format = 0;
    quint32 canvas_width = 0;
    quint32 canvas_height = 0;
    quint32 tile_size = 0;
    quint32 tile_count = 0;
    stream >> magic >> version >> format
           >> canvas_width >> canvas_height >> tile_size >> tile_count;
    const quint64 max_tiles =
        quint64((std::max(1, key.width) + fxm::cache_tile_payload::kTileSize - 1) /
                fxm::cache_tile_payload::kTileSize) *
        quint64((std::max(1, key.height) + fxm::cache_tile_payload::kTileSize - 1) /
                fxm::cache_tile_payload::kTileSize);
    if (stream.status() != QDataStream::Ok ||
        magic != kRawFrameMagic || version != kRawFrameVersion ||
        format != quint16(kDiskFrameFormat) ||
        canvas_width != quint32(key.width) ||
        canvas_height != quint32(key.height) ||
        tile_size != quint32(fxm::cache_tile_payload::kTileSize) ||
        tile_count > max_tiles)
        return false;

    const QRect canvas_rect(0, 0, key.width, key.height);
    refs.reserve(int(tile_count));
    for (quint32 index = 0; index < tile_count; ++index) {
        qint32 x = 0;
        qint32 y = 0;
        quint32 width = 0;
        quint32 height = 0;
        QByteArray digest;
        stream >> x >> y >> width >> height >> digest;
        const QRect rect(x, y, int(width), int(height));
        if (stream.status() != QDataStream::Ok || digest.size() != 32 ||
            width == 0 || height == 0 ||
            width > quint32(fxm::cache_tile_payload::kTileSize) ||
            height > quint32(fxm::cache_tile_payload::kTileSize) ||
            !canvas_rect.contains(rect))
            return false;
        refs.push_back({rect, digest});
    }
    return stream.status() == QDataStream::Ok;
}

bool DiskFrameCache::readTileLocked(const TileRef &ref, QImage &image) const
{
    image = QImage();
    QFile file(pathForTileDigest(ref.digest));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0;
    quint16 version = 0;
    quint16 format = 0;
    quint32 width = 0;
    quint32 height = 0;
    quint32 row_bytes = 0;
    quint16 codec = 0;
    quint16 reserved = 0;
    quint32 raw_size = 0;
    quint32 payload_size = 0;
    stream >> magic >> version >> format >> width >> height >> row_bytes
           >> codec >> reserved >> raw_size >> payload_size;
    const quint64 expected_row_bytes = quint64(ref.rect.width()) * 4ull;
    const quint64 expected_raw_size =
        expected_row_bytes * quint64(ref.rect.height());
    if (stream.status() != QDataStream::Ok ||
        magic != kTilePayloadMagic || version != kTilePayloadVersion ||
        format != quint16(kDiskFrameFormat) ||
        width != quint32(ref.rect.width()) ||
        height != quint32(ref.rect.height()) ||
        row_bytes != expected_row_bytes || raw_size != expected_raw_size ||
        reserved != 0 ||
        raw_size > quint32(std::numeric_limits<int>::max()) ||
        payload_size > quint32(std::numeric_limits<int>::max()) ||
        (codec != kFrameCodecRaw && codec != kFrameCodecLz4))
        return false;

    const QByteArray payload = file.read(payload_size);
    if (payload.size() != int(payload_size))
        return false;
    QByteArray raw;
    if (codec == kFrameCodecRaw) {
        raw = payload;
    } else {
#ifdef OBS_FXM_HAVE_LZ4
        raw.resize(int(raw_size));
        const int decoded = LZ4_decompress_safe(
            payload.constData(), raw.data(), int(payload.size()), int(raw.size()));
        if (decoded != raw.size())
            return false;
#else
        return false;
#endif
    }
    if (raw.size() != int(raw_size))
        return false;

    QImage loaded(ref.rect.size(), kDiskFrameFormat);
    if (loaded.isNull())
        return false;
    for (int y = 0; y < loaded.height(); ++y) {
        std::memcpy(loaded.scanLine(y),
                    raw.constData() + qsizetype(y) * row_bytes,
                    size_t(row_bytes));
    }
    if (fxm::cache_tile_payload::digest_for_tile(loaded) != ref.digest)
        return false;
    image = std::move(loaded);
    return true;
}

bool DiskFrameCache::get(const CacheFrameKey &key, QImage &image) const
{
    QMutexLocker lock(&mutex_);
    if (!indexed_keys_.contains(key.toString()))
        return false;

    QVector<TileRef> refs;
    if (!readFrameTileRefsLocked(key, refs))
        return false;

    QVector<fxm::cache_tile_payload::Tile> tiles;
    tiles.reserve(refs.size());
    for (const TileRef &ref : refs) {
        QImage tile_image;
        if (!readTileLocked(ref, tile_image))
            return false;
        tiles.push_back({ref.rect, std::move(tile_image), ref.digest});
    }
    image = fxm::cache_tile_payload::compose_sparse_tiles(
        tiles, key.width, key.height);
    return !image.isNull();
}

void DiskFrameCache::put(const CacheFrameKey &key, const QImage &image)
{
    if (image.isNull())
        return;
    QMutexLocker lock(&mutex_);
    putLocked(key, image);
}

void DiskFrameCache::putForGeneration(const CacheFrameKey &key,
                                      const QImage &image,
                                      quint64 generation,
                                      quint64 title_generation)
{
    if (image.isNull())
        return;
    QMutexLocker lock(&mutex_);
    /* Re-check after acquiring the disk-cache lock. Global cache rotations and
     * per-title deletion generations prevent an already-dequeued write from
     * recreating a removed proxy. */
    WriteJob identity;
    identity.key = key;
    identity.generation = generation;
    identity.title_generation = title_generation;
    if (!writeJobCurrent(identity))
        return;
    putLocked(key, image);
}

void DiskFrameCache::aliasForGeneration(
    const CacheFrameKey &key, const CacheFrameKey &canonical_key,
    quint64 generation, quint64 title_generation)
{
    QMutexLocker lock(&mutex_);
    WriteJob identity;
    identity.key = key;
    identity.generation = generation;
    identity.title_generation = title_generation;
    if (!writeJobCurrent(identity))
        return;
    aliasLocked(key, canonical_key);
}

bool DiskFrameCache::writeTileLocked(const QImage &image,
                                     const QByteArray &digest,
                                     quint64 &created_bytes)
{
    if (image.isNull() || digest.size() != 32)
        return false;
    const QString path = pathForTileDigest(digest);
    if (QFileInfo::exists(path)) {
        TileRef existing_ref;
        existing_ref.rect = QRect(QPoint(0, 0), image.size());
        existing_ref.digest = digest;
        QImage existing_image;
        if (readTileLocked(existing_ref, existing_image))
            return true;
        const quint64 corrupt_size = quint64(QFileInfo(path).size());
        if (QFile::remove(path))
            bytes_used_ -= std::min(bytes_used_, corrupt_size);
    }

    const QImage frame = image.format() == kDiskFrameFormat
        ? image : image.convertToFormat(kDiskFrameFormat);
    const QByteArray raw = fxm::cache_tile_payload::raw_bgra_bytes(frame);
    if (raw.isEmpty() || raw.size() > std::numeric_limits<int>::max())
        return false;

    QByteArray payload = raw;
    quint16 codec = kFrameCodecRaw;
#ifdef OBS_FXM_HAVE_LZ4
    if (raw.size() <= LZ4_MAX_INPUT_SIZE) {
        QByteArray compressed;
        const int raw_size = int(raw.size());
        compressed.resize(LZ4_compressBound(raw_size));
        const int compressed_size = LZ4_compress_default(
            raw.constData(), compressed.data(), raw_size,
            int(compressed.size()));
        if (compressed_size > 0 && compressed_size < raw_size) {
            compressed.resize(compressed_size);
            payload = std::move(compressed);
            codec = kFrameCodecLz4;
        }
    }
#endif

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kTilePayloadMagic << kTilePayloadVersion
           << quint16(kDiskFrameFormat)
           << quint32(frame.width()) << quint32(frame.height())
           << quint32(frame.width() * 4)
           << codec << quint16(0)
           << quint32(raw.size()) << quint32(payload.size());
    if (stream.status() != QDataStream::Ok ||
        file.write(payload) != payload.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return false;
    created_bytes += quint64(QFileInfo(path).size());
    return true;
}

void DiskFrameCache::addTileReferencesLocked(
    const QVector<QByteArray> &digests)
{
    for (const QByteArray &digest : digests)
        tile_ref_counts_[digest] = tile_ref_counts_.value(digest) + 1;
}

void DiskFrameCache::releaseTileReferencesLocked(
    const QVector<QByteArray> &digests)
{
    for (const QByteArray &digest : digests) {
        const qsizetype remaining = tile_ref_counts_.value(digest) - 1;
        if (remaining > 0) {
            tile_ref_counts_[digest] = remaining;
            continue;
        }
        tile_ref_counts_.remove(digest);
        const QString path = pathForTileDigest(digest);
        const quint64 size = QFileInfo(path).exists()
            ? quint64(QFileInfo(path).size()) : 0;
        if (QFile::remove(path))
            bytes_used_ -= std::min(bytes_used_, size);
    }
}

void DiskFrameCache::putLocked(const CacheFrameKey &key, const QImage &image)
{
    QDir().mkpath(cache_dir_);
    const QImage frame = image.format() == kDiskFrameFormat
        ? image : image.convertToFormat(kDiskFrameFormat);
    fxm::cache_frame_payload::Placement placement;
    if (!fxm::cache_frame_payload::read_placement(frame, placement) ||
        placement.canvas_width != key.width ||
        placement.canvas_height != key.height)
        return;

    const QVector<fxm::cache_tile_payload::Tile> tiles =
        fxm::cache_tile_payload::extract_nonempty_tiles(frame, placement);
    quint64 created_tile_bytes = 0;
    QVector<QByteArray> created_tile_digests;
    QVector<QByteArray> new_digests;
    new_digests.reserve(tiles.size());
    const auto discard_unreferenced_created_tiles = [&]() {
        for (const QByteArray &created : created_tile_digests) {
            if (!tile_ref_counts_.contains(created))
                QFile::remove(pathForTileDigest(created));
        }
    };
    for (const auto &tile : tiles) {
        const quint64 created_before = created_tile_bytes;
        if (!writeTileLocked(tile.image, tile.digest, created_tile_bytes)) {
            discard_unreferenced_created_tiles();
            return;
        }
        if (created_tile_bytes > created_before &&
            !tile_ref_counts_.contains(tile.digest))
            created_tile_digests.push_back(tile.digest);
        new_digests.push_back(tile.digest);
    }

    const QString indexed_key = key.toString();
    const bool already_indexed = indexed_keys_.contains(indexed_key);
    const QVector<QByteArray> previous_digests =
        frame_tile_digests_.value(indexed_key);
    const QString path = pathForKey(key);
    const quint64 previous_size = QFileInfo(path).exists()
        ? quint64(QFileInfo(path).size()) : 0;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        discard_unreferenced_created_tiles();
        return;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kRawFrameMagic << kRawFrameVersion
           << quint16(kDiskFrameFormat)
           << quint32(key.width) << quint32(key.height)
           << quint32(fxm::cache_tile_payload::kTileSize)
           << quint32(tiles.size());
    for (const auto &tile : tiles) {
        stream << qint32(tile.rect.x()) << qint32(tile.rect.y())
               << quint32(tile.rect.width()) << quint32(tile.rect.height())
               << tile.digest;
    }
    if (stream.status() != QDataStream::Ok) {
        file.cancelWriting();
        discard_unreferenced_created_tiles();
        return;
    }
    if (!file.commit()) {
        discard_unreferenced_created_tiles();
        return;
    }

    const quint64 new_frame_size = quint64(QFileInfo(path).size());
    bytes_used_ = bytes_used_ - std::min(bytes_used_, previous_size) +
                  new_frame_size + created_tile_bytes;
    addTileReferencesLocked(new_digests);
    releaseTileReferencesLocked(previous_digests);
    frame_tile_digests_[indexed_key] = new_digests;
    indexed_keys_[indexed_key] = key;
    setIndexedMembershipLocked(indexed_key, true);
    publishBytesUsedLocked();
    if (!already_indexed)
        appendManifestEntry(key);
}

bool DiskFrameCache::aliasLocked(
    const CacheFrameKey &key, const CacheFrameKey &canonical_key)
{
    if (key == canonical_key)
        return indexed_keys_.contains(key.toString());
    if (key.width <= 0 || key.height <= 0 ||
        key.width != canonical_key.width ||
        key.height != canonical_key.height ||
        !indexed_keys_.contains(canonical_key.toString()))
        return false;

    QVector<TileRef> refs;
    if (!readFrameTileRefsLocked(canonical_key, refs))
        return false;

    QVector<QByteArray> new_digests;
    new_digests.reserve(refs.size());
    for (const TileRef &ref : refs) {
        if (!QFileInfo::exists(pathForTileDigest(ref.digest)))
            return false;
        new_digests.push_back(ref.digest);
    }

    QDir().mkpath(cache_dir_);
    const QString indexed_key = key.toString();
    const bool already_indexed = indexed_keys_.contains(indexed_key);
    const QVector<QByteArray> previous_digests =
        frame_tile_digests_.value(indexed_key);
    const QString path = pathForKey(key);
    const quint64 previous_size = QFileInfo(path).exists()
        ? quint64(QFileInfo(path).size()) : 0;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_5_15);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kRawFrameMagic << kRawFrameVersion
           << quint16(kDiskFrameFormat)
           << quint32(key.width) << quint32(key.height)
           << quint32(fxm::cache_tile_payload::kTileSize)
           << quint32(refs.size());
    for (const TileRef &ref : refs) {
        stream << qint32(ref.rect.x()) << qint32(ref.rect.y())
               << quint32(ref.rect.width()) << quint32(ref.rect.height())
               << ref.digest;
    }
    if (stream.status() != QDataStream::Ok) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return false;

    const quint64 new_frame_size = quint64(QFileInfo(path).size());
    bytes_used_ = bytes_used_ - std::min(bytes_used_, previous_size) +
                  new_frame_size;
    addTileReferencesLocked(new_digests);
    releaseTileReferencesLocked(previous_digests);
    frame_tile_digests_[indexed_key] = new_digests;
    indexed_keys_[indexed_key] = key;
    setIndexedMembershipLocked(indexed_key, true);
    publishBytesUsedLocked();
    if (!already_indexed)
        appendManifestEntry(key);
    return true;
}

QVector<CacheFrameKey> DiskFrameCache::keysForTitle(const QString &title_id) const
{
    QMutexLocker lock(&mutex_);
    QVector<CacheFrameKey> keys;
    keys.reserve(indexed_keys_.size());
    for (auto it = indexed_keys_.cbegin(); it != indexed_keys_.cend(); ++it) {
        if (it.value().title_id == title_id)
            keys.push_back(it.value());
    }
    return keys;
}

void DiskFrameCache::remove(const CacheFrameKey &key)
{
    QMutexLocker lock(&mutex_);
    const QString indexed_key = key.toString();
    const QVector<QByteArray> digests =
        frame_tile_digests_.take(indexed_key);
    const QString path = pathForKey(key);
    const quint64 previous_size = QFileInfo(path).exists() ? (quint64)QFileInfo(path).size() : 0;
    if (QFile::remove(path))
        bytes_used_ -= std::min(bytes_used_, previous_size);
    indexed_keys_.remove(indexed_key);
    setIndexedMembershipLocked(indexed_key, false);
    releaseTileReferencesLocked(digests);
    publishBytesUsedLocked();
}

void DiskFrameCache::clear()
{
    cancelQueuedWrites();
    QMutexLocker lock(&mutex_);
    QDir dir(cache_dir_);
    if (!dir.exists()) {
        indexed_keys_.clear();
        frame_tile_digests_.clear();
        tile_ref_counts_.clear();
        bytes_used_ = 0;
        rebuildIndexedMembershipLocked();
        publishBytesUsedLocked();
        return;
    }
    for (const QFileInfo &file : dir.entryInfoList(
             QStringList() << QStringLiteral("*.ogsf")
                           << QStringLiteral("*.ogst")
                           << QStringLiteral("*.tmp"), QDir::Files))
        QFile::remove(file.absoluteFilePath());
    QFile::remove(manifestPath());
    indexed_keys_.clear();
    frame_tile_digests_.clear();
    tile_ref_counts_.clear();
    bytes_used_ = 0;
    rebuildIndexedMembershipLocked();
    publishBytesUsedLocked();
    FXM_LOG_INFO("DiskCache", QStringLiteral("Cleared disk frame cache directory=%1")
        .arg(cache_dir_));
}

void DiskFrameCache::clearFast()
{
    cancelQueuedWrites();
    QMutexLocker lock(&mutex_);
    const QString old_dir = cache_dir_;
    const QString tombstone = old_dir + QStringLiteral(".obsolete-%1")
        .arg(QDateTime::currentMSecsSinceEpoch());

    QDir old(old_dir);
    if (old.exists()) {
        QDir parent(QFileInfo(old_dir).absolutePath());
        parent.rename(QFileInfo(old_dir).fileName(), QFileInfo(tombstone).fileName());
    }
    QDir().mkpath(old_dir);
    indexed_keys_.clear();
    frame_tile_digests_.clear();
    tile_ref_counts_.clear();
    bytes_used_ = 0;
    rebuildIndexedMembershipLocked();
    publishBytesUsedLocked();
    FXM_LOG_INFO("DiskCache", QStringLiteral(
        "Detached disk cache generation directory=%1 tombstone=%2")
        .arg(old_dir, tombstone));

    /* The active generation is detached synchronously, then reclaimed by the
     * owned writer thread. Unlike a detached cleanup thread, this worker is
     * joined during shutdown, so obsolete cache generations cannot accumulate
     * indefinitely or outlive Qt/OBS. */
    if (QDir(tombstone).exists()) {
        {
            std::lock_guard<std::mutex> writer_lock(writer_mutex_);
            cleanup_queue_.push_back(tombstone);
        }
        writer_cv_.notify_one();
    }
}

void DiskFrameCache::setCacheDirectory(const QString &path)
{
    cancelQueuedWrites();
    QMutexLocker lock(&mutex_);
    const QString clean = QDir::cleanPath(path);
    if (clean.isEmpty() || clean == cache_dir_)
        return;
    cache_dir_ = clean;
    QDir().mkpath(cache_dir_);
    rebuildIndex();
    bytes_used_ = scanBytesUsed();
    publishBytesUsedLocked();
    FXM_LOG_INFO("DiskCache", QStringLiteral(
        "Set disk cache directory=%1 indexedFrames=%2 bytes=%3")
        .arg(cache_dir_)
        .arg(indexed_keys_.size())
        .arg(bytes_used_));
}

QString DiskFrameCache::cacheDirectory() const
{
    QMutexLocker lock(&mutex_);
    return cache_dir_;
}

quint64 DiskFrameCache::bytesUsed() const
{
    return bytes_used_snapshot_.load(std::memory_order_acquire);
}


void DiskFrameCache::rebuildIndex()
{
    indexed_keys_.clear();
    rebuildIndexedMembershipLocked();
    frame_tile_digests_.clear();
    tile_ref_counts_.clear();
    QSet<QString> indexed_frame_files;
    QFile manifest(manifestPath());
    if (!manifest.open(QIODevice::ReadOnly)) {
        QDir dir(cache_dir_);
        for (const QFileInfo &file : dir.entryInfoList(
                 QStringList() << QStringLiteral("*.ogsf")
                               << QStringLiteral("*.ogst")
                               << QStringLiteral("*.tmp"), QDir::Files))
            QFile::remove(file.absoluteFilePath());
        return;
    }
