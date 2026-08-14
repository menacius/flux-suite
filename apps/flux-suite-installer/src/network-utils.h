#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

class QUrl;

namespace NetworkUtils {

using ProgressCallback = std::function<void(qint64, qint64)>;

bool downloadBytes(const QUrl &url, qint64 maximumBytes, QByteArray *output, QString *error);
bool downloadFile(const QUrl &url, const QString &destination, qint64 maximumBytes,
                  const ProgressCallback &progress, const std::atomic_bool *cancelled,
                  QString *error);

}
