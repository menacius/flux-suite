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
#include <memory>
#include <string>
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
/* ══════════════════════════════════════════════════════════════════
 *  LayerStack  – AE-style layer list on the left of the timeline
 * ══════════════════════════════════════════════════════════════════ */
class LayerStack : public QWidget {
    Q_OBJECT

public:
    explicit LayerStack(QWidget *parent = nullptr);

    void set_title(std::shared_ptr<Title> t);
    void refresh();
    void set_selected_layer(const std::string &layer_id);
    void set_selected_layers(const std::vector<std::string> &layer_ids);
    void set_layer_clipboard_available(bool available);
    void set_playhead(double timeline_time);
    QScrollBar *vertical_scroll_bar() const;
    std::vector<std::string> selected_ids() const;

signals:
    void layer_selected(const std::string &layer_id);
    void layers_selected(const std::vector<std::string> &layer_ids);
    void layer_visibility_changed(const std::string &layer_id, bool v);
    void layer_audio_mute_changed(const std::string &layer_id, bool muted);
    void layer_audio_volume_changed(const std::string &layer_id, float volume);
    void layer_matte_visibility_changed(const std::string &layer_id, MatteVisibilityMode mode);
    void layer_lock_changed(const std::string &layer_id, bool locked);
    void layer_expand_changed(const std::string &layer_id, bool expanded);
    void camera_expand_changed(const std::string &owner_id, bool expanded);
    void property_channels_expanded_changed(const std::string &owner_id,
                                             const std::string &property_name,
                                             bool expanded);
    void group_expansion_state_changed(const std::string &layer_id, int state);
    void group_layers_requested();
    void ungroup_layers_requested();
    void add_to_group_requested(const std::string &group_id);
    void remove_from_group_requested();
    void layer_rows_dropped(const std::vector<std::string> &layer_ids,
                            const std::string &target_layer_id,
                            int placement);
    void layer_parent_changed(const std::string &layer_id, const std::string &parent_id);
    void layer_mask_changed(const std::string &layer_id, const std::string &mask_source_id, MaskMode mask_mode);
    void layer_blend_mode_changed(const std::string &layer_id, EffectBlendMode blend_mode);
    void layer_effects_enabled_changed(const std::string &layer_id, bool enabled);
    void layer_dimension_mode_changed(const std::string &layer_id, LayerDimensionMode mode);
    void layer_ui_color_changed(const std::string &layer_id, bool enabled,
                                uint32_t argb);
    void layer_name_changed(const std::string &layer_id, const std::string &name);
    void layer_order_changed();
    void add_layer_requested(LayerType type);
    void add_camera_requested();
    void add_light_requested(TitleLightType type);
    void clone_layer_requested(const std::string &layer_id);
    void copy_layer_requested(const std::string &layer_id);
    void paste_layer_requested(const std::string &layer_id);
    void delete_layer_requested(const std::string &layer_id);
    void property_keyframe_toggled(const std::string &layer_id, const std::string &property_name);
    void property_value_changed(const std::string &layer_id, const std::string &property_name, double x, double y, double z);
    void property_channel_value_changed(const std::string &layer_id, const std::string &property_name,
                                        int channel, double value);
    void property_graph_target_requested(const std::string &layer_id,
                                         const std::string &property_name,
                                         int channel_mode);
    void property_temporal_mode_changed(const std::string &layer_id, const std::string &property_name, int mode);
    void property_easy_ease_requested(const std::string &layer_id, const std::string &property_name, bool ease_in, bool ease_out);
    void property_velocity_requested(const std::string &layer_id, const std::string &property_name);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void on_add_text();
    void on_add_clock();
    void on_add_ticker();
    void on_add_rect();
    void on_add_image();
    void on_add_video();
    void on_add_audio();
    void on_add_empty();
    void on_add_chart();
    void on_add_adjustment();
    void on_add_color_solid();
    void on_add_camera();
    void on_add_ambient_light();
    void on_add_point_light();
    void on_add_spot_light();
    void on_add_parallel_light();
    void on_add_environment_light();
    void on_move_up();
    void on_move_down();
    void on_delete();
    void on_item_changed(QListWidgetItem *item);
    void on_selection_changed();
    void show_layer_context_menu(const QPoint &pos);

private:
    void populate();
    void update_property_rows();
    void sync_order_from_list();
    void refresh_layer_row_backgrounds();
    std::string selected_id() const;

    std::shared_ptr<Title> title_;
    QListWidget  *list_     = nullptr;
    QToolButton  *btn_add_  = nullptr;
    QToolButton  *btn_move_up_ = nullptr;
    QToolButton  *btn_move_down_ = nullptr;
    QToolButton  *btn_del_       = nullptr;
    bool          layer_clipboard_available_ = false;
    double        playhead_ = 0.0;
};
