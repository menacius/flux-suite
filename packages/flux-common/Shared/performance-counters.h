#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace fxm::perf {

enum class Counter : std::size_t {
    ExternalUpdatesSubmitted,
    ExternalUpdatesCoalesced,
    ExternalUpdatesPublished,
    PatternCacheHits,
    PatternCacheMisses,
    PatternCacheEvictions,
    PatternRenderCacheHits,
    PatternRenderCacheMisses,
    FormattingRuleEvaluations,
    AudioCacheHits,
    AudioCacheMisses,
    AudioDecodeCancelled,
    AudioDecodedSamples,
    AudioMixBlocks,
    AudioMixNanoseconds,
    AudioOutputBlocks,
    AudioOutputUnderruns,
    AudioTimestampRepairs,
    ProxyJobsQueued,
    ProxyJobsCompleted,
    ProxyJobsCancelled,
    CachePlaybackHits,
    CachePlaybackMisses,
    DirtyRegionInvalidations,
    StingerVideoFrames,
    BezierEvaluations,
    MotionPathCacheHits,
    MotionPathCacheMisses,
    CacheStateFrameLookups,
    CacheStateIndexRebuilds,
    TimelineCacheFramesInspected,
    CacheRenderJobs,
    CacheRenderNanoseconds,
    GpuReadbackJobs,
    GpuReadbackNanoseconds,
    UiNotificationFlushes,
    UiNotificationsCoalesced,
    RenderQueuePeak,
    GizmoGeometryCacheHits,
    GizmoGeometryCacheMisses,
    EffectParameterResolutions,
    EffectParameterResolutionNanoseconds,
    EffectShaderCacheHits,
    EffectShaderCacheMisses,
    EffectBoundsEvaluations,
    EffectPasses,
    EffectPassNanoseconds,
    EffectSurfacePoolHits,
    EffectSurfacePoolMisses,
    EffectEmptyStackFastPaths,
    EffectOutputCacheHits,
    EffectOutputCacheMisses,
    EffectNoOpPassesSkipped,
    BackgroundJobsActive,
    Count
};

#ifndef NDEBUG
inline constexpr bool enabled = true;
#else
inline constexpr bool enabled = false;
#endif

inline std::array<std::atomic<std::uint64_t>,
                  static_cast<std::size_t>(Counter::Count)> &storage()
{
    static std::array<std::atomic<std::uint64_t>,
                      static_cast<std::size_t>(Counter::Count)> values{};
    return values;
}

inline const char *counter_name(Counter counter)
{
    switch (counter) {
    case Counter::ExternalUpdatesSubmitted: return "external_updates_submitted";
    case Counter::ExternalUpdatesCoalesced: return "external_updates_coalesced";
    case Counter::ExternalUpdatesPublished: return "external_updates_published";
    case Counter::PatternCacheHits: return "pattern_cache_hits";
    case Counter::PatternCacheMisses: return "pattern_cache_misses";
    case Counter::PatternCacheEvictions: return "pattern_cache_evictions";
    case Counter::PatternRenderCacheHits: return "pattern_render_cache_hits";
    case Counter::PatternRenderCacheMisses: return "pattern_render_cache_misses";
    case Counter::FormattingRuleEvaluations: return "formatting_rule_evaluations";
    case Counter::AudioCacheHits: return "audio_cache_hits";
    case Counter::AudioCacheMisses: return "audio_cache_misses";
    case Counter::AudioDecodeCancelled: return "audio_decode_cancelled";
    case Counter::AudioDecodedSamples: return "audio_decoded_samples";
    case Counter::AudioMixBlocks: return "audio_mix_blocks";
    case Counter::AudioMixNanoseconds: return "audio_mix_nanoseconds";
    case Counter::AudioOutputBlocks: return "audio_output_blocks";
    case Counter::AudioOutputUnderruns: return "audio_output_underruns";
    case Counter::AudioTimestampRepairs: return "audio_timestamp_repairs";
    case Counter::ProxyJobsQueued: return "proxy_jobs_queued";
    case Counter::ProxyJobsCompleted: return "proxy_jobs_completed";
    case Counter::ProxyJobsCancelled: return "proxy_jobs_cancelled";
    case Counter::CachePlaybackHits: return "cache_playback_hits";
    case Counter::CachePlaybackMisses: return "cache_playback_misses";
    case Counter::DirtyRegionInvalidations: return "dirty_region_invalidations";
    case Counter::StingerVideoFrames: return "stinger_video_frames";
    case Counter::BezierEvaluations: return "bezier_evaluations";
    case Counter::MotionPathCacheHits: return "motion_path_cache_hits";
    case Counter::MotionPathCacheMisses: return "motion_path_cache_misses";
    case Counter::CacheStateFrameLookups: return "cache_state_frame_lookups";
    case Counter::CacheStateIndexRebuilds: return "cache_state_index_rebuilds";
    case Counter::TimelineCacheFramesInspected: return "timeline_cache_frames_inspected";
    case Counter::CacheRenderJobs: return "cache_render_jobs";
    case Counter::CacheRenderNanoseconds: return "cache_render_nanoseconds";
    case Counter::GpuReadbackJobs: return "gpu_readback_jobs";
    case Counter::GpuReadbackNanoseconds: return "gpu_readback_nanoseconds";
    case Counter::UiNotificationFlushes: return "ui_notification_flushes";
    case Counter::UiNotificationsCoalesced: return "ui_notifications_coalesced";
    case Counter::RenderQueuePeak: return "render_queue_peak";
    case Counter::GizmoGeometryCacheHits: return "gizmo_geometry_cache_hits";
    case Counter::GizmoGeometryCacheMisses: return "gizmo_geometry_cache_misses";
    case Counter::EffectParameterResolutions: return "effect_parameter_resolutions";
    case Counter::EffectParameterResolutionNanoseconds: return "effect_parameter_resolution_ns";
    case Counter::EffectShaderCacheHits: return "effect_shader_cache_hits";
    case Counter::EffectShaderCacheMisses: return "effect_shader_cache_misses";
    case Counter::EffectBoundsEvaluations: return "effect_bounds_evaluations";
    case Counter::EffectPasses: return "effect_passes";
    case Counter::EffectPassNanoseconds: return "effect_pass_ns";
    case Counter::EffectSurfacePoolHits: return "effect_surface_pool_hits";
    case Counter::EffectSurfacePoolMisses: return "effect_surface_pool_misses";
    case Counter::EffectEmptyStackFastPaths: return "effect_empty_stack_fast_paths";
    case Counter::EffectOutputCacheHits: return "effect_output_cache_hits";
    case Counter::EffectOutputCacheMisses: return "effect_output_cache_misses";
    case Counter::EffectNoOpPassesSkipped: return "effect_noop_passes_skipped";
    case Counter::BackgroundJobsActive: return "background_jobs_active";
    case Counter::Count: break;
    }
    return "unknown";
}

