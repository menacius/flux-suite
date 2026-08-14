/*
 * title-editor.h
 *
 * Part 3: After Effects-style title editor.
 *
 * Layout
 * ┌───────────────────────────────────────────────────────────────┐
 * │  Menu bar  [File ▾]  [Title name]                             │
 * ├──────────────────────────┬────────────────────────────────────┤
 * │  CANVAS PREVIEW          │  PROPERTIES PANEL                  │
 * │  (live render, zoom)     │  (layer-specific controls)         │
 * │                          │                                    │
 * ├──────────────────────────┴────────────────────────────────────┤
 * │  LAYER STACK                │  TIMELINE / KEYFRAME EDITOR      │
 * │  (AE-style layer list)      │  (ruler, clips, keyframe dots)   │
 * └─────────────────────────────┴──────────────────────────────────┘
 *
 * The editor is a QMainWindow (non-modal so OBS stays usable) with Qt/OBS-style dock widgets.
 */

#pragma once

#include "title-data.h"
#include "editor-host-interfaces.h"
#include <QMainWindow>
#include <QWidget>
#include <QDockWidget>
#include <QSplitter>
#include <QListWidget>
#include <QToolBar>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QFormLayout>
#include <QTimer>
#include <QElapsedTimer>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QColor>
#include <QIcon>
#include <QKeySequence>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QVariantMap>
#include <memory>
#include <utility>
#include <string>
#include <vector>
#include <set>

/* Forward declarations for sub-widgets */
class CanvasPreview;
class LayerStack;
class TimelineWidget;
class PropertiesPanel;
class EffectsPanel;
class EffectsPresetsPanel;
class ToolsSidebar;
class TitlePropertiesPanel;
class PrerenderDock;
class ResponsiveSwatchGrid;
class QEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QCloseEvent;
class QShowEvent;
class QAction;
class QToolButton;
class QScrollBar;
class QMenuBar;
class QMenu;
class QStatusBar;
class QActionGroup;
class QVBoxLayout;
class QTextEdit;
class FxmEditorAudioMeter;
struct RichTextCharFormat;

namespace fxm::editor {

class ProjectPanel;

enum class EditorHostKind { Standalone, ObsPlugin, ExternalHost };

/* Host capabilities, not host-specific menus, drive the shared command set.
 * A future NLE or broadcast host can opt into individual workflows without
 * adding another File-menu implementation. */
struct EditorExecutionContext {
    EditorHostKind host = EditorHostKind::Standalone;
    bool supports_new_open = true;
    bool supports_recent_projects = true;
    bool supports_save_as = true;
    bool supports_media_export = true;
    bool close_returns_to_host = false;
    int host_canvas_width = 0;
    int host_canvas_height = 0;
    double host_frame_rate = 0.0;

    static EditorExecutionContext standalone() { return {}; }
    static EditorExecutionContext obsPlugin(int canvas_width = 0,
                                            int canvas_height = 0,
                                            double frame_rate = 0.0)
    {
        EditorExecutionContext context;
        context.host = EditorHostKind::ObsPlugin;
        context.supports_new_open = false;
        context.supports_recent_projects = false;
        context.supports_save_as = false;
        context.supports_media_export = true;
        context.close_returns_to_host = true;
        context.host_canvas_width = canvas_width;
        context.host_canvas_height = canvas_height;
        context.host_frame_rate = frame_rate;
        return context;
    }
};

struct StandaloneNewTitleSettings {
    double duration = 5.0;
    int width = 1920;
    int height = 1080;
    double frame_rate = 30.0;
};

/* Standalone creation asks for authored document settings. OBS-hosted titles
 * deliberately bypass this dialog and inherit the active OBS video format. */
bool prompt_standalone_new_title_settings(
    QWidget *parent, StandaloneNewTitleSettings &settings);
void apply_standalone_new_title_settings(
    const std::shared_ptr<Title> &title,
    const StandaloneNewTitleSettings &settings);

} // namespace fxm::editor

/* Render the same version-aware artwork used by About for startup surfaces. */
QPixmap fxm_about_graphic_pixmap(const QSize &logical_size,
                                 qreal device_pixel_ratio = 1.0);

/* ══════════════════════════════════════════════════════════════════
 *  TitleEditor  – main editor window
 * ══════════════════════════════════════════════════════════════════ */
