#include "title-editor-internal.h"
#include "title-logger.h"

#include <algorithm>
#include <cmath>
#include <QAbstractTextDocumentLayout>


static bool inline_text_group_chain_is_expanded(const std::shared_ptr<Title> &title,
                                                const Layer &layer)
{
    if (!title) return true;
    std::string parent_id = layer.parent_id;
    int guard = 0;
    while (!parent_id.empty() && guard++ < 64) {
        auto parent = title->find_layer(parent_id);
        if (!parent || !layer_type_is_container(parent->type))
            break;
        if (parent->group_collapsed)
            return false;
        parent_id = parent->parent_id;
    }
    return true;
}

std::shared_ptr<Layer> CanvasPreview::text_layer_at_view_pos(const QPointF &view_pt) const
{
    if (!title_) return nullptr;
    QPointF canvas = view_to_canvas(view_pt);
    for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
        const auto &layer = *it;
        if (!layer || !is_canvas_text_layer(*layer) || layer->type == LayerType::Clock) continue;
        if (!layer->visible || layer->locked ||
            !inline_text_group_chain_is_expanded(title_, *layer)) continue;
        if (playhead_ < layer->in_time || playhead_ > layer->out_time) continue;
        if (layer_local_rect(*layer).contains(canvas_to_layer(*layer, canvas)))
            return layer;
    }
    return nullptr;
}


static QString scale_rich_text_font_sizes(const QString &html, double scale)
{
    if (html.isEmpty() || std::abs(scale - 1.0) < 0.0001)
        return html;

    QString scaled = html;
    QRegularExpression re(
        QStringLiteral("((?:font-size|margin-left|margin-right|margin-top|margin-bottom|text-indent)\\s*:\\s*)(-?[0-9]+(?:\\.[0-9]+)?)(px|pt)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype offset = 0;
    QRegularExpressionMatch match;
    while ((match = re.match(scaled, offset)).hasMatch()) {
        const QString property = match.captured(1);
        const double value = match.captured(2).toDouble();
        const QString unit = match.captured(3);
        const double scaled_value = property.trimmed().startsWith(QStringLiteral("font-size"), Qt::CaseInsensitive)
                                      ? std::max(1.0, value * scale)
                                      : value * scale;
        const QString replacement = QStringLiteral("%1%2%3")
                                        .arg(property)
                                        .arg(scaled_value, 0, 'f', 3)
                                        .arg(unit);
        scaled.replace(match.capturedStart(0), match.capturedLength(0), replacement);
        offset = match.capturedStart(0) + replacement.size();
    }
    return scaled;
}


static bool inline_document_has_style_overrides(const QTextDocument *doc, const Layer &layer, double t, double visual_scale)
{
    if (!doc) return false;

    QFont expected_font = font_for_layer(layer, t);
    if (expected_font.pixelSize() > 0)
        expected_font.setPixelSize(std::max(1, (int)std::round(expected_font.pixelSize() * visual_scale)));
    const QColor expected_color = color_from_argb(eval_text_color(layer, t));
    const int expected_weight = expected_font.weight();
    const bool expected_italic = expected_font.italic();
    const bool expected_underline = layer.text_underline;
    const bool expected_strike = layer.text_strikethrough;

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.text().isEmpty()) continue;
            const QTextCharFormat fmt = fragment.charFormat();
            if (fmt.fontWeight() != expected_weight) return true;
            if (fmt.fontItalic() != expected_italic) return true;
            if (fmt.fontUnderline() != expected_underline) return true;
            if (fmt.fontStrikeOut() != expected_strike) return true;
            if (fmt.hasProperty(QTextFormat::FontFamily) && fmt.fontFamily() != expected_font.family()) return true;
            if (fmt.hasProperty(QTextFormat::FontPixelSize) && std::abs(fmt.font().pixelSize() - expected_font.pixelSize()) > 1) return true;
            if (fmt.hasProperty(QTextFormat::FontPointSize) && expected_font.pointSizeF() > 0.0 &&
                std::abs(fmt.fontPointSize() - expected_font.pointSizeF()) > 0.5) return true;
            if (fmt.foreground().style() != Qt::NoBrush) {
                const QColor color = fmt.foreground().color();
                if (layer.fill_type == 1 && color.isValid() && color.alpha() == 0)
                    continue;
                if (color.isValid() && color != expected_color) return true;
            }
        }
    }
    return false;
}

double CanvasPreview::inline_text_visual_scale(const Layer &layer) const
{
    const double lt = std::max(0.0, playhead_ - layer.in_time);
    const double sx = std::abs(layer.scale.evaluate(lt).x);
    const double sy = std::abs(layer.scale.evaluate(lt).y);
    return std::clamp(view_scale() * std::sqrt(std::max(0.0001, sx * sy)), 0.05, 16.0);
}

static int inline_text_visual_line_count(const QTextDocument &doc)
{
    int count = 0;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        const QTextLayout *layout = block.layout();
        if (layout)
            count += layout->lineCount();
    }
    return count;
}

static void apply_inline_text_vertical_distribute(QTextDocument &doc, const Layer &layer,
                                                  const QRectF &text_rect, double visual_scale)
{
    if (layer.align_v != 3 || layer.text_overflow_mode == 2)
        return;

    const QSizeF natural_size = doc.size();
    const int line_count = inline_text_visual_line_count(doc);
    const double target_height = text_rect.height() * visual_scale;
    if (line_count <= 1 || natural_size.height() >= target_height)
        return;

    const double extra_gap = (target_height - natural_size.height()) /
                             (static_cast<double>(line_count) - 1.0);
    if (extra_gap <= 0.0)
        return;

    QTextBlockFormat block_format;
    block_format.setLineHeight(extra_gap, QTextBlockFormat::LineDistanceHeight);
    QTextCursor cursor(&doc);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(block_format);
}

