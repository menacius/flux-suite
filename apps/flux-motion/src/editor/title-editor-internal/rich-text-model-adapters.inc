static bool rich_text_fills_equal(const RichTextFill &a, const RichTextFill &b)
{
    return a.type == b.type && a.color == b.color && a.gradient_type == b.gradient_type &&
           a.gradient_spread == b.gradient_spread &&
           a.gradient_start_color == b.gradient_start_color &&
           a.gradient_end_color == b.gradient_end_color &&
           std::abs(a.gradient_start_pos - b.gradient_start_pos) < 0.0001f &&
           std::abs(a.gradient_end_pos - b.gradient_end_pos) < 0.0001f &&
           std::abs(a.gradient_start_opacity - b.gradient_start_opacity) < 0.0001f &&
           std::abs(a.gradient_end_opacity - b.gradient_end_opacity) < 0.0001f &&
           std::abs(a.gradient_opacity - b.gradient_opacity) < 0.0001f &&
           std::abs(a.gradient_angle - b.gradient_angle) < 0.0001f &&
           std::abs(a.gradient_center_x - b.gradient_center_x) < 0.0001f &&
           std::abs(a.gradient_center_y - b.gradient_center_y) < 0.0001f &&
           std::abs(a.gradient_scale - b.gradient_scale) < 0.0001f &&
           std::abs(a.gradient_focal_x - b.gradient_focal_x) < 0.0001f &&
           std::abs(a.gradient_focal_y - b.gradient_focal_y) < 0.0001f &&
           a.gradient_stops == b.gradient_stops;
}

static bool rich_text_char_formats_equal(const RichTextCharFormat &a, const RichTextCharFormat &b)
{
    return a.font_family == b.font_family && a.font_style == b.font_style &&
           a.font_size == b.font_size && a.bold == b.bold && a.italic == b.italic &&
           a.underline == b.underline && a.strikethrough == b.strikethrough &&
           a.kerning == b.kerning && a.kerning_mode == b.kerning_mode &&
           std::abs(a.manual_kerning - b.manual_kerning) < 0.0001f &&
           std::abs(a.tracking - b.tracking) < 0.0001f &&
           std::abs(a.scale_x - b.scale_x) < 0.0001f &&
           std::abs(a.scale_y - b.scale_y) < 0.0001f &&
           std::abs(a.baseline_shift - b.baseline_shift) < 0.0001f &&
           a.text_style == b.text_style && a.ligatures == b.ligatures &&
           a.stylistic_alternates == b.stylistic_alternates && a.fractions == b.fractions &&
           a.opentype_features == b.opentype_features && a.language == b.language &&
           rich_text_fills_equal(a.fill, b.fill) &&
           rich_text_char_format_difference_mask(a, b, RichTextCharStroke) == 0;
}

static bool rich_text_ranges_equal(const std::vector<RichTextRange> &a, const std::vector<RichTextRange> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const RichTextRange &x = a[i];
        const RichTextRange &y = b[i];
        if (x.start != y.start || x.length != y.length || x.mask != y.mask) return false;
        if (rich_text_char_format_difference_mask(x.format, y.format,
                                                  x.mask | y.mask) != 0)
            return false;
    }
    return true;
}

static bool rich_text_blocks_equal(const std::vector<RichTextBlock> &a,
                                   const std::vector<RichTextBlock> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const RichTextBlock &x = a[i];
        const RichTextBlock &y = b[i];
        if (x.start != y.start || x.length != y.length || x.mask != y.mask)
            return false;
        if (rich_text_paragraph_format_difference_mask(x.format, y.format,
                                                       x.mask | y.mask) != 0)
            return false;
    }
    return true;
}

