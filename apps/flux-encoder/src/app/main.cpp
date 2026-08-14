#include "app/main-window.h"
#include "app/export-ipc-server.h"
#include "ui/flux/flux-theme.h"
#include "ui/flux-branding.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QEventLoop>
#include <QMessageBox>
#include <QScreen>
#include <QSettings>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Flux"));
    QCoreApplication::setApplicationName(QStringLiteral("Flux Encoder"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FLUX_ENCODER_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Flux Encoder"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption singleInstanceOption(
        QStringLiteral("single-instance"),
        QStringLiteral("Reuse the Flux Encoder IPC service if it is already running."));
    QCommandLineOption endpointOption(
        QStringLiteral("ipc-endpoint"),
        QStringLiteral("Local IPC endpoint used by Flux Suite applications."),
        QStringLiteral("name"),
        QString::fromLatin1(flux::ExportIpcServer::DefaultEndpoint));
    parser.addOption(singleInstanceOption);
    parser.addOption(endpointOption);
    parser.addPositionalArgument(
        QStringLiteral("sources"),
        QStringLiteral("Media or Flux Motion project files to add to the encoding queue."),
        QStringLiteral("[sources...]"));
    parser.process(app);

    const QString endpoint = parser.value(endpointOption).trimmed();
    if (parser.isSet(singleInstanceOption) &&
        flux::ExportIpcServer::serviceIsRunning(endpoint))
        return 0;

    flux::ExportIpcServer ipcServer;
    QString ipcError;

    // Publish the actual executable path in the native location consumed by
    // Flux Motion. This also makes portable/development builds discoverable
    // after they have been launched once.
    QSettings installation(QSettings::NativeFormat, QSettings::UserScope,
                           QStringLiteral("FluxSuite"), QStringLiteral("Flux Encoder"));
    installation.setValue(QStringLiteral("installation/executable"),
                          QCoreApplication::applicationFilePath());
    installation.setValue(QStringLiteral("installation/path"),
                          QCoreApplication::applicationDirPath());
    installation.sync();

    flux::ui::applyFluxBrandFont(app);
    flux::ui::applyFluxEncoderTheme(app);
    app.setWindowIcon(flux::ui::fluxEncoderIcon());

    const qreal dpr = app.primaryScreen() ? app.primaryScreen()->devicePixelRatio() : 1.0;
    flux::ui::FluxEncoderSplash splash(
        flux::ui::fluxEncoderArtwork(QSize(760, 292), dpr));
    splash.show();
    splash.setProgress(20, QStringLiteral("Loading encoder configuration..."));

    flux::MainWindow window;
    ipcServer.setOpenExportHandler(
        [&window](const QJsonObject &source, const QJsonObject &options,
                  QString *error) {
            return window.openExportFromIpc(source, options, error);
        });
    // Publish the endpoint only after the request handler is ready. Flux Motion
    // treats any connected response as authoritative and therefore must never
    // observe a half-started service.
    const bool ipcReady = ipcServer.start(endpoint, &ipcError);
    if (!ipcReady && parser.isSet(singleInstanceOption)) {
        // Cover a simultaneous-launch race between the initial probe and listen().
        if (flux::ExportIpcServer::serviceIsRunning(endpoint))
            return 0;
        QMessageBox::critical(nullptr, QStringLiteral("Flux Encoder"),
                              QStringLiteral("The export service could not start: %1")
                                  .arg(ipcError));
        return 2;
    }
    splash.setProgress(85, QStringLiteral("Preparing encoding workspace..."));
    window.show();
    window.openSourceFiles(parser.positionalArguments());
    splash.setProgress(100, QStringLiteral("Ready"));
    splash.finish(&window);
    if (!ipcReady) {
        QMessageBox::warning(&window, QStringLiteral("Flux Encoder"),
                             QStringLiteral("Flux Encoder opened, but its export service is unavailable: %1")
                                 .arg(ipcError));
    }
    return app.exec();
}
