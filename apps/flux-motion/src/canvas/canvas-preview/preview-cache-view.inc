namespace {
constexpr int kCanvasRulerThickness = 24;
constexpr double kCanvasGuideHitTolerancePx = 5.0;
constexpr double kPenBezierActivationDistancePx = 5.0;
constexpr const char *kEditorRulersVisibleKey = "rulersVisible";
constexpr const char *kEditorGuidesVisibleKey = "guidesVisible";
constexpr const char *kEditorGuidesLockedKey = "guidesLocked";
constexpr const char *kEditorGuideCoordinatesVisibleKey = "guideCoordinatesVisible";
constexpr const char *kEditorCanvasBorderVisibleKey = "canvasBorderVisible";
constexpr const char *kEditorVerticalGuidesKey = "verticalGuides";
constexpr const char *kEditorHorizontalGuidesKey = "horizontalGuides";
constexpr const char *kEditorAdaptiveRenderingKey = "adaptiveRendering";
constexpr const char *kEditorAdaptiveQualityKey = "adaptiveRenderingQuality";

QStringList guide_values_to_strings(const std::vector<double> &values)
{
    QStringList out;
    for (double v : values)
        if (std::isfinite(v)) out << QString::number(v, 'f', 3);
    return out;
}

std::vector<double> guide_values_from_strings(const QStringList &values)
{
    std::vector<double> out;
    out.reserve((size_t)values.size());
    for (const QString &value : values) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (ok && std::isfinite(v)) out.push_back(v);
    }
    return out;
}

QColor editor_canvas_helper_color(TitlePreferences::CanvasHelperColorRole role)
{
    return TitlePreferences::canvas_helper_color(role);
}

TitlePreferences::CanvasHelperColorRole snap_feedback_role_from_label(const QString &label)
{
    if (label == fxm_tr("OBSTitles.ObjectEdge") ||
        label == fxm_tr("OBSTitles.ObjectCenter") ||
        label == fxm_tr("OBSTitles.Spacing")) {
        return TitlePreferences::CanvasHelperColorRole::ObjectSnapLines;
    }
    return TitlePreferences::CanvasHelperColorRole::CanvasSnapLines;
}

bool layer_is_shape_sized(const Layer &layer)
{
    return layer_type_uses_shape_geometry(layer.type);
}

bool layer_has_fixed_canvas_geometry(const Layer &)
{
    // Adjustment and color layers start at canvas size, but remain ordinary
    // transformable layers. Their initial geometry is a default, not a lock.
    return false;
}

double shape_resize_metric_factor(double old_w, double old_h, double new_w, double new_h)
{
    constexpr double kEpsilon = 1e-6;
    if (old_w <= kEpsilon || old_h <= kEpsilon || new_w <= kEpsilon || new_h <= kEpsilon)
        return 1.0;
    const double sx = new_w / old_w;
    const double sy = new_h / old_h;
    const bool changed_x = std::abs(new_w - old_w) > kEpsilon;
    const bool changed_y = std::abs(new_h - old_h) > kEpsilon;
    if (changed_x && changed_y)
        return std::sqrt(std::max(0.0, sx * sy));
    if (changed_x)
        return sx;
    if (changed_y)
        return sy;
    return 1.0;
}

std::vector<double> capture_path_corner_radii(const Layer &layer)
{
    std::vector<double> radii;
    radii.reserve(layer.path_points.size());
    for (const auto &point : layer.path_points)
        radii.push_back(point.corner_radius);
    return radii;
}

void apply_shape_resize_metrics_from_state(Layer &layer, double old_w, double old_h,
                                           float start_stroke_width,
                                           float start_corner_radius_tl,
                                           float start_corner_radius_tr,
                                           float start_corner_radius_br,
                                           float start_corner_radius_bl,
                                           float start_shape_roundness,
                                           float start_shape_inner_roundness,
                                           const std::vector<double> &start_path_corner_radii,
                                           double new_w, double new_h)
{
    if (!layer_is_shape_sized(layer))
        return;
    const double factor = shape_resize_metric_factor(old_w, old_h, new_w, new_h);
    if (!std::isfinite(factor) || factor <= 0.0)
        return;
    if (layer.scale_stroke_with_shape &&
        layer.outline_enabled &&
        layer.stroke_fill_type != 0 &&
        start_stroke_width > 0.0f) {
        layer.stroke_width = (float)std::clamp((double)start_stroke_width * factor, 0.0, 512.0);
    }
    if (layer.scale_corners_with_shape) {
        set_layer_corner_radii(layer,
                               (float)std::clamp((double)start_corner_radius_tl * factor, 0.0, 9999.0),
                               (float)std::clamp((double)start_corner_radius_tr * factor, 0.0, 9999.0),
                               (float)std::clamp((double)start_corner_radius_br * factor, 0.0, 9999.0),
                               (float)std::clamp((double)start_corner_radius_bl * factor, 0.0, 9999.0));
        layer.shape_roundness = (float)std::clamp((double)start_shape_roundness * factor, 0.0, 9999.0);
        layer.shape_inner_roundness = (float)std::clamp((double)start_shape_inner_roundness * factor, 0.0, 9999.0);
        if (start_path_corner_radii.size() == layer.path_points.size()) {
            for (size_t i = 0; i < layer.path_points.size(); ++i)
                layer.path_points[i].corner_radius = std::clamp(start_path_corner_radii[i] * factor, 0.0, 9999.0);
        }
    }
}

std::string unique_canvas_layer_name(const Title &title, const std::string &base_name,
                                     const std::set<std::string> &exclude_ids,
                                     std::set<std::string> *reserved_names)
{
    std::string base = QString::fromStdString(base_name).trimmed().toStdString();
    if (base.empty())
        base = editor_text_std("OBSTitles.Layer");

    std::set<std::string> used;
    for (const auto &layer : title.layers) {
        if (!layer || exclude_ids.find(layer->id) != exclude_ids.end()) continue;
        used.insert(layer->name);
    }
    if (reserved_names)
        used.insert(reserved_names->begin(), reserved_names->end());

    if (used.find(base) == used.end()) {
        if (reserved_names) reserved_names->insert(base);
        return base;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        const std::string candidate = QStringLiteral("%1 %2")
            .arg(QString::fromStdString(base))
            .arg(suffix, 2, 10, QChar('0'))
            .toStdString();
        if (used.find(candidate) != used.end()) continue;
        if (reserved_names) reserved_names->insert(candidate);
        return candidate;
    }

    const std::string fallback = base + " " + TitleDataStore::make_uuid().substr(0, 8);
    if (reserved_names) reserved_names->insert(fallback);
    return fallback;
}
}

struct CanvasPreview::GpuDisplayState {
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    gs_swapchain_t *swapchain = nullptr;
    gs_texture_t *underlay = nullptr;
    gs_texture_t *overlay = nullptr;
    gs_texture_t *selection_mask = nullptr;
    uint32_t underlay_w = 0;
    uint32_t underlay_h = 0;
    uint32_t overlay_w = 0;
    uint32_t overlay_h = 0;
    uint32_t selection_mask_w = 0;
    uint32_t selection_mask_h = 0;
#endif
    uint32_t requested_w = 0;
    uint32_t requested_h = 0;
    uint32_t swapchain_w = 0;
    uint32_t swapchain_h = 0;
    QRect artwork_rect_px;
    bool shader_compile_screen_active = false;
    bool enabled = true;
    bool update_color_space = false;
    bool destroying = false;
    std::mutex mutex;
};

#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
static bool canvas_qt_to_gs_window(QWindow *window, gs_window &gswindow)
{
    if (!window)
        return false;
#ifdef _WIN32
    gswindow.hwnd = reinterpret_cast<HWND>(window->winId());
    return gswindow.hwnd != nullptr;
#elif defined(__APPLE__)
    gswindow.view = (id)window->winId();
    return gswindow.view != nullptr;
#else
    switch (obs_get_nix_platform()) {
    case OBS_NIX_PLATFORM_X11_EGL:
        gswindow.id = window->winId();
        gswindow.display = obs_get_nix_platform_display();
        return gswindow.display != nullptr;
#ifdef ENABLE_WAYLAND
    case OBS_NIX_PLATFORM_WAYLAND:
#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
#ifdef OBS_FXM_HAS_QPA_NATIVE_INTERFACE
        if (QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface())
            gswindow.display = native->nativeResourceForWindow("surface", window);
#else
        return false;
#endif
#else
        gswindow.display = reinterpret_cast<void *>(window->winId());
#endif
        return gswindow.display != nullptr;
#endif
    default:
        return false;
    }
#endif
}
#endif

static QImage gpu_upload_image(const QImage &source)
{
    if (source.isNull())
        return QImage();
    QImage image = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!image.isNull() && image.bytesPerLine() != image.width() * 4)
        image = image.copy();
    return image;
}

CanvasPreview::CanvasPreview(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(400, 225);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_StaticContents, true);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
#else
    setAttribute(Qt::WA_OpaquePaintEvent, true);
