#include "selfupdater.h"

#include "network-utils.h"
#include "update-security.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <QVersionNumber>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
QVersionNumber semanticVersion(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch match = pattern.match(value);
    return match.hasMatch()
        ? QVersionNumber(match.captured(1).toInt(), match.captured(2).toInt(),
                         match.captured(3).toInt())
        : QVersionNumber();
}

bool copyAtomically(const QString &source, const QString &target, QString *error)
{
    QFile input(source);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not open installer files for self-update.");
        return false;
    }
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(4 * 1024 * 1024);
        if (chunk.isEmpty() && input.error() != QFile::NoError) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Could not read the downloaded installer.");
            return false;
        }
        if (output.write(chunk) != chunk.size()) {
            output.cancelWriting();
            if (error) *error = QStringLiteral("Could not write the updated installer.");
            return false;
        }
    }
    if (!output.commit()) {
        if (error) *error = QStringLiteral("Could not activate the updated installer.");
        return false;
    }
    return true;
}
}

SelfUpdater::SelfUpdater(QObject *parent) : QObject(parent) {}

SelfUpdater::~SelfUpdater()
{
    cancel();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

bool SelfUpdater::isBusy() const
{
    return m_thread && m_thread->isRunning();
}

bool SelfUpdater::isNewerVersion(const QString &candidate, const QString &current)
{
    const QVersionNumber candidateVersion = semanticVersion(candidate);
    const QVersionNumber currentVersion = semanticVersion(current);
    if (candidateVersion.isNull() || currentVersion.isNull()) return false;
    const int comparison = QVersionNumber::compare(candidateVersion, currentVersion);
    // The 0.8.17 release standardized the public label by adding the `v`
    // marker. Treat that one-time label migration as an installer update even
    // though its numeric semantic version is unchanged.
    return comparison > 0 || (comparison == 0 && candidate.trimmed() != current.trimmed());
}

void SelfUpdater::download(const InstallerRelease &release)
{
    if (isBusy() || !release.isValid()) return;
    m_cancelled = false;
    QThread *thread = QThread::create([this, release] {
        const QString directory = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                      .filePath(QStringLiteral("FluxSuiteUpdater/")
                                                + QUuid::createUuid().toString(QUuid::WithoutBraces));
        if (!QDir().mkpath(directory)) {
            Q_EMIT finished(false, {}, {}, QStringLiteral("Could not prepare the self-update workspace."));
            return;
        }
        const QString destination = QDir(directory).filePath(QStringLiteral("Flux Suite Setup.exe"));
        QString error;
        const qint64 limit = qMax<qint64>(release.size + 1, 1024 * 1024);
        if (!NetworkUtils::downloadFile(QUrl(release.url), destination, limit,
                                        [this](qint64 received, qint64 total) {
                                            Q_EMIT progressChanged(received, total);
                                        }, &m_cancelled, &error)
            || QFileInfo(destination).size() != release.size
            || !UpdateSecurity::verifyFileSha256(destination, release.sha256, &error)) {
            QDir(directory).removeRecursively();
            Q_EMIT finished(false, {}, {}, error.isEmpty()
                                ? QStringLiteral("The installer update did not match its signed metadata.")
                                : error);
            return;
        }
        Q_EMIT finished(true, destination, release.sha256,
                        QStringLiteral("The verified installer update is ready."));
    });
    m_thread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this] { m_thread = nullptr; });
    thread->start();
}

void SelfUpdater::cancel()
{
    m_cancelled = true;
}

bool SelfUpdater::applyDownloaded(const QString &downloadedPath, const QString &expectedSha256,
                                  QString *error)
{
    if (!UpdateSecurity::verifyFileSha256(downloadedPath, expectedSha256, error)) return false;
    /* The signed bootstrap contains Flux Suite and every Qt dependency. A
     * bare Flux Suite.exe cannot run from the temporary download directory. */
    const QStringList arguments = {
        QStringLiteral("/UPDATE=1"),
        QStringLiteral("/CLOSEAPPLICATIONS")
    };
    if (!QProcess::startDetached(downloadedPath, arguments,
                                 QFileInfo(downloadedPath).absolutePath())) {
        if (error) *error = QStringLiteral("Could not start the verified Flux Suite Setup update.");
        return false;
    }
    return true;
}

int SelfUpdater::runApplyMode(const QString &source, const QString &target,
                              const QString &expectedSha256, quint32 parentPid,
                              QString *error)
{
    const QFileInfo sourceInfo(source);
    const QFileInfo targetInfo(target);
    if (!sourceInfo.isFile()
        || sourceInfo.absoluteFilePath() != QFileInfo(QCoreApplication::applicationFilePath()).absoluteFilePath()
        || targetInfo.fileName().compare(QStringLiteral("Flux Suite.exe"),
                                                              Qt::CaseInsensitive) != 0
        || sourceInfo.absoluteFilePath() == targetInfo.absoluteFilePath()
        || !UpdateSecurity::verifyFileSha256(source, expectedSha256, error)) return 20;

#ifdef Q_OS_WIN
    if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, parentPid)) {
        const DWORD waitResult = WaitForSingleObject(process, 60000);
        CloseHandle(process);
        if (waitResult == WAIT_TIMEOUT) {
            if (error) *error = QStringLiteral("Timed out waiting for the previous installer to close.");
            return 21;
        }
    }
#else
    Q_UNUSED(parentPid)
#endif

    if (!copyAtomically(source, target, error)) return 22;
    if (!QProcess::startDetached(target, {QStringLiteral("--self-update-complete")},
                                 targetInfo.absolutePath())) {
        if (error) *error = QStringLiteral("The installer was updated but could not be restarted.");
        return 23;
    }
    return 0;
}
