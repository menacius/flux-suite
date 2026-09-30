/*
 * title-source.h
 *
 * OBS source type "flux_motion_source".
 * Renders a Title through the unified GPU compositor used by OBS live output,
 * the editor preview and final cache readback.
 */

#pragma once

#include <obs-module.h>
#include <string>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <QImage>
#include <QRect>

#include "title-preview-renderer.h"

struct Title;

QImage render_title_gpu_frame_readback(const Title &title, double time,
                                       uint64_t model_revision = 0);
QImage render_title_to_image_obs(const Title &title, double time,
                                 uint64_t model_revision = 0);
QImage render_title_to_image_scaled(const Title &title, double time,
                                    double scale, bool editor_draft = false);
QImage render_title_region_to_image(const Title &title, double time,
                                    const QRect &region,
                                    uint64_t model_revision = 0);
QImage render_title_cache_to_image(const Title &title, double time,
                                   uint64_t model_revision = 0);
QImage render_title_cache_region_to_image(const Title &title, double time,
                                          const QRect &region,
                                          uint64_t model_revision = 0);
QImage render_title_over_cached_frame(const Title &title, double time,
                                      const QImage &cached_prefix,
                                      uint64_t model_revision = 0);

/* Runtime transport state shared by program sources and the private cue
 * preview source. PreviewReady is deliberately distinct from Paused: it means
 * the selected deterministic frame and its resources have been prepared, but
 * the program cue state machine has not been entered. */
enum class TitlePlaybackState : uint8_t {
    Idle = 0,
    PreviewReady,
    Queued,
    Playing,
    Paused,
    Stopped,
};

/* Registers the source type with OBS. Call once from obs_module_load(). */
void title_source_register();
using SceneMaskControlsOpener = void (*)(obs_source_t *source,
                                         const char *layer_id);
/* Optional host UI hook used by the OBS plugin to expose richer scene-mask
 * controls without coupling the shared source implementation to Qt widgets. */
void title_source_set_scene_mask_controls_opener(
    SceneMaskControlsOpener opener);
void release_title_gpu_render_resources();
/* Invalidates every live source presentation at a frontend lifecycle boundary
 * (most importantly scene-collection cleanup/change).  The next video tick
 * rebuilds each source from its current title; video_render refuses to sample
 * a texture from the previous generation in the meantime. */
void title_source_invalidate_all_presentations();
void title_source_begin_scene_collection_transition();
void title_source_begin_shutdown();
void title_source_end_scene_collection_transition();
/* Updates the private editor monitor source without routing every playback
 * frame through obs_data/source_update. Safe to call from the Qt/UI thread;
 * the source video/audio path consumes the atomic snapshot. */
void title_source_set_editor_transport(obs_source_t *source, double time,
                                       bool reverse,
                                       double audio_speed = 1.0);
/* Supplies the private editor monitor source with the current in-memory
 * editor title. This avoids waiting for Save/Live Edit before newly added
 * audio/video layers become audible in the editor preview. */
void title_source_set_editor_title_snapshot(obs_source_t *source,
                                            const std::shared_ptr<Title> &title);
bool title_source_get_audio_levels(obs_source_t *source, float *left,
                                   float *right,
                                   uint64_t *last_update_ns = nullptr);

/* Private-source preview API. These calls never mutate TitleDataStore cue
 * state, never increment cue_revision and never route the snapshot to Program.
 * The source must have been created with editor_transport_controlled=true. */
bool enterPreview(obs_source_t *source, const std::shared_ptr<Title> &title,
                  double preview_time);
void leavePreview(obs_source_t *source);
bool isPreviewReady(obs_source_t *source);

struct TitleGpuRenderSession;

/* Per-consumer render contract. Title coordinates remain in authored logical
 * space; output_width/output_height select the physical render target, while
 * sample_duration_seconds defines temporal sampling (motion blur, video and
 * time-varying raster caches). Zero dimensions retain the title's authored
 * resolution for backwards-compatible callers. */
struct TitleGpuRenderRequest {
    uint32_t output_width = 0;
    uint32_t output_height = 0;
    double sample_duration_seconds = 1.0 / 60.0;
};

