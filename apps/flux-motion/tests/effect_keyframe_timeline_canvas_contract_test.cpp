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
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 9);
    const std::string panel = read_file(argv[1]);
    const std::string animation = read_file(argv[2]);
    const std::string hierarchy = read_file(argv[3]);
    const std::string timeline = read_file(argv[4]);
    const std::string timeline_header = read_file(argv[5]);
    const std::string canvas_press = read_file(argv[6]);
    const std::string canvas_drag = read_file(argv[7]);
    const std::string canvas_draw = read_file(argv[8]);

    require(panel, "effect_has_any_keyframes(effect)");
    require(panel, "OBSTitles.RemoveEffectWithKeyframesQuestion");
    require(panel, "binding.has_keyframes(*active)");
    require(panel, "binding.clear_keyframes(*active)");
    require(panel, "OBSTitles.DeleteAllKeyframes");
    require(panel, "set_animated_value(\n                        *active, key, current_local_time(), encoded)");
    require(panel, "toggle_keyframe(\n                        *active, key, current_local_time(), value)");
    require(panel, "meta.value(QStringLiteral(\"type\")).toString() != QStringLiteral(\"point\")");
    require(panel, "QStringLiteral(\"__native.center\")");
    require(panel, "QStringLiteral(\"__native.gradient_center\")");
    require(panel, "QStringLiteral(\"__native.gradient_focal\")");
    require(panel, "QStringLiteral(\"space\"), QStringLiteral(\"layer\")");
    require(panel, "publish_canvas_handles();");
    require(panel, "handles == last_published_canvas_handles_");

    require(animation, "EasingType easing = EasingType::Linear");
    require(animation, "set_key_easing(key, easing)");
    require(animation, "write_track_keys(effect, path, keys)");

    require(hierarchy, "LayerEffect *extension_effect = nullptr");
    require(hierarchy, "EasingType extension_default_easing = EasingType::Linear");
    require(hierarchy, "__effect__%1__%2__%3");
    require(hierarchy, "fxm::effects::animation::track_keys");
    require(hierarchy, "fxm::effects::animation::set_key_easing");
    require(hierarchy, "effect-%1:");
    require(hierarchy, "std::vector<AnimatedProperty *> scalar_group");
    require(hierarchy, "name_group(\"color\"");
    require(hierarchy, "declared_type == QStringLiteral(\"bool\")");
    require(hierarchy, "extension_default_easing");

    require(timeline_header, "std::vector<Keyframe> scalar_group_keyframes");
    require(timeline_header, "bool is_scalar_group = false");
    require(timeline_header, "QJsonObject extension_keyframe");
    require(timeline_header, "bool is_extension = false");
    require(timeline, "copy.is_scalar_group = prop.is_scalar_group()");
    require(timeline, "copy.is_extension = prop.is_extension()");
    require(timeline, "prop.push_keyframes(pasted)");
    require(timeline, "prop.push_keyframe(pasted)");

    require(canvas_press, "layer_to_canvas(*handle_layer, local)");
    require(canvas_drag, "canvas_to_layer(*layer, canvas_point)");
    require(canvas_drag, "handle.value(QStringLiteral(\"minX\"))");
    require(canvas_draw, "layer_to_canvas(*handle_layer, local)");
    require(canvas_draw, "QString active_path");
    require(canvas_draw, "drag_mode_ == DragMode::ExtensionPoint");
    require(canvas_draw, "active_extension_handle_ = i");

    std::cout << "effect keyframe timeline/canvas contract: PASS\n";
    return 0;
}
