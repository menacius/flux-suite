#pragma once
#include <QJsonObject>
#include <QString>
namespace flux {
struct RenderSourceDescriptor {
    int schemaVersion=1;
    QString providerId;
    QString projectUri;
    QString compositionId;
    QJsonObject renderOptions;
    QJsonObject toJson() const;
    static RenderSourceDescriptor fromJson(const QJsonObject &json);
    static RenderSourceDescriptor forProject(const QString &providerId,const QString &path);
};
}
