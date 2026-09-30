#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "serialization-passthrough.h"
#include "external-data-types.h"
#include "layer-effects.h"
#include "animation.h"
#include "title-rich-text.h"
#include "text-animator.h"
#include "layer-transition.h"

/* Development Version 300: lights are ordinary non-raster layers.
 * Their authored transform, parenting, ordering, visibility and timeline
 * state therefore use the same layer graph as every other scene object. */
enum class TitleLightType : int { Ambient = 0, Point = 1, Spot = 2, Parallel = 3, Environment = 4 };
enum class TitleLightFalloff : int { None = 0, Linear = 1, InverseSquare = 2 };

struct TitleLight {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;
    std::string name = "Light";
    bool enabled = true;
    TitleLightType type = TitleLightType::Parallel;
    AnimatedVec3Property position { "light_position", {0.0, 0.0, -1000.0} };
    AnimatedVec3Property target { "light_target", {0.0, 0.0, 0.0} };
    AnimatedProperty color_a { "light_color_a", 255.0 };
    AnimatedProperty color_r { "light_color_r", 255.0 };
    AnimatedProperty color_g { "light_color_g", 255.0 };
    AnimatedProperty color_b { "light_color_b", 255.0 };
    AnimatedProperty intensity { "light_intensity", 100.0 };
    /* Radius/extent of the emitter in scene units. A zero-sized source keeps
     * the historical punctual-light result; larger emitters soften the
     * diffuse/specular response and any supported shadow map. */
    AnimatedProperty source_size { "light_source_size", 15.0 };
    TitleLightFalloff falloff = TitleLightFalloff::InverseSquare;
    /* Serialized legacy property retained for loading older titles. Runtime
     * falloff start is now derived from source_size. */
    AnimatedProperty falloff_start { "light_falloff_start", 15.0 };
    AnimatedProperty falloff_distance { "light_falloff_distance", 450.0 };
    AnimatedProperty cone_angle { "light_cone_angle", 45.0 };
    AnimatedProperty cone_feather { "light_cone_feather", 25.0 };
    bool casts_shadows = false;
    AnimatedProperty shadow_darkness { "light_shadow_darkness", 50.0 };
    AnimatedProperty shadow_softness { "light_shadow_softness", 2.0 };
    AnimatedProperty shadow_bias { "light_shadow_bias", 0.002 };
    std::string environment_path;
    AnimatedProperty environment_rotation { "light_environment_rotation", 0.0 };
    bool environment_visible = false;
    bool timeline_expanded = false;
};
inline Vec3Value evaluated_light_position(const TitleLight &l,double t){return l.position.evaluate(t);}
inline Vec3Value evaluated_light_target(const TitleLight &l,double t){return l.target.evaluate(t);}
inline uint32_t evaluated_light_color(const TitleLight &l,double t){auto c=[t](const AnimatedProperty&p){return (uint32_t)std::clamp((int)std::lround(p.evaluate(t)),0,255);};return(c(l.color_a)<<24)|(c(l.color_r)<<16)|(c(l.color_g)<<8)|c(l.color_b);}
inline bool title_light_has_authored_keyframes(const TitleLight &l){return l.position.is_animated()||l.target.is_animated()||l.color_a.is_animated()||l.color_r.is_animated()||l.color_g.is_animated()||l.color_b.is_animated()||l.intensity.is_animated()||l.source_size.is_animated()||l.falloff_distance.is_animated()||l.cone_angle.is_animated()||l.cone_feather.is_animated()||l.shadow_darkness.is_animated()||l.shadow_softness.is_animated()||l.shadow_bias.is_animated()||l.environment_rotation.is_animated();}

/* ══════════════════════════════════════════════════════════════════
 *  Layer type
 * ══════════════════════════════════════════════════════════════════ */
enum class LayerType {
    Text = 0,
    SolidRect = 1,
    Image = 2,
    Shape = 3,      /* vector primitives */
    Clock = 4,
    Ticker = 5,
    Adjustment = 6,
    ColorSolid = 7,
    Group = 8,
    Asset = 9,       /* reusable nested title composition */
    Audio = 10,       /* timeline audio clip; no visual transform */
    TransitionInput = 11, /* runtime Scene A/B texture supplied by OBS transitions */
    Video = 12,           /* decoded video picture with linked audio-stream child tracks */
    Light = 13,           /* non-raster 3D light layer */
    Empty = 14,           /* non-raster transform parent / spatial control */
    Chart = 15,           /* provider-backed, resolution-independent chart */
};

inline bool layer_type_is_asset(LayerType type)
{
    return type == LayerType::Asset;
}

inline bool layer_type_is_container(LayerType type)
{
    return type == LayerType::Group || type == LayerType::Asset;
}

/* Video is a visual layer, not an off-screen compositing group. It may still
 * own synchronized audio-stream child rows in the editor hierarchy. */
inline bool layer_type_can_have_children(LayerType type)
{
    return layer_type_is_container(type) || type == LayerType::Video;
}

inline bool layer_type_is_light(LayerType type) { return type == LayerType::Light; }

inline bool layer_type_is_audio(LayerType type)
{
    return type == LayerType::Audio;
}

inline bool layer_type_is_video(LayerType type)
{
    return type == LayerType::Video;
}

inline bool layer_type_is_image_like(LayerType type)
{
    return type == LayerType::Image || type == LayerType::Video;
}

inline bool layer_type_can_be_scene_mask(LayerType type)
{
    /* Scene masks are authored from any visual layer. Keep only non-visual
     * timeline/control rows out of the contract; text, clocks, tickers,
     * images, videos, groups/assets and transition placeholders can all
     * provide an alpha silhouette. */
    return type != LayerType::Audio && type != LayerType::Adjustment &&
           type != LayerType::Light && type != LayerType::Empty;
}

inline bool layer_type_is_transition_input(LayerType type)
{
    return type == LayerType::TransitionInput;
}

/* Scene A/B transition inputs use the same editable rectangular geometry as
 * shape layers. Their pixels come from OBS rather than a fill material, but
 * resize, animated Size, origin, transforms, masks and effects all follow the
 * shape-box contract. */
inline constexpr bool layer_type_uses_shape_geometry(LayerType type)
{
    return type == LayerType::Shape ||
           type == LayerType::SolidRect ||
           type == LayerType::TransitionInput;
}

inline constexpr bool layer_type_is_authored_shape(LayerType type)
{
    return type == LayerType::Shape || type == LayerType::SolidRect;
}


/* 3D transform contract.
 * Coordinate system: +X right, +Y down, +Z away from the active camera.
 * Existing 2D layers remain on Z=0 and keep the exact legacy transform path.
 * A 2D child of a 3D parent inherits the complete parent matrix while its own
 * local Z/XY tilt stay zero, which makes mixed hierarchies deterministic. */
enum class LayerDimensionMode {
    TwoD = 0,
    ThreeD = 1,
};

/* Public XYZ value used by the authoring and renderer facades. The serialized
 * representation intentionally remains the legacy XY animated property plus
 * its Z channel, so old projects require no schema rewrite while 3D-aware code
 * consumes one coherent Vector3 value. */
