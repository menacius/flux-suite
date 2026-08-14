#include "stinger-transition.h"

#include "title-data.h"
#include "title-source.h"
#include "cache-manager.h"
#include "performance-counters.h"

#include <obs-module.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr const char *kTransitionSourceId =
    "flux_motion_stinger_transition";
constexpr const char *kStingerTitleId = "stinger_title_id";
constexpr const char *kAudioBehavior = "audio_behavior";
constexpr const char *kAudioFollowVideoPoint = "audio_follow_video_point";
constexpr const char *kAudioTransitionPointPercent =
    "audio_transition_point_percent";
constexpr const char *kCustomCurveX1 = "custom_curve_x1";
constexpr const char *kCustomCurveY1 = "custom_curve_y1";
constexpr const char *kCustomCurveX2 = "custom_curve_x2";
constexpr const char *kCustomCurveY2 = "custom_curve_y2";
constexpr const char *kProxyFallback = "proxy_fallback";
constexpr const char *kValidationStatus = "validation_status";
constexpr const char *kPreviewHint = "preview_hint";

constexpr double kPointEpsilon = 0.0000005;
constexpr int64_t kMinimumResyncMs = 40;

enum class StingerAudioBehavior : int {
    StingerAudioOnly = 0,
    CrossfadeSceneAudio = 1,
    CutSceneAudio = 2,
    CustomTransitionCurve = 3,
};

enum class StingerProxyFallback : int {
    SafeLiveRender = 0,
    RequireValidProxy = 1,
};

struct ProxyReadiness {
    bool requested = false;
    bool cache_enabled = false;
    bool cacheable = false;
    bool ready = false;
    int ready_frames = 0;
    int total_frames = 0;

    int progress_percent() const
    {
        if (total_frames <= 0)
            return ready ? 100 : 0;
        return std::clamp(
            static_cast<int>(std::lround(
                100.0 * static_cast<double>(ready_frames) /
                static_cast<double>(total_frames))),
            0, 100);
    }
};

struct FxmStingerTransition {
    obs_source_t *source = nullptr;
    obs_source_t *graphic_source = nullptr;
    std::string title_id;
    uint64_t store_revision = 0;

    double document_duration = 1.0;
    double pre_roll = 0.0;
    double post_roll = 0.0;
    double total_duration = 1.0;
    float transition_point = 0.5f;
    float audio_transition_point = 0.5f;

    StingerAudioBehavior audio_behavior =
        StingerAudioBehavior::CrossfadeSceneAudio;
    bool audio_follow_video_point = true;
    float custom_curve_x1 = 0.42f;
    float custom_curve_y1 = 0.0f;
    float custom_curve_x2 = 0.58f;
    float custom_curve_y2 = 1.0f;
    StingerProxyFallback proxy_fallback =
        StingerProxyFallback::SafeLiveRender;
    StingerSwitchMode switch_mode = StingerSwitchMode::SwitchAtPoint;
    TitleGpuRenderSession *manual_render_session = nullptr;

    bool audio_enabled = true;
    bool alpha_output = true;
    bool document_valid = false;
    bool proxy_ready = false;
    int proxy_progress = 0;
    bool runtime_graphic_allowed = false;

    bool transitioning = false;
    bool scene_switched = false;
    bool child_active = false;
    bool child_started = false;
    bool child_holding = false;
    uint64_t transition_run = 0;
};

static const char *stinger_get_name(void *)
{
    return obs_module_text("OBSTitles.StingerObsTransition");
}

static std::shared_ptr<Title> selected_stinger(const std::string &id)
{
    auto title = TitleDataStore::instance().get_title_snapshot(id);
    if (!title || title->graphic_type != TitleGraphicType::Stinger)
        return {};
    return title;
}

static ProxyReadiness inspect_proxy(const std::shared_ptr<Title> &title)
{
    ProxyReadiness status;
    if (!title ||
        title->stinger_render_mode != StingerRenderMode::PrerenderedProxy)
        return status;

    status.requested = true;
    CacheManager &cache = CacheManager::instance();
    status.cache_enabled = cache.cacheEnabled();
    status.cacheable =
        cache.titleCacheability(title) != TitleCacheability::NonCacheable;
    if (!status.cache_enabled || !status.cacheable)
        return status;

    const double fps = std::max(1.0, cache.effectiveFrameRate());
    const int last_frame = std::max(
        0, cache.frameIndexForTitleTime(*title,
                                       std::max(0.0, title->duration)));
    status.total_frames = last_frame + 1;
    for (int frame = 0; frame <= last_frame; ++frame) {
        const double time = std::min(
            std::max(0.0, title->duration),
            static_cast<double>(frame) / fps);
        if (cache.frameReadyForPlayback(title, time))
            ++status.ready_frames;
    }
    status.ready = status.total_frames > 0 &&
                   status.ready_frames == status.total_frames;
    return status;
}

