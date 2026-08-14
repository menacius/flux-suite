#include "app/main-window.h"
#include "ui/preview-controls.h"
#include "app/profile-editor-dialog.h"
#include "core/ffmpeg-capabilities.h"
#include "core/job-builder.h"
#include "core/profile-catalog.h"
#include "ui/flux/flux-modern-controls.h"
#include "ui/flux-branding.h"
#include "providers/provider-catalog.h"
#include "providers/render-provider-support.h"
#include "providers/render-source-descriptor.h"

#include <QtConcurrent>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDockWidget>
#include <QCloseEvent>
#include <QComboBox>
#include <QPalette>
#include <QColor>
#include <QSettings>
#include <QFile>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QDrag>
#include <QAbstractItemView>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QPixmap>
#include <QKeySequence>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QFrame>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>

namespace flux {
namespace {
constexpr char kPresetMimeType[] = "application/x-flux-encoder-preset";

class PresetTreeWidget final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
protected:
    void startDrag(Qt::DropActions supportedActions) override {
        QTreeWidgetItem *item=currentItem();
        if(!item || item->data(0,Qt::UserRole+1).toString()!=QStringLiteral("profile") || item->isDisabled()) return;
        auto *mime=new QMimeData();
        mime->setData(kPresetMimeType,item->data(0,Qt::UserRole).toString().toUtf8());
        auto *drag=new QDrag(this); drag->setMimeData(mime); drag->setPixmap(viewport()->grab(visualItemRect(item)));
        drag->exec(Qt::CopyAction);
        Q_UNUSED(supportedActions);
    }
};

class QueueDropTable final : public QTableWidget {
public:
    using ApplyCallback=std::function<bool(const QString&,int,QString*)>;
    explicit QueueDropTable(QWidget *parent=nullptr):QTableWidget(parent){setAcceptDrops(true);viewport()->setAcceptDrops(true);setDropIndicatorShown(true);}
    ApplyCallback applyPreset;
protected:
    void dragEnterEvent(QDragEnterEvent *e) override { if(e->mimeData()->hasFormat(kPresetMimeType)){e->setDropAction(Qt::CopyAction);e->acceptProposedAction();}else QTableWidget::dragEnterEvent(e); }
    void dragMoveEvent(QDragMoveEvent *e) override { if(e->mimeData()->hasFormat(kPresetMimeType)){e->setDropAction(Qt::CopyAction);e->acceptProposedAction();}else QTableWidget::dragMoveEvent(e); }
    void dropEvent(QDropEvent *e) override {
        if(!e->mimeData()->hasFormat(kPresetMimeType)){QTableWidget::dropEvent(e);return;}
        const QString uuid=QString::fromUtf8(e->mimeData()->data(kPresetMimeType));
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
        const int row=rowAt(int(e->position().y()));
#else
        const int row=rowAt(e->pos().y());
#endif
        QString error;
        if(applyPreset && applyPreset(uuid,row,&error)){e->setDropAction(Qt::CopyAction);e->accept();}
        else {e->ignore(); if(!error.isEmpty()) QToolTip::showText(viewport()->mapToGlobal(e->position().toPoint()),error,this);}
    }
};

class FluxEncoderDockTitleBar final : public QWidget {
public:
    explicit FluxEncoderDockTitleBar(const QString &title, QDockWidget *dock)
        : QWidget(dock), dock_(dock)
    {
        setObjectName(QStringLiteral("FluxEncoderDockTitleBar"));
        setFixedHeight(25);
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(7, 0, 3, 0);
        layout->setSpacing(3);
        title_ = new QLabel(title, this);
        title_->setObjectName(QStringLiteral("FluxEncoderDockTitle"));
        layout->addWidget(title_);
        layout->addStretch(1);
        auto *menu = new QToolButton(this);
        menu->setObjectName(QStringLiteral("FluxEncoderDockMenuButton"));
        menu->setText(QStringLiteral("≡"));
        menu->setAutoRaise(true);
        menu->setPopupMode(QToolButton::InstantPopup);
        auto *dockMenu = new QMenu(menu);
        QAction *floating = dockMenu->addAction(QObject::tr("Floating"));
        floating->setCheckable(true);
        floating->setChecked(dock_->isFloating());
        // Only user activation may change the dock's floating state. Connecting
        // toggled() here creates a re-entrant setFloating() call when Qt emits
        // topLevelChanged() while a dock drag is already in progress.
        QObject::connect(floating, &QAction::triggered, dock_, &QDockWidget::setFloating);
        QObject::connect(dock_, &QDockWidget::topLevelChanged, floating,
                         [floating](bool topLevel) {
                             const QSignalBlocker blocker(floating);
                             floating->setChecked(topLevel);
                         });
        dockMenu->addSeparator();
        dockMenu->addAction(QObject::tr("Close Panel"), dock_, &QDockWidget::hide);
        menu->setMenu(dockMenu);
        layout->addWidget(menu);
    }
private:
    QDockWidget *dock_ = nullptr;
    QLabel *title_ = nullptr;
};

QLabel *panelTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text.toUpper(), parent);
    QFont font = label->font(); font.setBold(true); font.setPointSizeF(font.pointSizeF() * 0.92);
    label->setFont(font); label->setContentsMargins(6, 4, 6, 4);
    label->setObjectName(QStringLiteral("PanelTitle"));
    return label;
}
QPushButton *queueSettingsLink(const QString &text, QWidget *parent)
{
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("QueueSettingsLink"));
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setStyleSheet(QStringLiteral(
        "QPushButton#QueueSettingsLink{border:0;background:transparent;text-align:left;padding:1px 4px;color:#42a274;}"
        "QPushButton#QueueSettingsLink:hover{text-decoration:underline;color:#62bd90;}"
        "QPushButton#QueueSettingsLink:pressed{color:#8ad0ad;}"
        "QPushButton#QueueSettingsLink:focus{border:1px solid #42a274;padding:0px 3px;}"
        "QPushButton#QueueSettingsLink:disabled{color:palette(disabled, text);}"
        "QPushButton#QueueSettingsLink[queueSelected=\"true\"],"
        "QPushButton#QueueSettingsLink[queueSelected=\"true\"]:hover{color:palette(highlighted-text);}"));
    return button;
}

QToolButton *queueSettingsSplitLink(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setObjectName(QStringLiteral("QueueSettingsSplitLink"));
    button->setPopupMode(QToolButton::MenuButtonPopup);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    button->setStyleSheet(QStringLiteral(
        "QToolButton#QueueSettingsSplitLink{border:0;background:transparent;text-align:left;padding:1px 18px 1px 4px;color:#42a274;}"
        "QToolButton#QueueSettingsSplitLink:hover{text-decoration:underline;color:#62bd90;}"
        "QToolButton#QueueSettingsSplitLink:pressed,QToolButton#QueueSettingsSplitLink:checked{color:#8ad0ad;}"
        "QToolButton#QueueSettingsSplitLink:focus{border:1px solid #42a274;padding:0px 17px 0px 3px;}"
        "QToolButton#QueueSettingsSplitLink:disabled{color:palette(disabled, text);}"
        "QToolButton#QueueSettingsSplitLink::menu-button{width:16px;border:0;border-left:1px solid palette(mid);}"
        "QToolButton#QueueSettingsSplitLink[queueSelected=\"true\"],"
        "QToolButton#QueueSettingsSplitLink[queueSelected=\"true\"]:hover{color:palette(highlighted-text);}"));
    return button;
}

