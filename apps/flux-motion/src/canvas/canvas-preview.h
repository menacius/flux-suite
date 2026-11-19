#pragma once

#include "title-data.h"
#include "title-rich-text.h"

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QListWidget>
#include <QToolButton>
#include <QActionGroup>
#include <QButtonGroup>
#include <QMenu>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QTextEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSlider>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QPolygonF>
#include <QColor>
#include <QPixmap>
#include <QImage>
#include <QJsonArray>
#include <QTransform>
#include <QMatrix4x4>
#include <QVector3D>
#include <QHash>
#include <QElapsedTimer>
#include <memory>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <vector>
#include <set>
#include <limits>

namespace fxm { struct LiveCornerGeometry; }
struct TitleGpuRenderSession;

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QTimer;
class QResizeEvent;
class QPaintEvent;
class QPaintEngine;
class QDragEnterEvent;
class QDropEvent;
class QDragMoveEvent;
class QMimeData;
class QPainter;
class QScrollBar;
/* ══════════════════════════════════════════════════════════════════
 *  CanvasPreview  – renders the title at the current playhead
 * ══════════════════════════════════════════════════════════════════ */
class CanvasPreview : public QWidget {
    Q_OBJECT

public:
    explicit CanvasPreview(QWidget *parent = nullptr);
    ~CanvasPreview() override;
    void prepare_for_shutdown();

    enum class Editor3DView {
        ActiveCamera = 0, Front, Back, Left, Right, Top, Bottom, CustomPerspective
    };
    enum class GizmoMode { Move = 0, Rotate = 1, Scale = 2 };
    enum class GizmoAxis { None = 0, X, Y, Z, XY, XZ, YZ };

    struct ViewState {
        int zoom_percent = 100;
        bool fit_zoom_active = true;
        bool fit_zoom_up_to_100 = true;
        QPointF pan_offset;
        Editor3DView editor_3d_view = Editor3DView::ActiveCamera;
        QVector3D editor_3d_target;
        double editor_3d_distance = 1500.0;
        double editor_3d_yaw = -25.0;
        double editor_3d_pitch = -18.0;
        double editor_3d_ortho_zoom = 1.0;
        bool editor_3d_depth_debug_visible = false;
        bool editor_3d_normals_visible = false;
    };

    void set_title(std::shared_ptr<Title> t, bool preserve_view = false,
                   bool commit_active_text = true);
    ViewState view_state() const;
    void restore_view_state(const ViewState &state);
    void set_playhead(double t, bool playback_frame = false);
    void set_display_refresh_rate(double hz);
    void set_playback_active(bool active);
    void set_read_only(bool read_only);
    bool is_read_only() const { return read_only_; }
    void refresh_runtime_dynamic_content();
    void set_selected_layer(const std::string &lid);
    void set_selected_layers(const std::vector<std::string> &ids);
    void set_safe_guides_visible(bool visible);
    void set_overlay_graphics_visible(bool visible);
    bool overlay_graphics_visible() const { return overlay_graphics_visible_; }
    void set_rulers_visible(bool visible);
    void set_guides_visible(bool visible);
    void set_guides_locked(bool locked);
    void set_show_guide_coordinates(bool visible);
    void set_canvas_border_visible(bool visible);
    void clear_user_guides();
    bool rulers_visible() const { return rulers_visible_; }
    bool guides_visible() const { return guides_visible_; }
    bool guides_locked() const { return guides_locked_; }
    bool show_guide_coordinates() const { return show_guide_coordinates_; }
    bool canvas_border_visible() const { return canvas_border_visible_; }
    void set_snap_enabled(bool enabled);
    void set_snap_to_guides(bool enabled);
    void set_snap_to_grid(bool enabled);
    void set_snap_to_object_edges(bool enabled);
    void set_snap_to_object_centers(bool enabled);
    void set_snap_to_canvas_bounds(bool enabled);
    void set_snap_to_spacing(bool enabled);
    void refresh_preview();
    int last_render_cost_ms() const { return last_render_cost_ms_; }
    double average_render_cost_ms() const { return average_render_cost_ms_; }
    struct DiagnosticsSample {
        double average_prepare_ms = 0.0;
        double average_render_ms = 0.0;
        double average_graphics_wait_ms = 0.0;
        double average_preview_upload_ms = 0.0;
        double average_present_ms = 0.0;
        double average_total_frame_ms = 0.0;
        double playback_fps = 0.0;
        double preview_scale = 1.0;
        int requested_width = 0;
        int requested_height = 0;
        int render_target_width = 0;
        int render_target_height = 0;
        qint64 elapsed_ms = 0;
        int rendered_frames = 0;
        int presented_frames = 0;
    };
    DiagnosticsSample take_diagnostics_sample();
    double live_playback_fps() const;
    bool prepare_cached_playback_frame(double time);
    void clear_rendered_frame();
    QImage current_rendered_frame() const;
    void set_zoom_percent(int percent);
    int zoom_percent() const;
    void fit_canvas(bool up_to_100 = false);
    bool fit_zoom_active() const { return fit_zoom_active_; }
    void set_checkerboard_pattern(int pattern);
    int checkerboard_pattern() const { return checkerboard_pattern_; }
    enum class AdaptiveQualityMode { Auto = 0, Full, Percent75, Percent50, Percent37_5, Percent25 };
    void set_adaptive_rendering_enabled(bool enabled);
    bool adaptive_rendering_enabled() const { return adaptive_rendering_enabled_; }
    void set_adaptive_quality_mode(AdaptiveQualityMode mode);
    AdaptiveQualityMode adaptive_quality_mode() const { return adaptive_quality_mode_; }
    QString adaptive_quality_label() const;
    void set_editor_3d_view(Editor3DView view);
    Editor3DView editor_3d_view() const { return editor_3d_view_; }
    void set_3d_gizmo_mode(GizmoMode mode);
    GizmoMode gizmo_mode() const { return gizmo_mode_; }
    void set_3d_gizmo_space(TransformAxisSpace space);
    TransformAxisSpace gizmo_space() const;
    void frame_3d_selection();
    void frame_3d_all();
    void set_3d_depth_debug_visible(bool visible);
    bool depth_debug_visible() const { return editor_3d_depth_debug_visible_; }
    void set_3d_normals_visible(bool visible);
    bool normals_visible() const { return editor_3d_normals_visible_; }
    void set_selection_tool_active();
    void set_direct_selection_tool_active();
    void set_free_transform_tool_active(int mode);
    int free_transform_mode() const { return free_transform_mode_; }
    bool free_transform_controls_available() const;
    bool free_transform_keyframed() const;
    void toggle_free_transform_keyframe();
    void reset_free_transform();
    void set_shape_tool_active(ShapeType shape_type);
    void set_pen_tool_active();
    void set_text_tool_active(LayerType type);
    void set_image_tool_active();
    void set_color_picker_tool_active();
    void set_gradient_tool_active();
    void set_gradient_editor_active(bool active);
    void begin_text_edit_for_layer(const std::string &layer_id);
    void apply_active_text_char_format(const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask);
    QPointF view_center_canvas_point() const;
    void set_extension_canvas_handles(const QJsonArray &handles);

