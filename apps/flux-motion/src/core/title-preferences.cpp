#include "title-preferences.h"

#include "title-data.h"
#include "title-logger.h"
#include "system-memory.h"

#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <atomic>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

constexpr const char *kSettingsOrg = "FluxMotion";
constexpr const char *kSettingsApp = "Dock";
constexpr const char *kSettingsGroup = "Rendering";
constexpr const char *kEditorGroup = "Editor";
constexpr const char *kLoggingGroup = "Logging";
constexpr const char *kTimelineColorGroup = "TimelineColors";
constexpr const char *kAppearanceGroup = "Appearance";
constexpr const char *kCanvasHelperColorGroup = "CanvasHelperColors";
constexpr const char *kAutosaveEnabledKey = "autosaveEnabled";
constexpr const char *kAutosaveIntervalMinutesKey = "autosaveIntervalMinutes";
constexpr const char *kPreviewAudioDeviceKey = "previewAudioDevice";
constexpr const char *kCacheEnabledKey = "cacheEnabled";
constexpr const char *kCacheRamLimitMbKey = "cacheRamLimitMb";
constexpr const char *kShadowMapSizePxKey = "shadowMapSizePx";
constexpr const char *kCacheDiskLocationKey = "cacheDiskLocation";
constexpr const char *kClearCacheOnExitKey = "clearCacheOnExit";
constexpr const char *kLoggingEnabledKey = "enabled";
constexpr const char *kLoggingLevelKey = "level";
constexpr const char *kLoggingMirrorToObsKey = "mirrorToObs";
constexpr const char *kCachePlaybackLoggingKey = "cachePlayback";
constexpr const char *kLoggingFilePathKey = "filePath";
constexpr const char *kLoggingDirectoryKey = "directory";
constexpr const char *kLoggingCategoriesGroup = "Categories";
constexpr const char *kSceneMaskColorKey = "sceneMaskColor";
std::atomic_bool g_gpu_available{true};
std::mutex g_gpu_reason_mutex;
std::string g_gpu_unavailable_reason;

QString timeline_color_key(TitlePreferences::TimelineColorRole role)
{
    switch (role) {
    case TitlePreferences::TimelineColorRole::TextLayer:
        return QStringLiteral("textLayer");
    case TitlePreferences::TimelineColorRole::ClockLayer:
        return QStringLiteral("clockLayer");
    case TitlePreferences::TimelineColorRole::TickerLayer:
        return QStringLiteral("tickerLayer");
    case TitlePreferences::TimelineColorRole::ObjectLayer:
        return QStringLiteral("objectLayer");
    case TitlePreferences::TimelineColorRole::ImageLayer:
        return QStringLiteral("imageLayer");
    case TitlePreferences::TimelineColorRole::VideoLayer:
        return QStringLiteral("videoLayer");
    case TitlePreferences::TimelineColorRole::AudioLayer:
        return QStringLiteral("audioLayer");
    case TitlePreferences::TimelineColorRole::EmptyLayer:
        return QStringLiteral("emptyLayer");
    case TitlePreferences::TimelineColorRole::AdjustmentLayer:
        return QStringLiteral("adjustmentLayer");
    case TitlePreferences::TimelineColorRole::ColorSolidLayer:
        return QStringLiteral("colorSolidLayer");
    case TitlePreferences::TimelineColorRole::CameraLayer:
        return QStringLiteral("cameraLayer");
    case TitlePreferences::TimelineColorRole::LightLayer:
        return QStringLiteral("lightLayer");
    case TitlePreferences::TimelineColorRole::GroupLayer:
        return QStringLiteral("groupLayer");
    case TitlePreferences::TimelineColorRole::Current:
        return QStringLiteral("current");
    case TitlePreferences::TimelineColorRole::Pause:
        return QStringLiteral("pause");
    case TitlePreferences::TimelineColorRole::Loop:
        return QStringLiteral("loop");
    }
    return QStringLiteral("textLayer");
}


