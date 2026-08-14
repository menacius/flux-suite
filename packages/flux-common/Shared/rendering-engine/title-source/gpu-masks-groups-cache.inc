    enum class PresentationTargetState : uint8_t {
        Free = 0,
        Rendering,
        Complete,
        Published,
    };

    std::mutex mutex;
    std::atomic<bool> destroying {false};
    /* Prefix submission is a two-stage CPU transaction (update dynamic range,
     * then attach the cached base). Draw/readback must never observe the
     * intermediate range-only state. */
    std::atomic<bool> state_transaction_pending {false};
    /* The primitive shader is optional. A compile failure permanently routes
     * subsequent shape updates through the stable CPU base-raster generator
     * instead of leaving the first frame blank or retrying forever. */
    bool primitive_backend_unavailable = false;
    bool text_backend_unavailable = false;
    /* Optional GPU raster failures must schedule a compatibility rebuild
     * instead of blocking first-frame publication forever. */
    bool force_compatibility_raster_rebuild = false;
    Title title;
    bool has_title = false;
    double time = 0.0;
    uint64_t model_revision = std::numeric_limits<uint64_t>::max();
    std::size_t first_layer = 0;
    std::size_t last_layer = std::numeric_limits<std::size_t>::max();
    bool frame_dirty = true;
    /* Correlation counters used by Development Version 307 diagnostics. They
     * never participate in rendering or cache identity. */
    uint64_t diagnostics_update_serial = 0;
    uint64_t diagnostics_render_serial = 0;
    uint64_t diagnostics_publish_serial = 0;
    double diagnostics_last_published_time = 0.0;
    std::size_t diagnostics_hardware_depth_run_count = 0;
    std::size_t diagnostics_hardware_depth_layer_count = 0;
    std::size_t diagnostics_extrusion_pass_count = 0;
    std::unordered_map<std::string, LayerRaster> layers;
    /* Scene-mask OBS scenes are resolved synchronously for source presentation
     * and converted into transform-neutral layer rasters. Once registered
     * here they use the ordinary layer, Z, light and shadow pipeline. */
    std::unordered_map<std::string, RuntimeSceneMaskRaster>
        runtime_scene_mask_rasters;
    gs_effect_t *runtime_scene_mask_effect = nullptr;
    uint64_t runtime_scene_mask_generation = 0;
    /* Prevent source-aware effects from recursively evaluating dependency
     * cycles (for example A displaces B while B displaces A). */
    std::unordered_set<std::string> source_effect_visiting;

    gs_texrender_t *frame_a = nullptr;
    gs_texrender_t *frame_b = nullptr;
    /* Shared color + Z24/S8 target for contiguous compatible 3D runs. Opaque
     * planes establish depth first and transparent planes consume that depth
     * in a deterministic far-to-near pass. It is kept
     * separate from the ping/pong compositor because masks and destination
     * blend modes intentionally remain texture-based. */
    gs_texrender_t *depth_target = nullptr;
    bool depth_target_unavailable = false;
    /* Development Version 375: one portable shadow texture per GPU light
     * slot. Spot/Parallel lights use a conventional 2D map; Point lights use
     * a six-face 3x2 atlas in the same texture, avoiding cube-sampler and
     * texture-unit portability limits across OBS graphics backends. */
    std::array<gs_texrender_t *, 4> shadow_targets {};
    /* A close Point light can make one wide text quad span several cube faces.
     * The reusable UV grid keeps face clipping local to small cells instead of
     * clipping two canvas-sized triangles across W=0, which produced the
     * triangular gaps reported below roughly 300 px of caster distance. */
    gs_vertbuffer_t *close_point_text_shadow_grid = nullptr;
    std::array<bool, 4> shadow_target_unavailable {};
    /* R32F is the preferred numeric depth surface. Some OBS graphics
     * backends/drivers expose the enum but cannot begin an R32F texrender with
     * a Z24/S8 attachment. In that case retain floating-point depth in the
     * broadly supported RGBA16F target instead of silently disabling every
     * shadow for the lifetime of the session. */
    std::array<bool, 4> shadow_target_uses_rgba16f {};
    /* Tracks the preference value that created the current shadow-map targets.
     * If a driver rejects a large target and the user lowers Preferences >
     * Advanced > Shadowmap Size, previously disabled shadow slots are retried
     * instead of remaining unavailable for the lifetime of the session. */
    uint32_t shadow_map_preference_size = 0u;
    std::array<bool, 4> shadow_failure_logged {};
    std::array<bool, 4> shadow_success_logged {};
    std::array<bool, 4> shadow_receiver_binding_logged {};
    std::array<bool, 4> shadow_map_valid {};
    std::array<QMatrix4x4, 4> shadow_view_projections {};
    std::array<float, 4> shadow_darkness {};
    std::array<float, 4> shadow_softness {};
    std::array<float, 4> shadow_bias {0.002f, 0.002f, 0.002f, 0.002f};
    std::array<uint32_t, 4> shadow_map_width {1u, 1u, 1u, 1u};
    std::array<uint32_t, 4> shadow_map_height {1u, 1u, 1u, 1u};
    /* 0 none, 1 perspective/Spot, 2 Point cube atlas,
     * 3 orthographic/Parallel. */
    std::array<int, 4> shadow_map_kind {};
    std::array<QVector3D, 4> shadow_light_positions {};
    /* Spot/Parallel maps use an explicit light-space basis rather than a Qt
     * projection matrix in the shader. This keeps writer and receiver math
     * identical on D3D11/OpenGL/Metal and avoids backend matrix-layout or clip
     * convention mismatches. */
    std::array<QVector3D, 4> shadow_light_rights {};
    std::array<QVector3D, 4> shadow_light_ups {};
    std::array<QVector3D, 4> shadow_light_forwards {};
    std::array<float, 4> shadow_projection_center_x {};
    std::array<float, 4> shadow_projection_center_y {};
    std::array<float, 4> shadow_near_plane {0.01f, 0.01f, 0.01f, 0.01f};
    std::array<float, 4> shadow_far_plane {1.0f, 1.0f, 1.0f, 1.0f};
    /* Perspective entries store 2*tan(fov/2); orthographic entries store
     * the world-space projection span. Both let PCSS convert Source Size to
     * a receiver-space search/filter radius without baking one distance. */
    std::array<float, 4> shadow_projection_span_x {1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, 4> shadow_projection_span_y {1.0f, 1.0f, 1.0f, 1.0f};
    /* The receiver reuses the exact matrices that wrote each Point-atlas
     * face. This avoids backend-dependent Y/origin assumptions in manual
     * cube-face UV reconstruction. */
    std::array<std::array<QMatrix4x4, 6>, 4>
        shadow_point_face_view_projections {};
    gs_texrender_t *presentation_targets[2] = {nullptr, nullptr};
    PresentationTargetState presentation_target_states[2] = {
        PresentationTargetState::Free,
        PresentationTargetState::Free,
    };
    uint64_t presentation_target_generations[2] = {0, 0};
    uint64_t next_presentation_generation = 0;
    uint64_t published_presentation_generation = 0;
    int active_presentation_target = -1;
    gs_texrender_t *layer_target = nullptr;
    gs_texrender_t *mask_target = nullptr;
    std::unique_ptr<fxm::rendering::IRenderTarget>
        masked_target_resource;
    gs_texrender_t *masked_target = nullptr;
    std::unique_ptr<fxm::rendering::IRenderTarget>
        adjustment_coverage_target_resource;
    gs_texrender_t *adjustment_coverage_target = nullptr;
    gs_texrender_t *effect_a = nullptr;
    gs_texrender_t *effect_b = nullptr;
    gs_texrender_t *blur_a = nullptr;
    gs_texrender_t *blur_b = nullptr;
    /* Complete effected/transitioned shutter samples use one isolated sample
     * target and one normalized in-place accumulation target. The former
     * ping-pong spare was removed in Development Version 288. */
    gs_texrender_t *motion_sample_target = nullptr;
    gs_texrender_t *motion_accum_target = nullptr;
    /* Source-aware effects can recursively render another layer while an outer
     * Motion Blur exposure is still being accumulated. Nested passes use
     * isolated temporary targets instead of clearing the outer accumulation. */
    int motion_temporal_depth = 0;
    /* Immediate readback remains for compatibility callers. Phase 14 prerender
     * uses the independent triple-buffered slots below. */
    gs_stagesurf_t *stage = nullptr;
    std::array<AsyncReadbackSlot, 3> readback_slots;
    uint64_t next_readback_serial = 0;
    std::size_t next_readback_slot = 0;
    gs_texture_t *submitted_final_texture = nullptr;
    QImage pending_submitted_final;
    QRect submitted_final_rect;
    bool submitted_final_pending = false;
    bool use_submitted_final = false;
    bool use_gpu_cached_final = false;
    std::string submitted_gpu_cache_key;
    qint64 submitted_final_image_key = 0;
    uint32_t submitted_final_width = 0;
    uint32_t submitted_final_height = 0;
    uint64_t submitted_final_serial = 0;
    uint64_t uploaded_final_serial = 0;
    uint64_t published_final_serial = 0;
    uint64_t draw_serial = 0;
    bool lens_flare_pass_logged = false;

    gs_texture_t *base_frame_texture = nullptr;
    QImage pending_base_frame;
    QRect base_frame_rect;
    bool base_frame_pending = false;
    bool use_base_frame = false;
    bool use_gpu_cached_base = false;
    std::string base_gpu_cache_key;
    qint64 base_frame_image_key = 0;
    uint32_t base_frame_width = 0;
    uint32_t base_frame_height = 0;

    /* GPU-resident RAM-cache presentation pool. QImage remains the disk/cache
     * transport payload, but once a payload reaches a render session its
     * texture is retained and reused when playback revisits that frame. */
    std::unordered_map<qint64, CachedFrameTexture> cached_frame_textures;
    quint64 cached_frame_texture_bytes = 0;
    /* Compatibility-only QImage upload cache. Normal playback uses the
     * process-wide sparse GPU tile cache; keep this fallback deliberately
     * small so each source/editor session cannot reserve hundreds of MiB. */
    quint64 cached_frame_texture_budget = 32ull * 1024ull * 1024ull;
    uint64_t cached_frame_texture_tick = 0;

    /* Phase 13: transformed/effected track mattes are retained as full-canvas
     * GPU textures.  The cache is shared by alpha/luma/clipping variants because the
     * mode conversion happens in the final mask shader, not in the source
     * matte render.  Double buffering prevents a cache refresh from clearing a
     * texture that is still sampled by the currently published frame. */
    std::unordered_map<std::string, MaskTextureCacheEntry> mask_texture_cache;
    uint64_t mask_texture_cache_tick = 0;
    quint64 mask_texture_cache_bytes = 0;
    uint64_t effect_cache_tick = 0;
    quint64 effect_cache_bytes = 0;
    std::unordered_map<std::string, GroupRenderDiagnostics> group_render_diagnostics;
    std::unordered_map<std::string, TransitionMatteTexture> transition_matte_textures;
    uint64_t transition_matte_cache_tick = 0;
    quint64 transition_matte_cache_bytes = 0;

    /* Keep presentation/cache blits isolated from layer-copy uniforms.
     * Reusing copy_effect here leaked the last layer's wipe/crop state into the
     * whole frame and could make Preview/Program output disappear. */
    gs_effect_t *blit_effect = nullptr;
    gs_effect_t *copy_effect = nullptr;
    gs_effect_t *shadow_effect = nullptr;
    gs_effect_t *motion_composite_effect = nullptr;
    gs_effect_t *adjustment_mix_effect = nullptr;
    gs_effect_t *adjustment_coverage_effect = nullptr;
    gs_effect_t *primitive_shape_effect = nullptr;
    std::unique_ptr<fxm::gpu_text::Renderer> text_renderer;
    gs_effect_t *blend_effect = nullptr;
    gs_effect_t *mask_effect = nullptr;
    gs_effect_t *external_background_effect = nullptr;
    std::unique_ptr<fxm::rendering::IRenderTarget>
        external_background_local;
    std::unique_ptr<fxm::rendering::ITexture>
        external_background_snapshot_resource;
    gs_texture_t *external_background_snapshot = nullptr;
    uint32_t external_background_snapshot_width = 0;
    uint32_t external_background_snapshot_height = 0;
    enum gs_color_format external_background_snapshot_format = GS_UNKNOWN;
    std::unique_ptr<TitleEffectRegistry> effect_registry;
    /* Borrowed OBS transition textures. They are valid only during the
     * synchronous obs_transition_video_render callback and are never owned or
     * cached by FXM. */
    gs_texture_t *transition_input_textures[2] = {nullptr, nullptr};
    uint32_t transition_input_width = 0;
    uint32_t transition_input_height = 0;
    uint64_t transition_input_generation = 0;
    bool transition_input_preview_enabled = false;
    /* Editor-only affordance. When enabled, scene-mask layers are rasterized
     * as A/B-style colored grid placeholders so their effect stack can be
     * previewed. Runtime/source scene-mask matte rasters keep this disabled
     * and render the real layer artwork only. */
    bool scene_mask_placeholder_preview_enabled = false;
    gs_texture_t *final_texture = nullptr;
    /* A published texture is valid only for the exact model that produced it.
     * Keeping this identity beside the pointer makes cross-title/project
     * retention impossible even if a caller forgets an outer lifecycle reset. */
    std::string published_title_id;
    uint64_t published_model_revision =
        std::numeric_limits<uint64_t>::max();
    uint32_t width = 0;
    uint32_t height = 0;
    /* Physical target and temporal cadence belong to the consumer, not to the
     * authored title. A zero target falls back to the logical title extent. */
    uint32_t requested_output_width = 0;
    uint32_t requested_output_height = 0;
    double sample_duration_seconds = 1.0 / 60.0;
    uint32_t stage_width = 0;
    uint32_t stage_height = 0;
    float preview_quality_scale = 1.0f;
    bool editor_draft = false;
    /* Set only for the display-rate canvas transform path. Reuse the last
     * complete shadow maps during the gesture; the settled model update
     * rebuilds exact shadows once without blocking overlay presentation. */
    bool interactive_transform_update = false;
    /* Live OBS sources must stay inside the graphics-thread frame budget.
     * Editor/cache sessions keep their authored/offline quality contract. */
    std::atomic<bool> realtime_output { false };
    /* Development Version 388: shader/effect compilation is never performed
     * inline inside frame presentation. Missing shaders are queued for the
     * session compile worker and the canvas/source keeps the previous stable
     * texture visible while the worker progresses. */
    std::mutex shader_compile_mutex;
    std::condition_variable shader_compile_cv;
    std::thread shader_compile_thread;
    std::deque<ShaderCompileJob> shader_compile_queue;
    std::unordered_set<std::string> shader_compile_queued_ids;
    bool shader_compile_stop = false;
    bool shader_compile_active = false;
    std::string shader_compile_active_id;
    std::string shader_compile_label;
    uint32_t shader_compile_completed_jobs = 0;
    uint32_t shader_compile_total_jobs = 0;
    int shader_compile_progress_percent = 100;
    fxm::video::VideoDecodeClient video_decode_client =
        fxm::video::VideoDecodeClient::LiveOutput;
    std::string last_error_storage;
    const char *last_error = nullptr;
    /* True when the latest draw deliberately presented the last stable texture
     * because a required replacement raster was not ready. This is separate
     * from draw success: callers may keep the old texture visible while the
     * editor immediately schedules a full compatibility rebuild. */
    bool last_draw_deferred = false;
};

