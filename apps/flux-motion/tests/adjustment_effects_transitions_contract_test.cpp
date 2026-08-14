#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing: " << needle << '\n';
        assert(false);
    }
}

void forbid(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "forbidden: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 14);
    const std::string layer_model = read_file(argv[1]);
    const std::string effects_model = read_file(argv[2]);
    const std::string transition_model = read_file(argv[3]);
    const std::string title_data = read_file(argv[4]);
    const std::string title_source = read_file(argv[5]);
    const std::string cache_manager = read_file(argv[6]);
    const std::string effects_panel = read_file(argv[7]);
    const std::string transition_dialog = read_file(argv[8]);
    const std::string presets_panel = read_file(argv[9]);
    const std::string registry = read_file(argv[10]);
    const std::string properties = read_file(argv[11]);
    const std::string canvas = read_file(argv[12]);
    const std::string layer_stack = read_file(argv[13]);

    require(layer_model, "Adjustment = 6,");
    require(layer_model, "ColorSolid = 7,");
    require(effects_model, "LensFlare = 14");
    require(effects_model, "RoughenEdges = 17");
    require(effects_model, "AnimatedProperty complexity_prop");
    require(transition_model, "Blocks = 14");
    require(transition_model, "GradientWipe = 18");
    require(transition_model, "std::string image_path");

    require(title_data, "effect_secondary_color");
    require(title_data, "blocks_columns");
    require(title_data, "constexpr size_t kMaxPathLength = 4096;");
    require(title_data, "bounded_string(transition_json, \"image_path\", \"\", kMaxPathLength)");
    require(title_data, "bounded_string(j, \"image_path\", \"\", kMaxPathLength)");

    require(title_source, "kGpuAdjustmentMixEffect");
    require(title_source, "if (layer.type == LayerType::Adjustment)");
    require(title_source, "transitionMatteEnabled");
    require(title_source, "transitionType == 18");
    require(title_source, "if (transitionInvert != 0) matte = 1.0 - matte;");
    forbid(title_source, "alpha = 1.0 - alpha;");
    require(title_source, "LayerType::ColorSolid");
    require(title_source, "resolved.type == LayerEffectType::LensFlare");

    require(cache_manager, "gpu-renderer-v31-lens-flare-dx11-keyword-fix");
    require(cache_manager, "effect.type == LayerEffectType::Noise && effect.effect_animated");
    require(cache_manager, "layer.type == LayerType::Adjustment || layer.type == LayerType::ColorSolid");
    require(cache_manager, "matte_info.lastModified().toMSecsSinceEpoch()");

    require(effects_panel, "LayerEffectType::LensFlare");
    require(effects_panel, "LayerEffectType::RoughenEdges");
    require(effects_panel, "effect_complexity");
    require(effects_panel, "Subtle Natural");
    forbid(effects_panel, "QStringLiteral(\"Minimal\"), 4");
    require(transition_dialog, "blocks_columns_");
    require(transition_dialog, "image_browse_");
    require(transition_dialog, "LayerTransitionType::GradientWipe");

    require(presets_panel, "normalize_category_tree");
    require(presets_panel, "child->childCount() == 0");
    require(presets_panel, "child->childCount() == 1");

    require(registry, "shaders/lens-flare/lens-flare.effect");
    require(registry, "shaders/roughen-edges/roughen-edges.effect");
    require(properties, "is_fixed_canvas_layer");
    require(canvas, "layer_has_fixed_canvas_geometry");
    /* Adjustment and Group layers intentionally participate in the same
     * transform-parent and track-matte graph as artwork layers. */
    require(layer_stack, "QComboBox *parent = new QComboBox");
    require(layer_stack, "QComboBox *matte = new QComboBox");
    require(layer_stack, "emit layer_parent_changed");
    require(layer_stack, "emit layer_mask_changed");
    forbid(layer_stack, "candidate->type == LayerType::Adjustment");
    forbid(layer_stack, "setEnabled(l->type != LayerType::Adjustment)");

    std::cout << "adjustment/solid/effects/transitions integration contract: PASS\n";
    return 0;
}
