#include "cache-manager.h"
#include "performance-counters.h"

#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

bool render_job_better(const RenderQueueManager::Job &a,
                       const RenderQueueManager::Job &b)
{
    if (a.priority != b.priority)
        return a.priority < b.priority;
    return a.key.frame < b.key.frame;
}

void sort_render_jobs_for_take(QVector<RenderQueueManager::Job> &jobs)
{
    /* Keep the highest-priority job at the back so ordinary dequeue is O(1).
     * Queue construction can add thousands of frames; sorting once at the first
     * dequeue avoids the previous O(N log N) sort after every insertion. */
    std::sort(jobs.begin(), jobs.end(), [](const auto &a, const auto &b) {
        return render_job_better(b, a);
    });
}

bool merge_render_job(RenderQueueManager::Job &queued,
                      const RenderQueueManager::Job &job)
{
    bool changed = false;
    if (job.live_cue && !queued.live_cue) {
        queued.live_cue = true;
        queued.force_render = queued.force_render || job.force_render;
        queued.cue_row = job.cue_row;
        queued.cue_state_key = job.cue_state_key;
        queued.title = job.title;
        queued.time = job.time;
        queued.cache_epoch = job.cache_epoch;
        queued.title_generation = job.title_generation;
        changed = true;
    }
    if (job.realtime && !queued.realtime) {
        queued.realtime = true;
        queued.title = job.title;
        queued.time = job.time;
        queued.cache_epoch = job.cache_epoch;
        queued.title_generation = job.title_generation;
        changed = true;
    }
    if (job.urgent && !queued.urgent) {
        queued.urgent = true;
        changed = true;
    }
    if (job.force_render && !queued.force_render) {
        queued.force_render = true;
        changed = true;
    }
    if (job.priority < queued.priority) {
        queued.priority = job.priority;
        changed = true;
    }
    return changed;
}

} // namespace

RenderQueueManager::RenderQueueManager(QObject *parent) : QObject(parent) {}

void RenderQueueManager::rebuildQueuedIndicesLocked()
{
    queued_indices_.clear();
    queued_indices_.reserve(jobs_.size());
    for (int index = 0; index < jobs_.size(); ++index)
        queued_indices_.insert(jobs_[index].key.toString(), index);
}

bool RenderQueueManager::enqueue(const Job &job)
{
    if (!job.title)
        return false;

    const QString key = job.key.toString();
    bool changed = false;
    bool inserted = false;
    int queue_size = 0;
    {
        QMutexLocker lock(&mutex_);
        if (!accepting_jobs_)
            return false;
        const QString token = QStringLiteral("%1:%2")
            .arg(job.cache_epoch)
            .arg(job.title_generation);
        if (active_tokens_.value(key) == token)
            return false;

        const auto queued_index = queued_indices_.constFind(key);
        if (queued_index != queued_indices_.cend()) {
            changed = merge_render_job(jobs_[queued_index.value()], job);
        } else {
            const int index = jobs_.size();
            jobs_.push_back(job);
            queued_keys_.insert(key);
            queued_indices_.insert(key, index);
            changed = true;
            inserted = true;
        }
        if (changed)
            order_dirty_ = true;
        queue_size = jobs_.size();
    }

    fxm::perf::set_max(fxm::perf::Counter::RenderQueuePeak,
                       static_cast<std::uint64_t>(std::max(0, queue_size)));
    if (inserted)
        fxm::perf::add(fxm::perf::Counter::ProxyJobsQueued);
    if (changed)
        emit queueChanged();
    return changed;
}

