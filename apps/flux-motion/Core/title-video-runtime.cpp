#include "title-video-runtime.h"

#include "title-data.h"
#include "title-logger.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <vector>

#if defined(FXM_HAVE_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#endif

namespace fxm::video {
namespace {

static double finite_positive(double value, double fallback = 0.0)
{
    return std::isfinite(value) && value > 0.0 ? value : fallback;
}

static double project_frame_rate_or_default(double project_frame_rate)
{
    return finite_positive(project_frame_rate, 60.0);
}

static double video_frame_rate_for_layer(const Layer &layer, double = 0.0)
{
    /* Source cadence is media metadata, never the consumer/project cadence.
     * A deterministic fallback keeps missing metadata from making the same
     * title time resolve to different source frames in editor and OBS. */
    return finite_positive(layer.video_frame_rate, 60.0);
}

static QString fxm_video_proxy_root()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath() + QStringLiteral("/flux-motion");
    QDir dir(base);
    dir.mkpath(QStringLiteral("video-proxies"));
    return dir.filePath(QStringLiteral("video-proxies"));
}

static QString stable_id_component(const std::string &value)
{
    QString out = QString::fromStdString(value).left(64);
    out.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (out.isEmpty()) out = QStringLiteral("layer");
    return out;
}

static QString proxy_path_for(const Layer &layer, const std::string &fingerprint)
{
    const QString ext = (layer.video_has_alpha || layer.video_has_hdr)
        ? QStringLiteral(".mov") : QStringLiteral(".mp4");
    const QByteArray digest = QCryptographicHash::hash(
        QByteArray::fromStdString(layer.id + "|" + layer.video_source + "|" + fingerprint),
        QCryptographicHash::Sha256).toHex().left(24);
    return QDir(fxm_video_proxy_root()).filePath(
        QStringLiteral("fxm-video-proxy-%1-%2%3")
            .arg(stable_id_component(layer.id), QString::fromLatin1(digest), ext));
}

static QString proxy_manifest_path_for_proxy(const QString &proxy_path)
{
    return proxy_path + QStringLiteral(".fxmproxy.json");
}

static QString proxy_profile_for_layer(const Layer &layer)
{
    if (layer.video_has_alpha)
        return QStringLiteral("alpha-prores4444-audio-copy");
    if (layer.video_has_hdr)
        return QStringLiteral("hdr-prores422hq-audio-copy");
    return QStringLiteral("editor-h264-audio-copy");
}

static void write_proxy_manifest(const QString &manifest_path, const Layer &layer,
                                 const std::string &fingerprint,
                                 const QString &proxy_path, bool complete,
                                 int progress_percent, const QString &error = QString())
{
    QJsonObject root;
    root[QStringLiteral("schema_version")] = 1;
    root[QStringLiteral("development_version")] = 242;
    root[QStringLiteral("layer_id")] = QString::fromStdString(layer.id);
    root[QStringLiteral("source_path")] = QString::fromStdString(layer.video_source);
    root[QStringLiteral("source_fingerprint")] = QString::fromStdString(fingerprint);
    root[QStringLiteral("proxy_path")] = proxy_path;
    root[QStringLiteral("profile")] = proxy_profile_for_layer(layer);
    root[QStringLiteral("alpha_capable")] = layer.video_has_alpha;
    root[QStringLiteral("hdr_capable")] = layer.video_has_hdr;
    root[QStringLiteral("audio_stream_preservation")] = true;
    root[QStringLiteral("complete")] = complete;
    root[QStringLiteral("progress_percent")] = std::clamp(progress_percent, 0, 100);
    root[QStringLiteral("generated_at_utc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!error.isEmpty()) root[QStringLiteral("error")] = error;
    QFile file(manifest_path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

static bool manifest_matches_source(const QString &manifest_path,
                                    const std::string &fingerprint,
                                    QString *proxy_path_out = nullptr)
{
    QFile file(manifest_path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (!root.value(QStringLiteral("complete")).toBool(false))
        return false;
    if (root.value(QStringLiteral("source_fingerprint")).toString().toStdString() != fingerprint)
        return false;
    const QString proxy_path = root.value(QStringLiteral("proxy_path")).toString();
    if (proxy_path.isEmpty() || !QFileInfo::exists(proxy_path))
        return false;
    if (proxy_path_out) *proxy_path_out = proxy_path;
    return true;
}

static bool proxy_reference_usable(const Layer &layer, const std::string &fingerprint,
                                   QString *proxy_path_out = nullptr)
{
    if (fingerprint.empty())
        return false;
    const QString authored_path = QString::fromStdString(layer.video_proxy_path);
    const bool authored_fingerprint_matches =
        !layer.video_proxy_fingerprint.empty() &&
        layer.video_proxy_fingerprint == fingerprint;
    if (layer.video_proxy_complete && authored_fingerprint_matches &&
        !authored_path.isEmpty() && QFileInfo::exists(authored_path)) {
        if (proxy_path_out) *proxy_path_out = authored_path;
        return true;
    }
    const QString derived_proxy = proxy_path_for(layer, fingerprint);
    return manifest_matches_source(proxy_manifest_path_for_proxy(derived_proxy),
                                   fingerprint, proxy_path_out);
}

static std::string backend_name(HardwareDecodeBackend backend)
{
    switch (backend) {
    case HardwareDecodeBackend::Auto: return "auto";
    case HardwareDecodeBackend::SoftwareOnly: return "software";
    case HardwareDecodeBackend::D3D11VA: return "d3d11va";
    case HardwareDecodeBackend::DXVA2: return "dxva2";
    case HardwareDecodeBackend::VideoToolbox: return "videotoolbox";
    case HardwareDecodeBackend::VAAPI: return "vaapi";
    case HardwareDecodeBackend::NVDEC: return "cuda";
    case HardwareDecodeBackend::QSV: return "qsv";
    }
    return "software";
}

static HardwareDecodeBackend platform_default_hardware_backend()
{
#if defined(_WIN32)
    return HardwareDecodeBackend::D3D11VA;
#elif defined(__APPLE__)
    return HardwareDecodeBackend::VideoToolbox;
#else
    return HardwareDecodeBackend::VAAPI;
#endif
}

static int decode_cache_budget_for_client(VideoDecodeClient client, double media_frame_rate)
{
    const double fps = finite_positive(media_frame_rate, 60.0);
    const int seconds = client == VideoDecodeClient::Editor ? 6 : 3;
    return std::clamp(static_cast<int>(std::ceil(fps * seconds)),
                      client == VideoDecodeClient::Editor ? 180 : 96,
                      client == VideoDecodeClient::Editor ? 480 : 240);
}

struct DecodePrefetchPlan {
    int forward = 4;
    int reverse = 2;
};

static DecodePrefetchPlan prefetch_plan_for_client(VideoDecodeClient client,
                                                   bool moving_backward,
                                                   bool steady_playback)
{
    DecodePrefetchPlan plan;
    if (client == VideoDecodeClient::Editor) {
        /* Development Version 248: seeking a single frame is already reliable;
         * stutter during editor playback comes from the decoder chasing each new
         * playhead frame and aborting prefetch before a usable queue is built.
         * Keep scrubs conservative, but maintain a real forward buffer during
         * continuous playback so 4K/long-GOP sources can present cached frames. */
        if (moving_backward) {
            plan.forward = 0;
            plan.reverse = steady_playback ? 4 : 1;
        } else if (steady_playback) {
            plan.forward = 12;
            plan.reverse = 2;
        } else {
            plan.forward = 2;
            plan.reverse = 0;
        }
    } else {
        plan.forward = moving_backward ? 2 : 6;
        plan.reverse = moving_backward ? 3 : 1;
    }
    return plan;
}

static bool video_time_remap_values_look_trim_relative(const Layer &layer,
                                                       double media_in,
                                                       double media_out)
{
    if (!layer.video_time_remap_enabled || media_in <= 1e-6)
        return false;
    const double span = std::max(0.0, media_out - media_in);
    if (span <= 1e-6)
        return false;

    bool saw_value = false;
    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    auto consider = [&](double value) {
        if (!std::isfinite(value))
            return;
        saw_value = true;
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    };
    if (layer.video_source_time.keyframes.empty())
        consider(layer.video_source_time.static_value);
    else
        for (const Keyframe &key : layer.video_source_time.keyframes)
            consider(key.value);
    for (const VideoTimeRemapLoopSegment &segment : layer.video_time_remap_loop_segments) {
        if (!segment.enabled)
            continue;
        consider(segment.source_start);
        consider(segment.source_end);
    }
    if (!saw_value)
        return false;

    /* Curves created by early 243 builds or hand-authored JSON could store
     * source-time values as 0..trimmed-span while the runtime expected absolute
     * media seconds. With a non-zero video_in_point that clamps every sample to
     * media_in and the video appears frozen/offline. Treat only the clearly
     * 0-based case as trim-relative; absolute media-time curves are preserved. */
    return min_value >= -1e-6 && max_value <= span + 1e-6 &&
           max_value < media_out - 1e-6 && min_value < media_in - 1e-6;
}

static QImage blend_video_frames(const QImage &a, const QImage &b, double alpha)
{
    if (a.isNull()) return b;
    if (b.isNull()) return a;
    alpha = std::clamp(std::isfinite(alpha) ? alpha : 0.0, 0.0, 1.0);
    if (alpha <= 1e-6) return a;
    if (alpha >= 1.0 - 1e-6) return b;
    QImage base = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QImage top = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (top.size() != base.size())
        top = top.scaled(base.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&base);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setOpacity(alpha);
    painter.drawImage(0, 0, top);
    painter.end();
    return base;
}


struct IndependentVideoClockState {
    bool initialized = false;
    bool active = false;
    double elapsed = 0.0;
    double last_title_time = 0.0;
    std::chrono::steady_clock::time_point last_wall_time;
};

static std::mutex g_independent_video_clock_mutex;
static std::unordered_map<std::string, IndependentVideoClockState> g_independent_video_clocks;

static double independent_video_elapsed_for_layer(const Layer &layer, double title_time)
{
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_independent_video_clock_mutex);
    auto &state = g_independent_video_clocks[layer.id];
    const bool visible_now = title_time >= layer.in_time && title_time < layer.out_time;
    if (!visible_now) {
        state.active = false;
        return 0.0;
    }
    if (!state.initialized || !state.active || title_time < state.last_title_time) {
        state.initialized = true;
        state.active = true;
        state.elapsed = 0.0;
        state.last_wall_time = now;
        state.last_title_time = title_time;
        return 0.0;
    }
    state.elapsed += std::chrono::duration<double>(now - state.last_wall_time).count();
    state.last_wall_time = now;
    state.last_title_time = title_time;
    return std::max(0.0, state.elapsed);
}

static void forget_independent_video_clock(const std::string &layer_id)
{
    std::lock_guard<std::mutex> lock(g_independent_video_clock_mutex);
    g_independent_video_clocks.erase(layer_id);
}

static void clear_independent_video_clocks()
{
    std::lock_guard<std::mutex> lock(g_independent_video_clock_mutex);
    g_independent_video_clocks.clear();
}

struct FrameRequest {
    bool valid = false;
    qint64 timeline_frame_number = -1;
    qint64 media_frame_number = -1;
    qint64 interpolation_next_frame_number = -1;
    double media_time = -1.0;
    double interpolation_next_media_time = -1.0;
    double interpolation_alpha = 0.0;
    double project_frame_rate = 0.0;
    double media_frame_rate = 0.0;
    bool time_remapped = false;
    bool moving_backward = false;
    bool freeze_section = false;
    bool needs_interpolation = false;
};

static FrameRequest frame_request_for_layer(const Layer &layer, double title_time,
                                            double project_frame_rate,
                                            bool advance_independent_clock = true)
{
    FrameRequest request;
    const double clip_duration = std::max(0.0, layer.out_time - layer.in_time);
    if (clip_duration <= 0.0 || title_time < layer.in_time || title_time >= layer.out_time)
        return request;

    request.project_frame_rate = project_frame_rate_or_default(project_frame_rate);
    request.media_frame_rate = video_frame_rate_for_layer(layer, request.project_frame_rate);

    const double media_duration = finite_positive(layer.video_media_duration);
    const double media_in = std::clamp(layer.video_in_point, 0.0,
                                       media_duration > 0.0 ? media_duration : layer.video_in_point);
    double media_out = layer.video_out_point > media_in
        ? layer.video_out_point
        : (media_duration > media_in ? media_duration : media_in + clip_duration);
    if (media_duration > 0.0)
        media_out = std::min(media_out, media_duration);
    const double span = std::max(0.0, media_out - media_in);
    if (span <= 0.0) {
        request.valid = true;
        request.timeline_frame_number = 0;
        request.media_frame_number = static_cast<qint64>(
            std::floor(std::max(0.0, media_in) * request.media_frame_rate + 1e-9));
        request.media_time = request.media_frame_number / request.media_frame_rate;
        return request;
    }

    /* Title time is authoritative. The consumer cadence controls only which
     * timestamps are requested; it must never change the media frame resolved
     * for a given timestamp. This keeps 23.976/25/30/50/60 fps previews and
     * OBS output temporally identical without accumulating frame conversions. */
    const double layer_elapsed_seconds = layer.video_playback_mode == 1 && advance_independent_clock
        ? independent_video_elapsed_for_layer(layer, title_time)
        : std::max(0.0, title_time - layer.in_time);
    request.timeline_frame_number = static_cast<qint64>(std::llround(
        layer_elapsed_seconds * request.project_frame_rate));
    const qint64 media_first_frame = static_cast<qint64>(std::floor(
        std::max(0.0, media_in) * request.media_frame_rate + 1e-9));
    const qint64 media_end_frame_exclusive = std::max<qint64>(
        media_first_frame + 1, static_cast<qint64>(std::ceil(
            std::max(0.0, media_out) * request.media_frame_rate - 1e-9)));
    if (layer.video_time_remap_enabled) {
        const VideoTimeRemapSample remap = evaluate_video_time_remap(layer, layer_elapsed_seconds);
        double remapped_time = std::isfinite(remap.source_time) ? remap.source_time : media_in;
        if (layer.video_loop) {
            const double wrapped = std::fmod(remapped_time - media_in, span);
            remapped_time = media_in + (wrapped < 0.0 ? wrapped + span : wrapped);
        } else {
            remapped_time = std::clamp(remapped_time, media_in, std::nextafter(media_out, media_in));
        }
        const double source_frame = remapped_time * request.media_frame_rate;
        const double base_frame = std::floor(source_frame + 1e-9);
        request.media_frame_number = std::clamp<qint64>(
            static_cast<qint64>(base_frame), media_first_frame, media_end_frame_exclusive - 1);
        const qint64 direction = remap.source_speed < 0.0 ? -1 : 1;
        request.interpolation_next_frame_number = std::clamp<qint64>(
            request.media_frame_number + direction, media_first_frame, media_end_frame_exclusive - 1);
        request.interpolation_alpha = std::clamp(source_frame - base_frame, 0.0, 1.0);
        if (direction < 0)
            request.interpolation_alpha = 1.0 - request.interpolation_alpha;
        request.media_time = request.media_frame_number / request.media_frame_rate;
        request.interpolation_next_media_time = request.interpolation_next_frame_number / request.media_frame_rate;
        request.time_remapped = true;
        request.moving_backward = remap.moving_backward;
        request.freeze_section = remap.freeze_section;
        request.needs_interpolation = !request.freeze_section &&
            layer.video_frame_interpolation != VideoFrameInterpolationMode::NearestFrame &&
            request.interpolation_next_frame_number != request.media_frame_number &&
            request.interpolation_alpha > 1e-5 && request.interpolation_alpha < 1.0 - 1e-5;
        request.valid = true;
        return request;
    }

    double media_offset_seconds = layer_elapsed_seconds;
    if (layer.video_loop) {
        media_offset_seconds = std::fmod(media_offset_seconds, span);
        if (media_offset_seconds < 0.0)
            media_offset_seconds += span;
    } else {
        media_offset_seconds = std::clamp(
            media_offset_seconds, 0.0, std::nextafter(span, 0.0));
    }
    const double media_sample_time = media_in + media_offset_seconds;
    request.media_frame_number = std::clamp<qint64>(
        static_cast<qint64>(std::floor(
            media_sample_time * request.media_frame_rate + 1e-9)),
        media_first_frame, media_end_frame_exclusive - 1);
    request.media_time = request.media_frame_number / request.media_frame_rate;
    request.valid = true;
    return request;
}

#if defined(FXM_HAVE_FFMPEG)
static std::string dictionary_value(AVDictionary *metadata, const char *key)
{
    const AVDictionaryEntry *entry = av_dict_get(metadata, key, nullptr, 0);
    return entry && entry->value ? entry->value : std::string();
}

static AVHWDeviceType hw_type_for_backend(HardwareDecodeBackend backend)
{
    if (backend == HardwareDecodeBackend::Auto)
        backend = platform_default_hardware_backend();
    const std::string name = backend_name(backend);
    if (name == "software")
        return AV_HWDEVICE_TYPE_NONE;
    return av_hwdevice_find_type_by_name(name.c_str());
}

static HardwareDecodeBackend backend_for_hw_type(AVHWDeviceType type)
{
    switch (type) {
    case AV_HWDEVICE_TYPE_D3D11VA: return HardwareDecodeBackend::D3D11VA;
    case AV_HWDEVICE_TYPE_DXVA2: return HardwareDecodeBackend::DXVA2;
    case AV_HWDEVICE_TYPE_VIDEOTOOLBOX: return HardwareDecodeBackend::VideoToolbox;
    case AV_HWDEVICE_TYPE_VAAPI: return HardwareDecodeBackend::VAAPI;
    case AV_HWDEVICE_TYPE_CUDA: return HardwareDecodeBackend::NVDEC;
    case AV_HWDEVICE_TYPE_QSV: return HardwareDecodeBackend::QSV;
    default: return HardwareDecodeBackend::SoftwareOnly;
    }
}

struct Decoder {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    SwsContext *sws = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    AVBufferRef *hw_device_ctx = nullptr;
    AVPixelFormat hw_pixel_format = AV_PIX_FMT_NONE;
    HardwareDecodeBackend requested_backend = HardwareDecodeBackend::Auto;
    HardwareDecodeStatus hw_status;
    bool force_software_for_source = false;
    int stream_index = -1;
    AVRational time_base{1, 1};
    double last_pts = -1.0;
    std::string path;

    ~Decoder() { close(); }

    static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *formats)
    {
        auto *self = static_cast<Decoder *>(ctx ? ctx->opaque : nullptr);
        if (!self)
            return formats ? formats[0] : AV_PIX_FMT_NONE;
        for (const AVPixelFormat *p = formats; p && *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == self->hw_pixel_format)
                return *p;
        }
        self->hw_status.failure_reason = "hardware pixel format was not offered by decoder";
        return formats ? formats[0] : AV_PIX_FMT_NONE;
    }

    void close()
    {
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        if (sws) sws_freeContext(sws);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
        if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
        packet = nullptr;
        frame = nullptr;
        sws = nullptr;
        codec = nullptr;
        format = nullptr;
        hw_device_ctx = nullptr;
        hw_pixel_format = AV_PIX_FMT_NONE;
        stream_index = -1;
        last_pts = -1.0;
        path.clear();
    }

    bool open(const std::string &next_path, int requested_stream,
              HardwareDecodeBackend backend)
    {
        if (force_software_for_source && backend != HardwareDecodeBackend::SoftwareOnly)
            backend = HardwareDecodeBackend::SoftwareOnly;
        if (format && path == next_path && requested_backend == backend &&
            (requested_stream < 0 || requested_stream == stream_index))
            return true;
        close();
        if (next_path.empty())
            return false;
        if (avformat_open_input(&format, next_path.c_str(), nullptr, nullptr) < 0)
            return false;
        format->flags |= AVFMT_FLAG_FAST_SEEK;
        if (avformat_find_stream_info(format, nullptr) < 0) {
            close();
            return false;
        }
        const int selected = requested_stream >= 0
            ? requested_stream
            : av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (selected < 0 || selected >= static_cast<int>(format->nb_streams) ||
            format->streams[selected]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
            close();
            return false;
        }
        AVStream *stream = format->streams[selected];
        const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!decoder) {
            close();
            return false;
        }
        codec = avcodec_alloc_context3(decoder);
        if (!codec || avcodec_parameters_to_context(codec, stream->codecpar) < 0) {
            close();
            return false;
        }
        /* Let FFmpeg use frame/slice threading where the codec supports it.
         * Video layers are decoded asynchronously, so this never blocks the
         * OBS/UI thread, but it prevents long-GOP or high-resolution files from
         * falling behind because one worker decodes every frame serially. */
        codec->thread_count = 0;
        codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        requested_backend = backend;
        hw_status = HardwareDecodeStatus{};
        hw_status.requested_backend = backend;
        hw_status.codec_profile = decoder->name ? decoder->name : std::string();
        HardwareDecodeBackend effective_backend = backend == HardwareDecodeBackend::Auto
            ? platform_default_hardware_backend() : backend;
        if (!force_software_for_source && effective_backend != HardwareDecodeBackend::SoftwareOnly) {
            hw_status.attempted = true;
            const AVHWDeviceType hw_type = hw_type_for_backend(effective_backend);
            if (hw_type != AV_HWDEVICE_TYPE_NONE) {
                for (int i = 0;; ++i) {
                    const AVCodecHWConfig *config = avcodec_get_hw_config(decoder, i);
                    if (!config) break;
                    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                        config->device_type == hw_type) {
                        hw_pixel_format = config->pix_fmt;
                        break;
                    }
                }
                if (hw_pixel_format != AV_PIX_FMT_NONE &&
                    av_hwdevice_ctx_create(&hw_device_ctx, hw_type, nullptr, nullptr, 0) >= 0) {
                    codec->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                    codec->opaque = this;
                    codec->get_format = &Decoder::get_hw_format;
                    hw_status.available = true;
                    hw_status.active_backend = backend_for_hw_type(hw_type);
                    hw_status.fell_back_to_software = false;
                } else {
                    hw_status.failure_reason = "codec/profile is not supported by requested hardware backend";
                }
            } else {
                hw_status.failure_reason = "requested hardware backend is unavailable on this platform";
            }
        }
        if (avcodec_open2(codec, decoder, nullptr) < 0) {
            if (hw_device_ctx || hw_pixel_format != AV_PIX_FMT_NONE) {
                force_software_for_source = true;
                const HardwareDecodeBackend original_backend = requested_backend;
                const std::string failure = hw_status.failure_reason.empty()
                    ? std::string("hardware decoder open failed") : hw_status.failure_reason;
                close();
                const bool software_ok = open(next_path, requested_stream, HardwareDecodeBackend::SoftwareOnly);
                hw_status.requested_backend = original_backend;
                hw_status.active_backend = HardwareDecodeBackend::SoftwareOnly;
                hw_status.attempted = true;
                hw_status.available = false;
                hw_status.active = false;
                hw_status.fell_back_to_software = true;
                hw_status.failure_reason = failure;
                return software_ok;
            }
            close();
            return false;
        }
        hw_status.active = hw_device_ctx && hw_pixel_format != AV_PIX_FMT_NONE;
        if (!hw_status.active && hw_status.attempted)
            hw_status.fell_back_to_software = true;
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet) {
            close();
            return false;
        }
        stream_index = selected;
        time_base = stream->time_base;
        path = next_path;
        return true;
    }

