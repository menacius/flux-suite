#pragma once

#include "audio-interfaces.h"
#include "title-data.h"
#include "audio-output-scheduler.h"
#include <obs-module.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fxm::audio {

struct DecodedAudioAsset;

class SourceAudioRuntime {
public:
    explicit SourceAudioRuntime(obs_source_t *source, bool editor_preview = false);
    SourceAudioRuntime(std::unique_ptr<IAudioBackend> output_backend,
                       std::string source_name,
                       bool editor_preview = true);
    ~SourceAudioRuntime();

    SourceAudioRuntime(const SourceAudioRuntime &) = delete;
    SourceAudioRuntime &operator=(const SourceAudioRuntime &) = delete;

    void bind_title(const std::shared_ptr<Title> &title, uint64_t revision);
    void transport(double title_seconds, bool playing, bool visible,
                   bool discontinuity = false, bool reverse = false,
                   double playback_speed = 1.0);
    void stop(bool allow_fade = true);
    void pump();
    void set_editor_preview(bool editor_preview);
    void current_levels(float *left, float *right,
                        uint64_t *last_update_ns = nullptr) const;

    int64_t duration_ms() const;
    int64_t time_ms() const;
    enum obs_media_state media_state() const;

    /* Shared decode entries are weak and therefore never keep audio alive after
     * the last title/source closes. Shutdown still clears stale keys eagerly. */
    static void clear_shared_cache();
    static std::size_t shared_cache_entry_count();

private:
    struct ClipSpec;
    struct DecodedClip;
    struct ActiveClip;

    void worker_main();
    void output_worker_main();
    void log_output_packet(uint64_t timestamp_ns, uint32_t frames, const char *path);
    void request_rebuild(const std::shared_ptr<Title> &title, uint64_t revision);
    void publish_decoded(uint64_t generation, uint64_t decode_epoch,
                         std::vector<std::shared_ptr<DecodedClip>> decoded);
    void publish_waveform_status(const ClipSpec &spec, int progress_percent,
                                 const char *phase,
                                 const std::vector<float> *peaks = nullptr,
                                 double duration = 0.0);
    void build_waveform_for_clip(const std::shared_ptr<DecodedClip> &clip,
                                 uint64_t decode_epoch);
    std::shared_ptr<DecodedClip> decode_clip(const ClipSpec &spec,
                                             uint32_t target_rate,
                                             uint64_t decode_epoch);
    bool decode_cancelled(uint64_t decode_epoch) const;
    void mix_block(int64_t start_sample, uint32_t frames, bool reverse,
                   double playback_speed,
                   std::vector<float> &left, std::vector<float> &right);

    obs_source_t *source_ = nullptr;
    std::unique_ptr<IAudioBackend> output_backend_;
    bool editor_preview_ = false;
    std::string source_name_;
    uint32_t sample_rate_ = 48000;
    uint32_t block_frames_ = 480;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable output_cv_;
    std::thread worker_;
    std::thread output_worker_;
    std::atomic_bool quit_{false};
    bool rebuild_pending_ = false;
    std::atomic<uint64_t> decode_epoch_{1};
    uint64_t requested_generation_ = 0;
    uint64_t published_generation_ = 0;
    std::vector<ClipSpec> requested_specs_;
    std::vector<std::shared_ptr<DecodedClip>> clips_;
    struct PendingWaveform {
        std::vector<float> peaks;
        double duration = 0.0;
        int progress_percent = 0;
        bool complete = false;
        std::string label;
    };
    std::unordered_map<std::string, PendingWaveform> pending_waveforms_;

    double title_time_ = 0.0;
    double previous_title_time_ = 0.0;
    double independent_time_ = 0.0;
    bool playing_ = false;
    bool visible_ = false;
    bool reverse_ = false;
    double playback_speed_ = 1.0;
    bool discontinuity_ = true;
    int64_t output_sample_cursor_ = 0;
    RealtimeMonitorCadence editor_monitor_cadence_;
    AudioOutputScheduler output_scheduler_;
    uint64_t independent_sample_cursor_ = 0;
    int64_t duration_ms_ = 0;
    std::atomic<int64_t> reported_time_ms_{0};
    std::atomic<enum obs_media_state> state_{OBS_MEDIA_STATE_STOPPED};

    std::atomic<uint64_t> log_last_summary_ns_{0};
    std::atomic<uint64_t> log_last_packet_timestamp_ns_{0};
    std::atomic<uint64_t> log_last_delivery_ns_{0};
    std::atomic<uint64_t> log_last_idle_ns_{0};
    std::atomic<uint64_t> log_output_blocks_{0};
    std::atomic<uint64_t> log_output_frames_{0};
    std::atomic<uint64_t> log_transport_updates_{0};
    std::atomic<uint64_t> log_discontinuities_{0};
    std::atomic<uint64_t> log_late_repairs_{0};
    std::atomic<float> level_left_{0.0f};
    std::atomic<float> level_right_{0.0f};
    std::atomic<uint64_t> level_update_ns_{0};
};

} // namespace fxm::audio