static std::string proxy_status_text(const ProxyReadiness &proxy,
                                     StingerProxyFallback fallback)
{
    if (!proxy.requested)
        return "Live render mode is active.";
    if (!proxy.cache_enabled)
        return fallback == StingerProxyFallback::RequireValidProxy
            ? "Prerender proxy is required, but the global cache is disabled."
            : "The global cache is disabled; safe live rendering will be used.";
    if (!proxy.cacheable)
        return fallback == StingerProxyFallback::RequireValidProxy
            ? "Prerender proxy is required, but this document contains non-cacheable live content."
            : "This document contains non-cacheable live content; safe live rendering will be used.";
    if (proxy.ready)
        return "Prerender proxy is valid and ready (100%).";

    std::ostringstream stream;
    stream << "Prerender proxy is missing or stale ("
           << proxy.progress_percent() << "% ready). ";
    stream << (fallback == StingerProxyFallback::RequireValidProxy
        ? "The FXM overlay will be blocked until the proxy is valid."
        : "Safe live rendering will be used while the proxy is rebuilt.");
    return stream.str();
}

static StingerAudioBehavior audio_behavior_from_settings(obs_data_t *settings)
{
    const int value = static_cast<int>(
        obs_data_get_int(settings, kAudioBehavior));
    return static_cast<StingerAudioBehavior>(std::clamp(
        value,
        static_cast<int>(StingerAudioBehavior::StingerAudioOnly),
        static_cast<int>(StingerAudioBehavior::CustomTransitionCurve)));
}

static StingerProxyFallback proxy_fallback_from_settings(obs_data_t *settings)
{
    const int value = static_cast<int>(
        obs_data_get_int(settings, kProxyFallback));
    return static_cast<StingerProxyFallback>(std::clamp(
        value,
        static_cast<int>(StingerProxyFallback::SafeLiveRender),
        static_cast<int>(StingerProxyFallback::RequireValidProxy)));
}

static std::string build_validation_status(obs_data_t *settings,
                                           int *severity_out = nullptr)
{
    int severity = OBS_TEXT_INFO_NORMAL;
    const char *selected_id = obs_data_get_string(settings, kStingerTitleId);
    auto title = selected_stinger(selected_id ? selected_id : "");
    if (!title) {
        if (severity_out)
            *severity_out = OBS_TEXT_INFO_ERROR;
        return obs_module_text("OBSTitles.StingerValidationNoDocument");
    }

    const StingerValidationResult validation = validate_stinger_title(*title);
    std::ostringstream stream;
    if (!validation.errors.empty()) {
        severity = OBS_TEXT_INFO_ERROR;
        stream << "Invalid Stinger: ";
        for (size_t i = 0; i < validation.errors.size(); ++i) {
            if (i)
                stream << " ";
            stream << validation.errors[i];
        }
    } else if (!validation.warnings.empty()) {
        severity = OBS_TEXT_INFO_WARNING;
        stream << "Warning: ";
        for (size_t i = 0; i < validation.warnings.size(); ++i) {
            if (i)
                stream << " ";
            stream << validation.warnings[i];
        }
    } else {
        stream << obs_module_text("OBSTitles.StingerValidationOk");
    }

    const StingerProxyFallback fallback =
        proxy_fallback_from_settings(settings);
    const ProxyReadiness proxy = inspect_proxy(title);
    if (proxy.requested) {
        const bool proxy_error = !proxy.ready &&
            fallback == StingerProxyFallback::RequireValidProxy;
        if (proxy_error)
            severity = OBS_TEXT_INFO_ERROR;
        else if (!proxy.ready && severity == OBS_TEXT_INFO_NORMAL)
            severity = OBS_TEXT_INFO_WARNING;
        stream << "\n" << proxy_status_text(proxy, fallback);
    }

    if (severity_out)
        *severity_out = severity;
    return stream.str();
}

static void update_validation_setting(obs_properties_t *properties,
                                      obs_data_t *settings)
{
    int severity = OBS_TEXT_INFO_NORMAL;
    const std::string status = build_validation_status(settings, &severity);
    obs_data_set_string(settings, kValidationStatus, status.c_str());
    if (!properties)
        return;
    if (obs_property_t *property =
            obs_properties_get(properties, kValidationStatus)) {
        obs_property_text_set_info_type(
            property, static_cast<obs_text_info_type>(severity));
        obs_property_text_set_info_word_wrap(property, true);
    }
}

static void remove_active_child(FxmStingerTransition *data)
{
    if (!data || !data->graphic_source || !data->child_active)
        return;
    obs_source_remove_active_child(data->source, data->graphic_source);
    data->child_active = false;
}

static void release_graphic_source(FxmStingerTransition *data)
{
    if (!data)
        return;
    remove_active_child(data);
    if (data->graphic_source)
        obs_source_release(data->graphic_source);
    data->graphic_source = nullptr;
    data->child_started = false;
    data->child_holding = false;
}