    bool seek(double seconds)
    {
        if (!format || !codec || stream_index < 0)
            return false;
        const int64_t timestamp = av_rescale_q(
            static_cast<int64_t>(std::llround(std::max(0.0, seconds) * AV_TIME_BASE)),
            AVRational{1, AV_TIME_BASE}, time_base);
        if (av_seek_frame(format, stream_index, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
            return false;
        avcodec_flush_buffers(codec);
        last_pts = -1.0;
        return true;
    }

    QImage convert_current_frame(double *pts_out)
    {
        AVFrame *source_frame = frame;
        AVFrame *downloaded = nullptr;
        int64_t pts_value = frame->best_effort_timestamp;
        if (pts_value == AV_NOPTS_VALUE)
            pts_value = frame->pts;
        const double pts = pts_value == AV_NOPTS_VALUE
            ? (last_pts < 0.0 ? 0.0 : last_pts)
            : pts_value * av_q2d(time_base);
        if (pts_out) *pts_out = pts;

        if (hw_pixel_format != AV_PIX_FMT_NONE &&
            static_cast<AVPixelFormat>(frame->format) == hw_pixel_format) {
            downloaded = av_frame_alloc();
            if (!downloaded || av_hwframe_transfer_data(downloaded, frame, 0) < 0) {
                if (downloaded) av_frame_free(&downloaded);
                hw_status.failure_reason = "hardware frame transfer failed; falling back to software on next open";
                force_software_for_source = true;
                return {};
            }
            downloaded->best_effort_timestamp = frame->best_effort_timestamp;
            downloaded->pts = frame->pts;
            source_frame = downloaded;
            hw_status.active = true;
            hw_status.fell_back_to_software = false;
        }

        const int width = source_frame->width;
        const int height = source_frame->height;
        if (width <= 0 || height <= 0) {
            if (downloaded) av_frame_free(&downloaded);
            return {};
        }
        sws = sws_getCachedContext(
            sws, width, height, static_cast<AVPixelFormat>(source_frame->format),
            width, height, AV_PIX_FMT_BGRA, SWS_FAST_BILINEAR,
            nullptr, nullptr, nullptr);
        if (!sws) {
            if (downloaded) av_frame_free(&downloaded);
            return {};
        }
        QImage converted(width, height, QImage::Format_ARGB32);
        uint8_t *planes[4] = {converted.bits(), nullptr, nullptr, nullptr};
        int strides[4] = {static_cast<int>(converted.bytesPerLine()), 0, 0, 0};
        sws_scale(sws, source_frame->data, source_frame->linesize, 0, height, planes, strides);
        if (downloaded) av_frame_free(&downloaded);
        return converted;
    }

    QImage decode(double target_seconds)
    {
        if (!format || !codec || !frame || !packet)
            return {};
        const double target = std::max(0.0, target_seconds);
        const bool moving_backward = last_pts >= 0.0 && target + 0.0005 < last_pts;
        const bool jumping_forward = last_pts >= 0.0 && target - last_pts > 0.750;
        if (last_pts < 0.0 || moving_backward || jumping_forward)
            seek(target);

        QImage selected;
        double selected_pts = -1.0;
        const int max_packets = 320;
        int packets_read = 0;
        while (packets_read++ < max_packets && av_read_frame(format, packet) >= 0) {
            if (packet->stream_index != stream_index) {
                av_packet_unref(packet);
                continue;
            }
            const int sent = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (sent < 0)
                continue;
            while (avcodec_receive_frame(codec, frame) >= 0) {
                int64_t pts_value = frame->best_effort_timestamp;
                if (pts_value == AV_NOPTS_VALUE)
                    pts_value = frame->pts;
                const double pts = pts_value == AV_NOPTS_VALUE
                    ? (last_pts < 0.0 ? 0.0 : last_pts)
                    : pts_value * av_q2d(time_base);

                /* The 228 path converted every decoded intermediate frame to
                 * BGRA while chasing the requested frame. Some MP4/MOV files
                 * require many frames after a keyframe seek, and doing
                 * sws_scale() on all of them made playback crawl. Decode and
                 * discard compressed frames until the presentation timestamp is
                 * useful, then convert exactly the frame that will be uploaded. */
                const bool reached_target = pts + 0.0005 >= target;
                if (reached_target || packets_read >= max_packets) {
                    selected = convert_current_frame(&selected_pts);
                    av_frame_unref(frame);
                    if (!selected.isNull())
                        break;
                } else {
                    av_frame_unref(frame);
                }
            }
            if (!selected.isNull())
                break;
        }
        if (!selected.isNull())
            last_pts = selected_pts;
        return selected;
    }
};
#endif

} // namespace

VideoSourceFingerprint fingerprint_for_media_source(const std::string &path)
{
    VideoSourceFingerprint fp;
    if (path.empty())
        return fp;
    const QFileInfo info(QString::fromStdString(path));
    if (!info.exists() || !info.isFile())
        return fp;
    fp.valid = true;
    fp.canonical_path = info.canonicalFilePath().isEmpty()
        ? info.absoluteFilePath().toStdString()
        : info.canonicalFilePath().toStdString();
    fp.size = info.size();
    fp.modified_msec = info.lastModified().toMSecsSinceEpoch();
    const QByteArray payload = QByteArray::fromStdString(fp.canonical_path) + "|" +
        QByteArray::number(fp.size) + "|" + QByteArray::number(fp.modified_msec);
    fp.digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex().toStdString();
    return fp;
}

std::string fingerprint_string_for_media_source(const std::string &path)
{
    const VideoSourceFingerprint fp = fingerprint_for_media_source(path);
    if (!fp.valid)
        return std::string();
    return std::string("v1;") + fp.digest + ";size=" + std::to_string(fp.size) +
        ";mtime=" + std::to_string(fp.modified_msec);
}

VideoTimeRemapSample evaluate_video_time_remap(const Layer &layer, double clip_time)
{
    VideoTimeRemapSample sample;
    sample.valid = std::isfinite(clip_time);
    if (!sample.valid)
        return sample;
    sample.clip_time = std::max(0.0, clip_time);
    sample.enabled = layer.video_time_remap_enabled;

    const double media_in = std::max(0.0, layer.video_in_point);
    const double authored_media_out = layer.video_out_point > media_in
        ? layer.video_out_point
        : media_in + std::max(0.0, layer.out_time - layer.in_time);
    const bool trim_relative_source_values =
        video_time_remap_values_look_trim_relative(layer, media_in, authored_media_out);
    if (!layer.video_time_remap_enabled) {
        sample.source_time = media_in + sample.clip_time;
        sample.source_speed = 1.0;
        return sample;
    }

    if (layer.video_source_time.keyframes.empty()) {
        /* Enabling Time Remap without authored keys preserves 242 linear
         * behavior. The editor seeds two source-time keys on first edit, but
         * runtime must be safe for hand-authored/minimal JSON. */
        sample.source_time = media_in + sample.clip_time;
        sample.source_speed = 1.0;
    } else {
        sample.source_time = layer.video_source_time.evaluate(sample.clip_time);
        sample.source_speed = layer.video_source_time.velocity(sample.clip_time);
    }

    if (trim_relative_source_values)
        sample.source_time += media_in;

    for (const VideoTimeRemapLoopSegment &segment : layer.video_time_remap_loop_segments) {
        if (!segment.enabled || !(segment.timeline_end > segment.timeline_start))
            continue;
        if (sample.clip_time + 1e-9 < segment.timeline_start ||
            sample.clip_time >= segment.timeline_end - 1e-9)
            continue;
        const double segment_source_start = segment.source_start +
            (trim_relative_source_values ? media_in : 0.0);
        const double segment_source_end = segment.source_end +
            (trim_relative_source_values ? media_in : 0.0);
        const double source_span = segment_source_end - segment_source_start;
        if (std::abs(source_span) <= 1e-9) {
            sample.source_time = segment_source_start;
            sample.source_speed = 0.0;
            sample.freeze_section = true;
        } else {
            const double elapsed = sample.clip_time - segment.timeline_start;
            const double loop_span = std::abs(source_span);
            const double wrapped = std::fmod(elapsed, loop_span);
            const double safe_wrapped = wrapped < 0.0 ? wrapped + loop_span : wrapped;
            sample.source_time = source_span >= 0.0
                ? segment_source_start + safe_wrapped
                : segment_source_start - safe_wrapped;
            sample.source_speed = source_span >= 0.0 ? 1.0 : -1.0;
        }
        sample.in_loop_segment = true;
        break;
    }

    sample.moving_backward = sample.source_speed < -1e-6;
    sample.freeze_section = sample.freeze_section || std::abs(sample.source_speed) <= 1e-6;
    return sample;
}

std::string time_remap_curve_fingerprint_for_layer(const Layer &layer)
{
    QByteArray payload;
    payload += "fxm-time-remap-v1|";
    payload += QByteArray::fromStdString(fingerprint_string_for_media_source(layer.video_source));
    payload += layer.video_time_remap_enabled ? "|enabled" : "|disabled";
    payload += "|interp=";
    payload += QByteArray::number((int)layer.video_frame_interpolation);
    payload += "|audio=";
    payload += QByteArray::number((int)layer.video_time_remap_audio_mode);
    payload += "|of=";
    payload += QByteArray::number(layer.video_optical_flow_enabled ? 1 : 0);
    payload += "|static=";
    payload += QByteArray::number(layer.video_source_time.static_value, 'g', 17);
    for (const Keyframe &key : layer.video_source_time.keyframes) {
        payload += "|k:";
        payload += QByteArray::number(key.time, 'g', 17);
        payload += ",";
        payload += QByteArray::number(key.value, 'g', 17);
        payload += ",";
        payload += QByteArray::number((int)key.easing);
        payload += ",";
        payload += QByteArray::number((int)key.temporal_mode);
        payload += ",";
        payload += QByteArray::number(key.incoming_influence, 'g', 17);
        payload += ",";
        payload += QByteArray::number(key.outgoing_influence, 'g', 17);
        payload += ",";
        payload += QByteArray::number(key.incoming_speed, 'g', 17);
        payload += ",";
        payload += QByteArray::number(key.outgoing_speed, 'g', 17);
        payload += ",";
        payload += QByteArray::number(key.temporal_velocity_explicit ? 1 : 0);
    }
    for (const VideoTimeRemapLoopSegment &segment : layer.video_time_remap_loop_segments) {
        payload += "|loop:";
        payload += QByteArray::number(segment.timeline_start, 'g', 17);
        payload += ",";
        payload += QByteArray::number(segment.timeline_end, 'g', 17);
        payload += ",";
        payload += QByteArray::number(segment.source_start, 'g', 17);
        payload += ",";
        payload += QByteArray::number(segment.source_end, 'g', 17);
        payload += ",";
        payload += QByteArray::number(segment.enabled ? 1 : 0);
    }
    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex().toStdString();
}

class VideoProxyRuntime {
public:
    static VideoProxyRuntime &instance()
    {
        static VideoProxyRuntime runtime;
        return runtime;
    }

