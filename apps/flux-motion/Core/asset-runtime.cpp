#include "asset-runtime.h"

#include "title-data.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fxm::asset_runtime {
namespace {

using Clock = std::chrono::steady_clock;
constexpr double kEpsilon = 0.000001;

struct RuntimeState {
    std::string title_id;
    Clock::time_point anchor = Clock::now();
    std::uint64_t configuration_signature = 0;
    bool initialized = false;
};

std::mutex g_mutex;
std::unordered_map<std::string, RuntimeState> g_states;

static void hash_combine(std::uint64_t &seed, std::uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

static std::uint64_t double_bits(double value)
{
    static_assert(sizeof(double) == sizeof(std::uint64_t));
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static std::uint64_t configuration_signature(const Layer &layer)
{
    std::uint64_t result = 0xcbf29ce484222325ULL;
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_playback_mode));
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_source_playback_mode));
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_source_loop_type));
    hash_combine(result, double_bits(layer.asset_duration));
    hash_combine(result, double_bits(layer.asset_playback_offset));
    hash_combine(result, double_bits(layer.asset_source_loop_start));
    hash_combine(result, double_bits(layer.asset_source_loop_end));
    hash_combine(result, double_bits(layer.asset_source_pause_time));
    hash_combine(result, double_bits(layer.asset_pause_duration));
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_loop_count));
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_loop));
    hash_combine(result, static_cast<std::uint64_t>(layer.asset_animated));
    return result;
}

static bool effect_has_animation(const LayerEffect &effect)
{
    return effect.enabled_prop.is_animated() ||
           effect.opacity_prop.is_animated() ||
           effect.size_prop.is_animated() ||
           effect.distance_prop.is_animated() ||
           effect.angle_prop.is_animated() ||
           effect.spread_prop.is_animated() ||
           effect.falloff_prop.is_animated() ||
           effect.stroke_width_prop.is_animated() ||
           effect.stroke_opacity_prop.is_animated() ||
           effect.trim_start_prop.is_animated() ||
           effect.trim_end_prop.is_animated() ||
           effect.trim_offset_prop.is_animated() ||
           effect.padding_left_prop.is_animated() ||
           effect.padding_right_prop.is_animated() ||
           effect.padding_top_prop.is_animated() ||
           effect.padding_bottom_prop.is_animated() ||
           effect.corner_radius_tl_prop.is_animated() ||
           effect.corner_radius_tr_prop.is_animated() ||
           effect.corner_radius_br_prop.is_animated() ||
           effect.corner_radius_bl_prop.is_animated() ||
           effect.color_a.is_animated() ||
           effect.color_r.is_animated() ||
           effect.color_g.is_animated() ||
           effect.color_b.is_animated() ||
           effect.stroke_color_a.is_animated() ||
           effect.stroke_color_r.is_animated() ||
           effect.stroke_color_g.is_animated() ||
           effect.stroke_color_b.is_animated() ||
           effect.amount_prop.is_animated() ||
           effect.scale_prop.is_animated() ||
           effect.softness_prop.is_animated() ||
           effect.roundness_prop.is_animated() ||
           effect.speed_prop.is_animated() ||
           effect.center_x_prop.is_animated() ||
           effect.center_y_prop.is_animated() ||
           effect.complexity_prop.is_animated() ||
           effect.evolution_prop.is_animated() ||
           effect.secondary_color_a.is_animated() ||
           effect.secondary_color_r.is_animated() ||
           effect.secondary_color_g.is_animated() ||
           effect.secondary_color_b.is_animated() ||
           (!effect.extension_keyframes_json.empty() &&
            effect.extension_keyframes_json != "{}") ||
           ((effect.type == LayerEffectType::Noise || effect.type == LayerEffectType::Grain || effect.type == LayerEffectType::FilmDistortion || effect.type == LayerEffectType::AnalogDistortion || effect.type == LayerEffectType::DigitalDistortion) && effect.effect_animated);
}

