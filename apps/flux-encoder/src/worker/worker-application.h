#pragma once

#include "core/media-types.h"

#include <QObject>
#include <QProcess>

namespace flux {

class WorkerApplication final : public QObject {
    Q_OBJECT
public:
    explicit WorkerApplication(QObject *parent = nullptr);
    int run(const QString &jobFile);

private:
    qint64 probeDuration(const QueueJob &job) const;
    bool encoderExists(const QString &encoder) const;
    void emitJson(const QJsonObject &object) const;
    QStringList fallbackArguments(QStringList arguments,
                                  const QString &fromEncoder,
                                  const QString &toEncoder) const;
    bool runFfmpegAttempt(const QString &ffmpeg,
                          const QStringList &arguments,
                          qint64 durationMs,
                          QString *errorMessage,
                          const QString &renderExecutable = {},
                          const QStringList &renderArguments = {}) const;
};

} // namespace flux
