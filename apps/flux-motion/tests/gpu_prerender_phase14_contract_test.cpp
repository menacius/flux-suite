#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {


void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) { std::cerr << "missing: " << needle << "\n"; assert(false); }
}

void reject(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) { std::cerr << "forbidden: " << needle << "\n"; assert(false); }
}

void test_gpu_graph_and_readback_contract(const std::string &source,
                                          const std::string &header)
{
    require(header, "struct TitleGpuReadbackTicket");
    require(header, "render_title_gpu_cache_submit_readback");
    require(header, "title_gpu_render_session_resolve_readback");
    require(header, "title_gpu_frame_cache_contains");
    require(header, "title_gpu_render_session_submit_gpu_cached_frame");

    require(source, "std::array<AsyncReadbackSlot, 3> readback_slots");
    require(source, "render_gpu_session_locked(session)");
    require(source, "ScopedGpuReadbackContract final_frame_only");
    require(source, "GpuReadbackContract::FinalFrameOnly");
    require(source, "gs_stage_texture(slot.stage, readback_texture)");
    require(source, "gs_stagesurface_map(slot->stage");
    require(header, "title_gpu_frame_cache_store_image");
    require(source, "store_global_gpu_frame_tiles_locked");
    require(source, "draw_global_gpu_frame_locked");
}

void test_worker_contract(const std::string &source,
                          const std::string &header)
{
    require(header, "bool enqueuePut(const CacheFrameKey &key, const QImage &image)");
    require(header, "std::thread writer_thread_");
    require(source, "pending_readbacks.size() >= 3");
    require(source, "resolveOldestGpuReadback");
    require(source, "title_gpu_frame_cache_store_image(");
    require(source, "disk_cache_.enqueuePut(job.key, image)");
    require(source, "putForGeneration(job.key, job.image, job.generation,");
    require(source, "job.generation == writer_generation_.load");
    require(source, "dirty_pixels * 100 < full_pixels * 60");
    require(source, "previous_key.content_hash = previous_hash");
    require(source, "last_visual_hash_by_title_.value(job.key.title_id)");
    require(source, "title_requires_full_gpu_cache_frame");
    require(source, "animated_vec2_extents(layer.position)");
    require(source, "animated_scalar_max_nonnegative");
    require(source, "layer->mask_mode != MaskMode::None");
    require(source, "gpu-renderer-v31-lens-flare-dx11-keyword-fix");
    require(source, "std::memcpy(");

    reject(source, "QImage CacheManager::renderDirtyTiles");
    reject(source, "QImage CacheManager::mergeDirtyTiles");
    reject(source, "disk_cache_.put(job.key, image)");
    require(source, "ram_cache_.put(job.key, image)");
    reject(source, "#include <QPainter>");
}

void test_direct_gpu_playback_contract(const std::string &source,
                                       const std::string &canvas)
{
    require(source, "requestFrameGpuToken");
    require(source, "title_gpu_render_session_submit_gpu_cached_frame");
    require(canvas, "requestFrameGpuToken");
    require(canvas, "title_gpu_render_session_submit_gpu_cached_frame");
}

} // namespace

int main(int argc, char **argv)
{
    assert(argc == 6);
    const std::string title_source = read_file(argv[1]);
    const std::string title_header = read_file(argv[2]);
    const std::string cache_source = read_file(argv[3]);
    const std::string cache_header = read_file(argv[4]);
    const std::string canvas_source = read_file(argv[5]);
    test_gpu_graph_and_readback_contract(title_source, title_header);
    test_worker_contract(cache_source, cache_header);
    test_direct_gpu_playback_contract(title_source, canvas_source);
    return 0;
}
