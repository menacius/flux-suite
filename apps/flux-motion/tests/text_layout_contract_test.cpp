#include "title-text-layout.h"

#include <cassert>
#include <iostream>

static TextLayoutRequest request_for(const std::string &text)
{
    TextLayoutRequest request;
    request.document.plain_text = text;
    request.document.default_format.font_family = "Inter";
    request.document.default_format.font_size = 48;
    request.document.normalize();
    request.max_width = 640.0f;
    request.max_height = 360.0f;
    return request;
}

static ImmutableTextLayout fake_builder(const TextLayoutRequest &request)
{
    auto layout = std::make_shared<TextLayoutData>();
    layout->key = text_layout_key(request);
    layout->text = request.document.plain_text;
    layout->valid = true;
    layout->width = request.max_width;
    layout->height = request.max_height;
    TextLayoutLine line;
    line.byte_start = 0;
    line.byte_length = layout->text.size();
    line.cluster_begin = 0;
    line.cluster_count = 2;
    line.height = 20.0f;
    layout->lines.push_back(line);
    layout->clusters.push_back({0, 1, 0, 1, 0, 0, 0.0f, 0.0f, 10.0f, 20.0f, false});
    layout->clusters.push_back({1, layout->text.size() - 1, 1, 1, 0, 0, 10.0f, 0.0f, 30.0f, 20.0f, false});
    return layout;
}

static void test_key_and_cache_contract()
{
    TextLayoutRequest first = request_for("AB");
    TextLayoutRequest same = first;
    TextLayoutRequest resized = first;
    resized.max_width = 320.0f;
    TextLayoutRequest restyled = first;
    restyled.document.default_format.tracking = 4.0f;

    assert(text_layout_key(first) == text_layout_key(same));
    assert(text_layout_request_equivalent(first, same));
    assert(!(text_layout_key(first) == text_layout_key(resized)));
    assert(!(text_layout_key(first) == text_layout_key(restyled)));

    TextLayoutRequest repainted = first;
    repainted.document.default_format.fill.color = 0xFFFF0000;
    repainted.document.default_format.underline = true;
    repainted.document.default_format.language = "Greek";
    assert(text_layout_key(first) == text_layout_key(repainted));
    assert(text_layout_request_equivalent(first, repainted));

    int builds = 0;
    TextLayoutCache cache(4);
    const TextLayoutBuilder builder = [&](const TextLayoutRequest &request) {
        ++builds;
        return fake_builder(request);
    };
    ImmutableTextLayout a = cache.get_or_build(first, builder);
    ImmutableTextLayout b = cache.get_or_build(same, builder);
    assert(a && b && a == b);
    assert(builds == 1);
    assert(cache.size() == 1);

    ImmutableTextLayout c = cache.get_or_build(resized, builder);
    assert(c && c != a);
    assert(builds == 2);

    a.reset();
    b.reset();
    ImmutableTextLayout persistent = cache.get_or_build(first, builder);
    assert(persistent);
    assert(builds == 2);

    ImmutableTextLayout paint_reused = cache.get_or_build(repainted, builder);
    assert(paint_reused == persistent);
    assert(builds == 2);
    const auto repainted_runs = text_layout_paint_runs(repainted.document);
    assert(repainted_runs.size() == 1);
    assert(repainted_runs[0].style.fill.color == 0xFFFF0000);
    assert(repainted_runs[0].style.underline);

    cache.clear();
    assert(cache.size() == 0);
}

static void test_hit_testing_and_range_bounds()
{
    ImmutableTextLayout layout = fake_builder(request_for("AΩ"));
    assert(layout);
    assert(text_layout_byte_offset_at(*layout, 1.0f, 5.0f) == 0);
    assert(text_layout_byte_offset_at(*layout, 9.0f, 5.0f) == 1);
    assert(text_layout_byte_offset_at(*layout, 11.0f, 5.0f) == 1);
    assert(text_layout_byte_offset_at(*layout, 39.0f, 5.0f) == layout->text.size());

    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    assert(text_layout_range_bounds(*layout, 1, layout->text.size() - 1,
                                    x, y, width, height));
    assert(x == 10.0f && width == 30.0f && height == 20.0f);
}

