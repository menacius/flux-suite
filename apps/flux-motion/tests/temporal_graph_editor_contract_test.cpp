#include <iostream>
#include <string>

#include "source_bundle_reader.h"

namespace {

bool require(const std::string &source, const std::string &needle,
             const char *label)
{
    if (source.find(needle) != std::string::npos) return true;
    std::cerr << "Missing temporal Graph Editor contract: " << label
              << " (" << needle << ")\n";
    return false;
}

bool absent_between(const std::string &source, const std::string &begin,
                    const std::string &end, const std::string &needle,
                    const char *label)
{
    const size_t a = source.find(begin);
    const size_t b = a == std::string::npos ? std::string::npos : source.find(end, a);
    if (a != std::string::npos && b != std::string::npos &&
        source.substr(a, b - a).find(needle) == std::string::npos)
        return true;
    std::cerr << "Invalid temporal Graph Editor contract: " << label << "\n";
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 15) {
        std::cerr << "usage: temporal_graph_editor_contract_test "
                     "<animation-h> <animation-cpp> <title-data> <hierarchy> "
                     "<timeline-cpp> <timeline-h> <graph-editor> <toolbar> "
                     "<effect-animation> <cache-storage> <editor-transform> "
                     "<obs-compositor> <prerender-worker> <changelog>\n";
        return 2;
    }

    const std::string animation_h = read_file(argv[1]);
    const std::string animation_cpp = read_file(argv[2]);
    const std::string title_data = read_file(argv[3]);
    const std::string hierarchy = read_file(argv[4]);
    const std::string timeline_cpp = read_file(argv[5]);
    const std::string timeline_h = read_file(argv[6]);
    const std::string graph = read_file(argv[7]);
    const std::string toolbar = read_file(argv[8]);
    const std::string effect_animation = read_file(argv[9]);
    const std::string cache_storage = read_file(argv[10]);
    const std::string editor_transform = read_file(argv[11]);
    const std::string obs_compositor = read_file(argv[12]);
    const std::string prerender_worker = read_file(argv[13]);
    const std::string changelog = read_file(argv[14]);

    bool ok = true;
    ok &= require(animation_h, "enum class TemporalInterpolationMode", "temporal mode enum");
    ok &= require(animation_h, "Linear = 0", "stable Linear mode value");
    ok &= require(animation_h, "Hold", "Hold mode");
    ok &= require(animation_h, "AutoBezier", "Auto Bezier mode");
    ok &= require(animation_h, "ContinuousBezier", "Continuous Bezier mode");
    ok &= require(animation_h, "ManualBezier", "Manual Bezier mode");
    ok &= require(animation_h, "incoming_influence", "incoming influence persistence field");
    ok &= require(animation_h, "outgoing_influence", "outgoing influence persistence field");
    ok &= require(animation_h, "incoming_speed", "incoming speed persistence field");
    ok &= require(animation_h, "outgoing_speed", "outgoing speed persistence field");
    ok &= require(animation_h, "temporal_tangents_linked", "temporal linked state");
    ok &= require(animation_h, "temporal_velocity_explicit = false", "legacy-safe explicit flag");
    ok &= require(animation_h, "evaluate_temporal_segment", "shared temporal evaluator API");
    ok &= require(animation_h, "evaluate_temporal_segment_velocity", "shared speed evaluator API");

    ok &= require(animation_cpp, "temporal_parameter_for_time", "deterministic cubic time inversion");
    ok &= require(animation_cpp, "for (int i = 0; i < 32; ++i)", "deterministic bisection fallback");
    ok &= require(animation_cpp, "finite_or_zero(segment.outgoing_speed)", "finite manual velocity handling");
    ok &= require(animation_cpp, "return cubic_scalar(segment.start_value, y1, y2, segment.end_value, u)",
                  "unclamped cubic value output");
    ok &= require(animation_cpp, "Keep authored handle lengths unchanged",
                  "graph controls match temporal evaluation");
    ok &= absent_between(animation_cpp, "double evaluate_temporal_segment(",
                         "double evaluate_temporal_segment_velocity(", "std::clamp(segment.start_value",
                         "property values are not clamped");
    ok &= require(animation_cpp, "if (!(span > kAnimationEpsilon))", "short interval guard");
    ok &= require(animation_cpp, "return evaluate_temporal_segment(temporal_segment(segment), t)",
                  "scalar uses common evaluator");
    ok &= require(animation_cpp, "const double progress = evaluate_temporal_segment(segment, t)",
                  "position uses common evaluator");
    ok &= require(animation_cpp, "apply_easy_ease", "Easy Ease model command");

    ok &= require(title_data, "temporal_in_influence", "incoming influence serialization");
    ok &= require(title_data, "temporal_out_influence", "outgoing influence serialization");
    ok &= require(title_data, "temporal_in_speed", "incoming speed serialization");
    ok &= require(title_data, "temporal_out_speed", "outgoing speed serialization");
    ok &= require(title_data, "temporal_velocity_explicit", "legacy compatibility serialization");
    ok &= require(title_data, "json_bool(j, \"temporal_velocity_explicit\", false)",
                  "old scalar keyframes remain legacy");
    ok &= require(title_data, "json_bool(item, \"temporal_velocity_explicit\", false)",
                  "old position keyframes remain legacy");

    ok &= require(timeline_h, "set_graph_editor_enabled", "Graph Editor timeline mode API");
    ok &= require(timeline_h, "enum class GraphViewMode { Value = 0, Speed = 1 }", "Value/Speed views");
    ok &= require(timeline_h, "GraphIncomingHandle", "incoming handle drag state");
    ok &= require(timeline_h, "GraphOutgoingHandle", "outgoing handle drag state");
    ok &= require(timeline_h, "TemporalDragSnapshot", "multi-edit relative-value snapshots");

