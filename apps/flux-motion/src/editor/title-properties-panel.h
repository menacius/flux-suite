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
#include <QStackedWidget>
#include <QStringList>
#include <QPointF>
#include <QPoint>
#include <QRectF>
#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QAbstractButton>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <set>
#include <unordered_map>

class QEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class QContextMenuEvent;
class QResizeEvent;
class QPaintEvent;
class QPainter;
class QScrollBar;
class FxmCollapsiblePanel;
/* ══════════════════════════════════════════════════════════════════
 *  TitlePropertiesPanel – global title inspector
 * ══════════════════════════════════════════════════════════════════ */
class TitlePropertiesPanel : public QGroupBox {
    Q_OBJECT

public:
    explicit TitlePropertiesPanel(QWidget *parent = nullptr);
    void set_title(std::shared_ptr<Title> t);
    void set_playhead(double timeline_time);
    void set_document_format_editable(bool editable);
    QWidget *scene_controls_widget() const { return scene_controls_; }

signals:
    void title_changed(bool push_undo_snapshot = true);
    void document_format_changed(bool canvas_size_changed);
    void stinger_structure_changed();
    void stinger_editor_preview_changed();
    void keyframe_navigation_requested(double timeline_time);

protected:
    bool event(QEvent *event) override;

private:
    void apply_theme_style();
    void load_values();
    void update_stinger_validation();
    TitleCamera *current_camera();
    const TitleCamera *current_camera() const;
    std::string unique_camera_name(const std::string &base) const;
    void insert_camera_copy(const TitleCamera &source);
    TitleLight *current_light();
    const TitleLight *current_light() const;
    std::string unique_light_name(const std::string &base) const;
    QWidget *make_keyframe_controls(QAbstractButton *button, QWidget *parent);
    void register_keyframe_times(
        QAbstractButton *button,
        std::function<std::vector<double>()> provider);

    std::shared_ptr<Title> title_;
    bool loading_values_ = false;
    bool numeric_label_dragging_ = false;
    bool applying_theme_style_ = false;
    double playhead_ = 0.0;
    TitleGraphicType previous_non_stinger_graphic_type_ = TitleGraphicType::Title;
    QButtonGroup   *grp_playback_mode_ = nullptr;
    QWidget        *loop_area_row_ = nullptr;
    QComboBox      *cmb_cue_end_behavior_ = nullptr;
    QDoubleSpinBox *spn_pause_frame_ = nullptr;
    QDoubleSpinBox *spn_duration_ = nullptr;
    QWidget        *resolution_row_ = nullptr;
    QSpinBox       *spn_width_ = nullptr;
    QSpinBox       *spn_height_ = nullptr;
    QDoubleSpinBox *spn_frame_rate_ = nullptr;
    QDoubleSpinBox *spn_loop_start_ = nullptr;
    QDoubleSpinBox *spn_loop_end_ = nullptr;

