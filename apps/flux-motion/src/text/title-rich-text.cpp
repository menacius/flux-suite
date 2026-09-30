#include "title-rich-text.h"
#include "pattern-resource-cache.h"
#include <regex>
#include <sstream>
#ifndef OBS_FXM_RICH_TEXT_STANDALONE_TEST
#include "title-data.h"
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#ifndef OBS_FXM_RICH_TEXT_STANDALONE_TEST
RichTextCharFormat rich_text_char_format_from_layer(const Layer &layer)
{
    RichTextCharFormat f;
    f.font_family = layer.font_family;
    f.font_style = layer.font_style;
    f.font_size = layer.font_size;
    f.bold = layer.font_bold;
    f.italic = layer.font_italic;
    f.underline = layer.text_underline;
    f.strikethrough = layer.text_strikethrough;
    f.kerning = layer.font_kerning;
    f.kerning_mode = layer.kerning_mode;
    f.manual_kerning = layer.manual_kerning;
    f.tracking = layer.char_tracking;
    f.scale_x = layer.char_scale_x;
    f.scale_y = layer.char_scale_y;
    f.baseline_shift = layer.baseline_shift;
    f.text_style = layer.text_style;
    f.ligatures = layer.text_ligatures;
    f.stylistic_alternates = layer.text_stylistic_alternates;
    f.fractions = layer.text_fractions;
    f.opentype_features = layer.text_opentype_features;
    f.language = layer.text_language;
    f.fill.type = layer.fill_type;
    f.fill.color = layer.text_color;
    f.fill.gradient_type = layer.gradient_type;
    f.fill.gradient_spread = layer.gradient_spread;
    f.fill.gradient_start_color = layer.gradient_start_color;
    f.fill.gradient_end_color = layer.gradient_end_color;
    f.fill.gradient_start_pos = layer.gradient_start_pos;
    f.fill.gradient_end_pos = layer.gradient_end_pos;
    f.fill.gradient_start_opacity = layer.gradient_start_opacity;
    f.fill.gradient_end_opacity = layer.gradient_end_opacity;
    f.fill.gradient_opacity = layer.gradient_opacity;
    f.fill.gradient_angle = layer.gradient_angle;
    f.fill.gradient_center_x = layer.gradient_center_x;
    f.fill.gradient_center_y = layer.gradient_center_y;
    f.fill.gradient_scale = layer.gradient_scale;
    f.fill.gradient_focal_x = layer.gradient_focal_x;
    f.fill.gradient_focal_y = layer.gradient_focal_y;
    f.fill.gradient_stops = layer.gradient_stops;
    f.stroke.enabled = layer.outline_enabled &&
                       layer.stroke_fill_type != 0 &&
                       layer.stroke_width > 0.0f;
    f.stroke.width = layer.stroke_width;
    f.stroke.opacity = layer.outline_opacity;
    f.stroke.on_front = layer.outline_on_front;
    f.stroke.alignment = layer.outline_alignment;
    f.stroke.antialias = layer.outline_antialias;
    f.stroke.join_style = layer.outline_join_style;
    f.stroke.fill.type = layer.stroke_fill_type == 2 ? 1 : 0;
    f.stroke.fill.color = layer.stroke_color;
    f.stroke.fill.gradient_type = layer.stroke_gradient_type;
    f.stroke.fill.gradient_spread = layer.stroke_gradient_spread;
    f.stroke.fill.gradient_start_color = layer.stroke_gradient_start_color;
    f.stroke.fill.gradient_end_color = layer.stroke_gradient_end_color;
    f.stroke.fill.gradient_start_pos = layer.stroke_gradient_start_pos;
    f.stroke.fill.gradient_end_pos = layer.stroke_gradient_end_pos;
    f.stroke.fill.gradient_start_opacity = layer.stroke_gradient_start_opacity;
    f.stroke.fill.gradient_end_opacity = layer.stroke_gradient_end_opacity;
    f.stroke.fill.gradient_opacity = layer.stroke_gradient_opacity;
    f.stroke.fill.gradient_angle = layer.stroke_gradient_angle;
    f.stroke.fill.gradient_center_x = layer.stroke_gradient_center_x;
    f.stroke.fill.gradient_center_y = layer.stroke_gradient_center_y;
    f.stroke.fill.gradient_scale = layer.stroke_gradient_scale;
    f.stroke.fill.gradient_focal_x = layer.stroke_gradient_focal_x;
    f.stroke.fill.gradient_focal_y = layer.stroke_gradient_focal_y;
    f.stroke.fill.gradient_stops = layer.stroke_gradient_stops;
    return f;
}

static RichTextParagraphFormat layer_paragraph_format(const Layer &layer)
{
    RichTextParagraphFormat f;
    f.align_h = layer.align_h;
    f.align_v = layer.align_v;
    f.indent_left = layer.paragraph_indent_left;
    f.indent_right = layer.paragraph_indent_right;
    f.indent_first_line = layer.paragraph_indent_first_line;
    f.line_spacing = layer.text_leading;
    f.space_before = layer.paragraph_space_before;
    f.space_after = layer.paragraph_space_after;
    f.hyphenate = layer.paragraph_hyphenate;
    return f;
}

#endif

static bool same_fill(const RichTextFill &a, const RichTextFill &b)
{
    return a.type == b.type && a.color == b.color &&
           a.gradient_type == b.gradient_type &&
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
    return a.enabled == b.enabled && a.width == b.width &&
           a.opacity == b.opacity && a.on_front == b.on_front &&
           a.alignment == b.alignment && a.antialias == b.antialias &&
           a.join_style == b.join_style && same_fill(a.fill, b.fill);
}

static size_t clamped_end(size_t start, size_t length, size_t limit)
{
    start = std::min(start, limit);
    return length > limit - start ? limit : start + length;
}

uint32_t rich_text_char_format_difference_mask(const RichTextCharFormat &a,
                                               const RichTextCharFormat &b,
                                               uint32_t mask)
{
    uint32_t diff = 0;
    if ((mask & RichTextCharFontFamily) && a.font_family != b.font_family) diff |= RichTextCharFontFamily;
    if ((mask & RichTextCharFontStyle) && a.font_style != b.font_style) diff |= RichTextCharFontStyle;
    if ((mask & RichTextCharFontSize) && a.font_size != b.font_size) diff |= RichTextCharFontSize;
    if ((mask & RichTextCharBold) && a.bold != b.bold) diff |= RichTextCharBold;
    if ((mask & RichTextCharItalic) && a.italic != b.italic) diff |= RichTextCharItalic;
    if ((mask & RichTextCharUnderline) && a.underline != b.underline) diff |= RichTextCharUnderline;
    if ((mask & RichTextCharStrikethrough) && a.strikethrough != b.strikethrough) diff |= RichTextCharStrikethrough;
    if ((mask & RichTextCharKerning) &&
        (a.kerning != b.kerning || a.kerning_mode != b.kerning_mode ||
         a.manual_kerning != b.manual_kerning)) diff |= RichTextCharKerning;
    if ((mask & RichTextCharTracking) && a.tracking != b.tracking) diff |= RichTextCharTracking;
    if ((mask & RichTextCharScaleX) && a.scale_x != b.scale_x) diff |= RichTextCharScaleX;
    if ((mask & RichTextCharScaleY) && a.scale_y != b.scale_y) diff |= RichTextCharScaleY;
    if ((mask & RichTextCharBaselineShift) && a.baseline_shift != b.baseline_shift) diff |= RichTextCharBaselineShift;
    if ((mask & RichTextCharFillColor) && !same_fill(a.fill, b.fill)) diff |= RichTextCharFillColor;
    if ((mask & RichTextCharTextStyle) && a.text_style != b.text_style) diff |= RichTextCharTextStyle;
    if ((mask & RichTextCharLigatures) && a.ligatures != b.ligatures) diff |= RichTextCharLigatures;
    if ((mask & RichTextCharStylisticAlternates) && a.stylistic_alternates != b.stylistic_alternates)
        diff |= RichTextCharStylisticAlternates;
    if ((mask & RichTextCharFractions) && a.fractions != b.fractions) diff |= RichTextCharFractions;
    if ((mask & RichTextCharOpenTypeFeatures) && a.opentype_features != b.opentype_features)
        diff |= RichTextCharOpenTypeFeatures;
    if ((mask & RichTextCharLanguage) && a.language != b.language) diff |= RichTextCharLanguage;
    if ((mask & RichTextCharStroke) && !same_stroke(a.stroke, b.stroke)) diff |= RichTextCharStroke;
    return diff;
}

void rich_text_merge_char_format(RichTextCharFormat &target,
                                 const RichTextCharFormat &source, uint32_t mask)
{
    if (mask & RichTextCharFontFamily) target.font_family = source.font_family;
    if (mask & RichTextCharFontStyle) target.font_style = source.font_style;
    if (mask & RichTextCharFontSize) target.font_size = source.font_size;
    if (mask & RichTextCharBold) target.bold = source.bold;
    if (mask & RichTextCharItalic) target.italic = source.italic;
    if (mask & RichTextCharUnderline) target.underline = source.underline;
    if (mask & RichTextCharStrikethrough) target.strikethrough = source.strikethrough;
    if (mask & RichTextCharKerning) {
        target.kerning = source.kerning;
        target.kerning_mode = source.kerning_mode;
        target.manual_kerning = source.manual_kerning;
    }
    if (mask & RichTextCharTracking) target.tracking = source.tracking;
    if (mask & RichTextCharScaleX) target.scale_x = source.scale_x;
    if (mask & RichTextCharScaleY) target.scale_y = source.scale_y;
    if (mask & RichTextCharBaselineShift) target.baseline_shift = source.baseline_shift;
    if (mask & RichTextCharFillColor) target.fill = source.fill;
    if (mask & RichTextCharTextStyle) target.text_style = source.text_style;
    if (mask & RichTextCharLigatures) target.ligatures = source.ligatures;
    if (mask & RichTextCharStylisticAlternates) target.stylistic_alternates = source.stylistic_alternates;
    if (mask & RichTextCharFractions) target.fractions = source.fractions;
    if (mask & RichTextCharOpenTypeFeatures) target.opentype_features = source.opentype_features;
    if (mask & RichTextCharLanguage) target.language = source.language;
    if (mask & RichTextCharStroke) target.stroke = source.stroke;
}


