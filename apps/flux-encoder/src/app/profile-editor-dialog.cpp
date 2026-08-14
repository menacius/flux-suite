#include "app/profile-editor-dialog.h"
#include "app/metadata-export-dialog.h"
#include "ui/flux/flux-modern-controls.h"
#include "ui/preview-controls.h"
#include "core/job-builder.h"
#include "providers/render-provider-support.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPixmap>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace flux {
namespace {
QLabel *smallLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("ExportSettingsHint"));
    label->setWordWrap(true);
    return label;
}

QString formatBytes(double bytes)
{
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KB"), QStringLiteral("MB"), QStringLiteral("GB"), QStringLiteral("TB")};
    int unit = 0;
    while (bytes >= 1024.0 && unit < units.size() - 1) { bytes /= 1024.0; ++unit; }
    return QStringLiteral("%1 %2").arg(bytes, 0, unit < 2 ? 'f' : 'f', unit < 2 ? 0 : 2).arg(units.at(unit));
}


struct FluxEncoderFormatSpec {
    QString key;
    QString label;
    QString container;
    QString extension;
    QString softwareEncoder;
    QString audioEncoder;
    bool video = true;
    bool hardwareCapable = false;
};

const QList<FluxEncoderFormatSpec> &fluxEncoderFormats()
{
    static const QList<FluxEncoderFormatSpec> formats{
        {QStringLiteral("h264"), QStringLiteral("H.264"), QStringLiteral("mp4"), QStringLiteral("mp4"), QStringLiteral("libx264"), QStringLiteral("aac"), true, true},
        {QStringLiteral("hevc"), QStringLiteral("HEVC (H.265)"), QStringLiteral("mp4"), QStringLiteral("mp4"), QStringLiteral("libx265"), QStringLiteral("aac"), true, true},
        {QStringLiteral("av1"), QStringLiteral("AV1"), QStringLiteral("mkv"), QStringLiteral("mkv"), QStringLiteral("libsvtav1"), QStringLiteral("libopus"), true, true},
        {QStringLiteral("quicktime"), QStringLiteral("QuickTime"), QStringLiteral("mov"), QStringLiteral("mov"), QStringLiteral("prores_ks"), QStringLiteral("pcm_s24le"), true, false},
        {QStringLiteral("dnxhr"), QStringLiteral("DNxHR / DNxHD MXF OP1a"), QStringLiteral("mxf"), QStringLiteral("mxf"), QStringLiteral("dnxhd"), QStringLiteral("pcm_s24le"), true, false},
        {QStringLiteral("mpeg2"), QStringLiteral("MPEG2"), QStringLiteral("mxf"), QStringLiteral("mxf"), QStringLiteral("mpeg2video"), QStringLiteral("pcm_s24le"), true, false},
        {QStringLiteral("webm"), QStringLiteral("WebM"), QStringLiteral("webm"), QStringLiteral("webm"), QStringLiteral("libvpx-vp9"), QStringLiteral("libopus"), true, false},
        {QStringLiteral("waveform"), QStringLiteral("Waveform Audio"), QStringLiteral("wav"), QStringLiteral("wav"), QString(), QStringLiteral("pcm_s24le"), false, false},
        {QStringLiteral("mp3"), QStringLiteral("MP3"), QStringLiteral("mp3"), QStringLiteral("mp3"), QString(), QStringLiteral("libmp3lame"), false, false},
        {QStringLiteral("flac"), QStringLiteral("FLAC"), QStringLiteral("flac"), QStringLiteral("flac"), QString(), QStringLiteral("flac"), false, false},
        {QStringLiteral("aac"), QStringLiteral("AAC Audio"), QStringLiteral("m4a"), QStringLiteral("m4a"), QString(), QStringLiteral("aac"), false, false}
    };
    return formats;
}

const FluxEncoderFormatSpec &formatSpec(const QString &key)
{
    for (const auto &format : fluxEncoderFormats())
        if (format.key == key)
            return format;
    return fluxEncoderFormats().first();
}

QString formatKeyForProfile(const RenderProfile &profile)
{
    const QString video = profile.videoEncoder.toLower();
    const QString audio = profile.audioEncoder.toLower();
    if (video.contains(QStringLiteral("264"))) return QStringLiteral("h264");
    if (video.contains(QStringLiteral("265")) || video.contains(QStringLiteral("hevc"))) return QStringLiteral("hevc");
    if (video.contains(QStringLiteral("av1"))) return QStringLiteral("av1");
    if (video.contains(QStringLiteral("prores"))) return QStringLiteral("quicktime");
    if (video.contains(QStringLiteral("dnx"))) return QStringLiteral("dnxhr");
    if (video.contains(QStringLiteral("mpeg2"))) return QStringLiteral("mpeg2");
    if (video.contains(QStringLiteral("vp9"))) return QStringLiteral("webm");
    if (video.isEmpty()) {
        if (audio.contains(QStringLiteral("mp3"))) return QStringLiteral("mp3");
        if (audio.contains(QStringLiteral("flac"))) return QStringLiteral("flac");
        if (audio.startsWith(QStringLiteral("pcm_"))) return QStringLiteral("waveform");
        return QStringLiteral("aac");
    }
    return QStringLiteral("h264");
}

QString hardwareEncoderFor(const QString &formatKey, const QString &backend)
{
    const QString codec = formatKey == QStringLiteral("h264") ? QStringLiteral("h264")
                         : formatKey == QStringLiteral("hevc") ? QStringLiteral("hevc")
                         : formatKey == QStringLiteral("av1") ? QStringLiteral("av1") : QString();
    if (codec.isEmpty()) return QString();
    if (backend == QStringLiteral("nvenc")) return codec + QStringLiteral("_nvenc");
    if (backend == QStringLiteral("qsv")) return codec + QStringLiteral("_qsv");
    if (backend == QStringLiteral("amf")) return codec + QStringLiteral("_amf");
    if (backend == QStringLiteral("vaapi")) return codec + QStringLiteral("_vaapi");
    return QString();
}

QString bestHardwareBackend(const QString &formatKey, const QString &preferred,
                            const FfmpegCapabilities &capabilities)
{
    QStringList backends{preferred, QStringLiteral("nvenc"), QStringLiteral("qsv"),
                         QStringLiteral("amf"), QStringLiteral("vaapi")};
    backends.removeDuplicates();
    for (const QString &backend : backends) {
        const QString encoder = hardwareEncoderFor(formatKey, backend);
        if (!encoder.isEmpty() && capabilities.encoderUsable(encoder))
            return backend;
    }
    return QString();
}

QString shiftedTimecode(const QString &value, qint64 offsetMs, double fps)
{
    const QStringList parts = value.split(QLatin1Char(':'));
    if (parts.size() != 4 || fps <= 0.0) return value;
    bool ok[4] = {false, false, false, false};
    const qint64 h = parts[0].toLongLong(&ok[0]);
    const qint64 m = parts[1].toLongLong(&ok[1]);
    const qint64 sec = parts[2].toLongLong(&ok[2]);
    const qint64 frame = parts[3].toLongLong(&ok[3]);
    if (!(ok[0] && ok[1] && ok[2] && ok[3])) return value;
    const qint64 nominal = qMax<qint64>(1, qRound64(fps));
    qint64 totalFrames = (((h * 60 + m) * 60 + sec) * nominal + frame);
    totalFrames += qMax<qint64>(0, qRound64(offsetMs * fps / 1000.0));
    const qint64 framesPerDay = nominal * 24 * 60 * 60;
    totalFrames %= framesPerDay;
    const qint64 outH = totalFrames / (nominal * 3600);
    totalFrames %= nominal * 3600;
    const qint64 outM = totalFrames / (nominal * 60);
    totalFrames %= nominal * 60;
    const qint64 outS = totalFrames / nominal;
    const qint64 outF = totalFrames % nominal;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(outH, 2, 10, QLatin1Char('0')).arg(outM, 2, 10, QLatin1Char('0'))
        .arg(outS, 2, 10, QLatin1Char('0')).arg(outF, 2, 10, QLatin1Char('0'));
}

QString performanceLabel(const QString &backend)
{
    if (backend == QStringLiteral("nvenc")) return QObject::tr("Hardware Encoding (NVIDIA NVENC)");
    if (backend == QStringLiteral("qsv")) return QObject::tr("Hardware Encoding (Intel Quick Sync)");
    if (backend == QStringLiteral("amf")) return QObject::tr("Hardware Encoding (AMD AMF)");
    if (backend == QStringLiteral("vaapi")) return QObject::tr("Hardware Encoding (VAAPI)");
    return QObject::tr("Software Encoding");
}

QWidget *checkListPage(const QStringList &labels, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    for (const QString &text : labels) {
        auto *box = new QCheckBox(text, page);
        layout->addWidget(box);
    }
    layout->addStretch();
    return page;
}
}

