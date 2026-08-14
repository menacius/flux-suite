#pragma once

#include "title-rich-text.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>


/* Only properties that can change glyph choice, advances, bidi resolution or
 * line geometry participate in the shaping cache. Paint-only properties are
 * resolved from the evaluated RichTextDocument by the GPU material stage. */
constexpr uint32_t RichTextCharShapingMask =
    RichTextCharFontFamily | RichTextCharFontStyle | RichTextCharFontSize |
    RichTextCharBold | RichTextCharItalic | RichTextCharKerning |
    RichTextCharTracking | RichTextCharScaleX | RichTextCharScaleY |
    RichTextCharBaselineShift | RichTextCharTextStyle |
    RichTextCharLigatures | RichTextCharStylisticAlternates |
    RichTextCharFractions | RichTextCharOpenTypeFeatures;

struct TextLayoutKey {
    uint64_t content = 0;
    uint64_t geometry = 0;

    bool operator==(const TextLayoutKey &other) const
    {
        return content == other.content && geometry == other.geometry;
    }
};

struct TextLayoutRequest {
    RichTextDocument document;
    float max_width = 0.0f;
    float max_height = 0.0f;
    float device_scale = 1.0f;
    float minimum_horizontal_fit = 0.05f;
    int overflow_mode = 0; /* 0=wrap, 1=clip/no-wrap, 2=horizontal fit */
};

struct TextLayoutFontKey {
    std::string family;
    std::string style;
    float pixel_size = 0.0f;
    uint64_t fingerprint = 0;
};

struct TextLayoutShapingStyle {
    std::string font_family;
    std::string font_style;
    int font_size = 72;
    bool bold = false;
    bool italic = false;
    bool kerning = true;
    int kerning_mode = 0;
    float manual_kerning = 0.0f;
    float tracking = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float baseline_shift = 0.0f;
    int text_style = 0;
    bool ligatures = true;
    bool stylistic_alternates = false;
    bool fractions = false;
    bool opentype_features = false;
};

struct TextLayoutPaintStyle {
    RichTextFill fill;
    RichTextStroke stroke;
    bool underline = false;
    bool strikethrough = false;
};

struct TextLayoutPaintRun {
    size_t byte_start = 0;
    size_t byte_length = 0;
    TextLayoutPaintStyle style;
};

/* Cursor geometry is stored independently from paint so a cached shaped
 * cluster (including ligatures and RTL clusters) can be split into exact
 * paint ranges without reshaping the text. */
struct TextLayoutCursorBoundary {
    size_t byte_offset = 0;
    float x = 0.0f;
};

struct TextLayoutPaintSlice {
    size_t paint_index = 0;
    float x0 = 0.0f;
    float x1 = 0.0f;
};

/* Editor geometry derived from the same immutable layout consumed by the GPU
 * text renderer. Coordinates are relative to the layout text rectangle. */
struct TextLayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    uint32_t line_index = 0;
};

struct TextLayoutGlyph {
    uint32_t glyph_id = 0;
    uint32_t run_index = 0;
    uint32_t cluster_index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float advance_x = 0.0f;
    float advance_y = 0.0f;
    float ink_x = 0.0f;
    float ink_y = 0.0f;
    float ink_width = 0.0f;
    float ink_height = 0.0f;
    /* Effective per-glyph character scaling. These values are copied from the
     * exact rich-text format at the glyph's logical cluster, rather than being
     * inferred later from a QGlyphRun. This keeps mixed-style runs and Qt
     * backend run coalescing from losing H/V Scale before GPU emission. */
    float scale_x = 1.0f;
    float scale_y = 1.0f;
};

struct TextLayoutCluster {
    size_t byte_start = 0;
    size_t byte_length = 0;
    uint32_t glyph_begin = 0;
    uint32_t glyph_count = 0;
    uint32_t run_index = 0;
    uint32_t line_index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool right_to_left = false;
    uint32_t boundary_begin = 0;
    uint32_t boundary_count = 0;
};