RichTextFontScaleMetrics rich_text_font_scale_metrics(float scale_x, float scale_y)
{
    const float sx = std::clamp(scale_x, 0.01f, 100.0f);
    const float sy = std::clamp(scale_y, 0.01f, 100.0f);
    RichTextFontScaleMetrics result;
    result.vertical_factor = sy;
    /* Compatibility adapters may still need a normalized horizontal percent.
     * The canonical GPU pipeline does not pass this value to QFont::setStretch;
     * it transforms shaped cluster geometry explicitly. */
    result.horizontal_stretch_percent = std::clamp(
        static_cast<int>(std::lround(sx * 100.0f)), 1, 4000);
    return result;
}

uint32_t rich_text_paragraph_format_difference_mask(const RichTextParagraphFormat &a,
                                                    const RichTextParagraphFormat &b,
                                                    uint32_t mask)
{
    uint32_t diff = 0;
    if ((mask & RichTextParagraphAlignH) && a.align_h != b.align_h) diff |= RichTextParagraphAlignH;
    if ((mask & RichTextParagraphAlignV) && a.align_v != b.align_v) diff |= RichTextParagraphAlignV;
    if ((mask & RichTextParagraphIndentLeft) && a.indent_left != b.indent_left)
        diff |= RichTextParagraphIndentLeft;
    if ((mask & RichTextParagraphIndentRight) && a.indent_right != b.indent_right)
        diff |= RichTextParagraphIndentRight;
    if ((mask & RichTextParagraphIndentFirstLine) && a.indent_first_line != b.indent_first_line)
        diff |= RichTextParagraphIndentFirstLine;
    if ((mask & RichTextParagraphLineSpacing) && a.line_spacing != b.line_spacing)
        diff |= RichTextParagraphLineSpacing;
    if ((mask & RichTextParagraphSpaceBefore) && a.space_before != b.space_before)
        diff |= RichTextParagraphSpaceBefore;
    if ((mask & RichTextParagraphSpaceAfter) && a.space_after != b.space_after)
        diff |= RichTextParagraphSpaceAfter;
    if ((mask & RichTextParagraphHyphenate) && a.hyphenate != b.hyphenate)
        diff |= RichTextParagraphHyphenate;
    return diff;
}

void rich_text_merge_paragraph_format(RichTextParagraphFormat &target,
                                      const RichTextParagraphFormat &source, uint32_t mask)
{
    if (mask & RichTextParagraphAlignH) target.align_h = source.align_h;
    if (mask & RichTextParagraphAlignV) target.align_v = source.align_v;
    if (mask & RichTextParagraphIndentLeft) target.indent_left = source.indent_left;
    if (mask & RichTextParagraphIndentRight) target.indent_right = source.indent_right;
    if (mask & RichTextParagraphIndentFirstLine) target.indent_first_line = source.indent_first_line;
    if (mask & RichTextParagraphLineSpacing) target.line_spacing = source.line_spacing;
    if (mask & RichTextParagraphSpaceBefore) target.space_before = source.space_before;
    if (mask & RichTextParagraphSpaceAfter) target.space_after = source.space_after;
    if (mask & RichTextParagraphHyphenate) target.hyphenate = source.hyphenate;
}

static bool same_format(const RichTextCharFormat &a, const RichTextCharFormat &b)
{
    return rich_text_char_format_difference_mask(a, b) == 0;
}

static bool utf8_continuation(unsigned char c)
{
    return (c & 0xC0u) == 0x80u;
}

bool rich_text_utf8_is_boundary(const std::string &text, size_t byte_offset)
{
    return byte_offset == 0 || byte_offset >= text.size() ||
           !utf8_continuation(static_cast<unsigned char>(text[byte_offset]));
}

size_t rich_text_utf8_previous_boundary(const std::string &text, size_t byte_offset)
{
    byte_offset = std::min(byte_offset, text.size());
    while (byte_offset > 0 && byte_offset < text.size() &&
           utf8_continuation(static_cast<unsigned char>(text[byte_offset])))
        --byte_offset;
    return byte_offset;
}

size_t rich_text_utf8_next_boundary(const std::string &text, size_t byte_offset)
{
    byte_offset = std::min(byte_offset, text.size());
    while (byte_offset < text.size() &&
           utf8_continuation(static_cast<unsigned char>(text[byte_offset])))
        ++byte_offset;
    return byte_offset;
}

size_t rich_text_utf8_byte_offset_from_utf16_position(
    const std::string &text, int utf16_position)
{
    if (utf16_position <= 0 || text.empty())
        return 0;
    size_t byte_offset = 0;
    int utf16_units = 0;
    while (byte_offset < text.size() && utf16_units < utf16_position) {
        const unsigned char lead =
            static_cast<unsigned char>(text[byte_offset]);
        size_t byte_length = 1;
        uint32_t codepoint = lead;
        if ((lead & 0xE0u) == 0xC0u && byte_offset + 1 < text.size()) {
            byte_length = 2;
            codepoint = ((lead & 0x1Fu) << 6) |
                        (static_cast<unsigned char>(text[byte_offset + 1]) &
                         0x3Fu);
        } else if ((lead & 0xF0u) == 0xE0u &&
                   byte_offset + 2 < text.size()) {
            byte_length = 3;
            codepoint = ((lead & 0x0Fu) << 12) |
                        ((static_cast<unsigned char>(text[byte_offset + 1]) &
                          0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[byte_offset + 2]) &
                         0x3Fu);
        } else if ((lead & 0xF8u) == 0xF0u &&
                   byte_offset + 3 < text.size()) {
            byte_length = 4;
            codepoint = ((lead & 0x07u) << 18) |
                        ((static_cast<unsigned char>(text[byte_offset + 1]) &
                          0x3Fu) << 12) |
                        ((static_cast<unsigned char>(text[byte_offset + 2]) &
                          0x3Fu) << 6) |
                        (static_cast<unsigned char>(text[byte_offset + 3]) &
                         0x3Fu);
        }
        const int units = codepoint > 0xFFFFu ? 2 : 1;
        if (utf16_units + units > utf16_position)
            break;
        utf16_units += units;
        byte_offset += byte_length;
    }
    return std::min(byte_offset, text.size());
}

static size_t utf8_codepoint_end(const std::string &text, size_t start)
{
    if (start >= text.size()) return text.size();
    size_t end = start + 1;
    while (end < text.size() && utf8_continuation(static_cast<unsigned char>(text[end])))
        ++end;
    return end;
}

static size_t utf8_codepoint_start_before(const std::string &text, size_t end)
{
    if (end == 0) return 0;
    size_t start = std::min(end, text.size()) - 1;
    while (start > 0 && utf8_continuation(static_cast<unsigned char>(text[start])))
        --start;
    return start;
}

static std::vector<std::pair<size_t, size_t>> paragraph_spans(const std::string &text)
{
    std::vector<std::pair<size_t, size_t>> spans;
    const size_t text_len = text.size();
    size_t start = 0;
    while (start <= text_len) {
        size_t end = start;
        while (end < text_len && text[end] != '\n' && text[end] != '\r')
            ++end;
        spans.emplace_back(start, end - start);
        if (end >= text_len) break;
        if (text[end] == '\r' && end + 1 < text_len && text[end + 1] == '\n')
            start = end + 2;
        else
            start = end + 1;
    }
    return spans;
}

static const RichTextBlock *paragraph_block_for_start(const std::vector<RichTextBlock> &blocks,
                                                      size_t start)
{
    const RichTextBlock *previous = nullptr;
    for (const auto &block : blocks) {
        if (block.start == start)
            return &block;
        if (block.start <= start && start <= clamped_end(block.start, block.length, SIZE_MAX))
            return &block;
        if (block.start <= start && (!previous || block.start > previous->start))
            previous = &block;
    }
    return previous;
}

static RichTextParagraphFormat paragraph_format_for_start(const std::vector<RichTextBlock> &blocks,
                                                           size_t start,
                                                           const RichTextParagraphFormat &fallback,
                                                           uint32_t *resolved_mask = nullptr)
{
    RichTextParagraphFormat result = fallback;
    const RichTextBlock *block = paragraph_block_for_start(blocks, start);
    const uint32_t mask = block ? (block->mask & RichTextParagraphAll) : 0;
    if (block)
        rich_text_merge_paragraph_format(result, block->format, mask);
    if (resolved_mask)
        *resolved_mask = mask;
    return result;
}

RichTextParagraphFormat rich_text_paragraph_format_at(const RichTextDocument &doc,
                                                      size_t paragraph_byte_offset)
{
    return paragraph_format_for_start(doc.blocks,
                                      rich_text_utf8_previous_boundary(
                                          doc.plain_text,
                                          std::min(paragraph_byte_offset, doc.plain_text.size())),
                                      doc.default_paragraph_format);
}

RichTextCharFormat rich_text_format_at(const RichTextDocument &doc, size_t byte_offset)
{
    RichTextCharFormat result = doc.default_format;
    const size_t offset = rich_text_utf8_previous_boundary(doc.plain_text,
                                                            std::min(byte_offset, doc.plain_text.size()));
    for (const auto &range : doc.ranges) {
        if (offset >= range.start && offset < clamped_end(range.start, range.length, doc.plain_text.size()))
            rich_text_merge_char_format(result, range.format, range.mask);
    }
    return result;
}