struct LayerVector3Value {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class TransformAxisSpace {
    Local = 0,
    Parent = 1,
    World = 2,
};

enum class LayerDepthMode {
    Automatic = 0,
    LayerOrder = 1,
};

enum class AudioPlaybackMode {
    PlayOnce = 0,
    Loop = 1,
    PauseAtOut = 2,
};

enum class VideoFrameInterpolationMode {
    NearestFrame = 0,
    FrameBlend = 1,
    MotionCompensated = 2,
};

enum class VideoTimeRemapAudioMode {
    PreserveLinearClipAudio = 0,
    FollowSourceTime = 1,
    MuteReverseOrFreeze = 2,
};

struct VideoTimeRemapLoopSegment {
    double timeline_start = 0.0;
    double timeline_end = 0.0;
    double source_start = 0.0;
    double source_end = 0.0;
    bool enabled = true;
};


enum class AudioFadeCurve {
    Linear = 0,
    Smooth = 1,
    EqualPower = 2,
};

enum class AudioEffectType {
    Gain = 0,
    Fade = 1,
    HighPass = 2,
    LowPass = 3,
    CompressorLimiter = 4,
};

struct AudioEffect {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    AudioEffectType type = AudioEffectType::Gain;
    bool enabled = true;
    float gain_db = 0.0f;
    float frequency_hz = 120.0f;
    float threshold_db = -6.0f;
    float ratio = 4.0f;
    float attack_ms = 5.0f;
    float release_ms = 80.0f;
    float makeup_db = 0.0f;
    double fade_in = 0.0;
    double fade_out = 0.0;
    AudioFadeCurve fade_curve = AudioFadeCurve::Linear;
};


enum class ShapeType {
    Rectangle,
    RoundedRectangle,
    Ellipse,
    Triangle,
    Star,
    Polygon,
    Diamond,
    Line,
    Path,      /* arbitrary cubic Bézier path created by the Pen tool */
};

enum class CornerType {
    Round,
    Straight,
    Concave,
    Cutout,
};

enum class MaskMode {
    None,
    Alpha,
    InvertedAlpha,
    Luma,
    InvertedLuma,
    Clipping,
    InvertedClipping,
};

inline bool mask_mode_is_clipping(MaskMode mode)
{
    return mode == MaskMode::Clipping ||
           mode == MaskMode::InvertedClipping;
}

/* Visibility contract for a layer while it is referenced as a track matte.
 * HiddenInactive preserves the old disabled state, MatteOnly preserves the
 * old active-but-not-composited state, and VisibleAndMatte is the new state
 * that composites the artwork while it remains active as a matte. */
enum class MatteVisibilityMode {
    HiddenInactive = 0,
    MatteOnly = 1,
    VisibleAndMatte = 2,
};

enum class ImageScaleFilter {
    Disable,
    Bilinear,
    Bicubic,
    Lanczos,
    Area,
};

enum class ImageBoxMode {
    FitImageToBox = 0,
    FillHorizontal = 1,
    FillVertical = 2,
    LegacyFitHorizontalCrop = 3,
    LegacyFitVerticalCrop = 4,
    StretchToFill = 5,
    FitToLongSide = 6,
    FitToShortSide = 7,
};


struct BezierPathPoint {
    /* Coordinates are normalized to the layer editable box. Handle positions
     * are absolute (not offsets) in the same normalized coordinate space. */
    double x = 0.0;
    double y = 0.0;
    double in_x = 0.0;
    double in_y = 0.0;
    double out_x = 0.0;
    double out_y = 0.0;
    bool has_in = false;
    bool has_out = false;
    bool smooth = false;
    /* True on the first anchor of every additional contour in a compound
     * path. The first point is always treated as a contour start for backward
     * compatibility, even when this flag is false. */
    bool starts_subpath = false;
    /* Illustrator-style live-corner radius in layer-local pixels. */
    double corner_radius = 0.0;
};

/* Chart layers consume ExternalDataTableSnapshot through a provider-neutral
 * source/table reference. Provider configuration remains owned by Title's
 * external_data_sources collection; this mapping is intentionally only the
 * adapter between a table schema and chart semantics. */
enum class ChartType {
    Bar = 0,
    HorizontalBar = 1,
    Line = 2,
    Area = 3,
    Pie = 4,
    Donut = 5,
    Scatter = 6,
};

enum class ChartDataMode {
    StaticData = 0,
    ExternalData = 1,
};

struct ChartDataBinding {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string source_id;
    std::string table_path;
    std::string category_field = "name";
    std::string series_field;
    std::string secondary_value_field;
    std::string metadata_field;
    std::string color_field;
    int start_row = 0;
    int maximum_rows = 0; /* zero means all rows */
    bool ignore_empty_rows = true;
};

struct ChartSeries {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;
    std::string name = "Value";
    std::string value_field = "value";
    std::string secondary_value_field;
    std::string metadata_field;
    uint32_t color = 0xFF4C8DFFu;
    uint32_t fill_color = 0x804C8DFFu;
    bool visible = true;
    bool fill_enabled = true;
    bool outline_enabled = true;
    double line_thickness = -1.0; /* negative inherits the layer style */
};

/* Authored values live on the layer when no provider is required. Individual
 * values can be exposed as one compact, multi-line cue field in the OBS dock;
 * provider-backed charts continue to use ChartDataBinding unchanged. */
struct ChartValue {
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;
    std::string category;
    std::string series_id = "value";
    double value = 0.0;
    double secondary_value = 0.0;
    bool has_secondary_value = false;
    std::string metadata;
    bool color_override = false;
    uint32_t color = 0xFF4C8DFFu;
    bool expose_in_dock = false;
};

/* ══════════════════════════════════════════════════════════════════
 *  Layer
 * ══════════════════════════════════════════════════════════════════ */
struct Layer {
    /* Opaque source JSON used to preserve fields introduced by newer builds. */
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;          /* UUID */
    std::string name;
    LayerType   type = LayerType::Text;
    TitleLight  light; /* authoritative when type == LayerType::Light */
    bool        visible  = true;
    bool        locked   = false;
    bool        properties_expanded = false;
    /* Group rows can collapse their descendants in the layer/timeline UI.
     * This is presentation state only; it never affects rendering. */
    bool        group_collapsed = false;
    /* Optional per-layer editor color. It replaces the type-derived timeline
     * color in layer rows, timeline strips and canvas authoring overlays only;
     * it never changes the rendered graphic. */
    bool        custom_ui_color_enabled = false;
    uint32_t    custom_ui_color = 0xFF4C6EF5u;
    /* Runtime scene slot for manual Stinger composition: 0=Scene A, 1=Scene B.
     * Transition-input layers otherwise follow the ordinary visual-layer
     * contract. Only the two required document inputs are non-deletable;
     * duplicates/copies remain ordinary deletable layers that reference the
     * same runtime scene slot. */
    int         transition_input_slot = -1;
    bool        transition_input_required = false;
    /* Container hierarchy. Group and Asset layers may be referenced here. */
    std::string parent_id;
    /* Independent transform parenting. This never changes group membership,
     * layer ordering, or the Layers/Timeline hierarchy. */
    std::string transform_parent_id;

    /* Development Version 216: a non-animated bind matrix between the
     * effective parent basis and the authored local transform. Reparenting
     * updates this matrix at the current playhead instead of baking sampled
     * values into every transform channel. Authored keyframes, easing and
     * spatial tangents therefore remain byte-for-byte intact. The matrix is
     * row-major and follows QMatrix4x4 column-vector semantics. */
    bool parent_bind_enabled = false;
    std::array<double, 16> parent_bind_matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };

