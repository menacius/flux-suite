#include "title-rich-text.h"

#include <cassert>
#include <iostream>

static RichTextDocument text_doc(const std::string &text)
{
    RichTextDocument doc;
    doc.plain_text = text;
    doc.default_format.font_family = "Inter";
    doc.default_format.font_size = 40;
    doc.default_format.fill.color = 0xFFFFFFFF;
    doc.normalize();
    return doc;
}

static void test_mixed_styles_and_transactions()
{
    RichTextDocument doc = text_doc("HelloWorld");
    RichTextCharFormat red = doc.default_format;
    red.fill.color = 0xFFFF0000;
    red.font_size = 30;
    RichTextCharFormat blue = doc.default_format;
    blue.fill.color = 0xFF0000FF;
    blue.font_size = 72;
    blue.text_style = 1;
    blue.ligatures = false;
    blue.kerning_mode = 2;
    blue.manual_kerning = 12.0f;
    doc.ranges = {
        {0, 5, red, rich_text_char_format_difference_mask(red, doc.default_format)},
        {5, 5, blue, rich_text_char_format_difference_mask(blue, doc.default_format)},
    };
    doc.normalize();
    assert(doc.version == 2);
    assert(doc.ranges.size() == 2);
    assert(doc.ranges[1].format.text_style == 1);
    assert(!doc.ranges[1].format.ligatures);
    assert(doc.ranges[1].format.kerning_mode == 2);
    assert((doc.ranges[0].mask & RichTextCharFontSize) != 0);
    assert((doc.ranges[0].mask & RichTextCharFillColor) != 0);
    assert((doc.ranges[0].mask & RichTextCharFontFamily) == 0);

    rich_text_document_replace_text(doc, "Hello Big World");
    assert(doc.plain_text == "Hello Big World");
    assert(doc.transactions.size() == 1);
    assert(doc.transactions.back().position == 5);
    assert(doc.transactions.back().inserted_text == " Big ");
    assert(doc.ranges.size() == 2);
    assert(doc.ranges[0].start == 0 && doc.ranges[0].length == 5);
    assert(doc.ranges[1].start == 10 && doc.ranges[1].length == 5);

    RichTextCharFormat gradient = doc.default_format;
    gradient.fill.type = 1;
    gradient.fill.gradient_start_color = 0xFFFFAA00;
    gradient.fill.gradient_end_color = 0xFF0033FF;
    gradient.fill.gradient_start_opacity = 0.75f;
    gradient.fill.gradient_end_opacity = 0.5f;
    gradient.fill.gradient_opacity = 0.8f;
    gradient.fill.gradient_center_x = 0.25f;
    gradient.fill.gradient_center_y = 0.75f;
    gradient.fill.gradient_scale = 1.5f;
    gradient.fill.gradient_focal_x = 0.2f;
    gradient.fill.gradient_focal_y = 0.8f;
    gradient.fill.gradient_stops = {
        {0xFF00FF00, 0.35f, 0.9f},
        {0xFFFF00FF, 0.7f, 0.4f},
    };
    rich_text_document_replace_text(doc, "Hello Big Wide World", gradient,
                                    RichTextCharFillColor);
    bool has_gradient = false;
    for (const auto &range : doc.ranges) {
        has_gradient = has_gradient || (range.format.fill.type == 1);
        if (range.format.fill.type == 1) {
            assert((range.mask & RichTextCharFillColor) != 0);
            assert(range.format.fill.gradient_start_opacity == 0.75f);
            assert(range.format.fill.gradient_end_opacity == 0.5f);
            assert(range.format.fill.gradient_opacity == 0.8f);
            assert(range.format.fill.gradient_center_x == 0.25f);
            assert(range.format.fill.gradient_center_y == 0.75f);
            assert(range.format.fill.gradient_scale == 1.5f);
            assert(range.format.fill.gradient_focal_x == 0.2f);
            assert(range.format.fill.gradient_focal_y == 0.8f);
            assert(range.format.fill.gradient_stops == gradient.fill.gradient_stops);
        }
    }
    assert(has_gradient);

    RichTextDocument undo_snapshot = doc;
    rich_text_document_replace_text(doc, "Hello World");
    doc.normalize();
    for (const auto &range : doc.ranges)
        assert(range.start + range.length <= doc.plain_text.size());

    RichTextDocument redo_snapshot = doc;
    doc = undo_snapshot;
    assert(doc.plain_text == "Hello Big Wide World");
    doc = redo_snapshot;
    assert(doc.plain_text == "Hello World");

    RichTextDocument reloaded = doc;
    reloaded.normalize();
    assert(reloaded.plain_text == doc.plain_text);
    assert(reloaded.ranges.size() == doc.ranges.size());
}

