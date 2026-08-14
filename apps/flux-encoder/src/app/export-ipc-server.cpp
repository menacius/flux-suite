#include "app/export-ipc-server.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocalSocket>

namespace flux {
namespace {
constexpr qint64 kMaximumMessageBytes = 1024 * 1024;
constexpr int kProbeTimeoutMs = 200;
constexpr int kResponseCacheLimit = 256;
}

ExportIpcServer::ExportIpcServer(QObject *parent) : QObject(parent)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this,
            &ExportIpcServer::acceptConnections);
}

ExportIpcServer::~ExportIpcServer()
{
    server_.close();
    const auto sockets = server_.findChildren<QLocalSocket *>();
    for (QLocalSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        delete socket;
    }
    clientBuffers_.clear();
}

bool ExportIpcServer::serviceIsRunning(const QString &endpoint)
{
    QLocalSocket socket;
    socket.connectToServer(endpoint, QIODevice::ReadWrite);
    const bool connected = socket.waitForConnected(kProbeTimeoutMs);
    if (connected)
        socket.disconnectFromServer();
    return connected;
}

bool ExportIpcServer::start(const QString &endpoint, QString *error)
{
    const QString name = endpoint.trimmed();
    if (name.isEmpty()) {
        if (error)
            *error = tr("The IPC endpoint name is empty.");
        return false;
    }

    if (server_.isListening())
        server_.close();
    if (server_.listen(name)) {
        if (error)
            error->clear();
        return true;
    }

    // A crashed process can leave a stale Unix-domain socket. Never remove a
    // live endpoint: probe it first, then clean up and retry only when unused.
    if (!serviceIsRunning(name)) {
        QLocalServer::removeServer(name);
        if (server_.listen(name)) {
            if (error)
                error->clear();
            return true;
        }
    }

    if (error)
        *error = server_.errorString();
    return false;
}

void ExportIpcServer::setOpenExportHandler(OpenExportHandler handler)
{
    openExportHandler_ = std::move(handler);
}

void ExportIpcServer::acceptConnections()
{
    while (server_.hasPendingConnections()) {
        QLocalSocket *socket = server_.nextPendingConnection();
        if (!socket)
            continue;
        clientBuffers_.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this,
                &ExportIpcServer::readClient);
        connect(socket, &QLocalSocket::disconnected, this,
                &ExportIpcServer::forgetClient);
    }
}

void ExportIpcServer::readClient()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket || !clientBuffers_.contains(socket))
        return;

    QByteArray &buffer = clientBuffers_[socket];
    buffer += socket->readAll();
    if (buffer.size() > kMaximumMessageBytes && !buffer.contains('\n')) {
        sendResponse(socket, errorResponse({}, QStringLiteral("message_too_large"),
                                           tr("The IPC request is too large.")));
        socket->disconnectFromServer();
        return;
    }

    for (;;) {
        const qsizetype newline = buffer.indexOf('\n');
        if (newline < 0)
            break;
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;
        if (line.size() > kMaximumMessageBytes) {
            sendResponse(socket, errorResponse({}, QStringLiteral("message_too_large"),
                                               tr("The IPC request is too large.")));
            continue;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            sendResponse(socket, errorResponse({}, QStringLiteral("invalid_json"),
                                               tr("The IPC request is not valid JSON.")));
            continue;
        }
        sendResponse(socket, processRequest(document.object()));
    }
}

void ExportIpcServer::forgetClient()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket)
        return;
    clientBuffers_.remove(socket);
    socket->deleteLater();
}

QJsonObject ExportIpcServer::processRequest(const QJsonObject &request)
{
    const QString requestId = request.value(QStringLiteral("request_id")).toString();
    if (!requestId.isEmpty()) {
        const auto cached = responseCache_.constFind(requestId);
        if (cached != responseCache_.cend())
            return cached.value();
    }

    QJsonObject response;
    if (requestId.isEmpty()) {
        response = errorResponse({}, QStringLiteral("missing_request_id"),
                                 tr("The IPC request has no request_id."));
    } else if (request.value(QStringLiteral("protocol")).toString() !=
               QString::fromLatin1(ProtocolName)) {
        response = errorResponse(requestId, QStringLiteral("invalid_protocol"),
                                 tr("The IPC protocol name is not supported."));
    } else if (request.value(QStringLiteral("protocol_version")).toInt(-1) !=
               ProtocolVersion) {
        response = errorResponse(requestId, QStringLiteral("incompatible_version"),
                                 tr("This Flux Motion version uses an incompatible IPC protocol."));
    } else if (request.value(QStringLiteral("command")).toString() !=
               QStringLiteral("export.open")) {
        response = errorResponse(requestId, QStringLiteral("unsupported_command"),
                                 tr("The requested IPC command is not supported."));
    } else if (!request.value(QStringLiteral("source")).isObject()) {
        response = errorResponse(requestId, QStringLiteral("invalid_source"),
                                 tr("The export request has no valid source."));
    } else if (!openExportHandler_) {
        response = errorResponse(requestId, QStringLiteral("service_starting"),
                                 tr("Flux Encoder is still starting. Try the export again."));
    } else {
        const QJsonObject source = request.value(QStringLiteral("source")).toObject();
        const QJsonObject options = request.value(QStringLiteral("options")).toObject();
        QString message;
        if (!openExportHandler_(source, options, &message)) {
            if (message.isEmpty())
                message = tr("Flux Encoder could not open this export request.");
            response = errorResponse(requestId, QStringLiteral("export_open_failed"), message);
        } else {
            response = {{QStringLiteral("protocol"), QString::fromLatin1(ProtocolName)},
                        {QStringLiteral("protocol_version"), ProtocolVersion},
                        {QStringLiteral("request_id"), requestId},
                        {QStringLiteral("ok"), true}};
        }
    }

    if (!requestId.isEmpty()) {
        responseCache_.insert(requestId, response);
        responseCacheOrder_.append(requestId);
        while (responseCacheOrder_.size() > kResponseCacheLimit)
            responseCache_.remove(responseCacheOrder_.takeFirst());
    }
    return response;
}

QJsonObject ExportIpcServer::errorResponse(const QString &requestId,
                                           const QString &code,
                                           const QString &message) const
{
    return {{QStringLiteral("protocol"), QString::fromLatin1(ProtocolName)},
            {QStringLiteral("protocol_version"), ProtocolVersion},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("ok"), false},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), code},
                         {QStringLiteral("message"), message}}}};
}

void ExportIpcServer::sendResponse(QLocalSocket *socket,
                                   const QJsonObject &response)
{
    if (!socket || socket->state() != QLocalSocket::ConnectedState)
        return;
    QByteArray payload = QJsonDocument(response).toJson(QJsonDocument::Compact);
    payload.append('\n');
    socket->write(payload);
    socket->flush();
}

} // namespace flux