    /* Asset instance metadata. Asset layers are reusable title containers.
     * Their cloned descendants carry asset_owner_id and asset_source_layer_id
     * and remain hidden from the ordinary layer/timeline hierarchy. */
    std::string asset_title_id;
    std::string asset_owner_id;
    std::string asset_source_layer_id;
    std::string asset_category;
    bool        asset_animated = false;
    int         asset_playback_mode = 0; /* 0=synchronized, 1=independent */
    double      asset_playback_offset = 0.0;
    double      asset_duration = 5.0;
    /* Snapshot of the source title playback contract. These fields belong to
     * the Asset Layer instance so it remains self-contained if the library
     * asset is later renamed, edited or removed. */
    int         asset_source_playback_mode = 0; /* 0=play once, 1=loop, 2=pause */
    int         asset_source_loop_type = 0;     /* 0=restart, 1=ping-pong */
    double      asset_source_loop_start = 1.0;
    double      asset_source_loop_end = 4.0;
    double      asset_source_pause_time = 0.0;
    double      asset_pause_duration = 1.0;
    int         asset_loop_count = 1;
    bool        asset_loop = false; /* repeat the complete independent pass */
    /* Development Version 340: a 3D asset is a precomposition boundary.
     * Descendants are projected with the camera/canvas snapshot authored in
     * the source asset, then the resulting plane is placed by this Asset
     * Layer.  The opt-in flag keeps titles saved by older versions on their
     * exact legacy shared-title-space path. */
    bool        asset_isolated_3d_space = false;
    int         asset_space_width = 0;
    int         asset_space_height = 0;
    double      asset_space_center_x = 0.0;
    double      asset_space_center_y = 0.0;
    /* Internal descendants that mirror the source title's Active Camera track
     * evaluate that discrete track on the owner timeline, not layer-local
     * in/out time. Explicit per-layer camera assignments retain legacy local
     * timing. */
    bool        asset_camera_uses_owner_time = false;
    std::string mask_source_id;
    MaskMode    mask_mode = MaskMode::None;
    MatteVisibilityMode matte_visibility_mode = MatteVisibilityMode::MatteOnly;
    EffectBlendMode blend_mode = EffectBlendMode::Normal; /* AE-style layer mode */
    bool        use_as_scene_mask = false;
    bool        effect_stack_respects_masks = false; /* When true, stackable effects are applied after the layer track matte/mask. */
    std::vector<LayerEffect> effects;
    /* Premiere-style intro/outro transitions. At most one transition is kept
     * for each edge; presets replace the existing transition on that edge. */
    std::vector<LayerTransition> transitions;

    /* Optional provider-neutral bindings. Authored property values remain in
     * the ordinary Layer fields; live/fallback values are resolved transiently. */
    std::vector<ExternalPropertyBinding> external_bindings;
    /* Runtime-only binding override used by the active Live Text Cue row. It is
     * never serialized and takes precedence over the authored layer binding. */
    std::vector<ExternalPropertyBinding> runtime_external_bindings;

    /* ----- Chart-specific ----- */
    ChartType chart_type = ChartType::Bar;
    ChartDataMode chart_data_mode = ChartDataMode::ExternalData;
    OpaqueSerializationPassthrough chart_serialization_passthrough_json;
    ChartDataBinding chart_binding;
    std::vector<ChartSeries> chart_series;
    std::vector<ChartValue> chart_values;
    bool chart_background_enabled = false;
    bool chart_grid_enabled = true;
    bool chart_axes_enabled = true;
    bool chart_axis_labels_enabled = true;
    bool chart_data_labels_enabled = false;
    bool chart_legend_enabled = false;
    bool chart_fill_enabled = true;
    bool chart_outline_enabled = true;
    uint32_t chart_background_color = 0x00000000u;
    uint32_t chart_grid_color = 0x40FFFFFFu;
    uint32_t chart_axis_color = 0xBFFFFFFFu;
    uint32_t chart_label_color = 0xFFFFFFFFu;
    std::string chart_font_family = "Helvetica Neue";
    std::string chart_number_prefix;
    std::string chart_number_suffix;
    bool chart_thousands_separator = false;
    int chart_decimal_precision = 0;
    double chart_data_transition_seconds = 0.35;
    AnimatedProperty chart_animation_progress { "chart_animation_progress", 1.0 };
    AnimatedProperty chart_reveal_progress { "chart_reveal_progress", 1.0 };
    AnimatedProperty chart_value_transition { "chart_value_transition", 1.0 };
    AnimatedProperty chart_label_opacity { "chart_label_opacity", 1.0 };
    AnimatedProperty chart_element_opacity { "chart_element_opacity", 1.0 };
    AnimatedProperty chart_font_size { "chart_font_size", 24.0 };
    AnimatedProperty chart_line_thickness { "chart_line_thickness", 3.0 };
    AnimatedProperty chart_bar_width { "chart_bar_width", 0.72 };
    AnimatedProperty chart_bar_spacing { "chart_bar_spacing", 0.18 };
    AnimatedProperty chart_corner_radius { "chart_corner_radius", 4.0 };
    AnimatedProperty chart_padding { "chart_padding", 18.0 };
    AnimatedProperty chart_margin_left { "chart_margin_left", 72.0 };
    AnimatedProperty chart_margin_top { "chart_margin_top", 28.0 };
    AnimatedProperty chart_margin_right { "chart_margin_right", 28.0 };
    AnimatedProperty chart_margin_bottom { "chart_margin_bottom", 58.0 };

    /* Timeline in/out (seconds) within parent title clip */
    double      in_time  = 0.0;
    double      out_time = 5.0;

    /* ----- Audio-specific -----
     * Audio layers are timeline-only media clips. The generic in_time/out_time
     * fields are their clip position and duration in the title timeline. */
    std::string audio_source;
    int         audio_stream_index = -1; /* -1 = standalone audio/default stream */
    double      audio_in_point = 0.0;
    double      audio_out_point = 0.0;   /* 0 = use media duration when known */
    float       audio_volume = 1.0f;     /* legacy/static linear gain, 0..4 */
    float       audio_pan = 0.0f;        /* legacy/static -1 left .. +1 right */
    AnimatedProperty audio_volume_prop{"audio_volume", 1.0};
    AnimatedProperty audio_pan_prop{"audio_pan", 0.0};
    bool        audio_muted = false;
    bool        audio_solo = false;
    double      audio_fade_in = 0.0;
    double      audio_fade_out = 0.0;
    AudioFadeCurve audio_fade_curve = AudioFadeCurve::Linear;
    std::vector<AudioEffect> audio_effects; /* independent from visual effects */
    bool        audio_loop = false;
    AudioPlaybackMode audio_playback_mode = AudioPlaybackMode::PlayOnce;
    bool        audio_independent = false;
    double      audio_media_duration = 0.0; /* cached metadata, safe if source missing */
    int         audio_sample_rate = 0;
    int         audio_channels = 0;
    std::vector<float> audio_waveform; /* normalized min/max peak pairs for the full decoded asset */
    double audio_waveform_duration = 0.0; /* decoded asset duration represented by audio_waveform */
    int audio_waveform_progress_percent = 0; /* runtime/UI progress while waveform generation is running */
    bool audio_waveform_generating = false;
    std::string audio_waveform_progress_label;
    /* Non-empty for audio tracks generated from a Video layer.  The stream
     * remains an ordinary editable/mutable Audio layer, but its clip timing
     * is slaved to the owner video so picture and sound cannot drift. */
    std::string linked_media_layer_id;
    bool linked_media_stream = false;
    std::string media_stream_label;

