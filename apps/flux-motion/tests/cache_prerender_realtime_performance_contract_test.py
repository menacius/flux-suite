from pathlib import Path

root = Path(__file__).resolve().parents[1]
read = lambda rel: (root / rel).read_text(encoding="utf-8")

header = read("src/cache/cache-manager.h")
manager = read("src/cache/cache-manager/disk-cache-storage.inc")
policy = read("src/cache/cache-manager/cache-policy-invalidation.inc")
live = read("src/cache/cache-manager/live-cue-queueing.inc")
worker = read("src/cache/cache-manager/worker-publication.inc")
queue = read("src/cache/render-queue-manager.cpp")
ram = read("src/cache/ram-frame-cache.cpp")
disk = read("src/cache/cache-manager/visual-hash-keying.inc")
dock = read("src/cache/prerender-dock.cpp")
runtime = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-runtime.inc")

# Persistence must evaluate keyframes and transitions on the same frozen clock.
transition_clock = runtime.split("static double gpu_layer_transition_time", 1)[1].split(
    "static bool gpu_layer_chain_visible", 1
)[0]
assert "gpu_layer_uses_cue_persistence(title, layer)" in transition_clock
assert "cue_persistence_hold_time(title)" in transition_clock
assert "cache_layer_sample_time" in manager
assert "layer_transition_progress(transition, layer->in_time, layer->out_time, sample_time)" in manager + policy

# Large prerender ranges are batched and sorting is deferred until dequeue.
assert "int enqueueMany(const QVector<Job> &jobs)" in header
assert "order_dirty_" in header
assert "queued_indices_" in header
assert "rebuildQueuedIndicesLocked" in queue
assert "queued_indices_.constFind(key)" in queue
assert "RenderQueueManager::enqueueMany" in queue
assert "sort_render_jobs_for_take" in queue
assert "jobs_.takeLast()" in queue
assert "!jobs_[index].urgent && !jobs_[index].realtime" in queue
assert "render_job_better(jobs_[index], jobs_[best_index])" in queue
assert "return job.urgent || job.realtime" in queue
assert "queue_.enqueueMany(jobs)" in policy
assert "queue_.enqueueMany(pending_jobs)" in live

# RAM LRU operations must not scan the complete cache on every playback frame.
assert "std::list<CacheFrameKey> lru_" in header
assert "lru_positions_" in header
assert "lru_.splice" in ram
assert "removeAll" not in ram

# Realtime/UI request paths may only test disk membership; payload reads belong to the worker.
request_editor = policy.split("QImage CacheManager::requestFrame(", 1)[1].split(
    "QString CacheManager::requestFrameGpuToken", 1
)[0]
request_realtime = policy.split("QImage CacheManager::requestFrameRealtime", 1)[1].split(
    "void CacheManager::restoreDiskStates", 1
)[0]
request_live = live.split("QImage CacheManager::requestLiveCueFrame(", 1)[1].split(
    "QString CacheManager::requestLiveCueFrameGpuToken", 1
)[0]
request_live_realtime = live.split("QImage CacheManager::requestLiveCueFrameRealtime", 1)[1].split(
    "QImage CacheManager::requestLiveCueFrame(const std::shared_ptr<Title> &title, int row, bool", 1
)[0]
for section in (request_editor, request_realtime, request_live, request_live_realtime):
    assert "disk_cache_.get" not in section
    assert "disk_cache_.contains" in section
assert "disk_cache_.get(job.key, resident_image)" in worker
assert "ram_cache_.put(job.key, resident_image)" in worker

# Membership/diagnostics queries and Qt updates must not serialize with disk writes per frame.
contains = disk.split("bool DiskFrameCache::contains", 1)[1].split(
    "bool DiskFrameCache::readFrameTileRefsLocked", 1
)[0]
assert "QReadLocker" in contains
assert "indexed_membership_" in contains
assert "QMutexLocker lock(&mutex_)" not in contains
assert "bytes_used_snapshot_" in header
assert "scheduleUiNotificationFlush" in manager
assert "QTimer::singleShot(33" in manager
assert "status_update_timer_->setInterval(100)" in dock

# Disk persistence is bounded and best-effort: cache publication must never wait
# for compression/file writes while holding render/live-cue publication locks.
assert "writer_space_cv_" not in header
assert "writer_space_cv_.wait" not in disk
assert "writer_queue_limit_ = 8" in header
assert "in_flight >= writer_queue_limit_" in disk
assert "bool DiskFrameCache::enqueuePut" in disk
assert "const bool disk_write_queued = disk_cache_.enqueuePut" in worker
assert "ram_cache_.put(job.key, image)" in worker

# Playback reprioritization promotes only the visible/lookaround frames and does
# not rewrite/sort the complete title queue on every video tick.
reprioritize = policy.split("void CacheManager::reprioritize", 1)[1].split(
    "void CacheManager::resetCancelledWorkState", 1
)[0]
assert "queue_.reprioritizeAround" not in reprioritize

# Background live-cue generation must not carry artificial per-frame delays.
worker_loop = worker.split("void CacheManager::workerLoop", 1)[1].split(
    "bool CacheManager::retryFailedJob", 1
)[0]
assert "milliseconds(8)" not in worker_loop
assert "milliseconds(12)" not in worker_loop
assert "const int cooldown_ms = (job.urgent || job.realtime) ? 0 : 1" in worker_loop
assert "have_job = queue_.takeNextUrgent(job)" in worker_loop

print("live-cue persistence and realtime cache performance contract: OK")