#endif
    gpu_display_ = std::make_unique<GpuDisplayState>();
    gpu_render_session_ = title_gpu_render_session_create();
    title_gpu_render_session_set_editor_video_decode_client(
        gpu_render_session_, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    load_ruler_guide_settings();

    // Coalesce expensive full-title renders. Mouse/tablet events may arrive far
    // faster than the renderer can finish when a title contains many layers.
    // Keeping at most one scheduled render prevents an ever-growing GUI event
    // backlog and lets OBS keep servicing its own video thread.
    render_coalesce_timer_ = new QTimer(this);
    render_coalesce_timer_->setSingleShot(true);
    render_coalesce_timer_->setTimerType(Qt::PreciseTimer);
    connect(render_coalesce_timer_, &QTimer::timeout, this, [this]() {
        if (!shutting_down_ && dirty_ && isVisible())
            update();
    });

    /* obs_display_t is tied to OBS' project-rate main render loop. The editor
     * needs an independently paced presentation path, so it owns a public
     * libobs graphics swap chain and presents it from the Qt/monitor scheduler.
     * Playback still only queues presents from project-rate transport ticks. */
    present_coalesce_timer_ = new QTimer(this);
    present_coalesce_timer_->setSingleShot(true);
    present_coalesce_timer_->setTimerType(Qt::PreciseTimer);
    connect(present_coalesce_timer_, &QTimer::timeout, this, [this]() {
        if (!shutting_down_ && isVisible())
            update();
    });
    last_render_clock_.start();
    diagnostics_window_timer_.start();

    QSettings adaptive_settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    adaptive_settings.beginGroup(QStringLiteral("titleEditor"));
    adaptive_rendering_enabled_ = adaptive_settings.value(
        QString::fromUtf8(kEditorAdaptiveRenderingKey), false).toBool();
    const int saved_quality = adaptive_settings.value(
        QString::fromUtf8(kEditorAdaptiveQualityKey), 0).toInt();
    adaptive_quality_mode_ = static_cast<AdaptiveQualityMode>(std::clamp(saved_quality, 0, 5));
    adaptive_settings.endGroup();

    adaptive_full_quality_timer_ = new QTimer(this);
    adaptive_full_quality_timer_->setSingleShot(true);
    adaptive_full_quality_timer_->setInterval(140);
    connect(adaptive_full_quality_timer_, &QTimer::timeout, this, [this]() {
        if (shutting_down_)
            return;
        if (!adaptive_interaction_active_)
            return;
        /* Never refine while a canvas manipulation is still active. The old
         * timer expired 140 ms after mouse-down, which silently returned long
         * drags to full resolution and made Adaptive Rendering appear broken. */
        if (drag_mode_ != DragMode::None) {
            adaptive_full_quality_timer_->start();
            return;
        }
        const bool was_reduced_quality = frame_image_preview_scale_ < 0.999;
        end_adaptive_interaction();
        if (was_reduced_quality) {
            dirty_ = true;
            update();
        }
    });

    inline_text_editor_ = new QTextEdit(this);
    inline_text_editor_->hide();
    inline_text_editor_->setAcceptRichText(true);
    inline_text_editor_->setTextInteractionFlags(Qt::TextEditorInteraction);
    inline_text_editor_->setFrameShape(QFrame::NoFrame);
    inline_text_editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inline_text_editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inline_text_editor_->setLineWrapMode(QTextEdit::FixedPixelWidth);
    inline_text_editor_->setCursorWidth(0);
    inline_text_editor_->setContentsMargins(0, 0, 0, 0);
    inline_text_editor_->viewport()->setContentsMargins(0, 0, 0, 0);
    /* CanvasPreview is presented by OBS/D3D and has no QWidget backing store
     * for a transparent raster child to composite against. Keep QTextEdit as
     * the input/document adapter, but let CanvasPreview own all mouse hit
     * testing and all visible text/selection rendering. */
    inline_text_editor_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    inline_text_editor_->setAttribute(Qt::WA_TranslucentBackground, true);
    inline_text_editor_->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
    QPalette inline_palette = inline_text_editor_->palette();
    /* The editor itself stays transparent so the canvas remains the visual
     * source of truth. Selection inversion is applied later in display space;
     * clear every palette group so an inactive/focus-changing QTextEdit cannot
     * reintroduce a native white selection rectangle on some Qt styles. */
    const QColor transparent_selection(0, 0, 0, 0);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive,
                                        QPalette::Disabled}) {
        inline_palette.setColor(group, QPalette::Highlight, transparent_selection);
        inline_palette.setColor(group, QPalette::HighlightedText, transparent_selection);
    }
    inline_text_editor_->setPalette(inline_palette);
    inline_text_editor_->setStyleSheet(
        "QTextEdit{background:transparent;border:0px;padding:0px;"
        "color:rgba(255,255,255,0);"
        "selection-background-color:rgba(0,0,0,0);"
        "selection-color:rgba(0,0,0,0);}");
    inline_text_editor_->installEventFilter(this);
    inline_text_refresh_timer_ = new QTimer(this);
    inline_text_refresh_timer_->setSingleShot(true);
    inline_text_refresh_timer_->setInterval(0);
    connect(inline_text_refresh_timer_, &QTimer::timeout, this, [this]() {
        if (shutting_down_)
            return;
        const bool mark_dirty = inline_text_refresh_mark_dirty_;
        const bool emit_changed = inline_text_refresh_emit_changed_;
        inline_text_refresh_mark_dirty_ = false;
        inline_text_refresh_emit_changed_ = false;
        refresh_inline_text_edit(mark_dirty, emit_changed);
    });
    connect(inline_text_editor_->document(), &QTextDocument::contentsChange,
            this, [this](int position, int chars_removed, int chars_added) {
        if (updating_inline_text_editor_ || refreshing_inline_text_) return;
        if (inline_text_change_count_ == 0) {
            inline_text_change_position_ = position;
            inline_text_change_removed_ = chars_removed;
            inline_text_change_added_ = chars_added;
        }
        ++inline_text_change_count_;
        begin_adaptive_interaction();
        schedule_inline_text_refresh(true, true);
    });
    if (auto *layout = inline_text_editor_->document()->documentLayout()) {
        connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this, [this](const QSizeF &) {
            if (updating_inline_text_editor_ || refreshing_inline_text_) return;
            auto layer = title_ && !inline_text_layer_id_.empty()
                ? title_->find_layer(inline_text_layer_id_) : nullptr;
            if (!layer || (!layer->text_box_width_to_text &&
                           !layer->text_box_height_to_text))
                return;
            /* The final unconstrained size may arrive after the text signal.
             * Coalesce it with the same edit instead of rendering twice. */
            schedule_inline_text_refresh(true, true);
        });
        connect(layout, &QAbstractTextDocumentLayout::update, this, [this](const QRectF &) {
            if (committing_inline_text_ || updating_inline_text_editor_ || inline_text_layer_id_.empty()) return;
            if (inline_text_editor_)
                inline_text_editor_->viewport()->update();
        });
    }
    auto emit_cursor_changed = [this]() {
        if (committing_inline_text_ || updating_inline_text_editor_ || inline_text_layer_id_.empty()) return;
        const std::string layer_id = inline_text_layer_id_;
        /* contentsChange already queued one canonical model update. Avoid
         * processing the same keystroke again from cursorPositionChanged, and
         * do not rebuild every properties panel before the coalesced text
         * transaction has even been applied.  A true cursor/selection move is
         * still published immediately. */
        const bool content_change_pending = inline_text_change_count_ != 0;
        if (!content_change_pending)
            sync_inline_text_layer(false);
        invalidate_canvas_overlay_caches();
        if (inline_text_editor_) {
            inline_text_editor_->viewport()->update();
            update(inline_text_editor_view_rect_.adjusted(-4, -4, 4, 4));
        }
        if (!content_change_pending)
            emit text_edit_cursor_changed(layer_id);
    };
    connect(inline_text_editor_, &QTextEdit::cursorPositionChanged, this, emit_cursor_changed);
    connect(inline_text_editor_, &QTextEdit::selectionChanged, this, emit_cursor_changed);
}

void CanvasPreview::set_read_only(bool read_only)
{
    read_only_ = read_only;
    setAcceptDrops(!read_only);
    setFocusPolicy(read_only ? Qt::NoFocus : Qt::StrongFocus);
    setMouseTracking(!read_only);
    setAttribute(Qt::WA_TransparentForMouseEvents, read_only);
    if (inline_text_editor_) {
        inline_text_editor_->setReadOnly(read_only);
        if (read_only)
            inline_text_editor_->hide();
    }
    if (read_only)
        unsetCursor();
}

CanvasPreview::~CanvasPreview()
{
    prepare_for_shutdown();
}

void CanvasPreview::prepare_for_shutdown()
{
    if (shutting_down_)
        return;
    shutting_down_ = true;

    setUpdatesEnabled(false);
    if (render_coalesce_timer_)
        render_coalesce_timer_->stop();
    if (present_coalesce_timer_)
        present_coalesce_timer_->stop();
    if (adaptive_full_quality_timer_)
        adaptive_full_quality_timer_->stop();
    if (inline_text_refresh_timer_)
        inline_text_refresh_timer_->stop();
    blockSignals(true);

    drag_mode_ = DragMode::None;
    active_extension_handle_ = -1;
    extension_canvas_handles_ = QJsonArray();
    playback_frame_pending_ = false;
    playback_present_pending_ = false;
    editing_present_pending_ = false;
    transport_playback_active_ = false;
    force_present_pending_ = false;
    dirty_ = false;

    if (title_)
        fxm::ticker_runtime::set_adaptive_pause(title_->id, false);
    destroy_gpu_display();
    if (gpu_render_session_) {
        title_gpu_render_session_destroy(gpu_render_session_);
        gpu_render_session_ = nullptr;
    }
    title_.reset();
    frame_image_ = QImage();
    prefetched_cache_frame_ = QImage();
    gpu_overlay_cache_ = QImage();
    editor_quality_cache_.clear();
}

QPaintEngine *CanvasPreview::paintEngine() const
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    /* CanvasPreview is always a native, WA_PaintOnScreen widget. Returning
     * QWidget's backing-store engine while the OBS swapchain is still being
     * created mixes two incompatible paint paths. Qt 6.11 diagnoses that as
     * "QWidget::paintEngine: Should no longer be called" and can repeatedly
     * re-enter backing-store updates during the editor's first expose. OBS
     * owns every canvas presentation, including the pre-swapchain interval. */
    return nullptr;
#else
    return QWidget::paintEngine();
#endif
}