    /* ----- Video-specific -----
     * Video is a visual layer and therefore uses the complete Image box,
     * transform, mask and effect contract below.  Its picture and linked
     * audio tracks are evaluated from the same title clock. */
    std::string video_source;
    /* Development Version 244 serialization audit: keep the user-authored
     * reference and resolved absolute reference side-by-side. Neither form is
     * allowed to block project loading when the media is offline. */
    std::string video_source_relative;
    std::string video_source_absolute;
    std::string video_media_root;
    int         video_stream_index = -1;
    int         video_audio_stream_index = -1;
    std::string video_selected_streams_json = "{}";
    double      video_in_point = 0.0;
    double      video_out_point = 0.0;
    bool        video_loop = false;
    int         video_playback_mode = 0; /* 0 = synchronized, 1 = independent */
    double      video_media_duration = 0.0;
    double      video_frame_rate = 0.0;
    int         video_pixel_width = 0;
    int         video_pixel_height = 0;
    bool        video_has_alpha = false;
    bool        video_has_hdr = false;
    std::string video_color_primaries;
    std::string video_color_transfer;
    std::string video_color_matrix;
    std::string video_color_range;
    std::string video_decode_settings_json = "{}";
    bool        video_prefer_hardware_decode = true;
    bool        video_allow_hardware_fallback = true;
    std::string video_decode_cache_policy = "auto";

    /* Development Version 242: advisory Video proxy/cache identity. These
     * fields are intentionally source-media-only; transform, parenting, masks
     * and non-baked effects must never invalidate them. The runtime still
     * falls back to video_source whenever the proxy is missing, stale, paused,
     * cancelled or generated by an unsupported backend. */
    std::string video_source_fingerprint;
    std::string video_proxy_path;
    std::string video_proxy_fingerprint;
    std::string video_proxy_profile;
    bool        video_proxy_complete = false;
    bool        video_proxy_alpha = false;
    bool        video_proxy_hdr = false;
    bool        video_proxy_audio_preserved = false;
    int         video_proxy_progress_percent = 0;
    bool        video_proxy_generating = false;

    /* Development Version 243: authored video time remap.
     * video_source_time maps clip-local timeline seconds to source-media
     * seconds. Linear/Bezier/Hold keyframes produce speed ramps, reverse
     * sections and freeze sections without changing transform/effect cache
     * identity. Loop segments are evaluated in source-time space and are
     * invalidated only when media identity or the curve fingerprint changes. */
    bool        video_time_remap_enabled = false;
    AnimatedProperty video_source_time { "video_source_time", 0.0 };
    std::vector<VideoTimeRemapLoopSegment> video_time_remap_loop_segments;
    VideoTimeRemapAudioMode video_time_remap_audio_mode = VideoTimeRemapAudioMode::FollowSourceTime;
    VideoFrameInterpolationMode video_frame_interpolation = VideoFrameInterpolationMode::NearestFrame;
    bool        video_optical_flow_enabled = false;
    bool        video_optical_flow_analysis_running = false;
    int         video_optical_flow_progress_percent = 0;
    std::string video_time_remap_curve_fingerprint;
    std::string video_optical_flow_cache_fingerprint;

    /* ----- Animated properties ----- */
    AnimatedVectorProperty position { "position", {0.0, 0.0} };
    AnimatedVectorProperty scale    { "scale",    {1.0, 1.0, 1.0} };
    bool             scale_lock = true;
    AnimatedProperty rotation{ "rotation", 0.0 }; /* legacy/2D and 3D Z rotation */
    AnimatedProperty opacity { "opacity",  1.0 };

    /* ----- Optional planar 3D transform -----
     * The legacy XY properties above remain authoritative for 2D and are also
     * the X/Y components in 3D mode. This avoids migrations and guarantees
     * byte-for-byte 2D rendering for old projects. */
    LayerDimensionMode dimension_mode = LayerDimensionMode::TwoD;
    TransformAxisSpace transform_axis_space = TransformAxisSpace::Local;
    AnimatedProperty position_z { "position_z", 0.0 };
    /* Development Version 208: a unified XYZ position path. Legacy XY/Z
     * properties remain authoritative until this track is explicitly promoted,
     * preserving old 2D and 3D project output exactly. */
    AnimatedVec3Property position_3d { "position_3d", {0.0, 0.0, 0.0} };
    bool position_3d_path_enabled = false;
    AnimatedProperty rotation_x { "rotation_x", 0.0 };
    AnimatedProperty rotation_y { "rotation_y", 0.0 };
    AnimatedProperty scale_z { "scale_z", 1.0 };
    AnimatedProperty anchor_z { "anchor_z", 0.0 };
    AnimatedProperty orientation_x { "orientation_x", 0.0 };
    AnimatedProperty orientation_y { "orientation_y", 0.0 };
    AnimatedProperty orientation_z { "orientation_z", 0.0 };
    std::string camera_id; /* empty = active title camera */
    AnimatedDiscreteProperty camera_assignment { "camera_assignment", "" };
    LayerDepthMode depth_mode = LayerDepthMode::Automatic;
    bool depth_test = true;
    bool write_to_depth = true;
    bool double_sided = true;
    bool backface_culling = false;

    /* Development Version 298: physically based planar material controls.
     * Lighting is deliberately opt-in so every existing title preserves its
     * exact pre-298 appearance until Accepts Lights is enabled.  The scalar
     * values are AnimatedProperty instances from the start, avoiding a later
     * migration when material keyframes are exposed in the timeline. */
    bool material_accepts_lights = false;
    bool material_casts_shadows = true;
    bool material_accepts_shadows = true;
    bool material_appears_in_reflections = true;
    AnimatedProperty material_ambient { "material_ambient", 0.15 };
    AnimatedProperty material_diffuse { "material_diffuse", 1.0 };
    AnimatedProperty material_specular { "material_specular", 0.25 };
    AnimatedProperty material_shininess { "material_shininess", 32.0 };
    AnimatedProperty material_metallic { "material_metallic", 0.0 };
    AnimatedProperty material_roughness { "material_roughness", 0.5 };
    AnimatedProperty material_reflection_intensity { "material_reflection_intensity", 0.0 };
    /* Keep the packed value as a compatibility mirror for pre-304 titles and
     * external consumers.  The animated ARGB channels are authoritative. */
    uint32_t material_emissive_color = 0xFFFFFFFF;
    AnimatedProperty material_emissive_color_a { "material_emissive_color_a", 255.0 };
    AnimatedProperty material_emissive_color_r { "material_emissive_color_r", 255.0 };
    AnimatedProperty material_emissive_color_g { "material_emissive_color_g", 255.0 };
    AnimatedProperty material_emissive_color_b { "material_emissive_color_b", 255.0 };
    AnimatedProperty material_emissive_intensity { "material_emissive_intensity", 0.0 };