void CanvasPreview::configure_inline_text_editor(const Layer &layer)
{
    if (!inline_text_editor_) return;

    QSignalBlocker blocker(inline_text_editor_);
    QTextCursor saved_cursor = inline_text_editor_->textCursor();

    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    const double visual_scale = inline_text_visual_scale(layer);
    QFont font = font_for_layer(layer, local_time);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * visual_scale)));
    inline_text_editor_->setFont(font);
    QColor transparent_text_color = color_from_argb(eval_text_color(layer, local_time));
    transparent_text_color.setAlpha(0);
    inline_text_editor_->setTextColor(transparent_text_color);

    QTextDocument *doc = inline_text_editor_->document();
    doc->setDocumentMargin(0.0);
    doc->setDefaultFont(font);

    QTextOption option = doc->defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere
                                                         : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    Qt::Alignment align = Qt::AlignLeft;
    if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
    else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
    else if (layer.align_h >= 3) align = Qt::AlignJustify;
    option.setAlignment(align);
    doc->setDefaultTextOption(option);

    const QRectF local = layer_local_rect(layer);
    const QRectF text_rect = text_rect_for_style(local, layer);
    const int wrap_width_px = std::max(1, (int)std::ceil(text_rect.width() * visual_scale));
    doc->setTextWidth(layer.text_overflow_mode == 2 ? -1.0 : (qreal)wrap_width_px);
    doc->setPageSize(layer.text_overflow_mode == 2
                         ? QSizeF(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
                         : QSizeF(wrap_width_px, QWIDGETSIZE_MAX));
    inline_text_editor_->setLineWrapMode(layer.text_overflow_mode == 0 ? QTextEdit::FixedPixelWidth : QTextEdit::NoWrap);
    inline_text_editor_->setLineWrapColumnOrWidth(wrap_width_px);
    inline_text_editor_->setWordWrapMode(option.wrapMode());

    const RichTextDocument editor_model =
        rich_text_document_for_editor_time(layer, local_time);

    RichTextCharFormat layer_format = rich_text_effective_typing_format(editor_model);
    QTextCharFormat char_format = qtext_format_from_rich_text_format(layer_format, visual_scale);
    apply_editor_rich_text_baseline_delta(char_format, layer_format,
                                          editor_model.default_format.baseline_shift,
                                          visual_scale);
    char_format.setProperty(
        RichTextPropManualMask,
        (uint)(editor_model.has_typing_format
                   ? editor_model.typing_format_mask & RichTextCharAll
                   : 0));
    QColor editor_text_color = color_from_argb(eval_text_color(layer, local_time));
    editor_text_color.setAlpha(0);
    char_format.setForeground(editor_text_color);
    /* qtext_format_from_rich_text_format() already contains the effective
     * cursor/range underline and strikethrough state. Reapplying layer-wide
     * mirrors here erased mixed inline decoration and made newly typed text
     * inherit the document default instead of the caret style. */

    RichTextParagraphFormat paragraph_base = editor_model.default_paragraph_format;
    paragraph_base.align_h = layer.align_h;
    paragraph_base.align_v = layer.align_v;
    paragraph_base.indent_left = (float)eval_paragraph_indent_left(layer, local_time);
    paragraph_base.indent_right = (float)eval_paragraph_indent_right(layer, local_time);
    paragraph_base.indent_first_line = (float)eval_paragraph_indent_first_line(layer, local_time);
    paragraph_base.line_spacing = layer.text_leading;
    paragraph_base.space_before = (float)eval_paragraph_space_before(layer, local_time);
    paragraph_base.space_after = (float)eval_paragraph_space_after(layer, local_time);
    paragraph_base.hyphenate = layer.paragraph_hyphenate;
    apply_rich_text_paragraph_blocks_to_qtext_document(doc, editor_model,
                                                        paragraph_base, visual_scale);
    apply_inline_text_vertical_distribute(*doc, layer, text_rect, visual_scale);
    /*
     * Do not call mergeCurrentCharFormat() while the saved cursor owns a
     * selection. QTextEdit applies that merge to the selected document text,
     * so re-positioning the inline editor after begin_text_edit() could repaint
     * every character with the current/layer style and hide mixed per-character
     * sizes or colors until edit mode was committed.
     */
    if (!saved_cursor.hasSelection())
        inline_text_editor_->mergeCurrentCharFormat(char_format);
    if (auto *layout = doc->documentLayout())
        layout->documentSize();
    inline_text_editor_->setTextCursor(saved_cursor);
}