static RichTextParagraphFormat layer_paragraph_format_for_editor(const Layer &layer)
{
    if (!layer.rich_text.empty())
        return layer.rich_text.default_paragraph_format;
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

static RichTextDocument rich_text_document_from_qtext_document(const QTextDocument *doc, const Layer &layer, double visual_scale, const QTextCursor &text_cursor = QTextCursor())
{
    RichTextDocument model = layer.rich_text.empty() ? rich_text_document_from_layer_defaults(layer) : layer.rich_text;
    if (!doc) return model;
    const QString qplain = doc->toPlainText();
    const EditorQtUtf8PositionMap positions(qplain);
    /* Canonical offsets are UTF-8 byte offsets. Keep the conversion explicit so
     * selection/range positions cannot depend on the Windows active code page
     * when complex Greek/emoji rich-text edits take the full adapter path. */
    model.plain_text = qplain.toUtf8().toStdString();
    model.default_paragraph_format = layer_paragraph_format_for_editor(layer);
    model.blocks.clear();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        RichTextBlock rich_block;
        rich_block.start = positions.byte_from_qpos(block.position());
        const int text_end_qpos = block.position() + std::max(0, block.length() - 1);
        const size_t text_end = positions.byte_from_qpos(text_end_qpos);
        rich_block.length = text_end > rich_block.start ? text_end - rich_block.start : 0;
        rich_block.format = rich_text_paragraph_from_qtext_block_format(
            block.blockFormat(), model.default_paragraph_format, visual_scale);
        rich_block.mask = block.blockFormat().hasProperty(RichTextParagraphPropManualMask)
            ? (uint32_t)block.blockFormat().property(RichTextParagraphPropManualMask).toUInt() & RichTextParagraphAll
            : rich_text_paragraph_format_difference_mask(rich_block.format,
                                                         model.default_paragraph_format);
        model.blocks.push_back(rich_block);
    }
    model.ranges.clear();
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().isEmpty()) continue;
            const QTextCharFormat fragment_format = fragment.charFormat();
            /* Automatic/evaluated formatting is display-only. It may be visible
             * in the transparent QTextEdit, but it must never be converted into
             * a persistent manual range during paste/IME/full-adapter sync. */
            if (fragment_format.property(RichTextPropAutoGenerated).toBool())
                continue;
            RichTextRange range;
            range.start = positions.byte_from_qpos(std::max(0, fragment.position()));
            const size_t end = positions.byte_from_qpos(std::max(0, fragment.position() + fragment.length()));
            range.length = end > range.start ? end - range.start : 0;
            range.format = rich_text_format_from_qtext_format(fragment_format, model.default_format, visual_scale);
            const uint32_t difference_mask = rich_text_char_format_difference_mask(
                range.format, model.default_format);
            if (fragment_format.hasProperty(RichTextPropManualMask)) {
                range.mask = (uint32_t)fragment_format
                                 .property(RichTextPropManualMask)
                                 .toUInt() & RichTextCharAll;
            } else if (fragment_format
                           .property(RichTextPropCanonicalAdapter)
                           .toBool()) {
                /* Inherited canonical adapter fragments can differ visually
                 * because of animation or automatic styling. No explicit mask
                 * means there is no authored override to persist. */
                range.mask = 0;
            } else {
                /* External rich paste has no FXM metadata; infer only there. */
                range.mask = difference_mask;
            }
            if (range.length > 0 && range.mask != 0)
                model.ranges.push_back(range);
        }
    }
    if (!text_cursor.isNull()) {
        model.selection.anchor = positions.byte_from_qpos(std::max(0, text_cursor.anchor()));
        model.selection.head = positions.byte_from_qpos(std::max(0, text_cursor.position()));
        if (!text_cursor.hasSelection()) {
            model.typing_format = rich_text_format_from_qtext_format(text_cursor.charFormat(), model.default_format, visual_scale);
            const QTextCharFormat cursor_char_format = text_cursor.charFormat();
            if (cursor_char_format.hasProperty(RichTextPropManualMask)) {
                model.typing_format_mask =
                    (uint32_t)cursor_char_format
                        .property(RichTextPropManualMask)
                        .toUInt() & RichTextCharAll;
            } else if (cursor_char_format
                           .property(RichTextPropCanonicalAdapter)
                           .toBool()) {
                model.typing_format_mask = 0;
            } else {
                model.typing_format_mask = rich_text_char_format_difference_mask(
                    model.typing_format, model.default_format);
            }
            model.has_typing_format = true;
        } else {
            model.has_typing_format = false;
            model.typing_format_mask = 0;
        }
    } else {
        model.selection.anchor = 0;
        model.selection.head = 0;
        model.has_typing_format = false;
        model.typing_format_mask = 0;
    }
    model.normalize();
    return model;
}



struct RichTextCharFormatSummary {
    RichTextCharFormat format;
    uint32_t mixed = 0;
    uint32_t kerning_mixed = 0;
    uint32_t fill_mixed = 0;
    bool valid = false;
};

enum RichTextKerningMixedMask : uint32_t {
    RichTextKerningEnabledMixed = 1u << 0,
    RichTextKerningModeMixed = 1u << 1,
    RichTextKerningValueMixed = 1u << 2,
};