static void test_sparse_style_changes_invalidate_content_key()
{
    TextLayoutRequest request = request_for("Hello");
    const TextLayoutKey base = text_layout_key(request);
    RichTextCharFormat format = request.document.default_format;
    format.bold = true;
    request.document.ranges = {{0, 5, format, RichTextCharBold}};
    request.document.normalize();
    assert(!(base == text_layout_key(request)));

    const TextLayoutKey bold = text_layout_key(request);
    TextLayoutRequest stale_snapshot = request;
    stale_snapshot.document.ranges[0].format.tracking = 999.0f;
    assert(text_layout_key(stale_snapshot) == bold);
    assert(text_layout_request_equivalent(stale_snapshot, request));

    request.document.ranges[0].format.fill.color = 0xFFFF0000;
    request.document.ranges[0].mask |= RichTextCharFillColor;
    request.document.normalize();
    assert(bold == text_layout_key(request));
    assert(text_layout_request_equivalent(stale_snapshot, request));

    request.document.ranges[0].format.tracking = 12.0f;
    request.document.ranges[0].mask |= RichTextCharTracking;
    request.document.normalize();
    assert(!(bold == text_layout_key(request)));
}

static void test_shared_evaluated_defaults_preserve_sparse_runs()
{
    RichTextDocument document = request_for("AB").document;
    document.default_format.fill.type = 1;
    document.default_format.fill.gradient_start_color = 0xFF112233;
    RichTextCharFormat bold = document.default_format;
    bold.bold = true;
    document.ranges = {{0, 1, bold, RichTextCharBold}};
    document.normalize();

    RichTextEvaluatedDefaults defaults;
    defaults.font_size = 96;
    defaults.tracking = 8.0f;
    defaults.scale_x = 1.5f;
    defaults.scale_y = 0.75f;
    defaults.baseline_shift = 4.0f;
    defaults.solid_fill_color = 0xFFFF0000;
    defaults.indent_left = 12.0f;
    RichTextDocument evaluated =
        rich_text_document_with_evaluated_defaults(document, defaults);

    assert(evaluated.default_format.font_size == 96);
    assert(evaluated.default_format.tracking == 8.0f);
    assert(evaluated.default_format.scale_x == 1.5f);
    assert(evaluated.default_format.scale_y == 0.75f);
    assert(evaluated.default_format.fill.type == 1);
    assert(evaluated.default_format.fill.gradient_start_color == 0xFF112233);
    assert(evaluated.default_paragraph_format.indent_left == 12.0f);
    const RichTextCharFormat first = rich_text_format_at(evaluated, 0);
    const RichTextCharFormat second = rich_text_format_at(evaluated, 1);
    assert(first.bold && !second.bold);
    assert(first.font_size == 96 && second.font_size == 96);
    assert(first.tracking == 8.0f && second.tracking == 8.0f);
}

static void test_paint_runs_do_not_fragment_shaping()
{
    RichTextDocument document = request_for("ABCD").document;
    RichTextCharFormat red = document.default_format;
    red.fill.color = 0xFFFF0000;
    RichTextCharFormat decorated = red;
    decorated.underline = true;
    document.ranges = {
        {1, 2, red, RichTextCharFillColor},
        {2, 1, decorated, RichTextCharUnderline},
    };
    document.normalize();

    const std::vector<TextLayoutPaintRun> runs =
        text_layout_paint_runs(document);
    assert(runs.size() == 4);
    assert(runs[0].byte_start == 0 && runs[0].byte_length == 1);
    assert(runs[1].byte_start == 1 && runs[1].byte_length == 1);
    assert(runs[1].style.fill.color == 0xFFFF0000);
    assert(runs[2].byte_start == 2 && runs[2].byte_length == 1);
    assert(runs[2].style.fill.color == 0xFFFF0000);
    assert(runs[2].style.underline);
    assert(runs[3].byte_start == 3 && runs[3].byte_length == 1);
}