static void create_graphic_source(FxmStingerTransition *data)
{
    if (!data || data->title_id.empty())
        return;

    obs_data_t *settings = obs_data_create();
    obs_data_set_string(settings, PROP_TITLE_ID, data->title_id.c_str());
    obs_data_set_bool(settings, PROP_CUE_FIRST_ROW_WHEN_ACTIVE, false);
    obs_data_set_bool(settings, "editor_audio_preview", true);

    const char *source_name = obs_source_get_name(data->source);
    std::string name = source_name ? source_name : "FXM Stinger";
    name += " (Graphic)";
    data->graphic_source = obs_source_create_private(
        "flux_motion_source", name.c_str(), settings);
    obs_data_release(settings);

    if (!data->graphic_source)
        return;

    obs_source_set_muted(data->graphic_source, !data->audio_enabled);
    obs_source_media_stop(data->graphic_source);
    if (data->transitioning && data->runtime_graphic_allowed) {
        obs_source_add_active_child(data->source, data->graphic_source);
        data->child_active = true;
    }
}

static void update_runtime_proxy_policy(
    FxmStingerTransition *data, const std::shared_ptr<Title> &title)
{
    const ProxyReadiness proxy = inspect_proxy(title);
    data->proxy_ready = proxy.ready;
    data->proxy_progress = proxy.progress_percent();

    if (title &&
        title->stinger_render_mode == StingerRenderMode::PrerenderedProxy) {
        if (!proxy.ready)
            CacheManager::instance().queueWholeTimeline(title);
        data->runtime_graphic_allowed = proxy.ready ||
            data->proxy_fallback == StingerProxyFallback::SafeLiveRender;
    } else {
        data->runtime_graphic_allowed = true;
    }
}

static void refresh_document(FxmStingerTransition *data, bool recreate_child)
{
    if (!data)
        return;

    auto title = selected_stinger(data->title_id);
    if (!title) {
        data->document_duration = 1.0;
        data->pre_roll = 0.0;
        data->post_roll = 0.0;
        data->total_duration = 1.0;
        data->transition_point = 0.5f;
        data->audio_transition_point = 0.5f;
        data->document_valid = false;
        data->proxy_ready = false;
        data->proxy_progress = 0;
        data->runtime_graphic_allowed = false;
        data->switch_mode = StingerSwitchMode::SwitchAtPoint;
        obs_transition_enable_fixed(data->source, true, 1000);
        release_graphic_source(data);
        return;
    }

    const StingerValidationResult validation = validate_stinger_title(*title);
    data->document_valid = validation.valid();
    data->document_duration = std::max(0.001, title->duration);
    data->pre_roll = std::max(0.0, title->stinger_pre_roll);
    data->post_roll = std::max(0.0, title->stinger_post_roll);
    data->total_duration = std::max(
        0.001, data->pre_roll + data->document_duration + data->post_roll);
    data->audio_enabled = title->stinger_audio_enabled;
    data->alpha_output = title->stinger_alpha_output;
    data->switch_mode = title->stinger_switch_mode;

    const double switch_seconds = data->pre_roll +
        stinger_transition_point_seconds(*title);
    data->transition_point = static_cast<float>(std::clamp(
        switch_seconds / data->total_duration, 0.001, 0.999));
    if (data->audio_follow_video_point) {
        data->audio_transition_point = data->transition_point;
    }

    const uint32_t duration_ms = static_cast<uint32_t>(std::clamp(
        std::llround(data->total_duration * 1000.0), 1LL, 3600000LL));
    obs_transition_enable_fixed(data->source, true, duration_ms);

    update_runtime_proxy_policy(data, title);
    if (data->manual_render_session)
        title_gpu_render_session_invalidate_presentation(
            data->manual_render_session, true);
    data->runtime_graphic_allowed =
        data->runtime_graphic_allowed && data->document_valid;

    if (recreate_child || !data->graphic_source) {
        release_graphic_source(data);
        create_graphic_source(data);
    } else {
        obs_source_set_muted(data->graphic_source, !data->audio_enabled);
        obs_data_t *settings = obs_source_get_settings(data->graphic_source);
        obs_data_set_string(settings, PROP_TITLE_ID, data->title_id.c_str());
        obs_data_set_bool(settings, PROP_CUE_FIRST_ROW_WHEN_ACTIVE, false);
        obs_data_set_bool(settings, "editor_audio_preview", true);
        obs_source_update(data->graphic_source, settings);
        obs_data_release(settings);
    }

    if (data->transitioning) {
        if (data->runtime_graphic_allowed && data->graphic_source &&
            !data->child_active) {
            obs_source_add_active_child(data->source, data->graphic_source);
            data->child_active = true;
        } else if (!data->runtime_graphic_allowed) {
            if (data->graphic_source)
                obs_source_media_stop(data->graphic_source);
            remove_active_child(data);
            data->child_started = false;
            data->child_holding = false;
        }
    }

    for (const std::string &error : validation.errors) {
        blog(LOG_ERROR, "[Flux Motion] Stinger '%s': %s",
             title->name.c_str(), error.c_str());
    }
    for (const std::string &warning : validation.warnings) {
        blog(LOG_WARNING, "[Flux Motion] Stinger '%s': %s",
             title->name.c_str(), warning.c_str());
    }
    if (title->stinger_render_mode == StingerRenderMode::PrerenderedProxy &&
        !data->proxy_ready) {
        blog(data->proxy_fallback == StingerProxyFallback::RequireValidProxy
                 ? LOG_ERROR
                 : LOG_WARNING,
             "[Flux Motion] Stinger '%s': proxy %d%% ready; %s.",
             title->name.c_str(), data->proxy_progress,
             data->proxy_fallback == StingerProxyFallback::RequireValidProxy
                 ? "overlay blocked by strict proxy policy"
                 : "using safe live-render fallback");
    }
}

