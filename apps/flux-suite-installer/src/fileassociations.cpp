#include "fileassociations.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QProcess>
#include <QXmlStreamWriter>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <ShlObj.h>
#endif

namespace {

struct Association final {
    QString extension;
    QString progId;
    QString description;
    QString mimeType;
    QString iconResource;
    QString iconFileName;
};

QList<Association> associationsFor(const Product &product)
{
    if (product.id == QStringLiteral("motion-editor")) {
        return {
            {QStringLiteral(".fxmt"), QStringLiteral("FluxMotion.TitleGraphic"),
             QStringLiteral("Flux Motion Title or Graphic"),
             QStringLiteral("application/vnd.omniatv.flux-motion.title+json"),
             QStringLiteral(":/flux/icons/mime-flux-motion-title-graphics.svg"),
             QStringLiteral("flux-motion-title-graphics.ico")},
            {QStringLiteral(".fxmp"), QStringLiteral("FluxMotion.PackedTitleGraphic"),
             QStringLiteral("Packed Flux Motion Title or Graphic"),
             QStringLiteral("application/vnd.omniatv.flux-motion.title-package"),
             QStringLiteral(":/flux/icons/mime-flux-motion-title-graphics.svg"),
             QStringLiteral("flux-motion-title-graphics.ico")},
            {QStringLiteral(".fxmproj"), QStringLiteral("FluxMotion.Project"),
             QStringLiteral("Flux Motion Project"),
             QStringLiteral("application/vnd.omniatv.flux-motion.project+json"),
             QStringLiteral(":/flux/icons/mime-flux-motion-project.svg"),
             QStringLiteral("flux-motion-project.ico")}
        };
    }
    if (product.id == QStringLiteral("encoder")) {
        return {
            {QStringLiteral(".fxe"), QStringLiteral("FluxEncoder.Queue"),
             QStringLiteral("Flux Encoder Queue"),
             QStringLiteral("application/vnd.omniatv.flux-encoder.queue+json"),
             QStringLiteral(":/flux/icons/mime-flux-encoder-queue.svg"),
             QStringLiteral("flux-encoder-queue.ico")}
        };
    }
    return {};
}

bool writeIcon(const QString &resource, const QString &destination, QString *error)
{
    QSvgRenderer renderer(resource);
    if (!renderer.isValid()) {
        if (error) *error = QStringLiteral("The embedded file icon is invalid: %1").arg(resource);
        return false;
    }

    const QList<int> sizes = {16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> images;
    images.reserve(sizes.size());
    for (const int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        QSizeF renderedSize = renderer.defaultSize();
        renderedSize.scale(QSizeF(size, size), Qt::KeepAspectRatio);
        const QRectF target((size - renderedSize.width()) / 2.0,
                            (size - renderedSize.height()) / 2.0,
                            renderedSize.width(), renderedSize.height());
        renderer.render(&painter, target);
        painter.end();

        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            if (error) *error = QStringLiteral("Could not encode the Windows file icon.");
            return false;
        }
        images.append(png);
    }

    QByteArray icon;
    QDataStream stream(&icon, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(images.size());
    quint32 offset = 6 + quint32(images.size()) * 16;
    for (int i = 0; i < images.size(); ++i) {
        const int size = sizes.at(i);
        stream << quint8(size == 256 ? 0 : size) << quint8(size == 256 ? 0 : size)
               << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(images.at(i).size()) << offset;
        offset += quint32(images.at(i).size());
    }
    for (const QByteArray &image : images) stream.writeRawData(image.constData(), image.size());

    QSaveFile output(destination);
    if (!output.open(QIODevice::WriteOnly) || output.write(icon) != icon.size() || !output.commit()) {
        if (error) *error = QStringLiteral("Could not install the file icon: %1").arg(destination);
        return false;
    }
    return true;
}

#if defined(Q_OS_WIN)

bool isSystemWidePath(const QString &installPath)
{
    const QString normalized = QDir::cleanPath(QFileInfo(installPath).absoluteFilePath());
    const QStringList roots = {
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("ProgramW6432"),
        qEnvironmentVariable("ProgramFiles(x86)")
    };
    for (const QString &root : roots) {
        const QString candidate = QDir::cleanPath(root);
        if (!candidate.isEmpty()
            && (normalized.compare(candidate, Qt::CaseInsensitive) == 0
                || normalized.startsWith(candidate + QDir::separator(), Qt::CaseInsensitive))) {
            return true;
        }
    }
    return false;
}

QString classesKey(const QString &relative)
{
    return QStringLiteral("Software\\Classes\\") + relative;
}

bool setRegistryString(HKEY root, const QString &subKey, const QString &name,
                       const QString &value, QString *error)
{
    HKEY key = nullptr;
    const LONG created = RegCreateKeyExW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0,
                                         nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                         nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) {
        if (error) *error = QStringLiteral("Could not create registry key %1 (error %2).")
                                .arg(subKey).arg(created);
        return false;
    }
    const wchar_t *valueName = name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16());
    const DWORD bytes = DWORD((value.size() + 1) * sizeof(wchar_t));
    const LONG written = RegSetValueExW(key, valueName, 0, REG_SZ,
                                        reinterpret_cast<const BYTE *>(value.utf16()), bytes);
    RegCloseKey(key);
    if (written != ERROR_SUCCESS && error) {
        *error = QStringLiteral("Could not write registry key %1 (error %2).")
                     .arg(subKey).arg(written);
    }
    return written == ERROR_SUCCESS;
}