inline void add(Counter counter, std::uint64_t amount = 1)
{
#ifndef NDEBUG
    storage()[static_cast<std::size_t>(counter)].fetch_add(
        amount, std::memory_order_relaxed);
#else
    (void)counter;
    (void)amount;
#endif
}


inline void set_max(Counter counter, std::uint64_t candidate)
{
#ifndef NDEBUG
    auto &slot = storage()[static_cast<std::size_t>(counter)];
    std::uint64_t current = slot.load(std::memory_order_relaxed);
    while (current < candidate &&
           !slot.compare_exchange_weak(current, candidate,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
    }
#else
    (void)counter;
    (void)candidate;
#endif
}

inline void subtract(Counter counter, std::uint64_t amount = 1)
{
#ifndef NDEBUG
    storage()[static_cast<std::size_t>(counter)].fetch_sub(
        amount, std::memory_order_relaxed);
#else
    (void)counter;
    (void)amount;
#endif
}

inline std::uint64_t value(Counter counter)
{
#ifndef NDEBUG
    return storage()[static_cast<std::size_t>(counter)].load(
        std::memory_order_relaxed);
#else
    (void)counter;
    return 0;
#endif
}

inline void reset()
{
#ifndef NDEBUG
    for (auto &entry : storage())
        entry.store(0, std::memory_order_relaxed);
#endif
}

inline std::string snapshot_text()
{
#ifndef NDEBUG
    std::ostringstream stream;
    bool first = true;
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(Counter::Count); ++index) {
        const auto counter = static_cast<Counter>(index);
        const std::uint64_t current = storage()[index].load(
            std::memory_order_relaxed);
        if (!first)
            stream << ' ';
        first = false;
        stream << counter_name(counter) << '=' << current;
    }
    return stream.str();
#else
    return {};
#endif
}

class ScopedTimer {
public:
    explicit ScopedTimer(Counter destination)
#ifndef NDEBUG
        : destination_(destination), start_(std::chrono::steady_clock::now())
#endif
    {
#ifdef NDEBUG
        (void)destination;
#endif
    }

    ~ScopedTimer()
    {
#ifndef NDEBUG
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start_).count();
        if (elapsed > 0)
            add(destination_, static_cast<std::uint64_t>(elapsed));
#endif
    }

private:
#ifndef NDEBUG
    Counter destination_;
    std::chrono::steady_clock::time_point start_;
#endif
};

class BackgroundJobScope {
public:
    BackgroundJobScope()
    {
        add(Counter::BackgroundJobsActive);
    }
    ~BackgroundJobScope()
    {
        subtract(Counter::BackgroundJobsActive);
    }
};

} // namespace fxm::perf