static void stinger_update(void *private_data, obs_data_t *settings)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data || !settings)
        return;

    const char *selected_id = obs_data_get_string(settings, kStingerTitleId);
    std::string new_id = selected_id ? selected_id : "";
    if (new_id.empty()) {
        for (const auto &title : TitleDataStore::instance().title_snapshots()) {
            if (title && !title->is_asset &&
                title->graphic_type == TitleGraphicType::Stinger) {
                new_id = title->id;
                obs_data_set_string(settings, kStingerTitleId,
                                    new_id.c_str());
                break;
            }
        }
    }

    data->audio_behavior = audio_behavior_from_settings(settings);
    data->audio_follow_video_point =
        obs_data_get_bool(settings, kAudioFollowVideoPoint);
    data->custom_curve_x1 = static_cast<float>(std::clamp(
        obs_data_get_double(settings, kCustomCurveX1), 0.0, 1.0));
    data->custom_curve_y1 = static_cast<float>(std::clamp(
        obs_data_get_double(settings, kCustomCurveY1), 0.0, 1.0));
    data->custom_curve_x2 = static_cast<float>(std::clamp(
        obs_data_get_double(settings, kCustomCurveX2), 0.0, 1.0));
    data->custom_curve_y2 = static_cast<float>(std::clamp(
        obs_data_get_double(settings, kCustomCurveY2), 0.0, 1.0));
    if (data->custom_curve_x1 > data->custom_curve_x2)
        std::swap(data->custom_curve_x1, data->custom_curve_x2);
    data->proxy_fallback = proxy_fallback_from_settings(settings);
    if (!obs_data_get_string(settings, kPreviewHint)[0]) {
        obs_data_set_string(
            settings, kPreviewHint,
            obs_module_text("OBSTitles.StingerNativePreviewHint"));
    }

    const float configured_audio_point = static_cast<float>(std::clamp(
        obs_data_get_double(settings, kAudioTransitionPointPercent) / 100.0,
        0.001, 0.999));
    if (!data->audio_follow_video_point)
        data->audio_transition_point = configured_audio_point;

    const bool changed = new_id != data->title_id;
    data->title_id = std::move(new_id);
    data->store_revision = TitleDataStore::instance().revision();
    refresh_document(data, changed);
    update_validation_setting(nullptr, settings);
}

static void *stinger_create(obs_data_t *settings, obs_source_t *source)
{
    auto *data = new FxmStingerTransition;
    data->source = source;
    data->manual_render_session = title_gpu_render_session_create();
    title_gpu_render_session_set_realtime_output(
        data->manual_render_session, true);
    obs_transition_enable_fixed(source, true, 1000);
    stinger_update(data, settings);
    return data;
}

static void stinger_destroy(void *private_data)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return;
    release_graphic_source(data);
    if (data->manual_render_session)
        title_gpu_render_session_destroy(data->manual_render_session);
    data->manual_render_session = nullptr;
    delete data;
}

static void stinger_defaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, kStingerTitleId, "");
    obs_data_set_default_int(
        settings, kAudioBehavior,
        static_cast<int>(StingerAudioBehavior::CrossfadeSceneAudio));
    obs_data_set_default_bool(settings, kAudioFollowVideoPoint, true);
    obs_data_set_default_double(settings, kAudioTransitionPointPercent, 50.0);
    obs_data_set_default_double(settings, kCustomCurveX1, 0.42);
    obs_data_set_default_double(settings, kCustomCurveY1, 0.0);
    obs_data_set_default_double(settings, kCustomCurveX2, 0.58);
    obs_data_set_default_double(settings, kCustomCurveY2, 1.0);
    obs_data_set_default_int(
        settings, kProxyFallback,
        static_cast<int>(StingerProxyFallback::SafeLiveRender));
    obs_data_set_default_string(settings, kValidationStatus, "");
    obs_data_set_default_string(
        settings, kPreviewHint,
        obs_module_text("OBSTitles.StingerNativePreviewHint"));
}

static bool stinger_properties_modified(obs_properties_t *properties,
                                        obs_property_t *,
                                        obs_data_t *settings)
{
    const StingerAudioBehavior behavior =
        audio_behavior_from_settings(settings);
    const bool uses_audio_point =
        behavior == StingerAudioBehavior::CutSceneAudio ||
        behavior == StingerAudioBehavior::CustomTransitionCurve;
    const bool follows_video =
        obs_data_get_bool(settings, kAudioFollowVideoPoint);
    const bool custom =
        behavior == StingerAudioBehavior::CustomTransitionCurve;

    if (obs_property_t *property =
            obs_properties_get(properties, kAudioFollowVideoPoint))
        obs_property_set_visible(property, uses_audio_point);
    if (obs_property_t *property =
            obs_properties_get(properties, kAudioTransitionPointPercent)) {
        obs_property_set_visible(property, uses_audio_point);
        obs_property_set_enabled(property, !follows_video);
    }
    for (const char *name : {kCustomCurveX1, kCustomCurveY1,
                             kCustomCurveX2, kCustomCurveY2}) {
        if (obs_property_t *property = obs_properties_get(properties, name))
            obs_property_set_visible(property, custom);
    }

    update_validation_setting(properties, settings);
    return true;
}

