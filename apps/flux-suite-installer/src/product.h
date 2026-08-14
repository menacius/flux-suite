#pragma once

#include <QColor>
#include <QList>
#include <QString>

struct ArchivedRelease final {
    QString version;
    QString url;
    QString sha256;
    qint64 size = 0;
    QString packageRoot;
};

struct ProductChangelog final {
    QString fromVersion;
    QString version;
    QList<QString> entries;

    bool isEmpty() const { return entries.isEmpty(); }
};

struct Product final {
    QString id;
    QString name;
    QString tagline;
    QString description;
    QString version;
    QColor accent;
    QString glyph;
    QString icon;
    QString package;
    QString url;
    QString sha256;
    qint64 size = 0;
    QString installFolder;
    QString executable;
    QString packageRoot;
    QString kind;
    int archiveLimit = 3;
    QList<ArchivedRelease> archives;
    ProductChangelog changelog;
};

enum class ProductState {
    NotInstalled,
    Installed,
    UpdateAvailable,
    Queued,
    Working,
    Succeeded,
    Failed
};
