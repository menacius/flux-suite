#include "title-text-layout.h"
#include "title-text-layout-qt-font-registry.h"

#include <QByteArray>
#include <QFont>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QPointF>
#include <QPainterPath>
#include <QRawFont>
#include <QRectF>
#include <QString>
#include <QTextCharFormat>
#include <QTextFormat>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace {

struct RegisteredRawFont {
    QRawFont font;
    uint64_t generation = 0;
};

std::mutex g_raw_font_registry_mutex;
std::unordered_map<uint64_t, RegisteredRawFont> g_raw_font_registry;
uint64_t g_raw_font_registry_generation = 0;
constexpr size_t kRawFontRegistryCapacity = 512;

} // namespace

QRawFont text_layout_registered_raw_font(const TextLayoutFontKey &key)
{
    if (key.fingerprint == 0)
        return {};
    std::lock_guard<std::mutex> lock(g_raw_font_registry_mutex);
    const auto it = g_raw_font_registry.find(key.fingerprint);
    if (it == g_raw_font_registry.end() || !it->second.font.isValid())
        return {};
    it->second.generation = ++g_raw_font_registry_generation;
    QRawFont font = it->second.font;
    if (std::abs(font.pixelSize() - key.pixel_size) > 0.001)
        font.setPixelSize(key.pixel_size);
    return font;
}

void text_layout_register_raw_font(const TextLayoutFontKey &key,
                                   const QRawFont &font)
{
    if (key.fingerprint == 0 || !font.isValid())
        return;
    std::lock_guard<std::mutex> lock(g_raw_font_registry_mutex);
    const uint64_t generation = ++g_raw_font_registry_generation;
    const auto existing = g_raw_font_registry.find(key.fingerprint);
    if (existing != g_raw_font_registry.end()) {
        /* The same fallback face commonly appears in every line/run. Keep the
         * retained QRawFont and only refresh its LRU age instead of repeatedly
         * detaching/copying the implicitly shared font object while typing. */
        existing->second.generation = generation;
        if (!existing->second.font.isValid())
            existing->second.font = font;
        return;
    }
    g_raw_font_registry.emplace(
        key.fingerprint, RegisteredRawFont{font, generation});
    while (g_raw_font_registry.size() > kRawFontRegistryCapacity) {
        auto oldest = g_raw_font_registry.end();
        for (auto it = g_raw_font_registry.begin();
             it != g_raw_font_registry.end(); ++it) {
            if (oldest == g_raw_font_registry.end() ||
                it->second.generation < oldest->second.generation)
                oldest = it;
        }
        if (oldest == g_raw_font_registry.end())
            break;
        g_raw_font_registry.erase(oldest);
    }
}

void text_layout_clear_raw_font_registry()
{
    std::lock_guard<std::mutex> lock(g_raw_font_registry_mutex);
    g_raw_font_registry.clear();
}

namespace {

struct ParagraphSpan {
    size_t start = 0;
    size_t length = 0;
};

static std::vector<ParagraphSpan> paragraph_spans(const std::string &text)
{
    std::vector<ParagraphSpan> spans;
    size_t start = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            spans.push_back({start, i - start});
            start = i + 1;
        }
    }
    if (spans.empty())
        spans.push_back({0, 0});
    return spans;
}

/* QTextLayout uses UTF-16 positions while the canonical rich-text model uses
 * UTF-8 byte offsets.  The old helpers rebuilt progressively larger QString
 * slices for every range, glyph run and cursor boundary.  A single paragraph
 * could therefore spend quadratic time just converting positions while a
 * user typed.  Build one immutable map per paragraph and make both directions
 * O(log n)/O(1), snapping positions inside a surrogate pair to its start. */
class QtUtf8PositionMap {
public:
    explicit QtUtf8PositionMap(const QString &text)
    {
        byte_at_qpos_.assign(static_cast<size_t>(text.size()) + 1, 0);
        qpos_boundaries_.reserve(static_cast<size_t>(text.size()) + 1);
        byte_boundaries_.reserve(static_cast<size_t>(text.size()) + 1);
        qpos_boundaries_.push_back(0);
        byte_boundaries_.push_back(0);

        size_t bytes = 0;
        const int size = static_cast<int>(text.size());
        int qpos = 0;
        while (qpos < size) {
            const ushort first = text.at(qpos).unicode();
            const bool surrogate_pair = first >= 0xD800 && first <= 0xDBFF &&
                                        qpos + 1 < size &&
                                        text.at(qpos + 1).unicode() >= 0xDC00 &&
                                        text.at(qpos + 1).unicode() <= 0xDFFF;
            const int units = surrogate_pair ? 2 : 1;
            byte_at_qpos_[static_cast<size_t>(qpos)] = bytes;
            if (surrogate_pair)
                byte_at_qpos_[static_cast<size_t>(qpos + 1)] = bytes;

            const size_t encoded_bytes = surrogate_pair
                ? 4u
                : (first < 0x80u ? 1u : (first < 0x800u ? 2u : 3u));
            bytes += encoded_bytes;
            qpos += units;
            byte_at_qpos_[static_cast<size_t>(qpos)] = bytes;
            qpos_boundaries_.push_back(qpos);
            byte_boundaries_.push_back(bytes);
        }
        total_bytes_ = bytes;
    }

    int qpos_from_byte(size_t byte_offset) const
    {
        const size_t clamped = std::min(byte_offset, total_bytes_);
        const auto upper = std::upper_bound(byte_boundaries_.begin(),
                                            byte_boundaries_.end(), clamped);
        if (upper == byte_boundaries_.begin())
            return 0;
        const size_t index = static_cast<size_t>(std::prev(upper) -
                                                 byte_boundaries_.begin());
        return qpos_boundaries_[index];
    }

    size_t byte_from_qpos(int qpos) const
    {
        if (byte_at_qpos_.empty())
            return 0;
        qpos = std::clamp(qpos, 0,
                          static_cast<int>(byte_at_qpos_.size()) - 1);
        return byte_at_qpos_[static_cast<size_t>(qpos)];
    }

private:
    std::vector<size_t> byte_at_qpos_;
    std::vector<int> qpos_boundaries_;
    std::vector<size_t> byte_boundaries_;
    size_t total_bytes_ = 0;
};

static QFont font_from_rich_format(const RichTextCharFormat &format,
                                   float device_scale)
{
    QFont font(QString::fromStdString(format.font_family));
    if (!format.font_style.empty())
        font.setStyleName(QString::fromStdString(format.font_style));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(
        static_cast<double>(format.font_size) * device_scale))));
    font.setBold(format.bold);
    font.setItalic(format.italic);
    font.setUnderline(format.underline);
    font.setStrikeOut(format.strikethrough);
    font.setKerning(format.kerning_mode != 2 && format.kerning);
    font.setLetterSpacing(QFont::AbsoluteSpacing,
                          (format.tracking +
                           (format.kerning_mode == 2 ? format.manual_kerning : 0.0f)) *
                              device_scale);
    /* Character H/V Scale is never encoded in QFont. QFont::setStretch()
     * changes font matching on DirectWrite and can select discrete condensed/
     * expanded faces around backend-specific thresholds. It also makes the
     * shaped advances and the atlas outline come from different geometries.
     * The canonical layout stage applies both axes explicitly per cluster and
     * the GPU stage scales the authored-size outline exactly once. */
    font.setStretch(100);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.setFeature("kern", format.kerning_mode != 2 && format.kerning ? 1 : 0);
    font.setFeature("liga", format.ligatures ? 1 : 0);
    font.setFeature("clig", format.ligatures ? 1 : 0);
    font.setFeature("salt", format.stylistic_alternates ? 1 : 0);
    font.setFeature("frac", format.fractions ? 1 : 0);
    font.setFeature("calt", format.opentype_features ? 1 : 0);