static void test_sparse_overrides_follow_defaults()
{
    RichTextDocument doc = text_doc("Alpha");
    RichTextCharFormat local = doc.default_format;
    local.bold = true;
    local.fill.color = 0xFF00FF00;
    doc.ranges = {{0, doc.plain_text.size(), local,
                   rich_text_char_format_difference_mask(local, doc.default_format)}};
    doc.normalize();
    assert(doc.ranges.size() == 1);
    assert(doc.ranges[0].mask == (RichTextCharBold | RichTextCharFillColor));

    doc.default_format.font_family = "Noto Sans";
    doc.default_format.tracking = 14.0f;
    doc.normalize();
    const RichTextCharFormat effective = rich_text_format_at(doc, 0);
    assert(effective.font_family == "Noto Sans");
    assert(effective.tracking == 14.0f);
    assert(effective.bold);
    assert(effective.fill.color == 0xFF00FF00);

    RichTextCharFormat resized = effective;
    resized.font_size = 96;
    rich_text_document_apply_format(doc, 1, 2, resized, RichTextCharFontSize);
    assert(rich_text_format_at(doc, 0).font_size == 40);
    assert(rich_text_format_at(doc, 1).font_size == 96);
    assert(rich_text_format_at(doc, 3).font_size == 40);
}

static void test_overlapping_sparse_properties_compose_per_text_range()
{
    RichTextDocument doc = text_doc("ABCD");

    RichTextCharFormat horizontal = doc.default_format;
    horizontal.scale_x = 0.70f;
    rich_text_document_apply_format(
        doc, 0, 4, horizontal, RichTextCharScaleX);

    RichTextCharFormat vertical = doc.default_format;
    vertical.scale_y = 4.13f;
    rich_text_document_apply_format(
        doc, 1, 2, vertical, RichTextCharScaleY);

    RichTextCharFormat fill = doc.default_format;
    fill.fill.color = 0xFFFF0066;
    rich_text_document_apply_format(
        doc, 2, 2, fill, RichTextCharFillColor);

    RichTextCharFormat stroke = doc.default_format;
    stroke.stroke.enabled = true;
    stroke.stroke.width = 3.0f;
    stroke.stroke.fill.color = 0xFF00FFCC;
    rich_text_document_apply_format(
        doc, 1, 3, stroke, RichTextCharStroke);

    doc.normalize();
    const RichTextCharFormat a = rich_text_format_at(doc, 0);
    const RichTextCharFormat b = rich_text_format_at(doc, 1);
    const RichTextCharFormat c = rich_text_format_at(doc, 2);
    const RichTextCharFormat d = rich_text_format_at(doc, 3);

    assert(a.scale_x == 0.70f);
    assert(a.scale_y == 1.0f);
    assert(!a.stroke.enabled);

    assert(b.scale_x == 0.70f);
    assert(b.scale_y == 4.13f);
    assert(b.stroke.enabled && b.stroke.width == 3.0f);
    assert(b.fill.color == doc.default_format.fill.color);

    assert(c.scale_x == 0.70f);
    assert(c.scale_y == 4.13f);
    assert(c.fill.color == 0xFFFF0066);
    assert(c.stroke.enabled && c.stroke.fill.color == 0xFF00FFCC);

    assert(d.scale_x == 0.70f);
    assert(d.scale_y == 1.0f);
    assert(d.fill.color == 0xFFFF0066);
    assert(d.stroke.enabled);

    const uint32_t b_mask = rich_text_format_mask_at(doc, 1);
    assert((b_mask & RichTextCharScaleX) != 0);
    assert((b_mask & RichTextCharScaleY) != 0);
    assert((b_mask & RichTextCharStroke) != 0);
    const uint32_t c_mask = rich_text_format_mask_at(doc, 2);
    assert((c_mask & RichTextCharScaleX) != 0);
    assert((c_mask & RichTextCharScaleY) != 0);
    assert((c_mask & RichTextCharFillColor) != 0);
    assert((c_mask & RichTextCharStroke) != 0);
}