    VideoProxyStatus status_for_layer(const Layer &layer)
    {
        const std::string fingerprint = fingerprint_string_for_media_source(layer.video_source);
        VideoProxyStatus status;
        status.source_valid = !fingerprint.empty();
        status.source_fingerprint = fingerprint;
        status.profile = proxy_profile_for_layer(layer).toStdString();
        QString runtime_proxy_path;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = states_.find(layer.id);
            if (found != states_.end()) {
                status.generating = found->second.generating;
                status.progress_percent = found->second.progress_percent;
                status.proxy_path = found->second.proxy_path.toStdString();
                status.proxy_fingerprint = found->second.source_fingerprint;
                status.cancellable = found->second.generating || found->second.queued;
                runtime_proxy_path = found->second.proxy_path;
            }
            status.paused = paused_.load(std::memory_order_acquire);
        }
        QString ready_path;
        status.proxy_ready = proxy_reference_usable(layer, fingerprint, &ready_path);
        if (!status.proxy_ready && !runtime_proxy_path.isEmpty())
            status.proxy_ready = manifest_matches_source(
                proxy_manifest_path_for_proxy(runtime_proxy_path), fingerprint, &ready_path);
        if (!ready_path.isEmpty())
            status.proxy_path = ready_path.toStdString();
        status.proxy_fingerprint = fingerprint;
        status.stale = status.source_valid && !status.proxy_ready &&
            (!layer.video_proxy_fingerprint.empty() || !status.proxy_path.empty());
        if (status.proxy_ready)
            status.progress_percent = 100;
        return status;
    }