bool CanvasPreview::sync_inline_text_layer(bool mark_dirty)
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || !title_)
        return false;
    auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer)
        return false;
    /* begin_text_edit() and every model mutation leave a canonical document.
     * Avoid a full normalize() on every cursor move/keystroke; only migrate a
     * genuinely legacy/uninitialized layer here. */
    if (layer->rich_text.version < 2 || layer->rich_text.blocks.empty())
        rich_text_document_ensure_canonical(*layer);

    QTextDocument *editor_doc = inline_text_editor_->document();
    const QTextCursor cursor = inline_text_editor_->textCursor();

    /* Cursor/selection-only changes do not require toPlainText(), a complete
     * UTF-8 conversion, or QTextDocument fragment traversal. The canonical
     * model and QTextDocument contain the same text, so map Qt's UTF-16 cursor
     * positions directly against the UTF-8 model. */
    if (inline_text_change_count_ == 0 && editor_doc &&
        !editor_doc->isModified()) {
        RichTextSelection selection{
            rich_text_utf8_byte_offset_from_utf16_position(
                layer->rich_text.plain_text, std::max(0, cursor.anchor())),
            rich_text_utf8_byte_offset_from_utf16_position(
                layer->rich_text.plain_text, std::max(0, cursor.position()))};
        const size_t text_len = layer->rich_text.plain_text.size();
        selection.anchor = std::min(selection.anchor, text_len);
        selection.head = std::min(selection.head, text_len);
        const bool selection_changed =
            layer->rich_text.selection.anchor != selection.anchor ||
            layer->rich_text.selection.head != selection.head;
        if (selection_changed)
            layer->rich_text.selection = selection;
        if (!cursor.hasSelection()) {
            const double visual_scale = inline_text_visual_scale(*layer);
            layer->rich_text.typing_format =
                rich_text_format_from_qtext_format(
                    cursor.charFormat(), layer->rich_text.default_format,
                    visual_scale);
            layer->rich_text.typing_format_mask =
                cursor.charFormat().hasProperty(RichTextPropManualMask)
                    ? static_cast<uint32_t>(cursor.charFormat()
                                                .property(RichTextPropManualMask)
                                                .toUInt()) &
                          RichTextCharAll
                    : rich_text_char_format_difference_mask(
                          layer->rich_text.typing_format,
                          layer->rich_text.default_format);
            layer->rich_text.has_typing_format = true;
        } else {
            layer->rich_text.has_typing_format = false;
            layer->rich_text.typing_format_mask = 0;
        }
        return selection_changed;
    }

    /* Normal typing, backspace, Enter and one-character replacement arrive as
     * one precise QTextDocument range edit. Read only the changed fragment and
     * mutate the canonical UTF-8 model directly. This avoids three full-string
     * conversions, a complete fragment walk, and redundant pre-normalization
     * for every key press. Rich paste/IME/format batches retain the full model
     * conversion fallback below. */
    const bool simple_range_edit =
        editor_doc && inline_text_change_count_ == 1 &&
        inline_text_change_position_ >= 0 &&
        inline_text_change_removed_ >= 0 && inline_text_change_added_ >= 0 &&
        (inline_text_change_removed_ > 0 || inline_text_change_added_ > 0) &&
        inline_text_change_added_ <= 2 && !cursor.hasSelection();
    if (simple_range_edit) {
        const std::string &old_text = layer->rich_text.plain_text;
        const int old_position = std::max(0, inline_text_change_position_);
        const int old_end = old_position + inline_text_change_removed_;
        const size_t byte_position =
            rich_text_utf8_byte_offset_from_utf16_position(old_text,
                                                            old_position);
        const size_t removed_end =
            rich_text_utf8_byte_offset_from_utf16_position(old_text, old_end);

        QString inserted_qtext;
        if (inline_text_change_added_ > 0) {
            const int document_end =
                std::max(0, editor_doc->characterCount() - 1);
            const int insertion_start = std::clamp(
                inline_text_change_position_, 0, document_end);
            const int insertion_end = std::clamp(
                insertion_start + inline_text_change_added_, insertion_start,
                document_end);
            QTextCursor changed_cursor(editor_doc);
            changed_cursor.setPosition(insertion_start);
            changed_cursor.setPosition(insertion_end,
                                       QTextCursor::KeepAnchor);
            inserted_qtext = changed_cursor.selectedText();
            inserted_qtext.replace(QChar::ParagraphSeparator, QChar('\n'));
            inserted_qtext.replace(QChar::LineSeparator, QChar('\n'));
        }
        const std::string inserted_text =
            inserted_qtext.toUtf8().toStdString();

        const double visual_scale = inline_text_visual_scale(*layer);
        const RichTextCharFormat insertion_format =
            rich_text_format_from_qtext_format(
                cursor.charFormat(), layer->rich_text.default_format,
                visual_scale);
        const uint32_t insertion_mask =
            cursor.charFormat().hasProperty(RichTextPropManualMask)
                ? static_cast<uint32_t>(cursor.charFormat()
                                            .property(RichTextPropManualMask)
                                            .toUInt()) &
                      RichTextCharAll
                : rich_text_char_format_difference_mask(
                      insertion_format, layer->rich_text.default_format);

        rich_text_document_replace_canonical_range(
            layer->rich_text, byte_position, removed_end - byte_position,
            inserted_text, insertion_format, insertion_mask);
        layer->text_content = layer->rich_text.plain_text;
        layer->rich_text.selection = {
            rich_text_utf8_byte_offset_from_utf16_position(
                layer->rich_text.plain_text, std::max(0, cursor.anchor())),
            rich_text_utf8_byte_offset_from_utf16_position(
                layer->rich_text.plain_text, std::max(0, cursor.position()))};
        layer->rich_text.typing_format = insertion_format;
        layer->rich_text.typing_format_mask = insertion_mask;
        layer->rich_text.has_typing_format = true;
        editor_doc->setModified(false);
        inline_text_change_count_ = 0;
        if (mark_dirty)
            dirty_ = true;
        return true;
    }

    /* Complex paste, IME and explicit rich-format changes can alter multiple
     * QTextDocument fragments. Preserve exact formatting through the complete
     * conversion path, but execute it only for those non-keystroke edits. */
    const QString qplain = inline_text_editor_->toPlainText();
    const std::string plain = qplain.toUtf8().toStdString();
    const double visual_scale = inline_text_visual_scale(*layer);
    RichTextDocument next_model = rich_text_document_from_qtext_document(
        editor_doc, *layer, visual_scale, cursor);
    const bool selection_changed =
        layer->rich_text.selection.anchor != next_model.selection.anchor ||
        layer->rich_text.selection.head != next_model.selection.head;
    const bool changed =
        layer->text_content != plain ||
        layer->rich_text.plain_text != next_model.plain_text ||
        !rich_text_char_formats_equal(layer->rich_text.default_format,
                                      next_model.default_format) ||
        layer->rich_text.default_paragraph_format.align_h !=
            next_model.default_paragraph_format.align_h ||
        layer->rich_text.default_paragraph_format.align_v !=
            next_model.default_paragraph_format.align_v ||
        std::abs(layer->rich_text.default_paragraph_format.indent_left -
                 next_model.default_paragraph_format.indent_left) >= 0.0001f ||
        std::abs(layer->rich_text.default_paragraph_format.indent_right -
                 next_model.default_paragraph_format.indent_right) >= 0.0001f ||
        std::abs(layer->rich_text.default_paragraph_format.indent_first_line -
                 next_model.default_paragraph_format.indent_first_line) >=
            0.0001f ||
        std::abs(layer->rich_text.default_paragraph_format.line_spacing -
                 next_model.default_paragraph_format.line_spacing) >= 0.0001f ||
        std::abs(layer->rich_text.default_paragraph_format.space_before -
                 next_model.default_paragraph_format.space_before) >= 0.0001f ||
        std::abs(layer->rich_text.default_paragraph_format.space_after -
                 next_model.default_paragraph_format.space_after) >= 0.0001f ||
        layer->rich_text.default_paragraph_format.hyphenate !=
            next_model.default_paragraph_format.hyphenate ||
        layer->rich_text.has_typing_format != next_model.has_typing_format ||
        layer->rich_text.typing_format_mask != next_model.typing_format_mask ||
        (layer->rich_text.has_typing_format &&
         rich_text_char_format_difference_mask(
             layer->rich_text.typing_format, next_model.typing_format,
             layer->rich_text.typing_format_mask |
                 next_model.typing_format_mask) != 0) ||
        !rich_text_blocks_equal(layer->rich_text.blocks, next_model.blocks) ||
        !rich_text_ranges_equal(layer->rich_text.ranges, next_model.ranges);
    if (!changed) {
        if (selection_changed)
            layer->rich_text.selection = next_model.selection;
        inline_text_change_count_ = 0;
        if (editor_doc)
            editor_doc->setModified(false);
        return false;
    }

    /* Keep per-layer automatic styling metadata when committing the inline
     * QTextEdit back into the model. rich_text_document_from_qtext_document()
     * describes visible/manual rich text only. */
    const bool auto_style_enabled = layer->rich_text.auto_style_enabled;
    const std::string auto_default_style_preset_id =
        layer->rich_text.auto_default_style_preset_id;
    const RichTextCharFormat auto_default_style_cached_format =
        layer->rich_text.auto_default_style_cached_format;
    const uint32_t auto_default_style_cached_mask =
        layer->rich_text.auto_default_style_cached_mask;
    const std::vector<RichTextAutoStyleRule> auto_style_rules =
        layer->rich_text.auto_style_rules;

    layer->text_content = plain;
    layer->rich_text = std::move(next_model);
    layer->rich_text.auto_style_enabled = auto_style_enabled;
    layer->rich_text.auto_default_style_preset_id =
        auto_default_style_preset_id;
    layer->rich_text.auto_default_style_cached_format =
        auto_default_style_cached_format;
    layer->rich_text.auto_default_style_cached_mask =
        auto_default_style_cached_mask;
    layer->rich_text.auto_style_rules = auto_style_rules;
    rich_text_document_sync_layer_mirrors_canonical(*layer);
    if (editor_doc)
        editor_doc->setModified(false);
    inline_text_change_count_ = 0;
    if (mark_dirty)
        dirty_ = true;
    return true;
}