enum RichTextFillMixedMask : uint32_t {
    RichTextFillTypeMixed = 1u << 0,
    RichTextFillColorMixed = 1u << 1,
    RichTextFillGradientTypeMixed = 1u << 2,
    RichTextFillGradientSpreadMixed = 1u << 3,
    RichTextFillGradientStartColorMixed = 1u << 4,
    RichTextFillGradientEndColorMixed = 1u << 5,
    RichTextFillGradientStartPosMixed = 1u << 6,
    RichTextFillGradientEndPosMixed = 1u << 7,
    RichTextFillGradientStartOpacityMixed = 1u << 8,
    RichTextFillGradientEndOpacityMixed = 1u << 9,
    RichTextFillGradientOpacityMixed = 1u << 10,
    RichTextFillGradientAngleMixed = 1u << 11,
    RichTextFillGradientCenterXMixed = 1u << 12,
    RichTextFillGradientCenterYMixed = 1u << 13,
    RichTextFillGradientScaleMixed = 1u << 14,
    RichTextFillGradientFocalXMixed = 1u << 15,
    RichTextFillGradientFocalYMixed = 1u << 16,
    RichTextFillGradientStopsMixed = 1u << 17,
};

struct RichTextParagraphFormatSummary {
    RichTextParagraphFormat format;
    uint32_t mixed = 0;
    bool valid = false;
};

/* Evaluated text/paragraph helpers are defined later in this header, but the
 * canonical editor-time document builder is used earlier. Keep the complete
 * declaration set next to the first consumer so every translation unit that
 * includes this header sees the same contract before use (MSVC does not allow
 * the previous use-before-declaration ordering). */
static double eval_text_font_size(const Layer &layer, double t);
static double eval_char_tracking(const Layer &layer, double t);
static double eval_char_scale_x(const Layer &layer, double t);
static double eval_char_scale_y(const Layer &layer, double t);
static double eval_baseline_shift(const Layer &layer, double t);
static double eval_paragraph_space_before(const Layer &layer, double t);
static double eval_paragraph_space_after(const Layer &layer, double t);
static double eval_paragraph_indent_left(const Layer &layer, double t);
static double eval_paragraph_indent_right(const Layer &layer, double t);
static double eval_paragraph_indent_first_line(const Layer &layer, double t);
static uint32_t eval_text_color(const Layer &layer, double t);
static RichTextStroke layer_stroke_for_editor(const Layer &layer);

/* Build the exact default model used by the current frame while preserving
 * sparse manual ranges. The transparent inline QTextEdit is only an input/
 * selection adapter; its glyph geometry must still match the evaluated canvas
 * output when font size, tracking, H/V scale, baseline or paragraph metrics are
 * animated. */
static RichTextDocument rich_text_document_for_editor_time(const Layer &layer,
                                                            double local_time)
{
    RichTextDocument canonical = rich_text_document_canonical_copy(layer);
    if (layer_has_external_binding(layer, ExternalPropertyPaths::TextContent)) {
        const std::string display_text = effective_external_string(
            layer, ExternalPropertyPaths::TextContent, layer.text_content);
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(canonical);
        rich_text_document_replace_text_canonical(
            canonical, display_text, insertion_format,
            canonical.has_typing_format ? canonical.typing_format_mask : 0);
    }
    RichTextEvaluatedDefaults defaults;
    defaults.font_size = std::max(
        1, static_cast<int>(std::round(eval_text_font_size(layer, local_time))));
    defaults.tracking =
        static_cast<float>(eval_char_tracking(layer, local_time));
    defaults.scale_x =
        static_cast<float>(eval_char_scale_x(layer, local_time));
    defaults.scale_y =
        static_cast<float>(eval_char_scale_y(layer, local_time));
    defaults.baseline_shift =
        static_cast<float>(eval_baseline_shift(layer, local_time));
    defaults.solid_fill_color = eval_text_color(layer, local_time);
    defaults.align_h = canonical.default_paragraph_format.align_h;
    defaults.align_v = canonical.default_paragraph_format.align_v;
    defaults.indent_left =
        static_cast<float>(eval_paragraph_indent_left(layer, local_time));
    defaults.indent_right =
        static_cast<float>(eval_paragraph_indent_right(layer, local_time));
    defaults.indent_first_line = static_cast<float>(
        eval_paragraph_indent_first_line(layer, local_time));
    defaults.line_spacing = canonical.default_paragraph_format.line_spacing;
    defaults.space_before =
        static_cast<float>(eval_paragraph_space_before(layer, local_time));
    defaults.space_after =
        static_cast<float>(eval_paragraph_space_after(layer, local_time));
    defaults.hyphenate = canonical.default_paragraph_format.hyphenate;
    RichTextDocument model =
        rich_text_document_with_evaluated_defaults_canonical(
            std::move(canonical), defaults);
    /* Character appearance, including default and inline stroke, comes only
     * from the canonical RichTextDocument. Layer stroke fields are synchronized
     * compatibility mirrors and are never merged back into this frame model. */
    return model;
}

static RichTextCharFormat layer_char_format_for_editor(const Layer &layer)
{
    if (!layer.rich_text.empty())
        return layer.rich_text.default_format;
    return rich_text_char_format_from_layer(layer);
}


