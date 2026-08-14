#pragma once
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
namespace flux {
class ProviderProcess final : public QObject {
    Q_OBJECT
public:
    explicit ProviderProcess(QObject *parent=nullptr);
    bool start(const QString &executable,const QString &providerId,QString *error=nullptr);
    QString sendRequest(const QString &method,const QJsonObject &params=QJsonObject());
    void cancelSession(const QString &sessionId);
    void stop();
    bool isReady() const{return ready_;}
signals:
    void responseReceived(const QString &requestId,const QJsonObject &response);
    void providerError(const QString &message);
    void providerExited(int exitCode);
private slots:
    void readOutput();
    void heartbeat();
private:
    void sendObject(const QJsonObject &object);
    QProcess process_;
    QByteArray inputBuffer_;
    QTimer heartbeatTimer_;
    QString providerId_;
    bool ready_=false;
};
}