ProfileEditorDialog::ProfileEditorDialog(const QList<ProfileCategory> &categories,
                                         const QList<RenderProfile> &profiles,
                                         const FfmpegCapabilities &caps,
                                         const RenderProfile &profile,
                                         qint64 sourceDurationMs,
                                         const QString &sourceName,
                                         QWidget *parent,
                                         const QueueJob *sourceJob)
    : QDialog(parent), original_(profile), availableProfiles_(profiles), capabilities_(caps),
      sourceDurationMs_(sourceDurationMs), sourceName_(sourceName),
      sourceJob_(sourceJob ? *sourceJob : QueueJob{})
{
    setWindowTitle(tr("Export Settings"));
    resize(1560, 900);
    setMinimumSize(1120, 700);
    setModal(true);
    setWindowFlag(Qt::WindowMinimizeButtonHint, true);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    setSizeGripEnabled(true);
    setObjectName(QStringLiteral("FluxEncoderExportSettings"));

    metadataMode_ = original_.metadataMode;
    metadataPreservation_ = original_.metadataPreservation;
    metadataIncludeMarkers_ = original_.metadataIncludeMarkers;
    metadataTemplate_ = original_.metadataTemplate;
    metadataFields_ = original_.metadataFields;

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    auto *body = mainSplitter_;
    body->setChildrenCollapsible(false);
    body->setHandleWidth(3);
    root->addWidget(body);

    // Professional preview workspace: wide viewer on the left, narrow inspector on the right.
    auto *previewPane = new QWidget(body);
    previewPane->setObjectName(QStringLiteral("ExportPreviewWorkspace"));
    previewPane->setMinimumWidth(650);
    auto *previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(8, 6, 8, 7);
    previewLayout->setSpacing(5);

    previewModeTabs_ = new QTabWidget(previewPane);
    auto *previewModeTabs = previewModeTabs_;
    previewModeTabs->setObjectName(QStringLiteral("ExportPreviewModeTabs"));
    previewModeTabs->setDocumentMode(true);

    sourcePreview_ = new FluxEncoderZoomablePreview(previewModeTabs);
    sourcePreview_->setText(tr("Source Preview\n%1").arg(sourceName_.isEmpty() ? tr("No source assigned") : QFileInfo(sourceName_).fileName()));
    sourcePreview_->setObjectName(QStringLiteral("ExportPreview"));
    outputPreview_ = new FluxEncoderZoomablePreview(previewModeTabs);
    outputPreview_->setText(tr("Output Preview\nCompressed preview uses the selected export settings"));
    outputPreview_->setObjectName(QStringLiteral("ExportPreview"));

    auto *comparePage = new QWidget(previewModeTabs);
    comparePage->setObjectName(QStringLiteral("ExportComparePage"));
    auto *compareLayout = new QHBoxLayout(comparePage);
    compareLayout->setContentsMargins(0, 0, 0, 0);
    compareLayout->setSpacing(2);
    compareSourcePreview_ = new FluxEncoderZoomablePreview(comparePage);
    compareSourcePreview_->setObjectName(QStringLiteral("ExportPreview"));
    compareSourcePreview_->setText(tr("Source"));
    compareOutputPreview_ = new FluxEncoderZoomablePreview(comparePage);
    compareOutputPreview_->setObjectName(QStringLiteral("ExportPreview"));
    compareOutputPreview_->setText(tr("Compressed Output"));
    compareLayout->addWidget(compareSourcePreview_, 1);
    compareLayout->addWidget(compareOutputPreview_, 1);

    previewModeTabs->addTab(outputPreview_, tr("Output"));
    previewModeTabs->addTab(sourcePreview_, tr("Source"));
    previewModeTabs->addTab(comparePage, tr("Compare"));
    previewModeTabs->setCurrentWidget(outputPreview_);

    auto *previewToolbar = new QWidget(previewPane);
    previewToolbar->setObjectName(QStringLiteral("ExportPreviewToolbar"));
    auto *previewToolbarLayout = new QHBoxLayout(previewToolbar);
    previewToolbarLayout->setContentsMargins(0, 1, 0, 1);
    previewToolbarLayout->setSpacing(6);
    auto *scalingLabel = new QLabel(tr("Source Scaling:"), previewToolbar);
    auto *sourceScaling = new QComboBox(previewToolbar);
    sourceScaling->addItems({tr("Scale To Fit"), tr("100%"), tr("50%"), tr("25%")});
    sourceScaling->setMinimumWidth(135);
    auto *rotationLabel = new QLabel(tr("Source Rotation:"), previewToolbar);
    auto *sourceRotation = new QComboBox(previewToolbar);
    sourceRotation->addItem(tr("None (0°)"), 0);
    sourceRotation->addItem(tr("90° Clockwise"), 90);
    sourceRotation->addItem(tr("180°"), 180);
    sourceRotation->addItem(tr("90° Counter-clockwise"), 270);
    sourceRotation->setMinimumWidth(165);
    previewToolbarLayout->addWidget(scalingLabel);
    previewToolbarLayout->addWidget(sourceScaling);
    previewToolbarLayout->addSpacing(8);
    previewToolbarLayout->addWidget(rotationLabel);
    previewToolbarLayout->addWidget(sourceRotation);
    previewToolbarLayout->addStretch();
    previewLayout->addWidget(previewToolbar);
    previewLayout->addWidget(previewModeTabs, 1);

    outputPreviewDebounce_ = new QTimer(this);
    outputPreviewDebounce_->setSingleShot(true);
    outputPreviewDebounce_->setInterval(240);
    connect(outputPreviewDebounce_, &QTimer::timeout, this, &ProfileEditorDialog::startOutputPreview);

    if (!sourceName_.isEmpty() && QFileInfo::exists(sourceName_)) {
        const qint64 position = sourceDurationMs_ > 0
            ? (original_.trimStartMs > 0 ? original_.trimStartMs
                                         : qRound64(sourceDurationMs_ * 0.10))
            : 0;
        const QPixmap frame = renderSourceFrame(position);
        if (!frame.isNull()) {
            sourcePreview_->setPreviewPixmap(frame);
            compareSourcePreview_->setPreviewPixmap(frame);
            outputPreview_->setText(tr("Rendering compressed output preview…"));
        }
    }

    connect(sourceScaling, &QComboBox::currentIndexChanged, this, [this, sourceScaling](int) {
        const QString value = sourceScaling->currentText();
        const bool fit = value == tr("Scale To Fit");
        int zoom = 100;
        if (value == tr("50%")) zoom = 50;
        else if (value == tr("25%")) zoom = 25;
        sourcePreview_->setFitToView(fit);
        outputPreview_->setFitToView(fit);
        compareSourcePreview_->setFitToView(fit);
        compareOutputPreview_->setFitToView(fit);
        if (!fit) {
            sourcePreview_->setZoomPercent(zoom);
            outputPreview_->setZoomPercent(zoom);
            compareSourcePreview_->setZoomPercent(zoom);
            compareOutputPreview_->setZoomPercent(zoom);
        }
    });
    connect(sourceRotation, &QComboBox::currentIndexChanged, this, [this, sourceRotation](int) {
        const int degrees = sourceRotation->currentData().toInt();
        sourcePreview_->setRotationDegrees(degrees);
        outputPreview_->setRotationDegrees(degrees);
        compareSourcePreview_->setRotationDegrees(degrees);
        compareOutputPreview_->setRotationDegrees(degrees);
    });

    auto *timelinePanel = new QWidget(previewPane);
    timelinePanel->setObjectName(QStringLiteral("ExportTimelinePanel"));
    auto *timelineLayout = new QVBoxLayout(timelinePanel);
    timelineLayout->setContentsMargins(0, 2, 0, 0);
    timelineLayout->setSpacing(3);

    auto formatTimecode = [](qint64 ms) {
        const qint64 seconds = qMax<qint64>(0, ms / 1000);
        return QStringLiteral("%1:%2:%3:00")
            .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
            .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds % 60, 2, 10, QLatin1Char('0'));
    };

    trimStartMs_ = std::clamp<qint64>(original_.trimStartMs, 0, sourceDurationMs_ > 0 ? sourceDurationMs_ : 0);
    trimEndMs_ = original_.trimEndMs > trimStartMs_ ? original_.trimEndMs : sourceDurationMs_;
    const qint64 initialPreviewMs = sourceDurationMs_ > 0
        ? (trimStartMs_ > 0 ? trimStartMs_ : qRound64(sourceDurationMs_ * 0.10)) : 0;

    auto *timeRow = new QHBoxLayout();
    currentTimeLabel_ = new QLabel(formatTimecode(initialPreviewMs), timelinePanel);
    currentTimeLabel_->setObjectName(QStringLiteral("ExportCurrentTimecode"));
    auto *markIn = new QToolButton(timelinePanel);
    markIn->setText(QStringLiteral("{") );
    markIn->setToolTip(tr("Set In point"));
    auto *markOut = new QToolButton(timelinePanel);
    markOut->setText(QStringLiteral("}"));
    markOut->setToolTip(tr("Set Out point"));
    previewFit_ = new QToolButton(timelinePanel);
    previewFit_->setText(tr("Fit"));
    previewFit_->setCheckable(true);
    previewFit_->setChecked(true);
    previewFit_->setToolTip(tr("Fit the frame inside the preview"));
    previewZoom_ = new QSlider(Qt::Horizontal, timelinePanel);
    previewZoom_->setRange(10, 400);
    previewZoom_->setValue(100);
    previewZoom_->setFixedWidth(120);
    previewZoom_->setEnabled(false);
    auto *zoomValue = new QLabel(QStringLiteral("100%"), timelinePanel);
    zoomValue->setMinimumWidth(40);
    auto *durationLabel = new QLabel(formatTimecode(sourceDurationMs_), timelinePanel);
    durationLabel->setObjectName(QStringLiteral("ExportDurationTimecode"));
    timeRow->addWidget(currentTimeLabel_);
    timeRow->addWidget(markIn);
    timeRow->addWidget(markOut);
    timeRow->addStretch();
    timeRow->addWidget(previewFit_);
    timeRow->addWidget(previewZoom_);
    timeRow->addWidget(zoomValue);
    timeRow->addSpacing(12);
    timeRow->addWidget(durationLabel);
    timelineLayout->addLayout(timeRow);

    timeline_ = new FluxEncoderRangeSlider(Qt::Horizontal, timelinePanel);
    timeline_->setRange(0, 1000);
    timeline_->setEnabled(sourceDurationMs_ > 0);
    timeline_->setInOutValues(sourceDurationMs_ > 0 ? int(std::clamp(trimStartMs_ * 1000 / sourceDurationMs_, qint64(0), qint64(1000))) : 0,
                              sourceDurationMs_ > 0 ? int(std::clamp(trimEndMs_ * 1000 / sourceDurationMs_, qint64(0), qint64(1000))) : 1000);
    timeline_->setValue(sourceDurationMs_ > 0 ? int(std::clamp(initialPreviewMs * 1000 / sourceDurationMs_, qint64(0), qint64(1000))) : 0);
    timelineLayout->addWidget(timeline_);

    auto *rangeRow = new QHBoxLayout();
    auto *rangeLabel = new QLabel(tr("Source Range:"), timelinePanel);
    auto *rangeMode = new QComboBox(timelinePanel);
    rangeMode->addItems({tr("Entire Clip"), tr("Custom")});
    rangeMode->setCurrentIndex((trimStartMs_ > 0 || (sourceDurationMs_ > 0 && trimEndMs_ < sourceDurationMs_)) ? 1 : 0);
    inOutLabel_ = new QLabel(sourceDurationMs_ > 0
                                 ? tr("In %1  •  Out %2").arg(formatTimecode(trimStartMs_), formatTimecode(trimEndMs_))
                                 : QStringLiteral("--:--:--:--"), timelinePanel);
    rangeRow->addStretch();
    rangeRow->addWidget(rangeLabel);
    rangeRow->addWidget(rangeMode);
    rangeRow->addStretch();
    rangeRow->addWidget(inOutLabel_);
    timelineLayout->addLayout(rangeRow);
    previewLayout->addWidget(timelinePanel);

    connect(previewZoom_, &QSlider::valueChanged, this, [this, zoomValue](int value) {
        zoomValue->setText(QStringLiteral("%1%").arg(value));
        for (auto *preview : {sourcePreview_, outputPreview_, compareSourcePreview_, compareOutputPreview_})
            preview->setZoomPercent(value);
    });
    connect(previewFit_, &QToolButton::toggled, this, [this](bool checked) {
        previewZoom_->setEnabled(!checked);
        for (auto *preview : {sourcePreview_, outputPreview_, compareSourcePreview_, compareOutputPreview_})
            preview->setFitToView(checked);
    });
    connect(timeline_, &QSlider::valueChanged, this, [this, formatTimecode](int value) {
        const qint64 current = sourceDurationMs_ > 0 ? qRound64(sourceDurationMs_ * (value / 1000.0)) : 0;
        currentTimeLabel_->setText(formatTimecode(current));
    });
    connect(timeline_, &QSlider::sliderReleased, this, [this] {
        if (sourceDurationMs_ <= 0 || sourceName_.isEmpty()) return;
        const qint64 current = qRound64(sourceDurationMs_ * (timeline_->value() / 1000.0));
        const QPixmap frame = renderSourceFrame(current);
        if (!frame.isNull()) {
            sourcePreview_->setPreviewPixmap(frame);
            compareSourcePreview_->setPreviewPixmap(frame);
        }
        scheduleOutputPreview(current);
    });
    auto updateRange = [this, formatTimecode, rangeMode]() {
        inOutLabel_->setText(tr("In %1  •  Out %2").arg(formatTimecode(trimStartMs_), formatTimecode(trimEndMs_)));
        if (sourceDurationMs_ > 0)
            timeline_->setInOutValues(int(trimStartMs_ * 1000 / sourceDurationMs_), int(trimEndMs_ * 1000 / sourceDurationMs_));
        rangeMode->setCurrentIndex((trimStartMs_ > 0 || (sourceDurationMs_ > 0 && trimEndMs_ < sourceDurationMs_)) ? 1 : 0);
        updateEstimate();
    };
    connect(markIn, &QToolButton::clicked, this, [this, updateRange] {
        if (sourceDurationMs_ <= 0) return;
        const qint64 current = qRound64(sourceDurationMs_ * (timeline_->value() / 1000.0));
        trimStartMs_ = qMin(current, trimEndMs_ > 0 ? trimEndMs_ - 1 : sourceDurationMs_);
        updateRange();
    });
    connect(markOut, &QToolButton::clicked, this, [this, updateRange] {
        if (sourceDurationMs_ <= 0) return;
        const qint64 current = qRound64(sourceDurationMs_ * (timeline_->value() / 1000.0));
        trimEndMs_ = qMax(current, trimStartMs_ + 1);
        updateRange();
    });
    connect(rangeMode, &QComboBox::currentIndexChanged, this, [this, updateRange](int index) {
        if (index == 0 && sourceDurationMs_ > 0) {
            trimStartMs_ = 0;
            trimEndMs_ = sourceDurationMs_;
            updateRange();
        }
    });

    // Narrow export-settings inspector.
    auto *inspector = new QWidget(body);
    inspector->setObjectName(QStringLiteral("ExportSettingsInspector"));
    inspector->setMinimumWidth(330);
    inspector->setMaximumWidth(520);
    auto *inspectorLayout = new QVBoxLayout(inspector);
    inspectorLayout->setContentsMargins(0, 0, 0, 0);
    inspectorLayout->setSpacing(0);

    auto *headerScroll = new QScrollArea(inspector);
    headerScroll->setObjectName(QStringLiteral("ExportSettingsHeaderScroll"));
    headerScroll->setWidgetResizable(true);
    headerScroll->setFrameShape(QFrame::NoFrame);
    headerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    headerScroll->setMaximumHeight(285);
    auto *header = new QWidget(headerScroll);
    header->setObjectName(QStringLiteral("ExportSettingsHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(11, 9, 11, 8);
    headerLayout->setSpacing(6);

    auto *title = new QLabel(tr("Export Settings"), header);
    title->setObjectName(QStringLiteral("ExportInspectorTitle"));
    headerLayout->addWidget(title);

    auto *headerForm = new QFormLayout();
    headerForm->setContentsMargins(0, 0, 0, 0);
    headerForm->setHorizontalSpacing(8);
    headerForm->setVerticalSpacing(5);
    container_ = new QComboBox(header);
    container_->setEditable(false);
    for (const auto &format : fluxEncoderFormats())
        container_->addItem(format.label, format.key);
    container_->setCurrentIndex(qMax(0, container_->findData(formatKeyForProfile(profile))));
    container_->setToolTip(tr("Format selects the codec family and output container. The actual software or hardware encoder is selected automatically by Performance."));
    presetProfile_ = new QComboBox(header);
    presetProfile_->setObjectName(QStringLiteral("ExportPresetSelector"));
    presetProfile_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    presetProfile_->setToolTip(tr("Select a preset compatible with the current Format. Choose Custom to edit the settings manually."));
    name_ = new QLineEdit(profile.name, header);
    name_->setPlaceholderText(tr("Custom preset name"));
    name_->setVisible(false);
    description_ = new QLineEdit(profile.description, header);
    description_->setPlaceholderText(tr("Comments"));
    auto *outputName = new QLabel(sourceName_.isEmpty() ? tr("Assigned when added to the queue") : QFileInfo(sourceName_).fileName(), header);
    outputName->setObjectName(QStringLiteral("ExportOutputLink"));
    outputName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerForm->addRow(tr("Format:"), container_);
    headerForm->addRow(tr("Preset:"), presetProfile_);
    headerForm->addRow(tr("Custom Name:"), name_);
    headerForm->addRow(tr("Comments:"), description_);
    headerForm->addRow(tr("Output Name:"), outputName);
    headerLayout->addLayout(headerForm);

    auto *exportRow = new QHBoxLayout();
    exportVideo_ = new FluxSwitch(tr("Export Video"), header);
    exportVideo_->setChecked(!profile.videoEncoder.isEmpty());
    exportAudio_ = new FluxSwitch(tr("Export Audio"), header);
    exportAudio_->setChecked(!profile.audioEncoder.isEmpty());
    exportRow->addWidget(exportVideo_);
    exportRow->addWidget(exportAudio_);
    exportRow->addStretch();
    headerLayout->addLayout(exportRow);

    summaryLabel_ = new QLabel(header);
    summaryLabel_->setObjectName(QStringLiteral("ExportSummary"));
    summaryLabel_->setWordWrap(true);
    headerLayout->addWidget(summaryLabel_);

    category_ = new QComboBox(header);
    for (const auto &category : categories)
        category_->addItem(category.name, category.uuid);
    category_->setCurrentIndex(std::max(0, category_->findData(profile.categoryUuid)));
    auto *categoryRow = new QFormLayout();
    categoryRow->addRow(tr("Preset Group:"), category_);
    headerLayout->addLayout(categoryRow);
    headerScroll->setWidget(header);
    inspectorLayout->addWidget(headerScroll);

    settingsTabs_ = new QTabWidget(inspector);
    auto *tabs = settingsTabs_;
    tabs->setObjectName(QStringLiteral("ExportSettingsTabs"));
    tabs->setDocumentMode(true);
    tabs->addTab(makeVideoPage(), tr("Video"));
    tabs->addTab(makeAudioPage(), tr("Audio"));
    tabs->addTab(makeEffectsPage(), tr("Effects"));
    tabs->addTab(makeMuxerPage(), tr("Multiplexer"));
    tabs->addTab(makeCaptionsPage(), tr("Captions"));
    tabs->addTab(makePublishPage(), tr("Publish"));
    inspectorLayout->addWidget(tabs, 1);

    rebuildPresetList(container_->currentData().toString(), original_.uuid);
    connect(container_, &QComboBox::currentIndexChanged, this, [this](int) {
        rebuildPresetList(container_->currentData().toString());
        updateControlState();
        updateEstimate();
    });
    connect(presetProfile_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!presetProfile_) return;
        const QString uuid = presetProfile_->itemData(index).toString();
        const bool custom = uuid.isEmpty();
        name_->setVisible(custom);
        if (custom) {
            if (name_->text().trimmed().isEmpty()) name_->setText(tr("Custom Preset"));
            return;
        }
        for (const auto &candidate : availableProfiles_) {
            if (candidate.uuid == uuid) {
                applyPresetToControls(candidate);
                break;
            }
        }
    });

    auto *footer = new QWidget(inspector);
    footer->setObjectName(QStringLiteral("ExportSettingsFooter"));
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(10, 7, 10, 8);
    footerLayout->setSpacing(5);
    auto *optionsRow = new QHBoxLayout();
    auto *maxQuality = new QCheckBox(tr("Use Maximum Render Quality"), footer);
    maxQuality->setToolTip(tr("Uses the highest quality scaling path. This can increase render time."));
    auto *usePreviews = new QCheckBox(tr("Use Previews"), footer);
    usePreviews->setEnabled(false);
    usePreviews->setToolTip(tr("Preview cache export is not available for this source."));
    optionsRow->addWidget(maxQuality);
    optionsRow->addWidget(usePreviews);
    optionsRow->addStretch();
    footerLayout->addLayout(optionsRow);

    estimateLabel_ = new QLabel(footer);
    estimateLabel_->setObjectName(QStringLiteral("EstimatedFileSize"));
    estimateLabel_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    footerLayout->addWidget(estimateLabel_);

    auto *actionRow = new QHBoxLayout();
    auto *metadataButton = new QPushButton(tr("Metadata…"), footer);
    metadataButton->setToolTip(tr("Configure source preservation and embedded output metadata."));
    connect(metadataButton, &QPushButton::clicked, this, [this] {
        RenderProfile working = original_;
        working.metadataMode = metadataMode_;
        working.metadataPreservation = metadataPreservation_;
        working.metadataIncludeMarkers = metadataIncludeMarkers_;
        working.metadataTemplate = metadataTemplate_;
        working.metadataFields = metadataFields_;
        MetadataExportDialog dialog(working, this);
        if (dialog.exec() != QDialog::Accepted) return;
        metadataMode_ = dialog.metadataMode();
        metadataPreservation_ = dialog.preservationRule();
        metadataIncludeMarkers_ = dialog.includeMarkers();
        metadataTemplate_ = dialog.exportTemplate();
        metadataFields_ = dialog.metadataFields();
        if (writeMetadata_) writeMetadata_->setChecked(metadataMode_ != QStringLiteral("none"));
        updateEstimate();
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, footer);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    actionRow->addWidget(metadataButton);
    actionRow->addStretch();
    actionRow->addWidget(buttons);
    footerLayout->addLayout(actionRow);
    inspectorLayout->addWidget(footer);

    body->setStretchFactor(0, 5);
    body->setStretchFactor(1, 1);
    body->setSizes({1240, 360});

    const QList<QObject *> watched{container_, backend_, encoder_, width_, height_, fps_, quality_, videoBitrate_, maxBitrate_, bitrateUnit_,
                                   audioEncoder_, audioBitrate_, exportVideo_, exportAudio_, rateControl_, videoLimiter_, burnTimecode_,
                                   timecodePosition_, timecodeFontSize_, timecodeOpacity_, timecodeBackgroundOpacity_};
    for (QObject *object : watched) {
        if (auto *combo = qobject_cast<QComboBox *>(object))
            connect(combo, &QComboBox::currentTextChanged, this, &ProfileEditorDialog::updateEstimate);
        else if (auto *spin = qobject_cast<QSpinBox *>(object))
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, &ProfileEditorDialog::updateEstimate);
        else if (auto *doubleSpin = qobject_cast<QDoubleSpinBox *>(object))
            connect(doubleSpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &ProfileEditorDialog::updateEstimate);
        else if (auto *check = qobject_cast<QCheckBox *>(object))
            connect(check, &QCheckBox::toggled, this, &ProfileEditorDialog::updateEstimate);
    }
    connect(exportVideo_, &QCheckBox::toggled, this, &ProfileEditorDialog::updateControlState);
    connect(exportAudio_, &QCheckBox::toggled, this, &ProfileEditorDialog::updateControlState);
    connect(rateControl_, &QComboBox::currentTextChanged, this, &ProfileEditorDialog::updateControlState);
    if (burnTimecode_) connect(burnTimecode_, &QCheckBox::toggled, this, &ProfileEditorDialog::updateControlState);
    if (timecodeStart_) connect(timecodeStart_, &QLineEdit::textChanged, this, &ProfileEditorDialog::updateEstimate);
    updateControlState();
    updateEstimate();

    QSettings session(QStringLiteral("Flux"), QStringLiteral("FluxEncoder"));
    const QByteArray geometry = session.value(QStringLiteral("exportSettings/geometry")).toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    const QByteArray splitterState = session.value(QStringLiteral("exportSettings/splitter")).toByteArray();
    if (!splitterState.isEmpty() && mainSplitter_) mainSplitter_->restoreState(splitterState);
    if (previewModeTabs_) previewModeTabs_->setCurrentIndex(qBound(0, session.value(QStringLiteral("exportSettings/previewTab"), 0).toInt(), previewModeTabs_->count() - 1));
    if (settingsTabs_) settingsTabs_->setCurrentIndex(qBound(0, session.value(QStringLiteral("exportSettings/settingsTab"), 0).toInt(), settingsTabs_->count() - 1));
    if (previewZoom_) previewZoom_->setValue(qBound(previewZoom_->minimum(), session.value(QStringLiteral("exportSettings/zoom"), 100).toInt(), previewZoom_->maximum()));
    if (previewFit_) previewFit_->setChecked(session.value(QStringLiteral("exportSettings/fit"), true).toBool());
    if (session.value(QStringLiteral("exportSettings/maximized"), false).toBool())
        QTimer::singleShot(0, this, [this] { showMaximized(); });

    scheduleOutputPreview(initialPreviewMs);
}
ProfileEditorDialog::~ProfileEditorDialog()
{
    QSettings session(QStringLiteral("Flux"), QStringLiteral("FluxEncoder"));
    session.setValue(QStringLiteral("exportSettings/geometry"), saveGeometry());
    session.setValue(QStringLiteral("exportSettings/maximized"), isMaximized());
    if (mainSplitter_) session.setValue(QStringLiteral("exportSettings/splitter"), mainSplitter_->saveState());
    if (previewModeTabs_) session.setValue(QStringLiteral("exportSettings/previewTab"), previewModeTabs_->currentIndex());
    if (settingsTabs_) session.setValue(QStringLiteral("exportSettings/settingsTab"), settingsTabs_->currentIndex());
    if (previewZoom_) session.setValue(QStringLiteral("exportSettings/zoom"), previewZoom_->value());
    if (previewFit_) session.setValue(QStringLiteral("exportSettings/fit"), previewFit_->isChecked());

    if (outputPreviewEncode_ && outputPreviewEncode_->state() != QProcess::NotRunning) {
        outputPreviewEncode_->kill();
        outputPreviewEncode_->waitForFinished(1000);
    }
    if (outputPreviewDecode_ && outputPreviewDecode_->state() != QProcess::NotRunning) {
        outputPreviewDecode_->kill();
        outputPreviewDecode_->waitForFinished(1000);
    }
    delete outputPreviewTempDir_;
    outputPreviewTempDir_ = nullptr;
}

