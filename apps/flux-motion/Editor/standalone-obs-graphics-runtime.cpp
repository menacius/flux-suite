#include "standalone-obs-graphics-runtime.h"

#include "title-source.h"

#include <obs.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

namespace fxm::editor {
namespace {

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
#if defined(Q_OS_WIN)
    roots.push_back(QStringLiteral("C:/Program Files/obs-studio/bin/64bit"));
    roots.push_back(QStringLiteral("C:/Program Files (x86)/obs-studio/bin/64bit"));
#endif
    roots.push_back(QCoreApplication::applicationDirPath());
    roots.removeDuplicates();
    for (const QString &root : roots) {
        const QFileInfo candidate(QDir(root).filePath(module_name));
        if (candidate.exists() && candidate.isFile())
            return candidate.absoluteFilePath();
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
            "Could not find the OBS graphics module. Pass --obs-bin-root or "
            "set OBS_STUDIO_BIN_DIR to the OBS bin/64bit directory.");
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
     * title targets still carry their own explicit extent and cadence. libobs
     * validates this bootstrap surface against its normal video constraints. */
    video.base_width = std::clamp<std::uint32_t>(canvas_width, 2, 16384);
    video.base_height = std::clamp<std::uint32_t>(canvas_height, 2, 16384);
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
