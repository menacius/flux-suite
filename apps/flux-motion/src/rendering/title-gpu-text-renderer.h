#pragma once

#include "title-text-layout.h"
#include "text-animator.h"

#include <graphics/graphics.h>

#include <memory>
#include <string>
#include <vector>

namespace fxm::gpu_text {

struct PrepareOptions {
    float logical_width = 0.0f;
    float logical_height = 0.0f;
    float text_offset_x = 0.0f;
    float text_offset_y = 0.0f;
    float text_width = 0.0f;
    float text_height = 0.0f;
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_width = 0.0f;
    float clip_height = 0.0f;
    float raster_scale = 1.0f;
};

/* Per-layer GPU text state. CPU shaping/paint batches are replaced atomically
 * into the inactive target; the active target remains sampleable until the
 * replacement draw has completed. GPU resources are released explicitly by
 * Renderer::release_layer while the OBS graphics context is held. */
class Layer {
public:
    Layer();
    ~Layer();
    Layer(Layer &&) noexcept;
    Layer &operator=(Layer &&) noexcept;
    Layer(const Layer &) = delete;
    Layer &operator=(const Layer &) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class Renderer;
};

/* Session-local persistent glyph atlas + SDF material renderer. QRawFont is
 * used only to populate missing atlas glyphs; frame presentation never calls
 * QTextDocument::drawContents, QPainter text rasterization or Cairo text. */
class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(Renderer &&) noexcept;
    Renderer &operator=(Renderer &&) noexcept;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;

    bool prepare(Layer &layer, const ImmutableTextLayout &layout,
                 const std::vector<TextLayoutPaintRun> &paint_runs,
                 const PrepareOptions &options,
                 const TextAnimatorEvaluation *animation = nullptr,
                 std::string *reason = nullptr);

    /* Must be called under the OBS graphics context. */
    bool compile_effect();
    bool effect_ready() const;
    bool render(Layer &layer);
    void release_layer(Layer &layer);
    void reset();

    gs_texture_t *texture(const Layer &layer) const;
    uint32_t texture_width(const Layer &layer) const;
    uint32_t texture_height(const Layer &layer) const;
    bool owns_texture(const Layer &layer, const gs_texture_t *texture) const;

    bool backend_available() const;
    const char *last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fxm::gpu_text