static void test_unicode_boundaries_and_safe_transactions()
{
    RichTextDocument doc = text_doc(u8"AΩ🙂B");
    RichTextCharFormat bold = doc.default_format;
    bold.bold = true;

    /* Offset 2 is inside Ω (bytes 1..2). normalize() must expand it to a
     * complete codepoint instead of leaving an invalid range boundary. */
    doc.ranges = {{2, 1, bold, RichTextCharBold}};
    doc.selection = {5, 6}; /* both positions are inside the emoji bytes */
    doc.normalize();
    assert(doc.ranges.size() == 1);
    assert(doc.ranges[0].start == 1);
    assert(doc.ranges[0].length == 2);
    assert(rich_text_utf8_is_boundary(doc.plain_text, doc.ranges[0].start));
    assert(rich_text_utf8_is_boundary(doc.plain_text, doc.ranges[0].start + doc.ranges[0].length));
    assert(doc.selection.anchor == 3);
    assert(doc.selection.head == 3);

    doc.transactions.clear();
    rich_text_document_replace_text(doc, u8"AΨ🙂B");
    assert(doc.transactions.size() == 1);
    const RichTextTransaction &transaction = doc.transactions.back();
    assert(transaction.position == 1);
    assert(transaction.removed_text == u8"Ω");
    assert(transaction.inserted_text == u8"Ψ");
    assert(rich_text_utf8_is_boundary(doc.plain_text, transaction.position));
    assert(rich_text_utf8_is_boundary(doc.plain_text,
                                      transaction.position + transaction.inserted_text.size()));
}

static void test_paragraph_blocks_survive_normalization_and_edits()
{
    RichTextDocument doc = text_doc("One\nTwo\nThree");
    assert(doc.blocks.size() == 3);
    doc.blocks[1].format.indent_left = 42.0f;
    doc.blocks[1].format.space_before = 7.0f;
    doc.blocks[1].mask = RichTextParagraphIndentLeft | RichTextParagraphSpaceBefore;
    doc.normalize();
    assert(doc.blocks.size() == 3);
    assert(doc.blocks[1].format.indent_left == 42.0f);
    assert(doc.blocks[1].format.space_before == 7.0f);

    rich_text_document_replace_text(doc, "One\nNew\nTwo\nThree");
    assert(doc.blocks.size() == 4);
    assert(doc.blocks[2].start == 8);
    assert(doc.blocks[2].format.indent_left == 42.0f);
    assert(doc.blocks[2].format.space_before == 7.0f);

    doc.default_paragraph_format.indent_right = 25.0f;
    doc.normalize();
    const RichTextParagraphFormat inherited = rich_text_paragraph_format_at(doc, doc.blocks[2].start);
    assert(inherited.indent_left == 42.0f);
    assert(inherited.indent_right == 25.0f);
    assert((doc.blocks[2].mask & RichTextParagraphIndentRight) == 0);

    RichTextDocument boundary = text_doc("One\nTwo");
    boundary.blocks[1].format.indent_left = 42.0f;
    boundary.blocks[1].mask = RichTextParagraphIndentLeft;
    boundary.normalize();
    rich_text_document_replace_text(boundary, "One\nXTwo");
    assert(boundary.blocks.size() == 2);
    assert(boundary.blocks[1].start == 4);
    assert(boundary.blocks[1].format.indent_left == 42.0f);
    assert(boundary.blocks[1].mask == RichTextParagraphIndentLeft);

    rich_text_document_replace_text(boundary, "XTwo");
    assert(boundary.blocks.size() == 1);
    assert(boundary.blocks[0].format.indent_left == 42.0f);
    assert(boundary.blocks[0].mask == RichTextParagraphIndentLeft);
}

static void test_explicit_masks_survive_matching_defaults()
{
    RichTextDocument doc = text_doc("Mask");
    RichTextCharFormat local = doc.default_format;
    local.bold = true;
    doc.ranges = {{0, doc.plain_text.size(), local, RichTextCharBold}};
    doc.default_format.bold = true;
    doc.normalize();
    assert(doc.ranges.size() == 1);
    assert(doc.ranges[0].mask == RichTextCharBold);
    doc.default_format.bold = false;
    doc.normalize();
    assert(rich_text_format_at(doc, 0).bold);

    doc.blocks[0].format.space_after = 10.0f;
    doc.blocks[0].mask = RichTextParagraphSpaceAfter;
    doc.default_paragraph_format.space_after = 10.0f;
    doc.normalize();
    assert(doc.blocks[0].mask == RichTextParagraphSpaceAfter);
    doc.default_paragraph_format.space_after = 0.0f;
    doc.normalize();
    assert(rich_text_paragraph_format_at(doc, 0).space_after == 10.0f);
}


