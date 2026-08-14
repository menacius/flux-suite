#include "title-properties-panel.h"
#include "fxm-modern-controls.h"
#include "timecode-spinbox.h"
#include "title-animation-edit-utils.h"
#include "title-assets.h"
#include "title-localization.h"
#include <array>
#include <QColorDialog>

using fxm::editor::animation_edit::add_or_replace_keyframe;
using fxm::editor::animation_edit::any_keyframe_at_time;
using fxm::editor::animation_edit::any_keyframes;
using fxm::editor::animation_edit::keyframe_at_time;
using fxm::editor::animation_edit::remove_keyframe_at;
using fxm::editor::animation_edit::set_animated_value;
using fxm::editor::animation_edit::toggle_keyframe;

void TitlePropertiesPanel::register_keyframe_times(
    QAbstractButton *button, std::function<std::vector<double>()> provider)
{
    if (!button)
        return;
    keyframe_time_providers_[button] = std::move(provider);
    fxm_refresh_keyframe_navigation(button);
}

QWidget *TitlePropertiesPanel::make_keyframe_controls(QAbstractButton *button,
                                                       QWidget *parent)
{
    return fxm_make_keyframe_controls(
        button, parent,
        [this, button]() {
            const auto found = keyframe_time_providers_.find(button);
            return found == keyframe_time_providers_.end()
                ? std::vector<double>{} : found->second();
        }, [this]() { return playhead_; },
        [this](double time) { emit keyframe_navigation_requested(time); });
}

