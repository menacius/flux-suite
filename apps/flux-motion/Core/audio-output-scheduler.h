#pragma once

#include <algorithm>
#include <cstdint>

namespace fxm::audio {

enum class AudioOutputSchedulerMode {
    BufferedSource,
    RealtimeMonitor,
};

struct AudioOutputSchedulerProfile {
    std::uint64_t target_lead_ns = 160000000ULL;
    std::uint64_t low_watermark_ns = 80000000ULL;
    std::uint64_t late_tolerance_ns = 20000000ULL;
    std::uint64_t maximum_wait_ns = 10000000ULL;
    int maximum_blocks_per_wake = 64;
};

inline constexpr AudioOutputSchedulerProfile audio_output_scheduler_profile(
    AudioOutputSchedulerMode mode)
{
    /* OBS monitor-only playback does not consume the source's timestamped
     * mixer queue. Its platform callback writes each packet to the device
     * immediately. Therefore the monitor path must be paced in real time:
     * exactly one 10 ms packet per wake and no queued catch-up burst. */
    return mode == AudioOutputSchedulerMode::RealtimeMonitor
        ? AudioOutputSchedulerProfile{
              10000000ULL, 0ULL, 20000000ULL, 10000000ULL, 1}
        : AudioOutputSchedulerProfile{
              160000000ULL, 80000000ULL, 20000000ULL, 10000000ULL, 64};
}

/*
 * Timestamp-only scheduler for asynchronous OBS audio sources.
 *
 * Normal program sources need a bounded queue large enough to absorb desktop
 * scheduler jitter. OBS monitor-only sources are different: their capture
 * callback forwards packets directly to the monitoring device, so a large
 * future queue becomes an immediate burst. The profile keeps both paths off
 * the video/render thread while preserving the correct delivery cadence.
 */
class AudioOutputScheduler {
public:
    explicit AudioOutputScheduler(
        AudioOutputSchedulerMode mode = AudioOutputSchedulerMode::BufferedSource)
        : mode_(mode), profile_(audio_output_scheduler_profile(mode))
    {
    }

    void set_mode(AudioOutputSchedulerMode mode)
    {
        if (mode_ == mode)
            return;
        mode_ = mode;
        profile_ = audio_output_scheduler_profile(mode);
        clear();
    }

    AudioOutputSchedulerMode mode() const { return mode_; }
    const AudioOutputSchedulerProfile &profile() const { return profile_; }

    void reset(std::uint64_t now_ns)
    {
        next_timestamp_ns_ = now_ns;
        initialized_ = true;
    }

    bool initialized() const { return initialized_; }
    std::uint64_t next_timestamp_ns() const { return next_timestamp_ns_; }

    bool is_late(std::uint64_t now_ns) const
    {
        return initialized_ &&
               next_timestamp_ns_ + profile_.late_tolerance_ns < now_ns;
    }

    bool should_fill(std::uint64_t now_ns) const
    {
        return !initialized_ ||
               next_timestamp_ns_ < now_ns + profile_.target_lead_ns;
    }

    void advance(std::uint32_t frames, std::uint32_t sample_rate)
    {
        if (!initialized_ || sample_rate == 0)
            return;
        next_timestamp_ns_ +=
            static_cast<std::uint64_t>(frames) * 1000000000ULL /
            sample_rate;
    }

    std::uint64_t wait_ns(std::uint64_t now_ns) const
    {
        if (!initialized_ ||
            next_timestamp_ns_ <= now_ns + profile_.low_watermark_ns)
            return 0;
        return std::min<std::uint64_t>(
            profile_.maximum_wait_ns,
            next_timestamp_ns_ - (now_ns + profile_.low_watermark_ns));
    }

    int maximum_blocks_per_wake() const
    {
        return profile_.maximum_blocks_per_wake;
    }