#endif
    font.setCapitalization(QFont::MixedCase);
    if (format.text_style == 1)
        font.setCapitalization(QFont::AllUppercase);
    else if (format.text_style == 2)
        font.setCapitalization(QFont::SmallCaps);
    if (format.text_style == 3 || format.text_style == 4)
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * 0.65))));
    return font;
}

static constexpr int kLayoutScaleXProperty = QTextFormat::UserProperty + 0x275;
static constexpr int kLayoutScaleYProperty = QTextFormat::UserProperty + 0x276;

static QTextCharFormat qtext_format(const RichTextCharFormat &format,
                                    float device_scale)
{
    QTextCharFormat result;
    result.setFont(font_from_rich_format(format, device_scale));
    /* Keep scale-only rich-text boundaries visible to QTextLayout without
     * delegating the scale to QFont. This prevents ligatures/items from being
     * silently coalesced across two canonical scale ranges. */
    result.setProperty(kLayoutScaleXProperty,
                       static_cast<double>(format.scale_x));
    result.setProperty(kLayoutScaleYProperty,
                       static_cast<double>(format.scale_y));
    result.setFontUnderline(format.underline);
    result.setFontStrikeOut(format.strikethrough);
    result.setFontKerning(format.kerning_mode != 2 && format.kerning);
    result.setFontLetterSpacingType(QFont::AbsoluteSpacing);
    result.setFontLetterSpacing(
        (format.tracking +
         (format.kerning_mode == 2 ? format.manual_kerning : 0.0f)) *
        device_scale);
    if (format.text_style == 3)
        result.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
    else if (format.text_style == 4)
        result.setVerticalAlignment(QTextCharFormat::AlignSubScript);
    else
        result.setVerticalAlignment(QTextCharFormat::AlignNormal);
    return result;
}

static TextLayoutShapingStyle
shaping_style(const RichTextCharFormat &format)
{
    TextLayoutShapingStyle style;
    style.font_family = format.font_family;
    style.font_style = format.font_style;
    style.font_size = format.font_size;
    style.bold = format.bold;
    style.italic = format.italic;
    style.kerning = format.kerning;
    style.kerning_mode = format.kerning_mode;
    style.manual_kerning = format.manual_kerning;
    style.tracking = format.tracking;
    style.scale_x = format.scale_x;
    style.scale_y = format.scale_y;
    style.baseline_shift = format.baseline_shift;
    style.text_style = format.text_style;
    style.ligatures = format.ligatures;
    style.stylistic_alternates = format.stylistic_alternates;
    style.fractions = format.fractions;
    style.opentype_features = format.opentype_features;
    return style;
}

