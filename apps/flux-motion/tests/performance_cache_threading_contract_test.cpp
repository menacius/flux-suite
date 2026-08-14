#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require_contains(const std::string &text, const std::string &needle,
                      const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing performance/threading contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle,
                    const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Forbidden performance/threading path remains: " << label
              << " (" << needle << ")\n";
    return false;
}

std::string section(const std::string &text, const std::string &begin,
                    const std::string &end)
{
    const auto first = text.find(begin);
    if (first == std::string::npos)
        return {};
    const auto last = text.find(end, first + begin.size());
    if (last == std::string::npos)
        return text.substr(first);
    return text.substr(first, last - first);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 23) {
        std::cerr << "usage: performance_cache_threading_contract_test "
                     "<external-provider> <external-manager> <pattern-cache> "
                     "<rich-text> <audio-h> <audio-cpp> <animation> <canvas-h> "
                     "<motion-path> <content-hash> <disk-writer> <dirty-region> <worker> "
                     "<render-queue> <plugin> <editor-session> <stinger> "
                     "<gpu-readback> <source-tick> <cache-policy> <schema> <pattern-render>\n";
        return 2;
    }

    const std::string external_provider = read_file(argv[1]);
    const std::string external_manager = read_file(argv[2]);
    const std::string pattern_cache = read_file(argv[3]);
    const std::string rich_text = read_file(argv[4]);
    const std::string audio_h = read_file(argv[5]);
    const std::string audio_cpp = read_file(argv[6]);
    const std::string animation = read_file(argv[7]);
    const std::string canvas_h = read_file(argv[8]);
    const std::string motion_path = read_file(argv[9]);
    const std::string content_hash_source = read_file(argv[10]);
    const std::string disk_writer = read_file(argv[11]);
    const std::string dirty_region = read_file(argv[12]);
    const std::string worker = read_file(argv[13]);
    const std::string render_queue = read_file(argv[14]);
    const std::string plugin = read_file(argv[15]);
    const std::string editor_session = read_file(argv[16]);
    const std::string stinger = read_file(argv[17]);
    const std::string gpu_readback = read_file(argv[18]);
    const std::string source_tick = read_file(argv[19]);
    const std::string cache_policy = read_file(argv[20]);
    const std::string schema = read_file(argv[21]);
    const std::string pattern_render = read_file(argv[22]);

    bool ok = true;

    ok &= require_contains(external_provider, "controller->moveToThread(&thread)",
                           "network/file providers live on a dedicated QThread");
    ok &= require_contains(external_provider, "kMinimumPublishCoalesceMs = 16",
                           "provider bursts are frame-coalesced");
    ok &= require_contains(external_provider, "if (!publish_timer_.isActive())",
                           "one pending provider publication timer");
    ok &= require_contains(external_manager, "render_queue_index_",
                           "render updates coalesce by source/field key");
    ok &= require_contains(external_manager, "ExternalUpdatesCoalesced",
                           "external coalescing counter");

    ok &= require_contains(pattern_cache, "const std::size_t capacity_ = 256",
                           "bounded pattern resource cache");
    ok &= require_contains(pattern_cache, "std::list<std::string> lru_",
                           "pattern cache LRU");
    ok &= require_contains(rich_text, "cached_pattern(",
                           "formatting detection reuses compiled patterns");
    ok &= require_contains(rich_text, "FormattingRuleEvaluations",
                           "formatting debug counter");
    ok &= require_contains(pattern_render, "checkerboard_tile_",
                           "canvas pattern tile is retained between paints");
    ok &= require_contains(pattern_render, "PatternRenderCacheHits",
                           "render-pattern cache hit counter");
    ok &= require_contains(pattern_render, "PatternRenderCacheMisses",
                           "render-pattern cache miss counter");

    ok &= require_contains(audio_h, "std::thread output_worker_",
                           "audio output/mix worker");
    ok &= require_contains(audio_cpp,
                           "std::weak_ptr<const DecodedAudioAsset>",
                           "decoded audio cache does not own assets forever");
    ok &= require_contains(audio_cpp, "fmt->interrupt_callback.callback",
                           "FFmpeg decode cooperative cancellation");
    ok &= require_contains(audio_cpp, "decode_epoch_",
                           "stale audio decodes are generation-cancelled");
    ok &= require_contains(audio_cpp, "void SourceAudioRuntime::output_worker_main()",
                           "mix loop is off video_tick");
    const std::string pump = section(audio_cpp,
        "void SourceAudioRuntime::pump()",
        "int64_t SourceAudioRuntime::duration_ms()");
    ok &= require_absent(pump, "mix_block(",
                         "video_tick pump must not mix audio");
    ok &= require_contains(audio_cpp, "output_worker_.join()",
                           "audio output worker joins on source close");
    ok &= require_contains(audio_cpp, "worker_.join()",
                           "audio decode worker joins on source close");
    ok &= require_contains(audio_cpp, "swr_free(&swr)",
                           "failed FFmpeg resampler allocation is released");
    ok &= require_contains(plugin, "SourceAudioRuntime::clear_shared_cache()",
                           "audio cache cleanup on OBS shutdown");

    ok &= require_contains(animation, "std::upper_bound(",
                           "Bezier segment lookup is logarithmic");
    ok &= require_contains(canvas_h, "MotionPathSampleCache",
                           "motion-path UI cache exists only in canvas code");
    ok &= require_contains(motion_path, "motion_path_canvas_samples(",
                           "paint and hit-test share motion-path samples");
    ok &= require_contains(motion_path, "invalidate_motion_path_sample_cache()",
                           "motion-path cache has explicit invalidation");

    const std::string content_hash = section(content_hash_source,
        "QString CacheManager::contentHash(const Title &title) const",
        "QString CacheManager::evaluatedVisualStateHash");
    ok &= require_absent(content_hash, "proxy_metadata",
                         "source proxy metadata must not alter visual hash");
    ok &= require_contains(content_hash, "Source proxy state",
                           "proxy-only hash exclusion is documented");
    ok &= require_contains(dirty_region, "void CacheManager::markDirtyTiles",
                           "dirty-region tile invalidation remains enabled");
    ok &= require_contains(dirty_region, "DirtyRegionInvalidations",
                           "dirty-region debug counter");

    ok &= require_contains(disk_writer, "void DiskFrameCache::cancelTitleWrites",
                           "per-title pending proxy write cancellation");
    ok &= require_contains(disk_writer, "writeJobCurrent(identity)",
                           "dequeued writes revalidate generation before commit");
    ok &= require_contains(worker, "disk_cache_.cancelTitleWrites(title_id)",
                           "title close/delete cancels disk writes");
    ok &= require_contains(worker, "job_snapshots_.erase(it)",
                           "title snapshot release");
    ok &= require_contains(worker, "dirty_tiles_by_frame_.erase(it)",
                           "title dirty-region release");
    ok &= require_contains(worker, "title_gpu_render_session_cancel_readback",
                           "stale GPU readbacks cancel without mapping");
    ok &= require_contains(gpu_readback,
                           "void title_gpu_render_session_cancel_readback",
                           "non-blocking staging-slot cancellation API");
    ok &= require_contains(render_queue, "ProxyJobsCancelled",
                           "proxy cancellation counter");
    ok &= require_contains(editor_session, "cancelTitleWork(",
                           "editor close cancels title-specific background jobs");
    const std::string cancel_title_work = section(worker,
        "void CacheManager::cancelTitleWork",
        "void CacheManager::removeTitleCache");
    ok &= require_contains(cancel_title_work, "disk_cache_.cancelTitleWrites(title_id)",
                           "title close cancels pending disk-cache commits");

    ok &= require_contains(stinger, "StingerVideoFrames",
                           "Stinger render debug counter");
    const std::string stinger_render = section(stinger,
        "static void stinger_video_render", "static double cubic_bezier_coordinate");
    ok &= require_absent(stinger_render, "QFile",
                         "no file access in Stinger video render");
    ok &= require_absent(stinger_render, "QNetwork",
                         "no network access in Stinger video render");
    ok &= require_absent(stinger_render, "avformat",
                         "no media decode in Stinger video render");

    const std::string tick_audio = section(source_tick,
        "if (data->audio_runtime)", "const uint64_t external_revision");
    ok &= require_contains(tick_audio, "audio_runtime->pump();",
                           "video tick only wakes async audio output");
    ok &= require_absent(tick_audio, "mix_block",
                         "no direct audio mix in source tick");
    ok &= require_contains(cache_policy, "CachePlaybackHits",
                           "cache playback hit counter");
    ok &= require_contains(cache_policy, "CachePlaybackMisses",
                           "cache playback miss counter");

    ok &= require_contains(plugin, "CacheManager::instance().shutdownWorker()",
                           "cache worker stops before resource cleanup");
    ok &= require_contains(plugin, "clear_pattern_resource_cache()",
                           "pattern cache cleanup on OBS shutdown");
    ok &= require_contains(plugin, "snapshot_text()",
                           "debug performance counters emitted on shutdown");
    ok &= require_contains(schema, "kCurrentDevelopmentVersion = 243",
                           "Development Version 217 migration ledger");

    if (ok)
        std::cout << "performance/cache/threading contract: PASS\n";
    return ok ? 0 : 1;
}
