#include "title-text-layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

class StableHash {
public:
    explicit StableHash(uint64_t seed) : value_(seed) {}

    void bytes(const void *data, size_t size)
    {
        const auto *ptr = static_cast<const unsigned char *>(data);
        for (size_t i = 0; i < size; ++i) {
            value_ ^= ptr[i];
            value_ *= 1099511628211ULL;
        }
    }

    template<typename T> void scalar(const T &value) { bytes(&value, sizeof(value)); }

    void string(const std::string &value)
    {
        scalar(value.size());
        bytes(value.data(), value.size());
    }

    uint64_t value() const { return value_; }

private:
    uint64_t value_;
};

static void hash_fill(StableHash &hash, const RichTextFill &fill)
{
    hash.scalar(fill.type);
    hash.scalar(fill.color);
    hash.scalar(fill.gradient_type);
    hash.scalar(fill.gradient_spread);
    hash.scalar(fill.gradient_start_color);
    hash.scalar(fill.gradient_end_color);
    hash.scalar(fill.gradient_start_pos);
    hash.scalar(fill.gradient_end_pos);
    hash.scalar(fill.gradient_start_opacity);
    hash.scalar(fill.gradient_end_opacity);
    hash.scalar(fill.gradient_opacity);
    hash.scalar(fill.gradient_angle);
    hash.scalar(fill.gradient_center_x);
    hash.scalar(fill.gradient_center_y);
    hash.scalar(fill.gradient_scale);
    hash.scalar(fill.gradient_focal_x);
    hash.scalar(fill.gradient_focal_y);
    hash.scalar(fill.gradient_stops.size());
    for (const GradientStop &stop : fill.gradient_stops) {
        hash.scalar(stop.color);
        hash.scalar(stop.position);
        hash.scalar(stop.opacity);
    }
}

static void hash_char_format(StableHash &hash, const RichTextCharFormat &format,
                             uint32_t mask = RichTextCharAll)
{
    if (mask & RichTextCharFontFamily) hash.string(format.font_family);
    if (mask & RichTextCharFontStyle) hash.string(format.font_style);
    if (mask & RichTextCharFontSize) hash.scalar(format.font_size);
    if (mask & RichTextCharBold) hash.scalar(format.bold);
    if (mask & RichTextCharItalic) hash.scalar(format.italic);
    if (mask & RichTextCharUnderline) hash.scalar(format.underline);
    if (mask & RichTextCharStrikethrough) hash.scalar(format.strikethrough);
    if (mask & RichTextCharKerning) {
        hash.scalar(format.kerning);
        hash.scalar(format.kerning_mode);
        hash.scalar(format.manual_kerning);
    }
    if (mask & RichTextCharTracking) hash.scalar(format.tracking);
    if (mask & RichTextCharScaleX) hash.scalar(format.scale_x);
    if (mask & RichTextCharScaleY) hash.scalar(format.scale_y);
    if (mask & RichTextCharBaselineShift) hash.scalar(format.baseline_shift);
    if (mask & RichTextCharTextStyle) hash.scalar(format.text_style);
    if (mask & RichTextCharLigatures) hash.scalar(format.ligatures);
    if (mask & RichTextCharStylisticAlternates)
        hash.scalar(format.stylistic_alternates);
    if (mask & RichTextCharFractions) hash.scalar(format.fractions);
    if (mask & RichTextCharOpenTypeFeatures)
        hash.scalar(format.opentype_features);
    if (mask & RichTextCharLanguage) hash.string(format.language);
    if (mask & RichTextCharFillColor) hash_fill(hash, format.fill);
    /* Stroke is paint-only and deliberately excluded from the shaping key. */
}