int RenderQueueManager::enqueueMany(const QVector<Job> &jobs)
{
    if (jobs.isEmpty())
        return 0;

    int inserted = 0;
    bool changed = false;
    int queue_size = 0;
    {
        QMutexLocker lock(&mutex_);
        if (!accepting_jobs_)
            return 0;

        for (const Job &job : jobs) {
            if (!job.title)
                continue;
            const QString key = job.key.toString();
            const QString token = QStringLiteral("%1:%2")
                .arg(job.cache_epoch)
                .arg(job.title_generation);
            if (active_tokens_.value(key) == token)
                continue;

            bool job_changed = false;
            const auto queued_index = queued_indices_.constFind(key);
            if (queued_index != queued_indices_.cend()) {
                job_changed = merge_render_job(
                    jobs_[queued_index.value()], job);
            } else {
                const int index = jobs_.size();
                jobs_.push_back(job);
                queued_keys_.insert(key);
                queued_indices_.insert(key, index);
                ++inserted;
                job_changed = true;
            }
            changed = changed || job_changed;
        }
        if (changed)
            order_dirty_ = true;
        queue_size = jobs_.size();
    }

    fxm::perf::set_max(fxm::perf::Counter::RenderQueuePeak,
                       static_cast<std::uint64_t>(std::max(0, queue_size)));
    if (inserted > 0)
        fxm::perf::add(fxm::perf::Counter::ProxyJobsQueued,
                       static_cast<std::uint64_t>(inserted));
    if (changed)
        emit queueChanged();
    return inserted;
}

void RenderQueueManager::setAcceptingJobs(bool accepting)
{
    {
        QMutexLocker lock(&mutex_);
        accepting_jobs_ = accepting;
        if (!accepting) {
            jobs_.clear();
            queued_keys_.clear();
            queued_indices_.clear();
            order_dirty_ = false;
        }
    }
    emit queueChanged();
}

void RenderQueueManager::cancelTitle(const QString &title_id)
{
    std::uint64_t removed = 0;
    {
        QMutexLocker lock(&mutex_);
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->key.title_id == title_id) {
                queued_keys_.remove(it->key.toString());
                it = jobs_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        if (removed)
            rebuildQueuedIndicesLocked();
    }
    if (removed)
        fxm::perf::add(fxm::perf::Counter::ProxyJobsCancelled, removed);
    emit queueChanged();
}

void RenderQueueManager::cancelRange(const QString &title_id, int first_frame, int last_frame)
{
    if (last_frame < first_frame)
        std::swap(first_frame, last_frame);
    std::uint64_t removed = 0;
    {
        QMutexLocker lock(&mutex_);
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->key.title_id == title_id && it->key.frame >= first_frame &&
                it->key.frame <= last_frame) {
                queued_keys_.remove(it->key.toString());
                it = jobs_.erase(it);
                ++removed;
            } else {
                ++it;
            }
        }
        if (removed)
            rebuildQueuedIndicesLocked();
    }
    if (removed)
        fxm::perf::add(fxm::perf::Counter::ProxyJobsCancelled, removed);
    emit queueChanged();
}

bool RenderQueueManager::cancelKey(const CacheFrameKey &key)
{
    bool removed = false;
    {
        QMutexLocker lock(&mutex_);
        const QString key_string = key.toString();
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->key == key) {
                queued_keys_.remove(key_string);
                it = jobs_.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        if (removed)
            rebuildQueuedIndicesLocked();
    }
    if (removed) {
        fxm::perf::add(fxm::perf::Counter::ProxyJobsCancelled);
        emit queueChanged();
    }
    return removed;
}

void RenderQueueManager::reprioritizeAround(const QString &title_id,
                                            int current_frame)
{
    {
        QMutexLocker lock(&mutex_);
        for (Job &job : jobs_) {
            if (job.key.title_id != title_id || job.live_cue)
                continue;
            const int delta = job.key.frame - current_frame;
            if (delta == 0)
                job.priority = 0;
            else if (delta > 0)
                job.priority = 100 + delta;
            else
                job.priority = 1000 + std::abs(delta);
        }
        order_dirty_ = true;
    }
    emit queueChanged();
}

