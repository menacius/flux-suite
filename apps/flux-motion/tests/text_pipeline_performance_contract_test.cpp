#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require_contains(const std::string &text, const std::string &needle,
                      const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing text-pipeline contract: " << label << " ("
              << needle << ")\n";
    return false;
}

bool require_absent(const std::string &text, const std::string &needle,
                    const char *label)
{
    if (text.find(needle) == std::string::npos)
        return true;
    std::cerr << "Obsolete text-pipeline path remains: " << label << " ("
              << needle << ")\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 11) {
        std::cerr << "usage: text_pipeline_performance_contract_test "
                     "<layout-qt> <gpu-renderer> <inline-text> <preview-view> "
                     "<document-editing> <source-layout> <editor-model> <rich-text> "
                     "<rich-text-qt-adapter> <source-compat-rendering>\n";
        return 2;
    }

    const std::string layout_qt = read_file(argv[1]);
    const std::string gpu = read_file(argv[2]);
    const std::string inline_text = read_file(argv[3]);
    const std::string preview = read_file(argv[4]);
    const std::string editor = read_file(argv[5]);
    const std::string source_layout = read_file(argv[6]);
    const std::string editor_model = read_file(argv[7]);
    const std::string rich_text = read_file(argv[8]);
    const std::string qt_adapter = read_file(argv[9]);
    const std::string source_compat = read_file(argv[10]);

    bool ok = true;

    ok &= require_contains(layout_qt, "run.font = resolved_font_key(raw_font)",
                           "each shaped run keeps its resolved physical font key");
    ok &= require_contains(layout_qt, "text_layout_register_raw_font(key, raw_font)",
                           "exact shaped QRawFont retained for Linux fallback faces");
    ok &= require_contains(layout_qt, "class QtUtf8PositionMap",
                           "one UTF-8/UTF-16 position map per paragraph");
    ok &= require_absent(layout_qt, "text.left(qpos).toUtf8()",
                         "quadratic QTextLayout position conversion");
    ok &= require_absent(layout_qt, "text.mid(i, char_units).toUtf8()",
                         "per-character QString slicing in layout mapping");

    ok &= require_contains(gpu, "text_layout_registered_raw_font(font_key)",
                           "GPU atlas consumes exact shaped font face");
    ok &= require_contains(gpu, "page.mark_dirty", "glyph atlas dirty rectangle");
    ok &= require_contains(gpu, "gs_texture_map", "partial dynamic atlas upload");
    ok &= require_contains(gpu, "strokeAntialias", "stroke antialias property reaches shader");
    ok &= require_absent(gpu, "Miter/bevel inline text joins require the compatibility raster path",
                         "scaled text must not fall back because of stroke join mode");
    ok &= require_contains(gpu, "cluster_slices[cluster_index]",
                           "mixed underline/strike ranges follow cluster paint slices");
    ok &= require_contains(gpu, "raw.boundingRect(glyph_id)",
                           "visible glyph alpha-map failure falls back instead of disappearing");

    ok &= require_contains(preview, "QTextDocument::contentsChange",
                           "precise inline edit range signal");
    ok &= require_contains(inline_text, "rich_text_document_replace_canonical_range",
                           "canonical range edit fast path");
    ok &= require_contains(inline_text, "schedule_inline_text_refresh",
                           "coalesced inline refresh");
    ok &= require_absent(inline_text, "render_to_frame();",
                         "synchronous render from inline keystroke path");
    ok &= require_contains(editor, "inline_text_panel_refresh_timer_",
                           "properties/timeline reflection is coalesced");
    ok &= require_contains(editor, "inline_text_live_publish_timer_",
                           "live title publication is coalesced");

    ok &= require_contains(source_layout, "rich_text_document_canonical_copy(layer)",
                           "source avoids copying complete Layer for text model");
    ok &= require_contains(editor_model, "rich_text_document_canonical_copy(layer)",
                           "editor avoids copying complete Layer for text model");
    ok &= require_absent(source_layout, "Layer canonical = layer",
                         "source-side complete Layer copy");
    ok &= require_contains(source_layout, "class SourceQtUtf8PositionMap",
                           "compatibility source uses one Unicode position map");
    ok &= require_absent(source_layout, ".left(block.position()).toUtf8()",
                         "quadratic source paragraph position conversion");
    ok &= require_absent(editor_model, "Layer canonical = layer",
                         "editor-side complete Layer copy");
    ok &= require_contains(qt_adapter, "class EditorQtUtf8PositionMap",
                           "editor Qt adapter uses one Unicode position map");
    ok &= require_absent(qt_adapter, "text.left(qpos).toUtf8()",
                         "quadratic editor cursor position conversion");
    ok &= require_contains(qt_adapter,
                           "set_qtext_range_from_rich_range(cursor, qtext, positions,",
                           "range conversion receives the editor position map");
    ok &= require_absent(editor_model, "rich_byte_offset_from_qtext_position(",
                         "removed editor conversion helper call");
    ok &= require_contains(editor_model, "positions.byte_from_qpos(",
                           "editor model reuses the local position map");
    ok &= require_absent(source_compat,
                         "qtext_position_from_rich_byte_offset_source(",
                         "removed source conversion helper call");
    ok &= require_contains(source_compat, "positions.qpos_from_byte(",
                           "compatibility renderer reuses the local position map");
    ok &= require_contains(rich_text, "rich_text_document_with_auto_styles_canonical",
                           "canonical auto-style evaluation");
    ok &= require_contains(rich_text, "std::vector<size_t> boundaries",
                           "auto-style state allocated per codepoint rather than byte");

    const char *character_properties[] = {
        "RichTextPropFontFamily", "RichTextPropFontStyle",
        "RichTextPropFontSize", "RichTextPropBold", "RichTextPropItalic",
        "RichTextPropUnderline", "RichTextPropStrikethrough",
        "RichTextPropKerning", "RichTextPropKerningMode",
        "RichTextPropManualKerning", "RichTextPropTracking",
        "RichTextPropScaleX", "RichTextPropScaleY",
        "RichTextPropBaselineShift", "RichTextPropTextStyle",
        "RichTextPropLigatures", "RichTextPropStylisticAlternates",
        "RichTextPropFractions", "RichTextPropOpenTypeFeatures",
        "RichTextPropFillType", "RichTextPropGradientSpread",
        "RichTextPropGradientStartOpacity", "RichTextPropGradientEndOpacity",
        "RichTextPropGradientCenterX", "RichTextPropGradientCenterY",
        "RichTextPropGradientFocalX", "RichTextPropGradientFocalY",
        "RichTextPropStrokeEnabled", "RichTextPropStrokeWidth",
        "RichTextPropStrokeOpacity", "RichTextPropStrokeOnFront",
        "RichTextPropStrokeAlignment", "RichTextPropStrokeAntialias",
        "RichTextPropStrokeJoinStyle", "RichTextPropStrokeGradientSpread",
    };
    for (const char *property : character_properties)
        ok &= require_contains(qt_adapter, property,
                               "character property Qt round-trip mapping");

    const char *paragraph_properties[] = {
        "RichTextParagraphPropAlignH", "RichTextParagraphPropAlignV",
        "RichTextParagraphPropIndentLeft", "RichTextParagraphPropIndentRight",
        "RichTextParagraphPropIndentFirstLine",
        "RichTextParagraphPropLineSpacing", "RichTextParagraphPropSpaceBefore",
        "RichTextParagraphPropSpaceAfter", "RichTextParagraphPropHyphenate",
    };
    for (const char *property : paragraph_properties)
        ok &= require_contains(qt_adapter, property,
                               "paragraph property Qt round-trip mapping");

    ok &= require_contains(layout_qt, "font.setStretch(100);",
                           "font matching is independent from character scale");
    ok &= require_contains(layout_qt, "apply_line_horizontal_character_scale",
                           "independent horizontal character geometry");
    ok &= require_contains(layout_qt, "glyph.scale_x = std::clamp(glyph.scale_x * factor",
                           "horizontal fit scales glyph quads, not only advances");
    ok &= require_contains(layout_qt, "glyph_format.baseline_shift",
                           "baseline shift reaches shaped glyph geometry");
    ok &= require_contains(layout_qt, "font.setFeature(\"liga\"",
                           "ligature OpenType feature");
    ok &= require_contains(layout_qt, "font.setFeature(\"salt\"",
                           "stylistic alternates OpenType feature");
    ok &= require_contains(layout_qt, "font.setFeature(\"frac\"",
                           "fractions OpenType feature");
    ok &= require_contains(layout_qt, "paragraph_format.indent_first_line",
                           "first-line indent reaches layout");
    ok &= require_contains(layout_qt, "paragraph_format.space_before",
                           "paragraph spacing reaches layout");
    ok &= require_contains(layout_qt, "paragraph_format.hyphenate",
                           "paragraph wrapping option reaches layout");

    return ok ? 0 : 1;
}