QWidget *ProfileEditorDialog::makeSection(const QString &title, QWidget *content, bool expanded)
{
    auto *panel = new FluxCollapsiblePanel(title, content);
    panel->setOrderPersistenceEnabled(false);
    panel->setExpanded(expanded);
    return panel;
}

QWidget *ProfileEditorDialog::makeVideoPage()
{
    auto *page = new QScrollArea(this);
    page->setWidgetResizable(true);
    page->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(page);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(6);

    auto *basic = new QWidget(content);
    auto *form = new QFormLayout(basic);
    encoder_ = new QComboBox(basic);
    encoder_->setVisible(false); // Internal resolved FFmpeg encoder; Format + Performance own this choice.
    width_ = new QSpinBox(basic); width_->setRange(0, 16384); width_->setSpecialValueText(tr("Match Source")); width_->setValue(original_.width);
    height_ = new QSpinBox(basic); height_->setRange(0, 16384); height_->setSpecialValueText(tr("Match Source")); height_->setValue(original_.height);
    fps_ = new QDoubleSpinBox(basic); fps_->setRange(0, 240); fps_->setDecimals(3); fps_->setSpecialValueText(tr("Match Source")); fps_->setValue(original_.frameRate);
    fieldOrder_ = new QComboBox(basic);
    fieldOrder_->addItem(tr("Progressive"), QStringLiteral("progressive"));
    fieldOrder_->addItem(tr("Upper First"), QStringLiteral("upper"));
    fieldOrder_->addItem(tr("Lower First"), QStringLiteral("lower"));
    fieldOrder_->setCurrentIndex(qMax(0, fieldOrder_->findData(original_.fieldOrder)));
    aspect_ = new QComboBox(basic);
    aspect_->addItem(tr("Square Pixels (1.0)"), QStringLiteral("1:1"));
    aspect_->addItem(tr("D1/DV PAL (16:15)"), QStringLiteral("16:15"));
    aspect_->addItem(tr("Anamorphic 2:1"), QStringLiteral("2:1"));
    aspect_->setCurrentIndex(qMax(0, aspect_->findData(original_.aspectRatio)));

    auto *resolution = new QWidget(basic);
    resolution->setObjectName(QStringLiteral("FluxEncoderResolutionControl"));
    auto *resolutionGrid = new QGridLayout(resolution);
    resolutionGrid->setContentsMargins(0, 0, 0, 0);
    resolutionGrid->setHorizontalSpacing(7);
    resolutionGrid->setVerticalSpacing(5);
    auto *matchSource = new QPushButton(tr("Match Source"), resolution);
    matchSource->setObjectName(QStringLiteral("FluxEncoderMatchSourceButton"));
    widthOverride_ = new QCheckBox(resolution);
    heightOverride_ = new QCheckBox(resolution);
    widthOverride_->setToolTip(tr("Override source width"));
    heightOverride_->setToolTip(tr("Override source height"));
    widthOverride_->setChecked(original_.width > 0);
    heightOverride_->setChecked(original_.height > 0);
    auto *linkDimensions = new QToolButton(resolution);
    linkDimensions->setObjectName(QStringLiteral("FluxEncoderDimensionLink"));
    linkDimensions->setText(QStringLiteral("↔"));
    linkDimensions->setCheckable(true);
    linkDimensions->setChecked(true);
    linkDimensions->setToolTip(tr("Link width and height proportions"));
    resolutionGrid->addWidget(matchSource, 0, 2, 1, 2, Qt::AlignRight);
    resolutionGrid->addWidget(new QLabel(tr("Width:"), resolution), 1, 0, Qt::AlignRight);
    resolutionGrid->addWidget(width_, 1, 1);
    resolutionGrid->addWidget(linkDimensions, 1, 2, 2, 1, Qt::AlignCenter);
    resolutionGrid->addWidget(widthOverride_, 1, 3, Qt::AlignCenter);
    resolutionGrid->addWidget(new QLabel(tr("Height:"), resolution), 2, 0, Qt::AlignRight);
    resolutionGrid->addWidget(height_, 2, 1);
    resolutionGrid->addWidget(heightOverride_, 2, 3, Qt::AlignCenter);
    resolutionGrid->setColumnStretch(1, 1);
    form->addRow(resolution);

    const double initialAspect = original_.width > 0 && original_.height > 0
        ? double(original_.width) / double(original_.height) : 16.0 / 9.0;
    width_->setEnabled(widthOverride_->isChecked());
    height_->setEnabled(heightOverride_->isChecked());
    if (!widthOverride_->isChecked()) width_->setValue(0);
    if (!heightOverride_->isChecked()) height_->setValue(0);
    auto setOverride = [](QCheckBox *box, QSpinBox *field, int fallback) {
        QObject::connect(box, &QCheckBox::toggled, field, [field, fallback](bool enabled) {
            field->setEnabled(enabled);
            if (enabled && field->value() == 0) field->setValue(fallback);
            if (!enabled) field->setValue(0);
        });
    };
    setOverride(widthOverride_, width_, 1920);
    setOverride(heightOverride_, height_, 1080);
    connect(matchSource, &QPushButton::clicked, this, &ProfileEditorDialog::matchSource);
    connect(width_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, linkDimensions, initialAspect](int value) {
        if (!linkDimensions->isChecked() || !heightOverride_->isChecked() || value <= 0) return;
        const QSignalBlocker blocker(height_);
        height_->setValue(qMax(1, qRound(value / initialAspect)));
    });
    connect(height_, qOverload<int>(&QSpinBox::valueChanged), this,
            [this, linkDimensions, initialAspect](int value) {
        if (!linkDimensions->isChecked() || !widthOverride_->isChecked() || value <= 0) return;
        const QSignalBlocker blocker(width_);
        width_->setValue(qMax(1, qRound(value * initialAspect)));
    });

    form->addRow(tr("Frame Rate"), fps_);
    form->addRow(tr("Field Order"), fieldOrder_);
    form->addRow(tr("Aspect"), aspect_);
    layout->addWidget(makeSection(tr("Basic Video Settings"), basic, true));

    auto *encoding = new QWidget(content);
    auto *encodingForm = new QFormLayout(encoding);
    backend_ = new QComboBox(encoding);
    backend_->addItem(tr("Software Encoding"), QStringLiteral("software"));
    backend_->addItem(tr("Hardware Encoding"), QStringLiteral("hardware"));
    backend_->setToolTip(tr("Flux Encoder automatically chooses the compatible FFmpeg encoder for the selected Format."));
    backend_->setCurrentIndex(original_.hardwareBackend == QStringLiteral("software") ? 0 : 1);
    profileLevel_ = new QComboBox(encoding);
    profileLevel_->addItem(tr("Auto"), QString());
    profileLevel_->addItem(QStringLiteral("Baseline"), QStringLiteral("baseline"));
    profileLevel_->addItem(QStringLiteral("Main"), QStringLiteral("main"));
    profileLevel_->addItem(QStringLiteral("High"), QStringLiteral("high"));
    profileLevel_->addItem(QStringLiteral("High 10"), QStringLiteral("high10"));
    profileLevel_->setCurrentIndex(qMax(0, profileLevel_->findData(original_.codecProfile)));
    codecLevel_ = new QComboBox(encoding);
    codecLevel_->addItem(tr("Auto"), QString());
    for (const QString &level : {QStringLiteral("3.1"), QStringLiteral("4.0"), QStringLiteral("4.1"), QStringLiteral("4.2"),
                                 QStringLiteral("5.0"), QStringLiteral("5.1"), QStringLiteral("5.2"), QStringLiteral("6.0"),
                                 QStringLiteral("6.1"), QStringLiteral("6.2")})
        codecLevel_->addItem(level, level);
    codecLevel_->setCurrentIndex(qMax(0, codecLevel_->findData(original_.codecLevel)));
    preset_ = new QComboBox(encoding);
    fallback_ = new FluxSwitch(tr("Fall back to Software Encoding if hardware is unavailable"), encoding);
    fallback_->setChecked(original_.allowSoftwareFallback);
    encodingForm->addRow(tr("Performance"), backend_);
    encodingForm->addRow(tr("Profile"), profileLevel_);
    encodingForm->addRow(tr("Level"), codecLevel_);
    encodingForm->addRow(tr("Quality / Speed"), preset_);
    encodingForm->addRow(QString(), fallback_);
    auto refreshEncodingControls = [this]() {
        const QString key = container_->currentData().toString();
        const auto &spec = formatSpec(key);
        const QString preferred = original_.hardwareBackend;
        const QString hardwareBackend = spec.hardwareCapable
            ? bestHardwareBackend(key, preferred, capabilities_) : QString();
        const bool hardwareAvailable = !hardwareBackend.isEmpty();
        if (auto *model = qobject_cast<QStandardItemModel *>(backend_->model())) {
            if (QStandardItem *item = model->item(1))
                item->setEnabled(spec.video && spec.hardwareCapable && hardwareAvailable);
        }
        if (!spec.video || !spec.hardwareCapable || (!hardwareAvailable && backend_->currentData().toString() == QStringLiteral("hardware")))
            backend_->setCurrentIndex(0);

        QString actualBackend = QStringLiteral("software");
        QString actualEncoder = spec.softwareEncoder;
        if (backend_->currentData().toString() == QStringLiteral("hardware") && hardwareAvailable) {
            actualBackend = hardwareBackend;
            actualEncoder = hardwareEncoderFor(key, actualBackend);
        }
        encoder_->clear();
        encoder_->addItem(actualEncoder);
        encoder_->setCurrentText(actualEncoder);
        backend_->setItemText(0, tr("Software Encoding"));
        backend_->setItemText(1, hardwareAvailable ? performanceLabel(hardwareBackend) : tr("Hardware Encoding (Unavailable)"));

        preset_->clear();
        if (!spec.video) {
            preset_->addItem(tr("Not Applicable"), QString());
        } else if (actualBackend == QStringLiteral("nvenc")) {
            preset_->addItem(tr("Maximum Speed"), QStringLiteral("p1"));
            preset_->addItem(tr("Balanced"), QStringLiteral("p4"));
            preset_->addItem(tr("Maximum Quality"), QStringLiteral("p7"));
        } else if (actualBackend == QStringLiteral("amf")) {
            preset_->addItem(tr("Speed"), QStringLiteral("speed"));
            preset_->addItem(tr("Balanced"), QStringLiteral("balanced"));
            preset_->addItem(tr("Quality"), QStringLiteral("quality"));
        } else if (actualBackend == QStringLiteral("qsv")) {
            preset_->addItem(tr("Speed"), QStringLiteral("veryfast"));
            preset_->addItem(tr("Balanced"), QStringLiteral("medium"));
            preset_->addItem(tr("Quality"), QStringLiteral("veryslow"));
        } else if (actualBackend == QStringLiteral("vaapi")) {
            preset_->addItem(tr("Automatic"), QString());
        } else if (key == QStringLiteral("h264") || key == QStringLiteral("hevc")) {
            preset_->addItem(tr("Maximum Speed"), QStringLiteral("ultrafast"));
            preset_->addItem(tr("Fast"), QStringLiteral("fast"));
            preset_->addItem(tr("Balanced"), QStringLiteral("medium"));
            preset_->addItem(tr("High Quality"), QStringLiteral("slow"));
            preset_->addItem(tr("Maximum Quality"), QStringLiteral("veryslow"));
        } else if (key == QStringLiteral("av1")) {
            preset_->addItem(tr("Faster"), QStringLiteral("8"));
            preset_->addItem(tr("Balanced"), QStringLiteral("6"));
            preset_->addItem(tr("Higher Quality"), QStringLiteral("4"));
        } else {
            preset_->addItem(tr("Automatic"), QString());
        }
        int presetIndex = preset_->findData(original_.encoderPreset);
        if (presetIndex < 0) presetIndex = qMin(2, preset_->count() - 1);
        preset_->setCurrentIndex(qMax(0, presetIndex));

        exportVideo_->setEnabled(spec.video);
        exportVideo_->setChecked(spec.video);
        if (!spec.audioEncoder.isEmpty() && audioEncoder_) {
            int audioIndex = audioEncoder_->findText(spec.audioEncoder);
            if (audioIndex < 0) {
                audioEncoder_->addItem(spec.audioEncoder);
                audioIndex = audioEncoder_->findText(spec.audioEncoder);
            }
            audioEncoder_->setCurrentIndex(audioIndex);
        }
        if (!spec.video) exportAudio_->setChecked(true);
        profileLevel_->setEnabled(spec.video && (key == QStringLiteral("h264") || key == QStringLiteral("hevc")));
        codecLevel_->setEnabled(profileLevel_->isEnabled());
        fallback_->setEnabled(spec.video && actualBackend != QStringLiteral("software"));
        updateControlState();
    };
    connect(container_, &QComboBox::currentIndexChanged, this, [refreshEncodingControls](int) { refreshEncodingControls(); });
    connect(backend_, &QComboBox::currentIndexChanged, this, [refreshEncodingControls](int) { refreshEncodingControls(); });
    refreshEncodingControls();

    layout->addWidget(makeSection(tr("Encoding Settings"), encoding, true));

    auto *bitrate = new QWidget(content);
    auto *bitrateForm = new QFormLayout(bitrate);
    rateControl_ = new QComboBox(bitrate);
    rateControl_->addItems({tr("Constant Quality"), tr("CBR"), tr("VBR, 1 pass"), tr("VBR, 2 pass")});
    rateControl_->setCurrentIndex(original_.videoBitrateKbps > 0 ? 2 : 0);
    bitrateUnit_ = new QComboBox(bitrate);
    bitrateUnit_->addItem(QStringLiteral("Mbps"), 1000.0);
    bitrateUnit_->addItem(QStringLiteral("kbps"), 1.0);
    bitrateUnit_->setCurrentIndex(0);

    videoBitrate_ = new QDoubleSpinBox(bitrate);
    maxBitrate_ = new QDoubleSpinBox(bitrate);
    auto configureBitrateField = [](QDoubleSpinBox *field, double scale) {
        const bool mbps = scale >= 1000.0;
        field->setDecimals(mbps ? 3 : 0);
        field->setRange(0.0, mbps ? 500.0 : 500000.0);
        field->setSingleStep(mbps ? 0.1 : 100.0);
        field->setSuffix(mbps ? QStringLiteral(" Mbps") : QStringLiteral(" kbps"));
    };
    configureBitrateField(videoBitrate_, 1000.0);
    configureBitrateField(maxBitrate_, 1000.0);
    videoBitrate_->setValue(original_.videoBitrateKbps > 0 ? original_.videoBitrateKbps / 1000.0 : 10.0);
    const int storedMaxKbps = original_.maxVideoBitrateKbps > 0
                                  ? original_.maxVideoBitrateKbps
                                  : (original_.videoBitrateKbps > 0
                                         ? int(std::ceil(original_.videoBitrateKbps * 1.35))
                                         : 12000);
    maxBitrate_->setValue(storedMaxKbps / 1000.0);

    connect(bitrateUnit_, &QComboBox::currentIndexChanged, this,
            [this, configureBitrateField](int) {
        const double oldScale = bitrateUnit_->property("previousScale").toDouble();
        const double newScale = bitrateUnit_->currentData().toDouble();
        const double effectiveOldScale = oldScale > 0.0 ? oldScale : 1000.0;
        const double targetKbps = videoBitrate_->value() * effectiveOldScale;
        const double maximumKbps = maxBitrate_->value() * effectiveOldScale;
        configureBitrateField(videoBitrate_, newScale);
        configureBitrateField(maxBitrate_, newScale);
        videoBitrate_->setValue(targetKbps / newScale);
        maxBitrate_->setValue(maximumKbps / newScale);
        bitrateUnit_->setProperty("previousScale", newScale);
        updateEstimate();
    });
    bitrateUnit_->setProperty("previousScale", 1000.0);

    quality_ = new QSpinBox(bitrate); quality_->setRange(-1, 63); quality_->setSpecialValueText(tr("Automatic")); quality_->setValue(original_.quality);
    bitrateForm->addRow(tr("Bitrate Encoding"), rateControl_);
    bitrateForm->addRow(tr("Bitrate Units"), bitrateUnit_);
    bitrateForm->addRow(tr("Target Bitrate"), videoBitrate_);
    bitrateForm->addRow(tr("Maximum Bitrate"), maxBitrate_);
    bitrateForm->addRow(tr("Quality / CRF / CQ"), quality_);
    layout->addWidget(makeSection(tr("Bitrate Settings"), bitrate, true));

    auto *advanced = new QWidget(content);
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    extraArgs_ = new QPlainTextEdit(advanced);
    extraArgs_->setMaximumHeight(110);
    extraArgs_->setPlaceholderText(tr("Additional FFmpeg video arguments, one argument per line"));
    extraArgs_->setPlainText(original_.videoArguments.join('\n'));
    advancedLayout->addWidget(smallLabel(tr("Advanced arguments override normal encoder behavior. Invalid combinations are validated before queue execution."), advanced));
    advancedLayout->addWidget(extraArgs_);
    layout->addWidget(makeSection(tr("Advanced Settings"), advanced, false));
    layout->addStretch();
    page->setWidget(content);
    return page;
}

