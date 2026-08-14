#pragma once

#include "title-data.h"
#include "title-rich-text.h"

#include <QWidget>
#include <QGroupBox>
#include <QScrollArea>
#include <QListWidget>
#include <QTreeWidget>
#include <QToolButton>
#include <QActionGroup>
#include <QButtonGroup>
#include <QMenu>
#include <QVBoxLayout>
#include <QGridLayout>
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
#include <QProgressBar>
#include <QTableWidget>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QAbstractButton>
#include <memory>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QPaintEvent;
class QPainter;
class QScrollBar;
class QTimer;
class QDialog;
class QTextEdit;
class TimecodeSpinBox;

class PropertiesPanel : public QScrollArea {
    Q_OBJECT

public:
    explicit PropertiesPanel(QWidget *parent = nullptr);

    void set_layer(std::shared_ptr<Layer> layer, double playhead);
    void set_undo_redo_available(bool undo_available, bool redo_available);
    void update_playhead(double playhead);
    void refresh_media_range_controls();
    void set_title(std::shared_ptr<Title> t);
    void set_active_text_edit_layer(const std::string &layer_id);
    void set_text_style_editor_mode(bool enabled);

public slots:
    void apply_anchor_preset(int index);
    void open_foreground_color_selector();
    void open_background_color_selector();
    void swap_foreground_background_colors();
    void remember_next_color_popup_position(const QPoint &global_pos);
    bool apply_external_picked_color(const QColor &color, bool commit);

signals:
    void undo_requested();
    void redo_requested();
    void property_changed(bool push_undo_snapshot = true);
    void audio_property_changed(bool commit_undo = false);
    void live_visual_changed();
    void runtime_visual_changed();
    /* Emitted before a canonical inline character/paragraph mutation. The
     * editor uses it to checkpoint text typed since the last title snapshot,
     * so Undo reverses the property edit without also discarding that typing. */
    void text_property_change_started(const std::string &layer_id);
    void text_char_format_changed(const std::string &layer_id, const RichTextCharFormat &format, uint32_t mask);
    void gradient_editor_active_changed(bool active);
    void gradient_model_refresh_requested();
    void color_picker_tool_requested();
    void recent_colors_changed();
    void color_library_add_requested(const QColor &color);
    void asset_overrides_requested(const std::string &layer_id);
    void keyframe_navigation_requested(double timeline_time);

private:
    void build_text_section(QWidget *w, QFormLayout *fl);
    void build_rect_section(QWidget *w, QFormLayout *fl);
    void build_transform_section(QWidget *w, QFormLayout *fl);
    void build_text_animator_section(QWidget *parent, QVBoxLayout *panel_layout);
    void refresh_text_animator_controls();
    void rebuild_text_animator_item_editor();
    TextAnimator *selected_text_animator();
    TextAnimatorProperty *selected_text_animator_property();
    TextSelector *selected_text_animator_selector();
    void notify_text_animator_changed(bool rebuild = true);
    QWidget *make_keyframe_controls(QAbstractButton *button, QWidget *parent);
    void register_keyframe_times(
        QAbstractButton *button,
        std::function<std::vector<double>()> provider);

    void load_values();
    void apply_text_style_editor_mode_visibility();
    void update_asset_playback_controls_visibility();
    void update_ticker_runtime_button();
    void update_transform_dimension_ui(bool supports_3d, LayerDimensionMode mode);
    Q_INVOKABLE void reset_panel_defaults(const QString &panel_key);

    std::shared_ptr<Layer> layer_;
    std::shared_ptr<Title> title_;
    bool pending_color_popup_position_valid_ = false;
    QPoint pending_color_popup_position_;
    double playhead_ = 0.0;
    bool loading_values_ = false;
    QTimer *ticker_status_timer_ = nullptr;
    bool numeric_label_dragging_ = false;
    bool text_style_editor_mode_ = false;
    std::string active_text_edit_layer_id_;
    QToolButton     *btn_properties_undo_ = nullptr;
    QToolButton     *btn_properties_redo_ = nullptr;
    std::unordered_map<QAbstractButton *,
                       std::function<std::vector<double>()>>
        keyframe_time_providers_;
    std::string external_gradient_layer_id_;
    int external_gradient_stop_index_ = -1;
    bool external_gradient_stroke_ = false;
    bool external_gradient_inline_ = false;

    QGroupBox       *video_box_    = nullptr;
    QGroupBox       *live_playback_box_ = nullptr;
    QDoubleSpinBox  *spn_video_in_ = nullptr;
    QDoubleSpinBox  *spn_video_out_ = nullptr;
    QComboBox       *cmb_video_playback_ = nullptr;
    QCheckBox       *chk_video_time_remap_ = nullptr;
    QComboBox       *cmb_video_interpolation_ = nullptr;
    QComboBox       *cmb_video_time_remap_audio_ = nullptr;
    QCheckBox       *chk_video_optical_flow_ = nullptr;
    QLabel          *lbl_video_optical_flow_status_ = nullptr;
    QLabel          *lbl_video_loading_status_ = nullptr;
    QProgressBar    *bar_video_loading_ = nullptr;