static void test_stroke_is_paint_only_and_range_scoped()
{
    TextLayoutRequest request = request_for("ABCD");
    const TextLayoutKey base = text_layout_key(request);
    RichTextCharFormat stroked = request.document.default_format;
    stroked.stroke.enabled = true;
    stroked.stroke.width = 5.0f;
    stroked.stroke.fill.color = 0xFF00AAFF;
    request.document.ranges = {{1, 2, stroked, RichTextCharStroke}};
    request.document.normalize();

    assert(base == text_layout_key(request));
    const std::vector<TextLayoutPaintRun> runs =
        text_layout_paint_runs(request.document);
    assert(runs.size() == 3);
    assert(!runs[0].style.stroke.enabled);
    assert(runs[1].byte_start == 1 && runs[1].byte_length == 2);
    assert(runs[1].style.stroke.enabled);
    assert(runs[1].style.stroke.width == 5.0f);
    assert(runs[1].style.stroke.fill.color == 0xFF00AAFF);
    assert(!runs[2].style.stroke.enabled);
}

static void test_multiple_gradient_and_stroke_styles_split_one_cluster()
{
    RichTextDocument document = request_for("fi").document;
    RichTextCharFormat first = document.default_format;
    first.fill.type = 1;
    first.fill.gradient_start_color = 0xFFFF0000;
    first.fill.gradient_end_color = 0xFFFFFF00;
    first.stroke.enabled = true;
    first.stroke.width = 3.0f;
    first.stroke.on_front = false;
    first.stroke.alignment = 0;
    first.stroke.fill.color = 0xFF220000;

    RichTextCharFormat second = document.default_format;
    second.fill.type = 1;
    second.fill.gradient_start_color = 0xFF00FF00;
    second.fill.gradient_end_color = 0xFF00FFFF;
    second.stroke.enabled = true;
    second.stroke.width = 6.0f;
    second.stroke.on_front = true;
    second.stroke.alignment = 2;
    second.stroke.fill.color = 0xFF0000FF;

    rich_text_document_apply_format(
        document, 0, 1, first, RichTextCharFillColor | RichTextCharStroke);
    rich_text_document_apply_format(
        document, 1, 1, second, RichTextCharFillColor | RichTextCharStroke);
    const std::vector<TextLayoutPaintRun> runs =
        text_layout_paint_runs(document);
    assert(runs.size() == 2);
    assert(runs[0].style.fill.type == 1);
    assert(runs[0].style.fill.gradient_start_color == 0xFFFF0000);
    assert(runs[1].style.fill.gradient_start_color == 0xFF00FF00);
    assert(runs[0].style.stroke.width == 3.0f);
    assert(!runs[0].style.stroke.on_front);
    assert(runs[0].style.stroke.alignment == 0);
    assert(runs[1].style.stroke.width == 6.0f);
    assert(runs[1].style.stroke.on_front);
    assert(runs[1].style.stroke.alignment == 2);

    TextLayoutData layout;
    layout.text = "fi";
    TextLayoutCluster cluster;
    cluster.byte_start = 0;
    cluster.byte_length = 2;
    cluster.x = 10.0f;
    cluster.width = 24.0f;
    cluster.height = 20.0f;
    cluster.boundary_begin = 0;
    cluster.boundary_count = 3;
    layout.cursor_boundaries = {
        {0, 10.0f},
        {1, 19.0f},
        {2, 34.0f},
    };
    const std::vector<TextLayoutPaintSlice> slices =
        text_layout_cluster_paint_slices(layout, cluster, runs);
    assert(slices.size() == 2);
    assert(slices[0].paint_index == 0);
    assert(slices[0].x0 == 10.0f && slices[0].x1 == 19.0f);
    assert(slices[1].paint_index == 1);
    assert(slices[1].x0 == 19.0f && slices[1].x1 == 34.0f);

    cluster.right_to_left = true;
    layout.cursor_boundaries = {
        {0, 34.0f},
        {1, 25.0f},
        {2, 10.0f},
    };
    const std::vector<TextLayoutPaintSlice> rtl_slices =
        text_layout_cluster_paint_slices(layout, cluster, runs);
    assert(rtl_slices.size() == 2);
    assert(rtl_slices[0].paint_index == 1);
    assert(rtl_slices[0].x0 == 10.0f && rtl_slices[0].x1 == 25.0f);
    assert(rtl_slices[1].paint_index == 0);
    assert(rtl_slices[1].x0 == 25.0f && rtl_slices[1].x1 == 34.0f);
}


