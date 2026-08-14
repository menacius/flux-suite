#include "network-utils.h"

#include "update-security.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

namespace {
QNetworkRequest trustedRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Flux-Suite-Installer/%1").arg(QStringLiteral(FLUX_INSTALLER_VERSION)));
    request.setTransferTimeout(15000);
    return request;
}
}

bool NetworkUtils::downloadBytes(const QUrl &url, qint64 maximumBytes, QByteArray *output,
                                 QString *error)
{
    if (!output || !UpdateSecurity::isTrustedRemoteUrl(url, error)) return false;
    QNetworkAccessManager network;
    QNetworkReply *reply = network.get(trustedRequest(url));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        output->append(reply->readAll());
        if (output->size() > maximumBytes) reply->abort();
    });
    loop.exec();
    output->append(reply->readAll());
    const bool tooLarge = output->size() > maximumBytes;
    const bool success = reply->error() == QNetworkReply::NoError && !tooLarge;
    if (!success && error) {
        *error = tooLarge ? QStringLiteral("The update response exceeded the size limit.")
                          : QStringLiteral("Secure download failed: %1").arg(reply->errorString());
    }
    reply->deleteLater();
    return success;
}

bool NetworkUtils::downloadFile(const QUrl &url, const QString &destination,
                                qint64 maximumBytes, const ProgressCallback &progress,
                                const std::atomic_bool *cancelled, QString *error)
{
    if (!UpdateSecurity::isTrustedRemoteUrl(url, error)) return false;
    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not create the downloaded file.");
        return false;
    }

    QNetworkAccessManager network;
    QNetworkReply *reply = network.get(trustedRequest(url));
    QEventLoop loop;
    qint64 receivedBytes = 0;
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, [&] {
        const QByteArray chunk = reply->readAll();
        receivedBytes += chunk.size();
        output.write(chunk);
        if (receivedBytes > maximumBytes || (cancelled && *cancelled)) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop,
                     [&](qint64 received, qint64 total) {
                         if (progress) progress(received, total);
                         if (received > maximumBytes || (cancelled && *cancelled)) reply->abort();
                     });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const QByteArray tail = reply->readAll();
    receivedBytes += tail.size();
    output.write(tail);

    const bool wasCancelled = cancelled && *cancelled;
    const bool tooLarge = receivedBytes > maximumBytes;
    const bool success = reply->error() == QNetworkReply::NoError && !wasCancelled && !tooLarge;
    if (!success && error) {
        *error = wasCancelled ? QStringLiteral("Download cancelled.")
            : tooLarge ? QStringLiteral("The download exceeded the allowed size.")
                       : QStringLiteral("Secure download failed: %1").arg(reply->errorString());
    }
    reply->deleteLater();
    if (!success) {
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (error) *error = QStringLiteral("Could not finalize the downloaded file.");
        return false;
    }
    return true;
}
