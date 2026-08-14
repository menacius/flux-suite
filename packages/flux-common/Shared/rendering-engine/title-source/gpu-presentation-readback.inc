struct GpuEffectLayerSpace {
    QRectF bounds_uv{0.0, 0.0, 1.0, 1.0};
    QPointF origin_uv{0.0, 0.0};
    QPointF axis_x_uv{1.0, 0.0};
    QPointF axis_y_uv{0.0, 1.0};
    QPointF pixel_size{1.0, 1.0};
};

static GpuEffectLayerSpace gpu_effect_layer_space(
    const TitleGpuRenderSession *session, const Layer &layer,
    const TitleGpuRenderSession::LayerRaster *entry,
    uint32_t texture_width, uint32_t texture_height)
{
    GpuEffectLayerSpace space;
    if (texture_width == 0 || texture_height == 0)
        return space;

    if (entry && entry->logical_width > 0.0 && entry->logical_height > 0.0 &&
        entry->layer_box_rect.isValid()) {
        const QRectF rect(entry->layer_box_rect.x() / entry->logical_width,
                          entry->layer_box_rect.y() / entry->logical_height,
                          entry->layer_box_rect.width() / entry->logical_width,
                          entry->layer_box_rect.height() / entry->logical_height);
        space.bounds_uv = rect;
        space.origin_uv = rect.topLeft();
        space.axis_x_uv = QPointF(rect.width(), 0.0);
        space.axis_y_uv = QPointF(0.0, rect.height());
        space.pixel_size = QPointF(
            std::max(1.0, std::abs(rect.width()) * texture_width),
            std::max(1.0, std::abs(rect.height()) * texture_height));
        return space;
    }

    if (!session || !session->has_title)
        return space;

    const double resolved_time = gpu_layer_render_time(
        session->title, layer, session->time);
    const double local_time = std::max(0.0, resolved_time - layer.in_time);
    const double box_width = std::max(0.0001, eval_box_width(layer, local_time));
    const double box_height = std::max(0.0001, eval_box_height(layer, local_time));
    const QPointF local_origin(-eval_origin_x(layer, local_time) * box_width,
                               -eval_origin_y(layer, local_time) * box_height);
    const QTransform world = layer_world_transform_qt(
        session->title, layer, session->time);
    const double frame_scale_x = gpu_session_render_scale_x(session);
    const double frame_scale_y = gpu_session_render_scale_y(session);
    auto to_uv = [&](const QPointF &canvas_point) {
        return QPointF(canvas_point.x() * frame_scale_x / texture_width,
                       canvas_point.y() * frame_scale_y / texture_height);
    };
    const QPointF origin = to_uv(world.map(local_origin));
    const QPointF x_end = to_uv(world.map(local_origin + QPointF(box_width, 0.0)));
    const QPointF y_end = to_uv(world.map(local_origin + QPointF(0.0, box_height)));
    const QPointF axis_x = x_end - origin;
    const QPointF axis_y = y_end - origin;
    const QPointF opposite = origin + axis_x + axis_y;
    const double min_x = std::min({origin.x(), x_end.x(), y_end.x(), opposite.x()});
    const double min_y = std::min({origin.y(), x_end.y(), y_end.y(), opposite.y()});
    const double max_x = std::max({origin.x(), x_end.x(), y_end.x(), opposite.x()});
    const double max_y = std::max({origin.y(), x_end.y(), y_end.y(), opposite.y()});
    space.bounds_uv = QRectF(min_x, min_y,
                             std::max(0.000001, max_x - min_x),
                             std::max(0.000001, max_y - min_y));
    space.origin_uv = origin;
    space.axis_x_uv = axis_x;
    space.axis_y_uv = axis_y;
    space.pixel_size = QPointF(
        std::max(1.0, std::hypot(axis_x.x() * texture_width,
                                 axis_x.y() * texture_height)),
        std::max(1.0, std::hypot(axis_y.x() * texture_width,
                                 axis_y.y() * texture_height)));
    return space;
}

static QRectF gpu_effect_layer_uv_rect(
    const TitleGpuRenderSession *session, const Layer &layer,
    const TitleGpuRenderSession::LayerRaster *entry,
    uint32_t texture_width, uint32_t texture_height)
{
    return gpu_effect_layer_space(session, layer, entry,
                                  texture_width, texture_height).bounds_uv;
}

static void set_gpu_layer_space_params(
    gs_effect_t *effect, const TitleGpuRenderSession *session,
    const Layer &layer, const TitleGpuRenderSession::LayerRaster *entry,
    uint32_t texture_width, uint32_t texture_height)
{
    if (!effect || texture_width == 0 || texture_height == 0)
        return;
    const GpuEffectLayerSpace space = gpu_effect_layer_space(
        session, layer, entry, texture_width, texture_height);
    set_effect_vec4_param(effect, "layerUvRect",
                          static_cast<float>(space.bounds_uv.x()),
                          static_cast<float>(space.bounds_uv.y()),
                          static_cast<float>(std::max(space.bounds_uv.width(), 0.000001)),
                          static_cast<float>(std::max(space.bounds_uv.height(), 0.000001)));
    set_effect_vec2_param(effect, "layerUvOrigin",
                          static_cast<float>(space.origin_uv.x()),
                          static_cast<float>(space.origin_uv.y()));
    set_effect_vec2_param(effect, "layerUvAxisX",
                          static_cast<float>(space.axis_x_uv.x()),
                          static_cast<float>(space.axis_x_uv.y()));
    set_effect_vec2_param(effect, "layerUvAxisY",
                          static_cast<float>(space.axis_y_uv.x()),
                          static_cast<float>(space.axis_y_uv.y()));
    set_effect_vec2_param(effect, "layerPixelSize",
                          static_cast<float>(space.pixel_size.x()),
                          static_cast<float>(space.pixel_size.y()));
}

static void set_gpu_background_geometry_params(
    gs_effect_t *effect, const TitleGpuRenderSession *session,
    const ResolvedLayerEffect &resolved, const Layer &layer,
    double title_time, const TitleGpuRenderSession::LayerRaster *entry,
    uint32_t texture_width, uint32_t texture_height)
{
    if (!effect || texture_width == 0 || texture_height == 0)
        return;
    const GpuEffectLayerSpace space = gpu_effect_layer_space(
        session, layer, entry, texture_width, texture_height);
    const double resolved_time = session
        ? gpu_layer_render_time(session->title, layer, session->time)
        : title_time;
    const double local_time = std::max(0.0, resolved_time - layer.in_time);
    const double base_width = entry && entry->base_box_width > 0.0001
        ? entry->base_box_width
        : std::max(0.0001, eval_box_width(layer, local_time));
    const double base_height = entry && entry->base_box_height > 0.0001
        ? entry->base_box_height
        : std::max(0.0001, eval_box_height(layer, local_time));
    const double pixel_width = std::max(1.0, space.pixel_size.x());
    const double pixel_height = std::max(1.0, space.pixel_size.y());
    const double scale_x = pixel_width / base_width;
    const double scale_y = pixel_height / base_height;

    /* Geometry is expressed relative to the layer box rather than the backing
     * surface. That keeps Background Color attached to translated/rotated
     * layers and groups and prevents a later shadow/glow pass from turning the
     * background into a full-canvas rectangle. */
    const double x = -resolved.effect_padding_left * scale_x;
    const double y = -resolved.effect_padding_top * scale_y;
    const double w = std::max(0.0, pixel_width +
        resolved.effect_padding_left * scale_x +
        resolved.effect_padding_right * scale_x);
    const double h = std::max(0.0, pixel_height +
        resolved.effect_padding_top * scale_y +
        resolved.effect_padding_bottom * scale_y);
    set_effect_vec4_param(effect, "backgroundRect",
        static_cast<float>(x), static_cast<float>(y),
        static_cast<float>(w), static_cast<float>(h));
    set_effect_vec2_param(effect, "textureSize",
                          static_cast<float>(pixel_width),
                          static_cast<float>(pixel_height));
    const float geometry_scale = static_cast<float>(
        std::max(0.0001, std::min(std::abs(scale_x), std::abs(scale_y))));
    set_effect_vec4_param(effect, "cornerRadii",
                          resolved.effect_corner_radius_tl * geometry_scale,
                          resolved.effect_corner_radius_tr * geometry_scale,
                          resolved.effect_corner_radius_br * geometry_scale,
                          resolved.effect_corner_radius_bl * geometry_scale);
    set_effect_int_param(effect, "fillType", resolved.effect_fill_type);
    set_effect_int_param(effect, "gradientType", resolved.effect_gradient_type);
    set_effect_int_param(effect, "gradientSpread", resolved.effect_gradient_spread);
    set_effect_color_param(effect, "gradientStartColor", resolved.effect_gradient_start_color);
    set_effect_color_param(effect, "gradientEndColor", resolved.effect_gradient_end_color);
    set_effect_float_param(effect, "gradientStartPos", resolved.effect_gradient_start_pos);
    set_effect_float_param(effect, "gradientEndPos", resolved.effect_gradient_end_pos);
    set_effect_float_param(effect, "gradientStartOpacity", resolved.effect_gradient_start_opacity);
    set_effect_float_param(effect, "gradientEndOpacity", resolved.effect_gradient_end_opacity);
    set_effect_float_param(effect, "gradientOpacity", resolved.effect_gradient_opacity);
    set_effect_float_param(effect, "gradientAngle", resolved.effect_gradient_angle);
    set_effect_vec2_param(effect, "gradientCenter", resolved.effect_gradient_center_x,
                          resolved.effect_gradient_center_y);
    set_effect_vec2_param(effect, "gradientFocal", resolved.effect_gradient_focal_x,
                          resolved.effect_gradient_focal_y);
    set_effect_float_param(effect, "gradientScale", resolved.effect_gradient_scale);
    set_effect_color_param(effect, "strokeColor", resolved.effect_stroke_color);
    set_effect_float_param(effect, "strokeWidth",
                           resolved.effect_stroke_width * geometry_scale);
    set_effect_float_param(effect, "strokeOpacity", resolved.effect_stroke_opacity);
}

enum class GpuMaskGraphPurpose {
    MatteArtwork,
    ClippingShape,
    EffectSource,
};

static gs_texture_t *render_gpu_mask_graph_texture(
    TitleGpuRenderSession *session, const Title &title, const Layer &layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id,
    GpuMaskGraphPurpose purpose = GpuMaskGraphPurpose::MatteArtwork);

static bool gpu_effect_is_provable_noop(const ResolvedLayerEffect &effect)
{
    /* Called only for built-in semantics. Custom extension shaders own their
     * parameter interpretation and are excluded by append_resolved_effect(). */
    constexpr float kEpsilon = 0.0001f;
    const auto color_alpha = [](std::uint32_t argb) {
        return static_cast<float>((argb >> 24) & 0xFFu) / 255.0f;
    };
    if (!effect.enabled)
        return true;

    switch (effect.type) {
    case LayerEffectType::BackgroundColor:
        return effect.effect_opacity <= kEpsilon &&
               (effect.effect_stroke_width <= kEpsilon ||
                effect.effect_stroke_opacity <= kEpsilon ||
                color_alpha(effect.effect_stroke_color) <= kEpsilon);
    case LayerEffectType::Outline:
        /* The current Outline shader intentionally clamps zero width to a
         * one-pixel sample radius, so width==0 is not an identity state. */
        return effect.effect_opacity <= kEpsilon ||
               color_alpha(effect.effect_color) <= kEpsilon;
    case LayerEffectType::DropShadow:
    case LayerEffectType::LongShadow:
    case LayerEffectType::Glow:
    case LayerEffectType::InnerGlow:
    case LayerEffectType::InnerShadow:
    case LayerEffectType::Bloom:
    case LayerEffectType::Halation:
    case LayerEffectType::ColorOverlay:
        return effect.effect_opacity <= kEpsilon ||
               color_alpha(effect.effect_color) <= kEpsilon;
    case LayerEffectType::Glare:
        /* Glare uses the RGB tint but not effectColor.a. Its true identity
         * controls are opacity, amount and intensity/falloff. */
        return effect.effect_opacity <= kEpsilon ||
               std::abs(effect.effect_amount) <= kEpsilon ||
               effect.effect_falloff <= kEpsilon;
    case LayerEffectType::Blur:
        return effect.effect_opacity <= kEpsilon ||
               effect.effect_size <= 0.01f;
    case LayerEffectType::BrightnessContrast:
        return std::abs(effect.brightness) <= kEpsilon &&
               std::abs(effect.contrast - 1.0f) <= kEpsilon;
    case LayerEffectType::Saturation:
        return std::abs(effect.saturation - 1.0f) <= kEpsilon;
    case LayerEffectType::Sharpen:
    case LayerEffectType::UnsharpMask:
    case LayerEffectType::Clarity:
    case LayerEffectType::BilateralSharpen:
    case LayerEffectType::Noise:
    case LayerEffectType::Grain:
    case LayerEffectType::RoughenEdges:
    case LayerEffectType::LensFlare:
    case LayerEffectType::Vignette:
    case LayerEffectType::ChromaticAberration:
    case LayerEffectType::Ripple:
    case LayerEffectType::WaveWarp:
    case LayerEffectType::Scanlines:
        return std::abs(effect.effect_amount) <= kEpsilon ||
               effect.effect_opacity <= kEpsilon;
    case LayerEffectType::DirectionalBlur:
    case LayerEffectType::RadialBlur:
        return effect.effect_size <= kEpsilon ||
               effect.effect_opacity <= kEpsilon;
    case LayerEffectType::ZoomBlur:
        /* The existing nine-tap kernel sums to 0.92 at zero radius. Preserve
         * that authored result instead of treating zero radius as identity. */
        return effect.effect_opacity <= kEpsilon;
    case LayerEffectType::DisplacementMap:
        return std::abs(effect.effect_amount) <= kEpsilon &&
               std::abs(effect.effect_distance) <= kEpsilon;
    case LayerEffectType::LightWrap:
        return effect.effect_amount <= kEpsilon ||
               effect.effect_opacity <= kEpsilon;
    default:
        return false;
    }
}