    /* Development Version 330: the Development 300-306 alpha-slice
     * extrusion/bevel experiment is retired from production rendering. Keep
     * these fields only so affected documents can be migrated losslessly to the
     * retained-mesh implementation planned for Development 331+. They are not
     * visual inputs, are not exposed in the editor and must not select a depth
     * or compatibility-raster path. */
    bool geometry_extrusion_enabled = false;
    AnimatedProperty geometry_extrusion_depth { "geometry_extrusion_depth", 0.0 };
    AnimatedProperty geometry_bevel_depth { "geometry_bevel_depth", 0.0 };
    int geometry_bevel_segments = 3;
    int geometry_extrusion_segments = 12;
    bool geometry_bevel_front = true;
    bool geometry_bevel_back = true;

    /* Illustrator-style Free Transform quad offsets, normalized to the local
     * layer box. Zero values preserve the ordinary affine transform. The four
     * corners are ordered TL, TR, BR, BL. */
    float transform_quad_tl_x = 0.0f;
    float transform_quad_tl_y = 0.0f;
    float transform_quad_tr_x = 0.0f;
    float transform_quad_tr_y = 0.0f;
    float transform_quad_br_x = 0.0f;
    float transform_quad_br_y = 0.0f;
    float transform_quad_bl_x = 0.0f;
    float transform_quad_bl_y = 0.0f;

    /* Animatable counterparts used by the Free Transform tool. Legacy float
     * fields remain as a compatibility mirror for older project files and
     * extension code. */
    AnimatedVectorProperty transform_quad_tl { "transform_quad_tl", {0.0, 0.0} };
    AnimatedVectorProperty transform_quad_tr { "transform_quad_tr", {0.0, 0.0} };
    AnimatedVectorProperty transform_quad_br { "transform_quad_br", {0.0, 0.0} };
    AnimatedVectorProperty transform_quad_bl { "transform_quad_bl", {0.0, 0.0} };

    /* ----- Text-specific ----- */
    std::string text_content  = "Title";
    RichTextDocument rich_text; /* Structured source-of-truth rich text document. */
    /* Unified per-cluster/word/line/run Text Animator stack. Legacy text
     * transitions are converted into this model on load and no longer execute
     * through a preset-specific runtime renderer. */
    TextAnimatorStack text_animators;
    std::string clock_format  = "H:i:s";  /* PHP date()-style format for clock layers */
    bool        expose_text    = false;
    bool        exposed_hide_if_empty = false;
    bool        exposed_single_value = false;
    bool        expose_fill_color = false;
    bool        exposed_fill_single_value = false;
    bool        expose_stroke_color = false;
    bool        exposed_stroke_single_value = false;
    bool        live_cue_hidden_if_empty = false; /* Runtime-only: hide exposed text/image layer for empty cue values. */
    bool        ignore_persistence = false; /* Let this layer continue animating during Background Persistence holds. Disabled for exposed dock text. */
    std::string font_family   = "Helvetica Neue";
    std::string font_style    = "Regular";
    int         font_size     = 72;
    AnimatedProperty font_size_prop { "font_size", 72.0 };
    bool        font_bold     = false;
    bool        font_italic   = false;
    bool        font_kerning  = true;
    int         kerning_mode  = 0;  /* 0=metrics, 1=optical, 2=manual */
    float       manual_kerning = 0.0f;
    float       text_leading  = 0.0f;
    float       char_tracking = 0.0f;
    AnimatedProperty char_tracking_prop { "char_tracking", 0.0 };
    float       char_scale_x  = 1.0f;
    AnimatedProperty char_scale_x_prop { "char_scale_x", 1.0 };
    float       char_scale_y  = 1.0f;
    AnimatedProperty char_scale_y_prop { "char_scale_y", 1.0 };
    float       baseline_shift = 0.0f;
    AnimatedProperty baseline_shift_prop { "baseline_shift", 0.0 };
    int         text_style    = 0;  /* 0=normal, 1=all caps, 2=small caps, 3=superscript, 4=subscript */
    bool        text_underline = false;
    bool        text_strikethrough = false;
    bool        text_ligatures = true;
    bool        text_stylistic_alternates = false;
    bool        text_fractions = false;
    bool        text_opentype_features = false;
    std::string text_language = "English";
    int         text_overflow_mode = 0;  /* 0=wrap, 1=clip, 2=horizontal fit */
    float       text_fit_min_scale = 0.5f;
    bool        text_box_width_to_text = false;
    bool        text_box_height_to_text = false;
    float       max_text_box_width = 1920.0f;
    float       max_text_box_height = 100.0f;
    /* While false, Max Text Box follows the authored Size control. */
    bool        max_text_box_width_overridden = false;
    bool        max_text_box_height_overridden = false;

    /* ----- Ticker-specific -----
     * style: 0=horizontal scrolling, 1=vertical line-by-line, 2=vertical smooth.
     * direction: horizontal 0=left-to-right, 1=right-to-left; vertical 0=top-to-bottom, 1=bottom-to-top.
     * speed is pixels/second. line_hold is seconds between line-by-line moves.
     */
    int         ticker_style = 0;
    double      ticker_speed = 120.0;
    double      ticker_line_hold = 2.0;
    int         ticker_direction = 1;
    /* 0=always play, 1=paused until title is cued, 2=paused until hotkey/UI resume, 3=custom completion control. */
    int         ticker_playback_mode = 0;
    double      ticker_completion = 0.0; /* 0..100 percent */
    AnimatedProperty ticker_completion_prop { "ticker_completion", 0.0 };

    uint32_t    text_color    = 0xFFFFFFFF;  /* ARGB */

    /* ----- Outline shared by text and solid/shape layers ----- */
    bool        outline_enabled = false;
    int         stroke_fill_type = 1;  /* 0=none, 1=color, 2=gradient */
    uint32_t    stroke_color  = 0xFF000000;
    float       stroke_width  = 0.0f;
    AnimatedProperty stroke_width_prop { "stroke_width", 0.0 };
    float       stroke_offset = 0.0f; /* geometry offset of the stroke centreline */
    AnimatedProperty stroke_offset_prop { "stroke_offset", 0.0 };
    float       outline_opacity = 1.0f;
    int         outline_join_style = 1;  /* 0=miter, 1=round, 2=bevel */
    bool        outline_on_front = false;
    int         outline_alignment = 0;  /* 0=outer, 1=mid/centered, 2=inner */
    bool        outline_antialias = true;
    int         stroke_gradient_type = 0;  /* 0=linear, 1=radial, 2=conical */
    int         stroke_gradient_spread = 0; /* 0=no/pad, 1=reflect, 2=repeat */
    uint32_t    stroke_gradient_start_color = 0xFFFFFFFF;
    uint32_t    stroke_gradient_end_color   = 0xFF000000;
    float       stroke_gradient_start_pos = 0.0f;
    float       stroke_gradient_end_pos   = 1.0f;
    float       stroke_gradient_start_opacity = 1.0f;
    float       stroke_gradient_end_opacity   = 1.0f;
    float       stroke_gradient_opacity   = 1.0f;
    float       stroke_gradient_angle     = 0.0f;
    float       stroke_gradient_center_x  = 0.5f;
    float       stroke_gradient_center_y  = 0.5f;
    float       stroke_gradient_scale     = 1.0f;
    float       stroke_gradient_focal_x   = 0.5f;
    float       stroke_gradient_focal_y   = 0.5f;
    std::vector<GradientStop> stroke_gradient_stops; /* additional intermediate gradient stops between start/end */