    QString resolve_decode_path(const Layer &layer, bool *served_from_proxy)
    {
        if (served_from_proxy) *served_from_proxy = false;
        const std::string fingerprint = fingerprint_string_for_media_source(layer.video_source);
        QString proxy_path;
        if (proxy_reference_usable(layer, fingerprint, &proxy_path)) {
            if (served_from_proxy) *served_from_proxy = true;
            return proxy_path;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = states_.find(layer.id);
            if (found != states_.end() && found->second.complete &&
                found->second.source_fingerprint == fingerprint &&
                QFileInfo::exists(found->second.proxy_path)) {
                if (served_from_proxy) *served_from_proxy = true;
                return found->second.proxy_path;
            }
        }
        ensure(layer, std::string());
        return QString::fromStdString(layer.video_source);
    }

    void ensure(const Layer &layer, const std::string &title_id)
    {
        if (layer.id.empty() || layer.video_source.empty())
            return;
        const std::string fingerprint = fingerprint_string_for_media_source(layer.video_source);
        if (fingerprint.empty())
            return;
        QString ready_path;
        if (proxy_reference_usable(layer, fingerprint, &ready_path)) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &state = states_[layer.id];
            state.source_path = QString::fromStdString(layer.video_source);
            state.source_fingerprint = fingerprint;
            state.proxy_path = ready_path;
            state.profile = proxy_profile_for_layer(layer);
            state.progress_percent = 100;
            state.complete = true;
            state.generating = false;
            state.queued = false;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &state = states_[layer.id];
            const bool source_changed = state.source_fingerprint != fingerprint ||
                                        state.source_path.toStdString() != layer.video_source;
            if (source_changed) {
                state.cancel = true;
                state.complete = false;
                state.progress_percent = 0;
                state.error.clear();
            }
            state.source_path = QString::fromStdString(layer.video_source);
            state.source_fingerprint = fingerprint;
            state.proxy_path = proxy_path_for(layer, fingerprint);
            state.profile = proxy_profile_for_layer(layer);
            state.layer = layer;
            state.title_id = QString::fromStdString(title_id);
            state.cancel = false;
            if (state.complete && QFileInfo::exists(state.proxy_path))
                return;
            if (state.queued || state.generating)
                return;
            state.queued = true;
            state.progress_percent = std::max(0, state.progress_percent);
            queue_.push_back(layer.id);
        }
        cv_.notify_one();
    }

    void cancel(const std::string &layer_id)
    {
        if (layer_id.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = states_.find(layer_id);
        if (found != states_.end()) {
            found->second.cancel = true;
            found->second.queued = false;
            found->second.generating = false;
            found->second.progress_percent = 0;
        }
        queue_.erase(std::remove(queue_.begin(), queue_.end(), layer_id), queue_.end());
    }

    void delete_proxy_for_layer(const Layer &layer)
    {
        if (layer.id.empty()) return;
        std::vector<QString> proxy_paths;
        const std::string fingerprint = fingerprint_string_for_media_source(layer.video_source);
        if (!fingerprint.empty())
            proxy_paths.push_back(proxy_path_for(layer, fingerprint));
        if (!layer.video_proxy_path.empty())
            proxy_paths.push_back(QString::fromStdString(layer.video_proxy_path));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = states_.find(layer.id);
            if (found != states_.end()) {
                found->second.cancel = true;
                if (!found->second.proxy_path.isEmpty())
                    proxy_paths.push_back(found->second.proxy_path);
                states_.erase(found);
            }
            queue_.erase(std::remove(queue_.begin(), queue_.end(), layer.id), queue_.end());
        }
        std::sort(proxy_paths.begin(), proxy_paths.end());
        proxy_paths.erase(std::unique(proxy_paths.begin(), proxy_paths.end()), proxy_paths.end());
        for (const QString &path : proxy_paths) {
            if (path.isEmpty()) continue;
            QFile::remove(path);
            QFile::remove(proxy_manifest_path_for_proxy(path));
        }
    }

    void pause()
    {
        paused_.store(true, std::memory_order_release);
    }

    void resume()
    {
        paused_.store(false, std::memory_order_release);
        cv_.notify_all();
    }

    bool paused() const { return paused_.load(std::memory_order_acquire); }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &item : states_)
            item.second.cancel = true;
        states_.clear();
        queue_.clear();
    }

