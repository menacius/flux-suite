#pragma once

#include "title-data.h"

#include <QObject>
#include <QByteArray>
#include <QImage>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QDateTime>
#include <QReadWriteLock>
#include <QRect>
#include <QVector>

#include <memory>
#include <functional>
#include <list>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

enum class FrameCacheState {
    NotCached,
    Queued,
    Rendering,
    CachedRam,
    CachedDisk,
    Stale,
    Disabled
};

enum class TitleCacheability {
    Cacheable,
    PartiallyCacheable,
    NonCacheable
};

enum class CachePlaybackMode {
    Loop,
    PingPong,
    PlayOnce
};

struct CachePlaybackSettings {
    bool from_beginning = false;
    CachePlaybackMode mode = CachePlaybackMode::Loop;
    bool follow_title_playback_mode = false;
    bool cached_frames_only = false;
    bool play_every_frame = false;
    int skip_frames = 0;
    double speed_percent = 100.0;
};

struct LiveCueCacheStats {
    quint64 hits = 0;
    quint64 misses = 0;
    quint64 reuses = 0;
    quint64 invalidations = 0;
};

struct CacheFrameKey {
    QString title_id;
    QString content_hash;
    int frame = 0;
    int width = 0;
    int height = 0;

    QString toString() const;
    bool operator==(const CacheFrameKey &other) const;
};

uint qHash(const CacheFrameKey &key, uint seed = 0);

struct CacheTileRegion {
    int tile_x = 0;
    int tile_y = 0;
    QRect rect;
};

class RamFrameCache : public QObject {
    Q_OBJECT
public:
    explicit RamFrameCache(QObject *parent = nullptr);
    bool contains(const CacheFrameKey &key) const;
    bool get(const CacheFrameKey &key, QImage &image) const;
    void put(const CacheFrameKey &key, const QImage &image);
    void remove(const CacheFrameKey &key);
    void clear();
    void setMaxBytes(quint64 bytes);
    quint64 maxBytes() const;
    quint64 bytesUsed() const;
    qsizetype count() const;
    QVector<CacheFrameKey> keysForTitle(const QString &title_id) const;

private:
    struct PayloadEntry {
        QImage image;
        quint64 bytes = 0;
        qsizetype references = 0;
    };

    quint64 imageBytes(const QImage &image) const;
    quint64 payloadId(const QImage &image) const;
    void releaseKeyLocked(const CacheFrameKey &key);
    void evictIfNeeded();
    mutable QMutex mutex_;
    QHash<CacheFrameKey, quint64> key_payload_ids_;
    QHash<quint64, PayloadEntry> payloads_;
    void touchKeyLocked(const CacheFrameKey &key);
    std::list<CacheFrameKey> lru_;
    QHash<CacheFrameKey, std::list<CacheFrameKey>::iterator> lru_positions_;
    quint64 max_bytes_ = 512ull * 1024ull * 1024ull;
    quint64 bytes_used_ = 0;
};