QString timeText(qint64 seconds)
{
    if (seconds < 0) return QStringLiteral("—");
    return QStringLiteral("%1:%2:%3").arg(seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

qint64 probeDurationMs(const QString &ffprobePath, const QString &sourcePath)
{
    if (ffprobePath.isEmpty() || sourcePath.isEmpty()) return 0;
    QProcess process;
    process.start(ffprobePath, {QStringLiteral("-v"), QStringLiteral("error"),
                                QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                                QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                                sourcePath});
    if (!process.waitForFinished(8000)) { process.kill(); process.waitForFinished(); return 0; }
    bool ok = false;
    const double seconds = QString::fromUtf8(process.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok && seconds > 0.0 ? qRound64(seconds * 1000.0) : 0;
}

struct MotionProjectSource {
    QString storeFile;
    QJsonObject title;
};

bool findMotionProject(const QString &storePath, const QString &projectId,
                       MotionProjectSource *result, QString *error)
{
    const QFileInfo supplied(storePath);
    if (!supplied.exists()) {
        if (error) *error = QObject::tr("Flux Motion's project store does not exist: %1")
                                .arg(QDir::toNativeSeparators(storePath));
        return false;
    }

    QStringList candidates;
    if (supplied.isFile()) {
        candidates << supplied.absoluteFilePath();
    } else {
        const QDir root(supplied.absoluteFilePath());
        const QDir stores(root.filePath(QStringLiteral("scene-collection-titles")));
        for (const QFileInfo &file : stores.entryInfoList(
                 {QStringLiteral("*.json")}, QDir::Files, QDir::Time))
            candidates << file.absoluteFilePath();
        for (const QFileInfo &file : root.entryInfoList(
                 {QStringLiteral("*.json")}, QDir::Files, QDir::Time))
            candidates << file.absoluteFilePath();
    }

    constexpr qint64 kMaximumProjectStoreBytes = 256 * 1024 * 1024;
    for (const QString &candidate : candidates) {
        QFile file(candidate);
        if (file.size() > kMaximumProjectStoreBytes ||
            !file.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError)
            continue;
        QJsonArray titles;
        if (document.isArray())
            titles = document.array();
        else if (document.isObject())
            titles = document.object().value(QStringLiteral("titles")).toArray();
        for (const QJsonValue &value : titles) {
            const QJsonObject title = value.toObject();
            if (title.value(QStringLiteral("id")).toString() == projectId) {
                if (result) {
                    result->storeFile = candidate;
                    result->title = title;
                }
                if (error) error->clear();
                return true;
            }
        }
    }

    if (error)
        *error = QObject::tr("The saved Flux Motion project was not found in the supplied project store.");
    return false;
}

QString safeOutputBaseName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    return name.isEmpty() ? QStringLiteral("Flux Motion Export") : name;
}

QString formatEstimatedBytes(double bytes)
{
    const QStringList units{QStringLiteral("B"),QStringLiteral("KB"),QStringLiteral("MB"),QStringLiteral("GB"),QStringLiteral("TB")};
    int unit=0; while(bytes>=1024.0 && unit<units.size()-1){bytes/=1024.0;++unit;}
    return QStringLiteral("%1 %2").arg(bytes,0,'f',unit<2?0:2).arg(units.at(unit));
}


QString profileFormatKey(const RenderProfile &profile)
{
    const QString encoder = profile.videoEncoder.toLower();
    const QString audio = profile.audioEncoder.toLower();
    if (encoder.contains(QStringLiteral("h264")) || encoder.contains(QStringLiteral("x264")))
        return QStringLiteral("h264");
    if (encoder.contains(QStringLiteral("hevc")) || encoder.contains(QStringLiteral("h265")) || encoder.contains(QStringLiteral("x265")))
        return QStringLiteral("hevc");
    if (encoder.contains(QStringLiteral("av1")))
        return QStringLiteral("av1");
    if (encoder.contains(QStringLiteral("prores")))
        return QStringLiteral("prores");
    if (encoder.contains(QStringLiteral("dnx")))
        return QStringLiteral("dnx");
    if (encoder.contains(QStringLiteral("mpeg2")))
        return QStringLiteral("mpeg2");
    if (encoder.contains(QStringLiteral("vp9")))
        return QStringLiteral("vp9");
    if (encoder.isEmpty()) {
        if (audio.contains(QStringLiteral("mp3"))) return QStringLiteral("mp3");
        if (audio.contains(QStringLiteral("flac"))) return QStringLiteral("flac");
        if (audio.startsWith(QStringLiteral("pcm_"))) return QStringLiteral("wav");
        if (audio.contains(QStringLiteral("aac"))) return QStringLiteral("aac-audio");
        return QStringLiteral("audio");
    }
    return profile.container.toLower();
}

QString profileFormatLabel(const RenderProfile &profile)
{
    const QString key = profileFormatKey(profile);
    if (key == QStringLiteral("h264")) return QStringLiteral("H.264");
    if (key == QStringLiteral("hevc")) return QStringLiteral("HEVC (H.265)");
    if (key == QStringLiteral("av1")) return QStringLiteral("AV1");
    if (key == QStringLiteral("prores")) return QStringLiteral("Apple ProRes");
    if (key == QStringLiteral("dnx")) return QStringLiteral("DNxHR / DNxHD");
    if (key == QStringLiteral("mpeg2")) return QStringLiteral("MPEG2");
    if (key == QStringLiteral("vp9")) return QStringLiteral("WebM (VP9)");
    if (key == QStringLiteral("mp3")) return QStringLiteral("MP3");
    if (key == QStringLiteral("flac")) return QStringLiteral("FLAC");
    if (key == QStringLiteral("wav")) return QStringLiteral("Waveform Audio");
    if (key == QStringLiteral("aac-audio")) return QStringLiteral("AAC Audio");
    return profile.container.toUpper();
}

QString estimatedSizeText(const QueueJob &job)
{
    if (job.durationMs <= 0) return QStringLiteral("—");
    const auto &p = job.profileSnapshot;
    const double seconds = job.durationMs / 1000.0;
    const double audio = p.audioEncoder.isEmpty() ? 0.0 : p.audioBitrateKbps;
    if (p.videoEncoder.isEmpty())
        return formatEstimatedBytes((audio*1000.0/8.0)*seconds*1.018);
    if (p.videoBitrateKbps > 0) {
        const double bytes=((p.videoBitrateKbps+audio)*1000.0/8.0)*seconds*1.018;
        return formatEstimatedBytes(bytes);
    }
    const int width=p.width>0?p.width:1920; const int height=p.height>0?p.height:1080;
    const double fps=p.frameRate>0?p.frameRate:30.0; const int q=p.quality<0?20:p.quality;
    const double center=std::clamp((double(width)*height*fps/1000.0)*0.075*std::pow(2.0,(23.0-q)/6.0),450.0,180000.0);
    const double low=((center*0.62+audio)*1000.0/8.0)*seconds*1.018;
    const double high=((center*1.55+audio)*1000.0/8.0)*seconds*1.018;
    return QStringLiteral("%1–%2").arg(formatEstimatedBytes(low),formatEstimatedBytes(high));
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), runner_(&database_, this)
{
    setObjectName(QStringLiteral("FluxEncoderMainWindow"));
    setWindowTitle(tr("Flux Encoder — %1")
                       .arg(QStringLiteral(FLUX_ENCODER_VERSION)));
    resize(1680, 960);
    setMinimumSize(1180, 720);
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);
    if (!database_.open()) QMessageBox::critical(this, tr("Database Error"), database_.lastError());
    database_.markRunningJobsInterrupted(); providerCatalog_=new ProviderCatalog(this); providerCatalog_->discover(); buildMenus(); buildUi(); refreshProviderStatus(); runner_.setJobs(&jobs_);
    connect(&runner_, &QueueRunner::jobChanged, this, &MainWindow::handleJobChanged);
    connect(&runner_, &QueueRunner::logMessage, log_, &QPlainTextEdit::appendPlainText);
    connect(&runner_, &QueueRunner::runningChanged, this, [this](bool running) {
        startAction_->setEnabled(!running); stopAction_->setEnabled(running); if(queueStartButton_){queueStartButton_->setEnabled(!running && !jobs_.isEmpty());queueStartButton_->setText(running?tr("Queue Running…"):tr("Start Queue"));} updateEncodingPanel();
    });
    connect(&capabilityWatcher_, &QFutureWatcher<FfmpegCapabilities>::finished, this, &MainWindow::handleCapabilitiesReady);
    previewTimer_.setInterval(900); connect(&previewTimer_, &QTimer::timeout, this, &MainWindow::refreshEncodingPreview);
    connect(&previewProcess_, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, &MainWindow::previewFinished);
    previewTimer_.start(); detectCapabilities();
}

bool MainWindow::openExportFromIpc(const QJsonObject &source,
                                   const QJsonObject &options, QString *error)
{
    if (source.value(QStringLiteral("application")).toString() !=
        QStringLiteral("flux-motion")) {
        if (error) *error = tr("This export source is not a Flux Motion project.");
        return false;
    }

    const QString projectId = source.value(QStringLiteral("project_id")).toString().trimmed();
    const QString projectName = source.value(QStringLiteral("project_name")).toString().trimmed();
    const QString storePath = source.value(QStringLiteral("project_store_path")).toString().trimmed();
    const QString projectScope = source.value(QStringLiteral("project_scope")).toString().trimmed();
    if (projectId.isEmpty() || storePath.isEmpty()) {
        if (error) *error = tr("The Flux Motion export request is missing its project ID or project store path.");
        return false;
    }

    // The IPC endpoint is intentionally available while the window starts, but
    // FFmpeg probing and preset seeding finish asynchronously. Accept and queue
    // the request so Flux Motion never creates a job with an empty profile.
    if (!initializationComplete_) {
        pendingExportRequests_.append(qMakePair(source, options));
        if (options.value(QStringLiteral("activate_window")).toBool(true)) {
            if (isMinimized()) showNormal();
            show();
            raise();
            activateWindow();
        }
        statusBar()->showMessage(tr("Loading encoding presets…"));
        if (error) error->clear();
        return true;
    }
    if (profiles_.isEmpty()) {
        if (error) *error = tr("Flux Encoder could not load any encoding presets.");
        return false;
    }

    MotionProjectSource motionSource;
    if (!findMotionProject(storePath, projectId, &motionSource, error))
        return false;

    int row = -1;
    for (int index = 0; index < jobs_.size(); ++index) {
        const QueueJob &candidate = jobs_.at(index);
        if (candidate.sourceType == QStringLiteral("render-provider") &&
            candidate.providerId == QStringLiteral("org.fluxmotion.renderer") &&
            candidate.sourceDescriptor.value(QStringLiteral("compositionId")).toString() == projectId &&
            candidate.sourcePath == motionSource.storeFile && jobIsMutable(candidate)) {
            row = index;
            break;
        }
    }

    if (row < 0 && pendingIpcCompositionIds_.contains(projectId)) {
        if (options.value(QStringLiteral("activate_window")).toBool(true)) {
            if (isMinimized()) showNormal();
            show(); raise(); activateWindow();
        }
        statusBar()->showMessage(tr("Export Settings are already open for “%1”")
                                     .arg(projectName.isEmpty() ? projectId : projectName), 5000);
        if (error) error->clear();
        return true;
    }

    if (row < 0) {
        QueueJob job;
        job.uuid = createUuid();
        job.sourcePath = motionSource.storeFile;
        job.sourceType = QStringLiteral("render-provider");
        job.providerId = QStringLiteral("org.fluxmotion.renderer");
        RenderSourceDescriptor descriptor;
        descriptor.providerId = job.providerId;
        descriptor.projectUri = QUrl::fromLocalFile(motionSource.storeFile)
                                    .toString(QUrl::FullyEncoded);
        descriptor.compositionId = projectId;
        descriptor.renderOptions = {
            {QStringLiteral("projectName"), projectName},
            {QStringLiteral("projectScope"), projectScope},
            {QStringLiteral("projectStorePath"), QFileInfo(storePath).absoluteFilePath()},
            {QStringLiteral("externalDataMode"), QStringLiteral("snapshot")},
            {QStringLiteral("alphaMode"), QStringLiteral("straight")}};
        job.sourceDescriptor = descriptor.toJson();
        job.profileSnapshot = smartProfileForSource(motionSource.storeFile);
        job.profileSnapshot.width = motionSource.title.value(QStringLiteral("width"))
                                        .toInt(job.profileSnapshot.width);
        job.profileSnapshot.height = motionSource.title.value(QStringLiteral("height"))
                                         .toInt(job.profileSnapshot.height);
        job.profileSnapshot.frameRate = motionSource.title.value(QStringLiteral("frame_rate"))
                                            .toDouble(job.profileSnapshot.frameRate);
        job.profileUuid = job.profileSnapshot.uuid;
        job.profileRevision = job.profileSnapshot.revision;
        job.durationMs = qRound64(
            motionSource.title.value(QStringLiteral("duration")).toDouble() * 1000.0);

        QString outputRoot = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (outputRoot.isEmpty())
            outputRoot = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QString baseName = safeOutputBaseName(projectName);
        job.outputPath = QDir(outputRoot).filePath(
            QStringLiteral("%1.%2").arg(baseName, job.profileSnapshot.extension));
        QFileInfo output(job.outputPath);
        int suffix = 2;
        while (QFileInfo::exists(job.outputPath))
            job.outputPath = output.dir().filePath(
                QStringLiteral("%1_%2.%3").arg(output.completeBaseName())
                    .arg(suffix++).arg(output.suffix()));

        // A Flux Motion request is a draft until Export Settings is accepted.
        // Acknowledging IPC first keeps the caller responsive while the modal
        // settings dialog can remain open for as long as the user needs.
        pendingIpcCompositionIds_.insert(projectId);
        if (options.value(QStringLiteral("activate_window")).toBool(true)) {
            if (isMinimized()) showNormal();
            show(); raise(); activateWindow();
        }
        QTimer::singleShot(0, this, [this, job, projectId]() mutable {
            showNewIpcExportSettings(job);
            pendingIpcCompositionIds_.remove(projectId);
        });
        statusBar()->showMessage(tr("Configuring “%1” from Flux Motion")
                                     .arg(projectName.isEmpty() ? projectId : projectName), 5000);
        if (error) error->clear();
        return true;
    }

    if (options.value(QStringLiteral("activate_window")).toBool(true)) {
        if (isMinimized())
            showNormal();
        show();
        raise();
        activateWindow();
    }
    if (row >= 0 && row < queueTable_->rowCount()) {
        queueTable_->selectRow(row);
        queueSelectionChanged();
        if (options.value(QStringLiteral("open_settings")).toBool(true))
            QTimer::singleShot(0, this, [this, row]() { editJobExportSettings(row); });
    }
    statusBar()->showMessage(tr("Opened “%1” from Flux Motion").arg(
                                 projectName.isEmpty() ? projectId : projectName), 5000);
    if (error) error->clear();
    return true;
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(tr("File"));
    file->addAction(tr("Add Source…"), this, &MainWindow::addFiles, QKeySequence::Open);
    addRenderProjectAction_=file->addAction(tr("Add Render Project…"), this, &MainWindow::addRenderProject);
    file->addSeparator(); file->addAction(tr("Exit"), this, &QWidget::close, QKeySequence::Quit);
    auto *edit = menuBar()->addMenu(tr("Edit"));
    edit->addAction(tr("Export Settings…"), this, &MainWindow::editSelectedProfile, QKeySequence(Qt::CTRL | Qt::Key_E));
    edit->addAction(tr("Duplicate"), this, &MainWindow::duplicateSelectedJobs, QKeySequence(Qt::CTRL | Qt::Key_D));
    edit->addAction(tr("Remove"), this, &MainWindow::removeSelectedJobs, QKeySequence::Delete);
    auto *presetMenu = menuBar()->addMenu(tr("Preset"));
    presetMenu->addAction(tr("Create Encoding Preset…"), this, &MainWindow::createProfile);
    presetMenu->addAction(tr("Create Preset Group…"), this, &MainWindow::createCategory);
    auto *queue = menuBar()->addMenu(tr("Queue"));
    startAction_ = queue->addAction(tr("Start Queue"), this, &MainWindow::startQueue, QKeySequence(Qt::Key_Return));
    stopAction_ = queue->addAction(tr("Stop After Current Item"), &runner_, &QueueRunner::stopAfterCurrent); stopAction_->setEnabled(false);
    queue->addAction(tr("Stop Current Item"), &runner_, &QueueRunner::cancelCurrent);
    queue->addSeparator();
    queue->addAction(tr("Retry Selected Items"), this, &MainWindow::retrySelectedJobs);
    queue->addAction(tr("Move Up"), this, &MainWindow::moveSelectedJobsUp, QKeySequence(Qt::CTRL | Qt::Key_Up));
    queue->addAction(tr("Move Down"), this, &MainWindow::moveSelectedJobsDown, QKeySequence(Qt::CTRL | Qt::Key_Down));
    queue->addAction(tr("Set Output Folder…"), this, &MainWindow::chooseOutputFolderForSelection);
    windowMenu_ = menuBar()->addMenu(tr("Window"));
    auto *workspace = windowMenu_->addMenu(tr("Workspace"));
    workspace->addAction(tr("Save Workspace As…"), this, &MainWindow::saveWorkspaceAs);
    workspace->addAction(tr("Reset to Default Workspace"), this, &MainWindow::resetWorkspace);
    windowMenu_->addSeparator();
    lockDocksAction_ = windowMenu_->addAction(tr("Lock Docks"));
    lockDocksAction_->setCheckable(true);
    connect(lockDocksAction_, &QAction::toggled, this, &MainWindow::setLayoutLocked);
    windowMenu_->addSeparator();
    auto *help = menuBar()->addMenu(tr("Help"));
    help->addAction(tr("About Flux Encoder"), this,
                    [this] { ui::showFluxEncoderAbout(this); });
}

void MainWindow::buildUi()
{
    setDockNestingEnabled(true);
    auto *workspaceCenter = new QWidget(this);
    workspaceCenter->setFixedSize(0,0);
    workspaceCenter->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    setCentralWidget(workspaceCenter);
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    auto *toolbar = addToolBar(tr("Workspace"));
    toolbar->setObjectName(QStringLiteral("FluxEncoderWorkspaceToolbar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(14, 14));
    toolbar->setFixedHeight(29);

    auto *leftSpacer = new QWidget(toolbar);
    leftSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(leftSpacer);
    workspaceCombo_ = new QComboBox(toolbar);
    workspaceCombo_->setObjectName(QStringLiteral("FluxEncoderWorkspaceSelector"));
    workspaceCombo_->setMinimumWidth(190);
    workspaceCombo_->setMaximumWidth(260);
    workspaceCombo_->addItems({tr("Default Workspace"), tr("Encoding"), tr("Queue Management"), tr("Preset Editing"), tr("Compact")});
    workspaceCombo_->setCurrentText(tr("Default Workspace"));
    connect(workspaceCombo_, &QComboBox::currentTextChanged, this, &MainWindow::applyWorkspace);
    toolbar->addWidget(workspaceCombo_);
    auto *workspaceMenu = new QToolButton(toolbar);
    workspaceMenu->setObjectName(QStringLiteral("FluxEncoderWorkspaceMenuButton"));
    workspaceMenu->setText(QStringLiteral("≡"));
    workspaceMenu->setAutoRaise(true);
    workspaceMenu->setPopupMode(QToolButton::InstantPopup);
    auto *workspacePopup = new QMenu(workspaceMenu);
    workspacePopup->addAction(tr("Save Workspace As…"), this, &MainWindow::saveWorkspaceAs);
    workspacePopup->addAction(tr("Reset to Default Workspace"), this, &MainWindow::resetWorkspace);
    workspacePopup->addAction(lockDocksAction_);
    workspaceMenu->setMenu(workspacePopup);
    toolbar->addWidget(workspaceMenu);
    auto *rightSpacer = new QWidget(toolbar);
    rightSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(rightSpacer);

    auto makeDock = [this](const QString &title, const QString &objectName, QWidget *content,
                           Qt::DockWidgetArea area) {
        auto *dock = new QDockWidget(title, this);
        dock->setObjectName(objectName);
        dock->setWidget(content);
        dock->setAllowedAreas(Qt::AllDockWidgetAreas);
        dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                          QDockWidget::DockWidgetFloatable);
        dock->setTitleBarWidget(new FluxEncoderDockTitleBar(title, dock));
        addDockWidget(area, dock);
        dockPanels_.append(dock);
        if (windowMenu_)
            windowMenu_->addAction(dock->toggleViewAction());
        return dock;
    };

    auto *queuePanel = new QWidget(this);
    auto *queueLayout = new QVBoxLayout(queuePanel);
    queueLayout->setContentsMargins(0,0,0,0); queueLayout->setSpacing(0);
    auto *queueActions = new QWidget(queuePanel);
    auto *queueActionsLayout = new QHBoxLayout(queueActions);
    queueActionsLayout->setContentsMargins(6,4,6,4); queueActionsLayout->setSpacing(4);
    auto *queueAdd = new QToolButton(queueActions); queueAdd->setText(tr("+")); queueAdd->setToolTip(tr("Add Source"));
    auto *queueRemove = new QToolButton(queueActions); queueRemove->setText(tr("−")); queueRemove->setToolTip(tr("Remove Selected"));
    auto *queueDuplicate = new QToolButton(queueActions); queueDuplicate->setText(tr("Duplicate"));
    auto *queueRetry = new QToolButton(queueActions); queueRetry->setText(tr("Retry"));
    queueSelectionSummary_ = new QLabel(tr("No items selected"), queueActions);
    queueSelectionSummary_->setObjectName(QStringLiteral("QueueSelectionSummary"));
    queueActionsLayout->addWidget(queueAdd); queueActionsLayout->addWidget(queueRemove);
    queueActionsLayout->addWidget(queueDuplicate); queueActionsLayout->addWidget(queueRetry);
    queueActionsLayout->addStretch(1); queueActionsLayout->addWidget(queueSelectionSummary_);
    queueLayout->addWidget(queueActions);
    connect(queueAdd, &QToolButton::clicked, this, &MainWindow::addFiles);
    connect(queueRemove, &QToolButton::clicked, this, &MainWindow::removeSelectedJobs);
    connect(queueDuplicate, &QToolButton::clicked, this, &MainWindow::duplicateSelectedJobs);
    connect(queueRetry, &QToolButton::clicked, this, &MainWindow::retrySelectedJobs);

    queueTable_ = new QueueDropTable(queuePanel);
    static_cast<QueueDropTable*>(queueTable_)->applyPreset=[this](const QString &uuid,int row,QString *error){ QList<int> rows; if(row>=0){ if(queueTable_->selectionModel() && queueTable_->selectionModel()->isRowSelected(row,QModelIndex())) rows=selectedJobRows(); else rows={row}; } else rows=selectedJobRows(); return applyPresetToRows(uuid,rows,error); };
    queueTable_->setObjectName(QStringLiteral("EncodingQueue"));
    queueTable_->setColumnCount(9);
    queueTable_->setHorizontalHeaderLabels({tr("Source"),tr("Format"),tr("Preset"),tr("Output File"),tr("Status"),tr("Progress"),tr("Remaining"),tr("Speed"),tr("Estimated Size")});
    queueTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    queueTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    queueTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    queueTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    queueTable_->setAlternatingRowColors(true); queueTable_->setShowGrid(false);
    queueTable_->verticalHeader()->setVisible(false);
    queueTable_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    queueTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    queueTable_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    queueTable_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
    queueTable_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::ResizeToContents);
    queueLayout->addWidget(queueTable_);
    queueEmptyState_ = new QLabel(tr("Drop media files here, or click + Add Source to begin."), queuePanel);
    queueEmptyState_->setAlignment(Qt::AlignCenter); queueEmptyState_->setWordWrap(true);
    queueEmptyState_->setMinimumHeight(72); queueEmptyState_->setObjectName(QStringLiteral("QueueEmptyState"));
    QFont emptyFont = queueEmptyState_->font(); emptyFont.setPointSizeF(emptyFont.pointSizeF() * 1.08);
    queueEmptyState_->setFont(emptyFont); queueLayout->addWidget(queueEmptyState_);
    connect(queueTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::queueSelectionChanged);
    connect(queueTable_, &QTableWidget::customContextMenuRequested, this, &MainWindow::showQueueContextMenu);
    connect(queueTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int column){
        if (column==2) editSelectedProfile();
        else if (column==3) chooseOutput();
    });
    auto *progressRow = new QWidget(queuePanel);
    auto *progressLayout = new QHBoxLayout(progressRow); progressLayout->setContentsMargins(7,5,7,6);
    progressLayout->addWidget(new QLabel(tr("Queue:"), progressRow));
    totalProgress_ = new QProgressBar(progressRow); totalProgress_->setTextVisible(true);
    progressLayout->addWidget(totalProgress_,1);
    queueStartButton_ = new QPushButton(tr("Start Queue"), progressRow);
    queueStartButton_->setDefault(true); queueStartButton_->setMinimumWidth(120);
    queueStartButton_->setToolTip(tr("Run all ready items in queue order"));
    connect(queueStartButton_, &QPushButton::clicked, this, &MainWindow::startQueue);
    progressLayout->addWidget(queueStartButton_); queueLayout->addWidget(progressRow);
    auto *queueDock = makeDock(tr("Queue"), QStringLiteral("QueueDock"), queuePanel, Qt::RightDockWidgetArea);

    auto *presetPanel = new QWidget(this);
    auto *presetLayout = new QVBoxLayout(presetPanel); presetLayout->setContentsMargins(0,0,0,0); presetLayout->setSpacing(0);
    auto *search = new QLineEdit(presetPanel); search->setPlaceholderText(tr("Search Presets")); search->setClearButtonEnabled(true); presetLayout->addWidget(search);
    profileTree_ = new PresetTreeWidget(presetPanel); profileTree_->setObjectName(QStringLiteral("PresetBrowser")); profileTree_->setHeaderHidden(true); profileTree_->setAlternatingRowColors(true);
    profileTree_->setEditTriggers(QAbstractItemView::NoEditTriggers); profileTree_->setDragEnabled(true); profileTree_->setDragDropMode(QAbstractItemView::DragOnly);
    profileTree_->setContextMenuPolicy(Qt::CustomContextMenu); presetLayout->addWidget(profileTree_);
    capabilityLabel_ = new QLabel(tr("Detecting FFmpeg and GPU capabilities…"), presetPanel);
    capabilityLabel_->setWordWrap(true); capabilityLabel_->setContentsMargins(6,4,6,6); presetLayout->addWidget(capabilityLabel_);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (QTreeWidgetItemIterator it(profileTree_); *it; ++it)
            (*it)->setHidden(!text.isEmpty() && !(*it)->text(0).contains(text, Qt::CaseInsensitive));
    });
    connect(profileTree_, &QTreeWidget::itemDoubleClicked, this, &MainWindow::profileActivated);
    connect(profileTree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.addAction(tr("Apply Preset"), this, [this]{ if (auto *i=profileTree_->currentItem()) profileActivated(i,0); });
        menu.addAction(tr("Edit Preset"), this, &MainWindow::editSelectedProfile);
        menu.addAction(tr("Duplicate Preset"), this, &MainWindow::duplicateSelectedProfile);
        menu.exec(profileTree_->viewport()->mapToGlobal(pos));
    });
    auto *presetDock = makeDock(tr("Preset Browser"), QStringLiteral("PresetBrowserDock"), presetPanel, Qt::LeftDockWidgetArea);

    auto *mediaPanel = new QWidget(this);
    auto *mediaLayout = new QVBoxLayout(mediaPanel); mediaLayout->setContentsMargins(0,0,0,0); mediaLayout->setSpacing(0);
    auto *mediaPathRow = new QWidget(mediaPanel);
    auto *mediaPathLayout = new QHBoxLayout(mediaPathRow); mediaPathLayout->setContentsMargins(5,4,5,4); mediaPathLayout->setSpacing(4);
    mediaLocationCombo_ = new QComboBox(mediaPathRow); mediaLocationCombo_->setEditable(true); mediaLocationCombo_->addItem(QDir::homePath());
    auto *browseLocation = new QToolButton(mediaPathRow); browseLocation->setText(QStringLiteral("…")); browseLocation->setToolTip(tr("Browse Location"));
    mediaSearch_ = new QLineEdit(mediaPathRow); mediaSearch_->setPlaceholderText(tr("Search media")); mediaSearch_->setClearButtonEnabled(true);
    mediaPathLayout->addWidget(mediaLocationCombo_,1); mediaPathLayout->addWidget(browseLocation); mediaPathLayout->addWidget(mediaSearch_,1); mediaLayout->addWidget(mediaPathRow);
    mediaBrowser_ = new QListWidget(mediaPanel); mediaBrowser_->setAlternatingRowColors(true);
    mediaBrowser_->setEditTriggers(QAbstractItemView::NoEditTriggers); mediaLayout->addWidget(mediaBrowser_);
    connect(mediaBrowser_, &QListWidget::currentRowChanged, this, [this](int row){
        if (row >= 0 && row < queueTable_->rowCount()) { queueTable_->selectRow(row); queueSelectionChanged(); }
    });
    connect(mediaSearch_, &QLineEdit::textChanged, this, [this](const QString &text){
        for (int i=0;i<mediaBrowser_->count();++i) mediaBrowser_->item(i)->setHidden(!text.isEmpty() && !mediaBrowser_->item(i)->text().contains(text,Qt::CaseInsensitive));
    });
    connect(browseLocation, &QToolButton::clicked, this, &MainWindow::browseMediaLocation);
    connect(mediaLocationCombo_, &QComboBox::currentTextChanged, this, &MainWindow::refreshMediaBrowser);
    connect(mediaBrowser_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item){
        if (!item) return; const QString path = item->data(Qt::UserRole).toString();
        if (QFileInfo(path).isDir()) { mediaLocationCombo_->setCurrentText(path); refreshMediaBrowser(); }
        else addSources({path});
    });
    refreshMediaBrowser();
    auto *mediaDock = makeDock(tr("Media Browser"), QStringLiteral("MediaBrowserDock"), mediaPanel, Qt::LeftDockWidgetArea);
    splitDockWidget(presetDock, mediaDock, Qt::Vertical);

    auto *encodingPanel = new QWidget(this);
    auto *encodingLayout = new QVBoxLayout(encodingPanel); encodingLayout->setContentsMargins(4,4,4,4); encodingLayout->setSpacing(5);
    auto *rendererRow = new QWidget(encodingPanel);
    auto *rendererLayout = new QHBoxLayout(rendererRow); rendererLayout->setContentsMargins(4,0,4,0);
    rendererLayout->addWidget(new QLabel(tr("Renderer:"), rendererRow));
    rendererCombo_ = new QComboBox(rendererRow);
    rendererCombo_->addItem(tr("Automatic (Best Available)"), QStringLiteral("auto"));
    rendererCombo_->addItem(tr("Software Encoding"), QStringLiteral("software"));
    rendererCombo_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    rendererLayout->addWidget(rendererCombo_,1); encodingLayout->addWidget(rendererRow);
    connect(rendererCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, &MainWindow::rendererChanged);
    previewLabel_ = new FluxEncoderZoomablePreview(encodingPanel);
    previewLabel_->setText(tr("Select a queue item to preview"));
    previewLabel_->setMinimumSize(360,203);
    previewLabel_->setObjectName(QStringLiteral("EncodingPreview")); encodingLayout->addWidget(previewLabel_,1);
    auto *previewZoomRow = new QWidget(encodingPanel);
    auto *previewZoomLayout = new QHBoxLayout(previewZoomRow); previewZoomLayout->setContentsMargins(8,0,8,0); previewZoomLayout->setSpacing(6);
    previewZoomLayout->addWidget(new QLabel(tr("Zoom"), previewZoomRow));
    previewZoom_ = new QSlider(Qt::Horizontal, previewZoomRow); previewZoom_->setRange(10,400); previewZoom_->setValue(100); previewZoom_->setEnabled(false);
    auto *previewZoomValue = new QLabel(QStringLiteral("100%"), previewZoomRow); previewZoomValue->setMinimumWidth(42);
    previewFit_ = new QToolButton(previewZoomRow); previewFit_->setText(tr("Fit")); previewFit_->setCheckable(true); previewFit_->setChecked(true);
    previewFit_->setToolTip(tr("Fit the video frame inside the preview"));
    previewZoomLayout->addWidget(previewZoom_,1); previewZoomLayout->addWidget(previewZoomValue); previewZoomLayout->addWidget(previewFit_);
    encodingLayout->addWidget(previewZoomRow);
    connect(previewZoom_, &QSlider::valueChanged, this, [this, previewZoomValue](int value){ previewZoomValue->setText(QStringLiteral("%1%").arg(value)); previewLabel_->setZoomPercent(value); });
    connect(previewFit_, &QToolButton::toggled, this, [this](bool checked){ previewZoom_->setEnabled(!checked); previewLabel_->setFitToView(checked); });
    encodingTitle_=new QLabel(QStringLiteral("—"),encodingPanel); QFont ef=encodingTitle_->font();ef.setBold(true);encodingTitle_->setFont(ef);
    encodingTitle_->setWordWrap(true);encodingTitle_->setContentsMargins(8,0,8,0);encodingLayout->addWidget(encodingTitle_);
    encodingDetails_=new QLabel(tr("Queue is idle"),encodingPanel);encodingDetails_->setContentsMargins(8,0,8,0);encodingDetails_->setWordWrap(true);encodingLayout->addWidget(encodingDetails_);
    auto *previewScrubRow = new QWidget(encodingPanel);
    auto *previewScrubLayout = new QHBoxLayout(previewScrubRow); previewScrubLayout->setContentsMargins(8,0,8,0); previewScrubLayout->setSpacing(6);
    previewSlider_ = new FluxEncoderRangeSlider(Qt::Horizontal, previewScrubRow); previewSlider_->setRange(0,1000); previewSlider_->setEnabled(false);
    previewTimeLabel_ = new QLabel(QStringLiteral("00:00:00 / 00:00:00"), previewScrubRow); previewTimeLabel_->setMinimumWidth(130);
    previewScrubLayout->addWidget(previewSlider_,1); previewScrubLayout->addWidget(previewTimeLabel_); encodingLayout->addWidget(previewScrubRow);
    connect(previewSlider_, &QSlider::sliderPressed, this, [this]{ previewSliderDragging_ = true; });
    connect(previewSlider_, &QSlider::sliderReleased, this, [this]{ previewSliderDragging_ = false; previewPositionChanged(previewSlider_->value()); });
    connect(previewSlider_, &QSlider::valueChanged, this, &MainWindow::previewPositionChanged);
    encodingProgress_=new QProgressBar(encodingPanel);encodingProgress_->setContentsMargins(8,0,8,0);encodingLayout->addWidget(encodingProgress_);
    auto *encodingDock = makeDock(tr("Encoding"), QStringLiteral("EncodingDock"), encodingPanel, Qt::RightDockWidgetArea);

    auto *inspector = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(inspector); rightLayout->setContentsMargins(0,0,0,0); rightLayout->setSpacing(4);
    auto *sourceContent=new QWidget(inspector); auto *sourceForm=new QFormLayout(sourceContent);
    sourceValue_=new QLabel(QStringLiteral("—"),sourceContent);sourceValue_->setWordWrap(true); sourceValue_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    profileValue_=new QLabel(QStringLiteral("—"),sourceContent); encoderValue_=new QLabel(QStringLiteral("—"),sourceContent);
    sourceForm->addRow(tr("Source"),sourceValue_);sourceForm->addRow(tr("Preset"),profileValue_);sourceForm->addRow(tr("Encoder"),encoderValue_);
    flux_add_panel_section(rightLayout,sourceContent,tr("Source and Preset"));
    auto *outputContent=new QWidget(inspector);auto *outputLayout=new QVBoxLayout(outputContent);
    outputValue_=new QLineEdit(outputContent);outputValue_->setReadOnly(true);
    auto *choose=new QPushButton(tr("Output File…"),outputContent);outputLayout->addWidget(outputValue_);outputLayout->addWidget(choose);
    connect(choose,&QPushButton::clicked,this,&MainWindow::chooseOutput);flux_add_panel_section(rightLayout,outputContent,tr("Output"));
    auto *hardwareContent=new QWidget(inspector);auto *hardwareLayout=new QVBoxLayout(hardwareContent);
    fallbackSwitch_=new FluxSwitch(tr("Use software encoding if hardware fails"),hardwareContent);fallbackSwitch_->setChecked(true);hardwareLayout->addWidget(fallbackSwitch_);
    connect(fallbackSwitch_,&FluxSwitch::toggled,this,[this](bool checked){
        if (runner_.isRunning()) return;
        for(int row:selectedJobRows()){jobs_[row].profileSnapshot.allowSoftwareFallback=checked;jobs_[row].updatedAt=QDateTime::currentDateTimeUtc();database_.saveJob(jobs_[row]);}
    });
    flux_add_panel_section(rightLayout,hardwareContent,tr("Renderer"));rightLayout->addStretch();
    auto *settingsDock = makeDock(tr("Job Settings"), QStringLiteral("JobSettingsDock"), inspector, Qt::RightDockWidgetArea);
    splitDockWidget(encodingDock, settingsDock, Qt::Vertical);

    auto *watchPanel = new QWidget(this); auto *watchLayout = new QVBoxLayout(watchPanel); watchLayout->setContentsMargins(0,0,0,0);
    auto *watchToolbar = new QWidget(watchPanel); auto *watchToolbarLayout = new QHBoxLayout(watchToolbar); watchToolbarLayout->setContentsMargins(4,4,4,4);
    auto *watchAdd = new QPushButton(tr("Add Folder…"), watchToolbar); auto *watchRemove = new QPushButton(tr("Remove"), watchToolbar);
    watchToolbarLayout->addWidget(watchAdd); watchToolbarLayout->addWidget(watchRemove); watchToolbarLayout->addStretch(); watchLayout->addWidget(watchToolbar);
    watchFolders_ = new QListWidget(watchPanel); watchFolders_->setEditTriggers(QAbstractItemView::NoEditTriggers); watchLayout->addWidget(watchFolders_);
    connect(watchAdd, &QPushButton::clicked, this, &MainWindow::addWatchFolder);
    connect(watchRemove, &QPushButton::clicked, this, &MainWindow::removeWatchFolder);
    QSettings watchSettings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    for (const QString &folder : watchSettings.value(QStringLiteral("watchFolders")).toStringList()) watchFolders_->addItem(folder);
    watchFolderTimer_.setInterval(3000); connect(&watchFolderTimer_, &QTimer::timeout, this, &MainWindow::scanWatchFolders); watchFolderTimer_.start();
    auto *watchDock = makeDock(tr("Watch Folders"), QStringLiteral("WatchFoldersDock"), watchPanel, Qt::RightDockWidgetArea);
    tabifyDockWidget(queueDock, watchDock); queueDock->raise();

    // The default workspace keeps Queue/Watch Folders above Encoding.
    splitDockWidget(queueDock, encodingDock, Qt::Vertical);
    settingsDock->hide();

    log_ = new QPlainTextEdit(this); log_->setReadOnly(true); log_->setPlaceholderText(tr("Encoding log"));
    auto *logDock = makeDock(tr("Log"), QStringLiteral("LogDock"), log_, Qt::RightDockWidgetArea);
    tabifyDockWidget(watchDock, logDock); watchDock->raise();

    resizeDocks({mediaDock, queueDock}, {730, 950}, Qt::Horizontal);
    resizeDocks({mediaDock, presetDock}, {500, 390}, Qt::Vertical);
    resizeDocks({queueDock, encodingDock}, {485, 405}, Qt::Vertical);
    statusBar()->showMessage(tr("Initializing…"));
    defaultWorkspaceState_ = saveState(4);
    restoreLastWorkspace();
}