TitlePropertiesPanel::TitlePropertiesPanel(QWidget *parent)
    : QGroupBox(parent)
{
    apply_theme_style();

    auto *fl = new QFormLayout(this);
    fl->setContentsMargins(6, 5, 6, 6);
    fl->setHorizontalSpacing(4);
    fl->setVerticalSpacing(2);
    fl->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fl->setFormAlignment(Qt::AlignTop);
    fl->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto add_form_row = [this](QFormLayout *form, const QString &label_text, QWidget *field) {
        auto *label = new NumericDragLabel(label_text, field, form->parentWidget(),
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = true;
                                               emit title_changed(true);
                                           },
                                           [this]() {
                                               if (loading_values_) return;
                                               numeric_label_dragging_ = false;
                                               emit title_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(label, field);
    };

    spn_duration_ = new TimecodeSpinBox(this);
    spn_duration_->setRange(0.1, 3600.0);
    spn_duration_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.LengthLabel"), spn_duration_);

    resolution_row_ = new QWidget(this);
    auto *resolution_layout = new QHBoxLayout(resolution_row_);
    resolution_layout->setContentsMargins(0, 0, 0, 0);
    resolution_layout->setSpacing(4);
    spn_width_ = new QSpinBox(resolution_row_);
    spn_width_->setRange(1, 16384);
    spn_width_->setSuffix(QStringLiteral(" px"));
    spn_width_->setFixedHeight(20);
    spn_height_ = new QSpinBox(resolution_row_);
    spn_height_->setRange(1, 16384);
    spn_height_->setSuffix(QStringLiteral(" px"));
    spn_height_->setFixedHeight(20);
    resolution_layout->addWidget(spn_width_, 1);
    resolution_layout->addWidget(new QLabel(QString::fromUtf8("\xC3\x97"), resolution_row_));
    resolution_layout->addWidget(spn_height_, 1);
    add_form_row(fl, fxm_tr("OBSTitles.ResolutionLabel"), resolution_row_);

    spn_frame_rate_ = new QDoubleSpinBox(this);
    spn_frame_rate_->setRange(1.0, 240.0);
    spn_frame_rate_->setDecimals(3);
    spn_frame_rate_->setSingleStep(1.0);
    spn_frame_rate_->setSuffix(QStringLiteral(" fps"));
    spn_frame_rate_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.FrameRateLabel"), spn_frame_rate_);

    cmb_cue_end_behavior_ = new QComboBox(this);
    cmb_cue_end_behavior_->addItem(fxm_tr("OBSTitles.CueEndShowLastFrame"), 0);
    cmb_cue_end_behavior_->addItem(fxm_tr("OBSTitles.CueEndShowNothing"), 1);
    cmb_cue_end_behavior_->addItem(fxm_tr("OBSTitles.CueEndShowFirstFrame"), 2);
    cmb_cue_end_behavior_->setToolTip(fxm_tr("OBSTitles.CueEndBehaviorTooltip"));
    cmb_cue_end_behavior_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.CueEndBehaviorLabel"), cmb_cue_end_behavior_);

    auto *playback_row = new QWidget(this);
    auto *playback_layout = new QHBoxLayout(playback_row);
    playback_layout->setContentsMargins(0, 0, 0, 0);
    playback_layout->setSpacing(3);
    grp_playback_mode_ = new QButtonGroup(playback_row);
    grp_playback_mode_->setExclusive(true);
    auto add_playback_button = [this, playback_row, playback_layout](int id, const char *icon_name,
                                                                     const QString &text) {
        auto *button = new QToolButton(playback_row);
        button->setCheckable(true);
        button->setIcon(fxm_icon(icon_name));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(text);
        button->setAccessibleName(text);
        button->setFixedSize(30, 24);
        grp_playback_mode_->addButton(button, id);
        playback_layout->addWidget(button);
    };
    add_playback_button(0, "play-once.svg", fxm_tr("OBSTitles.PlayOnce"));
    add_playback_button(1, "restart-loop.svg", fxm_tr("OBSTitles.RestartLoop"));
    add_playback_button(2, "ping-pong-loop.svg", fxm_tr("OBSTitles.PingPongLoop"));
    add_playback_button(3, "pause-at-timeline-position.svg", fxm_tr("OBSTitles.PauseAtTimelinePosition"));
    add_playback_button(4, "stinger.svg", fxm_tr("OBSTitles.GraphicTypeStinger"));
    playback_layout->addStretch(1);
    add_form_row(fl, fxm_tr("OBSTitles.PlaybackModeLabel"), playback_row);

    spn_pause_frame_ = new TimecodeSpinBox(this);
    spn_pause_frame_->setRange(0.0, 3600.0);
    spn_pause_frame_->setToolTip(fxm_tr("OBSTitles.PauseFrameTooltip"));
    spn_pause_frame_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.PauseFrameLabel"), spn_pause_frame_);


    spn_loop_start_ = new TimecodeSpinBox(this);
    spn_loop_start_->setRange(0.0, 3600.0);
    spn_loop_start_->setToolTip(fxm_tr("OBSTitles.LoopStartTooltip"));
    spn_loop_start_->setFixedHeight(20);

    spn_loop_end_ = new TimecodeSpinBox(this);
    spn_loop_end_->setRange(0.0, 3600.0);
    spn_loop_end_->setToolTip(fxm_tr("OBSTitles.LoopEndTooltip"));
    spn_loop_end_->setFixedHeight(20);

    scene_controls_ = new QWidget(this);
    scene_controls_->setObjectName(QStringLiteral("Fxm3DSceneControls"));
    auto *scene_controls_layout = new QVBoxLayout(scene_controls_);
    scene_controls_layout->setContentsMargins(0, 0, 0, 0);
    scene_controls_layout->setSpacing(3);
    scene_controls_layout->setAlignment(Qt::AlignTop);

    loop_area_row_ = new QWidget(this);
    auto *loop_area_layout = new QHBoxLayout(loop_area_row_);
    loop_area_layout->setContentsMargins(0, 0, 0, 0);
    loop_area_layout->setSpacing(4);
    auto *loop_start_label = new QLabel(fxm_tr("OBSTitles.StartLabel"), loop_area_row_);
    auto *loop_end_label = new QLabel(fxm_tr("OBSTitles.EndLabel"), loop_area_row_);
    spn_loop_start_->setMinimumWidth(78);
    spn_loop_end_->setMinimumWidth(78);
    loop_area_layout->addWidget(loop_start_label);
    loop_area_layout->addWidget(spn_loop_start_, 1);
    loop_area_layout->addWidget(loop_end_label);
    loop_area_layout->addWidget(spn_loop_end_, 1);
    add_form_row(fl, fxm_tr("OBSTitles.LoopAreaLabel"), loop_area_row_);

    camera_box_ = new QWidget(this);
    camera_box_->setObjectName(QStringLiteral("FxmTitleCameraContent"));
    auto *camera_form = new QFormLayout(camera_box_);
    camera_form->setContentsMargins(5, 4, 5, 5);
    camera_form->setHorizontalSpacing(4);
    camera_form->setVerticalSpacing(2);
    camera_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto begin_camera_drag = [this]() {
        if (loading_values_) return;
        numeric_label_dragging_ = true;
        emit title_changed(true);
    };
    auto end_camera_drag = [this]() {
        if (loading_values_) return;
        numeric_label_dragging_ = false;
        emit title_changed(true);
    };
    auto make_camera_label = [this, begin_camera_drag, end_camera_drag](
                                 const QString &text, QWidget *target,
                                 QWidget *parent) {
        auto *label = new NumericDragLabel(text, target, parent,
                                           begin_camera_drag,
                                           end_camera_drag);
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(QStringLiteral("font-size:10px;background:transparent;"));
        return label;
    };
    auto add_camera_row = [camera_form, &make_camera_label](
                              const QString &label_text, QWidget *field,
                              QWidget *drag_target = nullptr) {
        if (label_text.isEmpty()) {
            camera_form->addRow(QString(), field);
            return;
        }
        camera_form->addRow(make_camera_label(label_text,
                                               drag_target ? drag_target : field,
                                               camera_form->parentWidget()),
                            field);
    };
    auto make_camera_spin = [this](double minimum, double maximum,
                                   int decimals = 3) {
        auto *spin = new QDoubleSpinBox(camera_box_);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(true);
        spin->setFixedHeight(20);
        spin->setMinimumWidth(66);
        spin->setStyleSheet(QStringLiteral("font-size:10px;padding:0 2px;"));
        return spin;
    };
    auto make_xyz_row = [camera_box_ = camera_box_, &make_camera_label](
                            QDoubleSpinBox *x, QDoubleSpinBox *y,
                            QDoubleSpinBox *z) {
        auto *row = new QWidget(camera_box_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        const std::array<std::pair<const char *, QDoubleSpinBox *>, 3> entries{{
            {"X", x}, {"Y", y}, {"Z", z}
        }};
        for (const auto &entry : entries) {
            auto *label = make_camera_label(QString::fromLatin1(entry.first),
                                            entry.second, row);
            label->setMinimumWidth(9);
            label->setAlignment(Qt::AlignCenter);
            layout->addWidget(label);
            layout->addWidget(entry.second, 1);
        }
        return row;
    };
    auto make_camera_keyframe_button = [this](const QString &property_name) {
        auto *button = new QToolButton(camera_box_);
        button->setIcon(fxm_keyframe_diamond_icon(false));
        button->setToolTip(QStringLiteral("Toggle %1 keyframe at current time")
                               .arg(property_name));
        button->setAccessibleName(button->toolTip());
        button->setProperty("active", false);
        button->setProperty("outlined", false);
        fxm_style_keyframe_button(button);
        return button;
    };
    auto make_camera_keyframed_row = [this](QWidget *field,
                                             QToolButton *button) {
        auto *row = new QWidget(camera_box_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        layout->addWidget(field, 1);
        layout->addWidget(make_keyframe_controls(button, row));
        return row;
    };

    auto *camera_select_row = new QWidget(camera_box_);
    auto *camera_select_layout = new QHBoxLayout(camera_select_row);
    camera_select_layout->setContentsMargins(0, 0, 0, 0);
    camera_select_layout->setSpacing(2);
    cmb_camera_ = new QComboBox(camera_select_row);
    cmb_camera_->setFixedHeight(20);
    btn_camera_add_ = new QPushButton(QStringLiteral("+"), camera_select_row);
    btn_camera_delete_ = new QPushButton(QStringLiteral("−"), camera_select_row);
    btn_camera_actions_ = new QToolButton(camera_select_row);
    btn_camera_add_->setFixedSize(22, 20);
    btn_camera_delete_->setFixedSize(22, 20);
    btn_camera_actions_->setFixedSize(24, 20);
    btn_camera_actions_->setText(QStringLiteral("⋯"));
    btn_camera_actions_->setPopupMode(QToolButton::InstantPopup);
    btn_camera_add_->setToolTip(QStringLiteral("Add camera"));
    btn_camera_delete_->setToolTip(QStringLiteral("Delete selected camera"));
    btn_camera_actions_->setToolTip(QStringLiteral("Camera actions"));
    auto *camera_actions_menu = new QMenu(btn_camera_actions_);
    act_camera_duplicate_ = camera_actions_menu->addAction(
        fxm_icon("duplicate.svg"), QStringLiteral("Duplicate Camera"));
    act_camera_copy_ = camera_actions_menu->addAction(QStringLiteral("Copy Camera"));
    act_camera_paste_ = camera_actions_menu->addAction(QStringLiteral("Paste Camera"));
    act_camera_paste_->setEnabled(false);
    btn_camera_actions_->setMenu(camera_actions_menu);
    camera_select_layout->addWidget(cmb_camera_, 1);
    camera_select_layout->addWidget(btn_camera_add_);
    camera_select_layout->addWidget(btn_camera_actions_);
    camera_select_layout->addWidget(btn_camera_delete_);
    add_camera_row(QStringLiteral("Active"), camera_select_row, cmb_camera_);

    chk_camera_canvas_default_ = new FxmSwitch(
        QStringLiteral("Match canvas (legacy 2D view)"), camera_box_);
    chk_camera_canvas_default_->setStyleSheet(QStringLiteral("font-size:10px;"));
    add_camera_row(QString(), chk_camera_canvas_default_);
    cmb_camera_projection_ = new QComboBox(camera_box_);
    cmb_camera_projection_->addItem(QStringLiteral("Perspective"),
                                    static_cast<int>(CameraProjection::Perspective));
    cmb_camera_projection_->addItem(QStringLiteral("Orthographic"),
                                    static_cast<int>(CameraProjection::Orthographic));
    cmb_camera_projection_->setFixedHeight(20);
    btn_kf_camera_projection_ = make_camera_keyframe_button(
        QStringLiteral("camera projection"));
    add_camera_row(QStringLiteral("Projection"),
                   make_camera_keyframed_row(cmb_camera_projection_,
                                              btn_kf_camera_projection_),
                   cmb_camera_projection_);

    spn_camera_pos_x_ = make_camera_spin(-1000000.0, 1000000.0);
    spn_camera_pos_y_ = make_camera_spin(-1000000.0, 1000000.0);
    spn_camera_pos_z_ = make_camera_spin(-1000000.0, 1000000.0);
    QWidget *camera_position_row = make_xyz_row(
        spn_camera_pos_x_, spn_camera_pos_y_, spn_camera_pos_z_);
    btn_kf_camera_position_ = make_camera_keyframe_button(
        QStringLiteral("camera position"));
    add_camera_row(QStringLiteral("Position"),
                   make_camera_keyframed_row(camera_position_row,
                                              btn_kf_camera_position_),
                   spn_camera_pos_x_);
    spn_camera_target_x_ = make_camera_spin(-1000000.0, 1000000.0);
    spn_camera_target_y_ = make_camera_spin(-1000000.0, 1000000.0);
    spn_camera_target_z_ = make_camera_spin(-1000000.0, 1000000.0);
    QWidget *camera_target_row = make_xyz_row(
        spn_camera_target_x_, spn_camera_target_y_, spn_camera_target_z_);
    btn_kf_camera_target_ = make_camera_keyframe_button(
        QStringLiteral("camera target"));
    add_camera_row(QStringLiteral("Target"),
                   make_camera_keyframed_row(camera_target_row,
                                              btn_kf_camera_target_),
                   spn_camera_target_x_);
    spn_camera_orientation_x_ = make_camera_spin(-360000.0, 360000.0);
    spn_camera_orientation_y_ = make_camera_spin(-360000.0, 360000.0);
    spn_camera_orientation_z_ = make_camera_spin(-360000.0, 360000.0);
    QWidget *camera_orientation_row = make_xyz_row(
        spn_camera_orientation_x_, spn_camera_orientation_y_, spn_camera_orientation_z_);
    btn_kf_camera_orientation_ = make_camera_keyframe_button(
        QStringLiteral("camera orientation"));
    add_camera_row(QStringLiteral("Orientation"),
                   make_camera_keyframed_row(camera_orientation_row,
                                              btn_kf_camera_orientation_),
                   spn_camera_orientation_x_);
    spn_camera_rot_x_ = make_camera_spin(-360000.0, 360000.0);
    spn_camera_rot_y_ = make_camera_spin(-360000.0, 360000.0);
    spn_camera_rot_z_ = make_camera_spin(-360000.0, 360000.0);
    QWidget *camera_rotation_row = make_xyz_row(
        spn_camera_rot_x_, spn_camera_rot_y_, spn_camera_rot_z_);
    btn_kf_camera_rotation_ = make_camera_keyframe_button(
        QStringLiteral("camera rotation"));
    add_camera_row(QStringLiteral("Rotation"),
                   make_camera_keyframed_row(camera_rotation_row,
                                              btn_kf_camera_rotation_),
                   spn_camera_rot_x_);
    spn_camera_focal_ = make_camera_spin(1.0, 1000000.0);
    spn_camera_fov_ = make_camera_spin(0.1, 179.0);
    spn_camera_zoom_ = make_camera_spin(0.0001, 10000.0, 4);
    spn_camera_near_ = make_camera_spin(0.0001, 1000000.0, 4);
    spn_camera_far_ = make_camera_spin(0.001, 1000000000.0);
    btn_kf_camera_focal_ = make_camera_keyframe_button(
        QStringLiteral("camera focal length"));
    btn_kf_camera_fov_ = make_camera_keyframe_button(
        QStringLiteral("camera field of view"));
    btn_kf_camera_zoom_ = make_camera_keyframe_button(
        QStringLiteral("camera zoom"));
    btn_kf_camera_near_ = make_camera_keyframe_button(
        QStringLiteral("camera near clip"));
    btn_kf_camera_far_ = make_camera_keyframe_button(
        QStringLiteral("camera far clip"));
    add_camera_row(QStringLiteral("Focal length"),
                   make_camera_keyframed_row(spn_camera_focal_,
                                              btn_kf_camera_focal_),
                   spn_camera_focal_);
    add_camera_row(QStringLiteral("Field of view"),
                   make_camera_keyframed_row(spn_camera_fov_,
                                              btn_kf_camera_fov_),
                   spn_camera_fov_);
    add_camera_row(QStringLiteral("Zoom"),
                   make_camera_keyframed_row(spn_camera_zoom_,
                                              btn_kf_camera_zoom_),
                   spn_camera_zoom_);
    add_camera_row(QStringLiteral("Near clip"),
                   make_camera_keyframed_row(spn_camera_near_,
                                              btn_kf_camera_near_),
                   spn_camera_near_);
    add_camera_row(QStringLiteral("Far clip"),
                   make_camera_keyframed_row(spn_camera_far_,
                                              btn_kf_camera_far_),
                   spn_camera_far_);

    camera_panel_ = new FxmCollapsiblePanel(QStringLiteral("3D Camera"),
                                            camera_box_, scene_controls_);
    camera_panel_->setPersistenceKey(QStringLiteral("title-properties"),
                                     QStringLiteral("3d-camera"));
    camera_panel_->setOrderPersistenceEnabled(false);
    scene_controls_layout->addWidget(camera_panel_);

    light_box_ = new QWidget(this);
    light_box_->setObjectName(QStringLiteral("FxmTitleLightContent"));
    auto *light_form = new QFormLayout(light_box_);
    light_form->setContentsMargins(5, 4, 5, 5);
    light_form->setHorizontalSpacing(4);
    light_form->setVerticalSpacing(2);
    light_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto add_light_row = [light_form](const QString &label, QWidget *field) {
        light_form->addRow(label, field);
    };
    auto make_light_spin = [this](double minimum, double maximum,
                                  double step = 1.0, int decimals = 2) {
        auto *spin = new QDoubleSpinBox(light_box_);
        spin->setRange(minimum, maximum);
        spin->setSingleStep(step);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(true);
        spin->setFixedHeight(20);
        spin->setMinimumWidth(66);
        spin->setStyleSheet(QStringLiteral("font-size:10px;padding:0 2px;"));
        return spin;
    };
    auto make_light_xyz_row = [this](QDoubleSpinBox *x, QDoubleSpinBox *y,
                                     QDoubleSpinBox *z) {
        auto *row = new QWidget(light_box_);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);
        const std::array<std::pair<const char *, QDoubleSpinBox *>, 3> entries{{
            {"X", x}, {"Y", y}, {"Z", z}
        }};
        for (const auto &entry : entries) {
            auto *label = new QLabel(QString::fromLatin1(entry.first), row);
            label->setMinimumWidth(9);
            label->setAlignment(Qt::AlignCenter);
            label->setStyleSheet(QStringLiteral("font-size:9px;"));
            layout->addWidget(label);
            layout->addWidget(entry.second, 1);
        }
        return row;
    };

    chk_lighting_enabled_ = new FxmSwitch(QStringLiteral("Enable 3D Lighting"),
                                           light_box_);
    chk_default_light_ = new FxmSwitch(
        QStringLiteral("Use compatibility light when no lights exist"), light_box_);
    chk_lighting_enabled_->setStyleSheet(QStringLiteral("font-size:10px;"));
    chk_default_light_->setStyleSheet(QStringLiteral("font-size:10px;"));
    add_light_row(QString(), chk_lighting_enabled_);
    add_light_row(QString(), chk_default_light_);
    spn_environment_exposure_ = make_light_spin(-16.0, 16.0, 0.1, 2);
    spn_environment_exposure_->setSuffix(QStringLiteral(" EV"));
    add_light_row(QStringLiteral("Environment"), spn_environment_exposure_);

    auto *light_select_row = new QWidget(light_box_);
    auto *light_select_layout = new QHBoxLayout(light_select_row);
    light_select_layout->setContentsMargins(0, 0, 0, 0);
    light_select_layout->setSpacing(2);
    cmb_light_ = new QComboBox(light_select_row);
    cmb_light_->setFixedHeight(20);
    btn_light_add_ = new QPushButton(QStringLiteral("+"), light_select_row);
    btn_light_delete_ = new QPushButton(QStringLiteral("−"), light_select_row);
    btn_light_add_->setFixedSize(22, 20);
    btn_light_delete_->setFixedSize(22, 20);
    btn_light_add_->setToolTip(QStringLiteral("Add light (maximum four renderer slots)"));
    btn_light_delete_->setToolTip(QStringLiteral("Delete selected light"));
    light_select_layout->addWidget(cmb_light_, 1);
    light_select_layout->addWidget(btn_light_add_);
    light_select_layout->addWidget(btn_light_delete_);
    add_light_row(QStringLiteral("Light"), light_select_row);

    chk_light_enabled_ = new FxmSwitch(QStringLiteral("Enabled"), light_box_);
    chk_light_enabled_->setStyleSheet(QStringLiteral("font-size:10px;"));
    add_light_row(QString(), chk_light_enabled_);
    cmb_light_type_ = new QComboBox(light_box_);
    cmb_light_type_->addItem(QStringLiteral("Ambient"),
                             static_cast<int>(TitleLightType::Ambient));
    cmb_light_type_->addItem(QStringLiteral("Point"),
                             static_cast<int>(TitleLightType::Point));
    cmb_light_type_->addItem(QStringLiteral("Spot"),
                             static_cast<int>(TitleLightType::Spot));
    cmb_light_type_->addItem(QStringLiteral("Parallel"),
                             static_cast<int>(TitleLightType::Parallel));
    cmb_light_type_->addItem(QStringLiteral("Environment"),
                             static_cast<int>(TitleLightType::Environment));
    cmb_light_type_->setFixedHeight(20);
    add_light_row(QStringLiteral("Type"), cmb_light_type_);

    btn_light_color_ = new QPushButton(QStringLiteral("Color"), light_box_);
    btn_light_color_->setFixedHeight(20);
    add_light_row(QStringLiteral("Color"), btn_light_color_);
    spn_light_intensity_ = make_light_spin(0.0, 100000.0, 1.0, 2);
    spn_light_intensity_->setSuffix(QStringLiteral(" %"));
    add_light_row(QStringLiteral("Intensity"), spn_light_intensity_);
    spn_light_source_size_ = make_light_spin(0.1, 100000.0, 0.1, 2);
    spn_light_source_size_->setSuffix(QStringLiteral(" px"));
    spn_light_source_size_->setToolTip(QStringLiteral(
        "Finite emitter size in scene units; softens lighting and supported shadows."));
    add_light_row(QStringLiteral("Source Size"), spn_light_source_size_);

    spn_light_pos_x_ = make_light_spin(-1000000.0, 1000000.0);
    spn_light_pos_y_ = make_light_spin(-1000000.0, 1000000.0);
    spn_light_pos_z_ = make_light_spin(-1000000.0, 1000000.0);
    add_light_row(QStringLiteral("Position"), make_light_xyz_row(
        spn_light_pos_x_, spn_light_pos_y_, spn_light_pos_z_));
    spn_light_target_x_ = make_light_spin(-1000000.0, 1000000.0);
    spn_light_target_y_ = make_light_spin(-1000000.0, 1000000.0);
    spn_light_target_z_ = make_light_spin(-1000000.0, 1000000.0);
    add_light_row(QStringLiteral("Target"), make_light_xyz_row(
        spn_light_target_x_, spn_light_target_y_, spn_light_target_z_));

    cmb_light_falloff_ = new QComboBox(light_box_);
    cmb_light_falloff_->addItem(QStringLiteral("None"),
                                static_cast<int>(TitleLightFalloff::None));
    cmb_light_falloff_->addItem(QStringLiteral("Linear"),
                                static_cast<int>(TitleLightFalloff::Linear));
    cmb_light_falloff_->addItem(QStringLiteral("Inverse Square"),
                                static_cast<int>(TitleLightFalloff::InverseSquare));
    cmb_light_falloff_->setFixedHeight(20);
    add_light_row(QStringLiteral("Falloff"), cmb_light_falloff_);
    spn_light_falloff_start_ = make_light_spin(0.0, 10000000.0);
    spn_light_falloff_distance_ = make_light_spin(0.0001, 10000000.0);
    /* Falloff Start is derived from Source Size and is intentionally not shown. */
    add_light_row(QStringLiteral("Falloff Distance"), spn_light_falloff_distance_);
    spn_light_cone_angle_ = make_light_spin(0.1, 179.0, 0.5, 2);
    spn_light_cone_angle_->setSuffix(QStringLiteral("°"));
    spn_light_cone_feather_ = make_light_spin(0.0, 100.0, 1.0, 2);
    spn_light_cone_feather_->setSuffix(QStringLiteral(" %"));
    add_light_row(QStringLiteral("Cone Angle"), spn_light_cone_angle_);
    add_light_row(QStringLiteral("Cone Feather"), spn_light_cone_feather_);

    chk_light_casts_shadows_ = new FxmSwitch(
        QStringLiteral("Cast Shadows"), light_box_);
    chk_light_casts_shadows_->setStyleSheet(QStringLiteral("font-size:10px;"));
    add_light_row(QString(), chk_light_casts_shadows_);
    spn_light_shadow_darkness_ = make_light_spin(0.0, 100.0, 1.0, 2);
    spn_light_shadow_darkness_->setSuffix(QStringLiteral(" %"));
    spn_light_shadow_softness_ = make_light_spin(0.0, 256.0, 0.5, 2);
    spn_light_shadow_bias_ = make_light_spin(0.0, 10.0, 0.0001, 5);
    add_light_row(QStringLiteral("Shadow Darkness"), spn_light_shadow_darkness_);
    add_light_row(QStringLiteral("Shadow Softness"), spn_light_shadow_softness_);
    add_light_row(QStringLiteral("Shadow Bias"), spn_light_shadow_bias_);

    light_panel_ = new FxmCollapsiblePanel(QStringLiteral("3D Lights & Environment"),
                                           light_box_, scene_controls_);
    light_panel_->setPersistenceKey(QStringLiteral("title-properties"),
                                    QStringLiteral("3d-lights"));
    light_panel_->setOrderPersistenceEnabled(false);
    scene_controls_layout->addWidget(light_panel_);
    scene_controls_layout->addStretch(1);

    cmb_stinger_switch_mode_ = new QComboBox(this);
    cmb_stinger_switch_mode_->addItem(
        fxm_tr("OBSTitles.StingerSwitchAtPoint"),
        static_cast<int>(StingerSwitchMode::SwitchAtPoint));
    cmb_stinger_switch_mode_->addItem(
        fxm_tr("OBSTitles.StingerManualSceneAnimation"),
        static_cast<int>(StingerSwitchMode::ManualSceneAnimation));
    cmb_stinger_switch_mode_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.StingerSwitchMode"), cmb_stinger_switch_mode_);

    spn_stinger_transition_timecode_ = new TimecodeSpinBox(this);
    spn_stinger_transition_timecode_->setRange(0.0, 3600.0);
    spn_stinger_transition_timecode_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.StingerTransitionPoint"), spn_stinger_transition_timecode_);

    chk_stinger_audio_ = new FxmSwitch(fxm_tr("OBSTitles.StingerOptionalAudio"), this);
    chk_stinger_alpha_ = new FxmSwitch(fxm_tr("OBSTitles.StingerAlphaOutput"), this);
    fl->addRow(QString(), chk_stinger_audio_);
    fl->addRow(QString(), chk_stinger_alpha_);

    spn_stinger_pre_roll_ = new TimecodeSpinBox(this);
    spn_stinger_post_roll_ = new TimecodeSpinBox(this);
    spn_stinger_pre_roll_->setRange(0.0, 3600.0);
    spn_stinger_post_roll_->setRange(0.0, 3600.0);
    spn_stinger_pre_roll_->setFixedHeight(20);
    spn_stinger_post_roll_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.StingerPreRoll"), spn_stinger_pre_roll_);
    add_form_row(fl, fxm_tr("OBSTitles.StingerPostRoll"), spn_stinger_post_roll_);

    cmb_stinger_render_mode_ = new QComboBox(this);
    cmb_stinger_render_mode_->addItem(fxm_tr("OBSTitles.StingerProceduralLive"),
                                      static_cast<int>(StingerRenderMode::ProceduralLive));
    cmb_stinger_render_mode_->addItem(fxm_tr("OBSTitles.StingerPrerenderedProxy"),
                                      static_cast<int>(StingerRenderMode::PrerenderedProxy));
    cmb_stinger_render_mode_->setFixedHeight(20);
    add_form_row(fl, fxm_tr("OBSTitles.StingerRenderMode"), cmb_stinger_render_mode_);

    lbl_stinger_validation_ = new QLabel(this);
    lbl_stinger_validation_->setWordWrap(true);
    lbl_stinger_validation_->setTextFormat(Qt::RichText);
    fl->addRow(fxm_tr("OBSTitles.StingerValidation"), lbl_stinger_validation_);

    connect(cmb_camera_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!title_ || loading_values_ || index < 0) return;
                const std::string camera_id = cmb_camera_->itemData(index).toString().toStdString();
                if (title_->active_camera.is_animated()) {
                    title_->active_camera.set(playhead_, camera_id);
                } else {
                    title_->active_camera.static_value = camera_id;
                    title_->active_camera_id = camera_id;
                }
                load_values();
                emit title_changed(true);
            });
    connect(btn_camera_add_, &QPushButton::clicked, this, [this]() {
        if (!title_ || loading_values_) return;
        TitleCamera camera;
        camera.id = TitleDataStore::make_uuid();
        camera.name = QStringLiteral("Camera %1")
                          .arg(std::count_if(
                              title_->cameras.begin(), title_->cameras.end(),
                              [](const TitleCamera &existing) {
                                  return existing.asset_space_owner_id.empty();
                              }) + 1).toStdString();
        camera.use_canvas_default = false;
        camera.position_x.static_value = title_->width * 0.5;
        camera.position_y.static_value = title_->height * 0.5;
        camera.position_z.static_value = -1000.0;
        camera.target_x.static_value = title_->width * 0.5;
        camera.target_y.static_value = title_->height * 0.5;
        camera.target_z.static_value = 0.0;
        const std::string camera_id = camera.id;
        title_->cameras.push_back(std::move(camera));
        if (title_->active_camera.is_animated()) {
            title_->active_camera.set(playhead_, camera_id);
        } else {
            title_->active_camera.static_value = camera_id;
            title_->active_camera_id = camera_id;
        }
        load_values();
        emit title_changed(true);
    });
    connect(act_camera_duplicate_, &QAction::triggered, this, [this]() {
        if (loading_values_) return;
        if (const TitleCamera *camera = current_camera())
            insert_camera_copy(*camera);
    });
    connect(act_camera_copy_, &QAction::triggered, this, [this]() {
        if (loading_values_) return;
        if (const TitleCamera *camera = current_camera()) {
            camera_clipboard_ = std::make_unique<TitleCamera>(*camera);
            if (act_camera_paste_) act_camera_paste_->setEnabled(true);
        }
    });
    connect(act_camera_paste_, &QAction::triggered, this, [this]() {
        if (loading_values_ || !camera_clipboard_) return;
        insert_camera_copy(*camera_clipboard_);
    });
    connect(btn_camera_delete_, &QPushButton::clicked, this, [this]() {
        if (!title_ || loading_values_ || title_->cameras.size() <= 1) return;
        const std::string selected = title_->active_camera.evaluate(playhead_);
        auto it = std::find_if(title_->cameras.begin(), title_->cameras.end(),
            [&](const TitleCamera &camera) { return camera.id == selected; });
        if (it == title_->cameras.end() || it->id == "default") return;
        title_->cameras.erase(it);
        const std::string fallback = title_->cameras.front().id;
        if (title_->active_camera.static_value == selected)
            title_->active_camera.static_value = fallback;
        for (auto &key : title_->active_camera.keyframes)
            if (key.value == selected) key.value = fallback;
        title_->active_camera_id = title_->active_camera.static_value;
        for (auto &layer : title_->layers) {
            if (!layer) continue;
            if (layer->camera_id == selected) layer->camera_id.clear();
            if (layer->camera_assignment.static_value == selected)
                layer->camera_assignment.static_value.clear();
            for (auto &key : layer->camera_assignment.keyframes)
                if (key.value == selected) key.value.clear();
            layer->camera_id = layer->camera_assignment.static_value;
        }
        load_values();
        emit title_changed(true);
    });
    connect(chk_camera_canvas_default_, &QCheckBox::toggled, this, [this](bool value) {
        if (loading_values_) return;
        if (TitleCamera *camera = current_camera()) {
            camera->use_canvas_default = value;
            load_values();
            emit title_changed(true);
        }
    });
    connect(cmb_camera_projection_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (loading_values_) return;
                if (TitleCamera *camera = current_camera()) {
                    const auto projection = static_cast<CameraProjection>(
                        cmb_camera_projection_->itemData(index).toInt());
                    const double value = projection == CameraProjection::Orthographic ? 1.0 : 0.0;
                    set_animated_value(camera->projection_mode, playhead_, value);
                    for (Keyframe &key : camera->projection_mode.keyframes) {
                        if (std::abs(key.time - playhead_) > 1.0 / 240.0) continue;
                        key.easing = EasingType::Hold;
                        key.temporal_mode = TemporalInterpolationMode::Hold;
                        key.temporal_velocity_explicit = true;
                    }
                    camera->projection = static_cast<CameraProjection>(
                        static_cast<int>(std::round(
                            std::clamp(camera->projection_mode.static_value, 0.0, 1.0))));
                    emit title_changed(true);
                }
            });
    auto connect_camera_property = [this](QDoubleSpinBox *spin,
                                          AnimatedProperty TitleCamera::*member) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, member](double value) {
                    if (loading_values_) return;
                    if (TitleCamera *camera = current_camera()) {
                        set_animated_value(camera->*member, playhead_, value);
                        if (member == &TitleCamera::near_clip &&
                            camera->far_clip.evaluate(playhead_) <= value)
                            set_animated_value(camera->far_clip, playhead_, value + 0.001);
                        if (member == &TitleCamera::far_clip &&
                            value <= camera->near_clip.evaluate(playhead_))
                            set_animated_value(camera->far_clip, playhead_,
                                camera->near_clip.evaluate(playhead_) + 0.001);
                        emit title_changed(!numeric_label_dragging_);
                    }
                });
    };
    auto connect_camera_vector_component = [this](QDoubleSpinBox *spin,
                                                   bool target, int component) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, target, component](double value) {
            if (loading_values_) return;
            if (TitleCamera *camera = current_camera()) {
                promote_camera_spatial_tracks(*camera);
                AnimatedVec3Property &property = target ? camera->target_3d
                                                        : camera->position_3d;
                Vec3Value next = property.evaluate(playhead_);
                if (component == 0) next.x = value;
                else if (component == 1) next.y = value;
                else next.z = value;
                set_animated_value(property, playhead_, next);
                if (target)
                    mirror_camera_vector_track_to_legacy(camera->target_3d, playhead_,
                        camera->target_x, camera->target_y, camera->target_z);
                else
                    mirror_camera_vector_track_to_legacy(camera->position_3d, playhead_,
                        camera->position_x, camera->position_y, camera->position_z);
                emit title_changed(!numeric_label_dragging_);
            }
        });
    };
    connect_camera_vector_component(spn_camera_pos_x_, false, 0);
    connect_camera_vector_component(spn_camera_pos_y_, false, 1);
    connect_camera_vector_component(spn_camera_pos_z_, false, 2);
    connect_camera_vector_component(spn_camera_target_x_, true, 0);
    connect_camera_vector_component(spn_camera_target_y_, true, 1);
    connect_camera_vector_component(spn_camera_target_z_, true, 2);
    connect_camera_property(spn_camera_orientation_x_, &TitleCamera::orientation_x);
    connect_camera_property(spn_camera_orientation_y_, &TitleCamera::orientation_y);
    connect_camera_property(spn_camera_orientation_z_, &TitleCamera::orientation_z);
    connect_camera_property(spn_camera_rot_x_, &TitleCamera::rotation_x);
    connect_camera_property(spn_camera_rot_y_, &TitleCamera::rotation_y);
    connect_camera_property(spn_camera_rot_z_, &TitleCamera::rotation_z);
    connect_camera_property(spn_camera_focal_, &TitleCamera::focal_length);
    connect_camera_property(spn_camera_fov_, &TitleCamera::field_of_view);
    connect_camera_property(spn_camera_zoom_, &TitleCamera::zoom);
    connect_camera_property(spn_camera_near_, &TitleCamera::near_clip);
    connect_camera_property(spn_camera_far_, &TitleCamera::far_clip);

    auto finish_camera_keyframe_toggle = [this]() {
        load_values();
        emit title_changed(true);
    };
    connect(btn_kf_camera_projection_, &QToolButton::clicked, this,
            [this, finish_camera_keyframe_toggle]() {
        if (loading_values_) return;
        if (TitleCamera *camera = current_camera()) {
            toggle_keyframe(camera->projection_mode, playhead_,
                            camera->projection_mode.evaluate(playhead_) >= 0.5
                                ? 1.0 : 0.0);
            for (Keyframe &key : camera->projection_mode.keyframes) {
                if (std::abs(key.time - playhead_) > 1.0 / 240.0) continue;
                key.easing = EasingType::Hold;
                key.temporal_mode = TemporalInterpolationMode::Hold;
                key.temporal_velocity_explicit = true;
            }
            finish_camera_keyframe_toggle();
        }
    });
    auto connect_camera_vector_keyframe =
        [this, finish_camera_keyframe_toggle](QToolButton *button, bool target) {
        connect(button, &QToolButton::clicked, this,
                [this, target, finish_camera_keyframe_toggle]() {
            if (loading_values_) return;
            if (TitleCamera *camera = current_camera()) {
                AnimatedVec3Property *property = nullptr;
                if (target) {
                    promote_camera_vector_track(
                        camera->target_3d, camera->target_3d_path_enabled,
                        camera->target_x, camera->target_y, camera->target_z);
                    property = &camera->target_3d;
                } else {
                    promote_camera_vector_track(
                        camera->position_3d, camera->position_3d_path_enabled,
                        camera->position_x, camera->position_y,
                        camera->position_z);
                    property = &camera->position_3d;
                }
                toggle_keyframe(*property, playhead_,
                                property->evaluate(playhead_));
                if (target)
                    mirror_camera_vector_track_to_legacy(
                        camera->target_3d, playhead_, camera->target_x,
                        camera->target_y, camera->target_z);
                else
                    mirror_camera_vector_track_to_legacy(
                        camera->position_3d, playhead_, camera->position_x,
                        camera->position_y, camera->position_z);
                finish_camera_keyframe_toggle();
            }
        });
    };
    connect_camera_vector_keyframe(btn_kf_camera_position_, false);
    connect_camera_vector_keyframe(btn_kf_camera_target_, true);

    auto connect_camera_scalar_group_keyframe =
        [this, finish_camera_keyframe_toggle](
            QToolButton *button, AnimatedProperty TitleCamera::*x,
            AnimatedProperty TitleCamera::*y,
            AnimatedProperty TitleCamera::*z) {
        connect(button, &QToolButton::clicked, this,
                [this, x, y, z, finish_camera_keyframe_toggle]() {
            if (loading_values_) return;
            if (TitleCamera *camera = current_camera()) {
                AnimatedProperty *properties[] = {
                    &(camera->*x), &(camera->*y), &(camera->*z)};
                const bool remove = any_keyframe_at_time(
                    {properties[0], properties[1], properties[2]}, playhead_);
                for (AnimatedProperty *property : properties) {
                    if (remove)
                        remove_keyframe_at(*property, playhead_);
                    else
                        add_or_replace_keyframe(
                            *property, playhead_, property->evaluate(playhead_));
                }
                finish_camera_keyframe_toggle();
            }
        });
    };
    connect_camera_scalar_group_keyframe(
        btn_kf_camera_orientation_, &TitleCamera::orientation_x,
        &TitleCamera::orientation_y, &TitleCamera::orientation_z);
    connect_camera_scalar_group_keyframe(
        btn_kf_camera_rotation_, &TitleCamera::rotation_x,
        &TitleCamera::rotation_y, &TitleCamera::rotation_z);

    auto connect_camera_scalar_keyframe =
        [this, finish_camera_keyframe_toggle](
            QToolButton *button, AnimatedProperty TitleCamera::*member) {
        connect(button, &QToolButton::clicked, this,
                [this, member, finish_camera_keyframe_toggle]() {
            if (loading_values_) return;
            if (TitleCamera *camera = current_camera()) {
                AnimatedProperty &property = camera->*member;
                toggle_keyframe(property, playhead_, property.evaluate(playhead_));
                finish_camera_keyframe_toggle();
            }
        });
    };
    connect_camera_scalar_keyframe(btn_kf_camera_focal_,
                                   &TitleCamera::focal_length);
    connect_camera_scalar_keyframe(btn_kf_camera_fov_,
                                   &TitleCamera::field_of_view);
    connect_camera_scalar_keyframe(btn_kf_camera_zoom_, &TitleCamera::zoom);
    connect_camera_scalar_keyframe(btn_kf_camera_near_, &TitleCamera::near_clip);
    connect_camera_scalar_keyframe(btn_kf_camera_far_, &TitleCamera::far_clip);

    auto scalar_times = [this](AnimatedProperty TitleCamera::*member) {
        return [this, member]() {
            std::vector<double> times;
            if (const TitleCamera *camera = current_camera())
                for (const Keyframe &key : (camera->*member).keyframes)
                    times.push_back(key.time);
            return times;
        };
    };
    auto install_camera_keyframe_menu =
        [this](QToolButton *button,
               std::function<std::vector<double>()> times,
               std::function<void(TitleCamera &)> clear) {
        register_keyframe_times(button, times);
        button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(button, &QWidget::customContextMenuRequested, this,
                [this, button, times, clear](const QPoint &position) {
            QMenu menu(button);
            QAction *remove_all = menu.addAction(
                fxm_tr("OBSTitles.DeleteAllKeyframes"));
            remove_all->setEnabled(!times().empty());
            if (menu.exec(button->mapToGlobal(position)) != remove_all)
                return;
            if (TitleCamera *camera = current_camera()) {
                clear(*camera);
                load_values();
                emit title_changed(true);
            }
        });
    };
    auto install_scalar = [&](QToolButton *button,
                              AnimatedProperty TitleCamera::*member) {
        install_camera_keyframe_menu(button, scalar_times(member),
            [member](TitleCamera &camera) { (camera.*member).keyframes.clear(); });
    };
    install_scalar(btn_kf_camera_projection_, &TitleCamera::projection_mode);
    install_camera_keyframe_menu(btn_kf_camera_position_, [this]() {
        std::vector<double> times;
        if (const TitleCamera *camera = current_camera()) {
            for (const Vector3Keyframe &key : camera->position_3d.keyframes)
                times.push_back(key.time);
            for (const Keyframe &key : camera->position_x.keyframes) times.push_back(key.time);
            for (const Keyframe &key : camera->position_y.keyframes) times.push_back(key.time);
            for (const Keyframe &key : camera->position_z.keyframes) times.push_back(key.time);
        }
        return times;
    }, [](TitleCamera &camera) {
        camera.position_3d.keyframes.clear();
        camera.position_x.keyframes.clear();
        camera.position_y.keyframes.clear();
        camera.position_z.keyframes.clear();
    });
    install_camera_keyframe_menu(btn_kf_camera_target_, [this]() {
        std::vector<double> times;
        if (const TitleCamera *camera = current_camera()) {
            for (const Vector3Keyframe &key : camera->target_3d.keyframes)
                times.push_back(key.time);
            for (const Keyframe &key : camera->target_x.keyframes) times.push_back(key.time);
            for (const Keyframe &key : camera->target_y.keyframes) times.push_back(key.time);
            for (const Keyframe &key : camera->target_z.keyframes) times.push_back(key.time);
        }
        return times;
    }, [](TitleCamera &camera) {
        camera.target_3d.keyframes.clear();
        camera.target_x.keyframes.clear();
        camera.target_y.keyframes.clear();
        camera.target_z.keyframes.clear();
    });
    install_camera_keyframe_menu(btn_kf_camera_orientation_, [this]() {
        std::vector<double> times;
        if (const TitleCamera *camera = current_camera())
            for (const AnimatedProperty *property : {&camera->orientation_x,
                                                     &camera->orientation_y,
                                                     &camera->orientation_z})
                for (const Keyframe &key : property->keyframes) times.push_back(key.time);
        return times;
    }, [](TitleCamera &camera) {
        camera.orientation_x.keyframes.clear();
        camera.orientation_y.keyframes.clear();
        camera.orientation_z.keyframes.clear();
    });
    install_camera_keyframe_menu(btn_kf_camera_rotation_, [this]() {
        std::vector<double> times;
        if (const TitleCamera *camera = current_camera())
            for (const AnimatedProperty *property : {&camera->rotation_x,
                                                     &camera->rotation_y,
                                                     &camera->rotation_z})
                for (const Keyframe &key : property->keyframes) times.push_back(key.time);
        return times;
    }, [](TitleCamera &camera) {
        camera.rotation_x.keyframes.clear();
        camera.rotation_y.keyframes.clear();
        camera.rotation_z.keyframes.clear();
    });
    install_scalar(btn_kf_camera_focal_, &TitleCamera::focal_length);
    install_scalar(btn_kf_camera_fov_, &TitleCamera::field_of_view);
    install_scalar(btn_kf_camera_zoom_, &TitleCamera::zoom);
    install_scalar(btn_kf_camera_near_, &TitleCamera::near_clip);
    install_scalar(btn_kf_camera_far_, &TitleCamera::far_clip);

    connect(chk_lighting_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_) return;
        title_->lighting_enabled = enabled;
        emit title_changed(true);
    });
    connect(chk_default_light_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_) return;
        title_->default_light_enabled = enabled;
        emit title_changed(true);
    });
    connect(spn_environment_exposure_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (!title_ || loading_values_) return;
                title_->environment_exposure = std::clamp(value, -16.0, 16.0);
                emit title_changed(!numeric_label_dragging_);
            });
    connect(cmb_light_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (loading_values_) return;
                load_values();
            });
    connect(btn_light_add_, &QPushButton::clicked, this, [this]() {
        if (!title_ || loading_values_) return;
        const auto light_count = std::count_if(title_->layers.begin(), title_->layers.end(), [](const auto &layer) { return layer && layer->type == LayerType::Light; });
        if (light_count >= 4) return;
        auto layer = std::make_shared<Layer>();
        layer->id = TitleDataStore::make_uuid();
        layer->name = unique_light_name("Light");
        layer->type = LayerType::Light;
        layer->dimension_mode = LayerDimensionMode::ThreeD;
        layer->position.static_value = {title_->width * 0.5, title_->height * 0.5};
        layer->position_z.static_value = -1000.0;
        layer->light.id = layer->id; layer->light.name = layer->name;
        layer->light.type = TitleLightType::Point;
        const QString id = QString::fromStdString(layer->id);
        title_->layers.push_back(layer);
        title_->default_light_enabled = false;
        load_values();
        const int index = cmb_light_->findData(id);
        if (index >= 0) cmb_light_->setCurrentIndex(index);
        load_values();
        emit title_changed(true);
    });
    connect(btn_light_delete_, &QPushButton::clicked, this, [this]() {
        if (!title_ || loading_values_) return;
        const std::string id = cmb_light_->currentData().toString().toStdString();
        const auto it = std::find_if(title_->layers.begin(), title_->layers.end(), [&id](const auto &layer) { return layer && layer->type == LayerType::Light && layer->id == id; });
        if (it == title_->layers.end()) return;
        title_->layers.erase(it);
        const bool any = std::any_of(title_->layers.begin(), title_->layers.end(), [](const auto &layer) { return layer && layer->type == LayerType::Light; });
        if (!any) title_->default_light_enabled = true;
        load_values(); emit title_changed(true);
    });
    connect(chk_light_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (loading_values_) return;
        if (TitleLight *light = current_light()) {
            light->enabled = enabled;
            emit title_changed(true);
        }
    });
    connect(cmb_light_type_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (loading_values_ || index < 0) return;
                if (TitleLight *light = current_light()) {
                    light->type = static_cast<TitleLightType>(
                        cmb_light_type_->itemData(index).toInt());
                    load_values();
                    emit title_changed(true);
                }
            });
    connect(cmb_light_falloff_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (loading_values_ || index < 0) return;
                if (TitleLight *light = current_light()) {
                    light->falloff = static_cast<TitleLightFalloff>(
                        cmb_light_falloff_->itemData(index).toInt());
                    load_values();
                    emit title_changed(true);
                }
            });
    connect(chk_light_casts_shadows_, &QCheckBox::toggled,
            this, [this](bool enabled) {
                if (loading_values_) return;
                if (TitleLight *light = current_light()) {
                    light->casts_shadows = enabled;
                    load_values();
                    emit title_changed(true);
                }
            });
    auto connect_light_property = [this](QDoubleSpinBox *spin,
                                         AnimatedProperty TitleLight::*member) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, member](double value) {
                    if (loading_values_) return;
                    if (TitleLight *light = current_light()) {
                        set_animated_value(light->*member, playhead_, value);
                        emit title_changed(!numeric_label_dragging_);
                    }
                });
    };
    auto connect_light_vector_component = [this](QDoubleSpinBox *spin,
                                                  bool target, int component) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, target, component](double value) {
                    if (loading_values_) return;
                    if (TitleLight *light = current_light()) {
                        AnimatedVec3Property &property = target
                            ? light->target : light->position;
                        Vec3Value next = property.evaluate(playhead_);
                        if (component == 0) next.x = value;
                        else if (component == 1) next.y = value;
                        else next.z = value;
                        set_animated_value(property, playhead_, next);
                        emit title_changed(!numeric_label_dragging_);
                    }
                });
    };
    connect_light_vector_component(spn_light_pos_x_, false, 0);
    connect_light_vector_component(spn_light_pos_y_, false, 1);
    connect_light_vector_component(spn_light_pos_z_, false, 2);
    connect_light_vector_component(spn_light_target_x_, true, 0);
    connect_light_vector_component(spn_light_target_y_, true, 1);
    connect_light_vector_component(spn_light_target_z_, true, 2);
    connect_light_property(spn_light_intensity_, &TitleLight::intensity);
    connect_light_property(spn_light_source_size_, &TitleLight::source_size);
    connect_light_property(spn_light_falloff_distance_, &TitleLight::falloff_distance);
    connect_light_property(spn_light_cone_angle_, &TitleLight::cone_angle);
    connect_light_property(spn_light_cone_feather_, &TitleLight::cone_feather);
    connect_light_property(spn_light_shadow_darkness_, &TitleLight::shadow_darkness);
    connect_light_property(spn_light_shadow_softness_, &TitleLight::shadow_softness);
    connect_light_property(spn_light_shadow_bias_, &TitleLight::shadow_bias);
    connect(btn_light_color_, &QPushButton::clicked, this, [this]() {
        if (loading_values_) return;
        TitleLight *light = current_light();
        if (!light) return;
        const QColor initial = QColor::fromRgba(
            static_cast<QRgb>(evaluated_light_color(*light, playhead_)));
        const QColor selected = QColorDialog::getColor(
            initial, this, QStringLiteral("Light Color"),
            QColorDialog::ShowAlphaChannel);
        if (!selected.isValid()) return;
        set_animated_value(light->color_a, playhead_, selected.alpha());
        set_animated_value(light->color_r, playhead_, selected.red());
        set_animated_value(light->color_g, playhead_, selected.green());
        set_animated_value(light->color_b, playhead_, selected.blue());
        load_values();
        emit title_changed(true);
    });

    connect(grp_playback_mode_, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this, [this](QAbstractButton *button) {
                if (!title_ || loading_values_ || !button) return;
                const int selection = grp_playback_mode_->id(button);
                if (selection == 4) {
                    const bool entering_stinger = title_->graphic_type != TitleGraphicType::Stinger;
                    if (entering_stinger)
                        previous_non_stinger_graphic_type_ = title_->graphic_type;
                    title_->graphic_type = TitleGraphicType::Stinger;
                    title_->playback_mode = 0;
                    if (entering_stinger) {
                        set_stinger_transition_point_seconds(*title_, title_->duration * 0.5);
                        title_->stinger_alpha_output = true;
                        title_->stinger_render_mode = StingerRenderMode::ProceduralLive;
                        title_->stinger_switch_mode = StingerSwitchMode::SwitchAtPoint;
                        title_->stinger_editor_background = StingerEditorBackground::FollowSwitchPoint;
                        title_->cue_end_behavior = 1;
                    }
                } else {
                    if (title_->graphic_type == TitleGraphicType::Stinger) {
                        title_->graphic_type = previous_non_stinger_graphic_type_ == TitleGraphicType::Stinger
                                                   ? TitleGraphicType::Title
                                                   : previous_non_stinger_graphic_type_;
                    }
                    if (selection == 1 || selection == 2) {
                        title_->playback_mode = 1;
                        title_->loop_type = selection == 2 ? 1 : 0;
                    } else {
                        title_->playback_mode = selection == 3 ? 2 : 0;
                    }
                    if (title_->playback_mode == 2 && title_->pause_time <= 0.0)
                        title_->pause_time = title_->duration;
                }
                load_values();
                emit title_changed(!numeric_label_dragging_);
                emit stinger_structure_changed();
            });

    connect(spn_pause_frame_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->pause_time = std::clamp(v, 0.0, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(cmb_cue_end_behavior_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                if (!title_ || loading_values_ || !cmb_cue_end_behavior_) return;
                title_->cue_end_behavior = std::clamp(cmb_cue_end_behavior_->currentData().toInt(), 0, 2);
                emit title_changed(!numeric_label_dragging_);
            });


    connect(spn_duration_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                double old_duration = title_->duration;
                title_->duration = v;
                for (auto &layer : title_->layers) {
                    if (std::abs(layer->out_time - old_duration) < 0.001 || layer->out_time > v)
                        layer->out_time = v;
                }
                title_->loop_start = std::clamp(title_->loop_start, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                title_->pause_time = std::clamp(title_->pause_time, 0.0, title_->duration);
                set_stinger_transition_point_seconds(*title_, title_->stinger_transition_point);
                if (title_->graphic_type == TitleGraphicType::Stinger &&
                    title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation)
                    ensure_stinger_transition_input_layers(*title_);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    auto apply_canvas_size = [this]() {
        if (!title_ || loading_values_ || !spn_width_ || !spn_height_) return;
        title_->width = spn_width_->value();
        title_->height = spn_height_->value();
        emit document_format_changed(true);
        emit title_changed(!numeric_label_dragging_);
    };
    connect(spn_width_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [apply_canvas_size](int) { apply_canvas_size(); });
    connect(spn_height_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [apply_canvas_size](int) { apply_canvas_size(); });
    connect(spn_frame_rate_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (!title_ || loading_values_) return;
                title_->frame_rate = std::clamp(value, 1.0, 240.0);
                emit document_format_changed(false);
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_start_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_start = std::clamp(v, 0.0, title_->duration);
                title_->loop_end = std::clamp(title_->loop_end, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(spn_loop_end_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
                if (!title_ || loading_values_) return;
                title_->loop_end = std::clamp(v, title_->loop_start, title_->duration);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });

    connect(cmb_stinger_switch_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!title_ || loading_values_ ||
                    title_->graphic_type != TitleGraphicType::Stinger)
                    return;
                title_->stinger_switch_mode = static_cast<StingerSwitchMode>(
                    std::clamp(cmb_stinger_switch_mode_->itemData(index).toInt(), 0, 1));
                if (title_->stinger_switch_mode == StingerSwitchMode::ManualSceneAnimation)
                    ensure_stinger_transition_input_layers(*title_);
                load_values();
                emit title_changed(true);
                emit stinger_structure_changed();
            });
    connect(spn_stinger_transition_timecode_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                set_stinger_transition_point_seconds(*title_, seconds);
                load_values();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(chk_stinger_audio_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
        title_->stinger_audio_enabled = enabled;
        update_stinger_validation();
        emit title_changed(true);
    });
    connect(chk_stinger_alpha_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
        title_->stinger_alpha_output = enabled;
        update_stinger_validation();
        emit title_changed(true);
    });
    connect(spn_stinger_pre_roll_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_pre_roll = std::max(0.0, seconds);
                update_stinger_validation();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(spn_stinger_post_roll_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double seconds) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_post_roll = std::max(0.0, seconds);
                update_stinger_validation();
                emit title_changed(!numeric_label_dragging_);
            });
    connect(cmb_stinger_render_mode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                if (!title_ || loading_values_ || title_->graphic_type != TitleGraphicType::Stinger) return;
                title_->stinger_render_mode = static_cast<StingerRenderMode>(
                    std::clamp(cmb_stinger_render_mode_->itemData(index).toInt(), 0, 1));
                update_stinger_validation();
                emit title_changed(true);
            });
}