QWidget *ProfileEditorDialog::makeAudioPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *basic = new QWidget(page);
    auto *form = new QFormLayout(basic);
    audioEncoder_ = new QComboBox(basic);
    audioEncoder_->setEditable(true);
    audioEncoder_->addItems({QStringLiteral("aac"), QStringLiteral("libopus"), QStringLiteral("pcm_s16le"), QStringLiteral("pcm_s24le"), QStringLiteral("ac3")});
    audioEncoder_->setCurrentText(original_.audioEncoder);
    sampleRate_ = new QComboBox(basic);
    sampleRate_->addItem(tr("Automatic"), 0);
    sampleRate_->addItem(QStringLiteral("44100 Hz"), 44100);
    sampleRate_->addItem(QStringLiteral("48000 Hz"), 48000);
    sampleRate_->addItem(QStringLiteral("96000 Hz"), 96000);
    sampleRate_->setCurrentIndex(qMax(0, sampleRate_->findData(original_.sampleRate)));
    channels_ = new QComboBox(basic);
    channels_->addItem(tr("Automatic"), 0);
    channels_->addItem(tr("Mono"), 1);
    channels_->addItem(tr("Stereo"), 2);
    channels_->addItem(QStringLiteral("5.1"), 6);
    channels_->addItem(QStringLiteral("7.1"), 8);
    channels_->setCurrentIndex(qMax(0, channels_->findData(original_.audioChannels)));
    audioBitrate_ = new QSpinBox(basic); audioBitrate_->setRange(0, 1536); audioBitrate_->setSuffix(tr(" kbps")); audioBitrate_->setValue(original_.audioBitrateKbps);
    form->addRow(tr("Audio Codec"), audioEncoder_);
    form->addRow(tr("Sample Rate"), sampleRate_);
    form->addRow(tr("Channels"), channels_);
    form->addRow(tr("Bitrate"), audioBitrate_);
    layout->addWidget(makeSection(tr("Basic Audio Settings"), basic, true));
    auto *loudness = new QWidget(page);
    auto *loudnessLayout = new QVBoxLayout(loudness);
    normalizeLoudness_ = new QCheckBox(tr("Normalize loudness to -16 LUFS"), loudness);
    normalizeLoudness_->setChecked(original_.normalizeLoudness);
    truePeakLimit_ = new QCheckBox(tr("Limit true peak to -1.5 dBTP"), loudness);
    truePeakLimit_->setChecked(original_.truePeakLimit);
    truePeakLimit_->setEnabled(normalizeLoudness_->isChecked());
    connect(normalizeLoudness_, &QCheckBox::toggled, truePeakLimit_, &QWidget::setEnabled);
    loudnessLayout->addWidget(normalizeLoudness_);
    loudnessLayout->addWidget(truePeakLimit_);
    layout->addWidget(makeSection(tr("Loudness Normalization"), loudness, false));
    layout->addStretch();
    return page;
}