    QGroupBox       *audio_box_    = nullptr;
    QLineEdit       *edt_audio_source_ = nullptr;
    QWidget         *row_audio_source_ = nullptr;
    QWidget         *row_audio_preview_ = nullptr;
    QWidget         *row_audio_range_ = nullptr;
    QWidget         *row_audio_range_tools_ = nullptr;
    QToolButton     *btn_audio_browse_ = nullptr;
    QToolButton     *btn_audio_clear_ = nullptr;
    QDoubleSpinBox  *spn_audio_in_ = nullptr;
    QDoubleSpinBox  *spn_audio_out_ = nullptr;
    QPushButton     *btn_audio_set_in_ = nullptr;
    QPushButton     *btn_audio_set_out_ = nullptr;
    QLabel          *lbl_audio_range_preview_ = nullptr;
    QLabel          *lbl_audio_range_feedback_ = nullptr;
    QLabel          *lbl_audio_waveform_status_ = nullptr;
    QProgressBar    *bar_audio_waveform_ = nullptr;
    QDoubleSpinBox  *spn_audio_volume_ = nullptr;
    QDoubleSpinBox  *spn_audio_pan_ = nullptr;
    QSlider         *sld_audio_volume_ = nullptr;
    QSlider         *sld_audio_pan_ = nullptr;
    QToolButton     *btn_kf_audio_volume_ = nullptr;
    QToolButton     *btn_kf_audio_pan_ = nullptr;
    QDoubleSpinBox  *spn_audio_fade_in_ = nullptr;
    QDoubleSpinBox  *spn_audio_fade_out_ = nullptr;
    QComboBox       *cmb_audio_fade_curve_ = nullptr;
    QComboBox       *cmb_audio_playback_ = nullptr;
    QCheckBox       *chk_audio_mute_ = nullptr;
    QCheckBox       *chk_audio_solo_ = nullptr;
    QCheckBox       *chk_audio_loop_ = nullptr;
    QCheckBox       *chk_audio_independent_ = nullptr;

    QGroupBox       *asset_box_    = nullptr;
    QLabel          *lbl_asset_name_ = nullptr;
    QComboBox       *cmb_asset_playback_ = nullptr;
    QDoubleSpinBox  *spn_asset_offset_ = nullptr;
    TimecodeSpinBox *spn_asset_pause_duration_ = nullptr;
    QSpinBox        *spn_asset_loop_count_ = nullptr;
    QCheckBox       *chk_asset_loop_ = nullptr;
    QPushButton     *btn_asset_overrides_ = nullptr;