QString registryString(HKEY root, const QString &subKey, const QString &name)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, KEY_QUERY_VALUE, &key)
        != ERROR_SUCCESS) return {};
    DWORD type = 0;
    DWORD bytes = 0;
    const wchar_t *valueName = name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16());
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return {};
    }
    QString value(int(bytes / sizeof(wchar_t)), Qt::Uninitialized);
    if (RegQueryValueExW(key, valueName, nullptr, nullptr,
                         reinterpret_cast<BYTE *>(value.data()), &bytes) != ERROR_SUCCESS) {
        RegCloseKey(key);
        return {};
    }
    RegCloseKey(key);
    if (!value.isEmpty() && value.back() == QChar(u'\0')) value.chop(1);
    return value;
}

bool registerAssociation(HKEY root, const Association &association, const QString &executable,
                         const QString &iconPath, QString *error)
{
    const QString extensionKey = classesKey(association.extension);
    const QString progIdKey = classesKey(association.progId);
    const QString mimeKey = classesKey(QStringLiteral("MIME\\Database\\Content Type\\")
                                       + association.mimeType);
    const QString command = QStringLiteral("\"") + QDir::toNativeSeparators(executable)
        + QStringLiteral("\" \"%1\"");
    const QString icon = QStringLiteral("\"") + QDir::toNativeSeparators(iconPath)
        + QStringLiteral("\",0");

    return setRegistryString(root, extensionKey, {}, association.progId, error)
        && setRegistryString(root, extensionKey, QStringLiteral("Content Type"), association.mimeType, error)
        && setRegistryString(root, progIdKey, {}, association.description, error)
        && setRegistryString(root, progIdKey + QStringLiteral("\\DefaultIcon"), {}, icon, error)
        && setRegistryString(root, progIdKey + QStringLiteral("\\shell\\open\\command"), {}, command, error)
        && setRegistryString(root, mimeKey, QStringLiteral("Extension"), association.extension, error);
}

void unregisterAssociation(HKEY root, const Association &association)
{
    const QString extensionKey = classesKey(association.extension);
    if (registryString(root, extensionKey, {}) == association.progId) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, reinterpret_cast<LPCWSTR>(extensionKey.utf16()), 0,
                          KEY_QUERY_VALUE | KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            RegDeleteValueW(key, nullptr);
            RegDeleteValueW(key, L"Content Type");
            RegCloseKey(key);
            RegDeleteKeyW(root, reinterpret_cast<LPCWSTR>(extensionKey.utf16()));
        }
    }
    const QString mimeKey = classesKey(QStringLiteral("MIME\\Database\\Content Type\\")
                                       + association.mimeType);
    if (registryString(root, mimeKey, QStringLiteral("Extension")) == association.extension) {
        RegDeleteTreeW(root, reinterpret_cast<LPCWSTR>(mimeKey.utf16()));
    }
    const QString progIdKey = classesKey(association.progId);
    RegDeleteTreeW(root, reinterpret_cast<LPCWSTR>(progIdKey.utf16()));
}

