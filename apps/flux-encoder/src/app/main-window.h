#pragma once
#include "core/database.h"
#include "core/queue-runner.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QByteArray>
#include <QJsonObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QSet>

class QTreeWidget; class QTreeWidgetItem; class FluxSwitch; class QTableWidget; class QLabel;
class QPlainTextEdit; class QProgressBar; class QAction; class QLineEdit; class QListWidget;
class QSlider; class QTabWidget; class QDockWidget; class QCloseEvent; class QMenu; class QComboBox; class QPushButton; class QToolButton; class FluxEncoderZoomablePreview; class FluxEncoderRangeSlider;

namespace flux {
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    void openSourceFiles(const QStringList &paths);
    bool openExportFromIpc(const QJsonObject &source, const QJsonObject &options,
                           QString *error=nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
private slots:
    void addFiles(); void removeSelectedJobs(); void startQueue(); void createProfile();
    void editSelectedProfile(); void duplicateSelectedProfile(); void createCategory();
    void profileActivated(QTreeWidgetItem *item,int column); void queueSelectionChanged();
    void handleJobChanged(const QString &uuid); void handleCapabilitiesReady(); void chooseOutput();
    void refreshEncodingPreview(); void previewFinished();
    void saveWorkspaceAs(); void resetWorkspace(); void setLayoutLocked(bool locked);
    void applyWorkspace(const QString &name);
    void duplicateSelectedJobs(); void retrySelectedJobs(); void moveSelectedJobsUp();
    void moveSelectedJobsDown(); void chooseOutputFolderForSelection(); void showQueueContextMenu(const QPoint &pos);
    void previewPositionChanged(int value);
    void browseMediaLocation();
    void refreshMediaBrowser();
    void addWatchFolder();
    void removeWatchFolder();
    void scanWatchFolders();
    void rendererChanged(int index);
    void addRenderProject();
private:
    void buildUi(); void buildMenus(); void detectCapabilities(); void reloadData();
    void repairIncompleteRenderJobs();
    void rebuildProfileTree(); void rebuildQueueTable(); void updateQueueRow(int row);
    void addSources(const QStringList &paths); void updateEncodingPanel();
    bool openQueueFile(const QString &path, QString *error=nullptr);
    void updateQueueSelectionStyles();
    void editJobExportSettings(int row);
    void showNewIpcExportSettings(QueueJob job);
    void revealOutputFile(const QString &path);
    bool preflightJob(QueueJob &job, QStringList *warnings=nullptr) const;
    RenderProfile smartProfileForSource(const QString &path) const;
    void saveCurrentWorkspace(const QString &name); void restoreLastWorkspace();
    RenderProfile selectedProfile() const; int jobIndexForUuid(const QString &uuid) const;
    QList<int> selectedJobRows() const;
    bool jobIsMutable(const QueueJob &job) const; void persistJobOrder();
    bool applyPresetToRows(const QString &profileUuid, const QList<int> &rows, QString *error=nullptr);
    void refreshProviderStatus();
    bool addRenderProjectPath(const QString &path, QString *error = nullptr);

    Database database_; FfmpegCapabilities capabilities_; QList<ProfileCategory> categories_;
    QList<RenderProfile> profiles_; QList<QueueJob> jobs_; QueueRunner runner_;
    QFutureWatcher<FfmpegCapabilities> capabilityWatcher_;
    QTreeWidget *profileTree_=nullptr; QTableWidget *queueTable_=nullptr;
    QLabel *sourceValue_=nullptr; QLabel *profileValue_=nullptr; QLabel *encoderValue_=nullptr;
    QLineEdit *outputValue_=nullptr; QPlainTextEdit *log_=nullptr; QProgressBar *totalProgress_=nullptr;
    QLabel *capabilityLabel_=nullptr; FluxSwitch *fallbackSwitch_=nullptr;
    QAction *startAction_=nullptr; QAction *stopAction_=nullptr;
    QLabel *queueEmptyState_=nullptr; QLabel *queueSelectionSummary_=nullptr; QPushButton *queueStartButton_=nullptr;
    FluxEncoderZoomablePreview *previewLabel_=nullptr; QLabel *encodingTitle_=nullptr; QLabel *encodingDetails_=nullptr;
    QProgressBar *encodingProgress_=nullptr; FluxEncoderRangeSlider *previewSlider_=nullptr; QLabel *previewTimeLabel_=nullptr;
    QSlider *previewZoom_=nullptr; QToolButton *previewFit_=nullptr;
    QComboBox *rendererCombo_=nullptr; QComboBox *workspaceCombo_=nullptr;
    QListWidget *mediaBrowser_=nullptr; QListWidget *watchFolders_=nullptr;
    QComboBox *mediaLocationCombo_=nullptr; QLineEdit *mediaSearch_=nullptr;
    QTimer watchFolderTimer_; QSet<QString> watchFolderSeen_;
    QProcess previewProcess_; QTimer previewTimer_; QString previewJobUuid_; qint64 lastPreviewTimeMs_=-1;
    bool previewProviderRaw_=false; int previewRawWidth_=0; int previewRawHeight_=0;
    QList<QDockWidget*> dockPanels_;
    QMenu *windowMenu_=nullptr; QAction *lockDocksAction_=nullptr;
    class ProviderCatalog *providerCatalog_=nullptr; QAction *addRenderProjectAction_=nullptr; bool layoutLocked_=false; bool previewSliderDragging_=false;
    QByteArray defaultWorkspaceState_; QString currentWorkspace_=QStringLiteral("Default Workspace");
    QStringList pendingSourcePaths_;
    QString pendingQueuePath_;
    QList<QPair<QJsonObject,QJsonObject>> pendingExportRequests_;
    QSet<QString> pendingIpcCompositionIds_;
    bool initializationComplete_=false;
};
}