static void hash_paragraph_format(
    StableHash &hash, const RichTextParagraphFormat &format,
    uint32_t mask = RichTextParagraphAll)
{
    if (mask & RichTextParagraphAlignH) hash.scalar(format.align_h);
    if (mask & RichTextParagraphAlignV) hash.scalar(format.align_v);
    if (mask & RichTextParagraphIndentLeft) hash.scalar(format.indent_left);
    if (mask & RichTextParagraphIndentRight) hash.scalar(format.indent_right);
    if (mask & RichTextParagraphIndentFirstLine)
        hash.scalar(format.indent_first_line);
    if (mask & RichTextParagraphLineSpacing) hash.scalar(format.line_spacing);
    if (mask & RichTextParagraphSpaceBefore) hash.scalar(format.space_before);
    if (mask & RichTextParagraphSpaceAfter) hash.scalar(format.space_after);
    if (mask & RichTextParagraphHyphenate) hash.scalar(format.hyphenate);
}

static uint64_t content_hash(const TextLayoutRequest &request, uint64_t seed)
{
    StableHash hash(seed);
    const RichTextDocument &doc = request.document;
    hash.scalar(doc.version);
    hash.string(doc.plain_text);
    hash_char_format(hash, doc.default_format, RichTextCharShapingMask);
    hash_paragraph_format(hash, doc.default_paragraph_format);
    size_t shaping_range_count = 0;
    for (const RichTextRange &range : doc.ranges)
        shaping_range_count += (range.mask & RichTextCharShapingMask) != 0;
    hash.scalar(shaping_range_count);
    for (const RichTextRange &range : doc.ranges) {
        const uint32_t shaping_mask = range.mask & RichTextCharShapingMask;
        if (shaping_mask == 0)
            continue;
        hash.scalar(range.start);
        hash.scalar(range.length);
        hash.scalar(shaping_mask);
        hash_char_format(hash, range.format, shaping_mask);
    }
    hash.scalar(doc.blocks.size());
    for (const RichTextBlock &block : doc.blocks) {
        hash.scalar(block.start);
        hash.scalar(block.length);
        hash.scalar(block.mask);
        hash_paragraph_format(hash, block.format, block.mask);
    }
    return hash.value();
}

static bool same_fill(const RichTextFill &a, const RichTextFill &b)
{
    return a.type == b.type && a.color == b.color && a.gradient_type == b.gradient_type &&
           a.gradient_spread == b.gradient_spread &&
           a.gradient_start_color == b.gradient_start_color &&
           a.gradient_end_color == b.gradient_end_color &&
           a.gradient_start_pos == b.gradient_start_pos &&
           a.gradient_end_pos == b.gradient_end_pos &&
           a.gradient_start_opacity == b.gradient_start_opacity &&
           a.gradient_end_opacity == b.gradient_end_opacity &&
           a.gradient_opacity == b.gradient_opacity &&
           a.gradient_angle == b.gradient_angle &&
           a.gradient_center_x == b.gradient_center_x &&
           a.gradient_center_y == b.gradient_center_y &&
           a.gradient_scale == b.gradient_scale &&
           a.gradient_focal_x == b.gradient_focal_x &&
           a.gradient_focal_y == b.gradient_focal_y &&
           a.gradient_stops == b.gradient_stops;
}

static bool same_stroke(const RichTextStroke &a, const RichTextStroke &b)
{
    RichTextCharFormat left;
    RichTextCharFormat right;
    left.stroke = a;
    right.stroke = b;
    return rich_text_char_format_difference_mask(left, right,
                                                  RichTextCharStroke) == 0;
}

static bool same_char_format(const RichTextCharFormat &a,
                             const RichTextCharFormat &b,
                             uint32_t mask = RichTextCharAll)
{
    return rich_text_char_format_difference_mask(a, b, mask) == 0 &&
           (!(mask & RichTextCharFillColor) || same_fill(a.fill, b.fill));
}

static bool same_paragraph_format(const RichTextParagraphFormat &a,
                                  const RichTextParagraphFormat &b,
                                  uint32_t mask = RichTextParagraphAll)
{
    return rich_text_paragraph_format_difference_mask(a, b, mask) == 0;
}

} // namespace

TextLayoutKey text_layout_key(const TextLayoutRequest &request)
{
    TextLayoutKey key;
    key.content = content_hash(request, 1469598103934665603ULL);
    StableHash geometry(1099511628211ULL ^ key.content);
    geometry.scalar(request.max_width);
    geometry.scalar(request.max_height);
    geometry.scalar(request.device_scale);
    geometry.scalar(request.minimum_horizontal_fit);
    geometry.scalar(request.overflow_mode);
    key.geometry = geometry.value();
    return key;
}