static void resolve_gpu_session_render_dimensions(
    const TitleGpuRenderSession *session, uint32_t &width, uint32_t &height)
{
    if (!session) {
        width = 1;
        height = 1;
        return;
    }
    const uint32_t base_width = session->requested_output_width > 0
        ? session->requested_output_width
        : clamped_source_dimension(session->title.width);
    const uint32_t base_height = session->requested_output_height > 0
        ? session->requested_output_height
        : clamped_source_dimension(session->title.height);
    const double quality_scale = session->editor_draft
        ? std::clamp(static_cast<double>(session->preview_quality_scale),
                     0.25, 1.0)
        : 1.0;
    width = clamped_source_dimension(std::max(
        1, static_cast<int>(std::ceil(base_width * quality_scale))));
    height = clamped_source_dimension(std::max(
        1, static_cast<int>(std::ceil(base_height * quality_scale))));
}

static double gpu_session_render_scale_x(const TitleGpuRenderSession *session)
{
    if (!session)
        return 1.0;
    uint32_t width = 1;
    uint32_t height = 1;
    resolve_gpu_session_render_dimensions(session, width, height);
    return static_cast<double>(width) /
        std::max(1.0, static_cast<double>(session->title.width));
}

static double gpu_session_render_scale_y(const TitleGpuRenderSession *session)
{
    if (!session)
        return 1.0;
    uint32_t width = 1;
    uint32_t height = 1;
    resolve_gpu_session_render_dimensions(session, width, height);
    return static_cast<double>(height) /
        std::max(1.0, static_cast<double>(session->title.height));
}

static double gpu_session_raster_scale(const TitleGpuRenderSession *session)
{
    return std::max(gpu_session_render_scale_x(session),
                    gpu_session_render_scale_y(session));
}


static int shader_compile_progress_locked(const TitleGpuRenderSession *session)
{
    if (!session)
        return 100;
    if (session->shader_compile_total_jobs == 0)
        return 100;
    const uint32_t completed = std::min(session->shader_compile_completed_jobs,
                                        session->shader_compile_total_jobs);
    const int base = static_cast<int>(std::floor(
        (static_cast<double>(completed) * 100.0) /
        std::max<uint32_t>(1, session->shader_compile_total_jobs)));
    return std::clamp(session->shader_compile_active ? std::max(1, base) : base,
                      0, 100);
}

static void mark_shader_compile_pending(TitleGpuRenderSession *session,
                                        const char *label)
{
    if (!session)
        return;
    session->last_draw_deferred = true;
    session->last_error_storage = std::string("Shader compile pending: ") +
        (label && *label ? label : "GPU effect");
    session->last_error = session->last_error_storage.c_str();
}

static void start_shader_compile_worker_if_needed(TitleGpuRenderSession *session);

static bool queue_shader_compile_job(TitleGpuRenderSession *session,
                                     const std::string &id,
                                     const std::string &label,
                                     std::function<bool()> run)
{
    if (!session || id.empty() || !run)
        return false;
    start_shader_compile_worker_if_needed(session);
    std::lock_guard<std::mutex> lock(session->shader_compile_mutex);
    if (session->shader_compile_stop)
        return false;
    if (session->shader_compile_active_id == id ||
        session->shader_compile_queued_ids.find(id) !=
            session->shader_compile_queued_ids.end()) {
        return true;
    }
    if (!session->shader_compile_active && session->shader_compile_queue.empty() &&
        session->shader_compile_completed_jobs >= session->shader_compile_total_jobs) {
        session->shader_compile_completed_jobs = 0;
        session->shader_compile_total_jobs = 0;
    }
    session->shader_compile_queue.push_back(
        TitleGpuRenderSession::ShaderCompileJob{id, label, std::move(run)});
    session->shader_compile_queued_ids.insert(id);
    ++session->shader_compile_total_jobs;
    session->shader_compile_progress_percent = shader_compile_progress_locked(session);
    session->shader_compile_cv.notify_one();
    FXM_LOG_INFO("GpuPipeline", QStringLiteral(
        "stage=shader-compile-queue session=%1 id=%2 label=%3 queued=%4 total=%5 progress=%6")
        .arg(reinterpret_cast<quintptr>(session), 0, 16)
        .arg(QString::fromStdString(id))
        .arg(QString::fromStdString(label))
        .arg(static_cast<qulonglong>(session->shader_compile_queue.size()))
        .arg(static_cast<qulonglong>(session->shader_compile_total_jobs))
        .arg(session->shader_compile_progress_percent));
    return true;
}

static void shader_compile_worker_loop(TitleGpuRenderSession *session)
{
    if (!session)
        return;
    for (;;) {
        TitleGpuRenderSession::ShaderCompileJob job;
        {
            std::unique_lock<std::mutex> lock(session->shader_compile_mutex);
            session->shader_compile_cv.wait(lock, [&]() {
                return session->shader_compile_stop ||
                       !session->shader_compile_queue.empty();
            });
            if (session->shader_compile_stop)
                break;
            job = std::move(session->shader_compile_queue.front());
            session->shader_compile_queue.pop_front();
            session->shader_compile_queued_ids.erase(job.id);
            session->shader_compile_active = true;
            session->shader_compile_active_id = job.id;
            session->shader_compile_label = job.label;
            session->shader_compile_progress_percent = shader_compile_progress_locked(session);
        }

        const auto started = std::chrono::steady_clock::now();
        bool ok = false;
        obs_enter_graphics();
        try {
            ok = job.run();
        } catch (...) {
            ok = false;
        }
        obs_leave_graphics();
        const auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();

        {
            std::lock_guard<std::mutex> lock(session->shader_compile_mutex);
            ++session->shader_compile_completed_jobs;
            session->shader_compile_active = false;
            session->shader_compile_active_id.clear();
            session->shader_compile_label.clear();
            session->shader_compile_progress_percent = shader_compile_progress_locked(session);
        }
        {
            std::lock_guard<std::mutex> lock(session->mutex);
            if (!session->destroying.load(std::memory_order_acquire)) {
                session->frame_dirty = true;
                session->last_draw_deferred = false;
                if (ok) {
                    session->last_error = nullptr;
                    session->last_error_storage.clear();
                }
            }
        }
        FXM_LOG_INFO("GpuPipeline", QStringLiteral(
            "stage=shader-compile-complete session=%1 id=%2 label=%3 result=%4 elapsedUs=%5")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(QString::fromStdString(job.id))
            .arg(QString::fromStdString(job.label))
            .arg(ok ? QStringLiteral("ok") : QStringLiteral("failed"))
            .arg(static_cast<qlonglong>(elapsed_us)));
    }
}

static void start_shader_compile_worker_if_needed(TitleGpuRenderSession *session)
{
    if (!session)
        return;
    std::lock_guard<std::mutex> lock(session->shader_compile_mutex);
    if (session->shader_compile_thread.joinable())
        return;
    session->shader_compile_stop = false;
    session->shader_compile_thread = std::thread([session]() {
        shader_compile_worker_loop(session);
    });
}