static bool legacy_background_has_animation(const Layer &layer)
{
    return layer.background_enabled_prop.is_animated() ||
           layer.background_opacity_prop.is_animated() ||
           layer.background_padding_x_prop.is_animated() ||
           layer.background_padding_y_prop.is_animated() ||
           layer.background_padding_left_prop.is_animated() ||
           layer.background_padding_right_prop.is_animated() ||
           layer.background_padding_top_prop.is_animated() ||
           layer.background_padding_bottom_prop.is_animated() ||
           layer.background_corner_radius_prop.is_animated() ||
           layer.background_corner_radius_tl_prop.is_animated() ||
           layer.background_corner_radius_tr_prop.is_animated() ||
           layer.background_corner_radius_br_prop.is_animated() ||
           layer.background_corner_radius_bl_prop.is_animated() ||
           layer.background_stroke_width_prop.is_animated() ||
           layer.background_stroke_opacity_prop.is_animated() ||
           layer.background_color_a.is_animated() ||
           layer.background_color_r.is_animated() ||
           layer.background_color_g.is_animated() ||
           layer.background_color_b.is_animated() ||
           layer.background_stroke_color_a.is_animated() ||
           layer.background_stroke_color_r.is_animated() ||
           layer.background_stroke_color_g.is_animated() ||
           layer.background_stroke_color_b.is_animated();
}

static bool legacy_shadow_has_animation(const Layer &layer)
{
    return layer.shadow_enabled_prop.is_animated() ||
           layer.shadow_opacity_prop.is_animated() ||
           layer.shadow_distance_prop.is_animated() ||
           layer.shadow_angle_prop.is_animated() ||
           layer.shadow_blur_prop.is_animated() ||
           layer.shadow_spread_prop.is_animated() ||
           layer.shadow_color_a.is_animated() ||
           layer.shadow_color_r.is_animated() ||
           layer.shadow_color_g.is_animated() ||
           layer.shadow_color_b.is_animated();
}

static double positive_mod(double value, double modulus)
{
    if (modulus <= kEpsilon)
        return 0.0;
    double result = std::fmod(value, modulus);
    if (result < 0.0)
        result += modulus;
    return result;
}

static double bounded_elapsed(double elapsed, double cycle_duration, bool repeat)
{
    if (repeat)
        return positive_mod(elapsed, std::max(kEpsilon, cycle_duration));
    return std::clamp(elapsed, 0.0, std::max(0.0, cycle_duration));
}

static double map_play_once(const Layer &layer, double elapsed)
{
    const double duration = std::max(0.1, layer.asset_duration);
    return bounded_elapsed(elapsed, duration, layer.asset_loop);
}

static double map_pause_mode(const Layer &layer, double elapsed)
{
    const double duration = std::max(0.1, layer.asset_duration);
    const double pause_at = std::clamp(layer.asset_source_pause_time, 0.0, duration);
    const double pause_for = std::max(0.0, layer.asset_pause_duration);
    const double pass_duration = duration + pause_for;
    const double pass_time = bounded_elapsed(elapsed, pass_duration, layer.asset_loop);

    if (pass_time <= pause_at)
        return pass_time;
    if (pass_time <= pause_at + pause_for)
        return pause_at;
    return std::clamp(pass_time - pause_for, pause_at, duration);
}

static double map_restart_loop_mode(const Layer &layer, double elapsed,
                                    double loop_start, double loop_end)
{
    const double duration = std::max(0.1, layer.asset_duration);
    const double loop_length = loop_end - loop_start;
    const int loop_count = std::max(1, layer.asset_loop_count);
    const double loop_window = loop_length * static_cast<double>(loop_count);
    const double pass_duration = duration + loop_length * static_cast<double>(loop_count - 1);
    const double pass_time = bounded_elapsed(elapsed, pass_duration, layer.asset_loop);

    if (pass_time < loop_start)
        return pass_time;
    if (pass_time < loop_start + loop_window)
        return loop_start + positive_mod(pass_time - loop_start, loop_length);
    return std::clamp(loop_end + (pass_time - loop_start - loop_window),
                      loop_end, duration);
}