static void hash_bytes(uint64_t &hash, const QByteArray &bytes)
{
    for (unsigned char value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
}

static TextLayoutFontKey font_key(const QRawFont &font)
{
    TextLayoutFontKey key;
    key.family = font.familyName().toStdString();
    key.style = font.styleName().toStdString();
    key.pixel_size = static_cast<float>(font.pixelSize());
    uint64_t hash = 1469598103934665603ULL;
    hash_bytes(hash, QByteArray::fromStdString(key.family));
    hash_bytes(hash, QByteArray("\n", 1));
    hash_bytes(hash, QByteArray::fromStdString(key.style));
    /* Family/style names are not unique enough for a persistent atlas. The
     * stable face tables distinguish fallback fonts and separate files that
     * expose the same display name. */
    hash_bytes(hash, font.fontTable("head"));
    hash_bytes(hash, font.fontTable("maxp"));
    hash_bytes(hash, font.fontTable("name"));
    uint32_t size_bits = 0;
    static_assert(sizeof(size_bits) == sizeof(key.pixel_size), "float size");
    std::memcpy(&size_bits, &key.pixel_size, sizeof(size_bits));
    hash ^= size_bits;
    hash *= 1099511628211ULL;
    key.fingerprint = hash;
    return key;
}

static void translate_line(TextLayoutData &data, TextLayoutLine &line,
                           float dx, float dy)
{
    line.x += dx;
    line.y += dy;
    line.baseline += dy;
    for (uint32_t i = 0; i < line.glyph_count; ++i) {
        TextLayoutGlyph &glyph = data.glyphs[line.glyph_begin + i];
        glyph.x += dx;
        glyph.y += dy;
        glyph.ink_x += dx;
        glyph.ink_y += dy;
    }
    for (uint32_t i = 0; i < line.cluster_count; ++i) {
        TextLayoutCluster &cluster = data.clusters[line.cluster_begin + i];
        cluster.x += dx;
        cluster.y += dy;
        if (cluster.boundary_begin <= data.cursor_boundaries.size() &&
            cluster.boundary_count <=
                data.cursor_boundaries.size() - cluster.boundary_begin) {
            for (uint32_t boundary = 0; boundary < cluster.boundary_count;
                 ++boundary) {
                data.cursor_boundaries[cluster.boundary_begin + boundary].x += dx;
            }
        }
    }
    for (uint32_t i = 0; i < line.run_count; ++i) {
        TextLayoutRun &run = data.runs[line.run_begin + i];
        run.clip_x += dx;
        run.clip_y += dy;
    }
}

static void scale_line_x(TextLayoutData &data, TextLayoutLine &line,
                         float origin_x, float factor)
{
    for (uint32_t i = 0; i < line.glyph_count; ++i) {
        TextLayoutGlyph &glyph = data.glyphs[line.glyph_begin + i];
        glyph.x = origin_x + (glyph.x - origin_x) * factor;
        glyph.advance_x *= factor;
        /* Horizontal-fit is a second, document-level geometry transform.
         * Carry it into the emitted glyph quad as well as positions; otherwise
         * fit mode compresses advances only and regresses into tracking-like
         * spacing. Authored H Scale remains in glyph.scale_x and is multiplied
         * exactly once by this independent fit factor. */
        glyph.scale_x = std::clamp(glyph.scale_x * factor, 0.0001f, 100.0f);
        glyph.ink_x = origin_x + (glyph.ink_x - origin_x) * factor;
        glyph.ink_width *= factor;
    }
    for (uint32_t i = 0; i < line.cluster_count; ++i) {
        TextLayoutCluster &cluster = data.clusters[line.cluster_begin + i];
        cluster.x = origin_x + (cluster.x - origin_x) * factor;
        cluster.width *= factor;
        if (cluster.boundary_begin <= data.cursor_boundaries.size() &&
            cluster.boundary_count <=
                data.cursor_boundaries.size() - cluster.boundary_begin) {
            for (uint32_t boundary = 0; boundary < cluster.boundary_count;
                 ++boundary) {
                TextLayoutCursorBoundary &cursor =
                    data.cursor_boundaries[cluster.boundary_begin + boundary];
                cursor.x = origin_x + (cursor.x - origin_x) * factor;
            }
        }
    }
    for (uint32_t i = 0; i < line.run_count; ++i) {
        TextLayoutRun &run = data.runs[line.run_begin + i];
        run.clip_x = origin_x + (run.clip_x - origin_x) * factor;
        run.clip_width *= factor;
    }
    line.width *= factor;
}

/* Rebuild split-run clipping from the canonical post-transform clusters. */
static void rebuild_line_run_clips(TextLayoutData &data,
                                   const TextLayoutLine &line)
{
    const uint32_t run_end = std::min<uint32_t>(
        line.run_begin + line.run_count,
        static_cast<uint32_t>(data.runs.size()));
    for (uint32_t run_index = line.run_begin; run_index < run_end; ++run_index) {
        TextLayoutRun &run = data.runs[run_index];
        float clip_left = std::numeric_limits<float>::max();
        float clip_right = -std::numeric_limits<float>::max();
        const uint32_t cluster_end = std::min<uint32_t>(
            run.cluster_begin + run.cluster_count,
            static_cast<uint32_t>(data.clusters.size()));
        for (uint32_t cluster_index = run.cluster_begin;
             cluster_index < cluster_end; ++cluster_index) {
            const TextLayoutCluster &cluster = data.clusters[cluster_index];
            if (cluster.run_index != run_index)
                continue;
            clip_left = std::min(clip_left, cluster.x);
            clip_right = std::max(clip_right, cluster.x + cluster.width);
        }
        if (std::isfinite(clip_left) && std::isfinite(clip_right)) {
            run.clip_x = clip_left;
            run.clip_width = std::max(0.0f, clip_right - clip_left);
        }
    }
}

static std::vector<uint32_t> visual_line_clusters(
    const TextLayoutData &data, const TextLayoutLine &line)
{
    std::vector<uint32_t> clusters;
    clusters.reserve(line.cluster_count);
    const uint32_t cluster_end = std::min<uint32_t>(
        line.cluster_begin + line.cluster_count,
        static_cast<uint32_t>(data.clusters.size()));
    for (uint32_t index = line.cluster_begin; index < cluster_end; ++index)
        clusters.push_back(index);
    std::stable_sort(clusters.begin(), clusters.end(),
                     [&](uint32_t left, uint32_t right) {
        const TextLayoutCluster &a = data.clusters[left];
        const TextLayoutCluster &b = data.clusters[right];
        if (a.x != b.x)
            return a.x < b.x;
        return a.byte_start < b.byte_start;
    });
    return clusters;
}

/* Apply canonical H Scale after shaping, in visual-cluster order. This is the
 * only place where character scale changes advances/positions. Alignment is
 * deliberately applied afterwards from the final scaled line width; preserving
 * QTextLayout's pre-scale center/right anchor makes a compressed or expanded
 * line drift inside its text box. */
static void apply_line_horizontal_character_scale(
    TextLayoutData &data, TextLayoutLine &line,
    const RichTextDocument &document)
{
    if (line.cluster_count == 0)
        return;

    const std::vector<uint32_t> visual_clusters =
        visual_line_clusters(data, line);
    if (visual_clusters.empty())
        return;

    float old_left = std::numeric_limits<float>::max();
    for (uint32_t index : visual_clusters)
        old_left = std::min(old_left, data.clusters[index].x);
    if (!std::isfinite(old_left))
        return;

    float visual_cursor = old_left;
    float previous_old_right = old_left;
    for (uint32_t cluster_index : visual_clusters) {
        TextLayoutCluster &cluster = data.clusters[cluster_index];
        const float old_x = cluster.x;
        const float old_width = cluster.width;
        const float gap = std::max(0.0f, old_x - previous_old_right);
        visual_cursor += gap;

        const RichTextCharFormat format =
            rich_text_format_at(document, cluster.byte_start);
        const float factor = std::clamp(format.scale_x, 0.01f, 100.0f);
        const float new_x = visual_cursor;
        const float new_width = std::max(0.0f, old_width * factor);

        const uint32_t glyph_end = std::min<uint32_t>(
            cluster.glyph_begin + cluster.glyph_count,
            static_cast<uint32_t>(data.glyphs.size()));
        for (uint32_t glyph_index = cluster.glyph_begin;
             glyph_index < glyph_end; ++glyph_index) {
            TextLayoutGlyph &glyph = data.glyphs[glyph_index];
            if (glyph.cluster_index != cluster_index)
                continue;
            const float old_glyph_x = glyph.x;
            glyph.x = new_x + (old_glyph_x - old_x) * factor;
            glyph.ink_x += glyph.x - old_glyph_x;
            /* advance_x already contains the per-glyph scale copied from the
             * canonical format. Do not multiply it a second time here. */
        }

        if (cluster.boundary_begin <= data.cursor_boundaries.size() &&
            cluster.boundary_count <=
                data.cursor_boundaries.size() - cluster.boundary_begin) {
            for (uint32_t boundary = 0; boundary < cluster.boundary_count;
                 ++boundary) {
                TextLayoutCursorBoundary &cursor =
                    data.cursor_boundaries[cluster.boundary_begin + boundary];
                cursor.x = new_x + (cursor.x - old_x) * factor;
            }
        }
        cluster.x = new_x;
        cluster.width = new_width;
        visual_cursor = new_x + new_width;
        previous_old_right = old_x + old_width;
    }

    line.x = old_left;
    line.width = std::max(0.0f, visual_cursor - old_left);
    rebuild_line_run_clips(data, line);
}

static bool cluster_is_expandable_space(const TextLayoutData &data,
                                        const TextLayoutCluster &cluster)
{
    if (cluster.byte_length == 0 || cluster.byte_start >= data.text.size())
        return false;
    const size_t length = std::min(
        cluster.byte_length, data.text.size() - cluster.byte_start);
    const QString text = QString::fromUtf8(
        data.text.data() + cluster.byte_start, static_cast<int>(length));
    if (text.isEmpty())
        return false;
    for (const QChar character : text) {
        const ushort code = character.unicode();
        if (!character.isSpace() || code == 0x00A0 || code == 0x202F)
            return false;
    }
    return true;
}

/* Expand only inter-word whitespace after authored H Scale has been resolved.
 * This keeps glyph outlines untouched while making justify modes fill the real
 * text-box width. Cursor/selection geometry is expanded with the whitespace,
 * so canvas painting and editing continue to share one layout. */
static bool justify_line_to_width(TextLayoutData &data, TextLayoutLine &line,
                                  float target_width)
{
    const std::vector<uint32_t> visual_clusters =
        visual_line_clusters(data, line);
    if (visual_clusters.size() < 3 || target_width <= line.width + 0.0001f)
        return false;

    size_t first_content = 0;
    while (first_content < visual_clusters.size() &&
           cluster_is_expandable_space(
               data, data.clusters[visual_clusters[first_content]]))
        ++first_content;
    size_t last_content = visual_clusters.size();
    while (last_content > first_content &&
           cluster_is_expandable_space(
               data, data.clusters[visual_clusters[last_content - 1]]))
        --last_content;
    if (last_content <= first_content + 1)
        return false;

    std::vector<bool> expandable(visual_clusters.size(), false);
    size_t count = 0;
    for (size_t i = first_content + 1; i < last_content; ++i) {
        if (cluster_is_expandable_space(
                data, data.clusters[visual_clusters[i]])) {
            expandable[i] = true;
            ++count;
        }
    }
    if (count == 0)
        return false;

    const float addition = (target_width - line.width) /
                           static_cast<float>(count);
    float accumulated = 0.0f;
    for (size_t visual_index = 0; visual_index < visual_clusters.size();
         ++visual_index) {
        const uint32_t cluster_index = visual_clusters[visual_index];
        TextLayoutCluster &cluster = data.clusters[cluster_index];
        const float old_x = cluster.x;
        const float old_width = cluster.width;
        const float new_x = old_x + accumulated;
        const float new_width = expandable[visual_index]
                                    ? std::max(0.0f, old_width + addition)
                                    : old_width;

        const uint32_t glyph_end = std::min<uint32_t>(
            cluster.glyph_begin + cluster.glyph_count,
            static_cast<uint32_t>(data.glyphs.size()));
        for (uint32_t glyph_index = cluster.glyph_begin;
             glyph_index < glyph_end; ++glyph_index) {
            TextLayoutGlyph &glyph = data.glyphs[glyph_index];
            if (glyph.cluster_index != cluster_index)
                continue;
            glyph.x += accumulated;
            glyph.ink_x += accumulated;
        }

        if (cluster.boundary_begin <= data.cursor_boundaries.size() &&
            cluster.boundary_count <=
                data.cursor_boundaries.size() - cluster.boundary_begin) {
            for (uint32_t boundary = 0; boundary < cluster.boundary_count;
                 ++boundary) {
                TextLayoutCursorBoundary &cursor =
                    data.cursor_boundaries[cluster.boundary_begin + boundary];
                const float relative = old_width > 0.0001f
                    ? std::clamp((cursor.x - old_x) / old_width, 0.0f, 1.0f)
                    : (boundary + 1 == cluster.boundary_count ? 1.0f : 0.0f);
                cursor.x = new_x + relative * new_width;
            }
        }
        cluster.x = new_x;
        cluster.width = new_width;
        if (expandable[visual_index])
            accumulated += addition;
    }

    line.width = target_width;
    rebuild_line_run_clips(data, line);
    return true;
}

static void align_scaled_line(TextLayoutData &data, TextLayoutLine &line,
                              float line_left, float available_width,
                              int align_h, bool last_in_paragraph)
{
    if (available_width <= 0.0f)
        return;

    const bool justify = align_h == 6 ||
        (align_h >= 3 && align_h <= 5 && !last_in_paragraph);
    if (line.x != line_left)
        translate_line(data, line, line_left - line.x, 0.0f);
    if (justify)
        justify_line_to_width(data, line, available_width);

    int terminal_alignment = align_h;
    if (justify)
        terminal_alignment = 0;
    else if (align_h == 3)
        terminal_alignment = 0;
    else if (align_h == 4)
        terminal_alignment = 1;
    else if (align_h == 5)
        terminal_alignment = 2;

    float target_x = line_left;
    if (terminal_alignment == 1)
        target_x += (available_width - line.width) * 0.5f;
    else if (terminal_alignment == 2)
        target_x += available_width - line.width;
    if (target_x != line.x)
        translate_line(data, line, target_x - line.x, 0.0f);
}

/* Expand a line to the real scaled glyph envelope without changing shaping.
 * V Scale is applied around each glyph baseline, so a run above 100% can extend
 * above and below QTextLine's natural metrics. Rebase that envelope to the
 * requested line top and use the resulting height for following lines and
 * vertical alignment. This is run-safe for mixed H/V scales. */
static void normalize_line_vertical_scale(TextLayoutData &data,
                                          TextLayoutLine &line)
{
    if (line.glyph_count == 0)
        return;

    float visual_top = line.y;
    float visual_bottom = line.y + line.height;
    const uint32_t glyph_end = std::min<uint32_t>(
        line.glyph_begin + line.glyph_count,
        static_cast<uint32_t>(data.glyphs.size()));
    for (uint32_t index = line.glyph_begin; index < glyph_end; ++index) {
        const TextLayoutGlyph &glyph = data.glyphs[index];
        if (glyph.ink_width <= 0.0f || glyph.ink_height <= 0.0f)
            continue;
        visual_top = std::min(visual_top, glyph.ink_y);
        visual_bottom = std::max(visual_bottom,
                                 glyph.ink_y + glyph.ink_height);
    }

    const float content_shift = std::max(0.0f, line.y - visual_top);
    if (content_shift > 0.0f) {
        for (uint32_t index = line.glyph_begin; index < glyph_end; ++index) {
            TextLayoutGlyph &glyph = data.glyphs[index];
            glyph.y += content_shift;
            glyph.ink_y += content_shift;
        }
        const uint32_t run_end = std::min<uint32_t>(
            line.run_begin + line.run_count,
            static_cast<uint32_t>(data.runs.size()));
        for (uint32_t index = line.run_begin; index < run_end; ++index)
            data.runs[index].clip_y += content_shift;
        line.baseline += content_shift;
        visual_bottom += content_shift;
    }

    line.height = std::max(line.height + content_shift,
                           visual_bottom - line.y);
    line.ascent = std::max(0.0f, line.baseline - line.y);
    line.descent = std::max(0.0f, line.height - line.ascent);

    const uint32_t cluster_end = std::min<uint32_t>(
        line.cluster_begin + line.cluster_count,
        static_cast<uint32_t>(data.clusters.size()));
    for (uint32_t index = line.cluster_begin; index < cluster_end; ++index) {
        data.clusters[index].y = line.y;
        data.clusters[index].height = line.height;
    }

    /* Split-ligature clipping is horizontal. Give it the complete scaled line
     * envelope vertically so tall glyphs are never cut at 100% metrics. */
    const uint32_t run_end = std::min<uint32_t>(
        line.run_begin + line.run_count,
        static_cast<uint32_t>(data.runs.size()));
    for (uint32_t index = line.run_begin; index < run_end; ++index) {
        data.runs[index].clip_y = line.y;
        data.runs[index].clip_height = line.height;
    }
}

static std::vector<size_t> format_breaks(const RichTextDocument &doc,
                                         const ParagraphSpan &paragraph)
{
    const size_t paragraph_end = paragraph.start + paragraph.length;
    std::vector<size_t> breaks{paragraph.start, paragraph_end};
    for (const RichTextRange &range : doc.ranges) {
        if ((range.mask & RichTextCharShapingMask) == 0)
            continue;
        const size_t range_end = range.start + range.length;
        if (range_end <= paragraph.start || range.start >= paragraph_end)
            continue;
        breaks.push_back(std::max(range.start, paragraph.start));
        breaks.push_back(std::min(range_end, paragraph_end));
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());
    return breaks;
}

static bool same_shaping_format(const RichTextCharFormat &left,
                                const RichTextCharFormat &right)
{
    return rich_text_char_format_difference_mask(
               left, right, RichTextCharShapingMask) == 0;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using LayoutFormatList = QList<QTextLayout::FormatRange>;
#else
using LayoutFormatList = QVector<QTextLayout::FormatRange>;
#endif

static LayoutFormatList formats_for_paragraph(const RichTextDocument &doc,
                                              const ParagraphSpan &paragraph,
                                              const QtUtf8PositionMap &positions,
                                              float device_scale)
{
    LayoutFormatList formats;
    const std::vector<size_t> breaks = format_breaks(doc, paragraph);
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        const size_t begin = breaks[i];
        const size_t end = breaks[i + 1];
        if (end <= begin)
            continue;
        QTextLayout::FormatRange range;
        range.start = positions.qpos_from_byte(begin - paragraph.start);
        const int qend = positions.qpos_from_byte(end - paragraph.start);
        range.length = std::max(0, qend - range.start);
        range.format = qtext_format(rich_text_format_at(doc, begin), device_scale);
        if (range.length > 0)
            formats.push_back(range);
    }
    return formats;
}

static float paragraph_horizontal_scale_hint(const RichTextDocument &doc,
                                             const ParagraphSpan &paragraph)
{
    if (paragraph.length == 0)
        return 1.0f;
    const std::vector<size_t> breaks = format_breaks(doc, paragraph);
    double weighted = 0.0;
    double weight = 0.0;
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        const size_t begin = breaks[i];
        const size_t end = breaks[i + 1];
        if (end <= begin)
            continue;
        const double segment_weight = static_cast<double>(end - begin);
        const RichTextCharFormat format = rich_text_format_at(doc, begin);
        weighted += segment_weight * std::clamp(
            static_cast<double>(format.scale_x), 0.01, 100.0);
        weight += segment_weight;
    }
    return static_cast<float>(weight > 0.0 ? weighted / weight : 1.0);
}

