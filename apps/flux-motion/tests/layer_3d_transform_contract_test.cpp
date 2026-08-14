#include <cassert>
#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {
void require(const std::string &source, const char *needle)
{
    if (source.find(needle) == std::string::npos) {
        std::cerr << "missing 3D contract: " << needle << '\n';
        assert(false);
    }
}

void forbid(const std::string &source, const char *needle)
{
    if (source.find(needle) != std::string::npos) {
        std::cerr << "forbidden 3D regression: " << needle << '\n';
        assert(false);
    }
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 11);
    const std::string layer_model = read_file(argv[1]);
    const std::string title_model = read_file(argv[2]);
    const std::string serialization = read_file(argv[3]);
    const std::string transform_header = read_file(argv[4]);
    const std::string transform_source = read_file(argv[5]);
    const std::string source_runtime = read_file(argv[6]);
    const std::string cache_source = read_file(argv[7]);
    const std::string layer_ui = read_file(argv[8]);
    const std::string camera_ui = read_file(argv[9]);
    const std::string timeline = read_file(argv[10]);

    require(layer_model, "enum class LayerDimensionMode");
    require(layer_model, "AnimatedProperty position_z");
    require(layer_model, "AnimatedProperty rotation_x");
    require(layer_model, "AnimatedProperty rotation_y");
    require(layer_model, "AnimatedProperty scale_z");
    require(layer_model, "AnimatedProperty anchor_z");
    require(layer_model, "AnimatedProperty orientation_z");
    require(layer_model, "bool depth_test = true");
    require(layer_model, "bool write_to_depth = true");
    require(layer_model, "bool backface_culling = false");

    require(title_model, "struct TitleCamera");
    require(title_model, "CameraProjection::Perspective");
    require(title_model, "std::vector<TitleCamera> cameras");
    require(title_model, "std::string active_camera_id");

    require(serialization, "j[\"dimension_mode\"]");
    require(serialization, "j[\"position_z\"]");
    require(serialization, "camera_to_json");
    require(serialization, "camera_from_json");
    require(serialization, "active_camera_id");

    require(transform_header, "+X: canvas right");
    require(transform_header, "+Y: canvas down");
    require(transform_header, "+Z: away from the active camera");
    require(transform_header, "projected_or_legacy");
    require(transform_source, "camera.use_canvas_default");
    require(transform_source, "result.projection.perspective");
    require(transform_source, "result.projection.ortho");
    require(transform_source, "QTransform::quadToQuad");
    require(transform_source, "layer_or_ancestor_uses_3d");
    require(transform_source, "ordered_root_layer_indices");
    require(transform_source, "layer_passes_backface_culling");

    require(source_runtime, "layer.position_z.is_animated()");
    require(source_runtime, "title_camera_has_animation");
    require(cache_source, "layer->dimension_mode == LayerDimensionMode::ThreeD");
    require(cache_source, "title_camera_has_animation(title)");

    require(layer_ui, "3D Layer");
    require(layer_ui, "Gizmo Space");
    require(layer_ui, "Position Z");
    require(layer_ui, "Backface Culling");
    require(layer_ui, "cmb_layer_camera_");
    require(camera_ui, "3D Camera");
    require(camera_ui, "Perspective");
    require(camera_ui, "Orthographic");
    require(camera_ui, "Near clip");
    require(camera_ui, "Far clip");
    require(camera_ui, "static_cast<QWidget *>(spn_camera_pos_y_)");
    require(camera_ui, "static_cast<QWidget *>(spn_camera_target_z_)");
    require(camera_ui, "static_cast<QWidget *>(spn_camera_rot_z_)");

    require(timeline, "layer.position_3d");
    require(timeline, "layer.rotation_x");
    require(timeline, "layer.orientation_z");
    forbid(transform_source, "glBegin(");

    std::cout << "layer 3D transform contract: PASS\n";
    return 0;
}
