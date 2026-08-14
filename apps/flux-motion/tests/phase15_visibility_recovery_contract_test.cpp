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
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    const std::string title_source = read_file(argv[1]);
    const std::string text_renderer = read_file(argv[2]);
    const std::string cache_source = read_file(argv[3]);
    const std::string preferences = read_file(argv[4]);
    const std::string cmake = read_file(argv[5]);

    require(title_source, "kGpuPrimitiveShapeEffect");
    require(title_source, "render_gpu_primitive_raster");
    require(title_source, "render_gpu_text_raster");
    require(title_source, "apply_gpu_layer_effect_stack");
    require(title_source, "render_layer_unmasked");
    require(text_renderer, "kGpuTextEffect");
    require(cache_source, "gpu-renderer-v31-lens-flare-dx11-keyword-fix");
    require(preferences, "fxm::system_memory::clamp_cache_ram_mb");
    require(cmake, "../../packages/flux-common/Shared/system-memory.cpp");
    reject(cmake, "title-gpu-vector-renderer.cpp");

    std::cout << "Phase 15 runtime visibility recovery contract passed\n";
    return 0;
}