void MainWindow::detectCapabilities(){capabilityWatcher_.setFuture(QtConcurrent::run([]{return FfmpegCapabilityDetector::detect();}));}
void MainWindow::handleCapabilitiesReady(){capabilities_=capabilityWatcher_.result();if(capabilities_.ffmpegPath.isEmpty()){capabilityLabel_->setText(tr("FFmpeg was not found. Install FFmpeg or place it beside the application."));statusBar()->showMessage(tr("FFmpeg unavailable"));}else{int usable=0;for(auto it=capabilities_.encoderProbes.cbegin();it!=capabilities_.encoderProbes.cend();++it)if(it->runtimeAvailable)++usable;capabilityLabel_->setText(tr("%1\nAvailable hardware encoders: %2").arg(capabilities_.version).arg(usable));statusBar()->showMessage(tr("Ready"));}
    if(rendererCombo_){
        const QSignalBlocker blocker(rendererCombo_); while(rendererCombo_->count()>2) rendererCombo_->removeItem(2);
        const QList<QPair<QString,QString>> encoders={
            {QStringLiteral("h264_nvenc"),tr("Mercury-style GPU Acceleration (NVIDIA NVENC — H.264)")},
            {QStringLiteral("hevc_nvenc"),tr("Mercury-style GPU Acceleration (NVIDIA NVENC — HEVC)")},
            {QStringLiteral("h264_qsv"),tr("Hardware Encoding (Intel Quick Sync — H.264)")},
            {QStringLiteral("hevc_qsv"),tr("Hardware Encoding (Intel Quick Sync — HEVC)")},
            {QStringLiteral("h264_amf"),tr("Hardware Encoding (AMD AMF — H.264)")},
            {QStringLiteral("hevc_amf"),tr("Hardware Encoding (AMD AMF — HEVC)")},
            {QStringLiteral("h264_vaapi"),tr("Hardware Encoding (VAAPI — H.264)")},
            {QStringLiteral("hevc_vaapi"),tr("Hardware Encoding (VAAPI — HEVC)")}};
        for(const auto &entry:encoders) if(capabilities_.encoderUsable(entry.first)) rendererCombo_->addItem(entry.second,entry.first);
    }
    ProfileCatalog::seedMissing(database_,capabilities_);reloadData();
    repairIncompleteRenderJobs();
    initializationComplete_=true;
    if(!pendingQueuePath_.isEmpty()){
        const QString path=pendingQueuePath_;
        pendingQueuePath_.clear();
        QString error;
        if(!openQueueFile(path,&error))
            QMessageBox::warning(this,tr("Could not open Flux Encoder queue"),error);
    }
    if(!pendingSourcePaths_.isEmpty()){
        const QStringList paths=pendingSourcePaths_;
        pendingSourcePaths_.clear();
        addSources(paths);
    }
    if(!pendingExportRequests_.isEmpty()){
        const auto requests=pendingExportRequests_;
        pendingExportRequests_.clear();
        QStringList errors;
        for(const auto &request:requests){
            QString requestError;
            if(!openExportFromIpc(request.first,request.second,&requestError))
                errors<<requestError;
        }
        if(!errors.isEmpty())
            QMessageBox::warning(this,tr("Could not open Flux Motion export"),
                                 errors.join(QStringLiteral("\n\n")));
    }
}
void MainWindow::reloadData(){categories_=database_.loadCategories();profiles_=database_.loadProfiles();jobs_=database_.loadJobs();runner_.setJobs(&jobs_);rebuildProfileTree();rebuildQueueTable();}