static gs_texture_t *apply_gpu_layer_effect_stack(TitleGpuRenderSession *session,
                                                   const Layer &layer,
                                                   double title_time,
                                                   gs_texture_t *input,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   TitleGpuRenderSession::LayerRaster *cache_entry = nullptr,
                                                   bool adjustment_only = false,
                                                   gs_texture_t *composition_background = nullptr,
                                                   bool input_is_composition = false,
                                                   int execution_space_filter = -1)
{
    if (!session || !input)
        return input;
    const double local_time = std::max(0.0, title_time - layer.in_time);
    struct vec4 clear;
    vec4_zero(&clear);

    /* Development Version 396 effect-cache preflight: the old order resolved
     * every animated property and entered the shader registry before checking an
     * already valid static effect output. That CPU work
     * occurred on every OBS draw even though no GPU pass was required. Determine
     * cache eligibility from the authored stack first and return the resident
     * output before any shader/extension work. */
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time, title_time);
    const bool transition_blur_selected =
        (execution_space_filter < 0 ||
         execution_space_filter == static_cast<int>(LayerEffectSpace::LayerSpace)) &&
        transition.active && transition.blur > 0.01;
    bool has_selected_effect = transition_blur_selected;
    bool has_source_dependency = false;
    for (const LayerEffect &effect_config : layer.effects) {
        if (!eval_effect_enabled(effect_config, local_time) ||
            effect_config.type == LayerEffectType::MotionBlur ||
            effect_config.type == LayerEffectType::TrimPaths ||
            (execution_space_filter >= 0 &&
             static_cast<int>(effect_execution_space(effect_config)) !=
                 execution_space_filter) ||
            (adjustment_only && !effect_config.affect_layers_behind) ||
            (!adjustment_only && effect_config.affect_layers_behind))
            continue;
        has_selected_effect = true;
        has_source_dependency = has_source_dependency ||
            effect_config.type == LayerEffectType::LightWrap ||
            effect_config.type == LayerEffectType::DisplacementMap;
    }
    const bool can_cache = has_selected_effect && cache_entry &&
                           input == cache_entry->texture &&
                           !cache_entry->key.empty() &&
                           !has_source_dependency;
    const std::string desired_cache_key = can_cache
        ? cache_entry->key +
              "|gpu-effects-v26-3d-lighting-materials-shadows-v27-per-light-source-size-shadows-v28-point-shadow-atlas-complete-3d-interactions-v29-matrix-exact-rgb24-point-shadows-v30-float32-receiver-plane-point-shadows-v31-shared-point-cube-projection-v32-distance-stable-point-shadows-v33-pcss-contact-hardening-v34-shadow-runtime-recovery-v35-stable-multiscale-poisson-shadows-v36-restored-3d-shadow-visibility-v37-signed-point-shadow-clip-depth-v38-realtime-receiver-matched-point-pcf-v39-compile-safe-receiver-matched-point-pcf-v40-async-shader-compile-progress-v41-d3d-planar-shadow-clip-v42-effect-cache-preflight-noop-elision-v43-conservative-text-shadow-alpha-min-source-size-v44-explicit-planar-basis-text-footprint-v45-close-point-text-shadow-projection-grid|" +
              effect_layer_cache_key(nullptr, session->title, layer, title_time,
                                     static_cast<int>(width), static_cast<int>(height),
                                     false, false)
        : std::string();
    if (can_cache && cache_entry->effect_cache &&
        cache_entry->effect_cache_key == desired_cache_key) {
        if (gs_texture_t *cached = gs_texrender_get_texture(cache_entry->effect_cache)) {
            cache_entry->effect_cache_last_used = ++session->effect_cache_tick;
            fxm::perf::add(fxm::perf::Counter::EffectOutputCacheHits);
            return cached;
        }
    }
    if (can_cache)
        fxm::perf::add(fxm::perf::Counter::EffectOutputCacheMisses);

    /* Keep pass state local to this invocation. Effect-stack rendering can be
     * nested (groups, mattes, scene masks and adjustment layers) and can also
     * be reached by separate frontend/source paths. Session-owned scratch was
     * therefore not re-entrant: a nested render could clear or replace shader
     * handles still in use by its caller. ResolvedLayerEffect is lightweight,
     * so a reserved local vector preserves the allocation win without sharing
     * mutable pass lifetime across renders. */
    struct GpuEffectPass {
        ResolvedLayerEffect resolved;
        gs_effect_t *shader = nullptr;
        gs_texture_t *source_texture = nullptr;
        bool source_is_composition = false;
    };
    constexpr std::size_t kInlineEffectPasses = 16;
    std::array<GpuEffectPass, kInlineEffectPasses> inline_passes{};
    std::vector<GpuEffectPass> overflow_passes;
    std::size_t pass_count = 0;
    auto append_pass = [&](const GpuEffectPass &pass) {
        if (pass_count < kInlineEffectPasses)
            inline_passes[pass_count] = pass;
        else
            overflow_passes.push_back(pass);
        ++pass_count;
    };
    auto pass_at = [&](std::size_t index) -> GpuEffectPass & {
        return index < kInlineEffectPasses
            ? inline_passes[index]
            : overflow_passes[index - kInlineEffectPasses];
    };

    auto append_resolved_effect = [&](const ResolvedLayerEffect &resolved) {
        const std::string *extension_id = resolved.source
            ? &resolved.source->extension_id : nullptr;
        LayerEffectType registered_builtin_type{};
        const bool has_extension_id = extension_id && !extension_id->empty();
        const bool is_registered_builtin = has_extension_id &&
            FxmEffectExtensionCatalog::builtInTypeForId(
                QString::fromStdString(*extension_id), &registered_builtin_type);
        const bool uses_builtin_semantics = !has_extension_id || is_registered_builtin;

        /* Do not allocate targets, enter the registry or bind dozens of uniforms
         * for built-in states that are mathematically identical to the input.
         * Custom extension semantics and ambiguous identities continue through
         * the normal stack. Reuse the extension classification already required
         * for shader selection, avoiding an additional catalog lookup. */
        if (uses_builtin_semantics && gpu_effect_is_provable_noop(resolved)) {
            fxm::perf::add(fxm::perf::Counter::EffectNoOpPassesSkipped);
            return;
        }
        /* Every effect whose visual construction depends on blurring uses the
         * same general separable-Gaussian shader.  Only the final composite
         * technique differs (shadow/glow/inner/bloom/blur). */
        LayerEffectType shader_type = resolved.type;
        switch (resolved.type) {
        case LayerEffectType::DropShadow:
        case LayerEffectType::Glow:
        case LayerEffectType::InnerGlow:
        case LayerEffectType::InnerShadow:
        case LayerEffectType::Blur:
        case LayerEffectType::Bloom:
            shader_type = LayerEffectType::Blur;
            break;
        default:
            break;
        }
        gs_effect_t *effect = uses_builtin_semantics
            ? session_registry_effect_or_queue(
                  session, shader_type, session_effect_compile_label(shader_type))
            : session_registry_effect_or_queue(
                  session, *extension_id, extension_id->c_str());
        if (!effect) {
            /* GPU-only means there is no CPU compositor fallback. Keep the
             * layer visible if an optional shader asset is unavailable and
             * expose the error through the GPU status instead of dropping the
             * complete layer from editor/live output. */
            session->last_error = session->effect_registry->last_error();
            return;
        }
        gs_texture_t *source_texture = nullptr;
        bool source_is_composition = false;
        const bool source_aware = resolved.type == LayerEffectType::LightWrap ||
                                  resolved.type == LayerEffectType::DisplacementMap;
        if (source_aware && resolved.source) {
            const LayerEffect &source_config = *resolved.source;
            if (resolved.type == LayerEffectType::LightWrap &&
                source_config.effect_source_mode == 0) {
                source_texture = composition_background;
                source_is_composition = true;
            } else if (!source_config.effect_source_layer_id.empty() &&
                       source_config.effect_source_layer_id != layer.id) {
                const std::string visit_key = layer.id + "->" +
                    source_config.effect_source_layer_id;
                if (session->source_effect_visiting.insert(visit_key).second) {
                    const Layer *source_layer = find_layer_by_id(
                        session->title, source_config.effect_source_layer_id);
                    if (source_layer) {
                        const bool composition_mapping =
                            resolved.type == LayerEffectType::LightWrap ||
                            source_config.effect_mapping_space == 1;
                        if (composition_mapping) {
                            source_texture = render_gpu_mask_graph_texture(
                                session, session->title, *source_layer,
                                title_time, gpu_session_layer_id(*source_layer),
                                source_layer->parent_id,
                                GpuMaskGraphPurpose::EffectSource);
                            source_is_composition = true;
                        } else {
                            auto source_raster = session->layers.find(
                                gpu_session_layer_id(*source_layer));
                            if (source_raster != session->layers.end())
                                source_texture = source_raster->second.texture;
                        }
                    }
                    session->source_effect_visiting.erase(visit_key);
                }
            }
        }
        append_pass({resolved, effect, source_texture, source_is_composition});
    };

    for (const LayerEffect &effect_config : layer.effects) {
        if (!eval_effect_enabled(effect_config, local_time) ||
            effect_config.type == LayerEffectType::MotionBlur ||
            effect_config.type == LayerEffectType::TrimPaths ||
            (execution_space_filter >= 0 &&
             static_cast<int>(effect_execution_space(effect_config)) !=
                 execution_space_filter) ||
            (adjustment_only && !effect_config.affect_layers_behind) ||
            (!adjustment_only && effect_config.affect_layers_behind))
            continue;
        ResolvedLayerEffect resolved = resolve_gpu_layer_effect(effect_config, local_time);
        const float physical_scale = static_cast<float>(
            gpu_session_raster_scale(session));
        if (std::abs(physical_scale - 1.0f) > 0.0001f) {
            /* Effect distances are authored in logical title pixels. Convert
             * them to physical target pixels for both reduced previews and
             * supersampled/high-resolution output without changing topology. */
            switch (resolved.type) {
            case LayerEffectType::Blur:
            case LayerEffectType::DropShadow:
            case LayerEffectType::Glow:
            case LayerEffectType::InnerGlow:
            case LayerEffectType::InnerShadow:
            case LayerEffectType::Bloom:
            case LayerEffectType::Halation:
            case LayerEffectType::Glare:
            case LayerEffectType::DirectionalBlur:
            case LayerEffectType::ZoomBlur:
            case LayerEffectType::RadialBlur:
            case LayerEffectType::Sharpen:
            case LayerEffectType::UnsharpMask:
            case LayerEffectType::HighPass:
            case LayerEffectType::Clarity:
            case LayerEffectType::BilateralSharpen:
                resolved.effect_size *= physical_scale;
                break;
            case LayerEffectType::LongShadow:
                resolved.effect_distance *= physical_scale;
                resolved.effect_size *= physical_scale;
                break;
            default:
                break;
            }
        }
        append_resolved_effect(resolved);
    }

    if (transition_blur_selected) {
        ResolvedLayerEffect transition_blur =
            make_resolved_layer_effect(LayerEffectType::Blur);
        transition_blur.enabled = true;
        transition_blur.effect_size = static_cast<float>(transition.blur);
        transition_blur.effect_opacity = 1.0f;
        transition_blur.effect_blur_type = static_cast<int>(ShadowBlurType::StackFast);
        append_resolved_effect(transition_blur);
    }

    if (pass_count == 0) {
        fxm::perf::add(fxm::perf::Counter::EffectEmptyStackFastPaths);
        if (cache_entry && cache_entry->effect_cache) {
            gs_texrender_destroy(cache_entry->effect_cache);
            cache_entry->effect_cache = nullptr;
            cache_entry->effect_cache_key.clear();
            cache_entry->effect_cache_last_used = 0;
        }
        return input;
    }

    if (can_cache && !cache_entry->effect_cache)
        cache_entry->effect_cache = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
    if (can_cache)
        cache_entry->effect_cache_key.clear();

    gs_texture_t *current = input;
    bool use_a = true;
    for (std::size_t index = 0; index < pass_count; ++index) {
        const bool last = index + 1 == pass_count;
        GpuEffectPass &pass = pass_at(index);
        ResolvedLayerEffect &resolved = pass.resolved;
        gs_effect_t *pass_effect = pass.shader;
        if (!pass_effect) {
            session->last_error = "Effect shader became unavailable during the render pass.";
            return current;
        }
        fxm::perf::ScopedTimer effect_timer(fxm::perf::Counter::EffectPassNanoseconds);
        fxm::perf::add(fxm::perf::Counter::EffectPasses);
        gs_texture_t *blurred = nullptr;
        gs_texture_t *long_shadow = nullptr;
        if (effect_uses_separable_gaussian(resolved.type)) {
            const int prefilter_mode =
                (resolved.type == LayerEffectType::Bloom ||
                 resolved.type == LayerEffectType::Halation ||
                 resolved.type == LayerEffectType::Glare) ? 1 : 0;
            blurred = render_separable_gaussian(
                session, current, width, height,
                effect_gaussian_radius(resolved), prefilter_mode,
                std::clamp(resolved.effect_spread, 0.0f, 1.0f));
        } else if (resolved.type == LayerEffectType::LongShadow) {
            long_shadow = render_long_shadow_mask(
                session, pass_effect, resolved, current, width, height);
            if (resolved.effect_size > 0.01f) {
                long_shadow = render_separable_gaussian(
                    session, long_shadow, width, height,
                    resolved.effect_size, 0, 0.0f);
            }
        }
        gs_texrender_t *target = last && can_cache && cache_entry->effect_cache
            ? cache_entry->effect_cache
            : (use_a ? session->effect_a : session->effect_b);
        if (!target || !begin_gpu_target(target, width, height, clear)) {
            session->last_error = "Could not allocate an effect render target.";
            return current;
        }
        if (gs_eparam_t *image = fxm_effect_param(pass_effect, "image"))
            gs_effect_set_texture(image, current);
        /* Outside/inside hard-alpha clipping must always use the untouched
         * artwork silhouette, not the output of an earlier effect pass. This is
         * especially important for groups, whose first effect can expand alpha
         * over the complete child composite. */
        if (gs_eparam_t *hard_mask =
                fxm_effect_param(pass_effect, "hardMask"))
            gs_effect_set_texture(hard_mask, input);
        if (blurred) {
            if (gs_eparam_t *blurred_image =
                    fxm_effect_param(pass_effect, "blurredImage"))
                gs_effect_set_texture(blurred_image, blurred);
        }
        if (long_shadow) {
            if (gs_eparam_t *shadow_image =
                    fxm_effect_param(pass_effect, "shadowImage"))
                gs_effect_set_texture(shadow_image, long_shadow);
        }
        if (pass.source_texture) {
            if (gs_eparam_t *source_image =
                    fxm_effect_param(pass_effect, "sourceImage"))
                gs_effect_set_texture(source_image, pass.source_texture);
            const uint32_t source_width = std::max(
                1u, gs_texture_get_width(pass.source_texture));
            const uint32_t source_height = std::max(
                1u, gs_texture_get_height(pass.source_texture));
            set_effect_vec2_param(pass_effect, "sourceTexelSize",
                                  1.0f / source_width, 1.0f / source_height);
        }
        set_effect_int_param(pass_effect, "sourceEnabled",
                             pass.source_texture ? 1 : 0);
        set_effect_int_param(pass_effect, "sourceIsComposition",
                             pass.source_is_composition ? 1 : 0);
        set_effect_int_param(pass_effect, "xChannel", resolved.effect_x_channel);
        set_effect_int_param(pass_effect, "yChannel", resolved.effect_y_channel);
        set_effect_int_param(pass_effect, "wrapMode", resolved.effect_wrap_mode);
        set_effect_int_param(pass_effect, "mappingSpace", resolved.effect_mapping_space);
        set_effect_int_param(pass_effect, "alphaAware", resolved.effect_alpha_aware ? 1 : 0);
        set_effect_int_param(pass_effect, "inputIsComposition",
                             input_is_composition ? 1 : 0);
        set_gpu_surface_effect_params(pass_effect, resolved,
                                      (int)width, (int)height, local_time);
        set_gpu_layer_space_params(pass_effect, session, layer, cache_entry,
                                   width, height);
        /* Extension parameters use a name-based ABI: scalar JSON values map to
         * same-named shader uniforms. Compound graphs are flattened to
         * element<N>_<property> uniforms, keeping native plugins optional. */
        if (resolved.source && !resolved.source->extension_id.empty()) {
            const LayerEffect &extension_source = *resolved.source;
            const QJsonDocument extDoc = QJsonDocument::fromJson(
                QByteArray::fromStdString(extension_source.extension_parameters_json));
            if (extDoc.isObject()) {
                auto &extensionCatalog = FxmEffectExtensionCatalog::instance();
                if (extensionCatalog.effects().empty())
                    extensionCatalog.reload();
                const auto *extensionDefinition = extensionCatalog.find(
                    QString::fromStdString(extension_source.extension_id));
                const QJsonObject parameterSchema = extensionDefinition
                    ? extensionDefinition->parameterSchema : QJsonObject{};
                auto setUniform = [&](const QString &name, const QJsonValue &value) {
                    gs_eparam_t *param = fxm_effect_param(pass_effect, name.toUtf8().constData());
                    if (!param) return;
                    const QString declaredType = parameterSchema.value(name).toObject()
                        .value(QStringLiteral("type")).toString();
                    if (value.isBool()) gs_effect_set_bool(param, value.toBool());
                    else if (value.isDouble()) {
                        if (declaredType == QStringLiteral("int") ||
                            declaredType == QStringLiteral("enum"))
                            gs_effect_set_int(param, value.toInt());
                        else
                            gs_effect_set_float(param, static_cast<float>(value.toDouble()));
                    } else if (value.isString()) {
                        const QColor color(value.toString());
                        if (color.isValid()) {
                            vec4 v; vec4_set(&v, color.redF(), color.greenF(), color.blueF(), color.alphaF());
                            gs_effect_set_vec4(param, &v);
                        }
                    } else if (value.isArray()) {
                        const QJsonArray a = value.toArray();
                        if (a.size() == 2) {
                            vec2 v;
                            vec2_set(&v, static_cast<float>(a.at(0).toDouble()),
                                         static_cast<float>(a.at(1).toDouble()));
                            gs_effect_set_vec2(param, &v);
                        } else if (a.size() == 3) {
                            vec3 v;
                            vec3_set(&v, static_cast<float>(a.at(0).toDouble()),
                                         static_cast<float>(a.at(1).toDouble()),
                                         static_cast<float>(a.at(2).toDouble()));
                            gs_effect_set_vec3(param, &v);
                        } else if (a.size() == 4) {
                            vec4 v;
                            vec4_set(&v, static_cast<float>(a.at(0).toDouble()),
                                         static_cast<float>(a.at(1).toDouble()),
                                         static_cast<float>(a.at(2).toDouble()),
                                         static_cast<float>(a.at(3).toDouble()));
                            gs_effect_set_vec4(param, &v);
                        }
                    }
                };
                QJsonObject object = extDoc.object();
                /* Evaluate host-owned extension animation tracks before binding
                 * uniforms. Tracks are keyed by JSON paths such as
                 * "intensity" or "elements.0.opacity". */
                const QJsonDocument tracksDoc = QJsonDocument::fromJson(
                    QByteArray::fromStdString(extension_source.extension_keyframes_json));
                auto interpolateValue = [](const QJsonValue &a, const QJsonValue &b, double t) -> QJsonValue {
                    if (a.isDouble() && b.isDouble())
                        return a.toDouble() + (b.toDouble() - a.toDouble()) * t;
                    if (a.isArray() && b.isArray()) {
                        const QJsonArray aa = a.toArray(), bb = b.toArray();
                        QJsonArray out;
                        const qsizetype n = std::min(aa.size(), bb.size());
                        for (qsizetype i = 0; i < n; ++i)
                            out.append(aa.at(i).toDouble() + (bb.at(i).toDouble() - aa.at(i).toDouble()) * t);
                        return out;
                    }
                    return t < 1.0 ? a : b;
                };
                auto setPath = [](QJsonObject &root, const QString &path, const QJsonValue &value) {
                    const QStringList parts = path.split(QLatin1Char('.'), Qt::SkipEmptyParts);
                    if (parts.size() == 1) { root.insert(parts.front(), value); return; }
                    if (parts.size() == 3 && parts.at(0) == QStringLiteral("elements")) {
                        bool ok = false; const int index = parts.at(1).toInt(&ok);
                        QJsonArray elements = root.value(QStringLiteral("elements")).toArray();
                        if (ok && index >= 0 && index < elements.size()) {
                            QJsonObject element = elements.at(index).toObject();
                            element.insert(parts.at(2), value);
                            elements.replace(index, element);
                            root.insert(QStringLiteral("elements"), elements);
                        }
                    }
                };
                if (tracksDoc.isObject()) {
                    const QJsonObject tracks = tracksDoc.object();
                    for (auto trackIt = tracks.begin(); trackIt != tracks.end(); ++trackIt) {
                        const QJsonArray keys = trackIt.value().toArray();
                        if (keys.isEmpty()) continue;
                        QJsonObject left = keys.first().toObject(), right = keys.last().toObject();
                        for (int k = 0; k < keys.size(); ++k) {
                            const QJsonObject key = keys.at(k).toObject();
                            const double kt = key.value(QStringLiteral("time")).toDouble();
                            if (kt <= local_time) left = key;
                            if (kt >= local_time) { right = key; break; }
                        }
                        const double lt0 = left.value(QStringLiteral("time")).toDouble();
                        const double rt0 = right.value(QStringLiteral("time")).toDouble();
                        const double linear_mix = rt0 > lt0
                            ? (local_time - lt0) / (rt0 - lt0) : 0.0;
                        const double mix = fxm::effects::animation::eased_mix(linear_mix, left);
                        setPath(object, trackIt.key(), interpolateValue(left.value(QStringLiteral("value")), right.value(QStringLiteral("value")), mix));
                    }
                }
                if (extensionDefinition) {
                    const QJsonArray assets = extensionDefinition->assetIndex.value(QStringLiteral("items")).toArray();
                    for (const QJsonValue &assetValue : assets) {
                        const QJsonObject asset = assetValue.toObject();
                        const QString uniformName = asset.value(QStringLiteral("uniform")).toString();
                        const QString relativeFile = asset.value(QStringLiteral("file")).toString();
                        if (uniformName.isEmpty() || relativeFile.isEmpty()) continue;
                        gs_eparam_t *textureParam = fxm_effect_param(pass_effect, uniformName.toUtf8().constData());
                        if (!textureParam) continue;
                        const QString assetPath = QFileInfo(relativeFile).isAbsolute()
                            ? relativeFile : QDir(extensionDefinition->basePath).filePath(relativeFile);
                        if (gs_texture_t *assetTexture = extension_image_texture(assetPath))
                            gs_effect_set_texture(textureParam, assetTexture);
                    }
                }
                for (auto it = object.begin(); it != object.end(); ++it) {
                    if (it.key() == QStringLiteral("elements") && it.value().isArray()) {
                        const QJsonArray elements = it.value().toArray();
                        if (gs_eparam_t *count = fxm_effect_param(pass_effect, "elementCount"))
                            gs_effect_set_int(count, std::min<int>(static_cast<int>(elements.size()), 16));
                        for (int ei = 0; ei < elements.size() && ei < 16; ++ei) {
                            const QJsonObject element = elements.at(ei).toObject();
                            for (auto ep = element.begin(); ep != element.end(); ++ep)
                                setUniform(QStringLiteral("element%1_%2").arg(ei).arg(ep.key()), ep.value());
                        }
                    } else setUniform(it.key(), it.value());
                }
            }
        }
        if (resolved.type == LayerEffectType::BackgroundColor)
            set_gpu_background_geometry_params(pass_effect, session,
                                               resolved, layer, title_time,
                                               cache_entry, width, height);
        gs_enable_blending(false);
        const char *technique = gpu_effect_primary_technique(resolved, blurred != nullptr);
        int executed_passes = 0;
        while (gs_effect_loop(pass_effect, technique)) {
            ++executed_passes;
            gs_draw_sprite(current, 0, width, height);
        }
        if (resolved.type == LayerEffectType::LensFlare) {
            if (!session->lens_flare_pass_logged) {
                FXM_LOG_INFO("Effects", QStringLiteral(
                    "Lens flare GPU pass active passes=%1 size=%2x%3 profile=%4 radius=%5 amount=%6 opacity=%7")
                    .arg(executed_passes).arg(width).arg(height)
                    .arg(resolved.effect_profile)
                    .arg(resolved.effect_size)
                    .arg(resolved.effect_amount)
                    .arg(resolved.effect_opacity));
                session->lens_flare_pass_logged = true;
            }
            if (executed_passes == 0) {
                session->last_error = "Lens flare effect compiled but Draw technique executed no passes.";
                FXM_LOG_WARNING("Effects", QStringLiteral(
                    "Lens flare Draw technique executed no passes"));
            }
        }
        if (executed_passes == 0) {
            const EffectDescriptor *descriptor = effect_descriptor(resolved.type);
            const std::string effect_name = descriptor && descriptor->stable_id
                ? descriptor->stable_id
                : std::to_string(static_cast<int>(resolved.type));
            session->last_error_storage =
                std::string("Effect technique executed no passes: ") + effect_name;
            session->last_error = session->last_error_storage.c_str();
            FXM_LOG_WARNING("Effects", QStringLiteral("Effect technique %1 executed no passes for %2")
                .arg(QString::fromUtf8(technique), QString::fromStdString(effect_name)));
            gs_texrender_end(target);
            continue;
        }
        gs_texrender_end(target);
        current = gs_texrender_get_texture(target);
        if (!current)
            return input;
        if (!(last && can_cache))
            use_a = !use_a;
    }

    if (can_cache && cache_entry->effect_cache &&
        current == gs_texrender_get_texture(cache_entry->effect_cache)) {
        cache_entry->effect_cache_key = desired_cache_key;
        cache_entry->effect_cache_last_used = ++session->effect_cache_tick;
    }
    return current;
}

