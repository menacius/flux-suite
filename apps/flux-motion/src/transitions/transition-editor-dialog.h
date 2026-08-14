#pragma once

#include "layer-transition.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QWidget;

class TransitionEditorDialog final : public QDialog {
public:
    explicit TransitionEditorDialog(const LayerTransition &transition,
                                    double maximum_duration,
                                    QWidget *parent = nullptr);

    LayerTransition transition() const;

private:
    void update_model_from_controls();
    void update_control_visibility();

    LayerTransition transition_;
    QWidget *preview_ = nullptr;
    QCheckBox *enabled_ = nullptr;
    QDoubleSpinBox *duration_ = nullptr;
    QComboBox *easing_ = nullptr;
    QComboBox *unit_ = nullptr;
    QComboBox *direction_ = nullptr;
    QDoubleSpinBox *stagger_ = nullptr;
    QDoubleSpinBox *blur_ = nullptr;
    QDoubleSpinBox *scale_ = nullptr;
    QDoubleSpinBox *offset_ = nullptr;
    QDoubleSpinBox *softness_ = nullptr;
    QCheckBox *reverse_order_ = nullptr;
    QCheckBox *text_slide_fade_ = nullptr;
    QCheckBox *text_slide_crop_to_unit_bounds_ = nullptr;
    QSpinBox *blocks_columns_ = nullptr;
    QSpinBox *blocks_rows_ = nullptr;
    QSpinBox *random_seed_ = nullptr;
    QLineEdit *image_path_ = nullptr;
    QPushButton *image_browse_ = nullptr;
    QComboBox *image_channel_ = nullptr;
    QCheckBox *invert_ = nullptr;
    QCheckBox *clockwise_ = nullptr;
    QDoubleSpinBox *center_x_ = nullptr;
    QDoubleSpinBox *center_y_ = nullptr;
    QDoubleSpinBox *rotation_ = nullptr;
    QDoubleSpinBox *aspect_ = nullptr;
    QComboBox *profile_ = nullptr;
    QLabel *unit_label_ = nullptr;
    QLabel *direction_label_ = nullptr;
    QLabel *stagger_label_ = nullptr;
    QLabel *blur_label_ = nullptr;
    QLabel *scale_label_ = nullptr;
    QLabel *offset_label_ = nullptr;
    QLabel *softness_label_ = nullptr;
    QLabel *blocks_columns_label_ = nullptr;
    QLabel *blocks_rows_label_ = nullptr;
    QLabel *random_seed_label_ = nullptr;
    QLabel *image_path_label_ = nullptr;
    QLabel *image_channel_label_ = nullptr;
    QLabel *center_x_label_ = nullptr;
    QLabel *center_y_label_ = nullptr;
    QLabel *rotation_label_ = nullptr;
    QLabel *aspect_label_ = nullptr;
    QLabel *profile_label_ = nullptr;
};