    QGroupBox       *text_box_     = nullptr;
    QGroupBox       *text_animators_box_ = nullptr;
    QListWidget     *lst_text_animators_ = nullptr;
    QTreeWidget     *tree_text_animator_items_ = nullptr;
    QLineEdit       *edt_text_animator_name_ = nullptr;
    QCheckBox       *chk_text_animator_enabled_ = nullptr;
    QCheckBox       *chk_text_animator_expanded_ = nullptr;
    QCheckBox       *chk_text_animator_transform_as_unit_ = nullptr;
    QComboBox       *cmb_text_animator_blend_ = nullptr;
    QComboBox       *cmb_text_animator_granularity_ = nullptr;
    QComboBox       *cmb_text_animator_change_behaviour_ = nullptr;
    QPushButton     *btn_text_animator_add_ = nullptr;
    QPushButton     *btn_text_animator_duplicate_ = nullptr;
    QPushButton     *btn_text_animator_delete_ = nullptr;
    QPushButton     *btn_text_animator_up_ = nullptr;
    QPushButton     *btn_text_animator_down_ = nullptr;
    QPushButton     *btn_text_animator_add_property_ = nullptr;
    QPushButton     *btn_text_animator_add_selector_ = nullptr;
    QPushButton     *btn_text_animator_delete_item_ = nullptr;
    QPushButton     *btn_text_animator_item_up_ = nullptr;
    QPushButton     *btn_text_animator_item_down_ = nullptr;
    QPushButton     *btn_text_animator_apply_preset_ = nullptr;
    QPushButton     *btn_text_animator_save_preset_ = nullptr;
    QWidget         *text_animator_item_editor_host_ = nullptr;
    QWidget         *text_animator_item_editor_ = nullptr;
    QGroupBox       *type_options_box_ = nullptr;
    QGroupBox       *chart_box_ = nullptr;
    QGroupBox       *chart_data_box_ = nullptr;
    QGroupBox       *chart_axes_box_ = nullptr;
    QGroupBox       *chart_labels_box_ = nullptr;
    QGroupBox       *chart_appearance_box_ = nullptr;
    QGroupBox       *chart_animation_box_ = nullptr;
    QComboBox       *cmb_chart_type_ = nullptr;
    QComboBox       *cmb_chart_orientation_ = nullptr;
    QComboBox       *cmb_chart_data_mode_ = nullptr;
    QComboBox       *cmb_chart_source_ = nullptr;
    QToolButton     *btn_chart_data_settings_ = nullptr;
    QComboBox       *cmb_chart_table_ = nullptr;
    QComboBox       *cmb_chart_category_ = nullptr;
    QComboBox       *cmb_chart_series_field_ = nullptr;
    QComboBox       *cmb_chart_secondary_field_ = nullptr;
    QComboBox       *cmb_chart_metadata_field_ = nullptr;
    QComboBox       *cmb_chart_color_field_ = nullptr;
    QSpinBox        *spn_chart_start_row_ = nullptr;
    QSpinBox        *spn_chart_maximum_rows_ = nullptr;
    QCheckBox       *chk_chart_ignore_empty_ = nullptr;
    QTableWidget    *tbl_chart_series_ = nullptr;
    QToolButton     *btn_chart_add_series_ = nullptr;
    QToolButton     *btn_chart_remove_series_ = nullptr;
    QTableWidget    *tbl_chart_values_ = nullptr;
    QToolButton     *btn_chart_add_value_ = nullptr;
    QToolButton     *btn_chart_remove_value_ = nullptr;
    QWidget         *row_chart_series_ = nullptr;
    QWidget         *row_chart_source_ = nullptr;
    QWidget         *row_chart_values_ = nullptr;
    QWidget         *row_chart_bar_width_ = nullptr;
    QWidget         *row_chart_bar_spacing_ = nullptr;
    QWidget         *row_chart_corner_radius_ = nullptr;
    QWidget         *row_chart_line_thickness_ = nullptr;
    QCheckBox       *chk_chart_grid_ = nullptr;
    QCheckBox       *chk_chart_axes_ = nullptr;
    QCheckBox       *chk_chart_axis_labels_ = nullptr;
    QCheckBox       *chk_chart_data_labels_ = nullptr;
    QCheckBox       *chk_chart_legend_ = nullptr;
    QCheckBox       *chk_chart_background_ = nullptr;
    QCheckBox       *chk_chart_fill_ = nullptr;
    QCheckBox       *chk_chart_outline_ = nullptr;
    QCheckBox       *chk_chart_thousands_ = nullptr;
    QLineEdit       *edt_chart_font_family_ = nullptr;
    QLineEdit       *edt_chart_number_prefix_ = nullptr;
    QLineEdit       *edt_chart_number_suffix_ = nullptr;
    QPushButton     *btn_chart_background_color_ = nullptr;
    QPushButton     *btn_chart_grid_color_ = nullptr;
    QPushButton     *btn_chart_axis_color_ = nullptr;
    QPushButton     *btn_chart_label_color_ = nullptr;
    QComboBox       *cmb_chart_fill_style_ = nullptr;
    QPushButton     *btn_chart_gradient_ = nullptr;
    QSpinBox        *spn_chart_precision_ = nullptr;
    QDoubleSpinBox  *spn_chart_transition_ = nullptr;
    QDoubleSpinBox  *spn_chart_animation_ = nullptr;
    QDoubleSpinBox  *spn_chart_reveal_ = nullptr;
    QDoubleSpinBox  *spn_chart_value_transition_ = nullptr;
    QDoubleSpinBox  *spn_chart_label_opacity_ = nullptr;
    QDoubleSpinBox  *spn_chart_element_opacity_ = nullptr;
    QDoubleSpinBox  *spn_chart_font_size_ = nullptr;
    QDoubleSpinBox  *spn_chart_line_thickness_ = nullptr;
    QDoubleSpinBox  *spn_chart_bar_width_ = nullptr;
    QDoubleSpinBox  *spn_chart_bar_spacing_ = nullptr;
    QDoubleSpinBox  *spn_chart_corner_radius_ = nullptr;
    QDoubleSpinBox  *spn_chart_padding_ = nullptr;
    QDoubleSpinBox  *spn_chart_margin_left_ = nullptr;
    QDoubleSpinBox  *spn_chart_margin_top_ = nullptr;
    QDoubleSpinBox  *spn_chart_margin_right_ = nullptr;
    QDoubleSpinBox  *spn_chart_margin_bottom_ = nullptr;
    QPushButton     *btn_kf_chart_animation_ = nullptr;
    QPushButton     *btn_kf_chart_reveal_ = nullptr;
    QPushButton     *btn_kf_chart_value_transition_ = nullptr;
    QPushButton     *btn_kf_chart_label_opacity_ = nullptr;
    QPushButton     *btn_kf_chart_element_opacity_ = nullptr;
    QGroupBox       *paragraph_box_ = nullptr;
    QGroupBox       *dynamic_text_box_ = nullptr;
    QGroupBox       *live_edit_box_ = nullptr;
    QGroupBox       *bullets_box_ = nullptr;
    QGroupBox       *rect_box_     = nullptr;
    QWidget         *image_box_    = nullptr;
    QWidget         *image_box_size_box_ = nullptr;

