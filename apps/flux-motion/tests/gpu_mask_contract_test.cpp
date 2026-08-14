#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>

#include "source_bundle_reader.h"

namespace {

struct PremultipliedPixel {
    double r;
    double g;
    double b;
    double a;
};

double gpu_mask_value(const PremultipliedPixel &pixel, int mode)
{
    double value = pixel.a;
    if (mode == 3 || mode == 4) {
        const double inv_alpha = pixel.a > 1e-6 ? 1.0 / pixel.a : 0.0;
        const double straight_r = pixel.r * inv_alpha;
        const double straight_g = pixel.g * inv_alpha;
        const double straight_b = pixel.b * inv_alpha;
        value = (straight_r * 0.2126 + straight_g * 0.7152 +
                 straight_b * 0.0722) * pixel.a;
    }
    if (mode == 2 || mode == 4 || mode == 6)
        value = 1.0 - value;
    return std::clamp(value, 0.0, 1.0);
}

bool near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}


void test_mask_modes()
{
    const PremultipliedPixel half_red {0.5, 0.0, 0.0, 0.5};
    assert(near(gpu_mask_value(half_red, 1), 0.5));
    assert(near(gpu_mask_value(half_red, 2), 0.5));
    assert(near(gpu_mask_value(half_red, 3), 0.2126 * 0.5));
    assert(near(gpu_mask_value(half_red, 4), 1.0 - 0.2126 * 0.5));
    assert(near(gpu_mask_value(half_red, 5), 0.5));
    assert(near(gpu_mask_value(half_red, 6), 0.5));

    const PremultipliedPixel transparent {0.0, 0.0, 0.0, 0.0};
    assert(near(gpu_mask_value(transparent, 1), 0.0));
    assert(near(gpu_mask_value(transparent, 2), 1.0));
    assert(near(gpu_mask_value(transparent, 3), 0.0));
    assert(near(gpu_mask_value(transparent, 4), 1.0));
    assert(near(gpu_mask_value(transparent, 5), 0.0));
    assert(near(gpu_mask_value(transparent, 6), 1.0));
}

void test_source_contract(const std::string &source)
{
    assert(source.find("static constexpr const char *kGpuMaskEffect") !=
           std::string::npos);
    assert(source.find("maskMode == 3 || maskMode == 4") !=
           std::string::npos);
    assert(source.find("maskMode == 2 || maskMode == 4 || maskMode == 6") !=
           std::string::npos);
    assert(source.find("render_gpu_mask_graph_texture") !=
           std::string::npos);
    assert(source.find("MaskTextureCacheEntry") != std::string::npos);
    assert(source.find("mask_texture_cache") != std::string::npos);
    assert(source.find("effect_stack_respects_masks") != std::string::npos);
    assert(source.find("layer_is_clipping_matte_source") != std::string::npos);
    assert(source.find("MaskMode::InvertedClipping") != std::string::npos);
    assert(source.find("emitted_clipping_bases") != std::string::npos);
    assert(source.find("composite_clipping_base") != std::string::npos);
    assert(source.find("base_layer->matte_visibility_mode !=") !=
           std::string::npos);
    assert(source.find("MatteVisibilityMode::VisibleAndMatte") !=
           std::string::npos);
    assert(source.find("enum class GpuMaskGraphPurpose") != std::string::npos);
    assert(source.find("GpuMaskGraphPurpose::ClippingShape") != std::string::npos);
    assert(source.find("|purpose=") != std::string::npos);
    assert(source.find("const bool clipping_shape") != std::string::npos);
    assert(source.find("clipping_shape ? 1.0") != std::string::npos);
    assert(source.find("mask_mode_is_clipping(mode)") != std::string::npos);
    assert(source.find("including 0%") != std::string::npos);
    assert(source.find("Visible-base artwork is") != std::string::npos);
    assert(source.find("MatteOnly still supplies alpha") !=
           std::string::npos);
    assert(source.find("cairo_mask_surface") == std::string::npos);
    assert(source.find("render_layer_with_mask") == std::string::npos);
    assert(source.find("convert_argb32_surface_to_luma_alpha_mask") ==
           std::string::npos);
    assert(source.find("render_gpu_scene_mask_base_raster") ==
           std::string::npos);
    assert(source.find("kMaximumMaskTextures") != std::string::npos);

    const std::size_t apply_mask = source.find("static gs_texture_t *apply_gpu_mask");
    const std::size_t copy_mask = source.find("static bool copy_full_canvas_gpu_texture");
    assert(apply_mask != std::string::npos);
    assert(copy_mask != std::string::npos);
    const std::string apply_body = source.substr(apply_mask, copy_mask - apply_mask);
    assert(apply_body.find("gs_matrix_push()") != std::string::npos);
    assert(apply_body.find("gs_matrix_identity()") != std::string::npos);
    assert(apply_body.find("gs_matrix_pop()") != std::string::npos);
    assert(source.find("layer_texture == masked_storage") != std::string::npos);
    assert(source.find("mask_texture == masked_storage") != std::string::npos);
    assert(source.find("GPU read/write feedback loop") != std::string::npos);
    const std::size_t copy_end = source.find("enum class ExternalBackgroundMapping", copy_mask);
    assert(copy_end != std::string::npos);
    const std::string copy_body = source.substr(copy_mask, copy_end - copy_mask);
    assert(copy_body.find("gs_matrix_push()") != std::string::npos);
    assert(copy_body.find("gs_matrix_identity()") != std::string::npos);
    assert(copy_body.find("gs_matrix_pop()") != std::string::npos);
}

void test_editor_first_frame_contract(const std::string &source)
{
    const std::size_t set_title = source.find("void CanvasPreview::set_title");
    assert(set_title != std::string::npos);
    const std::size_t next_function = source.find(
        "CanvasPreview::ViewState CanvasPreview::view_state", set_title);
    assert(next_function != std::string::npos);
    const std::string body = source.substr(set_title, next_function - set_title);
    assert(body.find("title_gpu_render_session_update") != std::string::npos);
    assert(body.find("gpu_model_dirty_ = true") != std::string::npos);
}

} // namespace

int main(int argc, char **argv)
{
    assert(argc == 3);
    test_mask_modes();
    test_source_contract(read_file(argv[1]));
    test_editor_first_frame_contract(read_file(argv[2]));
    return 0;
}