static void test_explicit_insertion_mask_survives_default_change()
{
    RichTextDocument doc = text_doc("A");
    RichTextCharFormat insertion = doc.default_format;
    insertion.bold = false;
    rich_text_document_replace_text(doc, "AB", insertion, RichTextCharBold);
    assert(doc.ranges.size() == 1);
    assert(doc.ranges[0].start == 1);
    assert(doc.ranges[0].length == 1);
    assert(doc.ranges[0].mask == RichTextCharBold);

    doc.default_format.bold = true;
    doc.normalize();
    assert(rich_text_format_at(doc, 0).bold);
    assert(!rich_text_format_at(doc, 1).bold);
}


static void test_sparse_typing_and_unicode_auto_style()
{
    RichTextDocument doc = text_doc(u8"ΑΒΓ");
    doc.typing_format = doc.default_format;
    doc.typing_format.bold = true;
    doc.typing_format_mask = RichTextCharBold;
    doc.has_typing_format = true;
    doc.default_format.tracking = 22.0f;
    doc.normalize();
    const RichTextCharFormat typing = rich_text_effective_typing_format(doc);
    assert(typing.bold);
    assert(typing.tracking == 22.0f);
    assert(doc.typing_format_mask == RichTextCharBold);

    RichTextAutoStyleRule rule;
    rule.style_preset_id = "unicode-test";
    rule.condition_type = "start_to_char";
    rule.start = 1;
    rule.length = 1;
    rule.cached_format = doc.default_format;
    rule.cached_format.italic = true;
    rule.cached_mask = RichTextCharItalic;
    doc.auto_style_enabled = true;
    doc.auto_style_rules = {rule};
    RichTextDocument styled = rich_text_document_with_auto_styles(doc, {});
    assert(styled.ranges.size() == 1);
    assert(styled.ranges[0].start == 2);
    assert(styled.ranges[0].length == 2);
    assert(rich_text_utf8_is_boundary(styled.plain_text, styled.ranges[0].start));
    assert(rich_text_utf8_is_boundary(styled.plain_text,
                                      styled.ranges[0].start + styled.ranges[0].length));
    assert(rich_text_format_at(styled, 2).italic);
}


static void test_independent_font_scale_metrics()
{
    RichTextFontScaleMetrics horizontal = rich_text_font_scale_metrics(2.0f, 1.0f);
    assert(horizontal.vertical_factor == 1.0f);
    assert(horizontal.horizontal_stretch_percent == 200);

    RichTextFontScaleMetrics vertical = rich_text_font_scale_metrics(1.0f, 2.0f);
    assert(vertical.vertical_factor == 2.0f);
    assert(vertical.horizontal_stretch_percent == 100);

    RichTextFontScaleMetrics uniform = rich_text_font_scale_metrics(2.0f, 2.0f);
    assert(uniform.vertical_factor == 2.0f);
    assert(uniform.horizontal_stretch_percent == 200);
}

static void test_object_level_property_replaces_only_matching_overrides()
{
    RichTextDocument doc = text_doc("RedBold");
    RichTextCharFormat local = doc.default_format;
    local.bold = true;
    local.fill.color = 0xFFFF0000;
    local.scale_x = 1.75f;
    doc.ranges = {{0, doc.plain_text.size(), local,
                   RichTextCharBold | RichTextCharFillColor | RichTextCharScaleX}};
    doc.normalize();

    doc.default_format.scale_x = 1.25f;
    rich_text_document_clear_char_format_mask(doc, RichTextCharScaleX);
    RichTextCharFormat effective = rich_text_format_at(doc, 0);
    assert(effective.scale_x == 1.25f);
    assert(effective.bold);
    assert(effective.fill.color == 0xFFFF0000);
    assert((doc.ranges[0].mask & RichTextCharScaleX) == 0);
    assert((doc.ranges[0].mask & RichTextCharBold) != 0);
    assert((doc.ranges[0].mask & RichTextCharFillColor) != 0);
}

