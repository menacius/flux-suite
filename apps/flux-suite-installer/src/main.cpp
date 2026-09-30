#include "installerwindow.h"
#include "installengine.h"
#include "manifest.h"
#include "selfupdater.h"
#include "update-security.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QSvgRenderer>
#include <QStringList>

namespace {
QIcon makeAppIcon()
{
    QPixmap pixmap(256, 256);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QSvgRenderer renderer(QStringLiteral(":/flux/icons/flux-suite.svg"));
    renderer.render(&painter, QRectF(pixmap.rect()));
    return QIcon(pixmap);
}

QString loadSatoshi()
{
    const QStringList fontResources = {
        QStringLiteral(":/flux/fonts/Satoshi-Regular.otf"),
        QStringLiteral(":/flux/fonts/Satoshi-Medium.otf"),
        QStringLiteral(":/flux/fonts/Satoshi-Bold.otf"),
        QStringLiteral(":/flux/fonts/Satoshi-Black.otf")
    };
    QString family;
    for (const QString &resource : fontResources) {
        const int id = QFontDatabase::addApplicationFont(resource);
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (family.isEmpty() && !families.isEmpty()) {
            family = families.constFirst();
        }
    }
    return family.isEmpty() ? QStringLiteral("Segoe UI") : family;
}
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("Flux Motion"));
    application.setOrganizationDomain(QStringLiteral("omniatv.com"));
    application.setApplicationName(QStringLiteral("Flux Suite"));
    application.setApplicationVersion(QStringLiteral(FLUX_INSTALLER_VERSION));
    application.setWindowIcon(makeAppIcon());
    application.setFont(QFont(loadSatoshi(), 10));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Flux Suite Installer, Downloader and Updater"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption manifestOption({QStringLiteral("m"), QStringLiteral("manifest")},
                                      QStringLiteral("Use a local path or HTTPS update manifest."),
                                      QStringLiteral("source"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Render the main window to a PNG and exit."),
                                        QStringLiteral("path"));
    QCommandLineOption uninstallScreenshotOption(QStringLiteral("screenshot-uninstall-dialog"),
                                                 QStringLiteral("Render the themed uninstall dialog to a PNG and exit."),
                                                 QStringLiteral("path"));
    QCommandLineOption validateOption(QStringLiteral("validate-manifest"),
                                      QStringLiteral("Validate the bundled manifest and exit."));
    QCommandLineOption validateWindowOption(
        QStringLiteral("validate-window-behavior"),
        QStringLiteral("Validate that the main window is movable and resizable."));
    QCommandLineOption verifyOption(QStringLiteral("verify-packages"),
                                    QStringLiteral("Verify every local package against the manifest and exit."));
    QCommandLineOption installProductOption(QStringLiteral("install-product"),
                                            QStringLiteral("Install one product without opening the dashboard."),
                                            QStringLiteral("id"));
    QCommandLineOption targetOption(QStringLiteral("target"),
                                    QStringLiteral("Explicit target for --install-product."),
                                    QStringLiteral("path"));
    QCommandLineOption applySelfUpdateOption(QStringLiteral("apply-self-update"),
                                             QStringLiteral("Apply a verified installer self-update."));
    QCommandLineOption sourceOption(QStringLiteral("source"), QStringLiteral("Self-update source executable."),
                                    QStringLiteral("path"));
    QCommandLineOption targetInstallerOption(QStringLiteral("target-installer"),
                                             QStringLiteral("Self-update target executable."),
                                             QStringLiteral("path"));
    QCommandLineOption expectedShaOption(QStringLiteral("expected-sha256"),
                                         QStringLiteral("Expected self-update SHA-256."),
                                         QStringLiteral("sha256"));
    QCommandLineOption parentPidOption(QStringLiteral("parent-pid"),
                                       QStringLiteral("Parent installer process id."),
                                       QStringLiteral("pid"));
    QCommandLineOption selfUpdateCompleteOption(QStringLiteral("self-update-complete"),
                                                QStringLiteral("Indicate a completed self-update."));
    QCommandLineOption verifyFeedOption(QStringLiteral("verify-feed"),
                                        QStringLiteral("Verify a detached signed update feed and exit."),
                                        QStringLiteral("manifest"));
    parser.addOption(manifestOption);
    parser.addOption(screenshotOption);
    parser.addOption(uninstallScreenshotOption);
    parser.addOption(validateOption);
    parser.addOption(validateWindowOption);
    parser.addOption(verifyOption);
    parser.addOption(installProductOption);
    parser.addOption(targetOption);
    parser.addOption(applySelfUpdateOption);
    parser.addOption(sourceOption);
    parser.addOption(targetInstallerOption);
    parser.addOption(expectedShaOption);
    parser.addOption(parentPidOption);
    parser.addOption(selfUpdateCompleteOption);
    parser.addOption(verifyFeedOption);
    parser.process(application);

    if (parser.isSet(applySelfUpdateOption)) {
        QString error;
        const int result = SelfUpdater::runApplyMode(
            parser.value(sourceOption), parser.value(targetInstallerOption),
            parser.value(expectedShaOption), parser.value(parentPidOption).toUInt(), &error);
        if (result != 0) qCritical("%s", qPrintable(error));
        return result;
    }

    if (parser.isSet(verifyFeedOption)) {
        const QString feedPath = QFileInfo(parser.value(verifyFeedOption)).absoluteFilePath();
        QFile feed(feedPath);
        QFile signature(feedPath + QStringLiteral(".sig"));
        if (!feed.open(QIODevice::ReadOnly) || !signature.open(QIODevice::ReadOnly)) {
            qCritical("Signed feed files are missing.");
            return 24;
        }
        QString error;
        const QByteArray payload = feed.readAll();
        const QByteArray detachedSignature = signature.readAll();
        if (!UpdateSecurity::verifyFeedSignature(payload, detachedSignature, &error)) {
            qCritical("%s", qPrintable(error));
            return 25;
        }
        QByteArray tampered = payload;
        tampered.append('\n');
        if (UpdateSecurity::verifyFeedSignature(tampered, detachedSignature, nullptr)) {
            qCritical("Update signature verification accepted a modified payload.");
            return 26;
        }
        return 0;
    }

    if (parser.isSet(validateOption)) {
        QString error;
        const Manifest manifest = Manifest::load(QStringLiteral(":/flux/manifest.json"), &error);
        if (!manifest.isValid()) {
            qCritical("%s", qPrintable(error));
            return 2;
        }
        return 0;
    }

    if (parser.isSet(verifyOption)) {
        QString error;
        QString source = parser.value(manifestOption);
        if (source.isEmpty()) {
            source = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("manifest.json"));
        }
        const Manifest manifest = Manifest::load(source, &error);
        if (!manifest.isValid()) {
            qCritical("%s", qPrintable(error));
            return 3;
        }
        for (const Product &product : manifest.products) {
            QFile package(product.package);
            if (!package.open(QIODevice::ReadOnly)) {
                qCritical("Package missing: %s", qPrintable(product.package));
                return 4;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            while (!package.atEnd()) {
                hash.addData(package.read(4 * 1024 * 1024));
            }
            if (QString::fromLatin1(hash.result().toHex()) != product.sha256) {
                qCritical("Package hash mismatch: %s", qPrintable(product.name));
                return 5;
            }
        }
        return 0;
    }

    if (parser.isSet(installProductOption)) {
        const QString target = parser.value(targetOption);
        if (target.isEmpty()) {
            qCritical("--target is required with --install-product");
            return 6;
        }
        QString error;
        QString source = parser.value(manifestOption);
        if (source.isEmpty()) {
            source = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("manifest.json"));
        }
        const Manifest manifest = Manifest::load(source, &error);
        const QString requestedId = parser.value(installProductOption);
        const Product *selected = nullptr;
        for (const Product &product : manifest.products) {
            if (product.id == requestedId) {
                selected = &product;
                break;
            }
        }
        if (!selected) {
            qCritical("Unknown product id: %s", qPrintable(requestedId));
            return 7;
        }
        int exitCode = 8;
        InstallEngine engine;
        QObject::connect(&engine, &InstallEngine::finished, &application,
                         [&](const QString &, bool success, const QString &) {
                             exitCode = success ? 0 : 8;
                             QTimer::singleShot(0, &application, &QCoreApplication::quit);
                         });
        engine.start(*selected, QCoreApplication::applicationDirPath(), QFileInfo(target).absoluteFilePath());
        application.exec();
        return exitCode;
    }

    InstallerWindow window(parser.value(manifestOption));
    if (parser.isSet(validateWindowOption)) {
#if defined(Q_OS_LINUX)
        const Qt::WindowFlags flags = window.windowFlags();
        if (flags.testFlag(Qt::FramelessWindowHint) ||
            !flags.testFlag(Qt::WindowTitleHint) ||
            !flags.testFlag(Qt::WindowMinMaxButtonsHint) ||
            !flags.testFlag(Qt::WindowCloseButtonHint) ||
            window.minimumSize() == window.maximumSize()) {
            qCritical("Linux window manager decorations or resize constraints are invalid.");
            return 27;
        }
#endif
        return 0;
    }
    window.show();

    if (parser.isSet(screenshotOption)) {
        const QString destination = QFileInfo(parser.value(screenshotOption)).absoluteFilePath();
        QTimer::singleShot(900, &application, [&window, destination] {
            QDir().mkpath(QFileInfo(destination).absolutePath());
            window.grab().save(destination, "PNG");
            QApplication::quit();
        });
    } else if (parser.isSet(uninstallScreenshotOption)) {
        const QString destination = QFileInfo(parser.value(uninstallScreenshotOption)).absoluteFilePath();
        QTimer::singleShot(500, &application, [&window, destination] {
            window.saveUninstallDialogScreenshot(destination);
            QApplication::quit();
        });
    }
    return application.exec();
}
