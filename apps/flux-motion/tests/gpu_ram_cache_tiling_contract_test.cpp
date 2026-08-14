#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}
void reject(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "forbidden: " << needle << '\n';
        assert(false);
    }
}
void reject_between(const std::string &source, const char *begin,
                    const char *end, const char *needle)
{
    const std::size_t first = source.find(begin);
    const std::size_t last = first == std::string::npos
        ? std::string::npos : source.find(end, first);
    assert(first != std::string::npos && last != std::string::npos);
    if (source.substr(first, last - first).find(needle) != std::string::npos) {
        std::cerr << "forbidden in " << begin << ": " << needle << '\n';
        assert(false);
    }
}
}

int main(int argc, char **argv)
{
    assert(argc == 5);
    const std::string title_source = read_file(argv[1]);
    const std::string title_header = read_file(argv[2]);
    const std::string cache_source = read_file(argv[3]);
    const std::string cache_header = read_file(argv[4]);

    require(title_source, "static constexpr int kGpuRamTileSize = 128");
    require(title_source, "struct GpuRamTileEntry");
    require(title_source, "struct GpuRamTileRef");
    require(title_source, "std::unordered_map<std::string, GpuRamTileEntry> g_gpu_ram_tiles");
    require(title_source, "extract_nonempty_tiles(");
    require(title_source, "store_global_gpu_frame_tiles_locked");
    require(title_source, "release_gpu_ram_frame_locked");
    require(title_source, "references = 1");
    require(title_source, "QImage::Format_ARGB32_Premultiplied");
    require(title_source, "title_gpu_frame_cache_store_image(");
    require(title_source, "for (const GpuRamTileRef &reference : found->second.tiles)");
    require(title_source, "cached_frame_texture_budget = 32ull * 1024ull * 1024ull");
    require(title_header, "shared 128x128");
    require(title_header, "title_gpu_frame_cache_store_image");

    require(cache_source, "const bool gpu_ram_stored = title_gpu_frame_cache_store_image(");
    require(cache_source, "A CPU/disk-resident frame does not need to be rerendered");
    require(cache_source, "in_flight >= writer_queue_limit_");
    require(cache_source, "writer_pending_bytes_ <= writer_queue_budget_ - job.bytes");
    reject(cache_source, "writer_space_cv_.wait");
    require(cache_source, "16ull * 1024ull * 1024ull");
    require(cache_source, "ram_cache_.bytesUsed()");
    require(cache_header, "writer_queue_budget_ = 128ull * 1024ull * 1024ull");
    require(cache_header, "quint64 writer_pending_bytes_ = 0");

    reject(title_source, "store_global_gpu_frame_locked");
    reject(title_source, "Each entry owns a stable texrender target");
    reject_between(title_source, "struct GpuRamFrameEntry {", "};",
                   "gs_texrender_t");
    reject_between(title_source, "struct GpuRamFrameEntry {", "};",
                   "gs_texture_t");

    std::cout << "GPU RAM sparse tile and bounded writer contract passed\n";
    return 0;
}