QString canvas_helper_color_key(TitlePreferences::CanvasHelperColorRole role)
{
    switch (role) {
    case TitlePreferences::CanvasHelperColorRole::Guides:
        return QStringLiteral("guides");
    case TitlePreferences::CanvasHelperColorRole::ActiveGuide:
        return QStringLiteral("activeGuide");
    case TitlePreferences::CanvasHelperColorRole::RulerMouseIndicator:
        return QStringLiteral("rulerMouseIndicator");
    case TitlePreferences::CanvasHelperColorRole::HoverBoundingBox:
        return QStringLiteral("hoverBoundingBox");
    case TitlePreferences::CanvasHelperColorRole::SelectionBoundingBox:
        return QStringLiteral("selectionBoundingBox");
    case TitlePreferences::CanvasHelperColorRole::TextBoundingBox:
        return QStringLiteral("textBoundingBox");
    case TitlePreferences::CanvasHelperColorRole::SnapLines:
        return QStringLiteral("snapLines");
    case TitlePreferences::CanvasHelperColorRole::CanvasSnapLines:
        return QStringLiteral("canvasSnapLines");
    case TitlePreferences::CanvasHelperColorRole::ObjectSnapLines:
        return QStringLiteral("objectSnapLines");
    case TitlePreferences::CanvasHelperColorRole::CanvasBorder:
        return QStringLiteral("canvasBorder");
    case TitlePreferences::CanvasHelperColorRole::ActionSafe:
        return QStringLiteral("actionSafe");
    case TitlePreferences::CanvasHelperColorRole::GraphicsSafe:
        return QStringLiteral("graphicsSafe");
    }
    return QStringLiteral("guides");
}

int normalize_shadow_map_size_px(int pixels)
{
    constexpr std::array<int, 5> kAllowedSizes {{256, 512, 1024, 2048, 4096}};
    int best = kAllowedSizes.front();
    int best_delta = std::abs(pixels - best);
    for (int size : kAllowedSizes) {
        const int delta = std::abs(pixels - size);
        if (delta < best_delta || (delta == best_delta && size > best)) {
            best = size;
            best_delta = delta;
        }
    }
    return best;
}

} // namespace

namespace TitlePreferences {

bool autosave_enabled()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    const bool enabled = settings.value(QString::fromUtf8(kAutosaveEnabledKey), true).toBool();
    settings.endGroup();
    return enabled;
}

void set_autosave_enabled(bool enabled)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    settings.setValue(QString::fromUtf8(kAutosaveEnabledKey), enabled);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

int autosave_interval_minutes()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    const int minutes = settings.value(QString::fromUtf8(kAutosaveIntervalMinutesKey), 2).toInt();
    settings.endGroup();
    return std::clamp(minutes, 1, 60);
}

void set_autosave_interval_minutes(int minutes)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    settings.setValue(QString::fromUtf8(kAutosaveIntervalMinutesKey), std::clamp(minutes, 1, 60));
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

QString preview_audio_device_id()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg),
                       QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    const QString device_id = settings.value(
        QString::fromUtf8(kPreviewAudioDeviceKey)).toString();
    settings.endGroup();
    return device_id;
}

void set_preview_audio_device_id(const QString &device_id)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg),
                       QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kEditorGroup));
    settings.setValue(QString::fromUtf8(kPreviewAudioDeviceKey), device_id);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

bool cache_enabled()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    /* First-install default. An explicit user choice (including false) remains
     * persisted under the same key and is never overwritten on upgrade. */
    const bool enabled = settings.value(QString::fromUtf8(kCacheEnabledKey), true).toBool();
    settings.endGroup();
    return enabled;
}

void set_cache_enabled(bool enabled)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    settings.setValue(QString::fromUtf8(kCacheEnabledKey), enabled);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

int cache_ram_limit_mb()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    const int limit = settings.value(QString::fromUtf8(kCacheRamLimitMbKey), 512).toInt();
    settings.endGroup();
    return fxm::system_memory::clamp_cache_ram_mb(limit);
}

