#include "cache-manager.h"
#include "performance-counters.h"

#include <QMutexLocker>

#include <algorithm>
#include <limits>
#include <utility>

namespace {

int cache_state_rank(FrameCacheState state)
{
    switch (state) {
    case FrameCacheState::Rendering: return 6;
    case FrameCacheState::Queued: return 5;
    case FrameCacheState::CachedRam: return 4;
    case FrameCacheState::CachedDisk: return 3;
    case FrameCacheState::Stale: return 2;
    case FrameCacheState::Disabled: return 1;
    case FrameCacheState::NotCached: return 0;
    }
    return 0;
}

} // namespace

CacheStateTracker::CacheStateTracker(QObject *parent) : QObject(parent) {}

void CacheStateTracker::rebuildTitleIndexLocked(const QString &title_id)
{
    QHash<int, FrameCacheState> rebuilt;
    for (auto it = states_.cbegin(); it != states_.cend(); ++it) {
        if (it.key().title_id != title_id)
            continue;
        const int frame = it.key().frame;
        const FrameCacheState current = rebuilt.value(frame, FrameCacheState::NotCached);
        if (cache_state_rank(it.value()) > cache_state_rank(current))
            rebuilt[frame] = it.value();
    }
    if (rebuilt.isEmpty())
        frame_states_.remove(title_id);
    else
        frame_states_[title_id] = std::move(rebuilt);
    fxm::perf::add(fxm::perf::Counter::CacheStateIndexRebuilds);
}

FrameCacheState CacheStateTracker::state(const CacheFrameKey &key) const
{
    QMutexLocker lock(&mutex_);
    return states_.value(key, FrameCacheState::NotCached);
}

FrameCacheState CacheStateTracker::stateForFrame(const QString &title_id, int frame) const
{
    fxm::perf::add(fxm::perf::Counter::CacheStateFrameLookups);
    QMutexLocker lock(&mutex_);
    const auto title_it = frame_states_.constFind(title_id);
    if (title_it == frame_states_.cend())
        return FrameCacheState::NotCached;
    return title_it.value().value(frame, FrameCacheState::NotCached);
}

void CacheStateTracker::setState(const CacheFrameKey &key, FrameCacheState state)
{
    {
        QMutexLocker lock(&mutex_);
        auto it = states_.find(key);
        if (it != states_.end() && it.value() == state)
            return;

        const FrameCacheState previous = it == states_.end()
            ? FrameCacheState::NotCached : it.value();
        states_[key] = state;

        QHash<int, FrameCacheState> &title_index = frame_states_[key.title_id];
        const FrameCacheState indexed = title_index.value(
            key.frame, FrameCacheState::NotCached);
        if (cache_state_rank(state) >= cache_state_rank(indexed)) {
            title_index[key.frame] = state;
        } else if (cache_state_rank(previous) >= cache_state_rank(indexed)) {
            /* A variant that supplied the aggregate state was demoted. Rebuild
             * this title once on the producer side so timeline/UI reads remain
             * O(1) even when several content hashes share the same frame. */
            rebuildTitleIndexLocked(key.title_id);
        }
    }
    emit stateChanged(key.title_id, key.frame, key.frame);
}

void CacheStateTracker::markRange(const QString &title_id, int first_frame,
                                  int last_frame, FrameCacheState state)
{
    if (last_frame < first_frame)
        std::swap(first_frame, last_frame);
    bool changed = false;
    {
        QMutexLocker lock(&mutex_);
        for (auto it = states_.begin(); it != states_.end(); ++it) {
            if (it.key().title_id == title_id &&
                it.key().frame >= first_frame && it.key().frame <= last_frame &&
                it.value() != state) {
                it.value() = state;
                changed = true;
            }
        }
        if (changed)
            rebuildTitleIndexLocked(title_id);
    }
    if (changed)
        emit stateChanged(title_id, first_frame, last_frame);
}

void CacheStateTracker::clearTitle(const QString &title_id)
{
    int first = std::numeric_limits<int>::max();
    int last = -1;
    {
        QMutexLocker lock(&mutex_);
        for (auto it = states_.begin(); it != states_.end();) {
            if (it.key().title_id == title_id) {
                first = std::min(first, it.key().frame);
                last = std::max(last, it.key().frame);
                it = states_.erase(it);
            } else {
                ++it;
            }
        }
        frame_states_.remove(title_id);
    }
    if (last >= 0)
        emit stateChanged(title_id, first, last);
}

void CacheStateTracker::resetTransient(const QString &title_id)
{
    QHash<QString, QPair<int, int>> changed_ranges;
    {
        QMutexLocker lock(&mutex_);
        for (auto it = states_.begin(); it != states_.end(); ++it) {
            if (!title_id.isEmpty() && it.key().title_id != title_id)
                continue;
            if (it.value() != FrameCacheState::Queued &&
                it.value() != FrameCacheState::Rendering)
                continue;

            const QString changed_title = it.key().title_id;
            auto range_it = changed_ranges.find(changed_title);
            if (range_it == changed_ranges.end()) {
                changed_ranges.insert(changed_title,
                                      qMakePair(it.key().frame, it.key().frame));
            } else {
                range_it.value().first = std::min(range_it.value().first,
                                                  it.key().frame);
                range_it.value().second = std::max(range_it.value().second,
                                                   it.key().frame);
            }
            it.value() = FrameCacheState::NotCached;
        }
        for (auto it = changed_ranges.cbegin(); it != changed_ranges.cend(); ++it)
            rebuildTitleIndexLocked(it.key());
    }
    for (auto it = changed_ranges.cbegin(); it != changed_ranges.cend(); ++it)
        emit stateChanged(it.key(), it.value().first, it.value().second);
}

void CacheStateTracker::clear()
{
    {
        QMutexLocker lock(&mutex_);
        states_.clear();
        frame_states_.clear();
    }
    emit stateChanged(QString(), 0, std::numeric_limits<int>::max());
}

QHash<int, FrameCacheState> CacheStateTracker::titleStates(
    const QString &title_id) const
{
    QMutexLocker lock(&mutex_);
    return frame_states_.value(title_id);
}

QHash<int, FrameCacheState> CacheStateTracker::statesForRange(
    const QString &title_id, int first_frame, int last_frame) const
{
    if (last_frame < first_frame)
        std::swap(first_frame, last_frame);
    QHash<int, FrameCacheState> result;
    result.reserve(last_frame - first_frame + 1);
    QMutexLocker lock(&mutex_);
    const auto title_it = frame_states_.constFind(title_id);
    if (title_it == frame_states_.cend())
        return result;
    for (int frame = first_frame; frame <= last_frame; ++frame) {
        const auto state_it = title_it.value().constFind(frame);
        if (state_it != title_it.value().cend())
            result.insert(frame, state_it.value());
    }
    return result;
}
