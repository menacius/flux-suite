#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fxm::serialization {

using json = nlohmann::json;

/* The title store intentionally remains a top-level JSON array so builds made
 * before schema versioning can still open newly-saved scene collections. The
 * schema/development identity therefore lives on every title object. Template
 * exports additionally expose the same values on their object root. */
inline constexpr int kCurrentTitleSchemaVersion = 6;
inline constexpr int kCurrentDevelopmentVersion = 265;
inline constexpr int kFirstAuditedDevelopmentVersion = 144;
inline constexpr int kCurrentProxyManifestSchemaVersion = 2;
inline constexpr int kCurrentDockSettingsSchemaVersion = 2;
inline constexpr int kCurrentFormattingSchemaVersion = 2;
inline constexpr int kCurrentPatternSchemaVersion = 1;

struct MigrationReport {
    int source_schema_version = 0;
    int source_development_version = 0;
    int target_schema_version = kCurrentTitleSchemaVersion;
    int target_development_version = kCurrentDevelopmentVersion;
    std::vector<int> applied_development_versions;
    std::vector<std::string> recoveries;
    std::vector<std::string> warnings;

    bool changed() const
    {
        return source_schema_version != target_schema_version ||
               source_development_version != target_development_version ||
               !recoveries.empty() || !applied_development_versions.empty();
    }
};

inline int safe_integer(const json &object, const char *key, int fallback)
{
    if (!object.is_object())
        return fallback;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer())
        return fallback;
    const auto value = it->get<std::int64_t>();
    if (value < 0 || value > 1000000)
        return fallback;
    return static_cast<int>(value);
}

inline double safe_number(const json &object, const char *key, double fallback)
{
    if (!object.is_object())
        return fallback;
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number())
        return fallback;
    const double value = it->get<double>();
    return std::isfinite(value) ? value : fallback;
}

inline json animated_scalar_default(double value);
inline json animated_discrete_default(const std::string &value);

inline bool title_has_layer_type(const json &title, int type)
{
    const auto layers = title.find("layers");
    if (layers == title.end() || !layers->is_array())
        return false;
    for (const auto &layer : *layers) {
        if (layer.is_object() && safe_integer(layer, "type", -1) == type)
            return true;
    }
    return false;
}

inline int infer_development_version(const json &title)
{
    const int explicit_version = safe_integer(title, "development_version", 0);
    if (explicit_version > 0)
        return explicit_version;

    /* Inference is deliberately conservative. It is used only to skip
     * migrations that clearly predate the fields already present. */
    if (title_has_layer_type(title, 12))
        return 227;
    if (title.contains("layers") && title["layers"].is_array()) {
        for (const auto &layer : title["layers"])
            if (layer.is_object() &&
                (layer.contains("position_3d") ||
                 safe_integer(layer, "position_3d_path_enabled", 0) != 0))
                return 208;
    }
    if (title.contains("cameras") && title["cameras"].is_array()) {
        for (const auto &camera : title["cameras"])
            if (camera.is_object() &&
                (camera.contains("position_3d") || camera.contains("target_3d")))
                return 208;
    }
    if (title.contains("stinger_switch_mode") || title_has_layer_type(title, 11))
        return 168;
    if (title.contains("stinger_transition_point") ||
        safe_integer(title, "graphic_type", 0) == 3)
        return 167;
    if (title_has_layer_type(title, 10))
        return 145;
    return kFirstAuditedDevelopmentVersion - 1;
}

inline void recover_array_member(json &object, const char *key,
                                 MigrationReport &report)
{
    if (!object.is_object() || !object.contains(key))
        return;
    if (!object[key].is_array()) {
        object[key] = json::array();
        report.recoveries.emplace_back(std::string(key) + " was not an array");
    }
}

inline void recover_object_member(json &object, const char *key,
                                  MigrationReport &report)
{
    if (!object.is_object() || !object.contains(key))
        return;
    if (!object[key].is_object()) {
        object[key] = json::object();
        report.recoveries.emplace_back(std::string(key) + " was not an object");
    }
}

inline void migrate_formatting_and_patterns(json &title, MigrationReport &report)
{
    if (!title.contains("layers") || !title["layers"].is_array())
        return;
    for (auto &layer : title["layers"]) {
        if (!layer.is_object() || !layer.contains("rich_text"))
            continue;
        if (!layer["rich_text"].is_object()) {
            layer.erase("rich_text");
            report.recoveries.emplace_back("discarded malformed rich_text object");
            continue;
        }
        auto &document = layer["rich_text"];
        document["formatting_schema_version"] = kCurrentFormattingSchemaVersion;
        recover_array_member(document, "auto_style_rules", report);
        if (document.contains("auto_style_rules") && document["auto_style_rules"].is_array()) {
            auto &rules = document["auto_style_rules"];
            const auto old_size = rules.size();
            rules.erase(std::remove_if(rules.begin(), rules.end(),
                                       [](const json &rule) { return !rule.is_object(); }),
                        rules.end());
            if (rules.size() != old_size)
                report.recoveries.emplace_back("discarded malformed formatting/pattern rules");
            for (auto &rule : document["auto_style_rules"]) {
                rule["pattern_schema_version"] = kCurrentPatternSchemaVersion;
                recover_array_member(rule, "excludes_rule_ids", report);
            }
        }
    }
}

