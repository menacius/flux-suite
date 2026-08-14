#include "app/export-ipc-server.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <QUuid>
#include <functional>

namespace {
bool waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return condition();
}

QJsonObject exchange(const QString &endpoint, const QJsonObject &request)
{
    QLocalSocket socket;
    socket.connectToServer(endpoint, QIODevice::ReadWrite);
    if (!waitUntil([&socket]() { return socket.state() == QLocalSocket::ConnectedState; }, 2000))
        return {};
    QByteArray payload = QJsonDocument(request).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket.write(payload);
    socket.flush();
    QByteArray response;
    if (!waitUntil([&]() {
            response += socket.readAll();
            return response.contains('\n');
        }, 2000))
        return {};
    response.truncate(response.indexOf('\n'));
    return QJsonDocument::fromJson(response).object();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString endpoint = QStringLiteral("flux-encoder-ipc-test-%1")
                                 .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    flux::ExportIpcServer server;
    QString startupError;
    if (!server.start(endpoint, &startupError))
        return 1;

    bool handled = false;
    server.setOpenExportHandler(
        [&handled](const QJsonObject &source, const QJsonObject &options, QString *error) {
            handled = source.value(QStringLiteral("project_id")).toString() == QStringLiteral("title-1") &&
                      options.value(QStringLiteral("activate_window")).toBool();
            if (error) error->clear();
            return handled;
        });

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject request{
        {QStringLiteral("protocol"), QString::fromLatin1(flux::ExportIpcServer::ProtocolName)},
        {QStringLiteral("protocol_version"), flux::ExportIpcServer::ProtocolVersion},
        {QStringLiteral("request_id"), requestId},
        {QStringLiteral("command"), QStringLiteral("export.open")},
        {QStringLiteral("source"), QJsonObject{
             {QStringLiteral("application"), QStringLiteral("flux-motion")},
             {QStringLiteral("project_id"), QStringLiteral("title-1")}}},
        {QStringLiteral("options"), QJsonObject{
             {QStringLiteral("activate_window"), true}}}};
    const QJsonObject success = exchange(endpoint, request);
    if (!handled || !success.value(QStringLiteral("ok")).toBool() ||
        success.value(QStringLiteral("request_id")).toString() != requestId ||
        success.value(QStringLiteral("protocol_version")).toInt() != 1)
        return 2;

    QJsonObject incompatible = request;
    incompatible.insert(QStringLiteral("request_id"), QStringLiteral("incompatible"));
    incompatible.insert(QStringLiteral("protocol_version"), 99);
    const QJsonObject failure = exchange(endpoint, incompatible);
    if (failure.value(QStringLiteral("ok")).toBool(true) ||
        failure.value(QStringLiteral("error")).toObject()
                .value(QStringLiteral("code")).toString() != QStringLiteral("incompatible_version"))
        return 3;
    return 0;
}