struct TextLayoutRun {
    size_t byte_start = 0;
    size_t byte_length = 0;
    uint32_t glyph_begin = 0;
    uint32_t glyph_count = 0;
    uint32_t cluster_begin = 0;
    uint32_t cluster_count = 0;
    uint32_t line_index = 0;
    TextLayoutFontKey font;
    TextLayoutShapingStyle shaping_style;
    bool right_to_left = false;
    bool split_ligature = false;
    float clip_x = 0.0f;
    float clip_y = 0.0f;
    float clip_width = 0.0f;
    float clip_height = 0.0f;
};

struct TextLayoutLine {
    size_t byte_start = 0;
    size_t byte_length = 0;
    uint32_t run_begin = 0;
    uint32_t run_count = 0;
    uint32_t cluster_begin = 0;
    uint32_t cluster_count = 0;
    uint32_t glyph_begin = 0;
    uint32_t glyph_count = 0;
    uint32_t paragraph_index = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
};

struct TextLayoutData {
    TextLayoutKey key;
    std::string text;
    std::vector<TextLayoutGlyph> glyphs;
    std::vector<TextLayoutCluster> clusters;
    std::vector<TextLayoutCursorBoundary> cursor_boundaries;
    std::vector<TextLayoutRun> runs;
    std::vector<TextLayoutLine> lines;
    float width = 0.0f;
    float height = 0.0f;
    float natural_width = 0.0f;
    float natural_height = 0.0f;
    float horizontal_fit = 1.0f;
    bool valid = false;
};

using ImmutableTextLayout = std::shared_ptr<const TextLayoutData>;
using TextLayoutBuilder = std::function<ImmutableTextLayout(const TextLayoutRequest &)>;

TextLayoutKey text_layout_key(const TextLayoutRequest &request);
bool text_layout_request_equivalent(const TextLayoutRequest &a,
                                    const TextLayoutRequest &b);

class TextLayoutCache {
public:
    explicit TextLayoutCache(size_t capacity = 256);

    ImmutableTextLayout get_or_build(const TextLayoutRequest &request,
                                     const TextLayoutBuilder &builder);
    void clear();
    void set_capacity(size_t capacity);
    size_t size() const;

private:
    struct Entry {
        ImmutableTextLayout layout;
        uint64_t generation = 0;
        size_t estimated_bytes = 0;
    };

    struct KeyHash {
        size_t operator()(const TextLayoutKey &key) const;
    };

    void trim_locked();

    mutable std::mutex mutex_;
    std::unordered_map<TextLayoutKey, Entry, KeyHash> entries_;
    size_t capacity_ = 256;
    size_t total_estimated_bytes_ = 0;
    size_t byte_capacity_ = 64u * 1024u * 1024u;
    uint64_t generation_ = 0;
};

TextLayoutCache &shared_text_layout_cache();
ImmutableTextLayout build_text_layout(const TextLayoutRequest &request);
ImmutableTextLayout cached_text_layout(const TextLayoutRequest &request);

/* Paint runs are derived from the evaluated rich-text document without
 * invalidating or rebuilding the immutable shaped layout. */
std::vector<TextLayoutPaintRun>
text_layout_paint_runs(const RichTextDocument &document);
std::vector<TextLayoutPaintRun>
text_layout_paint_runs_canonical(const RichTextDocument &document);

std::vector<TextLayoutPaintSlice> text_layout_cluster_paint_slices(
    const TextLayoutData &layout, const TextLayoutCluster &cluster,
    const std::vector<TextLayoutPaintRun> &paint_runs);

size_t text_layout_byte_offset_at(const TextLayoutData &layout, float x, float y);
bool text_layout_range_bounds(const TextLayoutData &layout, size_t byte_start,
                              size_t byte_length, float &x, float &y,
                              float &width, float &height);

std::vector<TextLayoutRect> text_layout_selection_rects(
    const TextLayoutData &layout, size_t byte_start, size_t byte_end);
bool text_layout_caret_rect(const TextLayoutData &layout, size_t byte_offset,
                            float caret_width, TextLayoutRect &rect);
