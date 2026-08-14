#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <QImage>
#include <QRect>

struct Title;
struct TitleGpuRenderSession;

struct TitleGpuRenderRequest {
    std::uint32_t output_width = 0;
    std::uint32_t output_height = 0;
    double sample_duration_seconds = 1.0 / 60.0;
};

struct TitleGpuShaderCompileStatus {
    bool active = false;
    bool queued = false;
    int progress_percent = 100;
    std::uint32_t completed_jobs = 0;
    std::uint32_t total_jobs = 0;
    std::string label;
};

struct TitleGpuRenderDiagnostics {
    bool valid = false;
    double session_time = 0.0;
    double last_published_time = 0.0;
    std::uint64_t update_serial = 0;
    std::uint64_t render_serial = 0;
    std::uint64_t publish_serial = 0;
    std::uint64_t presentation_generation = 0;
    std::uint64_t model_revision = 0;
    std::uint64_t published_model_revision = 0;
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
};

struct TitleGpuReadbackTicket {
    TitleGpuRenderSession *session = nullptr;
    std::uint64_t serial = 0;
    QRect region;
    std::uint32_t canvas_width = 0;
    std::uint32_t canvas_height = 0;

    bool valid() const { return session != nullptr && serial != 0; }
};

TitleGpuRenderSession *title_gpu_render_session_create();
void title_gpu_render_session_destroy(TitleGpuRenderSession *session);
void title_gpu_render_session_invalidate_presentation(
    TitleGpuRenderSession *session, bool discard_model = true);
void title_gpu_render_session_update(
    TitleGpuRenderSession *session, const Title &title, double time,
    std::uint64_t model_revision, bool transform_only_update = false);
void title_gpu_render_session_update_range(
    TitleGpuRenderSession *session, const Title &title, double time,
    std::uint64_t model_revision, std::size_t first_layer,
    std::size_t last_layer, bool transform_only_update = false);
void title_gpu_render_session_set_preview_quality(
    TitleGpuRenderSession *session, double scale, bool editor_draft);
void title_gpu_render_session_set_render_request(
    TitleGpuRenderSession *session, const TitleGpuRenderRequest &request);
void title_gpu_render_session_set_editor_video_decode_client(
    TitleGpuRenderSession *session, bool editor_client);
void title_gpu_render_session_set_transition_input_preview(
    TitleGpuRenderSession *session, bool enabled);
void title_gpu_render_session_set_scene_mask_placeholder_preview(
    TitleGpuRenderSession *session, bool enabled);
bool title_gpu_render_session_submit_final_frame(
    TitleGpuRenderSession *session, const Title &title, const QImage &image,
    std::uint64_t model_revision);
bool title_gpu_render_session_submit_cached_prefix(
    TitleGpuRenderSession *session, const Title &title,
    const QImage &cached_prefix, double time,
    std::size_t first_dynamic_layer, std::uint64_t model_revision);
bool title_gpu_render_session_submit_gpu_cached_frame(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, std::uint64_t model_revision);
bool title_gpu_render_session_submit_gpu_cached_prefix(
    TitleGpuRenderSession *session, const Title &title,
    const std::string &cache_key, double time,
    std::size_t first_dynamic_layer, std::uint64_t model_revision);
std::string title_gpu_render_session_last_error(
    TitleGpuRenderSession *session);
bool title_gpu_render_session_last_draw_deferred(
    TitleGpuRenderSession *session);
bool title_gpu_render_session_shader_compile_status(
    TitleGpuRenderSession *session, TitleGpuShaderCompileStatus &status);
bool title_gpu_render_session_get_diagnostics(
    TitleGpuRenderSession *session, TitleGpuRenderDiagnostics &diagnostics);
QImage title_gpu_render_session_readback(TitleGpuRenderSession *session);
bool render_title_gpu_cache_submit_readback(
    const Title &title, double time, std::uint64_t model_revision,
    const std::string &cache_key, const QRect &region,
    TitleGpuReadbackTicket &ticket);
bool title_gpu_render_session_resolve_readback(
    const TitleGpuReadbackTicket &ticket, QImage &image);
void title_gpu_render_session_discard_readback(
    const TitleGpuReadbackTicket &ticket);
void title_gpu_render_session_cancel_readback(
    const TitleGpuReadbackTicket &ticket);

bool title_gpu_frame_cache_contains(const std::string &cache_key);
bool title_gpu_frame_cache_alias(const std::string &cache_key,
                                 const std::string &canonical_cache_key);
bool title_gpu_frame_cache_store_image(
    const std::string &cache_key, const QImage &sparse_image,
    std::uint32_t canvas_width, std::uint32_t canvas_height);
void title_gpu_frame_cache_remove(const std::string &cache_key);
void title_gpu_frame_cache_remove_title(const std::string &title_id);
void title_gpu_frame_cache_clear();
void title_gpu_frame_cache_set_budget(std::uint64_t bytes);
std::uint64_t title_gpu_frame_cache_bytes_used();
