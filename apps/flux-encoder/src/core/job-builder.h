#pragma once
#include "core/media-types.h"

namespace flux {

class JobBuilder final {
public:
    static QStringList ffmpegArguments(const QueueJob &job, const QString &temporaryOutput,
                                       QString *effectiveEncoder = nullptr,
                                       const QStringList &inputOverride = {},
                                       int audioInputIndex = -1);
    static QString defaultOutputPath(const QString &source, const RenderProfile &profile);
    static QString temporaryOutputPath(const QString &output);
    static bool validate(const QueueJob &job, QString *error);
};

} // namespace flux