inline void migrate_external_data(json &title, MigrationReport &report)
{
    recover_array_member(title, "external_data_sources", report);
    recover_array_member(title, "live_text_external_bindings", report);
    recover_array_member(title, "live_text_table_bindings", report);
    recover_array_member(title, "live_text_cue_style_overrides", report);

    if (title.contains("external_data_sources") &&
        title["external_data_sources"].is_array()) {
        for (auto &source : title["external_data_sources"]) {
            if (!source.is_object())
                continue;
            recover_array_member(source, "fields", report);
            if (source.contains("provider") && !source["provider"].is_object()) {
                source["provider"] = json::object();
                report.recoveries.emplace_back("reset malformed external data provider");
            }
        }
    }

    if (title.contains("layers") && title["layers"].is_array()) {
        for (auto &layer : title["layers"]) {
            if (layer.is_object())
                recover_array_member(layer, "external_bindings", report);
        }
    }

    if (title.contains("live_text_external_bindings") &&
        title["live_text_external_bindings"].is_array()) {
        for (auto &cell : title["live_text_external_bindings"]) {
            if (!cell.is_object())
                continue;
            if (cell.contains("binding") && !cell["binding"].is_object()) {
                cell["binding"] = json::object();
                report.recoveries.emplace_back("reset malformed live-text external binding");
            }
        }
    }

    if (title.contains("live_text_table_bindings") &&
        title["live_text_table_bindings"].is_array()) {
        for (auto &mapping : title["live_text_table_bindings"]) {
            if (mapping.is_object())
                recover_array_member(mapping, "columns", report);
        }
    }
}

inline void migrate_audio_layers(json &title, MigrationReport &report)
{
    if (!title.contains("layers") || !title["layers"].is_array())
        return;
    for (auto &layer : title["layers"]) {
        if (!layer.is_object() ||
            (safe_integer(layer, "type", -1) != 10 && !layer.contains("audio_source")))
            continue;
        recover_array_member(layer, "audio_effects", report);
        recover_array_member(layer, "audio_waveform", report);
        if (!layer.contains("audio_volume_prop")) {
            layer["audio_volume_prop"] = {
                {"static_value", safe_number(layer, "audio_volume", 1.0)},
                {"keyframes", json::array()}};
        }
        if (!layer.contains("audio_pan_prop")) {
            layer["audio_pan_prop"] = {
                {"static_value", safe_number(layer, "audio_pan", 0.0)},
                {"keyframes", json::array()}};
        }
    }
}

inline void validate_recover_title_shape(json &title, MigrationReport &report);

inline bool json_string_nonempty(const json &object, const char *key)
{
    if (!object.is_object())
        return false;
    const auto it = object.find(key);
    return it != object.end() && it->is_string() && !it->get<std::string>().empty();
}

inline bool json_truthy(const json &object, const char *key)
{
    if (!object.is_object())
        return false;
    const auto it = object.find(key);
    if (it == object.end())
        return false;
    if (it->is_boolean())
        return it->get<bool>();
    if (it->is_number_integer())
        return it->get<int>() != 0;
    return false;
}