static quint64 gpu_transition_matte_budget(
    const TitleGpuRenderSession *session)
{
    constexpr quint64 kRealtimeBudget = 64ull * 1024ull * 1024ull;
    constexpr quint64 kOfflineBudget = 128ull * 1024ull * 1024ull;
    return session && session->realtime_output.load(std::memory_order_relaxed)
        ? kRealtimeBudget : kOfflineBudget;
}

static void prune_gpu_transition_matte_cache(
    TitleGpuRenderSession *session, const std::string &protected_key)
{
    constexpr std::size_t kMaximumEntries = 8;
    if (!session)
        return;
    quint64 bytes = 0;
    for (const auto &cached : session->transition_matte_textures)
        bytes += cached.second.bytes;
    const quint64 budget = gpu_transition_matte_budget(session);
    std::size_t evicted = 0;
    while ((bytes > budget ||
            session->transition_matte_textures.size() > kMaximumEntries) &&
           !session->transition_matte_textures.empty()) {
        auto victim = session->transition_matte_textures.end();
        for (auto it = session->transition_matte_textures.begin();
             it != session->transition_matte_textures.end(); ++it) {
            if (it->first == protected_key)
                continue;
            if (victim == session->transition_matte_textures.end() ||
                it->second.last_used < victim->second.last_used)
                victim = it;
        }
        if (victim == session->transition_matte_textures.end())
            break;
        bytes -= std::min(bytes, victim->second.bytes);
        session->transition_matte_textures.erase(victim);
        ++evicted;
    }
    session->transition_matte_cache_bytes = bytes;
    if (evicted > 0) {
        FXM_LOG_DEBUG("GpuMemory", QStringLiteral(
            "cache=transition-matte action=evict entries=%1 remaining=%2 bytes=%3 budget=%4")
            .arg(static_cast<qulonglong>(evicted))
            .arg(static_cast<qulonglong>(
                session->transition_matte_textures.size()))
            .arg(static_cast<qulonglong>(bytes))
            .arg(static_cast<qulonglong>(budget)));
    }
}

static gs_texture_t *gpu_transition_matte_texture(
    TitleGpuRenderSession *session, const std::string &path_value)
{
    if (!session || path_value.empty())
        return nullptr;
    const QString path = QString::fromStdString(path_value);
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || !info.isReadable())
        return nullptr;
    const std::string key = info.absoluteFilePath().toStdString();
    auto &entry = session->transition_matte_textures[key];
    const qint64 modified = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();
    if (entry.texture && entry.modified_msecs == modified &&
        entry.file_size == size) {
        entry.last_used = ++session->transition_matte_cache_tick;
        prune_gpu_transition_matte_cache(session, key);
        return entry.texture;
    }

    if (entry.texture) {
        entry.texture_resource.reset();
        entry.texture = nullptr;
        entry.bytes = 0;
    }
    QImage upload = load_cached_layer_image(info.absoluteFilePath());
    if (upload.isNull()) {
        session->transition_matte_textures.erase(key);
        return nullptr;
    }
    if (upload.format() != QImage::Format_ARGB32_Premultiplied)
        upload = upload.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    upload.detach();
    unpremultiply_bgra_for_obs(upload.bits(),
        static_cast<size_t>(upload.width()) * static_cast<size_t>(upload.height()));
    const fxm::rendering::TextureDescriptor descriptor {
        {
            static_cast<uint32_t>(upload.width()),
            static_cast<uint32_t>(upload.height()),
        },
        fxm::rendering::TextureFormat::BGRA8,
        fxm::rendering::TextureUsage::Immutable,
    };
    const fxm::rendering::TextureUpload texture_upload {
        upload.constBits(),
        static_cast<std::size_t>(upload.sizeInBytes()),
        static_cast<uint32_t>(upload.bytesPerLine()),
    };
    entry.texture_resource =
        g_obs_render_backend.create_texture(descriptor, &texture_upload, nullptr);
    entry.texture = fxm::obs_plugin::native_texture(
        entry.texture_resource.get());
    if (!entry.texture) {
        session->transition_matte_textures.erase(key);
        return nullptr;
    }
    entry.modified_msecs = modified;
    entry.file_size = size;
    entry.width = static_cast<uint32_t>(upload.width());
    entry.height = static_cast<uint32_t>(upload.height());
    entry.bytes = static_cast<quint64>(entry.width) *
                  static_cast<quint64>(entry.height) * 4ull;
    entry.last_used = ++session->transition_matte_cache_tick;
    prune_gpu_transition_matte_cache(session, key);
    return entry.texture;
}

static void set_gpu_transition_effect_params(
    TitleGpuRenderSession *session, gs_effect_t *effect,
    const LayerTransitionVisualState &transition)
{
    if (!effect)
        return;
    set_effect_float_param(effect, "wipeProgress", static_cast<float>(transition.wipe));
    set_effect_float_param(effect, "wipeSoftness", static_cast<float>(transition.wipe_softness));
    set_effect_int_param(effect, "wipeDirection",
        transition.active && transition.wipe < 0.999999
            ? static_cast<int>(transition.wipe_direction) : 0);
    set_effect_int_param(effect, "transitionType",
        transition.active ? static_cast<int>(transition.type) : 0);
    set_effect_int_param(effect, "blocksColumns", std::max(1, transition.blocks_columns));
    set_effect_int_param(effect, "blocksRows", std::max(1, transition.blocks_rows));
    set_effect_float_param(effect, "randomSeed", static_cast<float>(transition.random_seed));
    set_effect_int_param(effect, "imageChannel", std::clamp(transition.image_channel, 0, 4));
    set_effect_int_param(effect, "transitionInvert", transition.invert ? 1 : 0);
    set_effect_int_param(effect, "transitionClockwise", transition.clockwise ? 1 : 0);
    set_effect_vec2_param(effect, "transitionCenter",
        static_cast<float>(transition.center_x), static_cast<float>(transition.center_y));
    set_effect_float_param(effect, "transitionRotation", static_cast<float>(transition.rotation));
    set_effect_float_param(effect, "transitionAspect", static_cast<float>(transition.aspect));
    set_effect_int_param(effect, "transitionProfile", transition.profile);

    gs_texture_t *matte = transition.type == LayerTransitionType::ImageWipe
        ? gpu_transition_matte_texture(session, transition.image_path) : nullptr;
    set_effect_int_param(effect, "transitionMatteEnabled", matte ? 1 : 0);
    if (matte) {
        if (gs_eparam_t *param = fxm_effect_param(effect, "transitionMatte"))
            gs_effect_set_texture(param, matte);
    }
}

static gs_texture_t *apply_gpu_mask(TitleGpuRenderSession *session,
                                    gs_texture_t *layer_texture,
                                    gs_texture_t *mask_texture,
                                    MaskMode mode);

static gs_texture_t *apply_gpu_track_matte_texture(
    TitleGpuRenderSession *session, const Title &title,
    gs_texture_t *target_texture, const Layer &mask_layer,
    double title_time, const std::string &raster_id,
    const std::string &compositor_group_id, MaskMode mode);

static gs_texture_t *render_gpu_adjustment_coverage(
    TitleGpuRenderSession *session, const Title &title, const Layer &layer,
    double title_time)
{
    const double resolved_time = gpu_layer_render_time(title, layer, title_time);
    if (!session || !session->adjustment_coverage_target)
        return nullptr;
    if (!session->adjustment_coverage_effect &&
        !ensure_session_shader_effect(
            session, session->adjustment_coverage_effect,
            "core:adjustment-coverage", "Adjustment coverage",
            kGpuAdjustmentCoverageEffect,
            "obs-fxm-adjustment-coverage.effect",
            &session->adjustment_coverage_effect))
        return nullptr;
    struct vec4 clear; vec4_zero(&clear);
    if (!begin_gpu_target(session->adjustment_coverage_target,
                          session->width, session->height, clear))
        return nullptr;
    const double lt = std::max(0.0, resolved_time - layer.in_time);
    const double w = std::max(0.0001, eval_box_width(layer, lt));
    const double h = std::max(0.0001, eval_box_height(layer, lt));
    gs_enable_blending(false);
    gs_matrix_push();
    gs_matrix_identity();
    gs_matrix_scale3f(
        static_cast<float>(gpu_session_render_scale_x(session)),
        static_cast<float>(gpu_session_render_scale_y(session)), 1.0f);
    apply_layer_world_transform_gs(title, layer, title_time, 0);
    gs_matrix_translate3f(static_cast<float>(-eval_origin_x(layer, lt) * w),
                          static_cast<float>(-eval_origin_y(layer, lt) * h), 0.0f);
    while (gs_effect_loop(session->adjustment_coverage_effect, "Draw"))
        gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(std::ceil(w)),
                       static_cast<uint32_t>(std::ceil(h)));
    gs_matrix_pop();
    gs_texrender_end(session->adjustment_coverage_target);
    gs_texture_t *coverage = fxm::obs_plugin::native_texture(
        session->adjustment_coverage_target_resource->texture());
    if (!coverage)
        return nullptr;
    if (layer.mask_mode != MaskMode::None && !layer.mask_source_id.empty()) {
        const Layer *mask_layer = find_layer_by_id(title, layer.mask_source_id);
        if (mask_layer && gpu_layer_chain_active_as_matte(title, *mask_layer, title_time)) {
            coverage = apply_gpu_track_matte_texture(
                session, title, coverage, *mask_layer, title_time,
                std::string(), layer.parent_id, layer.mask_mode);
            if (!coverage)
                return nullptr;
        } else if (!mask_mode_is_inverted(layer.mask_mode)) {
            return nullptr;
        }
    }
    return coverage;
}

static gs_texture_t *mix_gpu_adjustment_layer(
    TitleGpuRenderSession *session, const Title &title, const Layer &layer,
    double title_time, gs_texture_t *original, gs_texture_t *effected,
    gs_texture_t *coverage, gs_texrender_t *target,
    bool bounded_backdrop = false, double opacity_override = -1.0)
{
    const double resolved_time = gpu_layer_render_time(title, layer, title_time);
    if (!session || !original || !effected || !coverage || !target)
        return original;
    if (!session->adjustment_mix_effect &&
        !ensure_session_shader_effect(
            session, session->adjustment_mix_effect, "core:adjustment-mix",
            "Adjustment mix", kGpuAdjustmentMixEffect,
            "obs-fxm-adjustment-mix.effect",
            &session->adjustment_mix_effect))
        return original;
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return original;
    gs_effect_t *effect = session->adjustment_mix_effect;
    if (gs_eparam_t *param = fxm_effect_param(effect, "originalImage"))
        gs_effect_set_texture(param, original);
    if (gs_eparam_t *param = fxm_effect_param(effect, "effectedImage"))
        gs_effect_set_texture(param, effected);
    if (gs_eparam_t *param = fxm_effect_param(effect, "coverageImage"))
        gs_effect_set_texture(param, coverage);
    /* A bounded backdrop effect is a replacement of the pixels behind the
     * artwork, not another semi-transparent copy of them.  Layer opacity and
     * blend mode belong exclusively to the later artwork composite. */
    const double resolved_opacity = opacity_override >= 0.0
        ? opacity_override
        : gpu_layer_chain_opacity(title, layer, title_time);
    const float amount = bounded_backdrop
        ? 1.0f
        : static_cast<float>(std::clamp(resolved_opacity, 0.0, 1.0));
    set_effect_float_param(effect, "amount", amount);
    set_effect_int_param(effect, "blendMode", static_cast<int>(
        bounded_backdrop ? EffectBlendMode::Normal : layer.blend_mode));
    bool invert_coverage = false;
    for (const LayerEffect &candidate : layer.effects) {
        if (candidate.enabled && candidate.affect_layers_behind && candidate.affect_layers_behind_invert) {
            invert_coverage = true;
            break;
        }
    }
    set_effect_int_param(effect, "coverageInvert", invert_coverage ? 1 : 0);
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time,
        gpu_layer_transition_time(title, layer, title_time));
    set_gpu_transition_effect_params(session, effect, transition);
    gs_enable_blending(false);
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(original, 0, session->width, session->height);
    gs_texrender_end(target);
    gs_texture_t *result = gs_texrender_get_texture(target);
    return result ? result : original;
}

static Vec2Value render_quad_value(const AnimatedVec2Property &prop, float x, float y, double local_time)
{
    if (prop.is_animated() || prop.static_value.x != 0.0 || prop.static_value.y != 0.0)
        return prop.evaluate(local_time);
    return {x, y};
}

static bool layer_has_free_transform_quad(const Layer &layer, double local_time)
{
    const Vec2Value values[] = {
        render_quad_value(layer.transform_quad_tl, layer.transform_quad_tl_x, layer.transform_quad_tl_y, local_time),
        render_quad_value(layer.transform_quad_tr, layer.transform_quad_tr_x, layer.transform_quad_tr_y, local_time),
        render_quad_value(layer.transform_quad_br, layer.transform_quad_br_x, layer.transform_quad_br_y, local_time),
        render_quad_value(layer.transform_quad_bl, layer.transform_quad_bl_x, layer.transform_quad_bl_y, local_time)};
    for (const auto &value : values)
        if (std::abs(value.x) > 0.00001 || std::abs(value.y) > 0.00001) return true;
    return false;
}

static gs_vertbuffer_t *create_free_transform_grid(
    const Layer &layer, uint32_t texture_width, uint32_t texture_height,
    double logical_width, double logical_height, const QPointF &origin,
    double base_box_width, double base_box_height, double raster_sx,
    double raster_sy, double origin_x, double origin_y, double local_time)
{
    /* A coarse fixed grid makes glyph edges visibly kink at cell boundaries,
     * especially when a text layer is compressed into a strong perspective
     * trapezoid.  Use an adaptive grid so each cell covers roughly 20-24
     * source pixels, while keeping the mesh bounded for realtime playback. */
    const int divisions = std::clamp(
        static_cast<int>(std::ceil(std::max(texture_width, texture_height) / 22.0)),
        24, 96);
    const int quads = divisions * divisions;
    gs_vb_data *data = gs_vbdata_create();
    if (!data) return nullptr;
    data->num = static_cast<size_t>(quads * 6);
    data->points = static_cast<vec3 *>(bzalloc(sizeof(vec3) * data->num));
    data->num_tex = 1;
    data->tvarray = static_cast<gs_tvertarray *>(bzalloc(sizeof(gs_tvertarray)));
    if (!data->points || !data->tvarray) { gs_vbdata_destroy(data); return nullptr; }
    data->tvarray[0].width = 2;
    data->tvarray[0].array = bzalloc(sizeof(vec2) * data->num);
    if (!data->tvarray[0].array) { gs_vbdata_destroy(data); return nullptr; }
    auto *uvs = static_cast<vec2 *>(data->tvarray[0].array);

    const Vec2Value qtl = render_quad_value(
        layer.transform_quad_tl, layer.transform_quad_tl_x,
        layer.transform_quad_tl_y, local_time);
    const Vec2Value qtr = render_quad_value(
        layer.transform_quad_tr, layer.transform_quad_tr_x,
        layer.transform_quad_tr_y, local_time);
    const Vec2Value qbr = render_quad_value(
        layer.transform_quad_br, layer.transform_quad_br_x,
        layer.transform_quad_br_y, local_time);
    const Vec2Value qbl = render_quad_value(
        layer.transform_quad_bl, layer.transform_quad_bl_x,
        layer.transform_quad_bl_y, local_time);

    /* Map the unit square to the distorted layer quad with one projective
     * transform.  Bilinear corner-offset interpolation bends horizontal text
     * rows and makes neighboring mesh cells disagree visually.  A homography
     * preserves straight lines, which is the expected behavior for perspective
     * and free-distort transforms. */
    const double x0 = qtl.x;
    const double y0 = qtl.y;
    const double x1 = 1.0 + qtr.x;
    const double y1 = qtr.y;
    const double x2 = 1.0 + qbr.x;
    const double y2 = 1.0 + qbr.y;
    const double x3 = qbl.x;
    const double y3 = 1.0 + qbl.y;

    const double dx1 = x1 - x2;
    const double dx2 = x3 - x2;
    const double dy1 = y1 - y2;
    const double dy2 = y3 - y2;
    const double sxq = x0 - x1 + x2 - x3;
    const double syq = y0 - y1 + y2 - y3;
    const double det = dx1 * dy2 - dx2 * dy1;

    double g = 0.0;
    double h = 0.0;
    if (std::abs(det) > 1.0e-10) {
        g = (sxq * dy2 - dx2 * syq) / det;
        h = (dx1 * syq - sxq * dy1) / det;
    }
    const double a = x1 - x0 + g * x1;
    const double b = x3 - x0 + h * x3;
    const double c = x0;
    const double d = y1 - y0 + g * y1;
    const double e = y3 - y0 + h * y3;
    const double f = y0;

    size_t out = 0;
    auto vertex = [&](double u, double v) {
        const double logical_x = origin.x() + u * logical_width;
        const double logical_y = origin.y() + v * logical_height;
        const double nx = (logical_x + origin_x * base_box_width) /
            std::max(0.0001, base_box_width);
        const double ny = (logical_y + origin_y * base_box_height) /
            std::max(0.0001, base_box_height);

        double denom = g * nx + h * ny + 1.0;
        if (std::abs(denom) < 1.0e-8)
            denom = std::copysign(1.0e-8, denom == 0.0 ? 1.0 : denom);
        const double warped_x = (a * nx + b * ny + c) / denom;
        const double warped_y = (d * nx + e * ny + f) / denom;
        const double dx = (warped_x - nx) * base_box_width /
            std::max(0.000001, raster_sx);
        const double dy = (warped_y - ny) * base_box_height /
            std::max(0.000001, raster_sy);

        vec3_set(&data->points[out],
                 static_cast<float>(u * texture_width + dx),
                 static_cast<float>(v * texture_height + dy), 0.0f);
        vec2_set(&uvs[out], static_cast<float>(u), static_cast<float>(v));
        ++out;
    };
    for (int y = 0; y < divisions; ++y) {
        for (int x = 0; x < divisions; ++x) {
            const double u0 = static_cast<double>(x) / divisions;
            const double v0 = static_cast<double>(y) / divisions;
            const double u1 = static_cast<double>(x + 1) / divisions;
            const double v1 = static_cast<double>(y + 1) / divisions;
            vertex(u0,v0); vertex(u1,v0); vertex(u1,v1);
            vertex(u0,v0); vertex(u1,v1); vertex(u0,v1);
        }
    }
    return gs_vertexbuffer_create(data, 0);
}