    ok &= require(graph, "paint_graph_editor", "Graph Editor renderer");
    ok &= require(graph, "channel.graph_value(local_time)", "Value Graph samples final evaluator");
    ok &= require(graph, "channel.graph_speed(local_time)", "Speed Graph samples final evaluator");
    ok &= require(graph, "graph_x_to_time", "sub-frame graph sampling");
    ok &= require(graph, "owner->in_time + time", "local-to-timeline coordinate mapping");
    ok &= require(graph, "DragMode::GraphIncomingHandle", "incoming speed/influence dragging");
    ok &= require(graph, "DragMode::GraphOutgoingHandle", "outgoing speed/influence dragging");
    ok &= require(graph, "influence_delta", "relative multi-influence edit");
    ok &= require(graph, "speed_delta", "relative multi-speed edit");
    ok &= require(graph, "global_key_time - pointer_time",
                  "incoming handles remain on the incoming side");
    ok &= require(graph, "pointer_time - global_key_time",
                  "outgoing handles remain on the outgoing side");
    ok &= require(graph, "value_delta", "relative multi-value edit");
    ok &= require(graph, "selected_times", "selection identity captured before keyframe sorting");
    ok &= require(graph, "used_indices", "selection remapped after keyframe crossing");
    ok &= require(graph, "Qt::AltModifier", "Alt breaks temporal handles");
    ok &= require(graph, "QDialogButtonBox::Ok", "numeric Keyframe Velocity dialog");
    ok &= require(graph, "OBSTitles.IncomingInfluence", "incoming influence numeric field");
    ok &= require(graph, "OBSTitles.OutgoingSpeed", "outgoing speed numeric field");
    ok &= require(graph, "fit_graph_time_range", "horizontal fit graphs/selection");
    ok &= require(graph, "DragMode::GraphPan", "graph panning");
    ok &= require(graph, "GraphMarquee", "multi-keyframe marquee selection");
    ok &= require(graph, "emit keyframe_easing_changed()", "shared timeline undo notification");
    ok &= require(graph, "OBSTitles.EasyEaseIn", "Easy Ease In command");
    ok &= require(graph, "OBSTitles.EasyEaseOut", "Easy Ease Out command");

    ok &= require(toolbar, "OBSTitles.GraphEditor", "Graph Editor toolbar toggle");
    ok &= require(toolbar, "OBSTitles.ValueGraph", "Value Graph toolbar view");
    ok &= require(toolbar, "OBSTitles.SpeedGraph", "Speed Graph toolbar view");
    ok &= require(toolbar, "fit_graph_to_view", "Fit Graphs toolbar action");
    ok &= require(toolbar, "fit_graph_selection", "Fit Selection toolbar action");
    ok &= require(toolbar, "TimelineWidget::keyframe_easing_changed", "timeline changes use editor undo path");
    ok &= require(toolbar, "on_title_modified()", "Graph Editor commits title snapshot undo");

    ok &= require(timeline_cpp, "OBSTitles.TemporalInterpolation", "normal timeline temporal menu");
    ok &= require(timeline_cpp, "OBSTitles.KeyframeVelocity", "normal timeline numeric dialog action");
    ok &= require(timeline_cpp, "apply_easy_ease", "normal timeline Easy Ease mutation");
    ok &= require(timeline_cpp, "apply_temporal_mode", "normal timeline mode mutation");

    ok &= require(hierarchy, "keyframe_temporal_influence", "property abstraction influence read");
    ok &= require(hierarchy, "keyframe_temporal_speed", "property abstraction speed read");
    ok &= require(hierarchy, "set_temporal_handle", "property abstraction handle mutation");
    ok &= require(hierarchy, "apply_temporal_mode", "property abstraction mode mutation");
    ok &= require(hierarchy, "apply_easy_ease", "property abstraction Easy Ease mutation");
    ok &= require(hierarchy,
                  "key.insert(QStringLiteral(\"temporal_velocity_explicit\"), false)",
                  "legacy extension easing exits explicit velocity mode");

    ok &= require(effect_animation, "evaluate_temporal_segment(segment, time)",
                  "extension properties share core evaluator");
    ok &= require(effect_animation, "TemporalInterpolationMode::AutoBezier", "extension Auto Bezier");
    ok &= require(effect_animation, "automatic_speed", "extension automatic velocity refresh");
    ok &= require(effect_animation, "key = keys.at(existing).toObject()",
                  "extension key updates preserve temporal metadata");
    ok &= require(effect_animation, "write_track_keys_preserve_order",
                  "interactive extension drags preserve keyframe index identity");
    ok &= require(hierarchy, "write_track_keys_preserve_order",
                  "extension key times sort only after drag completion");

    ok &= require(cache_storage, "k.incoming_influence", "cache hash incoming influence");
    ok &= require(cache_storage, "k.outgoing_speed", "cache hash outgoing speed");
    ok &= require(cache_storage, "k.temporal_velocity_explicit", "cache hash temporal representation");
    ok &= require(editor_transform, "editor_evaluated_local_position_xy", "editor uses canonical local position evaluation");
    ok &= require(obs_compositor, "layer.position.evaluate(local_time)", "OBS uses common position evaluation");
    ok &= require(prerender_worker, "render_title_gpu_cache_submit_readback",
                  "prerender delegates to common OBS renderer");
    ok &= require(changelog, "Development Version 121 — Temporal Graph Editor and Manual Velocity Handles",
                  "revision documentation");

    return ok ? 0 : 1;
}