inline void migrate_serialization_audit_244(json &title, MigrationReport &report)
{
    validate_recover_title_shape(title, report);
    if (!title.contains("layers") || !title["layers"].is_array())
        return;

    for (auto &layer : title["layers"]) {
        if (!layer.is_object())
            continue;

        recover_array_member(layer, "effects", report);
        recover_array_member(layer, "audio_effects", report);

        if (safe_integer(layer, "type", -1) == 12 || layer.contains("video_source")) {
            if (!layer.contains("video_source_relative") && json_string_nonempty(layer, "video_source")) {
                const std::string source = layer["video_source"].get<std::string>();
                const bool looks_absolute = source.size() > 1 &&
                    (source[0] == '/' || source[0] == '\\' ||
                     (source.size() > 2 && source[1] == ':'));
                layer[looks_absolute ? "video_source_absolute" : "video_source_relative"] = source;
            }
            if (!layer.contains("video_selected_streams"))
                layer["video_selected_streams"] = json::object();
            if (!layer.contains("video_audio_stream_index"))
                layer["video_audio_stream_index"] = -1;
            if (!layer.contains("video_color_primaries"))
                layer["video_color_primaries"] = "";
            if (!layer.contains("video_color_transfer"))
                layer["video_color_transfer"] = "";
            if (!layer.contains("video_color_matrix"))
                layer["video_color_matrix"] = "";
            if (!layer.contains("video_color_range"))
                layer["video_color_range"] = "";
            if (!layer.contains("video_decode_settings"))
                layer["video_decode_settings"] = json::object();
            if (!layer.contains("video_prefer_hardware_decode"))
                layer["video_prefer_hardware_decode"] = true;
            if (!layer.contains("video_allow_hardware_fallback"))
                layer["video_allow_hardware_fallback"] = true;
            if (!layer.contains("video_decode_cache_policy"))
                layer["video_decode_cache_policy"] = "auto";
            if (!layer.contains("video_time_remap_enabled"))
                layer["video_time_remap_enabled"] = false;
            if (!layer.contains("video_source_time"))
                layer["video_source_time"] = animated_scalar_default(safe_number(layer, "video_in_point", 0.0));
        }

        if (layer.contains("effects") && layer["effects"].is_array()) {
            for (auto &effect : layer["effects"]) {
                if (!effect.is_object())
                    continue;
                const int type = safe_integer(effect, "type", -1);
                if (!effect.contains("effect_id")) {
                    if (json_string_nonempty(effect, "extension_id"))
                        effect["effect_id"] = effect["extension_id"];
                    else
                        effect["effect_id"] = std::string("builtin-type-") + std::to_string(type);
                }
                if (!effect.contains("extension_id") && effect.contains("effect_id"))
                    effect["extension_id"] = effect["effect_id"];
                if (!effect.contains("extension_loaded_schema_version"))
                    effect["extension_loaded_schema_version"] =
                        std::max(1, safe_integer(effect, "extension_schema_version", 1));
                if (!effect.contains("extension_runtime_schema_version"))
                    effect["extension_runtime_schema_version"] =
                        std::max(1, safe_integer(effect, "extension_schema_version", 1));
                if (!effect.contains("extension_explicit_migration"))
                    effect["extension_explicit_migration"] = false;
                if (!effect.contains("extension_parameters"))
                    effect["extension_parameters"] = json::object();
                if (!effect.contains("extension_keyframes"))
                    effect["extension_keyframes"] = json::object();
                if (!effect.contains("extension_binary_state"))
                    effect["extension_binary_state"] = json::object();
                if (!effect.contains("missing_plugin_placeholder"))
                    effect["missing_plugin_placeholder"] = false;
                if (!effect.contains("effect_preset_schema_version"))
                    effect["effect_preset_schema_version"] = 0;

                /* Legacy Glow and Noise must not opt into revised runtime
                 * implementations silently. They keep the loaded schema version
                 * and parameters unless an explicit migration flag exists. */
                if ((type == 7 || type == 16) &&
                    !json_truthy(effect, "extension_explicit_migration")) {
                    effect["extension_runtime_schema_version"] =
                        effect["extension_loaded_schema_version"];
                }
            }
        }
    }

    report.recoveries.emplace_back("audited effect/plugin/video serialization envelope for Development Version 244");
}

inline void migrate_stinger_metadata(json &title, MigrationReport &report)
{
    const bool stinger = safe_integer(title, "graphic_type", 0) == 3 ||
                         title.contains("stinger_transition_point") ||
                         title.contains("stinger_transition_point_frames") ||
                         title.contains("stinger_transition_point_percent");
    if (!stinger)
        return;

    const double duration = std::max(0.1, safe_number(title, "duration", 5.0));
    if (!title.contains("stinger_transition_point")) {
        if (title.contains("stinger_transition_point_frames")) {
            const double fps = std::max(1.0, safe_number(title, "frame_rate", 30.0));
            title["stinger_transition_point"] =
                safe_number(title, "stinger_transition_point_frames", duration * fps * 0.5) / fps;
            report.recoveries.emplace_back("migrated Stinger transition point from frames");
        } else if (title.contains("stinger_transition_point_percent")) {
            const double percent = std::clamp(
                safe_number(title, "stinger_transition_point_percent", 50.0), 0.0, 100.0);
            title["stinger_transition_point"] = duration * percent / 100.0;
            report.recoveries.emplace_back("migrated Stinger transition point from percentage");
        }
    }
    if (!title.contains("stinger_pre_roll"))
        title["stinger_pre_roll"] = 0.0;
    if (!title.contains("stinger_post_roll"))
        title["stinger_post_roll"] = 0.0;
    if (!title.contains("stinger_audio_enabled"))
        title["stinger_audio_enabled"] = true;
    if (!title.contains("stinger_alpha_output"))
        title["stinger_alpha_output"] = true;
}

