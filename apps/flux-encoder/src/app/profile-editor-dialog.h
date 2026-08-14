#pragma once
#include "core/media-types.h"
#include <QDialog>

class QCheckBox; class QComboBox; class QLineEdit; class QSpinBox; class QDoubleSpinBox;
class QPlainTextEdit; class QLabel; class QSlider; class QTabWidget; class QToolButton; class QProcess; class QTimer; class QTemporaryDir; class QSplitter;
class QPixmap;
class FluxSwitch; class FluxEncoderRangeSlider; class FluxEncoderZoomablePreview;

namespace flux {
class ProfileEditorDialog final : public QDialog {
    Q_OBJECT
public:
    ProfileEditorDialog(const QList<ProfileCategory> &categories,
                        const QList<RenderProfile> &profiles,
                        const FfmpegCapabilities &capabilities,
                        const RenderProfile &profile,
                        qint64 sourceDurationMs = 0,
                        const QString &sourceName = QString(),
                        QWidget *parent = nullptr,
                        const QueueJob *sourceJob = nullptr);
    ~ProfileEditorDialog() override;
    RenderProfile profile() const;

private slots:
    void updateControlState();
    void updateEstimate();
    void matchSource();

private:
    QWidget *makeVideoPage();
    QWidget *makeAudioPage();
    QWidget *makeEffectsPage();
    QWidget *makeMuxerPage();
    QWidget *makeCaptionsPage();
    QWidget *makePublishPage();
    void rebuildPresetList(const QString &formatKey, const QString &preferredUuid = QString());
    void applyPresetToControls(const RenderProfile &profile);
    QWidget *makeSection(const QString &title, QWidget *content, bool expanded = true);
    QString estimateText() const;
    void scheduleOutputPreview(qint64 positionMs);
    void startOutputPreview();
    void startOutputPreviewDecode();
    void failOutputPreview(const QString &message);
    QPixmap renderSourceFrame(qint64 positionMs) const;

    RenderProfile original_;
    QList<RenderProfile> availableProfiles_;
    FfmpegCapabilities capabilities_;
    qint64 sourceDurationMs_ = 0;
    QString sourceName_;
    QueueJob sourceJob_;

    QLineEdit *name_ = nullptr;
    QComboBox *presetProfile_ = nullptr;
    QLineEdit *description_ = nullptr;
    QComboBox *category_ = nullptr;
    QComboBox *backend_ = nullptr;
    QComboBox *encoder_ = nullptr;
    QComboBox *audioEncoder_ = nullptr;
    QComboBox *container_ = nullptr;
    QComboBox *rateControl_ = nullptr;
    QComboBox *fieldOrder_ = nullptr;
    QComboBox *aspect_ = nullptr;
    QComboBox *profileLevel_ = nullptr;
    QComboBox *codecLevel_ = nullptr;
    QComboBox *sampleRate_ = nullptr;
    QComboBox *channels_ = nullptr;
    QSpinBox *width_ = nullptr;
    QSpinBox *height_ = nullptr;
    QCheckBox *widthOverride_ = nullptr;
    QCheckBox *heightOverride_ = nullptr;
    QDoubleSpinBox *fps_ = nullptr;
    QSpinBox *quality_ = nullptr;
    QDoubleSpinBox *videoBitrate_ = nullptr;
    QDoubleSpinBox *maxBitrate_ = nullptr;
    QComboBox *bitrateUnit_ = nullptr;
    QSpinBox *audioBitrate_ = nullptr;
    QComboBox *preset_ = nullptr;
    FluxSwitch *fallback_ = nullptr;
    FluxSwitch *exportVideo_ = nullptr;
    FluxSwitch *exportAudio_ = nullptr;
    QPlainTextEdit *extraArgs_ = nullptr;
    QLabel *estimateLabel_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    QCheckBox *normalizeLoudness_ = nullptr;
    QCheckBox *truePeakLimit_ = nullptr;
    QCheckBox *writeMetadata_ = nullptr;
    QString metadataMode_;
    QString metadataPreservation_;
    bool metadataIncludeMarkers_ = true;
    QString metadataTemplate_;
    QJsonObject metadataFields_;
    QCheckBox *fastStart_ = nullptr;
    QCheckBox *preserveTimecode_ = nullptr;
    QCheckBox *burnCaptions_ = nullptr;
    QCheckBox *videoLimiter_ = nullptr;
    QCheckBox *burnTimecode_ = nullptr;
    QLineEdit *timecodeStart_ = nullptr;
    QComboBox *timecodePosition_ = nullptr;
    QSpinBox *timecodeFontSize_ = nullptr;
    QSpinBox *timecodeOpacity_ = nullptr;
    QSpinBox *timecodeBackgroundOpacity_ = nullptr;
    FluxEncoderRangeSlider *timeline_ = nullptr;
    FluxEncoderZoomablePreview *sourcePreview_ = nullptr;
    FluxEncoderZoomablePreview *outputPreview_ = nullptr;
    FluxEncoderZoomablePreview *compareSourcePreview_ = nullptr;
    FluxEncoderZoomablePreview *compareOutputPreview_ = nullptr;
    QTabWidget *previewModeTabs_ = nullptr;
    QTabWidget *settingsTabs_ = nullptr;
    QSplitter *mainSplitter_ = nullptr;
    QSlider *previewZoom_ = nullptr;
    QToolButton *previewFit_ = nullptr;
    QLabel *currentTimeLabel_ = nullptr;
    QLabel *inOutLabel_ = nullptr;
    qint64 trimStartMs_ = 0;
    qint64 trimEndMs_ = 0;
    qint64 pendingOutputPreviewMs_ = 0;
    qint64 outputPreviewSegmentStartMs_ = 0;
    QString outputPreviewFile_;
    QTimer *outputPreviewDebounce_ = nullptr;
    QProcess *outputPreviewEncode_ = nullptr;
    QProcess *outputPreviewDecode_ = nullptr;
    QTemporaryDir *outputPreviewTempDir_ = nullptr;
};
}
