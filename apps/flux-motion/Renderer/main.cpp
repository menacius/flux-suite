#include "standalone-editor-host.h"
#include "standalone-obs-graphics-runtime.h"

#include "audio-interfaces.h"
#include "cache-manager.h"
#include "editor-host-interfaces.h"
#include "effect-extension-catalog.h"
#include "effect-runtime.h"
#include "title-audio-runtime.h"
#include "title-assets.h"
#include "title-data.h"
#include "title-localization.h"
#include "title-preview-renderer.h"
#include "obs-title-preview-renderer.h"
#include "title-source.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>

#include <util/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#if defined(Q_OS_WIN)
#include <fcntl.h>
#include <io.h>
#endif

namespace {

void renderer_obs_log_handler(int, const char *message, va_list arguments, void *)
{
    if (!message)
        return;
    std::vfprintf(stderr, message, arguments);
    std::fputc('\n', stderr);
}

class CapturingAudioBackend final : public fxm::audio::IAudioBackend {
public:
    CapturingAudioBackend(std::uint32_t sample_rate, std::uint64_t target_frames)
        : sample_rate_(sample_rate), target_frames_(target_frames) {}

    std::uint32_t sample_rate() const noexcept override { return sample_rate_; }

    bool submit(const fxm::audio::AudioPacketView &packet,
                std::string *error) override
    {
        if (error)
            error->clear();
        if (!packet.valid() || packet.sample_rate != sample_rate_ ||
            packet.channel_layout != fxm::audio::AudioChannelLayout::Stereo) {
            if (error)
                *error = "Unexpected Flux Motion audio packet format";
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t already = pcm_.size() / 2;
        const std::uint32_t accepted = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(packet.frames,
                                    target_frames_ > already
                                        ? target_frames_ - already : 0));
        pcm_.reserve(pcm_.size() + static_cast<std::size_t>(accepted) * 2);
        for (std::uint32_t frame = 0; frame < accepted; ++frame) {
            pcm_.push_back(packet.planes[0][frame]);
            pcm_.push_back(packet.planes[1][frame]);
        }
        ready_cv_.notify_all();
        return true;
    }

    bool wait_until_ready(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return ready_cv_.wait_for(lock, timeout, [&] {
            return pcm_.size() / 2 >= target_frames_;
        });
    }

    std::vector<float> pcm() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return pcm_;
    }

private:
    std::uint32_t sample_rate_ = 48000;
    std::uint64_t target_frames_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable ready_cv_;
    std::vector<float> pcm_;
};

bool title_has_audio(const Title &title)
{
    return std::any_of(title.layers.begin(), title.layers.end(),
                       [](const auto &layer) {
                           return layer && layer->type == LayerType::Audio;
                       });
}

bool write_float_wav(const QString &path, std::uint32_t sample_rate,
                     const std::vector<float> &pcm, QString *error)
{
    if (pcm.size() > std::numeric_limits<quint32>::max() / sizeof(float)) {
        if (error)
            *error = QStringLiteral("Audio output is too large for WAV");
        return false;
    }
    QFile output(path);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = output.errorString();
        return false;
    }
    const quint32 data_bytes = static_cast<quint32>(pcm.size() * sizeof(float));
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + data_bytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(3) << quint16(2)
           << quint32(sample_rate) << quint32(sample_rate * 8)
           << quint16(8) << quint16(32);
    stream.writeRawData("data", 4);
    stream << data_bytes;
    for (float sample : pcm)
        stream << sample;
    output.close();
    if (stream.status() != QDataStream::Ok) {
        if (error)
            *error = QStringLiteral("Could not finish WAV output");
        return false;
    }
    return true;
}