static RichTextStroke layer_stroke_for_editor(const Layer &layer)
{
    RichTextStroke stroke;
    stroke.enabled = layer.outline_enabled && layer.stroke_fill_type != 0 &&
                     layer.stroke_width > 0.0f;
    stroke.width = layer.stroke_width;
    stroke.opacity = layer.outline_opacity;
    stroke.on_front = layer.outline_on_front;
    stroke.alignment = layer.outline_alignment;
    stroke.antialias = layer.outline_antialias;
    stroke.join_style = layer.outline_join_style;
    stroke.fill.type = layer.stroke_fill_type == 2 ? 1 : 0;
    stroke.fill.color = layer.stroke_color;
    stroke.fill.gradient_type = layer.stroke_gradient_type;
    stroke.fill.gradient_spread = layer.stroke_gradient_spread;
    stroke.fill.gradient_start_color = layer.stroke_gradient_start_color;
    stroke.fill.gradient_end_color = layer.stroke_gradient_end_color;
    stroke.fill.gradient_start_pos = layer.stroke_gradient_start_pos;
    stroke.fill.gradient_end_pos = layer.stroke_gradient_end_pos;
    stroke.fill.gradient_start_opacity = layer.stroke_gradient_start_opacity;
    stroke.fill.gradient_end_opacity = layer.stroke_gradient_end_opacity;
    stroke.fill.gradient_opacity = layer.stroke_gradient_opacity;
    stroke.fill.gradient_angle = layer.stroke_gradient_angle;
    stroke.fill.gradient_center_x = layer.stroke_gradient_center_x;
    stroke.fill.gradient_center_y = layer.stroke_gradient_center_y;
    stroke.fill.gradient_scale = layer.stroke_gradient_scale;
    stroke.fill.gradient_focal_x = layer.stroke_gradient_focal_x;
    stroke.fill.gradient_focal_y = layer.stroke_gradient_focal_y;
    stroke.fill.gradient_stops = layer.stroke_gradient_stops;
    return stroke;
}

static bool format_value_differs(const RichTextCharFormat &a, const RichTextCharFormat &b, uint32_t bit)
{
    switch (bit) {
    case RichTextCharFontFamily: return a.font_family != b.font_family;
    case RichTextCharFontStyle: return a.font_style != b.font_style;
    case RichTextCharFontSize: return a.font_size != b.font_size;
    case RichTextCharBold: return a.bold != b.bold;
    case RichTextCharItalic: return a.italic != b.italic;
    case RichTextCharUnderline: return a.underline != b.underline;
    case RichTextCharStrikethrough: return a.strikethrough != b.strikethrough;
    case RichTextCharKerning:
        return a.kerning != b.kerning || a.kerning_mode != b.kerning_mode ||
               std::abs(a.manual_kerning - b.manual_kerning) >= 0.0001f;
    case RichTextCharTracking: return a.tracking != b.tracking;
    case RichTextCharScaleX: return a.scale_x != b.scale_x;
    case RichTextCharScaleY: return a.scale_y != b.scale_y;
    case RichTextCharBaselineShift: return a.baseline_shift != b.baseline_shift;
    case RichTextCharFillColor: return !rich_text_fills_equal(a.fill, b.fill);
    case RichTextCharTextStyle: return a.text_style != b.text_style;
    case RichTextCharLigatures: return a.ligatures != b.ligatures;
    case RichTextCharStylisticAlternates: return a.stylistic_alternates != b.stylistic_alternates;
    case RichTextCharFractions: return a.fractions != b.fractions;
    case RichTextCharOpenTypeFeatures: return a.opentype_features != b.opentype_features;
    case RichTextCharLanguage: return a.language != b.language;
    case RichTextCharStroke:
        return rich_text_char_format_difference_mask(a, b, RichTextCharStroke) != 0;
    default: return false;
    }
}

static RichTextCharFormat format_at_offset(const RichTextDocument &doc, size_t offset)
{
    return rich_text_format_at(doc, offset);
}

static RichTextCharFormat rich_text_cursor_format(const RichTextDocument &doc)
{
    if (doc.has_typing_format)
        return rich_text_effective_typing_format(doc);
    if (doc.plain_text.empty())
        return doc.default_format;
    const size_t pos = std::min(doc.selection.head, doc.plain_text.size());
    const size_t sample = pos < doc.plain_text.size() ? pos : (pos > 0 ? pos - 1 : 0);
    return format_at_offset(doc, sample);
}

static RichTextCharFormat insertion_format_for_text_replace(const RichTextDocument &doc)
{
    if (doc.plain_text.empty())
        return doc.default_format;
    const size_t start = std::min(doc.selection.anchor, doc.selection.head);
    const size_t sample = std::min(start, doc.plain_text.size() - 1);
    return format_at_offset(doc, sample);
}

