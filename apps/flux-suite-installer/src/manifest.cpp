#include "manifest.h"

#include "network-utils.h"
#include "update-security.h"

#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QUrl>

namespace {
bool isSha256(const QString &value)
{
    if (value.size() != 64) return false;
    for (const QChar character : value) {
        if (!character.isDigit() && (character.toLower() < QLatin1Char('a')
                                     || character.toLower() > QLatin1Char('f'))) return false;
    }
    return true;
}

QUrl resolvedTrustedUrl(const QUrl &origin, const QString &value, QString *error)
{
    const QUrl resolved = origin.resolved(QUrl(value));
    return UpdateSecurity::isTrustedRemoteUrl(resolved, error) ? resolved : QUrl();
}
}

Manifest Manifest::load(const QString &source, QString *error)
{
    const QUrl sourceUrl(source);
    if (sourceUrl.scheme() == QStringLiteral("https")
        || sourceUrl.scheme() == QStringLiteral("http")) {
        if (!UpdateSecurity::isTrustedRemoteUrl(sourceUrl, error)) return {};
        QByteArray payload;
        if (!NetworkUtils::downloadBytes(sourceUrl, 2 * 1024 * 1024, &payload, error)) return {};
        QByteArray signature;
        QUrl signatureUrl = sourceUrl;
        signatureUrl.setPath(sourceUrl.path() + QStringLiteral(".sig"));
        if (!NetworkUtils::downloadBytes(signatureUrl, 4096, &signature, error)
            || !UpdateSecurity::verifyFeedSignature(payload, signature, error)) return {};

        Manifest result = fromJson(payload, error);
        if (!result.isValid()) return {};
        result.origin = source;
        result.trustedRemote = true;
        if (!result.publishedAt.isValid() || !result.expiresAt.isValid()
            || result.expiresAt <= QDateTime::currentDateTimeUtc()
            || result.publishedAt > QDateTime::currentDateTimeUtc().addSecs(300)) {
            if (error) *error = QStringLiteral("The signed update catalog is expired or has invalid dates.");
            return {};
        }
        for (Product &product : result.products) {
            const QUrl productUrl = resolvedTrustedUrl(sourceUrl, product.url, error);
            if (!productUrl.isValid() || !isSha256(product.sha256) || product.size <= 0) return {};
            product.package.clear();
            product.url = productUrl.toString();
            for (ArchivedRelease &archive : product.archives) {
                const QUrl archiveUrl = resolvedTrustedUrl(sourceUrl, archive.url, error);
                if (!archiveUrl.isValid() || !isSha256(archive.sha256) || archive.size <= 0) return {};
                archive.url = archiveUrl.toString();
            }
        }
        if (result.installer.isValid()) {
            const QUrl installerUrl = resolvedTrustedUrl(sourceUrl, result.installer.url, error);
            if (!installerUrl.isValid()) return {};
            result.installer.url = installerUrl.toString();
        }
        if (result.bootstrap.isValid()) {
            const QUrl bootstrapUrl = resolvedTrustedUrl(sourceUrl, result.bootstrap.url, error);
            if (!bootstrapUrl.isValid()) return {};
            result.bootstrap.url = bootstrapUrl.toString();
        }
        return result;
    }
    if (!sourceUrl.scheme().isEmpty() && !QFileInfo(source).isAbsolute()
        && !source.startsWith(QStringLiteral(":"))) {
        if (error) *error = QStringLiteral("Unsupported manifest source: %1").arg(source);
        return {};
    }

    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not open manifest: %1").arg(source);
        }
        return {};
    }
    Manifest result = fromJson(file.readAll(), error);
    result.origin = source;
    if (!source.startsWith(QStringLiteral(":"))) {
        const QDir originDirectory = QFileInfo(source).absoluteDir();
        for (Product &product : result.products) {
            if (!product.package.isEmpty() && !QDir::isAbsolutePath(product.package)
                && QUrl(product.package).scheme().isEmpty()) {
                product.package = originDirectory.absoluteFilePath(product.package);
            }
        }
    }
    return result;
}

