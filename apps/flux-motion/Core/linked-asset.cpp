#include "linked-asset.h"

#include "asset-runtime.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace fxm::linked_asset {
namespace {

void offset_position(Layer &layer, double dx, double dy)
{
    layer.position.static_value.x += dx;
    layer.position.static_value.y += dy;
    for (VectorKeyframe &key : layer.position.keyframes) {
        key.value.x += dx;
        key.value.y += dy;
    }
}

bool refresh_instance(Title &host, const std::shared_ptr<Layer> &instance,
                      const Title &source, const IdFactory &make_id)
{
    if (!instance || instance->type != LayerType::Asset ||
        instance->asset_title_id.empty())
        return false;

    std::unordered_set<std::string> owned_ids{instance->id};
    bool expanded = true;
    while (expanded) {
        expanded = false;
        for (const auto &layer : host.layers) {
            if (!layer || layer == instance || layer->asset_owner_id.empty() ||
                owned_ids.find(layer->id) != owned_ids.end())
                continue;
            if (owned_ids.find(layer->asset_owner_id) != owned_ids.end()) {
                owned_ids.insert(layer->id);
                expanded = true;
            }
        }
    }

    std::unordered_map<std::string, std::shared_ptr<Layer>> existing_by_source;
    for (const auto &layer : host.layers) {
        if (layer && owned_ids.find(layer->id) != owned_ids.end() &&
            layer != instance && !layer->asset_source_layer_id.empty())
            existing_by_source[layer->asset_source_layer_id] = layer;
    }

    std::unordered_map<std::string, std::string> id_map;
    for (const auto &source_layer : source.layers) {
        if (!source_layer)
            continue;
        const auto existing = existing_by_source.find(source_layer->id);
        id_map[source_layer->id] = existing != existing_by_source.end()
            ? existing->second->id : make_id();
    }

    host.cameras.erase(std::remove_if(host.cameras.begin(), host.cameras.end(),
        [&](const TitleCamera &camera) {
            return owned_ids.find(camera.asset_space_owner_id) != owned_ids.end();
        }), host.cameras.end());

    std::unordered_map<std::string, std::string> camera_id_map;
    for (const TitleCamera &source_camera : source.cameras) {
        if (source_camera.id.empty())
            continue;
        TitleCamera camera = source_camera;
        camera.id = make_id();
        const auto owner = id_map.find(source_camera.asset_space_owner_id);
        camera.asset_space_owner_id = owner != id_map.end()
            ? owner->second : instance->id;
        camera.timeline_expanded = false;
        camera_id_map[source_camera.id] = camera.id;
        host.cameras.push_back(std::move(camera));
    }

    auto remap_camera = [&](std::string camera_id, double source_time) {
        if (camera_id.empty()) {
            camera_id = source.active_camera.evaluate(std::max(0.0, source_time));
            if (camera_id.empty())
                camera_id = source.active_camera_id;
        }
        const auto mapped = camera_id_map.find(camera_id);
        if (mapped != camera_id_map.end())
            return mapped->second;
        const auto fallback = camera_id_map.find(source.active_camera_id);
        return fallback != camera_id_map.end() ? fallback->second : std::string();
    };

    std::vector<std::shared_ptr<Layer>> refreshed;
    refreshed.reserve(source.layers.size());
    const double source_center_x = instance->asset_space_center_x;
    const double source_center_y = instance->asset_space_center_y;
    for (const auto &source_layer : source.layers) {
        if (!source_layer)
            continue;
        auto clone = std::make_shared<Layer>(*source_layer);
        clone->id = id_map[source_layer->id];
        clone->asset_source_layer_id = source_layer->id;
        const auto owner = id_map.find(source_layer->asset_owner_id);
        clone->asset_owner_id = owner != id_map.end()
            ? owner->second : instance->id;
        const auto parent = id_map.find(source_layer->parent_id);
        clone->parent_id = parent != id_map.end()
            ? parent->second : instance->id;
        const auto media_owner = id_map.find(source_layer->linked_media_layer_id);
        if (media_owner != id_map.end()) {
            clone->linked_media_layer_id = media_owner->second;
        } else if (clone->linked_media_stream) {
            clone->linked_media_layer_id.clear();
            clone->linked_media_stream = false;
        }
        const auto transform_parent = id_map.find(source_layer->transform_parent_id);
        clone->transform_parent_id = transform_parent != id_map.end()
            ? transform_parent->second : std::string();
        const auto mask = id_map.find(source_layer->mask_source_id);
        if (mask != id_map.end()) {
            clone->mask_source_id = mask->second;
        } else {
            clone->mask_source_id.clear();
            clone->mask_mode = MaskMode::None;
        }

        const bool follows_source_camera = source_layer->camera_id.empty() &&
            source_layer->camera_assignment.static_value.empty() &&
            source_layer->camera_assignment.keyframes.empty();
        if (follows_source_camera) {
            clone->camera_assignment = source.active_camera;
            clone->camera_assignment.name = "camera_assignment";
            clone->asset_camera_uses_owner_time = true;
        }
        clone->camera_assignment.static_value = remap_camera(
            clone->camera_assignment.static_value.empty()
                ? source_layer->camera_id
                : clone->camera_assignment.static_value,
            0.0);
        for (DiscreteKeyframe &key : clone->camera_assignment.keyframes)
            key.value = remap_camera(key.value, key.time);
        clone->camera_id = clone->camera_assignment.static_value;

        if (source_layer->parent_id.empty())
            offset_position(*clone, -source_center_x, -source_center_y);

        /* Exposed values belong to the instance. All other authored fields
         * continue to follow the source asset. */
        const auto previous = existing_by_source.find(source_layer->id);
        if (source_layer->expose_text && previous != existing_by_source.end()) {
            if (source_layer->type == LayerType::Text ||
                source_layer->type == LayerType::Ticker ||
                source_layer->type == LayerType::Clock) {
                clone->text_content = previous->second->text_content;
                clone->rich_text = previous->second->rich_text;
            } else if (source_layer->type == LayerType::Image) {
                clone->image_path = previous->second->image_path;
            }
        }
        clone->group_collapsed = clone->type == LayerType::Asset
            ? true : clone->group_collapsed;
        refreshed.push_back(std::move(clone));
    }

    host.layers.erase(std::remove_if(host.layers.begin(), host.layers.end(),
        [&](const std::shared_ptr<Layer> &layer) {
            return layer && layer != instance &&
                owned_ids.find(layer->id) != owned_ids.end();
        }), host.layers.end());
    const auto instance_position = std::find(host.layers.begin(), host.layers.end(), instance);
    host.layers.insert(instance_position, refreshed.begin(), refreshed.end());

    instance->asset_category = source.asset_category;
    instance->asset_animated = asset_runtime::title_has_timeline_animation(source);
    instance->asset_duration = std::max(0.1, source.duration);
    instance->asset_source_playback_mode = std::clamp(source.playback_mode, 0, 2);
    instance->asset_source_loop_type = std::clamp(source.loop_type, 0, 1);
    instance->asset_source_loop_start = std::clamp(
        source.loop_start, 0.0, instance->asset_duration);
    instance->asset_source_loop_end = std::clamp(
        source.loop_end, instance->asset_source_loop_start,
        instance->asset_duration);
    instance->asset_source_pause_time = std::clamp(
        source.pause_time, 0.0, instance->asset_duration);
    instance->asset_space_width = std::max(1, source.width);
    instance->asset_space_height = std::max(1, source.height);
    if (!instance->asset_animated)
        instance->asset_playback_mode = 0;
    return true;
}

} // namespace