QWidget *ProfileEditorDialog::makeEffectsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto *imageProcessing = new QWidget(page);
    auto *imageLayout = new QVBoxLayout(imageProcessing);
    videoLimiter_ = new QCheckBox(tr("Video limiter (clamp output levels)"), imageProcessing);
    videoLimiter_->setChecked(original_.videoLimiter);
    imageLayout->addWidget(videoLimiter_);
    layout->addWidget(makeSection(tr("Image Processing"), imageProcessing, true));

    auto *timecode = new QWidget(page);
    auto *timecodeLayout = new QVBoxLayout(timecode);
    timecodeLayout->setContentsMargins(0, 0, 0, 0);
    timecodeLayout->setSpacing(8);

    burnTimecode_ = new QCheckBox(tr("Timecode Overlay"), timecode);
    burnTimecode_->setChecked(original_.burnTimecode);
    burnTimecode_->setToolTip(tr("Permanently burns a timecode display into the exported video."));
    timecodeLayout->addWidget(burnTimecode_);

    auto *formHost = new QWidget(timecode);
    auto *form = new QFormLayout(formHost);
    form->setContentsMargins(22, 0, 0, 0);

    auto *source = new QComboBox(formHost);
    source->addItem(tr("Generate Timecode"), QStringLiteral("generated"));
    source->setEnabled(false);
    source->setToolTip(tr("The current implementation generates a deterministic timecode from the selected starting value."));

    timecodeStart_ = new QLineEdit(original_.timecodeStart, formHost);
    timecodeStart_->setInputMask(QStringLiteral("00:00:00:00;_"));
    timecodeStart_->setPlaceholderText(QStringLiteral("00:00:00:00"));
    timecodeStart_->setToolTip(tr("Starting timecode in HH:MM:SS:FF format."));

    timecodePosition_ = new QComboBox(formHost);
    timecodePosition_->addItem(tr("Top Left"), QStringLiteral("top-left"));
    timecodePosition_->addItem(tr("Top Center"), QStringLiteral("top-center"));
    timecodePosition_->addItem(tr("Top Right"), QStringLiteral("top-right"));
    timecodePosition_->addItem(tr("Center Left"), QStringLiteral("middle-left"));
    timecodePosition_->addItem(tr("Center"), QStringLiteral("center"));
    timecodePosition_->addItem(tr("Center Right"), QStringLiteral("middle-right"));
    timecodePosition_->addItem(tr("Bottom Left"), QStringLiteral("bottom-left"));
    timecodePosition_->addItem(tr("Bottom Center"), QStringLiteral("bottom-center"));
    timecodePosition_->addItem(tr("Bottom Right"), QStringLiteral("bottom-right"));
    timecodePosition_->setCurrentIndex(qMax(0, timecodePosition_->findData(original_.timecodePosition)));

    timecodeFontSize_ = new QSpinBox(formHost);
    timecodeFontSize_->setRange(12, 240);
    timecodeFontSize_->setSuffix(tr(" px"));
    timecodeFontSize_->setValue(qBound(12, original_.timecodeFontSize, 240));

    timecodeOpacity_ = new QSpinBox(formHost);
    timecodeOpacity_->setRange(0, 100);
    timecodeOpacity_->setSuffix(QStringLiteral("%"));
    timecodeOpacity_->setValue(qBound(0, original_.timecodeOpacityPercent, 100));

    timecodeBackgroundOpacity_ = new QSpinBox(formHost);
    timecodeBackgroundOpacity_->setRange(0, 100);
    timecodeBackgroundOpacity_->setSuffix(QStringLiteral("%"));
    timecodeBackgroundOpacity_->setValue(qBound(0, original_.timecodeBackgroundOpacityPercent, 100));

    form->addRow(tr("Timecode Source"), source);
    form->addRow(tr("Starting Timecode"), timecodeStart_);
    form->addRow(tr("Position"), timecodePosition_);
    form->addRow(tr("Size"), timecodeFontSize_);
    form->addRow(tr("Opacity"), timecodeOpacity_);
    form->addRow(tr("Background Opacity"), timecodeBackgroundOpacity_);
    timecodeLayout->addWidget(formHost);
    timecodeLayout->addWidget(smallLabel(tr("The Timecode Overlay is hard coded into every exported frame and is visible in Output Preview."), timecode));

    auto updateTimecodeState = [this, formHost] {
        const bool enabled = burnTimecode_ && burnTimecode_->isChecked() && exportVideo_->isChecked();
        formHost->setEnabled(enabled);
    };
    connect(burnTimecode_, &QCheckBox::toggled, this, updateTimecodeState);
    connect(exportVideo_, &QCheckBox::toggled, this, updateTimecodeState);
    updateTimecodeState();

    layout->addWidget(makeSection(tr("Timecode Overlay"), timecode, true));
    layout->addWidget(smallLabel(tr("Effects are applied in the displayed order before compression."), page));
    layout->addStretch();
    return page;
}