bool text_layout_request_equivalent(const TextLayoutRequest &a,
                                    const TextLayoutRequest &b)
{
    if (a.max_width != b.max_width || a.max_height != b.max_height ||
        a.device_scale != b.device_scale ||
        a.minimum_horizontal_fit != b.minimum_horizontal_fit ||
        a.overflow_mode != b.overflow_mode)
        return false;
    const RichTextDocument &da = a.document;
    const RichTextDocument &db = b.document;
    if (da.version != db.version || da.plain_text != db.plain_text ||
        !same_char_format(da.default_format, db.default_format,
                          RichTextCharShapingMask) ||
        !same_paragraph_format(da.default_paragraph_format,
                               db.default_paragraph_format) ||
        da.blocks.size() != db.blocks.size())
        return false;
    std::vector<const RichTextRange *> shaping_a;
    std::vector<const RichTextRange *> shaping_b;
    for (const RichTextRange &range : da.ranges) {
        if ((range.mask & RichTextCharShapingMask) != 0)
            shaping_a.push_back(&range);
    }
    for (const RichTextRange &range : db.ranges) {
        if ((range.mask & RichTextCharShapingMask) != 0)
            shaping_b.push_back(&range);
    }
    if (shaping_a.size() != shaping_b.size())
        return false;
    for (size_t i = 0; i < shaping_a.size(); ++i) {
        const RichTextRange &ra = *shaping_a[i];
        const RichTextRange &rb = *shaping_b[i];
        const uint32_t mask_a = ra.mask & RichTextCharShapingMask;
        const uint32_t mask_b = rb.mask & RichTextCharShapingMask;
        if (ra.start != rb.start || ra.length != rb.length ||
            mask_a != mask_b ||
            !same_char_format(ra.format, rb.format, mask_a))
            return false;
    }
    for (size_t i = 0; i < da.blocks.size(); ++i) {
        const RichTextBlock &ba = da.blocks[i];
        const RichTextBlock &bb = db.blocks[i];
        if (ba.start != bb.start || ba.length != bb.length ||
            ba.mask != bb.mask ||
            !same_paragraph_format(ba.format, bb.format, ba.mask))
            return false;
    }
    return true;
}

TextLayoutCache::TextLayoutCache(size_t capacity) : capacity_(capacity) {}

static size_t estimated_layout_bytes(const TextLayoutData &layout)
{
    return sizeof(TextLayoutData) + layout.text.capacity() +
           layout.glyphs.capacity() * sizeof(TextLayoutGlyph) +
           layout.clusters.capacity() * sizeof(TextLayoutCluster) +
           layout.cursor_boundaries.capacity() *
               sizeof(TextLayoutCursorBoundary) +
           layout.runs.capacity() * sizeof(TextLayoutRun) +
           layout.lines.capacity() * sizeof(TextLayoutLine);
}

size_t TextLayoutCache::KeyHash::operator()(const TextLayoutKey &key) const
{
    return static_cast<size_t>(key.content ^
                               (key.geometry + 0x9e3779b97f4a7c15ULL +
                                (key.content << 6) + (key.content >> 2)));
}

ImmutableTextLayout TextLayoutCache::get_or_build(
    const TextLayoutRequest &request, const TextLayoutBuilder &builder)
{
    if (!builder)
        return {};
    const TextLayoutKey key = text_layout_key(request);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = entries_.find(key);
        if (it != entries_.end() && it->second.layout) {
            it->second.generation = ++generation_;
            return it->second.layout;
        }
    }

    ImmutableTextLayout built = builder(request);
    if (!built)
        return {};

    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = entries_.find(key);
    if (existing != entries_.end() && existing->second.layout) {
        existing->second.generation = ++generation_;
        return existing->second.layout;
    }
    const size_t bytes = estimated_layout_bytes(*built);
    entries_.emplace(key, Entry{built, ++generation_, bytes});
    total_estimated_bytes_ += bytes;
    trim_locked();
    return built;
}

void TextLayoutCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    total_estimated_bytes_ = 0;
}

void TextLayoutCache::set_capacity(size_t capacity)
{
    std::lock_guard<std::mutex> lock(mutex_);
    capacity_ = capacity;
    trim_locked();
}

size_t TextLayoutCache::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void TextLayoutCache::trim_locked()
{
    while (entries_.size() > capacity_ ||
           total_estimated_bytes_ > byte_capacity_) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (oldest == entries_.end() ||
                it->second.generation < oldest->second.generation)
                oldest = it;
        }
        if (oldest == entries_.end())
            break;
        total_estimated_bytes_ -= std::min(total_estimated_bytes_,
                                           oldest->second.estimated_bytes);
        entries_.erase(oldest);
    }
}

TextLayoutCache &shared_text_layout_cache()
{
    static TextLayoutCache cache(512);
    return cache;
}

ImmutableTextLayout cached_text_layout(const TextLayoutRequest &request)
{
#if !defined(OBS_FXM_TEXT_LAYOUT_STANDALONE_TEST) && !defined(OBS_FXM_RICH_TEXT_STANDALONE_TEST)
    return shared_text_layout_cache().get_or_build(request, build_text_layout);
#else
    (void)request;
    return {};
#endif
}

std::vector<TextLayoutPaintRun>
text_layout_paint_runs_canonical(const RichTextDocument &document)
{
    std::vector<size_t> breaks{0, document.plain_text.size()};
    constexpr uint32_t paint_mask =
        RichTextCharFillColor | RichTextCharStroke |
        RichTextCharUnderline | RichTextCharStrikethrough;
    for (const RichTextRange &range : document.ranges) {
        if ((range.mask & paint_mask) == 0 || range.length == 0)
            continue;
        breaks.push_back(std::min(range.start, document.plain_text.size()));
        breaks.push_back(std::min(range.start + range.length,
                                  document.plain_text.size()));
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

    std::vector<TextLayoutPaintRun> runs;
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        const size_t begin = breaks[i];
        const size_t end = breaks[i + 1];
        if (end <= begin)
            continue;
        const RichTextCharFormat format = rich_text_format_at(document, begin);
        TextLayoutPaintRun run;
        run.byte_start = begin;
        run.byte_length = end - begin;
        run.style.fill = format.fill;
        run.style.stroke = format.stroke;
        run.style.underline = format.underline;
        run.style.strikethrough = format.strikethrough;
        if (!runs.empty()) {
            TextLayoutPaintRun &previous = runs.back();
            if (previous.byte_start + previous.byte_length == run.byte_start &&
                same_fill(previous.style.fill, run.style.fill) &&
                same_stroke(previous.style.stroke, run.style.stroke) &&
                previous.style.underline == run.style.underline &&
                previous.style.strikethrough == run.style.strikethrough) {
                previous.byte_length += run.byte_length;
                continue;
            }
        }
        runs.push_back(std::move(run));
    }
    return runs;
}

std::vector<TextLayoutPaintRun>
text_layout_paint_runs(const RichTextDocument &source_document)
{
    RichTextDocument document = source_document;
    document.normalize();
    return text_layout_paint_runs_canonical(document);
}