bool TitlePropertiesPanel::event(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
        apply_theme_style();
    return QGroupBox::event(event);
}

void TitlePropertiesPanel::apply_theme_style()
{
    if (applying_theme_style_)
        return;

    const QPalette pal = palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor subtle_text = text.lightness() < 128 ? text.lighter(135) : text.darker(135);
    const QColor section_bg = window.lightness() < 128 ? window.lighter(112) : window.darker(104);
    const QColor hover = button.lightness() < 128 ? button.lighter(125) : button.darker(110);

    const QString theme_style = QStringLiteral(
        "QGroupBox{color:%1;background:%2;border:none;border-top:2px solid %3;margin:0;padding-top:4px;font-size:11px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:14px;top:8px;padding:0;background:transparent;}"
        "QDoubleSpinBox,QSpinBox,QComboBox{color:%4;background:%5;border:1px solid %3;"
        "border-radius:2px;padding:1px 3px;selection-background-color:%9;}"
        "QDoubleSpinBox:focus,QSpinBox:focus,QComboBox:focus{border-color:%9;}"
        "QToolButton{color:%6;background:%7;border:1px solid %3;"
        "border-radius:2px;padding:2px;}"
        "QToolButton:hover{background:%8;border-color:%3;}"
        "QToolButton:checked{background:%9;color:%10;border-color:%9;}"
        "QLabel{color:%11;font-size:10px;font-weight:normal;background:transparent;}"
        "QCheckBox{color:%11;font-size:10px;font-weight:normal;background:transparent;}")
        .arg(text.name(QColor::HexRgb),
             section_bg.name(QColor::HexRgb),
             border.name(QColor::HexRgb),
             pal.color(QPalette::Text).name(QColor::HexRgb),
             base.name(QColor::HexRgb),
             button_text.name(QColor::HexRgb),
             button.name(QColor::HexRgb),
             hover.name(QColor::HexRgb),
             highlight.name(QColor::HexRgb),
             highlighted_text.name(QColor::HexRgb),
             subtle_text.name(QColor::HexRgb));

    if (styleSheet() == theme_style)
        return;

    applying_theme_style_ = true;
    setStyleSheet(theme_style);
    applying_theme_style_ = false;
}