static void test_canonical_default_mutation_preserves_unrelated_range_properties()
{
    RichTextDocument doc = text_doc("ABCD");
    RichTextCharFormat local = doc.default_format;
    local.bold = true;
    local.fill.color = 0xFFFF0066;
    local.scale_x = 1.75f;
    local.scale_y = 2.25f;
    doc.ranges = {{0, doc.plain_text.size(), local,
                   RichTextCharBold | RichTextCharFillColor |
                       RichTextCharScaleX | RichTextCharScaleY}};

    RichTextCharFormat object_scale = doc.default_format;
    object_scale.scale_x = 0.70f;
    rich_text_document_apply_default_char_format(
        doc, object_scale, RichTextCharScaleX, true);

    const RichTextCharFormat effective = rich_text_format_at(doc, 1);
    assert(effective.scale_x == 0.70f);
    assert(effective.scale_y == 2.25f);
    assert(effective.bold);
    assert(effective.fill.color == 0xFFFF0066);
    assert(doc.ranges.size() == 1);
    assert((doc.ranges[0].mask & RichTextCharScaleX) == 0);
    assert((doc.ranges[0].mask & RichTextCharScaleY) != 0);
    assert((doc.ranges[0].mask & RichTextCharBold) != 0);
    assert((doc.ranges[0].mask & RichTextCharFillColor) != 0);

    doc.default_paragraph_format.indent_left = 5.0f;
    doc.default_paragraph_format.space_after = 2.0f;
    doc.normalize();
    doc.blocks[0].format.indent_left = 30.0f;
    doc.blocks[0].format.space_after = 12.0f;
    doc.blocks[0].mask = RichTextParagraphIndentLeft |
                         RichTextParagraphSpaceAfter;
    doc.normalize();

    RichTextParagraphFormat object_paragraph =
        doc.default_paragraph_format;
    object_paragraph.indent_left = 18.0f;
    rich_text_document_apply_default_paragraph_format(
        doc, object_paragraph, RichTextParagraphIndentLeft, true);
    const RichTextParagraphFormat paragraph =
        rich_text_paragraph_format_at(doc, 0);
    assert(paragraph.indent_left == 18.0f);
    assert(paragraph.space_after == 12.0f);
    assert((doc.blocks[0].mask & RichTextParagraphIndentLeft) == 0);
    assert((doc.blocks[0].mask & RichTextParagraphSpaceAfter) != 0);
}

static void test_paragraph_selection_formatting()
{
    RichTextDocument doc = text_doc(u8"Ένα\nΔύο\nΤρία");
    RichTextParagraphFormat format = doc.default_paragraph_format;
    format.indent_left = 31.0f;

    const size_t second_start = doc.plain_text.find(u8"Δύο");
    rich_text_document_apply_paragraph_format(doc, second_start, second_start,
                                              format, RichTextParagraphIndentLeft);
    assert(rich_text_paragraph_format_at(doc, 0).indent_left == 0.0f);
    assert(rich_text_paragraph_format_at(doc, second_start).indent_left == 31.0f);
    const size_t third_start = doc.plain_text.find(u8"Τρία");
    assert(rich_text_paragraph_format_at(doc, third_start).indent_left == 0.0f);

    format.space_after = 9.0f;
    rich_text_document_apply_paragraph_format(doc, 0, third_start, format,
                                              RichTextParagraphSpaceAfter);
    assert(rich_text_paragraph_format_at(doc, 0).space_after == 9.0f);
    assert(rich_text_paragraph_format_at(doc, second_start).space_after == 9.0f);
    assert(rich_text_paragraph_format_at(doc, third_start).space_after == 0.0f);

    doc.default_paragraph_format.indent_left = 12.0f;
    rich_text_document_clear_paragraph_format_mask(doc, RichTextParagraphIndentLeft);
    assert(rich_text_paragraph_format_at(doc, second_start).indent_left == 12.0f);
    assert(rich_text_paragraph_format_at(doc, second_start).space_after == 9.0f);
}

static void test_multiple_styles_and_colors_remain_independent()
{
    RichTextDocument doc = text_doc("One Two Three");
    RichTextCharFormat red = doc.default_format;
    red.fill.color = 0xFFFF0000;
    RichTextCharFormat blue_bold = doc.default_format;
    blue_bold.fill.color = 0xFF0000FF;
    blue_bold.bold = true;
    doc.ranges = {
        {0, 3, red, RichTextCharFillColor},
        {4, 3, blue_bold, RichTextCharFillColor | RichTextCharBold},
    };
    doc.normalize();

    RichTextCharFormat scaled = rich_text_format_at(doc, 4);
    scaled.scale_y = 1.8f;
    rich_text_document_apply_format(doc, 4, 3, scaled, RichTextCharScaleY);
    assert(rich_text_format_at(doc, 0).fill.color == 0xFFFF0000);
    assert(rich_text_format_at(doc, 4).fill.color == 0xFF0000FF);
    assert(rich_text_format_at(doc, 4).bold);
    assert(rich_text_format_at(doc, 4).scale_y == 1.8f);
    assert(rich_text_format_at(doc, 8).scale_y == 1.0f);
}