static uint32_t insertion_mask_for_text_replace(const RichTextDocument &doc)
{
    if (doc.plain_text.empty())
        return 0;
    const size_t start = std::min(doc.selection.anchor, doc.selection.head);
    const size_t sample = std::min(start, doc.plain_text.size() - 1);
    return rich_text_format_mask_at(doc, sample);
}

static RichTextCharFormatSummary summarize_rich_text_char_format(
    const Layer &layer, bool active_selection,
    double local_time = std::numeric_limits<double>::quiet_NaN())
{
    RichTextDocument doc = std::isfinite(local_time)
        ? rich_text_document_for_editor_time(layer, local_time)
        : (layer.rich_text.empty() ? rich_text_document_from_layer_defaults(layer)
                                   : layer.rich_text);
    /* Properties inspect the effective canonical character appearance.
     * Automatic styles are transient, while manual default/inline properties
     * remain authored exclusively in RichTextDocument. */
    doc = rich_text_effective_document_for_editor(doc);
    doc.normalize();
    RichTextCharFormatSummary summary;
    const size_t text_len = doc.plain_text.size();
    size_t start = 0;
    size_t end = text_len;
    if (active_selection) {
        start = std::min(doc.selection.anchor, doc.selection.head);
        end = std::max(doc.selection.anchor, doc.selection.head);
        if (start == end) {
            summary.format = rich_text_cursor_format(doc);
            summary.valid = true;
            return summary;
        }
    }
    if (text_len == 0) {
        summary.format = active_selection ? rich_text_effective_typing_format(doc)
                                          : doc.default_format;
        summary.valid = true;
        return summary;
    }
    start = std::min(start, text_len);
    end = std::min(end, text_len);
    if (start >= end) {
        summary.format = doc.default_format;
        summary.valid = true;
        return summary;
    }
    summary.format = format_at_offset(doc, start);
    summary.valid = true;
    constexpr uint32_t bits[] = {RichTextCharFontFamily, RichTextCharFontStyle, RichTextCharFontSize,
        RichTextCharBold, RichTextCharItalic, RichTextCharUnderline, RichTextCharStrikethrough,
        RichTextCharKerning, RichTextCharTracking, RichTextCharScaleX, RichTextCharScaleY,
        RichTextCharBaselineShift, RichTextCharFillColor, RichTextCharTextStyle, RichTextCharLigatures,
        RichTextCharStylisticAlternates, RichTextCharFractions, RichTextCharOpenTypeFeatures,
        RichTextCharLanguage, RichTextCharStroke};
    size_t i = rich_text_utf8_next_boundary(doc.plain_text, start + 1);
    while (i < end) {
        RichTextCharFormat f = format_at_offset(doc, i);
        for (uint32_t bit : bits)
            if (format_value_differs(summary.format, f, bit)) summary.mixed |= bit;
        if (summary.format.kerning != f.kerning)
            summary.kerning_mixed |= RichTextKerningEnabledMixed;
        if (summary.format.kerning_mode != f.kerning_mode)
            summary.kerning_mixed |= RichTextKerningModeMixed;
        if (summary.format.manual_kerning != f.manual_kerning)
            summary.kerning_mixed |= RichTextKerningValueMixed;

        const RichTextFill &base_fill = summary.format.fill;
        const RichTextFill &fill = f.fill;
        if (base_fill.type != fill.type)
            summary.fill_mixed |= RichTextFillTypeMixed;
        if (base_fill.color != fill.color)
            summary.fill_mixed |= RichTextFillColorMixed;
        if (base_fill.gradient_type != fill.gradient_type)
            summary.fill_mixed |= RichTextFillGradientTypeMixed;
        if (base_fill.gradient_spread != fill.gradient_spread)
            summary.fill_mixed |= RichTextFillGradientSpreadMixed;
        if (base_fill.gradient_start_color != fill.gradient_start_color)
            summary.fill_mixed |= RichTextFillGradientStartColorMixed;
        if (base_fill.gradient_end_color != fill.gradient_end_color)
            summary.fill_mixed |= RichTextFillGradientEndColorMixed;
        if (base_fill.gradient_start_pos != fill.gradient_start_pos)
            summary.fill_mixed |= RichTextFillGradientStartPosMixed;
        if (base_fill.gradient_end_pos != fill.gradient_end_pos)
            summary.fill_mixed |= RichTextFillGradientEndPosMixed;
        if (base_fill.gradient_start_opacity != fill.gradient_start_opacity)
            summary.fill_mixed |= RichTextFillGradientStartOpacityMixed;
        if (base_fill.gradient_end_opacity != fill.gradient_end_opacity)
            summary.fill_mixed |= RichTextFillGradientEndOpacityMixed;
        if (base_fill.gradient_opacity != fill.gradient_opacity)
            summary.fill_mixed |= RichTextFillGradientOpacityMixed;
        if (base_fill.gradient_angle != fill.gradient_angle)
            summary.fill_mixed |= RichTextFillGradientAngleMixed;
        if (base_fill.gradient_center_x != fill.gradient_center_x)
            summary.fill_mixed |= RichTextFillGradientCenterXMixed;
        if (base_fill.gradient_center_y != fill.gradient_center_y)
            summary.fill_mixed |= RichTextFillGradientCenterYMixed;
        if (base_fill.gradient_scale != fill.gradient_scale)
            summary.fill_mixed |= RichTextFillGradientScaleMixed;
        if (base_fill.gradient_focal_x != fill.gradient_focal_x)
            summary.fill_mixed |= RichTextFillGradientFocalXMixed;
        if (base_fill.gradient_focal_y != fill.gradient_focal_y)
            summary.fill_mixed |= RichTextFillGradientFocalYMixed;
        if (base_fill.gradient_stops != fill.gradient_stops)
            summary.fill_mixed |= RichTextFillGradientStopsMixed;
        i = rich_text_utf8_next_boundary(doc.plain_text, i + 1);
    }
    return summary;
}