int render_audio(const std::shared_ptr<Title> &title, const QString &output_path,
                 qint64 start_ms, qint64 duration_ms,
                 std::uint32_t requested_sample_rate)
{
    if (!title)
        return 30;
    const std::uint32_t sample_rate = std::clamp<std::uint32_t>(
        requested_sample_rate > 0 ? requested_sample_rate : 48000,
        8000, 192000);
    const double start = std::clamp(start_ms / 1000.0, 0.0,
                                    std::max(0.0, title->duration));
    const double available = std::max(0.0, title->duration - start);
    const double duration = duration_ms > 0
        ? std::min(duration_ms / 1000.0, available) : available;
    const std::uint64_t target_frames = static_cast<std::uint64_t>(
        std::ceil(duration * sample_rate - 0.0000001));

    std::vector<float> pcm;
    if (title_has_audio(*title) && target_frames > 0) {
        auto backend = std::make_unique<CapturingAudioBackend>(
            sample_rate, target_frames);
        CapturingAudioBackend *capture = backend.get();
        fxm::audio::SourceAudioRuntime runtime(
            std::move(backend), "Flux Encoder OBS-compatible audio", false);
        runtime.bind_title(title, 0);
        runtime.transport(start, true, true, true, false, 1.0);
        runtime.pump();
        const auto timeout = std::chrono::milliseconds(
            std::max<qint64>(120000, duration_ms * 2 + 60000));
        if (!capture->wait_until_ready(timeout)) {
            runtime.stop(false);
            std::fprintf(stderr, "Flux Motion audio capture timed out.\n");
            return 31;
        }
        runtime.stop(false);
        pcm = capture->pcm();
    } else {
        pcm.assign(static_cast<std::size_t>(target_frames) * 2, 0.0f);
    }
    pcm.resize(static_cast<std::size_t>(target_frames) * 2, 0.0f);
    QString error;
    if (!write_float_wav(output_path, sample_rate, pcm, &error)) {
        std::fprintf(stderr, "Flux Motion audio output failed: %s\n",
                     error.toUtf8().constData());
        return 32;
    }
    std::fprintf(stderr, "render_audio_frames=%llu sample_rate=%u\n",
                 static_cast<unsigned long long>(target_frames), sample_rate);
    return 0;
}

int render_video(const std::shared_ptr<Title> &title, double requested_fps,
                 qint64 start_ms, qint64 duration_ms)
{
    if (!title)
        return 20;
    const double fps = std::clamp(
        requested_fps > 0.0 ? requested_fps : title->frame_rate, 1.0, 240.0);
    const double start = std::clamp(start_ms / 1000.0, 0.0,
                                    std::max(0.0, title->duration));
    const double available = std::max(0.0, title->duration - start);
    const double duration = duration_ms > 0
        ? std::min(duration_ms / 1000.0, available) : available;
    const qint64 frame_count = std::max<qint64>(
        1, static_cast<qint64>(std::ceil(duration * fps - 0.0000001)));
    const int width = std::max(1, title->width);
    const int height = std::max(1, title->height);

    std::unique_ptr<TitleGpuRenderSession,
                    decltype(&title_gpu_render_session_destroy)> session(
        title_gpu_render_session_create(), title_gpu_render_session_destroy);
    if (!session) {
        std::fprintf(stderr, "Could not create Flux Motion GPU render session.\n");
        return 22;
    }
    title_gpu_render_session_set_render_request(
        session.get(), TitleGpuRenderRequest{
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), 1.0 / fps});
    title_gpu_render_session_set_realtime_output(session.get(), false);
    title_gpu_render_session_set_editor_video_decode_client(session.get(), true);
    const auto frame_at = [&](double project_time) {
        title_gpu_render_session_update(session.get(), *title, project_time, 1);
        return title_gpu_render_session_readback(session.get());
    };
    (void)frame_at(std::max(0.0, start - 1.0 / fps));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);