    /* Text controls */
    QTextEdit       *txt_content_  = nullptr;
    QToolButton     *btn_text_external_binding_ = nullptr;
    QComboBox       *cmb_clock_preset_ = nullptr;
    QComboBox       *cmb_font_     = nullptr;
    QComboBox       *cmb_font_style_ = nullptr;
    QSpinBox        *spn_size_     = nullptr;
    QToolButton     *chk_bold_     = nullptr;
    QToolButton     *chk_italic_   = nullptr;
    QToolButton     *chk_font_kerning_ = nullptr;
    QComboBox       *cmb_kerning_mode_ = nullptr;
    QDoubleSpinBox  *spn_kerning_value_ = nullptr;
    QDoubleSpinBox  *spn_text_leading_ = nullptr;
    QDoubleSpinBox  *spn_char_tracking_ = nullptr;
    QDoubleSpinBox  *spn_char_scale_x_ = nullptr;
    QDoubleSpinBox  *spn_char_scale_y_ = nullptr;
    QDoubleSpinBox  *spn_baseline_shift_ = nullptr;
    QComboBox       *cmb_text_style_ = nullptr;
    QWidget         *row_text_color_ = nullptr;
    QToolButton     *btn_all_caps_ = nullptr;
    QToolButton     *btn_small_caps_ = nullptr;
    QToolButton     *btn_superscript_ = nullptr;
    QToolButton     *btn_subscript_ = nullptr;
    QToolButton     *btn_underline_ = nullptr;
    QToolButton     *btn_strikethrough_ = nullptr;
    QToolButton     *btn_ligatures_ = nullptr;
    QToolButton     *btn_stylistic_alternates_ = nullptr;
    QToolButton     *btn_fractions_ = nullptr;
    QToolButton     *btn_opentype_features_ = nullptr;
    QComboBox       *cmb_text_overflow_ = nullptr;
    QDoubleSpinBox  *spn_text_fit_min_scale_ = nullptr;
    QWidget         *row_ticker_playback_ = nullptr;
    QToolButton     *btn_ticker_pause_ = nullptr;
    QComboBox       *cmb_ticker_playback_mode_ = nullptr;
    QWidget         *row_ticker_completion_ = nullptr;
    QDoubleSpinBox  *spn_ticker_completion_ = nullptr;
    QPushButton     *btn_kf_ticker_completion_ = nullptr;
    QComboBox       *cmb_ticker_style_ = nullptr;
    QDoubleSpinBox  *spn_ticker_speed_ = nullptr;
    QDoubleSpinBox  *spn_ticker_line_hold_ = nullptr;
    QComboBox       *cmb_ticker_direction_ = nullptr;
    QLabel          *lbl_text_fit_scale_ = nullptr;
    QCheckBox       *chk_text_box_width_to_text_ = nullptr;
    QCheckBox       *chk_text_box_height_to_text_ = nullptr;
    QDoubleSpinBox  *spn_max_text_box_width_ = nullptr;
    QDoubleSpinBox  *spn_max_text_box_height_ = nullptr;
    QCheckBox       *chk_expose_text_ = nullptr;
    QCheckBox       *chk_exposed_hide_if_empty_ = nullptr;
    QCheckBox       *chk_exposed_single_value_ = nullptr;
    QCheckBox       *chk_expose_fill_ = nullptr;
    QCheckBox       *chk_exposed_fill_single_value_ = nullptr;
    QCheckBox       *chk_expose_stroke_ = nullptr;
    QCheckBox       *chk_exposed_stroke_single_value_ = nullptr;
    QCheckBox       *chk_ignore_persistence_ = nullptr;
    QButtonGroup    *grp_text_align_ = nullptr;
    QButtonGroup    *grp_text_valign_ = nullptr;
    QDoubleSpinBox  *spn_paragraph_indent_left_ = nullptr;
    QDoubleSpinBox  *spn_paragraph_indent_right_ = nullptr;
    QDoubleSpinBox  *spn_paragraph_indent_first_line_ = nullptr;
    QDoubleSpinBox  *spn_paragraph_space_before_ = nullptr;
    QDoubleSpinBox  *spn_paragraph_space_after_ = nullptr;
    QCheckBox       *chk_paragraph_hyphenate_ = nullptr;
    QPushButton     *btn_text_color_ = nullptr;
    QGroupBox       *auto_style_box_ = nullptr;
    QCheckBox       *chk_auto_style_enabled_ = nullptr;
    QComboBox       *cmb_auto_default_style_ = nullptr;
    QListWidget     *lst_auto_style_rules_ = nullptr;
    QDialog         *auto_rules_dialog_ = nullptr;
    QListWidget     *lst_auto_rules_dialog_ = nullptr;
    QTextEdit       *txt_auto_rules_demo_ = nullptr;
    QLabel          *lbl_auto_rules_demo_status_ = nullptr;
    QPushButton     *btn_auto_rules_edit_ = nullptr;
    QPushButton     *btn_auto_rules_clear_ = nullptr;
    QComboBox       *cmb_auto_rule_style_ = nullptr;
    QLineEdit       *edt_auto_rule_name_ = nullptr;
    QCheckBox       *chk_auto_rule_enabled_ = nullptr;
    QComboBox       *cmb_auto_rule_start_condition_ = nullptr;
    QSpinBox        *spn_auto_rule_start_offset_ = nullptr;
    QLineEdit       *edt_auto_rule_start_chars_ = nullptr;
    QComboBox       *cmb_auto_rule_end_condition_ = nullptr;
    QSpinBox        *spn_auto_rule_end_offset_ = nullptr;
    QLineEdit       *edt_auto_rule_end_chars_ = nullptr;
    QComboBox       *cmb_auto_rule_conflict_mode_ = nullptr;
    QComboBox       *cmb_auto_rule_match_mode_ = nullptr;
    QComboBox       *cmb_auto_rule_generalization_ = nullptr;
    QCheckBox       *chk_auto_rule_prevent_duplicates_ = nullptr;
    QCheckBox       *chk_auto_rule_allow_multiple_cases_ = nullptr;
    QLineEdit       *edt_auto_rule_excludes_ = nullptr;
    QCheckBox       *chk_auto_rule_stop_processing_ = nullptr;
    QCheckBox       *chk_auto_rule_require_stop_match_ = nullptr;
    QCheckBox       *chk_auto_rule_include_start_marker_ = nullptr;
    QCheckBox       *chk_auto_rule_include_end_marker_ = nullptr;
    QSpinBox        *spn_auto_rule_chars_ = nullptr; // legacy hidden fallback
    QPushButton     *btn_auto_learn_formatting_ = nullptr;
    QPushButton     *btn_auto_rules_load_ = nullptr;
    QPushButton     *btn_auto_rules_save_ = nullptr;
    QPushButton     *btn_auto_rules_save_as_ = nullptr;
    QLabel          *lbl_auto_rules_status_ = nullptr;
    QGroupBox       *auto_rule_editor_box_ = nullptr;
    QWidget         *auto_rule_advanced_widget_ = nullptr;
    QToolButton     *btn_auto_rule_advanced_ = nullptr;
    QString          auto_style_rules_file_path_;
    QPushButton     *btn_auto_rule_add_ = nullptr;
    QPushButton     *btn_auto_rule_update_ = nullptr;
    QPushButton     *btn_auto_rule_delete_ = nullptr;
    QPushButton     *btn_auto_rules_clear_dialog_ = nullptr;
    QPushButton     *btn_auto_rule_up_ = nullptr;
    QPushButton     *btn_auto_rule_down_ = nullptr;

