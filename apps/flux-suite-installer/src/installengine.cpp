#include "installengine.h"

#include "fileassociations.h"
#include "network-utils.h"
#include "update-security.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QUuid>

namespace {
QString shellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QStringLiteral("'") + value + QStringLiteral("'");
}

bool removeTree(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        return true;
    }
    return QDir(path).removeRecursively();
}

bool receiptMatches(const QString &root, const Product &product)
{
    QFile receipt(QDir(root).filePath(QStringLiteral(".flux-install.json")));
    if (receipt.open(QIODevice::ReadOnly)) {
        return QJsonDocument::fromJson(receipt.readAll()).object()
                   .value(QStringLiteral("product")).toString() == product.id;
    }
    return QFileInfo(QDir(root).filePath(product.executable)).isFile();
}
}

InstallEngine::InstallEngine(QObject *parent)
    : QObject(parent)
{
}

InstallEngine::~InstallEngine()
{
    cancel();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

bool InstallEngine::isBusy() const
{
    return m_thread && m_thread->isRunning();
}

void InstallEngine::start(const Product &product, const QString &applicationDir,
                          const QString &installPath)
{
    if (isBusy()) {
        return;
    }

    m_cancelled = false;
    QThread *thread = QThread::create([this, product, applicationDir, installPath] {
        const Result result = performInstall(product, applicationDir, installPath);
        Q_EMIT finished(product.id, result.success, result.message);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this] { m_thread = nullptr; });
    thread->start();
}

void InstallEngine::startUninstall(const Product &product, const QString &installPath)
{
    if (isBusy()) return;
    m_cancelled = false;
    QThread *thread = QThread::create([this, product, installPath] {
        const Result result = performUninstall(product, installPath);
        Q_EMIT finished(product.id, result.success, result.message);
    });
    m_thread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this] { m_thread = nullptr; });
    thread->start();
}

void InstallEngine::cancel()
{
    m_cancelled = true;
}

