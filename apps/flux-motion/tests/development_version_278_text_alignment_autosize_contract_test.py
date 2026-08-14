from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_version_and_gpu_cache_revision_are_278():
    assert 'set(OBS_FXM_DEVELOPMENT_VERSION "278")' in read('CMakeLists.txt')
    assert '#define FXM_DEVELOPMENT_VERSION "278"' in read('src/core/build-info.h')
    assert '|gpu-text-pipeline=278' in read(
        '../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc'
    )


def test_h_scale_alignment_uses_final_scaled_line_geometry():
    source = read('src/text/title-text-layout-qt.cpp')
    assert 'option.setAlignment(Qt::AlignLeft);' in source
    assert 'apply_line_horizontal_character_scale(\n                *data, line, request.document);' in source
    assert 'align_scaled_line(*data, line, left_indent + line_indent,' in source
    assert source.index('apply_line_horizontal_character_scale(') < source.index(
        'static void align_scaled_line('
    )
    build_call = source.index(
        'apply_line_horizontal_character_scale(\n                *data, line, request.document);'
    )
    align_call = source.index(
        'align_scaled_line(*data, line, left_indent + line_indent,',
        build_call,
    )
    assert build_call < align_call
    assert 'target_x += (available_width - line.width) * 0.5f;' in source
    assert 'target_x += available_width - line.width;' in source
    assert 'justify_line_to_width(data, line, available_width);' in source
    assert 'anchor_shift = (old_left + old_right)' not in source


def test_justify_expands_selection_geometry_not_glyph_outlines():
    source = read('src/text/title-text-layout-qt.cpp')
    assert 'cluster_is_expandable_space' in source
    assert 'cursor.x = new_x + relative * new_width;' in source
    assert 'glyph.x += accumulated;' in source
    assert 'glyph.scale_x +=' not in source
    assert 'line.width = target_width;' in source


def test_manual_bounding_box_resize_disables_matching_auto_size_axis():
    source = read('src/canvas/canvas-preview/pointer-events.inc')
    assert 'disable_text_auto_size_for_manual_resize' in source
    assert 'if (width_changed && layer.text_box_width_to_text)' in source
    assert 'layer.text_box_width_to_text = false;' in source
    assert 'if (height_changed && layer.text_box_height_to_text)' in source
    assert 'layer.text_box_height_to_text = false;' in source
    # Both multi-selection and single-layer size-backed resize branches use it.
    assert source.count('disable_text_auto_size_for_manual_resize(') >= 2
    assert 'std::abs(resized_width - state.w) > 0.01f' in source
    assert 'std::abs(new_w - start_width) > 0.01' in source