static struct matrix4 qmatrix4x4_to_gs(const QMatrix4x4 &source)
{
    struct matrix4 result;
    vec4_set(&result.x, source(0, 0), source(1, 0),
             source(2, 0), source(3, 0));
    vec4_set(&result.y, source(0, 1), source(1, 1),
             source(2, 1), source(3, 1));
    vec4_set(&result.z, source(0, 2), source(1, 2),
             source(2, 2), source(3, 2));
    vec4_set(&result.t, source(0, 3), source(1, 3),
             source(2, 3), source(3, 3));
    return result;
}

static QMatrix4x4 gpu_canvas_camera_model_view(
    const QMatrix4x4 &qt_model_view)
{
    /* Qt projects into Y-up NDC and project_point() explicitly converts that
     * result to the canvas' Y-down coordinates. libobs' gs_perspective/ortho
     * already owns the top-to-bottom viewport convention, so feeding it the
     * unadjusted Qt camera mirrors world translation on Y. Convert only the
     * camera-space Y basis at this API boundary; authored positions, parent
     * matrices, keyframes and editor hit-testing remain consistently Y-down. */
    QMatrix4x4 qt_to_gs_camera;
    qt_to_gs_camera.setToIdentity();
    qt_to_gs_camera.scale(1.0f, -1.0f, 1.0f);
    return qt_to_gs_camera * qt_model_view;
}

static struct matrix4 gpu_qtransform_to_gs_matrix(
    const QTransform &transform)
{
    struct matrix4 result;
    matrix4_identity(&result);
    vec4_set(&result.x,
             static_cast<float>(transform.m11()),
             static_cast<float>(transform.m12()),
             0.0f, static_cast<float>(transform.m13()));
    vec4_set(&result.y,
             static_cast<float>(transform.m21()),
             static_cast<float>(transform.m22()),
             0.0f, static_cast<float>(transform.m23()));
    vec4_set(&result.t,
             static_cast<float>(transform.dx()),
             static_cast<float>(transform.dy()),
             0.0f, static_cast<float>(transform.m33()));
    return result;
}