void MainWindow::repairIncompleteRenderJobs()
{
    if(profiles_.isEmpty())return;
    bool changed=false;
    for(auto &job:jobs_){
        if(job.sourceType!=QStringLiteral("render-provider") ||
           (!job.profileUuid.trimmed().isEmpty() &&
            !job.profileSnapshot.extension.trimmed().isEmpty()))continue;
        RenderProfile profile=smartProfileForSource(job.sourcePath);
        if(profile.uuid.isEmpty())continue;
        const RenderProviderSourceFormat format=readRenderProviderSourceFormat(job);
        if(format.width>0)profile.width=format.width;
        if(format.height>0)profile.height=format.height;
        if(format.frameRate>0.0)profile.frameRate=format.frameRate;
        job.profileUuid=profile.uuid;
        job.profileRevision=profile.revision;
        job.profileSnapshot=profile;
        QFileInfo output(job.outputPath);
        if(job.outputPath.trimmed().isEmpty())
            job.outputPath=JobBuilder::defaultOutputPath(job.sourcePath,profile);
        else if(output.suffix().isEmpty())
            job.outputPath=output.dir().filePath(
                QStringLiteral("%1.%2").arg(output.completeBaseName(),profile.extension));
        database_.saveJob(job);
        changed=true;
    }
    if(changed){
        runner_.setJobs(&jobs_);
        rebuildQueueTable();
        statusBar()->showMessage(tr("Recovered incomplete Flux Motion export items"),5000);
    }
}