class DiskFrameCache : public QObject {
    Q_OBJECT
public:
    explicit DiskFrameCache(QObject *parent = nullptr);
    ~DiskFrameCache() override;
    bool contains(const CacheFrameKey &key) const;
    bool get(const CacheFrameKey &key, QImage &image) const;
    void put(const CacheFrameKey &key, const QImage &image);
    bool enqueuePut(const CacheFrameKey &key, const QImage &image);
    void enqueueAlias(const CacheFrameKey &key,
                      const CacheFrameKey &canonical_key);
    void cancelTitleWrites(const QString &title_id);
    void flushWrites();
    QVector<CacheFrameKey> keysForTitle(const QString &title_id) const;
    void remove(const CacheFrameKey &key);
    void clear();
    void clearFast();
    void setCacheDirectory(const QString &path);
    QString cacheDirectory() const;
    quint64 bytesUsed() const;

private:
    struct WriteJob;
    struct TileRef {
        QRect rect;
        QByteArray digest;
    };
    QString pathForKey(const CacheFrameKey &key) const;
    QString pathForTileDigest(const QByteArray &digest) const;
    QString manifestPath() const;
    void appendManifestEntry(const CacheFrameKey &key) const;
    void rewriteManifestLocked() const;
    void rebuildIndex();
    quint64 scanBytesUsed() const;
    void writerLoop();
    void cancelQueuedWrites();
    void putForGeneration(const CacheFrameKey &key, const QImage &image,
                          quint64 generation, quint64 title_generation);
    void aliasForGeneration(const CacheFrameKey &key,
                            const CacheFrameKey &canonical_key,
                            quint64 generation, quint64 title_generation);
    quint64 titleWriteGeneration(const QString &title_id) const;
    bool writeJobCurrent(const WriteJob &job) const;
    void putLocked(const CacheFrameKey &key, const QImage &image);
    bool aliasLocked(const CacheFrameKey &key,
                     const CacheFrameKey &canonical_key);
    bool readFrameTileRefsLocked(const CacheFrameKey &key,
                                 QVector<TileRef> &refs) const;
    bool readTileLocked(const TileRef &ref, QImage &image) const;
    bool writeTileLocked(const QImage &image, const QByteArray &digest,
                         quint64 &created_bytes);
    void setIndexedMembershipLocked(const QString &indexed_key, bool present);
    void rebuildIndexedMembershipLocked();
    void publishBytesUsedLocked();
    void addTileReferencesLocked(const QVector<QByteArray> &digests);
    void releaseTileReferencesLocked(const QVector<QByteArray> &digests);
    struct WriteJob {
        enum class Kind { Put, Alias };
        Kind kind = Kind::Put;
        CacheFrameKey key;
        CacheFrameKey canonical_key;
        QImage image;
        quint64 generation = 0;
        quint64 title_generation = 0;
        quint64 bytes = 0;
    };
    mutable QMutex mutex_;
    QString cache_dir_;
    QHash<QString, CacheFrameKey> indexed_keys_;
    mutable QReadWriteLock membership_mutex_;
    QSet<QString> indexed_membership_;
    QHash<QString, QVector<QByteArray>> frame_tile_digests_;
    QHash<QByteArray, qsizetype> tile_ref_counts_;
    quint64 bytes_used_ = 0;
    std::atomic<quint64> bytes_used_snapshot_{0};
    std::mutex writer_mutex_;
    std::condition_variable writer_cv_;
    std::condition_variable writer_idle_cv_;
    std::deque<WriteJob> writer_queue_;
    std::deque<QString> cleanup_queue_;
    std::thread writer_thread_;
    bool writer_stop_ = false;
    bool writer_active_ = false;
    quint64 writer_pending_bytes_ = 0;
    quint64 writer_queue_budget_ = 128ull * 1024ull * 1024ull;
    std::size_t writer_queue_limit_ = 8;
    std::atomic<quint64> writer_generation_{1};
    mutable std::mutex writer_title_generation_mutex_;
    QHash<QString, quint64> writer_title_generations_;
};

class CacheStateTracker : public QObject {
    Q_OBJECT
public:
    explicit CacheStateTracker(QObject *parent = nullptr);
    FrameCacheState state(const CacheFrameKey &key) const;
    FrameCacheState stateForFrame(const QString &title_id, int frame) const;
    void setState(const CacheFrameKey &key, FrameCacheState state);
    void markRange(const QString &title_id, int first_frame, int last_frame, FrameCacheState state);
    void clearTitle(const QString &title_id);
    void resetTransient(const QString &title_id = QString());
    void clear();
    QHash<int, FrameCacheState> titleStates(const QString &title_id) const;
    QHash<int, FrameCacheState> statesForRange(
        const QString &title_id, int first_frame, int last_frame) const;

signals:
    void stateChanged(const QString &title_id, int first_frame, int last_frame);

private:
    void rebuildTitleIndexLocked(const QString &title_id);
    mutable QMutex mutex_;
    QHash<CacheFrameKey, FrameCacheState> states_;
    QHash<QString, QHash<int, FrameCacheState>> frame_states_;
};

class RenderQueueManager : public QObject {
    Q_OBJECT
public:
    enum class PriorityBand {
        Visible = 0,
        AfterCurrent = 1,
        BeforeCurrent = 2,
        WorkArea = 3,
        FullTimeline = 4,
        LiveCue = 5
    };

    struct Job {
        CacheFrameKey key;
        std::shared_ptr<Title> title;
        double time = 0.0;
        int priority = 0;
        bool live_cue = false;
        bool realtime = false;
        // Explicit on-air/playback urgency. Background live-cue prerender jobs
        // must not outrank an open editor merely because they are live-cue variants.
        bool urgent = false;
        bool force_render = false;
        int cue_row = -1;
        QString cue_state_key;
        quint64 cache_epoch = 0;
        quint64 title_generation = 0;
        int retry_count = 0;
    };