class TitleEditor : public QMainWindow {
    Q_OBJECT

public:
    explicit TitleEditor(QWidget *parent = nullptr);
    TitleEditor(QWidget *parent, Qt::WindowFlags flags);
    TitleEditor(QWidget *parent, Qt::WindowFlags flags,
                fxm::editor::EditorExecutionContext execution_context);
    ~TitleEditor() override;

    void open_title(const std::string &title_id);
    void set_project_file_path(const QString &path);
    void begin_unsaved_project();
    void set_project_context(const TitleProject &project, const QString &path);
    QString project_file_path() const { return current_project_path_; }
    static void show_global_preferences(QWidget *parent = nullptr);

signals:
    void title_saved(const std::string &title_id);
    void editor_closed();
    void recent_projects_changed();

public slots:
    /* Transport */
    void play_pause();
    void play_full_loop();
    void reverse_play();
    void go_to_start();
    void go_to_end();
    void step_backward();
    void step_forward();
    void previous_keyframe();
    void next_keyframe();

    /* Called by sub-widgets */
    void on_layer_selected(const std::string &layer_id);
    void on_playhead_changed(double t);
    void on_title_modified(bool push_undo_snapshot = true);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *ev) override;
    void showEvent(QShowEvent *ev) override;
    void closeEvent(QCloseEvent *ev) override;

private slots:
    void tick();
    void show_about();
    void show_preferences();
    void reject();