void CanvasPreview::schedule_inline_text_refresh(bool mark_dirty,
                                                 bool emit_changed)
{
    inline_text_refresh_mark_dirty_ =
        inline_text_refresh_mark_dirty_ || mark_dirty;
    inline_text_refresh_emit_changed_ =
        inline_text_refresh_emit_changed_ || emit_changed;
    if (inline_text_refresh_timer_) {
        if (!inline_text_refresh_timer_->isActive())
            inline_text_refresh_timer_->start();
    } else {
        const bool dirty = inline_text_refresh_mark_dirty_;
        const bool changed = inline_text_refresh_emit_changed_;
        inline_text_refresh_mark_dirty_ = false;
        inline_text_refresh_emit_changed_ = false;
        refresh_inline_text_edit(dirty, changed);
    }
}

void CanvasPreview::refresh_inline_text_edit(bool mark_dirty, bool emit_changed)
{
    if (committing_inline_text_ || updating_inline_text_editor_ || refreshing_inline_text_ ||
        !inline_text_editor_ || inline_text_layer_id_.empty())
        return;

    refreshing_inline_text_ = true;
    const std::string layer_id = inline_text_layer_id_;
    const bool model_changed = sync_inline_text_layer(mark_dirty);

    bool geometry_changed = false;
    if (auto layer = title_ ? title_->find_layer(layer_id) : nullptr) {
        if ((layer->text_box_width_to_text || layer->text_box_height_to_text) &&
            inline_text_editor_) {
            /* Preserve the pre-12D point-text editing contract. The live
             * QTextDocument may grow beyond the current box while typing; the
             * box then follows those unconstrained metrics. */
            QTextDocument *doc = inline_text_editor_->document();
            const double visual_scale =
                std::max(0.0001, inline_text_visual_scale(*layer));
            QSizeF doc_size;
            if (doc) {
                if (auto *layout = doc->documentLayout())
                    doc_size = layout->documentSize();
                if (!doc_size.isValid() || doc_size.isEmpty())
                    doc_size = doc->size();
            }
            QFontMetricsF metrics(inline_text_editor_->currentFont());
            const double min_w = std::max(
                24.0, metrics.horizontalAdvance(fxm_tr("OBSTitles.M")) /
                          visual_scale);
            const double min_h =
                std::max(12.0, metrics.lineSpacing() / visual_scale);
            if (layer->text_box_width_to_text) {
                const double ideal = doc
                    ? std::max(doc->idealWidth(), doc_size.width())
                    : 0.0;
                const double next_w = std::clamp(
                    ideal / visual_scale + 2.0, min_w,
                    static_cast<double>(std::max(
                        1.0f, layer->max_text_box_width)));
                if (std::abs(layer->rect_width - next_w) > 0.5) {
                    layer->rect_width = static_cast<float>(next_w);
                    layer->size.static_value.x = next_w;
                    geometry_changed = true;
                }
            }
            if (layer->text_box_height_to_text) {
                const double next_h = std::clamp(
                    doc_size.height() / visual_scale + 2.0, min_h,
                    static_cast<double>(std::max(
                        1.0f, layer->max_text_box_height)));
                if (std::abs(layer->rect_height - next_h) > 0.5) {
                    layer->rect_height = static_cast<float>(next_h);
                    layer->size.static_value.y = next_h;
                    geometry_changed = true;
                }
            }
        }
    }

    if (mark_dirty || model_changed || geometry_changed) {
        dirty_ = true;
        gpu_model_dirty_ = true;

        /* A text keystroke is an explicit editor-frame request. Merely
         * invalidating the QTextEdit rectangle can leave the OBS swapchain's
         * artwork texture on the previous model until the next unrelated
         * repaint, which is especially visible on Linux/Wayland. Cancel any
         * queued transport classification and authorize one immediate full
         * canvas present for the newly committed canonical text model. */
        playback_frame_pending_ = false;
        playback_present_pending_ = false;
        editing_present_pending_ = true;
        force_present_pending_ = true;
        if (render_coalesce_timer_)
            render_coalesce_timer_->stop();
        if (present_coalesce_timer_)
            present_coalesce_timer_->stop();
    }

    /* QTextEdit already maintains glyph/cursor layout for a text-only edit.
     * Reconfiguring and reformatting the whole document on every keystroke was
     * redundant. Reposition only when an auto-sized box actually changed. */
    if (geometry_changed)
        position_text_editor();

    /* Preserve the pre-12D inline-edit presentation semantics. Text edits and
     * the delayed document-size notification must publish the expanded box in
     * the same edit transaction; cursor/selection-only changes still bypass
     * this function and remain overlay-only. */
    invalidate_canvas_overlay_caches();
    if (inline_text_editor_) {
        /* Artwork is presented by a full-widget GPU swapchain; request the
         * whole widget rather than only the child editor rectangle. */
        update();
        inline_text_editor_->update();
        inline_text_editor_->viewport()->update();
    } else {
        update();
    }

    refreshing_inline_text_ = false;

    if (emit_changed && (mark_dirty || model_changed))
        emit text_edit_changed(layer_id);
}

