/*
 * title-data.h
 *
 * Core data model for the Flux Motion plugin.
 *
 * A Title is composed of one or more Layers. Each layer has a set of
 * Properties (position, scale, opacity, colour, text …). Properties
 * can be animated over time via Keyframes that live on a Timeline.
 *
 * The TitleDataStore is a singleton that owns all titles for the active
 * scene collection and persists them to a scene-collection-specific JSON
 * file in the OBS profile directory.
 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <thread>
#include <mutex>
#include <set>
#include <unordered_map>
#include "layer-model.h"

enum class TitleGraphicType : int {
    Title = 0,
    Graphic = 1,
    Mask = 2,
    Stinger = 3,
};

enum class StingerRenderMode : int {
    ProceduralLive = 0,
    PrerenderedProxy = 1,
};

enum class StingerSwitchMode : int {
    SwitchAtPoint = 0,
    ManualSceneAnimation = 1,
};

enum class StingerEditorBackground : int {
    /* Values 0/1 were used by the temporary static A/B selector in v168-v169.
     * They remain readable for compatibility and are migrated to FollowSwitchPoint. */
    SceneA = 0,
    SceneB = 1,
    CanvasTransparency = 2,
    FollowSwitchPoint = 3,
};


enum class CameraProjection : int {
    Perspective = 0,
    Orthographic = 1,
};

/* Camera properties are title-level so editor navigation can remain separate
 * from the render camera. The default camera is derived from the canvas and
 * reproduces the historical 2D projection exactly at Z=0. */
struct TitleCamera {
    /* Development Version 218: opaque source JSON retained across model round-trips. */
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id = "default";
    std::string name = "Default Camera";
    /* Non-empty only for an internal camera snapshot owned by an isolated
     * Asset Layer. These cameras are serialized with the host title but stay
     * hidden from its camera UI and cannot become its active camera. */
    std::string asset_space_owner_id;
    bool use_canvas_default = true;
    AnimatedProperty position_x { "camera_position_x", 0.0 };
    AnimatedProperty position_y { "camera_position_y", 0.0 };
    AnimatedProperty position_z { "camera_position_z", -1000.0 };
    AnimatedVec3Property position_3d { "camera_position_3d", {0.0, 0.0, -1000.0} };
    bool position_3d_path_enabled = false;
    AnimatedProperty target_x { "camera_target_x", 0.0 };
    AnimatedProperty target_y { "camera_target_y", 0.0 };
    AnimatedProperty target_z { "camera_target_z", 0.0 };
    AnimatedVec3Property target_3d { "camera_target_3d", {0.0, 0.0, 0.0} };
    bool target_3d_path_enabled = false;
    AnimatedProperty orientation_x { "camera_orientation_x", 0.0 };
    AnimatedProperty orientation_y { "camera_orientation_y", 0.0 };
    AnimatedProperty orientation_z { "camera_orientation_z", 0.0 };
    AnimatedProperty rotation_x { "camera_rotation_x", 0.0 };
    AnimatedProperty rotation_y { "camera_rotation_y", 0.0 };
    AnimatedProperty rotation_z { "camera_rotation_z", 0.0 };
    AnimatedProperty focal_length { "camera_focal_length", 1000.0 };
    AnimatedProperty field_of_view { "camera_field_of_view", 56.7380926 };
    AnimatedProperty zoom { "camera_zoom", 1.0 };
    AnimatedProperty near_clip { "camera_near_clip", 0.1 };
    AnimatedProperty far_clip { "camera_far_clip", 100000.0 };
    AnimatedProperty projection_mode { "camera_projection", 0.0 };
    CameraProjection projection = CameraProjection::Perspective; /* static compatibility mirror */
    /* AE-style timeline disclosure state. Camera channels stay hidden until
     * the camera row is expanded, matching ordinary layer property rows. */
    bool timeline_expanded = false;
};