static double map_ping_pong_loop_mode(const Layer &layer, double elapsed,
                                      double loop_start, double loop_end)
{
    const double duration = std::max(0.1, layer.asset_duration);
    const double loop_length = loop_end - loop_start;
    const int loop_count = std::max(1, layer.asset_loop_count);
    /* One requested loop is one forward pass. Reverse legs are inserted only
     * between forward passes so the final pass always reaches loop_end and can
     * continue into the outro without a discontinuity. */
    const int segment_count = std::max(1, loop_count * 2 - 1);
    const double loop_window = loop_length * static_cast<double>(segment_count);
    const double pass_duration = duration +
        loop_length * static_cast<double>(segment_count - 1);
    const double pass_time = bounded_elapsed(elapsed, pass_duration, layer.asset_loop);

    if (pass_time < loop_start)
        return pass_time;
    if (pass_time < loop_start + loop_window) {
        const double relative = pass_time - loop_start;
        const int segment = std::min(segment_count - 1,
            static_cast<int>(std::floor(relative / loop_length)));
        const double phase = positive_mod(relative, loop_length);
        return (segment % 2 == 0) ? loop_start + phase : loop_end - phase;
    }
    return std::clamp(loop_end + (pass_time - loop_start - loop_window),
                      loop_end, duration);
}

static double map_loop_mode(const Layer &layer, double elapsed)
{
    const double duration = std::max(0.1, layer.asset_duration);
    const double loop_start = std::clamp(layer.asset_source_loop_start, 0.0, duration);
    const double loop_end = std::clamp(layer.asset_source_loop_end, loop_start, duration);
    if (loop_end - loop_start <= kEpsilon)
        return map_play_once(layer, elapsed);

    if (layer.asset_source_loop_type == 1)
        return map_ping_pong_loop_mode(layer, elapsed, loop_start, loop_end);
    return map_restart_loop_mode(layer, elapsed, loop_start, loop_end);
}

static double map_independent_elapsed(const Layer &layer, double elapsed)
{
    const double shifted = elapsed + layer.asset_playback_offset;
    switch (std::clamp(layer.asset_source_playback_mode, 0, 2)) {
    case 1:
        return map_loop_mode(layer, shifted);
    case 2:
        return map_pause_mode(layer, shifted);
    case 0:
    default:
        return map_play_once(layer, shifted);
    }
}

static double independent_elapsed(const std::string &title_id,
                                  const Layer &layer)
{
    const auto now = Clock::now();
    const std::string key = title_id + "\n" + layer.id;
    const std::uint64_t signature = configuration_signature(layer);

    std::lock_guard<std::mutex> lock(g_mutex);
    RuntimeState &state = g_states[key];
    if (!state.initialized || state.title_id != title_id ||
        state.configuration_signature != signature) {
        state.title_id = title_id;
        state.anchor = now;
        state.configuration_signature = signature;
        state.initialized = true;
    }
    return std::max(0.0, std::chrono::duration<double>(now - state.anchor).count());
}

} // namespace