    int         align_h       = 1;  /* 0=left 1=center 2=right 3=justify last left 4=justify last center 5=justify last right 6=justify all */
    int         align_v       = 1;  /* 0=top  1=middle 2=bottom 3=distribute lines */
    float       paragraph_indent_left = 0.0f;
    float       paragraph_indent_right = 0.0f;
    float       paragraph_indent_first_line = 0.0f;
    AnimatedProperty paragraph_indent_left_prop { "paragraph_indent_left", 0.0 };
    AnimatedProperty paragraph_indent_right_prop { "paragraph_indent_right", 0.0 };
    AnimatedProperty paragraph_indent_first_line_prop { "paragraph_indent_first_line", 0.0 };
    float       paragraph_space_before = 0.0f;
    AnimatedProperty paragraph_space_before_prop { "paragraph_space_before", 0.0 };
    float       paragraph_space_after = 0.0f;
    AnimatedProperty paragraph_space_after_prop { "paragraph_space_after", 0.0 };
    bool        paragraph_hyphenate = false;

    /* ----- Solid / shape ----- */
    uint32_t    fill_color    = 0xFF222222;
    int         fill_type     = 0;  /* 0=solid, 1=gradient */
    int         gradient_type = 0;  /* 0=linear, 1=radial, 2=conical */
    int         gradient_spread = 0; /* 0=no/pad, 1=reflect, 2=repeat */
    uint32_t    gradient_start_color = 0xFF4B6EA8;
    uint32_t    gradient_end_color   = 0xFF1B1B1B;
    float       gradient_start_pos = 0.0f;
    float       gradient_end_pos   = 1.0f;
    float       gradient_start_opacity = 1.0f;
    float       gradient_end_opacity   = 1.0f;
    float       gradient_opacity   = 1.0f;
    float       gradient_angle     = 0.0f;
    float       gradient_center_x  = 0.5f;
    float       gradient_center_y  = 0.5f;
    float       gradient_scale     = 1.0f;
    float       gradient_focal_x   = 0.5f;
    float       gradient_focal_y   = 0.5f;
    std::vector<GradientStop> gradient_stops; /* additional intermediate gradient stops between start/end */

    /* Optional box background for text/image layers. */
    bool        background_enabled = false;
    uint32_t    background_color = 0xFF000000;
    float       background_opacity = 0.35f;
    float       background_padding_x = 0.0f; /* legacy symmetric horizontal padding */
    float       background_padding_y = 0.0f; /* legacy symmetric vertical padding */
    float       background_padding_left = 0.0f;
    float       background_padding_right = 0.0f;
    float       background_padding_top = 0.0f;
    float       background_padding_bottom = 0.0f;
    float       background_corner_radius = 0.0f; /* legacy shared radius */
    float       background_corner_radius_tl = 0.0f;
    float       background_corner_radius_tr = 0.0f;
    float       background_corner_radius_br = 0.0f;
    float       background_corner_radius_bl = 0.0f;
    CornerType  background_corner_type = CornerType::Round;
    int         background_fill_type = 0;  /* 0=solid, 1=gradient */
    uint32_t    background_stroke_color = 0x00000000;
    float       background_stroke_width = 0.0f;
    float       background_stroke_opacity = 1.0f;
    int         background_stroke_fill_type = 0;  /* reserved: 0=solid */
    int         background_gradient_type = 0;  /* 0=linear, 1=radial, 2=conical */
    int         background_gradient_spread = 0; /* 0=no/pad, 1=reflect, 2=repeat */
    uint32_t    background_gradient_start_color = 0xFF4B6EA8;
    uint32_t    background_gradient_end_color   = 0xFF1B1B1B;
    float       background_gradient_start_pos = 0.0f;
    float       background_gradient_end_pos   = 1.0f;
    float       background_gradient_start_opacity = 1.0f;
    float       background_gradient_end_opacity   = 1.0f;
    float       background_gradient_opacity   = 1.0f;
    float       background_gradient_angle     = 0.0f;
    float       background_gradient_center_x  = 0.5f;
    float       background_gradient_center_y  = 0.5f;
    float       background_gradient_scale     = 1.0f;
    float       background_gradient_focal_x   = 0.5f;
    float       background_gradient_focal_y   = 0.5f;
    std::vector<GradientStop> background_gradient_stops; /* additional intermediate gradient stops between start/end */
    AnimatedProperty background_enabled_prop { "background_enabled", 0.0 };
    AnimatedProperty background_opacity_prop { "background_opacity", 0.35 };
    AnimatedProperty background_padding_x_prop { "background_padding_x", 0.0 };
    AnimatedProperty background_padding_y_prop { "background_padding_y", 0.0 };
    AnimatedProperty background_padding_left_prop { "background_padding_left", 0.0 };
    AnimatedProperty background_padding_right_prop { "background_padding_right", 0.0 };
    AnimatedProperty background_padding_top_prop { "background_padding_top", 0.0 };
    AnimatedProperty background_padding_bottom_prop { "background_padding_bottom", 0.0 };
    AnimatedProperty background_corner_radius_prop { "background_corner_radius", 0.0 };
    AnimatedProperty background_corner_radius_tl_prop { "background_corner_radius_tl", 0.0 };
    AnimatedProperty background_corner_radius_tr_prop { "background_corner_radius_tr", 0.0 };
    AnimatedProperty background_corner_radius_br_prop { "background_corner_radius_br", 0.0 };
    AnimatedProperty background_corner_radius_bl_prop { "background_corner_radius_bl", 0.0 };
    AnimatedProperty background_stroke_width_prop { "background_stroke_width", 0.0 };
    AnimatedProperty background_stroke_opacity_prop { "background_stroke_opacity", 1.0 };
    AnimatedProperty background_color_a { "background_color_a", 255.0 };
    AnimatedProperty background_color_r { "background_color_r", 0.0 };
    AnimatedProperty background_color_g { "background_color_g", 0.0 };
    AnimatedProperty background_color_b { "background_color_b", 0.0 };
    AnimatedProperty background_stroke_color_a { "background_stroke_color_a", 0.0 };
    AnimatedProperty background_stroke_color_r { "background_stroke_color_r", 0.0 };
    AnimatedProperty background_stroke_color_g { "background_stroke_color_g", 0.0 };
    AnimatedProperty background_stroke_color_b { "background_stroke_color_b", 0.0 };

    float       rect_width    = 1920.0f;
    float       rect_height   = 100.0f;
    float       corner_radius = 0.0f;
    float       corner_radius_tl = 0.0f;
    float       corner_radius_tr = 0.0f;
    float       corner_radius_br = 0.0f;
    float       corner_radius_bl = 0.0f;
    bool        corner_radius_locked = true;
    float       corner_bevel_roundness = 100.0f; /* -100=inverted round, 0=flat bevel, 100=round */
    ShapeType   shape_type = ShapeType::Rectangle;
    std::vector<BezierPathPoint> path_points;
    bool        path_closed = true;
    int         shape_points = 5;
    int         shape_sides = 6;
    float       shape_inner_radius = 0.20f;
    float       shape_outer_radius = 0.5f;
    float       shape_roundness = 0.0f;       /* polygon / star outer corners */
    float       shape_inner_roundness = 0.0f; /* star inner corners */
    bool        scale_stroke_with_shape = false;
    bool        scale_corners_with_shape = false;

