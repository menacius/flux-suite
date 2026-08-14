#pragma once

#include "core/media-types.h"
#include <QObject>
#include <QProcess>

namespace flux {

class Database;

class QueueRunner final : public QObject {
    Q_OBJECT
public:
    explicit QueueRunner(Database *database, QObject *parent = nullptr);
    ~QueueRunner() override;
    void setJobs(QList<QueueJob> *jobs);
    bool isRunning() const { return running_; }

public slots:
    void start();
    void stopAfterCurrent();
    void cancelCurrent();

signals:
    void jobChanged(const QString &uuid);
    void runningChanged(bool running);
    void logMessage(const QString &message);
    void queueFinished();

private slots:
    void readWorkerOutput();
    void workerFinished(int exitCode, QProcess::ExitStatus status);

private:
    int nextJobIndex() const;
    void startJob(int index);
    void finishCurrent(bool success, const QString &error = QString());
    void startNext();
    QString workerExecutable() const;
    void terminateWorkerTree();

    Database *database_ = nullptr;
    QList<QueueJob> *jobs_ = nullptr;
    QProcess worker_;
    QByteArray outputBuffer_;
    int currentIndex_ = -1;
    bool running_ = false;
    bool stopRequested_ = false;
    bool currentReportedSuccess_ = false;
    QString currentJobFile_;
};

} // namespace flux
