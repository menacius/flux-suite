#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool contains(const std::string &text, const std::string &needle,
              const char *label)
{
    if (text.find(needle) != std::string::npos)
        return true;
    std::cerr << "Missing spatial Bezier contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool ordered(const std::string &text, const std::string &first,
             const std::string &second, const char *label)
{
    const size_t a = text.find(first);
    const size_t b = text.find(second);
    if (a != std::string::npos && b != std::string::npos && a < b)
        return true;
    std::cerr << "Invalid spatial Bezier ordering: " << label << "\n";
    return false;
}

bool absent_after(const std::string &text, const std::string &start,
                  const std::string &needle, const char *label)
{
    const size_t begin = text.find(start);
    if (begin != std::string::npos &&
        text.find(needle, begin) == std::string::npos)
        return true;
    std::cerr << "Invalid spatial Bezier hot path: " << label << "\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 16) {
        std::cerr << "usage: spatial_bezier_keyframes_contract_test "
                     "<animation-h> <animation-cpp> <title-data> <timeline-widget> "
                     "<hierarchy-model> <canvas-spatial> <canvas-input> <canvas-release> "
                     "<cache-storage> <cache-invalidation> <editor-connections> "
                     "<editor-transform> <obs-compositor> <prerender-worker> <changelog>\n";
        return 2;
    }

    const std::string animation_h = read_file(argv[1]);
    const std::string animation_cpp = read_file(argv[2]);
    const std::string title_data = read_file(argv[3]);
    const std::string timeline = read_file(argv[4]);
    const std::string hierarchy = read_file(argv[5]);
    const std::string canvas = read_file(argv[6]);
    const std::string canvas_input = read_file(argv[7]);
    const std::string release = read_file(argv[8]);
    const std::string cache_storage = read_file(argv[9]);
    const std::string cache_invalidation = read_file(argv[10]);
    const std::string editor_connections = read_file(argv[11]);
    const std::string editor_transform = read_file(argv[12]);
    const std::string obs_compositor = read_file(argv[13]);
    const std::string prerender_worker = read_file(argv[14]);
    const std::string changelog = read_file(argv[15]);

    bool ok = true;

    ok &= contains(animation_h, "enum class SpatialInterpolationMode", "spatial mode enum");
    ok &= contains(animation_h, "Linear = 0", "legacy-safe Linear enum value");
    ok &= contains(animation_h, "AutoBezier", "Auto Bezier mode");
    ok &= contains(animation_h, "ContinuousBezier", "Continuous Bezier mode");
    ok &= contains(animation_h, "ManualBezier", "Manual Bezier mode");
    ok &= contains(animation_h, "Vec2Value incoming_tangent", "incoming local tangent");
    ok &= contains(animation_h, "Vec2Value outgoing_tangent", "outgoing local tangent");
    ok &= contains(animation_h, "spatial_tangents_linked", "broken/linked state");
    ok &= contains(animation_h, "rove_across_time", "rove-across-time state");
    ok &= contains(animation_h, "split_spatial_segment", "path insertion API");
    ok &= contains(animation_h, "spatial_mode = SpatialInterpolationMode::Linear",
                   "legacy keys default to linear");

    ok &= contains(animation_cpp, "evaluate_spatial_segment", "shared spatial evaluator");
    ok &= contains(animation_cpp, "cubic_bezier", "deterministic cubic evaluator");
    ok &= contains(animation_cpp, "return evaluate_spatial_segment(i, spatial_progress)",
                   "temporal progress feeds spatial interpolation");
    ok &= ordered(animation_cpp, "AnimatedProperty::ease(",
                  "evaluate_spatial_segment(i, spatial_progress)",
                  "temporal evaluation precedes spatial evaluation");
    ok &= contains(animation_cpp,
                   "k0.spatial_mode == SpatialInterpolationMode::Linear",
                   "exact legacy linear branch");
    ok &= contains(animation_cpp, "recalculate_rove_times", "deterministic roving timing");
    ok &= contains(animation_cpp, "split_spatial_segment", "deterministic path split");
    ok &= contains(animation_cpp, "const Vec2Value q0 = lerp", "de Casteljau subdivision");

    ok &= contains(title_data, "spatial_in_tangent", "incoming tangent persistence");
    ok &= contains(title_data, "spatial_out_tangent", "outgoing tangent persistence");
    ok &= contains(title_data, "spatial_mode", "mode persistence");
    ok &= contains(title_data, "spatial_tangents_linked", "link-state persistence");
    ok &= contains(title_data, "rove_across_time", "rove state persistence");
    ok &= contains(title_data,
                   "json_int(item, \"spatial_mode\", (int)SpatialInterpolationMode::Linear)",
                   "missing spatial metadata loads as Linear");

    ok &= contains(timeline, "entry.vector_keyframe", "copy stores full vector keyframe");
    ok &= contains(timeline, "VectorKeyframe pasted = entry.vector_keyframe",
                   "paste restores full vector keyframe");
    ok &= contains(timeline, "OBSTitles.SpatialInterpolation", "spatial mode context menu");
    ok &= contains(timeline, "OBSTitles.BreakSpatialTangents", "break tangents action");
    ok &= contains(timeline, "OBSTitles.JoinSpatialTangents", "join tangents action");
    ok &= contains(timeline, "OBSTitles.RoveAcrossTime", "timeline rove action");
    ok &= contains(timeline, "set_keyframe_rove_across_time", "timeline rove mutation");
    ok &= contains(timeline, "emit keyframe_easing_changed()",
                   "mode and join/break changes notify undo pipeline");

    ok &= contains(hierarchy, "supports_spatial_interpolation", "Position-only spatial UI gate");
    ok &= contains(hierarchy, "apply_spatial_mode", "mode mutation");
    ok &= contains(hierarchy, "set_spatial_tangents_linked", "break/join mutation");
    ok &= contains(hierarchy, "set_keyframe_rove_across_time", "rove mutation");

    ok &= contains(canvas, "editor_parent_world_transform", "parent/group transform mapping");
    ok &= contains(canvas, "parent.inverted(&invertible)", "drag converts world to local");
    ok &= contains(canvas, "evaluate_spatial_segment", "motion path uses core geometry");
    ok &= contains(canvas, "SpatialInterpolationMode::ContinuousBezier",
                   "linked tangent drag enters Continuous mode");
    ok &= contains(canvas, "SpatialInterpolationMode::ManualBezier",
                   "broken tangent drag enters Manual mode");
    ok &= contains(canvas, "begin_position_vertex_drag", "direct vertex editing");
    ok &= contains(canvas, "split_spatial_segment", "double-click curve subdivision");
    ok &= contains(canvas, "Qt::AltModifier", "Alt breaks selected tangent pair");
    ok &= contains(canvas, "Qt::ShiftModifier", "Shift constrains tangent angle");
    ok &= contains(canvas, "OBSTitles.RoveAcrossTime", "canvas rove context action");
    ok &= contains(canvas, "layer.position.evaluate(time)", "current-position marker");
    ok &= contains(canvas, "motion_path_arrow_point", "direction indication");
    ok &= contains(canvas, "motion_path_layer_depends_on", "parent/group child snap exclusion");
    ok &= contains(canvas, "transform_parent_id", "transform-parent child snap exclusion");
    ok &= contains(canvas, "invalidate_selection_overlay_cache", "overlay-only hover invalidation");
    ok &= absent_after(canvas, "bool CanvasPreview::update_position_motion_path_hover",
                       "refresh_preview()", "hover must not request a rendered canvas frame");
    ok &= absent_after(canvas, "bool CanvasPreview::update_position_motion_path_hover",
                       "gpu_model_dirty_", "hover must not dirty the render model");
    ok &= contains(canvas_input, "add_position_keyframe_at_path(ev->pos())",
                   "double-click path insertion wiring");
    ok &= contains(canvas_input, "begin_position_vertex_drag(ev->pos())",
                   "vertex drag input wiring");
    ok &= contains(release, "DragMode::PositionVertex", "vertex drag release path");
    ok &= contains(release, "emit layer_geometry_changed()",
                   "completed tangent drag enters undo history");

    ok &= contains(cache_storage, "k.incoming_tangent.x", "cache hash incoming tangent");
    ok &= contains(cache_storage, "k.outgoing_tangent.x", "cache hash outgoing tangent");
    ok &= contains(cache_storage, "k.spatial_mode", "cache hash spatial mode");
    ok &= contains(cache_storage, "k.spatial_tangents_linked", "cache hash link state");
    ok &= contains(cache_storage, "k.rove_across_time", "cache hash rove state");
    ok &= contains(cache_invalidation, "resolved_outgoing_tangent",
                   "dirty bounds include outgoing cubic control point");
    ok &= contains(cache_invalidation, "resolved_incoming_tangent",
                   "dirty bounds include incoming cubic control point");

    ok &= contains(editor_connections,
                   "TimelineWidget::keyframe_easing_changed",
                   "timeline spatial changes connect to model undo");
    ok &= contains(editor_connections, "on_title_modified()",
                   "model change snapshot path");
    ok &= contains(editor_transform, "layer.position.evaluate(lt)",
                   "editor transform uses common vector evaluator");
    ok &= contains(obs_compositor, "layer.position.evaluate(local_time)",
                   "OBS output uses common vector evaluator");
    ok &= contains(prerender_worker, "render_title_gpu_cache_submit_readback",
                   "prerender delegates to the OBS GPU title renderer");
    ok &= contains(changelog, "Development Version 119 — Spatial Bezier Keyframes Core",
                   "spatial core revision documentation");
    ok &= contains(changelog, "Development Version 120 — On-Canvas Motion Paths",
                   "on-canvas motion path revision documentation");

    return ok ? 0 : 1;
}