QWidget *ProfileEditorDialog::makeMuxerPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    writeMetadata_ = new QCheckBox(tr("Write source metadata"), page);
    fastStart_ = new QCheckBox(tr("Fast Start / Web Optimized"), page);
    preserveTimecode_ = new QCheckBox(tr("Preserve source timecode"), page);
    writeMetadata_->setChecked(original_.writeMetadata);
    fastStart_->setChecked(original_.fastStart);
    preserveTimecode_->setChecked(original_.preserveTimecode);
    layout->addWidget(writeMetadata_); layout->addWidget(fastStart_); layout->addWidget(preserveTimecode_);
    layout->addStretch();
    return page;
}

QWidget *ProfileEditorDialog::makeCaptionsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    burnCaptions_ = new QCheckBox(tr("Burn the first subtitle stream into video"), page);
    burnCaptions_->setChecked(original_.burnCaptions);
    layout->addWidget(burnCaptions_);
    layout->addWidget(smallLabel(tr("Uses FFmpeg's subtitles filter. The selected FFmpeg build must include libass."), page));
    layout->addStretch();
    return page;
}

QWidget *ProfileEditorDialog::makePublishPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *notice = smallLabel(tr("Publishing destinations are not configured. Add a destination provider before upload options become available."), page);
    layout->addWidget(notice);
    auto *upload = new QCheckBox(tr("Upload after export"), page); upload->setEnabled(false);
    auto *remove = new QCheckBox(tr("Delete local file after upload"), page); remove->setEnabled(false);
    auto *notify = new QCheckBox(tr("Notify when publishing completes"), page); notify->setEnabled(false);
    layout->addWidget(upload); layout->addWidget(remove); layout->addWidget(notify); layout->addStretch();
    return page;
}


void ProfileEditorDialog::rebuildPresetList(const QString &formatKey, const QString &preferredUuid)
{
    if (!presetProfile_) return;
    const QSignalBlocker blocker(presetProfile_);
    const QString previousUuid = preferredUuid.isEmpty() ? presetProfile_->currentData().toString() : preferredUuid;
    presetProfile_->clear();
    presetProfile_->addItem(tr("Custom"), QString());
    presetProfile_->insertSeparator(1);

    int selected = 0;
    for (const auto &candidate : availableProfiles_) {
        if (!candidate.enabled || formatKeyForProfile(candidate) != formatKey) continue;
        presetProfile_->addItem(candidate.name, candidate.uuid);
        const int row = presetProfile_->count() - 1;
        if (!candidate.unavailableReason.isEmpty()) {
            if (auto *model = qobject_cast<QStandardItemModel *>(presetProfile_->model())) {
                if (auto *item = model->item(row)) {
                    item->setEnabled(false);
                    item->setToolTip(candidate.unavailableReason);
                }
            }
        }
        if (!previousUuid.isEmpty() && candidate.uuid == previousUuid) selected = row;
    }
    presetProfile_->setCurrentIndex(selected);
    const bool custom = selected == 0;
    if (name_) name_->setVisible(custom);
}

void ProfileEditorDialog::applyPresetToControls(const RenderProfile &profile)
{
    original_ = profile;
    // Export Settings edits always produce a user-owned snapshot; never overwrite a built-in preset.
    original_.uuid.clear();
    original_.builtIn = false;
    if (description_) description_->setText(profile.description);
    if (category_) category_->setCurrentIndex(qMax(0, category_->findData(profile.categoryUuid)));
    if (exportVideo_) exportVideo_->setChecked(!profile.videoEncoder.isEmpty());
    if (exportAudio_) exportAudio_->setChecked(!profile.audioEncoder.isEmpty());
    if (widthOverride_) widthOverride_->setChecked(profile.width > 0);
    if (heightOverride_) heightOverride_->setChecked(profile.height > 0);
    if (width_) width_->setValue(profile.width);
    if (height_) height_->setValue(profile.height);
    if (fps_) fps_->setValue(profile.frameRate);
    if (quality_ && profile.quality >= 0) quality_->setValue(profile.quality);
    if (videoBitrate_) videoBitrate_->setValue(profile.videoBitrateKbps > 0 ? profile.videoBitrateKbps / 1000.0 : videoBitrate_->value());
    if (maxBitrate_) maxBitrate_->setValue(profile.maxVideoBitrateKbps > 0 ? profile.maxVideoBitrateKbps / 1000.0 : maxBitrate_->value());
    if (audioBitrate_ && profile.audioBitrateKbps > 0) audioBitrate_->setValue(profile.audioBitrateKbps);
    if (fieldOrder_) fieldOrder_->setCurrentIndex(qMax(0, fieldOrder_->findData(profile.fieldOrder)));
    if (aspect_) aspect_->setCurrentIndex(qMax(0, aspect_->findData(profile.aspectRatio)));
    if (profileLevel_) profileLevel_->setCurrentIndex(qMax(0, profileLevel_->findData(profile.codecProfile)));
    if (codecLevel_) codecLevel_->setCurrentIndex(qMax(0, codecLevel_->findData(profile.codecLevel)));
    if (sampleRate_) sampleRate_->setCurrentIndex(qMax(0, sampleRate_->findData(profile.sampleRate)));
    if (channels_) channels_->setCurrentIndex(qMax(0, channels_->findData(profile.audioChannels)));
    if (normalizeLoudness_) normalizeLoudness_->setChecked(profile.normalizeLoudness);
    if (truePeakLimit_) truePeakLimit_->setChecked(profile.truePeakLimit);
    if (fastStart_) fastStart_->setChecked(profile.fastStart);
    if (preserveTimecode_) preserveTimecode_->setChecked(profile.preserveTimecode);
    if (burnCaptions_) burnCaptions_->setChecked(profile.burnCaptions);
    if (videoLimiter_) videoLimiter_->setChecked(profile.videoLimiter);
    if (burnTimecode_) burnTimecode_->setChecked(profile.burnTimecode);
    if (timecodeStart_) timecodeStart_->setText(profile.timecodeStart);
    if (timecodePosition_) timecodePosition_->setCurrentIndex(qMax(0, timecodePosition_->findData(profile.timecodePosition)));
    if (timecodeFontSize_) timecodeFontSize_->setValue(profile.timecodeFontSize);
    if (timecodeOpacity_) timecodeOpacity_->setValue(profile.timecodeOpacityPercent);
    if (timecodeBackgroundOpacity_) timecodeBackgroundOpacity_->setValue(profile.timecodeBackgroundOpacityPercent);
    if (fallback_) fallback_->setChecked(profile.allowSoftwareFallback);
    metadataMode_ = profile.metadataMode;
    metadataPreservation_ = profile.metadataPreservation;
    metadataIncludeMarkers_ = profile.metadataIncludeMarkers;
    metadataTemplate_ = profile.metadataTemplate;
    metadataFields_ = profile.metadataFields;
    updateControlState();
    updateEstimate();
    scheduleOutputPreview(pendingOutputPreviewMs_);
}