bool CanvasPreview::ensure_gpu_display()
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    if (shutting_down_)
        return false;
    if (!gpu_display_)
        gpu_display_ = std::make_unique<GpuDisplayState>();
    if (gpu_display_->swapchain)
        return true;
    if (gpu_display_->destroying || !isVisible())
        return false;

    (void)winId();
    QWindow *window = windowHandle();
    if (!window || !window->isExposed())
        return false;

    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());
    gs_init_data info = {};
    info.cx = static_cast<uint32_t>(std::max(1, qRound(width() * dpr)));
    info.cy = static_cast<uint32_t>(std::max(1, qRound(height() * dpr)));
    info.format = GS_BGRA;
    info.zsformat = GS_ZS_NONE;
    if (!canvas_qt_to_gs_window(window, info.window))
        return false;

    obs_enter_graphics();
    gpu_display_->swapchain = gs_swapchain_create(&info);
    obs_leave_graphics();
    if (!gpu_display_->swapchain)
        return false;
    gpu_display_->requested_w = info.cx;
    gpu_display_->requested_h = info.cy;
    gpu_display_->swapchain_w = info.cx;
    gpu_display_->swapchain_h = info.cy;
    gpu_display_->enabled = true;
    gpu_display_->update_color_space = true;
    force_present_pending_ = true;
    return true;
#else
    return false;
#endif
}

void CanvasPreview::destroy_gpu_display()
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    if (!gpu_display_)
        return;
    gpu_display_->destroying = true;
    if (present_coalesce_timer_)
        present_coalesce_timer_->stop();

    const bool has_graphics_resources = gpu_display_->swapchain ||
        gpu_display_->underlay || gpu_display_->overlay ||
        gpu_display_->selection_mask;
    if (has_graphics_resources) {
        obs_enter_graphics();
        {
            std::lock_guard<std::mutex> lock(gpu_display_->mutex);
            if (gpu_display_->underlay)
                gs_texture_destroy(gpu_display_->underlay);
            if (gpu_display_->overlay)
                gs_texture_destroy(gpu_display_->overlay);
            if (gpu_display_->selection_mask)
                gs_texture_destroy(gpu_display_->selection_mask);
            gpu_display_->underlay = nullptr;
            gpu_display_->overlay = nullptr;
            gpu_display_->selection_mask = nullptr;
            if (gpu_display_->swapchain)
                gs_swapchain_destroy(gpu_display_->swapchain);
            gpu_display_->swapchain = nullptr;
        }
        obs_leave_graphics();
    }
#else
    gpu_display_.reset();
#endif
}

void CanvasPreview::update_gpu_display_textures(const QImage *underlay_source,
                                                const QImage *overlay_source,
                                                const QImage *selection_mask_source)
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    if (shutting_down_ || !gpu_display_ || !gpu_display_->swapchain)
        return;

    const QImage underlay = underlay_source ? gpu_upload_image(*underlay_source) : QImage();
    const QImage overlay = overlay_source ? gpu_upload_image(*overlay_source) : QImage();
    const QImage selection_mask = selection_mask_source
        ? gpu_upload_image(*selection_mask_source)
        : QImage();
    const qreal dpr = std::max<qreal>(1.0, devicePixelRatioF());

    QRect artwork_rect;
    if (title_) {
        const double scale = view_scale();
        const QPointF origin = view_origin();
        artwork_rect = QRect(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
                             qRound(title_->width * scale * dpr),
                             qRound(title_->height * scale * dpr));
    }

    auto upload_texture = [](gs_texture_t *&texture, uint32_t &stored_w,
                             uint32_t &stored_h, const QImage &image) {
        if (image.isNull()) {
            if (texture)
                gs_texture_destroy(texture);
            texture = nullptr;
            stored_w = stored_h = 0;
            return;
        }
        const uint32_t w = static_cast<uint32_t>(image.width());
        const uint32_t h = static_cast<uint32_t>(image.height());
        if (!texture || stored_w != w || stored_h != h) {
            if (texture)
                gs_texture_destroy(texture);
            const uint8_t *data[1] = {image.constBits()};
            texture = gs_texture_create(w, h, GS_BGRA, 1, data, GS_DYNAMIC);
            stored_w = texture ? w : 0;
            stored_h = texture ? h : 0;
        } else {
            gs_texture_set_image(texture, image.constBits(),
                                 static_cast<uint32_t>(image.bytesPerLine()), false);
        }
    };

    obs_enter_graphics();
    {
        std::lock_guard<std::mutex> lock(gpu_display_->mutex);
        if (underlay_source) {
            upload_texture(gpu_display_->underlay, gpu_display_->underlay_w,
                           gpu_display_->underlay_h, underlay);
        }
        if (overlay_source) {
            upload_texture(gpu_display_->overlay, gpu_display_->overlay_w,
                           gpu_display_->overlay_h, overlay);
        }
        if (selection_mask_source) {
            upload_texture(gpu_display_->selection_mask,
                           gpu_display_->selection_mask_w,
                           gpu_display_->selection_mask_h, selection_mask);
        }
        gpu_display_->artwork_rect_px = artwork_rect;
    }
    obs_leave_graphics();
#else
    (void)underlay_source;
    (void)overlay_source;
    (void)selection_mask_source;
#endif
}

bool CanvasPreview::present_gpu_display_if_due()
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    if (shutting_down_ || !gpu_display_ || !gpu_display_->swapchain ||
        !gpu_display_->enabled || !isVisible())
        return false;

    /* While transport is active, generic repaints may not create extra
     * frames. A project tick or an explicit scrub/edit request must authorize
     * the present; the latter remains monitor-capped. */
    if (transport_playback_active_ && !playback_present_pending_ &&
        !editing_present_pending_)
        return false;

    /* An explicit scrub/edit request remains monitor-paced even if transport
     * was still armed when the pointer interaction began. Only a real
     * transport tick receives the project-rate bypass. */
    const bool project_rate_present = transport_playback_active_ &&
        playback_present_pending_ && !editing_present_pending_;
    const int cadence_ms = project_rate_present ? 0 : display_refresh_interval_ms_;
    const qint64 elapsed_ms = last_present_clock_.isValid()
        ? last_present_clock_.elapsed() : cadence_ms;
    if (!force_present_pending_ && elapsed_ms < cadence_ms) {
        if (present_coalesce_timer_ && !present_coalesce_timer_->isActive())
            present_coalesce_timer_->start(
                std::max(1, cadence_ms - static_cast<int>(elapsed_ms)));
        return false;
    }

    const uint32_t cx = std::max<uint32_t>(1, gpu_display_->requested_w);
    const uint32_t cy = std::max<uint32_t>(1, gpu_display_->requested_h);
    bool presented = false;
    qint64 graphics_wait_ns = 0;
    qint64 gpu_render_ns = 0;
    qint64 present_ns = 0;
    QElapsedTimer presentation_total;
    QElapsedTimer phase_timer;
    presentation_total.start();

    /* Measure presentation cadence from the start of the present attempt.
     * Some graphics backends may block briefly inside gs_present(); counting
     * that time prevents an accidental second monitor-interval delay. */
    last_present_clock_.restart();
    phase_timer.start();
    obs_enter_graphics();
    graphics_wait_ns = phase_timer.nsecsElapsed();
    gs_load_swapchain(gpu_display_->swapchain);
    if (gpu_display_->swapchain_w != cx || gpu_display_->swapchain_h != cy) {
        gs_resize(cx, cy);
        gpu_display_->swapchain_w = cx;
        gpu_display_->swapchain_h = cy;
    }
    if (gpu_display_->update_color_space)
        gs_update_color_space();
    gpu_display_->update_color_space = false;

    if (gs_is_present_ready()) {
        gs_begin_scene();
        struct vec4 clear_color;
        vec4_set(&clear_color, 0.0f, 0.0f, 0.0f, 1.0f);
        /* The swapchain shares state with retained 3D passes. Re-arm writes,
         * depth, culling and blending before clearing the next presentation. */
        gs_enable_color(true, true, true, true);
        gs_enable_depth_test(false);
        gs_depth_function(GS_LEQUAL);
        gs_set_cull_mode(GS_NEITHER);
        gs_enable_blending(false);
        gs_clear(GS_CLEAR_COLOR | GS_CLEAR_DEPTH | GS_CLEAR_STENCIL,
                 &clear_color, 1.0f, 0);
        gs_ortho(0.0f, static_cast<float>(cx), 0.0f,
                 static_cast<float>(cy), -100.0f, 100.0f);
        gs_set_viewport(0, 0, cx, cy);
        phase_timer.restart();
        gpu_display_draw(this, cx, cy);
        gpu_render_ns = phase_timer.nsecsElapsed();
        gs_end_scene();
        phase_timer.restart();
        gs_present();
        present_ns = phase_timer.nsecsElapsed();
        presented = true;
    }
    obs_leave_graphics();

    if (presented) {
        const qint64 presentation_total_ns = presentation_total.nsecsElapsed();
        TitleGpuRenderDiagnostics diagnostics;
        const bool have_diagnostics = gpu_render_session_ &&
            title_gpu_render_session_get_diagnostics(
                gpu_render_session_, diagnostics);
        const int requested_width = have_diagnostics
            ? static_cast<int>(diagnostics.requested_output_width)
            : (title_ ? title_->width : 0);
        const int requested_height = have_diagnostics
            ? static_cast<int>(diagnostics.requested_output_height)
            : (title_ ? title_->height : 0);
        const int render_target_width = have_diagnostics
            ? static_cast<int>(diagnostics.render_target_width) : 0;
        const int render_target_height = have_diagnostics
            ? static_cast<int>(diagnostics.render_target_height) : 0;
        const double actual_preview_scale = have_diagnostics
            ? diagnostics.preview_quality_scale
            : frame_image_preview_scale_;
        const qint64 preview_upload_ns = pending_preview_upload_ns_;
        pending_preview_upload_ns_ = 0;
        record_frame_pipeline_sample(
            graphics_wait_ns, gpu_render_ns, preview_upload_ns, present_ns,
            presentation_total_ns, requested_width, requested_height,
            render_target_width, render_target_height,
            actual_preview_scale);
        FXM_LOG_TRACE("Performance", QStringLiteral(
            "stage=canvas-frame-pipeline title=%1 target=%2x%3 actual=%4x%5 swapchain=%6x%7 scale=%8 prepareMs=%9 graphicsWaitMs=%10 renderSubmitMs=%11 previewUploadMs=%12 presentWaitMs=%13 readbackMs=0 totalMs=%14 playback=%15")
            .arg(title_ ? QString::fromStdString(title_->id)
                        : QStringLiteral("<none>"))
            .arg(requested_width).arg(requested_height)
            .arg(render_target_width).arg(render_target_height)
            .arg(cx).arg(cy)
            .arg(actual_preview_scale, 0, 'f', 3)
            .arg(double(last_prepare_cost_ns_) / 1000000.0, 0, 'f', 3)
            .arg(double(graphics_wait_ns) / 1000000.0, 0, 'f', 3)
            .arg(double(gpu_render_ns) / 1000000.0, 0, 'f', 3)
            .arg(double(preview_upload_ns) / 1000000.0, 0, 'f', 3)
            .arg(double(present_ns) / 1000000.0, 0, 'f', 3)
            .arg(double(presentation_total_ns + preview_upload_ns +
                        last_prepare_cost_ns_) / 1000000.0, 0, 'f', 3)
            .arg(transport_playback_active_ ? 1 : 0));
        if (project_rate_present)
            record_live_playback_present();
        playback_present_pending_ = false;
        editing_present_pending_ = false;
        force_present_pending_ = false;
    }
    return presented;
