#pragma once

#include <cstdint>
#include <vector>

namespace fxm::gpu_text {

struct TextStrokeCoverageExtents {
    float outside = 0.0f;
    float inside = 0.0f;
};

/* Text-only SDF stroke contract. Alignment: 0=outer, 1=mid, 2=inner.
 * Order phase: 0=behind fill, 2=in front of fill. */
TextStrokeCoverageExtents text_stroke_coverage_extents(float width,
                                                       int alignment);
int text_stroke_draw_phase(bool on_front);

/* Builds an 8-bit signed-distance field with 0.5 at the glyph boundary,
 * values above 0.5 inside, and values below 0.5 outside. The returned image is
 * padded by spread + 2 pixels on every edge. */
std::vector<uint8_t> build_glyph_sdf(const uint8_t *alpha, int width,
                                     int height, int stride, int spread,
                                     int &output_width,
                                     int &output_height);

} // namespace fxm::gpu_text