static void stop_shader_compile_worker(TitleGpuRenderSession *session)
{
    if (!session)
        return;
    {
        std::lock_guard<std::mutex> lock(session->shader_compile_mutex);
        session->shader_compile_stop = true;
        session->shader_compile_cv.notify_all();
    }
    if (session->shader_compile_thread.joinable())
        session->shader_compile_thread.join();
}

static bool ensure_session_shader_effect(
    TitleGpuRenderSession *session, gs_effect_t *current,
    const std::string &id, const char *label,
    const char *source, const char *filename,
    gs_effect_t **destination)
{
    if (!session)
        return false;
    if (current || (destination && *destination))
        return true;
    const std::string human_label = label && *label ? label : id;
    queue_shader_compile_job(session, id, human_label,
        [session, source, filename, destination, id, human_label]() -> bool {
            gs_effect_t *compiled = gs_effect_create(source, filename, nullptr);
            std::lock_guard<std::mutex> lock(session->mutex);
            if (session->destroying.load(std::memory_order_acquire)) {
                if (compiled)
                    gs_effect_destroy(compiled);
                return false;
            }
            if (destination && !*destination)
                *destination = compiled;
            else if (compiled)
                gs_effect_destroy(compiled);
            const bool ok = destination && *destination;
            if (!ok) {
                session->last_error_storage = "Could not compile shader: " +
                    human_label;
                session->last_error = session->last_error_storage.c_str();
            }
            return ok;
        });
    mark_shader_compile_pending(session, human_label.c_str());
    return false;
}

static const char *session_effect_compile_label(LayerEffectType type)
{
    const TitleEffectDefinition *definition =
        TitleEffectRegistry::definition(type);
    if (definition && definition->display_name && *definition->display_name)
        return definition->display_name;
    if (definition && definition->stable_id && *definition->stable_id)
        return definition->stable_id;
    return "GPU effect";
}

static gs_effect_t *session_registry_effect_or_queue(
    TitleGpuRenderSession *session, LayerEffectType type,
    const char *label)
{
    if (!session)
        return nullptr;
    /* Contract: effect lookup runs only from the GPU compositor while the
     * caller already owns session->mutex (render_gpu_session_locked()).  The
     * Dev388 async-compile conversion added a second non-recursive lock here;
     * an active transition that first touched a registry effect therefore
     * threw std::system_error(EDEADLK) on every frame.  The registry has its
     * own recursive mutex for find()/compile(), so do not reacquire the session
     * mutex in this locked render helper. */
    if (!session->effect_registry)
        session->effect_registry = std::make_unique<TitleEffectRegistry>();
    const std::string id = std::string("registry:") + std::to_string((int)type);
    const std::string human_label = label && *label ? label : id;
    gs_effect_t *compiled = session->effect_registry->find(type);
    if (compiled)
        return compiled;
    queue_shader_compile_job(session, id, human_label,
        [session, type, human_label]() -> bool {
            std::unique_ptr<TitleEffectRegistry> *registry = nullptr;
            {
                std::lock_guard<std::mutex> lock(session->mutex);
                if (session->destroying.load(std::memory_order_acquire))
                    return false;
                if (!session->effect_registry)
                    session->effect_registry = std::make_unique<TitleEffectRegistry>();
                registry = &session->effect_registry;
            }
            gs_effect_t *effect = (*registry)->compile(type);
            if (!effect) {
                std::lock_guard<std::mutex> lock(session->mutex);
                session->last_error_storage = "Could not compile effect shader: " +
                    human_label;
                session->last_error = session->last_error_storage.c_str();
            }
            return effect != nullptr;
        });
    mark_shader_compile_pending(session, human_label.c_str());
    return nullptr;
}

static gs_effect_t *session_registry_effect_or_queue(
    TitleGpuRenderSession *session, const std::string &stable_id,
    const char *label)
{
    if (!session || stable_id.empty())
        return nullptr;
    /* Same locked-render contract as the built-in overload above. */
    if (!session->effect_registry)
        session->effect_registry = std::make_unique<TitleEffectRegistry>();
    const std::string id = std::string("registry:") + stable_id;
    const std::string human_label = label && *label ? label : stable_id;
    gs_effect_t *compiled = session->effect_registry->find(stable_id);
    if (compiled)
        return compiled;
    queue_shader_compile_job(session, id, human_label,
        [session, stable_id, human_label]() -> bool {
            std::unique_ptr<TitleEffectRegistry> *registry = nullptr;
            {
                std::lock_guard<std::mutex> lock(session->mutex);
                if (session->destroying.load(std::memory_order_acquire))
                    return false;
                if (!session->effect_registry)
                    session->effect_registry = std::make_unique<TitleEffectRegistry>();
                registry = &session->effect_registry;
            }
            gs_effect_t *effect = (*registry)->compile(stable_id);
            if (!effect) {
                std::lock_guard<std::mutex> lock(session->mutex);
                session->last_error_storage = "Could not compile extension shader: " +
                    human_label;
                session->last_error = session->last_error_storage.c_str();
            }
            return effect != nullptr;
        });
    mark_shader_compile_pending(session, human_label.c_str());
    return nullptr;
}

/* Phase 14 RAM tier, corrected to use sparse content-addressed GPU tiles.
 * A full-canvas render target per frame makes a 1920x1080 frame cost ~8 MiB
 * even when the title contains only a small logo or line of text. Frames now
 * reference shared 128x128 tile textures; transparent tiles are omitted and
 * identical pixels across animation frames are uploaded only once. The live
 * and editor paths still present directly from GPU textures. */
static constexpr int kGpuRamTileSize = 128;

struct GpuRamTileEntry {
    std::unique_ptr<fxm::rendering::ITexture> texture_resource;
    gs_texture_t *texture = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t bytes = 0;
    uint64_t references = 0;
    uint64_t last_used = 0;
};

struct GpuRamTileRef {
    std::string digest;
    QRect destination;
};

struct GpuRamFrameEntry {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<GpuRamTileRef> tiles;
    uint64_t metadata_bytes = 0;
    uint64_t last_used = 0;
};

static std::mutex g_gpu_frame_cache_mutex;
static std::unordered_map<std::string, GpuRamFrameEntry> g_gpu_frame_cache;
static std::unordered_map<std::string, GpuRamTileEntry> g_gpu_ram_tiles;
static uint64_t g_gpu_frame_cache_bytes = 0;
static uint64_t g_gpu_frame_cache_budget = 512ull * 1024ull * 1024ull;
static uint64_t g_gpu_frame_cache_tick = 0;

static std::string gpu_session_layer_id(const Layer &layer)
{
    return layer.id.empty()
        ? std::string("__gpu_layer_") + std::to_string((uintptr_t)&layer)
        : layer.id;
}

static TitleGpuRenderSession::LayerRaster *gpu_session_layer_raster(
    TitleGpuRenderSession *session, const Layer &layer)
{
    if (!session)
        return nullptr;
    if (layer.use_as_scene_mask) {
        const auto runtime =
            session->runtime_scene_mask_rasters.find(layer.id);
        if (runtime != session->runtime_scene_mask_rasters.end() &&
            runtime->second.raster.texture)
            return &runtime->second.raster;
    }
    const auto found = session->layers.find(gpu_session_layer_id(layer));
    return found == session->layers.end() ? nullptr : &found->second;
}

static ResolvedLayerEffect resolve_gpu_layer_effect(const LayerEffect &effect, double t)
{
    return resolve_layer_effect(effect, t);
}

static bool video_effects_can_use_direct_gpu_base_raster(const Layer &layer, double title_time)
{
    if (layer.type != LayerType::Video)
        return false;
    const double t = std::max(0.0, title_time - layer.in_time);
    for (const LayerEffect &effect : layer.effects) {
        if (!eval_effect_enabled(effect, t))
            continue;
        switch (effect.type) {
        case LayerEffectType::BrightnessContrast:
        case LayerEffectType::Saturation:
        case LayerEffectType::ColorOverlay:
        case LayerEffectType::Vignette:
        case LayerEffectType::Noise:
        case LayerEffectType::Grain:
        case LayerEffectType::FilmDistortion:
        case LayerEffectType::AnalogDistortion:
        case LayerEffectType::DigitalDistortion:
        case LayerEffectType::Sharpen:
        case LayerEffectType::UnsharpMask:
        case LayerEffectType::HighPass:
        case LayerEffectType::Clarity:
        case LayerEffectType::BilateralSharpen:
        case LayerEffectType::LensDistortion:
        case LayerEffectType::ChromaticAberration:
        case LayerEffectType::Ripple:
        case LayerEffectType::WaveWarp:
        case LayerEffectType::Pixelate:
        case LayerEffectType::EdgeDetect:
        case LayerEffectType::Posterize:
        case LayerEffectType::Threshold:
        case LayerEffectType::Scanlines:
        case LayerEffectType::ChromaKey:
        case LayerEffectType::LumaKey:
        case LayerEffectType::ColorRange:
        case LayerEffectType::SpillSuppression:
        case LayerEffectType::MatteChoker:
        case LayerEffectType::DisplacementMap:
            break;
        default:
            /* Expanding effects such as Blur/Glow/Drop Shadow/Light Wrap still
             * need the padded compatibility surface so their tails are not
             * clipped. */
            return false;
        }
    }
    return true;
}

static bool layer_can_use_direct_gpu_image_raster(const Layer &layer,
                                                   double title_time,
                                                   int transition_blur_padding)
{
    if (layer.type == LayerType::Image) {
        if (effective_external_string(layer, ExternalPropertyPaths::ImagePath, layer.image_path).empty())
            return false;
    } else if (layer.type == LayerType::Video) {
        if (layer.video_source.empty())
            return false;
    } else {
        return false;
    }
    if (transition_blur_padding > 0)
        return false;

    const double t = std::max(0.0, title_time - layer.in_time);
    const bool grouped_or_parented = !layer.parent_id.empty() ||
                                     !layer.transform_parent_id.empty();
    if (grouped_or_parented) {
        const bool has_color_overlay = std::any_of(
            layer.effects.begin(), layer.effects.end(),
            [t](const LayerEffect &effect) {
                return effect.type == LayerEffectType::ColorOverlay &&
                       eval_effect_enabled(effect, t);
            });
        if (has_color_overlay)
            return false;
    }
    if (layer_has_any_gpu_pixel_effects(layer) &&
        !video_effects_can_use_direct_gpu_base_raster(layer, title_time))
        return false;

    const bool has_rounded_corners =
        layer.corner_radius_tl > 0.01f || layer.corner_radius_tr > 0.01f ||
        layer.corner_radius_br > 0.01f || layer.corner_radius_bl > 0.01f;
    /* The direct shader implements the normal circular corner contract. Keep
     * bevel, chamfer and inverted-corner variants on the exact path until the
     * same curve family is implemented analytically on the GPU. */
    if (has_rounded_corners &&
        std::abs(layer.corner_bevel_roundness - 100.0f) > 0.001f)
        return false;
    if (eval_background_enabled(layer, t) ||
        layer_has_visible_outline(layer, t))
        return false;

    const ShadowRenderParams shadow_params = evaluated_shadow_params(layer, t);
    if (shadow_params.drop_enabled || shadow_params.long_enabled)
        return false;

    return true;
}

static uint32_t argb_with_multiplied_alpha(uint32_t argb, double opacity)
{
    const uint32_t alpha = static_cast<uint32_t>(std::clamp(
        std::round(((argb >> 24) & 0xFF) * std::clamp(opacity, 0.0, 1.0)),
        0.0, 255.0));
    return (argb & 0x00FFFFFFu) | (alpha << 24);
}