static bool set_gpu_layer_lighting_params(
    TitleGpuRenderSession *session, gs_effect_t *effect,
    gs_texture_t *fallback_texture, const Title &title, const Layer &layer,
    double title_time, double local_time, const QPointF &origin,
    double logical_width, double logical_height, double sx, double sy,
    double local_z_offset)
{
    if (!session || !effect || !fallback_texture)
        return false;

    auto param = [effect](const char *name) {
        return fxm_effect_param(effect, name);
    };
    gs_eparam_t *lighting_enabled = param("lightingEnabled");
    gs_eparam_t *plane_origin = param("planeOrigin");
    gs_eparam_t *plane_axis_u = param("planeAxisU");
    gs_eparam_t *plane_axis_v = param("planeAxisV");
    gs_eparam_t *world_normal = param("worldNormal");
    gs_eparam_t *camera_world_position = param("cameraWorldPosition");
    gs_eparam_t *material_primary = param("materialPrimary");
    gs_eparam_t *material_pbr = param("materialPbr");
    gs_eparam_t *material_emissive = param("materialEmissive");
    gs_eparam_t *environment_exposure = param("environmentExposure");
    if (!lighting_enabled || !plane_origin || !plane_axis_u || !plane_axis_v ||
        !world_normal || !camera_world_position || !material_primary ||
        !material_pbr || !material_emissive || !environment_exposure)
        return false;

    const bool enabled = title.lighting_enabled &&
        layer.material_accepts_lights &&
        fxm::transform3d::layer_supports_3d(layer) &&
        fxm::transform3d::layer_or_ancestor_uses_3d(title, layer);
    gs_effect_set_int(lighting_enabled, enabled ? 1 : 0);

    const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(
        title, layer, title_time);
    const float local_x0 = static_cast<float>(sx * origin.x());
    const float local_y0 = static_cast<float>(sy * origin.y());
    const float local_x1 = local_x0 + static_cast<float>(sx * logical_width);
    const float local_y1 = local_y0 + static_cast<float>(sy * logical_height);
    const float local_z = static_cast<float>(local_z_offset);
    const QVector3D p00 = world.map(QVector3D(local_x0, local_y0, local_z));
    const QVector3D p10 = world.map(QVector3D(local_x1, local_y0, local_z));
    const QVector3D p01 = world.map(QVector3D(local_x0, local_y1, local_z));
    const fxm::transform3d::EvaluatedTransform evaluated =
        fxm::transform3d::evaluate(title, layer, title_time);

    auto set_vec3 = [](gs_eparam_t *destination, const QVector3D &value) {
        struct vec3 v;
        vec3_set(&v, value.x(), value.y(), value.z());
        gs_effect_set_vec3(destination, &v);
    };
    auto set_vec4 = [](gs_eparam_t *destination, float x, float y,
                       float z, float w) {
        struct vec4 v;
        vec4_set(&v, x, y, z, w);
        gs_effect_set_vec4(destination, &v);
    };
    set_vec3(plane_origin, p00);
    set_vec3(plane_axis_u, p10 - p00);
    set_vec3(plane_axis_v, p01 - p00);
    set_vec3(world_normal, evaluated.world_normal);
    set_vec3(camera_world_position, evaluated.camera_position);

    set_vec4(material_primary,
             static_cast<float>(std::clamp(
                 layer.material_ambient.evaluate(local_time), 0.0, 4.0)),
             static_cast<float>(std::clamp(
                 layer.material_diffuse.evaluate(local_time), 0.0, 4.0)),
             static_cast<float>(std::clamp(
                 layer.material_specular.evaluate(local_time), 0.0, 4.0)),
             static_cast<float>(std::clamp(
                 layer.material_shininess.evaluate(local_time), 1.0, 512.0)));
    set_vec4(material_pbr,
             static_cast<float>(std::clamp(
                 layer.material_metallic.evaluate(local_time), 0.0, 1.0)),
             static_cast<float>(std::clamp(
                 layer.material_roughness.evaluate(local_time), 0.02, 1.0)),
             layer.material_appears_in_reflections
                 ? static_cast<float>(std::clamp(
                       layer.material_reflection_intensity.evaluate(local_time),
                       0.0, 4.0))
                 : 0.0f,
             static_cast<float>(std::clamp(
                 layer.material_emissive_intensity.evaluate(local_time),
                 0.0, 16.0)));
    const uint32_t emissive = evaluated_material_emissive_color(layer, local_time);
    set_vec4(material_emissive,
             static_cast<float>((emissive >> 16) & 0xFFu) / 255.0f,
             static_cast<float>((emissive >> 8) & 0xFFu) / 255.0f,
             static_cast<float>(emissive & 0xFFu) / 255.0f,
             static_cast<float>((emissive >> 24) & 0xFFu) / 255.0f);
    gs_effect_set_float(environment_exposure,
        static_cast<float>(std::pow(2.0,
            std::clamp(title.environment_exposure, -16.0, 16.0))));

    struct EvaluatedLightSlot {
        TitleLightType type = TitleLightType::Ambient;
        TitleLightFalloff falloff = TitleLightFalloff::None;
        QVector3D position;
        QVector3D direction {0.0f, 0.0f, 1.0f};
        QVector3D color {0.0f, 0.0f, 0.0f};
        float intensity = 0.0f;
        float source_size = 0.0f;
        float falloff_start = 0.0f;
        float falloff_distance = 1.0f;
        float cone_outer_cos = -1.0f;
        float cone_inner_cos = -1.0f;
        bool enabled = false;
    };
    std::array<EvaluatedLightSlot, 4> light_slots {};
    std::size_t slot_count = 0;

    auto append_light = [&](const TitleLight &light) {
        if (!light.enabled || slot_count >= light_slots.size())
            return;
        EvaluatedLightSlot &slot = light_slots[slot_count++];
        slot.type = light.type;
        slot.falloff = light.falloff;
        const Vec3Value position_value = evaluated_light_position(light, title_time);
        const Vec3Value target_value = evaluated_light_target(light, title_time);
        slot.position = QVector3D(static_cast<float>(position_value.x),
                                  static_cast<float>(position_value.y),
                                  static_cast<float>(position_value.z));
        const QVector3D target(static_cast<float>(target_value.x),
                               static_cast<float>(target_value.y),
                               static_cast<float>(target_value.z));
        slot.direction = target - slot.position;
        if (slot.direction.lengthSquared() <= 0.000001f)
            slot.direction = QVector3D(0.0f, 0.0f, 1.0f);
        else
            slot.direction.normalize();
        const uint32_t color = evaluated_light_color(light, title_time);
        const float alpha = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
        slot.color = QVector3D(
            static_cast<float>((color >> 16) & 0xFFu) / 255.0f,
            static_cast<float>((color >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(color & 0xFFu) / 255.0f);
        slot.intensity = static_cast<float>(std::max(
            0.0, light.intensity.evaluate(title_time) / 100.0)) * alpha;
        slot.source_size = static_cast<float>(std::clamp(
            light.source_size.evaluate(title_time), 0.1, 100000.0));
        /* Falloff start is no longer authored independently. It tracks the
         * emitter Source Size so the lit core and soft source representation
         * stay coherent. */
        slot.falloff_start = slot.source_size;
        slot.falloff_distance = static_cast<float>(std::max(
            0.0001, light.falloff_distance.evaluate(title_time)));
        const double outer_half = std::clamp(
            light.cone_angle.evaluate(title_time), 0.1, 179.0) *
            0.5 * 3.14159265358979323846 / 180.0;
        const double feather = std::clamp(
            light.cone_feather.evaluate(title_time) / 100.0, 0.0, 1.0);
        const double inner_half = outer_half * (1.0 - feather);
        slot.cone_outer_cos = static_cast<float>(std::cos(outer_half));
        slot.cone_inner_cos = static_cast<float>(std::cos(inner_half));
        slot.enabled = slot.intensity > 0.000001f;
    };

    std::size_t authored_light_count = 0;
    for (const auto &light_layer : title.layers) {
        if (!light_layer || light_layer->type != LayerType::Light || !light_layer->visible) continue;
        TitleLight evaluated = light_layer->light;
        evaluated.enabled = evaluated.enabled && light_layer->visible;
        evaluated.id = light_layer->id; evaluated.name = light_layer->name;
        const QMatrix4x4 world = fxm::transform3d::layer_world_matrix(title, *light_layer, title_time);
        const QVector3D world_position = world.map(QVector3D(static_cast<float>(evaluated.position.evaluate(title_time).x), static_cast<float>(evaluated.position.evaluate(title_time).y), static_cast<float>(evaluated.position.evaluate(title_time).z)));
        const Vec3Value local_target = evaluated.target.evaluate(title_time);
        const QVector3D world_target = world.map(QVector3D(static_cast<float>(local_target.x), static_cast<float>(local_target.y), static_cast<float>(local_target.z)));
        evaluated.position.static_value = {world_position.x(), world_position.y(), world_position.z()};
        evaluated.position.keyframes.clear();
        evaluated.target.static_value = {world_target.x(), world_target.y(), world_target.z()};
        evaluated.target.keyframes.clear();
        append_light(evaluated); ++authored_light_count;
    }
    for (const TitleLight &light : title.lights) { append_light(light); ++authored_light_count; }
    if (authored_light_count == 0 && title.default_light_enabled &&
        slot_count < light_slots.size()) {
        EvaluatedLightSlot &slot = light_slots[slot_count++];
        slot.type = TitleLightType::Parallel;
        slot.falloff = TitleLightFalloff::None;
        slot.position = QVector3D(static_cast<float>(title.width) * 0.5f,
                                  static_cast<float>(title.height) * 0.5f,
                                  -1000.0f);
        slot.direction = QVector3D(0.0f, 0.0f, 1.0f);
        slot.color = QVector3D(1.0f, 1.0f, 1.0f);
        slot.intensity = 1.0f;
        slot.enabled = true;
    }

    for (std::size_t index = 0; index < light_slots.size(); ++index) {
        const std::string suffix = std::to_string(index);
        gs_eparam_t *position_type = param(
            (std::string("lightPositionType") + suffix).c_str());
        gs_eparam_t *direction_intensity = param(
            (std::string("lightDirectionIntensity") + suffix).c_str());
        gs_eparam_t *color_enabled = param(
            (std::string("lightColorEnabled") + suffix).c_str());
        gs_eparam_t *light_params = param(
            (std::string("lightParams") + suffix).c_str());
        gs_eparam_t *source_size = param(
            (std::string("lightSourceSize") + suffix).c_str());
        gs_eparam_t *falloff = param(
            (std::string("lightFalloff") + suffix).c_str());
        gs_eparam_t *shadow_map = param(
            (std::string("shadowMap") + suffix).c_str());
        gs_eparam_t *shadow_enabled = param(
            (std::string("shadowEnabled") + suffix).c_str());
        gs_eparam_t *shadow_kind = param(
            (std::string("shadowKind") + suffix).c_str());
        gs_eparam_t *shadow_view_projection = param(
            (std::string("shadowViewProj") + suffix).c_str());
        gs_eparam_t *shadow_light_position = param(
            (std::string("shadowLightPosition") + suffix).c_str());
        gs_eparam_t *shadow_basis_right_center_x = param(
            (std::string("shadowBasisRightCenterX") + suffix).c_str());
        gs_eparam_t *shadow_basis_up_center_y = param(
            (std::string("shadowBasisUpCenterY") + suffix).c_str());
        gs_eparam_t *shadow_basis_forward = param(
            (std::string("shadowBasisForward") + suffix).c_str());
        gs_eparam_t *shadow_near = param(
            (std::string("shadowNear") + suffix).c_str());
        gs_eparam_t *shadow_far = param(
            (std::string("shadowFar") + suffix).c_str());
        gs_eparam_t *shadow_projection_span = param(
            (std::string("shadowProjectionSpan") + suffix).c_str());
        gs_eparam_t *shadow_darkness = param(
            (std::string("shadowDarkness") + suffix).c_str());
        gs_eparam_t *shadow_bias = param(
            (std::string("shadowBias") + suffix).c_str());
        gs_eparam_t *shadow_softness = param(
            (std::string("shadowSoftness") + suffix).c_str());
        gs_eparam_t *shadow_texel_size = param(
            (std::string("shadowTexelSize") + suffix).c_str());
        if (!position_type || !direction_intensity || !color_enabled ||
            !light_params || !source_size || !falloff || !shadow_map ||
            !shadow_enabled || !shadow_kind || !shadow_view_projection ||
            !shadow_light_position || !shadow_basis_right_center_x ||
            !shadow_basis_up_center_y || !shadow_basis_forward ||
            !shadow_near || !shadow_far || !shadow_projection_span ||
            !shadow_darkness || !shadow_bias ||
            !shadow_softness || !shadow_texel_size)
            return false;
        const EvaluatedLightSlot &slot = light_slots[index];
        set_vec4(position_type, slot.position.x(), slot.position.y(),
                 slot.position.z(), static_cast<float>(slot.type));
        set_vec4(direction_intensity, slot.direction.x(), slot.direction.y(),
                 slot.direction.z(), slot.intensity);
        set_vec4(color_enabled, slot.color.x(), slot.color.y(), slot.color.z(),
                 slot.enabled ? 1.0f : 0.0f);
        set_vec4(light_params, slot.falloff_start, slot.falloff_distance,
                 slot.cone_outer_cos, slot.cone_inner_cos);
        gs_effect_set_float(source_size, slot.source_size);
        gs_effect_set_int(falloff, static_cast<int>(slot.falloff));

        gs_texrender_t *shadow_target = session->shadow_targets[index];
        gs_texture_t *shadow_texture = shadow_target
            ? gs_texrender_get_texture(shadow_target) : nullptr;
        const bool receives_shadow = enabled &&
            layer.dimension_mode == LayerDimensionMode::ThreeD &&
            layer.material_accepts_shadows &&
            session->shadow_map_kind[index] != 0 &&
            session->shadow_map_valid[index] && shadow_texture;
        if (!session->shadow_receiver_binding_logged[index] &&
            layer.dimension_mode == LayerDimensionMode::ThreeD &&
            session->shadow_map_valid[index]) {
            session->shadow_receiver_binding_logged[index] = true;
            FXM_LOG_INFO("Shadows", QStringLiteral(
                "stage=receiver-bind slot=%1 result=%2 layer=%3 acceptsLights=%4 acceptsShadows=%5 mapValid=%6 texture=%7 kind=%8 darkness=%9 bias=%10 softness=%11 sourceSize=%12 light=(%13,%14,%15) plane=(%16,%17,%18)")
                .arg(static_cast<qulonglong>(index))
                .arg(receives_shadow ? QStringLiteral("enabled")
                                     : QStringLiteral("disabled"))
                .arg(QString::fromStdString(layer.name))
                .arg(layer.material_accepts_lights ? 1 : 0)
                .arg(layer.material_accepts_shadows ? 1 : 0)
                .arg(session->shadow_map_valid[index] ? 1 : 0)
                .arg(shadow_texture ? 1 : 0)
                .arg(session->shadow_map_kind[index])
                .arg(session->shadow_darkness[index], 0, 'f', 4)
                .arg(session->shadow_bias[index], 0, 'f', 7)
                .arg(session->shadow_softness[index], 0, 'f', 4)
                .arg(slot.source_size, 0, 'f', 4)
                .arg(session->shadow_light_positions[index].x(), 0, 'f', 3)
                .arg(session->shadow_light_positions[index].y(), 0, 'f', 3)
                .arg(session->shadow_light_positions[index].z(), 0, 'f', 3)
                .arg(p00.x(), 0, 'f', 3)
                .arg(p00.y(), 0, 'f', 3)
                .arg(p00.z(), 0, 'f', 3));
        }
        gs_effect_set_texture(shadow_map,
            receives_shadow ? shadow_texture : fallback_texture);
        gs_effect_set_int(shadow_enabled, receives_shadow ? 1 : 0);
        gs_effect_set_int(shadow_kind,
            receives_shadow ? session->shadow_map_kind[index] : 0);
        const struct matrix4 shadow_matrix = qmatrix4x4_to_gs(
            session->shadow_view_projections[index]);
        gs_effect_set_matrix4(shadow_view_projection, &shadow_matrix);
        set_vec3(shadow_light_position,
                 session->shadow_light_positions[index]);
        set_vec4(shadow_basis_right_center_x,
                 session->shadow_light_rights[index].x(),
                 session->shadow_light_rights[index].y(),
                 session->shadow_light_rights[index].z(),
                 session->shadow_projection_center_x[index]);
        set_vec4(shadow_basis_up_center_y,
                 session->shadow_light_ups[index].x(),
                 session->shadow_light_ups[index].y(),
                 session->shadow_light_ups[index].z(),
                 session->shadow_projection_center_y[index]);
        set_vec3(shadow_basis_forward,
                 session->shadow_light_forwards[index]);
        gs_effect_set_float(shadow_near,
            session->shadow_near_plane[index]);
        gs_effect_set_float(shadow_far,
            session->shadow_far_plane[index]);
        struct vec2 shadow_projection;
        vec2_set(&shadow_projection,
            session->shadow_projection_span_x[index],
            session->shadow_projection_span_y[index]);
        gs_effect_set_vec2(shadow_projection_span, &shadow_projection);
        gs_effect_set_float(shadow_darkness,
            session->shadow_darkness[index]);
        gs_effect_set_float(shadow_bias, session->shadow_bias[index]);
        gs_effect_set_float(shadow_softness,
            session->shadow_softness[index]);
        struct vec2 shadow_texel;
        vec2_set(&shadow_texel,
            1.0f / static_cast<float>(std::max(
                1u, session->shadow_map_width[index])),
            1.0f / static_cast<float>(std::max(
                1u, session->shadow_map_height[index])));
        gs_effect_set_vec2(shadow_texel_size, &shadow_texel);
    }
    return true;
}

static bool draw_gpu_layer_texture(TitleGpuRenderSession *session,
                                   TitleGpuRenderSession::LayerRaster *raster_entry,
                                   const Title &title,
                                   const Layer &layer,
                                   gs_texture_t *texture,
                                   const QPointF &origin,
                                   uint32_t texture_width,
                                   uint32_t texture_height,
                                   double logical_width,
                                   double logical_height,
                                   const QRectF &image_clip_rect,
                                   double base_box_width,
                                   double base_box_height,
                                   double title_time,
                                   float weight,
                                   bool hardware_depth = false,
                                   gs_texture_t *post_projection_mask = nullptr,
                                   MaskMode post_projection_mask_mode = MaskMode::None)
{
    const double resolved_time = gpu_layer_render_time(title, layer, title_time);
    if (!session || !texture || weight <= 0.0f)
        return false;
    if (!session->copy_effect &&
        !ensure_session_shader_effect(
            session, session->copy_effect, "core:layer-copy",
            "GPU layer copy", kGpuLayerCopyEffect,
            "obs-fxm-gpu-layer-copy.effect", &session->copy_effect))
        return false;
    gs_eparam_t *image = fxm_effect_param(session->copy_effect, "image");
    gs_eparam_t *weight_param = fxm_effect_param(session->copy_effect, "weight");
    gs_eparam_t *clip_enabled = fxm_effect_param(session->copy_effect, "imageClipEnabled");
    gs_eparam_t *clip_rect = fxm_effect_param(session->copy_effect, "imageClipRect");
    gs_eparam_t *corner_radii = fxm_effect_param(session->copy_effect, "imageCornerRadii");
    gs_eparam_t *logical_size = fxm_effect_param(session->copy_effect, "imageLogicalSize");
    gs_eparam_t *source_texel_size = fxm_effect_param(session->copy_effect, "sourceTexelSize");
    gs_eparam_t *image_scale_filter = fxm_effect_param(session->copy_effect, "imageScaleFilter");
    gs_eparam_t *projected_mask = fxm_effect_param(
        session->copy_effect, "postProjectionMask");
    gs_eparam_t *projected_mask_enabled = fxm_effect_param(
        session->copy_effect, "postProjectionMaskEnabled");
    gs_eparam_t *projected_mask_mode_param = fxm_effect_param(
        session->copy_effect, "postProjectionMaskMode");
    if (!image || !weight_param || !clip_enabled || !clip_rect ||
        !corner_radii || !logical_size || !source_texel_size ||
        !image_scale_filter || !projected_mask ||
        !projected_mask_enabled || !projected_mask_mode_param)
        return false;
    gs_effect_set_texture(image, texture);
    gs_effect_set_float(weight_param, weight);
    struct vec2 source_texel_value;
    vec2_set(&source_texel_value,
             1.0f / static_cast<float>(std::max(1u, texture_width)),
             1.0f / static_cast<float>(std::max(1u, texture_height)));
    gs_effect_set_vec2(source_texel_size, &source_texel_value);
    gs_effect_set_int(image_scale_filter, layer_type_is_image_like(layer.type)
        ? static_cast<int>(layer.scale_filter)
        : static_cast<int>(ImageScaleFilter::Bilinear));
    const bool use_projected_mask = post_projection_mask &&
        post_projection_mask_mode != MaskMode::None;
    gs_effect_set_texture(projected_mask, use_projected_mask
        ? post_projection_mask : texture);
    gs_effect_set_int(projected_mask_enabled, use_projected_mask ? 1 : 0);
    gs_effect_set_int(projected_mask_mode_param,
                      static_cast<int>(post_projection_mask_mode));
    const LayerTransitionVisualState transition = evaluate_layer_general_transitions(
        layer.transitions, layer.in_time, layer.out_time,
        gpu_layer_transition_time(title, layer, title_time));
    set_gpu_transition_effect_params(session, session->copy_effect, transition);
    const bool gpu_image_clip =
        (layer_type_is_image_like(layer.type) ||
         layer.type == LayerType::TransitionInput) &&
        image_clip_rect.isValid() && !image_clip_rect.isEmpty() &&
        (layer.image_crop_when_outside_box ||
         layer.corner_radius_tl > 0.01f || layer.corner_radius_tr > 0.01f ||
         layer.corner_radius_br > 0.01f || layer.corner_radius_bl > 0.01f);
    gs_effect_set_int(clip_enabled, gpu_image_clip ? 1 : 0);
    struct vec4 clip_value;
    vec4_set(&clip_value,
             static_cast<float>(image_clip_rect.x()),
             static_cast<float>(image_clip_rect.y()),
             static_cast<float>(image_clip_rect.width()),
             static_cast<float>(image_clip_rect.height()));
    gs_effect_set_vec4(clip_rect, &clip_value);
    struct vec4 radii_value;
    vec4_set(&radii_value, layer.corner_radius_tl, layer.corner_radius_tr,
             layer.corner_radius_br, layer.corner_radius_bl);
    gs_effect_set_vec4(corner_radii, &radii_value);
    struct vec2 logical_value;
    vec2_set(&logical_value, static_cast<float>(std::max(0.0001, logical_width)),
             static_cast<float>(std::max(0.0001, logical_height)));
    gs_effect_set_vec2(logical_size, &logical_value);
    const double local_time = std::max(0.0, resolved_time - layer.in_time);
    const double current_box_width = std::max(
        0.0001, eval_box_width(layer, local_time));
    const double current_box_height = std::max(
        0.0001, eval_box_height(layer, local_time));
    const double sx = current_box_width / std::max(0.0001, base_box_width);
    const double sy = current_box_height / std::max(0.0001, base_box_height);
    const double raster_sx = logical_width > 0.0
        ? logical_width / std::max(1.0, static_cast<double>(texture_width))
        : 1.0;
    const double raster_sy = logical_height > 0.0
        ? logical_height / std::max(1.0, static_cast<double>(texture_height))
        : 1.0;

    if (!set_gpu_layer_lighting_params(
            session, session->copy_effect, texture, title, layer, resolved_time,
            local_time, origin, logical_width, logical_height, sx, sy, 0.0))
        return false;

    /* The bounding-box overlay already projects the real local rectangle.
     * The fallback artwork path previously projected a unit square and then
     * extrapolated that homography to texture coordinates. With an off-centre
     * anchor those coordinates can be hundreds or thousands of units from the
     * fitted square, so conversion to the float OBS matrix produced visible
     * scale pulsation while moving. Fit the homography to the actual raster
     * rectangle instead, making content and overlays use the same geometry. */
    const bool projected_3d = !hardware_depth &&
        fxm::transform3d::layer_supports_3d(layer) &&
        fxm::transform3d::layer_or_ancestor_uses_3d(title, layer);
    bool exact_projected_raster = false;
    QTransform texture_to_canvas;
    QPolygonF texture_quad;
    if (projected_3d && texture_width > 0 && texture_height > 0) {
        const double local_x0 = sx * origin.x();
        const double local_y0 = sy * origin.y();
        const double local_x1 = local_x0 + sx * logical_width;
        const double local_y1 = local_y0 + sy * logical_height;
        texture_quad << QPointF(0.0, 0.0)
                     << QPointF(static_cast<double>(texture_width), 0.0)
                     << QPointF(static_cast<double>(texture_width),
                                static_cast<double>(texture_height))
                     << QPointF(0.0, static_cast<double>(texture_height));
        QPolygonF local_quad;
        local_quad << QPointF(local_x0, local_y0)
                   << QPointF(local_x1, local_y0)
                   << QPointF(local_x1, local_y1)
                   << QPointF(local_x0, local_y1);
        exact_projected_raster =
            fxm::transform3d::projected_local_quad_transform(
                title, layer, resolved_time, texture_quad, local_quad,
                texture_to_canvas);
        const double frame_scale_x = gpu_session_render_scale_x(session);
        const double frame_scale_y = gpu_session_render_scale_y(session);
        if (exact_projected_raster &&
            (std::abs(frame_scale_x - 1.0) > 0.000001 ||
             std::abs(frame_scale_y - 1.0) > 0.000001)) {
            QPolygonF scaled_canvas_quad;
            scaled_canvas_quad.reserve(4);
            for (const QPointF &source_point : texture_quad) {
                const QPointF canvas_point =
                    texture_to_canvas.map(source_point);
                scaled_canvas_quad << QPointF(
                    canvas_point.x() * frame_scale_x,
                    canvas_point.y() * frame_scale_y);
            }
            exact_projected_raster = QTransform::quadToQuad(
                texture_quad, scaled_canvas_quad, texture_to_canvas);
        }
    }

    bool projection_pushed = false;
    gs_matrix_push();
    if (hardware_depth) {
        fxm::transform3d::CameraRenderState camera_state;
        const std::string render_camera_id =
            fxm::transform3d::resolved_layer_camera_id(title, layer, resolved_time);
        if (!fxm::transform3d::camera_render_state(
                title, resolved_time, camera_state, render_camera_id)) {
            gs_matrix_pop();
            return false;
        }
        gs_projection_push();
        projection_pushed = true;
        if (camera_state.projection == CameraProjection::Orthographic) {
            gs_ortho(-camera_state.ortho_half_width,
                     camera_state.ortho_half_width,
                     -camera_state.ortho_half_height,
                     camera_state.ortho_half_height,
                     camera_state.near_clip, camera_state.far_clip);
        } else {
            gs_perspective(camera_state.vertical_fov, camera_state.aspect,
                           camera_state.near_clip, camera_state.far_clip);
        }
        const QMatrix4x4 model_view = gpu_canvas_camera_model_view(
            camera_state.view * fxm::transform3d::layer_world_matrix(
                title, layer, resolved_time));
        const struct matrix4 model_view_gs = qmatrix4x4_to_gs(model_view);
        gs_matrix_set(&model_view_gs);
    } else if (exact_projected_raster) {
        const struct matrix4 projected =
            gpu_qtransform_to_gs_matrix(texture_to_canvas);
        gs_matrix_set(&projected);
    } else {
        gs_matrix_identity();
        gs_matrix_scale3f(
            static_cast<float>(gpu_session_render_scale_x(session)),
            static_cast<float>(gpu_session_render_scale_y(session)), 1.0f);
        apply_layer_world_transform_gs(title, layer, resolved_time, 0);
    }

    if (!exact_projected_raster) {
        /* The source texture can have a different pixel resolution from its
         * logical image-box size (native bitmap pixels or bucketed SVG pixels).
         * Fold that ratio into the local scale and express the translation back
         * in texture units so the logical origin is not scaled twice. */
        gs_matrix_scale3f(static_cast<float>(sx * raster_sx),
                          static_cast<float>(sy * raster_sy), 1.0f);
        gs_matrix_translate3f(
            static_cast<float>(origin.x() /
                               std::max(0.000001, raster_sx)),
            static_cast<float>(origin.y() /
                               std::max(0.000001, raster_sy)),
            0.0f);
    }
    gs_vertbuffer_t *transform_grid = nullptr;
    if (layer_has_free_transform_quad(layer, local_time)) {
        const Vec2Value qtl = render_quad_value(layer.transform_quad_tl, layer.transform_quad_tl_x, layer.transform_quad_tl_y, local_time);
        const Vec2Value qtr = render_quad_value(layer.transform_quad_tr, layer.transform_quad_tr_x, layer.transform_quad_tr_y, local_time);
        const Vec2Value qbr = render_quad_value(layer.transform_quad_br, layer.transform_quad_br_x, layer.transform_quad_br_y, local_time);
        const Vec2Value qbl = render_quad_value(layer.transform_quad_bl, layer.transform_quad_bl_x, layer.transform_quad_bl_y, local_time);
        auto q = [](double v) { return std::llround(v * 1000000.0); };
        std::ostringstream key_stream;
        key_stream << texture_width << 'x' << texture_height << '|'
                   << q(logical_width) << '|' << q(logical_height) << '|'
                   << q(origin.x()) << '|' << q(origin.y()) << '|'
                   << q(base_box_width) << '|' << q(base_box_height) << '|'
                   << q(raster_sx) << '|' << q(raster_sy) << '|'
                   << q(eval_origin_x(layer, local_time)) << '|'
                   << q(eval_origin_y(layer, local_time)) << '|'
                   << q(qtl.x) << '|' << q(qtl.y) << '|'
                   << q(qtr.x) << '|' << q(qtr.y) << '|'
                   << q(qbr.x) << '|' << q(qbr.y) << '|'
                   << q(qbl.x) << '|' << q(qbl.y);
        const std::string grid_key = key_stream.str();
        if (raster_entry) {
            if (!raster_entry->transform_grid || raster_entry->transform_grid_key != grid_key) {
                if (raster_entry->transform_grid)
                    gs_vertexbuffer_destroy(raster_entry->transform_grid);
                raster_entry->transform_grid = create_free_transform_grid(
                    layer, texture_width, texture_height, logical_width, logical_height,
                    origin, base_box_width, base_box_height, raster_sx, raster_sy,
                    eval_origin_x(layer, local_time), eval_origin_y(layer, local_time), local_time);
                raster_entry->transform_grid_key = raster_entry->transform_grid ? grid_key : std::string();
            }
            transform_grid = raster_entry->transform_grid;
        } else {
            transform_grid = create_free_transform_grid(
                layer, texture_width, texture_height, logical_width, logical_height,
                origin, base_box_width, base_box_height, raster_sx, raster_sy,
                eval_origin_x(layer, local_time), eval_origin_y(layer, local_time), local_time);
        }
    }
    auto draw_geometry_pass = [&]() {
        while (gs_effect_loop(session->copy_effect, hardware_depth ? "DepthDraw" : "Draw")) {
            if (transform_grid) {
                gs_load_vertexbuffer(transform_grid); gs_draw(GS_TRIS, 0, 0); gs_load_vertexbuffer(nullptr);
            } else {
                gs_draw_sprite(texture, 0, texture_width, texture_height);
            }
        }
    };
    /* Development Version 330: legacy slice extrusion is deliberately inert.
     * Preserve the old code below only as a short-lived migration reference;
     * no production draw can enter it. */
    constexpr bool legacy_slice_extrusion_production_enabled = false;
    const bool extrudable = legacy_slice_extrusion_production_enabled &&
        hardware_depth && layer.geometry_extrusion_enabled &&
        (layer.type == LayerType::Text || layer.type == LayerType::Clock ||
         layer.type == LayerType::Ticker || layer.type == LayerType::Shape);
    const double extrusion_depth = extrudable ? std::max(0.0, layer.geometry_extrusion_depth.evaluate(local_time)) : 0.0;
    const double bevel_depth = extrudable ? std::min(extrusion_depth * 0.5, std::max(0.0, layer.geometry_bevel_depth.evaluate(local_time))) : 0.0;
    if (extrusion_depth > 0.001) {
        ++session->diagnostics_extrusion_pass_count;
        FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
            "stage=extrusion-pass-begin session=%1 render=%2 layer=%3 type=%4 titleTime=%5 localTime=%6 hardwareDepth=%7 depth=%8 bevel=%9 authoredSegments=%10 bevelSegments=%11 texture=%12x%13 logical=%14x%15 origin=%16,%17")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(session->diagnostics_render_serial)
            .arg(QString::fromStdString(layer.id))
            .arg(static_cast<int>(layer.type))
            .arg(title_time, 0, 'f', 9)
            .arg(local_time, 0, 'f', 9)
            .arg(hardware_depth ? 1 : 0)
            .arg(extrusion_depth, 0, 'f', 4)
            .arg(bevel_depth, 0, 'f', 4)
            .arg(layer.geometry_extrusion_segments)
            .arg(layer.geometry_bevel_segments)
            .arg(texture_width)
            .arg(texture_height)
            .arg(logical_width, 0, 'f', 3)
            .arg(logical_height, 0, 'f', 3)
            .arg(origin.x(), 0, 'f', 3)
            .arg(origin.y(), 0, 'f', 3));
        /* Preserve the authored minimum while adding enough shells for smooth
         * depth motion.  The cap keeps realtime broadcast scenes bounded. */
        const int authored_body_segments =
            std::clamp(layer.geometry_extrusion_segments, 1, 64);
        const int adaptive_body_segments = std::clamp(
            static_cast<int>(std::ceil(extrusion_depth / 6.0)), 1, 64);
        const int body_segments =
            std::max(authored_body_segments, adaptive_body_segments);
        const int bevel_segments = std::clamp(layer.geometry_bevel_segments, 1, 16);
        const int total_segments = body_segments +
            (bevel_depth > 0.001 ? bevel_segments * 2 : 0);
        FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
            "stage=extrusion-pass-geometry session=%1 render=%2 layer=%3 bodySegments=%4 adaptiveSegments=%5 totalSegments=%6 frontBevel=%7 backBevel=%8")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(session->diagnostics_render_serial)
            .arg(QString::fromStdString(layer.id))
            .arg(body_segments)
            .arg(adaptive_body_segments)
            .arg(total_segments)
            .arg(layer.geometry_bevel_front ? 1 : 0)
            .arg(layer.geometry_bevel_back ? 1 : 0));
        for (int slice = total_segments; slice >= 1; --slice) {
            const double t = static_cast<double>(slice) / total_segments;
            const double z = -extrusion_depth * t;
            double inset = 0.0;
            if (bevel_depth > 0.001) {
                const double distance_from_front = -z;
                const double distance_from_back = extrusion_depth + z;
                if (layer.geometry_bevel_front && distance_from_front < bevel_depth) inset = std::max(inset, 1.0 - distance_from_front / bevel_depth);
                if (layer.geometry_bevel_back && distance_from_back < bevel_depth) inset = std::max(inset, 1.0 - distance_from_back / bevel_depth);
            }
            const double bevel_scale = std::max(0.01, 1.0 - inset * std::min(0.45, bevel_depth / std::max(1.0, std::min(logical_width, logical_height))));
            gs_matrix_push();
            gs_matrix_translate3f(static_cast<float>(texture_width * 0.5), static_cast<float>(texture_height * 0.5), static_cast<float>(z));
            gs_matrix_scale3f(static_cast<float>(bevel_scale), static_cast<float>(bevel_scale), 1.0f);
            gs_matrix_translate3f(static_cast<float>(-texture_width * 0.5), static_cast<float>(-texture_height * 0.5), 0.0f);
            /* Lighting and shadow lookup use the actual shell plane instead of
             * reusing the front-face world position for every depth slice. */
            set_gpu_layer_lighting_params(
                session, session->copy_effect, texture, title, layer, title_time,
                local_time, origin, logical_width, logical_height, sx, sy, z);
            draw_geometry_pass();
            gs_matrix_pop();
        }
    }
    set_gpu_layer_lighting_params(
        session, session->copy_effect, texture, title, layer, title_time,
        local_time, origin, logical_width, logical_height, sx, sy, 0.0);
    /* A front bevel needs an inset front face. Drawing the original full-size
     * face after the shells covers the entire bevel ring and makes the option
     * appear to do nothing. Keep the outer body silhouette at full size while
     * shrinking only the face, matching the front-to-body bevel profile. */
    const double front_face_scale =
        extrudable && layer.geometry_bevel_front && bevel_depth > 0.001
            ? std::max(0.01, 1.0 - std::min(
                  0.45, bevel_depth / std::max(
                            1.0, std::min(logical_width, logical_height))))
            : 1.0;
    if (front_face_scale < 0.999999) {
        gs_matrix_push();
        gs_matrix_translate3f(static_cast<float>(texture_width * 0.5),
                              static_cast<float>(texture_height * 0.5), 0.0f);
        gs_matrix_scale3f(static_cast<float>(front_face_scale),
                          static_cast<float>(front_face_scale), 1.0f);
        gs_matrix_translate3f(static_cast<float>(-texture_width * 0.5),
                              static_cast<float>(-texture_height * 0.5), 0.0f);
        draw_geometry_pass();
        gs_matrix_pop();
    } else {
        draw_geometry_pass();
    }
    if (extrusion_depth > 0.001) {
        FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
            "stage=extrusion-pass-end session=%1 render=%2 layer=%3 frontScale=%4 extrusionPasses=%5")
            .arg(reinterpret_cast<quintptr>(session), 0, 16)
            .arg(session->diagnostics_render_serial)
            .arg(QString::fromStdString(layer.id))
            .arg(front_face_scale, 0, 'f', 6)
            .arg(session->diagnostics_extrusion_pass_count));
    }
    if (transform_grid && !raster_entry) gs_vertexbuffer_destroy(transform_grid);
    gs_matrix_pop();
    if (projection_pushed)
        gs_projection_pop();
    return true;
}