bool layer_has_timeline_animation(const Layer &layer)
{
    const bool transition_animation = std::any_of(
        layer.transitions.begin(), layer.transitions.end(),
        [](const LayerTransition &transition) {
            return transition.enabled && transition.duration > kEpsilon;
        });
    const bool effect_animation = std::any_of(
        layer.effects.begin(), layer.effects.end(), effect_has_animation);
    const bool nested_asset_animation =
        layer.type == LayerType::Asset && layer.asset_animated;
    /* Video and audio advance from local timeline time even without authored
     * keyframes.  Treat them as animated asset content so a media-only Asset
     * exposes synchronized/independent playback instead of being frozen as a
     * static library item. */
    const bool media_playback = layer.type == LayerType::Video ||
                                layer.type == LayerType::Audio;

    return nested_asset_animation || media_playback || transition_animation ||
           effect_animation ||
           layer.position.is_animated() ||
           layer.position_z.is_animated() ||
           (layer.position_3d_path_enabled && layer.position_3d.is_animated()) ||
           layer.scale.is_animated() ||
           layer.scale_z.is_animated() ||
           layer.rotation.is_animated() ||
           layer.rotation_x.is_animated() ||
           layer.rotation_y.is_animated() ||
           layer.anchor_z.is_animated() ||
           layer.orientation_x.is_animated() ||
           layer.orientation_y.is_animated() ||
           layer.orientation_z.is_animated() ||
           layer.opacity.is_animated() ||
           layer.material_ambient.is_animated() ||
           layer.material_diffuse.is_animated() ||
           layer.material_specular.is_animated() ||
           layer.material_shininess.is_animated() ||
           layer.material_metallic.is_animated() ||
           layer.material_roughness.is_animated() ||
           layer.material_reflection_intensity.is_animated() ||
           layer.material_emissive_color_a.is_animated() ||
           layer.material_emissive_color_r.is_animated() ||
           layer.material_emissive_color_g.is_animated() ||
           layer.material_emissive_color_b.is_animated() ||
           layer.material_emissive_intensity.is_animated() ||
           layer.transform_quad_tl.is_animated() ||
           layer.transform_quad_tr.is_animated() ||
           layer.transform_quad_br.is_animated() ||
           layer.transform_quad_bl.is_animated() ||
           layer.size.is_animated() ||
           layer.image_size.is_animated() ||
           layer.origin_prop.is_animated() ||
           layer.font_size_prop.is_animated() ||
           layer.char_tracking_prop.is_animated() ||
           layer.char_scale_x_prop.is_animated() ||
           layer.char_scale_y_prop.is_animated() ||
           layer.baseline_shift_prop.is_animated() ||
           layer.ticker_completion_prop.is_animated() ||
           layer.paragraph_indent_left_prop.is_animated() ||
           layer.paragraph_indent_right_prop.is_animated() ||
           layer.paragraph_indent_first_line_prop.is_animated() ||
           layer.paragraph_space_before_prop.is_animated() ||
           layer.paragraph_space_after_prop.is_animated() ||
           layer.text_color_a.is_animated() ||
           layer.text_color_r.is_animated() ||
           layer.text_color_g.is_animated() ||
           layer.text_color_b.is_animated() ||
           layer.fill_color_a.is_animated() ||
           layer.fill_color_r.is_animated() ||
           layer.fill_color_g.is_animated() ||
           layer.fill_color_b.is_animated() ||
           layer.stroke_color_a.is_animated() ||
           layer.stroke_color_r.is_animated() ||
           layer.stroke_color_g.is_animated() ||
           layer.stroke_color_b.is_animated() ||
           layer.stroke_width_prop.is_animated() ||
           layer.stroke_offset_prop.is_animated() ||
           layer.chart_animation_progress.is_animated() ||
           layer.chart_reveal_progress.is_animated() ||
           layer.chart_value_transition.is_animated() ||
           layer.chart_label_opacity.is_animated() ||
           layer.chart_element_opacity.is_animated() ||
           layer.chart_font_size.is_animated() ||
           layer.chart_line_thickness.is_animated() ||
           layer.chart_bar_width.is_animated() ||
           layer.chart_bar_spacing.is_animated() ||
           layer.chart_corner_radius.is_animated() ||
           layer.chart_padding.is_animated() ||
           layer.chart_margin_left.is_animated() ||
           layer.chart_margin_top.is_animated() ||
           layer.chart_margin_right.is_animated() ||
           layer.chart_margin_bottom.is_animated() ||
           legacy_background_has_animation(layer) ||
           legacy_shadow_has_animation(layer);
}