#else
    return false;
#endif
}

void CanvasPreview::gpu_display_draw(void *data, uint32_t cx, uint32_t cy)
{
#if defined(FXM_CANVAS_WITH_OBS) && FXM_CANVAS_WITH_OBS
    auto *canvas = static_cast<CanvasPreview *>(data);
    if (!canvas || canvas->shutting_down_ || !canvas->gpu_display_)
        return;
    auto &state = *canvas->gpu_display_;
    std::lock_guard<std::mutex> lock(state.mutex);

    gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    if (!effect)
        return;
    gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
    if (!image)
        return;

    auto draw_texture = [&](gs_texture_t *texture, const QRect &rect) {
        if (!texture || rect.isEmpty())
            return;
        gs_effect_set_texture(image, texture);
        gs_matrix_push();
        gs_matrix_identity();
        gs_matrix_translate3f(static_cast<float>(rect.x()),
                              static_cast<float>(rect.y()), 0.0f);
        while (gs_effect_loop(effect, "Draw"))
            gs_draw_sprite(texture, 0, static_cast<uint32_t>(rect.width()),
                           static_cast<uint32_t>(rect.height()));
        gs_matrix_pop();
    };

    gs_viewport_push();
    gs_projection_push();
    gs_blend_state_push();
    gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy),
             -100.0f, 100.0f);
    gs_enable_blending(true);
    gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

    draw_texture(state.underlay, QRect(0, 0, static_cast<int>(cx), static_cast<int>(cy)));
    if (state.shader_compile_screen_active) {
        gs_blend_state_pop();
        gs_projection_pop();
        gs_viewport_pop();
        return;
    }
    if (canvas->gpu_render_session_ && !state.artwork_rect_px.isEmpty()) {
        gs_matrix_push();
        gs_matrix_identity();
        gs_matrix_translate3f(static_cast<float>(state.artwork_rect_px.x()),
                              static_cast<float>(state.artwork_rect_px.y()), 0.0f);
        const bool destination_composite = canvas->title_ &&
            title_requires_destination_compositing(*canvas->title_);
        const bool artwork_drawn = destination_composite && state.underlay
            ? title_gpu_render_session_draw_over_background_rect(
                  canvas->gpu_render_session_, state.underlay,
                  state.underlay_w, state.underlay_h,
                  static_cast<float>(state.artwork_rect_px.x()),
                  static_cast<float>(state.artwork_rect_px.y()),
                  static_cast<float>(state.artwork_rect_px.width()),
                  static_cast<float>(state.artwork_rect_px.height()),
                  static_cast<uint32_t>(state.artwork_rect_px.width()),
                  static_cast<uint32_t>(state.artwork_rect_px.height()),
                  false)
            : title_gpu_render_session_draw(
                  canvas->gpu_render_session_,
                  static_cast<uint32_t>(state.artwork_rect_px.width()),
                  static_cast<uint32_t>(state.artwork_rect_px.height()));
        const bool draw_deferred =
            title_gpu_render_session_last_draw_deferred(
                canvas->gpu_render_session_);
        const std::string gpu_error = (!artwork_drawn || draw_deferred)
            ? title_gpu_render_session_last_error(canvas->gpu_render_session_)
            : std::string();
        if (TitleLogger::wouldLog(TitleLogLevel::Trace,
                                  "RenderDiagnostics")) {
            TitleGpuRenderDiagnostics draw_diagnostics;
            const bool have_draw_diagnostics =
                title_gpu_render_session_get_diagnostics(
                    canvas->gpu_render_session_, draw_diagnostics);
            FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
                "stage=canvas-present canvas=%1 session=%2 title=%3 playhead=%4 result=%5 deferred=%6 output=%7x%8 sessionValid=%9 sessionTime=%10 sessionUpdate=%11 sessionRender=%12 sessionPublish=%13 publishedTime=%14 modelRevision=%15 publishedRevision=%16 dirty=%17 pendingRasters=%18 extrusionLayers=%19 lights=%20 depthRuns=%21 depthLayers=%22 extrusionPasses=%23 error=%24")
                .arg(reinterpret_cast<quintptr>(canvas), 0, 16)
                .arg(reinterpret_cast<quintptr>(canvas->gpu_render_session_), 0, 16)
                .arg(canvas->title_
                         ? QString::fromStdString(canvas->title_->id)
                         : QStringLiteral("<none>"))
                .arg(canvas->playhead_, 0, 'f', 9)
                .arg(artwork_drawn ? QStringLiteral("drawn")
                                   : QStringLiteral("failed"))
                .arg(draw_deferred ? 1 : 0)
                .arg(state.artwork_rect_px.width())
                .arg(state.artwork_rect_px.height())
                .arg(have_draw_diagnostics ? 1 : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.session_time : -1.0,
                     0, 'f', 9)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.update_serial : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.render_serial : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.publish_serial : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.last_published_time : -1.0,
                     0, 'f', 9)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.model_revision : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.published_model_revision : 0)
                .arg(have_draw_diagnostics && draw_diagnostics.frame_dirty ? 1 : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.pending_raster_count : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.extrusion_layer_count : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.visible_light_layer_count : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.hardware_depth_run_count : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.hardware_depth_layer_count : 0)
                .arg(have_draw_diagnostics
                         ? draw_diagnostics.extrusion_pass_count : 0)
                .arg(gpu_error.empty() ? QStringLiteral("none")
                                       : QString::fromStdString(gpu_error)));
        }
        if (!artwork_drawn || draw_deferred) {
            FXM_LOG_DEBUG("Canvas", QStringLiteral(
                "GPU canvas draw deferred title=%1 output=%2x%3 error=%4")
                .arg(canvas->title_
                         ? QString::fromStdString(canvas->title_->id)
                         : QStringLiteral("<none>"))
                .arg(state.artwork_rect_px.width())
                .arg(state.artwork_rect_px.height())
                .arg(gpu_error.empty() ? QStringLiteral("unknown")
                                       : QString::fromStdString(gpu_error)));
            /* A permanent GPU-text backend failure changes the next model
             * preparation path to the compatibility raster. The GPU presentation path
             * itself must not mutate the Qt/editor model, so queue exactly one
             * recovery snapshot on the widget thread. This also retries transient
             * first-frame atlas failures without requiring a user edit. */
            const bool shader_compile_pending =
                gpu_error.rfind("Shader compile pending:", 0) == 0;
            bool expected = false;
            if (canvas->gpu_recovery_queued_.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                FXM_LOG_DEBUG("RenderDiagnostics", QStringLiteral(
                    "stage=canvas-recovery-queue canvas=%1 session=%2 playhead=%3 drawn=%4 deferred=%5 error=%6")
                    .arg(reinterpret_cast<quintptr>(canvas), 0, 16)
                    .arg(reinterpret_cast<quintptr>(canvas->gpu_render_session_), 0, 16)
                    .arg(canvas->playhead_, 0, 'f', 9)
                    .arg(artwork_drawn ? 1 : 0)
                    .arg(draw_deferred ? 1 : 0)
                    .arg(gpu_error.empty()
                             ? QStringLiteral("none")
                             : QString::fromStdString(gpu_error)));
                QMetaObject::invokeMethod(canvas, [canvas, shader_compile_pending]() {
                    const auto retry = [canvas]() {
                        canvas->gpu_recovery_queued_.store(
                            false, std::memory_order_release);
                        if (!canvas->title_ || !canvas->gpu_render_session_)
                            return;
                        canvas->gpu_model_dirty_ = true;
                        canvas->full_gpu_model_refresh_pending_ = true;
                        canvas->force_live_full_quality_render_ = true;
                        canvas->dirty_ = true;
                        /* Re-enter model preparation after asynchronous shader
                         * completion. A repaint alone redraws the old texture. */
                        canvas->refresh_preview();
                    };
                    if (shader_compile_pending)
                        QTimer::singleShot(50, canvas, retry);
                    else
                        retry();
                }, Qt::QueuedConnection);
            }
        }
        if (TitlePreferences::cache_playback_logging_enabled()) {
            FXM_LOG_INFO("CachePlayback", QStringLiteral(
                "consumer=editor-canvas stage=draw result=%1 output=%2x%3 rect=%4,%5,%6x%7 error=%8")
                .arg(draw_deferred ? QStringLiteral("deferred")
                                   : artwork_drawn ? QStringLiteral("ok")
                                                   : QStringLiteral("failed"))
                .arg(state.artwork_rect_px.width()).arg(state.artwork_rect_px.height())
                .arg(state.artwork_rect_px.x()).arg(state.artwork_rect_px.y())
                .arg(state.artwork_rect_px.width()).arg(state.artwork_rect_px.height())
                .arg(gpu_error.empty() ? QStringLiteral("none")
                                       : QString::fromStdString(gpu_error)));
        }
        gs_matrix_pop();
    }
    if (state.selection_mask) {
        /* A white texel with inverse-destination/inverse-source blending
         * produces 1 - destination; a transparent black texel leaves the
         * destination untouched. Keep alpha unchanged so this is a true
         * display-space inversion rather than a white alpha overlay. */
        gs_blend_function_separate(GS_BLEND_INVDSTCOLOR, GS_BLEND_INVSRCCOLOR,
                                   GS_BLEND_ZERO, GS_BLEND_ONE);
        draw_texture(state.selection_mask,
                     QRect(0, 0, static_cast<int>(cx), static_cast<int>(cy)));
        gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
    }
    draw_texture(state.overlay, QRect(0, 0, static_cast<int>(cx), static_cast<int>(cy)));

    gs_blend_state_pop();
    gs_projection_pop();
    gs_viewport_pop();