inline Vec3Value evaluated_camera_position_3d(const TitleCamera &camera, double time)
{
    if (camera.position_3d_path_enabled)
        return camera.position_3d.evaluate(time);
    return {camera.position_x.evaluate(time), camera.position_y.evaluate(time),
            camera.position_z.evaluate(time)};
}

inline Vec3Value evaluated_camera_target_3d(const TitleCamera &camera, double time)
{
    if (camera.target_3d_path_enabled)
        return camera.target_3d.evaluate(time);
    return {camera.target_x.evaluate(time), camera.target_y.evaluate(time),
            camera.target_z.evaluate(time)};
}

inline void promote_camera_vector_track(AnimatedVec3Property &track, bool &enabled,
                                        const AnimatedProperty &x,
                                        const AnimatedProperty &y,
                                        const AnimatedProperty &z,
                                        bool activate = true)
{
    if (enabled) return;
    track.static_value = {x.static_value, y.static_value, z.static_value};
    std::vector<double> times;
    times.reserve(x.keyframes.size() + y.keyframes.size() + z.keyframes.size());
    for (const Keyframe &key : x.keyframes) times.push_back(key.time);
    for (const Keyframe &key : y.keyframes) times.push_back(key.time);
    for (const Keyframe &key : z.keyframes) times.push_back(key.time);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](double a, double b) {
        return std::abs(a - b) <= 1.0e-9;
    }), times.end());
    track.keyframes.clear();
    track.keyframes.reserve(times.size());
    for (double time : times) {
        Vector3Keyframe key;
        key.time = time;
        key.value = {x.evaluate(time), y.evaluate(time), z.evaluate(time)};
        const Keyframe *source = nullptr;
        auto find_at = [time](const AnimatedProperty &property) -> const Keyframe * {
            for (const Keyframe &candidate : property.keyframes)
                if (std::abs(candidate.time - time) <= 1.0e-9) return &candidate;
            return nullptr;
        };
        source = find_at(x);
        if (!source) source = find_at(y);
        if (!source) source = find_at(z);
        if (source) {
            key.easing = source->easing;
            key.cx1 = source->cx1; key.cy1 = source->cy1;
            key.cx2 = source->cx2; key.cy2 = source->cy2;
            key.temporal_mode = source->temporal_mode;
            key.incoming_influence = source->incoming_influence;
            key.outgoing_influence = source->outgoing_influence;
            key.incoming_speed = source->incoming_speed;
            key.outgoing_speed = source->outgoing_speed;
            key.temporal_tangents_linked = source->temporal_tangents_linked;
            key.temporal_velocity_explicit = source->temporal_velocity_explicit;
        }
        track.keyframes.push_back(key);
    }
    enabled = activate;
    track.recalculate_rove_times();
}

inline void promote_camera_spatial_tracks(TitleCamera &camera, bool activate = true)
{
    promote_camera_vector_track(camera.position_3d, camera.position_3d_path_enabled,
                                camera.position_x, camera.position_y, camera.position_z,
                                activate);
    promote_camera_vector_track(camera.target_3d, camera.target_3d_path_enabled,
                                camera.target_x, camera.target_y, camera.target_z,
                                activate);
}

inline void mirror_camera_vector_track_to_legacy(AnimatedVec3Property &track, double time,
                                                  AnimatedProperty &x,
                                                  AnimatedProperty &y,
                                                  AnimatedProperty &z)
{
    const Vec3Value value = track.evaluate(time);
    x.static_value = value.x;
    y.static_value = value.y;
    z.static_value = value.z;
}