static RichTextParagraphFormatSummary summarize_rich_text_paragraph_format(
    const Layer &layer, bool active_selection,
    double local_time = std::numeric_limits<double>::quiet_NaN())
{
    RichTextDocument doc = std::isfinite(local_time)
        ? rich_text_document_for_editor_time(layer, local_time)
        : (layer.rich_text.empty() ? rich_text_document_from_layer_defaults(layer)
                                   : layer.rich_text);
    doc.normalize();

    RichTextParagraphFormatSummary summary;
    const size_t text_len = doc.plain_text.size();
    size_t start = 0;
    size_t end = text_len;
    if (active_selection) {
        start = std::min(doc.selection.anchor, doc.selection.head);
        end = std::max(doc.selection.anchor, doc.selection.head);
        if (start == end) {
            summary.format = rich_text_paragraph_format_at(doc, start);
            summary.valid = true;
            return summary;
        }
    }

    bool have_format = false;
    for (const auto &block : doc.blocks) {
        const size_t block_start = std::min(block.start, text_len);
        const size_t block_end = block.length > text_len - block_start
            ? text_len : block_start + block.length;
        bool selected = !active_selection;
        if (active_selection) {
            selected = block_start < end && block_end > start;
            if (block_start == end)
                selected = false;
            if (block.length == 0 && block_start == text_len)
                selected = start <= text_len && end == text_len;
        }
        if (!selected)
            continue;

        const RichTextParagraphFormat current =
            rich_text_paragraph_format_at(doc, block_start);
        if (!have_format) {
            summary.format = current;
            summary.valid = true;
            have_format = true;
        } else {
            summary.mixed |= rich_text_paragraph_format_difference_mask(
                summary.format, current, RichTextParagraphAll);
        }
    }

    if (!summary.valid) {
        summary.format = doc.default_paragraph_format;
        summary.valid = true;
    }
    return summary;
}

static void apply_rich_text_format_to_layer_range(Layer &layer, const RichTextCharFormat &format, uint32_t mask, bool active_selection)
{
    if (layer.type != LayerType::Text && layer.type != LayerType::Ticker && layer.type != LayerType::Clock) return;
    if (layer.rich_text.empty())
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
    RichTextDocument &doc = layer.rich_text;
    doc.normalize();
    const size_t text_len = doc.plain_text.size();
    size_t start = active_selection ? std::min(doc.selection.anchor, doc.selection.head) : 0;
    size_t end = active_selection ? std::max(doc.selection.anchor, doc.selection.head) : 0;
    start = std::min(start, text_len);
    end = std::min(end, text_len);
    if (!active_selection) {
        /* Object-level formatting applies the requested property to the whole
         * textbox. Remove only that property's inline overrides; preserve every
         * unrelated local style/color. */
        rich_text_document_apply_default_char_format(doc, format, mask, true);
        doc.has_typing_format = false;
        doc.typing_format_mask = 0;
        doc.normalize();
        rich_text_document_sync_layer_mirrors_canonical(layer);
        return;
    }
    if (start == end) {
        uint32_t cursor_mask = doc.has_typing_format ? doc.typing_format_mask : 0;
        if (!doc.has_typing_format && !doc.plain_text.empty()) {
            const size_t pos = std::min(doc.selection.head, doc.plain_text.size());
            const size_t sample = pos < doc.plain_text.size() ? pos : (pos > 0 ? pos - 1 : 0);
            cursor_mask = rich_text_format_mask_at(doc, sample);
        }
        RichTextCharFormat cursor_format = rich_text_cursor_format(doc);
        rich_text_merge_char_format(cursor_format, format, mask);
        doc.typing_format = cursor_format;
        doc.typing_format_mask = (cursor_mask | mask) & RichTextCharAll;
        doc.has_typing_format = true;
        doc.normalize();
        rich_text_document_sync_layer_mirrors_canonical(layer);
        return;
    }
    rich_text_document_apply_format(doc, start, end - start, format, mask);
    doc.has_typing_format = false;
    doc.typing_format_mask = 0;
    rich_text_document_sync_layer_mirrors_canonical(layer);
}

