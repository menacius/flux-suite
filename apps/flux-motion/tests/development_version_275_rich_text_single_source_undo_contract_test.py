from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_development_version_and_gpu_cache_revision():
    cmake_match = re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', read("CMakeLists.txt"))
    header_match = re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', read("src/core/build-info.h"))
    assert cmake_match and int(cmake_match.group(1)) >= 275
    assert header_match and int(header_match.group(1)) >= 275
    assert '"|gpu-text-pipeline=276"' in read(
        "../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc"
    )


def test_gpu_layout_uses_canonical_effective_shaping_spans():
    source = read("src/text/title-text-layout-qt.cpp")
    assert "const auto string_indexes = glyph_run.stringIndexes();" in source
    assert "mapped.formats.push_back(" in source
    assert "rich_text_format_at(request.document, cluster_byte)" in source
    assert "same_shaping_format(" in source
    assert "glyph_format.scale_x" in source
    assert "glyph_format.scale_y" in source
    assert "apply_line_horizontal_character_scale" in source


def test_rich_text_is_static_property_source_and_scalars_are_mirrors():
    source = read("src/text/title-rich-text.cpp")
    assert "The RichTextDocument default is the sole authored static source" in source
    for prop in (
        "font_size_prop",
        "char_tracking_prop",
        "char_scale_x_prop",
        "char_scale_y_prop",
        "baseline_shift_prop",
    ):
        assert f"if (!layer.{prop}.is_animated())" in source

    controls = read("src/editor/properties-panel/construction-transform-character.inc")
    # Property controls may update animation tracks, but must not author both a
    # scalar field and the canonical rich-text document in the same callback.
    forbidden = (
        "layer_->font_size = v;",
        "layer_->char_tracking = (float)v;",
        "layer_->char_scale_x = (float)(v / 100.0);",
        "layer_->char_scale_y = (float)(v / 100.0);",
        "layer_->baseline_shift = (float)v;",
    )
    for statement in forbidden:
        assert statement not in controls
    assert "apply_rich_text_format_to_layer_range(*layer_, format, mask, active);" in controls


def test_multiple_properties_per_text_box_remain_sparse_and_composable():
    model = read("src/text/title-rich-text.cpp")
    adapters = read("src/editor/title-editor-internal/rich-text-model-adapters.inc")
    assert "range.mask & RichTextCharAll" in model
    assert "merge_char_format" in model
    assert "rich_text_document_apply_format" in adapters
    assert "doc.normalize();" in adapters
    # Object-level edits clear only the edited properties from inline ranges;
    # unrelated style attributes remain intact on the same text box/range.
    assert "rich_text_document_apply_default_char_format(doc, format, mask, true);" in adapters
    assert "range.mask &= ~mask;" in model


def test_undo_discards_stale_inline_adapter_before_snapshot_restore():
    header = read("src/canvas/canvas-preview.h")
    canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")
    inline = read("src/canvas/canvas-preview-inline-text.cpp")
    editor = read("src/editor/title-editor/layout-template-tools.inc")
    connections = read("src/editor/title-editor/document-shape-editing.inc")

    assert "bool commit_active_text = true" in header
    assert "bool emit_commit_signal = true" in header
    assert "commit_text_edit(false, false);" in canvas
    restore_start = editor.index("void TitleEditor::restore_undo_snapshot")
    restore_end = editor.index("void TitleEditor::update_undo_redo_actions", restore_start)
    restore = editor[restore_start:restore_end]
    assert "canvas_->set_title(title_, true, false);" in restore
    assert "canvas_->set_title(title_, true);" not in restore
    assert "canvas_->begin_text_edit_for_layer(active_text_edit_layer_id_);" in restore
    assert "availableUndoSteps() == 0" in inline
    assert "availableRedoSteps() == 0" in inline
    assert "emit title_undo_requested();" in inline
    assert "emit title_redo_requested();" in inline
    assert "CanvasPreview::title_undo_requested" in connections
    assert "CanvasPreview::title_redo_requested" in connections


def test_property_edit_checkpoints_typing_and_clears_adapter_only_undo():
    header = read("src/editor/properties-panel.h")
    controls = read("src/editor/properties-panel/construction-transform-character.inc")
    editor_header = read("src/editor/title-editor.h")
    commands = read("src/editor/title-editor/commands-docks.inc")
    changes = read("src/editor/title-editor/document-shape-editing.inc")
    canvas = read("src/canvas/canvas-preview/preview-cache-view.inc")

    assert "void text_property_change_started(const std::string &layer_id);" in header
    assert "emit text_property_change_started(layer_->id);" in controls
    assert "inline_text_changed_since_undo_snapshot_" in editor_header
    assert "push_undo_snapshot();" in commands
    assert "inline_text_changed_since_undo_snapshot_ = true;" in changes
    assert "adapter_document->setUndoRedoEnabled(false);" in canvas
    assert "adapter_document->setUndoRedoEnabled(undo_redo_enabled);" in canvas
