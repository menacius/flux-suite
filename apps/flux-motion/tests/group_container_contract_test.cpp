#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing group-container contract: " << label << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 7) {
        std::cerr << "usage: group_container_contract_test <model> <data> <editor> "
                     "<layers> <canvas> <source>\n";
        return 2;
    }
    const std::string model = read_file(argv[1]);
    const std::string data = read_file(argv[2]);
    const std::string editor = read_file(argv[3]);
    const std::string layers = read_file(argv[4]);
    const std::string canvas = read_file(argv[5]);
    const std::string source = read_file(argv[6]);

    bool ok = true;
    ok &= require(model, "Group = 8", "persisted group layer type");
    ok &= require(model, "group_collapsed", "group collapse state");
    ok &= require(data, "j[\"group_collapsed\"]", "group collapse serialization");
    ok &= require(data, "LayerType::Group", "group deserialization range");

    ok &= require(editor, "void TitleEditor::group_selected_layers()", "group command");
    ok &= require(editor, "void TitleEditor::ungroup_selected_layers()", "ungroup command");
    ok &= require(editor, "add_selected_layers_to_group", "add-to-group command");
    ok &= require(editor, "editor_top_level_selected_layers", "nested selection roots");
    ok &= require(editor, "group->group_collapsed = true", "new groups start collapsed");
    ok &= require(editor, "layers_->set_selected_layer(group->id)", "new group remains selected");
    ok &= require(editor, "editor_selection_with_group_descendants", "group copy/delete integrity");

    ok &= require(layers, "visible_layer_hierarchy_rows", "hierarchical layer rows");
    ok &= require(layers, "group_expansion_state_changed", "three-state group caret behavior");
    ok &= require(layers, "add_to_group_requested", "add-to-group UI");
    ok &= require(layers, "remove_from_group_requested", "remove-from-group UI");
    ok &= require(layers, "Group's own keyframe properties",
                  "groups expose ordinary animated property rows");
    ok &= require(layers, "has_group_ancestor(title_, *l, candidate->id)",
                  "matte menu rejects recursive group-container dependencies");

    ok &= require(canvas, "layer.type == LayerType::Group", "dynamic group canvas bounds");
    ok &= require(canvas, "editor_outermost_group_ancestor", "canvas group drill-down helper");
    ok &= require(canvas, "editor_canvas_selection_target", "canvas selection follows group expansion state");
    ok &= require(canvas, "Direct Selection obeys the same hierarchy visibility contract",
                  "direct-selection cannot bypass a collapsed group");
    ok &= require(canvas, "layer->type == LayerType::Group && !layer->group_collapsed",
                  "marquee exposes children only for fully expanded groups");
    ok &= require(canvas, "editor_layer_has_ancestor(title_, *candidate, layer.id)",
                  "group bounds follow descendants");
    ok &= require(canvas, "layer->locked || has_selected_parent(*layer)",
                  "group plus child selection transforms only hierarchy roots");

    ok &= require(source, "if (layer.type == LayerType::Group)",
                  "group has no empty compositor raster");
    ok &= require(source, "Children are composited through their group surface",
                  "group child compositor contract");
    ok &= require(source, "A group owns the child opacity boundary",
                  "child effects render before single local-opacity composite");
    ok &= require(source, "Affect-behind effects on a child operate",
                  "grouped child backdrop effects remain active");
    ok &= require(source, "layer_compositor_opacity",
                  "scope-correct matte opacity for groups and children");
    ok &= require(source, "group-matte-v3",
                  "groups publish a normal layer-style matte texture");
    ok &= require(source, "snapshot_gpu_track_matte_target",
                  "group matte rendering preserves its consumer texture");
    ok &= require(source, "apply_gpu_track_matte_texture",
                  "all target paths apply the protected matte graph");
    ok &= require(source, "Could not preserve target texture before Group track-matte rendering",
                  "group matte snapshot failures are explicit");
    ok &= require(source, "apply_group_target_mask",
                  "group target uses the ordinary layer-style matte graph");
    ok &= require(source, "Match the ordinary-layer effect/matte ordering",
                  "group target respects effects-before/after-mask ordering");
    ok &= require(source, "A Group has no raster of its own",
                  "group matte dependencies recursively include descendants");
    ok &= require(source, "Preserve it before an",
                  "child foreground survives shared effect buffers");

    return ok ? 0 : 1;
}