std::vector<TextLayoutPaintSlice> text_layout_cluster_paint_slices(
    const TextLayoutData &layout, const TextLayoutCluster &cluster,
    const std::vector<TextLayoutPaintRun> &paint_runs)
{
    std::vector<TextLayoutPaintSlice> slices;
    if (paint_runs.empty() || cluster.byte_length == 0)
        return slices;

    const size_t text_size = layout.text.size();
    const size_t cluster_start = std::min(cluster.byte_start, text_size);
    const size_t cluster_end = cluster.byte_length > text_size - cluster_start
                                   ? text_size
                                   : cluster_start + cluster.byte_length;
    if (cluster_end <= cluster_start)
        return slices;

    auto x_at = [&](size_t byte_offset) {
        byte_offset = std::clamp(byte_offset, cluster_start, cluster_end);
        const uint32_t begin = cluster.boundary_begin;
        const uint32_t count = cluster.boundary_count;
        if (count > 0 && begin <= layout.cursor_boundaries.size() &&
            count <= layout.cursor_boundaries.size() - begin) {
            const TextLayoutCursorBoundary *lower = nullptr;
            const TextLayoutCursorBoundary *upper = nullptr;
            for (uint32_t i = 0; i < count; ++i) {
                const TextLayoutCursorBoundary &boundary =
                    layout.cursor_boundaries[begin + i];
                if (boundary.byte_offset == byte_offset)
                    return boundary.x;
                if (boundary.byte_offset < byte_offset &&
                    (!lower || boundary.byte_offset > lower->byte_offset))
                    lower = &boundary;
                if (boundary.byte_offset > byte_offset &&
                    (!upper || boundary.byte_offset < upper->byte_offset))
                    upper = &boundary;
            }
            if (lower && upper && upper->byte_offset > lower->byte_offset) {
                const float ratio = static_cast<float>(
                    static_cast<double>(byte_offset - lower->byte_offset) /
                    static_cast<double>(upper->byte_offset - lower->byte_offset));
                return lower->x + (upper->x - lower->x) * ratio;
            }
            if (lower)
                return lower->x;
            if (upper)
                return upper->x;
        }

        const float ratio = static_cast<float>(
            static_cast<double>(byte_offset - cluster_start) /
            static_cast<double>(cluster_end - cluster_start));
        const float logical = cluster.right_to_left ? 1.0f - ratio : ratio;
        return cluster.x + cluster.width * logical;
    };

    /* Paint runs are canonical and ordered by byte start. Begin at the first
     * possible overlap instead of scanning every range for every cluster. */
    auto first = std::upper_bound(
        paint_runs.begin(), paint_runs.end(), cluster_start,
        [](size_t offset, const TextLayoutPaintRun &run) {
            return offset < run.byte_start;
        });
    if (first != paint_runs.begin())
        --first;
    while (first != paint_runs.end()) {
        const size_t run_start = std::min(first->byte_start, text_size);
        const size_t run_end = first->byte_length > text_size - run_start
                                   ? text_size
                                   : run_start + first->byte_length;
        if (run_end > cluster_start)
            break;
        ++first;
    }

    for (auto it = first; it != paint_runs.end(); ++it) {
        const size_t i = static_cast<size_t>(it - paint_runs.begin());
        const size_t run_start = std::min(it->byte_start, text_size);
        if (run_start >= cluster_end)
            break;
        const size_t run_end = it->byte_length > text_size - run_start
                                   ? text_size
                                   : run_start + it->byte_length;
        const size_t begin = std::max(cluster_start, run_start);
        const size_t end = std::min(cluster_end, run_end);
        if (end <= begin)
            continue;
        const float xa = x_at(begin);
        const float xb = x_at(end);
        const float left = std::min(xa, xb);
        const float right = std::max(xa, xb);
        if (right - left <= 0.0001f)
            continue;
        slices.push_back({i, left, right});
    }

    if (slices.empty()) {
        size_t index = paint_runs.size() - 1;
        const auto upper = std::upper_bound(
            paint_runs.begin(), paint_runs.end(), cluster_start,
            [](size_t offset, const TextLayoutPaintRun &run) {
                return offset < run.byte_start;
            });
        if (upper != paint_runs.begin())
            index = static_cast<size_t>(std::prev(upper) - paint_runs.begin());
        slices.push_back({index, cluster.x, cluster.x + cluster.width});
        return slices;
    }

    std::sort(slices.begin(), slices.end(), [](const TextLayoutPaintSlice &a,
                                               const TextLayoutPaintSlice &b) {
        if (a.x0 != b.x0)
            return a.x0 < b.x0;
        if (a.x1 != b.x1)
            return a.x1 < b.x1;
        return a.paint_index < b.paint_index;
    });
    std::vector<TextLayoutPaintSlice> merged;
    for (const TextLayoutPaintSlice &slice : slices) {
        if (!merged.empty() && merged.back().paint_index == slice.paint_index &&
            std::abs(merged.back().x1 - slice.x0) <= 0.0001f) {
            merged.back().x1 = slice.x1;
        } else {
            merged.push_back(slice);
        }
    }
    return merged;
}