void TitlePropertiesPanel::set_title(std::shared_ptr<Title> t)
{
    const bool same_title = title_ && t && title_.get() == t.get();
    title_ = t;
    if (!same_title) {
        previous_non_stinger_graphic_type_ =
            title_ && title_->graphic_type != TitleGraphicType::Stinger
                ? title_->graphic_type
                : TitleGraphicType::Title;
    }
    setTitle(QString());


    load_values();
}

void TitlePropertiesPanel::set_document_format_editable(bool editable)
{
    auto *form = qobject_cast<QFormLayout *>(layout());
    const auto set_row_visible = [form, editable](QWidget *field) {
        if (!field)
            return;
        field->setVisible(editable);
        if (form) {
            if (QWidget *label = form->labelForField(field))
                label->setVisible(editable);
        }
    };
    set_row_visible(resolution_row_);
    set_row_visible(spn_frame_rate_);
}

void TitlePropertiesPanel::set_playhead(double timeline_time)
{
    playhead_ = std::max(0.0, timeline_time);
    load_values();
}

TitleCamera *TitlePropertiesPanel::current_camera()
{
    if (!title_ || title_->cameras.empty()) return nullptr;
    auto it = std::find_if(title_->cameras.begin(), title_->cameras.end(),
        [this](const TitleCamera &camera) {
            return camera.id == title_->active_camera.evaluate(playhead_);
        });
    return it == title_->cameras.end() ? &title_->cameras.front() : &*it;
}