    QWidget        *scene_controls_ = nullptr;
    QWidget        *camera_box_ = nullptr;
    FxmCollapsiblePanel *camera_panel_ = nullptr;
    QComboBox      *cmb_camera_ = nullptr;
    QPushButton    *btn_camera_add_ = nullptr;
    QPushButton    *btn_camera_delete_ = nullptr;
    QToolButton    *btn_camera_actions_ = nullptr;
    QAction        *act_camera_duplicate_ = nullptr;
    QAction        *act_camera_copy_ = nullptr;
    QAction        *act_camera_paste_ = nullptr;
    std::unique_ptr<TitleCamera> camera_clipboard_;
    QCheckBox      *chk_camera_canvas_default_ = nullptr;
    QComboBox      *cmb_camera_projection_ = nullptr;
    QToolButton    *btn_kf_camera_projection_ = nullptr;
    QToolButton    *btn_kf_camera_position_ = nullptr;
    QToolButton    *btn_kf_camera_target_ = nullptr;
    QToolButton    *btn_kf_camera_orientation_ = nullptr;
    QToolButton    *btn_kf_camera_rotation_ = nullptr;
    QToolButton    *btn_kf_camera_focal_ = nullptr;
    QToolButton    *btn_kf_camera_fov_ = nullptr;
    QToolButton    *btn_kf_camera_zoom_ = nullptr;
    QToolButton    *btn_kf_camera_near_ = nullptr;
    QToolButton    *btn_kf_camera_far_ = nullptr;
    std::unordered_map<QAbstractButton *,
                       std::function<std::vector<double>()>>
        keyframe_time_providers_;
    QDoubleSpinBox *spn_camera_pos_x_ = nullptr;
    QDoubleSpinBox *spn_camera_pos_y_ = nullptr;
    QDoubleSpinBox *spn_camera_pos_z_ = nullptr;
    QDoubleSpinBox *spn_camera_target_x_ = nullptr;
    QDoubleSpinBox *spn_camera_target_y_ = nullptr;
    QDoubleSpinBox *spn_camera_target_z_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_x_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_y_ = nullptr;
    QDoubleSpinBox *spn_camera_orientation_z_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_x_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_y_ = nullptr;
    QDoubleSpinBox *spn_camera_rot_z_ = nullptr;
    QDoubleSpinBox *spn_camera_focal_ = nullptr;
    QDoubleSpinBox *spn_camera_fov_ = nullptr;
    QDoubleSpinBox *spn_camera_zoom_ = nullptr;
    QDoubleSpinBox *spn_camera_near_ = nullptr;
    QDoubleSpinBox *spn_camera_far_ = nullptr;

    QWidget        *light_box_ = nullptr;
    FxmCollapsiblePanel *light_panel_ = nullptr;
    QCheckBox      *chk_lighting_enabled_ = nullptr;
    QCheckBox      *chk_default_light_ = nullptr;
    QDoubleSpinBox *spn_environment_exposure_ = nullptr;
    QComboBox      *cmb_light_ = nullptr;
    QPushButton    *btn_light_add_ = nullptr;
    QPushButton    *btn_light_delete_ = nullptr;
    QCheckBox      *chk_light_enabled_ = nullptr;
    QComboBox      *cmb_light_type_ = nullptr;
    QPushButton    *btn_light_color_ = nullptr;
    QDoubleSpinBox *spn_light_intensity_ = nullptr;
    QDoubleSpinBox *spn_light_source_size_ = nullptr;
    QDoubleSpinBox *spn_light_pos_x_ = nullptr;
    QDoubleSpinBox *spn_light_pos_y_ = nullptr;
    QDoubleSpinBox *spn_light_pos_z_ = nullptr;
    QDoubleSpinBox *spn_light_target_x_ = nullptr;
    QDoubleSpinBox *spn_light_target_y_ = nullptr;
    QDoubleSpinBox *spn_light_target_z_ = nullptr;
    QComboBox      *cmb_light_falloff_ = nullptr;
    QDoubleSpinBox *spn_light_falloff_start_ = nullptr;
    QDoubleSpinBox *spn_light_falloff_distance_ = nullptr;
    QDoubleSpinBox *spn_light_cone_angle_ = nullptr;
    QDoubleSpinBox *spn_light_cone_feather_ = nullptr;
    QCheckBox      *chk_light_casts_shadows_ = nullptr;
    QDoubleSpinBox *spn_light_shadow_darkness_ = nullptr;
    QDoubleSpinBox *spn_light_shadow_softness_ = nullptr;
    QDoubleSpinBox *spn_light_shadow_bias_ = nullptr;

    QComboBox      *cmb_stinger_switch_mode_ = nullptr;
    QDoubleSpinBox *spn_stinger_transition_timecode_ = nullptr;
    QCheckBox      *chk_stinger_audio_ = nullptr;
    QCheckBox      *chk_stinger_alpha_ = nullptr;
    QDoubleSpinBox *spn_stinger_pre_roll_ = nullptr;
    QDoubleSpinBox *spn_stinger_post_roll_ = nullptr;
    QComboBox      *cmb_stinger_render_mode_ = nullptr;
    QLabel         *lbl_stinger_validation_ = nullptr;
};
