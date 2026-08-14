#include "providers/provider-catalog.h"
#include "flux-encoder-provider-version.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
namespace flux {
ProviderCatalog::ProviderCatalog(QObject *parent):QObject(parent){}
void ProviderCatalog::discover(){providers_.clear();QStringList roots={QCoreApplication::applicationDirPath()+QStringLiteral("/providers"),QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+QStringLiteral("/providers")};for(const QString &root:roots){QDir dir(root);for(const QString &folder:dir.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){const QString path=dir.filePath(folder+QStringLiteral("/provider.json"));QFile f(path);if(!f.open(QIODevice::ReadOnly))continue;QJsonParseError e;const auto doc=QJsonDocument::fromJson(f.readAll(),&e);if(e.error!=QJsonParseError::NoError||!doc.isObject())continue;const auto j=doc.object();ProviderManifest p;p.id=j.value("id").toString();p.name=j.value("name").toString();p.version=j.value("version").toString();p.vendor=j.value("vendor").toString();p.abiVersion=j.value("fluxEncoderProviderAbi").toInt();p.protocolVersion=j.value("protocolVersion").toInt(1);p.executionMode=j.value("executionMode").toString("out-of-process");p.executable=j.value("executable").toString();p.executablePath=QFileInfo(dir.filePath(folder+QLatin1Char('/')+p.executable)).absoluteFilePath();p.manifestPath=path;for(const auto &v:j.value("projectExtensions").toArray())p.projectExtensions<<v.toString();if(p.abiVersion!=int(FLUX_ENCODER_RENDER_PROVIDER_ABI_VERSION))p.unavailableReason=tr("Provider ABI %1 is incompatible with host ABI %2.").arg(p.abiVersion).arg(FLUX_ENCODER_RENDER_PROVIDER_ABI_VERSION);else if(p.executionMode!=QStringLiteral("out-of-process"))p.unavailableReason=tr("Only out-of-process providers are accepted by this host version.");else if(p.executable.isEmpty()||!QFileInfo(p.executablePath).isExecutable())p.unavailableReason=tr("Provider executable is not installed.");else p.usable=true;if(!p.id.isEmpty())providers_<<p;}}}
bool ProviderCatalog::hasUsableProvider() const{for(const auto &p:providers_)if(p.usable)return true;return false;}
const ProviderManifest *ProviderCatalog::providerForPath(const QString &path) const{const QString suffix=QStringLiteral("*.%1").arg(QFileInfo(path).suffix().toLower());for(const auto &p:providers_)if(p.usable)for(const QString &pattern:p.projectExtensions)if(pattern.toLower()==suffix)return &p;return nullptr;}
const ProviderManifest *ProviderCatalog::providerById(const QString &id) const{for(const auto &p:providers_)if(p.id==id)return &p;return nullptr;}
}
