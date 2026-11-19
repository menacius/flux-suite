#include "standalone-obs-graphics-runtime.h"

#include "title-source.h"

#include <obs.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QVersionNumber>

#include <algorithm>

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <dlfcn.h>
#include <obs-nix-platform.h>
#include <QtGui/qguiapplication_platform.h>
#endif

namespace fxm::editor {
namespace {

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
void configure_obs_nix_platform()
{
    auto *application = qobject_cast<QGuiApplication *>(
        QCoreApplication::instance());
    if (!application)
        return;

    const QString platform_name = QGuiApplication::platformName();
#if QT_CONFIG(xcb)
    if (platform_name == QStringLiteral("xcb")) {
        if (auto *native = application->nativeInterface<
                QNativeInterface::QX11Application>()) {
            if (native->display()) {
                obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL);
                obs_set_nix_platform_display(native->display());
            }
        }
        return;
    }
#endif
#if QT_CONFIG(wayland)
    if (platform_name.contains(QStringLiteral("wayland"))) {
        if (auto *native = application->nativeInterface<
                QNativeInterface::QWaylandApplication>()) {
            if (native->display()) {
                obs_set_nix_platform(OBS_NIX_PLATFORM_WAYLAND);
                obs_set_nix_platform_display(native->display());
            }
        }
    }
#endif
    /* Offscreen/minimal render workers intentionally have no native display.
     * libobs can still create a headless graphics device; CanvasPreview will
     * use its backing-store/readback presentation path in that case. */
}
#endif

QString find_graphics_module_in_directory(const QString &directory_path,
                                          const QString &module_name)
{
    const QDir directory(directory_path);
    const QFileInfo unversioned(directory.filePath(module_name));
    if (unversioned.exists() && unversioned.isFile())
        return unversioned.absoluteFilePath();

#if !defined(Q_OS_WIN)
    /* Ubuntu's libobs runtime package may only ship the ABI-versioned module
     * (for example libobs-opengl.so.30).  The unversioned symlink belongs to
     * the development package and must not be required at runtime. */
    const QString version_prefix = module_name + QLatin1Char('.');
    const QFileInfoList versioned_modules = directory.entryInfoList(
        {version_prefix + QLatin1Char('*')},
        QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name);

    QString best_path;
    QVersionNumber best_version;
    for (const QFileInfo &candidate : versioned_modules) {
        if (!candidate.exists() || !candidate.isFile())
            continue;

        const QString suffix = candidate.fileName().mid(version_prefix.size());
        qsizetype parsed_characters = 0;
        const QVersionNumber version =
            QVersionNumber::fromString(suffix, &parsed_characters);
        if (version.isNull() || parsed_characters != suffix.size())
            continue;
        if (best_path.isEmpty() ||
            QVersionNumber::compare(version, best_version) > 0) {
            best_version = version;
            best_path = candidate.absoluteFilePath();
        }
    }
    return best_path;
#else
    return {};
#endif
}

QString locate_obs_graphics_module(QString explicit_root)
{
#if defined(Q_OS_WIN)
    const QString module_name = QStringLiteral("libobs-d3d11.dll");
#else
    const QString module_name = QStringLiteral("libobs-opengl.so");
#endif
    QStringList roots;
    if (!explicit_root.trimmed().isEmpty())
        roots.push_back(QFileInfo(explicit_root).absoluteFilePath());
    const QString environment_root =
        qEnvironmentVariable("OBS_STUDIO_BIN_DIR").trimmed();
    if (!environment_root.isEmpty())
        roots.push_back(environment_root);
    roots.push_back(QCoreApplication::applicationDirPath());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    roots.push_back(QLibraryInfo::path(QLibraryInfo::LibrariesPath));
#else
    roots.push_back(QLibraryInfo::location(QLibraryInfo::LibrariesPath));
#endif
#if !defined(Q_OS_WIN)
    const QStringList systemLibraryRoots = {
        QStringLiteral("/usr/lib"), QStringLiteral("/usr/lib64"),
        QStringLiteral("/usr/lib/x86_64-linux-gnu"),
        QStringLiteral("/usr/local/lib"), QStringLiteral("/usr/local/lib64")};
    roots.append(systemLibraryRoots);

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    Dl_info libobsInfo{};
    if (dladdr(reinterpret_cast<const void *>(&obs_get_version), &libobsInfo) != 0 &&
        libobsInfo.dli_fname) {
        roots.push_back(QFileInfo(QString::fromLocal8Bit(libobsInfo.dli_fname))
                            .absolutePath());
    }
#endif

    const QString snapRoot = qEnvironmentVariable("SNAP").trimmed();
    if (!snapRoot.isEmpty()) {
        roots.push_back(QDir(snapRoot).filePath(QStringLiteral("usr/lib")));
        roots.push_back(QDir(snapRoot).filePath(
            QStringLiteral("usr/lib/x86_64-linux-gnu")));
    }
    const QString appDirRoot = qEnvironmentVariable("APPDIR").trimmed();
    if (!appDirRoot.isEmpty())
        roots.push_back(QDir(appDirRoot).filePath(QStringLiteral("usr/lib")));
#endif
#if defined(Q_OS_WIN)
    roots.push_back(QStringLiteral("C:/Program Files/obs-studio/bin/64bit"));
    roots.push_back(QStringLiteral("C:/Program Files (x86)/obs-studio/bin/64bit"));
#endif
    roots.removeDuplicates();
    for (const QString &root : roots) {
        const QStringList candidateDirectories = {
            root,
            QDir(root).filePath(QStringLiteral("obs-plugins")),
            QDir(root).filePath(QStringLiteral("libobs")),
            QDir(root).filePath(QStringLiteral("lib/obs-plugins")),
            QDir(root).filePath(QStringLiteral("lib64/obs-plugins"))};
        for (const QString &directory : candidateDirectories) {
            const QString candidate =
                find_graphics_module_in_directory(directory, module_name);
            if (!candidate.isEmpty())
                return candidate;
        }
    }
    return {};
}

QString locate_obs_data_directory(const QString &graphics_module_path)
{
    QStringList candidates;
    const QDir graphics_dir = QFileInfo(graphics_module_path).absoluteDir();
    candidates.push_back(
        graphics_dir.absoluteFilePath(QStringLiteral("../../data/libobs")));
    candidates.push_back(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral("data/libobs")));
#if !defined(Q_OS_WIN)
    for (const QString &dataRoot : QStandardPaths::standardLocations(
             QStandardPaths::GenericDataLocation)) {
        candidates.push_back(QDir(dataRoot).filePath(QStringLiteral("obs/libobs")));
        candidates.push_back(QDir(dataRoot).filePath(
            QStringLiteral("obs/obs-studio/libobs")));
    }
    const QString snapRoot = qEnvironmentVariable("SNAP").trimmed();
    if (!snapRoot.isEmpty()) {
        candidates.push_back(QDir(snapRoot).filePath(
            QStringLiteral("usr/share/obs/libobs")));
        candidates.push_back(QDir(snapRoot).filePath(
            QStringLiteral("usr/share/obs/obs-studio/libobs")));
    }
    const QString appDirRoot = qEnvironmentVariable("APPDIR").trimmed();
    if (!appDirRoot.isEmpty()) {
        candidates.push_back(QDir(appDirRoot).filePath(
            QStringLiteral("usr/share/obs/libobs")));
        candidates.push_back(QDir(appDirRoot).filePath(
            QStringLiteral("usr/share/obs/obs-studio/libobs")));
    }
#endif
#if defined(Q_OS_WIN)
    candidates.push_back(
        QStringLiteral("C:/Program Files/obs-studio/data/libobs"));
    candidates.push_back(
        QStringLiteral("C:/Program Files (x86)/obs-studio/data/libobs"));
#endif
    candidates.removeDuplicates();
    for (const QString &candidate : candidates) {
        const QDir directory(candidate);
        if (directory.exists(QStringLiteral("default.effect")))
            return directory.absolutePath();
    }
    return {};
}

} // namespace