Manifest Manifest::fromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Invalid manifest JSON: %1").arg(parseError.errorString());
        }
        return {};
    }

    const QJsonObject root = document.object();
    Manifest result;
    result.schema = root.value(QStringLiteral("schema")).toInt();
    result.suiteVersion = root.value(QStringLiteral("suiteVersion")).toString();
    result.channel = root.value(QStringLiteral("channel")).toString();
    result.publishedAt = QDateTime::fromString(root.value(QStringLiteral("publishedAt")).toString(),
                                               Qt::ISODate);
    result.expiresAt = QDateTime::fromString(root.value(QStringLiteral("expiresAt")).toString(),
                                             Qt::ISODate);

    if (result.schema != 1 && result.schema != 2) {
        if (error) {
            *error = QStringLiteral("Unsupported manifest schema: %1").arg(result.schema);
        }
        return {};
    }

    const QJsonArray products = root.value(QStringLiteral("products")).toArray();
    for (const QJsonValue &value : products) {
        const QJsonObject object = value.toObject();
        Product product;
        product.id = object.value(QStringLiteral("id")).toString();
        product.name = object.value(QStringLiteral("name")).toString();
        product.tagline = object.value(QStringLiteral("tagline")).toString();
        product.description = object.value(QStringLiteral("description")).toString();
        product.version = object.value(QStringLiteral("version")).toString();
        product.accent = QColor(object.value(QStringLiteral("accent")).toString());
        product.glyph = object.value(QStringLiteral("glyph")).toString();
        product.icon = object.value(QStringLiteral("icon")).toString();
        product.package = object.value(QStringLiteral("package")).toString();
        product.url = object.value(QStringLiteral("url")).toString();
        product.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
        product.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
        product.installFolder = object.value(QStringLiteral("installFolder")).toString();
        product.executable = object.value(QStringLiteral("executable")).toString();
        product.packageRoot = object.value(QStringLiteral("packageRoot")).toString();
        product.kind = object.value(QStringLiteral("kind")).toString();
        product.archiveLimit = qBound(0, object.value(QStringLiteral("archiveLimit")).toInt(3), 10);
        const QJsonObject changelogObject = object.value(QStringLiteral("changelog")).toObject();
        product.changelog.fromVersion = changelogObject.value(QStringLiteral("fromVersion")).toString();
        product.changelog.version = changelogObject.value(QStringLiteral("version")).toString();
        for (const QJsonValue &entry : changelogObject.value(QStringLiteral("entries")).toArray()) {
            if (entry.isString() && !entry.toString().trimmed().isEmpty())
                product.changelog.entries.append(entry.toString().trimmed());
        }
        for (const QJsonValue &archiveValue : object.value(QStringLiteral("archives")).toArray()) {
            const QJsonObject archiveObject = archiveValue.toObject();
            ArchivedRelease archive;
            archive.version = archiveObject.value(QStringLiteral("version")).toString();
            archive.url = archiveObject.value(QStringLiteral("url")).toString();
            archive.sha256 = archiveObject.value(QStringLiteral("sha256")).toString().toLower();
            archive.size = static_cast<qint64>(archiveObject.value(QStringLiteral("size")).toDouble());
            archive.packageRoot = archiveObject.value(QStringLiteral("packageRoot")).toString();
            if (!archive.version.isEmpty() && !archive.url.isEmpty() && isSha256(archive.sha256)
                && archive.size > 0) product.archives.append(archive);
        }

        const bool required = !product.id.isEmpty() && !product.name.isEmpty()
            && !product.version.isEmpty() && !product.installFolder.isEmpty()
            && !product.executable.isEmpty() && (!product.package.isEmpty() || !product.url.isEmpty());
        if (!required) {
            if (error) {
                *error = QStringLiteral("Product entry is missing required fields: %1").arg(product.name);
            }
            return {};
        }
        if (!product.sha256.isEmpty() && product.sha256.size() != 64) {
            if (error) {
                *error = QStringLiteral("Product %1 has an invalid SHA-256 value.").arg(product.name);
            }
            return {};
        }
        if (!product.accent.isValid()) {
            product.accent = QColor(QStringLiteral("#7457ff"));
        }
        result.products.append(product);
    }

    const QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
    result.installer.version = installer.value(QStringLiteral("version")).toString();
    result.installer.url = installer.value(QStringLiteral("url")).toString();
    result.installer.sha256 = installer.value(QStringLiteral("sha256")).toString().toLower();
    result.installer.applicationSha256 = installer.value(
        QStringLiteral("applicationSha256")).toString().toLower();
    result.installer.size = static_cast<qint64>(installer.value(QStringLiteral("size")).toDouble());
    if (!result.installer.applicationSha256.isEmpty() &&
        !isSha256(result.installer.applicationSha256)) {
        if (error) *error = QStringLiteral("Installer application hash is invalid.");
        return {};
    }

    const QJsonObject bootstrap = root.value(QStringLiteral("bootstrap")).toObject();
    result.bootstrap.version = bootstrap.value(QStringLiteral("version")).toString();
    result.bootstrap.url = bootstrap.value(QStringLiteral("url")).toString();
    result.bootstrap.sha256 = bootstrap.value(QStringLiteral("sha256")).toString().toLower();
    result.bootstrap.size = static_cast<qint64>(bootstrap.value(QStringLiteral("size")).toDouble());

    if (result.products.isEmpty() && error) {
        *error = QStringLiteral("The manifest contains no products.");
    }
    return result;
}