void set_cache_ram_limit_mb(int megabytes)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    settings.setValue(QString::fromUtf8(kCacheRamLimitMbKey),
                      fxm::system_memory::clamp_cache_ram_mb(megabytes));
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

int shadow_map_size_px()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    const int pixels = settings.value(QString::fromUtf8(kShadowMapSizePxKey), 2048).toInt();
    settings.endGroup();
    return normalize_shadow_map_size_px(pixels);
}

void set_shadow_map_size_px(int pixels)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    settings.setValue(QString::fromUtf8(kShadowMapSizePxKey),
                      normalize_shadow_map_size_px(pixels));
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

QString cache_disk_location()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    QString path = settings.value(QString::fromUtf8(kCacheDiskLocationKey)).toString();
    settings.endGroup();
    if (!path.trimmed().isEmpty())
        return QDir::cleanPath(path);
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/FluxMotion");
    return QDir(base).filePath(QStringLiteral("frame-cache"));
}

void set_cache_disk_location(const QString &path)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    settings.setValue(QString::fromUtf8(kCacheDiskLocationKey), QDir::cleanPath(path));
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

bool clear_cache_on_exit()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    const bool enabled = settings.value(QString::fromUtf8(kClearCacheOnExitKey), false).toBool();
    settings.endGroup();
    return enabled;
}

void set_clear_cache_on_exit(bool enabled)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kSettingsGroup));
    settings.setValue(QString::fromUtf8(kClearCacheOnExitKey), enabled);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

bool logging_enabled()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    const bool enabled = settings.value(QString::fromUtf8(kLoggingEnabledKey), false).toBool();
    settings.endGroup();
    return enabled;
}

void set_logging_enabled(bool enabled)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.setValue(QString::fromUtf8(kLoggingEnabledKey), enabled);
    settings.endGroup();
    settings.sync();
    TitleLogger::refreshConfiguration();
    notify_changed(nullptr);
}

int logging_level()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    const int level = settings.value(QString::fromUtf8(kLoggingLevelKey), 3).toInt();
    settings.endGroup();
    return std::clamp(level, 0, 5);
}

void set_logging_level(int level)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.setValue(QString::fromUtf8(kLoggingLevelKey), std::clamp(level, 0, 5));
    settings.endGroup();
    settings.sync();
    TitleLogger::refreshConfiguration();
    notify_changed(nullptr);
}

bool logging_mirror_to_obs()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    const bool enabled = settings.value(QString::fromUtf8(kLoggingMirrorToObsKey), false).toBool();
    settings.endGroup();
    return enabled;
}

void set_logging_mirror_to_obs(bool enabled)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.setValue(QString::fromUtf8(kLoggingMirrorToObsKey), enabled);
    settings.endGroup();
    settings.sync();
    TitleLogger::refreshConfiguration();
    notify_changed(nullptr);
}

bool logging_category_enabled(const QString &category, bool default_enabled)
{
    const QString clean = category.trimmed().isEmpty()
        ? QStringLiteral("General") : category.trimmed();
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.beginGroup(QString::fromUtf8(kLoggingCategoriesGroup));
    const bool enabled = settings.value(clean, default_enabled).toBool();
    settings.endGroup();
    settings.endGroup();
    return enabled;
}

void set_logging_category_enabled(const QString &category, bool enabled)
{
    const QString clean = category.trimmed().isEmpty()
        ? QStringLiteral("General") : category.trimmed();
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.beginGroup(QString::fromUtf8(kLoggingCategoriesGroup));
    settings.setValue(clean, enabled);
    settings.endGroup();
    settings.endGroup();
    settings.sync();
    TitleLogger::refreshConfiguration();
    notify_changed(nullptr);
}

