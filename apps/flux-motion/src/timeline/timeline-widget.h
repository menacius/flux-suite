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
#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QJsonObject>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <array>

class QEvent;
class QMouseEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QPaintEvent;
class QPainter;
class QScrollBar;
struct TimelinePropertyRef;
/* ══════════════════════════════════════════════════════════════════
 *  TimelineWidget  – keyframe timeline
 * ══════════════════════════════════════════════════════════════════ */
class TimelineWidget : public QWidget {
    Q_OBJECT

public:
    explicit TimelineWidget(QWidget *parent = nullptr);

    void set_title(std::shared_ptr<Title> t);
    void set_selected_layer(const std::string &lid);
    void set_selected_layers(const std::vector<std::string> &layer_ids);
    void set_playhead(double t);
    void set_vertical_scroll(int scroll_y);
    void set_zoom_percent(int percent);
    int zoom_percent() const;
    void fit_timeline();
    double horizontal_content_duration() const;
    double horizontal_view_start() const;
    double horizontal_view_duration() const;
    void set_horizontal_view(double start, double duration);
    void set_graph_editor_enabled(bool enabled);
    bool graph_editor_enabled() const { return graph_editor_enabled_; }
    void set_graph_view_mode(int mode);
    int graph_view_mode() const { return (int)graph_view_mode_; }
    void set_graph_channel_mode(int mode);
    int graph_channel_mode() const { return (int)graph_channel_mode_; }
    int graph_channel_count() const;
    QString graph_channel_label(int component) const;
    QColor graph_channel_color(int component) const;
    void select_graph_property(const std::string &owner_id,
                               const std::string &property_name,
                               int channel_mode = 3);
    void fit_graph_to_view();
    void fit_graph_selection();
    void show_property_velocity_dialog(const std::string &layer_id,
                                       const std::string &property_name,
                                       double local_time);
    bool has_selected_keyframes() const;
    bool has_keyframe_clipboard() const;
    bool copy_keyframe_selection();
    bool cut_keyframe_selection();
    bool delete_keyframe_selection();
    bool paste_keyframes_at_playhead();
    bool has_transition_target_selection() const;
    bool has_selected_transition() const;
    bool has_transition_clipboard() const;
    bool can_paste_transition_to_selection() const;
    bool copy_transition_selection();
    bool cut_transition_selection();
    bool delete_transition_selection();
    bool paste_transition_to_selection();
    void clear_transition_target_selection();

signals:
    void playhead_changed(double t);
    void keyframe_added(const std::string &layer_id,
                        const std::string &prop_name, double t);
    void keyframe_moved(const std::string &layer_id,
                        const std::string &prop_name, int kf_idx, double new_t);
    void keyframe_easing_changed();
    void keyframe_structure_changed();
    void vertical_scroll_delta_requested(int delta);
    void zoom_percent_changed(int percent);
    void horizontal_view_changed(double start, double duration,
                                 double content_duration);
    void graph_editor_enabled_changed(bool enabled);
    void graph_view_mode_changed(int mode);
    void graph_channel_mode_changed(int mode);
    void graph_channel_count_changed(int count);
    void layer_selected(const std::string &layer_id);
    void layers_selected(const std::vector<std::string> &layer_ids);
    void layer_context_menu_requested(const QPoint &global_pos);
    void effect_preset_dropped(const QString &file_path, const std::string &layer_id);
    void audio_effect_dropped(int effect_type, const std::string &layer_id);
    void transition_preset_dropped(const QString &file_path, const std::string &layer_id, int edge);
    void transition_edit_requested(const std::string &layer_id, int edge);
    void transition_modified();
    void layer_timing_changed(bool commit_undo);
    void audio_layer_property_changed(bool commit_undo);

protected:
    void paintEvent(QPaintEvent *ev) override;
    void mousePressEvent(QMouseEvent *ev) override;
    void mouseMoveEvent(QMouseEvent *ev) override;
    void mouseReleaseEvent(QMouseEvent *ev) override;
    void mouseDoubleClickEvent(QMouseEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dragMoveEvent(QDragMoveEvent *ev) override;
    void dragLeaveEvent(QDragLeaveEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void contextMenuEvent(QContextMenuEvent *ev) override;
    void wheelEvent(QWheelEvent *ev) override;
    void resizeEvent(QResizeEvent *ev) override;

private:
    double x_to_time(int x) const;
    int    time_to_x(double t) const;
    double timeline_pre_roll() const;
    double timeline_post_roll() const;
    double timeline_display_duration() const;
    double x_to_display_time(int x) const;
    int    display_time_to_x(double t) const;
    int    ruler_height() const { return 44; }
    int    row_height()   const { return 28; }
    double snap_time(double t) const;
    void   clamp_scroll();
    void   notify_horizontal_view_changed();
    void   clamp_vertical_scroll();
    int    max_vertical_scroll() const;
    bool   hit_keyframe(const QPoint &pos, std::shared_ptr<Layer> *layer,
                        TimelinePropertyRef *prop, int *kf_idx, int *row_idx,
                        std::string *owner_id = nullptr) const;
    struct KeyframeRef {
        std::string layer_id;
        std::string prop_name;
        int index = -1;
        bool operator<(const KeyframeRef &other) const;
    };
    struct DraggedKeyframe {
        KeyframeRef ref;
        double start_time = 0.0;
    };
    struct ClipboardKeyframe {
        std::string layer_id;
        std::string prop_name;
        Keyframe keyframe;
        VectorKeyframe vector_keyframe;
        Vector3Keyframe vector3_keyframe;
        std::vector<Keyframe> scalar_group_keyframes;
        QJsonObject extension_keyframe;
        DiscreteKeyframe discrete_keyframe;
        bool is_vector = false;
        bool is_vector3 = false;
        bool is_scalar_group = false;
        bool is_extension = false;
        bool is_discrete = false;
        double offset = 0.0;
    };
    struct DraggedLayerStrip {
        struct KeyframeTime {
            std::string prop_name;
            int index = -1;
            double start_time = 0.0;
        };
        std::string layer_id;
        double start_in = 0.0;
        double start_out = 0.0;
        double start_media_in = 0.0;
        double start_media_out = 0.0;
        std::vector<KeyframeTime> keyframes;
    };
    enum class DragMode { None, Playhead, Keyframe, Marquee, TrimIn, TrimOut, Layer, TransitionDuration, AudioFadeIn, AudioFadeOut, LoopStart, LoopEnd, PauseMarker, StingerTransitionPoint, GraphKeyframe, GraphIncomingHandle, GraphOutgoingHandle, GraphMarquee, GraphPan };
    enum class GraphViewMode { Value = 0, Speed = 1 };
    enum class GraphChannelMode { X = 0, Y = 1, Z = 2, All = 3, Fourth = 4 };
    enum class GraphHitType { None, Keyframe, IncomingHandle, OutgoingHandle };
    struct GraphHit {
        GraphHitType type = GraphHitType::None;
        KeyframeRef ref;
        QPointF point;
        int channel = -1;
    };
    struct TemporalDragSnapshot {
        KeyframeRef ref;
        double incoming_influence = 33.3333333333;
        double outgoing_influence = 33.3333333333;
        double incoming_speed = 0.0;
        double outgoing_speed = 0.0;
        double time = 0.0;
        double value = 0.0;
        std::array<double, 4> channel_values {0.0, 0.0, 0.0, 0.0};
        int channel_count = 1;
        bool linked = true;
    };
    struct TransitionHit {
        std::shared_ptr<Layer> layer;
        LayerTransitionEdge edge = LayerTransitionEdge::In;
        QRect rect;
        bool duration_handle = false;
    };

    void   clear_keyframe_selection();
    void   prune_keyframe_selection();
    bool   is_keyframe_selected(const std::string &layer_id, const std::string &prop_name, int kf_idx) const;
    void   select_keyframe(const std::string &layer_id, const std::string &prop_name, int kf_idx, bool additive, bool toggle);
    void   select_keyframes_in_rect(const QRect &rect, bool additive);
    bool   copy_selected_keyframes();
    bool   delete_selected_keyframes();
    bool   cut_selected_keyframes();
    bool   paste_keyframes_at(double timeline_time, bool snap_origin = true);
    bool   add_keyframe_at(const std::string &owner_id,
                           const std::string &property_name,
                           double timeline_time, bool snap_to_frame);
    bool   clipboard_single_property(std::string *owner_id,
                                     std::string *property_name) const;
    bool   clipboard_property_compatible(const ClipboardKeyframe &entry,
                                         const TimelinePropertyRef &target) const;
    TimelinePropertyRef clipboard_target_property(std::string *owner_id,
                                                  std::string *property_name) const;
    void   erase_keyframes_at(TimelinePropertyRef prop, double local_time);
    int    keyframe_index_near(const TimelinePropertyRef &prop,
                               double local_time,
                               double tolerance = 1e-6) const;
    QRect  marquee_rect() const;
    void   begin_keyframe_drag(const std::string &layer_id, const std::string &prop_name, int kf_idx, double start_time);
    TimelinePropertyRef find_timeline_property(Layer &layer, const std::string &prop_name) const;
    TimelinePropertyRef find_timeline_property(const std::string &owner_id,
                                               const std::string &prop_name) const;
    bool   keep_playhead_visible();
    void   set_pixels_per_sec(double pixels_per_sec, double anchor_time, int anchor_x);
    bool   is_layer_selected(const std::string &layer_id) const;
    void   select_layer_from_mouse(const std::string &layer_id, Qt::KeyboardModifiers modifiers);
    void   begin_layer_strip_drag(const std::string &layer_id, DragMode mode, double start_time);
    QRect  playhead_dirty_rect(int playhead_x) const;
    std::shared_ptr<Layer> layer_strip_at_pos(const QPoint &pos) const;
    bool transition_hit_at_pos(const QPoint &pos, TransitionHit *hit) const;
    bool transition_drop_target_at_pos(const QPoint &pos, std::shared_ptr<Layer> *layer,
                                       LayerTransitionEdge *edge) const;
    bool transition_edge_target_at_pos(const QPoint &pos, std::shared_ptr<Layer> *layer,
                                       LayerTransitionEdge *edge) const;
    QRect transition_rect(const Layer &layer, const LayerTransition &transition, int row_y) const;
    QRect transition_edge_target_rect(const Layer &layer, LayerTransitionEdge edge, int row_y) const;
    void normalize_transition_durations(Layer &layer);
    void clear_transition_drop_preview();
    void select_transition_target(const std::string &layer_id, LayerTransitionEdge edge);
    void clear_transition_selection();
    std::shared_ptr<Layer> selected_transition_layer() const;
    const LayerTransition *selected_transition() const;
    LayerTransition *selected_transition();
    bool layer_accepts_transition(const Layer &layer, const LayerTransition &transition) const;

    QRect graph_rect() const;
    TimelinePropertyRef active_graph_base_property(std::shared_ptr<Layer> *layer = nullptr) const;
    TimelinePropertyRef active_graph_property(std::shared_ptr<Layer> *layer = nullptr) const;
    std::vector<TimelinePropertyRef> active_graph_channels(std::shared_ptr<Layer> *layer = nullptr) const;
    void notify_graph_channel_count();
    int graph_channel_component() const;
    GraphChannelMode graph_mode_for_component(int component) const;
    void set_graph_target(const std::string &owner_id,
                          const std::string &property_name);
    bool graph_ref_property(const KeyframeRef &ref, TimelinePropertyRef *prop,
                            std::shared_ptr<Layer> *layer = nullptr) const;
    double graph_value_to_y(double value) const;
    double graph_y_to_value(double y) const;
    double graph_x_to_time(double x) const;
    void paint_graph_editor(QPainter &painter, const QRect &dirty);
    GraphHit graph_hit_test(const QPoint &pos) const;
    bool graph_mouse_press(QMouseEvent *event);
    bool graph_mouse_move(QMouseEvent *event);
    bool graph_mouse_release(QMouseEvent *event);
    void graph_context_menu(QContextMenuEvent *event);
    void update_graph_fit(bool selection_only);
    void fit_graph_time_range(bool selection_only);
    void show_temporal_velocity_dialog(TimelinePropertyRef prop,
                                       const std::vector<int> &indices);
    std::vector<KeyframeRef> graph_edit_targets(const KeyframeRef &primary) const;

    std::shared_ptr<Title> title_;
    std::string sel_layer_id_;
    std::vector<std::string> selected_layer_ids_;
    std::string selection_anchor_layer_id_;
    bool fit_zoom_active_ = true;
    double playhead_  = 0.0;
    DragMode drag_mode_ = DragMode::None;
    std::string drag_layer_id_;
    std::string drag_prop_name_;
    int drag_keyframe_index_ = -1;
    double drag_start_time_ = 0.0;
    double drag_start_in_ = 0.0;
    double drag_start_out_ = 0.0;
    LayerTransitionEdge drag_transition_edge_ = LayerTransitionEdge::In;
    std::set<KeyframeRef> selected_keyframes_;
    std::vector<DraggedKeyframe> dragged_keyframes_;
    std::vector<DraggedLayerStrip> dragged_layer_strips_;
    std::vector<ClipboardKeyframe> keyframe_clipboard_;
    QPoint marquee_start_;
    QPoint marquee_current_;
    bool marquee_additive_ = false;
    bool marquee_moved_ = false;
    double pixels_per_sec_ = 80.0;
    int    scroll_x_       = 0;
    int    scroll_y_       = 0;
    std::string transition_drop_preview_layer_id_;
    LayerTransitionEdge transition_drop_preview_edge_ = LayerTransitionEdge::In;
    bool transition_target_selected_ = false;
    std::string selected_transition_layer_id_;
    LayerTransitionEdge selected_transition_edge_ = LayerTransitionEdge::In;
    bool transition_clipboard_valid_ = false;
    LayerTransition transition_clipboard_;

    bool graph_editor_enabled_ = false;
    GraphViewMode graph_view_mode_ = GraphViewMode::Value;
    GraphChannelMode graph_channel_mode_ = GraphChannelMode::All;
    double graph_value_min_ = -1.0;
    double graph_value_max_ = 1.0;
    bool graph_fit_pending_ = true;
    std::string graph_target_owner_id_;
    std::string graph_target_property_name_;
    GraphHit graph_drag_hit_;
    std::vector<TemporalDragSnapshot> temporal_drag_snapshots_;
    QPoint graph_drag_start_;
    QPoint graph_marquee_current_;
    double graph_drag_start_min_ = -1.0;
    double graph_drag_start_max_ = 1.0;
    int graph_drag_start_scroll_x_ = 0;
    bool graph_drag_changed_ = false;

    /* Debug/trace profiling is aggregated to one record per second so it can
     * remain enabled without turning every paint into a logging bottleneck. */
    QElapsedTimer paint_profile_window_;
    qint64 paint_profile_total_us_ = 0;
    int paint_profile_samples_ = 0;
};