static obs_properties_t *stinger_properties(void *)
{
    obs_properties_t *properties = obs_properties_create();
    obs_properties_set_flags(properties, OBS_PROPERTIES_DEFER_UPDATE);

    obs_property_t *list = obs_properties_add_list(
        properties, kStingerTitleId,
        obs_module_text("OBSTitles.StingerDocument"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    bool found = false;
    for (const auto &title : TitleDataStore::instance().title_snapshots()) {
        if (!title || title->is_asset ||
            title->graphic_type != TitleGraphicType::Stinger)
            continue;
        obs_property_list_add_string(list, title->name.c_str(),
                                     title->id.c_str());
        found = true;
    }
    if (!found) {
        obs_property_list_add_string(
            list, obs_module_text("OBSTitles.StingerNoDocuments"), "");
    }
    obs_property_set_modified_callback(list, stinger_properties_modified);

    obs_property_t *audio = obs_properties_add_list(
        properties, kAudioBehavior,
        obs_module_text("OBSTitles.StingerAudioBehavior"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(
        audio, obs_module_text("OBSTitles.StingerAudioOnly"),
        static_cast<int>(StingerAudioBehavior::StingerAudioOnly));
    obs_property_list_add_int(
        audio, obs_module_text("OBSTitles.StingerAudioCrossfade"),
        static_cast<int>(StingerAudioBehavior::CrossfadeSceneAudio));
    obs_property_list_add_int(
        audio, obs_module_text("OBSTitles.StingerAudioCut"),
        static_cast<int>(StingerAudioBehavior::CutSceneAudio));
    obs_property_list_add_int(
        audio, obs_module_text("OBSTitles.StingerAudioCustom"),
        static_cast<int>(StingerAudioBehavior::CustomTransitionCurve));
    obs_property_set_modified_callback(audio, stinger_properties_modified);

    obs_property_t *follow = obs_properties_add_bool(
        properties, kAudioFollowVideoPoint,
        obs_module_text("OBSTitles.StingerAudioFollowVideoPoint"));
    obs_property_set_modified_callback(follow, stinger_properties_modified);

    obs_property_t *audio_point = obs_properties_add_float_slider(
        properties, kAudioTransitionPointPercent,
        obs_module_text("OBSTitles.StingerAudioTransitionPoint"),
        0.0, 100.0, 0.1);
    obs_property_float_set_suffix(audio_point, "%");

    obs_properties_add_float_slider(
        properties, kCustomCurveX1,
        obs_module_text("OBSTitles.StingerCurveX1"), 0.0, 1.0, 0.01);
    obs_properties_add_float_slider(
        properties, kCustomCurveY1,
        obs_module_text("OBSTitles.StingerCurveY1"), 0.0, 1.0, 0.01);
    obs_properties_add_float_slider(
        properties, kCustomCurveX2,
        obs_module_text("OBSTitles.StingerCurveX2"), 0.0, 1.0, 0.01);
    obs_properties_add_float_slider(
        properties, kCustomCurveY2,
        obs_module_text("OBSTitles.StingerCurveY2"), 0.0, 1.0, 0.01);

    obs_property_t *fallback = obs_properties_add_list(
        properties, kProxyFallback,
        obs_module_text("OBSTitles.StingerProxyFallback"),
        OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(
        fallback, obs_module_text("OBSTitles.StingerProxySafeLive"),
        static_cast<int>(StingerProxyFallback::SafeLiveRender));
    obs_property_list_add_int(
        fallback, obs_module_text("OBSTitles.StingerProxyRequire"),
        static_cast<int>(StingerProxyFallback::RequireValidProxy));
    obs_property_set_modified_callback(fallback, stinger_properties_modified);

    obs_property_t *validation = obs_properties_add_text(
        properties, kValidationStatus,
        obs_module_text("OBSTitles.StingerValidation"), OBS_TEXT_INFO);
    obs_property_text_set_info_type(validation, OBS_TEXT_INFO_NORMAL);
    obs_property_text_set_info_word_wrap(validation, true);

    obs_property_t *preview = obs_properties_add_text(
        properties, kPreviewHint, "", OBS_TEXT_INFO);
    obs_property_text_set_info_type(preview, OBS_TEXT_INFO_NORMAL);
    obs_property_text_set_info_word_wrap(preview, true);

    return properties;
}

static void start_child(FxmStingerTransition *data, int64_t desired_time_ms)
{
    if (!data || !data->graphic_source || data->child_started ||
        !data->runtime_graphic_allowed)
        return;
    obs_source_media_restart(data->graphic_source);
    if (desired_time_ms > kMinimumResyncMs)
        obs_source_media_set_time(data->graphic_source, desired_time_ms);
    data->child_started = true;
    data->child_holding = false;
}

static void hold_child_last_frame(FxmStingerTransition *data)
{
    if (!data || !data->graphic_source || !data->child_started ||
        data->child_holding)
        return;
    const int64_t last_frame_time_ms = std::max<int64_t>(
        0, static_cast<int64_t>(
               std::llround(data->document_duration * 1000.0)) - 1);
    obs_source_media_set_time(data->graphic_source, last_frame_time_ms);
    obs_source_media_play_pause(data->graphic_source, true);
    data->child_holding = true;
}

static void update_scene_switch_latch(FxmStingerTransition *data, float t)
{
    if (!data || data->scene_switched ||
        static_cast<double>(t) + kPointEpsilon < data->transition_point)
        return;
    data->scene_switched = true;
    blog(LOG_DEBUG,
         "[Flux Motion] Stinger run %llu switched Scene A -> Scene B at progress %.6f (configured %.6f).",
         static_cast<unsigned long long>(data->transition_run),
         static_cast<double>(t),
         static_cast<double>(data->transition_point));
}

static void stinger_video_tick(void *private_data, float)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return;

    const uint64_t revision = TitleDataStore::instance().revision();
    if (!data->transitioning && revision != data->store_revision) {
        data->store_revision = revision;
        refresh_document(data, false);
    }
    if (!data->transitioning)
        return;

    const float progress = std::clamp(
        obs_transition_get_time(data->source), 0.0f, 1.0f);
    update_scene_switch_latch(data, progress);

    if (!data->graphic_source || !data->runtime_graphic_allowed)
        return;

    const double elapsed = static_cast<double>(progress) *
                           data->total_duration;
    if (elapsed + kPointEpsilon < data->pre_roll)
        return;

    const double document_time = std::clamp(
        elapsed - data->pre_roll, 0.0, data->document_duration);
    const int64_t desired_time_ms = static_cast<int64_t>(
        std::llround(document_time * 1000.0));
    if (!data->child_started)
        start_child(data, desired_time_ms);

    if (data->child_started && !data->child_holding &&
        document_time + kPointEpsilon < data->document_duration) {
        const int64_t actual_time_ms =
            obs_source_media_get_time(data->graphic_source);
        const int64_t frame_tolerance_ms = static_cast<int64_t>(std::max(
            static_cast<double>(kMinimumResyncMs),
            2000.0 / std::max(1.0,
                CacheManager::instance().effectiveFrameRate())));
        if (actual_time_ms >= 0 &&
            std::llabs(actual_time_ms - desired_time_ms) >
                frame_tolerance_ms) {
            obs_source_media_set_time(data->graphic_source,
                                      desired_time_ms);
        }
    }

    if (data->child_started &&
        elapsed + kPointEpsilon >=
            data->pre_roll + data->document_duration)
        hold_child_last_frame(data);
}

static void manual_transition_render_callback(void *private_data,
                                              gs_texture_t *scene_a,
                                              gs_texture_t *scene_b,
                                              float progress,
                                              uint32_t width,
                                              uint32_t height)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data || !scene_a || !scene_b || !data->manual_render_session ||
        !data->runtime_graphic_allowed)
        return;
    auto title = selected_stinger(data->title_id);
    if (!title || title->stinger_switch_mode !=
                      StingerSwitchMode::ManualSceneAnimation)
        return;
    const double elapsed = std::clamp(static_cast<double>(progress), 0.0, 1.0) *
                           data->total_duration;
    const double document_time = std::clamp(
        elapsed - data->pre_roll, 0.0, data->document_duration);
    struct obs_video_info video_info = {};
    const double sample_duration =
        obs_get_video_info(&video_info) && video_info.fps_num > 0 &&
            video_info.fps_den > 0
        ? static_cast<double>(video_info.fps_den) /
              static_cast<double>(video_info.fps_num)
        : 1.0 / 60.0;
    title_gpu_render_session_set_render_request(
        data->manual_render_session,
        TitleGpuRenderRequest{width, height, sample_duration});
    title_gpu_render_session_update(
        data->manual_render_session, *title, document_time,
        TitleDataStore::instance().revision(), false);

    if (!data->alpha_output) {
        gs_effect_t *solid_effect = obs_get_base_effect(OBS_EFFECT_SOLID);
        gs_eparam_t *color_param =
            gs_effect_get_param_by_name(solid_effect, "color");
        struct vec4 opaque_black;
        vec4_set(&opaque_black, 0.0f, 0.0f, 0.0f, 1.0f);
        gs_effect_set_vec4(color_param, &opaque_black);
        while (gs_effect_loop(solid_effect, "Solid"))
            gs_draw_sprite(nullptr, 0, width, height);
    }

    title_gpu_render_session_draw_transition_inputs(
        data->manual_render_session, scene_a, scene_b,
        width, height, width, height);
}

static void stinger_video_render(void *private_data, gs_effect_t *)
{
    fxm::perf::add(fxm::perf::Counter::StingerVideoFrames);
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return;

    const float progress = std::clamp(
        obs_transition_get_time(data->source), 0.0f, 1.0f);
    update_scene_switch_latch(data, progress);
    if (data->switch_mode == StingerSwitchMode::ManualSceneAnimation) {
        if (data->runtime_graphic_allowed && data->manual_render_session) {
            obs_transition_video_render(data->source,
                                        manual_transition_render_callback);
        } else {
            const enum obs_transition_target fallback_target = data->scene_switched
                ? OBS_TRANSITION_SOURCE_B : OBS_TRANSITION_SOURCE_A;
            obs_transition_video_render_direct(data->source, fallback_target);
        }
        return;
    }
    const enum obs_transition_target target = data->scene_switched
        ? OBS_TRANSITION_SOURCE_B
        : OBS_TRANSITION_SOURCE_A;

    /* This is a real libobs transition. Scene A/B are rendered by the native
     * transition API; the FXM document is then composited over that result.
     * No scene-source overlay or frontend scene mutation is used. */
    if (!obs_transition_video_render_direct(data->source, target))
        return;
    if (!data->runtime_graphic_allowed || !data->graphic_source ||
        !data->child_started)
        return;

    const uint32_t source_width = obs_source_get_width(data->source);
    const uint32_t source_height = obs_source_get_height(data->source);
    const uint32_t graphic_width =
        obs_source_get_width(data->graphic_source);
    const uint32_t graphic_height =
        obs_source_get_height(data->graphic_source);
    if (!source_width || !source_height || !graphic_width || !graphic_height)
        return;

    const bool previous = gs_set_linear_srgb(true);
    if (!data->alpha_output) {
        gs_effect_t *solid_effect = obs_get_base_effect(OBS_EFFECT_SOLID);
        gs_eparam_t *color_param =
            gs_effect_get_param_by_name(solid_effect, "color");
        struct vec4 opaque_black;
        vec4_set(&opaque_black, 0.0f, 0.0f, 0.0f, 1.0f);
        gs_effect_set_vec4(color_param, &opaque_black);
        while (gs_effect_loop(solid_effect, "Solid"))
            gs_draw_sprite(nullptr, 0, source_width, source_height);
    }

    gs_matrix_push();
    gs_matrix_scale3f(
        static_cast<float>(source_width) / graphic_width,
        static_cast<float>(source_height) / graphic_height, 1.0f);
    obs_source_video_render(data->graphic_source);
    gs_matrix_pop();
    gs_set_linear_srgb(previous);
}

static double cubic_bezier_coordinate(double u, double p1, double p2)
{
    const double one_minus = 1.0 - u;
    return 3.0 * one_minus * one_minus * u * p1 +
           3.0 * one_minus * u * u * p2 + u * u * u;
}

static double cubic_bezier_derivative(double u, double p1, double p2)
{
    const double one_minus = 1.0 - u;
    return 3.0 * one_minus * one_minus * p1 +
           6.0 * one_minus * u * (p2 - p1) +
           3.0 * u * u * (1.0 - p2);
}

static float custom_curve_progress(const FxmStingerTransition *data, float t)
{
    const double x = std::clamp(static_cast<double>(t), 0.0, 1.0);
    double u = x;
    for (int i = 0; i < 8; ++i) {
        const double estimate = cubic_bezier_coordinate(
            u, data->custom_curve_x1, data->custom_curve_x2) - x;
        const double derivative = cubic_bezier_derivative(
            u, data->custom_curve_x1, data->custom_curve_x2);
        if (std::abs(estimate) < 1e-6 || std::abs(derivative) < 1e-7)
            break;
        u = std::clamp(u - estimate / derivative, 0.0, 1.0);
    }
    return static_cast<float>(std::clamp(
        cubic_bezier_coordinate(
            u, data->custom_curve_y1, data->custom_curve_y2),
        0.0, 1.0));
}

static float custom_scene_audio_blend(
    const FxmStingerTransition *data, float t)
{
    const float point = std::clamp(
        data->audio_transition_point, 0.001f, 0.999f);
    const float point_aligned_t = t <= point
        ? 0.5f * (t / point)
        : 0.5f + 0.5f * ((t - point) / (1.0f - point));
    return custom_curve_progress(data, point_aligned_t);
}

static float scene_mix_a(void *private_data, float t)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    switch (data->audio_behavior) {
    case StingerAudioBehavior::StingerAudioOnly:
        return 0.0f;
    case StingerAudioBehavior::CrossfadeSceneAudio:
        return 1.0f - t;
    case StingerAudioBehavior::CutSceneAudio:
        return t + static_cast<float>(kPointEpsilon) <
                       data->audio_transition_point
            ? 1.0f
            : 0.0f;
    case StingerAudioBehavior::CustomTransitionCurve:
        return 1.0f - custom_scene_audio_blend(data, t);
    }
    return 0.0f;
}

static float scene_mix_b(void *private_data, float t)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    switch (data->audio_behavior) {
    case StingerAudioBehavior::StingerAudioOnly:
        return 0.0f;
    case StingerAudioBehavior::CrossfadeSceneAudio:
        return t;
    case StingerAudioBehavior::CutSceneAudio:
        return t + static_cast<float>(kPointEpsilon) >=
                       data->audio_transition_point
            ? 1.0f
            : 0.0f;
    case StingerAudioBehavior::CustomTransitionCurve:
        return custom_scene_audio_blend(data, t);
    }
    return 0.0f;
}

