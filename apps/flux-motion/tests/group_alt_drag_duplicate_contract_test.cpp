#include "source_bundle_reader.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::cerr << "usage: group_alt_drag_duplicate_contract_test <canvas-cpp>\n";
        return 2;
    }
    const std::string source = read_file(argv[1]);

    const bool expands_group_descendants =
        source.find("Alt-dragging a group must duplicate the complete container") != std::string::npos &&
        source.find("parent->type == LayerType::Group") != std::string::npos &&
        source.find("selected_ids.insert(layer->id)") != std::string::npos;
    const bool remaps_hierarchy =
        source.find("clone->parent_id = parent_clone->second") != std::string::npos &&
        source.find("clone->transform_parent_id = transform_parent_clone->second") != std::string::npos &&
        source.find("clone->mask_source_id = mask_clone->second") != std::string::npos;
    const bool moves_only_requested_roots =
        source.find("requested_ids.find(original_id->first)") != std::string::npos;
    const bool object_alt_left_precedes_orbit =
        source.find("Alt+Left is the canvas duplication gesture") != std::string::npos &&
        source.find("hit_test_selected(ev->position()) != DragMode::None") != std::string::npos &&
        source.find("layer_at_view_pos(ev->position())") != std::string::npos;

    if (!expands_group_descendants || !remaps_hierarchy || !moves_only_requested_roots ||
        !object_alt_left_precedes_orbit) {
        std::cerr << "Group Alt+drag duplication contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