struct TitleGpuShaderCompileStatus {
    bool active = false;
    bool queued = false;
    int progress_percent = 100;
    uint32_t completed_jobs = 0;
    uint32_t total_jobs = 0;
    std::string label;
};

/* Development Version 307 correlated render diagnostics. This snapshot is
 * read-only and intentionally contains only scalar/session counters so the
 * editor can log GPU state without exposing render-thread-owned textures. */
struct TitleGpuRenderDiagnostics {
    bool valid = false;
    double session_time = 0.0;
    double last_published_time = 0.0;
    uint64_t update_serial = 0;
    uint64_t render_serial = 0;
    uint64_t publish_serial = 0;
    uint64_t presentation_generation = 0;
    uint64_t model_revision = 0;
    uint64_t published_model_revision = 0;
    bool frame_dirty = false;
    bool last_draw_deferred = false;
    bool state_transaction_pending = false;
    bool has_published_frame = false;
    bool force_compatibility_raster_rebuild = false;
    bool use_submitted_final = false;
    bool use_gpu_cached_final = false;
    bool use_base_frame = false;
    std::size_t raster_count = 0;
    std::size_t pending_raster_count = 0;
    std::size_t gpu_text_raster_count = 0;
    std::size_t gpu_primitive_raster_count = 0;
    std::size_t extrusion_layer_count = 0;
    std::size_t visible_light_layer_count = 0;
    std::size_t hardware_depth_run_count = 0;
    std::size_t hardware_depth_layer_count = 0;
    std::size_t extrusion_pass_count = 0;
    uint32_t requested_output_width = 0;
    uint32_t requested_output_height = 0;
    uint32_t render_target_width = 0;
    uint32_t render_target_height = 0;
    double preview_quality_scale = 1.0;
    bool editor_draft = false;
};

/* Phase 14 asynchronous final-frame readback ticket. The GPU render and the
 * staging copy are submitted together; CPU mapping is deliberately deferred so
 * later GPU frames can execute while an older staging surface completes. */
struct TitleGpuReadbackTicket {
    TitleGpuRenderSession *session = nullptr;
    uint64_t serial = 0;
    QRect region;
    uint32_t canvas_width = 0;
    uint32_t canvas_height = 0;

    bool valid() const { return session != nullptr && serial != 0; }
};

TitleGpuRenderSession *title_gpu_render_session_create();
void title_gpu_render_session_destroy(TitleGpuRenderSession *session);
void title_gpu_render_session_invalidate_presentation(
    TitleGpuRenderSession *session, bool discard_model = true);
void title_gpu_render_session_update(TitleGpuRenderSession *session, const Title &title,
                                     double time, uint64_t model_revision,
                                     bool transform_only_update = false);
void title_gpu_render_session_set_preview_quality(TitleGpuRenderSession *session,
                                                   double scale, bool editor_draft);
void title_gpu_render_session_set_render_request(
    TitleGpuRenderSession *session, const TitleGpuRenderRequest &request);
void title_gpu_render_session_set_realtime_output(
    TitleGpuRenderSession *session, bool realtime_output);
bool title_gpu_render_session_is_realtime_output(
    const TitleGpuRenderSession *session);
void title_gpu_render_session_set_editor_video_decode_client(
    TitleGpuRenderSession *session, bool editor_client);
void title_gpu_render_session_set_transition_input_preview(
    TitleGpuRenderSession *session, bool enabled);
void title_gpu_render_session_set_scene_mask_placeholder_preview(
    TitleGpuRenderSession *session, bool enabled);
void title_gpu_render_session_update_range(TitleGpuRenderSession *session,
                                           const Title &title, double time,
                                           uint64_t model_revision,
                                           std::size_t first_layer,
                                           std::size_t last_layer,
                                           bool transform_only_update = false);
bool title_gpu_render_session_submit_final_frame(TitleGpuRenderSession *session,
                                                 const Title &title,
                                                 const QImage &image,
                                                 uint64_t model_revision);
bool title_gpu_render_session_submit_cached_prefix(
    TitleGpuRenderSession *session, const Title &title, const QImage &cached_prefix,
    double time, std::size_t first_dynamic_layer, uint64_t model_revision);