    explicit RenderQueueManager(QObject *parent = nullptr);
    bool enqueue(const Job &job);
    int enqueueMany(const QVector<Job> &jobs);
    void setAcceptingJobs(bool accepting);
    void cancelTitle(const QString &title_id);
    void cancelRange(const QString &title_id, int first_frame, int last_frame);
    bool cancelKey(const CacheFrameKey &key);
    void reprioritizeAround(const QString &title_id, int current_frame);
    bool takeNext(Job &job);
    bool takeNextUrgent(Job &job);
    bool takeNextForTitle(const QString &title_id, Job &job);
    bool contains(const CacheFrameKey &key) const;
    int queuedCount() const;
    bool hasAvailableJob(bool live_cue_only) const;
    bool hasAvailableJobForTitle(const QString &title_id) const;
    void complete(const Job &job);
    void clear();

signals:
    void queueChanged();

private:
    void rebuildQueuedIndicesLocked();
    mutable QMutex mutex_;
    QVector<Job> jobs_;
    QSet<QString> queued_keys_;
    QHash<QString, int> queued_indices_;
    bool order_dirty_ = false;
    QHash<QString, QString> active_tokens_;
    bool accepting_jobs_ = true;
};

class CacheInvalidationManager : public QObject {
    Q_OBJECT
public:
    explicit CacheInvalidationManager(QObject *parent = nullptr);
    void invalidateAll(const std::shared_ptr<Title> &title);
    void invalidateRange(const std::shared_ptr<Title> &title, double start, double end);
    void invalidateLayer(const std::shared_ptr<Title> &title, const std::string &layer_id);

signals:
    void rangeInvalidated(const QString &title_id, int first_frame, int last_frame);

private:
    int frameForTime(double t) const;
};

class CacheManager : public QObject {
    Q_OBJECT
public:
    static CacheManager &instance();

    QImage requestFrame(const std::shared_ptr<Title> &title, double time, bool cached_only = false);
    QString requestFrameGpuToken(const std::shared_ptr<Title> &title, double time,
                                 bool queue_if_missing = true,
                                 const QString &known_content_hash = QString());
    QImage requestFrameRealtime(const std::shared_ptr<Title> &title, double time,
                                const QString &known_content_hash = QString());
    void rejectFramePayload(const std::shared_ptr<Title> &title, double time,
                            const QString &known_content_hash = QString());
    QString contentHashForTitle(const Title &title) const;
    bool visualStateCurrent(const Title &title) const;
    void queueFrame(const std::shared_ptr<Title> &title, double time, RenderQueueManager::PriorityBand band);
    void queueRange(const std::shared_ptr<Title> &title, double start, double end, RenderQueueManager::PriorityBand band);
    void queueWholeTimeline(const std::shared_ptr<Title> &title);
    void queueWorkArea(const std::shared_ptr<Title> &title);
    void reprioritize(const std::shared_ptr<Title> &title, double current_time);
    void restoreDiskStates(const std::shared_ptr<Title> &title);
    bool titleHasTimelineChanges(const Title &title) const;
    FrameCacheState displayStateForFrame(const std::shared_ptr<Title> &title, int frame) const;
    QHash<int, FrameCacheState> displayStatesForRange(
        const std::shared_ptr<Title> &title, int first_frame, int last_frame) const;
    QHash<int, bool> displayStaticFramesForRange(
        const std::shared_ptr<Title> &title, int first_frame, int last_frame) const;
    bool frameReadyForPlayback(const std::shared_ptr<Title> &title, double time) const;
    bool titleFullyReadyForPlayback(const std::shared_ptr<Title> &title) const;
    bool displayFrameIsStatic(const std::shared_ptr<Title> &title, int frame) const;

    void clearRam();
    void clearDisk();
    void clearAll();
    void shutdownWorker();
    void pausePrerender();
    void resumePrerender();
    bool prerenderPaused() const { return paused_.load(); }
    bool cacheEnabled() const { return cache_enabled_.load(); }
    void setCacheEnabled(bool enabled);
    void setInteractiveBypass(bool bypass);
    void setEditorPrerenderFocus(const QString &title_id, bool active);
    void setRamCacheLimitMb(int megabytes);
    void setDiskCacheLocation(const QString &path);
    quint64 ramBytesUsed() const;
    quint64 ramBytesLimit() const { return ram_cache_.maxBytes(); }
    quint64 diskBytesUsed() const { return disk_cache_.bytesUsed(); }
    QString diskCacheLocation() const { return disk_cache_.cacheDirectory(); }
    TitleCacheability titleCacheability(const std::shared_ptr<Title> &title) const;
    QString titleCacheabilityMessage(const std::shared_ptr<Title> &title) const;