void MainWindow::rebuildProfileTree(){profileTree_->clear();QMap<QString,QTreeWidgetItem*> items;std::function<void(const QString&,QTreeWidgetItem*)> add=[&](const QString &parent,QTreeWidgetItem *pi){for(const auto &c:categories_)if(c.parentUuid==parent){auto *i=pi?new QTreeWidgetItem(pi):new QTreeWidgetItem(profileTree_);i->setText(0,c.name);i->setData(0,Qt::UserRole,c.uuid);i->setData(0,Qt::UserRole+1,QStringLiteral("category"));QFont f=i->font(0);f.setBold(true);i->setFont(0,f);items.insert(c.uuid,i);add(c.uuid,i);}};add({},nullptr);for(const auto &p:profiles_){auto *parent=items.value(p.categoryUuid);if(!parent)continue;auto *i=new QTreeWidgetItem(parent);i->setText(0,QStringLiteral("%1%2").arg(p.enabled?QStringLiteral("  "):QStringLiteral("⚠ "),p.name));i->setData(0,Qt::UserRole,p.uuid);i->setData(0,Qt::UserRole+1,QStringLiteral("profile"));if(!p.enabled){i->setDisabled(true);i->setToolTip(0,p.unavailableReason);}}profileTree_->expandToDepth(1);}
void MainWindow::rebuildQueueTable(){
    queueTable_->setRowCount(jobs_.size()); mediaBrowser_->clear();
    for(int r=0;r<jobs_.size();++r){ updateQueueRow(r); mediaBrowser_->addItem(jobs_[r].sourcePath); }
    if(queueEmptyState_) queueEmptyState_->setVisible(jobs_.isEmpty());
    queueTable_->setVisible(!jobs_.isEmpty());
    if(queueStartButton_) queueStartButton_->setEnabled(!jobs_.isEmpty() && !runner_.isRunning());
    queueSelectionChanged();
}
void MainWindow::updateQueueRow(int row)
{
    if (row < 0 || row >= jobs_.size()) return;
    auto &job = jobs_[row];
    const bool mutableJob = jobIsMutable(job) && !runner_.isRunning();

    const QStringList values = {QFileInfo(job.sourcePath).fileName(), QString(), QString(), QString(),
                                jobStatusName(job.status), QStringLiteral("%1%").arg(job.progress, 0, 'f', 1),
                                timeText(job.etaSeconds), job.speed, estimatedSizeText(job)};
    for (int column = 0; column < values.size(); ++column) {
        if (column == 1 || column == 2 || column == 3) continue;
        auto *item = queueTable_->item(row, column);
        if (!item) { item = new QTableWidgetItem; queueTable_->setItem(row, column, item); }
        item->setText(values[column]);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setToolTip(column == 0 ? job.sourcePath : job.errorMessage);
        if (column == 4) {
            const QPalette pal = queueTable_->palette();
            QColor color = pal.color(QPalette::Text);
            if (job.status == JobStatus::Completed) color = QColor(64, 180, 96);
            else if (job.status == JobStatus::Failed) color = QColor(220, 80, 80);
            else if (job.status == JobStatus::Encoding || job.status == JobStatus::Rendering) color = pal.color(QPalette::Highlight);
            else if (job.status == JobStatus::Interrupted || job.status == JobStatus::Cancelled) color = QColor(220, 160, 65);
            item->setForeground(color);
            QFont statusFont = item->font(); statusFont.setBold(job.status == JobStatus::Encoding || job.status == JobStatus::Rendering || job.status == JobStatus::Failed); item->setFont(statusFont);
        }
    }

    auto configureSplitLink = [this, row, mutableJob](int column, const QString &label, bool formatMenu) {
        auto *button = qobject_cast<QToolButton *>(queueTable_->cellWidget(row, column));
        if (!button) {
            button = queueSettingsSplitLink(label, queueTable_);
            queueTable_->setCellWidget(row, column, button);
            connect(button, &QToolButton::clicked, this, [this, row]() { editJobExportSettings(row); });
        }
        button->setText(label);
        button->setEnabled(mutableJob);
        button->setToolTip(mutableJob
                               ? tr("Click the label to open Export Settings. Use the arrow for quick selection.")
                               : tr("This setting cannot be changed while the item is active."));

        auto *menu = new QMenu(button);
        if (formatMenu) {
            QMap<QString, QString> formats;
            for (const auto &profile : profiles_) {
                if (!profile.enabled && !profile.allowSoftwareFallback) continue;
                formats.insert(profileFormatKey(profile), profileFormatLabel(profile));
            }
            for (auto it = formats.cbegin(); it != formats.cend(); ++it) {
                QAction *action = menu->addAction(it.value());
                action->setCheckable(true);
                action->setChecked(it.key() == profileFormatKey(jobs_[row].profileSnapshot));
                connect(action, &QAction::triggered, this, [this, row, key = it.key()]() {
                    if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row]) || runner_.isRunning()) return;
                    const RenderProfile *replacement = nullptr;
                    for (const auto &profile : profiles_) {
                        if (profileFormatKey(profile) == key && (profile.enabled || profile.allowSoftwareFallback)) {
                            replacement = &profile;
                            if (profile.enabled) break;
                        }
                    }
                    if (!replacement) return;
                    auto &target = jobs_[row];
                    target.profileUuid = replacement->uuid;
                    target.profileRevision = replacement->revision;
                    target.profileSnapshot = *replacement;
                    { QFileInfo out(target.outputPath); const QString base = out.completeBaseName().isEmpty() ? QFileInfo(target.sourcePath).completeBaseName() : out.completeBaseName(); target.outputPath = out.dir().filePath(base + QStringLiteral(".") + replacement->extension); }
                    target.status = JobStatus::Pending;
                    target.updatedAt = QDateTime::currentDateTimeUtc();
                    database_.saveJob(target);
                    updateQueueRow(row); queueSelectionChanged();
                });
            }
        } else {
            const QString formatKey = profileFormatKey(jobs_[row].profileSnapshot);
            for (const auto &profile : profiles_) {
                if (profileFormatKey(profile) != formatKey) continue;
                QAction *action = menu->addAction(profile.name);
                action->setCheckable(true);
                action->setChecked(profile.uuid == jobs_[row].profileUuid);
                action->setEnabled(profile.enabled || profile.allowSoftwareFallback);
                if (!profile.enabled) action->setToolTip(profile.unavailableReason);
                connect(action, &QAction::triggered, this, [this, row, uuid = profile.uuid]() {
                    if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row]) || runner_.isRunning()) return;
                    auto it = std::find_if(profiles_.cbegin(), profiles_.cend(), [&](const RenderProfile &p) { return p.uuid == uuid; });
                    if (it == profiles_.cend()) return;
                    auto &target = jobs_[row];
                    target.profileUuid = it->uuid;
                    target.profileRevision = it->revision;
                    target.profileSnapshot = *it;
                    { QFileInfo out(target.outputPath); const QString base = out.completeBaseName().isEmpty() ? QFileInfo(target.sourcePath).completeBaseName() : out.completeBaseName(); target.outputPath = out.dir().filePath(base + QStringLiteral(".") + it->extension); }
                    target.status = JobStatus::Pending;
                    target.updatedAt = QDateTime::currentDateTimeUtc();
                    database_.saveJob(target);
                    updateQueueRow(row); queueSelectionChanged();
                });
            }
        }
        button->setMenu(menu);
    };

    configureSplitLink(1, profileFormatLabel(job.profileSnapshot), true);
    configureSplitLink(2, job.profileSnapshot.name, false);

    auto *outputLink = qobject_cast<QPushButton *>(queueTable_->cellWidget(row, 3));
    if (!outputLink) {
        outputLink = queueSettingsLink(QString(), queueTable_);
        queueTable_->setCellWidget(row, 3, outputLink);
        connect(outputLink, &QPushButton::clicked, this, [this, row]() {
            if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row]) || runner_.isRunning()) return;
            auto &target = jobs_[row];
            queueTable_->selectRow(row);
            if(target.status==JobStatus::Completed && QFileInfo::exists(target.outputPath)){
                revealOutputFile(target.outputPath);
                return;
            }
            const QString selected = QFileDialog::getSaveFileName(this, tr("Choose Output File"), target.outputPath);
            if (selected.isEmpty()) return;
            target.outputPath = selected;
            target.status = JobStatus::Pending;
            target.updatedAt = QDateTime::currentDateTimeUtc();
            database_.saveJob(target);
            updateQueueRow(row); queueSelectionChanged();
        });
    }
    outputLink->setText(QFileInfo(job.outputPath).fileName());
    outputLink->setEnabled(mutableJob);
    outputLink->setToolTip(job.status==JobStatus::Completed
        ? tr("Open the completed file in its folder\n%1").arg(job.outputPath)
        : job.outputPath);
    updateQueueSelectionStyles();

    int done = 0; double sum = 0;
    for (const auto &candidate : jobs_) { sum += candidate.progress; if (candidate.status == JobStatus::Completed) ++done; }
    totalProgress_->setValue(jobs_.isEmpty() ? 0 : int(sum / jobs_.size()));
    totalProgress_->setFormat(tr("%1 of %2 complete — %p%").arg(done).arg(jobs_.size()));
    updateEncodingPanel();
}