    /* Keyframable geometry mirrors the static fields above so older saved
     * titles remain readable while new titles can animate size/origin.
     */
    AnimatedVectorProperty size { "size", {1920.0, 100.0} };

    /* ----- Geometry anchor / origin -----
     * Normalized inside the editable bounding box: 0.0 = left/top,
     * 0.5 = center, 1.0 = right/bottom. The layer position is this origin.
     */
    float       origin_x      = 0.5f;
    float       origin_y      = 0.5f;
    AnimatedVectorProperty origin_prop { "origin", {0.5, 0.5} };

    /* ----- Drop shadow ----- */
    bool        shadow_enabled = false;
    uint32_t    shadow_color   = 0x99000000;
    float       shadow_opacity = 0.6f;
    float       shadow_distance = 8.0f;
    float       shadow_angle = 135.0f;
    float       shadow_blur = 4.0f;
    float       shadow_spread = 0.0f;
    ShadowBlurType shadow_blur_type = ShadowBlurType::StackFast;
    bool        long_shadow_enabled = false;
    uint32_t    long_shadow_color = 0x99000000;
    float       long_shadow_opacity = 0.45f;
    float       long_shadow_length = 0.0f;
    float       long_shadow_angle = 135.0f;
    float       long_shadow_falloff = 1.0f;
    LongShadowBlurType long_shadow_blur_type = LongShadowBlurType::None;
    float       long_shadow_blur = 8.0f;
    AnimatedProperty shadow_enabled_prop { "shadow_enabled", 0.0 };
    AnimatedProperty shadow_opacity_prop { "shadow_opacity", 0.6 };
    AnimatedProperty shadow_distance_prop { "shadow_distance", 8.0 };
    AnimatedProperty shadow_angle_prop { "shadow_angle", 135.0 };
    AnimatedProperty shadow_blur_prop { "shadow_blur", 4.0 };
    AnimatedProperty shadow_spread_prop { "shadow_spread", 0.0 };
    AnimatedProperty shadow_color_a { "shadow_color_a", 153.0 };
    AnimatedProperty shadow_color_r { "shadow_color_r", 0.0 };
    AnimatedProperty shadow_color_g { "shadow_color_g", 0.0 };
    AnimatedProperty shadow_color_b { "shadow_color_b", 0.0 };

    /* ----- Keyframable color channels, 0-255 ARGB. */
    AnimatedProperty text_color_a { "text_color_a", 255.0 };
    AnimatedProperty text_color_r { "text_color_r", 255.0 };
    AnimatedProperty text_color_g { "text_color_g", 255.0 };
    AnimatedProperty text_color_b { "text_color_b", 255.0 };
    AnimatedProperty fill_color_a { "fill_color_a", 255.0 };
    AnimatedProperty fill_color_r { "fill_color_r",  34.0 };
    AnimatedProperty fill_color_g { "fill_color_g",  34.0 };
    AnimatedProperty fill_color_b { "fill_color_b",  34.0 };
    AnimatedProperty stroke_color_a { "stroke_color_a", 255.0 };
    AnimatedProperty stroke_color_r { "stroke_color_r",   0.0 };
    AnimatedProperty stroke_color_g { "stroke_color_g",   0.0 };
    AnimatedProperty stroke_color_b { "stroke_color_b",   0.0 };

    /* ----- Image ----- */
    std::string image_path;
    bool        lock_aspect_ratio = true;
    bool        image_box_lock_aspect_ratio = false;
    ImageScaleFilter scale_filter = ImageScaleFilter::Bilinear;
    ImageBoxMode image_box_mode = ImageBoxMode::FitImageToBox;
    bool        image_size_auto_fit = true;
    bool        image_crop_when_outside_box = false;
    float       image_anchor_x = 0.5f;
    float       image_anchor_y = 0.5f;
    float       image_width = 1920.0f;
    float       image_height = 1080.0f;
    AnimatedVectorProperty image_size { "image_size", {1920.0, 1080.0} };
};

inline LayerVector3Value evaluated_layer_position_3d(const Layer &layer, double time)
{
    if (layer.position_3d_path_enabled) {
        const Vec3Value xyz = layer.position_3d.evaluate(time);
        return {xyz.x, xyz.y, xyz.z};
    }
    const Vec2Value xyz = layer.position.evaluate(time);
    return {xyz.x, xyz.y, xyz.z};
}

inline Vec3Value legacy_layer_position_3d(const Layer &layer, double time)
{
    const Vec2Value xyz = layer.position.evaluate(time);
    return {xyz.x, xyz.y, xyz.z};
}

inline void promote_layer_position_to_3d_path(Layer &layer, bool activate = true)
{
    if (layer.position_3d_path_enabled) return;
    layer.position_3d.name = "position_3d";
    layer.position_3d.static_value = legacy_layer_position_3d(layer, 0.0);

    std::vector<double> times;
    times.reserve(layer.position.keyframes.size() + layer.position_z.keyframes.size());
    for (const VectorKeyframe &key : layer.position.keyframes) times.push_back(key.time);
    for (const Keyframe &key : layer.position_z.keyframes) times.push_back(key.time);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-9;
    }), times.end());

    layer.position_3d.keyframes.clear();
    layer.position_3d.keyframes.reserve(times.size());
    for (double time : times) {
        Vector3Keyframe key;
        key.time = time;
        key.value = legacy_layer_position_3d(layer, time);
        auto xy = std::find_if(layer.position.keyframes.begin(),
                               layer.position.keyframes.end(),
            [time](const VectorKeyframe &candidate) {
                return std::abs(candidate.time - time) <= 1.0e-9;
            });
        if (xy != layer.position.keyframes.end()) {
            key.easing = xy->easing;
            key.cx1 = xy->cx1; key.cy1 = xy->cy1;
            key.cx2 = xy->cx2; key.cy2 = xy->cy2;
            key.temporal_mode = xy->temporal_mode;
            key.incoming_influence = xy->incoming_influence;
            key.outgoing_influence = xy->outgoing_influence;
            key.incoming_speed = xy->incoming_speed;
            key.outgoing_speed = xy->outgoing_speed;
            key.temporal_tangents_linked = xy->temporal_tangents_linked;
            key.temporal_velocity_explicit = xy->temporal_velocity_explicit;
            key.incoming_tangent = {xy->incoming_tangent.x,
                                    xy->incoming_tangent.y,
                                    xy->incoming_tangent.z};
            key.outgoing_tangent = {xy->outgoing_tangent.x,
                                    xy->outgoing_tangent.y,
                                    xy->outgoing_tangent.z};
            key.spatial_mode = xy->spatial_mode;
            key.spatial_tangents_linked = xy->spatial_tangents_linked;
            key.rove_across_time = xy->rove_across_time;
        }
        layer.position_3d.keyframes.push_back(key);
    }
    layer.position_3d_path_enabled = activate;
    layer.position_3d.recalculate_rove_times();
}

inline void mirror_position_3d_to_legacy(Layer &layer, double time)
{
    if (!layer.position_3d_path_enabled) return;
    const Vec3Value value = layer.position_3d.evaluate(time);
    layer.position.static_value = {value.x, value.y, value.z};
    layer.position_z.static_value = value.z;
}

