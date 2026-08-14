#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <vector>

namespace fxm::editor {

struct RecentProject {
    QString path;
    QString name;
    QDateTime last_opened;
    QByteArray thumbnail_png_base64;
};

/* One persistent, file-path based recent-project model is shared by the File
 * menu and the standalone Home screen. Reading also prunes missing files, so
 * stale entries can never accumulate in either surface. */
class RecentProjects final {
public:
    static constexpr int maximumEntries() noexcept { return 12; }

    static std::vector<RecentProject> entries()
    {
        QSettings settings;
        const QVariantList stored = settings.value(settingsKey()).toList();
        std::vector<RecentProject> result;
        QVariantList cleaned;
        result.reserve(std::min<int>(static_cast<int>(stored.size()),
                                     maximumEntries()));

        for (const QVariant &value : stored) {
            const QVariantMap map = value.toMap();
            const QFileInfo info(map.value(QStringLiteral("path")).toString());
            if (!info.exists() || !info.isFile())
                continue;

            const QString path = info.absoluteFilePath();
            const auto duplicate = std::find_if(
                result.begin(), result.end(), [&path](const RecentProject &item) {
                    return item.path.compare(path, pathCaseSensitivity()) == 0;
                });
            if (duplicate != result.end())
                continue;

            RecentProject item;
            item.path = path;
            item.name = map.value(QStringLiteral("name")).toString().trimmed();
            if (item.name.isEmpty())
                item.name = info.completeBaseName();
            item.last_opened = QDateTime::fromString(
                map.value(QStringLiteral("lastOpened")).toString(),
                Qt::ISODateWithMs);
            if (!item.last_opened.isValid())
                item.last_opened = info.lastModified();
            item.thumbnail_png_base64 =
                map.value(QStringLiteral("thumbnail")).toByteArray();
            result.push_back(item);
            cleaned.push_back(toVariant(item));
            if (static_cast<int>(result.size()) >= maximumEntries())
                break;
        }

        if (cleaned != stored) {
            settings.setValue(settingsKey(), cleaned);
            settings.sync();
        }
        return result;
    }

    static void recordOpened(const QString &path, const QString &name = {},
                             const QByteArray &thumbnail_png_base64 = {})
    {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
            return;

        RecentProject opened;
        opened.path = info.absoluteFilePath();
        opened.name = name.trimmed().isEmpty() ? info.completeBaseName()
                                                : name.trimmed();
        opened.last_opened = QDateTime::currentDateTime();
        opened.thumbnail_png_base64 = thumbnail_png_base64;

        std::vector<RecentProject> current = entries();
        current.erase(std::remove_if(
            current.begin(), current.end(), [&opened](const RecentProject &item) {
                return item.path.compare(opened.path, pathCaseSensitivity()) == 0;
            }), current.end());
        current.insert(current.begin(), opened);
        if (static_cast<int>(current.size()) > maximumEntries())
            current.resize(maximumEntries());

        QVariantList stored;
        for (const RecentProject &item : current)
            stored.push_back(toVariant(item));
        QSettings settings;
        settings.setValue(settingsKey(), stored);
        settings.sync();
    }

    static void remove(const QString &path)
    {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        std::vector<RecentProject> current = entries();
        current.erase(std::remove_if(
            current.begin(), current.end(), [&absolute](const RecentProject &item) {
                return item.path.compare(absolute, pathCaseSensitivity()) == 0;
            }), current.end());
        write(current);
    }

    static void clear()
    {
        QSettings settings;
        settings.remove(settingsKey());
        settings.sync();
    }

private:
    static QString settingsKey()
    {
        return QStringLiteral("standalone/recent-projects-v2");
    }

    static Qt::CaseSensitivity pathCaseSensitivity()
    {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return Qt::CaseInsensitive;
#else
        return Qt::CaseSensitive;
#endif
    }

    static QVariantMap toVariant(const RecentProject &item)
    {
        QVariantMap map;
        map.insert(QStringLiteral("path"), item.path);
        map.insert(QStringLiteral("name"), item.name);
        map.insert(QStringLiteral("lastOpened"),
                   item.last_opened.toString(Qt::ISODateWithMs));
        map.insert(QStringLiteral("thumbnail"), item.thumbnail_png_base64);
        return map;
    }

    static void write(const std::vector<RecentProject> &items)
    {
        QVariantList stored;
        for (const RecentProject &item : items)
            stored.push_back(toVariant(item));
        QSettings settings;
        settings.setValue(settingsKey(), stored);
        settings.sync();
    }
};

} // namespace fxm::editor