void MainWindow::addFiles(){addSources(QFileDialog::getOpenFileNames(this,tr("Add Sources"),QString(),tr("Supported sources (*.fxmt *.fxmp *.json *.*)")));}
void MainWindow::openSourceFiles(const QStringList &paths){
    if(paths.isEmpty())return;
    QStringList normalized;
    normalized.reserve(paths.size());
    QString queuePath;
    for(const QString &path:paths){
        const QFileInfo info(path);
        if(info.suffix().compare(QStringLiteral("fxe"),Qt::CaseInsensitive)==0)
            queuePath=info.absoluteFilePath();
        else
            normalized<<info.absoluteFilePath();
    }
    if(!queuePath.isEmpty()){
        if(!initializationComplete_){
            pendingQueuePath_=queuePath;
        }else{
            QString error;
            if(!openQueueFile(queuePath,&error))
                QMessageBox::warning(this,tr("Could not open Flux Encoder queue"),error);
        }
    }
    if(normalized.isEmpty())return;
    if(profiles_.isEmpty()&&!capabilityWatcher_.isFinished()){
        pendingSourcePaths_.append(normalized);
        return;
    }
    addSources(normalized);
}
bool MainWindow::openQueueFile(const QString &path,QString *error){
    if(runner_.isRunning()){
        if(error)*error=tr("Stop the active queue before opening another queue file.");
        return false;
    }
    QFile file(path);
    constexpr qint64 maximumQueueBytes=64LL*1024*1024;
    if(!file.open(QIODevice::ReadOnly)||file.size()>maximumQueueBytes){
        if(error)*error=tr("The queue file could not be read or is too large.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if(parseError.error!=QJsonParseError::NoError){
        if(error)*error=tr("The queue file is not valid JSON: %1").arg(parseError.errorString());
        return false;
    }
    QJsonArray serializedJobs;
    if(document.isObject()){
        const QJsonObject root=document.object();
        const QString schema=root.value(QStringLiteral("schema")).toString();
        const int schemaVersion=root.value(QStringLiteral("schemaVersion")).toInt(1);
        if((!schema.isEmpty()&&schema!=QStringLiteral("flux-encoder.queue"))||schemaVersion!=1){
            if(error)*error=tr("This is not a supported Flux Encoder queue file.");
            return false;
        }
        serializedJobs=root.value(QStringLiteral("jobs")).toArray();
    }else if(document.isArray()){
        serializedJobs=document.array();
    }else{
        if(error)*error=tr("The queue file does not contain a queue.");
        return false;
    }

    QList<QueueJob> imported;
    QSet<QString> identifiers;
    const QDir queueDirectory=QFileInfo(path).absoluteDir();
    imported.reserve(serializedJobs.size());
    for(const QJsonValue &value:serializedJobs){
        if(!value.isObject()){
            if(error)*error=tr("The queue contains an invalid item.");
            return false;
        }
        QueueJob job=QueueJob::fromJson(value.toObject());
        if(job.sourcePath.isEmpty()||job.outputPath.isEmpty()){
            if(error)*error=tr("A queue item is missing its source or output path.");
            return false;
        }
        if(QDir::isRelativePath(job.sourcePath))job.sourcePath=queueDirectory.absoluteFilePath(job.sourcePath);
        if(QDir::isRelativePath(job.outputPath))job.outputPath=queueDirectory.absoluteFilePath(job.outputPath);
        if(job.uuid.isEmpty()||identifiers.contains(job.uuid))job.uuid=createUuid();
        identifiers.insert(job.uuid);
        if(job.status==JobStatus::Encoding||job.status==JobStatus::Rendering||job.status==JobStatus::Preparing){
            job.status=JobStatus::Pending;
            job.progress=0.0;
            job.currentTimeMs=0;
            job.etaSeconds=-1;
            job.speed.clear();
        }
        imported.push_back(job);
    }

    for(const QueueJob &job:jobs_)database_.deleteJob(job.uuid);
    jobs_=imported;
    for(const QueueJob &job:jobs_)database_.saveJob(job);
    runner_.setJobs(&jobs_);
    rebuildQueueTable();
    if(!jobs_.isEmpty())queueTable_->selectRow(0);
    queueSelectionChanged();
    setWindowFilePath(QFileInfo(path).absoluteFilePath());
    statusBar()->showMessage(tr("Opened queue “%1”").arg(QFileInfo(path).fileName()),5000);
    if(error)error->clear();
    return true;
}
void MainWindow::addSources(const QStringList &paths){
    QStringList errors;
    for(const QString &path:paths){
        QFileInfo info(path); if(!info.isFile())continue;
        const QString suffix=info.suffix().toLower();
        if((suffix==QStringLiteral("fxmt")||suffix==QStringLiteral("fxmp"))&&providerCatalog_&&providerCatalog_->providerForPath(path)){
            QString error;if(!addRenderProjectPath(path,&error))errors<<tr("%1: %2").arg(info.fileName(),error);continue;
        }
        RenderProfile p=smartProfileForSource(path);
        QueueJob j;j.uuid=createUuid();j.sourcePath=info.absoluteFilePath();j.profileUuid=p.uuid;j.profileRevision=p.revision;j.profileSnapshot=p;
        j.outputPath=JobBuilder::defaultOutputPath(path,p);
        QFileInfo outputInfo(j.outputPath); int outputSuffix=2;
        while(QFileInfo::exists(j.outputPath)) j.outputPath=outputInfo.dir().filePath(QStringLiteral("%1_%2.%3").arg(outputInfo.completeBaseName()).arg(outputSuffix++).arg(outputInfo.suffix()));
        j.durationMs=probeDurationMs(capabilities_.ffprobePath,j.sourcePath);database_.saveJob(j);jobs_.push_back(j);
    }
    rebuildQueueTable();
    if(!jobs_.isEmpty()){queueTable_->selectRow(jobs_.size()-1);queueSelectionChanged();}
    if(!errors.isEmpty())QMessageBox::warning(this,tr("Could not add Flux Motion project"),errors.join(QStringLiteral("\n\n")));
}
void MainWindow::removeSelectedJobs(){
    QList<int> rows=selectedJobRows(); if(rows.isEmpty()) return;
    int removable=0; for(int row:rows) if(row>=0 && row<jobs_.size() && jobIsMutable(jobs_[row])) ++removable;
    if(removable==0){ statusBar()->showMessage(tr("Active queue items cannot be removed"),3000); return; }
    const auto answer=QMessageBox::question(this,tr("Remove Queue Items"),tr("Remove %1 selected item(s) from the queue? Source files will not be deleted.").arg(removable),QMessageBox::Yes|QMessageBox::No,QMessageBox::No);
    if(answer!=QMessageBox::Yes) return;
    std::sort(rows.begin(),rows.end(),std::greater<int>());
    for(int row:rows){ if(row<0||row>=jobs_.size()||!jobIsMutable(jobs_[row])) continue; database_.deleteJob(jobs_[row].uuid); jobs_.removeAt(row); }
    rebuildQueueTable(); statusBar()->showMessage(tr("Queue items removed"),2500);
}
void MainWindow::startQueue(){
    if(capabilities_.ffmpegPath.isEmpty()){QMessageBox::warning(this,tr("FFmpeg unavailable"),tr("FFmpeg is required before the queue can start."));return;}
    QStringList warnings; int blocked=0;
    for(auto &job:jobs_){
        if(job.status!=JobStatus::Pending && job.status!=JobStatus::Interrupted) continue;
        QStringList local; if(!preflightJob(job,&local)) ++blocked; warnings.append(local); database_.saveJob(job);
    }
    rebuildQueueTable();
    if(blocked>0){
        QMessageBox::warning(this,tr("Queue Preflight"),
                             tr("%1 job(s) cannot start. Fix the highlighted output or source problems first.\n\n%2")
                                 .arg(blocked).arg(warnings.join(QStringLiteral("\n"))));
        return;
    }
    if(!warnings.isEmpty()){
        const auto result=QMessageBox::warning(this,tr("Queue Preflight"),
                                               tr("The queue can run, but Flux Encoder found these warnings:\n\n%1\n\nStart anyway?")
                                                   .arg(warnings.join(QStringLiteral("\n"))),
                                               QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes);
        if(result!=QMessageBox::Yes)return;
    }
    statusBar()->showMessage(tr("Queue started"),2500); runner_.start();
}
RenderProfile MainWindow::selectedProfile() const{if(!profileTree_->currentItem())return {};const QString id=profileTree_->currentItem()->data(0,Qt::UserRole).toString();for(const auto &p:profiles_)if(p.uuid==id)return p;return {};}
void MainWindow::profileActivated(QTreeWidgetItem *item,int){if(!item||item->data(0,Qt::UserRole+1).toString()!=QStringLiteral("profile"))return;QString error;if(!applyPresetToRows(item->data(0,Qt::UserRole).toString(),selectedJobRows(),&error)&&!error.isEmpty())QMessageBox::warning(this,tr("Cannot apply preset"),error);}
void MainWindow::editJobExportSettings(int row)
{
    if (row < 0 || row >= jobs_.size()) return;
    if (!jobIsMutable(jobs_[row]) || runner_.isRunning()) return;

    queueTable_->selectRow(row);
    auto &job = jobs_[row];
    RenderProfile editable = job.profileSnapshot;
    if (editable.builtIn) {
        editable.uuid.clear();
        editable.builtIn = false;
        editable.name = tr("%1 Custom").arg(editable.name);
    }

    ProfileEditorDialog dialog(categories_, profiles_, capabilities_, editable,
                               job.durationMs, job.sourcePath, this, &job);
    if (dialog.exec() != QDialog::Accepted) return;

    RenderProfile saved = dialog.profile();
    database_.saveProfile(saved);
    job.profileUuid = saved.uuid;
    job.profileRevision = saved.revision;
    job.profileSnapshot = saved;
    QFileInfo output(job.outputPath);
    job.outputPath = output.dir().filePath(output.completeBaseName() + QStringLiteral(".") + saved.extension);
    job.status = JobStatus::Pending;
    job.updatedAt = QDateTime::currentDateTimeUtc();
    database_.saveJob(job);
    reloadData();
    if (row < queueTable_->rowCount()) queueTable_->selectRow(row);
}

void MainWindow::showNewIpcExportSettings(QueueJob job)
{
    RenderProfile editable=job.profileSnapshot;
    if(editable.builtIn){
        editable.uuid.clear();editable.builtIn=false;
        editable.name=tr("%1 Custom").arg(editable.name);
    }
    ProfileEditorDialog dialog(categories_,profiles_,capabilities_,editable,
                               job.durationMs,job.sourcePath,this,&job);
    if(dialog.exec()!=QDialog::Accepted){
        statusBar()->showMessage(tr("Flux Motion export cancelled — no queue item was added"),5000);
        return;
    }
    RenderProfile saved=dialog.profile();
    database_.saveProfile(saved);
    job.profileUuid=saved.uuid;
    job.profileRevision=saved.revision;
    job.profileSnapshot=saved;
    QFileInfo output(job.outputPath);
    job.outputPath=output.dir().filePath(
        output.completeBaseName()+QStringLiteral(".")+saved.extension);
    job.status=JobStatus::Pending;
    job.updatedAt=QDateTime::currentDateTimeUtc();
    database_.saveJob(job);
    jobs_.push_back(job);
    runner_.setJobs(&jobs_);
    rebuildQueueTable();
    const int row=jobs_.size()-1;
    if(row>=0){queueTable_->selectRow(row);queueSelectionChanged();}
    statusBar()->showMessage(tr("Flux Motion export added to the queue"),5000);
}

void MainWindow::createProfile(){RenderProfile p;p.categoryUuid=categories_.isEmpty()?QString():categories_.last().uuid;ProfileEditorDialog d(categories_,profiles_,capabilities_,p,0,QString(),this);if(d.exec()==QDialog::Accepted){database_.saveProfile(d.profile());reloadData();}}
void MainWindow::editSelectedProfile(){RenderProfile p=selectedProfile();const auto rows=selectedJobRows();if(p.uuid.isEmpty()&&!rows.isEmpty())p=jobs_[rows.first()].profileSnapshot;if(p.uuid.isEmpty())return;if(p.builtIn){p.uuid.clear();p.name+=tr(" Copy");p.builtIn=false;}const qint64 duration=rows.isEmpty()?0:jobs_[rows.first()].durationMs;const QString source=rows.isEmpty()?QString():jobs_[rows.first()].sourcePath;const QueueJob *sourceJob=rows.isEmpty()?nullptr:&jobs_[rows.first()];ProfileEditorDialog d(categories_,profiles_,capabilities_,p,duration,source,this,sourceJob);if(d.exec()==QDialog::Accepted){RenderProfile saved=d.profile();database_.saveProfile(saved);for(int row:rows){jobs_[row].profileUuid=saved.uuid;jobs_[row].profileRevision=saved.revision;jobs_[row].profileSnapshot=saved;database_.saveJob(jobs_[row]);}reloadData();}}
void MainWindow::duplicateSelectedProfile(){RenderProfile p=selectedProfile();if(p.uuid.isEmpty())return;p.uuid.clear();p.name+=tr(" Copy");p.builtIn=false;ProfileEditorDialog d(categories_,profiles_,capabilities_,p,0,QString(),this);if(d.exec()==QDialog::Accepted){database_.saveProfile(d.profile());reloadData();}}
void MainWindow::createCategory(){bool ok=false;QString name=QInputDialog::getText(this,tr("New Preset Group"),tr("Group name"),QLineEdit::Normal,QString(),&ok).trimmed();if(!ok||name.isEmpty())return;ProfileCategory c;c.uuid=createUuid();c.name=name;if(auto *i=profileTree_->currentItem();i&&i->data(0,Qt::UserRole+1).toString()==QStringLiteral("category"))c.parentUuid=i->data(0,Qt::UserRole).toString();database_.saveCategory(c);reloadData();}
QList<int> MainWindow::selectedJobRows() const{QSet<int> s;for(auto *i:queueTable_->selectedItems())s.insert(i->row());QList<int> rows=s.values();std::sort(rows.begin(),rows.end());return rows;}
int MainWindow::jobIndexForUuid(const QString &uuid) const{for(int i=0;i<jobs_.size();++i)if(jobs_[i].uuid==uuid)return i;return -1;}
void MainWindow::handleJobChanged(const QString &uuid){int i=jobIndexForUuid(uuid);if(i>=0)updateQueueRow(i);queueSelectionChanged();}
void MainWindow::queueSelectionChanged(){
    updateQueueSelectionStyles();
    auto rows=selectedJobRows();
    if(queueSelectionSummary_) queueSelectionSummary_->setText(rows.isEmpty() ? tr("No items selected") : tr("%1 item(s) selected").arg(rows.size()));
    if(queueStartButton_) queueStartButton_->setEnabled(!jobs_.isEmpty() && !runner_.isRunning());
    if(rows.isEmpty()){sourceValue_->setText(QStringLiteral("—"));profileValue_->setText(QStringLiteral("—"));encoderValue_->setText(QStringLiteral("—"));outputValue_->clear();fallbackSwitch_->setEnabled(false);return;}
    const auto &j=jobs_[rows.first()];sourceValue_->setText(j.sourcePath);profileValue_->setText(j.profileSnapshot.name);
    encoderValue_->setText(QStringLiteral("%1 (%2)").arg(j.profileSnapshot.videoEncoder,j.profileSnapshot.hardwareBackend));outputValue_->setText(j.outputPath);
    fallbackSwitch_->setEnabled(!runner_.isRunning());QSignalBlocker b(fallbackSwitch_);fallbackSwitch_->setChecked(j.profileSnapshot.allowSoftwareFallback);
    bool active = j.status==JobStatus::Preparing||j.status==JobStatus::Encoding||j.status==JobStatus::Rendering;
    if(!active){previewJobUuid_=j.uuid;lastPreviewTimeMs_=-1;encodingTitle_->setText(QFileInfo(j.sourcePath).fileName());encodingDetails_->setText(tr("Source preview  •  %1").arg(j.profileSnapshot.name));encodingProgress_->setValue(int(j.progress));}
}

void MainWindow::updateQueueSelectionStyles()
{
    if(!queueTable_||!queueTable_->selectionModel())return;
    for(int row=0;row<queueTable_->rowCount();++row){
        const bool selected=queueTable_->selectionModel()->isRowSelected(row,QModelIndex());
        for(int column:{1,2,3}){
            QWidget *widget=queueTable_->cellWidget(row,column);
            if(!widget||widget->property("queueSelected").toBool()==selected)continue;
            widget->setProperty("queueSelected",selected);
            widget->style()->unpolish(widget);
            widget->style()->polish(widget);
            widget->update();
        }
    }
}

void MainWindow::revealOutputFile(const QString &path)
{
    const QFileInfo file(path);
    if(!file.exists()){
        QMessageBox::warning(this,tr("Output file unavailable"),
                             tr("The completed output file no longer exists:\n%1").arg(path));
        return;
    }
#if defined(Q_OS_WIN)
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            {QStringLiteral("/select,"),QDir::toNativeSeparators(file.absoluteFilePath())});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(file.absolutePath()));
#endif
}
void MainWindow::chooseOutput(){auto rows=selectedJobRows();if(rows.size()!=1)return;auto &j=jobs_[rows.first()];QString p=QFileDialog::getSaveFileName(this,tr("Choose Output File"),j.outputPath);if(p.isEmpty())return;j.outputPath=p;j.updatedAt=QDateTime::currentDateTimeUtc();database_.saveJob(j);updateQueueRow(rows.first());queueSelectionChanged();}

void MainWindow::updateEncodingPanel(){
    const QueueJob *active=nullptr;
    for(const auto &j:jobs_) if(j.status==JobStatus::Preparing||j.status==JobStatus::Encoding||j.status==JobStatus::Rendering){active=&j;break;}
    if(!active){
        auto rows=selectedJobRows();
        if(rows.isEmpty()){encodingTitle_->setText(tr("No active encoding"));encodingDetails_->setText(tr("Select a queue item to preview"));encodingProgress_->setValue(0);previewLabel_->clearPreview(tr("Select a queue item to preview"));previewJobUuid_.clear();lastPreviewTimeMs_=-1;previewSlider_->setEnabled(false);previewTimeLabel_->setText(QStringLiteral("00:00:00 / 00:00:00"));}
        else {const auto &j=jobs_[rows.first()];encodingTitle_->setText(QFileInfo(j.sourcePath).fileName());encodingDetails_->setText(tr("Source preview  •  %1").arg(j.profileSnapshot.name));encodingProgress_->setValue(int(j.progress));previewSlider_->setEnabled(j.durationMs>0);if(j.durationMs>0){const qint64 inMs=std::clamp<qint64>(j.profileSnapshot.trimStartMs,0,j.durationMs);const qint64 outMs=j.profileSnapshot.trimEndMs>inMs?std::min<qint64>(j.profileSnapshot.trimEndMs,j.durationMs):j.durationMs;previewSlider_->setInOutValues(int(inMs*1000/j.durationMs),int(outMs*1000/j.durationMs));}previewTimeLabel_->setText(QStringLiteral("%1 / %2").arg(timeText(qRound64((previewSlider_->value()/1000.0)*j.durationMs)/1000),timeText(j.durationMs/1000)));if(previewJobUuid_!=j.uuid){previewJobUuid_=j.uuid;lastPreviewTimeMs_=-1;previewSlider_->setValue(100);}}
        return;
    }
    encodingTitle_->setText(QFileInfo(active->sourcePath).fileName());encodingProgress_->setValue(int(active->progress));
    encodingProgress_->setFormat(QStringLiteral("%1% — %2").arg(active->progress,0,'f',1).arg(timeText(active->etaSeconds)));
    encodingDetails_->setText(tr("%1  •  %2  •  Frame time %3").arg(active->profileSnapshot.name,active->speed,timeText(active->currentTimeMs/1000)));
    previewSlider_->setEnabled(false);if(active->durationMs>0){const qint64 inMs=std::clamp<qint64>(active->profileSnapshot.trimStartMs,0,active->durationMs);const qint64 outMs=active->profileSnapshot.trimEndMs>inMs?std::min<qint64>(active->profileSnapshot.trimEndMs,active->durationMs):active->durationMs;previewSlider_->setInOutValues(int(inMs*1000/active->durationMs),int(outMs*1000/active->durationMs));}if(active->durationMs>0&&!previewSliderDragging_)previewSlider_->setValue(int(std::clamp(active->currentTimeMs*1000.0/active->durationMs,0.0,1000.0)));previewTimeLabel_->setText(QStringLiteral("%1 / %2").arg(timeText(active->currentTimeMs/1000),timeText(active->durationMs/1000)));if(previewJobUuid_!=active->uuid){previewJobUuid_=active->uuid;lastPreviewTimeMs_=-1;if(active->sourceType==QStringLiteral("render-provider"))previewLabel_->clearPreview(tr("Rendering Flux Motion preview…"));QTimer::singleShot(0,this,&MainWindow::refreshEncodingPreview);}
}