static void append_empty_line(TextLayoutData &data,
                              const RichTextDocument &doc,
                              const ParagraphSpan &paragraph,
                              uint32_t paragraph_index, float y,
                              float device_scale)
{
    const RichTextCharFormat format =
        rich_text_format_at(doc, std::min(paragraph.start, doc.plain_text.size()));
    const QFontMetricsF metrics(font_from_rich_format(format, device_scale));
    TextLayoutLine line;
    line.byte_start = paragraph.start;
    line.byte_length = 0;
    line.paragraph_index = paragraph_index;
    line.x = 0.0f;
    line.y = y;
    line.width = 0.0f;
    line.height = static_cast<float>(metrics.height());
    line.ascent = static_cast<float>(metrics.ascent());
    line.descent = static_cast<float>(metrics.descent());
    line.baseline = y + line.ascent;
    data.lines.push_back(line);
}

/* QGlyphRun boundaries are local to each shaping/style run. The final cluster
 * of a run must end at the next logical cluster on the line, not at the end of
 * the complete line. Using a per-run list made style-boundary clusters overlap
 * all following styled text, so selection rectangles acquired apparent extra
 * character padding/margins. Finalize every run against one line-wide logical
 * boundary table and rebuild cursor geometry from that exact table. */
static void finalize_line_cluster_boundaries(
    TextLayoutData &data, TextLayoutLine &line,
    const ParagraphSpan &paragraph,
    const QtUtf8PositionMap &positions,
    const QTextLine &qline)
{
    const uint32_t cluster_end_index =
        static_cast<uint32_t>(data.clusters.size());
    if (line.cluster_begin >= cluster_end_index)
        return;

    const size_t line_end = line.byte_start + line.byte_length;
    std::vector<size_t> logical_starts;
    logical_starts.reserve(cluster_end_index - line.cluster_begin + 1);
    for (uint32_t index = line.cluster_begin; index < cluster_end_index; ++index) {
        const size_t start = std::clamp(data.clusters[index].byte_start,
                                        line.byte_start, line_end);
        logical_starts.push_back(start);
    }
    logical_starts.push_back(line_end);
    std::sort(logical_starts.begin(), logical_starts.end());
    logical_starts.erase(
        std::unique(logical_starts.begin(), logical_starts.end()),
        logical_starts.end());

    for (uint32_t index = line.cluster_begin; index < cluster_end_index; ++index) {
        TextLayoutCluster &cluster = data.clusters[index];
        const size_t cluster_start = std::clamp(cluster.byte_start,
                                                 line.byte_start, line_end);
        const auto next = std::upper_bound(logical_starts.begin(),
                                           logical_starts.end(), cluster_start);
        const size_t cluster_end = next == logical_starts.end()
            ? line_end : *next;
        cluster.byte_start = cluster_start;
        cluster.byte_length = cluster_end - cluster_start;

        const int cluster_qstart = positions.qpos_from_byte(
            cluster_start - paragraph.start);
        const int cluster_qend = positions.qpos_from_byte(
            cluster_end - paragraph.start);
        const qreal cursor_start = qline.cursorToX(cluster_qstart);
        const qreal cursor_end = qline.cursorToX(cluster_qend);
        cluster.x = line.x + static_cast<float>(
            std::min(cursor_start, cursor_end));
        cluster.y = line.y;
        cluster.width = std::max(
            0.0f, static_cast<float>(std::abs(cursor_end - cursor_start)));
        cluster.height = line.height;

        cluster.boundary_begin = static_cast<uint32_t>(
            data.cursor_boundaries.size());
        size_t boundary_offset = cluster_start;
        while (true) {
            const int boundary_qpos = positions.qpos_from_byte(
                boundary_offset - paragraph.start);
            data.cursor_boundaries.push_back({
                boundary_offset,
                line.x + static_cast<float>(qline.cursorToX(boundary_qpos))});
            if (boundary_offset >= cluster_end)
                break;
            size_t following = rich_text_utf8_next_boundary(
                data.text, boundary_offset + 1);
            if (following <= boundary_offset || following > cluster_end)
                following = cluster_end;
            boundary_offset = following;
        }
        cluster.boundary_count = static_cast<uint32_t>(
            data.cursor_boundaries.size()) - cluster.boundary_begin;
    }

    const uint32_t run_end_index = static_cast<uint32_t>(data.runs.size());
    for (uint32_t run_index = line.run_begin; run_index < run_end_index;
         ++run_index) {
        TextLayoutRun &run = data.runs[run_index];
        size_t run_start = line_end;
        size_t run_end = line.byte_start;
        const uint32_t last_cluster = std::min<uint32_t>(
            run.cluster_begin + run.cluster_count, cluster_end_index);
        for (uint32_t cluster_index = run.cluster_begin;
             cluster_index < last_cluster; ++cluster_index) {
            const TextLayoutCluster &cluster = data.clusters[cluster_index];
            run_start = std::min(run_start, cluster.byte_start);
            run_end = std::max(run_end,
                               cluster.byte_start + cluster.byte_length);
        }
        if (run_end >= run_start) {
            run.byte_start = run_start;
            run.byte_length = run_end - run_start;
        }
    }
}

} // namespace