static bool copy_full_canvas_gpu_texture(TitleGpuRenderSession *session,
                                         gs_texture_t *texture,
                                         gs_texrender_t *target);

static bool append_gpu_temporal_sample(
    TitleGpuRenderSession *session, gs_texture_t *sample,
    float sample_weight, gs_texrender_t *target)
{
    if (!session || !sample || !target || sample_weight <= 0.0f)
        return false;
    if (!session->motion_composite_effect &&
        !ensure_session_shader_effect(
            session, session->motion_composite_effect, "core:temporal-composite",
            "GPU temporal composite", kGpuTemporalCompositeEffect,
            "obs-fxm-gpu-temporal-composite.effect",
            &session->motion_composite_effect))
        return false;
    gs_effect_t *effect = session->motion_composite_effect;
    gs_eparam_t *sample_param = fxm_effect_param(effect, "sampleImage");
    gs_eparam_t *weight_param = fxm_effect_param(effect, "sampleWeight");
    if (!sample_param || !weight_param)
        return false;

    /* The target is cleared once before the shutter loop. Reset only re-arms
     * the existing texrender for another begin/end cycle; at unchanged
     * dimensions it preserves the texture contents. Do not clear here: each
     * weighted premultiplied sample must be added in place. This removes the
     * previous read-previous/write-next full-canvas ping-pong pass. */
    gs_texrender_reset(target);
    if (!gs_texrender_begin(target, session->width, session->height))
        return false;
    gs_enable_color(true, true, true, true);
    gs_enable_depth_test(false);
    gs_depth_function(GS_LEQUAL);
    gs_set_cull_mode(GS_NEITHER);
    gs_ortho(0.0f, static_cast<float>(session->width), 0.0f,
             static_cast<float>(session->height), -100.0f, 100.0f);
    gs_effect_set_texture(sample_param, sample);
    gs_effect_set_float(weight_param, sample_weight);
    gs_blend_state_push();
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
    gs_matrix_push();
    gs_matrix_identity();
    while (gs_effect_loop(effect, "Accumulate"))
        gs_draw_sprite(sample, 0, session->width, session->height);
    gs_matrix_pop();
    gs_blend_state_pop();
    gs_texrender_end(target);
    return true;
}

static bool resolve_gpu_motion_blur(
    TitleGpuRenderSession *session, gs_texture_t *exposure,
    gs_texture_t *current_frame, float effect_mix, gs_texrender_t *target)
{
    if (!session || !exposure || !current_frame || !target)
        return false;
    if (!session->motion_composite_effect &&
        !ensure_session_shader_effect(
            session, session->motion_composite_effect, "core:temporal-composite",
            "GPU temporal composite", kGpuTemporalCompositeEffect,
            "obs-fxm-gpu-temporal-composite.effect",
            &session->motion_composite_effect))
        return false;
    gs_effect_t *effect = session->motion_composite_effect;
    gs_eparam_t *trail_param = fxm_effect_param(effect, "accumulatedImage");
    gs_eparam_t *sharp_param = fxm_effect_param(effect, "sampleImage");
    gs_eparam_t *opacity_param = fxm_effect_param(effect, "sampleWeight");
    gs_eparam_t *has_param = fxm_effect_param(effect, "hasAccumulated");
    if (!trail_param || !sharp_param || !opacity_param || !has_param)
        return false;

    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;
    gs_effect_set_texture(trail_param, exposure);
    gs_effect_set_texture(sharp_param, current_frame);
    gs_effect_set_float(opacity_param, std::clamp(effect_mix, 0.0f, 1.0f));
    gs_effect_set_int(has_param, 1);
    gs_blend_state_push();
    gs_enable_blending(false);
    gs_matrix_push();
    gs_matrix_identity();
    while (gs_effect_loop(effect, "Resolve"))
        gs_draw_sprite(current_frame, 0, session->width, session->height);
    gs_matrix_pop();
    gs_blend_state_pop();
    gs_texrender_end(target);
    return true;
}

static int gpu_motion_blur_realtime_sample_cap(
    const TitleGpuRenderSession *session, bool complete_temporal_pipeline,
    bool requires_cpu_raster = false)
{
    if (!session || !title_gpu_render_session_is_realtime_output(session))
        return 64;

    const uint64_t pixels = static_cast<uint64_t>(
        std::max(1u, session->width)) * static_cast<uint64_t>(
        std::max(1u, session->height));
    if (complete_temporal_pipeline) {
        /* CPU geometry generation/upload (animated Trim Paths, text geometry)
         * remains the genuinely expensive source path. GPU-only effect sampling
         * such as animated Noise/Grain is allowed a higher temporal density so
         * the effect itself participates in the exposure instead of appearing
         * frozen or posterized. */
        if (requires_cpu_raster)
            return pixels >= 3000000ull ? 2 : 3;
        if (pixels >= 7000000ull)
            return 6;
        if (pixels >= 3000000ull)
            return 8;
        return 12;
    }
    /* Resident-texture transform sampling is only a draw call per exposure
     * sample. Do not apply the old full-pipeline 2-6 sample budget here. */
    if (pixels >= 7000000ull)
        return 32;
    if (pixels >= 3000000ull)
        return 48;
    return 64;
}

static double gpu_motion_blur_temporal_raster_scale(
    const TitleGpuRenderSession *session)
{
    if (!session)
        return 1.0;
    const double physical_scale = gpu_session_raster_scale(session);
    if (session->editor_draft)
        return physical_scale;
    if (!title_gpu_render_session_is_realtime_output(session))
        return physical_scale;

    /* The sharp current frame remains full resolution. Only historical trail
     * samples use this reduced source raster, which is visually tolerant of the
     * reduction and removes the source-only CPU raster/upload spike. */
    const uint64_t pixels = static_cast<uint64_t>(
        std::max(1u, session->width)) * static_cast<uint64_t>(
        std::max(1u, session->height));
    if (pixels >= 7000000ull)
        return physical_scale * 0.375;
    if (pixels >= 1900000ull)
        return physical_scale * 0.5;
    return physical_scale * 0.625;
}

/* Ordinary artwork effects execute on the transform-neutral, padded layer
 * raster. This preserves effect order and layer-space coordinates; the final
 * raster is transformed only after the whole stack has been evaluated. */