    /* Reuse the exact Canvas layer context menu for selections originating
     * from the Timeline or another synchronized editor view. */
    void show_selected_layers_context_menu(const QPoint &global_pos);

    bool corner_controls_available() const;
    double corner_control_radius(bool *mixed = nullptr) const;
    bool corner_control_sync(bool *mixed = nullptr) const;
    double corner_control_bevel_roundness(bool *mixed = nullptr) const;
    void set_corner_control_radius(double radius);
    void set_corner_control_sync(bool enabled);
    void set_corner_control_bevel_roundness(double roundness);

    bool point_controls_available() const;
    QPointF point_control_position(bool *x_mixed = nullptr, bool *y_mixed = nullptr) const;
    bool point_control_smooth(bool *mixed = nullptr) const;
    bool point_control_show_handles() const { return show_selected_path_handles_; }
    void set_point_control_x(double x);
    void set_point_control_y(double y);
    void convert_selected_points_to_corner();
    void convert_selected_points_to_smooth();
    void set_point_control_show_handles(bool show);

signals:
    void layer_clicked(const std::string &layer_id);
    void layers_selected(const std::vector<std::string> &layer_ids);
    void layer_geometry_changed();
    void layer_structure_changed();
    void zoom_percent_changed(int percent);
    void shape_drawing_started(ShapeType shape_type, const QPointF &canvas_pt);
    void text_drawing_started(LayerType type, const QPointF &canvas_pt);
    void image_drawing_started(const QPointF &canvas_pt);
    void shape_drawing_changed(const QRectF &canvas_rect);
    void shape_drawing_finished(bool keep_layer);
    void pen_path_finished(const std::vector<BezierPathPoint> &canvas_points, bool closed);
    void text_edit_changed(const std::string &layer_id);
    void text_edit_cursor_changed(const std::string &layer_id);
    void text_edit_committed(const std::string &layer_id);
    /* Canonical formatting shortcuts (Ctrl+B/I/U) do not belong to the
     * QTextDocument undo stack. Bracket them explicitly in title history. */
    void text_property_change_started(const std::string &layer_id);
    void text_property_change_committed(const std::string &layer_id);
    /* Inline QTextEdit keeps its native undo stack for actual typing.  When
     * that stack is empty (for example after a canonical Properties-panel
     * format edit rebuilt the adapter), route history to the title-wide
     * authoritative snapshot stack instead. */
    void title_undo_requested();
    void title_redo_requested();
    void color_picker_previewed(const QColor &color);
    void color_picked(const QColor &color, bool foreground);
    void external_image_layer_requested(const QString &image_path, const QPointF &canvas_pt);
    void external_file_import_requested(const QString &path, const QPointF &canvas_pt);
    void external_text_layer_requested(const QString &text, const QPointF &canvas_pt);
    void asset_layer_requested(const QString &asset_id, const QPointF &canvas_pt);
    void edit_asset_requested(const QString &asset_id);
    void effect_preset_dropped(const QString &file_path, const std::string &layer_id);
    void corner_context_changed();
    void extension_canvas_handle_moved(const QString &path, const QPointF &normalized_position, bool final_change);
    void duplicate_layers_requested();
    void copy_layers_requested();
    void cut_layers_requested();
    void paste_layers_requested();
    void delete_layers_requested();
    void group_layers_requested();
    void ungroup_layers_requested();
    void add_layers_to_group_requested(const std::string &group_id);
    void remove_layers_from_group_requested();
    void flip_layers_requested(bool horizontal);
    void rotate_layers_requested(double degrees);
    void fit_fill_layers_requested(bool fill_screen);
    void layer_order_requested(int action);
    void set_layers_locked_requested(bool locked);
    void set_layers_visible_requested(bool visible);
    void editor_3d_view_changed(int view);
    void gizmo_mode_changed(int mode);
    void gizmo_space_changed(int space);

protected:
    bool event(QEvent *ev) override;
    QPaintEngine *paintEngine() const override;
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void keyReleaseEvent(QKeyEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dragMoveEvent(QDragMoveEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;
    void leaveEvent(QEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *ev) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private:
    bool read_only_ = false;
    enum class CanvasPaintPass { All, Underlay, Overlay };
    struct GpuDisplayState;
    void paint_canvas(QPainter &p, CanvasPaintPass pass);
    bool ensure_gpu_display();
    void destroy_gpu_display();
    void update_gpu_display_textures(const QImage *underlay, const QImage *overlay,
                                     const QImage *selection_mask);
    static void gpu_display_draw(void *data, uint32_t cx, uint32_t cy);

    enum class DragMode {
        None,
        Marquee,
        Move,
        ResizeNW,
        ResizeN,
        ResizeNE,
        ResizeE,
        ResizeSE,
        ResizeS,
        ResizeSW,
        ResizeW,
        Origin,
        Rotate,
        GradientStart,
        GradientEnd,
        GradientCenter,
        GradientRadius,
        GradientFocal,
        CornerRadiusTL,
        CornerRadiusTR,
        CornerRadiusBR,
        CornerRadiusBL,
        CornerRadius,
        PathAnchor,
        PathInHandle,
        PathOutHandle,
        PositionVertex,
        PositionInTangent,
        PositionOutTangent,
        PathMarquee,
        GuideX,
        GuideY,
        ExtensionPoint,
        TransformCornerTL,
        TransformCornerTR,
        TransformCornerBR,
        TransformCornerBL
    };
    enum class CanvasTool { Selection, DirectSelection, FreeTransform, Shape, Pen, Text, Image, ColorPicker, Gradient };

    struct PathHit {
        DragMode mode = DragMode::None;
        int point_index = -1;
    };

    struct GradientHandleTarget {
        enum class Kind { LayerFill, TextFillRanges };
        Kind kind = Kind::LayerFill;
        RichTextFill fill;
        std::vector<std::pair<size_t, size_t>> text_ranges;
        bool selection_target = false;
        size_t display_index = 0;
    };
    struct GradientHandleGeometry {
        bool valid = false;
        bool radial = false;
        QRectF local_rect;
        QPointF center;
        QPointF start;
        QPointF end;
        QPointF radius;
        QPointF focal;
        GradientHandleTarget target;
    };
    struct SelectionOverlayLayerGeometry {
        const Layer *layer = nullptr;
        bool editing_text_layer = false;
        QPolygonF outline;
        QPointF handles[8];
        bool handle_visible[8] = {false, false, false, false,
                                  false, false, false, false};
        QPointF origin;
        bool origin_visible = false;
    };
    struct SelectionOverlayGeometry {
        bool valid = false;
        std::vector<SelectionOverlayLayerGeometry> group_contexts;
        std::vector<SelectionOverlayLayerGeometry> layers;
        QRectF multi_bounds_view;
        QPointF multi_handles[8];
    };
    struct HoverOverlayGeometry {
        const Layer *layer = nullptr;
        bool hovered_is_selected = false;
        QPolygonF outline;
    };

    void finish_pen_path(bool closed, bool keep = true);
    void cancel_pen_path();
    void draw_pen_path_preview(QPainter &p);
    void update_pen_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void undo_pen_path_point();
    void redo_pen_path_point();
    QPointF constrain_path_direction(const QPointF &anchor, const QPointF &target,
                                     Qt::KeyboardModifiers modifiers) const;
    bool direct_selection_supported(const Layer &layer) const;
    bool ensure_direct_selection_path(const std::shared_ptr<Layer> &layer);
    PathHit hit_test_direct_path(const Layer &layer, const QPointF &view_pt) const;
    void draw_direct_selection_path(QPainter &p, const Layer &layer);
    bool begin_direct_selection(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    bool select_direct_object_at(const QPointF &view_pt);
    void begin_path_point_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void update_path_point_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void finish_path_point_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void apply_direct_path_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void delete_selected_path_points();
    bool nudge_selected_path_points(double dx, double dy);
    void reframe_custom_path(Layer &layer, const std::vector<QPointF> &anchors_local,
                             const std::vector<QPointF> &in_handles_local,
                             const std::vector<QPointF> &out_handles_local,
                             const std::vector<bool> &has_in,
                             const std::vector<bool> &has_out);
    QPointF path_normalized_to_canvas(const Layer &layer, double x, double y) const;
    QPointF path_canvas_to_normalized(const Layer &layer, const QPointF &canvas_pt) const;
    void clear_path_point_selection();

    int position_keyframe_at_playhead(const Layer &layer) const;
    bool position_motion_path_visible(const Layer &layer) const;
    int active_position_keyframe_index(const Layer &layer) const;
    QPointF position_keyframe_canvas_point(const Layer &layer, int keyframe_index,
                                           const Vec3Value &offset) const;
    QPointF position_segment_canvas_point(const Layer &layer, size_t segment_index,
                                          double temporal_progress) const;
    const std::vector<std::vector<QPointF>> &motion_path_canvas_samples(
        const Layer &layer, int samples_per_segment) const;
    void invalidate_motion_path_sample_cache() const;
    int hit_test_position_keyframe_vertices(const Layer &layer,
                                            const QPointF &view_pt) const;
    DragMode hit_test_position_tangent_handles(const Layer &layer,
                                                const QPointF &view_pt,
                                                int *keyframe_index = nullptr) const;
    bool hit_test_position_motion_path(const Layer &layer,
                                       const QPointF &view_pt,
                                       size_t *segment_index = nullptr,
                                       double *temporal_progress = nullptr,
                                       double hit_radius = 6.0) const;
    void draw_position_motion_path(QPainter &p, const Layer &layer);
    bool begin_position_tangent_drag(const QPointF &view_pt,
                                     Qt::KeyboardModifiers modifiers);
    bool begin_position_vertex_drag(const QPointF &view_pt);
    bool apply_position_vertex_drag(const QPointF &view_pt,
                                    Qt::KeyboardModifiers modifiers);
    bool apply_position_tangent_drag(const QPointF &view_pt,
                                     Qt::KeyboardModifiers modifiers);
    QPointF snap_position_keyframe_canvas_point(const Layer &layer,
                                                int keyframe_index,
                                                const QPointF &canvas_point,
                                                bool allow_snap);
    bool add_position_keyframe_at_path(const QPointF &view_pt);
    bool show_position_motion_path_context_menu(QContextMenuEvent *ev);
    bool update_position_motion_path_hover(const QPointF &view_pt);

    std::shared_ptr<Title> projection_title() const;
    void invalidate_editor_projection(bool transform_only = true);
    TitleCamera build_editor_view_camera() const;
    QString editor_3d_view_name() const;
    void reset_editor_3d_navigation();
    bool begin_editor_3d_navigation(QMouseEvent *ev);
    bool update_editor_3d_navigation(QMouseEvent *ev);
    bool finish_editor_3d_navigation(QMouseEvent *ev);
    void dolly_editor_3d_view(double delta);
    void draw_editor_3d_view_overlay(QPainter &p);
    bool light_layer_overlay_contains(const Layer &layer, const QPointF &view_point) const;
    void draw_light_layer_overlays(QPainter &p);
    bool empty_layer_overlay_points(const Layer &layer,
                                    std::array<QPointF, 4> &view_points,
                                    bool &is_3d) const;
    bool empty_layer_overlay_contains(const Layer &layer,
                                      const QPointF &view_point) const;
    void draw_empty_layer_overlays(QPainter &p);
    void draw_3d_material_debug_overlay(QPainter &p);
    bool collect_editor_3d_bounds(
        const std::vector<std::shared_ptr<Layer>> &layers,
        QVector3D &minimum, QVector3D &maximum) const;

    struct GizmoGeometry;
    GizmoGeometry build_3d_gizmo_geometry() const;
    GizmoAxis hit_test_3d_gizmo(const QPointF &view_point) const;
    void update_3d_gizmo_hover(const QPointF &view_point);
    bool begin_3d_gizmo_drag(QMouseEvent *ev);
    bool update_3d_gizmo_drag(QMouseEvent *ev);
    bool finish_3d_gizmo_drag(QMouseEvent *ev);
    void draw_3d_gizmo(QPainter &p);

    void render_to_frame();
    void render_dirty_frame_if_due();
    bool present_gpu_display_if_due();
    void reset_live_playback_fps_measurement();
    void record_live_playback_present();
    void record_frame_pipeline_sample(qint64 graphics_wait_ns,
                                      qint64 gpu_render_ns,
                                      qint64 preview_upload_ns,
                                      qint64 present_ns,
                                      qint64 presentation_total_ns,
                                      int requested_width,
                                      int requested_height,
                                      int render_target_width,
                                      int render_target_height,
                                      double preview_scale);
    void update_adaptive_playback_quality(double processing_ms,
                                          double workload_ms,
                                          double present_interval_ms);
    void begin_adaptive_interaction();
    void end_adaptive_interaction();
    double adaptive_preview_scale() const;
    bool layer_is_editor_visible(const Layer &layer) const;
    std::shared_ptr<Layer> selected_layer() const;
    std::vector<std::shared_ptr<Layer>> selected_layers() const;
    QRectF layer_local_rect(const Layer &layer) const;
    double fit_scale() const;
    double view_scale() const;
    QPointF centered_view_origin() const;
    QPointF view_origin() const;
    QPointF view_to_canvas(const QPointF &view_pt) const;
    QPointF canvas_to_view(const QPointF &canvas_pt) const;
    QPointF canvas_to_layer(const Layer &layer, const QPointF &canvas_pt) const;
    QPointF layer_to_canvas(const Layer &layer, const QPointF &layer_pt) const;
    QPointF transformed_layer_point(const Layer &layer, const QPointF &layer_pt) const;
    DragMode hit_test_selected(const QPointF &view_pt) const;
    bool active_draw_tool_can_manipulate_selected() const;
    void set_cursor_for_drag_mode(DragMode mode, bool dragging);
    bool gradient_handles_visible() const;
    bool selected_text_fill(const Layer &layer, RichTextFill &fill) const;
    bool apply_selected_text_gradient_fill(Layer &layer, const RichTextFill &fill);
    std::vector<GradientHandleTarget> gradient_handle_targets(const Layer &layer) const;
    bool layer_supports_gradient_handles(const Layer &layer) const;
    GradientHandleGeometry gradient_handle_geometry(const Layer &layer,
                                                     const GradientHandleTarget &target) const;
    DragMode hit_test_gradient_handles(const Layer &layer, const QPointF &view_pt,
                                       GradientHandleTarget *target = nullptr) const;
    void draw_gradient_handles(QPainter &p, const Layer &layer);
    void begin_gradient_drag(const Layer &layer, const GradientHandleTarget &target);
    bool apply_gradient_target_fill(Layer &layer, const GradientHandleTarget &target,
                                    const RichTextFill &fill);
    bool apply_gradient_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    bool begin_gradient_tool_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    bool layer_supports_corner_radius_handles(const Layer &layer) const;
    bool corner_radius_handle_view_geometry(const Layer &layer,
                                            const fxm::LiveCornerGeometry &corner,
                                            QPointF &anchor_view,
                                            QPointF &handle_view) const;
    QPointF corner_radius_handle_view_pos(const Layer &layer,
                                          const fxm::LiveCornerGeometry &corner) const;
    int hit_test_corner_radius_handle_index(const Layer &layer, const QPointF &view_pt) const;
    std::shared_ptr<Layer> hit_test_selected_corner_layer(const QPointF &view_pt, int &point_index) const;
    DragMode hit_test_corner_radius_handles(const Layer &layer, const QPointF &view_pt) const;
    double corner_radius_value(const Layer &layer, int point_index) const;
    void set_corner_radius_value(Layer &layer, int point_index, double radius, bool affect_group);
    void clear_corner_selection(bool notify = true);
    std::vector<int> corner_indices_for_layer(const Layer &layer, bool selected_only) const;
    std::vector<int> mapped_corner_indices(const Layer &source, const Layer &target,
                                           const std::vector<int> &source_indices) const;
    void notify_corner_context_changed();
    void draw_corner_radius_handles(QPainter &p, const Layer &layer);
    void begin_corner_radius_drag(const Layer &layer);
    bool apply_corner_radius_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    QRectF layer_canvas_bounds(const Layer &layer) const;
    QRectF selected_canvas_bounds() const;
    void begin_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    void update_marquee(const QPointF &view_pt, Qt::KeyboardModifiers modifiers);
    bool duplicate_selected_layers_for_drag();
    bool nudge_selected_layers(double dx, double dy);
    QPointF snap_delta_for_bounds(const QRectF &start_bounds, const QPointF &delta, bool snap_x, bool snap_y,
                                  bool allow_snap = true);
    QPointF snap_canvas_point(const QPointF &canvas_pt, bool snap_x, bool snap_y, bool allow_snap = true);
    void collect_snap_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const;
    QRectF canvas_view_rect() const;
    QRectF ruler_top_rect() const;
    QRectF ruler_left_rect() const;
    QRectF ruler_corner_rect() const;
    bool ruler_hit_test(const QPointF &view_pt, bool &vertical_guide) const;
    int guide_hit_test(const QPointF &view_pt, bool &x_axis, bool include_locked = false) const;
    double snap_guide_value_to_objects(bool x_axis, double raw_value);
    void draw_rulers(QPainter &p, const QRectF &canvas_rect, double scale, const QPointF &origin);
    void draw_user_guides(QPainter &p, const QRectF &canvas_rect);
    void draw_guide_coordinate(QPainter &p, const QPointF &view_pt, bool x_axis, double value) const;
    void save_ruler_guide_settings() const;
    void load_ruler_guide_settings();
    void collect_spacing_targets(bool x_axis, std::vector<double> &targets, std::vector<QString> &labels) const;
    void clear_snap_feedback();
    void add_snap_feedback(bool x_axis, double value, const QString &label);
    void apply_drag(const QPointF &view_pt, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    QRectF toolbar_draw_rect(const QPointF &canvas_pt, Qt::KeyboardModifiers modifiers) const;
    QRectF snapped_toolbar_draw_rect(const QRectF &raw_rect, bool allow_snap = true);
    double toolbar_draw_aspect_ratio() const;
    QRect toolbar_preview_update_rect() const;
    void draw_toolbar_preview(QPainter &p);
    void draw_empty_image_placeholders(QPainter &p);
    void draw_shader_compile_overlay(QPainter &p, const QRectF &canvas_rect);
    bool sample_color_at_view(const QPointF &view_pt, QColor &color);
    void update_color_picker_tooltip(const QPointF &view_pt);
    void draw_color_picker_tooltip(QPainter &p);
    bool mime_has_external_canvas_content(const QMimeData *mime) const;
    bool handle_external_canvas_mime(const QMimeData *mime, const QPointF &canvas_pt);
    QString canvas_drag_tooltip_text() const;
    void draw_canvas_drag_tooltip(QPainter &p);
    QRect snap_cursor_update_rect() const;
    QRect final_snap_cursor_update_rect() const;
    QRect canvas_drag_tooltip_update_rect() const;
    void clear_draw_tool_snap_cursor();
    void update_draw_tool_snap_cursor(const QPointF &view_pt, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void draw_snap_cursor_indicator(QPainter &p);
    void update_shape_drawing(const QPointF &view_pt, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    void begin_text_edit(const std::shared_ptr<Layer> &layer);
    void commit_text_edit(bool accept_changes = true,
                          bool emit_commit_signal = true);
    void position_text_editor();
    void configure_inline_text_editor(const Layer &layer);
    bool inline_text_view_point_hits_document(const QPointF &view_point) const;
    bool set_inline_text_cursor_from_view(const QPointF &view_point,
                                          bool keep_anchor,
                                          bool select_word = false);
    bool sync_inline_text_layer(bool mark_dirty);
    void schedule_inline_text_refresh(bool mark_dirty, bool emit_changed);
    void refresh_inline_text_edit(bool mark_dirty, bool emit_changed);
    void suspend_inline_text_edit_for_gradient();
    void resume_inline_text_edit_after_gradient();
    double inline_text_visual_scale(const Layer &layer) const;
    QRectF inline_text_document_local_rect(const Layer &layer) const;
    std::vector<QPolygonF> inline_text_selection_view_polygons() const;
    bool inline_text_caret_view_polygon(QPolygonF &polygon) const;
    std::shared_ptr<Layer> layer_at_view_pos(const QPointF &view_pt) const;
    std::shared_ptr<Layer> text_layer_at_view_pos(const QPointF &view_pt) const;
    void update_hover_layer(const QPointF &view_pt);
    void draw_hover_layer_box(QPainter &p);
    void draw_canvas_border(QPainter &p, const QRectF &canvas_rect);
    void draw_ruler_mouse_indicators(QPainter &p, const QRectF &canvas_rect);
    void invalidate_checkerboard_cache();
    void draw_static_checkerboard(QPainter &p, const QRect &canvas_rect_px);
    void invalidate_selection_overlay_cache() const;
    void invalidate_hover_overlay_cache() const;
    void invalidate_canvas_overlay_caches() const;
    bool layer_overlay_changes_with_playhead(const Layer &layer) const;
    bool overlay_visibility_crosses_playhead_boundary(const Layer &layer, double old_playhead, double new_playhead) const;
    bool canvas_overlay_changes_with_playhead(double old_playhead, double new_playhead) const;
    const SelectionOverlayGeometry &selection_overlay_geometry() const;
    const HoverOverlayGeometry &hover_overlay_geometry() const;

    std::shared_ptr<Title> title_;
    mutable std::shared_ptr<Title> editor_projection_title_;
    mutable bool editor_projection_dirty_ = true;
    bool editor_camera_transform_only_pending_ = false;
    Editor3DView editor_3d_view_ = Editor3DView::ActiveCamera;
    GizmoMode gizmo_mode_ = GizmoMode::Move;
    QVector3D editor_3d_target_;
    double editor_3d_distance_ = 1500.0;
    double editor_3d_yaw_ = -25.0;
    double editor_3d_pitch_ = -18.0;
    double editor_3d_ortho_zoom_ = 1.0;
    bool editor_3d_depth_debug_visible_ = false;
    bool editor_3d_normals_visible_ = false;
    enum class Editor3DNavigationMode { None, Orbit, Pan, Dolly };
    Editor3DNavigationMode editor_3d_navigation_mode_ = Editor3DNavigationMode::None;
    QPointF editor_3d_navigation_start_view_;
    QVector3D editor_3d_navigation_start_target_;
    double editor_3d_navigation_start_distance_ = 1500.0;
    double editor_3d_navigation_start_yaw_ = -25.0;
    double editor_3d_navigation_start_pitch_ = -18.0;
    double editor_3d_navigation_start_ortho_zoom_ = 1.0;
    bool shutting_down_ = false;
    std::string sel_layer_id_;
    std::vector<std::string> selected_layer_ids_;
    std::vector<std::string> context_menu_selection_override_;
    double playhead_ = 0.0;
    int zoom_percent_ = 100;
    bool fit_zoom_active_ = true;
    bool fit_zoom_up_to_100_ = true;
    QPointF pan_offset_;
    bool panning_ = false;
    QPointF pan_start_view_;
    QPointF pan_start_offset_;
    QImage frame_image_;
    std::unique_ptr<GpuDisplayState> gpu_display_;
    TitleGpuRenderSession *gpu_render_session_ = nullptr;
    uint64_t gpu_model_revision_ = 0;
    bool gpu_model_dirty_ = true;
    /* A model edit (including keyframe insertion/removal) must receive one
     * authoritative snapshot update before transform-only updates may resume. */
    bool full_gpu_model_refresh_pending_ = true;
    std::atomic_bool gpu_recovery_queued_{false};
    QString gpu_underlay_key_;
    QString gpu_shader_compile_overlay_key_;
    mutable bool gpu_overlay_dirty_ = true;
    QImage gpu_overlay_cache_;
    QPoint frame_image_canvas_offset_;
    QSize frame_image_canvas_size_;
    bool dirty_ = true;
    QTimer *render_coalesce_timer_ = nullptr;
    QTimer *present_coalesce_timer_ = nullptr;
    QTimer *adaptive_full_quality_timer_ = nullptr;
    QElapsedTimer last_render_clock_;
    QElapsedTimer last_present_clock_;
    int render_interval_ms_ = 16;
    int display_refresh_interval_ms_ = 16;
    bool playback_frame_pending_ = false;
    bool playback_present_pending_ = false;
    bool editing_present_pending_ = false;
    bool transport_playback_active_ = false;
    bool force_present_pending_ = true;
    QImage prefetched_cache_frame_;
    double prefetched_cache_time_ = -1.0;
    qint64 last_runtime_clock_second_ = std::numeric_limits<qint64>::min();
    bool render_in_progress_ = false;
    bool adaptive_rendering_enabled_ = false;
    AdaptiveQualityMode adaptive_quality_mode_ = AdaptiveQualityMode::Auto;
    bool adaptive_interaction_active_ = false;
    /* Transient state for the currently active 3D pointer interaction. It is
     * never allowed to survive a model/keyframe refresh boundary. */
    bool interactive_transform_pending_ = false;
    /* Gives the first exact post-release snapshot monitor-cadence priority,
     * without incorrectly treating that snapshot as transform-only. */
    bool interactive_settle_pending_ = false;
    bool force_live_full_quality_render_ = false;
    double frame_image_preview_scale_ = 1.0;
    int last_full_quality_render_cost_ms_ = 0;
    int last_adaptive_render_cost_ms_ = 0;
    int last_render_cost_ms_ = 0;
    qint64 last_prepare_cost_ns_ = 0;
    qint64 render_cost_accumulator_ns_ = 0;
    int render_cost_sample_count_ = 0;
    double average_render_cost_ms_ = 0.0;
    qint64 gpu_render_cost_accumulator_ns_ = 0;
    qint64 graphics_wait_accumulator_ns_ = 0;
    qint64 preview_upload_accumulator_ns_ = 0;
    qint64 present_wait_accumulator_ns_ = 0;
    qint64 total_frame_cost_accumulator_ns_ = 0;
    qint64 pending_preview_upload_ns_ = 0;
    int pipeline_sample_count_ = 0;
    int diagnostics_requested_width_ = 0;
    int diagnostics_requested_height_ = 0;
    int diagnostics_render_target_width_ = 0;
    int diagnostics_render_target_height_ = 0;
    double diagnostics_preview_scale_ = 1.0;
    double adaptive_auto_scale_ = 1.0;
    int adaptive_over_budget_frames_ = 0;
    int adaptive_under_budget_frames_ = 0;
    QElapsedTimer adaptive_present_interval_clock_;
    QElapsedTimer diagnostics_window_timer_;
    /* Correlated transport/canvas diagnostics. These fields are observability
     * only and never affect scheduling or render decisions. */
    uint64_t diagnostics_playhead_request_serial_ = 0;
    uint64_t diagnostics_render_request_serial_ = 0;
    double diagnostics_last_prepared_playhead_ =
        std::numeric_limits<double>::quiet_NaN();
    int diagnostics_repeated_prepared_playhead_count_ = 0;
    QElapsedTimer diagnostics_repeated_playhead_clock_;
    int live_fps_frame_count_ = 0;
    double measured_live_fps_ = 0.0;
    QHash<QString, QImage> editor_quality_cache_;
    bool safe_guides_visible_ = false;
    bool overlay_graphics_visible_ = true;
    bool rulers_visible_ = false;
    bool guides_visible_ = true;
    bool guides_locked_ = false;
    bool show_guide_coordinates_ = true;
    bool canvas_border_visible_ = true;
    bool mouse_inside_canvas_ = false;
    QPointF last_mouse_view_pos_;
    std::string hovered_layer_id_;
    std::vector<double> vertical_guides_;
    std::vector<double> horizontal_guides_;
    bool dragging_new_guide_ = false;
    bool dragging_guide_x_axis_ = true;
    int dragging_guide_index_ = -1;
    double dragging_guide_value_ = 0.0;
    int checkerboard_pattern_ = 1;
    QPixmap checkerboard_tile_;
    int checkerboard_tile_pattern_ = -1;
    CanvasTool active_tool_ = CanvasTool::Selection;
    ShapeType active_shape_type_ = ShapeType::Rectangle;
    int free_transform_mode_ = 0; // 0=free transform, 1=perspective, 2=free distort
    LayerType active_text_layer_type_ = LayerType::Text;
    std::vector<BezierPathPoint> pen_path_points_; /* canvas-space while drawing */
    std::vector<BezierPathPoint> pen_redo_points_; /* local history for an unfinished path */
    bool pen_path_active_ = false;
    bool pen_close_pending_ = false;
    int pen_drag_point_index_ = -1;
    QPointF pen_press_canvas_;
    QPointF pen_last_drag_canvas_;
    bool pen_alt_break_active_ = false;
    QPointF pen_alt_fixed_in_canvas_;
    bool pen_ctrl_unequal_active_ = false;
    double pen_ctrl_fixed_in_length_ = 0.0;
    bool pen_space_reposition_ = false;
    std::set<int> selected_path_point_indices_;
    std::set<int> path_marquee_base_selection_;
    std::string path_marquee_layer_id_;
    bool path_marquee_active_ = false;
    bool show_selected_path_handles_ = true;
    int path_drag_point_index_ = -1;
    bool path_alt_break_active_ = false;
    std::vector<BezierPathPoint> path_drag_start_points_;
    struct SpatialTangentDragState {
        bool active = false;
        std::string layer_id;
        int keyframe_index = -1;
        bool incoming = false;
        bool linked = true;
        bool moved = false;
        bool path_3d = false;
        QVector3D plane_point;
        QVector3D plane_normal;
    };
    SpatialTangentDragState spatial_tangent_drag_;
    struct SpatialVertexDragState {
        bool active = false;
        std::string layer_id;
        int keyframe_index = -1;
        double keyframe_time = 0.0;
        Vec3Value start_value;
        bool moved = false;
        bool path_3d = false;
        QVector3D plane_point;
        QVector3D plane_normal;
    };
    SpatialVertexDragState spatial_vertex_drag_;
    enum class MotionPathHoverType {
        None,
        Path,
        Vertex,
        InTangent,
        OutTangent,
    };
    struct MotionPathSampleCache {
        std::string title_id;
        std::string layer_id;
        size_t keyframe_count = 0;
        int samples_per_segment = 0;
        std::vector<std::vector<QPointF>> canvas_segments;
        bool valid = false;
    };
    mutable MotionPathSampleCache motion_path_sample_cache_;
    int selected_position_keyframe_index_ = -1;
    MotionPathHoverType motion_path_hover_type_ = MotionPathHoverType::None;
    int motion_path_hover_keyframe_index_ = -1;
    bool drawing_shape_ = false;
    bool drawing_shape_changed_ = false;
    QTextEdit *inline_text_editor_ = nullptr;
    QRect inline_text_editor_view_rect_;
    bool inline_text_mouse_selecting_ = false;
    std::string inline_text_layer_id_;
    double inline_text_last_visual_scale_ = 0.0;
    bool committing_inline_text_ = false;
    bool updating_inline_text_editor_ = false;
    bool refreshing_inline_text_ = false;
    bool inline_text_suspended_for_gradient_ = false;
    QTimer *inline_text_refresh_timer_ = nullptr;
    bool inline_text_refresh_mark_dirty_ = false;
    bool inline_text_refresh_emit_changed_ = false;
    int inline_text_change_position_ = 0;
    int inline_text_change_removed_ = 0;
    int inline_text_change_added_ = 0;
    int inline_text_change_count_ = 0;
    QPointF shape_draw_start_canvas_;
    QPointF shape_draw_current_canvas_;
    QRectF shape_draw_current_rect_;
    Qt::KeyboardModifiers shape_draw_modifiers_ = Qt::NoModifier;
    bool snap_cursor_visible_ = false;
    QPointF snap_cursor_canvas_;
    bool final_snap_cursor_visible_ = false;
    QPointF final_snap_cursor_canvas_;
    QRect last_toolbar_preview_update_rect_;
    bool color_picker_tooltip_visible_ = false;
    bool gradient_tool_dragging_ = false;
    QPointF gradient_tool_start_local_;
    QPointF color_picker_tooltip_pos_;
    QColor color_picker_tooltip_color_;
    QImage color_picker_source_image_;

    struct SnapSettings {
        bool enabled = true;
        bool guides = true;
        bool grid = false;
        bool object_edges = true;
        bool object_centers = true;
        bool canvas_bounds = true;
        bool spacing = true;
    };
    struct SnapFeedback {
        bool x_axis = true;
        double value = 0.0;
        QString label;
    };
    SnapSettings snap_settings_;
    std::vector<SnapFeedback> snap_feedback_;

    QJsonArray extension_canvas_handles_;
    int active_extension_handle_ = -1;
    DragMode drag_mode_ = DragMode::None;
    bool drag_changed_ = false;
    bool alt_duplicate_pending_ = false;
    bool alt_duplicate_done_ = false;
    bool alt_duplicate_active_ = false;
    bool drag_text_object_scaling_ = false;
    bool marquee_active_ = false;
    QPointF drag_start_view_;
    QPointF drag_current_view_;
    std::vector<std::string> marquee_base_selection_;
    QPointF drag_start_canvas_;
    QPointF drag_rotation_pivot_canvas_;
    double drag_start_rotation_angle_ = 0.0;
    double drag_current_rotation_delta_ = 0.0;
    double drag_start_x_ = 0.0;
    double drag_start_y_ = 0.0;
    float drag_start_w_ = 1.0f;
    float drag_start_h_ = 1.0f;
    float drag_start_origin_x_ = 0.5f;
    float drag_start_origin_y_ = 0.5f;
    QRectF drag_start_selection_bounds_;
    struct GizmoGeometry {
        bool valid = false;
        QVector3D origin_world;
        QPointF origin_view;
        QVector3D axes_world[3];
        QPointF axis_end_view[3];
        bool axis_visible[3] = {false, false, false};
        QPolygonF plane_view[3]; /* XY, XZ, YZ */
        std::vector<QPolygonF> rotation_rings;
        double world_length = 120.0;
    };
    mutable GizmoGeometry gizmo_geometry_cache_;
    mutable bool gizmo_geometry_cache_valid_ = false;

    struct GizmoLayerStart {
        std::string id;
        Vec2Value position;
        double position_z = 0.0;
        Vec2Value scale;
        double scale_z = 1.0;
        double rotation_x = 0.0;
        double rotation_y = 0.0;
        double rotation_z = 0.0;
        double orientation_x = 0.0;
        double orientation_y = 0.0;
        double orientation_z = 0.0;
    };
    struct GizmoDragState {
        bool active = false;
        bool changed = false;
        GizmoAxis axis = GizmoAxis::None;
        QPointF start_view;
        QPointF last_view;
        bool has_last_view = false;
        QVector3D axes_world[3];
        QPointF axis_screen[3];
        int rotation_scrub_axis = -1; /* 0 = horizontal, 1 = vertical */
        double world_length = 120.0;
        std::vector<GizmoLayerStart> layers;
    };
    GizmoDragState gizmo_drag_;
    GizmoAxis gizmo_hover_axis_ = GizmoAxis::None;

    struct GradientDragState {
        bool active = false;
        bool radial = false;
        std::string layer_id;
        GradientHandleTarget target;
        RichTextFill fill;
        QRectF local_rect;
        QPointF center;
        QPointF start;
        QPointF end;
        QPointF radius;
        QPointF focal;
        float center_x = 0.5f;
        float center_y = 0.5f;
        float focal_x = 0.5f;
        float focal_y = 0.5f;
        float scale = 1.0f;
        float angle = 0.0f;
    };
    GradientDragState gradient_drag_;
    bool gradient_editor_active_ = false;
    struct CornerPointDragState {
        int point_index = -1;
        double radius = 0.0;
    };
    struct CornerLayerDragState {
        std::string id;
        bool sync = true;
        std::vector<CornerPointDragState> points;
        std::vector<int> target_indices;
    };
    struct CornerRadiusDragState {
        bool active = false;
        std::string primary_layer_id;
        int point_index = -1;
        double primary_start_radius = 0.0;
        bool isolated = false;
        bool moved = false;
        bool value_changed = false;
        std::vector<CornerLayerDragState> layers;
    };
    CornerRadiusDragState corner_radius_drag_;
    std::string selected_corner_layer_id_;
    std::set<int> selected_corner_indices_;
    struct LayerDragState {
        std::string id;
        std::string resize_root_id;
        double x = 0.0;
        double y = 0.0;
        float w = 1.0f;
        float h = 1.0f;
        double local_left = -0.5;
        double local_top = -0.5;
        double scale_x = 1.0;
        double scale_y = 1.0;
        double rotation = 0.0;
        float stroke_width = 0.0f;
        float corner_radius_tl = 0.0f;
        float corner_radius_tr = 0.0f;
        float corner_radius_br = 0.0f;
        float corner_radius_bl = 0.0f;
        float shape_roundness = 0.0f;
        float shape_inner_roundness = 0.0f;
        std::vector<double> path_corner_radii;
        float quad[8] = {0,0,0,0,0,0,0,0};
    };
    std::vector<LayerDragState> drag_layer_states_;
    std::vector<LayerDragState> drag_child_layer_states_;
    mutable bool selection_overlay_cache_valid_ = false;
    mutable SelectionOverlayGeometry selection_overlay_cache_;
    mutable bool hover_overlay_cache_valid_ = false;
    mutable HoverOverlayGeometry hover_overlay_cache_;
};
