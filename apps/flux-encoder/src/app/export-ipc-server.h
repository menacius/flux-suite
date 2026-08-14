#pragma once

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <functional>

class QLocalSocket;

namespace flux {

class ExportIpcServer final : public QObject {
    Q_OBJECT
public:
    static constexpr const char *ProtocolName = "com.fluxsuite.encoder.ipc";
    static constexpr int ProtocolVersion = 1;
    static constexpr const char *DefaultEndpoint = "com.fluxsuite.encoder.ipc.v1";

    using OpenExportHandler =
        std::function<bool(const QJsonObject &source, const QJsonObject &options,
                           QString *error)>;

    explicit ExportIpcServer(QObject *parent = nullptr);
    ~ExportIpcServer() override;

    bool start(const QString &endpoint, QString *error = nullptr);
    void setOpenExportHandler(OpenExportHandler handler);

    static bool serviceIsRunning(const QString &endpoint);

private slots:
    void acceptConnections();
    void readClient();
    void forgetClient();

private:
    QJsonObject processRequest(const QJsonObject &request);
    QJsonObject errorResponse(const QString &requestId, const QString &code,
                              const QString &message) const;
    void sendResponse(QLocalSocket *socket, const QJsonObject &response);

    QLocalServer server_;
    QHash<QLocalSocket *, QByteArray> clientBuffers_;
    QHash<QString, QJsonObject> responseCache_;
    QStringList responseCacheOrder_;
    OpenExportHandler openExportHandler_;
};

} // namespace flux