QRectF CanvasPreview::inline_text_document_local_rect(const Layer &layer) const
{
    const QRectF local = layer_local_rect(layer);
    const QRectF text_rect = text_rect_for_style(local, layer);

    const double visual_scale = inline_text_visual_scale(layer);
    const double local_time = std::max(0.0, playhead_ - layer.in_time);
    QFont font = font_for_layer(layer, local_time);
    if (font.pixelSize() > 0)
        font.setPixelSize(std::max(1, (int)std::round(font.pixelSize() * visual_scale)));

    QTextDocument measure_doc;
    measure_doc.setDocumentMargin(0.0);
    measure_doc.setDefaultFont(font);

    QTextOption option = measure_doc.defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(layer.text_overflow_mode == 0
                           ? (layer.paragraph_hyphenate ? QTextOption::WrapAnywhere
                                                         : QTextOption::WrapAtWordBoundaryOrAnywhere)
                           : QTextOption::NoWrap);
    Qt::Alignment align = Qt::AlignLeft;
    if (layer.align_h == 1 || layer.align_h == 4) align = Qt::AlignHCenter;
    else if (layer.align_h == 2 || layer.align_h == 5) align = Qt::AlignRight;
    else if (layer.align_h >= 3) align = Qt::AlignJustify;
    option.setAlignment(align);
    measure_doc.setDefaultTextOption(option);

    const int wrap_width_px = std::max(1, (int)std::ceil(text_rect.width() * visual_scale));
    measure_doc.setTextWidth(layer.text_overflow_mode == 2 ? -1.0 : (qreal)wrap_width_px);
    measure_doc.setPageSize(layer.text_overflow_mode == 2
                                ? QSizeF(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)
                                : QSizeF(wrap_width_px, QWIDGETSIZE_MAX));

    const RichTextDocument editor_model =
        rich_text_document_for_editor_time(layer, local_time);
    populate_qtext_document_from_rich_text(&measure_doc, editor_model, visual_scale);
    RichTextParagraphFormat paragraph_base = editor_model.default_paragraph_format;
    paragraph_base.align_h = layer.align_h;
    paragraph_base.align_v = layer.align_v;
    paragraph_base.indent_left = (float)eval_paragraph_indent_left(layer, local_time);
    paragraph_base.indent_right = (float)eval_paragraph_indent_right(layer, local_time);
    paragraph_base.indent_first_line = (float)eval_paragraph_indent_first_line(layer, local_time);
    paragraph_base.line_spacing = layer.text_leading;
    paragraph_base.space_before = (float)eval_paragraph_space_before(layer, local_time);
    paragraph_base.space_after = (float)eval_paragraph_space_after(layer, local_time);
    paragraph_base.hyphenate = layer.paragraph_hyphenate;
    apply_rich_text_paragraph_blocks_to_qtext_document(&measure_doc, editor_model,
                                                        paragraph_base, visual_scale);
    apply_inline_text_vertical_distribute(measure_doc, layer, text_rect, visual_scale);

    QSizeF doc_size;
    if (auto *layout = measure_doc.documentLayout()) {
        doc_size = layout->documentSize();
        if (!doc_size.isValid() || doc_size.isEmpty())
            doc_size = measure_doc.size();
    }

    const double doc_width = layer.text_overflow_mode == 2 && doc_size.width() > 0.0
                                 ? doc_size.width() / std::max(0.0001, visual_scale)
                                 : text_rect.width();
    const double doc_height = doc_size.height() > 0.0
                                  ? doc_size.height() / std::max(0.0001, visual_scale)
                                  : text_rect.height();

    double y = text_rect.top();
    if (layer.align_v == 1)
        y = text_rect.top() + (text_rect.height() - doc_height) / 2.0;
    else if (layer.align_v == 2)
        y = text_rect.bottom() - doc_height;
    y -= eval_baseline_shift(layer, local_time);

    double x = text_rect.left();
    if (layer.text_overflow_mode == 2 && doc_width < text_rect.width()) {
        if (layer.align_h == 1 || layer.align_h == 4)
            x = text_rect.left() + (text_rect.width() - doc_width) / 2.0;
        else if (layer.align_h == 2 || layer.align_h == 5)
            x = text_rect.right() - doc_width;
    }

    return QRectF(x, y, std::max(1.0, doc_width), std::max(1.0, doc_height));
}


