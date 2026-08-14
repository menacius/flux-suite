#include "cache-manager.h"
#include "title-logger.h"

#include <QMutexLocker>

#include <algorithm>
#include <iterator>

RamFrameCache::RamFrameCache(QObject *parent) : QObject(parent) {}

bool RamFrameCache::contains(const CacheFrameKey &key) const
{
    QMutexLocker lock(&mutex_);
    const auto key_it = key_payload_ids_.constFind(key);
    if (key_it == key_payload_ids_.constEnd())
        return false;
    const auto payload_it = payloads_.constFind(key_it.value());
    return payload_it != payloads_.constEnd() && !payload_it->image.isNull();
}

bool RamFrameCache::get(const CacheFrameKey &key, QImage &image) const
{
    QMutexLocker lock(&mutex_);
    const auto key_it = key_payload_ids_.constFind(key);
    if (key_it == key_payload_ids_.constEnd())
        return false;
    const auto payload_it = payloads_.constFind(key_it.value());
    if (payload_it == payloads_.constEnd() || payload_it->image.isNull())
        return false;
    auto *self = const_cast<RamFrameCache *>(this);
    self->touchKeyLocked(key);
    image = payload_it->image;
    return true;
}

void RamFrameCache::put(const CacheFrameKey &key, const QImage &image)
{
    if (image.isNull())
        return;

    QMutexLocker lock(&mutex_);
    const quint64 payload_id = payloadId(image);
    const auto existing_key = key_payload_ids_.constFind(key);
    if (existing_key != key_payload_ids_.constEnd() && existing_key.value() == payload_id) {
        touchKeyLocked(key);
        return;
    }

    releaseKeyLocked(key);

    auto payload_it = payloads_.find(payload_id);
    if (payload_it == payloads_.end()) {
        PayloadEntry entry;
        entry.image = image; // QImage is implicitly shared: no pixel copy here.
        entry.bytes = imageBytes(image);
        entry.references = 1;
        payloads_.insert(payload_id, entry);
        bytes_used_ += entry.bytes;
    } else {
        ++payload_it->references;
    }

    key_payload_ids_.insert(key, payload_id);
    touchKeyLocked(key);
    evictIfNeeded();
}

void RamFrameCache::remove(const CacheFrameKey &key)
{
    QMutexLocker lock(&mutex_);
    releaseKeyLocked(key);
}

void RamFrameCache::clear()
{
    QMutexLocker lock(&mutex_);
    const quint64 released_bytes = bytes_used_;
    const int released_keys = key_payload_ids_.size();
    key_payload_ids_.clear();
    payloads_.clear();
    lru_.clear();
    lru_positions_.clear();
    bytes_used_ = 0;
    FXM_LOG_INFO("RamCache", QStringLiteral(
        "Cleared RAM frame cache keys=%1 bytes=%2")
        .arg(released_keys).arg(released_bytes));
}

void RamFrameCache::setMaxBytes(quint64 bytes)
{
    QMutexLocker lock(&mutex_);
    max_bytes_ = std::max<quint64>(16ull * 1024ull * 1024ull, bytes);
    evictIfNeeded();
    FXM_LOG_INFO("RamCache", QStringLiteral(
        "Set RAM frame cache limit bytes=%1 used=%2")
        .arg(max_bytes_).arg(bytes_used_));
}

quint64 RamFrameCache::maxBytes() const
{
    QMutexLocker lock(&mutex_);
    return max_bytes_;
}

quint64 RamFrameCache::bytesUsed() const
{
    QMutexLocker lock(&mutex_);
    return bytes_used_;
}

qsizetype RamFrameCache::count() const
{
    QMutexLocker lock(&mutex_);
    return key_payload_ids_.size();
}

QVector<CacheFrameKey> RamFrameCache::keysForTitle(const QString &title_id) const
{
    QMutexLocker lock(&mutex_);
    QVector<CacheFrameKey> keys;
    keys.reserve(key_payload_ids_.size());
    for (auto it = key_payload_ids_.constBegin(); it != key_payload_ids_.constEnd(); ++it) {
        if (it.key().title_id == title_id)
            keys.push_back(it.key());
    }
    return keys;
}

quint64 RamFrameCache::imageBytes(const QImage &image) const
{
    return image.isNull() ? 0 : (quint64)image.bytesPerLine() * (quint64)image.height();
}

quint64 RamFrameCache::payloadId(const QImage &image) const
{
    /* cacheKey() identifies the implicitly shared QImage backing store. It is
     * stable across shallow copies and changes whenever Qt detaches/modifies
     * the pixels, which is exactly the identity required for RAM accounting. */
    return static_cast<quint64>(image.cacheKey());
}

void RamFrameCache::touchKeyLocked(const CacheFrameKey &key)
{
    auto position = lru_positions_.find(key);
    if (position != lru_positions_.end()) {
        lru_.splice(lru_.end(), lru_, position.value());
        position.value() = std::prev(lru_.end());
        return;
    }
    lru_.push_back(key);
    lru_positions_.insert(key, std::prev(lru_.end()));
}

void RamFrameCache::releaseKeyLocked(const CacheFrameKey &key)
{
    auto lru_position = lru_positions_.find(key);
    if (lru_position != lru_positions_.end()) {
        lru_.erase(lru_position.value());
        lru_positions_.erase(lru_position);
    }

    const auto key_it = key_payload_ids_.find(key);
    if (key_it == key_payload_ids_.end())
        return;

    const quint64 payload_id = key_it.value();
    key_payload_ids_.erase(key_it);

    auto payload_it = payloads_.find(payload_id);
    if (payload_it == payloads_.end())
        return;
    if (--payload_it->references <= 0) {
        bytes_used_ -= std::min(bytes_used_, payload_it->bytes);
        payloads_.erase(payload_it);
    }
}

void RamFrameCache::evictIfNeeded()
{
    while (bytes_used_ > max_bytes_ && !lru_.empty()) {
        const CacheFrameKey victim = lru_.front();
        /* Removing an alias is free until the last reference is evicted. Keep
         * walking the LRU until enough unique payloads have actually gone. */
        releaseKeyLocked(victim);
    }
}
