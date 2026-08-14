#include "providers/render-source-descriptor.h"
#include <QUrl>
namespace flux {
QJsonObject RenderSourceDescriptor::toJson() const{return {{"schema","flux-encoder.render-source"},{"schemaVersion",schemaVersion},{"providerId",providerId},{"projectUri",projectUri},{"compositionId",compositionId},{"renderOptions",renderOptions}};}
RenderSourceDescriptor RenderSourceDescriptor::fromJson(const QJsonObject &json){RenderSourceDescriptor d;d.schemaVersion=json.value("schemaVersion").toInt(1);d.providerId=json.value("providerId").toString();d.projectUri=json.value("projectUri").toString();d.compositionId=json.value("compositionId").toString();d.renderOptions=json.value("renderOptions").toObject();return d;}
RenderSourceDescriptor RenderSourceDescriptor::forProject(const QString &provider,const QString &path){RenderSourceDescriptor d;d.providerId=provider;d.projectUri=QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);d.renderOptions={{"externalDataMode","snapshot"},{"alphaMode","straight"}};return d;}
}