std::vector<QPolygonF> CanvasPreview::inline_text_selection_view_polygons() const
{
    std::vector<QPolygonF> polygons;
    if (!title_ || inline_text_layer_id_.empty() || !inline_text_editor_ ||
        !inline_text_editor_->isVisible())
        return polygons;
    const auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer || layer->rich_text.selection.anchor ==
                      layer->rich_text.selection.head)
        return polygons;

    const QRectF text_rect = text_rect_for_style(layer_local_rect(*layer), *layer);
    const double local_time = std::max(0.0, playhead_ - layer->in_time);
    const ImmutableTextLayout layout = editor_text_layout_for_metrics(
        *layer, local_time, text_rect.width(), layer->text_overflow_mode,
        text_rect.height());
    if (!layout || !layout->valid)
        return polygons;

    const size_t start = std::min(layer->rich_text.selection.anchor,
                                  layer->rich_text.selection.head);
    const size_t end = std::max(layer->rich_text.selection.anchor,
                                layer->rich_text.selection.head);
    for (const TextLayoutRect &geometry :
         text_layout_selection_rects(*layout, start, end)) {
        QRectF rect(text_rect.x() + geometry.x,
                    text_rect.y() + geometry.y,
                    geometry.width, geometry.height);
        if (!rect.isValid() || rect.isEmpty())
            continue;
        QPolygonF polygon;
        polygon << canvas_to_view(layer_to_canvas(*layer, rect.topLeft()))
                << canvas_to_view(layer_to_canvas(*layer, rect.topRight()))
                << canvas_to_view(layer_to_canvas(*layer, rect.bottomRight()))
                << canvas_to_view(layer_to_canvas(*layer, rect.bottomLeft()));
        polygons.push_back(std::move(polygon));
    }
    return polygons;
}

bool CanvasPreview::inline_text_caret_view_polygon(QPolygonF &polygon) const
{
    polygon.clear();
    if (!title_ || inline_text_layer_id_.empty() || !inline_text_editor_ ||
        !inline_text_editor_->isVisible())
        return false;
    const auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer || layer->rich_text.selection.anchor !=
                      layer->rich_text.selection.head)
        return false;

    const QRectF text_rect = text_rect_for_style(layer_local_rect(*layer), *layer);
    const double local_time = std::max(0.0, playhead_ - layer->in_time);
    const ImmutableTextLayout layout = editor_text_layout_for_metrics(
        *layer, local_time, text_rect.width(), layer->text_overflow_mode,
        text_rect.height());
    if (!layout || !layout->valid)
        return false;

    TextLayoutRect geometry;
    const float local_caret_width = static_cast<float>(
        std::max(0.75, 1.5 / std::max(0.01, view_scale())));
    if (!text_layout_caret_rect(*layout, layer->rich_text.selection.head,
                                local_caret_width, geometry))
        return false;
    QRectF rect(text_rect.x() + geometry.x,
                text_rect.y() + geometry.y,
                geometry.width, geometry.height);
    if (!rect.isValid() || rect.isEmpty())
        return false;
    polygon << canvas_to_view(layer_to_canvas(*layer, rect.topLeft()))
            << canvas_to_view(layer_to_canvas(*layer, rect.topRight()))
            << canvas_to_view(layer_to_canvas(*layer, rect.bottomRight()))
            << canvas_to_view(layer_to_canvas(*layer, rect.bottomLeft()));
    return true;
}

void CanvasPreview::position_text_editor()
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || !title_) return;
    auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer) {
        inline_text_editor_view_rect_ = QRect();
        inline_text_editor_->hide();
        return;
    }

    const double visual_scale = inline_text_visual_scale(*layer);
    const bool was_updating_inline_text_editor = updating_inline_text_editor_;
    updating_inline_text_editor_ = true;
    configure_inline_text_editor(*layer);
    {
        QSignalBlocker blocker(inline_text_editor_);
        const QTextCursor saved_cursor = inline_text_editor_->textCursor();
        const int anchor = saved_cursor.anchor();
        const int position = saved_cursor.position();
        const QString layer_plain = !layer->rich_text.empty()
                                        ? QString::fromStdString(layer->rich_text.plain_text)
                                        : QString::fromStdString(layer->text_content);
        const bool scale_changed = std::abs(inline_text_last_visual_scale_ - visual_scale) > 0.001;
        const bool text_changed_externally = inline_text_editor_->toPlainText() != layer_plain;
        /* Formatting changes are pushed explicitly from the canonical model.
         * Rebuilding on every position pass destroyed active mouse selections. */
        if (scale_changed || text_changed_externally) {
            {
                const double local_time = std::max(0.0, playhead_ - layer->in_time);
                const RichTextDocument editor_model =
                    rich_text_document_for_editor_time(*layer, local_time);
                populate_qtext_document_from_rich_text(inline_text_editor_->document(),
                                                       editor_model, visual_scale);
            }
            inline_text_last_visual_scale_ = visual_scale;
            QTextCursor restored(inline_text_editor_->document());
            const int text_len = inline_text_editor_->toPlainText().size();
            restored.setPosition(std::clamp(anchor, 0, text_len));
            restored.setPosition(std::clamp(position, 0, text_len), QTextCursor::KeepAnchor);
            inline_text_editor_->setTextCursor(restored);
            if (auto *doc = inline_text_editor_->document())
                doc->setModified(false);
        }
        if (auto *doc = inline_text_editor_->document())
            if (auto *layout = doc->documentLayout())
                layout->documentSize();
    }
    updating_inline_text_editor_ = was_updating_inline_text_editor;

    const QRectF document_rect = inline_text_document_local_rect(*layer);
    QPolygonF poly;
    poly << canvas_to_view(layer_to_canvas(*layer, document_rect.topLeft()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.topRight()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.bottomRight()))
         << canvas_to_view(layer_to_canvas(*layer, document_rect.bottomLeft()));
    QRectF bounds = poly.boundingRect();
    const int left = (int)std::floor(bounds.left());
    const int top = (int)std::floor(bounds.top());
    const int right = (int)std::ceil(bounds.right());
    const int bottom = (int)std::ceil(bounds.bottom());
    inline_text_editor_view_rect_ =
        QRect(left, top, std::max(1, right - left),
              std::max(1, bottom - top));

    /* A transparent QWidget cannot be composited correctly over the native
     * OBS/D3D surface: Windows fills its native child surface with black (or
     * stale backing-store pixels). Preserve the adapter's logical size for
     * wrapping/IME, but place it wholly outside the parent's clipped client
     * area. The GPU canvas remains the only visible renderer. */
    inline_text_editor_->setGeometry(
        QRect(width() + 64, height() + 64,
              inline_text_editor_view_rect_.width(),
              inline_text_editor_view_rect_.height()));
}

