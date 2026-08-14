#include "text-animator-preset-io.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <utility>

using json = nlohmann::json;

namespace {
constexpr size_t kMaxName = 512;
constexpr size_t kMaxDescription = 8192;
constexpr size_t kMaxProperties = 64;
constexpr size_t kMaxSelectors = 64;
constexpr size_t kMaxKeyframes = 100000;
constexpr size_t kMaxRanges = 4096;
constexpr double kMaxValue = 1.0e9;
constexpr double kMaxTime = 86400.0 * 365.0;
constexpr const char *kPresetFormat = "fxm-text-animator-preset";

void fail(std::string *error, const std::string &message)
{
    if (error) *error = message;
}

double finite_number(const json &object, const char *key, double fallback,
                     double minimum = -kMaxValue, double maximum = kMaxValue)
{
    auto it = object.find(key);
    if (it == object.end() || !it->is_number()) return fallback;
    const double value = it->get<double>();
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

int integer(const json &object, const char *key, int fallback, int minimum, int maximum)
{
    auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) return fallback;
    const long long value = it->get<long long>();
    return (int)std::clamp<long long>(value, minimum, maximum);
}

bool boolean(const json &object, const char *key, bool fallback)
{
    auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::string string_value(const json &object, const char *key,
                         const std::string &fallback, size_t maximum)
{
    auto it = object.find(key);
    if (it == object.end() || !it->is_string()) return fallback;
    std::string value = it->get<std::string>();
    if (value.size() > maximum) value.resize(maximum);
    return value;
}

json keyframe_to_json(const Keyframe &key)
{
    return {{"time", key.time}, {"value", key.value}, {"easing", (int)key.easing},
            {"cx1", key.cx1}, {"cy1", key.cy1}, {"cx2", key.cx2}, {"cy2", key.cy2},
            {"temporal_mode", (int)key.temporal_mode},
            {"incoming_influence", key.incoming_influence},
            {"outgoing_influence", key.outgoing_influence},
            {"incoming_speed", key.incoming_speed},
            {"outgoing_speed", key.outgoing_speed},
            {"temporal_tangents_linked", key.temporal_tangents_linked},
            {"temporal_velocity_explicit", key.temporal_velocity_explicit}};
}

Keyframe keyframe_from_json(const json &object)
{
    Keyframe key;
    if (!object.is_object()) return key;
    key.time = finite_number(object, "time", 0.0, -kMaxTime, kMaxTime);
    key.value = finite_number(object, "value", 0.0);
    key.easing = (EasingType)integer(object, "easing", (int)EasingType::EaseInOut,
                                     (int)EasingType::Linear, (int)EasingType::Hold);
    key.cx1 = (float)finite_number(object, "cx1", 0.333, -10.0, 10.0);
    key.cy1 = (float)finite_number(object, "cy1", 0.0, -100.0, 100.0);
    key.cx2 = (float)finite_number(object, "cx2", 0.667, -10.0, 10.0);
    key.cy2 = (float)finite_number(object, "cy2", 1.0, -100.0, 100.0);
    key.temporal_mode = (TemporalInterpolationMode)integer(
        object, "temporal_mode", (int)TemporalInterpolationMode::AutoBezier,
        (int)TemporalInterpolationMode::Linear,
        (int)TemporalInterpolationMode::ManualBezier);
    key.incoming_influence = finite_number(object, "incoming_influence", 33.3333333333, 0.0, 100.0);
    key.outgoing_influence = finite_number(object, "outgoing_influence", 33.3333333333, 0.0, 100.0);
    key.incoming_speed = finite_number(object, "incoming_speed", 0.0);
    key.outgoing_speed = finite_number(object, "outgoing_speed", 0.0);
    key.temporal_tangents_linked = boolean(object, "temporal_tangents_linked", true);
    key.temporal_velocity_explicit = boolean(object, "temporal_velocity_explicit", false);
    return key;
}

json animated_to_json(const AnimatedProperty &property)
{
    json keys = json::array();
    for (const Keyframe &key : property.keyframes) keys.push_back(keyframe_to_json(key));
    return {{"name", property.name}, {"static_value", property.static_value},
            {"keyframes", std::move(keys)}};
}

AnimatedProperty animated_from_json(const json &object, const std::string &fallback_name)
{
    AnimatedProperty property;
    property.name = fallback_name;
    if (!object.is_object()) return property;
    property.name = string_value(object, "name", fallback_name, kMaxName);
    property.static_value = finite_number(object, "static_value", 0.0);
    auto it = object.find("keyframes");
    if (it != object.end() && it->is_array()) {
        const size_t count = std::min(it->size(), kMaxKeyframes);
        property.keyframes.reserve(count);
        for (size_t i = 0; i < count; ++i)
            property.keyframes.push_back(keyframe_from_json((*it)[i]));
        std::stable_sort(property.keyframes.begin(), property.keyframes.end(),
                         [](const Keyframe &left, const Keyframe &right) {
                             return left.time < right.time;
                         });
    }
    return property;
}

json property_to_json(const TextAnimatorProperty &property)
{
    return {{"id", property.id}, {"name", property.name}, {"type", (int)property.type},
            {"enabled", property.enabled}, {"value", animated_to_json(property.value)},
            {"secondary", animated_to_json(property.secondary)},
            {"tertiary", animated_to_json(property.tertiary)},
            {"quaternary", animated_to_json(property.quaternary)}};
}

TextAnimatorProperty property_from_json(const json &object, size_t ordinal)
{
    TextAnimatorProperty property;
    if (!object.is_object()) return property;
    property.type = (TextAnimatorPropertyType)integer(
        object, "type", (int)TextAnimatorPropertyType::Opacity,
        (int)TextAnimatorPropertyType::Position,
        (int)TextAnimatorPropertyType::ScrambleAmount);
    property.id = string_value(object, "id", "", kMaxName);
    property.name = string_value(object, "name", "Property", kMaxName);
    if (property.id.empty()) property.id = make_text_animator_id("property", property.name, ordinal);
    property.enabled = boolean(object, "enabled", true);
    if (object.contains("value")) property.value = animated_from_json(object["value"], property.name + ".value");
    if (object.contains("secondary")) property.secondary = animated_from_json(object["secondary"], property.name + ".secondary");
    if (object.contains("tertiary")) property.tertiary = animated_from_json(object["tertiary"], property.name + ".tertiary");
    if (object.contains("quaternary")) property.quaternary = animated_from_json(object["quaternary"], property.name + ".quaternary");
    return property;
}

json selector_to_json(const TextSelector &selector)
{
    json ranges = json::array();
    for (const auto &range : selector.tagged_byte_ranges)
        ranges.push_back({{"start", range.first}, {"length", range.second}});
    return {{"id", selector.id}, {"name", selector.name}, {"type", (int)selector.type},
            {"combination", (int)selector.combination}, {"based_on", (int)selector.based_on},
            {"enabled", selector.enabled}, {"expanded", selector.expanded},
            {"range_units", (int)selector.range_units}, {"range_shape", (int)selector.range_shape},
            {"start", animated_to_json(selector.start)}, {"end", animated_to_json(selector.end)},
            {"offset", animated_to_json(selector.offset)}, {"amount", animated_to_json(selector.amount)},
            {"ease_high", animated_to_json(selector.ease_high)}, {"ease_low", animated_to_json(selector.ease_low)},
            {"smoothness", animated_to_json(selector.smoothness)},
            {"completion", animated_to_json(selector.completion)},
            {"stagger_percent", animated_to_json(selector.stagger_percent)},
            {"unit_easing", (int)selector.unit_easing},
            {"stagger_mode", (int)selector.stagger_mode},
            {"exclude_whitespace", selector.exclude_whitespace},
            {"randomize_order", selector.randomize_order}, {"random_seed", selector.random_seed},
            {"invert", selector.invert}, {"procedural_mode", (int)selector.procedural_mode},
            {"amplitude", animated_to_json(selector.amplitude)},
            {"frequency", animated_to_json(selector.frequency)}, {"phase", animated_to_json(selector.phase)},
            {"speed", animated_to_json(selector.speed)}, {"falloff", animated_to_json(selector.falloff)},
            {"minimum", animated_to_json(selector.minimum)}, {"maximum", animated_to_json(selector.maximum)},
            {"custom_index", animated_to_json(selector.custom_index)}, {"direction", (int)selector.direction},
            {"match_mode", (int)selector.match_mode}, {"range_start", selector.range_start},
            {"range_end", selector.range_end}, {"match_text", selector.match_text},
            {"regular_expression", selector.regular_expression}, {"case_sensitive", selector.case_sensitive},
            {"rich_text_run_index", selector.rich_text_run_index},
            {"tagged_byte_ranges", std::move(ranges)},
            {"wiggly_amount", animated_to_json(selector.wiggly_amount)},
            {"wiggly_frequency", animated_to_json(selector.wiggly_frequency)},
            {"correlation", animated_to_json(selector.correlation)},
            {"temporal_phase", animated_to_json(selector.temporal_phase)},
            {"spatial_phase", animated_to_json(selector.spatial_phase)},
            {"minimum_influence", animated_to_json(selector.minimum_influence)},
            {"maximum_influence", animated_to_json(selector.maximum_influence)},
            {"wiggly_seed", selector.wiggly_seed}, {"lock_dimensions", selector.lock_dimensions},
            {"per_character_random", selector.per_character_random}};
}

TextSelector selector_from_json(const json &object, size_t ordinal)
{
    TextSelector selector;
    if (!object.is_object()) return selector;
    selector.id = string_value(object, "id", "", kMaxName);
    selector.name = string_value(object, "name", "Selector", kMaxName);
    if (selector.id.empty()) selector.id = make_text_animator_id("selector", selector.name, ordinal);
    selector.type = (TextSelectorType)integer(object, "type", 0, 0, (int)TextSelectorType::Staggered);
    selector.combination = (TextSelectorCombinationMode)integer(object, "combination", 2, 0, (int)TextSelectorCombinationMode::Multiply);
    selector.based_on = (TextAnimatorUnit)integer(object, "based_on", 0, 0, (int)TextAnimatorUnit::Sentence);
    selector.enabled = boolean(object, "enabled", true);
    selector.expanded = boolean(object, "expanded", true);
    selector.range_units = (TextRangeUnits)integer(object, "range_units", 0, 0, 1);
    selector.range_shape = (TextRangeShape)integer(object, "range_shape", 0, 0, (int)TextRangeShape::Smooth);
    auto read_property = [&](const char *key, AnimatedProperty &target) {
        if (object.contains(key)) target = animated_from_json(object[key], key);
    };
    read_property("start", selector.start); read_property("end", selector.end);
    read_property("offset", selector.offset); read_property("amount", selector.amount);
    read_property("ease_high", selector.ease_high); read_property("ease_low", selector.ease_low);
    read_property("smoothness", selector.smoothness);
    read_property("completion", selector.completion);
    read_property("stagger_percent", selector.stagger_percent);
    selector.unit_easing = (EasingType)integer(
        object, "unit_easing", (int)EasingType::EaseInOut,
        (int)EasingType::Linear, (int)EasingType::Hold);
    selector.stagger_mode = (TextStaggerMode)integer(
        object, "stagger_mode", (int)TextStaggerMode::Entrance, 0, 1);
    selector.exclude_whitespace = boolean(object, "exclude_whitespace", true);
    selector.randomize_order = boolean(object, "randomize_order", false);
    selector.random_seed = integer(object, "random_seed", 1, 0, std::numeric_limits<int>::max());
    selector.invert = boolean(object, "invert", false);
    selector.procedural_mode = (TextProceduralMode)integer(object, "procedural_mode", 0, 0, (int)TextProceduralMode::DistanceFromCustomIndex);
    read_property("amplitude", selector.amplitude); read_property("frequency", selector.frequency);
    read_property("phase", selector.phase); read_property("speed", selector.speed);
    read_property("falloff", selector.falloff); read_property("minimum", selector.minimum);
    read_property("maximum", selector.maximum); read_property("custom_index", selector.custom_index);
    selector.direction = (TextSelectorDirection)integer(object, "direction", 0, 0, (int)TextSelectorDirection::Bidirectional);
    selector.match_mode = (TextMatchMode)integer(object, "match_mode", (int)TextMatchMode::ExactText, 0, (int)TextMatchMode::ChangedText);
    selector.range_start = (size_t)std::max(0, integer(object, "range_start", 0, 0, std::numeric_limits<int>::max()));
    selector.range_end = (size_t)std::max(0, integer(object, "range_end", 0, 0, std::numeric_limits<int>::max()));
    selector.match_text = string_value(object, "match_text", "", kMaxDescription);
    selector.regular_expression = string_value(object, "regular_expression", "", kMaxDescription);
    selector.case_sensitive = boolean(object, "case_sensitive", true);
    selector.rich_text_run_index = integer(object, "rich_text_run_index", -1, -1, 65535);
    auto ranges = object.find("tagged_byte_ranges");
    if (ranges != object.end() && ranges->is_array()) {
        const size_t count = std::min(ranges->size(), kMaxRanges);
        for (size_t i = 0; i < count; ++i) {
            const json &item = (*ranges)[i];
            if (!item.is_object()) continue;
            const size_t start = (size_t)integer(item, "start", 0, 0, std::numeric_limits<int>::max());
            const size_t length = (size_t)integer(item, "length", 0, 0, std::numeric_limits<int>::max());
            selector.tagged_byte_ranges.emplace_back(start, length);
        }
    }
    read_property("wiggly_amount", selector.wiggly_amount);
    read_property("wiggly_frequency", selector.wiggly_frequency);
    read_property("correlation", selector.correlation);
    read_property("temporal_phase", selector.temporal_phase);
    read_property("spatial_phase", selector.spatial_phase);
    read_property("minimum_influence", selector.minimum_influence);
    read_property("maximum_influence", selector.maximum_influence);
    selector.wiggly_seed = integer(object, "wiggly_seed", 1, 0, std::numeric_limits<int>::max());
    selector.lock_dimensions = boolean(object, "lock_dimensions", true);
    selector.per_character_random = boolean(object, "per_character_random", true);
    return selector;
}

json animator_to_json(const TextAnimator &animator)
{
    json properties = json::array();
    for (const TextAnimatorProperty &property : animator.properties)
        properties.push_back(property_to_json(property));
    json selectors = json::array();
    for (const TextSelector &selector : animator.selectors)
        selectors.push_back(selector_to_json(selector));
    return {{"id", animator.id}, {"name", animator.name}, {"enabled", animator.enabled},
            {"expanded", animator.expanded}, {"blend_mode", (int)animator.blend_mode},
            {"granularity", (int)animator.granularity},
            {"transform_as_unit", animator.transform_as_unit},
            {"clip_to_unit_bounds", animator.clip_to_unit_bounds},
            {"change_behaviour", (int)animator.change_behaviour},
            {"playback_role", (int)animator.playback_role},
            {"local_time_offset", animator.local_time_offset},
            {"preset_id", animator.preset_id},
            {"preset_schema_version", animator.preset_schema_version},
            {"properties", std::move(properties)}, {"selectors", std::move(selectors)}};
}

TextAnimator animator_from_json(const json &object)
{
    TextAnimator animator;
    if (!object.is_object()) return animator;
    animator.id = string_value(object, "id", "", kMaxName);
    animator.name = string_value(object, "name", "Animator", kMaxName);
    if (animator.id.empty()) animator.id = make_text_animator_id("animator", animator.name, 0);
    animator.enabled = boolean(object, "enabled", true);
    animator.expanded = boolean(object, "expanded", true);
    animator.blend_mode = (TextAnimatorBlendMode)integer(object, "blend_mode", 0, 0, (int)TextAnimatorBlendMode::Multiply);
    animator.granularity = (TextAnimatorUnit)integer(object, "granularity", 0, 0, (int)TextAnimatorUnit::Sentence);
    animator.transform_as_unit = boolean(object, "transform_as_unit", false);
    animator.clip_to_unit_bounds = boolean(object, "clip_to_unit_bounds", false);
    animator.change_behaviour = (TextChangeBehaviour)integer(object, "change_behaviour", (int)TextChangeBehaviour::ReevaluateFullText, 0, (int)TextChangeBehaviour::ReevaluateFullText);
    animator.playback_role = (TextAnimatorPlaybackRole)integer(object, "playback_role", 0, 0, (int)TextAnimatorPlaybackRole::Continuous);
    animator.local_time_offset = finite_number(object, "local_time_offset", 0.0, -kMaxTime, kMaxTime);
    animator.preset_id = string_value(object, "preset_id", "", kMaxName);
    animator.preset_schema_version = integer(object, "preset_schema_version", 0, 0, 1024);
    auto properties = object.find("properties");
    if (properties != object.end() && properties->is_array()) {
        const size_t count = std::min(properties->size(), kMaxProperties);
        for (size_t i = 0; i < count; ++i)
            animator.properties.push_back(property_from_json((*properties)[i], i));
    }
    auto selectors = object.find("selectors");
    if (selectors != object.end() && selectors->is_array()) {
        const size_t count = std::min(selectors->size(), kMaxSelectors);
        for (size_t i = 0; i < count; ++i)
            animator.selectors.push_back(selector_from_json((*selectors)[i], i));
    }
    return animator;
}
} // namespace

bool save_text_animator_preset_file(const std::string &path,
                                    const TextAnimatorPresetMetadata &metadata,
                                    const TextAnimator &animator,
                                    std::string *error)
{
    if (error) error->clear();
    if (path.empty()) { fail(error, "Preset path is empty."); return false; }
    try {
        json root = {{"format", kPresetFormat},
                     {"schema_version", kTextAnimatorSchemaVersion},
                     {"metadata", {{"name", metadata.name.empty() ? animator.name : metadata.name},
                                   {"category", metadata.category},
                                   {"description", metadata.description},
                                   {"identifier", metadata.identifier},
                                   {"schema_version", metadata.schema_version}}},
                     {"animator", animator_to_json(animator)}};
        const std::filesystem::path target = std::filesystem::u8path(path);
        std::filesystem::path temporary = target;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) { fail(error, "Unable to open preset file for writing."); return false; }
            output << root.dump(2) << '\n';
            output.flush();
            if (!output) { fail(error, "Unable to finish writing preset file."); return false; }
        }
        std::error_code filesystem_error;
        std::filesystem::remove(target, filesystem_error);
        filesystem_error.clear();
        std::filesystem::rename(temporary, target, filesystem_error);
        if (filesystem_error) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            fail(error, "Unable to replace preset file atomically: " +
                        filesystem_error.message());
            return false;
        }
        return true;
    } catch (const std::exception &exception) {
        fail(error, std::string("Unable to save Text Animator preset: ") + exception.what());
        return false;
    }
}