#else
    (void)data;
    (void)cx;
    (void)cy;
#endif
}


void CanvasPreview::begin_text_edit_for_layer(const std::string &layer_id)
{
    if (!title_ || layer_id.empty()) return;
    auto layer = title_->find_layer(layer_id);
    if (!layer || !is_canvas_text_layer(*layer) || layer->type == LayerType::Clock) return;
    selected_layer_ids_ = {layer_id};
    sel_layer_id_ = layer_id;
    invalidate_canvas_overlay_caches();
    begin_text_edit(layer);
}

void CanvasPreview::apply_active_text_char_format(const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask)
{
    (void)format;
    (void)mask;
    if (!inline_text_editor_ || inline_text_layer_id_.empty() || inline_text_layer_id_ != layer_id)
        return;
    auto layer = title_ ? title_->find_layer(layer_id) : nullptr;
    if (!layer)
        return;

    /* The Properties panel has already updated the canonical RichTextDocument.
     * Rebuild the transparent Qt editing adapter from that model instead of
     * incrementally merging QTextCharFormat fragments. This preserves sparse
     * masks, mixed styles/colors and independent H/V scale. */
    const QTextCursor saved_cursor = inline_text_editor_->textCursor();
    const int anchor = saved_cursor.anchor();
    const int position = saved_cursor.position();
    const double visual_scale = inline_text_visual_scale(*layer);
    const bool previous_updating = updating_inline_text_editor_;
    updating_inline_text_editor_ = true;
    {
        QSignalBlocker blocker(inline_text_editor_);
        rich_text_document_ensure_canonical(*layer);
        const double local_time = std::max(0.0, playhead_ - layer->in_time);
        const RichTextDocument editor_model =
            rich_text_document_for_editor_time(*layer, local_time);
        populate_qtext_document_from_rich_text(inline_text_editor_->document(),
                                               editor_model, visual_scale);
        QTextCursor restored(inline_text_editor_->document());
        const int text_len = inline_text_editor_->toPlainText().size();
        restored.setPosition(std::clamp(anchor, 0, text_len));
        restored.setPosition(std::clamp(position, 0, text_len), QTextCursor::KeepAnchor);
        inline_text_editor_->setTextCursor(restored);
        if (!restored.hasSelection()) {
            const RichTextCharFormat effective_typing =
                rich_text_effective_typing_format(layer->rich_text);
            QTextCharFormat typing = qtext_format_from_rich_text_format(
                effective_typing, visual_scale);
            apply_editor_rich_text_baseline_delta(
                typing, effective_typing, layer->rich_text.default_format.baseline_shift,
                visual_scale);
            typing.setProperty(RichTextPropManualMask,
                               (uint)(layer->rich_text.typing_format_mask & RichTextCharAll));
            typing.setProperty(RichTextPropAutoGenerated, false);
            inline_text_editor_->setCurrentCharFormat(typing);
        }
        inline_text_editor_->document()->setModified(false);
        /* A Properties edit has already been checkpointed in the title-level
         * history and applied to the canonical RichTextDocument. Rebuilding
         * the QTextEdit adapter creates implementation-defined local undo
         * commands on some Qt backends. Drop those adapter-only commands so
         * Ctrl+Z cannot restore stale formatting over the canonical model; the
         * next undo is routed to TitleEditor history instead. */
        QTextDocument *adapter_document = inline_text_editor_->document();
        const bool undo_redo_enabled = adapter_document->isUndoRedoEnabled();
        adapter_document->setUndoRedoEnabled(false);
        adapter_document->setUndoRedoEnabled(undo_redo_enabled);
        adapter_document->setModified(false);
    }
    inline_text_last_visual_scale_ = visual_scale;
    updating_inline_text_editor_ = previous_updating;
    invalidate_canvas_overlay_caches();
    dirty_ = true;
    gpu_model_dirty_ = true;
    position_text_editor();
    update();
    inline_text_editor_->viewport()->update();
    update(inline_text_editor_view_rect_.adjusted(-4, -4, 4, 4));
}


QPointF CanvasPreview::view_center_canvas_point() const
{
    return view_to_canvas(QPointF(width() * 0.5, height() * 0.5));
}

void CanvasPreview::set_title(std::shared_ptr<Title> t, bool preserve_view,
                              bool commit_active_text)
{
    if (shutting_down_)
        return;
    /* A normal title switch commits the live adapter. Undo/redo, however,
     * has already replaced the canonical Title model. In that case the old
     * QTextDocument is stale and must be discarded without writing it back
     * over the restored RichTextDocument. */
    if (commit_active_text)
        commit_text_edit(true);
    else
        commit_text_edit(false, false);
    if (title_)
        fxm::ticker_runtime::set_adaptive_pause(title_->id, false);
    title_ = t;
    diagnostics_playhead_request_serial_ = 0;
    diagnostics_render_request_serial_ = 0;
    diagnostics_last_prepared_playhead_ =
        std::numeric_limits<double>::quiet_NaN();
    diagnostics_repeated_prepared_playhead_count_ = 0;
    diagnostics_repeated_playhead_clock_.invalidate();
    invalidate_motion_path_sample_cache();
    selected_position_keyframe_index_ = -1;
    motion_path_hover_type_ = MotionPathHoverType::None;
    motion_path_hover_keyframe_index_ = -1;
    spatial_tangent_drag_ = SpatialTangentDragState{};
    spatial_vertex_drag_ = SpatialVertexDragState{};
    gizmo_drag_ = GizmoDragState{};
    editor_projection_title_.reset();
    editor_projection_dirty_ = true;
    editor_camera_transform_only_pending_ = false;
    interactive_transform_pending_ = false;
    interactive_settle_pending_ = false;
    full_gpu_model_refresh_pending_ = true;
    if (!preserve_view) {
        editor_3d_view_ = Editor3DView::ActiveCamera;
        reset_editor_3d_navigation();
        emit editor_3d_view_changed(static_cast<int>(editor_3d_view_));
    }
    FXM_LOG_INFO("Canvas", QStringLiteral(
        "Set canvas title=%1 preserveView=%2")
        .arg(title_ ? QString::fromStdString(title_->id) : QStringLiteral("<none>"))
        .arg(preserve_view ? 1 : 0));
    gpu_model_dirty_ = true;
    if (gpu_render_session_) {
        /* Opening a title that already contains 3D text must not reuse a stale
         * 2D presentation target from the previously edited title. Force a
         * presentation discard before the first queued paint so the initial
         * 3D text/content pass is visible without waiting for an unrelated
         * selection or property change. */
        title_gpu_render_session_invalidate_presentation(gpu_render_session_, true);
    }
    playback_frame_pending_ = false;
    prefetched_cache_frame_ = QImage();
    prefetched_cache_time_ = -1.0;
    last_runtime_clock_second_ = std::numeric_limits<qint64>::min();
    editor_quality_cache_.clear();
    invalidate_canvas_overlay_caches();
    dirty_ = true;
    adaptive_interaction_active_ = false;
    force_live_full_quality_render_ = false;
    frame_image_preview_scale_ = 1.0;
    last_full_quality_render_cost_ms_ = 0;
    last_adaptive_render_cost_ms_ = 0;
    last_render_cost_ms_ = 0;
    last_prepare_cost_ns_ = 0;

    /* Prime the immutable GPU model immediately when a title is opened. The
     * GPU swap chain can request its first presentation before QWidget's
     * deferred paint scheduler reaches render_to_frame(). Session update does
     * no GPU draw/upload here; it only prepares pending layer resources.
     *
     * Do not mark the model clean after this early snapshot. open_title() still
     * reflects the selected layer into the properties/effects panels after
     * set_title() returns, and that setup can finalize rich-text state without
     * constituting a user edit. A queued refresh below runs after that editor
     * initialization cycle and guarantees that the first normal presentation tick
     * receives the final text model even when caching is disabled. */
    const std::shared_ptr<Title> primed_source_title = title_;
    const std::shared_ptr<Title> primed_title = projection_title();
    if (primed_title && gpu_render_session_) {
        ++gpu_model_revision_;
        title_gpu_render_session_set_render_request(
            gpu_render_session_, TitleGpuRenderRequest{
                static_cast<uint32_t>(std::max(1, primed_title->width)),
                static_cast<uint32_t>(std::max(1, primed_title->height)),
                1.0 / std::max(1.0, primed_title->frame_rate)});
        title_gpu_render_session_update(gpu_render_session_, *primed_title,
                                        playhead_, gpu_model_revision_);
    }
    QTimer::singleShot(0, this, [this, primed_source_title]() {
        if (shutting_down_ || !primed_source_title || title_ != primed_source_title)
            return;
        /* Dock restoration and the conditional 3D toolbar can change the
         * final viewport after set_title() performs its immediate fit. Refit
         * once the queued Qt layout pass has settled, while preserving an
         * explicit user zoom if Fit was disabled in the meantime. */
        if (fit_zoom_active_)
            fit_canvas(fit_zoom_up_to_100_);
        gpu_model_dirty_ = true;
        dirty_ = true;
        update();
    });
    if (!preserve_view) {
        pan_offset_ = QPointF(0, 0);
        /* Opening a document uses the non-upscaling fit. On large/high-DPI
         * workspaces an unrestricted fit can enlarge a 1080p canvas beyond
         * 100%, which looks offset even though its mathematical center is
         * correct. Explicit user Fit choices remain available afterwards. */
        if (title_) fit_canvas(true);
        else update();
    } else {
        update();
    }
    position_text_editor();
}