bool rich_text_document_has_anisotropic_scale(const RichTextDocument &doc,
                                               float epsilon)
{
    const size_t text_size = doc.plain_text.size();
    if (text_size == 0)
        return false;

    std::vector<size_t> boundaries;
    boundaries.reserve(doc.ranges.size() * 2 + 1);
    boundaries.push_back(0);
    for (const RichTextRange &range : doc.ranges) {
        const size_t start = std::min(range.start, text_size);
        const size_t end = clamped_end(start, range.length, text_size);
        if (start < text_size)
            boundaries.push_back(start);
        if (end < text_size)
            boundaries.push_back(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());

    const double tolerance = std::max(0.0, static_cast<double>(epsilon));
    for (const size_t offset : boundaries) {
        const RichTextCharFormat format = rich_text_format_at(doc, offset);
        const double scale_x = static_cast<double>(format.scale_x);
        const double scale_y = static_cast<double>(format.scale_y);
        if (!std::isfinite(scale_x) || !std::isfinite(scale_y) ||
            std::abs(scale_x - scale_y) > tolerance)
            return true;
    }
    return false;
}

uint32_t rich_text_format_mask_at(const RichTextDocument &doc, size_t byte_offset)
{
    uint32_t mask = 0;
    const size_t offset = rich_text_utf8_previous_boundary(
        doc.plain_text, std::min(byte_offset, doc.plain_text.size()));
    for (const auto &range : doc.ranges) {
        if (offset >= range.start &&
            offset < clamped_end(range.start, range.length, doc.plain_text.size()))
            mask |= range.mask;
    }
    return mask & RichTextCharAll;
}

uint32_t rich_text_paragraph_format_mask_at(const RichTextDocument &doc,
                                            size_t paragraph_byte_offset)
{
    if (doc.blocks.empty())
        return 0;
    const size_t text_len = doc.plain_text.size();
    const size_t offset = rich_text_utf8_previous_boundary(
        doc.plain_text, std::min(paragraph_byte_offset, text_len));
    for (const auto &block : doc.blocks) {
        const size_t end = clamped_end(block.start, block.length, text_len);
        const bool empty_final_block = block.length == 0 && block.start == text_len;
        if ((offset >= block.start && offset < end) ||
            empty_final_block ||
            (offset == text_len && end == text_len))
            return block.mask & RichTextParagraphAll;
    }
    return 0;
}

void rich_text_document_apply_paragraph_format(RichTextDocument &doc,
                                               size_t selection_start,
                                               size_t selection_end,
                                               const RichTextParagraphFormat &format,
                                               uint32_t mask)
{
    doc.normalize();
    mask &= RichTextParagraphAll;
    if (mask == 0 || doc.blocks.empty())
        return;

    const size_t text_len = doc.plain_text.size();
    selection_start = rich_text_utf8_previous_boundary(
        doc.plain_text, std::min(selection_start, text_len));
    selection_end = rich_text_utf8_next_boundary(
        doc.plain_text, std::min(selection_end, text_len));
    if (selection_end < selection_start)
        std::swap(selection_start, selection_end);

    const bool collapsed = selection_start == selection_end;
    for (auto &block : doc.blocks) {
        const size_t block_end = clamped_end(block.start, block.length, text_len);
        bool selected = false;
        if (collapsed) {
            selected = selection_start >= block.start &&
                       (selection_start < block_end ||
                        (selection_start == text_len && block_end == text_len));
        } else {
            selected = block.start < selection_end && block_end >= selection_start;
            /* A selection ending exactly at the next paragraph start must not
             * style that next paragraph. */
            if (block.start == selection_end)
                selected = false;
        }
        if (!selected)
            continue;
        RichTextParagraphFormat effective = doc.default_paragraph_format;
        rich_text_merge_paragraph_format(effective, block.format, block.mask);
        rich_text_merge_paragraph_format(effective, format, mask);
        block.format = effective;
        block.mask = (block.mask | mask) & RichTextParagraphAll;
    }
    doc.normalize();
}

void rich_text_document_apply_default_char_format(
    RichTextDocument &doc, const RichTextCharFormat &format, uint32_t mask,
    bool clear_matching_overrides)
{
    mask &= RichTextCharAll;
    if (mask == 0)
        return;
    doc.normalize();
    rich_text_merge_char_format(doc.default_format, format, mask);
    if (clear_matching_overrides) {
        for (auto &range : doc.ranges)
            range.mask &= ~mask;
        doc.typing_format_mask &= ~mask;
        if (doc.typing_format_mask == 0)
            doc.has_typing_format = false;
    }
    doc.normalize();
}

void rich_text_document_apply_default_paragraph_format(
    RichTextDocument &doc, const RichTextParagraphFormat &format, uint32_t mask,
    bool clear_matching_overrides)
{
    mask &= RichTextParagraphAll;
    if (mask == 0)
        return;
    doc.normalize();
    rich_text_merge_paragraph_format(doc.default_paragraph_format, format, mask);
    if (clear_matching_overrides) {
        for (auto &block : doc.blocks)
            block.mask &= ~mask;
    }
    doc.normalize();
}

void rich_text_document_clear_char_format_mask(RichTextDocument &doc, uint32_t mask)
{
    mask &= RichTextCharAll;
    if (mask == 0)
        return;
    for (auto &range : doc.ranges)
        range.mask &= ~mask;
    doc.typing_format_mask &= ~mask;
    if (doc.typing_format_mask == 0)
        doc.has_typing_format = false;
    doc.normalize();
}

void rich_text_document_clear_paragraph_format_mask(RichTextDocument &doc, uint32_t mask)
{
    mask &= RichTextParagraphAll;
    if (mask == 0)
        return;
    for (auto &block : doc.blocks)
        block.mask &= ~mask;
    doc.normalize();
}

RichTextCharFormat rich_text_effective_typing_format(const RichTextDocument &doc)
{
    RichTextCharFormat result = doc.default_format;
    if (doc.has_typing_format)
        rich_text_merge_char_format(result, doc.typing_format,
                                    doc.typing_format_mask & RichTextCharAll);
    return result;
}

void RichTextDocument::normalize()
{
    version = 2;
    const size_t text_len = plain_text.size();
    selection.anchor = rich_text_utf8_previous_boundary(plain_text, std::min(selection.anchor, text_len));
    selection.head = rich_text_utf8_previous_boundary(plain_text, std::min(selection.head, text_len));

    std::vector<RichTextRange> input;
    input.reserve(ranges.size());
    std::vector<size_t> boundaries{0, text_len};
    for (auto range : ranges) {
        if (range.length == 0 || range.start >= text_len || (range.mask & RichTextCharAll) == 0)
            continue;
        const size_t raw_end = clamped_end(range.start, range.length, text_len);
        range.start = rich_text_utf8_previous_boundary(plain_text, range.start);
        const size_t end = rich_text_utf8_next_boundary(plain_text, raw_end);
        if (end <= range.start) continue;
        range.length = end - range.start;
        range.mask &= RichTextCharAll;
        input.push_back(range);
        boundaries.push_back(range.start);
        boundaries.push_back(end);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    ranges.clear();
    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const size_t start = boundaries[i];
        const size_t end = boundaries[i + 1];
        if (end <= start) continue;
        RichTextCharFormat effective = default_format;
        uint32_t explicit_mask = 0;
        for (const auto &range : input) {
            if (start >= range.start && start < clamped_end(range.start, range.length, text_len)) {
                rich_text_merge_char_format(effective, range.format, range.mask);
                explicit_mask |= range.mask;
            }
        }
        explicit_mask &= RichTextCharAll;
        if (explicit_mask == 0) continue;
        if (!ranges.empty()) {
            auto &previous = ranges.back();
            if (clamped_end(previous.start, previous.length, text_len) == start &&
                previous.mask == explicit_mask &&
                same_format(previous.format, effective)) {
                previous.length += end - start;
                continue;
            }
        }
        ranges.push_back({start, end - start, effective, explicit_mask});
    }

    const std::vector<RichTextBlock> previous_blocks = blocks;
    blocks.clear();
    for (const auto &[start, length] : paragraph_spans(plain_text)) {
        RichTextBlock block;
        block.start = start;
        block.length = length;
        block.format = paragraph_format_for_start(previous_blocks, start,
                                                  default_paragraph_format, &block.mask);
        blocks.push_back(block);
    }

    auto_default_style_cached_mask &= RichTextCharAll;
    for (auto &rule : auto_style_rules)
        rule.cached_mask &= RichTextCharAll;

    typing_format_mask &= RichTextCharAll;
    if (!has_typing_format) {
        typing_format = default_format;
        typing_format_mask = 0;
    }
}

void rich_text_document_apply_format(RichTextDocument &doc, size_t start, size_t length,
                                     const RichTextCharFormat &format, uint32_t mask)
{
    doc.normalize();
    if (doc.plain_text.empty() || length == 0 || (mask & RichTextCharAll) == 0)
        return;
    const size_t text_len = doc.plain_text.size();
    const size_t raw_start = std::min(start, text_len);
    const size_t raw_end = clamped_end(raw_start, length, text_len);
    start = rich_text_utf8_previous_boundary(doc.plain_text, raw_start);
    const size_t end = rich_text_utf8_next_boundary(doc.plain_text,
                                                     raw_end);
    if (end <= start) return;

    std::vector<size_t> boundaries{0, text_len, start, end};
    for (const auto &range : doc.ranges) {
        boundaries.push_back(range.start);
        boundaries.push_back(clamped_end(range.start, range.length, text_len));
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

    std::vector<RichTextRange> next;
    for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const size_t seg_start = boundaries[i];
        const size_t seg_end = boundaries[i + 1];
        if (seg_end <= seg_start) continue;
        RichTextCharFormat effective = rich_text_format_at(doc, seg_start);
        uint32_t effective_mask = 0;
        for (const auto &range : doc.ranges) {
            if (seg_start >= range.start &&
                seg_start < clamped_end(range.start, range.length, text_len))
                effective_mask |= range.mask;
        }
        if (seg_start >= start && seg_start < end) {
            rich_text_merge_char_format(effective, format, mask);
            effective_mask |= mask;
        }
        effective_mask &= RichTextCharAll;
        if (effective_mask != 0)
            next.push_back({seg_start, seg_end - seg_start, effective, effective_mask});
    }
    doc.ranges = std::move(next);
    doc.normalize();
}

#ifndef OBS_FXM_RICH_TEXT_STANDALONE_TEST
RichTextDocument rich_text_document_from_layer_defaults(const Layer &layer)
{
    RichTextDocument doc;
    doc.plain_text = layer.text_content;
    doc.default_format = rich_text_char_format_from_layer(layer);
    doc.default_paragraph_format = layer_paragraph_format(layer);
    doc.selection = {doc.plain_text.size(), doc.plain_text.size()};
    doc.typing_format = doc.default_format;
    doc.typing_format_mask = 0;
    doc.has_typing_format = false;
    doc.normalize();
    return doc;
}

RichTextDocument rich_text_document_canonical_copy(const Layer &layer)
{
    if (layer.rich_text.empty())
        return rich_text_document_from_layer_defaults(layer);
    RichTextDocument document = layer.rich_text;
    /* Version-2 documents with paragraph blocks are normalized at every
     * persistent mutation/deserialization boundary. Do not rescan the complete
     * text simply because a frame needs a read-only copy. */
    if (document.version < 2 || document.blocks.empty())
        document.normalize();
    return document;
}



static void set_static_argb_channels(AnimatedProperty &a, AnimatedProperty &r,
                                     AnimatedProperty &g, AnimatedProperty &b,
                                     uint32_t argb)
{
    a.static_value = (argb >> 24) & 0xFF;
    r.static_value = (argb >> 16) & 0xFF;
    g.static_value = (argb >> 8) & 0xFF;
    b.static_value = argb & 0xFF;
}

static void rich_text_document_sync_layer_mirrors_impl(
    Layer &layer, bool document_is_canonical)
{
    if (layer.type != LayerType::Text && layer.type != LayerType::Ticker && layer.type != LayerType::Clock)
        return;
    if (layer.rich_text.empty()) {
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
        document_is_canonical = true;
    }
    if (!document_is_canonical)
        layer.rich_text.normalize();

    /* Layer scalar fields are compatibility mirrors for the document defaults.
     * Cursor/typing state is editor-session data and must never rewrite the
     * persistent layer-wide style or alter non-rich fallback rendering. */
    const RichTextCharFormat &f = layer.rich_text.default_format;
    layer.text_content = layer.rich_text.plain_text;
    layer.font_family = f.font_family;
    layer.font_style = f.font_style;
    layer.font_size = f.font_size;
    layer.font_bold = f.bold;
    layer.font_italic = f.italic;
    layer.text_underline = f.underline;
    layer.text_strikethrough = f.strikethrough;
    layer.font_kerning = f.kerning;
    layer.kerning_mode = f.kerning_mode;
    layer.manual_kerning = f.manual_kerning;
    layer.char_tracking = f.tracking;
    layer.char_scale_x = f.scale_x;
    layer.char_scale_y = f.scale_y;
    layer.baseline_shift = f.baseline_shift;
    /* The RichTextDocument default is the sole authored static source. The
     * scalar fields above and non-animated timeline properties are mirrors for
     * legacy serialization/UI only. Animated tracks remain independent
     * overlays and therefore retain their keyframes/static base. */
    if (!layer.font_size_prop.is_animated())
        layer.font_size_prop.static_value = f.font_size;
    if (!layer.char_tracking_prop.is_animated())
        layer.char_tracking_prop.static_value = f.tracking;
    if (!layer.char_scale_x_prop.is_animated())
        layer.char_scale_x_prop.static_value = f.scale_x;
    if (!layer.char_scale_y_prop.is_animated())
        layer.char_scale_y_prop.static_value = f.scale_y;
    if (!layer.baseline_shift_prop.is_animated())
        layer.baseline_shift_prop.static_value = f.baseline_shift;
    layer.text_style = f.text_style;
    layer.text_ligatures = f.ligatures;
    layer.text_stylistic_alternates = f.stylistic_alternates;
    layer.text_fractions = f.fractions;
    layer.text_opentype_features = f.opentype_features;
    layer.text_language = f.language;
    layer.fill_type = f.fill.type;
    layer.text_color = f.fill.color;
    layer.gradient_type = f.fill.gradient_type;
    layer.gradient_spread = f.fill.gradient_spread;
    layer.gradient_start_color = f.fill.gradient_start_color;
    layer.gradient_end_color = f.fill.gradient_end_color;
    layer.gradient_start_pos = f.fill.gradient_start_pos;
    layer.gradient_end_pos = f.fill.gradient_end_pos;
    layer.gradient_start_opacity = f.fill.gradient_start_opacity;
    layer.gradient_end_opacity = f.fill.gradient_end_opacity;
    layer.gradient_opacity = f.fill.gradient_opacity;
    layer.gradient_angle = f.fill.gradient_angle;
    layer.gradient_center_x = f.fill.gradient_center_x;
    layer.gradient_center_y = f.fill.gradient_center_y;
    layer.gradient_scale = f.fill.gradient_scale;
    layer.gradient_focal_x = f.fill.gradient_focal_x;
    layer.gradient_focal_y = f.fill.gradient_focal_y;
    layer.gradient_stops = f.fill.gradient_stops;
    set_static_argb_channels(layer.text_color_a, layer.text_color_r, layer.text_color_g,
                             layer.text_color_b, layer.text_color);

    const RichTextStroke &stroke = f.stroke;
    layer.outline_enabled = stroke.enabled && stroke.width > 0.0f;
    layer.stroke_width = stroke.width;
    layer.stroke_width_prop.static_value = stroke.width;
    layer.outline_opacity = stroke.opacity;
    layer.outline_on_front = stroke.on_front;
    layer.outline_alignment = stroke.alignment;
    layer.outline_antialias = stroke.antialias;
    layer.outline_join_style = stroke.join_style;
    layer.stroke_fill_type = !stroke.enabled ? 0 :
        (stroke.fill.type == 1 ? 2 : 1);
    layer.stroke_color = stroke.fill.color;
    set_static_argb_channels(layer.stroke_color_a, layer.stroke_color_r,
                             layer.stroke_color_g, layer.stroke_color_b,
                             layer.stroke_color);
    layer.stroke_gradient_type = stroke.fill.gradient_type;
    layer.stroke_gradient_spread = stroke.fill.gradient_spread;
    layer.stroke_gradient_start_color = stroke.fill.gradient_start_color;
    layer.stroke_gradient_end_color = stroke.fill.gradient_end_color;
    layer.stroke_gradient_start_pos = stroke.fill.gradient_start_pos;
    layer.stroke_gradient_end_pos = stroke.fill.gradient_end_pos;
    layer.stroke_gradient_start_opacity = stroke.fill.gradient_start_opacity;
    layer.stroke_gradient_end_opacity = stroke.fill.gradient_end_opacity;
    layer.stroke_gradient_opacity = stroke.fill.gradient_opacity;
    layer.stroke_gradient_angle = stroke.fill.gradient_angle;
    layer.stroke_gradient_center_x = stroke.fill.gradient_center_x;
    layer.stroke_gradient_center_y = stroke.fill.gradient_center_y;
    layer.stroke_gradient_scale = stroke.fill.gradient_scale;
    layer.stroke_gradient_focal_x = stroke.fill.gradient_focal_x;
    layer.stroke_gradient_focal_y = stroke.fill.gradient_focal_y;
    layer.stroke_gradient_stops = stroke.fill.gradient_stops;

    const RichTextParagraphFormat &p = layer.rich_text.default_paragraph_format;
    layer.align_h = p.align_h;
    layer.align_v = p.align_v;
    layer.paragraph_indent_left = p.indent_left;
    layer.paragraph_indent_right = p.indent_right;
    layer.paragraph_indent_first_line = p.indent_first_line;
    layer.text_leading = p.line_spacing;
    layer.paragraph_space_before = p.space_before;
    layer.paragraph_space_after = p.space_after;
    layer.paragraph_hyphenate = p.hyphenate;
    layer.paragraph_indent_left_prop.static_value = p.indent_left;
    layer.paragraph_indent_right_prop.static_value = p.indent_right;
    layer.paragraph_indent_first_line_prop.static_value = p.indent_first_line;
    layer.paragraph_space_before_prop.static_value = p.space_before;
    layer.paragraph_space_after_prop.static_value = p.space_after;
}


void rich_text_document_sync_layer_mirrors(Layer &layer)
{
    rich_text_document_sync_layer_mirrors_impl(layer, false);
}

void rich_text_document_sync_layer_mirrors_canonical(Layer &layer)
{
    rich_text_document_sync_layer_mirrors_impl(layer, true);
}

#endif

static void rich_text_document_replace_range_impl(
    RichTextDocument &doc, size_t prefix, size_t removed_len,
    const std::string &inserted_text,
    const RichTextCharFormat *insertion_format, uint32_t insertion_mask,
    bool input_is_canonical)
{
    if (!input_is_canonical)
        doc.normalize();
    const std::string old = doc.plain_text;
    prefix = rich_text_utf8_previous_boundary(old, std::min(prefix, old.size()));
    const size_t raw_old_suffix = removed_len > old.size() - prefix
        ? old.size() : prefix + removed_len;
    const size_t old_suffix = rich_text_utf8_next_boundary(old, raw_old_suffix);
    const size_t inserted_len = inserted_text.size();
    const std::string next_text = old.substr(0, prefix) + inserted_text +
                                  old.substr(old_suffix);
    if (old == next_text)
        return;
    removed_len = old_suffix - prefix;
    const RichTextSelection before_selection = doc.selection;
    const RichTextCharFormat inherited_format = insertion_format ? *insertion_format : doc.default_format;
    insertion_mask = insertion_format ? (insertion_mask & RichTextCharAll) : 0;

    std::vector<RichTextRange> next_ranges;
    for (auto range : doc.ranges) {
        const size_t range_end = clamped_end(range.start, range.length, old.size());
        if (range_end <= prefix) {
            next_ranges.push_back(range);
        } else if (range.start >= old_suffix) {
            range.start = range.start - removed_len + inserted_len;
            next_ranges.push_back(range);
        } else {
            if (range.start < prefix)
                next_ranges.push_back({range.start, prefix - range.start, range.format, range.mask});
            if (range_end > old_suffix)
                next_ranges.push_back({prefix + inserted_len, range_end - old_suffix, range.format, range.mask});
        }
    }
    if (inserted_len > 0 && insertion_mask != 0)
        next_ranges.push_back({prefix, inserted_len, inherited_format, insertion_mask});

    /* Rebuild paragraph starts from the edited text, but source each sparse
     * paragraph override from the corresponding paragraph in the old text.
     * Merely shifting block starts loses the style when characters are inserted
     * at the beginning of a paragraph without adding a line break. */
    const std::vector<RichTextBlock> previous_blocks = doc.blocks;
    std::vector<RichTextBlock> next_blocks;
    for (const auto &[new_start, new_length] : paragraph_spans(next_text)) {
        size_t source_pos = 0;
        if (new_start < prefix) {
            source_pos = new_start;
        } else if (new_start >= prefix + inserted_len) {
            source_pos = new_start - inserted_len + removed_len;
        } else {
            /* Newly inserted paragraphs inherit the paragraph at the edit
             * point. At an exact paragraph boundary this intentionally selects
             * that paragraph rather than the preceding one. */
            source_pos = prefix;
        }
        source_pos = std::min(source_pos, old.size());
        RichTextBlock block;
        block.start = new_start;
        block.length = new_length;
        block.format = paragraph_format_for_start(previous_blocks, source_pos,
                                                  doc.default_paragraph_format,
                                                  &block.mask);
        next_blocks.push_back(block);
    }

    const RichTextSelection after_selection{prefix + inserted_len, prefix + inserted_len};
    if (insertion_format) {
        doc.typing_format = *insertion_format;
        doc.typing_format_mask = insertion_mask;
        doc.has_typing_format = true;
    }

    doc.plain_text = next_text;
    doc.ranges = std::move(next_ranges);
    doc.blocks = std::move(next_blocks);
    doc.selection = after_selection;

    RichTextTransaction transaction;
    transaction.type = "replace_text";
    transaction.position = prefix;
    transaction.removed_text = old.substr(prefix, removed_len);
    transaction.inserted_text = inserted_text;
    transaction.before_selection = before_selection;
    transaction.after_selection = after_selection;
    doc.transactions.push_back(std::move(transaction));
    if (doc.transactions.size() > 100)
        doc.transactions.erase(doc.transactions.begin(), doc.transactions.begin() + (doc.transactions.size() - 100));

    doc.normalize();
}

static void rich_text_document_replace_text_impl(
    RichTextDocument &doc, const std::string &next_text,
    const RichTextCharFormat *insertion_format, uint32_t insertion_mask,
    bool input_is_canonical)
{
    if (!input_is_canonical)
        doc.normalize();
    const std::string old = doc.plain_text;
    if (old == next_text) {
        doc.normalize();
        return;
    }

    /* Diff on complete UTF-8 codepoints. A byte-prefix diff can stop in the
     * middle of Greek, emoji or combining text and corrupt both ranges and
     * transaction strings. */
    size_t prefix = 0;
    while (prefix < old.size() && prefix < next_text.size()) {
        const size_t old_end = utf8_codepoint_end(old, prefix);
        const size_t next_end = utf8_codepoint_end(next_text, prefix);
        const size_t old_len = old_end - prefix;
        const size_t next_len = next_end - prefix;
        if (old_len != next_len ||
            old.compare(prefix, old_len, next_text, prefix, next_len) != 0)
            break;
        prefix = old_end;
    }

    size_t old_suffix = old.size();
    size_t new_suffix = next_text.size();
    while (old_suffix > prefix && new_suffix > prefix) {
        const size_t old_start = utf8_codepoint_start_before(old, old_suffix);
        const size_t new_start = utf8_codepoint_start_before(next_text, new_suffix);
        const size_t old_len = old_suffix - old_start;
        const size_t new_len = new_suffix - new_start;
        if (old_len != new_len ||
            old.compare(old_start, old_len, next_text, new_start, new_len) != 0)
            break;
        old_suffix = old_start;
        new_suffix = new_start;
    }

    rich_text_document_replace_range_impl(
        doc, prefix, old_suffix - prefix,
        next_text.substr(prefix, new_suffix - prefix), insertion_format,
        insertion_mask, true);
}

void rich_text_document_replace_text(RichTextDocument &doc, const std::string &next_text)
{
    rich_text_document_replace_text_impl(doc, next_text, nullptr, 0,
                                         false);
}

void rich_text_document_replace_text(RichTextDocument &doc, const std::string &next_text,
                                     const RichTextCharFormat &insertion_format,
                                     uint32_t insertion_mask)
{
    rich_text_document_replace_text_impl(doc, next_text, &insertion_format,
                                         insertion_mask, false);
}

void rich_text_document_replace_text_canonical(
    RichTextDocument &doc, const std::string &next_text,
    const RichTextCharFormat &insertion_format, uint32_t insertion_mask)
{
    rich_text_document_replace_text_impl(doc, next_text, &insertion_format,
                                         insertion_mask, true);
}

void rich_text_document_replace_range(RichTextDocument &doc, size_t byte_position,
                                      size_t removed_byte_length,
                                      const std::string &inserted_text,
                                      const RichTextCharFormat &insertion_format,
                                      uint32_t insertion_mask)
{
    rich_text_document_replace_range_impl(
        doc, byte_position, removed_byte_length, inserted_text,
        &insertion_format, insertion_mask, false);
}

void rich_text_document_replace_canonical_range(
    RichTextDocument &doc, size_t byte_position, size_t removed_byte_length,
    const std::string &inserted_text,
    const RichTextCharFormat &insertion_format, uint32_t insertion_mask)
{
    rich_text_document_replace_range_impl(
        doc, byte_position, removed_byte_length, inserted_text,
        &insertion_format, insertion_mask, true);
}


static void apply_rich_text_evaluated_defaults(
    RichTextDocument &document, const RichTextEvaluatedDefaults &defaults)
{
    document.default_format.font_size = std::max(1, defaults.font_size);
    document.default_format.tracking = defaults.tracking;
    document.default_format.scale_x = defaults.scale_x;
    document.default_format.scale_y = defaults.scale_y;
    document.default_format.baseline_shift = defaults.baseline_shift;
    if (document.default_format.fill.type == 0)
        document.default_format.fill.color = defaults.solid_fill_color;
    document.default_paragraph_format.align_h = defaults.align_h;
    document.default_paragraph_format.align_v = defaults.align_v;
    document.default_paragraph_format.indent_left = defaults.indent_left;
    document.default_paragraph_format.indent_right = defaults.indent_right;
    document.default_paragraph_format.indent_first_line =
        defaults.indent_first_line;
    document.default_paragraph_format.line_spacing = defaults.line_spacing;
    document.default_paragraph_format.space_before = defaults.space_before;
    document.default_paragraph_format.space_after = defaults.space_after;
    document.default_paragraph_format.hyphenate = defaults.hyphenate;
}

RichTextDocument rich_text_document_with_evaluated_defaults_canonical(
    RichTextDocument document, const RichTextEvaluatedDefaults &defaults)
{
    apply_rich_text_evaluated_defaults(document, defaults);
    return document;
}

RichTextDocument rich_text_document_with_evaluated_defaults(
    RichTextDocument document, const RichTextEvaluatedDefaults &defaults)
{
    document.normalize();
    return rich_text_document_with_evaluated_defaults_canonical(
        std::move(document), defaults);
}

#ifndef OBS_FXM_RICH_TEXT_STANDALONE_TEST
void rich_text_document_ensure_canonical(Layer &layer)
{
    if (layer.type != LayerType::Text && layer.type != LayerType::Ticker && layer.type != LayerType::Clock)
        return;

    if (layer.rich_text.empty()) {
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
    }
    /* Once a canonical document exists it owns defaults and text content.
     * Scalar Layer fields are mirrors only; importing them back here created an
     * accidental two-way sync and allowed cursor formatting to overwrite the
     * document-wide style. */

    /* No legacy rich text HTML path: the canonical model is plain_text +
     * manual ranges + automatic rules. This avoids QTextDocument HTML
     * round-tripping, stale spans, and mismatched Properties/Canvas state. */
    layer.rich_text.normalize();
    layer.text_content = layer.rich_text.plain_text;
    rich_text_document_sync_layer_mirrors_canonical(layer);
}
#endif

static bool is_line_break_at(const std::string &text, size_t pos, size_t *advance = nullptr)
{
    if (pos >= text.size()) return false;
    if (text[pos] == '\r') {
        if (advance) *advance = (pos + 1 < text.size() && text[pos + 1] == '\n') ? 2 : 1;
        return true;
    }
    if (text[pos] == '\n') {
        if (advance) *advance = 1;
        return true;
    }
    return false;
}

static size_t utf8_codepoint_advance(const std::string &s, size_t pos)
{
    if (pos >= s.size()) return 0;
    const unsigned char c = static_cast<unsigned char>(s[pos]);
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return (pos + 1 < s.size()) ? 2 : 1;
    if ((c & 0xF0) == 0xE0) return (pos + 2 < s.size()) ? 3 : 1;
    if ((c & 0xF8) == 0xF0) return (pos + 3 < s.size()) ? 4 : 1;
    return 1;
}


static size_t utf8_byte_offset_for_codepoint_index(const std::string &text, size_t index)
{
    size_t pos = 0;
    size_t cp = 0;
    while (pos < text.size() && cp < index) {
        pos += std::max<size_t>(1, utf8_codepoint_advance(text, pos));
        ++cp;
    }
    return std::min(pos, text.size());
}



static size_t find_nth_marker(const std::string &text, const std::string &marker, size_t occurrence, const std::string &custom_chars);

static size_t marker_advance_at(const std::string &text, const std::string &marker, size_t pos, const std::string &custom_chars)
{
    if (pos > text.size()) return 0;
    if (marker == "text_start" || marker == "text_end" || marker == "character_index" ||
        marker == "paragraph_start" || marker == "paragraph_end")
        return 0;
    if (marker == "space")
        return (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ? 1 : 0;
    if (marker == "line_break" || marker == "newline") {
        size_t adv = 0;
        return is_line_break_at(text, pos, &adv) ? std::max<size_t>(1, adv) : 0;
    }
    if (marker == "custom_char") {
        if (custom_chars.empty() || pos >= text.size()) return 0;
        for (size_t cpos = 0; cpos < custom_chars.size();) {
            const size_t adv = std::max<size_t>(1, utf8_codepoint_advance(custom_chars, cpos));
            if (cpos + adv <= custom_chars.size() && pos + adv <= text.size() &&
                text.compare(pos, adv, custom_chars, cpos, adv) == 0)
                return adv;
            cpos += adv;
        }
    }
    return 0;
}

static std::vector<size_t> marker_positions(const std::string &text, const std::string &marker,
                                            const std::string &custom_chars)
{
    const size_t text_len = text.size();
    std::vector<size_t> positions;
    if (marker == "text_start") {
        positions.push_back(0);
        return positions;
    }
    if (marker == "text_end") {
        positions.push_back(text_len);
        return positions;
    }
    if (marker == "paragraph_start") {
        positions.push_back(0);
        for (size_t i = 0; i < text_len; ++i) {
            size_t adv = 0;
            if (is_line_break_at(text, i, &adv)) {
                positions.push_back(std::min(text_len, i + adv));
                i += adv ? adv - 1 : 0;
            }
        }
        return positions;
    }
    if (marker == "paragraph_end") {
        for (size_t i = 0; i < text_len; ++i) {
            size_t adv = 0;
            if (is_line_break_at(text, i, &adv)) {
                positions.push_back(i);
                i += adv ? adv - 1 : 0;
            }
        }
        positions.push_back(text_len);
        return positions;
    }
    if (marker == "character_index" || marker == "character_count" || marker == "word_count")
        return positions;

    for (size_t i = 0; i < text_len; ++i) {
        const size_t adv = marker_advance_at(text, marker, i, custom_chars);
        if (adv == 0)
            continue;
        positions.push_back(i);
        i += adv - 1;
    }
    return positions;
}

struct AutoStyleMarkerHit {
    size_t pos = 0;
    size_t advance = 0;
    bool found = false;
};

static bool is_word_byte(unsigned char ch)
{
    return std::isalnum(ch) || ch == '_' || ch >= 0x80;
}

static AutoStyleMarkerHit byte_offset_after_word_count(const std::string &text, size_t start_pos, size_t word_count)
{
    const size_t text_len = text.size();
    size_t pos = std::min(start_pos, text_len);
    size_t seen = 0;
    while (pos < text_len) {
        while (pos < text_len && !is_word_byte((unsigned char)text[pos]))
            ++pos;
        if (pos >= text_len)
            break;
        while (pos < text_len && is_word_byte((unsigned char)text[pos]))
            ++pos;
        ++seen;
        if (seen >= word_count)
            return {pos, 0, true};
    }
    return {text_len, 0, word_count == 0};
}

static AutoStyleMarkerHit byte_offset_after_character_count(const std::string &text, size_t start_pos, size_t count)
{
    const size_t text_len = text.size();
    size_t pos = std::min(start_pos, text_len);
    size_t seen = 0;
    while (pos < text_len && seen < count) {
        pos += std::max<size_t>(1, utf8_codepoint_advance(text, pos));
        ++seen;
    }
    return {std::min(pos, text_len), 0, seen >= count};
}

static size_t find_nth_marker(const std::string &text, const std::string &marker, size_t occurrence, const std::string &custom_chars)
{
    if (marker == "character_index" || marker == "character_count")
        return utf8_byte_offset_for_codepoint_index(text, occurrence);
    if (marker == "word_count")
        return byte_offset_after_word_count(text, 0, occurrence).pos;
    const auto positions = marker_positions(text, marker, custom_chars);
    if (positions.empty())
        return text.size();
    return positions[std::min(occurrence, positions.size() - 1)];
}

static AutoStyleMarkerHit find_next_marker_after(const std::string &text, const std::string &marker,
                                                 size_t start_pos, size_t occurrence_skip,
                                                 const std::string &custom_chars)
{
    const size_t text_len = text.size();
    if (marker == "text_end") return {text_len, 0, true};
    if (marker == "text_start") return {start_pos, 0, true};
    if (marker == "character_index")
        return {utf8_byte_offset_for_codepoint_index(text, occurrence_skip), 0, true};
    if (marker == "character_count")
        return byte_offset_after_character_count(text, start_pos, occurrence_skip);
    if (marker == "word_count")
        return byte_offset_after_word_count(text, start_pos, occurrence_skip);

    const auto positions = marker_positions(text, marker, custom_chars);
    size_t seen = 0;
    for (const size_t pos : positions) {
        if (pos <= start_pos)
            continue;
        if (seen++ == occurrence_skip)
            return {std::min(pos, text_len), marker_advance_at(text, marker, pos, custom_chars), true};
    }
    return {text_len, 0, false};
}

static std::vector<std::pair<size_t, size_t>> regex_auto_style_ranges(
    const RichTextAutoStyleRule &rule, const std::string &text)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    if (rule.regex_pattern.empty())
        return ranges;
    try {
        const auto expression = fxm::text::cached_pattern(
            rule.regex_pattern, rule.regex_case_sensitive);
        if (!expression)
            return ranges;
        for (std::sregex_iterator it(text.begin(), text.end(), *expression), end; it != end; ++it) {
            const std::smatch &match = *it;
            const size_t group = std::min(rule.regex_capture_group, match.size() ? match.size() - 1 : size_t{0});
            if (!match[group].matched)
                continue;
            const size_t start = static_cast<size_t>(match.position(group));
            const size_t length = static_cast<size_t>(match.length(group));
            if (length > 0)
                ranges.push_back({start, start + length});
            if (rule.match_mode == "first_match")
                break;
        }
    } catch (const std::regex_error &) {
        /* Invalid user patterns fail closed and never affect rendering. */
    } catch (const std::exception &) {
        /* Allocation/iterator failures must not escape into Qt's event loop. */
    } catch (...) {
        /* Fail closed for any platform-specific regex implementation failure. */
    }
    return ranges;
}

static std::vector<std::pair<size_t, size_t>> auto_style_rule_ranges(const RichTextAutoStyleRule &rule, const std::string &text)
{
    fxm::perf::add(fxm::perf::Counter::FormattingRuleEvaluations);
    const size_t text_len = text.size();
    std::vector<std::pair<size_t, size_t>> ranges;

    if (rule.condition_type == "regex")
        return regex_auto_style_ranges(rule, text);

    /* Backwards compatibility for files/rules from the original start_to_char UI. */
    if (rule.condition_type == "start_to_char") {
        const size_t start = utf8_byte_offset_for_codepoint_index(text, rule.start);
        const size_t end = byte_offset_after_character_count(text, start, rule.length).pos;
        if (end > start) ranges.push_back({start, end});
        return ranges;
    }

    const std::string start_marker = rule.start_condition.empty() ? "text_start" : rule.start_condition;
    const std::string end_marker = rule.end_condition.empty() ? "text_end" : rule.end_condition;
    const bool start_is_custom = start_marker == "custom_char";
    const bool end_is_custom = end_marker == "custom_char";

    /* Absolute index ranges remain single ranges. They are explicit by design,
     * so strict stop matching is not needed for character_index end markers. */
    if (start_marker == "character_index" || end_marker == "character_index" ||
        start_marker == "character_count" || end_marker == "character_count" ||
        start_marker == "word_count" || end_marker == "word_count") {
        size_t start = find_nth_marker(text, start_marker, rule.start_offset, rule.start_custom_chars);
        size_t end = find_nth_marker(text, end_marker, rule.end_offset, rule.end_custom_chars);
        if (start_is_custom && !rule.include_start_marker)
            start = std::min(text_len, start + marker_advance_at(text, start_marker, start, rule.start_custom_chars));
        if (end_is_custom && rule.include_end_marker)
            end = std::min(text_len, end + marker_advance_at(text, end_marker, end, rule.end_custom_chars));
        if (end < start) std::swap(start, end);
        start = std::min(start, text_len);
        end = std::min(end, text_len);
        if (end > start) ranges.push_back({start, end});
        return ranges;
    }

    const auto starts = marker_positions(text, start_marker, rule.start_custom_chars);
    if (starts.empty())
        return ranges;

    size_t skipped_starts = 0;
    for (const size_t raw_start : starts) {
        if (skipped_starts++ < rule.start_offset)
            continue;
        size_t start = std::min(raw_start, text_len);
        if (start_is_custom && !rule.include_start_marker)
            start = std::min(text_len, start + marker_advance_at(text, start_marker, raw_start, rule.start_custom_chars));

        AutoStyleMarkerHit hit = find_next_marker_after(text, end_marker, start, rule.end_offset, rule.end_custom_chars);
        if (rule.require_stop_match && !hit.found)
            continue;
        size_t end = hit.pos;
        if (end_is_custom && rule.include_end_marker)
            end = std::min(text_len, end + hit.advance);
        end = std::min(end, text_len);
        if (end > start)
            ranges.push_back({start, end});
        if (start_marker == "text_start")
            break;
    }
    if (rule.match_mode == "first_match" && ranges.size() > 1)
        ranges.resize(1);
    return ranges;
}

static void merge_auto_format_bits(RichTextCharFormat &target, const RichTextCharFormat &source, uint32_t mask)
{
    rich_text_merge_char_format(target, source, mask);
}

RichTextDocument rich_text_document_with_auto_styles_canonical(
    RichTextDocument out, const RichTextAutoStyleResolver &resolver)
{
    if (!out.auto_style_enabled)
        return out;

    if (!out.auto_default_style_preset_id.empty()) {
        RichTextCharFormat fmt = out.auto_default_style_cached_format;
        uint32_t mask = out.auto_default_style_cached_mask;
        if (resolver) {
            RichTextCharFormat resolved;
            uint32_t resolved_mask = 0;
            if (resolver(out.auto_default_style_preset_id, resolved,
                         resolved_mask)) {
                fmt = resolved;
                mask = resolved_mask;
            }
        }
        if (mask != 0)
            merge_auto_format_bits(out.default_format, fmt, mask);
    }

    const size_t text_len = out.plain_text.size();
    if (text_len == 0) {
        out.ranges.clear();
        return out;
    }

    /* Auto-style state used to be allocated once per UTF-8 byte. That wastes
     * 2-4 entries for every Greek/CJK/emoji code point and made rule exclusion
     * matrices particularly expensive. Operate on canonical codepoint spans
     * while keeping all public ranges in UTF-8 byte offsets. */
    std::vector<size_t> boundaries;
    boundaries.reserve(text_len + 1);
    boundaries.push_back(0);
    for (size_t pos = 0; pos < text_len;) {
        pos += std::max<size_t>(1, utf8_codepoint_advance(out.plain_text, pos));
        boundaries.push_back(std::min(pos, text_len));
    }
    if (boundaries.back() != text_len)
        boundaries.push_back(text_len);
    const size_t unit_count = boundaries.size() - 1;
    auto boundary_unit = [&](size_t byte_offset) {
        byte_offset = std::min(byte_offset, text_len);
        const auto it = std::lower_bound(boundaries.begin(), boundaries.end(),
                                         byte_offset);
        return static_cast<size_t>(it - boundaries.begin());
    };

    std::vector<RichTextCharFormat> formats(unit_count, out.default_format);
    std::vector<uint32_t> claimed_masks(unit_count, 0);
    std::vector<bool> blocked_for_future_rules(unit_count, false);

    auto normalized_units = [&](size_t start, size_t length) {
        const size_t raw_start = std::min(start, text_len);
        const size_t raw_end = clamped_end(raw_start, length, text_len);
        const size_t begin = rich_text_utf8_previous_boundary(
            out.plain_text, raw_start);
        const size_t end = rich_text_utf8_next_boundary(out.plain_text,
                                                        raw_end);
        return std::pair<size_t, size_t>{boundary_unit(begin),
                                         boundary_unit(end)};
    };

    auto merge_masked_range = [&](size_t start, size_t length,
                                  const RichTextCharFormat &format,
                                  uint32_t mask,
                                  const std::string &mode) {
        const auto [first, last] = normalized_units(start, length);
        for (size_t unit = first; unit < last && unit < unit_count; ++unit) {
            if (blocked_for_future_rules[unit])
                continue;
            uint32_t effective_mask = mask;
            if (mode == "respect_previous")
                effective_mask &= ~claimed_masks[unit];
            else if (mode == "apply_if_empty" && claimed_masks[unit] != 0)
                effective_mask = 0;
            if (effective_mask == 0)
                continue;
            merge_auto_format_bits(formats[unit], format, effective_mask);
            claimed_masks[unit] |= effective_mask;
        }
    };

    auto mark_blocked = [&](size_t start, size_t length) {
        const auto [first, last] = normalized_units(start, length);
        for (size_t unit = first; unit < last && unit < unit_count; ++unit)
            blocked_for_future_rules[unit] = true;
    };

    std::vector<std::vector<std::pair<size_t, size_t>>>
        resolved_rule_ranges(out.auto_style_rules.size());
    for (size_t ri = 0; ri < out.auto_style_rules.size(); ++ri)
        resolved_rule_ranges[ri] =
            auto_style_rule_ranges(out.auto_style_rules[ri], out.plain_text);

    std::vector<std::vector<bool>> excluded_by_rule(
        out.auto_style_rules.size(), std::vector<bool>(unit_count, false));
    for (size_t ri = 0; ri < out.auto_style_rules.size(); ++ri) {
        const auto &rule = out.auto_style_rules[ri];
        if (!rule.enabled || (rule.style_preset_id.empty() && rule.cached_mask == 0))
            continue;
        RichTextCharFormat fmt = rule.cached_format;
        uint32_t mask = rule.cached_mask;
        if (resolver) {
            RichTextCharFormat resolved;
            uint32_t resolved_mask = 0;
            if (resolver(rule.style_preset_id, resolved, resolved_mask)) {
                fmt = resolved;
                mask = resolved_mask;
            }
        }
        if (mask == 0)
            continue;

        for (const auto &range : resolved_rule_ranges[ri]) {
            if (range.second <= range.first)
                continue;
            const auto [first, last] = normalized_units(
                range.first, range.second - range.first);
            size_t segment = first;
            while (segment < last && segment < unit_count) {
                while (segment < last && excluded_by_rule[ri][segment])
                    ++segment;
                size_t segment_end = segment;
                while (segment_end < last &&
                       !excluded_by_rule[ri][segment_end])
                    ++segment_end;
                if (segment_end > segment) {
                    merge_masked_range(
                        boundaries[segment],
                        boundaries[segment_end] - boundaries[segment], fmt,
                        mask, rule.conflict_mode);
                }
                segment = segment_end;
            }
        }

        if (rule.conflict_mode == "exclude_other_rules" &&
            !rule.excludes_rule_ids.empty()) {
            for (size_t other = 0; other < out.auto_style_rules.size();
                 ++other) {
                const std::string &other_id =
                    out.auto_style_rules[other].rule_id;
                const std::string other_index_id = std::to_string(other + 1);
                const bool should_exclude =
                    std::find(rule.excludes_rule_ids.begin(),
                              rule.excludes_rule_ids.end(), other_id) !=
                        rule.excludes_rule_ids.end() ||
                    std::find(rule.excludes_rule_ids.begin(),
                              rule.excludes_rule_ids.end(), other_index_id) !=
                        rule.excludes_rule_ids.end();
                if (!should_exclude)
                    continue;
                for (const auto &range : resolved_rule_ranges[ri]) {
                    if (range.second <= range.first)
                        continue;
                    const auto [first, last] = normalized_units(
                        range.first, range.second - range.first);
                    for (size_t unit = first;
                         unit < last && unit < unit_count; ++unit)
                        excluded_by_rule[other][unit] = true;
                }
            }
        }
        if (rule.stop_processing) {
            for (const auto &range : resolved_rule_ranges[ri])
                mark_blocked(range.first,
                             range.second > range.first
                                 ? range.second - range.first
                                 : 0);
        }
    }

    /* Manual inline styles remain authoritative over automatic rules. */
    for (const auto &range : out.ranges) {
        if (range.length == 0 || range.start >= text_len)
            continue;
        const auto [first, last] = normalized_units(range.start, range.length);
        for (size_t unit = first; unit < last && unit < unit_count; ++unit)
            rich_text_merge_char_format(formats[unit], range.format,
                                        range.mask);
    }

    out.ranges.clear();
    size_t start_unit = 0;
    while (start_unit < unit_count) {
        size_t end_unit = start_unit + 1;
        while (end_unit < unit_count &&
               same_format(formats[end_unit], formats[start_unit]))
            ++end_unit;
        const uint32_t mask = rich_text_char_format_difference_mask(
            formats[start_unit], out.default_format);
        if (mask != 0) {
            out.ranges.push_back(
                {boundaries[start_unit],
                 boundaries[end_unit] - boundaries[start_unit],
                 formats[start_unit], mask});
        }
        start_unit = end_unit;
    }
    return out;
}

static std::string regex_escape_literal(const std::string &text)
{
    static const std::string special = R"(\.^$|()[]{}*+?)";
    std::string escaped;
    escaped.reserve(text.size() * 2);
    for (const char ch : text) {
        if (special.find(ch) != std::string::npos)
            escaped.push_back('\\');
        escaped.push_back(ch);
    }
    return escaped;
}

static uint32_t utf8_codepoint_at(const std::string &text, size_t pos, size_t *advance)
{
    if (advance)
        *advance = 0;
    if (pos >= text.size())
        return 0;
    const auto b0 = static_cast<unsigned char>(text[pos]);
    if (b0 < 0x80) {
        if (advance) *advance = 1;
        return b0;
    }
    size_t count = 1;
    uint32_t value = 0;
    if ((b0 & 0xE0) == 0xC0) { count = 2; value = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { count = 3; value = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { count = 4; value = b0 & 0x07; }
    else {
        if (advance) *advance = 1;
        return b0;
    }
    if (pos + count > text.size()) {
        if (advance) *advance = 1;
        return b0;
    }
    for (size_t i = 1; i < count; ++i) {
        const auto bx = static_cast<unsigned char>(text[pos + i]);
        if ((bx & 0xC0) != 0x80) {
            if (advance) *advance = 1;
            return b0;
        }
        value = (value << 6) | (bx & 0x3F);
    }
    if (advance) *advance = count;
    return value;
}

static bool is_unicode_invisible_or_space(uint32_t cp)
{
    if (cp <= 0x20 || cp == 0x7F || cp == 0x85 || cp == 0xA0 || cp == 0xAD)
        return true;
    if (cp >= 0x2000 && cp <= 0x200F)
        return true;
    return cp == 0x1680 || cp == 0x2028 || cp == 0x2029 || cp == 0x202F ||
           cp == 0x205F || cp == 0x2060 || cp == 0x3000 || cp == 0xFEFF;
}

static bool is_ascii_special(uint32_t cp)
{
    return cp < 0x80 && !std::isalnum(static_cast<unsigned char>(cp)) && cp != '_';
}

static std::string regex_escape_structural_literal(const std::string &text)
{
    std::string escaped;
    escaped.reserve(text.size() * 4);
    for (size_t pos = 0; pos < text.size();) {
        size_t advance = 0;
        const uint32_t cp = utf8_codepoint_at(text, pos, &advance);
        if (advance == 0)
            break;
        switch (cp) {
        case '\r': escaped += "\\r"; break;
        case '\n': escaped += "\\n"; break;
        case '\t': escaped += "\\t"; break;
        case '\f': escaped += "\\f"; break;
        case '\v': escaped += "\\v"; break;
        case 0x20: escaped += "\\x20"; break;
        default:
            if (cp < 0x20 || cp == 0x7F) {
                static constexpr char hex[] = "0123456789ABCDEF";
                escaped += "\\x";
                escaped.push_back(hex[(cp >> 4) & 0xF]);
                escaped.push_back(hex[cp & 0xF]);
            } else {
                escaped += regex_escape_literal(text.substr(pos, advance));
            }
            break;
        }
        pos += advance;
    }
    return escaped;
}

static bool is_line_boundary_codepoint(uint32_t cp)
{
    return cp == '\n' || cp == '\r' || cp == 0x0B || cp == 0x0C ||
           cp == 0x85 || cp == 0x2028 || cp == 0x2029;
}

static bool starts_at_structural_line_boundary(const std::string &text, size_t pos)
{
    if (pos == 0)
        return true;
    const size_t previous = utf8_codepoint_start_before(text, pos);
    size_t advance = 0;
    return is_line_boundary_codepoint(utf8_codepoint_at(text, previous, &advance));
}

static std::string learned_line_start_pattern()
{
    /* CRLF is kept as one logical boundary. U+0085, U+2028 and U+2029 cover
     * next-line, Unicode line separator and Unicode paragraph separator. */
    return "(^|\\r\\n|[\\n\\r\\v\\f]|\xC2\x85|\xE2\x80\xA8|\xE2\x80\xA9)";
}

static std::string structural_separator_after(const std::string &text, size_t pos)
{
    std::string separator;
    size_t cursor = pos;
    while (cursor < text.size()) {
        size_t advance = 0;
        const uint32_t cp = utf8_codepoint_at(text, cursor, &advance);
        if (advance == 0)
            break;
        if (!is_unicode_invisible_or_space(cp) && !is_ascii_special(cp))
            break;
        /* A newline after punctuation/horizontal spacing belongs to the next
         * structural field, not to this separator. A newline directly after
         * the formatted run is itself the separator. */
        if (is_line_boundary_codepoint(cp) && !separator.empty())
            break;
        separator.append(text, cursor, advance);
        cursor += advance;
        /* A line/paragraph separator is already an unambiguous boundary. */
        if (is_line_boundary_codepoint(cp)) {
            if (cp == '\r' && cursor < text.size() && text[cursor] == '\n') {
                separator.push_back('\n');
            }
            break;
        }
        /* Keep a useful punctuation token plus its following invisible spacing,
         * but do not absorb the next unrelated punctuation field. */
        if (separator.size() >= 16)
            break;
    }
    return separator;
}

static bool ascii_all_digits_or_number(const std::string &sample)
{
    if (sample.empty()) return false;
    bool digit = false;
    for (unsigned char ch : sample) {
        if (std::isdigit(ch)) { digit = true; continue; }
        if (ch == '.' || ch == ',' || ch == '+' || ch == '-' || ch == '%' || ch == ' ')
            continue;
        return false;
    }
    return digit;
}

static bool ascii_looks_like_time(const std::string &sample)
{
    try { return std::regex_match(sample, std::regex(R"(^[0-2]?[0-9]:[0-5][0-9](?::[0-5][0-9])?$)")); }
    catch (...) { return false; }
}

static bool ascii_looks_like_date(const std::string &sample)
{
    try { return std::regex_match(sample, std::regex(R"(^[0-3]?[0-9][./-][0-1]?[0-9][./-](?:[0-9]{2}|[0-9]{4})$)")); }
    catch (...) { return false; }
}

static bool ascii_looks_like_email(const std::string &sample)
{
    const auto at = sample.find('@');
    return at != std::string::npos && at > 0 && sample.find('.', at) != std::string::npos;
}

static bool ascii_looks_like_url(const std::string &sample)
{
    return sample.rfind("http://", 0) == 0 || sample.rfind("https://", 0) == 0 ||
           sample.rfind("www.", 0) == 0;
}

static std::string generic_pattern_for_sample(const std::string &sample, std::string *description)
{
    if (ascii_looks_like_time(sample)) {
        if (description) *description = "Time";
        return R"([0-2]?[0-9]:[0-5][0-9](?::[0-5][0-9])?)";
    }
    if (ascii_looks_like_date(sample)) {
        if (description) *description = "Date";
        return R"([0-3]?[0-9][./-][0-1]?[0-9][./-](?:[0-9]{2}|[0-9]{4}))";
    }
    if (ascii_looks_like_email(sample)) {
        if (description) *description = "Email address";
        return R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})";
    }
    if (ascii_looks_like_url(sample)) {
        if (description) *description = "Web address";
        return R"((?:https?://|www\.)[^\s\r\n]+)";
    }
    if (ascii_all_digits_or_number(sample)) {
        if (description) *description = "Number";
        return R"([+\-]?[0-9]+(?:[.,][0-9]+)?%?)";
    }
    return {};
}

static std::string visible_separator_name(const std::string &separator)
{
    if (separator == "\n" || separator == "\r" || separator == "\r\n") return "line break";
    if (separator == " ") return "space";
    if (separator == "\t") return "tab";
    std::string visible;
    for (unsigned char ch : separator) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
        visible.push_back(static_cast<char>(ch));
    }
    return visible.empty() ? "separator" : visible;
}

