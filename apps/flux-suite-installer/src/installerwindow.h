#pragma once

#include "installengine.h"
#include "manifest.h"
#include "selfupdater.h"

#include <QHash>
#include <QMainWindow>
#include <QPoint>
#include <QSet>
#include <QStringList>

class QLabel;
class QProgressBar;
class QProcess;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWidget;

class InstallerWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit InstallerWindow(const QString &manifestSource = {}, QWidget *parent = nullptr);

    bool manifestValid() const { return m_manifest.isValid(); }
    bool saveUninstallDialogScreenshot(const QString &destination);

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    struct CardWidgets {
        QWidget *card = nullptr;
        QLabel *status = nullptr;
        QLabel *version = nullptr;
        QProgressBar *progress = nullptr;
        QPushButton *action = nullptr;
        QPushButton *changelog = nullptr;
        QPushButton *more = nullptr;
        ProductState state = ProductState::NotInstalled;
    };

    void buildUi();
    void loadManifest(const QString &source);
    void checkRemoteUpdates();
    void populateProducts();
    QWidget *createProductCard(const Product &product);
    QString installPathFor(const Product &product) const;
    QStringList installCandidatesFor(const Product &product) const;
    QStringList installedPathsFor(const Product &product) const;
    QString installedPathFor(const Product &product) const;
    QString installedVersion(const Product &product) const;
    QString installedPackageHash(const Product &product) const;
    ProductState stateFor(const Product &product) const;
    void refreshCard(const Product &product);
    void startProduct(const QString &productId);
    void startProductRelease(const Product &product);
    void startElevatedProductInstall(const Product &product);
    void startNextQueuedProduct();
    void launchProduct(const Product &product);
    void showProductMenu(const Product &product, QPushButton *anchor);
    void showChangelog(const Product &product);
    void uninstallProduct(const Product &product);
    void beginSelfUpdate();
    bool confirmInstallerUpdate(const QString &version);
    void chooseInstallLocation();
    void showAbout();
    void setGlobalMessage(const QString &message, bool error = false);
    const Product *findProduct(const QString &id) const;

    Manifest m_manifest;
    QString m_manifestSource;
    QString m_applicationInstallRoot;
    bool m_systemWideInstall = false;
    QProcess *m_elevatedProcess = nullptr;
    QString m_elevatedProductId;
    QVBoxLayout *m_productLayout = nullptr;
    QLabel *m_suiteVersion = nullptr;
    QLabel *m_globalStatus = nullptr;
    QLabel *m_updateSummary = nullptr;
    QPushButton *m_installAll = nullptr;
    QPushButton *m_selfUpdate = nullptr;
    QPushButton *m_refreshUpdates = nullptr;
    QHash<QString, CardWidgets> m_cards;
    QStringList m_queue;
    QSet<QString> m_succeededProducts;
    QSet<QString> m_failedProducts;
    bool m_updateAllOperation = false;
    InstallEngine m_engine;
    SelfUpdater m_selfUpdater;
    QPoint m_dragOffset;
    bool m_dragging = false;
    bool m_installerUpdateAvailable = false;
    QString m_notifiedInstallerVersion;
};