#if defined(Q_OS_WIN)
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        return 21;
#endif
    for (qint64 index = 0; index < frame_count; ++index) {
        const double time = std::min(
            title->duration, start + static_cast<double>(index) / fps);
        QImage frame;
        const auto frame_deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(90);
        while (std::chrono::steady_clock::now() < frame_deadline) {
            /* Do not contend with the background shader compiler for libobs'
             * global graphics context. This is particularly important for
             * D3D11, where repeatedly entering readback while a first-use
             * effect is compiling can turn startup into a long busy loop. */
            TitleGpuShaderCompileStatus compile_status;
            if (title_gpu_render_session_shader_compile_status(
                    session.get(), compile_status) &&
                (compile_status.active || compile_status.queued)) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                QThread::msleep(5);
                continue;
            }
            frame = frame_at(time);
            TitleGpuRenderDiagnostics diagnostics;
            const bool have_diagnostics =
                title_gpu_render_session_get_diagnostics(
                    session.get(), diagnostics);
            compile_status = {};
            const bool compiling =
                title_gpu_render_session_shader_compile_status(
                    session.get(), compile_status) &&
                (compile_status.active || compile_status.queued);
            const double frame_tolerance =
                std::max(1.0e-6, 0.5 / fps);
            const bool published_requested_frame = have_diagnostics &&
                diagnostics.valid && diagnostics.has_published_frame &&
                !diagnostics.last_draw_deferred &&
                !diagnostics.frame_dirty &&
                !diagnostics.state_transaction_pending && !compiling &&
                diagnostics.published_model_revision == 1 &&
                std::abs(diagnostics.last_published_time - time) <=
                    frame_tolerance;
            /* A readback target has its final dimensions as soon as it is
             * allocated. During asynchronous first-use shader compilation its
             * contents may still be the cleared (black/transparent) surface.
             * Do not export that placeholder, or a stale prior-time surface. */
            if (!frame.isNull() && frame.size() == QSize(width, height) &&
                published_requested_frame)
                break;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(5);
        }
        if (frame.isNull() || frame.size() != QSize(width, height))
            return 22;
        if (frame.format() != QImage::Format_ARGB32_Premultiplied)
            frame = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const std::size_t row_bytes = static_cast<std::size_t>(width) * 4;
        for (int y = 0; y < height; ++y) {
            if (std::fwrite(frame.constScanLine(y), 1, row_bytes, stdout) != row_bytes)
                return 23;
        }
        if ((index % 10) == 0) {
            std::fprintf(stderr, "render_frame=%lld/%lld\n",
                         static_cast<long long>(index + 1),
                         static_cast<long long>(frame_count));
        }
    }
    return std::fflush(stdout) == 0 ? 0 : 24;
}