static size_t line_start_before(const std::string &text, size_t pos)
{
    while (pos > 0) {
        const size_t previous = utf8_codepoint_start_before(text, pos);
        size_t advance = 0;
        if (is_line_boundary_codepoint(utf8_codepoint_at(text, previous, &advance)))
            break;
        pos = previous;
    }
    return pos;
}

static std::string structural_separator_before(const std::string &text, size_t pos)
{
    if (pos == 0) return {};
    const size_t start = line_start_before(text, pos);
    size_t cursor = pos;
    while (cursor > start) {
        const size_t previous = utf8_codepoint_start_before(text, cursor);
        size_t advance = 0;
        const uint32_t cp = utf8_codepoint_at(text, previous, &advance);
        if (!is_unicode_invisible_or_space(cp) && !is_ascii_special(cp)) break;
        cursor = previous;
        if (pos - cursor >= 16) break;
    }
    return text.substr(cursor, pos - cursor);
}

static std::string learned_style_fingerprint(const RichTextAutoStyleRule &rule)
{
    const auto &f = rule.cached_format;
    std::ostringstream out;
    out << rule.cached_mask << '|' << rule.style_preset_id << '|'
        << f.font_family << '|' << f.font_style << '|' << f.font_size << '|'
        << f.bold << f.italic << f.underline << f.strikethrough << '|'
        << f.tracking << '|' << f.scale_x << '|' << f.scale_y << '|'
        << f.baseline_shift << '|' << f.fill.type << '|' << f.fill.color << '|'
        << f.stroke.enabled << '|' << f.stroke.width << '|' << f.stroke.opacity << '|'
        << f.stroke.fill.type << '|' << f.stroke.fill.color;
    return out.str();
}