    void invalidateAll(const std::shared_ptr<Title> &title);
    void invalidateRange(const std::shared_ptr<Title> &title, double start, double end);
    void invalidateLayer(const std::shared_ptr<Title> &title, const std::string &layer_id);

    void queueLiveCue(const std::shared_ptr<Title> &title, int row, bool urgent = false);
    void cacheLiveCueNow(const std::shared_ptr<Title> &title, int row);
    QImage requestLiveCueFrame(const std::shared_ptr<Title> &title, int row, double time, bool queue_if_missing = true);
    QString requestLiveCueFrameGpuToken(const std::shared_ptr<Title> &title, int row,
                                        double time, bool queue_if_missing = true);
    QImage requestLiveCueFrameRealtime(const std::shared_ptr<Title> &title, int row, double time,
                                       const QString &known_content_hash = QString());
    QImage requestLiveCueFrame(const std::shared_ptr<Title> &title, int row, bool queue_if_missing = true);
    void preloadLiveCues(const std::shared_ptr<Title> &title, int current_row, int nearby_count);
    FrameCacheState liveCueState(const std::shared_ptr<Title> &title, int row) const;
    int liveCueProgressPercent(const std::shared_ptr<Title> &title, int row) const;
    bool isLiveCueReady(const std::shared_ptr<Title> &title, int row);
    // Playback readiness is stricter than badge readiness. Normally the first
    // reachable frames are promoted to RAM before start; under memory pressure
    // SSD-resident frames are accepted and RAM is used only as a small buffer.
    bool prepareLiveCueForPlayback(const std::shared_ptr<Title> &title, int row);
    FrameCacheState liveCueAggregateState(const std::shared_ptr<Title> &title) const;
    int liveCueAggregateProgressPercent(const std::shared_ptr<Title> &title) const;
    void removeTitleCache(const QString &title_id, bool remove_disk = false);
    void cancelTitleWork(const QString &title_id);
    void invalidateLiveCue(const std::shared_ptr<Title> &title, int row);
    void invalidateLiveCues(const std::shared_ptr<Title> &title);
    void refreshLiveCueStructure(const std::shared_ptr<Title> &title);
    void refreshLiveCueStructureAsync(const std::shared_ptr<Title> &title);
    void setLiveCueRowRenderPaused(const std::shared_ptr<Title> &title, int row, bool paused);
    LiveCueCacheStats liveCueStats() const;