inline bool title_camera_has_authored_keyframes(const TitleCamera &camera)
{
    const bool position_animated = camera.position_3d_path_enabled
        ? camera.position_3d.is_animated()
        : camera.position_x.is_animated() || camera.position_y.is_animated() ||
          camera.position_z.is_animated() || camera.position_3d.is_animated();
    const bool target_animated = camera.target_3d_path_enabled
        ? camera.target_3d.is_animated()
        : camera.target_x.is_animated() || camera.target_y.is_animated() ||
          camera.target_z.is_animated() || camera.target_3d.is_animated();
    return position_animated || target_animated ||
           camera.orientation_x.is_animated() || camera.orientation_y.is_animated() ||
           camera.orientation_z.is_animated() || camera.rotation_x.is_animated() ||
           camera.rotation_y.is_animated() || camera.rotation_z.is_animated() ||
           camera.focal_length.is_animated() || camera.field_of_view.is_animated() ||
           camera.zoom.is_animated() || camera.near_clip.is_animated() ||
           camera.far_clip.is_animated() || camera.projection_mode.is_animated();
}

struct TitleProxyMetadata {
    int schema_version = 0;
    std::string content_hash;
    std::string cache_namespace;
    std::string proxy_path;
    std::string generated_at;
    int generated_development_version = 0;
    int width = 0;
    int height = 0;
    double frame_rate = 0.0;
    int frame_count = 0;
    bool has_audio = false;
    bool complete = false;
};

struct LiveTextCueStyleOverride {
    std::string row_id;
    std::string layer_id;
    bool fill_color_set = false;
    uint32_t fill_color = 0xFFFFFFFF;
    bool stroke_color_set = false;
    uint32_t stroke_color = 0xFF000000;
};

struct StingerValidationResult {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    bool valid() const { return errors.empty(); }
};

/* ══════════════════════════════════════════════════════════════════
 *  Title
 * ══════════════════════════════════════════════════════════════════ */
struct Title {
    /* Unknown/newer fields are preserved verbatim through load → edit → save. */
    OpaqueSerializationPassthrough serialization_passthrough_json;
    std::string id;
    std::string name        = "Untitled";
    std::string description;
    std::string creator;
    std::string creation_date;
    double      duration    = 5.0;   /* total clip duration (seconds) */
    double      loop_start  = 1.0;   /* live-cue loop start (seconds) */
    double      loop_end    = 4.0;   /* live-cue loop end (seconds) */
    int         playback_mode = 0;   /* 0=play once, 1=loop in/out, 2=pause at position */
    int         loop_type     = 0;   /* 0=restart, 1=ping-pong */
    int         cue_end_behavior = 0; /* 0=show last frame, 1=show nothing, 2=show first frame */
    double      pause_time    = 0.0; /* seconds from timeline start */
    uint32_t    bg_color    = 0x00000000;  /* transparent by default */
    int         width       = 1920;
    int         height      = 1080;
    double      frame_rate  = 30.0;  /* authored/editor preview cadence */

    TitleGraphicType graphic_type = TitleGraphicType::Title;
    std::vector<TitleCamera> cameras { TitleCamera{} };
    std::string active_camera_id = "default";
    AnimatedDiscreteProperty active_camera { "active_camera", "default" };
    std::vector<TitleLight> lights;
    bool default_light_enabled = true;
    bool lighting_enabled = true;
    double environment_exposure = 0.0;
    /* The global switch track follows the same disclosure model as cameras. */
    bool camera_switches_expanded = false;
    /* Shared disclosure state for multi-channel timeline properties.  The
     * layer list and TimelineWidget both flatten rows from this one set, so
     * they can never disagree about whether X/Y/Z child rows are visible. */
    std::set<std::string> expanded_property_channels;
    /* Runtime-only render override used by non-destructive editor 3D views.
     * It is deliberately excluded from serialization and cache identity so
     * orbiting the editor camera can never alter OBS output or title content. */
    std::string render_camera_override_id;

    /* Stinger document settings. The transition point is stored canonically
     * in seconds and is always edited/displayed as timecode. */
    double      stinger_transition_point = 2.5;
    bool        stinger_audio_enabled = true;
    bool        stinger_alpha_output = true;
    double      stinger_pre_roll = 0.0;
    double      stinger_post_roll = 0.0;
    StingerRenderMode stinger_render_mode = StingerRenderMode::ProceduralLive;
    StingerSwitchMode stinger_switch_mode = StingerSwitchMode::SwitchAtPoint;
    StingerEditorBackground stinger_editor_background = StingerEditorBackground::FollowSwitchPoint;
    TitleProxyMetadata proxy_metadata; /* advisory only; stale/missing files never block title loading */