static bool stinger_audio_render(void *private_data, uint64_t *timestamp_out,
                                 struct obs_source_audio_mix *audio,
                                 uint32_t mixers, size_t channels,
                                 size_t sample_rate)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return false;

    uint64_t child_timestamp = 0;
    if (data->runtime_graphic_allowed && data->audio_enabled &&
        data->graphic_source && data->child_started &&
        !obs_source_audio_pending(data->graphic_source)) {
        child_timestamp =
            obs_source_get_audio_timestamp(data->graphic_source);
    }

    const bool success = obs_transition_audio_render(
        data->source, timestamp_out, audio, mixers, channels, sample_rate,
        scene_mix_a, scene_mix_b);
    if (!child_timestamp || !data->runtime_graphic_allowed ||
        !data->audio_enabled || !data->graphic_source)
        return success;

    if (!*timestamp_out || child_timestamp < *timestamp_out)
        *timestamp_out = child_timestamp;

    struct obs_source_audio_mix child_audio = {};
    obs_source_get_audio_mix(data->graphic_source, &child_audio);
    for (size_t mix = 0; mix < MAX_AUDIO_MIXES; ++mix) {
        if ((mixers & (1u << mix)) == 0)
            continue;
        for (size_t channel = 0; channel < channels; ++channel) {
            float *out = audio->output[mix].data[channel];
            float *in = child_audio.output[mix].data[channel];
            float *end = in + AUDIO_OUTPUT_FRAMES;
            while (in < end)
                *(out++) += *(in++);
        }
    }
    return true;
}

