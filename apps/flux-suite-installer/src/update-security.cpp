#include "update-security.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <cstring>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {
constexpr auto kUpdateHost = "software.omniatv.com";
constexpr auto kUpdatePathPrefix = "/flux-suite/";

QByteArray decodedCoordinate(const QJsonObject &key, const char *name)
{
    return QByteArray::fromBase64(key.value(QString::fromLatin1(name)).toString().toLatin1(),
                                  QByteArray::AbortOnBase64DecodingErrors);
}
}

QString UpdateSecurity::defaultFeedUrl()
{
    return QStringLiteral("https://software.omniatv.com/flux-suite/windows-x64/manifest.json");
}

bool UpdateSecurity::isTrustedRemoteUrl(const QUrl &url, QString *error)
{
    const bool valid = url.isValid() && url.scheme() == QStringLiteral("https")
        && url.host().compare(QString::fromLatin1(kUpdateHost), Qt::CaseInsensitive) == 0
        && (url.port() == -1 || url.port() == 443) && url.userInfo().isEmpty()
        && url.path().startsWith(QString::fromLatin1(kUpdatePathPrefix));
    if (!valid && error) {
        *error = QStringLiteral("Update URLs must use HTTPS under %1%2")
                     .arg(QString::fromLatin1(kUpdateHost), QString::fromLatin1(kUpdatePathPrefix));
    }
    return valid;
}

bool UpdateSecurity::verifyFeedSignature(const QByteArray &payload,
                                         const QByteArray &base64Signature,
                                         QString *error)
{
#ifndef Q_OS_WIN
    Q_UNUSED(payload)
    Q_UNUSED(base64Signature)
    if (error) {
        *error = QStringLiteral("Signed update verification is only available on Windows.");
    }
    return false;
#else
    QFile keyFile(QStringLiteral(":/flux/update-public-key.json"));
    if (!keyFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The update trust anchor is unavailable.");
        return false;
    }
    const QJsonObject key = QJsonDocument::fromJson(keyFile.readAll()).object();
    if (key.value(QStringLiteral("algorithm")).toString()
        != QStringLiteral("ECDSA-P256-SHA256")) {
        if (error) *error = QStringLiteral("The update trust anchor uses an unsupported algorithm.");
        return false;
    }

    const QByteArray x = decodedCoordinate(key, "x");
    const QByteArray y = decodedCoordinate(key, "y");
    const QByteArray signature = QByteArray::fromBase64(base64Signature.trimmed(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
    if (x.size() != 32 || y.size() != 32 || signature.size() != 64) {
        if (error) *error = QStringLiteral("The update signature or public key is malformed.");
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE publicKey = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM,
                                                   nullptr, 0);
    QByteArray blob(sizeof(BCRYPT_ECCKEY_BLOB) + x.size() + y.size(), Qt::Uninitialized);
    auto *header = reinterpret_cast<BCRYPT_ECCKEY_BLOB *>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), x.constData(), static_cast<size_t>(x.size()));
    memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB) + x.size(), y.constData(),
           static_cast<size_t>(y.size()));
    if (status >= 0) {
        status = BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB, &publicKey,
                                     reinterpret_cast<PUCHAR>(blob.data()),
                                     static_cast<ULONG>(blob.size()), 0);
    }
    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    if (status >= 0) {
        status = BCryptVerifySignature(publicKey, nullptr,
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(digest.constData())),
                                       static_cast<ULONG>(digest.size()),
                                       reinterpret_cast<PUCHAR>(const_cast<char *>(signature.constData())),
                                       static_cast<ULONG>(signature.size()), 0);
    }
    if (publicKey) BCryptDestroyKey(publicKey);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) {
        if (error) *error = QStringLiteral("The update catalog signature is not trusted.");
        return false;
    }
    return true;
#endif
}

bool UpdateSecurity::verifyFileSha256(const QString &path, const QString &expectedSha256,
                                      QString *error)
{
    if (expectedSha256.size() != 64) {
        if (error) *error = QStringLiteral("A valid SHA-256 value is required.");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open the downloaded file for verification.");
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(4 * 1024 * 1024));
    const QString actual = QString::fromLatin1(hash.result().toHex()).toLower();
    if (actual != expectedSha256.toLower()) {
        if (error) *error = QStringLiteral("SHA-256 verification failed (expected %1, got %2).")
                                .arg(expectedSha256, actual);
        return false;
    }
    return true;
}
