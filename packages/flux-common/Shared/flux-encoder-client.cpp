#include "flux-encoder-client.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>

namespace fxm::encoder {
namespace {

constexpr int kProbeTimeoutMs = 100;
constexpr int kConnectTimeoutMs = 400;
constexpr int kWriteTimeoutMs = 1500;
constexpr int kResponseTimeoutMs = 5000;
constexpr int kStartupTimeoutMs = 10000;
constexpr qint64 kMaximumResponseBytes = 1024 * 1024;
constexpr const char *kMotionSettingsOrg = "FluxMotion";
constexpr const char *kMotionSettingsApp = "Dock";
constexpr const char *kMotionEditorGroup = "Editor";
constexpr const char *kExecutableOverrideKey = "FLUX_ENCODER_EXECUTABLE";

enum class SendResult {
    Success,
    NoService,
    Failed,
};

bool is_executable_file(const QString &path)
{
    if (path.trimmed().isEmpty())
        return false;
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile())
        return false;
#if defined(Q_OS_WIN)
    return file.suffix().compare(QStringLiteral("exe"),
                                 Qt::CaseInsensitive) == 0;
#else
    return file.isExecutable();
#endif
}

void append_install_candidate(QStringList &candidates, const QString &value)
{
    const QString path = QDir::cleanPath(value.trimmed());
    if (!path.isEmpty() && !candidates.contains(path, Qt::CaseInsensitive))
        candidates.push_back(path);
}

void append_settings_candidates(QStringList &candidates,
                                QSettings::Scope scope)
{
    QSettings settings(QSettings::NativeFormat, scope,
                       QStringLiteral("FluxSuite"),
                       QStringLiteral("Flux Encoder"));
    const QString executable =
        settings.value(QStringLiteral("installation/executable")).toString();
    append_install_candidate(candidates, executable);

    const QString install_root =
        settings.value(QStringLiteral("installation/path")).toString();
    if (!install_root.isEmpty()) {
#if defined(Q_OS_WIN)
        append_install_candidate(
            candidates, QDir(install_root).filePath(QStringLiteral("Flux Encoder.exe")));
        append_install_candidate(
            candidates, QDir(install_root).filePath(QStringLiteral("flux-encoder.exe")));
#else
        append_install_candidate(
            candidates, QDir(install_root).filePath(QStringLiteral("flux-encoder")));
#endif
    }
}

QString discover_executable()
{
    QStringList candidates;
    append_install_candidate(
        candidates, FluxEncoderClient::configuredExecutablePath());
    append_install_candidate(
        candidates, qEnvironmentVariable("FLUX_ENCODER_EXECUTABLE"));
    append_settings_candidates(candidates, QSettings::UserScope);
    append_settings_candidates(candidates, QSettings::SystemScope);

    const QDir application_dir(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
    append_install_candidate(
        candidates, application_dir.filePath(QStringLiteral("Flux Encoder.exe")));
    append_install_candidate(
        candidates, application_dir.filePath(QStringLiteral("flux-encoder.exe")));
    append_install_candidate(candidates,
        application_dir.filePath(QStringLiteral("../Flux Encoder/Flux Encoder.exe")));
    for (const char *variable : {"ProgramFiles", "ProgramFiles(x86)",
                                 "LOCALAPPDATA"}) {
        const QString root = qEnvironmentVariable(variable);
        if (!root.isEmpty()) {
            append_install_candidate(candidates,
                QDir(root).filePath(
                    QStringLiteral("Flux Suite/Flux Encoder/Flux Encoder.exe")));
        }
    }
#elif defined(Q_OS_MACOS)
    append_install_candidate(candidates,
        application_dir.filePath(QStringLiteral("../../../../Flux Encoder.app/Contents/MacOS/Flux Encoder")));
    append_install_candidate(candidates,
        QStringLiteral("/Applications/Flux Encoder.app/Contents/MacOS/Flux Encoder"));
#else
    append_install_candidate(
        candidates, application_dir.filePath(QStringLiteral("flux-encoder")));
    append_install_candidate(candidates, QStringLiteral("/opt/flux-suite/flux-encoder"));
#endif

    const QString from_path =
        QStandardPaths::findExecutable(QStringLiteral("flux-encoder"));
    append_install_candidate(candidates, from_path);

    for (const QString &candidate : candidates) {
        if (is_executable_file(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

bool service_is_running()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromUtf8(kLocalServerName),
                           QIODevice::ReadWrite);
    if (!socket.waitForConnected(kProbeTimeoutMs))
        return false;
    socket.disconnectFromServer();
    return true;
}

QString response_error(const QJsonObject &response)
{
    const QJsonValue error_value = response.value(QStringLiteral("error"));
    if (error_value.isString())
        return error_value.toString();
    if (error_value.isObject()) {
        const QJsonObject error = error_value.toObject();
        const QString message = error.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            return message;
        const QString code = error.value(QStringLiteral("code")).toString();
        if (!code.isEmpty())
            return code;
    }
    return QStringLiteral("Flux Encoder rejected the export request.");
}

SendResult send_request(const QJsonObject &request, QString *error)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromUtf8(kLocalServerName),
                           QIODevice::ReadWrite);
    if (!socket.waitForConnected(kConnectTimeoutMs)) {
        if (error)
            *error = socket.errorString();
        return SendResult::NoService;
    }

    QByteArray message = QJsonDocument(request).toJson(QJsonDocument::Compact);
    message.append('\n');
    if (socket.write(message) != message.size() ||
        !socket.waitForBytesWritten(kWriteTimeoutMs)) {
        if (error)
            *error = QStringLiteral("Could not send the export request to Flux Encoder: %1")
                         .arg(socket.errorString());
        return SendResult::Failed;
    }

    QByteArray response_bytes;
    QElapsedTimer response_timer;
    response_timer.start();
    while (!response_bytes.contains('\n') &&
           response_timer.elapsed() < kResponseTimeoutMs) {
        const int remaining = static_cast<int>(
            kResponseTimeoutMs - response_timer.elapsed());
        if (!socket.waitForReadyRead(qMax(1, remaining)))
            break;
        response_bytes += socket.readAll();
        if (response_bytes.size() > kMaximumResponseBytes) {
            if (error)
                *error = QStringLiteral("Flux Encoder returned an oversized IPC response.");
            return SendResult::Failed;
        }
    }
    response_bytes += socket.readAll();
    if (response_bytes.size() > kMaximumResponseBytes) {
        if (error)
            *error = QStringLiteral("Flux Encoder returned an oversized IPC response.");
        return SendResult::Failed;
    }
    const int newline = response_bytes.indexOf('\n');
    if (newline >= 0)
        response_bytes.truncate(newline);

    QJsonParseError parse_error;
    const QJsonDocument parsed =
        QJsonDocument::fromJson(response_bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !parsed.isObject()) {
        if (error) {
            *error = response_bytes.isEmpty()
                ? QStringLiteral("Flux Encoder did not acknowledge the export request.")
                : QStringLiteral("Flux Encoder returned an invalid IPC response: %1")
                      .arg(parse_error.errorString());
        }
        return SendResult::Failed;
    }

    const QJsonObject response = parsed.object();
    const QString expected_id =
        request.value(QStringLiteral("request_id")).toString();
    const QString response_id =
        response.value(QStringLiteral("request_id")).toString();
    if (!response_id.isEmpty() && response_id != expected_id) {
        if (error)
            *error = QStringLiteral("Flux Encoder returned a response for a different request.");
        return SendResult::Failed;
    }
    if (response.value(QStringLiteral("protocol")).toString() !=
            QString::fromUtf8(kProtocolName) ||
        response.value(QStringLiteral("protocol_version")).toInt() !=
            kProtocolVersion) {
        if (error)
            *error = QStringLiteral("This Flux Encoder version uses an incompatible IPC protocol.");
        return SendResult::Failed;
    }
    if (!response.value(QStringLiteral("ok")).toBool(false)) {
        if (error)
            *error = response_error(response);
        return SendResult::Failed;
    }

    if (error)
        error->clear();
    return SendResult::Success;
}

} // namespace

FluxEncoderClient &FluxEncoderClient::instance()
{
    static FluxEncoderClient client;
    return client;
}

Availability FluxEncoderClient::refreshAvailability()
{
    availability_.service_running = service_is_running();
    availability_.executable_path = discover_executable();
    return availability_;
}

Availability FluxEncoderClient::availability() const
{
    return availability_;
}

QString FluxEncoderClient::configuredExecutablePath()
{
    QSettings settings(QString::fromUtf8(kMotionSettingsOrg),
                       QString::fromUtf8(kMotionSettingsApp));
    settings.beginGroup(QString::fromUtf8(kMotionEditorGroup));
    const QString path =
        settings.value(QString::fromUtf8(kExecutableOverrideKey)).toString();
    settings.endGroup();
    return path.trimmed();
}

void FluxEncoderClient::setConfiguredExecutablePath(const QString &path)
{
    QSettings settings(QString::fromUtf8(kMotionSettingsOrg),
                       QString::fromUtf8(kMotionSettingsApp));
    settings.beginGroup(QString::fromUtf8(kMotionEditorGroup));
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
        settings.remove(QString::fromUtf8(kExecutableOverrideKey));
    else
        settings.setValue(QString::fromUtf8(kExecutableOverrideKey),
                          QDir::cleanPath(trimmed));
    settings.endGroup();
    settings.sync();
}

QJsonObject FluxEncoderClient::buildOpenExportRequest(
    const ExportRequest &request, const QString &request_id)
{
    QJsonObject source;
    source.insert(QStringLiteral("application"), QStringLiteral("flux-motion"));
    source.insert(QStringLiteral("project_id"), request.project_id);
    source.insert(QStringLiteral("project_name"), request.project_name);
    source.insert(QStringLiteral("project_store_path"),
                  request.project_store_path);
    source.insert(QStringLiteral("project_scope"), request.project_scope);

    QJsonObject options;
    options.insert(QStringLiteral("activate_window"), true);

    QJsonObject envelope;
    envelope.insert(QStringLiteral("protocol"), QString::fromUtf8(kProtocolName));
    envelope.insert(QStringLiteral("protocol_version"), kProtocolVersion);
    envelope.insert(QStringLiteral("request_id"), request_id);
    envelope.insert(QStringLiteral("command"), QStringLiteral("export.open"));
    envelope.insert(QStringLiteral("source"), source);
    envelope.insert(QStringLiteral("options"), options);
    return envelope;
}

bool FluxEncoderClient::openExport(const ExportRequest &request, QString *error)
{
    if (request.project_id.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Flux Motion has no saved project to export.");
        return false;
    }

    const QJsonObject envelope = buildOpenExportRequest(
        request, QUuid::createUuid().toString(QUuid::WithoutBraces));
    QString send_error;
    const SendResult first_result = send_request(envelope, &send_error);
    if (first_result == SendResult::Success)
        return true;
    if (first_result == SendResult::Failed) {
        if (error)
            *error = send_error;
        return false;
    }

    const Availability current = refreshAvailability();
    if (current.executable_path.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "Flux Encoder is required for media export, but it is not installed.");
        }
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--single-instance")
              << QStringLiteral("--ipc-endpoint")
              << QString::fromUtf8(kLocalServerName);
    qint64 process_id = 0;
    if (!QProcess::startDetached(current.executable_path, arguments,
                                 QFileInfo(current.executable_path).absolutePath(),
                                 &process_id)) {
        if (error) {
            *error = QStringLiteral("Flux Encoder could not be launched from %1.")
                         .arg(QDir::toNativeSeparators(current.executable_path));
        }
        return false;
    }

    QElapsedTimer startup_timer;
    startup_timer.start();
    while (startup_timer.elapsed() < kStartupTimeoutMs) {
        QThread::msleep(100);
        send_error.clear();
        const SendResult result = send_request(envelope, &send_error);
        if (result == SendResult::Success) {
            availability_.service_running = true;
            if (error)
                error->clear();
            return true;
        }
        /* A connected service returned a real error. Starting another instance
         * would violate the Encoder single-instance contract. */
        if (result == SendResult::Failed) {
            if (error)
                *error = send_error;
            return false;
        }
    }

    if (error) {
        *error = QStringLiteral(
            "Flux Encoder started but its export service did not become available.");
    }
    return false;
}

} // namespace fxm::encoder