    /* Text/shape outline controls */
    QGroupBox       *outline_box_ = nullptr;
    QCheckBox       *chk_outline_enabled_ = nullptr;
    QComboBox       *cmb_stroke_fill_type_ = nullptr;
    QDoubleSpinBox  *spn_outline_width_ = nullptr;
    QPushButton     *btn_outline_color_ = nullptr;
    QWidget         *row_outline_color_ = nullptr;
    QDoubleSpinBox  *spn_outline_opacity_ = nullptr;
    QComboBox       *cmb_outline_join_ = nullptr;
    QComboBox       *cmb_outline_position_ = nullptr;
    QCheckBox       *chk_outline_antialias_ = nullptr;
    QComboBox       *cmb_stroke_gradient_type_ = nullptr;
    QPushButton     *btn_stroke_gradient_start_color_ = nullptr;
    QPushButton     *btn_stroke_gradient_end_color_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_start_pos_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_end_pos_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_start_opacity_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_end_opacity_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_opacity_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_angle_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_center_x_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_center_y_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_scale_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_focal_x_ = nullptr;
    QDoubleSpinBox  *spn_stroke_gradient_focal_y_ = nullptr;

    /* Rectangle/Image geometry controls */
    QDoubleSpinBox  *spn_layer_w_   = nullptr;
    QDoubleSpinBox  *spn_layer_h_   = nullptr;
    QDoubleSpinBox  *spn_rect_corner_tl_ = nullptr;
    QDoubleSpinBox  *spn_rect_corner_tr_ = nullptr;
    QDoubleSpinBox  *spn_rect_corner_br_ = nullptr;
    QDoubleSpinBox  *spn_rect_corner_bl_ = nullptr;
    QCheckBox       *chk_corner_lock_ = nullptr;
    QWidget         *row_rect_corners_ = nullptr;
    QDoubleSpinBox  *spn_corner_bevel_roundness_ = nullptr;
    QComboBox       *cmb_shape_type_ = nullptr;
    QButtonGroup    *grp_shape_type_ = nullptr;
    QToolButton     *btn_shape_defaults_ = nullptr;
    QCheckBox       *chk_size_lock_ = nullptr;
    QSpinBox        *spn_shape_points_ = nullptr;
    QSpinBox        *spn_shape_sides_ = nullptr;
    QDoubleSpinBox  *spn_shape_inner_radius_ = nullptr;
    QDoubleSpinBox  *spn_shape_outer_radius_ = nullptr;
    QDoubleSpinBox  *spn_shape_roundness_ = nullptr;
    QPushButton     *btn_fill_color_ = nullptr;
    QWidget         *row_fill_color_ = nullptr;
    QComboBox       *cmb_fill_type_ = nullptr;
    QWidget         *row_fill_type_ = nullptr;
    QGroupBox       *gradient_box_ = nullptr;
    QComboBox       *cmb_gradient_type_ = nullptr;
    QComboBox       *cmb_gradient_spread_ = nullptr;
    QPushButton     *btn_gradient_start_color_ = nullptr;
    QPushButton     *btn_gradient_end_color_ = nullptr;
    QDoubleSpinBox  *spn_gradient_start_pos_ = nullptr;
    QDoubleSpinBox  *spn_gradient_end_pos_ = nullptr;
    QDoubleSpinBox  *spn_gradient_start_opacity_ = nullptr;
    QDoubleSpinBox  *spn_gradient_end_opacity_ = nullptr;
    QDoubleSpinBox  *spn_gradient_opacity_ = nullptr;
    QDoubleSpinBox  *spn_gradient_angle_ = nullptr;
    QDoubleSpinBox  *spn_gradient_center_x_ = nullptr;
    QDoubleSpinBox  *spn_gradient_center_y_ = nullptr;
    QDoubleSpinBox  *spn_gradient_scale_ = nullptr;
    QDoubleSpinBox  *spn_gradient_focal_x_ = nullptr;
    QDoubleSpinBox  *spn_gradient_focal_y_ = nullptr;

