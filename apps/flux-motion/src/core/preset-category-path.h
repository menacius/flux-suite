#pragma once

#include <QChar>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

inline QStringList fxm_preset_category_path_from_json(const QJsonValue &value)
{
    QStringList parts;
    if (value.isString()) {
        QString path = value.toString().trimmed();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &part : array) {
            if (!part.isString())
                return {};
            parts.push_back(part.toString());
        }
    }

    if (parts.size() > 16)
        return {};
    for (QString &part : parts) {
        part = part.trimmed();
        if (part.isEmpty() || part.size() > 128 ||
            part == QStringLiteral(".") || part == QStringLiteral("..") ||
            part.contains(QLatin1Char('/')) || part.contains(QLatin1Char('\\')))
            return {};
        for (const QChar ch : part) {
            if (ch.category() == QChar::Other_Control)
                return {};
        }
    }
    return parts;
}