InstallEngine::Result InstallEngine::performInstall(Product product, QString applicationDir,
                                                     QString installPath)
{
    QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/FluxSuite-XXXXXX"));
    temporary.setAutoRemove(true);
    if (!temporary.isValid()) {
        return {false, QStringLiteral("Could not create the temporary workspace.")};
    }

    Q_EMIT stageChanged(product.id, QStringLiteral("download"),
                        QStringLiteral("Preparing %1").arg(product.name));
    QString error;
    const QString packagePath = acquirePackage(product, applicationDir, temporary.path(), &error);
    if (packagePath.isEmpty()) {
        return {false, error};
    }
    if (m_cancelled) {
        return {false, QStringLiteral("Installation cancelled.")};
    }

    Q_EMIT stageChanged(product.id, QStringLiteral("verify"),
                        QStringLiteral("Verifying secure package"));
    if (!UpdateSecurity::verifyFileSha256(packagePath, product.sha256, &error)) {
        return {false, error};
    }

    const QFileInfo targetInfo(installPath);
    if (!QDir().mkpath(targetInfo.absolutePath())) {
        return {false, QStringLiteral("Cannot create install directory: %1").arg(targetInfo.absolutePath())};
    }

    const QString suffix = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString stagingPath = installPath + QStringLiteral(".flux-staging-") + suffix;
    if (!removeTree(stagingPath) || !QDir().mkpath(stagingPath)) {
        return {false, QStringLiteral("Cannot create staging directory: %1").arg(stagingPath)};
    }

    Q_EMIT stageChanged(product.id, QStringLiteral("extract"),
                        QStringLiteral("Installing application files"));
    if (!extractPackage(packagePath, stagingPath, &error)) {
        removeTree(stagingPath);
        return {false, error};
    }
    if (m_cancelled) {
        removeTree(stagingPath);
        return {false, QStringLiteral("Installation cancelled.")};
    }

    const QString stagedExecutable = QDir(product.packageRoot.isEmpty()
                                               ? stagingPath
                                               : QDir(stagingPath).filePath(product.packageRoot))
                                         .filePath(product.executable);
    if (!QFileInfo(stagedExecutable).isFile()) {
        removeTree(stagingPath);
        return {false, QStringLiteral("Package is incomplete; missing %1").arg(product.executable)};
    }
#if !defined(Q_OS_WIN)
    const QFileDevice::Permissions executablePermissions = QFile::permissions(stagedExecutable)
        | QFileDevice::ExeOwner | QFileDevice::ExeUser | QFileDevice::ExeGroup
        | QFileDevice::ExeOther;
    if (!QFile::setPermissions(stagedExecutable, executablePermissions)) {
        removeTree(stagingPath);
        return {false, QStringLiteral("Could not mark %1 as executable.").arg(product.executable)};
    }
#endif

    Q_EMIT stageChanged(product.id, QStringLiteral("activate"),
                        QStringLiteral("Finalizing %1").arg(product.name));
    if (!activateStaging(stagingPath, product, installPath, &error)) {
        removeTree(stagingPath);
        return {false, error};
    }

    QJsonObject receipt;
    receipt.insert(QStringLiteral("product"), product.id);
    receipt.insert(QStringLiteral("name"), product.name);
    receipt.insert(QStringLiteral("version"), product.version);
    receipt.insert(QStringLiteral("sha256"), product.sha256);
    receipt.insert(QStringLiteral("installedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QSaveFile receiptFile(QDir(installPath).filePath(QStringLiteral(".flux-install.json")));
    if (receiptFile.open(QIODevice::WriteOnly)) {
        receiptFile.write(QJsonDocument(receipt).toJson(QJsonDocument::Indented));
        receiptFile.commit();
    }

    if (product.kind == QStringLiteral("application")) {
        createStartMenuShortcut(product, installPath);
        if (!FileAssociations::registerForProduct(product, installPath, &error)) {
            return {false, QStringLiteral("%1 was installed, but its file types could not be registered: %2")
                               .arg(product.name, error)};
        }
    }
    Q_EMIT progressChanged(product.id, 1, 1);
    return {true, QStringLiteral("%1 %2 is ready.").arg(product.name, product.version)};
}

InstallEngine::Result InstallEngine::performUninstall(Product product, QString installPath)
{
    Q_EMIT stageChanged(product.id, QStringLiteral("uninstall"),
                        QStringLiteral("Removing %1").arg(product.name));
    const QFileInfo target(installPath);
    if (target.absoluteFilePath().length() < 12 || target.absoluteFilePath() == target.absolutePath()
        || !receiptMatches(installPath, product)) {
        return {false, QStringLiteral("Uninstall refused because the installation could not be safely identified.")};
    }
    if (!removeTree(installPath)) {
        return {false, QStringLiteral("Could not remove %1. Close the application and OBS Studio, then try again.")
                           .arg(product.name)};
    }
    removeTree(QDir(target.absolutePath()).filePath(QStringLiteral(".flux-archives/") + product.id));
    if (product.kind == QStringLiteral("application")) {
        removeStartMenuShortcut(product);
        FileAssociations::unregisterForProduct(product, installPath);
    }
    Q_EMIT progressChanged(product.id, 1, 1);
    return {true, QStringLiteral("%1 was uninstalled.").arg(product.name)};
}

QString InstallEngine::acquirePackage(const Product &product, const QString &applicationDir,
                                      const QString &temporaryDir, QString *error)
{
    QString localPath = product.package;
    if (!localPath.isEmpty() && !QDir::isAbsolutePath(localPath)) {
        localPath = QDir(applicationDir).absoluteFilePath(localPath);
    }
    localPath = QDir::cleanPath(localPath);
    if (!localPath.isEmpty() && QFileInfo(localPath).isFile()) {
        Q_EMIT progressChanged(product.id, product.size, product.size);
        return localPath;
    }

    QUrl source(product.url);
    if ((!source.isValid() || source.scheme().isEmpty()) && !product.package.isEmpty()) {
        source = QUrl(product.package);
    }
    if (!source.isValid() || (source.scheme() != QStringLiteral("https")
                              && source.scheme() != QStringLiteral("http"))) {
        if (error) {
            *error = QStringLiteral("Package not found locally and no download URL is configured. Looked for: %1")
                         .arg(localPath);
        }
        return {};
    }

    const QString destination = QDir(temporaryDir).filePath(product.id + QStringLiteral(".zip"));
    QFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("Could not create downloaded package: %1").arg(destination);
        }
        return {};
    }

    output.close();
    QFile::remove(destination);
    const qint64 maximum = product.size > 0 ? product.size + 1 : 2LL * 1024 * 1024 * 1024;
    if (!NetworkUtils::downloadFile(source, destination, maximum,
                                    [this, &product](qint64 received, qint64 total) {
                                        Q_EMIT progressChanged(product.id, received, total);
                                    }, &m_cancelled, error)) return {};
    if (product.size > 0 && QFileInfo(destination).size() != product.size) {
        QFile::remove(destination);
        if (error) *error = QStringLiteral("Downloaded package size does not match the signed catalog.");
        return {};
    }
    return destination;
}