bool refresh_graph(std::vector<std::shared_ptr<Title>> &titles,
                   const SourceResolver &fallback_resolver,
                   const IdFactory &make_id)
{
    if (!make_id)
        return false;
    std::unordered_map<std::string, std::shared_ptr<Title>> local;
    for (const auto &title : titles)
        if (title && !title->id.empty()) local[title->id] = title;

    std::unordered_map<std::string, int> state;
    bool changed = false;
    std::function<void(const std::shared_ptr<Title> &)> resolve_title;
    resolve_title = [&](const std::shared_ptr<Title> &title) {
        if (!title || state[title->id] == 2)
            return;
        if (state[title->id] == 1)
            return;
        state[title->id] = 1;
        std::vector<std::shared_ptr<Layer>> instances;
        for (const auto &layer : title->layers) {
            if (layer && layer->type == LayerType::Asset &&
                layer->asset_owner_id.empty() && !layer->asset_title_id.empty())
                instances.push_back(layer);
        }
        for (const auto &instance : instances) {
            std::shared_ptr<const Title> source;
            const auto local_source = local.find(instance->asset_title_id);
            if (local_source != local.end()) {
                resolve_title(local_source->second);
                source = local_source->second;
            } else if (fallback_resolver) {
                source = fallback_resolver(instance->asset_title_id);
            }
            if (source && source.get() != title.get())
                changed |= refresh_instance(*title, instance, *source, make_id);
        }
        state[title->id] = 2;
    };
    for (const auto &title : titles)
        resolve_title(title);
    return changed;
}

} // namespace fxm::linked_asset