void ProfileEditorDialog::updateControlState()
{
    // This slot is also called while the individual settings pages are still
    // being constructed. Every control is therefore optional until the full
    // dialog has finished initialization.
    const bool video = exportVideo_ ? exportVideo_->isChecked() : true;
    const bool audio = exportAudio_ ? exportAudio_->isChecked() : true;
    const bool qualityMode = !rateControl_ || rateControl_->currentIndex() == 0;

    if (backend_) backend_->setEnabled(video);
    if (width_) width_->setEnabled(video && (!widthOverride_ || widthOverride_->isChecked()));
    if (height_) height_->setEnabled(video && (!heightOverride_ || heightOverride_->isChecked()));
    if (fps_) fps_->setEnabled(video);
    if (preset_) preset_->setEnabled(video);
    if (codecLevel_) codecLevel_->setEnabled(video && profileLevel_ && profileLevel_->isEnabled());
    if (fallback_ && backend_)
        fallback_->setEnabled(video && backend_->currentData().toString() == QStringLiteral("hardware"));
    if (rateControl_) rateControl_->setEnabled(video);
    if (quality_) quality_->setEnabled(video && qualityMode);
    if (videoBitrate_) videoBitrate_->setEnabled(video && !qualityMode);
    if (maxBitrate_ && rateControl_) maxBitrate_->setEnabled(video && rateControl_->currentIndex() >= 2);
    if (bitrateUnit_) bitrateUnit_->setEnabled(video && !qualityMode);
    if (extraArgs_) extraArgs_->setEnabled(video);
    if (fieldOrder_) fieldOrder_->setEnabled(video);
    if (aspect_) aspect_->setEnabled(video);
    if (profileLevel_) profileLevel_->setEnabled(video);
    if (videoLimiter_) videoLimiter_->setEnabled(video);
    if (burnCaptions_) burnCaptions_->setEnabled(video);
    if (burnTimecode_) burnTimecode_->setEnabled(video);
    if (timecodeStart_) timecodeStart_->setEnabled(video && burnTimecode_ && burnTimecode_->isChecked());
    if (timecodePosition_) timecodePosition_->setEnabled(video && burnTimecode_ && burnTimecode_->isChecked());
    if (timecodeFontSize_) timecodeFontSize_->setEnabled(video && burnTimecode_ && burnTimecode_->isChecked());
    if (timecodeOpacity_) timecodeOpacity_->setEnabled(video && burnTimecode_ && burnTimecode_->isChecked());
    if (timecodeBackgroundOpacity_) timecodeBackgroundOpacity_->setEnabled(video && burnTimecode_ && burnTimecode_->isChecked());

    if (audioEncoder_) audioEncoder_->setEnabled(audio);
    if (audioBitrate_) audioBitrate_->setEnabled(audio);
    if (sampleRate_) sampleRate_->setEnabled(audio);
    if (channels_) channels_->setEnabled(audio);
    if (normalizeLoudness_) normalizeLoudness_->setEnabled(audio);
    if (truePeakLimit_)
        truePeakLimit_->setEnabled(audio && normalizeLoudness_ && normalizeLoudness_->isChecked());

    updateEstimate();
}

void ProfileEditorDialog::matchSource()
{
    if(widthOverride_)widthOverride_->setChecked(false);
    if(heightOverride_)heightOverride_->setChecked(false);
    width_->setValue(0);
    height_->setValue(0);
    fps_->setValue(0.0);
    updateEstimate();
}

QString ProfileEditorDialog::estimateText() const
{
    if (sourceDurationMs_ <= 0)
        return tr("Estimated File Size: Assign media with a known duration to calculate it before rendering.");
    const qint64 effectiveDurationMs = trimEndMs_ > trimStartMs_ ? trimEndMs_ - trimStartMs_ : sourceDurationMs_;
    const double seconds = effectiveDurationMs / 1000.0;
    const double audioKbps = exportAudio_ && exportAudio_->isChecked() && audioBitrate_
                                 ? audioBitrate_->value() : 0.0;
    const double overhead = 1.018;
    if (exportVideo_ && !exportVideo_->isChecked())
        return tr("Estimated File Size: %1").arg(formatBytes((audioKbps * 1000.0 / 8.0) * seconds * overhead));

    if (rateControl_ && rateControl_->currentIndex() != 0) {
        const double unitScale = bitrateUnit_ ? bitrateUnit_->currentData().toDouble() : 1000.0;
        const double videoKbps = (videoBitrate_ ? videoBitrate_->value() : 0.0) * unitScale;
        const double bytes = ((videoKbps + audioKbps) * 1000.0 / 8.0) * seconds * overhead;
        return tr("Estimated File Size: %1 (bitrate-based)").arg(formatBytes(bytes));
    }

    const int width = width_ && width_->value() > 0 ? width_->value() : 1920;
    const int height = height_ && height_->value() > 0 ? height_->value() : 1080;
    const double fps = fps_ && fps_->value() > 0.0 ? fps_->value() : 30.0;
    const int quality = !quality_ || quality_->value() < 0 ? 20 : quality_->value();
    const double pixelsPerSecond = double(width) * double(height) * fps;
    const double qualityFactor = std::pow(2.0, (23.0 - quality) / 6.0);
    const double centerVideoKbps = std::clamp((pixelsPerSecond / 1000.0) * 0.075 * qualityFactor, 450.0, 180000.0);
    const double low = ((centerVideoKbps * 0.62 + audioKbps) * 1000.0 / 8.0) * seconds * overhead;
    const double high = ((centerVideoKbps * 1.55 + audioKbps) * 1000.0 / 8.0) * seconds * overhead;
    return tr("Estimated File Size: %1–%2 (quality-based estimate)").arg(formatBytes(low), formatBytes(high));
}

void ProfileEditorDialog::updateEstimate()
{
    if (!estimateLabel_ || !summaryLabel_ || !container_ || !width_ || !height_ || !fps_ ||
        !exportVideo_ || !exportAudio_ || !backend_ || !audioEncoder_ || !audioBitrate_)
        return;
    estimateLabel_->setText(estimateText());
    const QString resolution = width_->value() == 0 || height_->value() == 0
                                   ? tr("Match Source")
                                   : QStringLiteral("%1 × %2").arg(width_->value()).arg(height_->value());
    const QString fps = fps_->value() == 0.0 ? tr("Match Source") : QString::number(fps_->value(), 'f', 3);
    summaryLabel_->setText(tr("Output Summary\n%1 • %2 • %3 fps\nVideo: %4 • %5\nAudio: %6 @ %7 kbps")
                               .arg(container_->currentText(), resolution, fps,
                                    exportVideo_->isChecked() ? container_->currentText() : tr("Disabled"),
                                    exportVideo_->isChecked() ? backend_->currentText() : tr("Not Applicable"),
                                    exportAudio_->isChecked() ? audioEncoder_->currentText() : tr("Disabled"))
                               .arg(exportAudio_->isChecked() ? audioBitrate_->value() : 0));
    if (timeline_ && sourceDurationMs_ > 0) {
        const qint64 current = qRound64(sourceDurationMs_ * (timeline_->value() / 1000.0));
        scheduleOutputPreview(current);
    }
}

void ProfileEditorDialog::scheduleOutputPreview(qint64 positionMs)
{
    pendingOutputPreviewMs_ = std::clamp<qint64>(positionMs, 0, qMax<qint64>(0, sourceDurationMs_));
    if (!outputPreviewDebounce_ || sourceName_.isEmpty() || capabilities_.ffmpegPath.isEmpty())
        return;
    outputPreviewDebounce_->start();
}

QPixmap ProfileEditorDialog::renderSourceFrame(qint64 positionMs) const
{
    if (sourceName_.isEmpty() || !QFileInfo::exists(sourceName_))
        return {};
    QProcess preview;
    if (sourceJob_.sourceType == QStringLiteral("render-provider")) {
        const auto format = readRenderProviderSourceFormat(sourceJob_);
        const QString executable =
            locateRenderProviderExecutable(sourceJob_.providerId);
        if (format.width <= 0 || format.height <= 0 ||
            format.frameRate <= 0.0 || executable.isEmpty())
            return {};
        const qint64 frameDuration = qMax<qint64>(
            1, qCeil(1000.0 / format.frameRate));
        preview.start(executable, renderProviderVideoArguments(
            sourceJob_, executable, positionMs, frameDuration * 2,
            format.frameRate));
        if (!preview.waitForFinished(30000)) {
            preview.kill();
            preview.waitForFinished();
            return {};
        }
        const QByteArray raw = preview.readAllStandardOutput();
        const qsizetype frameBytes = qsizetype(format.width) * format.height * 4;
        if (preview.exitCode() != 0 || raw.size() < frameBytes)
            return {};
        const uchar *last = reinterpret_cast<const uchar *>(
            raw.constData() + raw.size() - frameBytes);
        return QPixmap::fromImage(
            QImage(last, format.width, format.height, format.width * 4,
                   QImage::Format_ARGB32_Premultiplied).copy());
    }
    if (capabilities_.ffmpegPath.isEmpty())
        return {};
    preview.start(capabilities_.ffmpegPath,
                  {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                   QStringLiteral("error"), QStringLiteral("-ss"),
                   QString::number(positionMs / 1000.0, 'f', 3),
                   QStringLiteral("-i"), sourceName_, QStringLiteral("-frames:v"),
                   QStringLiteral("1"), QStringLiteral("-vf"),
                   QStringLiteral("scale=1280:-2"), QStringLiteral("-f"),
                   QStringLiteral("image2pipe"), QStringLiteral("-vcodec"),
                   QStringLiteral("mjpeg"), QStringLiteral("pipe:1")});
    if (!preview.waitForFinished(6000)) {
        preview.kill();
        preview.waitForFinished();
        return {};
    }
    QPixmap frame;
    frame.loadFromData(preview.readAllStandardOutput());
    return frame;
}

void ProfileEditorDialog::failOutputPreview(const QString &message)
{
    if (outputPreview_)
        outputPreview_->clearPreview(tr("Compressed output preview unavailable\n%1").arg(message));
}