    /* Titles saved as reusable assets remain in the shared data store, but
     * are excluded from the Titles & Graphics dock and OBS source selector. */
    bool        is_asset = false;
    bool        asset_animated = false;
    std::string asset_category = "Default";
    std::vector<std::string> asset_tags;

    /* Fonts extracted from a packed title are application-local resources,
     * not system installations.  Persist their resolved paths so a later OBS
     * session can register them again before text layers are deserialized. */
    std::vector<std::string> packed_font_files;

    std::vector<std::shared_ptr<Layer>> layers;  /* bottom → top order */

    /* Provider-neutral external-data schema authored with this title. Current
     * values, timestamps and connection state are runtime-only manager state. */
    std::vector<ExternalDataSourceDefinition> external_data_sources;

    /* Editor defaults persisted with the title/template. These are used only
     * as the initial style for newly-created layers; they must not affect
     * rendering and must not carry an effect stack. */
    bool        editor_default_style_enabled = false;
    Layer       editor_default_layer_style;
    uint32_t    editor_default_foreground_color = 0xFF222222;
    uint32_t    editor_default_background_color = 0xFFFFFFFF;
    /* Photoshop-style recent colors for the editor color tab: stored newest-first,
     * de-duplicated, and persisted with the template rather than pushed on every drag. */
    std::vector<std::string> editor_recent_color_hexes;
    std::vector<std::vector<std::string>> live_text_rows;
    std::vector<std::string> live_text_row_ids; /* persistent stable IDs parallel to live_text_rows */
    std::vector<std::string> live_text_column_order; /* exposed text layer IDs by logical cue column */
    std::vector<LiveTextExternalBinding> live_text_external_bindings; /* optional provider bindings keyed by stable row/layer IDs */
    std::vector<LiveTextTableBinding> live_text_table_bindings; /* source-managed table-to-cue row mappings */
    std::vector<LiveTextCueStyleOverride> live_text_cue_style_overrides; /* per-row exposed Fill/Stroke overrides keyed by stable row/layer IDs */
    std::string live_text_header_state; /* base64-encoded dock header layout */
    std::string preview_screenshot_png_base64; /* manually captured title-list thumbnail */
    bool external_data_enabled = false; /* live text cue external data source toggle */
    bool playlist_loop = false; /* per-title live text playlist behavior */
    bool playlist_reverse = false;
    bool playlist_restart_on_source_active = false;
    bool playlist_stop_on_source_inactive = false;
    double playlist_hold_seconds = 5.0;
    int current_cue_row = -1; /* runtime-only active live text row */
    int pending_cue_row = -1; /* runtime-only next row waiting for outro */
    int last_cue_row = -1; /* runtime-only most recently sent-to-Program row */
    bool cue_uncue_requested = false; /* runtime-only: keep active status until the outro completes */
    uint64_t cue_revision = 0; /* runtime-only live text cue counter */
    bool playlist_active = false; /* runtime-only playlist state */
    int playlist_next_row = 0;
    int64_t playlist_next_due_ms = 0;
    bool playlist_stop_after_due = false;
    bool cue_background_persistence = false; /* runtime-only setting: enable background persistence for cue transitions */
    bool cue_text_persistence = false; /* runtime-only setting: freeze unchanged exposed text columns while cueing */
    bool cue_persistence_transition = false; /* runtime-only active persistent transition between cue rows */
    std::vector<bool> cue_persistent_text_columns; /* runtime-only exposed text columns held at pause/loop */

