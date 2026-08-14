#include "asset-runtime.h"
#include "linked-asset.h"
#include "title-data.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace {
bool near(double actual, double expected, const char *label)
{
    if (std::abs(actual - expected) <= 0.0001)
        return true;
    std::cerr << label << ": expected " << expected << ", got " << actual << "\n";
    return false;
}
}

int main()
{
    bool ok = true;

    Layer asset;
    asset.id = "asset-layer";
    asset.type = LayerType::Asset;
    asset.asset_animated = true;
    asset.asset_playback_mode = 1;
    asset.asset_duration = 10.0;
    asset.asset_loop = false;

    asset.asset_source_playback_mode = 2;
    asset.asset_source_pause_time = 3.0;
    asset.asset_pause_duration = 2.0;
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 2.0), 2.0,
               "pause mode before marker");
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 4.0), 3.0,
               "pause mode hold");
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 6.0), 4.0,
               "pause mode resume");

    asset.asset_source_playback_mode = 1;
    asset.asset_source_loop_type = 0;
    asset.asset_source_loop_start = 2.0;
    asset.asset_source_loop_end = 4.0;
    asset.asset_loop_count = 3;
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 6.5), 2.5,
               "restart finite loop");
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 9.0), 5.0,
               "restart loop outro");

    asset.asset_source_loop_type = 1;
    asset.asset_loop_count = 2;
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 4.5), 3.5,
               "ping-pong reverse leg");
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 6.5), 2.5,
               "ping-pong final forward leg");
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 9.0), 5.0,
               "ping-pong outro");

    asset.asset_source_playback_mode = 0;
    asset.asset_loop = true;
    ok &= near(fxm::asset_runtime::map_elapsed_to_local_time(asset, 12.5), 2.5,
               "whole animation loop");

    Title static_title;
    static_title.duration = 5.0;
    auto static_layer = std::make_shared<Layer>();
    static_layer->type = LayerType::Clock;
    static_layer->out_time = 5.0;
    static_title.layers.push_back(static_layer);
    if (fxm::asset_runtime::title_has_timeline_animation(static_title)) {
        std::cerr << "clock-only asset must not expose timeline playback controls\n";
        ok = false;
    }

    static_layer->position.keyframes.push_back({0.0, {0.0, 0.0}});
    static_layer->position.keyframes.push_back({1.0, {100.0, 0.0}});
    if (!fxm::asset_runtime::title_has_timeline_animation(static_title)) {
        std::cerr << "keyframed asset must expose timeline playback controls\n";
        ok = false;
    }

    Title stroke_title;
    stroke_title.duration = 5.0;
    auto stroke_layer = std::make_shared<Layer>();
    stroke_layer->type = LayerType::SolidRect;
    stroke_layer->out_time = stroke_title.duration;
    stroke_layer->stroke_color_r.keyframes.push_back({0.0, 0.0});
    stroke_layer->stroke_color_r.keyframes.push_back({1.0, 255.0});
    stroke_title.layers.push_back(stroke_layer);
    if (!fxm::asset_runtime::title_has_timeline_animation(stroke_title)) {
        std::cerr << "stroke color keyframes must mark an asset as animated\n";
        ok = false;
    }

    Title media_title;
    media_title.duration = 5.0;
    auto media_layer = std::make_shared<Layer>();
    media_layer->type = LayerType::Video;
    media_layer->in_time = 0.0;
    media_layer->out_time = media_title.duration;
    media_title.layers.push_back(media_layer);
    if (!fxm::asset_runtime::title_has_timeline_animation(media_title)) {
        std::cerr << "media-only asset must expose timeline playback controls\n";
        ok = false;
    }

    Title instance_title;
    instance_title.duration = 10.0;
    auto root_asset = std::make_shared<Layer>();
    root_asset->id = "root-asset";
    root_asset->type = LayerType::Asset;
    root_asset->asset_duration = 10.0;
    root_asset->out_time = 10.0;
    auto nested_child = std::make_shared<Layer>();
    nested_child->id = "nested-child";
    nested_child->asset_owner_id = root_asset->id;
    nested_child->out_time = 10.0;
    nested_child->opacity.keyframes.push_back({0.0, 0.0});
    nested_child->opacity.keyframes.push_back({1.0, 1.0});
    instance_title.layers = {nested_child, root_asset};
    if (!fxm::asset_runtime::asset_layer_has_timeline_animation(instance_title,
                                                                 *root_asset)) {
        std::cerr << "nested keyframes must mark the Asset Layer as animated\n";
        ok = false;
    }

    root_asset->asset_animated = true;
    root_asset->asset_playback_mode = 1;
    if (!fxm::asset_runtime::title_has_independent_playback(instance_title)) {
        std::cerr << "independent animated asset must keep a runtime refresh clock\n";
        ok = false;
    }
    root_asset->asset_playback_mode = 0;
    if (fxm::asset_runtime::title_has_independent_playback(instance_title)) {
        std::cerr << "synchronized asset must not request independent refresh\n";
        ok = false;
    }

    Title source;
    source.id = "source-asset";
    source.is_asset = true;
    source.duration = 8.0;
    auto source_text = std::make_shared<Layer>();
    source_text->id = "source-text";
    source_text->type = LayerType::Text;
    source_text->expose_text = true;
    source_text->text_content = "source default changed";
    source_text->position.static_value = {100.0, 50.0};
    auto source_shape = std::make_shared<Layer>();
    source_shape->id = "source-shape";
    source_shape->type = LayerType::SolidRect;
    source_shape->fill_color = 0xff123456u;
    source.layers = {source_text, source_shape};

    Title host;
    host.id = "host-title";
    auto old_text = std::make_shared<Layer>(*source_text);
    old_text->id = "stable-text-instance";
    old_text->asset_source_layer_id = source_text->id;
    old_text->asset_owner_id = "asset-instance";
    old_text->text_content = "instance override";
    auto old_shape = std::make_shared<Layer>(*source_shape);
    old_shape->id = "stable-shape-instance";
    old_shape->asset_source_layer_id = source_shape->id;
    old_shape->asset_owner_id = "asset-instance";
    old_shape->fill_color = 0xff000000u;
    auto linked = std::make_shared<Layer>();
    linked->id = "asset-instance";
    linked->type = LayerType::Asset;
    linked->asset_title_id = source.id;
    linked->asset_space_center_x = 100.0;
    linked->asset_space_center_y = 50.0;
    host.layers = {old_text, old_shape, linked};

    std::vector<std::shared_ptr<Title>> graph = {
        std::make_shared<Title>(source), std::make_shared<Title>(host)};
    int generated_id = 0;
    if (!fxm::linked_asset::refresh_graph(
            graph, {}, [&]() { return "generated-" + std::to_string(++generated_id); })) {
        std::cerr << "linked asset graph must report refreshed instances\n";
        ok = false;
    }
    auto refreshed_host = graph[1];
    const auto find_layer = [&refreshed_host](const std::string &id) {
        for (const auto &layer : refreshed_host->layers)
            if (layer && layer->id == id) return layer;
        return std::shared_ptr<Layer>();
    };
    const auto refreshed_text = find_layer("stable-text-instance");
    const auto refreshed_shape = find_layer("stable-shape-instance");
    if (!refreshed_text || refreshed_text->text_content != "instance override") {
        std::cerr << "linked asset refresh must preserve exposed instance text\n";
        ok = false;
    }
    if (!refreshed_shape || refreshed_shape->fill_color != 0xff123456u) {
        std::cerr << "linked asset refresh must apply source layer changes\n";
        ok = false;
    }
    if (!refreshed_text || !near(refreshed_text->position.static_value.x, 0.0,
                                  "linked source-space position"))
        ok = false;
    if (linked->asset_duration != 8.0) {
        std::cerr << "linked asset refresh must apply source duration\n";
        ok = false;
    }

    return ok ? 0 : 1;
}