static void stinger_transition_start(void *private_data)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return;

    data->store_revision = TitleDataStore::instance().revision();
    refresh_document(data, false);
    data->transitioning = true;
    data->scene_switched = false;
    data->child_started = false;
    data->child_holding = false;
    ++data->transition_run;

    if (data->runtime_graphic_allowed && data->graphic_source &&
        !data->child_active) {
        obs_source_add_active_child(data->source, data->graphic_source);
        data->child_active = true;
    }
    if (data->graphic_source) {
        obs_source_set_muted(data->graphic_source, !data->audio_enabled);
        obs_source_media_stop(data->graphic_source);
        obs_source_media_set_time(data->graphic_source, 0);
    }
    if (data->runtime_graphic_allowed && data->pre_roll <= kPointEpsilon)
        start_child(data, 0);

    blog(LOG_DEBUG,
         "[Flux Motion] Stinger run %llu started: duration=%.3fs switch=%.6f audioPoint=%.6f proxyReady=%s overlay=%s.",
         static_cast<unsigned long long>(data->transition_run),
         data->total_duration, static_cast<double>(data->transition_point),
         static_cast<double>(data->audio_transition_point),
         data->proxy_ready ? "yes" : "no",
         data->runtime_graphic_allowed ? "enabled" : "blocked");
}

