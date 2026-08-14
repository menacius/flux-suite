#pragma once

#include "layer-model.h"

#include <QImage>
#include <QObject>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>

struct Title;

namespace fxm::video {

struct AudioStreamInfo {
    int stream_index = -1;
    int sample_rate = 0;
    int channels = 0;
    std::string language;
    std::string title;
};

struct MediaInfo {
    bool valid = false;
    int video_stream_index = -1;
    int width = 0;
    int height = 0;
    double duration = 0.0;
    double frame_rate = 0.0;
    bool has_alpha = false;
    bool has_hdr = false;
    std::vector<AudioStreamInfo> audio_streams;
    std::string error;
};

/* Reads container/stream headers only. It never decodes the media body. */
MediaInfo probe_media(const std::string &path);

struct VideoFrame {
    QImage image;
    double media_time = -1.0;
    qint64 frame_number = -1;
    qint64 requested_frame_number = -1;
    bool exact_requested_frame = false;
    bool served_from_proxy = false;
    bool served_from_decode_cache = false;
    bool hardware_decode_attempted = false;
    bool hardware_decode_used = false;
    bool time_remapped = false;
    bool time_moving_backward = false;
    bool freeze_section = false;
    bool draft_preview_fallback = false;
    bool frame_blend_used = false;
    bool motion_interpolation_used = false;
};

struct VideoSourceFingerprint {
    bool valid = false;
    std::string canonical_path;
    qint64 size = -1;
    qint64 modified_msec = 0;
    std::string digest;
};

VideoSourceFingerprint fingerprint_for_media_source(const std::string &path);
std::string fingerprint_string_for_media_source(const std::string &path);

struct VideoTimeRemapSample {
    bool valid = false;
    bool enabled = false;
    double clip_time = 0.0;
    double source_time = 0.0;
    double source_speed = 1.0;
    bool moving_backward = false;
    bool freeze_section = false;
    bool in_loop_segment = false;
};

VideoTimeRemapSample evaluate_video_time_remap(const Layer &layer, double clip_time);
std::string time_remap_curve_fingerprint_for_layer(const Layer &layer);


enum class VideoDecodeClient {
    Editor,
    LiveOutput,
};

enum class HardwareDecodeBackend {
    Auto,
    SoftwareOnly,
    D3D11VA,
    DXVA2,
    VideoToolbox,
    VAAPI,
    NVDEC,
    QSV,
};

struct HardwareDecodeStatus {
    HardwareDecodeBackend requested_backend = HardwareDecodeBackend::Auto;
    HardwareDecodeBackend active_backend = HardwareDecodeBackend::SoftwareOnly;
    bool attempted = false;
    bool available = false;
    bool active = false;
    bool fell_back_to_software = true;
    std::string codec_profile;
    std::string failure_reason;
};


struct VideoFrameLoadingStatus {
    bool source_valid = false;
    bool frame_ready = false;
    bool decode_pending = false;
    bool using_proxy = false;
    bool proxy_ready = false;
    bool proxy_generating = false;
    bool proxy_stale = false;
    int progress_percent = 0;
    std::string label;
};

struct VideoProxyStatus {
    bool enabled = true;
    bool source_valid = false;
    bool proxy_ready = false;
    bool stale = false;
    bool generating = false;
    bool paused = false;
    bool cancellable = false;
    int progress_percent = 0;
    std::string source_fingerprint;
    std::string proxy_fingerprint;
    std::string proxy_path;
    std::string profile;
};

/* Shared, asynchronous video-frame decoder. Every request is expressed in the
 * title clock used by SourceAudioRuntime. The decoder may drop obsolete frames
 * while catching up, but it never advances its own clock, preventing A/V drift. */
class FrameRuntime : public QObject {
    Q_OBJECT
public:
    static FrameRuntime &instance();

    VideoFrame frame_for_layer(const Layer &layer, double title_time,
                               double project_frame_rate = 0.0,
                               VideoDecodeClient client = VideoDecodeClient::LiveOutput);
    std::string frame_cache_key_for_layer(const Layer &layer, double title_time,
                                          double project_frame_rate = 0.0);
    VideoProxyStatus proxy_status_for_layer(const Layer &layer) const;
    VideoFrameLoadingStatus loading_status_for_layer(const Layer &layer) const;
    int proxy_progress_for_title(const Title &title) const;
    void ensure_proxy_for_layer(const Layer &layer, const std::string &title_id = std::string());
    void cancel_proxy_for_layer(const std::string &layer_id);
    void delete_proxy_for_layer(const Layer &layer);
    int optical_flow_progress_for_layer(const Layer &layer) const;
    void ensure_optical_flow_analysis_for_layer(const Layer &layer);
    void cancel_optical_flow_analysis_for_layer(const std::string &layer_id);
    void pause_proxy_generation();
    void resume_proxy_generation();
    bool proxy_generation_paused() const;
    void setHardwareDecodeBackend(HardwareDecodeBackend backend);
    HardwareDecodeStatus hardwareDecodeStatus() const;
    void forget_layer(const std::string &layer_id);
    void clear();

signals:
    void frameReady(const QString &layer_id);

private:
    FrameRuntime();
    ~FrameRuntime();
    FrameRuntime(const FrameRuntime &) = delete;
    FrameRuntime &operator=(const FrameRuntime &) = delete;

    struct Impl;
    Impl *impl_ = nullptr;
};

} // namespace fxm::video