static void apply_rich_text_paragraph_format_to_layer(Layer &layer,
                                                      const RichTextParagraphFormat &format,
                                                      uint32_t mask,
                                                      bool active_selection)
{
    if (layer.type != LayerType::Text && layer.type != LayerType::Ticker && layer.type != LayerType::Clock) return;
    if (layer.rich_text.empty())
        layer.rich_text = rich_text_document_from_layer_defaults(layer);
    RichTextDocument &doc = layer.rich_text;
    doc.normalize();
    mask &= RichTextParagraphAll;
    if (!active_selection) {
        rich_text_document_apply_default_paragraph_format(
            doc, format, mask, true);
    } else {
        const size_t start = std::min(doc.selection.anchor, doc.selection.head);
        const size_t end = std::max(doc.selection.anchor, doc.selection.head);
        rich_text_document_apply_paragraph_format(doc, start, end, format, mask);
    }
    doc.normalize();
    rich_text_document_sync_layer_mirrors_canonical(layer);
}

/* Legacy HTML rich text helpers removed: RichTextDocument is the only source of truth. */

static double title_manual_screenshot_time(const Title &title)
{
    if (title.playback_mode == 1)
        return std::clamp(title.loop_start, 0.0, title.duration);
    if (title.playback_mode == 2)
        return std::clamp(title.pause_time, 0.0, title.duration);
    return std::clamp(title.duration * 0.5, 0.0, title.duration);
}

static std::string title_screenshot_png_base64(const QImage &screenshot)
{
    if (screenshot.isNull())
        return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!screenshot.save(&buffer, "PNG"))
        return {};
    const QByteArray encoded = png.toBase64();
    return std::string(encoded.constData(), (size_t)encoded.size());
}

static std::string title_manual_screenshot_png_base64(const Title &title)
{
    const QImage screenshot = render_title_to_image(
        title, title_manual_screenshot_time(title));
    return title_screenshot_png_base64(screenshot);
}


/* ────────────────────────────────────────────────────────────────── */
/*  Dark AE-style palette constants                                   */
/* ────────────────────────────────────────────────────────────────── */
static const QColor C_BG_DARK  { 0x1a1a1a };
static const QColor C_BG_MID   { 0x252525 };
static const QColor C_BG_LIGHT { 0x2e2e2e };
static const QColor C_ACCENT   { 0x0078d4 };

static bool editor_focus_accepts_text(QWidget *widget);

/* OBS safe area margins: Rec. ITU-R BT.1848-1 / EBU R 95. */
static constexpr double OBS_ACTION_SAFE_PERCENT = 0.035;
static constexpr double OBS_GRAPHICS_SAFE_PERCENT = 0.05;

/* Canvas editing controls are measured in view pixels so object scale never
 * changes their apparent size or proportions. */
static constexpr double CANVAS_CONTROL_SIZE_PX = 8.0;
static constexpr double CANVAS_CONTROL_HIT_RADIUS_PX = 8.0;
static constexpr double CANVAS_ROTATE_HIT_RADIUS_PX = 24.0;
static constexpr double CANVAS_ORIGIN_RADIUS_PX = 3.6;
static constexpr double CANVAS_GRADIENT_HANDLE_RADIUS_PX = 5.0;
static constexpr double CANVAS_ROTATION_SNAP_DEGREES = 15.0;

static bool editor_image_path_is_svg(const QString &path)
{
    return path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive) ||
           path.endsWith(QStringLiteral(".svgz"), Qt::CaseInsensitive);
}

static double radians_to_degrees(double radians)
{
    return radians * 180.0 / 3.14159265358979323846;
}

static double degrees_to_radians(double degrees)
{
    return degrees * 3.14159265358979323846 / 180.0;
}

static double normalize_degrees(double degrees)
{
    while (degrees > 180.0) degrees -= 360.0;
    while (degrees <= -180.0) degrees += 360.0;
    return degrees;
}