inline void migrate_stinger_switch_contract(json &title, MigrationReport &report)
{
    if (safe_integer(title, "graphic_type", 0) != 3 &&
        !title.contains("stinger_switch_mode"))
        return;
    if (!title.contains("stinger_switch_mode"))
        title["stinger_switch_mode"] = 0;
    const int background = safe_integer(title, "stinger_editor_background", 3);
    if (background == 0 || background == 1) {
        title["stinger_editor_background"] = 3;
        report.recoveries.emplace_back("migrated static Stinger A/B preview to follow-switch-point");
    }
}

inline void migrate_proxy_metadata(json &title, MigrationReport &report)
{
    if (!title.contains("proxy_metadata"))
        return;
    if (!title["proxy_metadata"].is_object()) {
        title["proxy_metadata"] = json::object();
        report.recoveries.emplace_back("reset malformed proxy metadata");
    }
    title["proxy_metadata"]["schema_version"] = kCurrentProxyManifestSchemaVersion;
}

inline void validate_recover_title_shape(json &title, MigrationReport &report)
{
    if (!title.is_object()) {
        title = json::object();
        report.recoveries.emplace_back("title root was not an object");
    }
    recover_array_member(title, "layers", report);
    recover_array_member(title, "cameras", report);
    recover_array_member(title, "expanded_property_channels", report);
    recover_object_member(title, "proxy_metadata", report);
    recover_array_member(title, "external_data_sources", report);
    recover_array_member(title, "live_text_rows", report);
    recover_array_member(title, "live_text_row_ids", report);
    recover_array_member(title, "live_text_column_order", report);
    recover_array_member(title, "live_text_external_bindings", report);
    recover_array_member(title, "live_text_table_bindings", report);
    recover_array_member(title, "live_text_cue_style_overrides", report);

    /* Invalid children are dropped individually so one damaged layer/source
     * never prevents the rest of the title from opening. Existing IDs and all
     * unknown fields on valid objects retain their original JSON values. */
    auto remove_non_objects = [&](const char *key) {
        if (!title.contains(key) || !title[key].is_array())
            return;
        auto &items = title[key];
        const auto old_size = items.size();
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [](const json &item) { return !item.is_object(); }),
                    items.end());
        if (items.size() != old_size)
            report.recoveries.emplace_back(std::string("discarded malformed entries from ") + key);
    };
    remove_non_objects("layers");
    remove_non_objects("cameras");
    remove_non_objects("external_data_sources");
    remove_non_objects("live_text_external_bindings");
    remove_non_objects("live_text_table_bindings");
    remove_non_objects("live_text_cue_style_overrides");
}


inline void migrate_serialization_audit_218(json &title, MigrationReport &report)
{
    validate_recover_title_shape(title, report);

    auto clean_object_array = [&](json &owner, const char *key,
                                  const std::string &context) {
        recover_array_member(owner, key, report);
        if (!owner.contains(key) || !owner[key].is_array())
            return;
        auto &items = owner[key];
        const auto old_size = items.size();
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [](const json &item) { return !item.is_object(); }),
                    items.end());
        if (items.size() != old_size)
            report.recoveries.emplace_back("discarded malformed entries from " + context);
    };

    if (title.contains("layers") && title["layers"].is_array()) {
        for (auto &layer : title["layers"]) {
            if (!layer.is_object())
                continue;
            clean_object_array(layer, "effects", "layer effects");
            clean_object_array(layer, "transitions", "layer transitions");
            clean_object_array(layer, "audio_effects", "audio effects");
            clean_object_array(layer, "external_bindings", "external bindings");

            const bool bind_enabled = layer.contains("parent_bind_enabled") &&
                                      layer["parent_bind_enabled"].is_boolean() &&
                                      layer["parent_bind_enabled"].get<bool>();
            if (bind_enabled) {
                bool valid = layer.contains("parent_bind_matrix") &&
                             layer["parent_bind_matrix"].is_array() &&
                             layer["parent_bind_matrix"].size() == 16;
                if (valid) {
                    for (const auto &value : layer["parent_bind_matrix"]) {
                        if (!value.is_number() || !std::isfinite(value.get<double>())) {
                            valid = false;
                            break;
                        }
                    }
                }
                if (!valid) {
                    layer["parent_bind_enabled"] = false;
                    layer.erase("parent_bind_matrix");
                    report.recoveries.emplace_back("disabled malformed parent bind matrix");
                }
            }
        }
    }

    if (title.contains("external_data_sources") &&
        title["external_data_sources"].is_array()) {
        for (auto &source : title["external_data_sources"]) {
            if (!source.is_object())
                continue;
            clean_object_array(source, "fields", "external data fields");
            recover_object_member(source, "provider", report);
        }
    }

}