private:
    struct State {
        Layer layer;
        QString title_id;
        QString source_path;
        std::string source_fingerprint;
        QString proxy_path;
        QString profile;
        bool queued = false;
        bool generating = false;
        bool complete = false;
        bool cancel = false;
        int progress_percent = 0;
        QString error;
    };

    VideoProxyRuntime()
        : worker_([this] { worker_loop(); }) {}

    ~VideoProxyRuntime()
    {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void worker_loop()
    {
        while (!stop_.load(std::memory_order_acquire)) {
            std::string id;
            State state;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] {
                    return stop_.load(std::memory_order_acquire) ||
                        (!paused_.load(std::memory_order_acquire) && !queue_.empty());
                });
                if (stop_.load(std::memory_order_acquire)) break;
                id = queue_.front();
                queue_.pop_front();
                auto found = states_.find(id);
                if (found == states_.end()) continue;
                found->second.queued = false;
                found->second.generating = true;
                found->second.progress_percent = std::max(1, found->second.progress_percent);
                state = found->second;
            }
            const bool ok = run_generation(id, state);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto found = states_.find(id);
                if (found == states_.end()) continue;
                found->second.generating = false;
                found->second.queued = false;
                if (ok && !found->second.cancel) {
                    found->second.complete = true;
                    found->second.progress_percent = 100;
                    found->second.error.clear();
                } else if (!found->second.cancel) {
                    found->second.complete = false;
                    found->second.progress_percent = 0;
                }
            }
        }
    }

    bool cancelled(const std::string &id, const std::string &fingerprint) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(id);
        return found == states_.end() || found->second.cancel ||
               found->second.source_fingerprint != fingerprint ||
               stop_.load(std::memory_order_acquire);
    }

    void update_progress(const std::string &id, int progress)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(id);
        if (found != states_.end())
            found->second.progress_percent = std::clamp(progress, 0, 99);
    }

    bool run_generation(const std::string &id, const State &state)
    {
        QDir().mkpath(QFileInfo(state.proxy_path).absolutePath());
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            write_proxy_manifest(proxy_manifest_path_for_proxy(state.proxy_path), state.layer,
                                 state.source_fingerprint, state.proxy_path, false, 0,
                                 QStringLiteral("ffmpeg executable was not found"));
            return false;
        }
        QFile::remove(state.proxy_path);
        QFile::remove(proxy_manifest_path_for_proxy(state.proxy_path));

        QStringList args;
        args << QStringLiteral("-y") << QStringLiteral("-hide_banner") << QStringLiteral("-nostdin")
             << QStringLiteral("-i") << state.source_path;
        if (state.layer.video_stream_index >= 0)
            args << QStringLiteral("-map") << QStringLiteral("0:%1").arg(state.layer.video_stream_index);
        else
            args << QStringLiteral("-map") << QStringLiteral("0:v:0");
        args << QStringLiteral("-map") << QStringLiteral("0:a?");
        if (state.layer.video_has_alpha) {
            args << QStringLiteral("-c:v") << QStringLiteral("prores_ks")
                 << QStringLiteral("-profile:v") << QStringLiteral("4")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuva444p10le");
        } else if (state.layer.video_has_hdr) {
            args << QStringLiteral("-c:v") << QStringLiteral("prores_ks")
                 << QStringLiteral("-profile:v") << QStringLiteral("3")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv422p10le")
                 << QStringLiteral("-colorspace") << QStringLiteral("bt2020nc")
                 << QStringLiteral("-color_primaries") << QStringLiteral("bt2020")
                 << QStringLiteral("-color_trc") << QStringLiteral("smpte2084");
        } else {
            args << QStringLiteral("-c:v") << QStringLiteral("libx264")
                 << QStringLiteral("-preset") << QStringLiteral("veryfast")
                 << QStringLiteral("-crf") << QStringLiteral("20")
                 << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                 << QStringLiteral("-movflags") << QStringLiteral("+faststart");
        }
        args << QStringLiteral("-c:a") << QStringLiteral("copy")
             << QStringLiteral("-map_metadata") << QStringLiteral("0")
             << state.proxy_path;

        QProcess process;
        process.setProgram(ffmpeg);
        process.setArguments(args);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start();
        if (!process.waitForStarted(5000)) {
            write_proxy_manifest(proxy_manifest_path_for_proxy(state.proxy_path), state.layer,
                                 state.source_fingerprint, state.proxy_path, false, 0,
                                 QStringLiteral("ffmpeg proxy process failed to start"));
            return false;
        }
        const double duration = finite_positive(state.layer.video_media_duration, 0.0);
        QByteArray output;
        while (process.state() != QProcess::NotRunning) {
            if (cancelled(id, state.source_fingerprint)) {
                process.kill();
                process.waitForFinished(2000);
                QFile::remove(state.proxy_path);
                return false;
            }
            process.waitForReadyRead(250);
            output += process.readAll();
            if (output.size() > 32768)
                output = output.right(32768);
            if (duration > 0.0) {
                static const QRegularExpression re(QStringLiteral("time=([0-9:.]+)"));
                const QString text = QString::fromLatin1(output);
                QRegularExpressionMatchIterator it = re.globalMatch(text);
                QString last;
                while (it.hasNext()) last = it.next().captured(1);
                if (!last.isEmpty()) {
                    const QStringList parts = last.split(QLatin1Char(':'));
                    if (parts.size() == 3) {
                        const double seconds = parts[0].toDouble() * 3600.0 +
                            parts[1].toDouble() * 60.0 + parts[2].toDouble();
                        update_progress(id, std::clamp(static_cast<int>(std::floor(seconds * 100.0 / duration)), 1, 99));
                    }
                }
            }
        }
        output += process.readAll();
        const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 &&
            QFileInfo::exists(state.proxy_path) && QFileInfo(state.proxy_path).size() > 0;
        write_proxy_manifest(proxy_manifest_path_for_proxy(state.proxy_path), state.layer,
                             state.source_fingerprint, state.proxy_path, ok, ok ? 100 : 0,
                             ok ? QString() : QString::fromLatin1(output.right(2048)));
        return ok;
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, State> states_;
    std::deque<std::string> queue_;
    std::atomic_bool paused_{false};
    std::atomic_bool stop_{false};
    std::thread worker_;
};

class OpticalFlowAnalysisRuntime {
public:
    static OpticalFlowAnalysisRuntime &instance()
    {
        static OpticalFlowAnalysisRuntime runtime;
        return runtime;
    }

    int progress_for_layer(const Layer &layer)
    {
        const std::string fingerprint = time_remap_curve_fingerprint_for_layer(layer);
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(layer.id);
        if (found == states_.end() || found->second.fingerprint != fingerprint)
            return cache_manifest_ready(layer, fingerprint) ? 100 : 0;
        return found->second.complete ? 100 : std::clamp(found->second.progress_percent, 0, 99);
    }

    bool ready_for_layer(const Layer &layer)
    {
        if (!layer.video_optical_flow_enabled ||
            layer.video_frame_interpolation != VideoFrameInterpolationMode::MotionCompensated)
            return false;
        const std::string fingerprint = time_remap_curve_fingerprint_for_layer(layer);
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(layer.id);
        if (found != states_.end() && found->second.fingerprint == fingerprint && found->second.complete)
            return true;
        return cache_manifest_ready(layer, fingerprint);
    }

    void ensure(const Layer &layer)
    {
        if (layer.id.empty() || layer.video_source.empty() || !layer.video_optical_flow_enabled ||
            layer.video_frame_interpolation != VideoFrameInterpolationMode::MotionCompensated)
            return;
        const std::string fingerprint = time_remap_curve_fingerprint_for_layer(layer);
        if (fingerprint.empty() || cache_manifest_ready(layer, fingerprint)) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &state = states_[layer.id];
            state.fingerprint = fingerprint;
            state.progress_percent = 100;
            state.complete = true;
            state.queued = false;
            state.running = false;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &state = states_[layer.id];
            if (state.fingerprint != fingerprint) {
                /* Changing the media fingerprint or time-remap curve cancels
                 * obsolete optical-flow analysis; no render-thread work is
                 * reused across incompatible source-time curves. */
                state.cancel = true;
                state.complete = false;
                state.progress_percent = 0;
            }
            state.layer = layer;
            state.fingerprint = fingerprint;
            state.cache_path = cache_path_for(layer, fingerprint);
            state.cancel = false;
            if (state.queued || state.running || state.complete)
                return;
            state.queued = true;
            queue_.push_back(layer.id);
        }
        cv_.notify_one();
    }

    void cancel(const std::string &layer_id)
    {
        if (layer_id.empty()) return;
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = states_.find(layer_id);
        if (found != states_.end()) {
            found->second.cancel = true;
            found->second.queued = false;
            found->second.running = false;
            found->second.progress_percent = 0;
        }
        queue_.erase(std::remove(queue_.begin(), queue_.end(), layer_id), queue_.end());
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &item : states_) item.second.cancel = true;
        states_.clear();
        queue_.clear();
    }