static void test_mixed_property_difference_masks_are_precise()
{
    RichTextCharFormat base;
    RichTextCharFormat changed = base;
    changed.tracking = 12.0f;
    assert(rich_text_char_format_difference_mask(base, changed) ==
           RichTextCharTracking);

    changed = base;
    changed.scale_x = 1.5f;
    assert(rich_text_char_format_difference_mask(base, changed) ==
           RichTextCharScaleX);

    changed = base;
    changed.scale_y = 0.75f;
    assert(rich_text_char_format_difference_mask(base, changed) ==
           RichTextCharScaleY);

    changed = base;
    changed.fill.gradient_angle = 45.0f;
    assert(rich_text_char_format_difference_mask(base, changed) ==
           RichTextCharFillColor);

    RichTextParagraphFormat paragraph_base;
    RichTextParagraphFormat paragraph_changed = paragraph_base;
    paragraph_changed.space_after = 18.0f;
    assert(rich_text_paragraph_format_difference_mask(
               paragraph_base, paragraph_changed) == RichTextParagraphSpaceAfter);
}

static void test_selected_swatch_fill_changes_only_selected_range()
{
    RichTextDocument doc;
    doc.plain_text = "red blue green";
    doc.default_format.fill.type = 0;
    doc.default_format.fill.color = 0xFFFFFFFF;
    doc.normalize();

    const size_t start = 4;
    const size_t length = 4;
    RichTextCharFormat selected = rich_text_format_at(doc, start);
    selected.fill.type = 0;
    selected.fill.color = 0xFF3366CC;
    rich_text_document_apply_format(
        doc, start, length, selected, RichTextCharFillColor);

    assert(rich_text_format_at(doc, 0).fill.color == 0xFFFFFFFF);
    assert(rich_text_format_at(doc, start).fill.color == 0xFF3366CC);
    assert(rich_text_format_at(doc, start + length).fill.color == 0xFFFFFFFF);
    assert(rich_text_format_mask_at(doc, start) & RichTextCharFillColor);
}


static void test_selected_inline_stroke_changes_only_selected_range()
{
    RichTextDocument doc = text_doc("one two three");
    const size_t start = 4;
    const size_t length = 3;
    RichTextCharFormat selected = rich_text_format_at(doc, start);
    selected.stroke.enabled = true;
    selected.stroke.width = 6.0f;
    selected.stroke.fill.type = 0;
    selected.stroke.fill.color = 0xFFFFAA00;
    rich_text_document_apply_format(
        doc, start, length, selected, RichTextCharStroke);

    assert(!rich_text_format_at(doc, 0).stroke.enabled);
    const RichTextCharFormat inside = rich_text_format_at(doc, start);
    assert(inside.stroke.enabled);
    assert(inside.stroke.width == 6.0f);
    assert(inside.stroke.fill.color == 0xFFFFAA00);
    assert(!rich_text_format_at(doc, start + length).stroke.enabled);
    assert((rich_text_format_mask_at(doc, start) & RichTextCharStroke) != 0);
    assert(rich_text_char_format_difference_mask(
               doc.default_format, inside, RichTextCharStroke) ==
           RichTextCharStroke);
}

static void test_utf16_mapping_and_canonical_single_edit()
{
    const std::string text = u8"AΩ🙂B";
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 0) == 0);
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 1) == 1);
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 2) == 3);
    /* Qt can transiently report a cursor inside a surrogate pair. Snap it to
     * the beginning of the scalar instead of producing an invalid UTF-8 edit. */
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 3) == 3);
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 4) == 7);
    assert(rich_text_utf8_byte_offset_from_utf16_position(text, 5) == 8);

    RichTextDocument doc = text_doc(text);
    RichTextCharFormat replacement = doc.default_format;
    replacement.bold = true;
    rich_text_document_replace_canonical_range(
        doc, 3, 4, u8"Ψ", replacement, RichTextCharBold);
    assert(doc.plain_text == u8"AΩΨB");
    assert(doc.transactions.size() == 1);
    assert(doc.transactions.back().position == 3);
    assert(doc.transactions.back().removed_text == u8"🙂");
    assert(doc.transactions.back().inserted_text == u8"Ψ");
    assert(rich_text_format_at(doc, 3).bold);
    assert(!rich_text_format_at(doc, 1).bold);
}