const TitleCamera *TitlePropertiesPanel::current_camera() const
{
    if (!title_ || title_->cameras.empty()) return nullptr;
    auto it = std::find_if(title_->cameras.begin(), title_->cameras.end(),
        [this](const TitleCamera &camera) {
            return camera.id == title_->active_camera.evaluate(playhead_);
        });
    return it == title_->cameras.end() ? &title_->cameras.front() : &*it;
}

std::string TitlePropertiesPanel::unique_camera_name(const std::string &base) const
{
    const QString stem = QString::fromStdString(base).trimmed().isEmpty()
        ? QStringLiteral("Camera") : QString::fromStdString(base).trimmed();
    std::set<std::string> used;
    if (title_) {
        for (const TitleCamera &camera : title_->cameras)
            used.insert(camera.name);
    }
    QString candidate = stem + QStringLiteral(" Copy");
    if (used.find(candidate.toStdString()) == used.end())
        return candidate.toStdString();
    for (int suffix = 2; suffix < 10000; ++suffix) {
        candidate = QStringLiteral("%1 Copy %2").arg(stem).arg(suffix);
        if (used.find(candidate.toStdString()) == used.end())
            return candidate.toStdString();
    }
    return (stem + QStringLiteral(" ") +
            QString::fromStdString(TitleDataStore::make_uuid().substr(0, 8))).toStdString();
}

