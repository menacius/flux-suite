#include "core/queue-runner.h"
#include "core/database.h"
#include "core/job-builder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QStandardPaths>
#include <limits>

namespace flux {

QueueRunner::QueueRunner(Database *database, QObject *parent) : QObject(parent), database_(database)
{
    connect(&worker_, &QProcess::readyReadStandardOutput, this, &QueueRunner::readWorkerOutput);
    connect(&worker_, &QProcess::readyReadStandardError, this, &QueueRunner::readWorkerOutput);
    connect(&worker_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &QueueRunner::workerFinished);
}

QueueRunner::~QueueRunner()
{
    if (worker_.state() != QProcess::NotRunning) {
        worker_.disconnect(this);
        terminateWorkerTree();
    }
}

void QueueRunner::setJobs(QList<QueueJob> *jobs) { jobs_ = jobs; }

QString QueueRunner::workerExecutable() const
{
    QString name = QStringLiteral("flux-encoder-worker");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
}

int QueueRunner::nextJobIndex() const
{
    if (!jobs_) return -1;
    int bestIndex = -1;
    int bestPriority = std::numeric_limits<int>::min();
    for (int i = 0; i < jobs_->size(); ++i) {
        const auto &job = jobs_->at(i);
        if (job.status != JobStatus::Pending && job.status != JobStatus::Interrupted)
            continue;
        if (bestIndex < 0 || job.priority > bestPriority) {
            bestIndex = i;
            bestPriority = job.priority;
        }
    }
    return bestIndex;
}

void QueueRunner::start()
{
    if (running_ || !jobs_) return;
    stopRequested_ = false; running_ = true; emit runningChanged(true); startNext();
}

void QueueRunner::stopAfterCurrent() { stopRequested_ = true; }

void QueueRunner::cancelCurrent()
{
    if (currentIndex_ < 0 || !jobs_)
        return;

    QueueJob &job = (*jobs_)[currentIndex_];
    job.status = JobStatus::Cancelled;
    job.errorMessage = QStringLiteral("Cancelled by user");
    job.updatedAt = QDateTime::currentDateTimeUtc();
    database_->saveJob(job);
    emit jobChanged(job.uuid);

    // Killing only the worker process can leave its FFmpeg child alive,
    // especially on Windows. Always terminate the complete process tree.
    terminateWorkerTree();
    QFile::remove(JobBuilder::temporaryOutputPath(job.outputPath));
}

void QueueRunner::terminateWorkerTree()
{
    if (worker_.state() == QProcess::NotRunning)
        return;

    const qint64 workerPid = worker_.processId();

#ifdef Q_OS_WIN
    // taskkill /T terminates the worker and every descendant, including FFmpeg
    // and any transient FFprobe process. /F is required because the worker may
    // be blocked in a synchronous wait and cannot process a graceful request.
    if (workerPid > 0) {
        QProcess treeKiller;
        treeKiller.setProgram(QStringLiteral("taskkill.exe"));
        treeKiller.setArguments({QStringLiteral("/PID"), QString::number(workerPid),
                                 QStringLiteral("/T"), QStringLiteral("/F")});
        treeKiller.start();
        treeKiller.waitForFinished(5000);
    }
#else
    // FFmpeg is a direct child of the worker. Ask children to terminate first
    // so they can flush and close their temporary output, then stop the worker.
    if (workerPid > 0) {
        QProcess childTerminator;
        childTerminator.start(QStringLiteral("pkill"),
                              {QStringLiteral("-TERM"), QStringLiteral("-P"),
                               QString::number(workerPid)});
        childTerminator.waitForFinished(1500);
    }
    worker_.terminate();
    if (!worker_.waitForFinished(1500) && workerPid > 0) {
        QProcess childKiller;
        childKiller.start(QStringLiteral("pkill"),
                          {QStringLiteral("-KILL"), QStringLiteral("-P"),
                           QString::number(workerPid)});
        childKiller.waitForFinished(1500);
    }
#endif

    // Fallback for systems where the platform process-tree command is missing
    // or was unable to terminate the worker itself.
    if (worker_.state() != QProcess::NotRunning) {
        worker_.kill();
        worker_.waitForFinished(2000);
    }
}

void QueueRunner::startNext()
{
    if (stopRequested_) {
        running_ = false; currentIndex_ = -1; emit runningChanged(false); emit queueFinished(); return;
    }
    const int index = nextJobIndex();
    if (index < 0) {
        running_ = false; currentIndex_ = -1; emit runningChanged(false); emit queueFinished(); return;
    }
    startJob(index);
}

void QueueRunner::startJob(int index)
{
    currentIndex_ = index; currentReportedSuccess_ = false; outputBuffer_.clear();
    QueueJob &job = (*jobs_)[index]; job.status = JobStatus::Preparing; job.progress = 0.0; job.errorMessage.clear();
    job.updatedAt = QDateTime::currentDateTimeUtc(); database_->saveJob(job); emit jobChanged(job.uuid);

    const QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(cache);
    currentJobFile_ = QDir(cache).filePath(QStringLiteral("job-%1.json").arg(job.uuid));
    QFile file(currentJobFile_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishCurrent(false, file.errorString()); return;
    }
    file.write(QJsonDocument(job.toJson()).toJson(QJsonDocument::Compact)); file.close();

    const QString executable = workerExecutable();
    worker_.setProgram(executable); worker_.setArguments({QStringLiteral("--job"), currentJobFile_});
    worker_.setProcessChannelMode(QProcess::SeparateChannels); worker_.start();
    if (!worker_.waitForStarted(3000)) finishCurrent(false, QStringLiteral("Could not start worker: %1").arg(worker_.errorString()));
}

void QueueRunner::readWorkerOutput()
{
    outputBuffer_ += worker_.readAllStandardOutput();
    const QByteArray stderrData = worker_.readAllStandardError();
    if (!stderrData.isEmpty()) emit logMessage(QString::fromUtf8(stderrData).trimmed());
    int newline = -1;
    while ((newline = outputBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = outputBuffer_.left(newline).trimmed(); outputBuffer_.remove(0, newline + 1);
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject() || currentIndex_ < 0 || !jobs_) continue;
        const QJsonObject message = doc.object();
        QueueJob &job = (*jobs_)[currentIndex_];
        const QString type = message.value("type").toString();
        if (type == QStringLiteral("progress")) {
            job.status = JobStatus::Encoding;
            job.progress = message.value("percent").toDouble(job.progress);
            job.durationMs = static_cast<qint64>(message.value("durationMs").toDouble(job.durationMs));
            job.currentTimeMs = static_cast<qint64>(message.value("outTimeMs").toDouble(job.currentTimeMs));
            job.etaSeconds = static_cast<qint64>(message.value("etaSeconds").toDouble(-1));
            job.speed = message.value("speed").toString();
            job.updatedAt = QDateTime::currentDateTimeUtc(); database_->saveJob(job); emit jobChanged(job.uuid);
        } else if (type == QStringLiteral("log")) {
            emit logMessage(message.value("message").toString());
        } else if (type == QStringLiteral("complete")) {
            currentReportedSuccess_ = true;
        } else if (type == QStringLiteral("error")) {
            job.errorMessage = message.value("message").toString();
        }
    }
}

void QueueRunner::workerFinished(int exitCode, QProcess::ExitStatus status)
{
    readWorkerOutput();
    const bool success = status == QProcess::NormalExit && exitCode == 0 && currentReportedSuccess_;
    QString error;
    if (!success && currentIndex_ >= 0 && jobs_) error = (*jobs_)[currentIndex_].errorMessage;
    if (error.isEmpty() && !success) error = QStringLiteral("Worker exited with code %1").arg(exitCode);
    finishCurrent(success, error);
}

void QueueRunner::finishCurrent(bool success, const QString &error)
{
    if (currentIndex_ < 0 || !jobs_) return;
    QueueJob &job = (*jobs_)[currentIndex_];
    if (job.status != JobStatus::Cancelled) {
        job.status = success ? JobStatus::Completed : JobStatus::Failed;
        job.progress = success ? 100.0 : job.progress;
        job.errorMessage = error; job.updatedAt = QDateTime::currentDateTimeUtc();
        database_->saveJob(job); emit jobChanged(job.uuid);
    }
    QFile::remove(currentJobFile_); currentJobFile_.clear(); currentIndex_ = -1;
    QMetaObject::invokeMethod(this, &QueueRunner::startNext, Qt::QueuedConnection);
}

} // namespace flux
