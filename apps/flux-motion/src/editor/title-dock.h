/*
 * title-dock.h
 *
 * Part 2: OBS Dock – "Flux Motion" panel.
 *
 * Shows a list of all saved titles with:
 *   • Live thumbnail preview
 *   • Add / Delete / Duplicate buttons
 *   • "Edit" button / double-click → launches the standalone TitleEditor
 *   • "Add to Scene" button → creates/replaces the source in the current scene
 */

#pragma once

#include "title-data.h"
#include "editor-host-interfaces.h"
#include <QDockWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include <QTableWidget>
#include <QSplitter>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QTimer>
#include <QSpinBox>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <map>
#include <memory>
#include <vector>

class CanvasPreview;
class QString;
class QEvent;
class QResizeEvent;
class QHBoxLayout;
class QDialog;
class QFrame;
class QPushButton;
class QAction;

class TitleDock : public QDockWidget {
    Q_OBJECT

public:
    explicit TitleDock(QWidget *parent = nullptr);
    ~TitleDock() override;

    /* Called externally to refresh the list (e.g. after editor saves) */
    void refresh();
    void update_scene_collection_title();

    /* Two-stage live cue API. enterPreview() prepares an isolated immutable
     * snapshot; leavePreview() discards it without touching undo history or
     * Program cue state. */
    bool enterPreview(const std::shared_ptr<Title> &title, int row);
    void leavePreview();
    bool isPreviewReady() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_add();
    void on_add_from_templates_library();
    void on_duplicate();
    void on_rename();
    void on_delete();
    void on_export();
    void on_import();
    void on_edit();
    void on_add_to_scene();
    void on_toggle_template_view();
    void on_toggle_visibility_filter(bool enabled);
    void on_selection_changed();
    void on_add_live_text_row();
    void on_delete_live_text_rows();
    void on_move_live_text_row_up();
    void on_move_live_text_row_down();
    void on_import_live_text_data();
    void on_import_append_live_text_data();
    void on_export_live_text_data();
    void on_map_external_table();
    void on_toggle_external_data_source();
    void on_show_external_data_settings();
    void on_refresh_external_data();
    void on_live_text_lines_per_row_changed(int lines);
    void on_toggle_playlist(bool enabled);
    void on_playlist_tick();

private:
    void build_ui();
    void populate_list();
    void populate_exposed_text();
    void update_live_text_cache_cell(const std::shared_ptr<Title> &title, int row);
    void update_live_text_cache_cells();
    void update_live_text_runtime_status(const std::shared_ptr<Title> &title);
    void update_live_text_runtime_status_fast(const std::shared_ptr<Title> &title, int primary_row, int previous_row = -1);
    void schedule_title_list_cache_icon_update(const QString &title_id);
    void flush_title_list_cache_icon_updates();
    void update_title_list_cache_icon(const QString &title_id);
    void refresh_title_list_cue_visual_states();
    void update_live_text_select_cell_status(int row);
    void adjust_live_text_table_columns(bool fill_to_viewport = false);
    void install_host_state_callbacks();
    void remove_host_state_callbacks();
    void refresh_for_host_state_change();
    QSet<QString> active_title_source_ids() const;
    bool should_show_title(const std::shared_ptr<Title> &title, const QSet<QString> &active_ids) const;
    void update_template_view_mode();
    void set_all_live_text_rows_checked(bool checked);
    void update_live_text_select_all_state();
    void save_live_text_header_state();
    bool restore_live_text_header_state();
    void load_dock_settings();
    void save_dock_settings() const;
    bool cue_live_text_row(int row, bool allow_uncue);
    bool cue_live_text_row_for_title(const std::shared_ptr<Title> &title, int row, bool allow_uncue, bool force_restart = false);
    int live_text_playlist_row_count(const std::shared_ptr<Title> &title) const;
    void start_playlist_step();
    void start_playlist_step_for_title(const std::shared_ptr<Title> &title);
    int next_playlist_row(const std::shared_ptr<Title> &title, int current_row, int row_count) const;
    int playlist_step_delay_ms(const std::shared_ptr<Title> &title) const;
    int playlist_hold_delay_ms(const std::shared_ptr<Title> &title) const;
    bool playlist_row_is_terminal(const std::shared_ptr<Title> &title, int row, int row_count) const;
    void play_playlist_outro(const std::shared_ptr<Title> &title);
    void update_playlist_controls();
    void update_persistence_controls();
    void update_external_data_controls();
    void apply_live_text_row_heights();
    int live_text_row_height() const;
    void apply_persistence_settings_to_title(const std::shared_ptr<Title> &title);
    void update_playlist_countdown_label();
    void update_live_cue_timer_label();
    void stop_playlist();
    void stop_playlist_for_title(const std::shared_ptr<Title> &title);
    void sync_playlist_runtime_state();
    bool has_checked_live_text_rows() const;
    void apply_live_text_row_selection(const std::vector<int> &rows, bool checked);
    std::string selected_id() const;
    std::vector<std::string> selected_title_ids() const;
    std::shared_ptr<Title> create_template_title(const std::string &name, int template_id);
    void select_title(const std::string &id);
    void create_title_from_template(const std::string &name, int template_id);
    void import_title_paths(const QStringList &paths);
    std::vector<int> selected_live_text_rows() const;
    void commit_live_text_cell_edit(const std::shared_ptr<Title> &title, int row, int col, const QString &text);
    void set_live_text_row_render_paused(const std::shared_ptr<Title> &title, int row, bool paused);
    void set_titles_collapsed(bool collapsed, bool persist = true);
    void update_titles_compact_rail();
    void update_titles_collapse_direction();
    void update_stored_dock_area(Qt::DockWidgetArea area);
    void enforce_live_text_cache_column_visibility();
    void open_live_text_columns_window();
    void restore_live_text_table_to_dock();
    void update_live_text_columns_window_title();
    void refresh_live_cue_preview();
    bool attach_live_cue_preview_to_host();
    void detach_live_cue_preview_from_host();
    void sync_live_cue_preview_output_route();
    void release_live_cue_preview_source();
    void handle_program_command(fxm::editor_host::ProgramCommand command,
                               const std::string &title_id);
    int program_hotkey_target_row(const std::shared_ptr<Title> &title,
                                  bool prefer_last) const;