bool CanvasPreview::inline_text_view_point_hits_document(
    const QPointF &view_point) const
{
    if (!title_ || inline_text_layer_id_.empty() || !inline_text_editor_ ||
        !inline_text_editor_->isVisible())
        return false;
    const auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer)
        return false;
    const QRectF document_rect = inline_text_document_local_rect(*layer);
    return document_rect.contains(
        canvas_to_layer(*layer, view_to_canvas(view_point)));
}

bool CanvasPreview::set_inline_text_cursor_from_view(
    const QPointF &view_point, bool keep_anchor, bool select_word)
{
    if (!title_ || inline_text_layer_id_.empty() || !inline_text_editor_ ||
        !inline_text_editor_->isVisible())
        return false;
    const auto layer = title_->find_layer(inline_text_layer_id_);
    if (!layer)
        return false;

    QTextDocument *document = inline_text_editor_->document();
    QAbstractTextDocumentLayout *layout =
        document ? document->documentLayout() : nullptr;
    if (!layout)
        return false;

    const QRectF document_rect = inline_text_document_local_rect(*layer);
    const QPointF layer_point =
        canvas_to_layer(*layer, view_to_canvas(view_point));
    const double visual_scale = inline_text_visual_scale(*layer);
    const QPointF document_point(
        (layer_point.x() - document_rect.left()) * visual_scale,
        (layer_point.y() - document_rect.top()) * visual_scale);
    int position = layout->hitTest(document_point, Qt::FuzzyHit);
    if (position < 0)
        return false;
    position = std::clamp(position, 0,
                          std::max(0, document->characterCount() - 1));

    QTextCursor cursor = inline_text_editor_->textCursor();
    cursor.setPosition(position, keep_anchor ? QTextCursor::KeepAnchor
                                             : QTextCursor::MoveAnchor);
    if (select_word)
        cursor.select(QTextCursor::WordUnderCursor);
    inline_text_editor_->setTextCursor(cursor);
    inline_text_editor_->setFocus(Qt::MouseFocusReason);
    return true;
}

void CanvasPreview::begin_text_edit(const std::shared_ptr<Layer> &layer)
{
    if (!layer || !inline_text_editor_) return;
    if (!inline_text_layer_id_.empty() && inline_text_layer_id_ != layer->id)
        commit_text_edit(true);

    inline_text_layer_id_ = layer->id;
    inline_text_suspended_for_gradient_ = false;
    inline_text_mouse_selecting_ = false;
    rich_text_document_ensure_canonical(*layer);
    updating_inline_text_editor_ = true;
    QSignalBlocker blocker(inline_text_editor_);
    configure_inline_text_editor(*layer);
    const double visual_scale = inline_text_visual_scale(*layer);
    const double local_time = std::max(0.0, playhead_ - layer->in_time);
    const RichTextDocument editor_model =
        rich_text_document_for_editor_time(*layer, local_time);
    populate_qtext_document_from_rich_text(inline_text_editor_->document(),
                                           editor_model, visual_scale);
    if (inline_text_editor_->toPlainText().isEmpty())
        inline_text_editor_->setCurrentCharFormat(
            qtext_format_from_rich_text_format(
                rich_text_effective_typing_format(editor_model), visual_scale));
    inline_text_last_visual_scale_ = visual_scale;

    QTextCursor cursor = inline_text_editor_->textCursor();
    cursor.select(QTextCursor::Document);
    inline_text_editor_->setTextCursor(cursor);
    if (!layer->rich_text.empty()) {
        layer->rich_text.selection.anchor = 0;
        layer->rich_text.selection.head = layer->rich_text.plain_text.size();
    }
    position_text_editor();
    updating_inline_text_editor_ = false;
    inline_text_editor_->show();
    inline_text_editor_->raise();
    inline_text_editor_->setFocus(Qt::MouseFocusReason);
    if (auto *doc = inline_text_editor_->document())
        doc->setModified(false);
    emit text_edit_cursor_changed(layer->id);
    dirty_ = true;
    update();
}

