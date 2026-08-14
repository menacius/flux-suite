#pragma once

#include "core/media-types.h"

namespace flux {

struct RenderProviderSourceFormat {
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
    qint64 durationMs = 0;
};

struct RenderProviderProjectInspection {
    bool valid = false;
    QString compositionId;
    QString name;
    RenderProviderSourceFormat format;
    bool hasAudio = false;
};

RenderProviderSourceFormat readRenderProviderSourceFormat(const QueueJob &job);
QString locateRenderProviderExecutable(const QString &providerId);
RenderProviderProjectInspection inspectRenderProviderProject(
    const QString &providerId, const QString &projectPath,
    QString *error = nullptr);
QStringList renderProviderVideoArguments(const QueueJob &job,
                                         const QString &executable,
                                         qint64 startMs,
                                         qint64 durationMs,
                                         double frameRate);
QStringList renderProviderAudioArguments(const QueueJob &job,
                                         const QString &executable,
                                         const QString &outputPath,
                                         qint64 startMs,
                                         qint64 durationMs,
                                         int sampleRate);

} // namespace flux
