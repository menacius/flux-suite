#pragma once

#include "manifest.h"

#include <QObject>
#include <QPointer>

#include <atomic>

class QThread;

class SelfUpdater final : public QObject {
    Q_OBJECT

public:
    explicit SelfUpdater(QObject *parent = nullptr);
    ~SelfUpdater() override;

    bool isBusy() const;
    static bool isNewerVersion(const QString &candidate, const QString &current);
    void download(const InstallerRelease &release);
    void cancel();
    bool applyDownloaded(const QString &downloadedPath, const QString &expectedSha256,
                         QString *error);
    static int runApplyMode(const QString &source, const QString &target,
                            const QString &expectedSha256, quint32 parentPid,
                            QString *error);

Q_SIGNALS:
    void progressChanged(qint64 received, qint64 total);
    void finished(bool success, const QString &path, const QString &sha256,
                  const QString &message);

private:
    QPointer<QThread> m_thread;
    std::atomic_bool m_cancelled = false;
};
