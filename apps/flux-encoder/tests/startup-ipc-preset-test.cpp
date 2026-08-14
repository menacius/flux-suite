#include "app/main-window.h"
#include "app/profile-editor-dialog.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QToolButton>
#include <cstdio>
#include <functional>

namespace {
bool waitUntil(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return condition();
}
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FluxEncoderTests"));
    QCoreApplication::setApplicationName(QStringLiteral("StartupIpcPreset"));
    QStandardPaths::setTestModeEnabled(true);

    const QString appData = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation);
    QDir(appData).removeRecursively();

    QTemporaryDir projectStore;
    if (!projectStore.isValid()) return 1;
    const QString storePath = projectStore.filePath(QStringLiteral("titles.json"));
    QFile store(storePath);
    if (!store.open(QIODevice::WriteOnly)) return 2;
    const QJsonObject title{
        {QStringLiteral("id"), QStringLiteral("startup-title")},
        {QStringLiteral("name"), QStringLiteral("Startup IPC Preset")},
        {QStringLiteral("width"), 1920},
        {QStringLiteral("height"), 1080},
        {QStringLiteral("frame_rate"), 25.0},
        {QStringLiteral("duration"), 2.0},
        {QStringLiteral("layers"), QJsonArray{}}};
    QJsonObject cancelledTitle=title;
    cancelledTitle.insert(QStringLiteral("id"),QStringLiteral("cancelled-title"));
    cancelledTitle.insert(QStringLiteral("name"),QStringLiteral("Cancelled IPC Preset"));
    store.write(QJsonDocument(QJsonObject{
        {QStringLiteral("titles"), QJsonArray{title,cancelledTitle}}}).toJson());
    store.close();

    int result = 0;
    {
        flux::MainWindow window;
        auto *queue = window.findChild<QTableWidget *>(QStringLiteral("EncodingQueue"));
        auto *presets = window.findChild<QTreeWidget *>(QStringLiteral("PresetBrowser"));
        if (!queue || !presets) return 3;

        const QJsonObject source{
            {QStringLiteral("application"), QStringLiteral("flux-motion")},
            {QStringLiteral("project_id"), QStringLiteral("startup-title")},
            {QStringLiteral("project_name"), QStringLiteral("Startup IPC Preset")},
            {QStringLiteral("project_store_path"), storePath},
            {QStringLiteral("project_scope"), QStringLiteral("Startup Test")}};
        const QJsonObject options{{QStringLiteral("activate_window"), false}};
        QTimer acceptDialog;
        acceptDialog.setInterval(10);
        QObject::connect(&acceptDialog,&QTimer::timeout,&window,[&]{
            if(auto *dialog=window.findChild<flux::ProfileEditorDialog *>()){
                acceptDialog.stop();
                dialog->accept();
            }
        });
        acceptDialog.start();
        QString error;
        if (!window.openExportFromIpc(source, options, &error) || !error.isEmpty())
            return 4;

        // The request must not create a partially initialized queue item.
        if (queue->rowCount() != 0) return 5;
        if (!waitUntil([&]() {
                return queue->rowCount() == 1 && presets->topLevelItemCount() > 0;
            }, 20000))
            return 6;

        const auto *presetButton =
            qobject_cast<QToolButton *>(queue->cellWidget(0, 2));
        const auto *formatButton =
            qobject_cast<QToolButton *>(queue->cellWidget(0, 1));
        if (!presetButton || presetButton->text().trimmed().isEmpty() ||
            !formatButton || formatButton->text().trimmed().isEmpty()) {
            std::fprintf(stderr, "preset='%s' format='%s'\n",
                         presetButton ? presetButton->text().toUtf8().constData() : "<null>",
                         formatButton ? formatButton->text().toUtf8().constData() : "<null>");
            result = 7;
        }
        if(!presetButton || !presetButton->property("queueSelected").toBool())
            return 11;

        QJsonObject cancelSource=source;
        cancelSource.insert(QStringLiteral("project_id"),QStringLiteral("cancelled-title"));
        cancelSource.insert(QStringLiteral("project_name"),QStringLiteral("Cancelled IPC Preset"));
        QTimer cancelDialog;
        cancelDialog.setInterval(10);
        QObject::connect(&cancelDialog,&QTimer::timeout,&window,[&]{
            if(auto *dialog=window.findChild<flux::ProfileEditorDialog *>()){
                cancelDialog.stop();
                dialog->reject();
            }
        });
        cancelDialog.start();
        error.clear();
        if(!window.openExportFromIpc(cancelSource,options,&error)||!error.isEmpty())
            return 8;
        if(!waitUntil([&]{return !cancelDialog.isActive();},5000))return 9;
        QApplication::processEvents();
        if(queue->rowCount()!=1)return 10;

        flux::RenderProfile sourceOverride;
        sourceOverride.name=QStringLiteral("Override Test");
        sourceOverride.container=QStringLiteral("mp4");
        sourceOverride.extension=QStringLiteral("mp4");
        sourceOverride.videoEncoder=QStringLiteral("libx264");
        sourceOverride.width=1280;
        sourceOverride.height=720;
        sourceOverride.frameRate=50.0;
        flux::FfmpegCapabilities emptyCapabilities;
        flux::ProfileEditorDialog matchDialog({}, {}, emptyCapabilities,
                                               sourceOverride,2000,QString());
        auto *matchButton=matchDialog.findChild<QPushButton *>(
            QStringLiteral("FluxEncoderMatchSourceButton"));
        if(!matchButton)return 12;
        matchButton->click();
        const flux::RenderProfile matched=matchDialog.profile();
        if(matched.width!=0||matched.height!=0||matched.frameRate!=0.0)return 13;
    }

    QDir(appData).removeRecursively();
    return result;
}