inline void mirror_position_3d_track_to_legacy(Layer &layer)
{
    if (!layer.position_3d_path_enabled) return;

    const Vec3Value static_value = layer.position_3d.static_value;
    layer.position.static_value =
        {static_value.x, static_value.y, static_value.z};
    layer.position_z.static_value = static_value.z;
    layer.position.keyframes.clear();
    layer.position_z.keyframes.clear();
    layer.position.keyframes.reserve(layer.position_3d.keyframes.size());
    layer.position_z.keyframes.reserve(layer.position_3d.keyframes.size());

    for (const Vector3Keyframe &source : layer.position_3d.keyframes) {
        VectorKeyframe xy;
        xy.time = source.time;
        xy.value = {source.value.x, source.value.y, source.value.z};
        xy.easing = source.easing;
        xy.cx1 = source.cx1; xy.cy1 = source.cy1;
        xy.cx2 = source.cx2; xy.cy2 = source.cy2;
        xy.temporal_mode = source.temporal_mode;
        xy.incoming_influence = source.incoming_influence;
        xy.outgoing_influence = source.outgoing_influence;
        xy.incoming_speed = source.incoming_speed;
        xy.outgoing_speed = source.outgoing_speed;
        xy.temporal_tangents_linked = source.temporal_tangents_linked;
        xy.temporal_velocity_explicit = source.temporal_velocity_explicit;
        xy.incoming_tangent = {source.incoming_tangent.x,
                               source.incoming_tangent.y,
                               source.incoming_tangent.z};
        xy.outgoing_tangent = {source.outgoing_tangent.x,
                               source.outgoing_tangent.y,
                               source.outgoing_tangent.z};
        xy.spatial_mode = source.spatial_mode;
        xy.spatial_tangents_linked = source.spatial_tangents_linked;
        xy.rove_across_time = source.rove_across_time;
        layer.position.keyframes.push_back(xy);

        Keyframe z;
        z.time = source.time;
        z.value = source.value.z;
        z.easing = source.easing;
        z.cx1 = source.cx1; z.cy1 = source.cy1;
        z.cx2 = source.cx2; z.cy2 = source.cy2;
        z.temporal_mode = source.temporal_mode;
        z.incoming_influence = source.incoming_influence;
        z.outgoing_influence = source.outgoing_influence;
        z.incoming_speed = source.incoming_speed;
        z.outgoing_speed = source.outgoing_speed;
        z.temporal_tangents_linked = source.temporal_tangents_linked;
        z.temporal_velocity_explicit = source.temporal_velocity_explicit;
        layer.position_z.keyframes.push_back(z);
    }
    layer.position.recalculate_rove_times();
}

inline void set_layer_dimension_mode_preserving_position_track(
    Layer &layer, LayerDimensionMode mode)
{
    if (layer.dimension_mode == mode) return;
    if (mode == LayerDimensionMode::ThreeD) {
        promote_layer_position_to_3d_path(layer);
        layer.position_3d_path_enabled = true;
        if (layer.double_sided && !layer.backface_culling) {
            layer.double_sided = false;
            layer.backface_culling = true;
        }
    } else {
        mirror_position_3d_track_to_legacy(layer);
        /* In 2D mode the legacy XYZ-compatible vector is authoritative.
         * Leaving the 3D path active would make panel edits write one track
         * while rendering continued to read another. */
        layer.position_3d_path_enabled = false;
    }
    layer.dimension_mode = mode;
}


inline double layer_timeline_span_seconds(const Layer &layer)
{
    return std::max(0.0, layer.out_time - layer.in_time);
}

inline double layer_media_duration_limit(const Layer &layer)
{
    if (layer.type == LayerType::Video)
        return std::max(0.0, layer.video_media_duration);
    if (layer.type == LayerType::Audio)
        return std::max(0.0, layer.audio_media_duration);
    return 0.0;
}

inline void normalize_layer_media_range_to_timeline_span(Layer &layer, bool anchor_out)
{
    if (layer.type != LayerType::Audio && layer.type != LayerType::Video)
        return;
    const double span = layer_timeline_span_seconds(layer);
    double *media_in = layer.type == LayerType::Video ? &layer.video_in_point : &layer.audio_in_point;
    double *media_out = layer.type == LayerType::Video ? &layer.video_out_point : &layer.audio_out_point;
    const double duration = layer_media_duration_limit(layer);

    double in_value = std::max(0.0, *media_in);
    double out_value = std::max(0.0, *media_out);
    if (span <= 0.0) {
        *media_in = in_value;
        *media_out = in_value;
        return;
    }

    if (anchor_out) {
        if (out_value <= 0.0)
            out_value = in_value + span;
        in_value = std::max(0.0, out_value - span);
        out_value = in_value + span;
    } else {
        out_value = in_value + span;
    }

    if (duration > 0.0 && out_value > duration) {
        out_value = duration;
        in_value = std::max(0.0, out_value - span);
        out_value = in_value + span;
        if (out_value > duration)
            out_value = duration;
    }

    *media_in = std::max(0.0, in_value);
    *media_out = std::max(*media_in, out_value);
}

inline void set_layer_media_range_in_point(Layer &layer, double media_in)
{
    if (layer.type == LayerType::Video)
        layer.video_in_point = std::max(0.0, media_in);
    else if (layer.type == LayerType::Audio)
        layer.audio_in_point = std::max(0.0, media_in);
    normalize_layer_media_range_to_timeline_span(layer, false);
}

inline void set_layer_media_range_out_point(Layer &layer, double media_out)
{
    if (layer.type == LayerType::Video)
        layer.video_out_point = std::max(0.0, media_out);
    else if (layer.type == LayerType::Audio)
        layer.audio_out_point = std::max(0.0, media_out);
    normalize_layer_media_range_to_timeline_span(layer, true);
}


inline uint32_t evaluated_material_emissive_color(const Layer &layer, double time)
{
    auto channel = [time](const AnimatedProperty &property) {
        return static_cast<uint32_t>(std::clamp(
            static_cast<int>(std::lround(property.evaluate(time))), 0, 255));
    };
    return (channel(layer.material_emissive_color_a) << 24) |
           (channel(layer.material_emissive_color_r) << 16) |
           (channel(layer.material_emissive_color_g) << 8) |
           channel(layer.material_emissive_color_b);
}

inline void mirror_material_emissive_color(Layer &layer, double time)
{
    layer.material_emissive_color = evaluated_material_emissive_color(layer, time);
}

inline LayerVector3Value evaluated_layer_scale_3d(const Layer &layer, double time)
{
    const Vec2Value xyz = layer.scale.evaluate(time);
    return {xyz.x, xyz.y, xyz.z};
}

inline LayerVector3Value evaluated_layer_anchor_3d(const Layer &layer, double time)
{
    const Vec2Value xyz = layer.origin_prop.evaluate(time);
    return {xyz.x, xyz.y, xyz.z};
}

inline LayerVector3Value evaluated_layer_rotation_3d(const Layer &layer, double time)
{
    return {layer.rotation_x.evaluate(time), layer.rotation_y.evaluate(time),
            layer.rotation.evaluate(time)};
}

inline LayerVector3Value evaluated_layer_orientation_3d(const Layer &layer, double time)
{
    return {layer.orientation_x.evaluate(time), layer.orientation_y.evaluate(time),
            layer.orientation_z.evaluate(time)};
}
