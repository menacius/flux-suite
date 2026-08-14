#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static std::string read_all(const char *path)
{
    std::ifstream file(path, std::ios::binary);
    assert(file.good());
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

int main(int argc, char **argv)
{
    assert(argc == 11);
    const std::string compatibility_text = read_all(argv[1]);
    const std::string title_data = read_all(argv[2]);
    const std::string editor_events = read_all(argv[3]);
    const std::string gpu_text = read_all(argv[4]);
    const std::string cache = read_all(argv[5]);
    const std::string lifecycle = read_all(argv[6]);
    const std::string glyph_renderer = read_all(argv[7]);
    const std::string transition_editor = read_all(argv[8]);
    const std::string layer_raster = read_all(argv[9]);
    const std::string layer_layout = read_all(argv[10]);

    /* Legacy descriptors may still be read by title-data migration, but the
     * compatibility raster must consume the unified cluster evaluation and
     * contain no descriptor-driven runtime evaluator. */
    assert(compatibility_text.find("active_text_layer_transition") ==
           std::string::npos);
    assert(compatibility_text.find("apply_isolated_text_layer_transition") ==
           std::string::npos);
    assert(compatibility_text.find("apply_text_layer_transition") ==
           std::string::npos);
    assert(compatibility_text.find("apply_unified_text_animator_raster") !=
           std::string::npos);
    assert(compatibility_text.find("apply_unified_text_animator_flattened") !=
           std::string::npos);
    assert(compatibility_text.find("evaluate_text_animators") !=
           std::string::npos);
    assert(compatibility_text.find("TextAnimatorClusterState") !=
           std::string::npos);
    assert(compatibility_text.find("integrated_text_animator_blur_image") !=
           std::string::npos);
    assert(compatibility_text.find("blur_premultiplied_pixels_for_type") !=
           std::string::npos);
    assert(compatibility_text.find("blurred_transition_image") ==
           std::string::npos);
    assert(compatibility_text.find("kTextAnimatorResamplingGutter") !=
           std::string::npos);
    assert(compatibility_text.find("max_rich_text_font_height_hint") !=
           std::string::npos);
    assert(compatibility_text.find("painter.setClipRect(QRectF(render_bounds))") !=
           std::string::npos);
    assert(compatibility_text.find("bool clip_required = false") !=
           std::string::npos);
    assert(compatibility_text.find("continuous_path.addPath(piece.path)") !=
           std::string::npos);
    assert(compatibility_text.find("if (piece.clip_required)") !=
           std::string::npos);

    assert(title_data.find("migrate_legacy_text_transitions") != std::string::npos);
    assert(title_data.find("text_animators") != std::string::npos);
    assert(editor_events.find("synchronize_text_transition_animators") !=
           std::string::npos);
    /* Applying a text transition must retain the descriptor instead of erasing
     * it, so timeline overlays and the transition editor keep working. */
    assert(editor_events.find("layer->transitions.push_back(transition)") !=
           std::string::npos);
    assert(editor_events.find("layer->transitions.erase(std::remove_if") ==
           std::string::npos);

    /* Editor/source output and cache both consume the same animator model. */
    assert(gpu_text.find("evaluate_text_animators") != std::string::npos);
    assert(gpu_text.find("layer.text_animators") != std::string::npos);
    assert(glyph_renderer.find("TextRevealDirection::Right") != std::string::npos);
    assert(glyph_renderer.find("animation->has_reveal_bounds") != std::string::npos);
    assert(glyph_renderer.find("animation->has_transform_origin") != std::string::npos);
    assert(glyph_renderer.find("blurredFillCoverage") != std::string::npos);
    assert(glyph_renderer.find("blurredStrokeCoverage") != std::string::npos);
    assert(glyph_renderer.find("atlasTexelSize") != std::string::npos);
    assert(glyph_renderer.find("max(fwidth(signedDistance), 0.55) +") ==
           std::string::npos);
    assert(cache.find("text_animator_stack_signature") != std::string::npos);
    assert(lifecycle.find("text_animator_stack_is_time_dependent") !=
           std::string::npos);
    assert(lifecycle.find("active_text_layer_transition(layer, layer_time)") ==
           std::string::npos);
    assert(lifecycle.find("resolved_text_transition_animator_stack") !=
           std::string::npos);
    /* Transition-managed animators use the same GPU glyph path as ordinary
     * animators. Routing them through QTextDocument reintroduced the
     * platform-dependent H/V Scale width-face threshold. */
    assert(gpu_text.find("text_animator_stack_has_managed_transition") ==
           std::string::npos);
    assert(gpu_text.find("layer.type == LayerType::Text") !=
           std::string::npos);
    assert(gpu_text.find(
        "!text_animator_stack_has_enabled_animators(layer.text_animators)") !=
        std::string::npos);

    /* The generic animator adapters must be complete before the layer-raster
     * module starts. v136 accidentally left the tail of the removed legacy
     * transition function at the beginning of this include, which broke the
     * translation-unit boundary and prevented the new renderer from being the
     * active implementation. */
    assert(layer_raster.rfind("static void render_layer_text", 0) == 0);
    assert(layer_raster.find("animated_text_blur_radius") == std::string::npos);
    assert(layer_raster.find("active_text_layer_transition") == std::string::npos);
    const auto animator_block = layer_raster.find(
        "if (use_unified_text_animator)");
    assert(animator_block != std::string::npos);
    const auto exact_adapter = layer_raster.find(
        "&exact_rich_text", animator_block);
    const auto qt_adapter = layer_raster.find(
        "apply_unified_text_animator_raster", exact_adapter);
    const auto flattened_fallback = layer_raster.find(
        "apply_unified_text_animator_flattened", qt_adapter);
    assert(exact_adapter != std::string::npos);
    assert(qt_adapter != std::string::npos);
    assert(flattened_fallback != std::string::npos);
    assert(exact_adapter < qt_adapter);
    assert(qt_adapter < flattened_fallback);
    assert(layer_raster.find("text_raster_ink_gutter") !=
           std::string::npos);
    /* Paint runs intentionally do not duplicate shaping fields. The glyph
     * envelope must resolve font size and vertical scale through effective
     * RichTextCharFormat values, otherwise MSVC fails on nonexistent
     * TextLayoutPaintStyle::font_size/scale_y members. */
    assert(layer_layout.find("run.style.font_size") == std::string::npos);
    assert(layer_layout.find("run.style.scale_y") == std::string::npos);
    assert(layer_layout.find("format_height(rich_text_format_at(model, offset))") !=
           std::string::npos);
    assert(layer_layout.find("TextLayoutPaintStyle deliberately contains paint-only state") !=
           std::string::npos);
    /* The regular Blur effect and Text Animator blur must converge on the
     * same shared premultiplied-color backend. */
    assert(layer_raster.find("blur_premultiplied_pixels_for_type") !=
           std::string::npos);
    /* The dialog may use QPainter for its thumbnail, but selector timing and
     * property state must come from the same generic evaluator as runtime. */
    const auto text_preview_begin = transition_editor.find(
        "void draw_text_preview");
    const auto text_preview_end = transition_editor.find(
        "QTimer timer_", text_preview_begin);
    assert(text_preview_begin != std::string::npos);
    assert(text_preview_end != std::string::npos);
    const std::string text_preview = transition_editor.substr(
        text_preview_begin, text_preview_end - text_preview_begin);
    assert(text_preview.find("make_text_animator_from_legacy_transition") !=
           std::string::npos);
    assert(text_preview.find("evaluate_text_animators") !=
           std::string::npos);
    assert(text_preview.find("layer_transition_ease(") ==
           std::string::npos);

    std::cout << "text animator integration contract passed\n";
    return 0;
}
