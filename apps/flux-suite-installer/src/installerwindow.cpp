#include "installerwindow.h"
#include "update-security.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSettings>
#include <QStandardPaths>
#include <QStyle>
#include <QSvgRenderer>
#include <QTimer>
#include <QToolButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class BrandMark final : public QWidget {
public:
    explicit BrandMark(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(34, 34);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(QStringLiteral(":/flux/icons/flux-suite.svg"));
        renderer.render(&painter, QRectF(rect()));
    }
};

class SuiteAboutGraphic final : public QWidget {
public:
    explicit SuiteAboutGraphic(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(760, 292);
        setCursor(Qt::PointingHandCursor);
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
            if (QWidget *topLevel = window())
                topLevel->close();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QPainterPath background;
        background.addRoundedRect(QRectF(rect()), 30, 30);
        QRadialGradient gradient(QPointF(250, 82), 760);
        gradient.setColorAt(0.0, QColor(QStringLiteral("#30275d")));
        gradient.setColorAt(0.46, QColor(QStringLiteral("#1d1932")));
        gradient.setColorAt(1.0, QColor(QStringLiteral("#08070b")));
        painter.fillPath(background, gradient);

        QSvgRenderer renderer(QStringLiteral(":/flux/icons/flux-suite.svg"));
        renderer.render(&painter, QRectF(70, 74, 144, 144));

        QFont heading = QApplication::font();
        heading.setPointSize(31);
        heading.setWeight(QFont::DemiBold);
        heading.setLetterSpacing(QFont::PercentageSpacing, 118);
        painter.setFont(heading);
        painter.setPen(QColor(QStringLiteral("#f4f3f8")));
        painter.drawText(QPointF(255, 119), QStringLiteral("FLUX"));
        painter.setPen(QColor(QStringLiteral("#7757ff")));
        painter.drawText(QPointF(255, 169), QStringLiteral("SUITE"));

        QFont version = QApplication::font();
        version.setPointSize(15);
        version.setWeight(QFont::Medium);
        version.setLetterSpacing(QFont::PercentageSpacing, 112);
        painter.setFont(version);
        painter.setPen(QColor(244, 243, 248, 220));
        painter.drawText(QPointF(255, 215), QCoreApplication::applicationVersion().toUpper());

        QFont copyrightFont = QApplication::font();
        copyrightFont.setPointSize(8);
        copyrightFont.setWeight(QFont::Medium);
        copyrightFont.setLetterSpacing(QFont::PercentageSpacing, 108);
        painter.setFont(copyrightFont);
        painter.setPen(QColor(244, 243, 248, 150));
        painter.drawText(QPointF(255, 246),
                         QStringLiteral("© 2026 OMNIATV. ALL RIGHTS RESERVED."));
    }
};

class ProductGlyph final : public QWidget {
public:
    ProductGlyph(QString icon, QString glyph, QColor accent, QWidget *parent = nullptr)
        : QWidget(parent), m_icon(std::move(icon)), m_glyph(std::move(glyph)),
          m_accent(std::move(accent))
    {
        setFixedSize(72, 72);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QSvgRenderer renderer(m_icon);
        if (renderer.isValid()) {
            renderer.render(&painter, QRectF(4, 4, 64, 64));
            return;
        }
        QPainterPath glow;
        glow.addRoundedRect(QRectF(4, 4, 64, 64), 17, 17);
        QLinearGradient gradient(4, 4, 68, 68);
        gradient.setColorAt(0, m_accent.lighter(125));
        gradient.setColorAt(1, m_accent.darker(115));
        painter.fillPath(glow, gradient);
        painter.setPen(QPen(QColor(255, 255, 255, 38), 1));
        painter.drawPath(glow);
        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 17, QFont::Bold));
        painter.drawText(rect(), Qt::AlignCenter, m_glyph);
    }

private:
    QString m_icon;
    QString m_glyph;
    QColor m_accent;
};

class UninstallDialog final : public QDialog {
public:
    explicit UninstallDialog(const Product &product, QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("uninstallDialog"));
        setWindowTitle(QStringLiteral("Uninstall %1").arg(product.name));
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setModal(true);
        setFixedWidth(500);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(28, 26, 28, 24);
        layout->setSpacing(12);
        auto *eyebrow = new QLabel(QStringLiteral("REMOVE APPLICATION"), this);
        eyebrow->setObjectName(QStringLiteral("dialogEyebrow"));
        layout->addWidget(eyebrow);
        auto *title = new QLabel(QStringLiteral("Uninstall %1?").arg(product.name), this);
        title->setObjectName(QStringLiteral("dialogTitle"));
        layout->addWidget(title);
        auto *message = new QLabel(
            QStringLiteral("The installed application files, file associations and Start Menu shortcut will be removed."), this);
        message->setObjectName(QStringLiteral("dialogMessage"));
        message->setWordWrap(true);
        layout->addWidget(message);

        layout->addSpacing(6);

        auto *buttons = new QHBoxLayout;
        buttons->setSpacing(9);
        auto *cancel = new QPushButton(QStringLiteral("Cancel"), this);
        cancel->setObjectName(QStringLiteral("dialogCancel"));
        buttons->addWidget(cancel);
        buttons->addStretch();
        auto *uninstall = new QPushButton(QStringLiteral("Uninstall"), this);
        uninstall->setObjectName(QStringLiteral("dialogDanger"));
        uninstall->setDefault(true);
        buttons->addWidget(uninstall);
        layout->addLayout(buttons);

        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(uninstall, &QPushButton::clicked, this, &QDialog::accept);

        setStyleSheet(QStringLiteral(R"CSS(
            QDialog#uninstallDialog { background: #1d1c20; border: 1px solid #494550; border-radius: 14px; }
            #dialogEyebrow { color: #a895ff; font-size: 10px; font-weight: 800; letter-spacing: 1.5px; }
            #dialogTitle { color: #f4f3f8; font-size: 22px; font-weight: 700; }
            #dialogMessage { color: #c0bcc7; font-size: 12px; }
            #dialogCancel, #dialogPrimary, #dialogDanger { min-height: 34px; border-radius: 8px; padding: 0 14px; font-weight: 700; }
            #dialogCancel { color: #d1cdd7; background: transparent; border: 1px solid #494550; }
            #dialogCancel:hover { background: #302e35; color: white; }
            #dialogPrimary { color: #17151a; background: #f1eff6; border: 0; }
            #dialogPrimary:hover { background: white; }
            #dialogDanger { color: #ffd9df; background: #4a252d; border: 1px solid #7d3544; }
            #dialogDanger:hover { background: #67303b; }
        )CSS"));
    }

};

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("Package size unavailable");
    }
    return QStringLiteral("%1 MB").arg(QString::number(bytes / 1024.0 / 1024.0, 'f', 1));
}

QString appDataEnvironment(const char *name, QStandardPaths::StandardLocation fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? QStandardPaths::writableLocation(fallback) : value;
}

QString powerShellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}
}