    /* Helpers */
    std::shared_ptr<Layer> find_layer(const std::string &layer_id) const;
    void add_layer(std::shared_ptr<Layer> l);
    void remove_layer(const std::string &layer_id);
    void move_layer(const std::string &layer_id, int delta);
};

/* A Flux Motion project is a versioned container of independently editable
 * titles/graphics. Title remains the composition-level model so the editor,
 * renderer and OBS source keep one canonical timeline/layer implementation. */
struct TitleProject {
    static constexpr int CurrentSchemaVersion = 1;

    int schema_version = CurrentSchemaVersion;
    std::string id;
    std::string name = "Untitled Project";
    std::string description;
    std::string created_at;
    std::string modified_at;
    std::string active_title_id;
    std::vector<std::shared_ptr<Title>> titles;
};

struct ProjectLoadProgress {
    std::string step;
    std::string file;
    std::size_t completed = 0;
    std::size_t total = 0;
};

/* Return false from the callback to cancel at the next safe boundary. */
using ProjectLoadCallback = std::function<bool(const ProjectLoadProgress &)>;

double stinger_transition_point_seconds(const Title &title);
void set_stinger_transition_point_seconds(Title &title, double seconds);

/* Snap native serialized animation keys to the nearest frame at target_fps.
 * The operation is intentionally document-wide (layers, effects, text
 * animators, cameras, lights and nested title data) and returns the number of
 * keys that were moved. Keys already on a target-frame boundary are left
 * untouched. */
std::size_t quantize_title_keyframes_to_frame_rate(Title &title,
                                                   double target_fps);
StingerValidationResult validate_stinger_title(const Title &title);
std::shared_ptr<Layer> stinger_transition_input_layer(const Title &title, int slot);
void ensure_stinger_transition_input_layers(Title &title);
bool stinger_transition_input_layer_is_protected(const Layer &layer);

/* Keep generated Video audio-stream tracks on the exact same clip/media clock
 * as their owner. Per-stream gain, pan, mute, solo and effects remain local. */
void synchronize_video_audio_streams(Title &title,
                                     const std::string &video_layer_id = std::string());

struct LiveCueRuntimeSnapshot {
    int row = -1;
    double playhead = 0.0;
    double elapsed_seconds = 0.0;
    int64_t updated_ms = 0;
    uintptr_t source_token = 0;
    bool active = false;
};

/* Thread-safe, runtime-only timing channel between OBS source playback and the
 * Live Text Cues dock. It deliberately lives outside Title so 60 Hz source
 * updates never race title cloning, persistence, or cache fingerprinting. */
void publish_live_cue_runtime_state(const std::string &title_id,
                                    uintptr_t source_token, int row,
                                    double playhead, double elapsed_seconds,
                                    int64_t updated_ms);
void clear_live_cue_runtime_state(const std::string &title_id,
                                  uintptr_t source_token);
LiveCueRuntimeSnapshot live_cue_runtime_state(const std::string &title_id);

void ensure_live_text_row_ids(Title &title);
std::string live_text_row_id(const Title &title, int row);
void prune_live_text_cue_style_overrides(Title &title);
bool live_text_cue_color_override(const Title &title, const Layer &layer, int row, bool stroke, uint32_t *out_argb);
uint32_t live_text_cue_effective_color(const Title &title, const Layer &layer, int row, bool stroke);
void set_live_text_cue_color_override(Title &title, const Layer &layer, int row, bool stroke, uint32_t argb);
void clear_live_text_cue_color_override(Title &title, const Layer &layer, int row, bool stroke);
void apply_live_text_cue_style_to_layer(Title &title, Layer &layer, int row);
/* Stable fingerprint of raster-affecting layer data. Transform, visibility,
 * parenting, masks and compositing state are intentionally excluded so the
 * GPU compositor can reuse a layer texture across matrix-only edits. */
std::string layer_render_fingerprint(const Layer &layer);


struct TitleImportDiagnostics {
    std::vector<std::string> missing_effects;
    std::vector<std::string> missing_images;
    std::vector<std::string> missing_audio;
    std::vector<std::string> missing_fonts;