size_t text_layout_byte_offset_at(const TextLayoutData &layout, float x, float y)
{
    if (layout.clusters.empty())
        return 0;
    const TextLayoutCluster *best = nullptr;
    float best_distance = std::numeric_limits<float>::max();
    for (const TextLayoutCluster &cluster : layout.clusters) {
        const float dx = x < cluster.x ? cluster.x - x
                         : x > cluster.x + cluster.width
                             ? x - (cluster.x + cluster.width)
                             : 0.0f;
        const float dy = y < cluster.y ? cluster.y - y
                         : y > cluster.y + cluster.height
                             ? y - (cluster.y + cluster.height)
                             : 0.0f;
        const float distance = dx * dx + dy * dy;
        if (distance < best_distance) {
            best_distance = distance;
            best = &cluster;
        }
    }
    if (!best)
        return 0;
    const bool trailing = best->width > 0.0f &&
                          x >= best->x + best->width * 0.5f;
    if (best->right_to_left)
        return trailing ? best->byte_start : best->byte_start + best->byte_length;
    return trailing ? best->byte_start + best->byte_length : best->byte_start;
}

bool text_layout_range_bounds(const TextLayoutData &layout, size_t byte_start,
                              size_t byte_length, float &x, float &y,
                              float &width, float &height)
{
    const size_t byte_end = byte_length > layout.text.size() -
                                             std::min(byte_start, layout.text.size())
                                ? layout.text.size()
                                : byte_start + byte_length;
    bool found = false;
    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    for (const TextLayoutCluster &cluster : layout.clusters) {
        const size_t cluster_end = cluster.byte_start + cluster.byte_length;
        if (cluster_end <= byte_start || cluster.byte_start >= byte_end)
            continue;
        found = true;
        left = std::min(left, cluster.x);
        top = std::min(top, cluster.y);
        right = std::max(right, cluster.x + cluster.width);
        bottom = std::max(bottom, cluster.y + cluster.height);
    }
    if (!found) {
        x = y = width = height = 0.0f;
        return false;
    }
    x = left;
    y = top;
    width = std::max(0.0f, right - left);
    height = std::max(0.0f, bottom - top);
    return true;
}

namespace {

static float text_layout_cluster_x_at(const TextLayoutData &layout,
                                      const TextLayoutCluster &cluster,
                                      size_t byte_offset)
{
    const size_t cluster_start = std::min(cluster.byte_start, layout.text.size());
    const size_t cluster_end = cluster.byte_length > layout.text.size() - cluster_start
                                   ? layout.text.size()
                                   : cluster_start + cluster.byte_length;
    byte_offset = std::clamp(byte_offset, cluster_start, cluster_end);
    const uint32_t begin = cluster.boundary_begin;
    const uint32_t count = cluster.boundary_count;
    if (count > 0 && begin <= layout.cursor_boundaries.size() &&
        count <= layout.cursor_boundaries.size() - begin) {
        const TextLayoutCursorBoundary *lower = nullptr;
        const TextLayoutCursorBoundary *upper = nullptr;
        for (uint32_t i = 0; i < count; ++i) {
            const TextLayoutCursorBoundary &boundary =
                layout.cursor_boundaries[begin + i];
            if (boundary.byte_offset == byte_offset)
                return boundary.x;
            if (boundary.byte_offset < byte_offset &&
                (!lower || boundary.byte_offset > lower->byte_offset))
                lower = &boundary;
            if (boundary.byte_offset > byte_offset &&
                (!upper || boundary.byte_offset < upper->byte_offset))
                upper = &boundary;
        }
        if (lower && upper && upper->byte_offset > lower->byte_offset) {
            const float ratio = static_cast<float>(
                static_cast<double>(byte_offset - lower->byte_offset) /
                static_cast<double>(upper->byte_offset - lower->byte_offset));
            return lower->x + (upper->x - lower->x) * ratio;
        }
        if (lower)
            return lower->x;
        if (upper)
            return upper->x;
    }
    if (cluster_end <= cluster_start)
        return cluster.x;
    const float ratio = static_cast<float>(
        static_cast<double>(byte_offset - cluster_start) /
        static_cast<double>(cluster_end - cluster_start));
    const float logical = cluster.right_to_left ? 1.0f - ratio : ratio;
    return cluster.x + cluster.width * logical;
}

} // namespace