bool layer_has_raster_animation(const Layer &layer)
{
    const bool transition_animation = std::any_of(
        layer.transitions.begin(), layer.transitions.end(),
        [](const LayerTransition &transition) {
            return transition.enabled && transition.duration > kEpsilon;
        });
    const bool effect_animation = std::any_of(
        layer.effects.begin(), layer.effects.end(), effect_has_animation);
    return layer.type == LayerType::Clock ||
           layer.type == LayerType::Ticker ||
           layer.type == LayerType::Video ||
           (layer.type == LayerType::Asset && layer.asset_animated) ||
           transition_animation || effect_animation ||
           layer.size.is_animated() || layer.image_size.is_animated() ||
           layer.origin_prop.is_animated() ||
           layer.font_size_prop.is_animated() ||
           layer.char_tracking_prop.is_animated() ||
           layer.char_scale_x_prop.is_animated() ||
           layer.char_scale_y_prop.is_animated() ||
           layer.baseline_shift_prop.is_animated() ||
           layer.ticker_completion_prop.is_animated() ||
           layer.paragraph_indent_left_prop.is_animated() ||
           layer.paragraph_indent_right_prop.is_animated() ||
           layer.paragraph_indent_first_line_prop.is_animated() ||
           layer.paragraph_space_before_prop.is_animated() ||
           layer.paragraph_space_after_prop.is_animated() ||
           layer.text_color_a.is_animated() ||
           layer.text_color_r.is_animated() ||
           layer.text_color_g.is_animated() ||
           layer.text_color_b.is_animated() ||
           layer.fill_color_a.is_animated() ||
           layer.fill_color_r.is_animated() ||
           layer.fill_color_g.is_animated() ||
           layer.fill_color_b.is_animated() ||
           layer.stroke_color_a.is_animated() ||
           layer.stroke_color_r.is_animated() ||
           layer.stroke_color_g.is_animated() ||
           layer.stroke_color_b.is_animated() ||
           layer.stroke_width_prop.is_animated() ||
           layer.stroke_offset_prop.is_animated() ||
           layer.chart_animation_progress.is_animated() ||
           layer.chart_reveal_progress.is_animated() ||
           layer.chart_value_transition.is_animated() ||
           layer.chart_label_opacity.is_animated() ||
           layer.chart_element_opacity.is_animated() ||
           layer.chart_font_size.is_animated() ||
           layer.chart_line_thickness.is_animated() ||
           layer.chart_bar_width.is_animated() ||
           layer.chart_bar_spacing.is_animated() ||
           layer.chart_corner_radius.is_animated() ||
           layer.chart_padding.is_animated() ||
           layer.chart_margin_left.is_animated() ||
           layer.chart_margin_top.is_animated() ||
           layer.chart_margin_right.is_animated() ||
           layer.chart_margin_bottom.is_animated() ||
           legacy_background_has_animation(layer) ||
           legacy_shadow_has_animation(layer);
}