    bool empty() const
    {
        return missing_effects.empty() && missing_images.empty() && missing_audio.empty() && missing_fonts.empty();
    }
};

struct TitleTemplateExportMetadata {
    std::string title;
    std::string description;
    std::string creator;
    std::string creation_date;
    std::string category;
    std::string subcategory;
    std::string collection;
    std::string screenshot_png_base64;
};

struct PackedTitleExportOptions {
    bool pack_images = true;
    bool pack_media = true; /* Video and audio source files. */
    bool pack_fonts = true;
};

inline bool title_has_custom_camera(const Title &title)
{
    return std::any_of(title.cameras.begin(), title.cameras.end(),
                       [](const TitleCamera &camera) {
                           return camera.asset_space_owner_id.empty() &&
                                  (camera.id != "default" ||
                                   !camera.use_canvas_default);
                       });
}

inline bool title_default_camera_has_authored_keyframes(const Title &title)
{
    const auto it = std::find_if(title.cameras.begin(), title.cameras.end(),
                                 [](const TitleCamera &camera) {
                                     return camera.id == "default";
                                 });
    return it != title.cameras.end() && title_camera_has_authored_keyframes(*it);
}

/* Clipboard/preset serialization uses the exact project effect schema so
 * copies preserve animation tracks, extension payloads and future passthrough
 * fields without maintaining a second serializer. */
std::string serialize_layer_effect_stack_json(
    const std::vector<LayerEffect> &effects);
bool deserialize_layer_effect_stack_json(
    const std::string &payload, std::vector<LayerEffect> *effects,
    std::string *error = nullptr);

/* Cross-editor/process clipboard payload. Uses the canonical layer and camera
 * schemas so copy/paste preserves groups, animation, effects, rich text, media
 * bindings and camera assignments without a parallel lossy format. */
std::string serialize_layer_clipboard_json(
    const std::vector<std::shared_ptr<Layer>> &layers,
    const std::vector<TitleCamera> &cameras,
    const std::string &source_title_id);
bool deserialize_layer_clipboard_json(
    const std::string &payload,
    std::vector<std::shared_ptr<Layer>> *layers,
    std::vector<TitleCamera> *cameras,
    std::string *source_title_id,
    std::string *error = nullptr);

/* ══════════════════════════════════════════════════════════════════
 *  TitleDataStore  (singleton)
 * ══════════════════════════════════════════════════════════════════ */
class TitleDataStore {
public:
    static TitleDataStore &instance();
    static std::string make_uuid();

    /* CRUD */
    std::shared_ptr<Title> create_title(const std::string &name = "New Title");
    /*
     * Mutable title handles are store-owned and primarily used by the UI and
     * cue state machine. They must never carry a lifetime mutex: handles are
     * routinely retained by widgets and callbacks, and doing so can block the
     * OBS graphics thread during source activation and shutdown. Render, audio
     * and cache readers must use get_title_snapshot().
     */
    std::shared_ptr<Title> get_title(const std::string &id) const;
    std::shared_ptr<Title> get_title_snapshot(const std::string &id) const;
    void                   delete_title(const std::string &id);
    void                   rename_title(const std::string &id,
                                        const std::string &name);
    bool                   export_title(const std::string &id,
                                        const std::string &path,
                                        std::string *error = nullptr) const;
    bool                   export_title(const std::string &id,
                                        const std::string &path,
                                        const TitleTemplateExportMetadata &metadata,
                                        std::string *error = nullptr) const;
    bool                   export_packed_title(
                                        const std::string &id,
                                        const std::string &path,
                                        const TitleTemplateExportMetadata &metadata,
                                        const PackedTitleExportOptions &options,
                                        std::string *error = nullptr) const;
    std::shared_ptr<Title> import_title(const std::string &path,
                                        std::string *error = nullptr,
                                        TitleImportDiagnostics *diagnostics = nullptr);
    std::shared_ptr<Title> duplicate_title(const std::string &id,
                                           const std::string &name = {});