static void stinger_transition_stop(void *private_data)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (!data)
        return;
    if (data->graphic_source) {
        obs_source_media_stop(data->graphic_source);
        obs_source_media_set_time(data->graphic_source, 0);
    }
    remove_active_child(data);
    data->transitioning = false;
    /* Keep the destination target latched between runs. OBS can render the
     * transition source once more after transition_stop; the next
     * transition_start resets the latch before progress begins. */
    data->scene_switched = true;
    data->child_started = false;
    data->child_holding = false;
    if (data->manual_render_session)
        title_gpu_render_session_invalidate_presentation(
            data->manual_render_session, false);
}

static void stinger_enum_active_sources(void *private_data,
                                        obs_source_enum_proc_t callback,
                                        void *param)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (data && data->graphic_source && data->child_active)
        callback(data->source, data->graphic_source, param);
}

static void stinger_enum_all_sources(void *private_data,
                                     obs_source_enum_proc_t callback,
                                     void *param)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    if (data && data->graphic_source)
        callback(data->source, data->graphic_source, param);
}

static enum gs_color_space stinger_get_color_space(
    void *private_data, size_t, const enum gs_color_space *)
{
    auto *data = static_cast<FxmStingerTransition *>(private_data);
    return data ? obs_transition_video_get_color_space(data->source)
                : GS_CS_SRGB;
}

} // namespace

void stinger_transition_register()
{
    static obs_source_info info = {};
    info.id = kTransitionSourceId;
    info.type = OBS_SOURCE_TYPE_TRANSITION;
    info.get_name = stinger_get_name;
    info.create = stinger_create;
    info.destroy = stinger_destroy;
    info.update = stinger_update;
    info.get_defaults = stinger_defaults;
    info.get_properties = stinger_properties;
    info.video_tick = stinger_video_tick;
    info.video_render = stinger_video_render;
    info.audio_render = stinger_audio_render;
    info.enum_active_sources = stinger_enum_active_sources;
    info.enum_all_sources = stinger_enum_all_sources;
    info.transition_start = stinger_transition_start;
    info.transition_stop = stinger_transition_stop;
    info.video_get_color_space = stinger_get_color_space;
    obs_register_source(&info);
    blog(LOG_INFO,
         "[Flux Motion] Native FXM Stinger transition registered.");
}
