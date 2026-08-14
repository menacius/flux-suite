#pragma once
#include <QObject>
#include <QList>
#include <QStringList>
namespace flux {
struct ProviderManifest {
    QString id,name,version,vendor,executionMode,executable,executablePath,manifestPath;
    int abiVersion=0, protocolVersion=0;
    QStringList projectExtensions;
    bool usable=false;
    QString unavailableReason;
};
class ProviderCatalog final:public QObject {
    Q_OBJECT
public:
    explicit ProviderCatalog(QObject *parent=nullptr);
    void discover();
    QList<ProviderManifest> providers() const{return providers_;}
    bool hasUsableProvider() const;
    const ProviderManifest *providerForPath(const QString &path) const;
    const ProviderManifest *providerById(const QString &id) const;
private:
    QList<ProviderManifest> providers_;
};
}