static void test_editor_selection_and_caret_geometry()
{
    TextLayoutData layout;
    layout.text = "ABCD";
    layout.valid = true;
    TextLayoutLine first;
    first.byte_start = 0;
    first.byte_length = 2;
    first.cluster_begin = 0;
    first.cluster_count = 2;
    first.x = 0.0f;
    first.y = 4.0f;
    first.width = 20.0f;
    first.height = 16.0f;
    TextLayoutLine second = first;
    second.byte_start = 2;
    second.byte_length = 2;
    second.cluster_begin = 2;
    second.y = 24.0f;
    layout.lines = {first, second};

    TextLayoutCluster a;
    a.byte_start = 0;
    a.byte_length = 1;
    a.line_index = 0;
    a.x = 0.0f;
    a.y = 4.0f;
    a.width = 10.0f;
    a.height = 16.0f;
    a.boundary_begin = 0;
    a.boundary_count = 2;
    TextLayoutCluster b = a;
    b.byte_start = 1;
    b.x = 10.0f;
    b.boundary_begin = 2;
    TextLayoutCluster c = a;
    c.byte_start = 2;
    c.line_index = 1;
    c.y = 24.0f;
    c.boundary_begin = 4;
    TextLayoutCluster d = c;
    d.byte_start = 3;
    d.x = 10.0f;
    d.boundary_begin = 6;
    layout.clusters = {a, b, c, d};
    layout.cursor_boundaries = {
        {0, 0.0f}, {1, 10.0f},
        {1, 10.0f}, {2, 20.0f},
        {2, 0.0f}, {3, 10.0f},
        {3, 10.0f}, {4, 20.0f},
    };

    const std::vector<TextLayoutRect> selection =
        text_layout_selection_rects(layout, 1, 3);
    assert(selection.size() == 2);
    assert(selection[0].line_index == 0 && selection[0].x == 10.0f &&
           selection[0].width == 10.0f && selection[0].y == 4.0f);
    assert(selection[1].line_index == 1 && selection[1].x == 0.0f &&
           selection[1].width == 10.0f && selection[1].y == 24.0f);

    TextLayoutRect caret;
    assert(text_layout_caret_rect(layout, 3, 1.5f, caret));
    assert(caret.line_index == 1 && caret.x == 10.0f &&
           caret.width == 1.5f && caret.height == 16.0f);

    TextLayoutData rtl = layout;
    rtl.text = "AB";
    rtl.lines.resize(1);
    rtl.lines[0].byte_length = 2;
    rtl.lines[0].cluster_count = 1;
    rtl.clusters.resize(1);
    rtl.clusters[0].byte_start = 0;
    rtl.clusters[0].byte_length = 2;
    rtl.clusters[0].right_to_left = true;
    rtl.clusters[0].x = 0.0f;
    rtl.clusters[0].width = 20.0f;
    rtl.clusters[0].boundary_begin = 0;
    rtl.clusters[0].boundary_count = 3;
    rtl.cursor_boundaries = {{0, 20.0f}, {1, 10.0f}, {2, 0.0f}};
    const std::vector<TextLayoutRect> rtl_selection =
        text_layout_selection_rects(rtl, 0, 1);
    assert(rtl_selection.size() == 1);
    assert(rtl_selection[0].x == 10.0f && rtl_selection[0].width == 10.0f);
    assert(text_layout_caret_rect(rtl, 1, 1.0f, caret));
    assert(caret.x == 10.0f);
}