StandaloneObsGraphicsRuntime::StandaloneObsGraphicsRuntime(
    QString obs_bin_root, std::uint32_t canvas_width,
    std::uint32_t canvas_height, std::uint32_t fps_num,
    std::uint32_t fps_den)
{
    graphics_module_path_ = locate_obs_graphics_module(std::move(obs_bin_root));
    if (graphics_module_path_.isEmpty()) {
        error_ = QStringLiteral(
            "Could not find the OBS graphics module (%1). Install the OBS/libobs "
            "runtime package, pass --obs-bin-root, or set OBS_STUDIO_BIN_DIR to "
            "the directory containing the module.")
                     .arg(
#if defined(Q_OS_WIN)
                         QStringLiteral("libobs-d3d11.dll")
#else
                         QStringLiteral("libobs-opengl.so")
#endif
                     );
        return;
    }

    /* libobs' Windows bootstrap still contains a few install-layout-relative
     * probes. Resolve those against the selected OBS bin directory, then put
     * the Editor's original working directory back before returning. */
    const QString previous_working_directory = QDir::currentPath();
    QDir::setCurrent(QFileInfo(graphics_module_path_).absolutePath());
    const auto restore_working_directory = [&]() {
        QDir::setCurrent(previous_working_directory);
    };

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    configure_obs_nix_platform();
#endif

    if (!obs_startup("en-US", nullptr, nullptr)) {
        error_ = QStringLiteral("obs_startup() failed.");
        restore_working_directory();
        return;
    }
    started_ = true;

    QString obs_data_path = locate_obs_data_directory(
        graphics_module_path_);
    if (obs_data_path.isEmpty()) {
        error_ = QStringLiteral(
            "Could not find the libobs shader data directory.");
        obs_shutdown();
        started_ = false;
        restore_working_directory();
        return;
    }
    if (!obs_data_path.endsWith(QDir::separator()))
        obs_data_path += QDir::separator();
    const QByteArray native_data_path = QDir::toNativeSeparators(
        obs_data_path).toUtf8();
    obs_add_data_path(native_data_path.constData());

    /* Effect includes in the Windows graphics module are resolved relative to
     * the process working directory even when the top-level effect was found
     * through obs_add_data_path(). Use the resolved shader directory while
     * libobs compiles its bootstrap effects so a self-contained bundle does
     * not depend on OBS Studio's ../../data install layout. */
    QDir::setCurrent(obs_data_path);

    const QByteArray module_path = QDir::toNativeSeparators(
        graphics_module_path_).toUtf8();
    obs_video_info video{};
    video.graphics_module = module_path.constData();
    video.adapter = 0;
    video.fps_num = std::max<std::uint32_t>(1, fps_num);
    video.fps_den = std::max<std::uint32_t>(1, fps_den);
    /* This reset exists only to create libobs's graphics device; individual
     * title targets carry their own explicit extent, while the FPS below still
     * supplies the editor's project cadence. Keeping an otherwise unused OBS
     * video surface at the full canvas size makes the libobs video thread
     * continuously process a 1080p frame. That is especially expensive on a
     * Linux software-GL fallback and needlessly consumes GPU time elsewhere. */
#if defined(Q_OS_WIN)
    /* Preserve the established D3D11 host-video extent. Some Windows libobs
     * effect/readback paths derive their viewport from this surface. */
    video.base_width = std::clamp<std::uint32_t>(canvas_width, 2, 16384);
    video.base_height = std::clamp<std::uint32_t>(canvas_height, 2, 16384);
#else
    Q_UNUSED(canvas_width);
    Q_UNUSED(canvas_height);
    /* NV12 output is chroma-subsampled and libobs aligns it to a four-pixel
     * boundary, so 4x4 is the smallest valid bootstrap surface. */
    constexpr std::uint32_t kBootstrapExtent = 4;
    video.base_width = kBootstrapExtent;
    video.base_height = kBootstrapExtent;
#endif
    video.output_width = video.base_width;
    video.output_height = video.base_height;
    video.output_format = VIDEO_FORMAT_NV12;
    video.colorspace = VIDEO_CS_SRGB;
    video.range = VIDEO_RANGE_FULL;
    video.scale_type = OBS_SCALE_BICUBIC;
    /* Use libobs' normal bootstrap format. The Flux renderer still reads BGRA
     * from its own render target; this surface exists only to establish the
     * graphics/video context expected by the shared OBS-plugin compositor. */
    video.gpu_conversion = true;

    const int reset_result = obs_reset_video(&video);
    if (reset_result != OBS_VIDEO_SUCCESS) {
        error_ = QStringLiteral("obs_reset_video() failed with code %1 using %2")
                     .arg(reset_result)
                     .arg(graphics_module_path_);
        obs_shutdown();
        started_ = false;
        restore_working_directory();
        return;
    }
    ready_ = true;
    restore_working_directory();
}

StandaloneObsGraphicsRuntime::~StandaloneObsGraphicsRuntime()
{
    if (!started_)
        return;
    release_title_gpu_render_resources();
    obs_shutdown();
}

} // namespace fxm::editor