bool title_has_timeline_animation(const Title &title)
{
    const auto camera_is_animated = [](const TitleCamera &camera) {
        return (camera.position_3d_path_enabled && camera.position_3d.is_animated()) ||
               (camera.target_3d_path_enabled && camera.target_3d.is_animated()) ||
               camera.position_x.is_animated() ||
               camera.position_y.is_animated() ||
               camera.position_z.is_animated() ||
               camera.target_x.is_animated() ||
               camera.target_y.is_animated() ||
               camera.target_z.is_animated() ||
               camera.orientation_x.is_animated() ||
               camera.orientation_y.is_animated() ||
               camera.orientation_z.is_animated() ||
               camera.rotation_x.is_animated() ||
               camera.rotation_y.is_animated() ||
               camera.rotation_z.is_animated() ||
               camera.focal_length.is_animated() ||
               camera.field_of_view.is_animated() ||
               camera.zoom.is_animated() ||
               camera.near_clip.is_animated() ||
               camera.far_clip.is_animated() ||
               camera.projection_mode.is_animated();
    };
    if (title.active_camera.is_animated() ||
        std::any_of(title.cameras.begin(), title.cameras.end(), camera_is_animated) ||
        std::any_of(title.lights.begin(), title.lights.end(),
                    [](const TitleLight &light) {
                        return title_light_has_authored_keyframes(light);
                    }))
        return true;
    const double duration = std::max(0.0, title.duration);
    return std::any_of(title.layers.begin(), title.layers.end(),
        [duration](const std::shared_ptr<Layer> &layer) {
            if (!layer)
                return false;
            const bool timed_visibility = layer->in_time > kEpsilon ||
                layer->out_time < duration - kEpsilon;
            return timed_visibility || layer->camera_assignment.is_animated() ||
                   layer_has_timeline_animation(*layer);
        });
}

bool asset_layer_has_timeline_animation(const Title &title,
                                         const Layer &asset_layer)
{
    if (asset_layer.type != LayerType::Asset)
        return false;

    auto belongs_to_asset = [&title, &asset_layer](const Layer &candidate) {
        std::string owner_id = candidate.asset_owner_id;
        std::size_t guard = 0;
        while (!owner_id.empty() && guard++ <= title.layers.size()) {
            if (owner_id == asset_layer.id)
                return true;
            const Layer *owner = nullptr;
            for (const auto &candidate_owner : title.layers) {
                if (candidate_owner && candidate_owner->id == owner_id) {
                    owner = candidate_owner.get();
                    break;
                }
            }
            if (!owner || owner->type != LayerType::Asset)
                return false;
            owner_id = owner->asset_owner_id;
        }
        return false;
    };

    const double duration = std::max(0.1, asset_layer.asset_duration);
    return std::any_of(title.layers.begin(), title.layers.end(),
        [&](const std::shared_ptr<Layer> &candidate) {
            if (!candidate || candidate->id == asset_layer.id ||
                !belongs_to_asset(*candidate))
                return false;
            const bool timed_visibility = candidate->in_time > kEpsilon ||
                candidate->out_time < duration - kEpsilon;
            return timed_visibility || layer_has_timeline_animation(*candidate);
        });
}

bool layer_uses_independent_playback(const Layer &layer)
{
    return layer.type == LayerType::Asset && layer.asset_animated &&
           layer.asset_playback_mode == 1;
}

bool title_has_independent_playback(const Title &title)
{
    return std::any_of(
        title.layers.begin(), title.layers.end(),
        [](const std::shared_ptr<Layer> &layer) {
            return layer && layer_uses_independent_playback(*layer);
        });
}

double map_elapsed_to_local_time(const Layer &asset_layer,
                                 double elapsed_seconds)
{
    return map_independent_elapsed(asset_layer, elapsed_seconds);
}

double resolve_local_time(const std::string &title_id, const Layer &asset_layer,
                          double synchronized_time)
{
    const double duration = std::max(0.1, asset_layer.asset_duration);
    if (!asset_layer.asset_animated)
        return std::clamp(synchronized_time, 0.0, duration);

    if (asset_layer.asset_playback_mode != 1)
        return std::clamp(synchronized_time, 0.0, duration);

    return map_elapsed_to_local_time(asset_layer,
                                     independent_elapsed(title_id, asset_layer));
}

void reset_layer(const std::string &title_id, const std::string &layer_id)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_states.erase(title_id + "\n" + layer_id);
}

void clear_title(const std::string &title_id)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto it = g_states.begin(); it != g_states.end();) {
        if (it->second.title_id == title_id)
            it = g_states.erase(it);
        else
            ++it;
    }
}

void clear_all()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_states.clear();
}

} // namespace fxm::asset_runtime