bool rich_text_auto_style_rules_equivalent(const RichTextAutoStyleRule &a,
                                           const RichTextAutoStyleRule &b)
{
    return a.condition_type == b.condition_type &&
           a.regex_pattern == b.regex_pattern &&
           a.regex_capture_group == b.regex_capture_group &&
           a.regex_case_sensitive == b.regex_case_sensitive &&
           a.start_condition == b.start_condition &&
           a.end_condition == b.end_condition &&
           a.start_offset == b.start_offset && a.end_offset == b.end_offset &&
           a.start_custom_chars == b.start_custom_chars &&
           a.end_custom_chars == b.end_custom_chars &&
           learned_style_fingerprint(a) == learned_style_fingerprint(b);
}

size_t rich_text_merge_auto_style_rules(std::vector<RichTextAutoStyleRule> &destination,
                                        const std::vector<RichTextAutoStyleRule> &incoming)
{
    size_t added = 0;
    for (auto candidate : incoming) {
        const bool deduplicate = candidate.prevent_duplicates &&
            candidate.generalization_mode != "keep_separate";
        auto duplicate = destination.end();
        if (deduplicate) {
            duplicate = std::find_if(destination.begin(), destination.end(),
                [&](const RichTextAutoStyleRule &existing) {
                    return rich_text_auto_style_rules_equivalent(existing, candidate);
                });
        }
        if (duplicate != destination.end()) {
            /* One equivalent rule already covers every occurrence. Preserve the
             * user's existing identity/order and promote it to reusable matching. */
            if (candidate.allow_multiple_cases) duplicate->match_mode = "all_matches";
            duplicate->allow_multiple_cases = duplicate->allow_multiple_cases || candidate.allow_multiple_cases;
            continue;
        }
        destination.push_back(std::move(candidate));
        ++added;
    }
    return added;
}

