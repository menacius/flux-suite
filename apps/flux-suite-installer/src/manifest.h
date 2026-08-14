#pragma once

#include "product.h"

#include <QList>
#include <QDateTime>
#include <QString>

struct InstallerRelease final {
    QString version;
    QString url;
    QString sha256;
    QString applicationSha256;
    qint64 size = 0;

    bool isValid() const
    {
        return !version.isEmpty() && !url.isEmpty() && sha256.size() == 64 && size > 0;
    }
};

class Manifest final {
public:
    static Manifest load(const QString &source, QString *error);
    static Manifest fromJson(const QByteArray &json, QString *error);

    bool isValid() const { return !products.isEmpty(); }

    int schema = 0;
    QString suiteVersion;
    QString channel;
    QDateTime publishedAt;
    QDateTime expiresAt;
    InstallerRelease installer;
    InstallerRelease bootstrap;
    QList<Product> products;
    QString origin;
    bool trustedRemote = false;
};
