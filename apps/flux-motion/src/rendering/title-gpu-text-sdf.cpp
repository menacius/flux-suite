#include "title-gpu-text-sdf.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace fxm::gpu_text {

TextStrokeCoverageExtents text_stroke_coverage_extents(float width,
                                                       int alignment)
{
    width = std::max(0.0f, width);
    switch (std::clamp(alignment, 0, 2)) {
    case 0: return {width, 0.0f};
    case 2: return {0.0f, width};
    case 1:
    default: return {width * 0.5f, width * 0.5f};
    }
}

int text_stroke_draw_phase(bool on_front)
{
    return on_front ? 2 : 0;
}

namespace {

constexpr float kInfinity = 1.0e20f;

struct GlyphSdfWorkspace {
    std::vector<uint8_t> alpha;
    std::vector<float> first_pass;
    std::vector<float> distance_inside;
    std::vector<float> distance_outside;
    std::vector<float> input;
    std::vector<float> output;
    std::vector<int> sites;
    std::vector<float> boundaries;
};

static thread_local GlyphSdfWorkspace glyph_sdf_workspace;

void distance_transform_1d(const float *input, float *output,
                           int count, std::vector<int> &sites,
                           std::vector<float> &boundaries)
{
    int k = -1;
    for (int q = 0; q < count; ++q) {
        if (input[q] >= kInfinity * 0.5f)
            continue;
        float intersection = -kInfinity;
        while (k >= 0) {
            const int p = sites[k];
            const float qf = static_cast<float>(q);
            const float pf = static_cast<float>(p);
            intersection = ((input[q] + qf * qf) -
                            (input[p] + pf * pf)) /
                           (2.0f * static_cast<float>(q - p));
            if (intersection > boundaries[k])
                break;
            --k;
        }
        ++k;
        sites[k] = q;
        boundaries[k] = k == 0 ? -kInfinity : intersection;
        boundaries[k + 1] = kInfinity;
    }
    if (k < 0) {
        std::fill(output, output + count, kInfinity);
        return;
    }
    int site = 0;
    for (int q = 0; q < count; ++q) {
        while (boundaries[site + 1] < static_cast<float>(q))
            ++site;
        const float delta = static_cast<float>(q - sites[site]);
        output[q] = delta * delta + input[sites[site]];
    }
}

void squared_distance_field(const std::vector<uint8_t> &mask,
                            int width, int height,
                            bool feature_is_inside,
                            std::vector<float> &result,
                            GlyphSdfWorkspace &workspace)
{
    const size_t count = static_cast<size_t>(width) * height;
    workspace.first_pass.resize(count);
    result.resize(count);
    std::fill(workspace.first_pass.begin(), workspace.first_pass.end(), kInfinity);
    std::fill(result.begin(), result.end(), kInfinity);
    const int maximum = std::max(width, height);
    workspace.input.resize(maximum);
    workspace.output.resize(maximum);
    workspace.sites.resize(maximum);
    workspace.boundaries.resize(static_cast<size_t>(maximum) + 1);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool inside = mask[static_cast<size_t>(y) * width + x] >= 128;
            workspace.input[x] = inside == feature_is_inside ? 0.0f : kInfinity;
        }
        distance_transform_1d(workspace.input.data(), workspace.output.data(),
                              width, workspace.sites, workspace.boundaries);
        for (int x = 0; x < width; ++x)
            workspace.first_pass[static_cast<size_t>(y) * width + x] =
                workspace.output[x];
    }

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y)
            workspace.input[y] =
                workspace.first_pass[static_cast<size_t>(y) * width + x];
        distance_transform_1d(workspace.input.data(), workspace.output.data(),
                              height, workspace.sites, workspace.boundaries);
        for (int y = 0; y < height; ++y)
            result[static_cast<size_t>(y) * width + x] = workspace.output[y];
    }
}

} // namespace

std::vector<uint8_t> build_glyph_sdf(const uint8_t *source, int source_width,
                                     int source_height, int source_stride,
                                     int spread, int &output_width,
                                     int &output_height)
{
    output_width = 0;
    output_height = 0;
    if (!source || source_width <= 0 || source_height <= 0 ||
        source_stride < source_width || spread <= 0)
        return {};

    constexpr int64_t kGuardPixels = 2;
    constexpr size_t kMaximumGlyphPixels = 64u * 1024u * 1024u;
    const int64_t padding64 = static_cast<int64_t>(spread) + kGuardPixels;
    const int64_t output_width64 = static_cast<int64_t>(source_width) +
                                   padding64 * 2;
    const int64_t output_height64 = static_cast<int64_t>(source_height) +
                                    padding64 * 2;
    if (padding64 > std::numeric_limits<int>::max() ||
        output_width64 > std::numeric_limits<int>::max() ||
        output_height64 > std::numeric_limits<int>::max())
        return {};

    const int padding = static_cast<int>(padding64);
    output_width = static_cast<int>(output_width64);
    output_height = static_cast<int>(output_height64);
    const size_t output_width_size = static_cast<size_t>(output_width);
    const size_t output_height_size = static_cast<size_t>(output_height);
    if (output_width_size >
        std::numeric_limits<size_t>::max() / output_height_size ||
        output_width_size * output_height_size > kMaximumGlyphPixels) {
        output_width = 0;
        output_height = 0;
        return {};
    }
    GlyphSdfWorkspace &workspace = glyph_sdf_workspace;
    const size_t pixel_count = output_width_size * output_height_size;
    workspace.alpha.resize(pixel_count);
    std::fill(workspace.alpha.begin(), workspace.alpha.end(), 0);
    for (int y = 0; y < source_height; ++y) {
        const uint8_t *row = source + static_cast<size_t>(y) * source_stride;
        std::copy(row, row + source_width,
                  workspace.alpha.begin() +
                      static_cast<size_t>(y + padding) * output_width + padding);
    }

    squared_distance_field(workspace.alpha, output_width, output_height, true,
                           workspace.distance_inside, workspace);
    squared_distance_field(workspace.alpha, output_width, output_height, false,
                           workspace.distance_outside, workspace);
    std::vector<uint8_t> sdf(pixel_count, 0);
    const float denominator = static_cast<float>(spread) * 2.0f;
    for (size_t i = 0; i < sdf.size(); ++i) {
        float signed_distance = std::sqrt(workspace.distance_outside[i]) -
                                std::sqrt(workspace.distance_inside[i]);
        signed_distance += static_cast<float>(workspace.alpha[i]) / 255.0f - 0.5f;
        const float normalized = std::clamp(
            0.5f + signed_distance / denominator, 0.0f, 1.0f);
        sdf[i] = static_cast<uint8_t>(std::lround(normalized * 255.0f));
    }
    return sdf;
}

} // namespace fxm::gpu_text