    /* Text/image background controls */
    QGroupBox       *background_gradient_box_ = nullptr;
    QComboBox       *cmb_background_gradient_type_ = nullptr;
    QPushButton     *btn_background_gradient_start_color_ = nullptr;
    QPushButton     *btn_background_gradient_end_color_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_start_pos_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_end_pos_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_start_opacity_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_end_opacity_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_opacity_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_angle_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_center_x_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_center_y_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_scale_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_focal_x_ = nullptr;
    QDoubleSpinBox  *spn_background_gradient_focal_y_ = nullptr;

    /* Image controls */
    QLabel          *lbl_image_preview_ = nullptr;
    QLineEdit       *edit_image_path_ = nullptr;
    QToolButton     *btn_image_external_binding_ = nullptr;
    QPushButton     *btn_pick_image_ = nullptr;
    QToolButton     *btn_clear_image_ = nullptr;
    QComboBox       *cmb_image_scale_filter_ = nullptr;
    QComboBox       *cmb_image_box_mode_ = nullptr;
    QCheckBox       *chk_image_crop_when_outside_box_ = nullptr;
    QPushButton     *btn_image_anchor_grid_ = nullptr;
    QDoubleSpinBox  *spn_image_box_w_ = nullptr;
    QDoubleSpinBox  *spn_image_box_h_ = nullptr;
    QCheckBox       *chk_image_box_size_lock_ = nullptr;
    QPushButton     *btn_kf_image_box_size_ = nullptr;