bool title_gpu_render_session_draw(TitleGpuRenderSession *session,
                                   uint32_t output_width, uint32_t output_height);
bool title_gpu_render_session_draw_transition_inputs(
    TitleGpuRenderSession *session, gs_texture_t *scene_a, gs_texture_t *scene_b,
    uint32_t input_width, uint32_t input_height,
    uint32_t output_width, uint32_t output_height);
/* Destination-aware presentation used by AE-style layer modes. The canvas
 * variant samples a rectangle from an already available background texture;
 * the OBS variant snapshots the current scene render target before the source
 * is drawn. Both execute the layer stack against that real destination. */
bool title_gpu_render_session_draw_over_background_rect(
    TitleGpuRenderSession *session, gs_texture_t *background,
    uint32_t background_width, uint32_t background_height,
    float background_x, float background_y, float background_width_px,
    float background_height_px, uint32_t output_width,
    uint32_t output_height,
    bool isolate_editor_background_from_effects = false);
bool title_gpu_render_session_draw_over_current_target(
    TitleGpuRenderSession *session, uint32_t output_width,
    uint32_t output_height);
std::string title_gpu_render_session_last_error(TitleGpuRenderSession *session);
bool title_gpu_render_session_last_draw_deferred(
    TitleGpuRenderSession *session);
bool title_gpu_render_session_shader_compile_status(
    TitleGpuRenderSession *session, TitleGpuShaderCompileStatus &status);
bool title_gpu_render_session_get_diagnostics(
    TitleGpuRenderSession *session, TitleGpuRenderDiagnostics &diagnostics);
QImage title_gpu_render_session_readback(TitleGpuRenderSession *session);

/* Submit/resolve a final-frame-only triple-buffered readback. The readback is
 * mapped once, then the completed sparse payload is published into the tiled
 * GPU RAM cache. region may be a dirty-tile union; empty means full canvas. */
bool render_title_gpu_cache_submit_readback(
    const Title &title, double time, uint64_t model_revision,
    const std::string &cache_key, const QRect &region,
    TitleGpuReadbackTicket &ticket);
bool title_gpu_render_session_resolve_readback(
    const TitleGpuReadbackTicket &ticket, QImage &image);
void title_gpu_render_session_discard_readback(
    const TitleGpuReadbackTicket &ticket);
void title_gpu_render_session_cancel_readback(
    const TitleGpuReadbackTicket &ticket);

/* Process-wide sparse GPU-resident RAM cache. Frames reference shared 128x128
 * textures; transparent tiles are omitted and identical tiles are deduplicated.
 * The token is CacheFrameKey::toString(). Source/editor playback submits it
 * directly without reconstructing a full CPU frame. */
bool title_gpu_frame_cache_contains(const std::string &cache_key);
bool title_gpu_frame_cache_alias(
    const std::string &cache_key,
    const std::string &canonical_cache_key);
bool title_gpu_frame_cache_store_image(
    const std::string &cache_key, const QImage &sparse_image,
    uint32_t canvas_width, uint32_t canvas_height);
void title_gpu_frame_cache_remove(const std::string &cache_key);
void title_gpu_frame_cache_remove_title(const std::string &title_id);
void title_gpu_frame_cache_clear();
void title_gpu_frame_cache_set_budget(uint64_t bytes);
uint64_t title_gpu_frame_cache_bytes_used();
bool title_gpu_render_session_submit_gpu_cached_frame(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, uint64_t model_revision);
bool title_gpu_render_session_submit_gpu_cached_prefix(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, double time,
    std::size_t first_dynamic_layer, uint64_t model_revision);

/* Source settings keys */
#define PROP_TITLE_ID      "title_id"
#define PROP_PROJECT_PATH  "project_path"
#define PROP_AUTO_ADVANCE  "auto_advance"
#define PROP_CUE_FIRST_ROW_WHEN_ACTIVE "cue_first_row_when_active"
#define PROP_SCENE_MASKS_GROUP "scene_masks"
#define PROP_SCENE_MASK_PREFIX "scene_mask_"
#define PROP_SCENE_MASK_CLIP_TO_BOUNDS "scene_masks_clip_to_bounds"