    void clear()
    {
        next_timestamp_ns_ = 0;
        initialized_ = false;
    }

private:
    AudioOutputSchedulerMode mode_ = AudioOutputSchedulerMode::BufferedSource;
    AudioOutputSchedulerProfile profile_ =
        audio_output_scheduler_profile(AudioOutputSchedulerMode::BufferedSource);
    std::uint64_t next_timestamp_ns_ = 0;
    bool initialized_ = false;
};


/*
 * Sample-locked cadence for editor monitor output.
 *
 * The delivery deadline advances from the previous deadline, never from the
 * actual wake time. This is critical on Windows: using `now + block` adds the
 * scheduler oversleep to every packet and periodically forces a timestamp
 * jump. Three packets are emitted immediately at reset to establish the same
 * 30 ms safety margin used by the working pre-threaded editor path. Small
 * wake-up jitter is recovered with a bounded contiguous catch-up; only a real
 * stall beyond the platform monitor tolerance requests a hard resync.
 */
class RealtimeMonitorCadence {
public:
    explicit RealtimeMonitorCadence(
        std::uint64_t block_ns = 10000000ULL,
        int prefill_packets = 3,
        std::uint64_t hard_resync_ns = 70000000ULL)
        : block_ns_(block_ns),
          prefill_packets_(std::max(0, prefill_packets)),
          hard_resync_ns_(hard_resync_ns)
    {
    }

    void reset(std::uint64_t now_ns)
    {
        next_timestamp_ns_ = now_ns;
        next_deadline_ns_ = now_ns;
        prefill_remaining_ = prefill_packets_;
        initialized_ = true;
    }

    void clear()
    {
        next_timestamp_ns_ = 0;
        next_deadline_ns_ = 0;
        prefill_remaining_ = 0;
        initialized_ = false;
    }

    bool initialized() const { return initialized_; }
    bool in_prefill() const { return prefill_remaining_ > 0; }
    int prefill_remaining() const { return prefill_remaining_; }
    std::uint64_t next_timestamp_ns() const { return next_timestamp_ns_; }
    std::uint64_t next_deadline_ns() const { return next_deadline_ns_; }
    std::uint64_t block_ns() const { return block_ns_; }

    bool ready(std::uint64_t now_ns) const
    {
        return initialized_ &&
               (prefill_remaining_ > 0 || now_ns >= next_deadline_ns_);
    }

    std::uint64_t wait_ns(std::uint64_t now_ns) const
    {
        if (!initialized_ || prefill_remaining_ > 0 ||
            now_ns >= next_deadline_ns_)
            return 0;
        return next_deadline_ns_ - now_ns;
    }

    std::uint64_t lateness_ns(std::uint64_t now_ns) const
    {
        if (!initialized_ || prefill_remaining_ > 0 ||
            now_ns <= next_deadline_ns_)
            return 0;
        return now_ns - next_deadline_ns_;
    }

    bool hard_resync_needed(std::uint64_t now_ns) const
    {
        return lateness_ns(now_ns) > hard_resync_ns_;
    }

    std::uint64_t take_timestamp()
    {
        if (!initialized_)
            return 0;

        const std::uint64_t timestamp = next_timestamp_ns_;
        next_timestamp_ns_ += block_ns_;

        if (prefill_remaining_ > 0) {
            --prefill_remaining_;
            if (prefill_remaining_ == 0)
                next_deadline_ns_ += block_ns_;
        } else {
            /* Absolute cadence: do not use wall-clock `now` here. */
            next_deadline_ns_ += block_ns_;
        }
        return timestamp;
    }

private:
    std::uint64_t block_ns_ = 10000000ULL;
    int prefill_packets_ = 3;
    std::uint64_t hard_resync_ns_ = 70000000ULL;
    std::uint64_t next_timestamp_ns_ = 0;
    std::uint64_t next_deadline_ns_ = 0;
    int prefill_remaining_ = 0;
    bool initialized_ = false;
};

inline std::uint64_t timeline_seconds_to_sample(double seconds,
                                                std::uint32_t sample_rate)
{
    if (seconds <= 0.0 || sample_rate == 0)
        return 0;
    return static_cast<std::uint64_t>(
        seconds * static_cast<double>(sample_rate) + 0.5);
}

} // namespace fxm::audio