static bool render_gpu_layer_to_target(TitleGpuRenderSession *session,
                                       const Title &title,
                                       const Layer &layer,
                                       double title_time,
                                       gs_texrender_t *target,
                                       bool apply_pixel_effects = true,
                                       const std::string &raster_id = std::string(),
                                       double opacity_override = -1.0,
                                       const std::string &visibility_group_id = std::string(),
                                       bool ignore_visibility = false,
                                       gs_texture_t *composition_background = nullptr,
                                       TitleGpuRenderSession::LayerRaster *raster_override = nullptr)
{
    /* Light and Empty controls never produce a foreground texture. Return failure instead of
     * a successful no-op so direct callers cannot consume stale reusable
     * layer-target storage if visibility filtering is ever bypassed. */
    if (layer.type == LayerType::Light) return false;
    if (layer.type == LayerType::Empty) return false;
    if (layer.type == LayerType::TransitionInput &&
        (title.graphic_type != TitleGraphicType::Stinger ||
         title.stinger_switch_mode != StingerSwitchMode::ManualSceneAnimation))
        return false;
    const double resolved_time = gpu_layer_render_time(title, layer, title_time);

    const std::string resolved_raster_id = raster_id.empty()
        ? gpu_session_layer_id(layer) : raster_id;
    TitleGpuRenderSession::LayerRaster *entry_ptr = raster_override;
    if (!entry_ptr) {
        entry_ptr = raster_id.empty()
            ? gpu_session_layer_raster(session, layer)
            : nullptr;
        if (!entry_ptr) {
            auto found = session->layers.find(resolved_raster_id);
            if (found == session->layers.end())
                return false;
            entry_ptr = &found->second;
        }
    }
    auto &entry = *entry_ptr;
    const bool transition_input = layer.type == LayerType::TransitionInput &&
        layer.transition_input_slot >= 0 && layer.transition_input_slot <= 1 &&
        session->transition_input_textures[layer.transition_input_slot];
    if (layer.type == LayerType::TransitionInput && !transition_input &&
        !session->transition_input_preview_enabled)
        return false;

    bool use_transform_motion_fast_path = false;
    double motion_shutter_start = title_time;
    double motion_shutter_end = title_time;
    int motion_transform_samples = 0;
    float motion_trail_opacity = 0.0f;

    const LayerEffect *motion_config = layer_motion_blur_effect(layer);
    const bool persistence_motion_bypass = motion_config &&
        gpu_layer_uses_cue_persistence(title, layer);
    const ResolvedLayerEffect motion = motion_config
        ? resolve_gpu_layer_effect(*motion_config,
                                   std::max(0.0, resolved_time - layer.in_time))
        : ResolvedLayerEffect{};
    if (persistence_motion_bypass && motion.effect_opacity > 0.0f &&
        motion.effect_size > 0.0f) {
        /* Background Persistence freezes this layer's complete visual clock.
         * A frozen layer has no shutter travel, so temporal accumulation is
         * both semantically wrong and unsafe: it repeatedly clears/reuses the
         * shared Motion Blur targets for samples that must be identical. The
         * incoming/non-persistent cue layers still execute Motion Blur. */
        FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
            "stage=motion-blur-persistence-bypass title=%1 layer=%2 time=%3 resolvedTime=%4 asset=%5")
            .arg(QString::fromStdString(title.id))
            .arg(QString::fromStdString(layer.id))
            .arg(title_time, 0, 'f', 9)
            .arg(resolved_time, 0, 'f', 9)
            .arg(layer.asset_owner_id.empty() ? 0 : 1));
    }
    if (!persistence_motion_bypass && motion_config &&
        motion.effect_opacity > 0.0f &&
        motion.effect_size > 0.0f && layer.type != LayerType::Clock) {
        Layer temporal_layer = layer_without_motion_blur_effects(layer);
        const bool text_like = temporal_layer.type == LayerType::Text ||
                               temporal_layer.type == LayerType::Clock ||
                               temporal_layer.type == LayerType::Ticker;
        const bool has_text_transition_descriptor = std::any_of(
            temporal_layer.transitions.begin(), temporal_layer.transitions.end(),
            [](const LayerTransition &transition) {
                return layer_transition_type_is_text(transition.type);
            });

        const double frame_seconds = std::max(1.0 / 240.0,
                                              source_frame_duration());
        const double shutter_seconds = frame_seconds *
            std::clamp(static_cast<double>(motion.effect_size), 0.0, 720.0) /
            360.0;
        const double start = std::clamp(
            title_time + shutter_seconds *
                (motion.effect_centered ? -0.5 : -1.0),
            0.0, std::max(0.0, title.duration));
        const double end = std::clamp(
            title_time + shutter_seconds *
                (motion.effect_centered ? 0.5 : 0.0),
            0.0, std::max(0.0, title.duration));
        const double travel = layer_shutter_travel_pixels(
            title, temporal_layer, start, end);

        const bool active_text_transition = text_like &&
            has_text_transition_descriptor &&
            layer_has_text_transition_during_shutter(
                title, temporal_layer, start, end);
        const bool active_general_blur =
            layer_has_general_blur_during_shutter(
                title, temporal_layer, start, end);
        const bool active_screen_space_effect =
            layer_has_screen_space_effect_during_shutter(
                title, temporal_layer, start, end);

        /* Transition-managed text animators remain in the saved/runtime stack
         * even outside their entrance/exit window. Exclude them from the motion
         * probe unless the shutter actually overlaps that window, otherwise one
         * dormant transition makes Motion Blur rerender the complete text/effect
         * pipeline for the entire title. */
        Layer temporal_probe = temporal_layer;
        if (!active_text_transition) {
            temporal_probe.text_animators.animators.erase(
                std::remove_if(
                    temporal_probe.text_animators.animators.begin(),
                    temporal_probe.text_animators.animators.end(),
                    [](const TextAnimator &animator) {
                        return animator.transition_managed;
                    }),
                temporal_probe.text_animators.animators.end());
        }
        const double temporal_resolved_start = gpu_layer_render_time(
            title, temporal_probe, start);
        const double temporal_resolved_end = gpu_layer_render_time(
            title, temporal_probe, end);
        const double temporal_local_start = std::max(
            0.0, temporal_resolved_start - temporal_probe.in_time);
        const double temporal_local_end = std::max(
            0.0, temporal_resolved_end - temporal_probe.in_time);
        const bool temporal_source_change =
            layer_non_transform_animation_changes_during_interval(
                temporal_probe, temporal_local_start, temporal_local_end) ||
            active_text_transition || active_general_blur ||
            temporal_layer.type == LayerType::Ticker ||
            temporal_layer.type == LayerType::TransitionInput;
        const bool requires_full_temporal_pipeline =
            temporal_source_change ||
            (apply_pixel_effects && active_screen_space_effect);

        const bool projected_motion =
            fxm::transform3d::layer_or_ancestor_uses_3d(title, temporal_layer);
        const bool image_or_video_motion =
            layer_type_is_image_like(temporal_layer.type);
        const double blur_mix = std::clamp(
            static_cast<double>(motion.effect_opacity), 0.0, 1.0);

        /* Development Version 288 performance audit:
         *
         * The dev286/287 implementation recursively rendered the complete layer
         * and every effect once per shutter sample, then issued another full-HD/
         * UHD ping-pong composite for every sample. A configured transition was
         * enough to select that path even while it was inactive. On ordinary
         * transform motion this multiplied OBS render cost by 14-96.
         *
         * Static visual content now takes the transform-only path below: the
         * already effected local texture is reused, while transform, parent,
         * camera, wipe and general transition parameters are still evaluated at
         * each shutter time. Full temporal rendering remains for animated Trim
         * Paths, active text transitions, animated source/effect geometry,
         * ticker content, active transition blur and screen-space effects. */
        if (!requires_full_temporal_pipeline) {
            if (travel < 0.01) {
                return render_gpu_layer_to_target(
                    session, title, temporal_layer, title_time, target,
                    apply_pixel_effects, resolved_raster_id, opacity_override,
                    visibility_group_id, ignore_visibility,
                    composition_background, raster_override);
            }

            /* Match the dense pre-regression GPU sampler. The large source
             * slowdown fixed in dev288 came from recursively rebuilding the full
             * layer/effect pipeline per sample, not from these resident-texture
             * transform draws. Restore distance-adaptive density here while the
             * expensive temporal branch below keeps its separate budget. */
            int sample_cap = projected_motion
                ? (image_or_video_motion ? 96 : 48)
                : (image_or_video_motion ? 64 : 40);
            sample_cap = std::min(
                sample_cap,
                gpu_motion_blur_realtime_sample_cap(session, false, false));
            if (session->editor_draft)
                sample_cap = std::min(sample_cap, 24);
            sample_cap = std::max(2, sample_cap);
            const double density = projected_motion
                ? (image_or_video_motion ? 1.25 : 0.90)
                : (image_or_video_motion ? 0.95 : 0.80);

            use_transform_motion_fast_path = true;
            motion_shutter_start = start;
            motion_shutter_end = end;
            motion_transform_samples = motion_blur_quality_sample_count(
                motion.effect_samples, travel, density, sample_cap);
            motion_trail_opacity = static_cast<float>(blur_mix);
        } else {
            if (active_text_transition) {
                temporal_layer.text_animators =
                    resolved_text_transition_animator_stack(
                        temporal_layer.transitions,
                        temporal_layer.text_animators,
                        temporal_layer.in_time, temporal_layer.out_time);
            }

            /* Only source/geometry properties require a fresh base raster at
             * each shutter time. Ordinary pixel effects are removed from this
             * probe because the recursive sample render resolves them on the GPU
             * after the source raster. Trim Paths is retained because it changes
             * stroke geometry before rasterization. */
            Layer raster_probe = temporal_layer;
            raster_probe.effects.erase(
                std::remove_if(
                    raster_probe.effects.begin(), raster_probe.effects.end(),
                    [](const LayerEffect &effect) {
                        return effect.type != LayerEffectType::TrimPaths;
                    }),
                raster_probe.effects.end());
            const bool temporal_base_raster =
                layer_non_transform_animation_changes_during_interval(
                    raster_probe, temporal_local_start, temporal_local_end) ||
                temporal_layer.type == LayerType::Ticker;

            /* A complete sampled layer can include several effect passes. When
             * temporal_base_raster is true it additionally performs CPU geometry
             * rasterization and a GPU upload per sample. Give that path its own
             * stricter source budget instead of treating all temporal work as
             * equivalent. */
            int sample_cap = temporal_base_raster
                ? (projected_motion ? 8 : 6)
                : (projected_motion
                    ? (image_or_video_motion ? 20 : 16)
                    : (image_or_video_motion ? 16 : 12));
            if (temporal_base_raster && image_or_video_motion)
                sample_cap += 2;
            sample_cap = std::min(
                sample_cap,
                gpu_motion_blur_realtime_sample_cap(
                    session, true, temporal_base_raster));
            if (session->editor_draft)
                sample_cap = std::min(sample_cap, temporal_base_raster ? 4 : 6);
            sample_cap = std::max(2, sample_cap);
            const double density = temporal_base_raster
                ? (projected_motion ? 0.18 : 0.12)
                : (projected_motion ? 0.42 : 0.30);
            const int samples = motion_blur_quality_sample_count(
                motion.effect_samples, travel, density, sample_cap);

            gs_texrender_t *local_motion_sample_target = nullptr;
            gs_texrender_t *local_motion_accum_target = nullptr;
            const bool nested_temporal_pass = session->motion_temporal_depth > 0;
            if (nested_temporal_pass) {
                local_motion_sample_target = fxm_create_motion_blur_sample_target();
                local_motion_accum_target = fxm_create_motion_blur_accumulation_target();
                if (!local_motion_sample_target ||
                    !local_motion_accum_target) {
                    if (local_motion_sample_target)
                        gs_texrender_destroy(local_motion_sample_target);
                    if (local_motion_accum_target)
                        gs_texrender_destroy(local_motion_accum_target);
                    session->last_error =
                        "Could not allocate nested Motion Blur render targets.";
                    return false;
                }
            }
            gs_texrender_t *motion_sample_target = nested_temporal_pass
                ? local_motion_sample_target : session->motion_sample_target;
            gs_texrender_t *motion_accum_target = nested_temporal_pass
                ? local_motion_accum_target : session->motion_accum_target;
            ++session->motion_temporal_depth;

            TitleGpuRenderSession::LayerRaster temporal_entry;
            auto release_temporal_entry = [&]() {
                if (temporal_entry.text_layer)
                    release_gpu_text_layer(session, temporal_entry);
                for (gs_texrender_t *&primitive_target :
                     temporal_entry.primitive_targets) {
                    if (primitive_target)
                        gs_texrender_destroy(primitive_target);
                    primitive_target = nullptr;
                }
                if (temporal_entry.texture)
                    gs_texture_destroy(temporal_entry.texture);
                if (temporal_entry.effect_cache)
                    gs_texrender_destroy(temporal_entry.effect_cache);
                if (temporal_entry.transform_grid)
                    gs_vertexbuffer_destroy(temporal_entry.transform_grid);
                temporal_entry.texture = nullptr;
                temporal_entry.effect_cache = nullptr;
                temporal_entry.transform_grid = nullptr;
            };
            bool temporal_resources_released = false;
            auto release_temporal_resources = [&]() noexcept {
                if (temporal_resources_released)
                    return;
                release_temporal_entry();
                if (session->motion_temporal_depth > 0)
                    --session->motion_temporal_depth;
                if (local_motion_sample_target)
                    gs_texrender_destroy(local_motion_sample_target);
                if (local_motion_accum_target)
                    gs_texrender_destroy(local_motion_accum_target);
                local_motion_sample_target = nullptr;
                local_motion_accum_target = nullptr;
                temporal_resources_released = true;
            };
            auto temporal_resource_deleter = [&](void *) noexcept {
                release_temporal_resources();
            };
            std::unique_ptr<void, decltype(temporal_resource_deleter)>
                temporal_resource_guard(
                    static_cast<void *>(&temporal_entry), temporal_resource_deleter);

            struct vec4 accumulation_clear;
            vec4_zero(&accumulation_clear);
            bool accumulation_ok = begin_gpu_target(
                motion_accum_target, session->width, session->height,
                accumulation_clear);
            bool temporal_sample_exception = false;
            std::string temporal_sample_exception_message;
            if (accumulation_ok)
                gs_texrender_end(motion_accum_target);
            int accumulated_samples = 0;
            auto accumulate_texture = [&](gs_texture_t *sample_texture,
                                          float sample_weight) -> bool {
                if (!sample_texture || sample_weight <= 0.0f)
                    return false;
                if (!append_gpu_temporal_sample(
                        session, sample_texture, sample_weight,
                        motion_accum_target))
                    return false;
                ++accumulated_samples;
                return true;
            };
            auto render_temporal_sample = [&](double sample_time) -> bool {
                TitleGpuRenderSession::LayerRaster *sample_entry =
                    raster_override;
                if (temporal_base_raster) {
                    const double sample_resolved_time = gpu_layer_render_time(
                        title, temporal_layer, sample_time);
                    const double sample_local_time = std::max(
                        0.0, sample_resolved_time - temporal_layer.in_time);
                    double raster_scale =
                        gpu_motion_blur_temporal_raster_scale(session);

                    /* Development Version 291: a trimmed geometry sample with
                     * a legacy layer shadow (or a layer-space Drop Shadow /
                     * Long Shadow effect) must keep a 1:1 local raster. The
                     * real-time source path normally downsamples historical
                     * temporal rasters; with Trim Paths the visible alpha bounds
                     * change every sample, and resampling an already-offset
                     * shadow against that changing local surface introduced an
                     * apparent shadow translation. The editor did not show it
                     * because its temporal raster stayed at full resolution.
                     * Keep only this coordinate-sensitive combination exact;
                     * ordinary Trim Paths motion blur retains the source budget. */
                    const bool has_active_trim_paths = std::any_of(
                        temporal_layer.effects.begin(), temporal_layer.effects.end(),
                        [sample_local_time](const LayerEffect &effect) {
                            return effect.type == LayerEffectType::TrimPaths &&
                                   eval_effect_enabled(effect, sample_local_time);
                        });
                    const ShadowRenderParams sampled_shadow =
                        evaluated_shadow_params(temporal_layer, sample_local_time);
                    const bool has_legacy_shadow =
                        sampled_shadow.drop_enabled || sampled_shadow.long_enabled;
                    const bool has_layer_space_shadow_effect = std::any_of(
                        temporal_layer.effects.begin(), temporal_layer.effects.end(),
                        [sample_local_time](const LayerEffect &effect) {
                            if (!eval_effect_enabled(effect, sample_local_time) ||
                                effect_execution_space(effect) !=
                                    LayerEffectSpace::LayerSpace)
                                return false;
                            return effect.type == LayerEffectType::DropShadow ||
                                   effect.type == LayerEffectType::LongShadow;
                        });
                    if (has_active_trim_paths &&
                        (has_legacy_shadow || has_layer_space_shadow_effect))
                        raster_scale = 1.0;

                    MotionBaseRaster raster;
                    try {
                        raster = render_gpu_layer_base_raster(
                            title, temporal_layer, sample_resolved_time,
                            raster_scale,
                            session->scene_mask_placeholder_preview_enabled,
                            session->video_decode_client);
                    } catch (const std::exception &error) {
                        temporal_sample_exception = true;
                        temporal_sample_exception_message = error.what();
                        FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
                            "stage=temporal-sample-exception title=%1 layer=%2 sampleTime=%3 error=%4 fallback=current-transition-frame")
                            .arg(QString::fromStdString(title.id))
                            .arg(QString::fromStdString(layer.id))
                            .arg(sample_time, 0, 'f', 9)
                            .arg(QString::fromUtf8(error.what())));
                        return false;
                    } catch (...) {
                        temporal_sample_exception = true;
                        temporal_sample_exception_message =
                            "unknown temporal raster exception";
                        FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
                            "stage=temporal-sample-exception title=%1 layer=%2 sampleTime=%3 error=unknown fallback=current-transition-frame")
                            .arg(QString::fromStdString(title.id))
                            .arg(QString::fromStdString(layer.id))
                            .arg(sample_time, 0, 'f', 9));
                        return false;
                    }

                    if (raster.image.isNull()) {
                        /* Empty Trim Paths/text-transition samples are real
                         * transparent exposure samples, not render failures. */
                        return copy_full_canvas_gpu_texture(
                            session, nullptr, motion_sample_target);
                    }

                    temporal_entry.pending_image = raster.image;
                    temporal_entry.origin = raster.origin;
                    temporal_entry.logical_width = raster.logical_width > 0.0
                        ? raster.logical_width : raster.image.width();
                    temporal_entry.logical_height = raster.logical_height > 0.0
                        ? raster.logical_height : raster.image.height();
                    temporal_entry.base_box_width = std::max(
                        0.0001, eval_box_width(
                            temporal_layer, sample_local_time));
                    temporal_entry.base_box_height = std::max(
                        0.0001, eval_box_height(
                            temporal_layer, sample_local_time));
                    temporal_entry.layer_box_rect = raster.layer_box_rect;
                    temporal_entry.image_clip_rect = raster.image_clip_rect;
                    temporal_entry.gpu_text = false;
                    temporal_entry.gpu_primitive = false;
                    temporal_entry.pending_upload = true;
                    if (!upload_gpu_layer_raster(session, temporal_entry))
                        return false;
                    sample_entry = &temporal_entry;
                }

                try {
                    return render_gpu_layer_to_target(
                        session, title, temporal_layer, sample_time,
                        motion_sample_target, apply_pixel_effects,
                        resolved_raster_id, opacity_override,
                        visibility_group_id, ignore_visibility,
                        composition_background, sample_entry);
                } catch (const std::exception &error) {
                    temporal_sample_exception = true;
                    temporal_sample_exception_message = error.what();
                    FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
                        "stage=temporal-sample-render-exception title=%1 layer=%2 sampleTime=%3 error=%4 fallback=current-transition-frame")
                        .arg(QString::fromStdString(title.id))
                        .arg(QString::fromStdString(layer.id))
                        .arg(sample_time, 0, 'f', 9)
                        .arg(QString::fromUtf8(error.what())));
                    return false;
                } catch (...) {
                    temporal_sample_exception = true;
                    temporal_sample_exception_message =
                        "unknown temporal sample render exception";
                    FXM_LOG_ERROR("GpuPipeline", QStringLiteral(
                        "stage=temporal-sample-render-exception title=%1 layer=%2 sampleTime=%3 error=unknown fallback=current-transition-frame")
                        .arg(QString::fromStdString(title.id))
                        .arg(QString::fromStdString(layer.id))
                        .arg(sample_time, 0, 'f', 9));
                    return false;
                }
            };
            auto render_and_accumulate = [&](double sample_time,
                                             float sample_weight) -> bool {
                if (!render_temporal_sample(sample_time))
                    return false;
                return accumulate_texture(
                    gs_texrender_get_texture(motion_sample_target),
                    sample_weight);
            };

            /* Integrate a normalized shutter exposure plus the max-alpha
             * envelope of the same samples. Development Version 296 resolves
             * temporal color against that envelope; the current frame is only
             * rendered for an authored partial dry/wet mix. */
            const float sample_weight =
                1.0f / static_cast<float>(samples);
            for (int i = 0; accumulation_ok && i < samples; ++i) {
                const double f = (static_cast<double>(i) + 0.5) /
                                 static_cast<double>(samples);
                const double sample_time = start + (end - start) * f;
                if (!render_and_accumulate(sample_time, sample_weight)) {
                    accumulation_ok = false;
                    break;
                }
            }

            if (accumulation_ok && accumulated_samples > 0) {
                gs_texture_t *exposure_texture =
                    gs_texrender_get_texture(motion_accum_target);
                gs_texture_t *current_texture = exposure_texture;
                if (blur_mix < 0.9999) {
                    accumulation_ok = render_temporal_sample(title_time);
                    if (accumulation_ok)
                        current_texture =
                            gs_texrender_get_texture(motion_sample_target);
                }
                if (accumulation_ok) {
                    accumulation_ok = resolve_gpu_motion_blur(
                        session, exposure_texture, current_texture,
                        static_cast<float>(blur_mix), target);
                }
            }
            release_temporal_resources();
            if (!accumulation_ok) {
                /* A transition sample may fail because its dynamic text raster
                 * throws, a first-use temporal shader is still compiling, or a
                 * nested target cannot be acquired. Motion Blur is optional;
                 * the authored transition is not. Re-render the displayed frame
                 * through the normal stack with Motion Blur removed instead of
                 * letting an exception escape into Qt/OBS or dropping the layer. */
                FXM_LOG_WARNING("GpuPipeline", QStringLiteral(
                    "stage=temporal-fallback title=%1 layer=%2 time=%3 activeTextTransition=%4 exception=%5 reason=%6")
                    .arg(QString::fromStdString(title.id))
                    .arg(QString::fromStdString(layer.id))
                    .arg(title_time, 0, 'f', 9)
                    .arg(active_text_transition ? 1 : 0)
                    .arg(temporal_sample_exception ? 1 : 0)
                    .arg(temporal_sample_exception_message.empty()
                        ? QStringLiteral("temporal-pass-unavailable")
                        : QString::fromStdString(
                              temporal_sample_exception_message)));
                return render_gpu_layer_to_target(
                    session, title, temporal_layer, title_time, target,
                    apply_pixel_effects, resolved_raster_id, opacity_override,
                    visibility_group_id, ignore_visibility,
                    composition_background, raster_override);
            }
            return true;
        }
    }

    /* A time-sampled geometry/text source may be transparent at the displayed
     * frame while still contributing visible shutter samples. Defer the normal
     * missing-raster rejection until after Motion Blur has had that chance. */
    if (!transition_input && !entry.texture)
        return false;

    gs_texture_t *source_texture = transition_input
        ? session->transition_input_textures[layer.transition_input_slot]
        : entry.texture;
    const uint32_t source_width = transition_input
        ? std::max(1u, session->transition_input_width) : entry.width;
    const uint32_t source_height = transition_input
        ? std::max(1u, session->transition_input_height) : entry.height;

    QPointF draw_origin = entry.origin;
    double draw_logical_width = entry.logical_width;
    double draw_logical_height = entry.logical_height;
    QRectF draw_clip_rect = entry.image_clip_rect;
    double draw_base_box_width = entry.base_box_width;
    double draw_base_box_height = entry.base_box_height;
    if (transition_input) {
        /* Scene inputs are rectangular shape surfaces. Map the complete OBS
         * scene texture directly into the authored layer box instead of using
         * dimensions cropped from the editor placeholder raster. */
        const double local_time = std::max(0.0, resolved_time - layer.in_time);
        draw_base_box_width = std::max(0.0001, eval_box_width(layer, local_time));
        draw_base_box_height = std::max(0.0001, eval_box_height(layer, local_time));
        draw_logical_width = draw_base_box_width;
        draw_logical_height = draw_base_box_height;
        draw_origin = QPointF(
            -eval_origin_x(layer, local_time) * draw_base_box_width,
            -eval_origin_y(layer, local_time) * draw_base_box_height);
        draw_clip_rect = QRectF();
        entry.base_box_width = draw_base_box_width;
        entry.base_box_height = draw_base_box_height;
        entry.logical_width = draw_logical_width;
        entry.logical_height = draw_logical_height;
        entry.origin = draw_origin;
        entry.layer_box_rect = QRectF(0.0, 0.0, draw_logical_width,
                                      draw_logical_height);
        entry.image_clip_rect = QRectF();
    }
    /* Scene A/B pixels change every transition frame. input != entry.texture
     * disables the ordinary effect cache, while passing the entry preserves
     * the same local shape geometry used by masks and effects. */
    gs_texture_t *local_texture = apply_pixel_effects
        ? apply_gpu_layer_effect_stack(
              session, layer, resolved_time, source_texture, source_width,
              source_height, &entry, false, composition_background, false,
              static_cast<int>(LayerEffectSpace::LayerSpace))
        : source_texture;
    if (!local_texture)
        return false;

    const bool compositor_visible = visibility_group_id.empty()
        ? gpu_layer_chain_visible(title, layer, title_time)
        : gpu_layer_visible_in_group_compositor(title, layer, title_time,
                                                visibility_group_id);
    const double current_opacity = (ignore_visibility || compositor_visible)
        ? (opacity_override >= 0.0
            ? std::clamp(opacity_override, 0.0, 1.0)
            : std::clamp(gpu_layer_chain_opacity(title, layer, title_time),
                         0.0, 1.0))
        : 0.0;

    if (use_transform_motion_fast_path && current_opacity > 0.0 &&
        motion_transform_samples >= 2) {
        /* The fast path uses the same normalized exposure as the complete
         * effect/transition path. At full strength no current-frame color is
         * rendered; partial effect opacity alone creates a conventional
         * premultiplied dry/wet mix. */
        gs_texrender_t *local_motion_sample_target = nullptr;
        gs_texrender_t *local_motion_accum_target = nullptr;
        const bool nested_temporal_pass = session->motion_temporal_depth > 0;
        if (nested_temporal_pass) {
            local_motion_sample_target = fxm_create_motion_blur_sample_target();
            local_motion_accum_target = fxm_create_motion_blur_accumulation_target();
            if (!local_motion_sample_target || !local_motion_accum_target) {
                if (local_motion_sample_target)
                    gs_texrender_destroy(local_motion_sample_target);
                if (local_motion_accum_target)
                    gs_texrender_destroy(local_motion_accum_target);
                session->last_error =
                    "Could not allocate nested fast Motion Blur render targets.";
                return false;
            }
        }
        gs_texrender_t *motion_sample_target = nested_temporal_pass
            ? local_motion_sample_target : session->motion_sample_target;
        gs_texrender_t *motion_accum_target = nested_temporal_pass
            ? local_motion_accum_target : session->motion_accum_target;
        ++session->motion_temporal_depth;

        struct vec4 motion_clear;
        vec4_zero(&motion_clear);
        bool motion_ok = begin_gpu_target(
            motion_accum_target, session->width, session->height, motion_clear);
        if (motion_ok) {
            gs_blend_state_push();
            gs_enable_blending(true);
            gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
            gs_blend_op(GS_BLEND_OP_ADD);
            const float sample_weight = static_cast<float>(
                current_opacity / static_cast<double>(motion_transform_samples));
            for (int i = 0; i < motion_transform_samples; ++i) {
                const double f = (static_cast<double>(i) + 0.5) /
                                 static_cast<double>(motion_transform_samples);
                const double sample_time = motion_shutter_start +
                    (motion_shutter_end - motion_shutter_start) * f;
                const bool sample_visible = ignore_visibility ||
                    (visibility_group_id.empty()
                        ? gpu_layer_chain_visible(title, layer, sample_time)
                        : gpu_layer_visible_in_group_compositor(
                              title, layer, sample_time, visibility_group_id));
                if (!sample_visible)
                    continue;
                draw_gpu_layer_texture(
                    session, &entry, title, layer, local_texture,
                    draw_origin, source_width, source_height,
                    draw_logical_width, draw_logical_height,
                    draw_clip_rect, draw_base_box_width,
                    draw_base_box_height, sample_time, sample_weight);
            }
            gs_blend_state_pop();
            gs_texrender_end(motion_accum_target);
        }

        gs_texture_t *exposure_texture = motion_ok
            ? gs_texrender_get_texture(motion_accum_target) : nullptr;
        gs_texture_t *current_texture = exposure_texture;
        if (motion_ok && motion_trail_opacity < 0.9999f) {
            motion_ok = begin_gpu_target(
                motion_sample_target, session->width, session->height,
                motion_clear);
            if (motion_ok) {
                gs_enable_blending(true);
                gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
                draw_gpu_layer_texture(
                    session, &entry, title, layer, local_texture,
                    draw_origin, source_width, source_height,
                    draw_logical_width, draw_logical_height,
                    draw_clip_rect, draw_base_box_width,
                    draw_base_box_height, title_time,
                    static_cast<float>(current_opacity));
                gs_texrender_end(motion_sample_target);
                current_texture =
                    gs_texrender_get_texture(motion_sample_target);
            }
        }
        if (motion_ok) {
            motion_ok = resolve_gpu_motion_blur(
                session, exposure_texture, current_texture,
                motion_trail_opacity, target);
        }

        --session->motion_temporal_depth;
        if (local_motion_sample_target)
            gs_texrender_destroy(local_motion_sample_target);
        if (local_motion_accum_target)
            gs_texrender_destroy(local_motion_accum_target);
        return motion_ok;
    }

    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;
    if (current_opacity > 0.0) {
        gs_enable_blending(true);
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
        draw_gpu_layer_texture(session, &entry, title, layer, local_texture,
                               draw_origin, source_width, source_height,
                               draw_logical_width, draw_logical_height,
                               draw_clip_rect, draw_base_box_width,
                               draw_base_box_height, title_time,
                               static_cast<float>(current_opacity));
    }
    gs_texrender_end(target);

    /* Screen-space effects consume the already transformed full-canvas layer.
     * Motion Blur recursively executes this complete normal path for every
     * shutter sample, therefore these effects are inside (below) Motion Blur
     * instead of being applied once to the already blurred result. */
    if (apply_pixel_effects) {
        gs_texture_t *projected = gs_texrender_get_texture(target);
        gs_texture_t *screen_effected = apply_gpu_layer_effect_stack(
            session, layer, resolved_time, projected, session->width,
            session->height, nullptr, false, composition_background, true,
            static_cast<int>(LayerEffectSpace::ScreenSpace));
        if (!screen_effected)
            return false;
        if (screen_effected != projected &&
            !copy_full_canvas_gpu_texture(session, screen_effected, target))
            return false;
    }
    return true;
}