static void test_all_fill_and_stroke_fields_reach_paint_runs()
{
    RichTextDocument document = request_for("X").document;
    RichTextCharFormat format = document.default_format;
    format.fill.type = 1;
    format.fill.color = 0xFF010203;
    format.fill.gradient_type = 3;
    format.fill.gradient_spread = 2;
    format.fill.gradient_start_color = 0xFF102030;
    format.fill.gradient_end_color = 0xFF405060;
    format.fill.gradient_start_pos = 0.15f;
    format.fill.gradient_end_pos = 0.85f;
    format.fill.gradient_start_opacity = 0.25f;
    format.fill.gradient_end_opacity = 0.75f;
    format.fill.gradient_opacity = 0.6f;
    format.fill.gradient_angle = 37.0f;
    format.fill.gradient_center_x = 0.2f;
    format.fill.gradient_center_y = 0.3f;
    format.fill.gradient_scale = 1.75f;
    format.fill.gradient_focal_x = 0.4f;
    format.fill.gradient_focal_y = 0.45f;
    format.fill.gradient_stops = {
        {0xFFABCDEF, 0.25f, 0.8f},
        {0xFF123456, 0.65f, 0.35f},
    };

    format.stroke.enabled = true;
    format.stroke.width = 7.5f;
    format.stroke.opacity = 0.55f;
    format.stroke.on_front = true;
    format.stroke.alignment = 2;
    format.stroke.antialias = false;
    format.stroke.join_style = 2;
    format.stroke.fill = format.fill;
    format.stroke.fill.gradient_angle = 91.0f;
    format.stroke.fill.gradient_stops = {
        {0xFF654321, 0.4f, 0.6f},
    };

    rich_text_document_apply_format(document, 0, 1, format,
                                    RichTextCharFillColor | RichTextCharStroke);
    const std::vector<TextLayoutPaintRun> runs =
        text_layout_paint_runs(document);
    assert(runs.size() == 1);
    const RichTextFill &fill = runs[0].style.fill;
    assert(fill.type == format.fill.type);
    assert(fill.color == format.fill.color);
    assert(fill.gradient_type == format.fill.gradient_type);
    assert(fill.gradient_spread == format.fill.gradient_spread);
    assert(fill.gradient_start_color == format.fill.gradient_start_color);
    assert(fill.gradient_end_color == format.fill.gradient_end_color);
    assert(fill.gradient_start_pos == format.fill.gradient_start_pos);
    assert(fill.gradient_end_pos == format.fill.gradient_end_pos);
    assert(fill.gradient_start_opacity == format.fill.gradient_start_opacity);
    assert(fill.gradient_end_opacity == format.fill.gradient_end_opacity);
    assert(fill.gradient_opacity == format.fill.gradient_opacity);
    assert(fill.gradient_angle == format.fill.gradient_angle);
    assert(fill.gradient_center_x == format.fill.gradient_center_x);
    assert(fill.gradient_center_y == format.fill.gradient_center_y);
    assert(fill.gradient_scale == format.fill.gradient_scale);
    assert(fill.gradient_focal_x == format.fill.gradient_focal_x);
    assert(fill.gradient_focal_y == format.fill.gradient_focal_y);
    assert(fill.gradient_stops == format.fill.gradient_stops);

    const RichTextStroke &stroke = runs[0].style.stroke;
    assert(stroke.enabled == format.stroke.enabled);
    assert(stroke.width == format.stroke.width);
    assert(stroke.opacity == format.stroke.opacity);
    assert(stroke.on_front == format.stroke.on_front);
    assert(stroke.alignment == format.stroke.alignment);
    assert(stroke.antialias == format.stroke.antialias);
    assert(stroke.join_style == format.stroke.join_style);
    assert(stroke.fill.gradient_angle == format.stroke.fill.gradient_angle);
    assert(stroke.fill.gradient_focal_x == format.stroke.fill.gradient_focal_x);
    assert(stroke.fill.gradient_focal_y == format.stroke.fill.gradient_focal_y);
    assert(stroke.fill.gradient_stops == format.stroke.fill.gradient_stops);
}

int main()
{
    test_key_and_cache_contract();
    test_hit_testing_and_range_bounds();
    test_sparse_style_changes_invalidate_content_key();
    test_shared_evaluated_defaults_preserve_sparse_runs();
    test_paint_runs_do_not_fragment_shaping();
    test_stroke_is_paint_only_and_range_scoped();
    test_multiple_gradient_and_stroke_styles_split_one_cluster();
    test_all_fill_and_stroke_fields_reach_paint_runs();
    test_editor_selection_and_caret_geometry();
    std::cout << "text layout contract tests passed\n";
    return 0;
}