bool InstallEngine::extractPackage(const QString &archive, const QString &destination,
                                   QString *error)
{
#if defined(Q_OS_WIN)
    const QString command = QStringLiteral("$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                                .arg(shellQuote(QDir::toNativeSeparators(archive)),
                                     shellQuote(QDir::toNativeSeparators(destination)));
    QProcess process;
    process.setProgram(QStringLiteral("powershell.exe"));
    process.setArguments({QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                          QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                          QStringLiteral("Bypass"), QStringLiteral("-Command"), command});
#else
    // Info-ZIP is available on every supported distribution and, unlike a
    // shell command, passing arguments directly preserves spaces and prevents
    // package paths from being interpreted as code.
    QProcess process;
    process.setProgram(QStringLiteral("unzip"));
    process.setArguments({QStringLiteral("-q"), QStringLiteral("-o"), archive,
                          QStringLiteral("-d"), destination});
#endif
    process.start();
    while (!process.waitForFinished(200)) {
        if (m_cancelled) {
            process.kill();
            process.waitForFinished();
            if (error) {
                *error = QStringLiteral("Installation cancelled.");
            }
            return false;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) {
            *error = QStringLiteral("Could not extract package: %1")
                         .arg(QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
        }
        return false;
    }
    return true;
}

bool InstallEngine::activateStaging(const QString &staging, const Product &product,
                                    const QString &target, QString *error)
{
    QString source = product.packageRoot.isEmpty() ? staging : QDir(staging).filePath(product.packageRoot);
    source = QDir::cleanPath(source);
    const QString backup = target + QStringLiteral(".flux-backup-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool hadExisting = QFileInfo::exists(target);

    if (hadExisting && !QDir().rename(target, backup)) {
        if (error) {
            *error = QStringLiteral("Could not replace the existing installation. Close %1 and OBS Studio, then try again.")
                         .arg(QFileInfo(target).fileName());
        }
        return false;
    }

    if (!QDir().rename(source, target)) {
        if (hadExisting) {
            QDir().rename(backup, target);
        }
        if (error) {
            *error = QStringLiteral("Could not activate the new installation. The previous version was restored.");
        }
        return false;
    }

    if (!product.packageRoot.isEmpty()) {
        removeTree(staging);
    }
    if (hadExisting) {
        removeTree(backup);
    }
    removeTree(QDir(QFileInfo(target).absolutePath())
                   .filePath(QStringLiteral(".flux-archives/") + product.id));
    return true;
}

void InstallEngine::createStartMenuShortcut(const Product &product, const QString &installPath)
{
    QString programs = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
#if defined(Q_OS_WIN)
    const QString programFiles = QDir::cleanPath(qEnvironmentVariable("ProgramFiles"));
    if (!programFiles.isEmpty()
        && QDir::cleanPath(installPath).startsWith(programFiles + QDir::separator(),
                                                    Qt::CaseInsensitive)) {
        const QString programData = qEnvironmentVariable("ProgramData");
        if (!programData.isEmpty())
            programs = QDir(programData).filePath(QStringLiteral("Microsoft/Windows/Start Menu/Programs"));
    }
#endif
    if (programs.isEmpty()) {
        return;
    }
    const QString suiteFolder =
#if defined(Q_OS_WIN)
        QDir(programs).filePath(QStringLiteral("Flux Suite"));
#else
        programs;
#endif
    QDir().mkpath(suiteFolder);
#if defined(Q_OS_WIN)
    const QString shortcut = QDir(suiteFolder).filePath(product.name + QStringLiteral(".lnk"));
    const QString executable = QDir(installPath).filePath(product.executable);
    const QString script = QStringLiteral("$s=(New-Object -ComObject WScript.Shell).CreateShortcut(%1);$s.TargetPath=%2;$s.WorkingDirectory=%3;$s.Description=%4;$s.Save()")
                               .arg(shellQuote(QDir::toNativeSeparators(shortcut)),
                                    shellQuote(QDir::toNativeSeparators(executable)),
                                    shellQuote(QDir::toNativeSeparators(installPath)),
                                    shellQuote(product.tagline));
    QProcess::execute(QStringLiteral("powershell.exe"),
                      {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
                       QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), script});
#else
    const QString desktopFile = QDir(suiteFolder).filePath(product.id + QStringLiteral(".desktop"));
    QString escapedExecutable = QDir(installPath).filePath(product.executable);
    escapedExecutable.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escapedExecutable.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escapedExecutable.replace(QLatin1Char('`'), QStringLiteral("\\`"));
    escapedExecutable.replace(QLatin1Char('$'), QStringLiteral("\\$"));
    QStringList mimeTypes;
    if (product.id == QStringLiteral("motion-editor"))
        mimeTypes = {QStringLiteral("application/vnd.omniatv.flux-motion.title+json"),
                     QStringLiteral("application/vnd.omniatv.flux-motion.title-package"),
                     QStringLiteral("application/vnd.omniatv.flux-motion.project+json")};
    else if (product.id == QStringLiteral("encoder"))
        mimeTypes = {QStringLiteral("application/vnd.omniatv.flux-encoder.queue+json")};
    const QByteArray entry = QStringLiteral(
        "[Desktop Entry]\nType=Application\nName=%1\nComment=%2\nExec=\"%3\" %F\n"
        "Terminal=false\nCategories=AudioVideo;Graphics;\nMimeType=%4;\n")
        .arg(product.name, product.tagline, escapedExecutable, mimeTypes.join(QLatin1Char(';'))).toUtf8();
    QSaveFile output(desktopFile);
    if (output.open(QIODevice::WriteOnly) && output.write(entry) == entry.size())
        output.commit();
#endif
}

void InstallEngine::removeStartMenuShortcut(const Product &product)
{
    const QString programs = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (programs.isEmpty()) return;
#if defined(Q_OS_WIN)
    QFile::remove(QDir(programs).filePath(QStringLiteral("Flux Suite/")
                                         + product.name + QStringLiteral(".lnk")));
#else
    QFile::remove(QDir(programs).filePath(product.id + QStringLiteral(".desktop")));
#endif
#if defined(Q_OS_WIN)
    const QString programData = qEnvironmentVariable("ProgramData");
    if (!programData.isEmpty()) {
        QFile::remove(QDir(programData).filePath(
            QStringLiteral("Microsoft/Windows/Start Menu/Programs/Flux Suite/")
            + product.name + QStringLiteral(".lnk")));
    }
#endif
}