    /* Transform controls (static) */
    QDoubleSpinBox  *spn_px_       = nullptr;
    QDoubleSpinBox  *spn_py_       = nullptr;
    QDoubleSpinBox  *spn_scale_x_  = nullptr;
    QDoubleSpinBox  *spn_scale_y_  = nullptr;
    QDoubleSpinBox  *spn_transform_size_w_ = nullptr;
    QDoubleSpinBox  *spn_transform_size_h_ = nullptr;
    QDoubleSpinBox  *spn_rot_      = nullptr;
    QDoubleSpinBox  *spn_opacity_  = nullptr;
    QCheckBox       *chk_scene_mask_ = nullptr;
    QDoubleSpinBox  *spn_origin_x_ = nullptr;
    QDoubleSpinBox  *spn_origin_y_ = nullptr;
    QCheckBox       *chk_scale_lock_ = nullptr;
    QCheckBox       *chk_transform_size_lock_ = nullptr;
    QCheckBox       *chk_shape_scale_stroke_ = nullptr;
    QCheckBox       *chk_shape_scale_corners_ = nullptr;
    QComboBox       *cmb_anchor_ = nullptr;
    QComboBox       *cmb_dimension_mode_ = nullptr;
    QComboBox       *cmb_transform_axis_space_ = nullptr;
    QComboBox       *cmb_layer_camera_ = nullptr;
    QComboBox       *cmb_depth_mode_ = nullptr;
    QWidget         *three_d_controls_ = nullptr;
    QWidget         *light_options_box_ = nullptr;
    QCheckBox       *chk_light_enabled_ = nullptr;
    QComboBox       *cmb_light_type_ = nullptr;
    QPushButton     *btn_light_color_ = nullptr;
    QDoubleSpinBox  *spn_light_intensity_ = nullptr;
    QDoubleSpinBox  *spn_light_source_size_ = nullptr;
    QComboBox       *cmb_light_falloff_ = nullptr;
    QDoubleSpinBox  *spn_light_falloff_start_ = nullptr;
    QDoubleSpinBox  *spn_light_falloff_distance_ = nullptr;
    QDoubleSpinBox  *spn_light_cone_angle_ = nullptr;
    QDoubleSpinBox  *spn_light_cone_feather_ = nullptr;
    QDoubleSpinBox  *spn_light_target_x_ = nullptr;
    QDoubleSpinBox  *spn_light_target_y_ = nullptr;
    QDoubleSpinBox  *spn_light_target_z_ = nullptr;
    QWidget         *row_light_target_ = nullptr;
    QCheckBox       *chk_light_casts_shadows_ = nullptr;
    QDoubleSpinBox  *spn_light_shadow_darkness_ = nullptr;
    QDoubleSpinBox  *spn_light_shadow_softness_ = nullptr;
    QDoubleSpinBox  *spn_light_shadow_bias_ = nullptr;
    QWidget         *material_options_box_ = nullptr;
    QWidget         *geometry_options_box_ = nullptr;
    QDoubleSpinBox  *spn_pz_ = nullptr;
    QDoubleSpinBox  *spn_rot_x_ = nullptr;
    QDoubleSpinBox  *spn_rot_y_ = nullptr;
    QDoubleSpinBox  *spn_scale_z_ = nullptr;
    QDoubleSpinBox  *spn_anchor_z_ = nullptr;
    QDoubleSpinBox  *spn_orientation_x_ = nullptr;
    QDoubleSpinBox  *spn_orientation_y_ = nullptr;
    QDoubleSpinBox  *spn_orientation_z_ = nullptr;
    QCheckBox       *chk_depth_test_ = nullptr;
    QCheckBox       *chk_write_depth_ = nullptr;
    QCheckBox       *chk_double_sided_ = nullptr;
    QCheckBox       *chk_backface_culling_ = nullptr;
    QCheckBox       *chk_material_accepts_lights_ = nullptr;
    QCheckBox       *chk_material_casts_shadows_ = nullptr;
    QCheckBox       *chk_material_accepts_shadows_ = nullptr;
    QCheckBox       *chk_material_reflections_ = nullptr;
    QDoubleSpinBox  *spn_material_ambient_ = nullptr;
    QDoubleSpinBox  *spn_material_diffuse_ = nullptr;
    QDoubleSpinBox  *spn_material_specular_ = nullptr;
    QDoubleSpinBox  *spn_material_shininess_ = nullptr;
    QDoubleSpinBox  *spn_material_metallic_ = nullptr;
    QDoubleSpinBox  *spn_material_roughness_ = nullptr;
    QDoubleSpinBox  *spn_material_reflection_ = nullptr;
    QPushButton     *btn_material_emissive_color_ = nullptr;
    QDoubleSpinBox  *spn_material_emissive_intensity_ = nullptr;
    QPushButton     *btn_kf_material_ambient_ = nullptr;
    QPushButton     *btn_kf_material_diffuse_ = nullptr;
    QPushButton     *btn_kf_material_specular_ = nullptr;
    QPushButton     *btn_kf_material_shininess_ = nullptr;
    QPushButton     *btn_kf_material_metallic_ = nullptr;
    QPushButton     *btn_kf_material_roughness_ = nullptr;
    QPushButton     *btn_kf_material_reflection_ = nullptr;
    QPushButton     *btn_kf_material_emissive_color_ = nullptr;
    QPushButton     *btn_kf_material_emissive_intensity_ = nullptr;
    QWidget         *appearance_emissive_color_label_ = nullptr;
    QWidget         *appearance_emissive_intensity_label_ = nullptr;
    QCheckBox       *chk_geometry_extrusion_ = nullptr;
    QDoubleSpinBox  *spn_geometry_extrusion_depth_ = nullptr;
    QDoubleSpinBox  *spn_geometry_bevel_depth_ = nullptr;
    QSpinBox        *spn_geometry_bevel_segments_ = nullptr;
    QSpinBox        *spn_geometry_extrusion_segments_ = nullptr;
    QCheckBox       *chk_geometry_bevel_front_ = nullptr;
    QCheckBox       *chk_geometry_bevel_back_ = nullptr;
    QPushButton     *btn_kf_geometry_extrusion_depth_ = nullptr;
    QPushButton     *btn_kf_geometry_bevel_depth_ = nullptr;
    QWidget         *transform_box_ = nullptr;
    QGridLayout      *transform_grid_ = nullptr;
    QWidget         *transform_scale_label_ = nullptr;
    QWidget         *transform_scale_field_x_ = nullptr;
    QWidget         *transform_scale_field_y_ = nullptr;
    QLabel          *transform_scale_axis_label_x_ = nullptr;
    QLabel          *transform_scale_axis_label_y_ = nullptr;
    QWidget         *transform_position_field_z_ = nullptr;
    QWidget         *transform_scale_field_z_ = nullptr;
    QWidget         *transform_anchor_field_z_ = nullptr;
    QWidget         *transform_rotation_field_x_ = nullptr;
    QWidget         *transform_rotation_field_y_ = nullptr;
    QWidget         *transform_rotation_field_z_ = nullptr;
    QLabel          *transform_rotation_axis_label_ = nullptr;
    QWidget         *transform_orientation_row_ = nullptr;
    QWidget         *transform_size_label_ = nullptr;
    QWidget         *transform_size_field_w_ = nullptr;
    QWidget         *transform_size_field_h_ = nullptr;
    QWidget         *row_shape_scale_options_ = nullptr;
    QPushButton     *btn_kf_transform_size_ = nullptr;
    QWidget         *shape_size_label_ = nullptr;
    QWidget         *shape_size_field_w_ = nullptr;
    QWidget         *shape_size_field_h_ = nullptr;
    QWidget         *appearance_box_ = nullptr;
    QPushButton     *btn_appearance_fill_color_ = nullptr;
    QPushButton     *btn_appearance_stroke_color_ = nullptr;
    QLabel          *btn_appearance_stroke_label_ = nullptr;
    QDoubleSpinBox  *spn_appearance_stroke_width_ = nullptr;
    QDoubleSpinBox  *spn_appearance_opacity_ = nullptr;
    QPushButton     *btn_kf_appearance_fill_ = nullptr;
    QPushButton     *btn_kf_appearance_stroke_ = nullptr;
    QPushButton     *btn_kf_appearance_opacity_ = nullptr;
    QPushButton     *btn_anchor_grid_ = nullptr;
    QToolButton     *btn_transform_defaults_ = nullptr;
    QGroupBox       *shadow_box_ = nullptr;
    QCheckBox       *chk_shadow_enabled_ = nullptr;
    QComboBox       *cmb_shadow_preset_ = nullptr;
    QComboBox       *cmb_shadow_blur_type_ = nullptr;
    QPushButton     *btn_shadow_color_ = nullptr;
    QDoubleSpinBox  *spn_shadow_opacity_ = nullptr;
    QDoubleSpinBox  *spn_shadow_distance_ = nullptr;
    QDoubleSpinBox  *spn_shadow_angle_ = nullptr;
    QDoubleSpinBox  *spn_shadow_blur_ = nullptr;
    QDoubleSpinBox  *spn_shadow_spread_ = nullptr;
    QCheckBox       *chk_long_shadow_enabled_ = nullptr;
    QPushButton     *btn_long_shadow_color_ = nullptr;
    QDoubleSpinBox  *spn_long_shadow_opacity_ = nullptr;
    QDoubleSpinBox  *spn_long_shadow_length_ = nullptr;
    QDoubleSpinBox  *spn_long_shadow_angle_ = nullptr;
    QDoubleSpinBox  *spn_long_shadow_falloff_ = nullptr;
    QComboBox       *cmb_long_shadow_blur_type_ = nullptr;
    QDoubleSpinBox  *spn_long_shadow_blur_ = nullptr;
    QPushButton     *btn_kf_shadow_enabled_ = nullptr;
    QPushButton     *btn_kf_shadow_color_ = nullptr;
    QPushButton     *btn_kf_shadow_opacity_ = nullptr;
    QPushButton     *btn_kf_shadow_distance_ = nullptr;
    QPushButton     *btn_kf_shadow_angle_ = nullptr;
    QPushButton     *btn_kf_shadow_blur_ = nullptr;
    QPushButton     *btn_kf_shadow_spread_ = nullptr;
    QPushButton     *btn_kf_pos_x_ = nullptr;
    QPushButton     *btn_kf_pos_y_ = nullptr;
    QPushButton     *btn_kf_scale_x_ = nullptr;
    QPushButton     *btn_kf_scale_y_ = nullptr;
    QPushButton     *btn_kf_rotation_ = nullptr;
    QPushButton     *btn_kf_opacity_ = nullptr;
    QPushButton     *btn_kf_origin_x_ = nullptr;
    QPushButton     *btn_kf_origin_y_ = nullptr;
    QPushButton     *btn_kf_position_z_ = nullptr;
    QPushButton     *btn_kf_rotation_x_ = nullptr;
    QPushButton     *btn_kf_rotation_y_ = nullptr;
    QPushButton     *btn_kf_scale_z_ = nullptr;
    QPushButton     *btn_kf_anchor_z_ = nullptr;
    QPushButton     *btn_kf_orientation_x_ = nullptr;
    QPushButton     *btn_kf_orientation_y_ = nullptr;
    QPushButton     *btn_kf_orientation_z_ = nullptr;
    QPushButton     *btn_kf_paragraph_indent_left_ = nullptr;
    QPushButton     *btn_kf_paragraph_indent_right_ = nullptr;
    QPushButton     *btn_kf_paragraph_indent_first_line_ = nullptr;
    QPushButton     *btn_kf_font_size_ = nullptr;
    QPushButton     *btn_kf_char_scale_x_ = nullptr;
    QPushButton     *btn_kf_char_scale_y_ = nullptr;
    QPushButton     *btn_kf_char_tracking_ = nullptr;
    QPushButton     *btn_kf_baseline_shift_ = nullptr;
    QPushButton     *btn_kf_paragraph_space_before_ = nullptr;
    QPushButton     *btn_kf_paragraph_space_after_ = nullptr;
    QPushButton     *btn_kf_width_ = nullptr;
    QPushButton     *btn_kf_text_color_ = nullptr;
    QPushButton     *btn_kf_fill_color_ = nullptr;
};