static void test_every_exposed_text_property_participates_in_model_masks()
{
    RichTextCharFormat base;
    RichTextCharFormat changed = base;
    changed.font_family = "Noto Sans";
    changed.font_style = "Semibold";
    changed.font_size += 7;
    changed.bold = !base.bold;
    changed.italic = !base.italic;
    changed.underline = !base.underline;
    changed.strikethrough = !base.strikethrough;
    changed.kerning = !base.kerning;
    changed.kerning_mode = 2;
    changed.manual_kerning = 3.5f;
    changed.tracking = 4.0f;
    changed.scale_x = 1.25f;
    changed.scale_y = 0.8f;
    changed.baseline_shift = 6.0f;
    changed.fill.type = 1;
    changed.fill.gradient_type = 1;
    changed.fill.gradient_start_color = 0xFF112233;
    changed.fill.gradient_end_color = 0xFF445566;
    changed.text_style = 1;
    changed.ligatures = !base.ligatures;
    changed.stylistic_alternates = !base.stylistic_alternates;
    changed.fractions = !base.fractions;
    changed.opentype_features = !base.opentype_features;
    changed.language = "el";
    changed.stroke.enabled = true;
    changed.stroke.width = 5.0f;
    changed.stroke.join_style = 1;
    assert(rich_text_char_format_difference_mask(base, changed) ==
           RichTextCharAll);

    RichTextParagraphFormat paragraph;
    RichTextParagraphFormat paragraph_changed = paragraph;
    paragraph_changed.align_h = 2;
    paragraph_changed.align_v = 2;
    paragraph_changed.indent_left = 1.0f;
    paragraph_changed.indent_right = 2.0f;
    paragraph_changed.indent_first_line = 3.0f;
    paragraph_changed.line_spacing = 4.0f;
    paragraph_changed.space_before = 5.0f;
    paragraph_changed.space_after = 6.0f;
    paragraph_changed.hyphenate = !paragraph.hyphenate;
    assert(rich_text_paragraph_format_difference_mask(
               paragraph, paragraph_changed) == RichTextParagraphAll);
}


static void test_learned_regex_auto_style_from_manual_prefix()
{
    RichTextDocument doc;
    doc.plain_text = u8"ΓΙΑΝΝΗΣ: Πρώτη δήλωση\nΜΑΡΙΑ: Δεύτερη δήλωση";
    RichTextCharFormat speaker = doc.default_format;
    speaker.bold = true;
    speaker.fill.color = 0xFFFFCC00;
    const size_t first_name_bytes = std::string(u8"ΓΙΑΝΝΗΣ").size();
    doc.ranges.push_back({0, first_name_bytes, speaker,
                          RichTextCharBold | RichTextCharFillColor});
    doc.normalize();

    const auto learned = rich_text_infer_auto_style_rules(doc);
    assert(learned.size() == 1);
    assert(learned.front().condition_type == "regex");
    assert(learned.front().regex_capture_group == 2);
    assert(learned.front().cached_mask ==
           (RichTextCharBold | RichTextCharFillColor));

    doc.ranges.clear();
    doc.auto_style_enabled = true;
    doc.auto_style_rules = learned;
    RichTextDocument styled = rich_text_document_with_auto_styles(doc, {});
    assert(rich_text_format_at(styled, 0).bold);
    const size_t second_name = doc.plain_text.find(u8"ΜΑΡΙΑ");
    assert(second_name != std::string::npos);
    assert(rich_text_format_at(styled, second_name).bold);
    assert(!rich_text_format_at(styled, doc.plain_text.find(u8"Πρώτη")).bold);
}


static RichTextDocument learned_prefix_document(const std::string &text,
                                                 const std::string &formatted_prefix)
{
    RichTextDocument doc;
    doc.plain_text = text;
    RichTextCharFormat format = doc.default_format;
    format.bold = true;
    doc.ranges.push_back({0, formatted_prefix.size(), format, RichTextCharBold});
    doc.normalize();
    const auto learned = rich_text_infer_auto_style_rules(doc);
    assert(learned.size() == 1);
    doc.ranges.clear();
    doc.auto_style_enabled = true;
    doc.auto_style_rules = learned;
    return rich_text_document_with_auto_styles(doc, {});
}