    int           cache_waiting_cue_row_ = -1;
    QString       cache_waiting_title_id_;
    QWidget      *container_  = nullptr;
    QWidget      *titles_section_ = nullptr;
    QWidget      *titles_expanded_content_ = nullptr;
    QWidget      *titles_compact_rail_ = nullptr;
    QHBoxLayout  *titles_header_layout_ = nullptr;
    QHBoxLayout  *titles_compact_layout_ = nullptr;
    QToolButton  *btn_titles_collapse_ = nullptr;
    QToolButton  *btn_titles_expand_ = nullptr;
    QLabel       *titles_compact_dock_icon_ = nullptr;
    QToolButton  *titles_compact_active_title_ = nullptr;
    QToolButton  *titles_compact_cue_state_ = nullptr;
    QToolButton  *titles_compact_cache_state_ = nullptr;
    QSplitter    *sections_   = nullptr;
    QListWidget  *list_       = nullptr;
    QToolButton *btn_add_    = nullptr;
    QToolButton *btn_dup_    = nullptr;
    QToolButton *btn_rename_ = nullptr;
    QToolButton *btn_del_    = nullptr;
    QToolButton *btn_export_ = nullptr;
    QToolButton *btn_edit_   = nullptr;
    QToolButton *btn_scene_  = nullptr;
    QToolButton *btn_view_   = nullptr;
    QToolButton *btn_visibility_filter_ = nullptr;
    QLabel       *template_lbl_ = nullptr;
    QLabel       *status_lbl_ = nullptr;
    QLabel       *text_editor_lbl_ = nullptr;
    QLabel       *live_cue_timer_lbl_ = nullptr;
    QLabel       *playlist_countdown_lbl_ = nullptr;
    QWidget      *live_text_section_ = nullptr;
    QVBoxLayout  *live_text_section_layout_ = nullptr;
    QWidget      *live_text_header_widget_ = nullptr;
    QToolBar     *live_text_toolbar_ = nullptr;
    QWidget      *live_text_table_host_ = nullptr;
    QVBoxLayout  *live_text_table_host_layout_ = nullptr;
    QWidget      *live_text_table_placeholder_ = nullptr;
    QTableWidget *text_table_ = nullptr;
    QToolButton *btn_add_text_row_ = nullptr;
    QToolButton *btn_delete_text_row_ = nullptr;
    QToolButton *btn_row_up_ = nullptr;
    QToolButton *btn_row_down_ = nullptr;
    QToolButton *btn_data_sources_ = nullptr;
    QToolButton *btn_live_text_settings_ = nullptr;
    QToolButton *btn_external_refresh_ = nullptr;
    QToolButton *btn_playlist_ = nullptr;
    QToolButton *btn_persistence_settings_ = nullptr;
    QAction     *act_live_text_columns_window_ = nullptr;
    QAction     *act_select_row_before_cue_ = nullptr;
    QAction     *act_show_cue_preview_ = nullptr;
    QAction     *act_external_data_source_ = nullptr;
    QAction     *act_external_data_settings_ = nullptr;
    QAction     *act_playlist_loop_ = nullptr;
    QAction     *act_playlist_reverse_ = nullptr;
    QAction     *act_playlist_restart_on_active_ = nullptr;
    QAction     *act_playlist_stop_on_inactive_ = nullptr;
    QAction     *act_playlist_hold_ = nullptr;
    QAction     *act_background_persistence_ = nullptr;
    QAction     *act_text_persistence_ = nullptr;
    QSpinBox    *spin_live_text_lines_per_row_ = nullptr;
    QFrame      *live_cue_preview_panel_ = nullptr;
    CanvasPreview *live_cue_preview_canvas_ = nullptr;
    QLabel      *live_cue_preview_status_ = nullptr;
    QPushButton *btn_live_cue_take_ = nullptr;
    QPushButton *btn_live_cue_cancel_preview_ = nullptr;
    bool          updating_exposed_text_ = false;
    bool          template_icon_view_ = false;
    bool          visibility_filter_active_ = false;
    bool          titles_collapsed_ = false;
    int           titles_expanded_width_ = 360;
    int           titles_expanded_splitter_size_ = 240;
    Qt::DockWidgetArea stored_dock_area_ = Qt::LeftDockWidgetArea;
    std::unique_ptr<fxm::editor_host::IHostEventSubscription>
        host_event_subscription_;
    std::unique_ptr<fxm::editor_host::IProgramCommandSubscription>
        program_command_subscription_;
    std::map<int, QByteArray> live_text_header_states_;
    QTimer       *live_refresh_timer_ = nullptr;
    QTimer       *playlist_timer_ = nullptr;
    double        playlist_hold_seconds_ = 5.0;
    bool          playlist_loop_ = false;
    bool          playlist_reverse_ = false;
    bool          background_persistence_ = false;
    bool          text_persistence_ = false;
    bool          select_row_before_cue_ = false;
    bool          show_cue_preview_ = true;
    QString       last_selected_title_id_;
    QString       live_text_width_initialized_title_id_;
    QSet<QString> pending_title_cache_icon_updates_;
    bool          title_cache_icon_update_scheduled_ = false;
    QHash<QString, int> focused_live_text_row_render_counts_;
    QHash<QString, int> last_cued_rows_;
    int           live_text_lines_per_row_ = 1;
    uint64_t      seen_store_revision_ = 0;
    uint64_t      change_callback_id_ = 0;
    uint64_t      external_data_callback_id_ = 0;
    QDialog      *live_text_columns_dialog_ = nullptr;
    std::unique_ptr<fxm::editor_host::ILiveCuePreviewSession>
        live_cue_preview_session_;
    bool          live_cue_preview_uses_host_ = false;
    std::shared_ptr<Title> live_cue_preview_title_;
    QString       live_cue_preview_title_id_;
    int           live_cue_preview_row_ = -1;
    double        live_cue_preview_time_ = 0.0;

    QDateTime     title_store_last_modified_;
    qint64        title_store_last_size_ = -1;
    int           title_store_poll_ticks_ = 0;
    bool          external_editor_launched_ = false;
};