private:
    static void show_preferences_dialog(QWidget *parent, TitleEditor *editor);
    void build_ui();
    enum class FileCommand {
        NewProject, OpenProject, Import, Save, SaveAs, SaveAsAsset, SaveInLibrary,
        Close, ExportMedia, ProjectSettings, Exit
    };
    void build_file_menu(QMenuBar *menu_bar);
    QAction *register_file_command(QMenu *menu, FileCommand command,
                                   const QString &text,
                                   const QIcon &icon = QIcon(),
                                   const QList<QKeySequence> &shortcuts = {});
    void execute_file_command(FileCommand command);
    void refresh_file_commands();
    void rebuild_recent_projects_menu();
    void build_toolbar();
    void update_corner_toolbar();
    void update_3d_toolbar_visibility();
    void update_title_bar();
    void set_dirty(bool dirty);
    void begin_title_name_edit();
    void commit_title_name_edit(bool accept);
    bool confirm_save_before_close();
    void new_project();
    void open_project();
    bool open_project_path(const QString &path);
    void new_title_contents();
    bool save_title();
    bool save_title_as();
    bool save_project_to_path(const QString &path, bool show_saved_status,
                              bool autosave_operation = false);
    TitleProject project_snapshot() const;
    void refresh_project_panel();
    void open_project_title(const std::string &title_id);
    void create_project_title();
    void duplicate_project_titles(const std::vector<std::string> &title_ids);
    void rename_project_title(const std::string &title_id);
    void delete_project_titles(const std::vector<std::string> &title_ids);
    void import_project_titles();
    void export_project_titles(const std::vector<std::string> &title_ids);
    void reorder_project_titles(const std::vector<std::string> &title_ids);
    void show_project_settings();
    void exit_application();
    bool persist_title_changes(bool update_preview_screenshot, bool show_saved_status,
                               bool autosave_operation = false);
    void perform_autosave();
    void update_autosave_timer();
    void update_statusbar_autosave_summary();
    void show_editor_activity(const QString &message, int timeout_ms = 4000);
    void set_live_editing_enabled(bool enabled);
    void save_live_edit();
    void save_title_as_new();
    void save_title_as_asset();
    void export_as_media();
    void refresh_flux_encoder_export_action();
    void export_title_template(bool save_in_library);
    void import_document();
    void import_vector_document();
    void import_vector_document(const QString &path);
    void import_gimp_document();
    void import_gimp_document(const QString &path);
    void import_photoshop_document();
    void import_photoshop_document(const QString &path);
    void import_layered_document(const QString &path, bool photoshop);
    void copy_title_to_store(const std::shared_ptr<Title> &source, const std::shared_ptr<Title> &dest) const;
    void align_selected_to_canvas(int x_mode, int y_mode);
    void align_selected_layers_horizontal();
    void align_selected_layers_vertical();
    void align_selected_layers(int x_mode, int y_mode);
    void distribute_selected_layers(bool horizontal);
    void flip_selected_layers(bool horizontal);
    void rotate_selected_layers(double degrees);
    void fit_fill_selected_layers(bool fill_screen);
    void reorder_selected_layers(int action);
    enum class BooleanShapeOperation { Union, SubtractFront, Intersect, Exclude };
    bool boolean_shape_selection_available() const;
    void apply_boolean_shape_operation(BooleanShapeOperation operation);
    std::shared_ptr<Title> clone_title(const Title &title) const;
    std::shared_ptr<Layer> clone_layer_for_insert(const Layer &layer, bool suffix_name) const;
    std::string unique_layer_name(const std::string &base_name,
                                  const std::set<std::string> &exclude_ids = {},
                                  std::set<std::string> *reserved_names = nullptr) const;
    void insert_layer_above(const std::string &anchor_id, std::shared_ptr<Layer> layer);
    void select_after_layer_list_mutation(const std::string &layer_id);
    std::vector<std::string> selected_layer_ids_for_operation() const;
    std::vector<std::shared_ptr<Layer>> clone_layers_for_insert(const std::vector<std::shared_ptr<Layer>> &layers, bool suffix_name) const;
    void apply_picked_color_to_selection(const QColor &color, bool commit = true);
    void duplicate_selected_layers();
    void group_selected_layers();
    void ungroup_selected_layers();
    void add_selected_layers_to_group(const std::string &group_id);
    void remove_selected_layers_from_group();
    void drop_layer_list_rows(const std::vector<std::string> &layer_ids,
                              const std::string &target_layer_id,
                              int placement);
    void copy_selected_layer();
    void cut_selected_layer();
    void paste_layer_from_clipboard();
    bool paste_external_clipboard_to_canvas();
    void delete_selected_layer();
    std::shared_ptr<Layer> create_basic_layer(LayerType type, const QString &name_override = QString());
    void create_shape_layer_from_canvas(ShapeType shape_type, const QPointF &canvas_pt);
    void create_pen_path_layer_from_canvas(const std::vector<BezierPathPoint> &canvas_points, bool closed);
    void create_text_layer_from_canvas(LayerType type, const QPointF &canvas_pt);
    void create_image_layer_from_canvas(const QPointF &canvas_pt);
    void choose_image_file_for_layer(const std::string &layer_id);
    void create_image_layer_from_external_source(const QString &image_path, const QPointF &canvas_pt);
    void create_text_layer_from_external_source(const QString &text, const QPointF &canvas_pt);
    void insert_asset_layer(const std::string &asset_id, const QPointF &canvas_pt);
    void edit_asset(const std::string &asset_id);
    void open_asset_overrides_dialog(const std::string &asset_layer_id);
    void update_canvas_created_shape(const QRectF &canvas_rect);
    void finish_canvas_created_shape(bool keep_layer);
    void push_undo_snapshot();
    void restore_undo_snapshot(int index);
    void update_undo_redo_actions();
    void begin_text_property_transaction(const std::string &layer_id);
    void commit_text_property_transaction(const std::string &layer_id);
    void create_docked_panel_menu(QMenuBar *menu_bar);
    QDockWidget *create_editor_dock(const QString &object_name, const QString &title, QWidget *panel);
    QWidget *create_prerender_panel();
    QWidget *create_editor_audio_panel();
    QWidget *create_effects_panel();
    QWidget *create_effects_presets_panel();
    QWidget *create_styles_panel();
    QWidget *create_color_swatches_panel();
    void remember_recent_color(const QColor &color);
    void remove_recent_color(int index);
    void persist_recent_colors();
    void refresh_color_swatches_panel();
    void load_color_libraries();
    void save_color_libraries() const;
    void restore_selected_color_library();
    void persist_selected_color_library() const;
    void refresh_color_library_controls();
    void refresh_color_library_swatches();
    void create_color_library();
    int prompt_create_color_library();
    void rename_current_color_library();
    void delete_current_color_library();
    bool show_add_color_to_library_dialog(const QColor &color);
    void add_color_to_current_library(const QColor &color);
    void add_color_to_library(int library_index, const QColor &color, const QString &name);
    void remove_color_from_current_library(int index);
    void update_layer_panels(std::shared_ptr<Layer> layer, double playhead);
    void synchronize_layer_selection(const std::vector<std::string> &layer_ids,
                                     bool clear_transition_target = true);
    void apply_effect_preset_to_layer(const QString &file_path, const std::string &layer_id);
    void apply_transition_preset_to_layer(const QString &file_path, const std::string &layer_id, int edge);
    void edit_layer_transition(const std::string &layer_id, int edge);
    void update_sidebar_color_swatches(std::shared_ptr<Layer> layer);
    void set_default_sidebar_colors_from_layer(const Layer &layer);
    void load_sidebar_default_colors();
    void save_sidebar_default_colors() const;
    void copy_layer_style_to_new_layer_defaults(const Layer &layer);
    void apply_new_layer_defaults(Layer &layer) const;
    void load_new_layer_defaults();
    void save_new_layer_defaults() const;
    void open_default_sidebar_color_popup(bool foreground);
    enum class EditorTool { Selection, DirectSelection, FreeTransform, Shape, Pen, Text, Image, ColorPicker, Gradient };
    void activate_editor_tool(EditorTool tool);
    void handle_gradient_editor_tool_request(bool active);
    void load_editor_layout();
    void save_editor_layout() const;
    void restore_editor_dock_sizes();
    void reset_default_layout();
    void set_editor_dock_visible(QDockWidget *dock, bool visible);
    void set_panels_locked(bool locked);
    void update_panel_lock_state();
    void schedule_cache_invalidation();
    void force_next_title_visual_update();
    void apply_playhead_change(double t, bool playback_frame);
    void refresh_playback_ui(double t, bool force_full_refresh);
    void reset_playback_timer_cadence();
    void schedule_next_playback_timer_interval();
    void try_start_playback_after_prerender();
    void update_display_refresh_pacing();
    void ensure_editor_audio_preview();
    void release_editor_audio_preview();
    void sync_editor_audio_preview(bool discontinuity);
    void apply_editor_audio_monitoring();
    void update_editor_audio_monitor_button();
    void update_editor_audio_meter();
    void publish_editor_audio_runtime_state();
    void update_footer_diagnostics();
    void begin_shutdown();

    /* Current editing state */
    std::shared_ptr<Title> title_;
    std::string            editing_title_id_;
    std::string            sel_layer_id_;
    std::vector<std::string> synchronized_layer_selection_;
    bool                   synchronizing_layer_selection_ = false;
    std::string            active_text_edit_layer_id_;
    double                 playhead_  = 0.0;
    bool                   playing_   = false;
    bool                   playback_reverse_ = false;
    bool                   manual_reverse_playback_ = false;
    bool                   full_loop_playback_ = false;
    bool                   pending_play_after_prerender_ = false;
    bool                   dirty_ = false;
    fxm::editor::EditorExecutionContext execution_context_;
    QString                current_project_path_;
    bool                   current_project_requires_save_as_ = false;
    TitleProject           current_project_;
    std::vector<std::string> project_title_ids_;
    bool                   project_dirty_ = false;
    bool                   shutting_down_ = false;
    bool                   force_next_visual_update_ = false;
    uint64_t               external_data_callback_id_ = 0;
    QString                external_data_visual_hash_;
    QTimer                *play_timer_ = nullptr;
    QTimer                *gui_refresh_timer_ = nullptr;
    QTimer                *layout_settle_timer_ = nullptr;
    QTimer                *clock_timer_ = nullptr;
    QTimer                *cache_invalidation_timer_ = nullptr;
    QTimer                *autosave_timer_ = nullptr;
    QTimer                *status_activity_timer_ = nullptr;
    QTimer                *inline_text_panel_refresh_timer_ = nullptr;
    QTimer                *inline_text_live_publish_timer_ = nullptr;
    QElapsedTimer          playback_clock_;
    QElapsedTimer          cache_reprioritize_clock_;
    QElapsedTimer          playback_transport_ui_clock_;
    QElapsedTimer          playback_inspector_ui_clock_;
    QElapsedTimer          playback_diagnostics_clock_;
    QElapsedTimer          playback_pointer_ui_clock_;
    double                 playback_timer_fractional_error_ms_ = 0.0;
    double                 playback_timer_frame_duration_ms_ = 0.0;
    double                 display_refresh_hz_ = 60.0;
    bool                   dock_layout_transition_ = false;

    /* Sub-widgets */
    CanvasPreview   *canvas_    = nullptr;
    LayerStack      *layers_    = nullptr;
    TimelineWidget  *timeline_  = nullptr;
    PropertiesPanel *props_     = nullptr;
    EffectsPanel    *effects_panel_ = nullptr;
    EffectsPresetsPanel *effects_presets_panel_ = nullptr;
    TitlePropertiesPanel *title_props_ = nullptr;
    QDockWidget     *layer_props_dock_ = nullptr;
    QDockWidget     *graphic_props_dock_ = nullptr;
    QDockWidget     *three_d_scene_dock_ = nullptr;
    QDockWidget     *effects_dock_ = nullptr;
    QDockWidget     *effects_presets_dock_ = nullptr;
    QDockWidget     *styles_dock_ = nullptr;
    QDockWidget     *timeline_dock_ = nullptr;
    QDockWidget     *prerender_dock_ = nullptr;
    QDockWidget     *editor_audio_dock_ = nullptr;
    QDockWidget     *tools_dock_ = nullptr;
    QDockWidget     *project_dock_ = nullptr;
    fxm::editor::ProjectPanel *project_panel_ = nullptr;
    ResponsiveSwatchGrid *recent_color_swatches_grid_ = nullptr;
    std::vector<QToolButton *> recent_color_swatch_buttons_;
    struct ColorLibraryColor {
        QString name;
        QColor color;
    };
    struct ColorLibrary {
        QString name;
        QString slug;
        bool built_in = false;
        std::vector<ColorLibraryColor> colors;
    };
    std::vector<ColorLibrary> color_libraries_;
    int              selected_color_library_index_ = 0;
    QComboBox       *color_library_combo_ = nullptr;
    QToolButton     *color_library_add_button_ = nullptr;
    QToolButton     *color_library_rename_button_ = nullptr;
    QToolButton     *color_library_delete_button_ = nullptr;
    QWidget         *color_library_swatch_widget_ = nullptr;
    ResponsiveSwatchGrid *color_library_swatches_grid_ = nullptr;
    ToolsSidebar    *tools_sidebar_ = nullptr;
    PrerenderDock   *prerender_panel_ = nullptr;
    QColor           default_foreground_color_ = QColor(34, 34, 34);
    QColor           default_background_color_ = QColor(255, 255, 255);
    bool             reopen_color_tab_after_canvas_pick_ = false;
    EditorTool       current_editor_tool_ = EditorTool::Selection;
    EditorTool       tool_before_gradient_editor_ = EditorTool::Selection;
    bool             gradient_editor_tool_override_active_ = false;
    Layer            default_new_layer_style_;
    QLabel          *time_lbl_  = nullptr;
    QLabel          *title_lbl_ = nullptr;
    QLineEdit       *title_name_edit_ = nullptr;
    QLabel          *dirty_indicator_ = nullptr;
    QStatusBar      *editor_status_bar_ = nullptr;
    QLabel          *status_diagnostics_label_ = nullptr;
    QLabel          *status_activity_label_ = nullptr;

    QToolBar        *toolbar_   = nullptr;
    QWidget         *editor_3d_toolbar_ = nullptr;
    QWidget         *dynamic_toolbar_widget_ = nullptr;
    QAction         *dynamic_toolbar_action_ = nullptr;
    QAction         *dynamic_toolbar_separator_ = nullptr;
    QWidget         *transform_toolbar_widget_ = nullptr;
    QComboBox       *transform_toolbar_mode_ = nullptr;
    QToolButton     *transform_toolbar_keyframe_ = nullptr;
    QToolButton     *transform_toolbar_reset_ = nullptr;
    QWidget         *anchor_toolbar_widget_ = nullptr;
    QPushButton     *anchor_toolbar_grid_ = nullptr;
    QWidget         *boolean_toolbar_widget_ = nullptr;
    QToolButton     *boolean_union_button_ = nullptr;
    QToolButton     *boolean_subtract_button_ = nullptr;
    QToolButton     *boolean_intersect_button_ = nullptr;
    QToolButton     *boolean_exclude_button_ = nullptr;
    QWidget         *corner_toolbar_widget_ = nullptr;
    QLabel          *corner_toolbar_label_ = nullptr;
    QDoubleSpinBox  *corner_toolbar_roundness_ = nullptr;
    QDoubleSpinBox  *corner_toolbar_radius_ = nullptr;
    QCheckBox       *corner_toolbar_sync_ = nullptr;
    QWidget         *point_toolbar_widget_ = nullptr;
    QToolButton     *point_toolbar_corner_ = nullptr;
    QToolButton     *point_toolbar_smooth_ = nullptr;
    QToolButton     *point_toolbar_handles_ = nullptr;
    QDoubleSpinBox  *point_toolbar_x_ = nullptr;
    QDoubleSpinBox  *point_toolbar_y_ = nullptr;
    bool             updating_corner_toolbar_ = false;
    QAction         *act_play_  = nullptr;
    QAction         *act_full_loop_ = nullptr;
    QAction         *act_rew_   = nullptr;
    QAction         *act_go_start_ = nullptr;
    QAction         *act_go_end_ = nullptr;
    QAction         *act_step_back_ = nullptr;
    QAction         *act_prev_kf_ = nullptr;
    QAction         *act_next_kf_ = nullptr;
    QAction         *act_safe_guides_ = nullptr;
    QAction         *act_rulers_visible_ = nullptr;
    QAction         *act_guides_visible_ = nullptr;
    QAction         *act_snap_enabled_ = nullptr;
    QAction         *act_guides_locked_ = nullptr;
    QAction         *act_clear_guides_ = nullptr;
    QAction         *act_guide_coordinates_ = nullptr;
    QAction         *act_canvas_border_visible_ = nullptr;
    QAction         *act_live_editing_ = nullptr;
    QAction         *act_undo_ = nullptr;
    QAction         *act_redo_ = nullptr;
    QAction         *act_export_media_ = nullptr;
    QMenu           *file_menu_ = nullptr;
    QMenu           *recent_projects_menu_ = nullptr;
    std::vector<std::pair<FileCommand, QAction *>> file_command_actions_;
    QAction         *act_lock_panels_ = nullptr;
    QAction         *act_layer_props_visible_ = nullptr;
    QAction         *act_graphic_props_visible_ = nullptr;
    QAction         *act_three_d_scene_visible_ = nullptr;
    QAction         *act_effects_visible_ = nullptr;
    QAction         *act_effects_presets_visible_ = nullptr;
    QAction         *act_styles_visible_ = nullptr;
    QAction         *act_timeline_visible_ = nullptr;
    QAction         *act_prerender_visible_ = nullptr;
    QAction         *act_editor_audio_visible_ = nullptr;
    QAction         *act_tools_visible_ = nullptr;
    std::string      canvas_created_shape_layer_id_;
    int              alignment_target_ = 3; /* 0=selection bounds, 1=title safe, 2=action safe, 3=artboard, 4=selection anchors */
    bool             distribute_to_anchors_ = false; /* false=bounds (default), true=layer anchors */
    std::vector<std::shared_ptr<Title>> undo_stack_;
    int              undo_index_ = -1;
    bool             restoring_undo_ = false;
    /* Inline typing uses QTextDocument's local history until commit. This flag
     * records that the canonical title has advanced beyond the most recent
     * title-wide snapshot, allowing a property edit to insert its true
     * pre-change checkpoint. */
    bool             inline_text_changed_since_undo_snapshot_ = false;
    /* A Properties drag/popup may emit many live updates before one committed
     * change. Track a single title-level transaction so the canonical rich-text
     * pre-state is checkpointed once and the post-state is pushed once. */
    bool             text_property_transaction_active_ = false;
    std::string      text_property_transaction_layer_id_;
    bool             live_editing_ = false;
    std::string      pending_inline_text_refresh_layer_id_;
    bool             updating_layer_panels_ = false;
    bool             panels_locked_ = false;
    bool             restoring_editor_layout_ = false;
    bool             editor_layout_save_suppressed_ = false;
    bool             initial_visible_layout_applied_ = false;
    QVariantMap      saved_editor_dock_widths_;
    QVariantMap      saved_editor_dock_heights_;
    std::unique_ptr<fxm::editor_host::IEditorAudioPreviewSession>
        editor_audio_preview_session_;
    QToolButton      *editor_audio_monitor_button_ = nullptr;
    FxmEditorAudioMeter *editor_audio_meter_ = nullptr;
    QTimer           *editor_audio_meter_timer_ = nullptr;
    bool              editor_audio_preview_seeking_ = false;
    bool              editor_audio_monitor_enabled_ = true;
    bool              editor_audio_snapshot_dirty_ = true;
    bool              editor_audio_play_state_known_ = false;
    bool              editor_audio_last_playing_ = false;
    QElapsedTimer     editor_audio_speed_clock_;
    double            editor_audio_speed_last_playhead_ = 0.0;
    double            editor_audio_speed_factor_ = 1.0;
    std::vector<std::shared_ptr<Layer>> layer_clipboard_;
    std::vector<TitleCamera> layer_clipboard_cameras_;
    std::string layer_clipboard_source_title_id_;
    std::set<std::string> pending_text_layer_auto_names_;
};