    CacheStateTracker *stateTracker() { return &state_tracker_; }
    CachePlaybackSettings playbackSettings() const;
    void setPlaybackSettings(const CachePlaybackSettings &settings);
    double effectiveFrameRate() const;
    int frameIndexForTitleTime(const Title &title, double time) const;

signals:
    void frameReady(const QString &title_id, int frame);
    void cacheStatesChanged(const QString &title_id, int first_frame, int last_frame);
    void liveCueStateChanged(const QString &title_id, int row);
    void queueChanged();
    void cacheEnabledChanged(bool enabled);
    void playbackSettingsChanged();
    void diagnosticsChanged();

private:
    explicit CacheManager(QObject *parent = nullptr);
    ~CacheManager() override;
    CacheFrameKey keyForFrame(const Title &title, int frame) const;
    CacheFrameKey keyForTime(const Title &title, double time) const;
    CacheFrameKey keyForTime(const Title &title, double time, const QString &known_content_hash) const;
    QVector<CacheFrameKey> frameKeysForTitle(const Title &title) const;
    QString contentHash(const Title &title) const;
    QString evaluatedVisualStateHash(const Title &title, double time, const QString &known_content_hash) const;
    QString adaptiveVisualStateHash(const Title &title, double time, const QString &known_content_hash) const;
    QString temporalStateKey(const CacheFrameKey &key, const QString &visual_state_hash) const;
    QVector<CacheTileRegion> tilesForRect(const QRect &rect, const QSize &frame_size) const;
    QRect layerDirtyRect(const Title &title, const Layer &layer) const;
    QString tileStateKey(const QString &title_id, int frame) const;
    void markDirtyTiles(const QString &title_id, int first_frame, int last_frame, const QVector<CacheTileRegion> &tiles);
    QVector<CacheTileRegion> dirtyTilesForKey(const CacheFrameKey &key) const;
    void rememberVisualHash(const Title &title, const QString &known_hash = QString());
    bool visualHashUnchanged(const Title &title) const;
    std::shared_ptr<Title> titleWithCueApplied(const std::shared_ptr<Title> &title, int row) const;
    QVector<std::shared_ptr<Title>> liveCueVariants(const std::shared_ptr<Title> &title, int row) const;
    QVector<std::shared_ptr<Title>> liveCueTransitionVariants(const std::shared_ptr<Title> &title, int from_row, int to_row) const;
    void queueLiveCueTransition(const std::shared_ptr<Title> &title, int from_row, int to_row, bool urgent = false);
    void queueLiveCueVariantSet(const std::shared_ptr<Title> &title, int row, const QString &state_key,
                                const QVector<std::shared_ptr<Title>> &variants, bool urgent,
                                bool hydrate_disk_to_ram = false, int manual_priority_group = 0);
    bool liveCueStateFullyResidentInRam(const QString &state_key) const;
    bool liveCueStateStartResidentInRam(const QString &state_key) const;
    bool liveCueStateRangePlayable(const QString &state_key, int first_frame, int last_frame) const;
    bool liveCueStateFullyPlayable(const QString &state_key) const;
    bool liveCueUseDiskStreaming() const;
    QString liveCueRowIdentity(const std::shared_ptr<Title> &title, int row) const;
    QString liveCueStateKey(const std::shared_ptr<Title> &title, int row) const;
    QString liveCueTransitionStateKey(const std::shared_ptr<Title> &title, int from_row, int to_row) const;
    int liveCueVariantsProgress(const QVector<std::shared_ptr<Title>> &variants) const;
    FrameCacheState liveCueVariantsState(const QVector<std::shared_ptr<Title>> &variants, const QString &state_key) const;
    CacheFrameKey liveCueKey(const std::shared_ptr<Title> &title, int row) const;
    int liveCueRangeProgress(const std::shared_ptr<Title> &cue_title) const;
    FrameCacheState liveCueRangeState(const std::shared_ptr<Title> &cue_title, const QString &state_key) const;
    int liveCueStoredProgress(const QString &state_key) const;
    FrameCacheState liveCueStoredState(const QString &state_key) const;
    bool shouldEmitLiveCueUpdate(const QString &state_key, FrameCacheState state, int progress) const;
    bool liveCueRowRenderPausedLocked(const QString &title_id, const QString &row_id) const;
    bool liveCueStateRenderPausedLocked(const QString &state_key) const;
    bool isLiveCueKeyReferenced(const CacheFrameKey &key) const;
    QString liveCueStateReferencingKey(const CacheFrameKey &key, const QString &preferred_state = QString()) const;
    void pruneUnreferencedLiveCueRam(const QString &title_id);
    void removeLiveCueState(const QString &state_key);
    int frameForTime(double t) const;
    double timeForFrame(int frame) const;
    void wakeWorker();
    void scheduleQueueChanged();
    void scheduleDiagnosticsChanged();
    void scheduleFrameReady(const QString &title_id, int frame);
    void scheduleLiveCueStateChanged(const QString &title_id, int row);
    void scheduleCacheStatesChanged(const QString &title_id, int first_frame, int last_frame);
    void scheduleUiNotificationFlush();
    void flushUiNotifications();
    void workerLoop();
    bool takePendingLiveCueStructureRefresh(std::shared_ptr<Title> &title);
    bool renderJob(RenderQueueManager::Job job);
    bool retryFailedJob(RenderQueueManager::Job job, const QString &live_state_key,
                        const char *stage);
    bool resolveOldestGpuReadback(bool force);
    bool hasPendingGpuReadbacks() const;
    void abandonJobState(const RenderQueueManager::Job &job, const QString &live_state_key);
    void resetCancelledWorkState(const QString &title_id = QString());
    void queueRealtimeJob(const std::shared_ptr<Title> &title, double time,
                          bool live_cue, int cue_row, const QString &cue_state_key,
                          const QString &known_content_hash = QString());
    void queueFrameWithHash(const std::shared_ptr<Title> &title, double time,
                            RenderQueueManager::PriorityBand band,
                            const QString &known_content_hash);
    quint64 titleGeneration(const QString &title_id) const;
    void bumpTitleGeneration(const QString &title_id);
    bool jobIsCurrent(const RenderQueueManager::Job &job) const;
    std::shared_ptr<Title> snapshotForJob(const std::shared_ptr<Title> &title,
                                          const CacheFrameKey &key);