void ProfileEditorDialog::startOutputPreview()
{
    if (!outputPreview_ || sourceName_.isEmpty() || !QFileInfo::exists(sourceName_) || capabilities_.ffmpegPath.isEmpty())
        return;

    if (outputPreviewEncode_) {
        outputPreviewEncode_->kill();
        outputPreviewEncode_->deleteLater();
        outputPreviewEncode_ = nullptr;
    }
    if (outputPreviewDecode_) {
        outputPreviewDecode_->kill();
        outputPreviewDecode_->deleteLater();
        outputPreviewDecode_ = nullptr;
    }
    delete outputPreviewTempDir_;
    outputPreviewTempDir_ = new QTemporaryDir();
    if (!outputPreviewTempDir_->isValid()) {
        failOutputPreview(tr("Temporary folder could not be created."));
        return;
    }

    RenderProfile previewProfile = profile();
    if (previewProfile.videoEncoder.isEmpty()) {
        failOutputPreview(tr("Video export is disabled."));
        return;
    }

    // Encode a short real segment with the selected codec/rate-control settings,
    // then decode a frame from that encoded segment. This makes compression,
    // scaling, pixel format and filter effects visible instead of mirroring Source.
    const qint64 segmentDurationMs = 900;
    outputPreviewSegmentStartMs_ = qMax<qint64>(0, pendingOutputPreviewMs_ - 350);
    qint64 segmentEndMs = outputPreviewSegmentStartMs_ + segmentDurationMs;
    if (sourceDurationMs_ > 0)
        segmentEndMs = qMin(segmentEndMs, sourceDurationMs_);
    if (segmentEndMs <= outputPreviewSegmentStartMs_)
        segmentEndMs = outputPreviewSegmentStartMs_ + 120;

    const qint64 fullOutputStartMs = qMax<qint64>(0, previewProfile.trimStartMs);
    previewProfile.trimStartMs = outputPreviewSegmentStartMs_;
    previewProfile.trimEndMs = segmentEndMs;
    if (previewProfile.burnTimecode) {
        const double tcFps = previewProfile.frameRate > 0.0 ? previewProfile.frameRate : 25.0;
        previewProfile.timecodeStart = shiftedTimecode(previewProfile.timecodeStart,
                                                       qMax<qint64>(0, outputPreviewSegmentStartMs_ - fullOutputStartMs),
                                                       tcFps);
    }
    previewProfile.audioEncoder.clear();
    previewProfile.fastStart = false;
    previewProfile.writeMetadata = false;
    previewProfile.preserveTimecode = false;

    QString extension = previewProfile.extension.trimmed();
    if (extension.isEmpty()) extension = previewProfile.container.trimmed();
    if (extension.isEmpty()) extension = QStringLiteral("mkv");
    outputPreviewFile_ = outputPreviewTempDir_->filePath(QStringLiteral("compressed-preview.%1").arg(extension));

    QueueJob previewJob = sourceJob_.sourceType == QStringLiteral("render-provider")
        ? sourceJob_ : QueueJob{};
    previewJob.sourcePath = sourceName_;
    previewJob.outputPath = outputPreviewFile_;
    previewJob.profileSnapshot = previewProfile;
    QString effectiveEncoder;
    QStringList arguments;
    QString previewProgram = capabilities_.ffmpegPath;
    if (previewJob.sourceType == QStringLiteral("render-provider")) {
        const QString jobPath = outputPreviewTempDir_->filePath(
            QStringLiteral("preview-job.json"));
        QFile jobFile(jobPath);
        if (!jobFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            jobFile.write(QJsonDocument(previewJob.toJson()).toJson(
                              QJsonDocument::Compact)) < 0) {
            failOutputPreview(tr("The preview job could not be created."));
            return;
        }
        jobFile.close();
#ifdef Q_OS_WIN
        previewProgram = QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("flux-encoder-worker.exe"));
#else
        previewProgram = QDir(QCoreApplication::applicationDirPath()).filePath(
            QStringLiteral("flux-encoder-worker"));
#endif
        if (!QFileInfo::exists(previewProgram)) {
            failOutputPreview(tr("The Flux Encoder worker is not installed."));
            return;
        }
        arguments = {QStringLiteral("--job"), jobPath};
        effectiveEncoder = previewProfile.videoEncoder;
    } else {
        arguments = JobBuilder::ffmpegArguments(
            previewJob, outputPreviewFile_, &effectiveEncoder);
    }

    outputPreview_->clearPreview(tr("Rendering compressed output preview…\n%1").arg(effectiveEncoder));
    outputPreviewEncode_ = new QProcess(this);
    outputPreviewEncode_->setProgram(previewProgram);
    outputPreviewEncode_->setArguments(arguments);
    connect(outputPreviewEncode_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        QProcess *process = outputPreviewEncode_;
        outputPreviewEncode_ = nullptr;
        const QString diagnostics = process ? QString::fromUtf8(process->readAllStandardError()).trimmed() : QString();
        if (process) process->deleteLater();
        if (status != QProcess::NormalExit || exitCode != 0 || !QFileInfo::exists(outputPreviewFile_)) {
            failOutputPreview(diagnostics.isEmpty() ? tr("The selected encoder could not render the preview.")
                                                    : diagnostics.left(240));
            return;
        }
        startOutputPreviewDecode();
    });
    outputPreviewEncode_->start();
}

void ProfileEditorDialog::startOutputPreviewDecode()
{
    if (outputPreviewFile_.isEmpty() || !QFileInfo::exists(outputPreviewFile_)) {
        failOutputPreview(tr("The encoded preview segment was not created."));
        return;
    }
    const qint64 relativeMs = qMax<qint64>(0, pendingOutputPreviewMs_ - outputPreviewSegmentStartMs_);
    outputPreviewDecode_ = new QProcess(this);
    outputPreviewDecode_->setProgram(capabilities_.ffmpegPath);
    outputPreviewDecode_->setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                                        QStringLiteral("-ss"), QString::number(relativeMs / 1000.0, 'f', 3),
                                        QStringLiteral("-i"), outputPreviewFile_, QStringLiteral("-frames:v"), QStringLiteral("1"),
                                        QStringLiteral("-vf"), QStringLiteral("scale=1280:-2"), QStringLiteral("-f"), QStringLiteral("image2pipe"),
                                        QStringLiteral("-vcodec"), QStringLiteral("mjpeg"), QStringLiteral("pipe:1")});
    connect(outputPreviewDecode_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus status) {
        QProcess *process = outputPreviewDecode_;
        outputPreviewDecode_ = nullptr;
        const QByteArray imageData = process ? process->readAllStandardOutput() : QByteArray();
        const QString diagnostics = process ? QString::fromUtf8(process->readAllStandardError()).trimmed() : QString();
        if (process) process->deleteLater();
        QPixmap frame;
        frame.loadFromData(imageData);
        if (status == QProcess::NormalExit && exitCode == 0 && !frame.isNull()) {
            outputPreview_->setPreviewPixmap(frame);
            if (compareOutputPreview_) compareOutputPreview_->setPreviewPixmap(frame);
            outputPreview_->setToolTip(tr("Decoded from a short segment encoded with the current output codec and compression settings."));
        } else {
            failOutputPreview(diagnostics.isEmpty() ? tr("The encoded preview frame could not be decoded.")
                                                    : diagnostics.left(240));
        }
    });
    outputPreviewDecode_->start();
}

RenderProfile ProfileEditorDialog::profile() const
{
    RenderProfile profile = original_;
    if (profile.uuid.isEmpty()) profile.uuid = createUuid();
    profile.builtIn = false;
    const bool customPreset = !presetProfile_ || presetProfile_->currentData().toString().isEmpty();
    profile.name = customPreset ? name_->text().trimmed() : presetProfile_->currentText();
    if (profile.name.isEmpty()) profile.name = tr("Custom Preset");
    profile.description = description_->text().trimmed();
    profile.categoryUuid = category_->currentData().toString();
    const QString formatKey = container_->currentData().toString();
    const auto &selectedFormat = formatSpec(formatKey);
    const QString performance = backend_->currentData().toString();
    QString resolvedBackend = QStringLiteral("software");
    if (performance == QStringLiteral("hardware"))
        resolvedBackend = bestHardwareBackend(formatKey, original_.hardwareBackend, capabilities_);
    if (resolvedBackend.isEmpty()) resolvedBackend = QStringLiteral("software");
    profile.hardwareBackend = resolvedBackend;
    profile.videoEncoder = exportVideo_->isChecked() ? encoder_->currentText().trimmed() : QString();
    profile.container = selectedFormat.container;
    profile.extension = selectedFormat.extension;
    profile.width = width_->value();
    profile.height = height_->value();
    profile.frameRate = fps_->value();
    profile.quality = rateControl_->currentIndex() == 0 ? quality_->value() : -1;
    const double bitrateScale = bitrateUnit_ ? bitrateUnit_->currentData().toDouble() : 1000.0;
    profile.videoBitrateKbps = rateControl_->currentIndex() == 0 ? 0 : qRound(videoBitrate_->value() * bitrateScale);
    profile.maxVideoBitrateKbps = rateControl_->currentIndex() >= 2 ? qRound(maxBitrate_->value() * bitrateScale) : 0;
    profile.audioEncoder = exportAudio_->isChecked() ? audioEncoder_->currentText().trimmed() : QString();
    profile.audioBitrateKbps = exportAudio_->isChecked() ? audioBitrate_->value() : 0;
    profile.encoderPreset = preset_->currentData().toString();
    profile.fieldOrder = fieldOrder_ ? fieldOrder_->currentData().toString() : QStringLiteral("progressive");
    profile.aspectRatio = aspect_ ? aspect_->currentData().toString() : QStringLiteral("1:1");
    profile.codecProfile = profileLevel_ ? profileLevel_->currentData().toString() : QString();
    profile.codecLevel = codecLevel_ ? codecLevel_->currentData().toString() : QString();
    profile.sampleRate = sampleRate_ ? sampleRate_->currentData().toInt() : 0;
    profile.audioChannels = channels_ ? channels_->currentData().toInt() : 0;
    profile.normalizeLoudness = normalizeLoudness_ && normalizeLoudness_->isChecked();
    profile.truePeakLimit = truePeakLimit_ && truePeakLimit_->isChecked();
    profile.writeMetadata = !writeMetadata_ || writeMetadata_->isChecked();
    profile.metadataMode = profile.writeMetadata ? metadataMode_ : QStringLiteral("none");
    profile.metadataPreservation = metadataPreservation_;
    profile.metadataIncludeMarkers = metadataIncludeMarkers_;
    profile.metadataTemplate = metadataTemplate_;
    profile.metadataFields = metadataFields_;
    profile.fastStart = fastStart_ && fastStart_->isChecked();
    profile.preserveTimecode = !preserveTimecode_ || preserveTimecode_->isChecked();
    profile.burnCaptions = burnCaptions_ && burnCaptions_->isChecked();
    profile.videoLimiter = videoLimiter_ && videoLimiter_->isChecked();
    profile.burnTimecode = burnTimecode_ && burnTimecode_->isChecked();
    profile.timecodeStart = timecodeStart_ ? timecodeStart_->text().trimmed() : QStringLiteral("00:00:00:00");
    if (profile.timecodeStart.isEmpty()) profile.timecodeStart = QStringLiteral("00:00:00:00");
    profile.timecodePosition = timecodePosition_ ? timecodePosition_->currentData().toString() : QStringLiteral("bottom-center");
    profile.timecodeFontSize = timecodeFontSize_ ? timecodeFontSize_->value() : 42;
    profile.timecodeOpacityPercent = timecodeOpacity_ ? timecodeOpacity_->value() : 100;
    profile.timecodeBackgroundOpacityPercent = timecodeBackgroundOpacity_ ? timecodeBackgroundOpacity_->value() : 55;
    profile.trimStartMs = trimStartMs_;
    profile.trimEndMs = trimEndMs_;
    profile.allowSoftwareFallback = fallback_->isChecked();
    profile.videoArguments = extraArgs_->toPlainText().split('\n', Qt::SkipEmptyParts);
    profile.revision = original_.uuid.isEmpty() ? 1 : std::max(1, original_.revision + 1);
    profile.enabled = profile.videoEncoder.isEmpty() || profile.hardwareBackend == QStringLiteral("software")
                        ? (profile.videoEncoder.isEmpty() || capabilities_.hasEncoder(profile.videoEncoder))
                        : capabilities_.encoderUsable(profile.videoEncoder);
    profile.unavailableReason = profile.enabled ? QString() : capabilities_.encoderReason(profile.videoEncoder);
    return profile;
}
}