bool cache_playback_logging_enabled()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.beginGroup(QString::fromUtf8(kLoggingCategoriesGroup));
    if (settings.contains(QStringLiteral("CachePlayback"))) {
        const bool enabled = settings.value(QStringLiteral("CachePlayback"), false).toBool();
        settings.endGroup();
        settings.endGroup();
        return enabled;
    }
    settings.endGroup();
    // Migrate the legacy dedicated toggle without changing its default.
    const bool enabled = settings.value(QString::fromUtf8(kCachePlaybackLoggingKey), false).toBool();
    settings.endGroup();
    return enabled;
}

void set_cache_playback_logging_enabled(bool enabled)
{
    set_logging_category_enabled(QStringLiteral("CachePlayback"), enabled);
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.setValue(QString::fromUtf8(kCachePlaybackLoggingKey), enabled);
    settings.endGroup();
    settings.sync();
}

QString logging_directory()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    QString path = settings.value(QString::fromUtf8(kLoggingDirectoryKey)).toString();
    if (path.trimmed().isEmpty()) {
        const QString legacy_file = settings.value(QString::fromUtf8(kLoggingFilePathKey)).toString();
        if (!legacy_file.trimmed().isEmpty())
            path = QFileInfo(legacy_file).absolutePath();
    }
    settings.endGroup();
    if (!path.trimmed().isEmpty())
        return QDir::cleanPath(path);
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/FluxMotion");
    return QDir(base).filePath(QStringLiteral("logs"));
}

void set_logging_directory(const QString &path)
{
    QString clean = path.trimmed();
    if (!clean.isEmpty()) {
        QFileInfo info(clean);
        if (info.suffix().compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0)
            clean = info.absolutePath();
        clean = QDir::cleanPath(clean);
    }
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kLoggingGroup));
    settings.setValue(QString::fromUtf8(kLoggingDirectoryKey), clean);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

QString logging_file_path()
{
    // Legacy API: return the folder plus a stable placeholder filename. New
    // code should use TitleLogger::currentSessionFilePath().
    return QDir(logging_directory()).filePath(
        QStringLiteral("flux-motion.log"));
}

void set_logging_file_path(const QString &path)
{
    set_logging_directory(path);
}

bool gpu_available()
{
    return g_gpu_available.load();
}

void set_gpu_available(bool available, const char *reason)
{
    const bool previous = g_gpu_available.exchange(available);
    {
        std::lock_guard<std::mutex> lock(g_gpu_reason_mutex);
        g_gpu_unavailable_reason = available ? std::string() : (reason ? reason : "GPU effects unavailable");
    }
    if (previous != available)
        notify_changed(nullptr);
}

const char *gpu_unavailable_reason()
{
    std::lock_guard<std::mutex> lock(g_gpu_reason_mutex);
    return g_gpu_unavailable_reason.empty() ? "GPU effects unavailable" : g_gpu_unavailable_reason.c_str();
}

QColor default_timeline_color(TimelineColorRole role)
{
    switch (role) {
    case TimelineColorRole::TextLayer:
        return QColor(0xb4, 0x5a, 0xa0);
    case TimelineColorRole::ClockLayer:
        return QColor(0x4b, 0x9a, 0xc8);
    case TimelineColorRole::TickerLayer:
        return QColor(0xd8, 0x8a, 0x30);
    case TimelineColorRole::ObjectLayer:
        return QColor(0x4f, 0x8f, 0x58);
    case TimelineColorRole::ImageLayer:
        return QColor(0x7d, 0x8b, 0x7f);
    case TimelineColorRole::VideoLayer:
        return QColor(0x8e, 0x5d, 0xcc);
    case TimelineColorRole::AudioLayer:
        return QColor(0x12, 0xb8, 0x86);
    case TimelineColorRole::EmptyLayer:
        return QColor(0x86, 0x8e, 0x96);
    case TimelineColorRole::AdjustmentLayer:
        return QColor(0xf0, 0x65, 0x95);
    case TimelineColorRole::ColorSolidLayer:
        return QColor(0xe8, 0x59, 0x0c);
    case TimelineColorRole::CameraLayer:
        return QColor(0x22, 0x8b, 0xe6);
    case TimelineColorRole::LightLayer:
        return QColor(0xfc, 0xc4, 0x19);
    case TimelineColorRole::GroupLayer:
        return QColor(0x65, 0x6f, 0xc8);
    case TimelineColorRole::Current:
        return QColor(0xff, 0x44, 0x44);
    case TimelineColorRole::Pause:
        return QColor(0xff, 0xc8, 0x32);
    case TimelineColorRole::Loop:
        return QColor(0x20, 0xa0, 0xff);
    }
    return QColor(0x65, 0x8a, 0xc8);
}