void CanvasPreview::suspend_inline_text_edit_for_gradient()
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() ||
        inline_text_suspended_for_gradient_)
        return;

    /* Persist the QTextEdit cursor/selection and any pending text changes into
     * the canonical rich-text model, but keep the edit session alive. Hiding
     * the adapter lets the canvas receive gradient-tool mouse events without
     * converting the selected range into an object-level edit. */
    sync_inline_text_layer(false);
    inline_text_suspended_for_gradient_ = true;
    inline_text_mouse_selecting_ = false;
    inline_text_editor_->hide();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::resume_inline_text_edit_after_gradient()
{
    if (!inline_text_editor_ || inline_text_layer_id_.empty() ||
        !inline_text_suspended_for_gradient_)
        return;

    /* Gradient drags write directly to the canonical rich-text model.  The
     * hidden QTextEdit still contains its pre-drag formatting, so merely
     * showing it again would let the next text sync overwrite the gradient
     * edit.  Rebuild the adapter once from the canonical model and preserve
     * the user's cursor/selection. */
    if (title_) {
        if (auto layer = title_->find_layer(inline_text_layer_id_)) {
            const QTextCursor saved_cursor = inline_text_editor_->textCursor();
            const int anchor = saved_cursor.anchor();
            const int position = saved_cursor.position();
            const bool was_updating_inline_text_editor = updating_inline_text_editor_;
            updating_inline_text_editor_ = true;
            {
                QSignalBlocker blocker(inline_text_editor_);
                configure_inline_text_editor(*layer);
                const double visual_scale = inline_text_visual_scale(*layer);
                const double local_time = std::max(0.0, playhead_ - layer->in_time);
                const RichTextDocument editor_model =
                    rich_text_document_for_editor_time(*layer, local_time);
                populate_qtext_document_from_rich_text(inline_text_editor_->document(),
                                                       editor_model, visual_scale);
                inline_text_last_visual_scale_ = visual_scale;

                QTextCursor restored(inline_text_editor_->document());
                const int text_len = inline_text_editor_->toPlainText().size();
                restored.setPosition(std::clamp(anchor, 0, text_len));
                restored.setPosition(std::clamp(position, 0, text_len),
                                     QTextCursor::KeepAnchor);
                inline_text_editor_->setTextCursor(restored);
                if (auto *doc = inline_text_editor_->document())
                    doc->setModified(false);
            }
            updating_inline_text_editor_ = was_updating_inline_text_editor;
        }
    }

    inline_text_suspended_for_gradient_ = false;
    position_text_editor();
    inline_text_editor_->show();
    inline_text_editor_->raise();
    inline_text_editor_->setFocus(Qt::OtherFocusReason);
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::commit_text_edit(bool accept_changes, bool emit_commit_signal)
{
    if (committing_inline_text_ || !inline_text_editor_ || inline_text_layer_id_.empty()) return;
    committing_inline_text_ = true;
    if (inline_text_refresh_timer_)
        inline_text_refresh_timer_->stop();
    inline_text_refresh_mark_dirty_ = false;
    inline_text_refresh_emit_changed_ = false;
    const std::string layer_id = inline_text_layer_id_;

    /* Suspension flushes all pending QTextEdit changes before the gradient tool
     * takes control, after which gradient edits are written directly to the
     * canonical model.  Syncing the hidden, pre-drag adapter here would undo
     * those edits when the user leaves the gradient tool without first
     * returning to text editing. */
    if (accept_changes && !inline_text_suspended_for_gradient_)
        sync_inline_text_layer(true);

    inline_text_layer_id_.clear();
    inline_text_suspended_for_gradient_ = false;
    inline_text_mouse_selecting_ = false;
    inline_text_editor_view_rect_ = QRect();
    inline_text_last_visual_scale_ = 0.0;
    inline_text_change_count_ = 0;
    inline_text_editor_->hide();
    {
        updating_inline_text_editor_ = true;
        QSignalBlocker blocker(inline_text_editor_);
        inline_text_editor_->clear();
        inline_text_editor_->setCurrentCharFormat(QTextCharFormat());
        inline_text_editor_->mergeCurrentCharFormat(QTextCharFormat());
        updating_inline_text_editor_ = false;
    }
    committing_inline_text_ = false;
    dirty_ = true;
    update();
    FXM_LOG_DEBUG("Text", QStringLiteral(
        "Committed inline text edit title=%1 layer=%2 accepted=%3")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(QString::fromStdString(layer_id))
        .arg(accept_changes ? 1 : 0));
    if (emit_commit_signal)
        emit text_edit_committed(layer_id);
}

bool CanvasPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == inline_text_editor_) {
        if (event->type() == QEvent::FocusOut) {
            return false;
        }
        if (event->type() == QEvent::KeyPress) {
            auto *key_event = static_cast<QKeyEvent *>(event);
            const Qt::KeyboardModifiers history_modifiers =
                key_event->modifiers() &
                (Qt::ControlModifier | Qt::ShiftModifier |
                 Qt::AltModifier | Qt::MetaModifier);
            const bool undo_shortcut = key_event->key() == Qt::Key_Z &&
                history_modifiers == Qt::ControlModifier;
            const bool redo_shortcut =
                (key_event->key() == Qt::Key_Z &&
                 history_modifiers ==
                     (Qt::ControlModifier | Qt::ShiftModifier)) ||
                (key_event->key() == Qt::Key_Y &&
                 history_modifiers == Qt::ControlModifier);
            if (undo_shortcut && inline_text_editor_->document() &&
                inline_text_editor_->document()->availableUndoSteps() == 0) {
                emit title_undo_requested();
                key_event->accept();
                return true;
            }
            if (redo_shortcut && inline_text_editor_->document() &&
                inline_text_editor_->document()->availableRedoSteps() == 0) {
                emit title_redo_requested();
                key_event->accept();
                return true;
            }
            auto apply_canonical_char_format = [this](uint32_t mask, auto mutate) {
                if (!title_ || inline_text_layer_id_.empty()) return;
                const std::string layer_id = inline_text_layer_id_;
                emit text_property_change_started(layer_id);
                sync_inline_text_layer(false);
                auto layer = title_->find_layer(layer_id);
                if (!layer) return;
                RichTextCharFormatSummary summary = summarize_rich_text_char_format(
                    *layer, true, std::max(0.0, playhead_ - layer->in_time));
                RichTextCharFormat format = summary.valid
                    ? summary.format
                    : rich_text_effective_typing_format(layer->rich_text);
                mutate(format, (summary.mixed & mask) != 0);
                apply_rich_text_format_to_layer_range(*layer, format, mask, true);
                apply_active_text_char_format(layer->id, format, mask);
                dirty_ = true;
                /* Formatting is a canonical property transaction, not a text
                 * insertion transaction. Do not emit text_edit_changed here:
                 * that signal intentionally marks unsnapshotted typing and
                 * would cancel the property transaction before its post-state
                 * enters title-level Undo/Redo. */
                emit text_property_change_committed(layer->id);
            };
            if (key_event->key() == Qt::Key_B && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                apply_canonical_char_format(RichTextCharBold,
                    [](RichTextCharFormat &format, bool mixed) {
                        format.bold = mixed ? true : !format.bold;
                    });
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_I && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                apply_canonical_char_format(RichTextCharItalic,
                    [](RichTextCharFormat &format, bool mixed) {
                        format.italic = mixed ? true : !format.italic;
                    });
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_U && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                apply_canonical_char_format(RichTextCharUnderline,
                    [](RichTextCharFormat &format, bool mixed) {
                        format.underline = mixed ? true : !format.underline;
                    });
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_Escape) {
                commit_text_edit(true);
                key_event->accept();
                return true;
            }
            if (key_event->key() == Qt::Key_Return && key_event->modifiers().testFlag(Qt::ControlModifier)) {
                commit_text_edit(true);
                key_event->accept();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
