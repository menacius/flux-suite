#include "title-logger.h"

#include "title-preferences.h"
#include "logger.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {

QMutex g_log_mutex;
QMutex g_config_mutex;
QString g_session_file_path;
QString g_session_stamp;
bool g_session_started = false;
bool g_session_start_line_written = false;
QFile g_log_file;
QString g_open_log_path;
QByteArray g_pending_log_data;
QElapsedTimer g_log_flush_clock;

std::atomic_bool g_configuration_initialized {false};
std::atomic_bool g_logging_enabled {false};
std::atomic_bool g_logging_mirror_to_obs {false};
std::atomic_int g_logging_level {static_cast<int>(TitleLogLevel::Info)};
std::atomic_uint64_t g_enabled_category_mask {0};

constexpr int kLogBufferFlushBytes = 64 * 1024;
constexpr qint64 kLogBufferFlushIntervalMs = 250;

const QVector<TitleLogCategory> &all_categories()
{
    static const QVector<TitleLogCategory> categories = {
        {QStringLiteral("General"), QStringLiteral("General messages"),
         QStringLiteral("Unclassified application messages."), QStringLiteral("Core and application"), true},
        {QStringLiteral("Plugin"), QStringLiteral("Plugin lifecycle"),
         QStringLiteral("Module load, unload and OBS frontend events."), QStringLiteral("Core and application"), true},
        {QStringLiteral("Preferences"), QStringLiteral("Preferences"),
         QStringLiteral("Application preference changes and configuration."), QStringLiteral("Core and application"), true},
        {QStringLiteral("TitleStore"), QStringLiteral("Titles and persistence"),
         QStringLiteral("Title loading, saving, revision and project storage."), QStringLiteral("Core and application"), true},
        {QStringLiteral("Serialization"), QStringLiteral("Serialization and recovery"),
         QStringLiteral("Title serialization, migration, recovery and compatibility warnings."), QStringLiteral("Core and application"), true},
        {QStringLiteral("ImportExport"), QStringLiteral("Import and export"),
         QStringLiteral("Title/template import, append and export operations."), QStringLiteral("Core and application"), true},

        {QStringLiteral("Source"), QStringLiteral("Source lifecycle and visibility"),
         QStringLiteral("Source creation, update, activation, visibility, title binding and presentation resets."), QStringLiteral("OBS source"), true},
        {QStringLiteral("SourceTiming"), QStringLiteral("Source timing and animation"),
         QStringLiteral("OBS tick cadence, playhead progression, cue phases, animation frame selection and discontinuities."), QStringLiteral("OBS source"), false},
        {QStringLiteral("SourcePresentation"), QStringLiteral("Source frame presentation"),
         QStringLiteral("Tick-to-render handoff, dirty state, frame publication, skipped draws, stale-frame holds and generation mismatches."), QStringLiteral("OBS source"), false},
        {QStringLiteral("SourceFlicker"), QStringLiteral("Source flicker and frame consistency"),
         QStringLiteral("Detect transparent/missing frames, alternating publication state, repeated first-frame recovery and rapid visible-output changes."), QStringLiteral("OBS source"), false},
        {QStringLiteral("Audio"), QStringLiteral("Audio"),
         QStringLiteral("Audio decode, editor monitoring, packet scheduling, transport and underrun diagnostics."), QStringLiteral("OBS source"), true},

        {QStringLiteral("Editor"), QStringLiteral("Editor lifecycle"),
         QStringLiteral("Editor lifecycle, title changes and preview synchronization."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Dock"), QStringLiteral("Docks and layout"),
         QStringLiteral("Dock lifecycle, layout restoration, title selection and cue controls."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Canvas"), QStringLiteral("Canvas"),
         QStringLiteral("Canvas presentation, interactions and display rendering."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Properties"), QStringLiteral("Properties panels"),
         QStringLiteral("Properties-panel edits and live visual updates."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("Timeline"), QStringLiteral("Timeline and transport"),
         QStringLiteral("Timeline edits, playback, work area and keyframes."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("LiveCueUI"), QStringLiteral("Live cue interface"),
         QStringLiteral("Live-cue table editing and user-interface state."), QStringLiteral("Editor and interface"), true},
        {QStringLiteral("CueControl"), QStringLiteral("Cue control"),
         QStringLiteral("Cue validation, selection, take, cancel and external-data cue actions."), QStringLiteral("Editor and interface"), true},

        {QStringLiteral("Layers"), QStringLiteral("Layers"),
         QStringLiteral("Layer creation, deletion, ordering, visibility and parenting."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Grouping"), QStringLiteral("Grouping and parenting"),
         QStringLiteral("Group membership, reparent conversion, keyframe preservation and group-render diagnostics."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Coordinates"), QStringLiteral("Coordinate systems"),
         QStringLiteral("Local, parent and world-space conversion diagnostics."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Text"), QStringLiteral("Text model"),
         QStringLiteral("Rich-text layout, inline editing and text model changes."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("TextAnimator"), QStringLiteral("Text animators"),
         QStringLiteral("Text animator parsing, evaluation, fallback and GPU application."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Animation"), QStringLiteral("Animation evaluation"),
         QStringLiteral("Keyframe evaluation, interpolation, temporal sampling and animation transitions."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Transitions"), QStringLiteral("Transitions"),
         QStringLiteral("Layer and title transition setup and evaluation."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Ticker"), QStringLiteral("Ticker playback"),
         QStringLiteral("Ticker pause, resume, stop, cue gates and adaptive auto-pause."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("LiveCue"), QStringLiteral("Live text cues"),
         QStringLiteral("Live-cue state, payload and render lifecycle."), QStringLiteral("Title model and animation"), true},
        {QStringLiteral("Playlist"), QStringLiteral("Playlist"),
         QStringLiteral("Playlist transitions, timing and cue selection."), QStringLiteral("Title model and animation"), true},

        {QStringLiteral("GpuPipeline"), QStringLiteral("GPU compositor"),
         QStringLiteral("GPU graph updates, frame publication and draw failures."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Shadows"), QStringLiteral("3D shadows"),
         QStringLiteral("Shadow-map allocation, compilation, caster/receiver binding and recovery."), QStringLiteral("Rendering"), true},
        {QStringLiteral("RenderDiagnostics"), QStringLiteral("Correlated render diagnostics"),
         QStringLiteral("Editor transport, canvas scheduling, GPU-session state, depth-run and extrusion diagnostics."), QStringLiteral("Rendering"), false},
        {QStringLiteral("GpuText"), QStringLiteral("GPU text"),
         QStringLiteral("Glyph layout, atlas rendering and text-raster readiness."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Effects"), QStringLiteral("Effects"),
         QStringLiteral("Effect compilation, passes and effect-stack changes."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Extensions"), QStringLiteral("Effect extensions"),
         QStringLiteral("Extension discovery, validation and shader loading."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Masks"), QStringLiteral("Layer masks and mattes"),
         QStringLiteral("Layer masks, track mattes and internal mask composition."), QStringLiteral("Rendering"), true},
        {QStringLiteral("Performance"), QStringLiteral("Performance (verbose)"),
         QStringLiteral("Render timing, resource and high-frequency diagnostics."), QStringLiteral("Rendering"), false},

        {QStringLiteral("Cache"), QStringLiteral("Cache state"),
         QStringLiteral("General frame-cache state, invalidation and diagnostics."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("CacheQueue"), QStringLiteral("Cache queue"),
         QStringLiteral("Prerender queue scheduling, cancellation and retries."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("CachePlayback"), QStringLiteral("Cache playback (verbose)"),
         QStringLiteral("Per-frame cache lookup and presentation decisions."), QStringLiteral("Cache and media"), false},
        {QStringLiteral("RamCache"), QStringLiteral("RAM/GPU cache"),
         QStringLiteral("GPU/RAM cache publication, eviction and memory use."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("DiskCache"), QStringLiteral("Disk cache"),
         QStringLiteral("Disk-cache restore, compression, writes and failures."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("Prerender"), QStringLiteral("Prerender pipeline"),
         QStringLiteral("GPU readback submission, resolution and frame publication."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("Assets"), QStringLiteral("Assets and media"),
         QStringLiteral("Images, fonts, video, media decode and external asset loading."), QStringLiteral("Cache and media"), true},
        {QStringLiteral("ExternalData"), QStringLiteral("External data"),
         QStringLiteral("Provider lifecycle, refreshes, parsing, bindings, table mapping and render-queue diagnostics."), QStringLiteral("Cache and media"), true},
    };

    return categories;
}

bool category_default_enabled(const QString &key)
{
    for (const auto &category : all_categories()) {
        if (category.key.compare(key, Qt::CaseInsensitive) == 0)
            return category.default_enabled;
    }
    return true;
}

QString normalized_category(const char *category)
{
    const QString clean = QString::fromUtf8(category ? category : "General").trimmed();
    return clean.isEmpty() ? QStringLiteral("General") : clean;
}

QString ensure_session_path_locked()
{
    if (!g_session_started) {
        g_session_stamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
        g_session_started = true;
    }
    if (g_session_file_path.isEmpty()) {
        const QString directory = TitlePreferences::logging_directory();
        QDir().mkpath(directory);
        g_session_file_path = QDir(directory).filePath(
            QStringLiteral("flux-motion_%1.log").arg(g_session_stamp));
    }
    return g_session_file_path;
}

fxm::LogLevel shared_log_level(TitleLogLevel level)
{
    switch (level) {
    case TitleLogLevel::Error:
        return fxm::LogLevel::Error;
    case TitleLogLevel::Warning:
        return fxm::LogLevel::Warning;
    case TitleLogLevel::Info:
        return fxm::LogLevel::Info;
    case TitleLogLevel::Debug:
        return fxm::LogLevel::Debug;
    case TitleLogLevel::Trace:
        return fxm::LogLevel::Trace;
    case TitleLogLevel::Off:
    default:
        return fxm::LogLevel::Info;
    }
}

const std::vector<std::string> &category_keys()
{
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(all_categories().size()));
        for (const auto &category : all_categories())
            result.push_back(category.key.toStdString());
        return result;
    }();
    return keys;
}

int category_index(const char *category)
{
    const std::string_view needle =
        (category && *category) ? std::string_view(category)
                                : std::string_view("General");
    const auto &keys = category_keys();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        if (keys[index] == needle)
            return static_cast<int>(index);
    }
    return -1;
}

void close_log_file_locked()
{
    if (g_log_file.isOpen()) {
        if (!g_pending_log_data.isEmpty()) {
            g_log_file.write(g_pending_log_data);
            g_pending_log_data.clear();
        }
        g_log_file.flush();
        g_log_file.close();
    }
    g_open_log_path.clear();
    g_log_flush_clock.invalidate();
}

bool ensure_log_file_open_locked(const QString &path)
{
    if (g_log_file.isOpen() && g_open_log_path == path)
        return true;
    close_log_file_locked();
    QDir().mkpath(QFileInfo(path).absolutePath());
    g_log_file.setFileName(path);
    if (!g_log_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;
    g_open_log_path = path;
    g_log_flush_clock.start();
    return true;
}

void flush_log_buffer_locked(bool force_file_flush)
{
    if (!g_log_file.isOpen() || g_pending_log_data.isEmpty()) {
        if (force_file_flush && g_log_file.isOpen())
            g_log_file.flush();
        return;
    }
    g_log_file.write(g_pending_log_data);
    g_pending_log_data.clear();
    if (force_file_flush)
        g_log_file.flush();
    if (g_log_flush_clock.isValid())
        g_log_flush_clock.restart();
    else
        g_log_flush_clock.start();
}

void append_line_locked(const QString &path, const QString &line,
                        bool force_flush = false)
{
    if (!ensure_log_file_open_locked(path))
        return;
    g_pending_log_data.append(line.toUtf8());
    g_pending_log_data.append('\n');
    const bool interval_elapsed = !g_log_flush_clock.isValid() ||
        g_log_flush_clock.elapsed() >= kLogBufferFlushIntervalMs;
    if (force_flush || g_pending_log_data.size() >= kLogBufferFlushBytes ||
        interval_elapsed)
        flush_log_buffer_locked(force_flush);
}

} // namespace

namespace TitleLogger {

QString levelName(TitleLogLevel level)
{
    switch (level) {
    case TitleLogLevel::Error:
        return QStringLiteral("ERROR");
    case TitleLogLevel::Warning:
        return QStringLiteral("WARN");
    case TitleLogLevel::Info:
        return QStringLiteral("INFO");
    case TitleLogLevel::Debug:
        return QStringLiteral("DEBUG");
    case TitleLogLevel::Trace:
        return QStringLiteral("TRACE");
    case TitleLogLevel::Off:
    default:
        return QStringLiteral("OFF");
    }
}

void refreshConfiguration()
{
    QMutexLocker lock(&g_config_mutex);
    const bool enabled = TitlePreferences::logging_enabled();
    const int level = TitlePreferences::logging_level();
    const bool mirror = TitlePreferences::logging_mirror_to_obs();
    uint64_t mask = 0;
    const auto &categories = all_categories();
    const int count = std::min(static_cast<int>(categories.size()), 64);
    for (int index = 0; index < count; ++index) {
        const auto &category = categories[index];
        if (TitlePreferences::logging_category_enabled(
                category.key, category.default_enabled))
            mask |= (uint64_t{1} << static_cast<unsigned>(index));
    }
    g_logging_level.store(level, std::memory_order_release);
    g_logging_mirror_to_obs.store(mirror, std::memory_order_release);
    g_enabled_category_mask.store(mask, std::memory_order_release);
    g_logging_enabled.store(enabled, std::memory_order_release);
    g_configuration_initialized.store(true, std::memory_order_release);
}

void startSession()
{
    refreshConfiguration();
    QMutexLocker lock(&g_log_mutex);
    ensure_session_path_locked();
    if (g_session_start_line_written)
        return;
    g_session_start_line_written = true;
    if (g_logging_enabled.load(std::memory_order_acquire)) {
        append_line_locked(g_session_file_path,
            QStringLiteral("%1 [INFO] [Plugin] Logging session started: %2")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                     g_session_file_path), true);
    }
}

void endSession()
{
    QMutexLocker lock(&g_log_mutex);
    if (!g_session_started)
        return;
    if (g_logging_enabled.load(std::memory_order_acquire) &&
        !g_session_file_path.isEmpty()) {
        append_line_locked(g_session_file_path,
            QStringLiteral("%1 [INFO] [Plugin] Logging session ended")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs)),
            true);
    }
    flush_log_buffer_locked(true);
    close_log_file_locked();
    g_session_started = false;
    g_session_start_line_written = false;
    g_session_file_path.clear();
    g_session_stamp.clear();
}

QString currentSessionFilePath()
{
    QMutexLocker lock(&g_log_mutex);
    const QString path = ensure_session_path_locked();
    flush_log_buffer_locked(true);
    return path;
}

bool relocateCurrentSession(const QString &directory)
{
    const QString requested_directory = directory.trimmed();
    if (requested_directory.isEmpty())
        return false;
    const QString clean_directory = QDir::cleanPath(requested_directory);

    QMutexLocker lock(&g_log_mutex);
    const QString old_path = ensure_session_path_locked();
    if (!QDir().mkpath(clean_directory))
        return false;
    const QString new_path = QDir(clean_directory).filePath(QFileInfo(old_path).fileName());
    if (QFileInfo(old_path).absoluteFilePath() == QFileInfo(new_path).absoluteFilePath())
        return true;

    flush_log_buffer_locked(true);
    close_log_file_locked();
    bool moved = true;
    if (QFile::exists(old_path)) {
        QFile::remove(new_path);
        moved = QFile::rename(old_path, new_path);
        if (!moved) {
            moved = QFile::copy(old_path, new_path);
            if (moved)
                QFile::remove(old_path);
        }
    }
    if (moved)
        g_session_file_path = new_path;
    return moved;
}

QVector<TitleLogCategory> categories()
{
    return all_categories();
}

bool categoryEnabled(const QString &category)
{
    if (!g_configuration_initialized.load(std::memory_order_acquire))
        refreshConfiguration();
    const QByteArray utf8 = category.trimmed().isEmpty()
        ? QByteArrayLiteral("General") : category.trimmed().toUtf8();
    const int index = category_index(utf8.constData());
    if (index < 0 || index >= 64)
        return TitlePreferences::logging_category_enabled(
            category, category_default_enabled(category));
    const uint64_t mask = g_enabled_category_mask.load(std::memory_order_acquire);
    return (mask & (uint64_t{1} << static_cast<unsigned>(index))) != 0;
}

bool wouldLog(TitleLogLevel level, const char *category)
{
    if (level == TitleLogLevel::Off)
        return false;
    if (!g_configuration_initialized.load(std::memory_order_acquire))
        refreshConfiguration();
    if (!g_logging_enabled.load(std::memory_order_acquire))
        return false;
    if (static_cast<int>(level) >
        g_logging_level.load(std::memory_order_acquire))
        return false;
    const int index = category_index(category);
    if (index < 0 || index >= 64)
        return true;
    const uint64_t mask = g_enabled_category_mask.load(std::memory_order_acquire);
    return (mask & (uint64_t{1} << static_cast<unsigned>(index))) != 0;
}

void logPrepared(TitleLogLevel level, const char *category,
                 const QString &message)
{
    const QString clean_category = normalized_category(category);
    const QString line = QStringLiteral("%1 [%2] [%3] %4")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(level), clean_category, message);

    {
        QMutexLocker lock(&g_log_mutex);
        const bool force_flush = level == TitleLogLevel::Error ||
                                 level == TitleLogLevel::Warning;
        append_line_locked(ensure_session_path_locked(), line, force_flush);
    }

    if (g_logging_mirror_to_obs.load(std::memory_order_acquire))
        fxm::log(shared_log_level(level), clean_category.toStdString(),
                 line.toStdString());
}

void log(TitleLogLevel level, const char *category, const QString &message)
{
    if (!wouldLog(level, category))
        return;
    logPrepared(level, category, message);
}

void error(const char *category, const QString &message)
{
    log(TitleLogLevel::Error, category, message);
}

void warning(const char *category, const QString &message)
{
    log(TitleLogLevel::Warning, category, message);
}

void info(const char *category, const QString &message)
{
    log(TitleLogLevel::Info, category, message);
}

void debug(const char *category, const QString &message)
{
    log(TitleLogLevel::Debug, category, message);
}

void trace(const char *category, const QString &message)
{
    log(TitleLogLevel::Trace, category, message);
}

} // namespace TitleLogger