inline json animated_scalar_default(double value)
{
    return json{{"static_value", value}, {"keyframes", json::array()}};
}

inline json animated_discrete_default(const std::string &value)
{
    return json{{"static_value", value}, {"keyframes", json::array()}};
}

inline void recover_animated_property(json &object, const char *key,
                                      double fallback, MigrationReport &report)
{
    if (!object.contains(key)) {
        object[key] = animated_scalar_default(fallback);
        return;
    }
    if (!object[key].is_object()) {
        object[key] = animated_scalar_default(fallback);
        report.recoveries.emplace_back(std::string("reset malformed animated camera property: ") + key);
        return;
    }
    auto &property = object[key];
    if (!property.contains("static_value") || !property["static_value"].is_number())
        property["static_value"] = fallback;
    recover_array_member(property, "keyframes", report);
    if (!property.contains("keyframes"))
        property["keyframes"] = json::array();
}

inline void recover_animated_discrete_property(json &object, const char *key,
                                               const std::string &fallback,
                                               MigrationReport &report)
{
    if (!object.contains(key)) {
        object[key] = animated_discrete_default(fallback);
        return;
    }
    if (!object[key].is_object()) {
        object[key] = animated_discrete_default(fallback);
        report.recoveries.emplace_back(std::string("reset malformed discrete camera property: ") + key);
        return;
    }
    auto &property = object[key];
    if (!property.contains("static_value") || !property["static_value"].is_string())
        property["static_value"] = fallback;
    recover_array_member(property, "keyframes", report);
    if (!property.contains("keyframes"))
        property["keyframes"] = json::array();
}

inline void migrate_camera_timeline_animation(json &title, MigrationReport &report)
{
    validate_recover_title_shape(title, report);
    if (!title.contains("cameras") || !title["cameras"].is_array())
        title["cameras"] = json::array();

    std::string fallback_camera = "default";
    if (!title["cameras"].empty() && title["cameras"].front().is_object()) {
        const auto id = title["cameras"].front().find("id");
        if (id != title["cameras"].front().end() && id->is_string() && !id->get<std::string>().empty())
            fallback_camera = id->get<std::string>();
    }
    if (title.contains("active_camera_id") && title["active_camera_id"].is_string())
        fallback_camera = title["active_camera_id"].get<std::string>();
    else
        title["active_camera_id"] = fallback_camera;
    recover_animated_discrete_property(title, "active_camera", fallback_camera, report);

    for (auto &camera : title["cameras"]) {
        if (!camera.is_object())
            continue;
        recover_animated_property(camera, "orientation_x", 0.0, report);
        recover_animated_property(camera, "orientation_y", 0.0, report);
        recover_animated_property(camera, "orientation_z", 0.0, report);
        const double projection = static_cast<double>(safe_integer(camera, "projection", 0));
        recover_animated_property(camera, "projection_mode",
                                  std::clamp(projection, 0.0, 1.0), report);
    }

    if (title.contains("layers") && title["layers"].is_array()) {
        for (auto &layer : title["layers"]) {
            if (!layer.is_object())
                continue;
            std::string camera_id;
            const auto id = layer.find("camera_id");
            if (id != layer.end() && id->is_string())
                camera_id = id->get<std::string>();
            recover_animated_discrete_property(layer, "camera_assignment", camera_id, report);
        }
    }
}

inline void migrate_full_3d_spatial_motion_paths(json &title,
                                                   MigrationReport &report)
{
    validate_recover_title_shape(title, report);
    if (title.contains("layers") && title["layers"].is_array()) {
        for (auto &layer : title["layers"]) {
            if (!layer.is_object()) continue;
            if (!layer.contains("position_3d_path_enabled"))
                layer["position_3d_path_enabled"] = false;
        }
    }
    if (title.contains("cameras") && title["cameras"].is_array()) {
        for (auto &camera : title["cameras"]) {
            if (!camera.is_object()) continue;
            if (!camera.contains("position_3d_path_enabled"))
                camera["position_3d_path_enabled"] = false;
            if (!camera.contains("target_3d_path_enabled"))
                camera["target_3d_path_enabled"] = false;
        }
    }
}

