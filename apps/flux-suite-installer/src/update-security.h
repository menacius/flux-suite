#pragma once

#include <QByteArray>
#include <QString>

class QUrl;

namespace UpdateSecurity {

QString defaultFeedUrl();
bool isTrustedRemoteUrl(const QUrl &url, QString *error = nullptr);
bool verifyFeedSignature(const QByteArray &payload, const QByteArray &base64Signature,
                         QString *error);
bool verifyFileSha256(const QString &path, const QString &expectedSha256, QString *error);

}
