#include <iostream>
#include <sstream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &text, const std::string &needle, const char *label)
{
    if (text.find(needle) != std::string::npos) return true;
    std::cerr << "Missing group hierarchy/timeline contract: " << label
              << " (" << needle << ")\n";
    return false;
}
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        std::cerr << "usage: group_hierarchy_timeline_contract_test "
                     "<internal-h> <layers-cpp> <timeline-cpp> <canvas-cpp> <locale>\n";
        return 2;
    }
    const std::string internal_h = read_file(argv[1]);
    const std::string layers = read_file(argv[2]);
    const std::string timeline = read_file(argv[3]);
    const std::string canvas = read_file(argv[4]);
    const std::string locale = read_file(argv[5]);

    bool ok = true;
    ok &= require(internal_h, "visible_layer_hierarchy_rows", "shared visible hierarchy");
    ok &= require(internal_h, "layer_has_collapsed_group_ancestor", "collapsed descendants hidden");
    ok &= require(internal_h, "group_descendant_object_count", "group object count helper");
    ok &= require(layers, "canonical_group_model_order", "group-scoped drag ordering");
    ok &= require(timeline, "const auto rows = timeline_rows(title)", "timeline uses shared row model");
    ok &= require(timeline, "OBSTitles.GroupItemsCount", "group strip object count");
    ok &= require(canvas, "parent->group_collapsed = false", "double-click expands group ancestors");
    ok &= require(canvas, "draw_layer_box(group_geometry, false)", "group context has no handles");
    ok &= require(locale, "OBSTitles.GroupItemCount", "singular object label");
    ok &= require(locale, "OBSTitles.GroupItemsCount", "plural object label");
    return ok ? 0 : 1;
}