#endif

#if !defined(Q_OS_WIN)
QString mimePackagePath(const Product &product)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("mime/packages/flux-suite-") + product.id + QStringLiteral(".xml"));
}

bool registerLinuxAssociations(const Product &product,
                               const QList<Association> &associations,
                               QString *error)
{
    const QString packagePath = mimePackagePath(product);
    if (!QDir().mkpath(QFileInfo(packagePath).absolutePath())) {
        if (error) *error = QStringLiteral("Could not create the XDG MIME package directory.");
        return false;
    }
    QSaveFile package(packagePath);
    if (!package.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not write %1.").arg(packagePath);
        return false;
    }
    QXmlStreamWriter xml(&package);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("mime-info"));
    xml.writeDefaultNamespace(QStringLiteral("http://www.freedesktop.org/standards/shared-mime-info"));
    for (const Association &association : associations) {
        xml.writeStartElement(QStringLiteral("mime-type"));
        xml.writeAttribute(QStringLiteral("type"), association.mimeType);
        xml.writeTextElement(QStringLiteral("comment"), association.description);
        xml.writeEmptyElement(QStringLiteral("glob"));
        xml.writeAttribute(QStringLiteral("pattern"), QStringLiteral("*") + association.extension);
        xml.writeEndElement();
    }
    xml.writeEndElement();
    xml.writeEndDocument();
    if (!package.commit()) {
        if (error) *error = QStringLiteral("Could not activate %1.").arg(packagePath);
        return false;
    }

    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString updateMime = QStandardPaths::findExecutable(QStringLiteral("update-mime-database"));
    if (!updateMime.isEmpty())
        QProcess::execute(updateMime, {QDir(dataRoot).filePath(QStringLiteral("mime"))});
    const QString xdgMime = QStandardPaths::findExecutable(QStringLiteral("xdg-mime"));
    if (!xdgMime.isEmpty()) {
        for (const Association &association : associations)
            QProcess::execute(xdgMime, {QStringLiteral("default"),
                product.id + QStringLiteral(".desktop"), association.mimeType});
    }
    return true;
}
#endif

}

namespace FileAssociations {

bool registerForProduct(const Product &product, const QString &installPath, QString *error)
{
    const QList<Association> associations = associationsFor(product);
    if (associations.isEmpty()) return true;

#if defined(Q_OS_WIN)
    const QString iconDir = QDir(installPath).filePath(QStringLiteral(".flux-file-icons"));
    if (!QDir().mkpath(iconDir)) {
        if (error) *error = QStringLiteral("Could not create the file icon directory.");
        return false;
    }
    QSet<QString> writtenIcons;
    for (const Association &association : associations) {
        if (writtenIcons.contains(association.iconFileName)) continue;
        const QString iconPath = QDir(iconDir).filePath(association.iconFileName);
        if (!writeIcon(association.iconResource, iconPath, error)) return false;
        writtenIcons.insert(association.iconFileName);
    }
    HKEY root = isSystemWidePath(installPath) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const QString executable = QDir(installPath).filePath(product.executable);
    for (const Association &association : associations) {
        const QString iconPath = QDir(iconDir).filePath(association.iconFileName);
        if (!registerAssociation(root, association, executable, iconPath, error)) return false;
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#else
    Q_UNUSED(installPath)
    return registerLinuxAssociations(product, associations, error);
#endif
    return true;
}

void unregisterForProduct(const Product &product, const QString &installPath)
{
#if defined(Q_OS_WIN)
    const QList<Association> associations = associationsFor(product);
    if (associations.isEmpty()) return;
    HKEY root = isSystemWidePath(installPath) ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    for (const Association &association : associations) unregisterAssociation(root, association);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#else
    Q_UNUSED(installPath)
    QFile::remove(mimePackagePath(product));
    const QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString updateMime = QStandardPaths::findExecutable(QStringLiteral("update-mime-database"));
    if (!updateMime.isEmpty())
        QProcess::execute(updateMime, {QDir(dataRoot).filePath(QStringLiteral("mime"))});
#endif
}

}