InstallerWindow::InstallerWindow(const QString &manifestSource, QWidget *parent)
    : QMainWindow(parent), m_manifestSource(manifestSource), m_engine(this), m_selfUpdater(this)
{
    setWindowTitle(QStringLiteral("Flux Suite"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(1120, 730);
    setMinimumSize(920, 640);

    QSettings settings;
    const QString defaultRoot = QDir(appDataEnvironment("LOCALAPPDATA", QStandardPaths::AppLocalDataLocation))
                                    .filePath(QStringLiteral("Flux Suite"));
    m_applicationInstallRoot = settings.value(QStringLiteral("installation/root"), defaultRoot).toString();
    m_systemWideInstall = settings.value(QStringLiteral("installation/systemWide"), false).toBool();
    const QSize savedSize = settings.value(QStringLiteral("window/size")).toSize();
    if (savedSize.isValid())
        resize(savedSize.expandedTo(minimumSize()));

    buildUi();
    loadManifest(manifestSource);
    if (manifestSource.isEmpty()) {
        QTimer::singleShot(100, this, &InstallerWindow::checkRemoteUpdates);
    }

    connect(&m_engine, &InstallEngine::stageChanged, this,
            [this](const QString &id, const QString &, const QString &detail) {
                if (m_cards.contains(id)) {
                    m_cards[id].status->setText(detail);
                }
                setGlobalMessage(detail);
            });
    connect(&m_engine, &InstallEngine::progressChanged, this,
            [this](const QString &id, qint64 received, qint64 total) {
                if (!m_cards.contains(id)) {
                    return;
                }
                QProgressBar *progress = m_cards[id].progress;
                progress->show();
                if (total > 0) {
                    progress->setRange(0, 1000);
                    progress->setValue(static_cast<int>((received * 1000) / total));
                } else {
                    progress->setRange(0, 0);
                }
            });
    connect(&m_engine, &InstallEngine::finished, this,
            [this](const QString &id, bool success, const QString &message) {
                if (m_cards.contains(id)) {
                    m_cards[id].progress->hide();
                }
                if (success) {
                    m_succeededProducts.insert(id);
                    m_failedProducts.remove(id);
                } else {
                    m_failedProducts.insert(id);
                    m_succeededProducts.remove(id);
                }
                if (const Product *product = findProduct(id)) {
                    refreshCard(*product);
                }
                setGlobalMessage(message, !success);
                QTimer::singleShot(180, this, &InstallerWindow::startNextQueuedProduct);
            });
    connect(&m_selfUpdater, &SelfUpdater::progressChanged, this,
            [this](qint64 received, qint64 total) {
                setGlobalMessage(total > 0
                    ? QStringLiteral("Downloading installer update · %1%").arg((received * 100) / total)
                    : QStringLiteral("Downloading installer update…"));
            });
    connect(&m_selfUpdater, &SelfUpdater::finished, this,
            [this](bool success, const QString &path, const QString &sha256, const QString &message) {
                if (m_selfUpdate) m_selfUpdate->setEnabled(true);
                setGlobalMessage(message, !success);
                if (!success) return;
                QString error;
                if (!m_selfUpdater.applyDownloaded(path, sha256, &error)) {
                    setGlobalMessage(error, true);
                    return;
                }
                setGlobalMessage(QStringLiteral("Opening the verified Flux Suite Setup update…"));
                close();
            });
}

void InstallerWindow::buildUi()
{
    QWidget *surface = new QWidget(this);
    surface->setObjectName(QStringLiteral("surface"));
    setCentralWidget(surface);

    auto *root = new QVBoxLayout(surface);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *titleBar = new QWidget(surface);
    titleBar->setObjectName(QStringLiteral("titleBar"));
    titleBar->setFixedHeight(64);
    auto *titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(24, 0, 14, 0);
    titleLayout->setSpacing(12);
    titleLayout->addWidget(new BrandMark(titleBar));
    auto *brand = new QLabel(QStringLiteral("Flux Suite"), titleBar);
    brand->setObjectName(QStringLiteral("brand"));
    titleLayout->addWidget(brand);
    auto *channel = new QLabel(QStringLiteral("DESKTOP"), titleBar);
    channel->setObjectName(QStringLiteral("channelPill"));
    titleLayout->addWidget(channel);
    titleLayout->addStretch();

    auto *settingsButton = new QPushButton(QStringLiteral("⚙"), titleBar);
    settingsButton->setObjectName(QStringLiteral("windowButton"));
    settingsButton->setToolTip(QStringLiteral("Installation location"));
    connect(settingsButton, &QPushButton::clicked, this, &InstallerWindow::chooseInstallLocation);
    titleLayout->addWidget(settingsButton);
    auto *aboutButton = new QPushButton(QStringLiteral("?"), titleBar);
    aboutButton->setObjectName(QStringLiteral("windowButton"));
    aboutButton->setToolTip(QStringLiteral("About Flux Suite"));
    connect(aboutButton, &QPushButton::clicked, this, &InstallerWindow::showAbout);
    titleLayout->addWidget(aboutButton);
    auto *minimizeButton = new QPushButton(QStringLiteral("−"), titleBar);
    minimizeButton->setObjectName(QStringLiteral("windowButton"));
    connect(minimizeButton, &QPushButton::clicked, this, &QWidget::showMinimized);
    titleLayout->addWidget(minimizeButton);
    auto *closeButton = new QPushButton(QStringLiteral("×"), titleBar);
    closeButton->setObjectName(QStringLiteral("closeButton"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    titleLayout->addWidget(closeButton);
    root->addWidget(titleBar);

    auto *scroll = new QScrollArea(surface);
    scroll->setObjectName(QStringLiteral("scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("page"));
    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(46, 32, 46, 32);
    pageLayout->setSpacing(22);

    auto *hero = new QFrame(page);
    hero->setObjectName(QStringLiteral("hero"));
    hero->setMinimumHeight(194);
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(34, 26, 24, 26);
    auto *heroText = new QVBoxLayout;
    heroText->setSpacing(7);
    auto *eyebrow = new QLabel(QStringLiteral("YOUR CREATIVE WORKSPACE"), hero);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    heroText->addWidget(eyebrow);
    auto *headline = new QLabel(QStringLiteral("Create. Encode. Go live."), hero);
    headline->setObjectName(QStringLiteral("headline"));
    heroText->addWidget(headline);
    auto *subhead = new QLabel(QStringLiteral("Install and keep every Flux app up to date from one place."), hero);
    subhead->setObjectName(QStringLiteral("subhead"));
    subhead->setWordWrap(true);
    heroText->addWidget(subhead);
    heroText->addStretch();
    auto *heroActions = new QHBoxLayout;
    heroActions->setSpacing(12);
    m_installAll = new QPushButton(QStringLiteral("Install all"), hero);
    m_installAll->setObjectName(QStringLiteral("primaryButton"));
    connect(m_installAll, &QPushButton::clicked, this, [this] {
        bool hasUpdates = false;
        for (const Product &product : m_manifest.products)
            hasUpdates = hasUpdates || stateFor(product) == ProductState::UpdateAvailable;
        m_updateAllOperation = hasUpdates;
        m_succeededProducts.clear();
        m_failedProducts.clear();
        for (const Product &product : m_manifest.products) {
            const ProductState state = stateFor(product);
            const bool requested = hasUpdates
                ? state == ProductState::UpdateAvailable
                : state == ProductState::NotInstalled;
            if (requested && !m_queue.contains(product.id)) {
                m_queue.append(product.id);
            }
        }
        for (const Product &product : m_manifest.products)
            refreshCard(product);
        startNextQueuedProduct();
    });
    heroActions->addWidget(m_installAll);
    m_suiteVersion = new QLabel(QStringLiteral("Flux Suite"), hero);
    m_suiteVersion->setObjectName(QStringLiteral("heroVersion"));
    heroActions->addWidget(m_suiteVersion);
    heroActions->addStretch();
    heroText->addLayout(heroActions);
    heroLayout->addLayout(heroText, 1);
    pageLayout->addWidget(hero);

    auto *sectionHeader = new QHBoxLayout;
    auto *appsTitle = new QLabel(QStringLiteral("Apps"), page);
    appsTitle->setObjectName(QStringLiteral("sectionTitle"));
    sectionHeader->addWidget(appsTitle);
    m_updateSummary = new QLabel(page);
    m_updateSummary->setObjectName(QStringLiteral("updateSummary"));
    sectionHeader->addWidget(m_updateSummary);
    sectionHeader->addStretch();
    m_selfUpdate = new QPushButton(QStringLiteral("Update Flux Suite Installer"), page);
    m_selfUpdate->setObjectName(QStringLiteral("secondaryButton"));
    m_selfUpdate->hide();
    connect(m_selfUpdate, &QPushButton::clicked, this, &InstallerWindow::beginSelfUpdate);
    sectionHeader->addWidget(m_selfUpdate);
    m_refreshUpdates = new QPushButton(QStringLiteral("↻  Check for updates"), page);
    m_refreshUpdates->setObjectName(QStringLiteral("textButton"));
    connect(m_refreshUpdates, &QPushButton::clicked, this, &InstallerWindow::checkRemoteUpdates);
    sectionHeader->addWidget(m_refreshUpdates);
    pageLayout->addLayout(sectionHeader);

    m_productLayout = new QVBoxLayout;
    m_productLayout->setSpacing(14);
    pageLayout->addLayout(m_productLayout);
    pageLayout->addStretch();
    scroll->setWidget(page);
    root->addWidget(scroll, 1);

    auto *statusBar = new QWidget(surface);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    statusBar->setFixedHeight(42);
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(22, 0, 22, 0);
    auto *onlineDot = new QLabel(QStringLiteral("●"), statusBar);
    onlineDot->setObjectName(QStringLiteral("onlineDot"));
    statusLayout->addWidget(onlineDot);
    m_globalStatus = new QLabel(QStringLiteral("Ready"), statusBar);
    m_globalStatus->setObjectName(QStringLiteral("globalStatus"));
    statusLayout->addWidget(m_globalStatus);
    statusLayout->addStretch();
    auto *build = new QLabel(QStringLiteral("Installer %1").arg(QCoreApplication::applicationVersion()), statusBar);
    build->setObjectName(QStringLiteral("buildLabel"));
    statusLayout->addWidget(build);
    root->addWidget(statusBar);

    setStyleSheet(QStringLiteral(R"CSS(
        * { color: #f4f3f8; }
        #surface { background: #141316; border: 1px solid #343239; border-radius: 13px; }
        #titleBar { background: #1a191d; border-bottom: 1px solid #2b2930; border-top-left-radius: 13px; border-top-right-radius: 13px; }
        #brand { font-size: 15px; font-weight: 700; letter-spacing: .3px; }
        #channelPill { color: #aaa6b3; background: #29272e; border-radius: 8px; padding: 3px 8px; font-size: 9px; font-weight: 700; }
        #windowButton, #closeButton { border: 0; background: transparent; border-radius: 8px; min-width: 34px; min-height: 34px; font-size: 17px; color: #bbb8c2; }
        #windowButton:hover { background: #302e35; color: white; }
        #closeButton:hover { background: #d7445b; color: white; }
        #scroll, #page { background: #141316; }
        QScrollBar:vertical { background: transparent; width: 9px; margin: 3px; }
        QScrollBar::handle:vertical { background: #3d3a43; border-radius: 3px; min-height: 40px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        #hero { background: #201e25; border: 1px solid #37333f; border-radius: 17px; }
        #eyebrow { color: #9d8cff; font-size: 10px; font-weight: 800; letter-spacing: 1.8px; }
        #headline { font-size: 31px; font-weight: 700; }
        #subhead { color: #aaa6b3; font-size: 13px; }
        #primaryButton { background: #7255f5; border: 0; border-radius: 9px; padding: 9px 19px; font-weight: 700; }
        #primaryButton:hover { background: #8369f8; }
        #primaryButton:disabled { background: #37333f; color: #77727f; }
        #heroVersion { color: #817c89; padding-left: 4px; }
        #sectionTitle { font-size: 22px; font-weight: 700; }
        #updateSummary { color: #9c97a5; background: #252329; border-radius: 9px; padding: 4px 10px; }
        #textButton { border: 0; color: #aaa6b3; background: transparent; padding: 7px 10px; }
        #textButton:hover { color: white; }
        #secondaryButton { background: #292633; border: 1px solid #554a87; border-radius: 8px; padding: 7px 12px; color: #d8d1ff; font-weight: 700; }
        #secondaryButton:hover { background: #373149; }
        #productCard { background: #1d1c20; border: 1px solid #302e35; border-radius: 14px; }
        #productCard:hover { border-color: #494550; background: #211f24; }
        #productName { font-size: 17px; font-weight: 700; }
        #tagline { color: #c0bcc7; font-size: 12px; }
        #description { color: #85818d; font-size: 11px; }
        #productVersion { color: #77727f; font-size: 10px; }
        #changelogLink { color: #a895ff; background: transparent; border: 0; padding: 0; font-size: 10px; text-decoration: underline; }
        #changelogLink:hover { color: #d8d1ff; }
        #stateReady { color: #58d7b2; font-size: 11px; font-weight: 600; }
        #stateUpdate { color: #d3caff; font-size: 11px; font-weight: 600; }
        #stateQueued { color: #f0c96b; font-size: 11px; font-weight: 600; }
        #stateFailed { color: #ff7188; font-size: 11px; font-weight: 600; }
        #stateMissing { color: #8b8792; font-size: 11px; font-weight: 600; }
        #actionButton { background: #f1eff6; color: #1a181e; border: 0; border-radius: 9px; padding: 8px 17px; min-width: 76px; font-weight: 700; }
        #actionButton:hover { background: white; }
        #actionButton:disabled { background: #3b3840; color: #8a8590; }
        #moreButton { background: transparent; border: 1px solid #403d46; border-radius: 9px; min-width: 34px; min-height: 32px; font-size: 18px; }
        #moreButton:hover { background: #302e35; }
        #cardProgress { background: #302d35; border: 0; border-radius: 2px; max-height: 3px; }
        #cardProgress::chunk { background: #7757ff; border-radius: 2px; }
        #statusBar { background: #19181b; border-top: 1px solid #29272d; border-bottom-left-radius: 13px; border-bottom-right-radius: 13px; }
        #onlineDot { color: #28c797; font-size: 10px; }
        #globalStatus { color: #9b97a2; font-size: 10px; }
        #buildLabel { color: #625e68; font-size: 10px; }
        QMenu { background: #27252b; border: 1px solid #44414a; border-radius: 8px; padding: 6px; }
        QMenu::item { padding: 7px 25px 7px 12px; border-radius: 5px; }
        QMenu::item:selected { background: #7457e8; }
        QMessageBox { background: #1d1c20; }
        QMessageBox QLabel { color: #f4f3f8; min-width: 320px; }
        QMessageBox QPushButton { background: #302d35; color: #f4f3f8; border: 1px solid #494550; border-radius: 8px; min-width: 82px; min-height: 32px; padding: 0 10px; }
        QMessageBox QPushButton:hover { background: #413d48; }
    )CSS"));
}

void InstallerWindow::showAbout()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("About Flux Suite"));
    dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setModal(true);
    dialog.setFixedSize(760, 292);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new SuiteAboutGraphic(&dialog));
    dialog.exec();
}

bool InstallerWindow::saveUninstallDialogScreenshot(const QString &destination)
{
    if (m_manifest.products.isEmpty()) return false;
    UninstallDialog dialog(m_manifest.products.constFirst(), this);
    dialog.show();
    QApplication::processEvents();
    QDir().mkpath(QFileInfo(destination).absolutePath());
    const bool saved = dialog.grab().save(destination, "PNG");
    dialog.close();
    return saved;
}

void InstallerWindow::loadManifest(const QString &source)
{
    QString error;
    QString resolved = source;
    if (resolved.isEmpty()) {
        const QString external = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("manifest.json"));
        resolved = QFileInfo(external).isFile() ? external : QStringLiteral(":/flux/manifest.json");
    }
    Manifest manifest = Manifest::load(resolved, &error);
    const bool remoteRequest = QUrl(resolved).scheme() == QStringLiteral("https");
    if (!manifest.isValid() && remoteRequest && m_manifest.isValid()) {
        setGlobalMessage(QStringLiteral("Secure update feed unavailable; keeping the verified local catalog. %1")
                             .arg(error), true);
        return;
    }
    if (!manifest.isValid() && resolved != QStringLiteral(":/flux/manifest.json")) {
        manifest = Manifest::load(QStringLiteral(":/flux/manifest.json"), &error);
        setGlobalMessage(QStringLiteral("Update feed unavailable; using bundled catalog."), true);
    }
    if (!manifest.isValid()) {
        setGlobalMessage(error, true);
        return;
    }
    if (manifest.trustedRemote) {
        QSettings settings;
        const QDateTime lastTrusted = QDateTime::fromString(
            settings.value(QStringLiteral("updates/latestPublishedAt")).toString(), Qt::ISODate);
        if (lastTrusted.isValid() && manifest.publishedAt < lastTrusted) {
            setGlobalMessage(QStringLiteral("The update catalog was rejected because it is older than the last trusted catalog."), true);
            return;
        }
        settings.setValue(QStringLiteral("updates/latestPublishedAt"),
                          manifest.publishedAt.toUTC().toString(Qt::ISODate));
    }
    m_manifest = std::move(manifest);
    m_suiteVersion->setText(QStringLiteral("%1 channel · %2").arg(m_manifest.channel, m_manifest.suiteVersion));
    const QString currentInstallerVersion = QCoreApplication::applicationVersion();
    const QString currentApplicationHash =
        m_manifest.installer.applicationSha256.size() == 64
            ? m_manifest.installer.applicationSha256
            : m_manifest.installer.sha256;
    const bool sameVersionRebuild = m_manifest.installer.version.trimmed()
            == currentInstallerVersion.trimmed()
        && !UpdateSecurity::verifyFileSha256(QCoreApplication::applicationFilePath(),
                                             currentApplicationHash, nullptr);
    const bool selfUpdateAvailable = m_manifest.trustedRemote && m_manifest.installer.isValid()
        && (SelfUpdater::isNewerVersion(m_manifest.installer.version, currentInstallerVersion)
            || sameVersionRebuild);
    m_installerUpdateAvailable = selfUpdateAvailable;
    populateProducts();
    m_selfUpdate->setVisible(selfUpdateAvailable);
    if (selfUpdateAvailable && m_notifiedInstallerVersion != m_manifest.installer.version) {
        m_notifiedInstallerVersion = m_manifest.installer.version;
        QTimer::singleShot(0, this, [this] {
            if (confirmInstallerUpdate(m_manifest.installer.version))
                beginSelfUpdate();
        });
    }
    setGlobalMessage(m_manifest.trustedRemote
        ? QStringLiteral("Signed catalog verified · %1").arg(m_manifest.suiteVersion)
        : QStringLiteral("Offline catalog ready · %1").arg(m_manifest.suiteVersion));
}

void InstallerWindow::checkRemoteUpdates()
{
    if (m_refreshUpdates)
        m_refreshUpdates->setEnabled(false);
    setGlobalMessage(QStringLiteral("Checking the signed update catalog…"));
    loadManifest(m_manifestSource.isEmpty() ? UpdateSecurity::defaultFeedUrl() : m_manifestSource);
    if (m_refreshUpdates)
        m_refreshUpdates->setEnabled(true);
}

void InstallerWindow::populateProducts()
{
    while (QLayoutItem *item = m_productLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
    m_cards.clear();
    int updates = 0;
    int missing = 0;
    for (const Product &product : m_manifest.products) {
        m_productLayout->addWidget(createProductCard(product));
        const ProductState state = stateFor(product);
        if (state == ProductState::UpdateAvailable || state == ProductState::Failed) ++updates;
        else if (state == ProductState::NotInstalled) ++missing;
    }
    const int actionable = updates + missing;
    if (m_installerUpdateAvailable) {
        m_updateSummary->setText(actionable == 0
            ? QStringLiteral("Installer update available")
            : QStringLiteral("Installer + %1 app update(s)").arg(actionable));
    } else {
        m_updateSummary->setText(actionable == 0
            ? QStringLiteral("All apps are current")
            : QStringLiteral("%1 available").arg(actionable));
    }
    m_installAll->setText(actionable == 0 ? QStringLiteral("Everything is up to date")
        : updates > 0 ? QStringLiteral("Update All") : QStringLiteral("Install All"));
    m_installAll->setEnabled(actionable > 0 && !m_engine.isBusy());
}

QWidget *InstallerWindow::createProductCard(const Product &product)
{
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("productCard"));
    card->setMinimumHeight(124);
    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(18, 14, 18, 11);
    outer->setSpacing(9);
    auto *row = new QHBoxLayout;
    row->setSpacing(17);
    row->addWidget(new ProductGlyph(product.icon, product.glyph, product.accent, card));

    auto *copy = new QVBoxLayout;
    copy->setSpacing(2);
    auto *name = new QLabel(product.name, card);
    name->setObjectName(QStringLiteral("productName"));
    copy->addWidget(name);
    auto *tagline = new QLabel(product.tagline, card);
    tagline->setObjectName(QStringLiteral("tagline"));
    copy->addWidget(tagline);
    auto *description = new QLabel(product.description, card);
    description->setObjectName(QStringLiteral("description"));
    description->setWordWrap(true);
    copy->addWidget(description);
    auto *versionRow = new QHBoxLayout;
    versionRow->setSpacing(8);
    auto *version = new QLabel(card);
    version->setObjectName(QStringLiteral("productVersion"));
    versionRow->addWidget(version);
    auto *changelog = new QPushButton(QStringLiteral("Latest changelog"), card);
    changelog->setObjectName(QStringLiteral("changelogLink"));
    changelog->setFlat(true);
    versionRow->addWidget(changelog);
    versionRow->addStretch();
    copy->addLayout(versionRow);
    row->addLayout(copy, 1);

    auto *stateColumn = new QVBoxLayout;
    stateColumn->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *status = new QLabel(card);
    status->setAlignment(Qt::AlignRight);
    stateColumn->addWidget(status);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    auto *action = new QPushButton(card);
    action->setObjectName(QStringLiteral("actionButton"));
    buttons->addWidget(action);
    auto *more = new QPushButton(QStringLiteral("···"), card);
    more->setObjectName(QStringLiteral("moreButton"));
    buttons->addWidget(more);
    stateColumn->addLayout(buttons);
    row->addLayout(stateColumn);
    outer->addLayout(row);

    auto *progress = new QProgressBar(card);
    progress->setObjectName(QStringLiteral("cardProgress"));
    progress->setTextVisible(false);
    progress->hide();
    outer->addWidget(progress);

    CardWidgets widgets;
    widgets.card = card;
    widgets.status = status;
    widgets.version = version;
    widgets.progress = progress;
    widgets.action = action;
    widgets.changelog = changelog;
    widgets.more = more;
    m_cards.insert(product.id, widgets);

    connect(action, &QPushButton::clicked, this, [this, id = product.id] {
        const Product *selected = findProduct(id);
        if (!selected) {
            return;
        }
        const ProductState state = stateFor(*selected);
        if (state == ProductState::Installed || state == ProductState::Succeeded) {
            launchProduct(*selected);
        } else {
            startProduct(id);
        }
    });
    connect(more, &QPushButton::clicked, this, [this, id = product.id, more] {
        if (const Product *selected = findProduct(id)) {
            showProductMenu(*selected, more);
        }
    });
    connect(changelog, &QPushButton::clicked, this, [this, id = product.id] {
        if (const Product *selected = findProduct(id)) showChangelog(*selected);
    });
    refreshCard(product);
    return card;
}

QString InstallerWindow::installPathFor(const Product &product) const
{
    if (product.kind == QStringLiteral("obs-plugin")) {
        // This is the current OBS-recommended Windows plugin location. Unlike
        // the old roaming-profile location, OBS scans this directory directly.
        const QString common = appDataEnvironment("ProgramData", QStandardPaths::GenericDataLocation);
        return QDir(common).filePath(QStringLiteral("obs-studio/plugins/") + product.installFolder);
    }
    if (m_systemWideInstall) {
        const QString programFiles = qEnvironmentVariable("ProgramFiles");
        return QDir(programFiles).filePath(QStringLiteral("Flux Suite/") + product.installFolder);
    }
    return QDir(m_applicationInstallRoot).filePath(product.installFolder);
}

QString InstallerWindow::installedVersion(const Product &product) const
{
    const QString root = installPathFor(product);
    if (!QFileInfo(QDir(root).filePath(product.executable)).isFile()) {
        return {};
    }
    QFile receipt(QDir(root).filePath(QStringLiteral(".flux-install.json")));
    if (receipt.open(QIODevice::ReadOnly)) {
        const QString version = QJsonDocument::fromJson(receipt.readAll()).object()
                                    .value(QStringLiteral("version")).toString();
        if (!version.isEmpty()) {
            return version;
        }
    }
    QFile versionFile(QDir(root).filePath(QStringLiteral("VERSION.txt")));
    if (versionFile.open(QIODevice::ReadOnly)) {
        return QString::fromUtf8(versionFile.readAll()).trimmed();
    }
    return QStringLiteral("Unknown version");
}

QString InstallerWindow::installedPackageHash(const Product &product) const
{
    QFile receipt(QDir(installPathFor(product)).filePath(QStringLiteral(".flux-install.json")));
    if (!receipt.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(receipt.readAll()).object()
        .value(QStringLiteral("sha256")).toString().toLower();
}

ProductState InstallerWindow::stateFor(const Product &product) const
{
    if (m_queue.contains(product.id)) return ProductState::Queued;
    if (m_engine.isBusy() && m_cards.contains(product.id)
        && m_cards.value(product.id).state == ProductState::Working) {
        return ProductState::Working;
    }
    if (m_failedProducts.contains(product.id)) return ProductState::Failed;
    if (m_succeededProducts.contains(product.id)) return ProductState::Succeeded;
    const QString installed = installedVersion(product);
    if (installed.isEmpty()) {
        return ProductState::NotInstalled;
    }
    const QString installedHash = installedPackageHash(product);
    // Legacy/manual installations have no trusted package receipt. Offer one
    // repair/update so they acquire a verifiable build identity.
    const bool sameBuild = !product.sha256.isEmpty()
        && installedHash == product.sha256.toLower();
    return installed == product.version && sameBuild
        ? ProductState::Installed : ProductState::UpdateAvailable;
}

void InstallerWindow::refreshCard(const Product &product)
{
    if (!m_cards.contains(product.id)) {
        return;
    }
    CardWidgets &card = m_cards[product.id];
    const ProductState state = stateFor(product);
    card.state = state;
    card.action->setEnabled(state != ProductState::Working);
    card.more->setEnabled(state != ProductState::Working);
    // Release notes remain useful after an update completes. Their visibility
    // depends only on whether the catalog supplies them, not installation state.
    card.changelog->setVisible(!product.changelog.isEmpty());
    const QString current = installedVersion(product);
    if (state == ProductState::Installed) {
        card.status->setObjectName(QStringLiteral("stateReady"));
        card.status->setText(QStringLiteral("●  Installed"));
        card.action->setText(product.kind == QStringLiteral("application") ? QStringLiteral("Open")
                                                                           : QStringLiteral("Installed"));
        card.action->setEnabled(product.kind == QStringLiteral("application"));
        card.version->setText(QStringLiteral("Version %1 · %2").arg(product.version, humanSize(product.size)));
    } else if (state == ProductState::UpdateAvailable) {
        card.status->setObjectName(QStringLiteral("stateUpdate"));
        card.status->setText(QStringLiteral("↑  Update available"));
        card.action->setText(QStringLiteral("Update"));
        card.version->setText(current == product.version
            ? QStringLiteral("A newer %1 build is available").arg(product.version)
            : QStringLiteral("Installed %1  →  %2").arg(current, product.version));
    } else if (state == ProductState::Queued) {
        card.status->setObjectName(QStringLiteral("stateQueued"));
        card.status->setText(QStringLiteral("○  Queued to update"));
        card.action->setText(QStringLiteral("Queued"));
        card.action->setEnabled(false);
        card.version->setText(QStringLiteral("Waiting for %1").arg(product.version));
    } else if (state == ProductState::Working) {
        card.status->setObjectName(QStringLiteral("stateUpdate"));
        card.status->setText(m_updateAllOperation
            ? QStringLiteral("●  Currently updating") : QStringLiteral("Installing…"));
        card.action->setText(QStringLiteral("Working"));
        card.version->setText(QStringLiteral("Version %1 · %2").arg(product.version, humanSize(product.size)));
    } else if (state == ProductState::Succeeded) {
        card.status->setObjectName(QStringLiteral("stateReady"));
        card.status->setText(QStringLiteral("✓  Successfully updated"));
        card.action->setText(product.kind == QStringLiteral("application")
            ? QStringLiteral("Open") : QStringLiteral("Installed"));
        card.action->setEnabled(product.kind == QStringLiteral("application"));
        card.version->setText(QStringLiteral("Version %1").arg(product.version));
    } else if (state == ProductState::Failed) {
        card.status->setObjectName(QStringLiteral("stateFailed"));
        card.status->setText(QStringLiteral("×  Failed to update"));
        card.action->setText(QStringLiteral("Retry"));
        card.version->setText(QStringLiteral("Update to %1 failed").arg(product.version));
    } else {
        card.status->setObjectName(QStringLiteral("stateMissing"));
        card.status->setText(QStringLiteral("Not installed"));
        card.action->setText(QStringLiteral("Install"));
        card.version->setText(QStringLiteral("Version %1 · %2").arg(product.version, humanSize(product.size)));
    }
    card.status->style()->unpolish(card.status);
    card.status->style()->polish(card.status);
}

void InstallerWindow::startProduct(const QString &productId)
{
    const Product *product = findProduct(productId);
    if (!product) {
        return;
    }
    m_succeededProducts.remove(productId);
    m_failedProducts.remove(productId);
    if (m_engine.isBusy()) {
        if (!m_queue.contains(productId)) {
            m_queue.append(productId);
            setGlobalMessage(QStringLiteral("%1 added to the install queue.").arg(product->name));
        }
        return;
    }
    startProductRelease(*product);
}

void InstallerWindow::startProductRelease(const Product &product)
{
    if (m_engine.isBusy() || m_elevatedProcess) return;
    if (m_cards.contains(product.id)) {
        m_queue.removeAll(product.id);
        m_cards[product.id].state = ProductState::Working;
        refreshCard(product);
    }
    m_installAll->setEnabled(false);
    if (m_systemWideInstall || product.kind == QStringLiteral("obs-plugin")) {
        startElevatedProductInstall(product);
        return;
    }
    m_engine.start(product, QCoreApplication::applicationDirPath(), installPathFor(product));
}

void InstallerWindow::startElevatedProductInstall(const Product &product)
{
#if defined(Q_OS_WIN)
    const QString source = m_manifestSource.isEmpty()
        ? UpdateSecurity::defaultFeedUrl() : m_manifestSource;
    const QStringList childArguments = {
        QStringLiteral("--install-product"), product.id,
        QStringLiteral("--target"), installPathFor(product),
        QStringLiteral("--manifest"), source
    };
    QStringList quotedArguments;
    for (QString argument : childArguments) {
        argument.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        quotedArguments.append(QLatin1Char('"') + argument + QLatin1Char('"'));
    }
    const QString command = QStringLiteral(
        "$p=Start-Process -FilePath %1 -ArgumentList %2 -Verb RunAs -Wait -PassThru; exit $p.ExitCode")
        .arg(powerShellQuote(QCoreApplication::applicationFilePath()),
             powerShellQuote(quotedArguments.join(QLatin1Char(' '))));
    m_elevatedProductId = product.id;
    m_elevatedProcess = new QProcess(this);
    connect(m_elevatedProcess, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus status) {
        const QString id = m_elevatedProductId;
        m_elevatedProductId.clear();
        m_elevatedProcess->deleteLater();
        m_elevatedProcess = nullptr;
        if (const Product *installed = findProduct(id)) refreshCard(*installed);
        if (status == QProcess::NormalExit && exitCode == 0) {
            m_succeededProducts.insert(id);
            m_failedProducts.remove(id);
        } else {
            m_failedProducts.insert(id);
            m_succeededProducts.remove(id);
        }
        if (const Product *installed = findProduct(id)) refreshCard(*installed);
        setGlobalMessage(status == QProcess::NormalExit && exitCode == 0
            ? QStringLiteral("System-wide installation completed.")
            : QStringLiteral("System-wide installation was cancelled or failed."),
            status != QProcess::NormalExit || exitCode != 0);
        QTimer::singleShot(100, this, &InstallerWindow::startNextQueuedProduct);
    });
    m_elevatedProcess->start(QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
         QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), command});
#else
    m_engine.start(product, QCoreApplication::applicationDirPath(), installPathFor(product));
#endif
}

void InstallerWindow::startNextQueuedProduct()
{
    if (m_engine.isBusy()) {
        QTimer::singleShot(120, this, &InstallerWindow::startNextQueuedProduct);
        return;
    }
    if (m_queue.isEmpty()) {
        populateProducts();
        return;
    }
    const QString next = m_queue.takeFirst();
    startProduct(next);
}

void InstallerWindow::launchProduct(const Product &product)
{
    const QString executable = QDir(installPathFor(product)).filePath(product.executable);
    if (!QProcess::startDetached(executable, {}, QFileInfo(executable).absolutePath())) {
        setGlobalMessage(QStringLiteral("Could not open %1.").arg(product.name), true);
    }
}

void InstallerWindow::showChangelog(const Product &product)
{
    if (product.changelog.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("changelogDialog"));
    dialog.setWindowTitle(QStringLiteral("%1 Changelog").arg(product.name));
    dialog.setModal(true);
    dialog.resize(650, 460);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(26, 24, 26, 22);
    layout->setSpacing(12);
    auto *title = new QLabel(QStringLiteral("%1 — Changelog").arg(product.name), &dialog);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);
    const QString from = product.changelog.fromVersion.isEmpty()
        ? installedVersion(product) : product.changelog.fromVersion;
    const QString to = product.changelog.version.isEmpty()
        ? product.version : product.changelog.version;
    auto *range = new QLabel(QStringLiteral("%1  →  %2").arg(from, to), &dialog);
    range->setObjectName(QStringLiteral("changelogRange"));
    layout->addWidget(range);
    auto *body = new QTextBrowser(&dialog);
    body->setObjectName(QStringLiteral("changelogBody"));
    QString html = QStringLiteral("<ul>");
    for (const QString &entry : product.changelog.entries)
        html += QStringLiteral("<li>%1</li>").arg(entry.toHtmlEscaped());
    html += QStringLiteral("</ul>");
    body->setHtml(html);
    layout->addWidget(body, 1);
    auto *close = new QPushButton(QStringLiteral("Close"), &dialog);
    close->setObjectName(QStringLiteral("dialogPrimary"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    dialog.setStyleSheet(QStringLiteral(R"CSS(
        QDialog#changelogDialog { background:#1d1c20; color:#f4f3f8; }
        #dialogTitle { color:#f4f3f8; font-size:22px; font-weight:700; }
        #changelogRange { color:#a895ff; font-size:11px; font-weight:700; }
        #changelogBody { color:#d8d5dd; background:#171619; border:1px solid #3b3840; border-radius:9px; padding:12px; font-size:12px; }
        #dialogPrimary { color:#17151a; background:#f1eff6; border:0; border-radius:8px; min-height:34px; padding:0 18px; font-weight:700; }
    )CSS"));
    dialog.exec();
}

void InstallerWindow::showProductMenu(const Product &product, QPushButton *anchor)
{
    QMenu menu(this);
    QAction *folder = menu.addAction(QStringLiteral("Open installation folder"));
    QAction *reinstall = menu.addAction(QStringLiteral("Repair / reinstall"));
    const bool installed = !installedVersion(product).isEmpty();
    folder->setEnabled(installed);
    reinstall->setEnabled(installed);

    QHash<QAction *, ArchivedRelease> releaseActions;
    if (!product.archives.isEmpty()) {
        QMenu *older = menu.addMenu(QStringLiteral("Install older release"));
        for (const ArchivedRelease &release : product.archives) {
            QAction *action = older->addAction(release.version);
            releaseActions.insert(action, release);
        }
    }
    QAction *uninstall = installed ? menu.addAction(QStringLiteral("Uninstall…")) : nullptr;
    menu.addSeparator();
    QAction *copyVersion = menu.addAction(QStringLiteral("Copy version"));
    QAction *selected = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 5)));
    if (selected == folder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(installPathFor(product)));
    } else if (selected == reinstall) {
        startProduct(product.id);
    } else if (uninstall && selected == uninstall) {
        uninstallProduct(product);
    } else if (releaseActions.contains(selected)) {
        const ArchivedRelease release = releaseActions.value(selected);
        Product archivedProduct = product;
        archivedProduct.version = release.version;
        archivedProduct.package.clear();
        archivedProduct.url = release.url;
        archivedProduct.sha256 = release.sha256;
        archivedProduct.size = release.size;
        archivedProduct.packageRoot = release.packageRoot;
        startProductRelease(archivedProduct);
    } else if (selected == copyVersion) {
        QApplication::clipboard()->setText(product.version);
        setGlobalMessage(QStringLiteral("Version copied to clipboard."));
    }
}

void InstallerWindow::uninstallProduct(const Product &product)
{
    UninstallDialog prompt(product, this);
    if (prompt.exec() != QDialog::Accepted) return;
    if (m_cards.contains(product.id)) m_cards[product.id].state = ProductState::Working;
    m_installAll->setEnabled(false);
    m_engine.startUninstall(product, installPathFor(product));
}

void InstallerWindow::beginSelfUpdate()
{
    if (!m_manifest.trustedRemote || !m_manifest.installer.isValid() || m_selfUpdater.isBusy()) return;
    m_selfUpdate->setEnabled(false);
    setGlobalMessage(QStringLiteral("Downloading the signed Flux Suite Setup update…"));
    m_selfUpdater.download(m_manifest.installer);
}

bool InstallerWindow::confirmInstallerUpdate(const QString &version)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("updateDialog"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground);
    dialog.setModal(true);
    dialog.setFixedSize(570, 310);

    auto *surface = new QFrame(&dialog);
    surface->setObjectName(QStringLiteral("updateSurface"));
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(surface);
    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(30, 26, 30, 26);
    layout->setSpacing(13);

    auto *header = new QHBoxLayout;
    header->addWidget(new BrandMark(surface));
    auto *brand = new QLabel(QStringLiteral("FLUX SUITE UPDATE"), surface);
    brand->setObjectName(QStringLiteral("updateEyebrow"));
    header->addWidget(brand);
    header->addStretch();
    auto *close = new QPushButton(QStringLiteral("×"), surface);
    close->setObjectName(QStringLiteral("updateClose"));
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    header->addWidget(close);
    layout->addLayout(header);

    auto *title = new QLabel(QStringLiteral("A new Flux Suite installer is ready"), surface);
    title->setObjectName(QStringLiteral("updateTitle"));
    layout->addWidget(title);
    auto *description = new QLabel(
        QStringLiteral("Flux Suite will download the signed, self-contained Setup package and open it here. All required Qt components are included."),
        surface);
    description->setObjectName(QStringLiteral("updateDescription"));
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *versionLabel = new QLabel(QStringLiteral("VERSION  %1").arg(version), surface);
    versionLabel->setObjectName(QStringLiteral("updateVersion"));
    layout->addWidget(versionLabel, 0, Qt::AlignLeft);
    layout->addStretch();

    auto *actions = new QHBoxLayout;
    actions->addStretch();
    auto *later = new QPushButton(QStringLiteral("Later"), surface);
    later->setObjectName(QStringLiteral("updateLater"));
    connect(later, &QPushButton::clicked, &dialog, &QDialog::reject);
    actions->addWidget(later);
    auto *install = new QPushButton(QStringLiteral("Download and update"), surface);
    install->setObjectName(QStringLiteral("updateInstall"));
    install->setDefault(true);
    connect(install, &QPushButton::clicked, &dialog, &QDialog::accept);
    actions->addWidget(install);
    layout->addLayout(actions);

    dialog.setStyleSheet(QStringLiteral(R"CSS(
        #updateSurface { background:#1d1c20; border:1px solid #494550; border-radius:15px; }
        #updateEyebrow { color:#a895ff; font-size:10px; font-weight:800; letter-spacing:1.5px; }
        #updateTitle { color:#f4f3f8; font-size:23px; font-weight:700; }
        #updateDescription { color:#aaa6b3; font-size:12px; }
        #updateVersion { color:#d8d1ff; background:#292633; border:1px solid #554a87; border-radius:8px; padding:6px 10px; font-size:10px; font-weight:700; }
        #updateClose { color:#bbb8c2; background:transparent; border:0; border-radius:7px; min-width:32px; min-height:32px; font-size:18px; }
        #updateClose:hover { color:white; background:#d7445b; }
        #updateLater, #updateInstall { min-height:38px; border-radius:9px; padding:0 17px; font-weight:700; }
        #updateLater { color:#d1cdd7; background:transparent; border:1px solid #494550; }
        #updateLater:hover { color:white; background:#302e35; }
        #updateInstall { color:white; background:#7255f5; border:0; }
        #updateInstall:hover { background:#8369f8; }
    )CSS"));
    return dialog.exec() == QDialog::Accepted;
}

void InstallerWindow::chooseInstallLocation()
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("scopeDialog"));
    dialog.setWindowTitle(QStringLiteral("Installation scope"));
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setModal(true);
    dialog.setFixedWidth(540);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 26, 28, 24);
    layout->setSpacing(12);
    auto *eyebrow = new QLabel(QStringLiteral("INSTALLATION SETTINGS"), &dialog);
    eyebrow->setObjectName(QStringLiteral("scopeEyebrow"));
    layout->addWidget(eyebrow);
    auto *title = new QLabel(QStringLiteral("Choose installation scope"), &dialog);
    title->setObjectName(QStringLiteral("scopeTitle"));
    layout->addWidget(title);
    auto *description = new QLabel(
        QStringLiteral("Choose where Flux applications will be installed. This setting applies to new installations and updates."),
        &dialog);
    description->setObjectName(QStringLiteral("scopeMessage"));
    description->setWordWrap(true);
    layout->addWidget(description);
    layout->addSpacing(4);
    auto *currentUser = new QRadioButton(QStringLiteral("Install apps for the current user"), &dialog);
    auto *allUsers = new QRadioButton(QStringLiteral("Install apps system-wide (requires administrator approval)"), &dialog);
    currentUser->setObjectName(QStringLiteral("scopeOption"));
    allUsers->setObjectName(QStringLiteral("scopeOption"));
    currentUser->setChecked(!m_systemWideInstall);
    allUsers->setChecked(m_systemWideInstall);
    layout->addWidget(currentUser);
    layout->addWidget(allUsers);
    auto *note = new QLabel(
        QStringLiteral("The OBS plugin uses OBS Studio's recommended C:\\ProgramData\\obs-studio\\plugins location and always requires administrator approval."),
        &dialog);
    note->setObjectName(QStringLiteral("scopeNote"));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *location = new QPushButton(QStringLiteral("Choose current-user apps folder…"), &dialog);
    location->setObjectName(QStringLiteral("scopeLocation"));
    location->setEnabled(currentUser->isChecked());
    layout->addWidget(location, 0, Qt::AlignLeft);
    auto *pathLabel = new QLabel(QDir::toNativeSeparators(m_applicationInstallRoot), &dialog);
    pathLabel->setObjectName(QStringLiteral("scopePath"));
    pathLabel->setWordWrap(true);
    layout->addWidget(pathLabel);
    layout->addSpacing(6);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), &dialog);
    cancel->setObjectName(QStringLiteral("scopeCancel"));
    buttons->addWidget(cancel);
    auto *apply = new QPushButton(QStringLiteral("Apply"), &dialog);
    apply->setObjectName(QStringLiteral("scopeApply"));
    apply->setDefault(true);
    buttons->addWidget(apply);
    layout->addLayout(buttons);
    connect(currentUser, &QRadioButton::toggled, location, &QWidget::setEnabled);
    connect(location, &QPushButton::clicked, &dialog, [&] {
        const QString selected = QFileDialog::getExistingDirectory(
            &dialog, QStringLiteral("Choose Flux apps location"), m_applicationInstallRoot);
        if (!selected.isEmpty()) {
            m_applicationInstallRoot = QDir::cleanPath(selected);
            pathLabel->setText(QDir::toNativeSeparators(m_applicationInstallRoot));
        }
    });
    connect(apply, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    dialog.setStyleSheet(QStringLiteral(R"CSS(
        QDialog#scopeDialog { background: #1d1c20; border: 1px solid #494550; border-radius: 14px; }
        #scopeEyebrow { color: #a895ff; font-size: 10px; font-weight: 800; letter-spacing: 1.5px; }
        #scopeTitle { color: #f4f3f8; font-size: 22px; font-weight: 700; }
        #scopeMessage { color: #c0bcc7; font-size: 12px; }
        QRadioButton#scopeOption { color: #e7e3ec; background: #27242b; border: 1px solid #3a353f; border-radius: 9px; padding: 13px; spacing: 10px; font-weight: 600; }
        QRadioButton#scopeOption:hover { border-color: #62558e; background: #2c2832; }
        QRadioButton#scopeOption:checked { border-color: #7757ff; background: #30294a; }
        QRadioButton#scopeOption::indicator { width: 16px; height: 16px; }
        #scopeNote { color: #aaa6b3; background: #27242b; border: 1px solid #3a353f; border-radius: 8px; padding: 10px; font-size: 11px; }
        #scopePath { color: #77727f; font-size: 10px; padding-left: 2px; }
        #scopeLocation { color: #d8d1ff; background: #292633; border: 1px solid #554a87; border-radius: 8px; min-height: 32px; padding: 0 12px; font-weight: 700; }
        #scopeLocation:hover { background: #373149; }
        #scopeLocation:disabled { color: #696471; background: #242228; border-color: #38343e; }
        #scopeCancel, #scopeApply { min-height: 34px; border-radius: 8px; padding: 0 16px; font-weight: 700; }
        #scopeCancel { color: #d1cdd7; background: transparent; border: 1px solid #494550; }
        #scopeCancel:hover { background: #302e35; color: white; }
        #scopeApply { color: #17151a; background: #f1eff6; border: 0; }
        #scopeApply:hover { background: white; }
    )CSS"));
    if (dialog.exec() != QDialog::Accepted) return;
    m_systemWideInstall = allUsers->isChecked();
    QSettings settings;
    settings.setValue(QStringLiteral("installation/root"), m_applicationInstallRoot);
    settings.setValue(QStringLiteral("installation/systemWide"), m_systemWideInstall);
    populateProducts();
    setGlobalMessage(m_systemWideInstall
        ? QStringLiteral("New apps will install system-wide under Program Files.")
        : QStringLiteral("New apps will install to %1").arg(QDir::toNativeSeparators(m_applicationInstallRoot)));
}

void InstallerWindow::setGlobalMessage(const QString &message, bool error)
{
    m_globalStatus->setText(message);
    m_globalStatus->setStyleSheet(error ? QStringLiteral("color:#ff7188;") : QString());
}

const Product *InstallerWindow::findProduct(const QString &id) const
{
    for (const Product &product : m_manifest.products) {
        if (product.id == id) {
            return &product;
        }
    }
    return nullptr;
}

bool InstallerWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *nativeMessage = static_cast<MSG *>(message);
    if (nativeMessage && nativeMessage->message == WM_NCHITTEST && !isMaximized()) {
        RECT windowRect{};
        if (GetWindowRect(reinterpret_cast<HWND>(winId()), &windowRect)) {
            const int x = GET_X_LPARAM(nativeMessage->lParam);
            const int y = GET_Y_LPARAM(nativeMessage->lParam);
            const int border = qMax(6, qRound(8.0 * devicePixelRatioF()));
            const bool left = x >= windowRect.left && x < windowRect.left + border;
            const bool right = x <= windowRect.right && x > windowRect.right - border;
            const bool top = y >= windowRect.top && y < windowRect.top + border;
            const bool bottom = y <= windowRect.bottom && y > windowRect.bottom - border;
            if (top && left) *result = HTTOPLEFT;
            else if (top && right) *result = HTTOPRIGHT;
            else if (bottom && left) *result = HTBOTTOMLEFT;
            else if (bottom && right) *result = HTBOTTOMRIGHT;
            else if (left) *result = HTLEFT;
            else if (right) *result = HTRIGHT;
            else if (top) *result = HTTOP;
            else if (bottom) *result = HTBOTTOM;
            else return QMainWindow::nativeEvent(eventType, message, result);
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void InstallerWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() <= 64) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        m_dragging = true;
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void InstallerWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

void InstallerWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

void InstallerWindow::closeEvent(QCloseEvent *event)
{
    if (!m_engine.isBusy()) {
        QSettings().setValue(QStringLiteral("window/size"), size());
        event->accept();
        return;
    }
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("Cancel installation?"),
        QStringLiteral("Flux Suite is still installing files. Do you want to cancel and close?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        m_engine.cancel();
        QSettings().setValue(QStringLiteral("window/size"), size());
        event->accept();
    } else {
        event->ignore();
    }
}