void TitlePropertiesPanel::insert_camera_copy(const TitleCamera &source)
{
    if (!title_ || loading_values_)
        return;
    TitleCamera camera = source;
    camera.id = TitleDataStore::make_uuid();
    camera.name = unique_camera_name(source.name);
    camera.asset_space_owner_id.clear();
    const std::string camera_id = camera.id;
    title_->cameras.push_back(std::move(camera));
    if (title_->active_camera.is_animated()) {
        title_->active_camera.set(playhead_, camera_id);
    } else {
        title_->active_camera.static_value = camera_id;
        title_->active_camera_id = camera_id;
    }
    load_values();
    emit title_changed(true);
}

TitleLight *TitlePropertiesPanel::current_light()
{
    if (!title_ || !cmb_light_) return nullptr;
    const std::string id = cmb_light_->currentData().toString().toStdString();
    for (auto &layer : title_->layers) if (layer && layer->type == LayerType::Light && layer->id == id) return &layer->light;
    for (auto &layer : title_->layers) if (layer && layer->type == LayerType::Light) return &layer->light;
    return nullptr;
}

const TitleLight *TitlePropertiesPanel::current_light() const
{
    if (!title_ || !cmb_light_) return nullptr;
    const std::string id = cmb_light_->currentData().toString().toStdString();
    for (const auto &layer : title_->layers) if (layer && layer->type == LayerType::Light && layer->id == id) return &layer->light;
    for (const auto &layer : title_->layers) if (layer && layer->type == LayerType::Light) return &layer->light;
    return nullptr;
}

