#include "cache-manager.h"

QString CacheFrameKey::toString() const
{
    return QStringLiteral("%1-%2-%3-%4x%5")
        .arg(title_id, content_hash)
        .arg(frame)
        .arg(width)
        .arg(height);
}

bool CacheFrameKey::operator==(const CacheFrameKey &other) const
{
    return title_id == other.title_id &&
           content_hash == other.content_hash &&
           frame == other.frame &&
           width == other.width &&
           height == other.height;
}

uint qHash(const CacheFrameKey &key, uint seed)
{
    return qHash(key.toString(), seed);
}