void MainWindow::refreshEncodingPreview(){
    if(previewProcess_.state()!=QProcess::NotRunning||previewJobUuid_.isEmpty())return;
    const QueueJob *job=nullptr;for(const auto &j:jobs_)if(j.uuid==previewJobUuid_){job=&j;break;}if(!job)return;
    const bool active=job->status==JobStatus::Preparing||job->status==JobStatus::Encoding||job->status==JobStatus::Rendering;
    qint64 targetMs=active?job->currentTimeMs:(job->durationMs>0?qRound64((previewSlider_->value()/1000.0)*job->durationMs):0);
    if(active&&targetMs<=0&&job->durationMs>0&&job->progress>0.0)
        targetMs=qRound64(job->durationMs*job->progress/100.0);
    if(targetMs<0)targetMs=0;if(qAbs(targetMs-lastPreviewTimeMs_)<500 && previewLabel_->hasPreview())return;
    lastPreviewTimeMs_=targetMs;
    previewProviderRaw_=job->sourceType==QStringLiteral("render-provider");
    if(previewProviderRaw_){
        const auto format=readRenderProviderSourceFormat(*job);
        const QString executable=locateRenderProviderExecutable(job->providerId);
        if(format.width<=0||format.height<=0||format.frameRate<=0.0||executable.isEmpty())return;
        previewRawWidth_=format.width;previewRawHeight_=format.height;
        const qint64 frameDuration=qMax<qint64>(1,qCeil(1000.0/format.frameRate));
        previewProcess_.setProgram(executable);
        previewProcess_.setArguments(renderProviderVideoArguments(
            *job,executable,targetMs,frameDuration,format.frameRate));
    }else{
        if(capabilities_.ffmpegPath.isEmpty())return;
        previewProcess_.setProgram(capabilities_.ffmpegPath);
        previewProcess_.setArguments({QStringLiteral("-hide_banner"),QStringLiteral("-loglevel"),QStringLiteral("error"),QStringLiteral("-ss"),QString::number(targetMs/1000.0,'f',3),QStringLiteral("-i"),job->sourcePath,QStringLiteral("-frames:v"),QStringLiteral("1"),QStringLiteral("-vf"),QStringLiteral("scale=960:-2"),QStringLiteral("-f"),QStringLiteral("image2pipe"),QStringLiteral("-vcodec"),QStringLiteral("mjpeg"),QStringLiteral("pipe:1")});
    }
    previewProcess_.start();
}

void MainWindow::previewFinished(){const QByteArray data=previewProcess_.readAllStandardOutput();QPixmap pix;if(previewProviderRaw_){const qsizetype frameBytes=qsizetype(previewRawWidth_)*previewRawHeight_*4;if(frameBytes>0&&data.size()>=frameBytes){const uchar *last=reinterpret_cast<const uchar*>(data.constData()+data.size()-frameBytes);pix=QPixmap::fromImage(QImage(last,previewRawWidth_,previewRawHeight_,previewRawWidth_*4,QImage::Format_ARGB32_Premultiplied).copy());}}else pix.loadFromData(data);if(!pix.isNull())previewLabel_->setPreviewPixmap(pix);else if(previewProviderRaw_&&!previewLabel_->hasPreview())previewLabel_->clearPreview(tr("Flux Motion preview is temporarily unavailable"));}
void MainWindow::dragEnterEvent(QDragEnterEvent *e){if(e->mimeData()->hasUrls())e->acceptProposedAction();}
void MainWindow::dropEvent(QDropEvent *e){QStringList files;for(const auto &u:e->mimeData()->urls())if(u.isLocalFile())files<<u.toLocalFile();addSources(files);e->acceptProposedAction();}


bool MainWindow::jobIsMutable(const QueueJob &job) const
{
    return job.status != JobStatus::Preparing && job.status != JobStatus::Rendering &&
           job.status != JobStatus::Encoding && job.status != JobStatus::Multiplexing;
}

void MainWindow::persistJobOrder()
{
    const int base = jobs_.size();
    for (int i = 0; i < jobs_.size(); ++i) {
        jobs_[i].priority = base - i;
        jobs_[i].updatedAt = QDateTime::currentDateTimeUtc();
        database_.saveJob(jobs_[i]);
    }
}

void MainWindow::duplicateSelectedJobs()
{
    const auto rows = selectedJobRows();
    if (rows.isEmpty()) return;
    QList<QueueJob> copies;
    for (int row : rows) if (row >= 0 && row < jobs_.size()) copies.append(jobs_.at(row));
    int insertOffset = 0;
    for (int index = 0; index < copies.size(); ++index) {
        QueueJob copy = copies.at(index);
        copy.uuid = createUuid();
        copy.status = JobStatus::Pending; copy.progress = 0.0; copy.currentTimeMs = 0;
        copy.etaSeconds = -1; copy.speed.clear(); copy.errorMessage.clear();
        copy.createdAt = copy.updatedAt = QDateTime::currentDateTimeUtc();
        QFileInfo output(copy.outputPath); int suffix = 1; QString candidate;
        do {
            const QString marker = suffix == 1 ? QStringLiteral("_copy") : QStringLiteral("_copy%1").arg(suffix);
            candidate = output.dir().filePath(QStringLiteral("%1%2.%3").arg(output.completeBaseName(), marker, output.suffix()));
            ++suffix;
        } while (QFileInfo::exists(candidate));
        copy.outputPath = candidate;
        const int destination = std::min(static_cast<int>(jobs_.size()),
                                         rows.at(index) + 1 + insertOffset);
        jobs_.insert(destination, copy); database_.saveJob(copy); ++insertOffset;
    }
    persistJobOrder(); rebuildQueueTable();
}

void MainWindow::retrySelectedJobs()
{
    for (int row : selectedJobRows()) {
        if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row])) continue;
        auto &job = jobs_[row]; job.status = JobStatus::Pending; job.progress = 0.0;
        job.currentTimeMs = 0; job.etaSeconds = -1; job.speed.clear(); job.errorMessage.clear();
        job.updatedAt = QDateTime::currentDateTimeUtc(); database_.saveJob(job); updateQueueRow(row);
    }
}

void MainWindow::moveSelectedJobsUp()
{
    auto rows = selectedJobRows();
    for (int row : rows) if (row > 0 && jobIsMutable(jobs_[row]) && jobIsMutable(jobs_[row-1])) jobs_.swapItemsAt(row,row-1);
    persistJobOrder(); rebuildQueueTable();
    for (int row : rows) queueTable_->selectRow(std::max(0,row-1));
}

void MainWindow::moveSelectedJobsDown()
{
    auto rows = selectedJobRows(); std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) if (row >= 0 && row + 1 < jobs_.size() && jobIsMutable(jobs_[row]) && jobIsMutable(jobs_[row+1])) jobs_.swapItemsAt(row,row+1);
    persistJobOrder(); rebuildQueueTable();
    const int lastRow = static_cast<int>(jobs_.size()) - 1;
    for (int row : rows)
        queueTable_->selectRow(std::min(lastRow, row + 1));
}

void MainWindow::chooseOutputFolderForSelection()
{
    const auto rows = selectedJobRows(); if (rows.isEmpty()) return;
    const QString folder = QFileDialog::getExistingDirectory(this,tr("Choose Output Folder")); if (folder.isEmpty()) return;
    for (int row : rows) {
        if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row])) continue;
        auto &job = jobs_[row]; job.outputPath = QDir(folder).filePath(QFileInfo(job.outputPath).fileName());
        job.status = JobStatus::Pending; job.updatedAt = QDateTime::currentDateTimeUtc(); database_.saveJob(job); updateQueueRow(row);
    }
}

void MainWindow::showQueueContextMenu(const QPoint &pos)
{
    QMenu menu(this);
    menu.addAction(tr("Export Settings…"), this, &MainWindow::editSelectedProfile);
    menu.addAction(tr("Set Output File…"), this, &MainWindow::chooseOutput);
    menu.addAction(tr("Set Output Folder…"), this, &MainWindow::chooseOutputFolderForSelection);
    menu.addSeparator();
    menu.addAction(tr("Duplicate"), this, &MainWindow::duplicateSelectedJobs);
    menu.addAction(tr("Retry / Reset Status"), this, &MainWindow::retrySelectedJobs);
    menu.addAction(tr("Move Up"), this, &MainWindow::moveSelectedJobsUp);
    menu.addAction(tr("Move Down"), this, &MainWindow::moveSelectedJobsDown);
    menu.addSeparator(); menu.addAction(tr("Remove"), this, &MainWindow::removeSelectedJobs);
    menu.exec(queueTable_->viewport()->mapToGlobal(pos));
}

void MainWindow::previewPositionChanged(int value)
{
    if (previewJobUuid_.isEmpty()) return;
    const QueueJob *job = nullptr; for (const auto &candidate : jobs_) if (candidate.uuid == previewJobUuid_) { job = &candidate; break; }
    if (!job || job->durationMs <= 0) return;
    const bool active = job->status == JobStatus::Preparing || job->status == JobStatus::Rendering || job->status == JobStatus::Encoding;
    if (active && !previewSliderDragging_) return;
    const qint64 target = qRound64((value / 1000.0) * job->durationMs);
    previewTimeLabel_->setText(QStringLiteral("%1 / %2").arg(timeText(target/1000),timeText(job->durationMs/1000)));
    if (!active) { lastPreviewTimeMs_ = -1; QTimer::singleShot(80, this, &MainWindow::refreshEncodingPreview); }
}



void MainWindow::browseMediaLocation()
{
    const QString current = mediaLocationCombo_ ? mediaLocationCombo_->currentText() : QDir::homePath();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose Media Location"), current);
    if (!folder.isEmpty()) mediaLocationCombo_->setCurrentText(folder);
}