    RamFrameCache ram_cache_;
    DiskFrameCache disk_cache_;
    CacheStateTracker state_tracker_;
    int next_manual_live_cue_priority_group_ = 0;
    RenderQueueManager queue_;
    CacheInvalidationManager invalidation_;
    std::atomic_bool paused_{false};
    std::atomic_bool cache_enabled_{true};
    std::atomic_bool interactive_bypass_{false};
    mutable QMutex editor_focus_mutex_;
    QString editor_focus_title_id_;
    bool editor_focus_active_ = false;
    std::atomic_bool worker_stop_{false};
    std::atomic<quint64> cache_epoch_{1};
    struct WorkerPipeline;
    std::unique_ptr<WorkerPipeline> worker_pipeline_;
    std::thread worker_thread_;
    mutable std::mutex worker_wait_mutex_;
    std::condition_variable worker_cv_;
    /* Serializes the final job-current check with cache publication and every
     * epoch/generation-changing clear/invalidation operation. */
    mutable std::recursive_mutex publication_mutex_;
    mutable std::mutex generation_mutex_;
    QHash<QString, quint64> title_generations_;
    mutable std::mutex snapshot_mutex_;
    mutable QMutex temporal_dedup_mutex_;
    QHash<QString, CacheFrameKey> temporal_canonical_keys_;
    QHash<QString, CacheFrameKey> adaptive_canonical_keys_;
    QHash<QString, std::weak_ptr<Title>> job_snapshots_;
    QString last_reprioritize_title_id_;
    int last_reprioritize_frame_ = -1;
    mutable QMutex playback_settings_mutex_;
    CachePlaybackSettings playback_settings_;
    mutable std::mutex live_cue_refresh_mutex_;
    QHash<QString, std::shared_ptr<Title>> pending_live_cue_structure_refreshes_;
    mutable std::recursive_mutex live_cue_mutex_;
    QHash<QString, FrameCacheState> live_cue_states_;
    QHash<QString, int> live_cue_progress_percent_;
    mutable QHash<QString, FrameCacheState> live_cue_last_emit_states_;
    mutable QHash<QString, int> live_cue_last_emit_buckets_;
    QHash<QString, QVector<CacheFrameKey>> live_cue_required_keys_;
    QHash<QString, int> live_cue_required_total_;
    QHash<QString, int> live_cue_rendered_since_emit_;
    QHash<QString, int> live_cue_total_by_key_;
    QHash<QString, int> live_cue_done_by_key_;
    QHash<QString, int> live_cue_rows_;
    QHash<QString, QString> live_cue_row_ids_;
    QHash<QString, QString> live_cue_title_ids_;
    QSet<QString> live_cue_transition_state_keys_;
    QHash<QString, QSet<QString>> live_cue_structure_row_ids_;
    QHash<QString, QHash<QString, QString>> live_cue_row_fingerprints_;
    QHash<QString, QString> live_cue_transition_signatures_;
    QSet<QString> live_cue_structure_refresh_in_progress_;
    QSet<CacheFrameKey> live_cue_known_keys_;
    QHash<QString, QSet<QString>> live_cue_paused_row_ids_;
    LiveCueCacheStats live_cue_stats_;
    mutable QMutex dirty_tiles_mutex_;
    mutable QHash<QString, QSet<QString>> dirty_tiles_by_frame_;
    mutable QMutex visual_hash_mutex_;
    QHash<QString, QString> last_visual_hash_by_title_;
    QHash<QString, QHash<QString, QRect>> last_layer_rects_by_title_;

    mutable std::mutex ui_notification_mutex_;
    std::atomic_bool ui_notification_flush_scheduled_{false};
    bool pending_queue_changed_ = false;
    bool pending_diagnostics_changed_ = false;
    QHash<QString, QPair<int, int>> pending_cache_state_ranges_;
    QHash<QString, QSet<int>> pending_frame_ready_;
    QHash<QString, QSet<int>> pending_live_cue_rows_;
};