static QPointF rotate_point_around(const QPointF &point, const QPointF &pivot, double degrees)
{
    double radians = degrees_to_radians(degrees);
    double c = std::cos(radians);
    double ss = std::sin(radians);
    double dx = point.x() - pivot.x();
    double dy = point.y() - pivot.y();
    return QPointF(pivot.x() + dx * c - dy * ss,
                   pivot.y() + dx * ss + dy * c);
}

static QCursor canvas_rotation_cursor()
{
    static const QCursor cursor = []() {
        QPixmap pixmap(24, 24);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(255, 255, 255), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawArc(QRectF(5.0, 4.0, 14.0, 14.0), 35 * 16, 285 * 16);
        QPolygonF arrow;
        arrow << QPointF(17.0, 4.0) << QPointF(22.0, 5.5) << QPointF(18.8, 10.0);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawPolygon(arrow);
        painter.setPen(QPen(QColor(0, 0, 0, 170), 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(QRectF(5.0, 4.0, 14.0, 14.0), 35 * 16, 285 * 16);
        return QCursor(pixmap, 12, 12);
    }();
    return cursor;
}

static QSize editor_image_intrinsic_size(const QString &path)
{
    if (editor_image_path_is_svg(path)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) return QSize();
        QSize size = renderer.defaultSize();
        if (!size.isValid() || size.isEmpty())
            size = renderer.viewBox().size();
        return size;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && !size.isEmpty())
        return size;

    QImage image = reader.read();
    return image.isNull() ? QSize() : image.size();
}

static QImage editor_load_layer_image(const QString &path, const QSize &fallback_size = QSize())
{
    if (editor_image_path_is_svg(path)) {
        QSvgRenderer renderer(path);
        if (!renderer.isValid()) return QImage();

        QSize size = fallback_size.isValid() && !fallback_size.isEmpty()
            ? fallback_size
            : renderer.defaultSize();
        if (!size.isValid() || size.isEmpty())
            size = renderer.viewBox().size();
        if (!size.isValid() || size.isEmpty())
            size = QSize(256, 256);

        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        renderer.render(&painter);
        return image;
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

static QPixmap editor_image_preview_pixmap(const QString &path, const QSize &size)
{
    const QSize target = size.isValid() && !size.isEmpty() ? size : QSize(104, 104);
    QPixmap pixmap(target);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    const QPalette pal = qApp ? qApp->palette() : QPalette();
    const bool dark = pal.color(QPalette::Window).lightness() < 128;
    const QColor checker_a = dark ? QColor(62, 62, 62) : QColor(224, 224, 224);
    const QColor checker_b = dark ? QColor(42, 42, 42) : QColor(188, 188, 188);
    constexpr int checker = 8;
    for (int y = 0; y < target.height(); y += checker) {
        for (int x = 0; x < target.width(); x += checker) {
            painter.fillRect(QRect(x, y, checker, checker),
                             (((x / checker) + (y / checker)) & 1) ? checker_b : checker_a);
        }
    }

    const QString trimmed_path = path.trimmed();
    if (!trimmed_path.isEmpty()) {
        const QSize image_target(std::max(1, target.width() - 8), std::max(1, target.height() - 8));
        QImage image = editor_load_layer_image(trimmed_path, image_target);
        if (!image.isNull()) {
            image = image.scaled(image_target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPoint top_left((target.width() - image.width()) / 2,
                                  (target.height() - image.height()) / 2);
            painter.drawImage(top_left, image);
        }
    }

    painter.setPen(pal.color(QPalette::Mid));
    painter.drawRect(QRect(QPoint(0, 0), target).adjusted(0, 0, -1, -1));
    return pixmap;
}

static void set_image_preview_label(QLabel *label, const QString &path)
{
    if (!label) return;
    const QSize size = label->size().isValid() && !label->size().isEmpty() ? label->size() : QSize(104, 104);
    label->setPixmap(editor_image_preview_pixmap(path, size));
}

static const QColor C_TEXT     { 0xcccccc };
static const QColor C_RULER    { 0x1e1e1e };
static double obs_frame_rate()
{
    return fxm::current_frame_rate();
}

static double obs_frame_duration()
{
    return 1.0 / std::max(1.0, obs_frame_rate());
}

static double snap_to_obs_frame(double t)
{
    double fd = obs_frame_duration();
    return std::round(t / fd) * fd;
}

static QString format_timecode(double t)
{
    double fps_d = obs_frame_rate();
    int fps = std::max(1, (int)std::round(fps_d));
    int total_frames = std::max(0, (int)std::round(t * fps_d));
    int frames = total_frames % fps;
    int total_seconds = total_frames / fps;
    int seconds = total_seconds % 60;
    int minutes = (total_seconds / 60) % 60;
    int hours = total_seconds / 3600;
    return QString("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(frames, 2, 10, QChar('0'));
}