CanvasPreview::ViewState CanvasPreview::view_state() const
{
    ViewState state;
    state.zoom_percent = zoom_percent_;
    state.fit_zoom_active = fit_zoom_active_;
    state.fit_zoom_up_to_100 = fit_zoom_up_to_100_;
    state.pan_offset = pan_offset_;
    state.editor_3d_view = editor_3d_view_;
    state.editor_3d_target = editor_3d_target_;
    state.editor_3d_distance = editor_3d_distance_;
    state.editor_3d_yaw = editor_3d_yaw_;
    state.editor_3d_pitch = editor_3d_pitch_;
    state.editor_3d_ortho_zoom = editor_3d_ortho_zoom_;
    state.editor_3d_depth_debug_visible = editor_3d_depth_debug_visible_;
    state.editor_3d_normals_visible = editor_3d_normals_visible_;
    return state;
}

void CanvasPreview::restore_view_state(const ViewState &state)
{
    zoom_percent_ = std::clamp(state.zoom_percent, 5, 1600);
    fit_zoom_active_ = state.fit_zoom_active;
    fit_zoom_up_to_100_ = state.fit_zoom_up_to_100;
    pan_offset_ = state.pan_offset;
    editor_3d_view_ = state.editor_3d_view;
    editor_3d_target_ = state.editor_3d_target;
    editor_3d_distance_ = std::clamp(state.editor_3d_distance, 1.0, 10000000.0);
    editor_3d_yaw_ = state.editor_3d_yaw;
    editor_3d_pitch_ = std::clamp(state.editor_3d_pitch, -89.9, 89.9);
    editor_3d_ortho_zoom_ = std::clamp(state.editor_3d_ortho_zoom, 0.001, 1000.0);
    editor_3d_depth_debug_visible_ = state.editor_3d_depth_debug_visible;
    editor_3d_normals_visible_ = state.editor_3d_normals_visible;
    editor_projection_dirty_ = true;
    editor_camera_transform_only_pending_ = true;
    invalidate_canvas_overlay_caches();
    dirty_ = true;
    emit zoom_percent_changed(zoom_percent_);
    emit editor_3d_view_changed(static_cast<int>(editor_3d_view_));
    position_text_editor();
    update();
}

void CanvasPreview::set_playhead(double t, bool playback_frame)
{
    if (shutting_down_)
        return;
    const uint64_t diagnostic_request_serial =
        ++diagnostics_playhead_request_serial_;
    const double previous_playhead = playhead_;
    const bool same_playhead = std::abs(previous_playhead - t) < 1e-9;
    FXM_LOG_TRACE("RenderDiagnostics", QStringLiteral(
        "stage=canvas-playhead-request canvas=%1 request=%2 title=%3 requestedTime=%4 previousTime=%5 same=%6 playbackFrame=%7 transportActive=%8 dirty=%9 renderInProgress=%10 playbackPending=%11 presentPending=%12 editingPending=%13 modelDirty=%14 fullRefresh=%15")
        .arg(reinterpret_cast<quintptr>(this), 0, 16)
        .arg(diagnostic_request_serial)
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(t, 0, 'f', 9)
        .arg(previous_playhead, 0, 'f', 9)
        .arg(same_playhead ? 1 : 0)
        .arg(playback_frame ? 1 : 0)
        .arg(transport_playback_active_ ? 1 : 0)
        .arg(dirty_ ? 1 : 0)
        .arg(render_in_progress_ ? 1 : 0)
        .arg(playback_frame_pending_ ? 1 : 0)
        .arg(playback_present_pending_ ? 1 : 0)
        .arg(editing_present_pending_ ? 1 : 0)
        .arg(gpu_model_dirty_ ? 1 : 0)
        .arg(full_gpu_model_refresh_pending_ ? 1 : 0));
    if (same_playhead) {
        /* A scrub may land on the currently displayed frame while a queued
         * transport repaint is still pending. Reclassify it as editing even
         * though the numeric playhead value did not change. */
        if (!playback_frame) {
            playback_frame_pending_ = false;
            playback_present_pending_ = false;
            editing_present_pending_ = true;
            update();
        } else {
            editing_present_pending_ = false;
            playback_present_pending_ = true;
            update();
        }
        return;
    }
    playhead_ = t;
    diagnostics_repeated_prepared_playhead_count_ = 0;
    diagnostics_last_prepared_playhead_ =
        std::numeric_limits<double>::quiet_NaN();
    if (diagnostics_repeated_playhead_clock_.isValid())
        diagnostics_repeated_playhead_clock_.restart();
    else
        diagnostics_repeated_playhead_clock_.start();
    /* Editor helpers are rendered into a separate full-widget QImage and then
     * uploaded to the swapchain. Rebuilding that CPU image on every transport
     * tick was a resolution-independent playback cost and could dominate the
     * reduced artwork target. Freeze editor chrome during ordinary playback;
     * scrubbing/editing still refreshes it immediately, and stopping playback
     * invalidates it once at the final playhead. */
    if (!playback_frame)
        invalidate_canvas_overlay_caches();
    /* Only real transport ticks bypass editor refresh pacing. Timeline
     * scrubbing is always editing and therefore remains capped by the monitor
     * refresh rate, independent of the surrounding transport state. */
    playback_frame_pending_ = playback_frame;
    playback_present_pending_ = playback_frame;
    editing_present_pending_ = !playback_frame;
    dirty_ = true;
    position_text_editor();
    update();
}

void CanvasPreview::set_display_refresh_rate(double hz)
{
    if (!std::isfinite(hz) || hz < 1.0)
        hz = 60.0;
    /* Round upward so the integer timer interval can never schedule editing
     * frames faster than the monitor's reported refresh rate. */
    display_refresh_interval_ms_ = std::max(
        1, static_cast<int>(std::ceil(1000.0 / std::clamp(hz, 1.0, 1000.0))));
    if (present_coalesce_timer_ && present_coalesce_timer_->isActive()) {
        present_coalesce_timer_->stop();
        update();
    }
}

void CanvasPreview::set_playback_active(bool active)
{
    if (transport_playback_active_ == active)
        return;
    transport_playback_active_ = active;
    reset_live_playback_fps_measurement();
    if (active) {
        adaptive_auto_scale_ = 1.0;
        adaptive_over_budget_frames_ = 0;
        adaptive_under_budget_frames_ = 0;
        adaptive_present_interval_clock_.invalidate();
        playback_frame_pending_ = false;
        playback_present_pending_ = false;
        editing_present_pending_ = false;
        if (present_coalesce_timer_)
            present_coalesce_timer_->stop();
    } else {
        const bool restore_full_quality = adaptive_auto_scale_ < 0.999;
        adaptive_auto_scale_ = 1.0;
        adaptive_over_budget_frames_ = 0;
        adaptive_under_budget_frames_ = 0;
        adaptive_present_interval_clock_.invalidate();
        playback_frame_pending_ = false;
        playback_present_pending_ = false;
        editing_present_pending_ = true;
        force_present_pending_ = true;
        invalidate_canvas_overlay_caches();
        if (restore_full_quality)
            force_live_full_quality_render_ = true;
        dirty_ = true;
        update();
    }
}

void CanvasPreview::reset_live_playback_fps_measurement()
{
    live_fps_frame_count_ = 0;
    measured_live_fps_ = 0.0;
    render_cost_accumulator_ns_ = 0;
    render_cost_sample_count_ = 0;
    average_render_cost_ms_ = 0.0;
    gpu_render_cost_accumulator_ns_ = 0;
    graphics_wait_accumulator_ns_ = 0;
    preview_upload_accumulator_ns_ = 0;
    present_wait_accumulator_ns_ = 0;
    total_frame_cost_accumulator_ns_ = 0;
    pending_preview_upload_ns_ = 0;
    pipeline_sample_count_ = 0;
    if (diagnostics_window_timer_.isValid())
        diagnostics_window_timer_.restart();
    else
        diagnostics_window_timer_.start();
}

void CanvasPreview::record_live_playback_present()
{
    ++live_fps_frame_count_;
}

void CanvasPreview::record_frame_pipeline_sample(
    qint64 graphics_wait_ns, qint64 gpu_render_ns,
    qint64 preview_upload_ns, qint64 present_ns,
    qint64 presentation_total_ns, int requested_width,
    int requested_height, int render_target_width,
    int render_target_height, double preview_scale)
{
    const qint64 prepare_ns = std::max<qint64>(0, last_prepare_cost_ns_);
    const qint64 total_ns = std::max<qint64>(
        1, prepare_ns + std::max<qint64>(0, preview_upload_ns) +
               std::max<qint64>(0, presentation_total_ns));
    gpu_render_cost_accumulator_ns_ += std::max<qint64>(0, gpu_render_ns);
    graphics_wait_accumulator_ns_ += std::max<qint64>(0, graphics_wait_ns);
    preview_upload_accumulator_ns_ += std::max<qint64>(0, preview_upload_ns);
    present_wait_accumulator_ns_ += std::max<qint64>(0, present_ns);
    total_frame_cost_accumulator_ns_ += total_ns;
    ++pipeline_sample_count_;
    diagnostics_requested_width_ = requested_width;
    diagnostics_requested_height_ = requested_height;
    diagnostics_render_target_width_ = render_target_width;
    diagnostics_render_target_height_ = render_target_height;
    diagnostics_preview_scale_ = preview_scale;

    double present_interval_ms = 0.0;
    if (adaptive_present_interval_clock_.isValid())
        present_interval_ms = adaptive_present_interval_clock_.nsecsElapsed() /
            1000000.0;
    adaptive_present_interval_clock_.restart();
    if (transport_playback_active_)
        update_adaptive_playback_quality(
            static_cast<double>(total_ns) / 1000000.0,
            static_cast<double>(std::max<qint64>(1, total_ns - present_ns)) /
                1000000.0,
            present_interval_ms);
}