static bool layer_can_use_gpu_primitive_raster(const Layer &layer,
                                                double title_time,
                                                bool backend_available)
{
    if (!backend_available ||
        (layer.type != LayerType::SolidRect && layer.type != LayerType::Shape &&
         layer.type != LayerType::ColorSolid))
        return false;

    const ShapeType shape_type = (layer.type == LayerType::SolidRect ||
                                  layer.type == LayerType::ColorSolid)
        ? ShapeType::RoundedRectangle : layer.shape_type;
    /* Line is intentionally excluded from the Phase 11 shape contract. The
     * current line-shaped primitive is no longer exposed as a shape tool and
     * legacy line layers keep using the exact path renderer until the planned
     * dedicated line tool defines its own rendering semantics. */
    if (shape_type == ShapeType::Path || shape_type == ShapeType::Star ||
        shape_type == ShapeType::Line)
        return false;

    const double local_time = std::max(0.0, title_time - layer.in_time);
    if (layer_has_stroke_geometry_modifiers(layer, local_time))
        return false;
    if (layer.fill_type != 0)
        return false;

    /* The analytic primitive target only reserves geometry/stroke padding.
     * Any appearance/effect that can paint outside the primitive needs the
     * full asymmetric surface from clipped_effect_surface_rect(). Keeping such
     * layers on this fast path clips shadows, glows and blurs exactly at the
     * layer box because the later effect shader cannot write beyond its input
     * texture. Route them through the exact base-raster path instead. */
    const ShadowRenderParams shadow = evaluated_shadow_params(layer, local_time);
    if (shadow.drop_enabled || shadow.long_enabled ||
        eval_background_enabled(layer, local_time) ||
        layer_has_any_gpu_pixel_effects(layer))
        return false;

    const bool has_stroke = layer_has_visible_outline(layer, local_time);
    if (has_stroke) {
        if (layer.stroke_fill_type != 1 || !eval_outline_antialias(layer, local_time))
            return false;
        /* Polygonal SDF coverage currently has a fixed analytic join. Keep
         * outlined polygon/star/triangle/diamond layers on the exact path
         * renderer until join-style parity is implemented. */
        if (shape_type == ShapeType::Triangle || shape_type == ShapeType::Star ||
            shape_type == ShapeType::Polygon || shape_type == ShapeType::Diamond)
            return false;
    }

    if ((shape_type == ShapeType::Rectangle ||
         shape_type == ShapeType::RoundedRectangle) &&
        std::abs(layer.corner_bevel_roundness - 100.0f) > 0.001f)
        return false;
    /* Triangle, diamond and polygon roundness use the editable-path corner
     * contract (edge-distance radii plus bevel roundness). Until that exact
     * contract is implemented analytically in the shader, rounded variants
     * must stay on the exact renderer rather than silently drawing sharp. */
    if ((shape_type == ShapeType::Triangle || shape_type == ShapeType::Diamond ||
         shape_type == ShapeType::Polygon) &&
        std::abs(layer.shape_roundness) > 0.001f)
        return false;

    return eval_box_width(layer, local_time) > 0.0 &&
           eval_box_height(layer, local_time) > 0.0;
}

static double gpu_primitive_padding(const Layer &layer, double local_time)
{
    const double stroke_width = layer_has_visible_outline(layer, local_time)
        ? eval_outline_width(layer, local_time) : 0.0;
    switch (eval_outline_alignment(layer, local_time)) {
    case 0: return std::ceil(stroke_width) + 2.0;
    case 2: return 2.0;
    case 1:
    default: return std::ceil(stroke_width * 0.5) + 2.0;
    }
}

static bool direct_gpu_image_geometry(const Layer &layer, double title_time,
                                      QPointF &origin, double &logical_width,
                                      double &logical_height)
{
    const double t = std::max(0.0, title_time - layer.in_time);
    const double box_w = eval_box_width(layer, t);
    const double box_h = eval_box_height(layer, t);
    if (box_w <= 0.0 || box_h <= 0.0)
        return false;

    const QString image_path = QString::fromStdString(
        effective_external_string(layer, ExternalPropertyPaths::ImagePath, layer.image_path));
    double image_w = eval_image_width(layer, t);
    double image_h = eval_image_height(layer, t);
    if (image_w <= 0.0 || image_h <= 0.0) {
        if (layer.type == LayerType::Video &&
            layer.video_pixel_width > 0 && layer.video_pixel_height > 0) {
            image_w = layer.video_pixel_width;
            image_h = layer.video_pixel_height;
        } else {
            const QSize intrinsic_size = image_intrinsic_size(image_path);
            if (!intrinsic_size.isValid() || intrinsic_size.isEmpty())
                return false;
            image_w = intrinsic_size.width();
            image_h = intrinsic_size.height();
        }
    }

    const ImageBoxLayout layout = image_box_layout_for_layer(
        layer, box_w, box_h, image_w, image_h);
    if (layout.w <= 0.0 || layout.h <= 0.0)
        return false;
    origin = QPointF(-eval_origin_x(layer, t) * box_w + layout.x,
                     -eval_origin_y(layer, t) * box_h + layout.y);
    logical_width = layout.w;
    logical_height = layout.h;
    return true;
}

static MotionBaseRaster render_gpu_image_layer_base_raster_direct(
    const Title &title, const Layer &layer, double title_time,
    double raster_scale = 1.0,
    fxm::video::VideoDecodeClient video_decode_client =
        fxm::video::VideoDecodeClient::LiveOutput)
{
    MotionBaseRaster result;
    const double t = std::max(0.0, title_time - layer.in_time);
    const double box_w = eval_box_width(layer, t);
    const double box_h = eval_box_height(layer, t);
    if (box_w <= 0.0 || box_h <= 0.0)
        return result;

    QPointF logical_origin;
    double logical_width = 0.0;
    double logical_height = 0.0;
    if (!direct_gpu_image_geometry(layer, title_time, logical_origin,
                                   logical_width, logical_height))
        return result;

    const QString image_path = QString::fromStdString(
        effective_external_string(layer, ExternalPropertyPaths::ImagePath, layer.image_path));
    const int max_sample_dim = std::clamp(std::max(title.width, title.height) * 2,
                                          512, 4096);
    const double resolved_scale = std::clamp(raster_scale, 0.25, 1.0);
    const QSize sample_size(
        std::clamp(static_cast<int>(std::ceil(logical_width * resolved_scale)), 1, max_sample_dim),
        std::clamp(static_cast<int>(std::ceil(logical_height * resolved_scale)), 1, max_sample_dim));
    QImage raster;
    if (layer.type == LayerType::Video) {
        const fxm::video::VideoFrame frame =
            fxm::video::FrameRuntime::instance().frame_for_layer(
                layer, title_time, 1.0 / std::max(1e-6, source_frame_duration()),
                video_decode_client);
        result.video_frame = true;
        result.video_exact_frame = frame.exact_requested_frame;
        result.video_frame_number = frame.frame_number;
        result.video_requested_frame_number = frame.requested_frame_number;
        raster = frame.image;
        if (raster.isNull()) {
            const fxm::video::VideoFrameLoadingStatus status =
                fxm::video::FrameRuntime::instance().loading_status_for_layer(layer);
            raster = make_video_loading_placeholder_raster(title, layer, title_time, status);
            result.video_exact_frame = false;
            result.video_frame_number = -1;
            result.video_requested_frame_number = frame.requested_frame_number;
            result.video_loading_progress_percent = status.progress_percent;
        }
    } else {
        raster = load_cached_layer_image(image_path, sample_size);
    }
    if (raster.isNull() || raster.width() <= 0 || raster.height() <= 0)
        return result;

    if (raster.format() != QImage::Format_ARGB32_Premultiplied)
        raster = raster.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (layer.type == LayerType::Video) {
        if (resolved_scale < 0.999 && sample_size.isValid() && !sample_size.isEmpty() &&
            sample_size != raster.size()) {
            raster = raster.scaled(sample_size, Qt::IgnoreAspectRatio,
                                   Qt::FastTransformation);
        }
    } else if (!image_path_is_svg(image_path) && resolved_scale < 0.999) {
        raster = raster.scaled(sample_size, Qt::IgnoreAspectRatio,
                               Qt::FastTransformation);
    }
    result.image = std::move(raster);
    result.origin = logical_origin;
    result.logical_width = logical_width;
    result.logical_height = logical_height;
    result.layer_box_rect = QRectF(
        -eval_origin_x(layer, t) * box_w - logical_origin.x(),
        -eval_origin_y(layer, t) * box_h - logical_origin.y(),
        box_w, box_h);
    const QRectF source_rect(0.0, 0.0, logical_width, logical_height);
    result.image_clip_rect = layer.image_crop_when_outside_box
        ? source_rect.intersected(result.layer_box_rect)
        : source_rect;
    return result;
}