static bool copy_full_canvas_gpu_texture(TitleGpuRenderSession *session,
                                         gs_texture_t *texture,
                                         gs_texrender_t *target);

static gs_texture_t *apply_gpu_mask(TitleGpuRenderSession *session,
                                    gs_texture_t *layer_texture,
                                    gs_texture_t *mask_texture,
                                    MaskMode mode)
{
    if (!session || !layer_texture || !mask_texture || mode == MaskMode::None)
        return layer_texture;

    /* Never sample a texture while rendering back into the texrender that owns
     * it. Group composition can legitimately leave its complete foreground in
     * masked_target when a child has its own matte. Applying the group target
     * matte directly would then create a GPU read/write feedback loop and the
     * group would appear unmasked or empty depending on the backend. Snapshot
     * any aliased input into an independent full-canvas texture first. */
    gs_texrender_t *layer_snapshot = nullptr;
    gs_texrender_t *mask_snapshot = nullptr;
    gs_texture_t *masked_storage = session->masked_target_resource
        ? fxm::obs_plugin::native_texture(
              session->masked_target_resource->texture())
        : nullptr;
    if (masked_storage && layer_texture == masked_storage) {
        layer_snapshot = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        if (!layer_snapshot ||
            !copy_full_canvas_gpu_texture(session, layer_texture, layer_snapshot)) {
            if (layer_snapshot)
                gs_texrender_destroy(layer_snapshot);
            return nullptr;
        }
        layer_texture = gs_texrender_get_texture(layer_snapshot);
    }
    if (masked_storage && mask_texture == masked_storage) {
        mask_snapshot = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
        if (!mask_snapshot ||
            !copy_full_canvas_gpu_texture(session, mask_texture, mask_snapshot)) {
            if (layer_snapshot)
                gs_texrender_destroy(layer_snapshot);
            if (mask_snapshot)
                gs_texrender_destroy(mask_snapshot);
            return nullptr;
        }
        mask_texture = gs_texrender_get_texture(mask_snapshot);
    }

    if (!session->mask_effect &&
        !ensure_session_shader_effect(
            session, session->mask_effect, "core:mask", "GPU mask",
            kGpuMaskEffect, "obs-fxm-gpu-mask.effect",
            &session->mask_effect)) {
        if (layer_snapshot) gs_texrender_destroy(layer_snapshot);
        if (mask_snapshot) gs_texrender_destroy(mask_snapshot);
        return nullptr;
    }
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(session->masked_target, session->width, session->height, clear)) {
        if (layer_snapshot) gs_texrender_destroy(layer_snapshot);
        if (mask_snapshot) gs_texrender_destroy(mask_snapshot);
        return nullptr;
    }
    gs_eparam_t *image = fxm_effect_param(session->mask_effect, "image");
    gs_eparam_t *mask = fxm_effect_param(session->mask_effect, "maskImage");
    gs_eparam_t *mask_mode = fxm_effect_param(session->mask_effect, "maskMode");
    if (!image || !mask || !mask_mode) {
        gs_texrender_end(session->masked_target);
        if (layer_snapshot) gs_texrender_destroy(layer_snapshot);
        if (mask_snapshot) gs_texrender_destroy(mask_snapshot);
        return nullptr;
    }
    gs_effect_set_texture(image, layer_texture);
    gs_effect_set_texture(mask, mask_texture);
    gs_effect_set_int(mask_mode, (int)mode);
    gs_enable_blending(false);
    /* Full-canvas mask composition must never inherit the model matrix left by
     * the OBS source, editor presentation, or a preceding transformed layer.
     * The mask shader samples both full-canvas textures in normalized UV space;
     * applying an inherited translate/scale/rotation moves the fullscreen quad
     * away from the target and makes otherwise valid layer masks appear empty. */
    gs_matrix_push();
    gs_matrix_identity();
    while (gs_effect_loop(session->mask_effect, "Draw"))
        gs_draw_sprite(layer_texture, 0, session->width, session->height);
    gs_matrix_pop();
    gs_texrender_end(session->masked_target);
    gs_texture_t *result = fxm::obs_plugin::native_texture(
        session->masked_target_resource->texture());
    if (layer_snapshot) gs_texrender_destroy(layer_snapshot);
    if (mask_snapshot) gs_texrender_destroy(mask_snapshot);
    return result;
}

static bool copy_full_canvas_gpu_texture(TitleGpuRenderSession *session,
                                         gs_texture_t *texture,
                                         gs_texrender_t *target)
{
    if (!session || !target)
        return false;
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;
    if (texture) {
        gs_eparam_t *image = session->blit_effect
            ? fxm_effect_param(session->blit_effect, "image")
            : nullptr;
        if (!session->blit_effect || !image) {
            gs_texrender_end(target);
            return false;
        }
        gs_effect_set_texture(image, texture);
        gs_enable_blending(false);
        /* Publishing a retained matte is also a full-canvas pass. Keep it
         * transform-neutral for the same reason as apply_gpu_mask(). */
        gs_matrix_push();
        gs_matrix_identity();
        while (gs_effect_loop(session->blit_effect, "Draw"))
            gs_draw_sprite(texture, 0, session->width, session->height);
        gs_matrix_pop();
    }
    gs_texrender_end(target);
    return true;
}

static bool replace_full_canvas_gpu_texture(TitleGpuRenderSession *session,
                                            gs_texture_t *texture,
                                            gs_texrender_t *target)
{
    if (!session || !texture || !target ||
        gs_texture_get_width(texture) != session->width ||
        gs_texture_get_height(texture) != session->height)
        return false;

    /* Publication is not a composition pass. Allocate and canonically clear
     * the inactive presentation target, then replace its complete resource.
     * This cannot inherit a projection/model matrix from a preceding 3D draw,
     * and a transparent frame overwrites every pixel of the older frame. */
    struct vec4 clear;
    vec4_zero(&clear);
    if (!begin_gpu_target(target, session->width, session->height, clear))
        return false;
    gs_texrender_end(target);

    gs_texture_t *destination = gs_texrender_get_texture(target);
    if (!destination || destination == texture ||
        gs_texture_get_width(destination) != session->width ||
        gs_texture_get_height(destination) != session->height ||
        gs_texture_get_color_format(destination) !=
            gs_texture_get_color_format(texture))
        return false;
    gs_copy_texture(destination, texture);
    return true;
}


enum class ExternalBackgroundMapping {
    Rectangle = 0,
    CurrentTransform = 1,
};

static bool ensure_external_background_snapshot(
    TitleGpuRenderSession *session, uint32_t width, uint32_t height,
    enum gs_color_format format)
{
    if (!session || width == 0 || height == 0 || format == GS_UNKNOWN)
        return false;
    const bool recreate = !session->external_background_snapshot ||
        session->external_background_snapshot_width != width ||
        session->external_background_snapshot_height != height ||
        session->external_background_snapshot_format != format;
    if (!recreate)
        return true;
    session->external_background_snapshot_resource.reset();
    session->external_background_snapshot = nullptr;
    fxm::rendering::TextureDescriptor descriptor;
    descriptor.extent = {width, height};
    descriptor.usage = fxm::rendering::TextureUsage::Immutable;
    session->external_background_snapshot_resource =
        g_obs_render_backend.create_texture_with_native_format(
            descriptor, format, nullptr, nullptr);
    session->external_background_snapshot =
        fxm::obs_plugin::native_texture(
            session->external_background_snapshot_resource.get());
    if (!session->external_background_snapshot) {
        session->external_background_snapshot_resource.reset();
        session->external_background_snapshot_width = 0;
        session->external_background_snapshot_height = 0;
        session->external_background_snapshot_format = GS_UNKNOWN;
        return false;
    }
    session->external_background_snapshot_width = width;
    session->external_background_snapshot_height = height;
    session->external_background_snapshot_format = format;
    return true;
}

static gs_texture_t *render_external_background_local_locked(
    TitleGpuRenderSession *session, gs_texture_t *background,
    uint32_t background_width, uint32_t background_height,
    ExternalBackgroundMapping mapping, const struct matrix4 *local_to_background,
    float rect_x, float rect_y, float rect_width, float rect_height,
    uint32_t logical_width, uint32_t logical_height)
{
    if (!session || !background || background_width == 0 ||
        background_height == 0 || session->width == 0 || session->height == 0)
        return nullptr;
    const fxm::rendering::Extent2D target_extent = {
        session->width, session->height};
    if (session->external_background_local &&
        (session->external_background_local->descriptor().extent.width !=
             target_extent.width ||
         session->external_background_local->descriptor().extent.height !=
             target_extent.height)) {
        session->external_background_local.reset();
    }
    if (!session->external_background_local) {
        fxm::rendering::RenderTargetDescriptor descriptor;
        descriptor.extent = target_extent;
        descriptor.color_format = fxm::rendering::TextureFormat::RGBA8;
        descriptor.depth_stencil = false;
        session->external_background_local =
            g_obs_render_backend.create_render_target(descriptor, nullptr);
    }
    if (!session->external_background_effect)
        (void)ensure_session_shader_effect(
            session, session->external_background_effect,
            "core:external-background", "External background",
            kGpuExternalBackgroundEffect,
            "obs-fxm-external-background.effect",
            &session->external_background_effect);
    if (!session->external_background_local ||
        !session->external_background_effect)
        return nullptr;

    struct vec4 clear;
    vec4_zero(&clear);
    fxm::rendering::FrameDescriptor frame;
    frame.output_extent = target_extent;
    if (!g_obs_render_backend.begin_frame(
            *session->external_background_local, frame, nullptr))
        return nullptr;
    gs_enable_color(true, true, true, true);
    gs_enable_depth_test(false);
    gs_depth_function(GS_LEQUAL);
    gs_set_cull_mode(GS_NEITHER);
    gs_enable_blending(false);
    gs_ortho(0.0f, static_cast<float>(session->width), 0.0f,
             static_cast<float>(session->height), -100.0f, 100.0f);
    gs_clear(GS_CLEAR_COLOR, &clear, 1.0f, 0);

    gs_effect_t *effect = session->external_background_effect;
    gs_eparam_t *background_param =
        fxm_effect_param(effect, "background");
    gs_eparam_t *matrix_param =
        fxm_effect_param(effect, "localToBackground");
    gs_eparam_t *background_size_param =
        fxm_effect_param(effect, "backgroundSize");
    gs_eparam_t *logical_size_param =
        fxm_effect_param(effect, "logicalSize");
    gs_eparam_t *rect_param =
        fxm_effect_param(effect, "backgroundRect");
    gs_eparam_t *title_background_param =
        fxm_effect_param(effect, "titleBackground");
    gs_eparam_t *mapping_param =
        fxm_effect_param(effect, "mappingMode");
    if (!background_param || !matrix_param || !background_size_param ||
        !logical_size_param || !rect_param || !title_background_param ||
        !mapping_param) {
        (void)g_obs_render_backend.end_frame(nullptr);
        return nullptr;
    }

    struct matrix4 mapping_matrix;
    if (local_to_background)
        matrix4_copy(&mapping_matrix, local_to_background);
    else
        matrix4_identity(&mapping_matrix);
    struct vec2 background_size;
    vec2_set(&background_size, static_cast<float>(background_width),
             static_cast<float>(background_height));
    struct vec2 logical_size;
    vec2_set(&logical_size, static_cast<float>(std::max(1u, logical_width)),
             static_cast<float>(std::max(1u, logical_height)));
    struct vec4 background_rect;
    vec4_set(&background_rect, rect_x, rect_y, rect_width, rect_height);
    double r, g, b, a;
    unpack_color(session->title.bg_color, r, g, b, a);
    struct vec4 title_background;
    vec4_set(&title_background, static_cast<float>(r * a),
             static_cast<float>(g * a), static_cast<float>(b * a),
             static_cast<float>(a));

    gs_effect_set_texture(background_param, background);
    gs_effect_set_matrix4(matrix_param, &mapping_matrix);
    gs_effect_set_vec2(background_size_param, &background_size);
    gs_effect_set_vec2(logical_size_param, &logical_size);
    gs_effect_set_vec4(rect_param, &background_rect);
    gs_effect_set_vec4(title_background_param, &title_background);
    gs_effect_set_int(mapping_param, static_cast<int>(mapping));
    gs_enable_blending(false);
    while (gs_effect_loop(effect, "Draw"))
        gs_draw_sprite(background, 0, session->width, session->height);
    if (!g_obs_render_backend.end_frame(nullptr))
        return nullptr;
    return fxm::obs_plugin::native_texture(
        session->external_background_local->texture());
}