void CanvasPreview::update_adaptive_playback_quality(
    double processing_ms, double workload_ms, double present_interval_ms)
{
    if (!adaptive_rendering_enabled_ ||
        adaptive_quality_mode_ != AdaptiveQualityMode::Auto ||
        !transport_playback_active_ || adaptive_interaction_active_ ||
        !title_ || !std::isfinite(processing_ms))
        return;

    static constexpr double kScales[] = {1.0, 0.75, 0.5, 0.375, 0.25};
    const double frame_budget_ms = 1000.0 /
        std::max(1.0, title_->frame_rate);
    const double effective_ms = present_interval_ms > 0.0
        ? std::max(processing_ms, present_interval_ms)
        : processing_ms;
    const bool over_budget = effective_ms > frame_budget_ms * 1.10;
    /* VSync wait is part of end-to-end latency but not evidence that a higher
     * render resolution is unaffordable. Restore quality only when the actual
     * non-present workload has sustained headroom and cadence is healthy. */
    const bool has_headroom = workload_ms < frame_budget_ms * 0.65 &&
        (present_interval_ms <= 0.0 ||
         present_interval_ms <= frame_budget_ms * 1.10);

    adaptive_over_budget_frames_ = over_budget
        ? adaptive_over_budget_frames_ + 1 : 0;
    adaptive_under_budget_frames_ = has_headroom
        ? adaptive_under_budget_frames_ + 1 : 0;

    int scale_index = 0;
    double best_distance = std::numeric_limits<double>::max();
    for (int index = 0; index < 5; ++index) {
        const double distance = std::abs(adaptive_auto_scale_ - kScales[index]);
        if (distance < best_distance) {
            best_distance = distance;
            scale_index = index;
        }
    }

    int next_index = scale_index;
    if (adaptive_over_budget_frames_ >= 4 && scale_index < 4)
        next_index = scale_index + 1;
    else if (adaptive_under_budget_frames_ >= 90 && scale_index > 0)
        next_index = scale_index - 1;
    if (next_index == scale_index)
        return;

    const double previous_scale = adaptive_auto_scale_;
    adaptive_auto_scale_ = kScales[next_index];
    adaptive_over_budget_frames_ = 0;
    adaptive_under_budget_frames_ = 0;
    /* Quality changes are discrete and hysteretic. This invalidates targets
     * once per transition, never on every playback frame. */
    dirty_ = true;
    FXM_LOG_DEBUG("Performance", QStringLiteral(
        "stage=adaptive-quality-change title=%1 from=%2 to=%3 processingMs=%4 intervalMs=%5 budgetMs=%6 target=%7x%8")
        .arg(QString::fromStdString(title_->id))
        .arg(previous_scale, 0, 'f', 3)
        .arg(adaptive_auto_scale_, 0, 'f', 3)
        .arg(processing_ms, 0, 'f', 3)
        .arg(present_interval_ms, 0, 'f', 3)
        .arg(frame_budget_ms, 0, 'f', 3)
        .arg(title_->width).arg(title_->height));
    update();
}

CanvasPreview::DiagnosticsSample CanvasPreview::take_diagnostics_sample()
{
    DiagnosticsSample sample;
    if (!diagnostics_window_timer_.isValid()) {
        diagnostics_window_timer_.start();
        return sample;
    }

    sample.elapsed_ms = std::max<qint64>(1, diagnostics_window_timer_.restart());
    sample.rendered_frames = render_cost_sample_count_;
    sample.presented_frames = live_fps_frame_count_;
    sample.average_prepare_ms = render_cost_sample_count_ > 0
        ? double(render_cost_accumulator_ns_) /
              (1000000.0 * double(render_cost_sample_count_))
        : 0.0;
    sample.average_render_ms = pipeline_sample_count_ > 0
        ? double(gpu_render_cost_accumulator_ns_) /
              (1000000.0 * double(pipeline_sample_count_))
        : 0.0;
    sample.average_graphics_wait_ms = pipeline_sample_count_ > 0
        ? double(graphics_wait_accumulator_ns_) /
              (1000000.0 * double(pipeline_sample_count_))
        : 0.0;
    sample.average_preview_upload_ms = pipeline_sample_count_ > 0
        ? double(preview_upload_accumulator_ns_) /
              (1000000.0 * double(pipeline_sample_count_))
        : 0.0;
    sample.average_present_ms = pipeline_sample_count_ > 0
        ? double(present_wait_accumulator_ns_) /
              (1000000.0 * double(pipeline_sample_count_))
        : 0.0;
    sample.average_total_frame_ms = pipeline_sample_count_ > 0
        ? double(total_frame_cost_accumulator_ns_) /
              (1000000.0 * double(pipeline_sample_count_))
        : 0.0;
    sample.preview_scale = diagnostics_preview_scale_;
    sample.requested_width = diagnostics_requested_width_;
    sample.requested_height = diagnostics_requested_height_;
    sample.render_target_width = diagnostics_render_target_width_;
    sample.render_target_height = diagnostics_render_target_height_;
    sample.playback_fps = transport_playback_active_
        ? 1000.0 * double(live_fps_frame_count_) / double(sample.elapsed_ms)
        : 0.0;

    average_render_cost_ms_ = sample.average_render_ms;
    measured_live_fps_ = sample.playback_fps;
    render_cost_accumulator_ns_ = 0;
    render_cost_sample_count_ = 0;
    gpu_render_cost_accumulator_ns_ = 0;
    graphics_wait_accumulator_ns_ = 0;
    preview_upload_accumulator_ns_ = 0;
    present_wait_accumulator_ns_ = 0;
    total_frame_cost_accumulator_ns_ = 0;
    pipeline_sample_count_ = 0;
    live_fps_frame_count_ = 0;
    return sample;
}

double CanvasPreview::live_playback_fps() const
{
    return measured_live_fps_;
}

void CanvasPreview::refresh_runtime_dynamic_content()
{
    if (!title_)
        return;

    bool has_ticker = false;
    bool has_clock = false;
    const bool has_independent_asset =
        fxm::asset_runtime::title_has_independent_playback(*title_);
    for (const auto &layer : title_->layers) {
        if (!layer)
            continue;
        has_ticker = has_ticker || layer->type == LayerType::Ticker;
        has_clock = has_clock || layer->type == LayerType::Clock;
    }
    if (!has_ticker && !has_clock && !has_independent_asset)
        return;

    const qint64 clock_second = QDateTime::currentSecsSinceEpoch();
    if (!has_ticker && !has_independent_asset &&
        clock_second == last_runtime_clock_second_)
        return;
    last_runtime_clock_second_ = clock_second;
    dirty_ = true;
    update();
}

void CanvasPreview::set_selected_layer(const std::string &lid)
{
    set_selected_layers(lid.empty() ? std::vector<std::string>()
                                    : std::vector<std::string>{lid});
}

void CanvasPreview::set_selected_layers(const std::vector<std::string> &ids)
{
    std::vector<std::string> visible_ids;
    visible_ids.reserve(ids.size());
    for (const auto &id : ids) {
        const auto layer = title_ ? title_->find_layer(id) : nullptr;
        if (layer && layer_is_editor_visible(*layer))
            visible_ids.push_back(id);
    }
    const std::string next = visible_ids.empty() ? std::string()
                                                 : visible_ids.back();
    if (sel_layer_id_ == next && selected_layer_ids_ == visible_ids)
        return;

    selected_layer_ids_ = std::move(visible_ids);
    sel_layer_id_ = next;
    clear_path_point_selection();
    clear_corner_selection();
    selected_position_keyframe_index_ = -1;
    motion_path_hover_type_ = MotionPathHoverType::None;
    motion_path_hover_keyframe_index_ = -1;
    invalidate_canvas_overlay_caches();
    position_text_editor();
    emit gizmo_space_changed(static_cast<int>(gizmo_space()));
    update();
}

void CanvasPreview::set_safe_guides_visible(bool visible)
{
    safe_guides_visible_ = visible;
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorSafeGuidesVisibleKey), visible);
    settings.endGroup();
    settings.sync();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_overlay_graphics_visible(bool visible)
{
    if (overlay_graphics_visible_ == visible)
        return;
    overlay_graphics_visible_ = visible;
    invalidate_canvas_overlay_caches();
    update();
}


void CanvasPreview::load_ruler_guide_settings()
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    rulers_visible_ = settings.value(QString::fromUtf8(kEditorRulersVisibleKey), false).toBool();
    guides_visible_ = settings.value(QString::fromUtf8(kEditorGuidesVisibleKey), true).toBool();
    guides_locked_ = settings.value(QString::fromUtf8(kEditorGuidesLockedKey), false).toBool();
    show_guide_coordinates_ = settings.value(QString::fromUtf8(kEditorGuideCoordinatesVisibleKey), true).toBool();
    canvas_border_visible_ = settings.value(QString::fromUtf8(kEditorCanvasBorderVisibleKey), true).toBool();
    vertical_guides_ = guide_values_from_strings(settings.value(QString::fromUtf8(kEditorVerticalGuidesKey)).toStringList());
    horizontal_guides_ = guide_values_from_strings(settings.value(QString::fromUtf8(kEditorHorizontalGuidesKey)).toStringList());
    settings.endGroup();
}

void CanvasPreview::save_ruler_guide_settings() const
{
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QString::fromUtf8(kEditorLayoutSettingsGroup));
    settings.setValue(QString::fromUtf8(kEditorRulersVisibleKey), rulers_visible_);
    settings.setValue(QString::fromUtf8(kEditorGuidesVisibleKey), guides_visible_);
    settings.setValue(QString::fromUtf8(kEditorGuidesLockedKey), guides_locked_);
    settings.setValue(QString::fromUtf8(kEditorGuideCoordinatesVisibleKey), show_guide_coordinates_);
    settings.setValue(QString::fromUtf8(kEditorCanvasBorderVisibleKey), canvas_border_visible_);
    settings.setValue(QString::fromUtf8(kEditorVerticalGuidesKey), guide_values_to_strings(vertical_guides_));
    settings.setValue(QString::fromUtf8(kEditorHorizontalGuidesKey), guide_values_to_strings(horizontal_guides_));
    settings.endGroup();
    settings.sync();
}