std::string TitlePropertiesPanel::unique_light_name(const std::string &base) const
{
    const QString stem = QString::fromStdString(base).trimmed().isEmpty()
        ? QStringLiteral("Light") : QString::fromStdString(base).trimmed();
    std::set<std::string> used;
    if (title_) {
        for (const auto &layer : title_->layers)
            if (layer && layer->type == LayerType::Light) used.insert(layer->name);
    }
    if (used.find(stem.toStdString()) == used.end())
        return stem.toStdString();
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(stem).arg(suffix);
        if (used.find(candidate.toStdString()) == used.end())
            return candidate.toStdString();
    }
    return (stem + QStringLiteral(" ") +
            QString::fromStdString(TitleDataStore::make_uuid().substr(0, 8))).toStdString();
}

void TitlePropertiesPanel::load_values()
{
    loading_values_ = true;
    double duration = title_ ? title_->duration : 5.0;
    double loop_start = title_ ? title_->loop_start : 1.0;
    double loop_end = title_ ? title_->loop_end : 4.0;
    int playback_mode = title_ ? std::clamp(title_->playback_mode, 0, 2) : 0;
    int loop_type = title_ ? std::clamp(title_->loop_type, 0, 1) : 0;
    int cue_end_behavior = title_ ? std::clamp(title_->cue_end_behavior, 0, 2) : 0;
    int playback_selection = title_ && title_->graphic_type == TitleGraphicType::Stinger
                                 ? 4
                                 : (playback_mode == 1 ? (loop_type == 1 ? 2 : 1)
                                                       : (playback_mode == 2 ? 3 : 0));
    double pause_time = title_ ? std::clamp(title_->pause_time, 0.0, duration) : 0.0;

    if (auto *button = grp_playback_mode_->button(playback_selection))
        button->setChecked(true);
    spn_duration_->setValue(duration);
    if (spn_width_) spn_width_->setValue(title_ ? title_->width : 1920);
    if (spn_height_) spn_height_->setValue(title_ ? title_->height : 1080);
    if (spn_frame_rate_)
        spn_frame_rate_->setValue(title_ ? title_->frame_rate : 30.0);
    if (cmb_cue_end_behavior_) {
        int idx = cmb_cue_end_behavior_->findData(cue_end_behavior);
        cmb_cue_end_behavior_->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    spn_loop_start_->setMaximum(duration);
    spn_loop_end_->setMaximum(duration);
    spn_loop_start_->setValue(std::clamp(loop_start, 0.0, duration));
    spn_loop_end_->setValue(std::clamp(loop_end, std::clamp(loop_start, 0.0, duration), duration));
    spn_pause_frame_->setMaximum(duration);
    spn_pause_frame_->setValue(pause_time);

    if (title_ && title_->cameras.empty())
        title_->cameras.push_back(TitleCamera{});
    if (cmb_camera_) {
        cmb_camera_->clear();
        if (title_) {
            for (const TitleCamera &camera : title_->cameras) {
                if (!camera.asset_space_owner_id.empty())
                    continue;
                cmb_camera_->addItem(QString::fromStdString(camera.name),
                                     QString::fromStdString(camera.id));
            }
            const std::string evaluated_camera = title_->active_camera.evaluate(playhead_);
            int camera_index = cmb_camera_->findData(
                QString::fromStdString(evaluated_camera));
            if (camera_index < 0 && cmb_camera_->count() > 0)
                camera_index = 0;
            cmb_camera_->setCurrentIndex(camera_index);
        }
    }
    const TitleCamera *camera = current_camera();
    const bool has_camera = camera != nullptr;
    if (camera_box_) camera_box_->setEnabled(has_camera);
    if (act_camera_duplicate_) act_camera_duplicate_->setEnabled(has_camera);
    if (act_camera_copy_) act_camera_copy_->setEnabled(has_camera);
    if (act_camera_paste_) act_camera_paste_->setEnabled(camera_clipboard_ != nullptr);
    if (camera) {
        chk_camera_canvas_default_->setChecked(camera->use_canvas_default);
        const int projection_index = cmb_camera_projection_->findData(
            static_cast<int>(camera->projection_mode.evaluate(playhead_) >= 0.5
                ? CameraProjection::Orthographic : CameraProjection::Perspective));
        cmb_camera_projection_->setCurrentIndex(projection_index >= 0 ? projection_index : 0);
        const Vec3Value camera_position = evaluated_camera_position_3d(*camera, playhead_);
        const Vec3Value camera_target = evaluated_camera_target_3d(*camera, playhead_);
        spn_camera_pos_x_->setValue(camera_position.x);
        spn_camera_pos_y_->setValue(camera_position.y);
        spn_camera_pos_z_->setValue(camera_position.z);
        spn_camera_target_x_->setValue(camera_target.x);
        spn_camera_target_y_->setValue(camera_target.y);
        spn_camera_target_z_->setValue(camera_target.z);
        spn_camera_orientation_x_->setValue(camera->orientation_x.evaluate(playhead_));
        spn_camera_orientation_y_->setValue(camera->orientation_y.evaluate(playhead_));
        spn_camera_orientation_z_->setValue(camera->orientation_z.evaluate(playhead_));
        spn_camera_rot_x_->setValue(camera->rotation_x.evaluate(playhead_));
        spn_camera_rot_y_->setValue(camera->rotation_y.evaluate(playhead_));
        spn_camera_rot_z_->setValue(camera->rotation_z.evaluate(playhead_));
        spn_camera_focal_->setValue(camera->focal_length.evaluate(playhead_));
        spn_camera_fov_->setValue(camera->field_of_view.evaluate(playhead_));
        spn_camera_zoom_->setValue(camera->zoom.evaluate(playhead_));
        spn_camera_near_->setValue(camera->near_clip.evaluate(playhead_));
        spn_camera_far_->setMinimum(camera->near_clip.evaluate(playhead_) + 0.001);
        spn_camera_far_->setValue(std::max(camera->far_clip.evaluate(playhead_),
                                           camera->near_clip.evaluate(playhead_) + 0.001));
        auto set_camera_keyframe_icon = [](QToolButton *button, bool active,
                                           bool has_keyframes) {
            if (!button) return;
            fxm_refresh_keyframe_navigation(button);
            const bool outlined = has_keyframes && !active;
            button->setIcon(fxm_keyframe_diamond_icon(active, outlined));
            button->setProperty("active", active);
            button->setProperty("outlined", outlined);
            button->style()->unpolish(button);
            button->style()->polish(button);
        };
        auto set_camera_scalar_keyframe_icon =
            [&](QToolButton *button, const AnimatedProperty &property) {
                set_camera_keyframe_icon(
                    button, keyframe_at_time(property, playhead_),
                    property.is_animated());
            };
        auto set_camera_scalar_group_keyframe_icon =
            [&](QToolButton *button,
                std::initializer_list<const AnimatedProperty *> properties) {
                set_camera_keyframe_icon(
                    button, any_keyframe_at_time(properties, playhead_),
                    any_keyframes(properties));
            };
        auto set_camera_vector_keyframe_icon =
            [&](QToolButton *button, const AnimatedVec3Property &vector,
                bool vector_enabled, const AnimatedProperty &x,
                const AnimatedProperty &y, const AnimatedProperty &z) {
                const bool vector_active = keyframe_at_time(vector, playhead_);
                const bool legacy_active = any_keyframe_at_time(
                    {&x, &y, &z}, playhead_);
                const bool active = vector_enabled ? vector_active
                                                   : vector_active || legacy_active;
                const bool animated = vector_enabled ? vector.is_animated()
                    : vector.is_animated() || any_keyframes({&x, &y, &z});
                set_camera_keyframe_icon(button, active, animated);
            };
        set_camera_scalar_keyframe_icon(btn_kf_camera_projection_,
                                        camera->projection_mode);
        set_camera_vector_keyframe_icon(
            btn_kf_camera_position_, camera->position_3d,
            camera->position_3d_path_enabled, camera->position_x,
            camera->position_y, camera->position_z);
        set_camera_vector_keyframe_icon(
            btn_kf_camera_target_, camera->target_3d,
            camera->target_3d_path_enabled, camera->target_x,
            camera->target_y, camera->target_z);
        set_camera_scalar_group_keyframe_icon(
            btn_kf_camera_orientation_, {&camera->orientation_x,
                                         &camera->orientation_y,
                                         &camera->orientation_z});
        set_camera_scalar_group_keyframe_icon(
            btn_kf_camera_rotation_, {&camera->rotation_x,
                                      &camera->rotation_y,
                                      &camera->rotation_z});
        set_camera_scalar_keyframe_icon(btn_kf_camera_focal_,
                                        camera->focal_length);
        set_camera_scalar_keyframe_icon(btn_kf_camera_fov_,
                                        camera->field_of_view);
        set_camera_scalar_keyframe_icon(btn_kf_camera_zoom_, camera->zoom);
        set_camera_scalar_keyframe_icon(btn_kf_camera_near_, camera->near_clip);
        set_camera_scalar_keyframe_icon(btn_kf_camera_far_, camera->far_clip);
        const bool custom = !camera->use_canvas_default;
        for (QWidget *widget : {static_cast<QWidget *>(spn_camera_pos_x_),
                                static_cast<QWidget *>(spn_camera_pos_y_),
                                static_cast<QWidget *>(spn_camera_pos_z_),
                                static_cast<QWidget *>(spn_camera_target_x_),
                                static_cast<QWidget *>(spn_camera_target_y_),
                                static_cast<QWidget *>(spn_camera_target_z_),
                                static_cast<QWidget *>(spn_camera_orientation_x_),
                                static_cast<QWidget *>(spn_camera_orientation_y_),
                                static_cast<QWidget *>(spn_camera_orientation_z_),
                                static_cast<QWidget *>(spn_camera_rot_x_),
                                static_cast<QWidget *>(spn_camera_rot_y_),
                                static_cast<QWidget *>(spn_camera_rot_z_)})
            widget->setEnabled(custom);
        btn_camera_delete_->setEnabled(title_->cameras.size() > 1 && camera->id != "default");
    }

    if (chk_lighting_enabled_)
        chk_lighting_enabled_->setChecked(title_ ? title_->lighting_enabled : true);
    if (chk_default_light_)
        chk_default_light_->setChecked(title_ ? title_->default_light_enabled : true);
    if (spn_environment_exposure_)
        spn_environment_exposure_->setValue(
            title_ ? title_->environment_exposure : 0.0);
    QString selected_light_id;
    if (cmb_light_)
        selected_light_id = cmb_light_->currentData().toString();
    if (cmb_light_) {
        cmb_light_->clear();
        if (title_) {
            for (const auto &layer : title_->layers)
                if (layer && layer->type == LayerType::Light)
                    cmb_light_->addItem(QString::fromStdString(layer->name), QString::fromStdString(layer->id));
        }
        int index = cmb_light_->findData(selected_light_id);
        if (index < 0 && cmb_light_->count() > 0)
            index = 0;
        cmb_light_->setCurrentIndex(index);
    }
    const TitleLight *light = current_light();
    const bool has_light = light != nullptr;
    if (btn_light_add_)
        btn_light_add_->setEnabled(title_ && std::count_if(title_->layers.begin(), title_->layers.end(), [](const auto &layer) { return layer && layer->type == LayerType::Light; }) < 4);
    if (btn_light_delete_) btn_light_delete_->setEnabled(has_light);
    for (QWidget *widget : {
             static_cast<QWidget *>(chk_light_enabled_),
             static_cast<QWidget *>(cmb_light_type_),
             static_cast<QWidget *>(btn_light_color_),
             static_cast<QWidget *>(spn_light_intensity_),
             static_cast<QWidget *>(spn_light_source_size_),
             static_cast<QWidget *>(spn_light_pos_x_),
             static_cast<QWidget *>(spn_light_pos_y_),
             static_cast<QWidget *>(spn_light_pos_z_),
             static_cast<QWidget *>(spn_light_target_x_),
             static_cast<QWidget *>(spn_light_target_y_),
             static_cast<QWidget *>(spn_light_target_z_),
             static_cast<QWidget *>(cmb_light_falloff_),
             static_cast<QWidget *>(spn_light_falloff_distance_),
             static_cast<QWidget *>(spn_light_cone_angle_),
             static_cast<QWidget *>(spn_light_cone_feather_),
             static_cast<QWidget *>(chk_light_casts_shadows_),
             static_cast<QWidget *>(spn_light_shadow_darkness_),
             static_cast<QWidget *>(spn_light_shadow_softness_),
             static_cast<QWidget *>(spn_light_shadow_bias_)}) {
        if (widget) widget->setEnabled(has_light);
    }
    if (light) {
        chk_light_enabled_->setChecked(light->enabled);
        const int type_index = cmb_light_type_->findData(
            static_cast<int>(light->type));
        cmb_light_type_->setCurrentIndex(type_index >= 0 ? type_index : 0);
        const uint32_t argb = evaluated_light_color(*light, playhead_);
        const QColor light_color = QColor::fromRgba(static_cast<QRgb>(argb));
        btn_light_color_->setText(light_color.name(QColor::HexRgb).toUpper());
        btn_light_color_->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid palette(mid);"
            "border-radius:2px;font-size:10px;}")
            .arg(light_color.name(QColor::HexArgb),
                 light_color.lightnessF() < 0.5 ? QStringLiteral("white")
                                                : QStringLiteral("black")));
        spn_light_intensity_->setValue(light->intensity.evaluate(playhead_));
        spn_light_source_size_->setValue(
            light->source_size.evaluate(playhead_));
        const Vec3Value position = evaluated_light_position(*light, playhead_);
        const Vec3Value target = evaluated_light_target(*light, playhead_);
        spn_light_pos_x_->setValue(position.x);
        spn_light_pos_y_->setValue(position.y);
        spn_light_pos_z_->setValue(position.z);
        spn_light_target_x_->setValue(target.x);
        spn_light_target_y_->setValue(target.y);
        spn_light_target_z_->setValue(target.z);
        const int falloff_index = cmb_light_falloff_->findData(
            static_cast<int>(light->falloff));
        cmb_light_falloff_->setCurrentIndex(
            falloff_index >= 0 ? falloff_index : 0);
        spn_light_falloff_distance_->setValue(
            light->falloff_distance.evaluate(playhead_));
        spn_light_cone_angle_->setValue(light->cone_angle.evaluate(playhead_));
        spn_light_cone_feather_->setValue(light->cone_feather.evaluate(playhead_));
        chk_light_casts_shadows_->setChecked(light->casts_shadows);
        spn_light_shadow_darkness_->setValue(
            light->shadow_darkness.evaluate(playhead_));
        spn_light_shadow_softness_->setValue(
            light->shadow_softness.evaluate(playhead_));
        spn_light_shadow_bias_->setValue(light->shadow_bias.evaluate(playhead_));

        const bool positional = light->type == TitleLightType::Point ||
                                light->type == TitleLightType::Spot ||
                                light->type == TitleLightType::Parallel;
        const bool distance_falloff = light->type == TitleLightType::Point ||
                                      light->type == TitleLightType::Spot;
        const bool spot = light->type == TitleLightType::Spot;
        const bool shadow_capable = distance_falloff ||
                                    light->type == TitleLightType::Parallel;
        const bool finite_source = distance_falloff ||
                                   light->type == TitleLightType::Parallel;
        for (QWidget *widget : {static_cast<QWidget *>(spn_light_pos_x_),
                                static_cast<QWidget *>(spn_light_pos_y_),
                                static_cast<QWidget *>(spn_light_pos_z_),
                                static_cast<QWidget *>(spn_light_target_x_),
                                static_cast<QWidget *>(spn_light_target_y_),
                                static_cast<QWidget *>(spn_light_target_z_)})
            widget->setEnabled(positional);
        cmb_light_falloff_->setEnabled(distance_falloff);
        spn_light_source_size_->setEnabled(finite_source);
        spn_light_falloff_distance_->setEnabled(
            distance_falloff && light->falloff != TitleLightFalloff::None);
        spn_light_cone_angle_->setEnabled(spot);
        spn_light_cone_feather_->setEnabled(spot);
        chk_light_casts_shadows_->setEnabled(shadow_capable);
        spn_light_shadow_darkness_->setEnabled(
            shadow_capable && light->casts_shadows);
        spn_light_shadow_softness_->setEnabled(
            shadow_capable && light->casts_shadows);
        spn_light_shadow_bias_->setEnabled(
            shadow_capable && light->casts_shadows);
    }

    bool show_loop = playback_mode == 1;
    bool show_pause = playback_mode == 2;
    auto *form = qobject_cast<QFormLayout *>(layout());
    loop_area_row_->setVisible(show_loop);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(loop_area_row_))) label->setVisible(show_loop);
    spn_pause_frame_->setVisible(show_pause);
    if (form) if (auto *label = qobject_cast<QWidget *>(form->labelForField(spn_pause_frame_))) label->setVisible(show_pause);

    const bool is_stinger = title_ && title_->graphic_type == TitleGraphicType::Stinger;
    auto set_form_field_visible = [form](QWidget *field, bool visible) {
        if (!field)
            return;
        field->setVisible(visible);
        if (form) {
            if (auto *label = qobject_cast<QWidget *>(form->labelForField(field)))
                label->setVisible(visible);
        }
    };
    const bool point_switch = is_stinger &&
        title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint;
    set_form_field_visible(cmb_stinger_switch_mode_, is_stinger);
    set_form_field_visible(spn_stinger_transition_timecode_, point_switch);
    set_form_field_visible(chk_stinger_audio_, is_stinger);
    set_form_field_visible(chk_stinger_alpha_, is_stinger);
    set_form_field_visible(spn_stinger_pre_roll_, is_stinger);
    set_form_field_visible(spn_stinger_post_roll_, is_stinger);
    set_form_field_visible(cmb_stinger_render_mode_, is_stinger);
    set_form_field_visible(lbl_stinger_validation_, is_stinger);
    if (is_stinger) {
        const int switch_mode = std::clamp(static_cast<int>(title_->stinger_switch_mode), 0, 1);
        const int switch_index = cmb_stinger_switch_mode_->findData(switch_mode);
        cmb_stinger_switch_mode_->setCurrentIndex(switch_index >= 0 ? switch_index : 0);
        const double point = stinger_transition_point_seconds(*title_);
        spn_stinger_transition_timecode_->setMaximum(title_->duration);
        spn_stinger_transition_timecode_->setValue(point);
        chk_stinger_audio_->setChecked(title_->stinger_audio_enabled);
        chk_stinger_alpha_->setChecked(title_->stinger_alpha_output);
        spn_stinger_pre_roll_->setValue(std::max(0.0, title_->stinger_pre_roll));
        spn_stinger_post_roll_->setValue(std::max(0.0, title_->stinger_post_roll));
        const int render_mode = std::clamp(static_cast<int>(title_->stinger_render_mode), 0, 1);
        const int render_index = cmb_stinger_render_mode_->findData(render_mode);
        cmb_stinger_render_mode_->setCurrentIndex(render_index >= 0 ? render_index : 0);
    }
    loading_values_ = false;
    update_stinger_validation();
}

void TitlePropertiesPanel::update_stinger_validation()
{
    if (!lbl_stinger_validation_)
        return;
    if (!title_ || title_->graphic_type != TitleGraphicType::Stinger) {
        lbl_stinger_validation_->clear();
        return;
    }

    const StingerValidationResult validation = validate_stinger_title(*title_);
    QStringList lines;
    for (const auto &error : validation.errors)
        lines << QStringLiteral("<span style='color:#e35d6a'>● %1</span>")
                     .arg(QString::fromStdString(error).toHtmlEscaped());
    for (const auto &warning : validation.warnings)
        lines << QStringLiteral("<span style='color:#d6a94d'>▲ %1</span>")
                     .arg(QString::fromStdString(warning).toHtmlEscaped());
    if (lines.isEmpty())
        lines << QStringLiteral("<span style='color:#55b87a'>✓ %1</span>")
                     .arg(fxm_tr("OBSTitles.StingerValidationOk").toHtmlEscaped());
    lbl_stinger_validation_->setText(lines.join(QStringLiteral("<br/>")));
}

/* ══════════════════════════════════════════════════════════════════
 *  PropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