std::vector<TextLayoutRect> text_layout_selection_rects(
    const TextLayoutData &layout, size_t byte_start, size_t byte_end)
{
    std::vector<TextLayoutRect> rects;
    byte_start = std::min(byte_start, layout.text.size());
    byte_end = std::min(byte_end, layout.text.size());
    if (byte_end < byte_start)
        std::swap(byte_start, byte_end);
    if (byte_start == byte_end)
        return rects;

    for (const TextLayoutCluster &cluster : layout.clusters) {
        const size_t cluster_start = std::min(cluster.byte_start, layout.text.size());
        const size_t cluster_end = cluster.byte_length > layout.text.size() - cluster_start
                                       ? layout.text.size()
                                       : cluster_start + cluster.byte_length;
        const size_t begin = std::max(byte_start, cluster_start);
        const size_t end = std::min(byte_end, cluster_end);
        if (end <= begin || cluster.line_index >= layout.lines.size())
            continue;
        const float xa = text_layout_cluster_x_at(layout, cluster, begin);
        const float xb = text_layout_cluster_x_at(layout, cluster, end);
        TextLayoutRect rect;
        rect.x = std::min(xa, xb);
        rect.y = layout.lines[cluster.line_index].y;
        rect.width = std::max(0.0f, std::abs(xb - xa));
        rect.height = layout.lines[cluster.line_index].height;
        rect.line_index = cluster.line_index;
        if (rect.width <= 0.0001f)
            continue;
        if (!rects.empty() && rects.back().line_index == rect.line_index &&
            rect.x <= rects.back().x + rects.back().width + 0.25f &&
            rect.x + rect.width >= rects.back().x - 0.25f) {
            const float right = std::max(rects.back().x + rects.back().width,
                                         rect.x + rect.width);
            rects.back().x = std::min(rects.back().x, rect.x);
            rects.back().width = right - rects.back().x;
            rects.back().y = std::min(rects.back().y, rect.y);
            rects.back().height = std::max(rects.back().height, rect.height);
        } else {
            rects.push_back(rect);
        }
    }
    return rects;
}

bool text_layout_caret_rect(const TextLayoutData &layout, size_t byte_offset,
                            float caret_width, TextLayoutRect &rect)
{
    byte_offset = std::min(byte_offset, layout.text.size());
    const TextLayoutCluster *candidate = nullptr;
    const TextLayoutLine *candidate_line = nullptr;
    float candidate_x = 0.0f;

    for (const TextLayoutCluster &cluster : layout.clusters) {
        if (cluster.line_index >= layout.lines.size())
            continue;
        const size_t start = std::min(cluster.byte_start, layout.text.size());
        const size_t end = cluster.byte_length > layout.text.size() - start
                               ? layout.text.size()
                               : start + cluster.byte_length;
        if (byte_offset < start || byte_offset > end)
            continue;
        candidate = &cluster;
        candidate_line = &layout.lines[cluster.line_index];
        candidate_x = text_layout_cluster_x_at(layout, cluster, byte_offset);
        if (byte_offset < end || byte_offset == layout.text.size())
            break;
    }

    if (!candidate_line && !layout.lines.empty()) {
        const TextLayoutLine &line = layout.lines.back();
        candidate_line = &line;
        candidate_x = line.x + line.width;
    }
    if (!candidate_line)
        return false;

    rect.x = candidate_x;
    rect.y = candidate_line->y;
    rect.width = std::max(0.5f, caret_width);
    rect.height = std::max(1.0f, candidate_line->height);
    rect.line_index = candidate ? candidate->line_index
                                : static_cast<uint32_t>(layout.lines.size() - 1);
    return true;
}