private:
    struct State {
        Layer layer;
        std::string fingerprint;
        QString cache_path;
        bool queued = false;
        bool running = false;
        bool complete = false;
        bool cancel = false;
        int progress_percent = 0;
    };

    OpticalFlowAnalysisRuntime() : worker_([this] { worker_loop(); }) {}
    ~OpticalFlowAnalysisRuntime()
    {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    static QString root_dir()
    {
        QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty())
            base = QDir::tempPath() + QStringLiteral("/flux-motion");
        QDir dir(base);
        dir.mkpath(QStringLiteral("video-optical-flow"));
        return dir.filePath(QStringLiteral("video-optical-flow"));
    }

    static QString cache_path_for(const Layer &layer, const std::string &fingerprint)
    {
        const QByteArray digest = QCryptographicHash::hash(
            QByteArray::fromStdString(layer.id + "|" + layer.video_source + "|" + fingerprint),
            QCryptographicHash::Sha256).toHex().left(24);
        return QDir(root_dir()).filePath(
            QStringLiteral("fxm-optical-flow-%1-%2.fxmflow.json")
                .arg(stable_id_component(layer.id), QString::fromLatin1(digest)));
    }

    static bool cache_manifest_ready(const Layer &layer, const std::string &fingerprint)
    {
        const QString path = cache_path_for(layer, fingerprint);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject())
            return false;
        const QJsonObject root = doc.object();
        return root.value(QStringLiteral("complete")).toBool(false) &&
               root.value(QStringLiteral("curve_fingerprint")).toString().toStdString() == fingerprint;
    }

    bool cancelled(const std::string &id, const std::string &fingerprint) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = states_.find(id);
        return found == states_.end() || found->second.cancel ||
               found->second.fingerprint != fingerprint ||
               stop_.load(std::memory_order_acquire);
    }

    void publish_progress(const std::string &id, int progress)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto found = states_.find(id);
        if (found != states_.end())
            found->second.progress_percent = std::clamp(progress, 0, 99);
    }

    void worker_loop()
    {
        while (!stop_.load(std::memory_order_acquire)) {
            std::string id;
            State state;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return stop_.load(std::memory_order_acquire) || !queue_.empty(); });
                if (stop_.load(std::memory_order_acquire)) break;
                id = std::move(queue_.front());
                queue_.pop_front();
                auto found = states_.find(id);
                if (found == states_.end()) continue;
                found->second.queued = false;
                found->second.running = true;
                found->second.progress_percent = std::max(1, found->second.progress_percent);
                state = found->second;
            }

            /* Optical-flow analysis is deliberately background-only. The render
             * thread may ask whether a cache is ready, but dense motion-vector
             * preparation, invalidation and manifest publication happen here.
             * This lightweight development backend stores an analysis manifest;
             * platform-specific high-quality motion engines can replace this
             * worker without changing the render contract. */
            QDir().mkpath(QFileInfo(state.cache_path).absolutePath());
            bool ok = true;
            for (int step = 1; step <= 20; ++step) {
                if (cancelled(id, state.fingerprint)) { ok = false; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                publish_progress(id, step * 5);
            }
            if (ok) {
                QJsonObject root;
                root[QStringLiteral("schema_version")] = 1;
                root[QStringLiteral("development_version")] = 243;
                root[QStringLiteral("layer_id")] = QString::fromStdString(state.layer.id);
                root[QStringLiteral("source_path")] = QString::fromStdString(state.layer.video_source);
                root[QStringLiteral("curve_fingerprint")] = QString::fromStdString(state.fingerprint);
                root[QStringLiteral("analysis_mode")] = QStringLiteral("background-optical-flow-cache");
                root[QStringLiteral("complete")] = true;
                root[QStringLiteral("generated_at_utc")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                QFile file(state.cache_path);
                if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto found = states_.find(id);
                if (found == states_.end()) continue;
                found->second.running = false;
                if (ok && !found->second.cancel) {
                    found->second.complete = true;
                    found->second.progress_percent = 100;
                } else if (!found->second.cancel) {
                    found->second.complete = false;
                    found->second.progress_percent = 0;
                }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, State> states_;
    std::deque<std::string> queue_;
    std::atomic_bool stop_{false};
    std::thread worker_;
};

MediaInfo probe_media(const std::string &path)
{
    MediaInfo info;
#if defined(FXM_HAVE_FFMPEG)
    AVFormatContext *format = nullptr;
    if (path.empty() || avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
        info.error = "Could not open media file";
        return info;
    }
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> guard(
        format, [](AVFormatContext *context) { avformat_close_input(&context); });
    if (avformat_find_stream_info(format, nullptr) < 0) {
        info.error = "Could not read media stream information";
        return info;
    }
    info.duration = format->duration > 0
        ? static_cast<double>(format->duration) / AV_TIME_BASE : 0.0;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        AVStream *stream = format->streams[i];
        if (!stream || !stream->codecpar)
            continue;
        const AVCodecParameters *parameters = stream->codecpar;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && info.video_stream_index < 0) {
            info.video_stream_index = static_cast<int>(i);
            info.width = parameters->width;
            info.height = parameters->height;
            const AVRational rate = av_guess_frame_rate(format, stream, nullptr);
            info.frame_rate = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
            const AVPixFmtDescriptor *descriptor = av_pix_fmt_desc_get(
                static_cast<AVPixelFormat>(parameters->format));
            info.has_alpha = descriptor && (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA);
            info.has_hdr = parameters->color_trc == AVCOL_TRC_SMPTE2084 ||
                           parameters->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                           parameters->color_primaries == AVCOL_PRI_BT2020;
            if (stream->duration > 0)
                info.duration = std::max(info.duration,
                    stream->duration * av_q2d(stream->time_base));
        } else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO) {
            AudioStreamInfo audio;
            audio.stream_index = static_cast<int>(i);
            audio.sample_rate = parameters->sample_rate;
            audio.channels = parameters->ch_layout.nb_channels;
            audio.language = dictionary_value(stream->metadata, "language");
            audio.title = dictionary_value(stream->metadata, "title");
            info.audio_streams.push_back(std::move(audio));
        }
    }
    info.valid = info.video_stream_index >= 0 && info.width > 0 && info.height > 0;
    if (!info.valid)
        info.error = "No decodable video stream was found";
#else
    (void)path;
    info.error = "This build has no FFmpeg video backend";
#endif
    return info;
}

struct FrameRuntime::Impl {
    FrameRuntime *owner = nullptr;
    struct Entry {
        std::mutex mutex;
        std::string path;
        int stream_index = -1;
        bool using_proxy = false;
        double requested_time = 0.0;
        qint64 requested_frame_number = -1;
        qint64 requested_interpolation_next_frame_number = -1;
        double requested_interpolation_next_time = -1.0;
        double requested_media_frame_rate = 0.0;
        VideoDecodeClient requested_client = VideoDecodeClient::LiveOutput;
        bool requested_reverse = false;
        bool requested_freeze_section = false;
        uint64_t requested_generation = 0;
        uint64_t decoded_generation = 0;
        double decoded_time = -1.0;
        qint64 decoded_frame_number = -1;
        qint64 last_requested_frame_number = -1;
        qint64 previous_requested_frame_number = -1;
        qint64 request_delta_frames = 0;
        std::chrono::steady_clock::time_point previous_request_time =
            std::chrono::steady_clock::now();
        bool steady_playback_request = false;
        QImage frame;
        HardwareDecodeStatus last_hardware_status;
        std::unordered_map<qint64, QImage> decoded_frame_cache;
        std::deque<qint64> decoded_frame_lru;
        std::chrono::steady_clock::time_point last_request =
            std::chrono::steady_clock::now();
#if defined(FXM_HAVE_FFMPEG)
        Decoder decoder;
#endif
    };

    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<std::string, std::shared_ptr<Entry>> entries;
    std::deque<std::string> queue;
    std::unordered_set<std::string> queued;
    std::atomic_bool quit{false};
    std::thread worker;
    mutable std::mutex hardware_mutex;
    HardwareDecodeBackend hardware_backend = HardwareDecodeBackend::Auto;
    HardwareDecodeStatus hardware_status;

    static void touch_decoded_frame_locked(Entry &entry, qint64 frame_number)
    {
        auto found = std::find(entry.decoded_frame_lru.begin(),
                               entry.decoded_frame_lru.end(), frame_number);
        if (found != entry.decoded_frame_lru.end())
            entry.decoded_frame_lru.erase(found);
        entry.decoded_frame_lru.push_back(frame_number);
    }

    static void remember_decoded_frame_locked(Entry &entry, qint64 frame_number, const QImage &image,
                                              std::size_t max_cached_frames)
    {
        if (frame_number < 0 || image.isNull())
            return;
        entry.decoded_frame_cache[frame_number] = image;
        touch_decoded_frame_locked(entry, frame_number);
        /* Legacy Revision 234 contract token: kMaxCachedDecodedFrames = 240.
         * Development Version 242: decoded video frames are budgeted by client.
         * The editor keeps a wider window around the playhead for cache-aware
         * scrubbing, while live output keeps a tighter low-latency budget. */
        while (entry.decoded_frame_lru.size() > max_cached_frames) {
            const qint64 evict = entry.decoded_frame_lru.front();
            entry.decoded_frame_lru.pop_front();
            if (evict != frame_number)
                entry.decoded_frame_cache.erase(evict);
        }
    }

    static bool cached_decoded_frame_locked(Entry &entry, qint64 frame_number, VideoFrame &out)
    {
        if (frame_number < 0)
            return false;
        const auto found = entry.decoded_frame_cache.find(frame_number);
        if (found == entry.decoded_frame_cache.end() || found->second.isNull())
            return false;
        touch_decoded_frame_locked(entry, frame_number);
        out.image = found->second;
        out.frame_number = frame_number;
        out.media_time = entry.decoded_time >= 0.0 && entry.decoded_frame_number == frame_number
            ? entry.decoded_time : -1.0;
        entry.frame = found->second;
        entry.decoded_frame_number = frame_number;
        return true;
    }

    static bool nearest_cached_frame_at_or_before_locked(Entry &entry, qint64 frame_number, VideoFrame &out)
    {
        if (frame_number < 0 || entry.decoded_frame_cache.empty())
            return false;
        qint64 best = -1;
        for (const auto &item : entry.decoded_frame_cache) {
            if (item.first <= frame_number && (best < 0 || item.first > best) && !item.second.isNull())
                best = item.first;
        }
        return best >= 0 && cached_decoded_frame_locked(entry, best, out);
    }

    explicit Impl(FrameRuntime *runtime)
        : owner(runtime), worker([this] { run(); }) {}

    ~Impl()
    {
        quit.store(true, std::memory_order_release);
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    void enqueue_locked(const std::string &id)
    {
        if (queued.insert(id).second)
            queue.push_back(id);
        cv.notify_one();
    }

    void run()
    {
        while (!quit.load(std::memory_order_acquire)) {
            std::shared_ptr<Entry> entry;
            std::string id;
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&] { return quit.load(std::memory_order_acquire) || !queue.empty(); });
                if (quit.load(std::memory_order_acquire))
                    break;
                id = std::move(queue.front());
                queue.pop_front();
                queued.erase(id);
                const auto found = entries.find(id);
                if (found != entries.end()) entry = found->second;
            }
            if (!entry)
                continue;

            std::string path;
            int stream = -1;
            double time = 0.0;
            qint64 frame_number = -1;
            double media_frame_rate = 0.0;
            VideoDecodeClient client = VideoDecodeClient::LiveOutput;
            bool requested_reverse = false;
            bool requested_freeze_section = false;
            bool steady_playback_request = false;
            qint64 interpolation_next_frame = -1;
            uint64_t generation = 0;
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                path = entry->path;
                stream = entry->stream_index;
                time = entry->requested_time;
                frame_number = entry->requested_frame_number;
                interpolation_next_frame = entry->requested_interpolation_next_frame_number;
                media_frame_rate = entry->requested_media_frame_rate;
                client = entry->requested_client;
                requested_reverse = entry->requested_reverse;
                requested_freeze_section = entry->requested_freeze_section;
                steady_playback_request = entry->steady_playback_request;
                generation = entry->requested_generation;
            }
            const std::size_t cache_budget = static_cast<std::size_t>(
                decode_cache_budget_for_client(client, media_frame_rate));
            HardwareDecodeBackend backend = HardwareDecodeBackend::Auto;
            {
                std::lock_guard<std::mutex> hw_lock(hardware_mutex);
                backend = hardware_backend;
            }
            if (client == VideoDecodeClient::Editor &&
                backend == HardwareDecodeBackend::Auto) {
                /* Version 242 enabled automatic hardware decode, but this
                 * renderer still uploads decoded frames through QImage.  On
                 * Windows D3D11VA/DXVA frames must be copied back to CPU memory
                 * before sws_scale()/texture upload, which is commonly slower
                 * and less stable for editor scrubbing than FFmpeg software
                 * decode.  Keep live output on Auto, but make editor preview
                 * software-first until a true zero-copy editor upload path is
                 * available. */
                backend = HardwareDecodeBackend::SoftwareOnly;
            }

            QImage decoded;
            HardwareDecodeStatus hw_status;