inline void apply_development_migration(int target_version, json &title,
                                        MigrationReport &report)
{
    /* Every development delivery from 144 onward has an explicit migration
     * step. Most are intentionally no-ops because their changes were UI or
     * renderer-only; keeping the contiguous ledger prevents silent gaps. */
    switch (target_version) {
    case 144:
        migrate_external_data(title, report);
        migrate_formatting_and_patterns(title, report);
        break;
    case 145:
    case 146:
    case 147:
    case 148:
    case 149:
    case 150:
    case 151:
    case 152:
    case 153:
    case 154:
    case 155:
    case 156:
    case 157:
    case 158:
    case 159:
    case 160:
    case 161:
    case 162:
        migrate_audio_layers(title, report);
        break;
    case 163:
    case 164:
        break;
    case 165:
        migrate_proxy_metadata(title, report);
        break;
    case 166:
    case 167:
        migrate_stinger_metadata(title, report);
        break;
    case 168:
    case 169:
    case 170:
        migrate_stinger_switch_contract(title, report);
        break;
    case 171:
    case 172:
    case 173:
    case 174:
    case 175:
    case 176:
    case 177:
    case 178:
        break;
    case 179:
        validate_recover_title_shape(title, report);
        migrate_external_data(title, report);
        migrate_formatting_and_patterns(title, report);
        migrate_audio_layers(title, report);
        migrate_stinger_metadata(title, report);
        migrate_stinger_switch_contract(title, report);
        migrate_proxy_metadata(title, report);
        break;
    case 180:
        /* Performance/cache/threading audit: no persisted schema changes.
         * Re-run current recovery so malformed documents remain loadable. */
        validate_recover_title_shape(title, report);
        break;
    case 181:
        /* Audio scheduler repair and automated regression pass introduce no
         * persisted fields. Keep current recovery idempotent. */
        validate_recover_title_shape(title, report);
        break;
    case 182:
        /* Portable worker-thread naming is runtime-only and does not alter
         * persisted title data. Keep current recovery idempotent. */
        validate_recover_title_shape(title, report);
        break;
    case 183:
        /* Editor monitor cadence is runtime-only. Program/source buffering
         * and persisted title data remain unchanged. */
        validate_recover_title_shape(title, report);
        break;
    case 184:
        /* Editor audio delivery restoration and diagnostics are runtime-only.
         * No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 185:
        /* Realtime editor monitor pacing and FXM audio diagnostics are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 186:
        /* Sample-locked editor monitor cadence is runtime-only. No persisted
         * title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 187:
        /* Live-cue transition persistence and cache/prerender scheduling are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 188:
        /* Dynamic OBS mixer visibility and editor reverse-audio transport are
         * runtime-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 189:
        /* The disabled-FX visual indicator and documentation consolidation are
         * editor/UI-only. No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 190:
        /* Planar 3D is strictly opt-in. Missing fields intentionally resolve
         * to 2D/default-camera values, preserving every older project without
         * rewriting its authored transforms. */
        validate_recover_title_shape(title, report);
        if (!title.contains("cameras"))
            title["cameras"] = json::array();
        if (!title.contains("active_camera_id"))
            title["active_camera_id"] = "default";
        break;
    case 191:
    case 192:
    case 193:
    case 194:
    case 195:
    case 196:
    case 197:
    case 198:
    case 199:
    case 200:
    case 201:
    case 202:
    case 203:
    case 204:
    case 205:
    case 206:
        /* Delivery/version synchronization, the MSVC-safe camera-control
         * initializer fix, editor-only 3D navigation/gizmos, the GPU depth
         * target, depth/culling semantics, AE-style Vector3 authoring UI,
         * transparent 3D ordering, compact inspector refinements, full 3D
         * reparenting/group-depth integration, and the anchor-action MSVC scope
         * correction introduce no new persisted fields. */
        validate_recover_title_shape(title, report);
        break;
    case 207:
        /* Cameras become first-class timeline owners. Populate the animated
         * switching, assignment, orientation, and projection tracks from the
         * static Development Version 206 compatibility fields. */
        migrate_camera_timeline_animation(title, report);
        break;
    case 208:
        /* Unified XYZ spatial tracks are opt-in compatibility mirrors. Legacy
         * XY/Z curves remain authoritative until an object is promoted by the
         * 208 authoring workflow. */
        migrate_full_3d_spatial_motion_paths(title, report);
        break;
    case 209:
        /* Camera-aware projected 3D motion blur is a runtime rendering change.
         * No persisted title fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 210:
        /* Performance, cache, scheduling, and keyframe-safe GPU model refresh
         * changes are runtime/editor behavior only. */
        validate_recover_title_shape(title, report);
        break;
    case 211:
        /* Compatibility/regression completion. Snapshot restoration, camera
         * clipboard remapping and runtime cleanup did not add persisted fields. */
        validate_recover_title_shape(title, report);
        break;
    case 212:
        /* Legacy vector2d JSON is promoted during model loading into unified
         * XYZ storage. Missing z remains field-specific (0 for ordinary 2D
         * vectors, 1 for Scale), so historical 2D rendering is unchanged. */
        validate_recover_title_shape(title, report);
        break;
    case 213:
    case 214:
    case 215:
        /* Editor pacing, coordinate audit and Timeline/Graph Editor completion
         * did not add persisted title fields. */
        validate_recover_title_shape(title, report);
        break;
    case 216:
        /* Parent bind matrices are optional and identity by default. Existing
         * files therefore require no authored transform rewrite. The loader
         * validates the 16 finite values before enabling the matrix. */
        validate_recover_title_shape(title, report);
        break;
    case 217:
        /* Performance, cache/threading diagnostics, channel-toggle styling,
         * and gizmo hover feedback are runtime/editor-only changes. */
        validate_recover_title_shape(title, report);
        break;
    case 218:
        /* Serialization/migration audit. Schema 6 formalizes lossless opaque
         * field preservation and nested malformed-entry recovery. */
        migrate_serialization_audit_218(title, report);
        migrate_camera_timeline_animation(title, report);
        migrate_full_3d_spatial_motion_paths(title, report);
        break;
    case 219:
        /* Automated test-suite and render hot-path repair. Opaque future-schema
         * payloads remain persisted, but are shared immutably and excluded from
         * render fingerprints; no authored schema fields changed. */
        validate_recover_title_shape(title, report);
        break;
    case 220:
        /* Unified effects runtime and render baseline. Effect descriptors,
         * evaluated parameter snapshots, resource pools and diagnostics are
         * runtime-only; stable effect IDs and authored schema 6 are unchanged. */
        validate_recover_title_shape(title, report);
        break;
    case 221:
        /* Initial procedural Noise runtime. Development Version 222 replaces
         * older built-in effect schemas with fresh current-default instances. */
        validate_recover_title_shape(title, report);
        break;
    case 222:
        /* Noise schema 3 removes compatibility modes. The built-in loader
         * resets older effect instances to current defaults. New convolution
         * and detail effects use append-only stable IDs and existing fields. */
        validate_recover_title_shape(title, report);
        break;
    case 223:
        /* Optical effects and catalog manifests use append-only effect IDs. */
        validate_recover_title_shape(title, report);
        break;
    case 224:
        /* Lens, distortion and finishing effects use append-only IDs and the
         * existing effect parameter envelope. Changed built-ins carry a new
         * schema and are intentionally reset to their current defaults. */
        validate_recover_title_shape(title, report);
        break;
    case 225:
        /* Development Version 225 appends keying/matte IDs without reshaping
         * existing effect records or retaining compatibility modes. */
        validate_recover_title_shape(title, report);
        break;
    case 226:
        /* Source-aware effects append stable effect IDs and optional source
         * fields. Presets and the shared effect/timeline hierarchy reuse the
         * existing effect envelope, so migration only validates/recoveries. */
        validate_recover_title_shape(title, report);
        break;
    case 227:
        /* Video layers append a new layer type and source metadata. Linked
         * audio streams remain ordinary Audio rows, so existing audio schema
         * recovery and forward-compatible passthrough continue to apply. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 228:
        /* Video visual-effect eligibility and decoded-frame cache keys do not
         * reshape saved documents. Re-run the Video/audio stream recovery so
         * old 227 projects keep synchronized embedded stream rows. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 229:
        /* Video performance, fixed layer-list columns and 3D editor refresh
         * semantics are runtime/editor-only. Keep saved projects unchanged,
         * but validate/recover media stream rows in case the file came from
         * an earlier 227/228 build. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 230:
        /* Frame-accurate Video playback is runtime-only: persisted media
         * sources keep the same trim, FPS metadata and stream links. Re-run
         * validation so older 227-229 documents recover missing stream rows
         * before the deterministic timeline-frame mapper evaluates them. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 231:
        /* Video decode/effects optimizations, frame-step shortcuts, scene-mask
         * placeholders and transform menu actions do not reshape saved data.
         * Validate/recover media stream rows and keep forward-compatible fields. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 232:
        /* Editor-only scene-mask placeholder shape preview, FX-strip filtering
         * and Video/Audio range authoring controls are UI/runtime behavior.
         * Saved layer fields are unchanged; keep stream rows recoverable. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 233:
        /* Range authoring now keeps media In/Out locked to the strip length
         * and the public Visual Effects SDK extends plugin metadata only.
         * Validate/recover media stream rows; project effect payloads remain
         * forward-compatible by stable extension ids and JSON state. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 234:
        /* Inspector-only range compaction, realtime timeline-trim refresh,
         * decoder LRU/reverse-step stability, playback start semantics,
         * effect preset additions and control styling do not change persisted
         * layer schema. Re-run media validation for older video/audio titles. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 235:
        /* Video decoder seek fixes, lightweight live range refresh and the
         * Noise/Grain/distortion effect split are compatible with existing
         * project JSON. Keep the same validation pass for older media titles. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 236:
        /* Video presentation fallback, faster live range trim and dedicated
         * Film/Analog/Digital damage shaders are runtime/UI/effect implementation
         * changes; saved title JSON remains compatible. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 237:
        /* Decoder look-ahead caching and the damage-map/composite multi-pass
         * effect engine are runtime implementation changes. Revalidate media
         * rows and keep serialized effect state stable by built-in effect id. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 238:
        /* Effect-pipeline audit unifies the GPU technique selector and moves
         * Film/Analog/Digital distortion to the shared artifact Draw pass.
         * Existing serialized effect ids/parameters remain stable; revalidate
         * media rows while preserving the fast Gaussian blur backend. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 239:
        /* Motion blur sample-density tuning and the organic damage shader
         * revision are runtime/effect implementation changes. Serialized
         * effect ids and parameters remain stable; keep media validation. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 240:
        /* Inspector widget unification is an editor-only styling/metrics pass.
         * No serialized title data changes; keep the latest media validation
         * path for projects opened from older development builds. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 241:
        /* Properties button metrics and parented/direct-image color-overlay
         * raster invalidation are editor/runtime fixes only. Serialized title
         * data remains compatible with Development Version 240 projects. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 242:
        /* Video proxy/cache/hardware-decode integration adds advisory runtime
         * proxy metadata and decode policy only. Existing Video/Audio layer
         * JSON remains valid; media rows are revalidated so stale/missing
         * source files cannot poison proxy relink or decoder cache state. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 243:
        /* Video time-remap and interpolation adds source-time animation,
         * loop-segment metadata, interpolation mode and optical-flow cache
         * state. Old projects default to the exact linear 242 mapping. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 244:
        /* Serialization/migration audit for effects, external plugins and
         * Video layers. Missing media/plugins are placeholders; old built-in
         * Glow/Noise state stays preserve-by-default; explicit migration is
         * required before a revised implementation may rewrite state. */
        migrate_serialization_audit_244(title, report);
        migrate_audio_layers(title, report);
        break;
    case 245:
    case 246:
    case 247:
    case 248:
    case 249:
    case 250:
    case 251:
    case 252:
    case 253:
    case 254:
    case 255:
    case 256:
    case 257:
    case 258:
    case 259:
    case 260:
    case 261:
    case 262:
    case 263:
    case 264:
        /* Development deliveries 245-264 are UI/runtime/effect-library
         * updates. They do not reshape persisted title JSON; keep the current
         * recovery and media-row validation paths contiguous. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    case 265:
        /* Development Version 298 adds title lights and opt-in planar PBR
         * materials. Missing fields intentionally retain compatibility
         * defaults: no authored lights and Accepts Lights disabled per layer. */
        validate_recover_title_shape(title, report);
        migrate_audio_layers(title, report);
        break;
    default:
        break;
    }
}