QColor timeline_color(TimelineColorRole role)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kTimelineColorGroup));
    const QColor fallback = default_timeline_color(role);
    const QColor color = settings.value(timeline_color_key(role), fallback).value<QColor>();
    settings.endGroup();
    return color.isValid() ? color : fallback;
}

void set_timeline_color(TimelineColorRole role, const QColor &color)
{
    if (!color.isValid())
        return;

    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kTimelineColorGroup));
    settings.setValue(timeline_color_key(role), color);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

QColor default_scene_mask_color()
{
    return QColor(255, 0, 200, 240);
}

QColor scene_mask_color()
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kAppearanceGroup));
    const QColor fallback = default_scene_mask_color();
    const QColor color = settings.value(QString::fromUtf8(kSceneMaskColorKey), fallback).value<QColor>();
    settings.endGroup();
    return color.isValid() ? color : fallback;
}

void set_scene_mask_color(const QColor &color)
{
    if (!color.isValid())
        return;

    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kAppearanceGroup));
    settings.setValue(QString::fromUtf8(kSceneMaskColorKey), color);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}


QColor default_canvas_helper_color(CanvasHelperColorRole role)
{
    switch (role) {
    case CanvasHelperColorRole::Guides:
        return QColor(0, 160, 255, 210);
    case CanvasHelperColorRole::ActiveGuide:
        return QColor(255, 220, 0, 240);
    case CanvasHelperColorRole::RulerMouseIndicator:
        return QColor(0, 120, 255, 220);
    case CanvasHelperColorRole::HoverBoundingBox:
        return QColor(0, 122, 255, 135);
    case CanvasHelperColorRole::SelectionBoundingBox:
        return QColor(0, 120, 255, 230);
    case CanvasHelperColorRole::TextBoundingBox:
        return QColor(255, 220, 0, 255);
    case CanvasHelperColorRole::SnapLines:
        return QColor(0, 220, 255, 235);
    case CanvasHelperColorRole::CanvasSnapLines:
        return QColor(0, 210, 110, 235);
    case CanvasHelperColorRole::ObjectSnapLines:
        return QColor(235, 45, 55, 235);
    case CanvasHelperColorRole::CanvasBorder:
        return QColor(0, 120, 255, 220);
    case CanvasHelperColorRole::ActionSafe:
        return QColor(0, 200, 255, 190);
    case CanvasHelperColorRole::GraphicsSafe:
        return QColor(255, 220, 0, 190);
    }
    return QColor(0, 160, 255, 210);
}

QColor canvas_helper_color(CanvasHelperColorRole role)
{
    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kCanvasHelperColorGroup));
    const QColor fallback = default_canvas_helper_color(role);
    const QColor color = settings.value(canvas_helper_color_key(role), fallback).value<QColor>();
    settings.endGroup();
    return color.isValid() ? color : fallback;
}

void set_canvas_helper_color(CanvasHelperColorRole role, const QColor &color)
{
    if (!color.isValid())
        return;

    QSettings settings(QString::fromUtf8(kSettingsOrg), QString::fromUtf8(kSettingsApp));
    settings.beginGroup(QString::fromUtf8(kCanvasHelperColorGroup));
    settings.setValue(canvas_helper_color_key(role), color);
    settings.endGroup();
    settings.sync();
    notify_changed(nullptr);
}

void notify_changed(QObject *)
{
    TitleDataStore::instance().touch_runtime_change();
}

} // namespace TitlePreferences