    /* Multi-title project I/O. read_project never mutates the store and is
     * therefore safe for OBS property enumeration. activate_project replaces
     * the current project atomically after a complete successful read. */
    bool read_project(const std::string &path, TitleProject *project,
                      std::string *error = nullptr,
                      ProjectLoadCallback progress = {}) const;
    bool export_project(const TitleProject &project, const std::string &path,
                        std::string *error = nullptr) const;
    bool activate_project(const TitleProject &project);
    std::vector<std::string> append_project(const TitleProject &project);
    void reorder_titles(const std::vector<std::string> &ordered_ids);

    std::vector<std::shared_ptr<Title>> titles() const;
    std::vector<std::shared_ptr<Title>> title_snapshots() const;

    /* Persistence */
    static std::string data_path();
    void load();
    void save() const;
    void save_async() const;
    void shutdownSaveWorker() const;

    /* Change notifications */
    using ChangeCallback = std::function<void()>;
    uint64_t on_change(ChangeCallback cb);
    void remove_change_callback(uint64_t callback_id);
    void notify_change();
    void touch_runtime_change();
    uint64_t revision() const { return revision_.load(); }

private:
    TitleDataStore();
    ~TitleDataStore();
    TitleDataStore(const TitleDataStore &) = delete;
    TitleDataStore &operator=(const TitleDataStore &) = delete;

    struct PendingSave {
        std::vector<std::shared_ptr<Title>> snapshot;
        std::string path;
        uint64_t generation = 0;
    };
    using TitleMutex = std::recursive_mutex;
    struct TitleAccessRecord {
        std::shared_ptr<Title> title;
        std::shared_ptr<TitleMutex> mutex;
    };
    void save_worker_loop() const;
    std::shared_ptr<TitleMutex> title_mutex_locked(
        const std::shared_ptr<Title> &title) const;
    std::vector<TitleAccessRecord> title_access_records() const;
    std::vector<std::shared_ptr<Title>> snapshot_authoritative_titles() const;
    void publish_title_snapshots(
        const std::vector<std::shared_ptr<Title>> &snapshots) const;
    void clear_title_publications_locked();
    static bool write_snapshot_atomic(const std::vector<std::shared_ptr<Title>> &snapshot,
                                       const std::string &path);
    mutable std::recursive_mutex         mutex_;
    std::vector<std::shared_ptr<Title>>  titles_;
    /*
     * Store-owned Title/Layer objects are mutable only behind their per-title
     * mutex. Published snapshots are deep copies and may be shared freely by
     * OBS render, cache, audio and provider threads.
     */
    mutable std::unordered_map<const Title *, std::shared_ptr<TitleMutex>>
                                         title_mutexes_;
    mutable std::unordered_map<std::string, std::shared_ptr<Title>>
                                         published_titles_;
    std::string                          loaded_path_;
    /* Development Version 244: never rewrite an existing scene-collection
     * titles file into the new schema unless the previous on-disk payload
     * loaded or cleanly initialized. Parse/read failures keep saving disabled
     * for that collection, preventing accidental empty/new-format overwrite. */
    bool                                 persistence_ready_for_save_ = false;
    struct ChangeObserver {
        uint64_t id = 0;
        ChangeCallback callback;
    };

    std::vector<ChangeObserver>          change_cbs_;
    uint64_t                             next_change_cb_id_ = 1;
    std::atomic<uint64_t>                revision_ { 0 };

    mutable std::mutex                   save_mutex_;
    mutable std::mutex                   save_io_mutex_;
    mutable std::condition_variable      save_cv_;
    mutable std::thread                  save_thread_;
    mutable bool                         save_stop_ = false;
    mutable bool                         save_worker_started_ = false;
    mutable uint64_t                     save_generation_ = 0;
    mutable std::unique_ptr<PendingSave> pending_save_;

};