static void test_learned_regex_recognizes_invisible_and_structural_characters()
{
    {
        const std::string text = "NAME\tVALUE\r\nOTHER\tSECOND";
        RichTextDocument styled = learned_prefix_document(text, "NAME");
        assert(rich_text_format_at(styled, 0).bold);
        const size_t other = text.find("OTHER");
        assert(other != std::string::npos);
        assert(rich_text_format_at(styled, other).bold);
        assert(!rich_text_format_at(styled, text.find("VALUE")).bold);
        assert(styled.auto_style_rules.front().regex_pattern.find("\\t") != std::string::npos);
    }
    {
        const std::string text = std::string("NAME") + "\xC2\xA0" + "VALUE\nOTHER" + "\xC2\xA0" + "SECOND";
        RichTextDocument styled = learned_prefix_document(text, "NAME");
        const size_t other = text.find("OTHER");
        assert(other != std::string::npos);
        assert(rich_text_format_at(styled, other).bold);
        assert(!rich_text_format_at(styled, text.find("VALUE")).bold);
    }
    {
        const std::string separator = "\xE2\x80\xA8"; // U+2028 LINE SEPARATOR
        const std::string text = std::string("FIRST:") + separator + "SECOND:" + separator + "THIRD:";
        RichTextDocument doc;
        doc.plain_text = text;
        RichTextCharFormat format = doc.default_format;
        format.italic = true;
        doc.ranges.push_back({0, std::string("FIRST").size(), format, RichTextCharItalic});
        doc.normalize();
        const auto learned = rich_text_infer_auto_style_rules(doc);
        assert(learned.size() == 1);
        doc.ranges.clear();
        doc.auto_style_enabled = true;
        doc.auto_style_rules = learned;
        RichTextDocument styled = rich_text_document_with_auto_styles(doc, {});
        const size_t second = text.find("SECOND");
        const size_t third = text.find("THIRD");
        assert(second != std::string::npos && third != std::string::npos);
        assert(rich_text_format_at(styled, second).italic);
        assert(rich_text_format_at(styled, third).italic);
    }
    {
        const std::string text = "LABEL  VALUE\nOTHER  SECOND";
        RichTextDocument styled = learned_prefix_document(text, "LABEL");
        const size_t other = text.find("OTHER");
        assert(other != std::string::npos);
        assert(rich_text_format_at(styled, other).bold);
        assert(styled.auto_style_rules.front().regex_pattern.find("\\x20\\x20") != std::string::npos);
    }
}

static void test_anisotropic_scale_detection_across_multistyle_ranges()
{
    RichTextDocument uniform = text_doc("AB");
    uniform.default_format.scale_x = 4.13f;
    uniform.default_format.scale_y = 4.13f;
    uniform.normalize();
    assert(!rich_text_document_has_anisotropic_scale(uniform));

    RichTextDocument reported_case = text_doc(u8"Τίτλος");
    reported_case.default_format.scale_x = 0.70f;
    reported_case.default_format.scale_y = 4.13f;
    reported_case.normalize();
    assert(rich_text_document_has_anisotropic_scale(reported_case));

    RichTextDocument multistyle = text_doc("AB");
    multistyle.default_format.scale_x = 0.70f;
    multistyle.default_format.scale_y = 0.70f;
    RichTextCharFormat second = multistyle.default_format;
    second.scale_y = 4.13f;
    multistyle.ranges.push_back(
        {1, 1, second, RichTextCharScaleY});
    multistyle.normalize();
    assert(rich_text_document_has_anisotropic_scale(multistyle));

    RichTextDocument empty;
    empty.default_format.scale_x = 0.70f;
    empty.default_format.scale_y = 4.13f;
    assert(!rich_text_document_has_anisotropic_scale(empty));
}

int main()
{
    test_anisotropic_scale_detection_across_multistyle_ranges();
    test_mixed_styles_and_transactions();
    test_sparse_overrides_follow_defaults();
    test_overlapping_sparse_properties_compose_per_text_range();
    test_unicode_boundaries_and_safe_transactions();
    test_paragraph_blocks_survive_normalization_and_edits();
    test_explicit_masks_survive_matching_defaults();
    test_explicit_insertion_mask_survives_default_change();
    test_sparse_typing_and_unicode_auto_style();
    test_independent_font_scale_metrics();
    test_object_level_property_replaces_only_matching_overrides();
    test_canonical_default_mutation_preserves_unrelated_range_properties();
    test_paragraph_selection_formatting();
    test_multiple_styles_and_colors_remain_independent();
    test_mixed_property_difference_masks_are_precise();
    test_selected_swatch_fill_changes_only_selected_range();
    test_selected_inline_stroke_changes_only_selected_range();
    test_utf16_mapping_and_canonical_single_edit();
    test_every_exposed_text_property_participates_in_model_masks();
    test_learned_regex_auto_style_from_manual_prefix();
    test_learned_regex_recognizes_invisible_and_structural_characters();
    std::cout << "rich text v2 sparse ranges, independent H/V scale, multi-style colors, Unicode-safe edits, transactions, and paragraph selection passed\n";
    return 0;
}