bool load_text_animator_preset_file(const std::string &path,
                                    TextAnimatorPresetMetadata *metadata,
                                    TextAnimator *animator,
                                    std::string *error)
{
    if (error) error->clear();
    if (!animator) { fail(error, "No destination animator was provided."); return false; }
    try {
        std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
        if (!input) { fail(error, "Unable to open Text Animator preset."); return false; }
        json root;
        input >> root;
        if (!root.is_object() || string_value(root, "format", "", 128) != kPresetFormat) {
            fail(error, "This file is not a FXM Text Animator preset.");
            return false;
        }
        const int schema = integer(root, "schema_version", 0, 1, 1024);
        if (schema > kTextAnimatorSchemaVersion) {
            fail(error, "This Text Animator preset was created by a newer incompatible schema.");
            return false;
        }
        auto animator_node = root.find("animator");
        if (animator_node == root.end() || !animator_node->is_object()) {
            fail(error, "The preset does not contain animator data.");
            return false;
        }
        TextAnimator decoded = animator_from_json(*animator_node);
        if (decoded.properties.empty()) {
            fail(error, "The preset does not contain any animator properties.");
            return false;
        }
        if (metadata) {
            const json empty = json::object();
            const json &meta = root.contains("metadata") && root["metadata"].is_object()
                ? root["metadata"] : empty;
            metadata->name = string_value(meta, "name", decoded.name, kMaxName);
            metadata->category = string_value(meta, "category", "Custom", kMaxName);
            metadata->description = string_value(meta, "description", "", kMaxDescription);
            metadata->identifier = string_value(meta, "identifier", "", kMaxName);
            metadata->schema_version = integer(meta, "schema_version", schema, 1, 1024);
        }
        *animator = std::move(decoded);
        return true;
    } catch (const std::exception &exception) {
        fail(error, std::string("Unable to load Text Animator preset: ") + exception.what());
        return false;
    }
}

void reseed_text_animator_ids(TextAnimator &animator,
                              const std::string &destination_layer_id,
                              size_t animator_ordinal)
{
    const std::string original = animator.id + ':' + animator.preset_id + ':' + animator.name;
    animator.id = make_text_animator_id("animator", destination_layer_id + ':' + original,
                                       animator_ordinal);
    for (size_t i = 0; i < animator.properties.size(); ++i)
        animator.properties[i].id = make_text_animator_id("property", animator.id, i);
    for (size_t i = 0; i < animator.selectors.size(); ++i)
        animator.selectors[i].id = make_text_animator_id("selector", animator.id, i);
}