void MainWindow::refreshMediaBrowser()
{
    if (!mediaBrowser_ || !mediaLocationCombo_) return;
    const QDir dir(mediaLocationCombo_->currentText());
    mediaBrowser_->clear();
    if (!dir.exists()) return;
    const QFileInfoList entries = dir.entryInfoList(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot,
                                                     QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    static const QSet<QString> mediaExtensions{QStringLiteral("mp4"),QStringLiteral("mov"),QStringLiteral("mkv"),QStringLiteral("avi"),QStringLiteral("webm"),QStringLiteral("mxf"),QStringLiteral("mp3"),QStringLiteral("wav"),QStringLiteral("flac"),QStringLiteral("aac"),QStringLiteral("m4a"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("tif"),QStringLiteral("tiff")};
    for (const QFileInfo &entry : entries) {
        if (entry.isFile() && !mediaExtensions.contains(entry.suffix().toLower())) continue;
        auto *item = new QListWidgetItem(entry.isDir() ? QStringLiteral("📁 %1").arg(entry.fileName()) : entry.fileName(), mediaBrowser_);
        item->setData(Qt::UserRole, entry.absoluteFilePath());
        item->setToolTip(entry.absoluteFilePath());
    }
}

void MainWindow::addWatchFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Add Watch Folder"));
    if (folder.isEmpty()) return;
    for (int i=0;i<watchFolders_->count();++i) if (watchFolders_->item(i)->text()==folder) return;
    watchFolders_->addItem(folder);
    QSettings settings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    QStringList folders; for(int i=0;i<watchFolders_->count();++i) folders << watchFolders_->item(i)->text(); settings.setValue(QStringLiteral("watchFolders"),folders);
    scanWatchFolders();
}

void MainWindow::removeWatchFolder()
{
    const auto selected = watchFolders_->selectedItems();
    for (QListWidgetItem *item : selected) delete item;
    QSettings settings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    QStringList folders; for(int i=0;i<watchFolders_->count();++i) folders << watchFolders_->item(i)->text(); settings.setValue(QStringLiteral("watchFolders"),folders);
}

void MainWindow::scanWatchFolders()
{
    if (!watchFolders_) return;
    static const QStringList filters{QStringLiteral("*.mp4"),QStringLiteral("*.mov"),QStringLiteral("*.mkv"),QStringLiteral("*.avi"),QStringLiteral("*.webm"),QStringLiteral("*.mxf"),QStringLiteral("*.mp3"),QStringLiteral("*.wav"),QStringLiteral("*.flac"),QStringLiteral("*.m4a")};
    QStringList discovered;
    for(int i=0;i<watchFolders_->count();++i) {
        QDir dir(watchFolders_->item(i)->text());
        for (const QFileInfo &file : dir.entryInfoList(filters,QDir::Files,QDir::Time)) {
            const QString path=file.absoluteFilePath();
            if (!watchFolderSeen_.contains(path)) { watchFolderSeen_.insert(path); discovered << path; }
        }
    }
    if (!discovered.isEmpty()) addSources(discovered);
}

void MainWindow::rendererChanged(int index)
{
    if (!rendererCombo_ || index < 0 || runner_.isRunning()) return;
    const QString requested = rendererCombo_->itemData(index).toString();
    for (int row : selectedJobRows()) {
        if (row < 0 || row >= jobs_.size() || !jobIsMutable(jobs_[row])) continue;
        RenderProfile replacement;
        if (requested == QStringLiteral("auto")) replacement = smartProfileForSource(jobs_[row].sourcePath);
        else {
            for (const RenderProfile &candidate : profiles_) {
                if (requested == QStringLiteral("software")) {
                    if (candidate.enabled && candidate.hardwareBackend == QStringLiteral("software") && profileFormatKey(candidate)==profileFormatKey(jobs_[row].profileSnapshot)) { replacement=candidate; break; }
                } else if (candidate.enabled && candidate.videoEncoder == requested) { replacement=candidate; break; }
            }
        }
        if (replacement.uuid.isEmpty()) continue;
        jobs_[row].profileUuid=replacement.uuid; jobs_[row].profileRevision=replacement.revision; jobs_[row].profileSnapshot=replacement;
        QFileInfo out(jobs_[row].outputPath); jobs_[row].outputPath=out.dir().filePath(out.completeBaseName()+QStringLiteral(".")+replacement.extension);
        jobs_[row].updatedAt=QDateTime::currentDateTimeUtc(); database_.saveJob(jobs_[row]); updateQueueRow(row);
    }
}

RenderProfile MainWindow::smartProfileForSource(const QString &path) const
{
    const QString suffix=QFileInfo(path).suffix().toLower();
    const QSet<QString> audioSuffixes={QStringLiteral("wav"),QStringLiteral("mp3"),QStringLiteral("aac"),QStringLiteral("m4a"),QStringLiteral("flac"),QStringLiteral("ogg"),QStringLiteral("opus"),QStringLiteral("aiff"),QStringLiteral("aif")};
    if(audioSuffixes.contains(suffix)){
        for(const auto &profile:profiles_) if(profile.enabled && profile.videoEncoder.isEmpty() && profile.audioEncoder.contains(QStringLiteral("aac"),Qt::CaseInsensitive)) return profile;
        for(const auto &profile:profiles_) if(profile.enabled && profile.videoEncoder.isEmpty()) return profile;
    }
    const QStringList preference{QStringLiteral("h264_nvenc"),QStringLiteral("h264_qsv"),QStringLiteral("h264_amf"),QStringLiteral("h264_vaapi"),QStringLiteral("libx264")};
    for(const QString &encoder:preference) for(const auto &profile:profiles_)
        if(profile.enabled && profile.videoEncoder==encoder && (encoder==QStringLiteral("libx264") || capabilities_.encoderUsable(encoder))) return profile;
    for(const auto &profile:profiles_) if(profile.enabled) return profile;
    return profiles_.isEmpty()?RenderProfile():profiles_.first();
}

bool MainWindow::preflightJob(QueueJob &job, QStringList *warnings) const
{
    bool valid=true;
    auto add=[&](const QString &text){if(warnings)warnings->append(QStringLiteral("• %1: %2").arg(QFileInfo(job.sourcePath).fileName(),text));};
    if(!QFileInfo::exists(job.sourcePath)){job.status=JobStatus::Failed;job.errorMessage=tr("Source file does not exist");add(job.errorMessage);valid=false;}
    QFileInfo output(job.outputPath);QDir outDir=output.dir();
    if(!outDir.exists() || !QFileInfo(outDir.absolutePath()).isWritable()){job.status=JobStatus::Failed;job.errorMessage=tr("Output folder is not writable");add(job.errorMessage);valid=false;}
    if(QFileInfo::exists(job.outputPath))add(tr("Output already exists and will be replaced"));
    if(!job.profileSnapshot.enabled && !job.profileSnapshot.allowSoftwareFallback){job.status=JobStatus::Failed;job.errorMessage=job.profileSnapshot.unavailableReason;add(job.errorMessage);valid=false;}
    if(job.durationMs<=0)add(tr("Duration could not be detected; ETA and size estimate may be unavailable"));
    if(valid && (job.status==JobStatus::Failed || job.status==JobStatus::Interrupted))job.status=JobStatus::Pending;
    return valid;
}

void MainWindow::saveCurrentWorkspace(const QString &name)
{
    QSettings settings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    settings.setValue(QStringLiteral("workspaces/%1/state").arg(name),saveState(4));
    settings.setValue(QStringLiteral("workspaces/%1/geometry").arg(name),saveGeometry());
    settings.setValue(QStringLiteral("workspace/last"),name); currentWorkspace_=name;
}

void MainWindow::applyWorkspace(const QString &name)
{
    if(workspaceCombo_ && workspaceCombo_->currentText()!=name){ const QSignalBlocker blocker(workspaceCombo_); workspaceCombo_->setCurrentText(name); }
    QSettings settings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    const QByteArray state=settings.value(QStringLiteral("workspaces/%1/state").arg(name)).toByteArray();
    const QByteArray geometry=settings.value(QStringLiteral("workspaces/%1/geometry").arg(name)).toByteArray();
    if(!geometry.isEmpty())restoreGeometry(geometry);if(!state.isEmpty())restoreState(state,4);currentWorkspace_=name;
    settings.setValue(QStringLiteral("workspace/last"),name);
}

void MainWindow::restoreLastWorkspace()
{
    constexpr int kWorkspaceLayoutRevision = 4;
    QSettings settings(QStringLiteral("Flux"),QStringLiteral("FluxEncoder"));
    if(settings.value(QStringLiteral("workspace/layoutRevision")).toInt()!=kWorkspaceLayoutRevision){
        settings.setValue(QStringLiteral("workspace/layoutRevision"),kWorkspaceLayoutRevision);
        if(!defaultWorkspaceState_.isEmpty())restoreState(defaultWorkspaceState_,4);
        if(workspaceCombo_){const QSignalBlocker blocker(workspaceCombo_);workspaceCombo_->setCurrentText(tr("Default Workspace"));}
        saveCurrentWorkspace(QStringLiteral("Default Workspace"));
        return;
    }
    const QString last=settings.value(QStringLiteral("workspace/last"),QStringLiteral("Default Workspace")).toString();
    if(settings.contains(QStringLiteral("workspaces/%1/state").arg(last)))applyWorkspace(last);else resetWorkspace();
}

void MainWindow::saveWorkspaceAs()
{
    bool ok=false;const QString name=QInputDialog::getText(this,tr("Save Workspace"),tr("Workspace name"),QLineEdit::Normal,currentWorkspace_,&ok).trimmed();if(ok&&!name.isEmpty())saveCurrentWorkspace(name);
}

void MainWindow::resetWorkspace()
{
    for(auto *dock:dockPanels_)dock->setFloating(false);
    if(!defaultWorkspaceState_.isEmpty())restoreState(defaultWorkspaceState_,4);
    if(workspaceCombo_){const QSignalBlocker blocker(workspaceCombo_);workspaceCombo_->setCurrentText(tr("Default Workspace"));}
    saveCurrentWorkspace(QStringLiteral("Default Workspace"));
}

void MainWindow::setLayoutLocked(bool locked)
{
    layoutLocked_=locked;
    if(lockDocksAction_ && lockDocksAction_->isChecked()!=locked){const QSignalBlocker blocker(lockDocksAction_);lockDocksAction_->setChecked(locked);}
    for(auto *dock:dockPanels_)dock->setFeatures(locked?QDockWidget::NoDockWidgetFeatures:(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveCurrentWorkspace(currentWorkspace_);QMainWindow::closeEvent(event);
}



bool MainWindow::applyPresetToRows(const QString &profileUuid, const QList<int> &requestedRows, QString *error)
{
    const auto it=std::find_if(profiles_.cbegin(),profiles_.cend(),[&](const RenderProfile &p){return p.uuid==profileUuid;});
    if(it==profiles_.cend()){if(error)*error=tr("The dropped preset no longer exists.");return false;}
    const RenderProfile preset=*it;
    if(!preset.enabled && !preset.allowSoftwareFallback){if(error)*error=preset.unavailableReason.isEmpty()?tr("This hardware preset is unavailable on this system."):preset.unavailableReason;return false;}
    QList<int> rows=requestedRows;
    rows.erase(std::remove_if(rows.begin(),rows.end(),[&](int row){return row<0||row>=jobs_.size();}),rows.end());
    if(rows.isEmpty()){if(error)*error=tr("Select a queue item or drop the preset directly on a queue row.");return false;}
    int applied=0;
    for(int row:rows){auto &job=jobs_[row];if(!jobIsMutable(job))continue;QFileInfo output(job.outputPath);job.profileUuid=preset.uuid;job.profileRevision=preset.revision;job.profileSnapshot=preset;job.outputPath=output.dir().filePath(output.completeBaseName()+QStringLiteral(".")+preset.extension);job.status=JobStatus::Pending;job.progress=0.0;job.errorMessage.clear();job.updatedAt=QDateTime::currentDateTimeUtc();database_.saveJob(job);updateQueueRow(row);++applied;}
    if(applied==0){if(error)*error=tr("Active queue items cannot be changed.");return false;}
    statusBar()->showMessage(tr("Applied “%1” to %2 queue item(s)").arg(preset.name).arg(applied),3000);queueSelectionChanged();return true;
}

void MainWindow::refreshProviderStatus()
{
    if(!providerCatalog_||!addRenderProjectAction_)return;
    const bool available=providerCatalog_->hasUsableProvider();
    addRenderProjectAction_->setEnabled(available);
    addRenderProjectAction_->setToolTip(available?tr("Add a project handled by an installed Flux Encoder Render Provider"):tr("Install a Flux Encoder Render Provider to add render projects. The Flux Motion provider can render projects with Flux Motion closed."));
}

void MainWindow::addRenderProject()
{
    if(!providerCatalog_||!providerCatalog_->hasUsableProvider()){QMessageBox::information(this,tr("No Render Provider"),tr("No Flux Encoder Render Provider is installed.\n\nInstall the Flux Motion Render Provider to render Flux Motion projects while Flux Motion is closed."));return;}
    const auto providers=providerCatalog_->providers();QStringList filters;for(const auto &p:providers)if(p.usable)filters+=p.projectExtensions;
    const QString filter=filters.isEmpty()?tr("Render projects (*.*)"):tr("Render projects (%1)").arg(filters.join(QLatin1Char(' ')));
    const QString path=QFileDialog::getOpenFileName(this,tr("Add Render Project"),QString(),filter);if(path.isEmpty())return;
    QString error;if(!addRenderProjectPath(path,&error)){QMessageBox::warning(this,tr("Could not add render project"),error);return;}runner_.setJobs(&jobs_);rebuildQueueTable();queueTable_->selectRow(jobs_.size()-1);queueSelectionChanged();
}

bool MainWindow::addRenderProjectPath(const QString &path, QString *error)
{
    if(!providerCatalog_){if(error)*error=tr("Render provider catalog is unavailable.");return false;}
    const auto *provider=providerCatalog_->providerForPath(path);
    if(!provider){if(error)*error=tr("No installed provider accepts this project type.");return false;}
    const auto inspection=inspectRenderProviderProject(provider->id,path,error);
    if(!inspection.valid)return false;
    QueueJob job;job.uuid=createUuid();job.sourcePath=QFileInfo(path).absoluteFilePath();job.sourceType=QStringLiteral("render-provider");job.providerId=provider->id;
    RenderSourceDescriptor descriptor=RenderSourceDescriptor::forProject(provider->id,job.sourcePath);descriptor.compositionId=inspection.compositionId;descriptor.renderOptions={
        {QStringLiteral("projectFilePath"),job.sourcePath},
        {QStringLiteral("projectName"),inspection.name},
        {QStringLiteral("sourceWidth"),inspection.format.width},
        {QStringLiteral("sourceHeight"),inspection.format.height},
        {QStringLiteral("sourceFrameRate"),inspection.format.frameRate},
        {QStringLiteral("sourceDurationMs"),inspection.format.durationMs},
        {QStringLiteral("hasAudio"),inspection.hasAudio},
        {QStringLiteral("externalDataMode"),QStringLiteral("snapshot")},
        {QStringLiteral("alphaMode"),QStringLiteral("straight")}};
    job.sourceDescriptor=descriptor.toJson();job.profileSnapshot=smartProfileForSource(path);job.profileSnapshot.width=inspection.format.width;job.profileSnapshot.height=inspection.format.height;job.profileSnapshot.frameRate=inspection.format.frameRate;job.profileUuid=job.profileSnapshot.uuid;job.profileRevision=job.profileSnapshot.revision;job.durationMs=inspection.format.durationMs;job.outputPath=JobBuilder::defaultOutputPath(path,job.profileSnapshot);
    QFileInfo outputInfo(job.outputPath);int suffix=2;while(QFileInfo::exists(job.outputPath))job.outputPath=outputInfo.dir().filePath(QStringLiteral("%1_%2.%3").arg(outputInfo.completeBaseName()).arg(suffix++).arg(outputInfo.suffix()));
    database_.saveJob(job);jobs_.push_back(job);runner_.setJobs(&jobs_);if(error)error->clear();return true;
}
} // namespace flux