std::vector<RichTextAutoStyleRule> rich_text_infer_auto_style_rules(
    const RichTextDocument &source)
{
    RichTextDocument doc = source;
    doc.normalize();
    std::vector<RichTextAutoStyleRule> rules;
    size_t index = 1;
    for (const RichTextRange &range : doc.ranges) {
        if (range.length == 0 || range.start >= doc.plain_text.size() || range.mask == 0)
            continue;
        const size_t end = clamped_end(range.start, range.length, doc.plain_text.size());
        if (end <= range.start)
            continue;
        const size_t sample_length = end - range.start;
        static constexpr size_t kMaxLearnedSampleBytes = 1024 * 1024;
        if (sample_length > kMaxLearnedSampleBytes)
            continue;
        const std::string sample = doc.plain_text.substr(range.start, sample_length);
        if (sample.empty()) continue;

        RichTextAutoStyleRule rule;
        rule.rule_id = "learned_rule_" + std::to_string(index++);
        rule.condition_type = "regex";
        rule.match_mode = "all_matches";
        rule.conflict_mode = "override_previous";
        /* The inferred rule owns a complete style snapshot. This keeps learned
         * formatting portable even when no named style preset exists. */
        rule.cached_format = range.format;
        rule.cached_mask = range.mask;
        rule.style_preset_id.clear();

        const bool line_start = starts_at_structural_line_boundary(doc.plain_text, range.start);
        const std::string after = structural_separator_after(doc.plain_text, end);
        const std::string before = structural_separator_before(doc.plain_text, range.start);
        std::string semantic_name;
        const std::string semantic_pattern = generic_pattern_for_sample(sample, &semantic_name);

        if (line_start && !after.empty()) {
            const std::string escaped_separator = regex_escape_structural_literal(after);
            const std::string line_boundary =
                "(?:\\r\\n|[\\n\\r\\v\\f]|\xC2\x85|\xE2\x80\xA8|\xE2\x80\xA9)";
            const std::string content = semantic_pattern.empty()
                ? "(?:(?!" + line_boundary + ").)+?"
                : semantic_pattern;
            rule.regex_pattern = learned_line_start_pattern() + "(" + content + ")(?=" + escaped_separator + ")";
            rule.regex_capture_group = 2;
            rule.display_name = "Paragraph beginning to " + visible_separator_name(after);
        } else if (!before.empty() && !after.empty()) {
            const std::string escaped_before = regex_escape_structural_literal(before);
            const std::string escaped_after = regex_escape_structural_literal(after);
            const std::string content = semantic_pattern.empty() ? "[^\\r\\n]+?" : semantic_pattern;
            rule.regex_pattern = "(?:" + escaped_before + ")(" + content + ")(?=" + escaped_after + ")";
            rule.regex_capture_group = 1;
            rule.display_name = "Between " + visible_separator_name(before) + " and " + visible_separator_name(after);
        } else if (!semantic_pattern.empty()) {
            rule.regex_pattern = semantic_pattern;
            rule.regex_capture_group = 0;
            rule.display_name = semantic_name;
        } else if (!before.empty()) {
            const std::string escaped_before = regex_escape_structural_literal(before);
            rule.regex_pattern = "(?:" + escaped_before + ")([^\\r\\n]+)";
            rule.regex_capture_group = 1;
            rule.display_name = "After " + visible_separator_name(before) + " to line end";
        } else {
            rule.regex_pattern = regex_escape_structural_literal(sample);
            rule.regex_capture_group = 0;
            rule.match_mode = "first_match";
            rule.display_name = "Exact text: " + sample.substr(0, std::min<size_t>(sample.size(), 32));
        }
        rule.generalization_mode = "auto_merge";
        rule.prevent_duplicates = true;
        rule.allow_multiple_cases = true;
        rule.match_mode = "all_matches";
        rich_text_merge_auto_style_rules(rules, {rule});
    }
    return rules;
}

RichTextDocument rich_text_document_with_auto_styles(
    const RichTextDocument &doc, const RichTextAutoStyleResolver &resolver)
{
    RichTextDocument out = doc;
    out.normalize();
    return rich_text_document_with_auto_styles_canonical(
        std::move(out), resolver);
}
