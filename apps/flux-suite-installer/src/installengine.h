#pragma once

#include "product.h"

#include <QObject>
#include <QPointer>

#include <atomic>

class QThread;

class InstallEngine final : public QObject {
    Q_OBJECT

public:
    explicit InstallEngine(QObject *parent = nullptr);
    ~InstallEngine() override;

    bool isBusy() const;
    void start(const Product &product, const QString &applicationDir, const QString &installPath);
    void startUninstall(const Product &product, const QString &installPath);
    void cancel();

Q_SIGNALS:
    void stageChanged(const QString &productId, const QString &stage, const QString &detail);
    void progressChanged(const QString &productId, qint64 received, qint64 total);
    void finished(const QString &productId, bool success, const QString &message);

private:
    struct Result {
        bool success = false;
        QString message;
    };

    Result performInstall(Product product, QString applicationDir, QString installPath);
    Result performUninstall(Product product, QString installPath);
    QString acquirePackage(const Product &product, const QString &applicationDir,
                           const QString &temporaryDir, QString *error);
    bool extractPackage(const QString &archive, const QString &destination, QString *error);
    bool activateStaging(const QString &staging, const Product &product,
                         const QString &target, QString *error);
    void createStartMenuShortcut(const Product &product, const QString &installPath);
    void removeStartMenuShortcut(const Product &product);

    QPointer<QThread> m_thread;
    std::atomic_bool m_cancelled = false;
};