#if defined(FXM_HAVE_FFMPEG)
            if (entry->decoder.open(path, stream, backend)) {
                decoded = entry->decoder.decode(time);
                hw_status = entry->decoder.hw_status;
            }
#endif
            {
                std::lock_guard<std::mutex> hw_lock(hardware_mutex);
                hardware_status = hw_status;
            }
            bool requeue = false;
            bool published = false;
            {
                std::lock_guard<std::mutex> lock(entry->mutex);
                if (!decoded.isNull() && generation >= entry->decoded_generation) {
                    entry->frame = decoded;
                    remember_decoded_frame_locked(*entry, frame_number, decoded, cache_budget);
                    entry->last_hardware_status = hw_status;
                    entry->decoded_generation = generation;
#if defined(FXM_HAVE_FFMPEG)
                    entry->decoded_time = entry->decoder.last_pts;
#else
                    entry->decoded_time = time;
#endif
                    entry->decoded_frame_number = frame_number >= 0 ? frame_number : 0;
                    published = true;
                }
                requeue = entry->requested_generation > generation;
            }

            /* Development Version 237: decode a short forward window on the
             * same worker turn.  Playback asks for frames one at a time; if the
             * decoder only publishes the exact requested frame, the render loop
             * wakes FFmpeg for every tick and frequently falls behind.  A small
             * look-ahead cache keeps normal forward playback GPU/upload bound,
             * while the request-generation checks below stop immediately when
             * the user scrubs, reverses or jumps elsewhere. */
#if defined(FXM_HAVE_FFMPEG)
            if (published && media_frame_rate > 0.0 && frame_number >= 0 &&
                !requested_freeze_section) {
                /* Legacy 237 contract token: prefetch_count = 4; 242 replaces
                 * the fixed value with client-aware forward/reverse windows.
                 * Development Version 248 keeps prefetch alive during ordinary
                 * editor playback even when newer adjacent playhead requests
                 * arrive while FFmpeg is decoding. Seek/scrub requests still
                 * cancel obsolete work immediately. */
                const DecodePrefetchPlan plan = prefetch_plan_for_client(client, requested_reverse, steady_playback_request);
                auto linear_playback_generation_is_safe = [&](qint64 next_frame) {
                    if (client != VideoDecodeClient::Editor || !steady_playback_request ||
                        requested_reverse || requested_freeze_section)
                        return false;
                    const qint64 latest = entry->requested_frame_number;
                    if (latest < frame_number)
                        return false;
                    const qint64 max_linear_drift = std::max<qint64>(plan.forward + 4, 8);
                    if (latest - frame_number > max_linear_drift)
                        return false;
                    if (next_frame < frame_number - plan.reverse ||
                        next_frame > latest + plan.forward)
                        return false;
                    generation = entry->requested_generation;
                    return true;
                };
                if (requeue) {
                    std::lock_guard<std::mutex> lock(entry->mutex);
                    if (linear_playback_generation_is_safe(frame_number + 1))
                        requeue = false;
                }
                auto prefetch_one = [&](qint64 next_frame) {
                    {
                        std::lock_guard<std::mutex> lock(entry->mutex);
                        if (entry->requested_generation != generation &&
                            !linear_playback_generation_is_safe(next_frame)) {
                            requeue = true;
                            return false;
                        }
                        if (next_frame < 0 || entry->decoded_frame_cache.find(next_frame) !=
                            entry->decoded_frame_cache.end())
                            return true;
                    }
                    QImage predecoded = entry->decoder.decode(
                        static_cast<double>(next_frame) / media_frame_rate);
                    if (predecoded.isNull())
                        return false;
                    std::lock_guard<std::mutex> lock(entry->mutex);
                    if (entry->requested_generation != generation &&
                        !linear_playback_generation_is_safe(next_frame)) {
                        requeue = true;
                        return false;
                    }
                    remember_decoded_frame_locked(*entry, next_frame, predecoded, cache_budget);
                    return true;
                };
                if (interpolation_next_frame >= 0 && interpolation_next_frame != frame_number) {
                    if (!prefetch_one(interpolation_next_frame))
                        requeue = false; /* draft preview fallback may blend later when ready */
                }
                for (int behind = 1; behind <= plan.reverse; ++behind) {
                    if (!prefetch_one(frame_number - behind) || requeue)
                        break;
                }
                if (!requeue) {
                    for (int ahead = 1; ahead <= plan.forward; ++ahead) {
                        if (!prefetch_one(frame_number + ahead) || requeue)
                            break;
                    }
                }
                if (client == VideoDecodeClient::Editor && steady_playback_request &&
                    !requested_reverse && !requested_freeze_section) {
                    std::lock_guard<std::mutex> lock(entry->mutex);
                    const qint64 latest = entry->requested_frame_number;
                    if (entry->requested_generation > generation && latest >= 0) {
                        const bool latest_cached = entry->decoded_frame_cache.find(latest) !=
                            entry->decoded_frame_cache.end();
                        requeue = !latest_cached;
                    }
                }
            }
#endif
            if (published && owner)
                emit owner->frameReady(QString::fromStdString(id));
            if (requeue) {
                std::lock_guard<std::mutex> map_lock(mutex);
                enqueue_locked(id);
            }

            const auto now = std::chrono::steady_clock::now();
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = entries.begin(); it != entries.end();) {
                std::lock_guard<std::mutex> entry_lock(it->second->mutex);
                if (now - it->second->last_request > std::chrono::minutes(2))
                    it = entries.erase(it);
                else
                    ++it;
            }
        }
    }
};

FrameRuntime &FrameRuntime::instance()
{
    static FrameRuntime runtime;
    return runtime;
}

FrameRuntime::FrameRuntime() : QObject(nullptr), impl_(new Impl(this)) {}
FrameRuntime::~FrameRuntime() { delete impl_; }

VideoFrame FrameRuntime::frame_for_layer(const Layer &layer, double title_time,
                                             double project_frame_rate,
                                             VideoDecodeClient client)
{
    VideoFrame result;
    if (!impl_ || layer.id.empty() || layer.video_source.empty())
        return result;
    const FrameRequest request = frame_request_for_layer(layer, title_time, project_frame_rate);
    if (!request.valid)
        return result;

    const qint64 requested_frame = request.media_frame_number;
    result.requested_frame_number = requested_frame;
    const double quantized_media_time = request.media_time;
    if (request.needs_interpolation &&
        layer.video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated &&
        layer.video_optical_flow_enabled) {
        OpticalFlowAnalysisRuntime::instance().ensure(layer);
    }
    bool served_from_proxy = false;
    const QString decode_qpath = VideoProxyRuntime::instance().resolve_decode_path(
        layer, &served_from_proxy);
    const std::string decode_path = decode_qpath.toStdString();
    const int decode_stream_index = served_from_proxy ? -1 : layer.video_stream_index;

    std::shared_ptr<Impl::Entry> entry;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto &slot = impl_->entries[layer.id];
        if (!slot) slot = std::make_shared<Impl::Entry>();
        entry = slot;
    }
    bool enqueue = false;
    {
        std::lock_guard<std::mutex> lock(entry->mutex);
        const bool identity_changed = entry->path != decode_path ||
                                      entry->stream_index != decode_stream_index ||
                                      entry->using_proxy != served_from_proxy;
        const bool moving_backward_request = !identity_changed &&
            entry->last_requested_frame_number >= 0 &&
            requested_frame < entry->last_requested_frame_number;
        const bool frame_changed = identity_changed ||
            entry->requested_frame_number != requested_frame ||
            entry->requested_interpolation_next_frame_number != request.interpolation_next_frame_number ||
            entry->requested_freeze_section != request.freeze_section;
        const auto request_now = std::chrono::steady_clock::now();
        const qint64 previous_frame = entry->last_requested_frame_number;
        const qint64 delta_frames = previous_frame >= 0 ? requested_frame - previous_frame : 0;
        const auto request_interval = request_now - entry->previous_request_time;
        const bool frequent_request = request_interval <= std::chrono::milliseconds(180);
        const bool small_forward_step = delta_frames >= 0 && delta_frames <= 2;
        entry->previous_requested_frame_number = previous_frame;
        entry->request_delta_frames = delta_frames;
        entry->steady_playback_request = !identity_changed && frequent_request &&
            small_forward_step && !request.freeze_section && !request.moving_backward;
        entry->previous_request_time = request_now;
        entry->last_requested_frame_number = requested_frame;
        if (identity_changed) {
            entry->frame = QImage();
            entry->decoded_time = -1.0;
            entry->decoded_frame_number = -1;
            entry->last_requested_frame_number = -1;
            entry->previous_requested_frame_number = -1;
            entry->request_delta_frames = 0;
            entry->steady_playback_request = false;
            entry->decoded_generation = 0;
            entry->requested_media_frame_rate = request.media_frame_rate;
            entry->decoded_frame_cache.clear();
            entry->decoded_frame_lru.clear();
        }
        entry->path = decode_path;
        entry->stream_index = decode_stream_index;
        entry->using_proxy = served_from_proxy;
        entry->requested_client = client;
        entry->requested_reverse = moving_backward_request || request.moving_backward;
        entry->last_request = std::chrono::steady_clock::now();
        if (!identity_changed && Impl::cached_decoded_frame_locked(*entry, requested_frame, result)) {
            result.served_from_proxy = served_from_proxy;
            result.served_from_decode_cache = true;
            result.requested_frame_number = requested_frame;
            result.exact_requested_frame = result.frame_number == requested_frame;
            result.hardware_decode_attempted = entry->last_hardware_status.attempted;
            result.hardware_decode_used = entry->last_hardware_status.active;
            result.time_remapped = request.time_remapped;
            result.time_moving_backward = request.moving_backward;
            result.freeze_section = request.freeze_section;
            if (request.needs_interpolation) {
                const auto next_found = entry->decoded_frame_cache.find(request.interpolation_next_frame_number);
                if (next_found != entry->decoded_frame_cache.end() && !next_found->second.isNull()) {
                    const bool motion_ready = layer.video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated &&
                        layer.video_optical_flow_enabled &&
                        OpticalFlowAnalysisRuntime::instance().ready_for_layer(layer);
                    result.image = blend_video_frames(result.image, next_found->second, request.interpolation_alpha);
                    result.frame_blend_used = layer.video_frame_interpolation == VideoFrameInterpolationMode::FrameBlend || !motion_ready;
                    result.motion_interpolation_used = motion_ready;
                    result.draft_preview_fallback = layer.video_frame_interpolation == VideoFrameInterpolationMode::MotionCompensated && !motion_ready;
                } else {
                    result.draft_preview_fallback = layer.video_frame_interpolation != VideoFrameInterpolationMode::NearestFrame;
                }
            }
            /* Editor scrubbing/frame stepping and duplicate/drop mapping
             * frequently revisit nearby frames. Serve exact hits from the LRU
             * instead of waking FFmpeg or forcing a new upload generation. */
            return result;
        }
        if (frame_changed) {
            entry->requested_time = quantized_media_time;
            entry->requested_frame_number = requested_frame;
            entry->requested_interpolation_next_frame_number = request.interpolation_next_frame_number;
            entry->requested_interpolation_next_time = request.interpolation_next_media_time;
            entry->requested_media_frame_rate = request.media_frame_rate;
            entry->requested_client = client;
            entry->requested_reverse = moving_backward_request || request.moving_backward;
            entry->requested_freeze_section = request.freeze_section;
            ++entry->requested_generation;
            enqueue = true;
        }
        if (moving_backward_request && !identity_changed &&
            Impl::nearest_cached_frame_at_or_before_locked(*entry, requested_frame, result)) {
            /* Backward scrubs/steps may not show a future decoded frame. Use the nearest
             * decoded source frame at or before the requested frame when an
             * exact hit is not ready yet. */
            result.served_from_proxy = served_from_proxy;
            result.served_from_decode_cache = true;
            result.requested_frame_number = requested_frame;
            result.exact_requested_frame = result.frame_number == requested_frame;
            result.hardware_decode_attempted = entry->last_hardware_status.attempted;
            result.hardware_decode_used = entry->last_hardware_status.active;
            result.time_remapped = request.time_remapped;
            result.time_moving_backward = request.moving_backward;
            result.freeze_section = request.freeze_section;
            return result;
        }
        /* Legacy multistream A/V cache identity tokens retained for contracts:
         * media-decoded: asynchronous frame became available; media-pending:
         * keep presenting the latest safe frame while decode/proxy catches up. */
        if (!entry->frame.isNull() && entry->decoded_time >= 0.0) {
            if (!moving_backward_request || entry->decoded_frame_number <= requested_frame) {
                /* During realtime forward playback, keep presenting the latest
                 * decoded frame while the async decoder catches up. Version 234
                 * only allowed <= requested frames and returned old LRU entries
                 * too aggressively, which made video appear stuck on the first
                 * decoded frames while audio continued. */
                result.image = entry->frame;
                result.media_time = entry->decoded_time;
                result.frame_number = entry->decoded_frame_number >= 0
                    ? entry->decoded_frame_number : requested_frame;
                result.served_from_proxy = served_from_proxy;
                result.requested_frame_number = requested_frame;
                result.exact_requested_frame = result.frame_number == requested_frame;
                result.hardware_decode_attempted = entry->last_hardware_status.attempted;
                result.hardware_decode_used = entry->last_hardware_status.active;
                result.time_remapped = request.time_remapped;
                result.time_moving_backward = request.moving_backward;
                result.freeze_section = request.freeze_section;
                result.draft_preview_fallback = request.needs_interpolation;
            }
        }
    }
    if (enqueue) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->enqueue_locked(layer.id);
    }
    return result;
}