void print_inspection(const Title &title)
{
    const QJsonObject result{
        {QStringLiteral("id"), QString::fromStdString(title.id)},
        {QStringLiteral("name"), QString::fromStdString(title.name)},
        {QStringLiteral("width"), title.width},
        {QStringLiteral("height"), title.height},
        {QStringLiteral("frameRate"), title.frame_rate},
        {QStringLiteral("durationMs"), qRound64(title.duration * 1000.0)},
        {QStringLiteral("hasAudio"), title_has_audio(title)}};
    const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
    std::fwrite(json.constData(), 1, static_cast<std::size_t>(json.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("FluxMotion"));
    QCoreApplication::setApplicationName(QStringLiteral("Flux Motion Renderer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(FXM_VERSION_LABEL));
    base_set_log_handler(renderer_obs_log_handler, nullptr);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption video_option(
        QStringLiteral("flux-encoder-render-raw"));
    const QCommandLineOption audio_option(
        QStringLiteral("flux-encoder-render-audio"));
    const QCommandLineOption inspect_option(
        QStringLiteral("inspect-project-file"), QString(), QStringLiteral("path"));
    const QCommandLineOption project_file_option(
        QStringLiteral("project-file"), QString(), QStringLiteral("path"));
    const QCommandLineOption title_option(
        QStringLiteral("title-id"), QString(), QStringLiteral("id"));
    const QCommandLineOption config_option(
        QStringLiteral("config-root"), QString(), QStringLiteral("path"));
    const QCommandLineOption scope_option(
        QStringLiteral("project-scope"), QString(), QStringLiteral("name"));
    const QCommandLineOption data_option(
        QStringLiteral("data-root"), QString(), QStringLiteral("path"));
    const QCommandLineOption obs_bin_option(
        QStringLiteral("obs-bin-root"), QString(), QStringLiteral("path"));
    const QCommandLineOption fps_option(
        QStringLiteral("render-frame-rate"), QString(), QStringLiteral("fps"));
    const QCommandLineOption start_option(
        QStringLiteral("render-start-ms"), QString(), QStringLiteral("ms"),
        QStringLiteral("0"));
    const QCommandLineOption duration_option(
        QStringLiteral("render-duration-ms"), QString(), QStringLiteral("ms"),
        QStringLiteral("0"));
    const QCommandLineOption audio_output_option(
        QStringLiteral("render-audio-output"), QString(), QStringLiteral("path"));
    const QCommandLineOption sample_rate_option(
        QStringLiteral("render-sample-rate"), QString(), QStringLiteral("hz"),
        QStringLiteral("48000"));
    parser.addOptions({video_option, audio_option, inspect_option,
                       project_file_option, title_option, config_option,
                       scope_option, data_option, obs_bin_option, fps_option,
                       start_option, duration_option, audio_output_option,
                       sample_rate_option});
    parser.process(application);

    const bool inspect = parser.isSet(inspect_option);
    const bool render_video_mode = parser.isSet(video_option);
    const bool render_audio_mode = parser.isSet(audio_option);
    if (!inspect && !render_video_mode && !render_audio_mode)
        return 2;

    QString project_file = inspect ? parser.value(inspect_option)
                                   : parser.value(project_file_option);
    std::unique_ptr<QTemporaryDir> isolated_store;
    QString config_root = parser.value(config_option);
    QString project_scope = parser.value(scope_option);
    if (!project_file.isEmpty()) {
        isolated_store = std::make_unique<QTemporaryDir>();
        if (!isolated_store->isValid())
            return 3;
        config_root = isolated_store->path();
        project_scope = QStringLiteral("Flux Encoder Imported Project");
    }

    const QString standalone_data_root =
        fxm::editor::resolve_standalone_data_root(parser.value(data_option));
    fxm::editor::StandaloneEditorHost editor_host(nullptr);
    fxm::editor::StandaloneAssetPathProvider asset_paths(
        standalone_data_root.toStdString());
    fxm::editor::StandaloneHostContextProvider host_context(
        config_root.toStdString(), project_scope.toStdString());
    fxm::editor::StandaloneFrameRateProvider frame_rate;
    fxm::editor::StandaloneLogger logger;
    fxm::editor::StandaloneTranslationProvider translations(
        standalone_data_root.toStdString());
    fxm::set_logger(&logger);
    fxm::set_host_context_provider(&host_context);
    fxm::set_asset_path_provider(&asset_paths);
    fxm_set_asset_path_provider(&asset_paths);
    fxm_set_translation_provider(&translations);
    fxm::editor_host::set_editor_host(&editor_host);
    fxm::set_frame_rate_provider(&frame_rate);

    TitleDataStore::instance().load();
    std::shared_ptr<Title> title;
    if (!project_file.isEmpty()) {
        std::string import_error;
        title = TitleDataStore::instance().import_title(
            project_file.toStdString(), &import_error, nullptr);
        if (!title) {
            std::fprintf(stderr, "Flux Motion import failed: %s\n",
                         import_error.c_str());
        }
    } else {
        title = TitleDataStore::instance().get_title(
            parser.value(title_option).toStdString());
    }
    if (!title) {
        TitleDataStore::instance().shutdownSaveWorker();
        return 4;
    }
    if (inspect) {
        print_inspection(*title);
        TitleDataStore::instance().shutdownSaveWorker();
        return 0;
    }

    bool start_ok = false;
    const qint64 start_ms = parser.value(start_option).toLongLong(&start_ok);
    bool duration_ok = false;
    const qint64 duration_ms = parser.value(duration_option).toLongLong(&duration_ok);
    int result = 0;
    if (render_audio_mode) {
        bool rate_ok = false;
        const auto rate = parser.value(sample_rate_option).toUInt(&rate_ok);
        result = render_audio(title, parser.value(audio_output_option),
                              start_ok ? start_ms : 0,
                              duration_ok ? duration_ms : 0,
                              rate_ok ? rate : 48000);
    } else {
        auto graphics = std::make_unique<fxm::editor::StandaloneObsGraphicsRuntime>(
            parser.value(obs_bin_option));
        if (!graphics->ready()) {
            std::fprintf(stderr, "%s\n", graphics->error().toUtf8().constData());
            result = 5;
        } else {
            fxm::rendering::set_title_preview_renderer(
                &fxm::obs_plugin::obs_title_preview_renderer());
            FxmEffectExtensionCatalog::instance().reload();
            QFontDatabase fonts;
            (void)fonts.families();
            (void)builtin_effect_descriptors();
            bool fps_ok = false;
            const double fps = parser.value(fps_option).toDouble(&fps_ok);
            result = render_video(title, fps_ok ? fps : 0.0,
                                  start_ok ? start_ms : 0,
                                  duration_ok ? duration_ms : 0);
            release_title_gpu_render_resources();
            CacheManager::instance().shutdownWorker();
            FxmEffectExtensionCatalog::instance().shutdown();
            fxm::rendering::set_title_preview_renderer(nullptr);
        }
    }

    TitleDataStore::instance().shutdownSaveWorker();
    fxm::editor_host::set_editor_host(nullptr);
    fxm::set_asset_path_provider(nullptr);
    fxm::set_host_context_provider(nullptr);
    fxm::set_logger(nullptr);
    return result;
}