static MotionBaseRaster render_gpu_layer_base_raster(const Title &title,
                                                       const Layer &layer,
                                                       double title_time,
                                                       double raster_scale = 1.0,
                                                       bool scene_mask_placeholder_preview = false,
                                                       fxm::video::VideoDecodeClient video_decode_client =
                                                           fxm::video::VideoDecodeClient::LiveOutput)
{
    MotionBaseRaster result;
    const int canvas_w = std::max(1, title.width);
    const int canvas_h = std::max(1, title.height);
    const double local_time = std::max(0.0, title_time - layer.in_time);
    const int transition_blur_padding = general_transition_blur_padding(layer);
    if (!(layer.use_as_scene_mask && scene_mask_placeholder_preview) &&
        layer_can_use_direct_gpu_image_raster(layer, title_time,
                                              transition_blur_padding)) {
        return render_gpu_image_layer_base_raster_direct(title, layer,
                                                         title_time, raster_scale,
                                                         video_decode_client);
    }
    QRect surface_rect = clipped_effect_surface_rect(layer, local_time,
                                                      canvas_w, canvas_h);
    if (transition_blur_padding > 0) {
        surface_rect = surface_rect.adjusted(-transition_blur_padding,
                                             -transition_blur_padding,
                                             transition_blur_padding,
                                             transition_blur_padding);
    }
    if (!surface_rect.isValid() || surface_rect.isEmpty())
        return result;

    const double resolved_scale = std::clamp(raster_scale, 0.25, 1.0);
    QImage canvas(std::max(1, static_cast<int>(std::ceil(surface_rect.width() * resolved_scale))),
                  std::max(1, static_cast<int>(std::ceil(surface_rect.height() * resolved_scale))),
                  QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    auto surface = make_image_surface_for_qimage(canvas);
    auto layer_cr = make_cairo_context(surface.get());
    if (!surface || !layer_cr)
        return result;

    cairo_set_operator(layer_cr.get(), CAIRO_OPERATOR_CLEAR);
    cairo_paint(layer_cr.get());
    cairo_set_operator(layer_cr.get(), CAIRO_OPERATOR_OVER);
    cairo_scale(layer_cr.get(), resolved_scale, resolved_scale);
    cairo_translate(layer_cr.get(), -surface_rect.x(), -surface_rect.y());

    Layer base_layer = layer_without_stackable_pixel_effects(layer);
    base_layer.transitions.erase(
        std::remove_if(base_layer.transitions.begin(), base_layer.transitions.end(),
                       [](const LayerTransition &transition) {
                           return transition.kind == LayerTransitionKind::General;
                       }),
        base_layer.transitions.end());
    neutralize_layer_transform_for_effect_cache(base_layer, 1.0, 0.0, 0.0);
    ScopedGpuReadbackContract no_intermediate_readback(GpuReadbackContract::FinalFrameOnly);
    ScopedSceneMaskPlaceholderPreview scene_mask_preview_scope(
        base_layer.use_as_scene_mask && scene_mask_placeholder_preview);
    ScopedRasterVideoDecodeClient video_decode_scope(video_decode_client);
    render_layer_unmasked(layer_cr.get(), title, base_layer, title_time,
                          canvas_w, canvas_h);
    layer_cr.reset();
    cairo_surface_flush(surface.get());
    surface.reset();

    result.layer_box_rect = QRectF(
        -eval_origin_x(layer, local_time) * eval_box_width(layer, local_time) - surface_rect.x(),
        -eval_origin_y(layer, local_time) * eval_box_height(layer, local_time) - surface_rect.y(),
        eval_box_width(layer, local_time), eval_box_height(layer, local_time));
    const QRect bounds = image_alpha_bounds(canvas);
    if (!bounds.isValid() || bounds.isEmpty())
        return result;
    const bool preserve_effect_surface = transition_blur_padding > 0 ||
        layer_requires_preserved_effect_surface(layer, local_time);
    if (preserve_effect_surface || resolved_scale < 0.999) {
        result.image = canvas;
        result.origin = QPointF(surface_rect.x(), surface_rect.y());
        result.logical_width = surface_rect.width();
        result.logical_height = surface_rect.height();
    } else {
        result.image = canvas.copy(bounds);
        result.origin = QPointF(surface_rect.x() + bounds.x(),
                                surface_rect.y() + bounds.y());
        result.logical_width = result.image.width();
        result.logical_height = result.image.height();
        /* Metadata is expressed in the retained raster's coordinate system.
         * Cropping alpha-only base rasters without translating these rectangles
         * made a later Background Color pass use stale pre-crop bounds. */
        result.layer_box_rect.translate(-bounds.x(), -bounds.y());
        if (result.image_clip_rect.isValid())
            result.image_clip_rect.translate(-bounds.x(), -bounds.y());
    }
    return result;
}

static constexpr const char *kGpuSceneMaskRasterPrefix = "__gpu_scene_mask__";

static std::string gpu_scene_mask_raster_id(const std::string &layer_id)
{
    return std::string(kGpuSceneMaskRasterPrefix) + layer_id;
}

static bool is_gpu_scene_mask_raster_id(const std::string &id)
{
    return id.rfind(kGpuSceneMaskRasterPrefix, 0) == 0;
}

static bool begin_gpu_target(gs_texrender_t *target, uint32_t width, uint32_t height,
                             const struct vec4 &clear_color)
{
    if (!target)
        return false;
    gs_texrender_reset(target);
    if (!gs_texrender_begin(target, width, height))
        return false;
    /* Every compositor surface is a replacement transaction. A preceding
     * hardware-depth/read-only pass may have disabled one or more colour
     * channels or left depth/cull state active. Clearing before restoring that
     * state can preserve pixels from the previous transform, which is most
     * visible when a negative rotation moves the new raster away from its old
     * position. Establish a canonical target state before the clear so a layer
     * can never move another layer or leave an upright stale copy behind. */
    gs_enable_color(true, true, true, true);
    gs_enable_depth_test(false);
    gs_depth_function(GS_LEQUAL);
    gs_set_cull_mode(GS_NEITHER);
    gs_enable_blending(false);
    gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
    gs_clear(GS_CLEAR_COLOR, &clear_color, 1.0f, 0);
    return true;
}


static QRect gpu_text_surface_rect(const Title &title, const Layer &layer,
                                   double local_time)
{
    QRect surface = clipped_effect_surface_rect(
        layer, local_time, std::max(1, title.width), std::max(1, title.height));
    const double box_width = std::max(0.0, eval_box_width(layer, local_time));
    const double box_height = std::max(0.0, eval_box_height(layer, local_time));
    const int inline_stroke_pad = static_cast<int>(std::ceil(
        std::max(0.0, max_rich_text_stroke_width(layer, local_time)))) + 6;
    const int animator_pad = static_cast<int>(std::ceil(
        text_animator_stack_visual_padding(layer.text_animators,
                                           box_width, box_height)));
    const int character_scale_pad = static_cast<int>(std::ceil(
        std::max(0.0, max_rich_text_scale_overhang_hint(layer, local_time)))) + 3;
    const int visual_pad = std::max({inline_stroke_pad, animator_pad,
                                     character_scale_pad});
    if (visual_pad > 2) {
        surface = surface.adjusted(-visual_pad, -visual_pad,
                                   visual_pad, visual_pad);
        const int max_w = std::max(1, title.width);
        const int max_h = std::max(1, title.height);
        surface = surface.intersected(
            QRect(-max_w, -max_h, max_w * 3, max_h * 3));
    }
    return surface;
}

/* Static Text, Clock and Ticker layers use the exact Qt compatibility raster.
 * Timeline text transitions use its shaped isolated-unit compositor as well:
 * that path preserves fractional QPainter coverage while deriving every crop
 * from painted alpha. Do not compensate for SDF aliasing by expanding glyph
 * geometry here; layout metrics, transition-unit bounds and reveal extents must
 * remain identical. Manually authored animators can still use the GPU path. */
static bool layer_can_use_gpu_text_raster(const Layer &layer,
                                          double title_time,
                                          bool backend_available)
{
    if (!backend_available)
        return false;

    const bool text_like = layer.type == LayerType::Text ||
                           layer.type == LayerType::Clock ||
                           layer.type == LayerType::Ticker;
    if (!text_like)
        return false;

    /* The GPU SDF path is reserved for geometry that must remain separable per
     * glyph. Ordinary text-like layers deliberately share the same exact,
     * anti-aliased raster path whether or not a stroke is enabled. */
    if (!text_animator_stack_has_enabled_animators(layer.text_animators))
        return false;

    const bool has_managed_text_transition = std::any_of(
        layer.text_animators.animators.begin(),
        layer.text_animators.animators.end(),
        [](const TextAnimator &animator) {
            return animator.enabled && animator.transition_managed;
        });
    if (has_managed_text_transition)
        return false;

    const double local_time = std::max(0.0, title_time - layer.in_time);
    if (layer_has_stroke_geometry_modifiers(layer, local_time))
        return false;

    return true;
}

static bool prepare_gpu_text_raster(TitleGpuRenderSession *session,
                                    const Layer &layer, double layer_time,
                                    const QRect &surface_rect,
                                    TitleGpuRenderSession::LayerRaster &entry,
                                    std::string *failure_reason)
{
    if (!session || !surface_rect.isValid() || surface_rect.isEmpty())
        return false;
    const double local_time = std::max(0.0, layer_time - layer.in_time);
    const double box_width = eval_box_width(layer, local_time);
    const double box_height = eval_box_height(layer, local_time);
    if (box_width <= 0.0 || box_height <= 0.0)
        return false;

    const QRectF style_rect = text_rect_for_style(
        QRectF(0.0, 0.0, box_width, box_height), layer);
    RichTextDocument model = rich_text_model_for_source_time(layer, local_time);
    double ticker_offset_x = 0.0;
    double ticker_offset_y = 0.0;
    const bool ticker_layer = layer.type == LayerType::Ticker;
    const bool ticker_custom_playback = ticker_layer &&
        layer.ticker_playback_mode == static_cast<int>(TickerPlaybackMode::CustomPlayback);
    const double ticker_completion = ticker_layer ? std::clamp(
        layer.ticker_completion_prop.is_animated()
            ? layer.ticker_completion_prop.evaluate(local_time)
            : layer.ticker_completion,
        0.0, 100.0) / 100.0 : 0.0;
    TickerRuntimeSnapshot ticker_state;
    if (ticker_layer)
        ticker_state = fxm::ticker_runtime::sample(
            session->title.id, layer,
            session->title.current_cue_row >= 0 ||
                session->title.pending_cue_row >= 0);

    if (ticker_layer && layer.ticker_style == 0) {
        QString ticker_text = QString::fromStdString(model.plain_text);
        ticker_text.replace('\r', ' ');
        ticker_text.replace('\n', QStringLiteral("     •     "));
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(model);
        rich_text_document_replace_text_canonical(
            model, ticker_text.toUtf8().toStdString(), insertion_format,
            model.has_typing_format ? model.typing_format_mask : 0);
    } else if (ticker_layer && layer.ticker_style == 1) {
        const QFont ticker_font = font_for_layer(layer, local_time);
        const QStringList lines = ticker_lines(
            QString::fromStdString(model.plain_text), ticker_font,
            style_rect.width(), false);
        const int line_count = std::max(1, static_cast<int>(lines.size()));
        int index = ticker_custom_playback
            ? std::min(line_count - 1,
                       static_cast<int>(std::floor(ticker_completion * line_count)))
            : static_cast<int>(std::floor(
                  ticker_state.time_seconds / std::max(0.1, layer.ticker_line_hold))) %
                  line_count;
        if (layer.ticker_direction == 0) index = line_count - 1 - index;
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(model);
        rich_text_document_replace_text_canonical(
            model, lines.value(index).toUtf8().toStdString(), insertion_format,
            model.has_typing_format ? model.typing_format_mask : 0);
    } else if (layer.text_overflow_mode == 2) {
        const QString fitted = overflow_layout_text(
            QString::fromStdString(model.plain_text), layer);
        const RichTextCharFormat insertion_format =
            rich_text_effective_typing_format(model);
        rich_text_document_replace_text_canonical(
            model, fitted.toUtf8().toStdString(), insertion_format,
            model.has_typing_format ? model.typing_format_mask : 0);
    }

    TextLayoutRequest request;
    request.document = model;
    request.max_width = static_cast<float>(std::max(
        1.0, ticker_layer && layer.ticker_style == 0
            ? 1000000.0 : style_rect.width()));
    request.max_height = static_cast<float>(std::max(
        1.0, ticker_layer && layer.ticker_style == 2
            ? std::max(style_rect.height(), 100000.0) : style_rect.height()));
    request.device_scale = 1.0f;
    request.minimum_horizontal_fit =
        std::clamp(layer.text_fit_min_scale, 0.05f, 1.0f);
    request.overflow_mode = ticker_layer
        ? (layer.ticker_style == 2 ? layer.text_overflow_mode : 1)
        : layer.text_overflow_mode;
    /* Preview resolution never changes shaping geometry. Reuse the bounded
     * layout cache in adaptive mode instead of rebuilding Qt shaping each frame. */
    const ImmutableTextLayout layout = cached_text_layout(request);
    if (!layout || !layout->valid)
        return false;

    if (ticker_layer) {
        const double speed = std::max(1.0, layer.ticker_speed);
        if (layer.ticker_style == 0) {
            const double text_width = std::max(1.0,
                static_cast<double>(layout->natural_width));
            const double travel = style_rect.width() + text_width;
            const double progress = ticker_custom_playback
                ? ticker_completion * travel
                : std::fmod(ticker_state.time_seconds * speed, travel);
            ticker_offset_x = layer.ticker_direction == 0
                ? -text_width + progress
                : style_rect.width() - progress;
        } else if (layer.ticker_style == 2) {
            const double content_height = std::max(1.0,
                static_cast<double>(layout->natural_height));
            const double travel = style_rect.height() + content_height;
            const double progress = ticker_custom_playback
                ? ticker_completion * travel
                : std::fmod(ticker_state.time_seconds * speed, travel);
            ticker_offset_y = layer.ticker_direction == 0
                ? -content_height + progress
                : style_rect.height() - progress;
        }
    }

    if (!session->text_renderer)
        session->text_renderer =
            std::make_unique<fxm::gpu_text::Renderer>();
    if (!entry.text_layer)
        entry.text_layer = std::make_unique<fxm::gpu_text::Layer>();

    const double box_local_x =
        -eval_origin_x(layer, local_time) * box_width - surface_rect.x();
    const double box_local_y =
        -eval_origin_y(layer, local_time) * box_height - surface_rect.y();
    const double text_offset_x = box_local_x + style_rect.x() + ticker_offset_x;
    const double text_offset_y = box_local_y + style_rect.y() + ticker_offset_y;
    /* The text box controls wrapping/alignment, but it is not an ink mask.
     * Scaled glyphs, italic overhang and inline stroke are allowed to extend
     * beyond its logical rectangle just as they do in the Text Style preview.
     * In v273 the GPU quad was correct but the fixed box clip cut V Scale above
     * 100%, producing a second apparently different scaling behaviour. */
    const double clip_pad = std::max(
        std::max(0.0, max_rich_text_stroke_width(layer, local_time)) + 6.0,
        std::max(0.0, max_rich_text_scale_overhang_hint(layer, local_time)) + 3.0);
    const QRectF target_bounds(0.0, 0.0, surface_rect.width(),
                               surface_rect.height());
    const QRectF text_clip = QRectF(text_offset_x, text_offset_y,
                                    style_rect.width(), style_rect.height())
                                 .adjusted(-clip_pad, -clip_pad,
                                           clip_pad, clip_pad)
                                 .intersected(target_bounds);

    fxm::gpu_text::PrepareOptions options;
    options.logical_width = static_cast<float>(surface_rect.width());
    options.logical_height = static_cast<float>(surface_rect.height());
    options.text_offset_x = static_cast<float>(text_offset_x);
    options.text_offset_y = static_cast<float>(text_offset_y);
    options.text_width = static_cast<float>(style_rect.width());
    options.text_height = static_cast<float>(style_rect.height());
    options.clip_x = static_cast<float>(text_clip.x());
    options.clip_y = static_cast<float>(text_clip.y());
    options.clip_width = static_cast<float>(std::max(0.0, text_clip.width()));
    options.clip_height = static_cast<float>(std::max(0.0, text_clip.height()));
    options.raster_scale = static_cast<float>(
        gpu_session_raster_scale(session));

    if (!entry.text_animation_layout ||
        entry.text_animation_layout->key.content != layout->key.content) {
        if (entry.text_animation_layout && entry.text_animation_layout->valid) {
            entry.text_animation_change = map_text_animator_content_change(
                *entry.text_animation_layout, *layout);
            entry.text_animation_change_local_time = local_time;
            if (!entry.text_animation_change.identical) {
                FXM_LOG_DEBUG("TextAnimator", QStringLiteral(
                    "stage=content-remap layer=%1 added=%2 changed=%3 removed=%4 localTime=%5")
                    .arg(QString::fromStdString(layer.id))
                    .arg(entry.text_animation_change.added_byte_ranges.size())
                    .arg(entry.text_animation_change.changed_byte_ranges.size())
                    .arg(entry.text_animation_change.removed_byte_ranges.size())
                    .arg(local_time, 0, 'f', 6));
            }
        } else {
            entry.text_animation_change = TextAnimatorContentChange();
            entry.text_animation_change.identical = true;
            entry.text_animation_change_local_time = local_time;
        }
        entry.text_animation_layout = layout;
    }

    const TextAnimatorStack evaluation_stack =
        entry.text_animation_change.identical
            ? layer.text_animators
            : text_animator_stack_for_content_change(
                  layer.text_animators, entry.text_animation_change,
                  entry.text_animation_change_local_time);
    const TextAnimatorEvaluation text_animation =
        evaluate_text_animators(evaluation_stack, *layout, local_time);
    if (!session->text_renderer->prepare(
            *entry.text_layer, layout,
            text_layout_paint_runs_canonical(request.document), options,
            &text_animation, failure_reason))
        return false;

    /* Prepare a one-shot CPU fallback only before the first valid texture is
     * published. This runs on the model/update thread, never inside the OBS
     * graphics callback. If the GPU glyph vertex buffer fails, the graphics
     * thread can upload this exact frame transactionally instead of deferring
     * publication until an unrelated scene edit changes the model revision. */
    if (!entry.texture && entry.gpu_text_fallback_image.isNull()) {
        const MotionBaseRaster fallback = render_gpu_layer_base_raster(
            session->title, layer, layer_time,
            gpu_session_raster_scale(session),
            session->scene_mask_placeholder_preview_enabled,
            session->video_decode_client);
        if (!fallback.image.isNull()) {
            entry.gpu_text_fallback_image = fallback.image;
            entry.gpu_text_fallback_origin = fallback.origin;
            entry.gpu_text_fallback_logical_width = fallback.logical_width;
            entry.gpu_text_fallback_logical_height = fallback.logical_height;
            entry.gpu_text_fallback_layer_box_rect = fallback.layer_box_rect;
            entry.gpu_text_fallback_image_clip_rect = fallback.image_clip_rect;
        }
    }

    entry.gpu_text = true;
    entry.gpu_primitive = false;
    entry.pending_image = QImage();
    entry.origin = QPointF(surface_rect.x(), surface_rect.y());
    entry.logical_width = surface_rect.width();
    entry.logical_height = surface_rect.height();
    entry.layer_box_rect = QRectF(box_local_x, box_local_y,
                                  box_width, box_height);
    entry.image_clip_rect = QRectF();
    return true;
}

static bool ensure_gpu_session_objects(TitleGpuRenderSession *session,
                                       uint32_t width, uint32_t height)
{
    if (!session)
        return false;
    auto create_target = [](gs_texrender_t *&target) {
        if (!target)
            target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        return target != nullptr;
    };
    auto create_backend_target = [width, height](
        std::unique_ptr<fxm::rendering::IRenderTarget> &resource,
        gs_texrender_t *&target) {
        if (!resource) {
            const fxm::rendering::RenderTargetDescriptor descriptor {
                {width, height},
                fxm::rendering::TextureFormat::BGRA8,
                false,
            };
            resource =
                g_obs_render_backend.create_render_target(descriptor, nullptr);
        }
        target = fxm::obs_plugin::native_render_target(resource.get());
        return target != nullptr;
    };
    auto create_motion_sample_target = [](gs_texrender_t *&target) {
        if (!target)
            target = fxm_create_motion_blur_sample_target();
        return target != nullptr;
    };
    auto create_motion_precision_target = [](gs_texrender_t *&target) {
        if (!target)
            target = fxm_create_motion_blur_accumulation_target();
        return target != nullptr;
    };
    /* Hardware depth is an optional acceleration/correctness path for
     * compatible opaque 3D runs. If a graphics backend cannot allocate a
     * Z24/S8 attachment, preserve the established 2D/compatibility compositor
     * rather than making the entire source fail to render. */
    if (!session->depth_target && !session->depth_target_unavailable) {
        session->depth_target = gs_texrender_create(GS_BGRA, GS_Z24_S8);
        session->depth_target_unavailable = session->depth_target == nullptr;
    }
    for (std::size_t index = 0; index < session->shadow_targets.size(); ++index) {
        if (!session->shadow_targets[index] &&
            !session->shadow_target_unavailable[index]) {
            /* Shadow depth is numeric data, not display colour.  A floating
             * point target avoids both sRGB transfer functions and packed
             * UNORM quantisation corrupting radial Point-light distances. */
            session->shadow_targets[index] = gs_texrender_create(
                GS_R32F, GS_Z24_S8);
            if (!session->shadow_targets[index]) {
                session->shadow_targets[index] = gs_texrender_create(
                    GS_RGBA16F, GS_Z24_S8);
                session->shadow_target_uses_rgba16f[index] =
                    session->shadow_targets[index] != nullptr;
            }
            session->shadow_target_unavailable[index] =
                session->shadow_targets[index] == nullptr;
            if (session->shadow_target_unavailable[index] &&
                !session->shadow_failure_logged[index]) {
                session->shadow_failure_logged[index] = true;
                FXM_LOG_WARNING("Shadows", QStringLiteral(
                    "stage=target-create slot=%1 result=failed formats=R32F,RGBA16F")
                    .arg(static_cast<qulonglong>(index)));
            }
        }
    }
    if (!create_target(session->frame_a) || !create_target(session->frame_b) ||
        !create_target(session->presentation_targets[0]) ||
        !create_target(session->presentation_targets[1]) ||
        !create_target(session->layer_target) || !create_target(session->mask_target) ||
        !create_backend_target(
            session->masked_target_resource,
            session->masked_target) ||
        !create_backend_target(
            session->adjustment_coverage_target_resource,
            session->adjustment_coverage_target) ||
        !create_target(session->effect_a) ||
        !create_target(session->effect_b) || !create_target(session->blur_a) ||
        !create_target(session->blur_b) ||
        !create_motion_sample_target(session->motion_sample_target) ||
        !create_motion_precision_target(session->motion_accum_target)) {
        session->last_error = "Could not allocate GPU render targets.";
        return false;
    }
    if (!ensure_session_shader_effect(
            session, session->blit_effect, "core:frame-blit",
            "GPU frame blit", kGpuFrameBlitEffect,
            "obs-fxm-gpu-frame-blit.effect", &session->blit_effect))
        return false;
    if (!session->effect_registry)
        session->effect_registry = std::make_unique<TitleEffectRegistry>();
    /* Staging is intentionally lazy. Interactive editor/live rendering never
     * allocates or maps a CPU-readable surface; only explicit cache/export
     * readback creates one. */
    session->width = width;
    session->height = height;
    return true;
}

static void set_gpu_effect_argb(gs_effect_t *effect, const char *name, uint32_t argb)
{
    gs_eparam_t *param = effect ? fxm_effect_param(effect, name) : nullptr;
    if (!param) return;
    struct vec4 color;
    vec4_set(&color, ((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f,
             (argb & 0xFF) / 255.0f, ((argb >> 24) & 0xFF) / 255.0f);
    gs_effect_set_vec4(param, &color);
}


static void release_gpu_text_layer(
    TitleGpuRenderSession *session,
    TitleGpuRenderSession::LayerRaster &entry)
{
    if (!entry.text_layer)
        return;
    const bool owns_current = session && session->text_renderer &&
        session->text_renderer->owns_texture(*entry.text_layer, entry.texture);
    if (session && session->text_renderer)
        session->text_renderer->release_layer(*entry.text_layer);
    entry.text_layer.reset();
    if (owns_current) {
        entry.texture = nullptr;
        entry.width = 0;
        entry.height = 0;
    }
}

static bool upload_gpu_layer_raster(
    TitleGpuRenderSession *session,
    TitleGpuRenderSession::LayerRaster &entry);

static bool activate_gpu_text_cpu_fallback(
    TitleGpuRenderSession *session,
    TitleGpuRenderSession::LayerRaster &entry)
{
    if (!session || entry.gpu_text_fallback_image.isNull())
        return false;
    entry.gpu_text = false;
    entry.pending_image = std::move(entry.gpu_text_fallback_image);
    entry.origin = entry.gpu_text_fallback_origin;
    entry.logical_width = entry.gpu_text_fallback_logical_width;
    entry.logical_height = entry.gpu_text_fallback_logical_height;
    entry.layer_box_rect = entry.gpu_text_fallback_layer_box_rect;
    entry.image_clip_rect = entry.gpu_text_fallback_image_clip_rect;
    entry.pending_upload = true;
    return upload_gpu_layer_raster(session, entry);
}

static bool render_gpu_text_raster(
    TitleGpuRenderSession *session,
    TitleGpuRenderSession::LayerRaster &entry)
{
    if (!session || !session->text_renderer || !entry.text_layer)
        return false;
    if (session->text_backend_unavailable) {
        entry.gpu_text = false;
        entry.pending_upload = false;
        entry.key.clear();
        session->force_compatibility_raster_rebuild = true;
        return entry.texture != nullptr;
    }

    gs_texture_t *old_texture = entry.texture;
    const bool old_text_texture =
        session->text_renderer->owns_texture(*entry.text_layer, old_texture);
    const bool old_primitive_targets =
        entry.primitive_targets[0] || entry.primitive_targets[1];
    if (!session->text_renderer->effect_ready()) {
        /* Explicit prerender/readback already runs off the UI thread while the
         * OBS graphics context is held. Compile here so the first text frame is
         * complete; fast asynchronous retries could otherwise expire before
         * the shader worker published readiness. Live editor/source rendering
         * keeps the non-blocking compile path below. */
        if (final_frame_readback_only()) {
            if (!session->text_renderer->compile_effect()) {
                session->text_backend_unavailable = true;
                session->force_compatibility_raster_rebuild = true;
                if (const char *error = session->text_renderer->last_error())
                    session->last_error = error;
                entry.gpu_text = false;
                entry.pending_upload = false;
                entry.key.clear();
                return activate_gpu_text_cpu_fallback(session, entry);
            }
        } else {
            queue_shader_compile_job(session, "core:gpu-text", "GPU text",
                [session]() -> bool {
                    std::unique_lock<std::mutex> lock(session->mutex);
                    if (session->destroying.load(std::memory_order_acquire))
                        return false;
                    if (!session->text_renderer)
                        session->text_renderer =
                            std::make_unique<fxm::gpu_text::Renderer>();
                    fxm::gpu_text::Renderer *renderer = session->text_renderer.get();
                    lock.unlock();
                    const bool ok = renderer->compile_effect();
                    if (!ok) {
                        std::lock_guard<std::mutex> failure_lock(session->mutex);
                        session->text_backend_unavailable = true;
                        session->force_compatibility_raster_rebuild = true;
                        if (const char *error = renderer->last_error())
                            session->last_error = error;
                    }
                    return ok;
                });
            mark_shader_compile_pending(session, "GPU text");
            entry.pending_upload = true;
            return entry.texture != nullptr;
        }
    }
    if (!session->text_renderer->render(*entry.text_layer)) {
        /* Any Phase 12C draw failure is optional. Route the session back to
         * the stable CPU text raster on the next model refresh and retain the
         * last valid texture, if one exists. Most importantly, do not leave a
         * pending GPU raster that blocks transactional first-frame
         * publication indefinitely. */
        session->text_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_text = false;
        entry.pending_upload = false;
        entry.key.clear();
        entry.effect_cache_key.clear();
        if (const char *error = session->text_renderer->last_error())
            session->last_error = error;
        const bool immediate_fallback =
            activate_gpu_text_cpu_fallback(session, entry);
        FXM_LOG_WARNING("GpuText", QStringLiteral(
            "stage=runtime-fallback reason=%1 retainedTexture=%2 immediateCpuFallback=%3")
            .arg(session->last_error
                     ? QString::fromUtf8(session->last_error)
                     : QStringLiteral("unknown GPU text failure"))
            .arg(entry.texture ? 1 : 0)
            .arg(immediate_fallback ? 1 : 0));
        if (immediate_fallback) {
            FXM_LOG_DEBUG("GpuPipeline", QStringLiteral(
                "stage=transactional-publication action=cpu-text-fallback-ready"));
            return true;
        }
        return false;
    }

    gs_texture_t *rendered =
        session->text_renderer->texture(*entry.text_layer);
    if (!rendered) {
        session->text_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_text = false;
        entry.pending_upload = false;
        entry.key.clear();
        entry.effect_cache_key.clear();
        session->last_error = "GPU text rendering returned no texture; falling back to the compatibility raster.";
        if (activate_gpu_text_cpu_fallback(session, entry))
            return true;
        return false;
    }

    if (old_primitive_targets) {
        for (gs_texrender_t *&target : entry.primitive_targets) {
            if (target)
                gs_texrender_destroy(target);
            target = nullptr;
        }
        entry.primitive_active_target = -1;
    } else if (old_texture && old_texture != rendered && !old_text_texture) {
        gs_texture_destroy(old_texture);
    }

    entry.texture = rendered;
    entry.width = session->text_renderer->texture_width(*entry.text_layer);
    entry.height = session->text_renderer->texture_height(*entry.text_layer);
    entry.gpu_text = true;
    entry.gpu_primitive = false;
    entry.pending_image = QImage();
    entry.gpu_text_fallback_image = QImage();
    entry.gpu_text_fallback_logical_width = 0.0;
    entry.gpu_text_fallback_logical_height = 0.0;
    entry.pending_upload = false;
    entry.effect_cache_key.clear();
    return true;
}

static bool render_gpu_primitive_raster(TitleGpuRenderSession *session,
                                        TitleGpuRenderSession::LayerRaster &entry)
{
    if (!session)
        return false;
    if (session->primitive_backend_unavailable) {
        entry.gpu_primitive = false;
        entry.pending_upload = false;
        entry.key.clear();
        session->force_compatibility_raster_rebuild = true;
        return entry.texture != nullptr;
    }
    if (entry.text_layer)
        release_gpu_text_layer(session, entry);
    /* Phase 11 remains lazy and optional: cache-only presentation does not
     * depend on this shader, and a backend compile failure is converted into a
     * per-layer CPU base-raster fallback on the next model update. */
    if (!session->primitive_shape_effect &&
        !ensure_session_shader_effect(
            session, session->primitive_shape_effect, "core:primitive-shape",
            "GPU primitive shape", kGpuPrimitiveShapeEffect,
            "obs-fxm-gpu-primitive-shape.effect",
            &session->primitive_shape_effect)) {
        entry.gpu_primitive = true;
        entry.pending_upload = true;
        session->last_error = "Shader compile pending: GPU primitive shape";
        return entry.texture != nullptr;
    }
    if (!session->primitive_shape_effect) {
        session->primitive_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_primitive = false;
        entry.pending_upload = false;
        entry.key.clear();
        entry.effect_cache_key.clear();
        session->last_error = "Could not compile optional GPU primitive-shape shader; falling back to the stable base raster.";
        return entry.texture != nullptr;
    }
    const bool replacing_owned_cpu_texture = entry.texture &&
        !entry.primitive_targets[0] && !entry.primitive_targets[1];
    gs_texture_t *owned_cpu_texture = replacing_owned_cpu_texture
        ? entry.texture : nullptr;
    const float raster_scale = static_cast<float>(
        gpu_session_raster_scale(session));
    const uint32_t width = clamped_source_dimension(static_cast<int>(
        std::ceil(entry.logical_width * raster_scale)));
    const uint32_t height = clamped_source_dimension(static_cast<int>(
        std::ceil(entry.logical_height * raster_scale)));

    /* Never reset the target currently sampled by the last valid frame.
     * Render into the inactive target and publish it only after a successful
     * draw. This prevents a transient cleared/black texture during shape
     * creation and interactive scaling. */
    const int render_index = entry.primitive_active_target == 0 ? 1 : 0;
    gs_texrender_t *&render_target = entry.primitive_targets[render_index];
    if (!render_target)
        render_target = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    if (!render_target) {
        session->primitive_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_primitive = false;
        entry.pending_upload = false;
        entry.key.clear();
        return entry.texture != nullptr;
    }

    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(render_target, width, height, clear)) {
        session->primitive_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_primitive = false;
        entry.pending_upload = false;
        entry.key.clear();
        return entry.texture != nullptr;
    }
    gs_effect_t *effect = session->primitive_shape_effect;
    if (auto *p = fxm_effect_param(effect, "shapeType")) gs_effect_set_int(p, entry.primitive_shape_type);
    if (auto *p = fxm_effect_param(effect, "vertexCount")) gs_effect_set_int(p, entry.primitive_vertex_count);
    if (auto *p = fxm_effect_param(effect, "starInnerRadius")) gs_effect_set_float(p, entry.primitive_inner_radius);
    if (auto *p = fxm_effect_param(effect, "starOuterRadius")) gs_effect_set_float(p, entry.primitive_outer_radius);
    if (auto *p = fxm_effect_param(effect, "strokeWidth")) gs_effect_set_float(p, entry.primitive_stroke_width);
    if (auto *p = fxm_effect_param(effect, "strokeAlignment")) gs_effect_set_int(p, entry.primitive_stroke_alignment);
    if (auto *p = fxm_effect_param(effect, "strokeOnFront")) gs_effect_set_int(p, entry.primitive_stroke_on_front ? 1 : 0);
    if (auto *p = fxm_effect_param(effect, "surfaceSize")) { struct vec2 v; vec2_set(&v,(float)entry.logical_width,(float)entry.logical_height); gs_effect_set_vec2(p,&v); }
    if (auto *p = fxm_effect_param(effect, "shapeSize")) { struct vec2 v; vec2_set(&v,(float)entry.primitive_shape_width,(float)entry.primitive_shape_height); gs_effect_set_vec2(p,&v); }
    if (auto *p = fxm_effect_param(effect, "shapeOffset")) { struct vec2 v; vec2_set(&v,(float)entry.primitive_padding,(float)entry.primitive_padding); gs_effect_set_vec2(p,&v); }
    if (auto *p = fxm_effect_param(effect, "cornerRadii")) { struct vec4 v; vec4_set(&v,entry.primitive_corner_radii[0],entry.primitive_corner_radii[1],entry.primitive_corner_radii[2],entry.primitive_corner_radii[3]); gs_effect_set_vec4(p,&v); }
    set_gpu_effect_argb(effect, "fillColor", entry.primitive_fill_color);
    set_gpu_effect_argb(effect, "strokeColor", entry.primitive_stroke_color);
    gs_enable_blending(false);
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(nullptr, 0, width, height);
    gs_texrender_end(render_target);

    gs_texture_t *rendered = gs_texrender_get_texture(render_target);
    if (!rendered) {
        session->primitive_backend_unavailable = true;
        session->force_compatibility_raster_rebuild = true;
        entry.gpu_primitive = false;
        entry.pending_upload = false;
        entry.key.clear();
        return entry.texture != nullptr;
    }
    entry.primitive_active_target = render_index;
    entry.texture = rendered;
    if (owned_cpu_texture)
        gs_texture_destroy(owned_cpu_texture);
    entry.width = width;
    entry.height = height;
    /* The effected variant is tied to the previous immutable primitive
     * generation even when its dimensions are unchanged. */
    entry.effect_cache_key.clear();
    entry.pending_upload = false;
    return true;
}

static bool upload_gpu_layer_raster(TitleGpuRenderSession *session,
                                    TitleGpuRenderSession::LayerRaster &entry)
{
    if (entry.gpu_text && entry.pending_upload)
        return render_gpu_text_raster(session, entry);
    if (entry.gpu_text && !entry.pending_upload)
        return entry.texture != nullptr;
    if (entry.text_layer)
        release_gpu_text_layer(session, entry);
    if (entry.gpu_primitive && entry.pending_upload)
        return render_gpu_primitive_raster(session, entry);
    if (!entry.pending_upload)
        return entry.texture != nullptr;
    if (!entry.gpu_primitive &&
        (entry.primitive_targets[0] || entry.primitive_targets[1])) {
        for (gs_texrender_t *&target : entry.primitive_targets) {
            if (target)
                gs_texrender_destroy(target);
            target = nullptr;
        }
        entry.primitive_active_target = -1;
        entry.texture = nullptr;
        entry.width = entry.height = 0;
    }
    if (entry.pending_image.isNull()) {
        if (entry.primitive_targets[0] || entry.primitive_targets[1]) {
            for (gs_texrender_t *&target : entry.primitive_targets) {
                if (target)
                    gs_texrender_destroy(target);
                target = nullptr;
            }
            entry.primitive_active_target = -1;
            entry.texture = nullptr;
        } else if (entry.texture) {
            gs_texture_destroy(entry.texture);
        }
        entry.texture = nullptr;
        if (entry.effect_cache)
            gs_texrender_destroy(entry.effect_cache);
        entry.effect_cache = nullptr;
        entry.effect_cache_key.clear();
        entry.effect_cache_last_used = 0;
        if (entry.transform_grid)
            gs_vertexbuffer_destroy(entry.transform_grid);
        entry.transform_grid = nullptr;
        entry.transform_grid_key.clear();
        entry.width = entry.height = 0;
        entry.pending_upload = false;
        return false;
    }
    QImage upload = entry.pending_image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (upload.bytesPerLine() != upload.width() * 4)
        upload = upload.copy();
    const uint32_t width = (uint32_t)upload.width();
    const uint32_t height = (uint32_t)upload.height();
    if (!entry.texture || entry.width != width || entry.height != height) {
        if (entry.texture)
            gs_texture_destroy(entry.texture);
        const uint8_t *planes[1] = {upload.constBits()};
        entry.texture = gs_texture_create(width, height, GS_BGRA, 1, planes, GS_DYNAMIC);
        entry.width = entry.texture ? width : 0;
        entry.height = entry.texture ? height : 0;
    } else {
        gs_texture_set_image(entry.texture, upload.constBits(),
                             (uint32_t)upload.bytesPerLine(), false);
    }
    /* A new base raster invalidates any GPU-resident effected variant, even
     * when the texture dimensions happened to stay the same. */
    entry.effect_cache_key.clear();
    entry.pending_image = QImage();
    entry.pending_upload = false;
    return entry.texture != nullptr;
}

struct PackedGaussianKernel {
    float center_weight = 1.0f;
    std::array<float, 4> offsets {1.0f, 3.0f, 5.0f, 7.0f};
    std::array<float, 4> weights {0.0f, 0.0f, 0.0f, 0.0f};
    int downsample = 1;
};

static PackedGaussianKernel packed_gaussian_kernel(double radius)
{
    PackedGaussianKernel kernel;
    kernel.downsample = gaussian_blur_downsample(radius);
    const double sigma = std::max(0.35, radius /
        (3.0 * static_cast<double>(kernel.downsample)));
    std::array<double, 9> discrete {};
    for (int i = 0; i <= 8; ++i) {
        const double x = static_cast<double>(i);
        discrete[static_cast<std::size_t>(i)] =
            std::exp(-(x * x) / (2.0 * sigma * sigma));
    }
    double normalization = discrete[0];
    for (int i = 1; i <= 8; ++i)
        normalization += 2.0 * discrete[static_cast<std::size_t>(i)];
    normalization = std::max(normalization, 1e-12);
    kernel.center_weight = static_cast<float>(discrete[0] / normalization);
    for (int pair = 0; pair < 4; ++pair) {
        const int first = pair * 2 + 1;
        const int second = first + 1;
        const double first_weight = discrete[static_cast<std::size_t>(first)];
        const double second_weight = discrete[static_cast<std::size_t>(second)];
        const double combined = std::max(first_weight + second_weight, 1e-12);
        kernel.offsets[static_cast<std::size_t>(pair)] = static_cast<float>(
            (first * first_weight + second * second_weight) / combined);
        kernel.weights[static_cast<std::size_t>(pair)] = static_cast<float>(
            combined / normalization);
    }
    return kernel;
}

static bool effect_uses_separable_gaussian(LayerEffectType type)
{
    switch (type) {
    case LayerEffectType::DropShadow:
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
    case LayerEffectType::InnerShadow:
    case LayerEffectType::Blur:
    case LayerEffectType::Bloom:
    case LayerEffectType::Halation:
    case LayerEffectType::Glare:
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::HighPass:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen:
        return true;
    default:
        return false;
    }
}

static double effect_gaussian_radius(const ResolvedLayerEffect &effect)
{
    switch (effect.type) {
    case LayerEffectType::DropShadow:
    case LayerEffectType::InnerShadow:
        return std::max(0.0, static_cast<double>(effect.effect_size) +
                              static_cast<double>(effect.effect_spread));
    default:
        return std::max(0.0, static_cast<double>(effect.effect_size));
    }
}

static gs_texture_t *render_separable_gaussian(
    TitleGpuRenderSession *session, gs_texture_t *source,
    uint32_t source_width, uint32_t source_height, double radius,
    int prefilter_mode, float threshold)
{
    if (!session || !source || !session->effect_registry)
        return source;
    if (radius <= 0.01 && prefilter_mode == 0)
        return source;

    gs_effect_t *blur = session_registry_effect_or_queue(
        session, LayerEffectType::Blur, "Blur effect");
    if (!blur) {
        session->last_error = session->effect_registry->last_error();
        return source;
    }

    const PackedGaussianKernel kernel = packed_gaussian_kernel(radius);
    struct vec4 clear;
    vec4_zero(&clear);

    gs_texture_t *current = source;
    uint32_t current_width = std::max<uint32_t>(1, source_width);
    uint32_t current_height = std::max<uint32_t>(1, source_height);
    const gs_texture_t *blur_a_texture = gs_texrender_get_texture(session->blur_a);
    const gs_texture_t *blur_b_texture = gs_texrender_get_texture(session->blur_b);
    bool next_is_a = current != blur_a_texture;
    if (current == blur_b_texture)
        next_is_a = true;
    int applied_prefilter = 0;

    /* Build a proper low-pass pyramid instead of jumping directly from the
     * source to a tiny target. Each 2x step averages four bilinear samples,
     * eliminating the sparse/ringing look that large single-pass kernels had. */
    for (int scale = 1; scale < kernel.downsample; scale *= 2) {
        const uint32_t next_width = std::max<uint32_t>(1, (current_width + 1) / 2);
        const uint32_t next_height = std::max<uint32_t>(1, (current_height + 1) / 2);
        gs_texrender_t *target = next_is_a ? session->blur_a : session->blur_b;
        if (!begin_gpu_target(target, next_width, next_height, clear))
            return source;
        if (gs_eparam_t *image = fxm_effect_param(blur, "image"))
            gs_effect_set_texture(image, current);
        set_effect_vec2_param(blur, "texelSize",
                              1.0f / static_cast<float>(current_width),
                              1.0f / static_cast<float>(current_height));
        set_effect_int_param(blur, "prefilterMode",
                             applied_prefilter ? 0 : prefilter_mode);
        set_effect_float_param(blur, "threshold", threshold);
        gs_enable_blending(false);
        while (gs_effect_loop(blur, "Downsample"))
            gs_draw_sprite(current, 0, next_width, next_height);
        gs_texrender_end(target);
        current = gs_texrender_get_texture(target);
        if (!current)
            return source;
        current_width = next_width;
        current_height = next_height;
        next_is_a = !next_is_a;
        applied_prefilter = prefilter_mode != 0;
    }

    auto configure_gaussian = [&](gs_texture_t *input, float dx, float dy,
                                  int active_prefilter) {
        if (gs_eparam_t *image = fxm_effect_param(blur, "image"))
            gs_effect_set_texture(image, input);
        set_effect_vec2_param(blur, "texelSize",
                              1.0f / static_cast<float>(current_width),
                              1.0f / static_cast<float>(current_height));
        set_effect_vec2_param(blur, "direction", dx, dy);
        set_effect_float_param(blur, "centerWeight", kernel.center_weight);
        set_effect_vec4_param(blur, "pairOffsets",
                              kernel.offsets[0], kernel.offsets[1],
                              kernel.offsets[2], kernel.offsets[3]);
        set_effect_vec4_param(blur, "pairWeights",
                              kernel.weights[0], kernel.weights[1],
                              kernel.weights[2], kernel.weights[3]);
        set_effect_int_param(blur, "prefilterMode", active_prefilter);
        set_effect_float_param(blur, "threshold", threshold);
    };

    gs_texrender_t *horizontal_target = next_is_a ? session->blur_a : session->blur_b;
    if (!begin_gpu_target(horizontal_target, current_width, current_height, clear))
        return source;
    configure_gaussian(current, 1.0f, 0.0f,
                       applied_prefilter ? 0 : prefilter_mode);
    gs_enable_blending(false);
    while (gs_effect_loop(blur, "Gaussian"))
        gs_draw_sprite(current, 0, current_width, current_height);
    gs_texrender_end(horizontal_target);
    gs_texture_t *horizontal = gs_texrender_get_texture(horizontal_target);
    if (!horizontal)
        return source;
    next_is_a = !next_is_a;

    gs_texrender_t *vertical_target = next_is_a ? session->blur_a : session->blur_b;
    if (!begin_gpu_target(vertical_target, current_width, current_height, clear))
        return source;
    configure_gaussian(horizontal, 0.0f, 1.0f, 0);
    gs_enable_blending(false);
    while (gs_effect_loop(blur, "Gaussian"))
        gs_draw_sprite(horizontal, 0, current_width, current_height);
    gs_texrender_end(vertical_target);
    gs_texture_t *result = gs_texrender_get_texture(vertical_target);
    return result ? result : source;
}

static gs_texture_t *render_long_shadow_mask(
    TitleGpuRenderSession *session, gs_effect_t *effect,
    const ResolvedLayerEffect &resolved, gs_texture_t *source,
    uint32_t width, uint32_t height)
{
    if (!session || !effect || !source)
        return source;
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(session->blur_a, width, height, clear))
        return source;
    if (gs_eparam_t *image = fxm_effect_param(effect, "image"))
        gs_effect_set_texture(image, source);
    set_gpu_surface_effect_params(effect, resolved,
                                  static_cast<int>(width),
                                  static_cast<int>(height));
    gs_enable_blending(false);
    while (gs_effect_loop(effect, "ShadowMask"))
        gs_draw_sprite(source, 0, width, height);
    gs_texrender_end(session->blur_a);
    gs_texture_t *mask = gs_texrender_get_texture(session->blur_a);
    return mask ? mask : source;
}