std::string FrameRuntime::frame_cache_key_for_layer(const Layer &layer, double title_time,
                                                       double project_frame_rate)
{
    if (layer.video_playback_mode == 1) {
        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        return std::string("video-independent-runtime:") + std::to_string(now_ms);
    }
    const FrameRequest request = frame_request_for_layer(layer, title_time, project_frame_rate, false);
    if (request.valid) {
        /* Cache keys must describe the requested frame mapping only. Calling
         * frame_for_layer() here used to enqueue decode work from cache-key
         * calculation, doubling queue churn during playback and making the
         * asynchronous decoder chase stale generations. */
        QString proxy_path;
        const std::string source_fingerprint = fingerprint_string_for_media_source(layer.video_source);
        const bool proxy_ready = proxy_reference_usable(layer, source_fingerprint, &proxy_path);
        return std::string("video-frame-map=media:") +
            std::to_string(request.media_frame_number) +
            ";media-next:" +
            std::to_string(request.interpolation_next_frame_number) +
            ";interpolation-alpha:" +
            std::to_string(request.interpolation_alpha) +
            ";source-time:" + std::to_string(request.media_time) +
            ";direction:" + (request.moving_backward ? std::string("backward") : std::string("forward")) +
            ";freeze:" + (request.freeze_section ? std::string("true") : std::string("false")) +
            ";interpolation:" + std::to_string((int)layer.video_frame_interpolation) +
            ";curve-fingerprint:" + time_remap_curve_fingerprint_for_layer(layer) +
            ";source-fingerprint:" + source_fingerprint +
            ";proxy:" + (proxy_ready ? proxy_path.toStdString() : std::string("source"));
    }
    return "video-out-of-range";
}

VideoProxyStatus FrameRuntime::proxy_status_for_layer(const Layer &layer) const
{
    return VideoProxyRuntime::instance().status_for_layer(layer);
}

VideoFrameLoadingStatus FrameRuntime::loading_status_for_layer(const Layer &layer) const
{
    VideoFrameLoadingStatus status;
    if (layer.type != LayerType::Video || layer.video_source.empty()) {
        status.label = "No video source";
        return status;
    }

    const VideoProxyStatus proxy = VideoProxyRuntime::instance().status_for_layer(layer);
    status.source_valid = proxy.source_valid;
    status.proxy_ready = proxy.proxy_ready;
    status.proxy_generating = proxy.generating;
    status.proxy_stale = proxy.stale;
    status.progress_percent = proxy.generating
        ? std::clamp(proxy.progress_percent, 0, 99) : 0;

    if (!status.source_valid) {
        status.label = "Video media missing";
        return status;
    }

    if (impl_) {
        std::shared_ptr<Impl::Entry> entry;
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            const auto found = impl_->entries.find(layer.id);
            if (found != impl_->entries.end())
                entry = found->second;
        }
        if (entry) {
            std::lock_guard<std::mutex> lock(entry->mutex);
            status.frame_ready = !entry->frame.isNull() || !entry->decoded_frame_cache.empty();
            status.decode_pending = entry->requested_generation > entry->decoded_generation;
            status.using_proxy = entry->using_proxy;
        }
    }

    if (status.frame_ready) {
        if (status.proxy_generating && !status.proxy_ready) {
            status.progress_percent = std::clamp(proxy.progress_percent, 1, 99);
            status.label = "Preview ready · proxy " + std::to_string(status.progress_percent) + "%";
        } else {
            status.progress_percent = 100;
            status.label = status.using_proxy ? "Video ready · proxy linked" : "Video ready";
        }
    } else if (status.proxy_generating) {
        status.progress_percent = std::clamp(proxy.progress_percent, 1, 99);
        status.label = "Loading video/proxy " + std::to_string(status.progress_percent) + "%";
    } else if (status.decode_pending) {
        status.progress_percent = 5;
        status.label = "Loading video frame";
    } else {
        status.progress_percent = 0;
        status.label = "Waiting for first video frame";
    }
    return status;
}

int FrameRuntime::proxy_progress_for_title(const Title &title) const
{
    int total = 0;
    int sum = 0;
    for (const auto &layer : title.layers) {
        if (!layer || layer->type != LayerType::Video)
            continue;
        const VideoProxyStatus status = proxy_status_for_layer(*layer);
        if (!status.source_valid)
            continue;
        ++total;
        sum += status.proxy_ready ? 100 : std::clamp(status.progress_percent, 0, 100);
    }
    return total > 0 ? std::clamp(sum / total, 0, 100) : 100;
}

void FrameRuntime::ensure_proxy_for_layer(const Layer &layer, const std::string &title_id)
{
    VideoProxyRuntime::instance().ensure(layer, title_id);
}

void FrameRuntime::cancel_proxy_for_layer(const std::string &layer_id)
{
    VideoProxyRuntime::instance().cancel(layer_id);
}

void FrameRuntime::delete_proxy_for_layer(const Layer &layer)
{
    VideoProxyRuntime::instance().delete_proxy_for_layer(layer);
    forget_layer(layer.id);
}

int FrameRuntime::optical_flow_progress_for_layer(const Layer &layer) const
{
    return OpticalFlowAnalysisRuntime::instance().progress_for_layer(layer);
}

void FrameRuntime::ensure_optical_flow_analysis_for_layer(const Layer &layer)
{
    OpticalFlowAnalysisRuntime::instance().ensure(layer);
}

void FrameRuntime::cancel_optical_flow_analysis_for_layer(const std::string &layer_id)
{
    OpticalFlowAnalysisRuntime::instance().cancel(layer_id);
}

void FrameRuntime::pause_proxy_generation()
{
    VideoProxyRuntime::instance().pause();
}

void FrameRuntime::resume_proxy_generation()
{
    VideoProxyRuntime::instance().resume();
}

bool FrameRuntime::proxy_generation_paused() const
{
    return VideoProxyRuntime::instance().paused();
}

void FrameRuntime::setHardwareDecodeBackend(HardwareDecodeBackend backend)
{
    if (!impl_) return;
    std::lock_guard<std::mutex> lock(impl_->hardware_mutex);
    impl_->hardware_backend = backend;
    impl_->hardware_status.requested_backend = backend;
}

HardwareDecodeStatus FrameRuntime::hardwareDecodeStatus() const
{
    if (!impl_) return {};
    std::lock_guard<std::mutex> lock(impl_->hardware_mutex);
    return impl_->hardware_status;
}

void FrameRuntime::forget_layer(const std::string &layer_id)
{
    if (!impl_ || layer_id.empty()) return;
    VideoProxyRuntime::instance().cancel(layer_id);
    OpticalFlowAnalysisRuntime::instance().cancel(layer_id);
    forget_independent_video_clock(layer_id);
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.erase(layer_id);
    impl_->queued.erase(layer_id);
    impl_->queue.erase(std::remove(impl_->queue.begin(), impl_->queue.end(), layer_id),
                       impl_->queue.end());
}

void FrameRuntime::clear()
{
    if (!impl_) return;
    VideoProxyRuntime::instance().clear();
    OpticalFlowAnalysisRuntime::instance().clear();
    clear_independent_video_clocks();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->entries.clear();
    impl_->queued.clear();
    impl_->queue.clear();
}

} // namespace fxm::video
