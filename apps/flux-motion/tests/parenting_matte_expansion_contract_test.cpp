#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos) return true;
    std::cerr << "Missing parenting/matte/expansion contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 9) {
        std::cerr << "usage: parenting_matte_expansion_contract_test <model> <data> "
                     "<editor> <layers> <canvas> <internal> <source> <cmake>\n";
        return 2;
    }
    const std::string model = read_file(argv[1]);
    const std::string data = read_file(argv[2]);
    const std::string editor = read_file(argv[3]);
    const std::string layers = read_file(argv[4]);
    const std::string canvas = read_file(argv[5]);
    const std::string internal_h = read_file(argv[6]);
    const std::string source = read_file(argv[7]);
    const std::string cmake = read_file(argv[8]);

    bool ok = true;
    ok &= require(model, "std::string transform_parent_id", "independent transform parent field");
    ok &= require(model, "VisibleAndMatte", "third matte visibility state");
    ok &= require(model, "InvertedClipping", "clipping matte enum values");
    ok &= require(data, "j[\"transform_parent_id\"]", "parent serialization");
    ok &= require(data, "j[\"matte_visibility_mode\"]", "matte state serialization");
    ok &= require(data, "MaskMode::InvertedClipping", "clipping matte deserialization bound");
    ok &= require(data, "Normal hidden layers must keep their own",
                  "non-matte visibility survives reload");
    ok &= require(data, "legacy_parent->type != LayerType::Group", "legacy parenting migration keeps groups");

    ok &= require(editor, "layer->parent_id,\n                                                          next_parent_id",
                  "parent dropdown preserves group membership");
    ok &= require(editor, "layer.transform_parent_id = new_transform_parent_id",
                  "new transform relation assignment");
    ok &= require(editor, "editor_layer_local_transform_for_parenting(layer, playhead) * parent_basis",
                  "group-compatible local-to-world transform order");
    ok &= require(editor, "world * parent_inverse",
                  "world-preserving parent/unparent conversion order");
    ok &= require(editor, "source_was_already_matte",
                  "first matte assignment preserves prior visibility state");
    ok &= require(editor, "Clipping visibility is now resolved by the GPU",
                  "clipping type does not leak source visibility state");
    ok &= require(layers, "l->transform_parent_id", "parent dropdown reads transform parent");
    ok &= require(layers, "QStringLiteral(\"%1. %2\")", "numbered dropdown labels");
    ok &= require(layers, "layer_matte_visibility_changed", "three-state matte button");
    ok &= require(layers, "matte-clipping.svg", "clipping matte UI icon");
    ok &= require(layers, "% 3", "alpha luma clipping type cycle");
    ok &= require(layers, "group_expansion_state_changed", "three-state group caret");

    ok &= require(canvas, "editor_layer_local_transform(layer, playhead) * parent_basis",
                  "canvas matches local-to-world transform order");
    ok &= require(canvas, "Moving a transform parent must not snap to any of its children",
                  "parent-child snap exclusion");
    ok &= require(canvas, "Moving a Group must not snap to artwork contained by the Group",
                  "group-child snap exclusion");
    ok &= require(internal_h, "layer->type == LayerType::Group && !layer->group_collapsed",
                  "open groups show group keyframes");
    ok &= require(source, "MatteVisibilityMode::VisibleAndMatte",
                  "visible-and-active matte composition");
    ok &= require(source, "emitted_clipping_bases",
                  "clipping matte base is emitted once by the compositor");
    ok &= require(source, "base_layer->matte_visibility_mode !=",
                  "clipping artwork obeys matte visibility mode");
    ok &= require(source, "layer.transform_parent_id", "renderer follows transform parent");
    ok &= require(source, "layer_local_transform_qt(layer, title_time) * parent_basis",
                  "renderer matches local-to-world transform order");
    ok &= require(source, "gs_matrix_mul(&affine)",
                  "GPU renderer applies the exact affine world transform");
    if (source.find("const double scale_x = std::hypot(transform.m11(), transform.m12())") != std::string::npos) {
        std::cerr << "Parenting renderer still decomposes affine transforms and can lose shear\n";
        ok = false;
    }
    ok &= require(cmake, "OBS_FXM_DEVELOPMENT_VERSION", "development version variable");
    return ok ? 0 : 1;
}