inline json migrate_title_json(const json &input, MigrationReport *report_out = nullptr)
{
    MigrationReport report;
    json title = input;
    report.source_schema_version = safe_integer(title, "schema_version", 0);
    report.source_development_version = infer_development_version(title);

    validate_recover_title_shape(title, report);
    const int first = std::max(kFirstAuditedDevelopmentVersion,
                               report.source_development_version + 1);
    for (int version = first; version <= kCurrentDevelopmentVersion; ++version) {
        apply_development_migration(version, title, report);
        report.applied_development_versions.push_back(version);
    }

    /* Always run current validation. A file may claim a current/future
     * development version while still containing malformed known fields. */
    if (first > kCurrentDevelopmentVersion)
        apply_development_migration(kCurrentDevelopmentVersion, title, report);

    title["schema_version"] = kCurrentTitleSchemaVersion;
    title["development_version"] = kCurrentDevelopmentVersion;

    if (report.source_schema_version > kCurrentTitleSchemaVersion) {
        report.warnings.emplace_back(
            "title was created by a newer schema; unknown fields were preserved");
    }
    if (report.source_development_version > kCurrentDevelopmentVersion) {
        report.warnings.emplace_back(
            "title was created by a newer development version; best-effort loading was used");
    }

    if (report_out)
        *report_out = std::move(report);
    return title;
}

inline std::vector<int> development_migration_ledger()
{
    std::vector<int> versions;
    versions.reserve(kCurrentDevelopmentVersion - kFirstAuditedDevelopmentVersion + 1);
    for (int version = kFirstAuditedDevelopmentVersion;
         version <= kCurrentDevelopmentVersion; ++version)
        versions.push_back(version);
    return versions;
}

} // namespace fxm::serialization