bool RenderQueueManager::takeNext(Job &job)
{
    {
        QMutexLocker lock(&mutex_);
        if (jobs_.empty())
            return false;
        if (order_dirty_) {
            sort_render_jobs_for_take(jobs_);
            rebuildQueuedIndicesLocked();
            order_dirty_ = false;
        }
        job = jobs_.takeLast();
        const QString key = job.key.toString();
        queued_keys_.remove(key);
        queued_indices_.remove(key);
        active_tokens_[key] = QStringLiteral("%1:%2")
            .arg(job.cache_epoch)
            .arg(job.title_generation);
    }
    emit queueChanged();
    return true;
}

bool RenderQueueManager::takeNextUrgent(Job &job)
{
    bool found = false;
    {
        QMutexLocker lock(&mutex_);
        int best_index = -1;
        for (int index = 0; index < jobs_.size(); ++index) {
            if (!jobs_[index].urgent && !jobs_[index].realtime)
                continue;
            if (best_index < 0 ||
                render_job_better(jobs_[index], jobs_[best_index]))
                best_index = index;
        }
        if (best_index >= 0) {
            job = jobs_[best_index];
            const QString key = job.key.toString();
            queued_keys_.remove(key);
            active_tokens_[key] = QStringLiteral("%1:%2")
                .arg(job.cache_epoch)
                .arg(job.title_generation);
            jobs_.removeAt(best_index);
            rebuildQueuedIndicesLocked();
            found = true;
        }
    }
    if (found)
        emit queueChanged();
    return found;
}

bool RenderQueueManager::takeNextForTitle(const QString &title_id, Job &job)
{
    if (title_id.isEmpty())
        return false;
    bool found = false;
    {
        QMutexLocker lock(&mutex_);
        if (order_dirty_) {
            sort_render_jobs_for_take(jobs_);
            rebuildQueuedIndicesLocked();
            order_dirty_ = false;
        }
        for (int index = jobs_.size() - 1; index >= 0; --index) {
            if (jobs_[index].key.title_id != title_id || jobs_[index].live_cue)
                continue;
            job = jobs_[index];
            const QString key = job.key.toString();
            queued_keys_.remove(key);
            active_tokens_[key] = QStringLiteral("%1:%2")
                .arg(job.cache_epoch)
                .arg(job.title_generation);
            jobs_.removeAt(index);
            rebuildQueuedIndicesLocked();
            found = true;
            break;
        }
    }
    if (found)
        emit queueChanged();
    return found;
}

bool RenderQueueManager::contains(const CacheFrameKey &key) const
{
    QMutexLocker lock(&mutex_);
    return queued_keys_.contains(key.toString());
}

int RenderQueueManager::queuedCount() const
{
    QMutexLocker lock(&mutex_);
    return jobs_.size();
}

bool RenderQueueManager::hasAvailableJob(bool urgent_or_realtime_only) const
{
    QMutexLocker lock(&mutex_);
    if (!urgent_or_realtime_only)
        return !jobs_.isEmpty();
    return std::any_of(jobs_.cbegin(), jobs_.cend(), [](const Job &job) {
        return job.urgent || job.realtime;
    });
}

bool RenderQueueManager::hasAvailableJobForTitle(const QString &title_id) const
{
    if (title_id.isEmpty())
        return false;
    QMutexLocker lock(&mutex_);
    return std::any_of(jobs_.cbegin(), jobs_.cend(), [&title_id](const Job &job) {
        return job.key.title_id == title_id && !job.live_cue;
    });
}

void RenderQueueManager::complete(const Job &job)
{
    fxm::perf::add(fxm::perf::Counter::ProxyJobsCompleted);
    {
        QMutexLocker lock(&mutex_);
        const QString key = job.key.toString();
        const QString token = QStringLiteral("%1:%2")
            .arg(job.cache_epoch)
            .arg(job.title_generation);
        if (active_tokens_.value(key) == token)
            active_tokens_.remove(key);
    }
    emit queueChanged();
}

void RenderQueueManager::clear()
{
    {
        QMutexLocker lock(&mutex_);
        jobs_.clear();
        queued_keys_.clear();
        queued_indices_.clear();
        order_dirty_ = false;
    }
    emit queueChanged();
}