void CanvasPreview::set_rulers_visible(bool visible)
{
    if (rulers_visible_ == visible) return;
    rulers_visible_ = visible;
    save_ruler_guide_settings();
    position_text_editor();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_guides_visible(bool visible)
{
    if (guides_visible_ == visible) return;
    guides_visible_ = visible;
    save_ruler_guide_settings();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_guides_locked(bool locked)
{
    if (guides_locked_ == locked) return;
    guides_locked_ = locked;
    save_ruler_guide_settings();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_show_guide_coordinates(bool visible)
{
    if (show_guide_coordinates_ == visible) return;
    show_guide_coordinates_ = visible;
    save_ruler_guide_settings();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::set_canvas_border_visible(bool visible)
{
    if (canvas_border_visible_ == visible) return;
    canvas_border_visible_ = visible;
    save_ruler_guide_settings();
    invalidate_canvas_overlay_caches();
    update();
}

void CanvasPreview::clear_user_guides()
{
    if (vertical_guides_.empty() && horizontal_guides_.empty()) return;
    vertical_guides_.clear();
    horizontal_guides_.clear();
    clear_snap_feedback();
    save_ruler_guide_settings();
    invalidate_canvas_overlay_caches();
    update();
}

bool CanvasPreview::prepare_cached_playback_frame(double time)
{
    prefetched_cache_frame_ = QImage();
    prefetched_cache_time_ = -1.0;
    if (!title_)
        return false;

    CacheManager &cache = CacheManager::instance();
    const TitleCacheability cacheability = cache.titleCacheability(title_);
    if (adaptive_interaction_active_ || !cache.cacheEnabled() ||
        cacheability == TitleCacheability::NonCacheable)
        return true;

    const int cue_row = title_->current_cue_row;
    const bool has_active_cue = cue_row >= 0 &&
        cue_row < static_cast<int>(title_->live_text_rows.size());

    /* Phase 14 prerenders normally live only in the GPU RAM tier. The old
     * readiness gate checked QImage RAM/disk exclusively, so “Play after
     * rendering” could wait forever while the exact frame was already ready
     * and directly presentable on the GPU. */
    const QString gpu_token = has_active_cue
        ? cache.requestLiveCueFrameGpuToken(title_, cue_row, time, true)
        : cache.requestFrameGpuToken(title_, time, true);
    if (!gpu_token.isEmpty())
        return true;

    QImage cached = has_active_cue
        ? cache.requestLiveCueFrame(title_, cue_row, time, true)
        : cache.requestFrame(title_, time, true);
    if (cached.isNull())
        return false;

    prefetched_cache_frame_ = cached;
    prefetched_cache_time_ = time;
    return true;
}

void CanvasPreview::refresh_preview()
{
    invalidate_motion_path_sample_cache();
    editor_projection_dirty_ = true;
    const bool resize_drag = drag_mode_ == DragMode::ResizeNW ||
        drag_mode_ == DragMode::ResizeN || drag_mode_ == DragMode::ResizeNE ||
        drag_mode_ == DragMode::ResizeE || drag_mode_ == DragMode::ResizeSE ||
        drag_mode_ == DragMode::ResizeS || drag_mode_ == DragMode::ResizeSW ||
        drag_mode_ == DragMode::ResizeW;
    const bool lightweight_transform_gesture = gizmo_drag_.active ||
        (drag_changed_ && (drag_mode_ == DragMode::Move ||
                           drag_mode_ == DragMode::Rotate || resize_drag));
    /* External model mutations (especially keyframe insertion/removal) and
     * post-release publication cancel transform-only classification. Active
     * move/rotate/resize gestures keep the lightweight path until release. */
    if (!lightweight_transform_gesture) {
        interactive_transform_pending_ = false;
        full_gpu_model_refresh_pending_ = true;
    }
    gpu_model_dirty_ = true;
    prefetched_cache_frame_ = QImage();
    prefetched_cache_time_ = -1.0;
    // Model edits invalidate only the editor-local reduced-quality cache. The
    // full-quality OBS/prerender cache retains its own independent lifecycle.
    editor_quality_cache_.clear();
    invalidate_canvas_overlay_caches();
    dirty_ = true;
    position_text_editor();
    /* Inline edits and canvas manipulation are presented by the display-rate
     * scheduler; never force a synchronous artwork render from an input event. */
    update();
    if (inline_text_editor_ && inline_text_editor_->isVisible()) {
        inline_text_editor_->viewport()->update();
        // Queue the affected region instead of forcing an immediate synchronous
        // paint, which can recursively stall mouse interaction and the OBS UI.
        update(inline_text_editor_view_rect_.adjusted(-4, -4, 4, 4));
    }
}

void CanvasPreview::clear_rendered_frame()
{
    frame_image_ = QImage();
    frame_image_canvas_offset_ = QPoint();
    frame_image_canvas_size_ = title_ ? QSize(title_->width, title_->height) : QSize();
    frame_image_preview_scale_ = 1.0;
    const std::shared_ptr<Title> render_title = projection_title();
    if (render_title && gpu_render_session_) {
        ++gpu_model_revision_;
        title_gpu_render_session_set_render_request(
            gpu_render_session_, TitleGpuRenderRequest{
                static_cast<uint32_t>(std::max(1, render_title->width)),
                static_cast<uint32_t>(std::max(1, render_title->height)),
                1.0 / std::max(1.0, render_title->frame_rate)});
        title_gpu_render_session_update(gpu_render_session_, *render_title, playhead_,
                                        gpu_model_revision_);
        gpu_model_dirty_ = false;
        full_gpu_model_refresh_pending_ = false;
    }
    invalidate_canvas_overlay_caches();
    dirty_ = false;
    playback_frame_pending_ = false;
    update();
}

QImage CanvasPreview::current_rendered_frame() const
{
    /* Explicit inspection/export path only. Normal editor presentation never
     * maps the GPU frame back to system memory.  Save/color-pick readbacks must
     * never persist the editor's reduced adaptive surface: that can leave a
     * group/shape snapshot scaled into the upper-left quarter after the stored
     * title is refreshed.  Temporarily read back at full quality, then restore
     * the editor draft quality for the next interactive frame. */
    if (!gpu_render_session_)
        return QImage();

    const bool restore_adaptive_draft =
        adaptive_rendering_enabled_ && frame_image_preview_scale_ < 0.999;
    const double restore_scale = frame_image_preview_scale_;
    if (restore_adaptive_draft)
        title_gpu_render_session_set_preview_quality(gpu_render_session_,
                                                     1.0, false);

    QImage image = title_gpu_render_session_readback(gpu_render_session_);

    if (restore_adaptive_draft) {
        title_gpu_render_session_set_preview_quality(gpu_render_session_,
                                                     restore_scale, true);
        const_cast<CanvasPreview *>(this)->dirty_ = true;
        const_cast<CanvasPreview *>(this)->update();
    }

    if (!image.isNull() && title_ &&
        image.size() != QSize(title_->width, title_->height)) {
        image = image.scaled(title_->width, title_->height,
                             Qt::IgnoreAspectRatio,
                             Qt::SmoothTransformation);
    }
    return image;
}


void CanvasPreview::set_adaptive_rendering_enabled(bool enabled)
{
    if (adaptive_rendering_enabled_ == enabled)
        return;
    if (!enabled && title_)
        fxm::ticker_runtime::set_adaptive_pause(title_->id, false);
    adaptive_rendering_enabled_ = enabled;
    adaptive_interaction_active_ = false;
    adaptive_auto_scale_ = 1.0;
    adaptive_over_budget_frames_ = 0;
    adaptive_under_budget_frames_ = 0;
    adaptive_present_interval_clock_.invalidate();
    editor_quality_cache_.clear();
    if (adaptive_full_quality_timer_)
        adaptive_full_quality_timer_->stop();
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QStringLiteral("titleEditor"));
    settings.setValue(QString::fromUtf8(kEditorAdaptiveRenderingKey), enabled);
    settings.endGroup();
    dirty_ = true;
    update();
}

void CanvasPreview::set_adaptive_quality_mode(AdaptiveQualityMode mode)
{
    if (adaptive_quality_mode_ == mode)
        return;
    if (title_)
        fxm::ticker_runtime::set_adaptive_pause(title_->id, false);
    adaptive_quality_mode_ = mode;
    adaptive_interaction_active_ = false;
    adaptive_auto_scale_ = 1.0;
    adaptive_over_budget_frames_ = 0;
    adaptive_under_budget_frames_ = 0;
    adaptive_present_interval_clock_.invalidate();
    force_live_full_quality_render_ = false;
    if (adaptive_full_quality_timer_)
        adaptive_full_quality_timer_->stop();
    QSettings settings(QStringLiteral("FluxMotion"), QStringLiteral("Dock"));
    settings.beginGroup(QStringLiteral("titleEditor"));
    settings.setValue(QString::fromUtf8(kEditorAdaptiveQualityKey), static_cast<int>(mode));
    settings.endGroup();
    // Changing quality invalidates the editor-local reduced-quality frames. It
    // does not invalidate or overwrite the full-quality OBS/prerender cache.
    editor_quality_cache_.clear();
    clear_rendered_frame();
    dirty_ = true;
    update();
}

QString CanvasPreview::adaptive_quality_label() const
{
    switch (adaptive_quality_mode_) {
    case AdaptiveQualityMode::Full: return QStringLiteral("Full");
    case AdaptiveQualityMode::Percent75: return QStringLiteral("75%");
    case AdaptiveQualityMode::Percent50: return QStringLiteral("50%");
    case AdaptiveQualityMode::Percent37_5: return QStringLiteral("37,5%");
    case AdaptiveQualityMode::Percent25: return QStringLiteral("25%");
    case AdaptiveQualityMode::Auto:
    default: return QStringLiteral("Auto");
    }
}