ImmutableTextLayout build_text_layout(const TextLayoutRequest &source_request)
{
    TextLayoutRequest request = source_request;
    if (request.document.version < 2 || request.document.blocks.empty())
        request.document.normalize();
    request.device_scale = std::clamp(request.device_scale, 0.01f, 64.0f);
    request.minimum_horizontal_fit =
        std::clamp(request.minimum_horizontal_fit, 0.01f, 1.0f);
    request.max_width = std::max(0.0f, request.max_width);
    request.max_height = std::max(0.0f, request.max_height);

    auto data = std::make_shared<TextLayoutData>();
    data->key = text_layout_key(request);
    data->text = request.document.plain_text;

    /* font_key() hashes physical OpenType tables so fallback faces with the
     * same display name remain distinct. That exactness is important on Linux,
     * but doing the table reads again for every glyph run/line is unnecessary.
     * Reuse the key for equal QRawFont instances within this layout build. */
    std::vector<std::pair<QRawFont, TextLayoutFontKey>> resolved_font_keys;
    auto resolved_font_key = [&](const QRawFont &raw_font) {
        for (const auto &entry : resolved_font_keys) {
            if (entry.first == raw_font)
                return entry.second;
        }
        TextLayoutFontKey key = font_key(raw_font);
        resolved_font_keys.emplace_back(raw_font, key);
        text_layout_register_raw_font(key, raw_font);
        return key;
    };

    const std::vector<ParagraphSpan> paragraphs = paragraph_spans(data->text);
    float cursor_y = 0.0f;
    float natural_width = 0.0f;

    for (uint32_t paragraph_index = 0;
         paragraph_index < static_cast<uint32_t>(paragraphs.size());
         ++paragraph_index) {
        const ParagraphSpan paragraph = paragraphs[paragraph_index];
        const RichTextParagraphFormat paragraph_format =
            rich_text_paragraph_format_at(request.document, paragraph.start);
        cursor_y += std::max(0.0f, paragraph_format.space_before) *
                    request.device_scale;

        const QString paragraph_text = QString::fromUtf8(
            data->text.data() + paragraph.start,
            static_cast<int>(paragraph.length));
        const QtUtf8PositionMap paragraph_positions(paragraph_text);
        if (paragraph_text.isEmpty()) {
            append_empty_line(*data, request.document, paragraph,
                              paragraph_index, cursor_y,
                              request.device_scale);
            cursor_y += data->lines.back().height;
            cursor_y += std::max(0.0f, paragraph_format.space_after) *
                        request.device_scale;
            continue;
        }

        const RichTextCharFormat base_format =
            rich_text_format_at(request.document, paragraph.start);
        QTextLayout layout(paragraph_text,
                           font_from_rich_format(base_format,
                                                 request.device_scale));
        layout.setFormats(formats_for_paragraph(request.document, paragraph,
                                                paragraph_positions,
                                                request.device_scale));
        QTextOption option;
        option.setUseDesignMetrics(true);
        option.setWrapMode(request.overflow_mode == 0
                               ? (paragraph_format.hyphenate
                                      ? QTextOption::WrapAnywhere
                                      : QTextOption::WrapAtWordBoundaryOrAnywhere)
                               : QTextOption::NoWrap);
        /* Shape every line from a neutral left origin. Center, right and all
         * justify variants are resolved after canonical per-cluster H Scale,
         * using the final line width rather than QTextLayout's 100% metrics. */
        option.setAlignment(Qt::AlignLeft);
        layout.setTextOption(option);

        const float left_indent =
            std::max(0.0f, paragraph_format.indent_left) * request.device_scale;
        const float right_indent =
            std::max(0.0f, paragraph_format.indent_right) * request.device_scale;
        const float first_indent = paragraph_format.indent_first_line *
                                   request.device_scale;
        const float available_base = request.max_width > 0.0f
                                         ? std::max(1.0f, request.max_width -
                                                              left_indent -
                                                              right_indent)
                                         : 10000000.0f;

        layout.beginLayout();
        uint32_t paragraph_line_index = 0;
        while (true) {
            QTextLine qline = layout.createLine();
            if (!qline.isValid())
                break;
            const float line_indent = paragraph_line_index == 0 ? first_indent : 0.0f;
            const float available = std::max(1.0f, available_base - line_indent);
            const float horizontal_hint = std::clamp(
                paragraph_horizontal_scale_hint(request.document, paragraph),
                0.01f, 100.0f);
            /* QTextLayout shapes at 100% character width. Compensate its line
             * width so ordinary uniform H Scale wraps at the same point as the
             * explicit post-shaping geometry. Mixed runs use a deterministic
             * weighted hint; final positions remain exact per cluster. */
            const float shaping_width = available / horizontal_hint;
            qline.setLineWidth(request.overflow_mode == 0 || request.max_width > 0.0f
                                   ? std::max(1.0f, shaping_width)
                                   : 10000000.0f);
            qline.setPosition(QPointF(left_indent + line_indent, cursor_y));

            TextLayoutLine line;
            line.byte_start = paragraph.start +
                              paragraph_positions.byte_from_qpos(
                                  qline.textStart());
            const size_t line_end = paragraph.start +
                                    paragraph_positions.byte_from_qpos(
                                        qline.textStart() + qline.textLength());
            line.byte_length = line_end - line.byte_start;
            line.paragraph_index = paragraph_index;
            line.x = static_cast<float>(qline.position().x());
            line.y = static_cast<float>(qline.position().y());
            line.width = static_cast<float>(qline.naturalTextWidth());
            line.height = static_cast<float>(qline.height());
            line.ascent = static_cast<float>(qline.ascent());
            line.descent = static_cast<float>(qline.descent());
            line.baseline = line.y + line.ascent;
            line.run_begin = static_cast<uint32_t>(data->runs.size());
            line.cluster_begin = static_cast<uint32_t>(data->clusters.size());
            line.glyph_begin = static_cast<uint32_t>(data->glyphs.size());

            const int line_qstart = qline.textStart();
            const int line_qend = qline.textStart() + qline.textLength();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            const auto glyph_runs = layout.glyphRuns(
                line_qstart, qline.textLength(),
                QTextLayout::RetrieveGlyphIndexes |
                    QTextLayout::RetrieveGlyphPositions |
                    QTextLayout::RetrieveStringIndexes);
#else
            const auto glyph_runs = layout.glyphRuns(
                line_qstart, qline.textLength());
#endif

            struct MappedGlyphRun {
                QGlyphRun glyph_run;
                std::vector<size_t> cluster_starts;
                std::vector<RichTextCharFormat> formats;
            };
            std::vector<MappedGlyphRun> mapped_runs;
            std::vector<size_t> line_logical_starts;
            line_logical_starts.push_back(line.byte_start + line.byte_length);

            /* Ask Qt for each visual glyph exactly once. Then map every glyph
             * back to its canonical UTF-8 cluster and resolve the complete
             * effective RichTextDocument format at that cluster. Querying
             * glyphRuns() separately for each style range is invalid: Qt may
             * return split/coalesced runs and the same visual glyph more than
             * once. This line-wide map is shared by geometry, selection and
             * GPU emission, so there is only one property source. */
            for (const QGlyphRun &glyph_run : glyph_runs) {
                const auto glyph_ids = glyph_run.glyphIndexes();
                const auto positions = glyph_run.positions();
                if (glyph_ids.isEmpty() || glyph_ids.size() != positions.size())
                    continue;

                MappedGlyphRun mapped;
                mapped.glyph_run = glyph_run;
                mapped.cluster_starts.reserve(static_cast<size_t>(glyph_ids.size()));
                mapped.formats.reserve(static_cast<size_t>(glyph_ids.size()));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                const auto string_indexes = glyph_run.stringIndexes();
#endif
                for (int i = 0; i < static_cast<int>(glyph_ids.size()); ++i) {
                    int qindex = line_qstart;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
                    /* QGlyphRun::stringIndexes() is the authoritative mapping
                     * from each visual glyph to the UTF-16 index in the complete
                     * QTextLayout string. Re-inferring that index from x geometry
                     * is incorrect for bidi text, ligatures, combining marks and
                     * wrapped lines, and was the reason a visible selection could
                     * contain H/V Scale while the emitted glyph used the document
                     * default. */
                    if (i < static_cast<int>(string_indexes.size())) {
                        qindex = static_cast<int>(string_indexes[i]);
                    } else {
                        const qreal relative_x =
                            positions[i].x() - qline.position().x();
                        qindex = qline.xToCursor(
                            relative_x, QTextLine::CursorOnCharacter);
                    }
#else
                    const qreal relative_x =
                        positions[i].x() - qline.position().x();
                    qindex = qline.xToCursor(
                        relative_x, QTextLine::CursorOnCharacter);
#endif
                    qindex = std::clamp(
                        qindex, line_qstart,
                        std::max(line_qstart, line_qend - 1));
                    size_t cluster_byte = paragraph.start +
                        paragraph_positions.byte_from_qpos(qindex);
                    if (line.byte_length > 0) {
                        cluster_byte = std::clamp(
                            cluster_byte, line.byte_start,
                            line.byte_start + line.byte_length - 1);
                        cluster_byte = rich_text_utf8_previous_boundary(
                            data->text, cluster_byte);
                    } else {
                        cluster_byte = line.byte_start;
                    }
                    mapped.cluster_starts.push_back(cluster_byte);
                    mapped.formats.push_back(
                        rich_text_format_at(request.document, cluster_byte));
                    line_logical_starts.push_back(cluster_byte);
                }
                mapped_runs.push_back(std::move(mapped));
            }

            std::sort(line_logical_starts.begin(), line_logical_starts.end());
            line_logical_starts.erase(
                std::unique(line_logical_starts.begin(),
                            line_logical_starts.end()),
                line_logical_starts.end());
            auto line_cluster_end_for = [&](size_t start) {
                const auto next = std::upper_bound(
                    line_logical_starts.begin(), line_logical_starts.end(), start);
                return next == line_logical_starts.end()
                    ? line.byte_start + line.byte_length : *next;
            };

            for (const MappedGlyphRun &mapped : mapped_runs) {
                const auto glyph_ids = mapped.glyph_run.glyphIndexes();
                const auto positions = mapped.glyph_run.positions();
                const QRawFont raw_font = mapped.glyph_run.rawFont();
                const auto advances = raw_font.advancesForGlyphIndexes(glyph_ids);
                const int count = static_cast<int>(glyph_ids.size());

                int segment_begin = 0;
                while (segment_begin < count) {
                    int segment_end = segment_begin + 1;
                    while (segment_end < count &&
                           same_shaping_format(
                               mapped.formats[static_cast<size_t>(segment_begin)],
                               mapped.formats[static_cast<size_t>(segment_end)])) {
                        ++segment_end;
                    }

                    const RichTextCharFormat &run_format =
                        mapped.formats[static_cast<size_t>(segment_begin)];
                    TextLayoutRun run;
                    run.glyph_begin = static_cast<uint32_t>(data->glyphs.size());
                    run.cluster_begin = static_cast<uint32_t>(data->clusters.size());
                    run.line_index = static_cast<uint32_t>(data->lines.size());
                    run.right_to_left = mapped.glyph_run.isRightToLeft();
                    run.split_ligature =
                        mapped.glyph_run.flags().testFlag(QGlyphRun::SplitLigature) ||
                        segment_begin != 0 || segment_end != count;
                    run.font = resolved_font_key(raw_font);
                    run.shaping_style = shaping_style(run_format);

                    size_t run_start = line.byte_start + line.byte_length;
                    size_t run_last_start = line.byte_start;
                    for (int i = segment_begin; i < segment_end; ++i) {
                        const size_t cluster =
                            mapped.cluster_starts[static_cast<size_t>(i)];
                        run_start = std::min(run_start, cluster);
                        run_last_start = std::max(run_last_start, cluster);
                    }
                    if (run_start > line.byte_start + line.byte_length)
                        run_start = line.byte_start;
                    const size_t run_end = line_cluster_end_for(run_last_start);
                    run.byte_start = run_start;
                    run.byte_length = run_end - run_start;

                    const int clip_qstart = paragraph_positions.qpos_from_byte(
                        run_start - paragraph.start);
                    const int clip_qend = paragraph_positions.qpos_from_byte(
                        run_end - paragraph.start);
                    const qreal clip_cursor_start = qline.cursorToX(clip_qstart);
                    const qreal clip_cursor_end = qline.cursorToX(clip_qend);
                    run.clip_x = line.x + static_cast<float>(
                        std::min(clip_cursor_start, clip_cursor_end));
                    run.clip_y = line.y;
                    run.clip_width = static_cast<float>(
                        std::abs(clip_cursor_end - clip_cursor_start));
                    run.clip_height = line.height;

                    std::vector<uint32_t> glyph_indices;
                    glyph_indices.reserve(
                        static_cast<size_t>(segment_end - segment_begin));
                    for (int i = segment_begin; i < segment_end; ++i) {
                        const RichTextCharFormat &glyph_format =
                            mapped.formats[static_cast<size_t>(i)];
                        TextLayoutGlyph glyph;
                        glyph.glyph_id = glyph_ids[i];
                        glyph.run_index = static_cast<uint32_t>(data->runs.size());
                        glyph.x = static_cast<float>(positions[i].x());
                        glyph.y = static_cast<float>(
                            positions[i].y() - glyph_format.baseline_shift *
                                                   request.device_scale);
                        glyph.scale_x = std::clamp(
                            glyph_format.scale_x, 0.01f, 100.0f);
                        glyph.scale_y = std::clamp(
                            glyph_format.scale_y, 0.01f, 100.0f);

                        const QPainterPath glyph_outline =
                            raw_font.pathForGlyph(glyph_ids[i]);
                        const QRectF ink = glyph_outline.isEmpty()
                            ? raw_font.boundingRect(glyph_ids[i])
                            : glyph_outline.boundingRect();
                        glyph.ink_x = glyph.x +
                            static_cast<float>(ink.x()) * glyph.scale_x;
                        glyph.ink_y = glyph.y +
                            static_cast<float>(ink.y()) * glyph.scale_y;
                        glyph.ink_width =
                            static_cast<float>(ink.width()) * glyph.scale_x;
                        glyph.ink_height =
                            static_cast<float>(ink.height()) * glyph.scale_y;
                        if (i < static_cast<int>(advances.size())) {
                            glyph.advance_x =
                                static_cast<float>(advances[i].x()) *
                                glyph.scale_x;
                            glyph.advance_y =
                                static_cast<float>(advances[i].y()) *
                                glyph.scale_y;
                        }
                        glyph_indices.push_back(
                            static_cast<uint32_t>(data->glyphs.size()));
                        data->glyphs.push_back(glyph);
                    }

                    std::vector<size_t> visual_clusters;
                    for (int i = segment_begin; i < segment_end; ++i) {
                        const size_t start =
                            mapped.cluster_starts[static_cast<size_t>(i)];
                        if (std::find(visual_clusters.begin(),
                                      visual_clusters.end(), start) ==
                            visual_clusters.end()) {
                            visual_clusters.push_back(start);
                        }
                    }
                    for (size_t cluster_start : visual_clusters) {
                        TextLayoutCluster cluster;
                        cluster.byte_start = cluster_start;
                        cluster.byte_length =
                            line_cluster_end_for(cluster_start) - cluster_start;
                        cluster.run_index =
                            static_cast<uint32_t>(data->runs.size());
                        cluster.line_index =
                            static_cast<uint32_t>(data->lines.size());
                        cluster.right_to_left = run.right_to_left;
                        cluster.glyph_begin =
                            std::numeric_limits<uint32_t>::max();
                        for (int i = segment_begin; i < segment_end; ++i) {
                            if (mapped.cluster_starts[static_cast<size_t>(i)] !=
                                cluster_start)
                                continue;
                            const uint32_t glyph_index = glyph_indices[
                                static_cast<size_t>(i - segment_begin)];
                            TextLayoutGlyph &glyph = data->glyphs[glyph_index];
                            if (cluster.glyph_begin ==
                                std::numeric_limits<uint32_t>::max()) {
                                cluster.glyph_begin = glyph_index;
                            }
                            ++cluster.glyph_count;
                            glyph.cluster_index =
                                static_cast<uint32_t>(data->clusters.size());
                        }
                        if (cluster.glyph_begin ==
                            std::numeric_limits<uint32_t>::max()) {
                            cluster.glyph_begin = run.glyph_begin;
                        }
                        const int cluster_qstart =
                            paragraph_positions.qpos_from_byte(
                                cluster_start - paragraph.start);
                        const int cluster_qend =
                            paragraph_positions.qpos_from_byte(
                                line_cluster_end_for(cluster_start) -
                                paragraph.start);
                        const qreal cursor_start =
                            qline.cursorToX(cluster_qstart);
                        const qreal cursor_end = qline.cursorToX(cluster_qend);
                        cluster.x = line.x + static_cast<float>(
                            std::min(cursor_start, cursor_end));
                        cluster.y = line.y;
                        cluster.width = std::max(
                            0.0f, static_cast<float>(
                                std::abs(cursor_end - cursor_start)));
                        cluster.height = line.height;
                        cluster.boundary_begin = 0;
                        cluster.boundary_count = 0;
                        data->clusters.push_back(cluster);
                    }

                    run.glyph_count =
                        static_cast<uint32_t>(data->glyphs.size()) -
                        run.glyph_begin;
                    run.cluster_count =
                        static_cast<uint32_t>(data->clusters.size()) -
                        run.cluster_begin;
                    data->runs.push_back(std::move(run));
                    segment_begin = segment_end;
                }
            }

            line.run_count = static_cast<uint32_t>(data->runs.size()) -
                             line.run_begin;
            line.cluster_count = static_cast<uint32_t>(data->clusters.size()) -
                                 line.cluster_begin;
            line.glyph_count = static_cast<uint32_t>(data->glyphs.size()) -
                               line.glyph_begin;
            finalize_line_cluster_boundaries(*data, line, paragraph,
                                             paragraph_positions, qline);
            apply_line_horizontal_character_scale(
                *data, line, request.document);
            normalize_line_vertical_scale(*data, line);
            const bool last_in_paragraph =
                qline.textStart() + qline.textLength() >=
                static_cast<int>(paragraph_text.size());
            const float intrinsic_line_width =
                left_indent + line_indent + line.width + right_indent;
            if (request.max_width > 0.0f) {
                align_scaled_line(*data, line, left_indent + line_indent,
                                  available, paragraph_format.align_h,
                                  last_in_paragraph);
            }
            data->lines.push_back(line);
            natural_width = std::max(natural_width, intrinsic_line_width);
            cursor_y += line.height;
            ++paragraph_line_index;
            if (request.overflow_mode != 0)
                break;
            if (qline.textStart() + qline.textLength() <
                static_cast<int>(paragraph_text.size()))
                cursor_y += paragraph_format.line_spacing * request.device_scale;
        }
        layout.endLayout();
        cursor_y += std::max(0.0f, paragraph_format.space_after) *
                    request.device_scale;
    }

    data->natural_width = std::max(0.0f, natural_width);
    data->natural_height = std::max(0.0f, cursor_y);
    data->horizontal_fit = 1.0f;

    if (request.overflow_mode == 2 && request.max_width > 0.0f &&
        data->natural_width > request.max_width) {
        data->horizontal_fit = std::clamp(
            request.max_width / std::max(1.0f, data->natural_width),
            request.minimum_horizontal_fit, 1.0f);
        for (TextLayoutLine &line : data->lines) {
            const RichTextParagraphFormat paragraph_format =
                rich_text_paragraph_format_at(request.document,
                                              line.byte_start);
            const float origin_x = line.x;
            scale_line_x(*data, line, origin_x, data->horizontal_fit);
            const float available = request.max_width;
            float shift = 0.0f;
            if (paragraph_format.align_h == 1 || paragraph_format.align_h == 4)
                shift = (available - line.width) * 0.5f - line.x;
            else if (paragraph_format.align_h == 2 ||
                     paragraph_format.align_h == 5)
                shift = available - line.width - line.x;
            if (shift != 0.0f)
                translate_line(*data, line, shift, 0.0f);
        }
    }

    const int align_v = request.document.default_paragraph_format.align_v;
    float vertical_shift = 0.0f;
    if (request.max_height > data->natural_height && align_v != 3) {
        if (align_v == 1)
            vertical_shift = (request.max_height - data->natural_height) * 0.5f;
        else if (align_v == 2)
            vertical_shift = request.max_height - data->natural_height;
    }
    if (vertical_shift != 0.0f) {
        for (TextLayoutLine &line : data->lines)
            translate_line(*data, line, 0.0f, vertical_shift);
    } else if (align_v == 3 && data->lines.size() > 1 &&
               request.max_height > data->natural_height) {
        const float gap = (request.max_height - data->natural_height) /
                          static_cast<float>(data->lines.size() - 1);
        for (size_t i = 0; i < data->lines.size(); ++i)
            translate_line(*data, data->lines[i], 0.0f,
                           gap * static_cast<float>(i));
    }

    data->width = request.max_width > 0.0f
                      ? request.max_width
                      : data->natural_width * data->horizontal_fit;
    data->height = request.max_height > 0.0f
                       ? request.max_height
                       : data->natural_height;
    data->valid = true;
    return data;
}
