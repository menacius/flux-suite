#include "title-editor-internal.h"
#include "fxm-modern-controls.h"
#include "effect-preset-catalog.h"
#include "effect-animation-utils.h"
#include "effect-runtime.h"
#include "extensions/effect-extension-catalog.h"

#include <QHash>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFrame>
#include <QDir>
#include <QMetaType>
#include <QMessageBox>
#include <QScopedValueRollback>
#include <QSet>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QClipboard>
#include <QApplication>
#include <QSettings>
#include <QSaveFile>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QPainter>
#include <QRegularExpression>
#include <array>
#include <optional>
#include <cmath>
#include <algorithm>
#include <functional>
#include <utility>


static QString effect_display_name(const LayerEffect &effect)
{
    if (!effect.extension_id.empty()) {
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty()) catalog.reload();
        if (const auto *definition = catalog.find(QString::fromStdString(effect.extension_id)))
            return definition->displayName;
        return QString::fromStdString(effect.extension_id);
    }
    return effect_type_name(effect.type);
}

static QString fxm_effects_panel_style()
{
    const QPalette pal = qApp->palette();
    const QColor window = pal.color(QPalette::Window);
    const QColor window_text = pal.color(QPalette::WindowText);
    const QColor base = pal.color(QPalette::Base);
    const QColor text = pal.color(QPalette::Text);
    const QColor button = pal.color(QPalette::Button);
    const QColor button_text = pal.color(QPalette::ButtonText);
    const QColor mid = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor alternate = pal.color(QPalette::AlternateBase);
    const QColor hover = button.lightness() < 128 ? button.lighter(122) : button.darker(108);
    const QColor disabled = window_text.lightness() < 128 ? window_text.lighter(160) : window_text.darker(160);

    QString css = QStringLiteral(
        "QWidget#FluxMotionEffectsPanel{background:@window@;color:@windowText@;}"
        "QLabel{color:@windowText@;background:transparent;}"
        "QListWidget{background:@base@;border:1px solid @mid@;color:@text@;alternate-background-color:@alternate@;}"
        "QListWidget::item{padding:4px;}"
        "QListWidget::item:selected{background:@highlight@;color:@highlightedText@;}"
        "QToolButton{color:@buttonText@;background:@button@;border:1px solid @mid@;border-radius:2px;padding:1px 6px;font-size:10px;min-height:18px;max-height:20px;}"
        "QToolButton:hover{background:@hover@;border-color:@mid@;}"
        "QToolButton:pressed{background:@highlight@;color:@highlightedText@;border-color:@highlight@;}"
        "QToolButton:checked{background:@highlight@;color:@highlightedText@;border-color:@highlight@;}"
        "QScrollArea{background:@window@;border:none;}"
        "QWidget#FluxMotionEffectsSettingsContainer{background:@window@;}"
        "QMenu{color:@windowText@;background:@window@;border:1px solid @mid@;}"
        "QMenu::item{padding:5px 22px;}"
        "QMenu::item:selected{background:@highlight@;color:@highlightedText@;}"
        "QMenu::item:disabled{color:@disabled@;}");
    css.replace(QStringLiteral("@window@"), window.name(QColor::HexRgb));
    css.replace(QStringLiteral("@windowText@"), window_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@base@"), base.name(QColor::HexRgb));
    css.replace(QStringLiteral("@text@"), text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@button@"), button.name(QColor::HexRgb));
    css.replace(QStringLiteral("@buttonText@"), button_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@mid@"), mid.name(QColor::HexRgb));
    css.replace(QStringLiteral("@highlight@"), highlight.name(QColor::HexRgb));
    css.replace(QStringLiteral("@highlightedText@"), highlighted_text.name(QColor::HexRgb));
    css.replace(QStringLiteral("@alternate@"), alternate.name(QColor::HexRgb));
    css.replace(QStringLiteral("@hover@"), hover.name(QColor::HexRgb));
    css.replace(QStringLiteral("@disabled@"), disabled.name(QColor::HexRgb));
    return css;
}

static QString fxm_theme_control_style()
{
    return fxm_transform_panel_control_style(qApp->palette());
}


static uint32_t color_button_argb(QPushButton *button)
{
    return button ? button->property("argb").toUInt() : 0xFFFFFFFF;
}

static void set_color_button_argb(QPushButton *button, uint32_t argb)
{
    if (!button) return;
    button->setProperty("argb", argb);
    QColor c = color_from_argb(argb);
    button->setText(c.name(QColor::HexArgb).toUpper());
    const QColor border = qApp->palette().color(QPalette::Mid);
    const QColor text = c.lightness() < 128 ? QColor(Qt::white) : QColor(Qt::black);
    button->setStyleSheet(QStringLiteral("QPushButton{color:%1;background:%2;border:1px solid %3;border-radius:3px;padding:3px 8px;}")
                          .arg(text.name(QColor::HexRgb), c.name(QColor::HexRgb), border.name(QColor::HexRgb)));
}

static uint32_t panel_eval_effect_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.color_a, (effect.effect_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.color_r, (effect.effect_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.color_g, (effect.effect_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.color_b, effect.effect_color & 0xFF, t);
}

static uint32_t panel_eval_effect_stroke_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.stroke_color_a, (effect.effect_stroke_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.stroke_color_r, (effect.effect_stroke_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.stroke_color_g, (effect.effect_stroke_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.stroke_color_b, effect.effect_stroke_color & 0xFF, t);
}

static uint32_t panel_eval_gradient_start_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.gradient_start_color_a, (effect.effect_gradient_start_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.gradient_start_color_r, (effect.effect_gradient_start_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.gradient_start_color_g, (effect.effect_gradient_start_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.gradient_start_color_b, effect.effect_gradient_start_color & 0xFF, t);
}

static uint32_t panel_eval_gradient_end_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.gradient_end_color_a, (effect.effect_gradient_end_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.gradient_end_color_r, (effect.effect_gradient_end_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.gradient_end_color_g, (effect.effect_gradient_end_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.gradient_end_color_b, effect.effect_gradient_end_color & 0xFF, t);
}

static void set_gradient_start_color_channels_at(LayerEffect &effect, double time, uint32_t argb)
{
    set_animated_value(effect.gradient_start_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(effect.gradient_start_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(effect.gradient_start_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(effect.gradient_start_color_b, time, argb & 0xFF);
}

static void set_gradient_end_color_channels_at(LayerEffect &effect, double time, uint32_t argb)
{
    set_animated_value(effect.gradient_end_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(effect.gradient_end_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(effect.gradient_end_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(effect.gradient_end_color_b, time, argb & 0xFF);
}

static double panel_eval_effect_property(const AnimatedProperty &prop, double fallback, double t)
{
    return prop.is_animated() ? prop.evaluate(t) : fallback;
}

static void set_effect_color_channels_at(LayerEffect &effect, double time, uint32_t argb)
{
    set_animated_value(effect.color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(effect.color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(effect.color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(effect.color_b, time, argb & 0xFF);
}

static void set_effect_stroke_color_channels_at(LayerEffect &effect, double time, uint32_t argb)
{
    set_animated_value(effect.stroke_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(effect.stroke_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(effect.stroke_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(effect.stroke_color_b, time, argb & 0xFF);
}

static uint32_t panel_eval_effect_secondary_color(const LayerEffect &effect, double t)
{
    return ((uint32_t)eval_channel(effect.secondary_color_a, (effect.effect_secondary_color >> 24) & 0xFF, t) << 24) |
           ((uint32_t)eval_channel(effect.secondary_color_r, (effect.effect_secondary_color >> 16) & 0xFF, t) << 16) |
           ((uint32_t)eval_channel(effect.secondary_color_g, (effect.effect_secondary_color >> 8) & 0xFF, t) << 8) |
           (uint32_t)eval_channel(effect.secondary_color_b, effect.effect_secondary_color & 0xFF, t);
}

static void set_effect_secondary_color_channels_at(LayerEffect &effect, double time, uint32_t argb)
{
    set_animated_value(effect.secondary_color_a, time, (argb >> 24) & 0xFF);
    set_animated_value(effect.secondary_color_r, time, (argb >> 16) & 0xFF);
    set_animated_value(effect.secondary_color_g, time, (argb >> 8) & 0xFF);
    set_animated_value(effect.secondary_color_b, time, argb & 0xFF);
}

static bool reset_effect_scalar_parameter(
    LayerEffect &effect, AnimatedProperty LayerEffect::*property)
{
    const LayerEffect defaults = fxm::effects::make_default_layer_effect(effect.type);
#define FXM_RESET_EFFECT_SCALAR(prop_name, field_name)                         \
    if (property == &LayerEffect::prop_name) {                                \
        effect.field_name = defaults.field_name;                              \
        effect.prop_name = defaults.prop_name;                                \
        return true;                                                          \
    }
    FXM_RESET_EFFECT_SCALAR(brightness_prop, brightness)
    FXM_RESET_EFFECT_SCALAR(contrast_prop, contrast)
    FXM_RESET_EFFECT_SCALAR(saturation_prop, saturation)
    FXM_RESET_EFFECT_SCALAR(opacity_prop, effect_opacity)
    FXM_RESET_EFFECT_SCALAR(size_prop, effect_size)
    FXM_RESET_EFFECT_SCALAR(distance_prop, effect_distance)
    FXM_RESET_EFFECT_SCALAR(angle_prop, effect_angle)
    FXM_RESET_EFFECT_SCALAR(spread_prop, effect_spread)
    FXM_RESET_EFFECT_SCALAR(falloff_prop, effect_falloff)
    FXM_RESET_EFFECT_SCALAR(amount_prop, effect_amount)
    FXM_RESET_EFFECT_SCALAR(scale_prop, effect_scale)
    FXM_RESET_EFFECT_SCALAR(softness_prop, effect_softness)
    FXM_RESET_EFFECT_SCALAR(roundness_prop, effect_roundness)
    FXM_RESET_EFFECT_SCALAR(speed_prop, effect_speed)
    FXM_RESET_EFFECT_SCALAR(center_x_prop, effect_center_x)
    FXM_RESET_EFFECT_SCALAR(center_y_prop, effect_center_y)
    FXM_RESET_EFFECT_SCALAR(complexity_prop, effect_complexity)
    FXM_RESET_EFFECT_SCALAR(evolution_prop, effect_evolution)
    FXM_RESET_EFFECT_SCALAR(stroke_width_prop, effect_stroke_width)
    FXM_RESET_EFFECT_SCALAR(stroke_opacity_prop, effect_stroke_opacity)
    FXM_RESET_EFFECT_SCALAR(trim_start_prop, effect_trim_start)
    FXM_RESET_EFFECT_SCALAR(trim_end_prop, effect_trim_end)
    FXM_RESET_EFFECT_SCALAR(trim_offset_prop, effect_trim_offset)
    FXM_RESET_EFFECT_SCALAR(padding_left_prop, effect_padding_left)
    FXM_RESET_EFFECT_SCALAR(padding_right_prop, effect_padding_right)
    FXM_RESET_EFFECT_SCALAR(padding_top_prop, effect_padding_top)
    FXM_RESET_EFFECT_SCALAR(padding_bottom_prop, effect_padding_bottom)
    FXM_RESET_EFFECT_SCALAR(corner_radius_tl_prop, effect_corner_radius_tl)
    FXM_RESET_EFFECT_SCALAR(corner_radius_tr_prop, effect_corner_radius_tr)
    FXM_RESET_EFFECT_SCALAR(corner_radius_br_prop, effect_corner_radius_br)
    FXM_RESET_EFFECT_SCALAR(corner_radius_bl_prop, effect_corner_radius_bl)
    FXM_RESET_EFFECT_SCALAR(gradient_start_pos_prop, effect_gradient_start_pos)
    FXM_RESET_EFFECT_SCALAR(gradient_end_pos_prop, effect_gradient_end_pos)
    FXM_RESET_EFFECT_SCALAR(gradient_start_opacity_prop, effect_gradient_start_opacity)
    FXM_RESET_EFFECT_SCALAR(gradient_end_opacity_prop, effect_gradient_end_opacity)
    FXM_RESET_EFFECT_SCALAR(gradient_angle_prop, effect_gradient_angle)
    FXM_RESET_EFFECT_SCALAR(gradient_center_x_prop, effect_gradient_center_x)
    FXM_RESET_EFFECT_SCALAR(gradient_center_y_prop, effect_gradient_center_y)
    FXM_RESET_EFFECT_SCALAR(gradient_scale_prop, effect_gradient_scale)
    FXM_RESET_EFFECT_SCALAR(gradient_focal_x_prop, effect_gradient_focal_x)
    FXM_RESET_EFFECT_SCALAR(gradient_focal_y_prop, effect_gradient_focal_y)
    FXM_RESET_EFFECT_SCALAR(gradient_opacity_prop, effect_gradient_opacity)
#undef FXM_RESET_EFFECT_SCALAR
    return false;
}

static bool reset_effect_color_parameter(
    LayerEffect &effect, AnimatedProperty LayerEffect::*alpha_property)
{
    const LayerEffect defaults = fxm::effects::make_default_layer_effect(effect.type);
    if (alpha_property == &LayerEffect::color_a) {
        effect.effect_color = defaults.effect_color;
        effect.color_a = defaults.color_a;
        effect.color_r = defaults.color_r;
        effect.color_g = defaults.color_g;
        effect.color_b = defaults.color_b;
        return true;
    }
    if (alpha_property == &LayerEffect::secondary_color_a) {
        effect.effect_secondary_color = defaults.effect_secondary_color;
        effect.secondary_color_a = defaults.secondary_color_a;
        effect.secondary_color_r = defaults.secondary_color_r;
        effect.secondary_color_g = defaults.secondary_color_g;
        effect.secondary_color_b = defaults.secondary_color_b;
        return true;
    }
    if (alpha_property == &LayerEffect::stroke_color_a) {
        effect.effect_stroke_color = defaults.effect_stroke_color;
        effect.stroke_color_a = defaults.stroke_color_a;
        effect.stroke_color_r = defaults.stroke_color_r;
        effect.stroke_color_g = defaults.stroke_color_g;
        effect.stroke_color_b = defaults.stroke_color_b;
        return true;
    }
    if (alpha_property == &LayerEffect::gradient_start_color_a) {
        effect.effect_gradient_start_color = defaults.effect_gradient_start_color;
        effect.gradient_start_color_a = defaults.gradient_start_color_a;
        effect.gradient_start_color_r = defaults.gradient_start_color_r;
        effect.gradient_start_color_g = defaults.gradient_start_color_g;
        effect.gradient_start_color_b = defaults.gradient_start_color_b;
        return true;
    }
    if (alpha_property == &LayerEffect::gradient_end_color_a) {
        effect.effect_gradient_end_color = defaults.effect_gradient_end_color;
        effect.gradient_end_color_a = defaults.gradient_end_color_a;
        effect.gradient_end_color_r = defaults.gradient_end_color_r;
        effect.gradient_end_color_g = defaults.gradient_end_color_g;
        effect.gradient_end_color_b = defaults.gradient_end_color_b;
        return true;
    }
    return false;
}

static bool effect_property_has_keyframe_at(const AnimatedProperty &property, double time)
{
    return std::any_of(property.keyframes.begin(), property.keyframes.end(),
                       [time](const Keyframe &keyframe) {
                           return std::abs(keyframe.time - time) < 0.0001;
                       });
}

static QPushButton *make_effect_keyframe_button(QWidget *parent,
                                                    const QString &tooltip = {})
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("FluxMotionEffectKeyframeButton"));
    button->setIcon(fxm_keyframe_diamond_icon(false));
    button->setFlat(true);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    fxm_style_keyframe_button(button);
    return button;
}

static void set_effect_keyframe_button_state(QPushButton *button, bool keyed,
                                             bool has_keyframes)
{
    if (!button)
        return;
    button->setText(QString());
    const bool outlined = has_keyframes && !keyed;
    button->setIcon(fxm_keyframe_diamond_icon(keyed, outlined));
    button->setProperty("active", keyed);
    button->setProperty("outlined", outlined);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->setAccessibleDescription(keyed
        ? QObject::tr("A keyframe exists at the current timeline position")
        : has_keyframes
            ? QObject::tr("This property is animated, but no keyframe exists at the current timeline position")
            : QObject::tr("No keyframes exist for this property"));
    fxm_refresh_keyframe_navigation(button);
}

static bool extension_track_has_keyframe_at(const LayerEffect &effect,
                                            const QString &path,
                                            double time)
{
    return fxm::effects::animation::has_keyframe_at(effect, path, time);
}

static std::vector<double> extension_track_keyframe_times(
    const LayerEffect &effect, const QString &path)
{
    std::vector<double> times;
    const QJsonArray keys = fxm::effects::animation::track_keys(effect, path);
    times.reserve(static_cast<size_t>(keys.size()));
    for (const QJsonValue &value : keys)
        times.push_back(value.toObject().value(QStringLiteral("time")).toDouble());
    return times;
}

static uint32_t extension_json_color_to_argb(const QJsonValue &value,
                                              uint32_t fallback = 0xFFFFFFFFu)
{
    if (value.isString()) {
        const QColor color(value.toString());
        return color.isValid() ? argb_from_color(color) : fallback;
    }
    const QJsonArray array = value.toArray();
    if (array.size() < 3)
        return fallback;
    QColor color;
    color.setRgbF(std::clamp(array.at(0).toDouble(1.0), 0.0, 1.0),
                  std::clamp(array.at(1).toDouble(1.0), 0.0, 1.0),
                  std::clamp(array.at(2).toDouble(1.0), 0.0, 1.0),
                  std::clamp(array.size() > 3 ? array.at(3).toDouble(1.0) : 1.0,
                             0.0, 1.0));
    return argb_from_color(color);
}

static QJsonArray extension_argb_to_json_color(uint32_t argb)
{
    const QColor color = color_from_argb(argb);
    return QJsonArray{color.redF(), color.greenF(), color.blueF(), color.alphaF()};
}

static QJsonValue extension_state_path_value(const LayerEffect &effect,
                                               const QString &path)
{
    return fxm::effects::animation::state_path_value(effect, path);
}

static QJsonValue evaluate_extension_track(const LayerEffect &effect,
                                           const QString &path,
                                           double time,
                                           const QJsonValue &fallback)
{
    return fxm::effects::animation::evaluate_track(effect, path, time, fallback);
}

EffectsPanel::EffectsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("FluxMotionEffectsPanel"));
    setAcceptDrops(true);
    setStyleSheet(fxm_effects_panel_style());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    layout->setSpacing(6);

    auto *button_bar = new QWidget(this);
    button_bar->setObjectName(QStringLiteral("FluxMotionEffectsButtonBar"));
    auto *button_layout = new QHBoxLayout(button_bar);
    button_layout->setContentsMargins(6, 4, 6, 4);
    button_layout->setSpacing(4);

    auto add_button = [button_bar, button_layout](const char *icon, const QString &tip) {
        auto *button = new QToolButton(button_bar);
        button->setIcon(obs_icon(icon));
        button->setIconSize(QSize(16, 16));
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button_layout->addWidget(button);
        return button;
    };

    auto *btn_add = add_button("add.svg", fxm_tr("OBSTitles.AddEffect"));
    btn_stack_enabled_ = new FxmSwitch(tr("Effect Stack"), button_bar);
    btn_stack_enabled_->setToolTip(tr("Enable or disable the complete effect stack"));
    btn_stack_enabled_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    btn_stack_enabled_->setChecked(true);
    button_layout->addWidget(btn_stack_enabled_);
    btn_respect_masks_ = add_button("timeline-mask.svg", fxm_tr("OBSTitles.ApplyEffectStackAfterMask"));
    btn_respect_masks_->setCheckable(true);
    btn_respect_masks_->setToolTip(fxm_tr("OBSTitles.ApplyEffectStackAfterMaskTooltip"));
    btn_stack_menu_ = add_button("settings.svg", tr("Effect stack actions"));
    button_layout->addStretch(1);

    auto *settings_scroll = new QScrollArea(this);
    settings_scroll->setWidgetResizable(true);
    settings_scroll->setFrameShape(QFrame::NoFrame);

    settings_container_ = new QWidget(settings_scroll);
    settings_container_->setObjectName(QStringLiteral("FluxMotionEffectsSettingsContainer"));
    settings_layout_ = new QVBoxLayout(settings_container_);
    settings_layout_->setContentsMargins(4, 2, 4, 6);
    settings_layout_->setSpacing(6);
    settings_scroll->setWidget(settings_container_);
    layout->addWidget(settings_scroll, 1);
    layout->addWidget(button_bar);

    connect(btn_add, &QToolButton::clicked, this, [this]() {
        if (!layer_ || layer_->locked)
            return;
        LayerEffect effect;
        if (!choose_effect(&effect))
            return;
        layer_->effects.push_back(std::move(effect));
        selected_index_ = static_cast<int>(layer_->effects.size()) - 1;
        rebuild_stack();
        emit_effect_changed();
    });

    connect(btn_stack_enabled_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (loading_values_ || !layer_)
            return;
        set_stack_enabled(enabled);
    });

    connect(btn_stack_menu_, &QToolButton::clicked, this, [this]() {
        if (!btn_stack_menu_)
            return;
        QMenu menu(btn_stack_menu_);
        QAction *copy_stack = menu.addAction(tr("Copy effect stack"));
        QAction *paste_stack = menu.addAction(tr("Paste effect stack"));
        QAction *append_stack = menu.addAction(tr("Append effect stack"));
        menu.addSeparator();
        QAction *save_preset = menu.addAction(tr("Save stack as preset…"));
        QAction *export_preset = menu.addAction(tr("Export stack preset…"));
        QAction *import_preset = menu.addAction(tr("Import stack preset…"));
        menu.addSeparator();
        QAction *enable_all = menu.addAction(tr("Enable complete stack"));
        QAction *disable_all = menu.addAction(tr("Disable complete stack"));
        QAction *chosen = menu.exec(btn_stack_menu_->mapToGlobal(
            QPoint(btn_stack_menu_->width(), btn_stack_menu_->height())));
        if (chosen == copy_stack) copy_stack_to_clipboard();
        else if (chosen == paste_stack) paste_stack_from_clipboard(true);
        else if (chosen == append_stack) paste_stack_from_clipboard(false);
        else if (chosen == save_preset) save_stack_preset(false);
        else if (chosen == export_preset) save_stack_preset(true);
        else if (chosen == import_preset) import_stack_preset(false);
        else if (chosen == enable_all) set_stack_enabled(true);
        else if (chosen == disable_all) set_stack_enabled(false);
    });

    connect(btn_respect_masks_, &QToolButton::toggled, this, [this](bool checked) {
        if (loading_values_ || !layer_) return;
        layer_->effect_stack_respects_masks = checked;
        emit_effect_changed();
    });

    rebuild_stack();
}

EffectsPanel::~EffectsPanel()
{
    begin_shutdown();
}

namespace {
constexpr int kEffectIdRole = Qt::UserRole + 101;
constexpr int kEffectCategoryRole = Qt::UserRole + 102;
constexpr int kEffectAudioRole = Qt::UserRole + 103;
constexpr const char *kEffectStackMimeType = "application/x-fxm-effect-stack+json";

QStringList effect_browser_categories()
{
    return {QObject::tr("All"), QObject::tr("Favorites"),
            QObject::tr("Recently used"), QObject::tr("Blur and Sharpen"),
            QObject::tr("Color Correction"), QObject::tr("Distortion"),
            QObject::tr("Generate"), QObject::tr("Keying"),
            QObject::tr("Light and Optical"), QObject::tr("Noise and Grain"),
            QObject::tr("Stylize"), QObject::tr("Utility"),
            QObject::tr("Audio"), QObject::tr("External Plugins")};
}

QString normalized_effect_category(const FxmEffectExtensionDefinition &definition)
{
    if (!definition.builtIn)
        return QObject::tr("External Plugins");
    if (const EffectDescriptor *descriptor = effect_descriptor(definition.builtInType)) {
        const QString category = QString::fromUtf8(descriptor->category ? descriptor->category : "Utility");
        if (definition.builtInType == LayerEffectType::Noise ||
            definition.builtInType == LayerEffectType::Grain)
            return QObject::tr("Noise and Grain");
        return category;
    }
    return QObject::tr("Utility");
}

QIcon effect_browser_thumbnail(const QString &name, const QString &category)
{
    QPixmap pixmap(96, 54);
    const QPalette palette = qApp->palette();
    pixmap.fill(palette.color(QPalette::Base));
    QPainter painter(&pixmap);
    const QRect rect = pixmap.rect().adjusted(1, 1, -1, -1);
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0.0, palette.color(QPalette::Button));
    gradient.setColorAt(1.0, palette.color(QPalette::AlternateBase));
    painter.fillRect(rect, gradient);
    painter.setPen(palette.color(QPalette::Mid));
    painter.drawRect(rect);
    painter.setPen(palette.color(QPalette::Text));
    QFont font = painter.font();
    font.setBold(true);
    font.setPointSize(std::max(8, font.pointSize()));
    painter.setFont(font);
    painter.drawText(rect.adjusted(6, 5, -6, -17), Qt::AlignCenter | Qt::TextWordWrap,
                     name.left(22));
    font.setBold(false);
    font.setPointSize(std::max(7, font.pointSize() - 2));
    painter.setFont(font);
    painter.drawText(rect.adjusted(4, 35, -4, -3), Qt::AlignCenter,
                     category.left(24));
    return QIcon(pixmap);
}

QString safe_preset_file_name(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._ -]+")),
                 QStringLiteral("_"));
    return name.isEmpty() ? QStringLiteral("Effect Stack") : name;
}
} // namespace

bool EffectsPanel::choose_effect(LayerEffect *effect, int replace_index)
{
    if (!effect || !layer_)
        return false;

    QDialog dialog(this);
    dialog.setWindowTitle(replace_index >= 0 ? tr("Replace Effect") : tr("Add Effect"));
    dialog.resize(720, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *filter_row = new QHBoxLayout();
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText(tr("Search effects…"));
    auto *category = new QComboBox(&dialog);
    category->addItems(effect_browser_categories());
    filter_row->addWidget(search, 1);
    filter_row->addWidget(category);
    layout->addLayout(filter_row);

    auto *list = new QListWidget(&dialog);
    list->setViewMode(QListView::IconMode);
    list->setIconSize(QSize(96, 54));
    list->setGridSize(QSize(150, 98));
    list->setResizeMode(QListView::Adjust);
    list->setWordWrap(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list, 1);

    auto *footer = new QHBoxLayout();
    auto *favorite = new QToolButton(&dialog);
    favorite->setCheckable(true);
    favorite->setText(QStringLiteral("★"));
    favorite->setToolTip(tr("Add or remove the selected effect from Favorites"));
    footer->addWidget(favorite);
    auto *btn_rescan_plugins = new QPushButton(tr("Rescan plugins"), &dialog);
    btn_rescan_plugins->setToolTip(tr("Rescan FXM visual effect plugin folders without restarting the editor."));
    footer->addWidget(btn_rescan_plugins);
    auto *btn_clear_quarantine = new QPushButton(tr("Clear quarantine"), &dialog);
    btn_clear_quarantine->setToolTip(tr("Allow previously quarantined visual effect plugins to be scanned again."));
    footer->addWidget(btn_clear_quarantine);
    footer->addStretch(1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                         QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    footer->addWidget(buttons);
    layout->addLayout(footer);

    QSettings settings;
    QStringList favorites = settings.value(QStringLiteral("effects/favorites")).toStringList();
    QStringList recents = settings.value(QStringLiteral("effects/recentlyUsed")).toStringList();

    auto &catalog = FxmEffectExtensionCatalog::instance();
    if (catalog.effects().empty())
        catalog.reload();

    std::function<void()> refresh;
    auto populate_effect_list = [&]() {
        list->clear();
        if (catalog.effects().empty())
            catalog.reload();
        for (const auto &definition : catalog.effects()) {
            const QString item_category = normalized_effect_category(definition);
            auto *item = new QListWidgetItem(
                effect_browser_thumbnail(definition.displayName, item_category),
                definition.displayName, list);
            item->setData(kEffectIdRole, definition.id);
            item->setData(kEffectCategoryRole, item_category);
            item->setData(kEffectAudioRole, false);
            QStringList badges;
            if (definition.builtIn) {
                if (const EffectDescriptor *descriptor = effect_descriptor(definition.builtInType)) {
                    badges << (descriptor->backend == EffectExecutionBackend::Cpu
                                   ? QStringLiteral("CPU") : QStringLiteral("GPU"));
                    if (descriptor->supports_hdr) badges << QStringLiteral("HDR");
                    if (descriptor->execution_space == LayerEffectSpace::ScreenSpace)
                        badges << QStringLiteral("SCREEN");
                    if (!descriptor->cacheable_when_static)
                        badges << QStringLiteral("CACHE BREAK");
                }
            } else {
                badges << QStringLiteral("PLUGIN");
                if (!definition.backend.isEmpty()) badges << definition.backend.toUpper();
                if (definition.multiPass) badges << QStringLiteral("MULTI-PASS");
                if (definition.cpuWorkerOnly) badges << QStringLiteral("WORKER CPU");
                if (!definition.declaredColorSpace.isEmpty()) badges << definition.declaredColorSpace.toUpper();
                if (!definition.declaredAlphaContract.isEmpty()) badges << definition.declaredAlphaContract.toUpper();
                if (definition.declaredInputCount > 1)
                    badges << tr("%1 INPUTS").arg(definition.declaredInputCount);
            }
            item->setToolTip(QStringLiteral("%1\n%2").arg(item_category, badges.join(QStringLiteral(" · "))));
        }

        if (replace_index < 0 &&
            (layer_->type == LayerType::Audio || layer_->type == LayerType::Video || layer_type_is_container(layer_->type))) {
            const std::array<std::pair<const char *, AudioEffectType>, 5> audio_effects{{
                {"Gain", AudioEffectType::Gain}, {"Fade", AudioEffectType::Fade},
                {"High Pass", AudioEffectType::HighPass},
                {"Low Pass", AudioEffectType::LowPass},
                {"Compressor / Limiter", AudioEffectType::CompressorLimiter}}};
            for (const auto &[label, type] : audio_effects) {
                const QString id = QStringLiteral("audio:%1").arg(static_cast<int>(type));
                auto *item = new QListWidgetItem(
                    effect_browser_thumbnail(QString::fromUtf8(label), tr("Audio")),
                    QString::fromUtf8(label), list);
                item->setData(kEffectIdRole, id);
                item->setData(kEffectCategoryRole, tr("Audio"));
                item->setData(kEffectAudioRole, true);
                item->setToolTip(tr("CPU · Audio"));
            }
        }
        if (refresh) refresh();
    };

    refresh = [&]() {
        const QString query = search->text().trimmed();
        const QString chosen_category = category->currentText();
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            const QString id = item->data(kEffectIdRole).toString();
            const QString item_category = item->data(kEffectCategoryRole).toString();
            bool category_match = chosen_category == tr("All") ||
                                  chosen_category == item_category;
            if (chosen_category == tr("Favorites"))
                category_match = favorites.contains(id);
            else if (chosen_category == tr("Recently used"))
                category_match = recents.contains(id);
            const bool search_match = query.isEmpty() ||
                item->text().contains(query, Qt::CaseInsensitive) ||
                item_category.contains(query, Qt::CaseInsensitive) ||
                id.contains(query, Qt::CaseInsensitive);
            item->setHidden(!(category_match && search_match));
        }
    };
    connect(search, &QLineEdit::textChanged, &dialog, [&](const QString &) { refresh(); });
    connect(category, &QComboBox::currentTextChanged, &dialog,
            [&](const QString &) { refresh(); });
    connect(list, &QListWidget::currentItemChanged, &dialog,
            [&](QListWidgetItem *current) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(current != nullptr);
        const bool blocked = favorite->blockSignals(true);
        favorite->setChecked(current && favorites.contains(
            current->data(kEffectIdRole).toString()));
        favorite->blockSignals(blocked);
    });
    connect(favorite, &QToolButton::toggled, &dialog, [&](bool checked) {
        QListWidgetItem *item = list->currentItem();
        if (!item) return;
        const QString id = item->data(kEffectIdRole).toString();
        favorites.removeAll(id);
        if (checked) favorites.prepend(id);
        settings.setValue(QStringLiteral("effects/favorites"), favorites);
        refresh();
    });
    connect(btn_rescan_plugins, &QPushButton::clicked, &dialog, [&]() {
        const QString previous = list->currentItem()
            ? list->currentItem()->data(kEffectIdRole).toString() : QString();
        catalog.rescan();
        populate_effect_list();
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->data(kEffectIdRole).toString() == previous) {
                list->setCurrentRow(i);
                break;
            }
        }
        const QStringList diagnostics = catalog.diagnostics();
        if (!diagnostics.isEmpty())
            QMessageBox::information(&dialog, tr("Effect plugin scan"),
                                     diagnostics.join(QStringLiteral("\n")));
    });
    connect(btn_clear_quarantine, &QPushButton::clicked, &dialog, [&]() {
        catalog.clearQuarantine();
        catalog.rescan();
        populate_effect_list();
        QMessageBox::information(&dialog, tr("Effect plugin scan"),
                                 tr("Quarantine cleared. Plugin folders were rescanned."));
    });
    connect(list, &QListWidget::itemDoubleClicked, &dialog,
            [&](QListWidgetItem *) { dialog.accept(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    populate_effect_list();
    search->setFocus();
    if (dialog.exec() != QDialog::Accepted || !list->currentItem())
        return false;

    QListWidgetItem *chosen = list->currentItem();
    const QString stable_id = chosen->data(kEffectIdRole).toString();
    recents.removeAll(stable_id);
    recents.prepend(stable_id);
    while (recents.size() > 16) recents.removeLast();
    settings.setValue(QStringLiteral("effects/recentlyUsed"), recents);

    if (chosen->data(kEffectAudioRole).toBool()) {
        const AudioEffectType type = static_cast<AudioEffectType>(
            stable_id.mid(QStringLiteral("audio:").size()).toInt());
        add_audio_effect(type);
        return false;
    }

    const auto *definition = catalog.find(stable_id);
    if (!definition)
        return false;
    if (definition->builtIn) {
        *effect = fxm::effects::make_default_layer_effect(definition->builtInType);
    } else {
        *effect = fxm::effects::make_default_layer_effect(LayerEffectType::BackgroundColor);
        effect->extension_id = stable_id.toStdString();
        effect->extension_parameters_json = QJsonDocument(definition->defaults)
            .toJson(QJsonDocument::Compact).toStdString();
        effect->extension_schema_version = definition->schemaVersion;
    }
    return true;
}

void EffectsPanel::copy_effect_to_clipboard(int effect_index) const
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    std::vector<LayerEffect> stack{layer_->effects[static_cast<size_t>(effect_index)]};
    auto *mime = new QMimeData();
    mime->setData(QString::fromUtf8(kEffectStackMimeType),
                  QByteArray::fromStdString(serialize_layer_effect_stack_json(stack)));
    mime->setText(QString::fromStdString(serialize_layer_effect_stack_json(stack)));
    QApplication::clipboard()->setMimeData(mime);
}

void EffectsPanel::copy_stack_to_clipboard() const
{
    if (!layer_)
        return;
    auto *mime = new QMimeData();
    mime->setData(QString::fromUtf8(kEffectStackMimeType),
                  QByteArray::fromStdString(serialize_layer_effect_stack_json(layer_->effects)));
    QApplication::clipboard()->setMimeData(mime);
}

bool EffectsPanel::paste_effect_from_clipboard(int insert_after)
{
    if (!layer_ || layer_->locked)
        return false;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QString::fromUtf8(kEffectStackMimeType)))
        return false;
    std::vector<LayerEffect> pasted;
    std::string error;
    if (!deserialize_layer_effect_stack_json(
            mime->data(QString::fromUtf8(kEffectStackMimeType)).toStdString(),
            &pasted, &error) || pasted.empty())
        return false;
    const int position = std::clamp(insert_after + 1, 0,
                                    static_cast<int>(layer_->effects.size()));
    layer_->effects.insert(layer_->effects.begin() + position, pasted.front());
    selected_index_ = position;
    rebuild_stack();
    emit_effect_changed();
    return true;
}

bool EffectsPanel::paste_stack_from_clipboard(bool replace_existing)
{
    if (!layer_ || layer_->locked)
        return false;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QString::fromUtf8(kEffectStackMimeType)))
        return false;
    std::vector<LayerEffect> pasted;
    std::string error;
    if (!deserialize_layer_effect_stack_json(
            mime->data(QString::fromUtf8(kEffectStackMimeType)).toStdString(),
            &pasted, &error))
        return false;
    if (replace_existing)
        layer_->effects = std::move(pasted);
    else
        layer_->effects.insert(layer_->effects.end(), pasted.begin(), pasted.end());
    selected_index_ = layer_->effects.empty() ? -1 : static_cast<int>(layer_->effects.size()) - 1;
    rebuild_stack();
    emit_effect_changed();
    return true;
}

bool EffectsPanel::save_stack_preset(bool export_file)
{
    if (!layer_)
        return false;
    QString path;
    if (export_file) {
        path = QFileDialog::getSaveFileName(this, tr("Export Effect Stack Preset"),
            QStringLiteral("Effect Stack.fxmstack"), tr("FXM effect stacks (*.fxmstack)"));
    } else {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Save Stack as Preset"),
            tr("Preset name:"), QLineEdit::Normal, tr("Effect Stack"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return false;
        QDir root(fxm::effects::effect_presets_root_path());
        root.mkpath(QStringLiteral("Stacks"));
        path = root.filePath(QStringLiteral("Stacks/%1.fxmstack")
                                 .arg(safe_preset_file_name(name)));
    }
    if (path.isEmpty())
        return false;
    if (!path.endsWith(QStringLiteral(".fxmstack"), Qt::CaseInsensitive))
        path += QStringLiteral(".fxmstack");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QByteArray::fromStdString(serialize_layer_effect_stack_json(layer_->effects)));
    return file.commit();
}

bool EffectsPanel::import_stack_preset(bool replace_existing)
{
    if (!layer_ || layer_->locked)
        return false;
    const QString path = QFileDialog::getOpenFileName(this, tr("Import Effect Stack Preset"),
        fxm::effects::effect_presets_root_path(),
        tr("FXM effect stacks (*.fxmstack);;All files (*)"));
    if (path.isEmpty())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    std::vector<LayerEffect> imported;
    std::string error;
    if (!deserialize_layer_effect_stack_json(file.readAll().toStdString(),
                                             &imported, &error)) {
        QMessageBox::warning(this, tr("Import Effect Stack"),
                             QString::fromStdString(error));
        return false;
    }
    if (replace_existing)
        layer_->effects = std::move(imported);
    else
        layer_->effects.insert(layer_->effects.end(), imported.begin(), imported.end());
    selected_index_ = layer_->effects.empty() ? -1 : static_cast<int>(layer_->effects.size()) - 1;
    rebuild_stack();
    emit_effect_changed();
    return true;
}

void EffectsPanel::reset_effect(int effect_index)
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    LayerEffect &current = layer_->effects[static_cast<size_t>(effect_index)];
    if (current.extension_id.empty()) {
        current = fxm::effects::make_default_layer_effect(current.type);
    } else {
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty()) catalog.reload();
        const QString id = QString::fromStdString(current.extension_id);
        if (const auto *definition = catalog.find(id)) {
            if (definition->builtIn) {
                current = fxm::effects::make_default_layer_effect(definition->builtInType);
            } else {
                current = fxm::effects::make_default_layer_effect(LayerEffectType::BackgroundColor);
                current.extension_id = id.toStdString();
                current.extension_parameters_json = QJsonDocument(definition->defaults)
                    .toJson(QJsonDocument::Compact).toStdString();
                current.extension_schema_version = definition->schemaVersion;
            }
        }
    }
    rebuild_stack();
    emit_effect_changed();
}

void EffectsPanel::set_stack_enabled(bool enabled)
{
    if (!layer_ || layer_->locked)
        return;
    const double time = current_local_time();
    for (LayerEffect &effect : layer_->effects) {
        effect.enabled = enabled;
        set_animated_value(effect.enabled_prop, time, enabled ? 1.0 : 0.0);
    }
    for (AudioEffect &effect : layer_->audio_effects)
        effect.enabled = enabled;
    rebuild_stack();
    emit_effect_changed();
}

void EffectsPanel::begin_shutdown()
{
    if (shutting_down_)
        return;
    shutting_down_ = true;

    /* Settings widgets are removed with deleteLater().  Suppress their final
     * focus/value notifications while the editor dock hierarchy is being
     * destroyed, otherwise a late effect update can target a canvas whose
     * native OBS display is already in teardown. */
    blockSignals(true);
    for (QTimer *timer : findChildren<QTimer *>())
        timer->stop();
    const auto child_objects = findChildren<QObject *>();
    for (QObject *child : child_objects)
        disconnect(child, nullptr, this, nullptr);
    layer_.reset();
    title_.reset();
    selected_index_ = -1;
    last_published_canvas_handles_ = QJsonArray();
    numeric_bindings_.clear();
    color_bindings_.clear();
    bool_bindings_.clear();
    combo_bindings_.clear();
    keyframe_bindings_.clear();
}


void EffectsPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event && fxm::effects::mime_has_effect_preset(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void EffectsPanel::dragMoveEvent(QDragMoveEvent *event)
{
    if (event && fxm::effects::mime_has_effect_preset(event->mimeData())) {
        if (layer_ && !layer_->locked) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }
    QWidget::dragMoveEvent(event);
}

void EffectsPanel::dropEvent(QDropEvent *event)
{
    if (event && fxm::effects::mime_has_effect_preset(event->mimeData())) {
        const QString path = fxm::effects::effect_preset_path_from_mime(event->mimeData());
        if (add_effect_from_preset_file(path)) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }
    QWidget::dropEvent(event);
}

bool EffectsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event && (event->type() == QEvent::MouseButtonPress ||
                  event->type() == QEvent::FocusIn ||
                  event->type() == QEvent::Wheel ||
                  event->type() == QEvent::KeyPress)) {
        const int index = effect_index_for_object(watched);
        if (index >= 0)
            set_active_effect_index(index);
    }
    return QWidget::eventFilter(watched, event);
}

bool EffectsPanel::add_effect_from_preset_file(const QString &file_path)
{
    if (!layer_ || layer_->locked || layer_->type == LayerType::Audio)
        return false;

    fxm::effects::EffectPresetDescriptor descriptor;
    if (!fxm::effects::load_effect_preset_file(file_path, &descriptor))
        return false;

    layer_->effects.push_back(descriptor.effect);
    selected_index_ = static_cast<int>(layer_->effects.size()) - 1;
    rebuild_stack();
    emit_effect_changed();
    return true;
}

bool EffectsPanel::add_audio_effect(AudioEffectType type)
{
    if (!layer_ || layer_->locked) return false;
    const bool audio_capable = layer_->type == LayerType::Audio || layer_->type == LayerType::Video || layer_type_is_container(layer_->type);
    if (!audio_capable) return false;
    AudioEffect effect;
    effect.type = type;
    layer_->audio_effects.push_back(effect);
    rebuild_stack();
    emit audio_property_changed(true);
    return true;
}

void EffectsPanel::set_layer(std::shared_ptr<Layer> layer, double playhead)
{
    if (shutting_down_)
        return;
    const bool same_layer = layer_ && layer && layer_.get() == layer.get();
    const int previous_effect_count = layer_ ? (int)layer_->effects.size() : -1;
    layer_ = layer;
    playhead_ = playhead;
    selected_index_ = layer_ && !layer_->effects.empty() ? std::clamp(selected_index_, 0, (int)layer_->effects.size() - 1) : -1;
    if (same_layer && previous_effect_count == (layer_ ? (int)layer_->effects.size() : -1) &&
        (settings_editor_has_focus() || numeric_label_dragging_ ||
         applying_panel_order_ || panel_rebuild_pending_)) {
        return;
    }
    rebuild_stack();
    publish_canvas_handles(true);
}

int EffectsPanel::effect_index_for_object(const QObject *object) const
{
    for (const QObject *candidate = object; candidate; candidate = candidate->parent()) {
        const QVariant value = candidate->property("fxmEffectIndex");
        if (value.isValid()) {
            const int index = value.toInt();
            if (layer_ && index >= 0 && index < static_cast<int>(layer_->effects.size()))
                return index;
        }
    }
    return -1;
}

LayerEffect *EffectsPanel::selected_effect()
{
    int index = building_effect_index_;
    if (index < 0)
        index = effect_index_for_object(sender());
    if (index < 0)
        index = selected_index_;
    if (!layer_ || index < 0 || index >= static_cast<int>(layer_->effects.size()))
        return nullptr;
    return &layer_->effects[static_cast<size_t>(index)];
}

const LayerEffect *EffectsPanel::selected_effect() const
{
    int index = building_effect_index_;
    if (index < 0)
        index = effect_index_for_object(sender());
    if (index < 0)
        index = selected_index_;
    if (!layer_ || index < 0 || index >= static_cast<int>(layer_->effects.size()))
        return nullptr;
    return &layer_->effects[static_cast<size_t>(index)];
}

void EffectsPanel::set_active_effect_index(int effect_index)
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    if (selected_index_ == effect_index)
        return;
    selected_index_ = effect_index;
    publish_canvas_handles(true);
}

void EffectsPanel::sync_legacy_enabled_flags()
{
    /* Effect stack is the only source of truth. Legacy layer flags are kept
     * only for old project import/export fallback and are not synchronized.
     */
}

QJsonArray EffectsPanel::extension_canvas_handles() const
{
    if (shutting_down_)
        return {};
    const LayerEffect *effect = selected_effect();
    if (!effect || !layer_)
        return {};

    const double local_time = current_local_time();
    QJsonArray result;
    auto append_handle = [&result](const QString &path, const QString &label,
                                   double x, double y, double min_x, double max_x,
                                   double min_y, double max_y,
                                   const QString &color = QStringLiteral("#ffb52e")) {
        QJsonObject handle;
        handle.insert(QStringLiteral("path"), path);
        handle.insert(QStringLiteral("label"), label);
        handle.insert(QStringLiteral("color"), color);
        handle.insert(QStringLiteral("space"), QStringLiteral("layer"));
        handle.insert(QStringLiteral("minX"), min_x);
        handle.insert(QStringLiteral("maxX"), max_x);
        handle.insert(QStringLiteral("minY"), min_y);
        handle.insert(QStringLiteral("maxY"), max_y);
        handle.insert(QStringLiteral("value"), QJsonArray{x, y});
        result.append(handle);
    };

    if (!effect->extension_id.empty()) {
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty())
            catalog.reload();
        const auto *definition = catalog.find(
            QString::fromStdString(effect->extension_id));
        const bool schema_driven = definition &&
            (!definition->builtIn ||
             effect->type == LayerEffectType::FourColorGradient);
        if (schema_driven) {
            QJsonArray handles = definition->canvasHandles;
            QSet<QString> declared_paths;
            for (const QJsonValue &value : handles) {
                declared_paths.insert(value.toObject()
                    .value(QStringLiteral("path")).toString());
            }

            /* Every point parameter gets a canvas control even when an
             * extension omitted an explicit visual-handle declaration. */
            for (auto it = definition->parameterSchema.begin();
                 it != definition->parameterSchema.end(); ++it) {
                const QJsonObject meta = it.value().toObject();
                if (meta.value(QStringLiteral("type")).toString() != QStringLiteral("point") ||
                    declared_paths.contains(it.key())) {
                    continue;
                }
                QJsonObject handle;
                handle.insert(QStringLiteral("path"), it.key());
                handle.insert(QStringLiteral("label"),
                    meta.value(QStringLiteral("label")).toString(it.key()));
                handles.append(handle);
            }

            for (const QJsonValue &value : handles) {
                QJsonObject handle = value.toObject();
                const QString path = handle.value(
                    QStringLiteral("path")).toString();
                if (path.isEmpty())
                    continue;
                const QJsonObject meta = definition->parameterSchema
                    .value(path).toObject();
                const QJsonValue state_value =
                    fxm::effects::animation::state_path_value(*effect, path);
                const QJsonValue fallback = state_value.isUndefined()
                    ? definition->defaults.value(path) : state_value;
                const QJsonValue position = evaluate_extension_track(
                    *effect, path, local_time, fallback);
                if (!position.isArray() || position.toArray().size() < 2)
                    continue;
                handle.insert(QStringLiteral("value"), position);
                handle.insert(QStringLiteral("space"),
                              QStringLiteral("layer"));
                handle.insert(QStringLiteral("minX"), meta.value(QStringLiteral("minX")).toDouble(-100.0));
                handle.insert(QStringLiteral("maxX"), meta.value(QStringLiteral("maxX")).toDouble(100.0));
                handle.insert(QStringLiteral("minY"), meta.value(QStringLiteral("minY")).toDouble(-100.0));
                handle.insert(QStringLiteral("maxY"), meta.value(QStringLiteral("maxY")).toDouble(100.0));
                result.append(handle);
            }
            return result;
        }
    }

    if (effect->type == LayerEffectType::LensFlare ||
        effect->type == LayerEffectType::Vignette) {
        append_handle(QStringLiteral("__native.center"), tr("Center"),
            panel_eval_effect_property(effect->center_x_prop, effect->effect_center_x, local_time),
            panel_eval_effect_property(effect->center_y_prop, effect->effect_center_y, local_time),
            -10.0, 10.0, -10.0, 10.0);
    } else if (effect->type == LayerEffectType::BackgroundColor &&
               effect->effect_fill_type == 1) {
        append_handle(QStringLiteral("__native.gradient_center"), tr("Gradient Center"),
            panel_eval_effect_property(effect->gradient_center_x_prop,
                                       effect->effect_gradient_center_x, local_time),
            panel_eval_effect_property(effect->gradient_center_y_prop,
                                       effect->effect_gradient_center_y, local_time),
            -100.0, 100.0, -100.0, 100.0, QStringLiteral("#5ec7ff"));
        if (effect->effect_gradient_type == 1) {
            append_handle(QStringLiteral("__native.gradient_focal"), tr("Gradient Focal Point"),
                panel_eval_effect_property(effect->gradient_focal_x_prop,
                                           effect->effect_gradient_focal_x, local_time),
                panel_eval_effect_property(effect->gradient_focal_y_prop,
                                           effect->effect_gradient_focal_y, local_time),
                -100.0, 100.0, -100.0, 100.0, QStringLiteral("#ff63c3"));
        }
    }
    return result;
}

void EffectsPanel::set_extension_canvas_handle_position(const QString &path,
                                                         const QPointF &normalized_position,
                                                         bool final_change)
{
    if (shutting_down_)
        return;
    LayerEffect *effect = selected_effect();
    if (!effect || path.isEmpty())
        return;
    const double time = current_local_time();

    auto set_native_pair = [time](AnimatedProperty &x_prop, float &x_legacy,
                                  AnimatedProperty &y_prop, float &y_legacy,
                                  const QPointF &value) {
        x_legacy = static_cast<float>(value.x());
        y_legacy = static_cast<float>(value.y());
        set_animated_value(x_prop, time, value.x());
        set_animated_value(y_prop, time, value.y());
    };

    if (path == QStringLiteral("__native.center")) {
        set_native_pair(effect->center_x_prop, effect->effect_center_x,
                        effect->center_y_prop, effect->effect_center_y,
                        normalized_position);
    } else if (path == QStringLiteral("__native.gradient_center")) {
        set_native_pair(effect->gradient_center_x_prop, effect->effect_gradient_center_x,
                        effect->gradient_center_y_prop, effect->effect_gradient_center_y,
                        normalized_position);
    } else if (path == QStringLiteral("__native.gradient_focal")) {
        set_native_pair(effect->gradient_focal_x_prop, effect->effect_gradient_focal_x,
                        effect->gradient_focal_y_prop, effect->effect_gradient_focal_y,
                        normalized_position);
    } else {
        double minimum_x = -100.0, maximum_x = 100.0;
        double minimum_y = -100.0, maximum_y = 100.0;
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty())
            catalog.reload();
        if (const auto *definition = catalog.find(QString::fromStdString(effect->extension_id))) {
            const QJsonObject meta = definition->parameterSchema.value(path).toObject();
            minimum_x = meta.value(QStringLiteral("minX")).toDouble(minimum_x);
            maximum_x = meta.value(QStringLiteral("maxX")).toDouble(maximum_x);
            minimum_y = meta.value(QStringLiteral("minY")).toDouble(minimum_y);
            maximum_y = meta.value(QStringLiteral("maxY")).toDouble(maximum_y);
        }
        const QJsonArray value{
            std::clamp(normalized_position.x(), minimum_x, maximum_x),
            std::clamp(normalized_position.y(), minimum_y, maximum_y)};
        fxm::effects::animation::set_animated_value(*effect, path, time, value);
    }

    sync_legacy_enabled_flags();
    emit property_changed(final_change);
    publish_canvas_handles();
    update_bound_controls();
}

void EffectsPanel::emit_effect_changed()
{
    if (shutting_down_)
        return;
    sync_legacy_enabled_flags();
    emit property_changed(!numeric_label_dragging_);
    publish_canvas_handles();
}

void EffectsPanel::publish_canvas_handles(bool force)
{
    if (shutting_down_)
        return;

    const QJsonArray handles = extension_canvas_handles();
    if (!force && handles == last_published_canvas_handles_)
        return;

    last_published_canvas_handles_ = handles;
    emit extension_canvas_handles_changed(handles);
}

bool EffectsPanel::settings_editor_has_focus() const
{
    QWidget *focus = qApp ? qApp->focusWidget() : nullptr;
    return focus && settings_container_ &&
           (focus == settings_container_ || settings_container_->isAncestorOf(focus));
}

void EffectsPanel::update_playhead(double playhead)
{
    if (shutting_down_)
        return;
    playhead_ = playhead;
    update_bound_controls();
    /* Canvas point controls must be rebuilt from the value evaluated at the
     * new playhead, not left at the last manually edited/static position. */
    publish_canvas_handles();
}

double EffectsPanel::current_local_time() const
{
    if (!layer_)
        return 0.0;
    return std::clamp(playhead_ - layer_->in_time, 0.0,
                      std::max(0.0, layer_->out_time - layer_->in_time));
}

QWidget *EffectsPanel::make_keyframe_controls(QPushButton *button,
                                               QWidget *parent)
{
    return fxm_make_keyframe_controls(
        button, parent,
        [this, button]() {
            for (const KeyframeBinding &binding : keyframe_bindings_) {
                if (binding.button != button || !binding.keyframe_times || !layer_ ||
                    binding.effect_index < 0 ||
                    binding.effect_index >= static_cast<int>(layer_->effects.size()))
                    continue;
                return binding.keyframe_times(
                    layer_->effects[static_cast<size_t>(binding.effect_index)]);
            }
            return std::vector<double>{};
        }, [this]() { return current_local_time(); },
        [this](double local_time) {
            if (layer_)
                emit keyframe_navigation_requested(layer_->in_time + local_time);
        });
}

void EffectsPanel::update_bound_controls()
{
    if (!layer_ || loading_values_ || numeric_label_dragging_)
        return;

    const double lt = std::clamp(playhead_ - layer_->in_time, 0.0,
                                 std::max(0.0, layer_->out_time - layer_->in_time));
    QScopedValueRollback<bool> loading_guard(loading_values_, true);

    auto effect_for = [this](int index) -> const LayerEffect * {
        if (!layer_ || index < 0 || index >= static_cast<int>(layer_->effects.size()))
            return nullptr;
        return &layer_->effects[static_cast<size_t>(index)];
    };

    for (const auto &binding : numeric_bindings_) {
        const LayerEffect *effect = effect_for(binding.effect_index);
        if (!effect || !binding.spin || !binding.value)
            continue;
        const double value = binding.value(*effect, lt);
        if (!std::isfinite(value) || std::abs(binding.spin->value() - value) < 0.000001)
            continue;
        QSignalBlocker blocker(binding.spin);
        binding.spin->setValue(value);
    }

    for (const auto &binding : color_bindings_) {
        const LayerEffect *effect = effect_for(binding.effect_index);
        if (!effect || !binding.button || !binding.value)
            continue;
        const uint32_t argb = binding.value(*effect, lt);
        if (binding.button->property("argb").toUInt() != argb)
            set_color_button_argb(binding.button, argb);
    }
    for (const auto &binding : bool_bindings_) {
        const LayerEffect *effect = effect_for(binding.effect_index);
        if (!effect || !binding.checkbox || !binding.value)
            continue;
        const bool value = binding.value(*effect, lt);
        if (binding.checkbox->isChecked() != value) {
            QSignalBlocker blocker(binding.checkbox);
            binding.checkbox->setChecked(value);
        }
    }
    for (const auto &binding : combo_bindings_) {
        const LayerEffect *effect = effect_for(binding.effect_index);
        if (!effect || !binding.combo || !binding.value)
            continue;
        const QVariant value = binding.value(*effect, lt);
        int index = binding.combo->findData(value);
        if (index < 0 && binding.combo->count() > 0)
            index = 0;
        if (index >= 0 && binding.combo->currentIndex() != index) {
            QSignalBlocker blocker(binding.combo);
            binding.combo->setCurrentIndex(index);
        }
    }
    for (const auto &binding : keyframe_bindings_) {
        const LayerEffect *effect = effect_for(binding.effect_index);
        if (!effect || !binding.button || !binding.has_keyframe)
            continue;
        const bool keyed = binding.has_keyframe(*effect, lt);
        const bool animated = binding.has_keyframes
            ? binding.has_keyframes(*effect) : keyed;
        set_effect_keyframe_button_state(binding.button, keyed, animated);
    }
}

void EffectsPanel::apply_effect_panel_order()
{
    if (loading_values_ || applying_panel_order_ || !layer_ || !settings_layout_)
        return;
    const int count = static_cast<int>(layer_->effects.size());
    if (count <= 1)
        return;

    std::vector<int> source_order;
    source_order.reserve(static_cast<size_t>(count));
    std::vector<QPointer<FxmCollapsiblePanel>> ordered_panels;
    ordered_panels.reserve(static_cast<size_t>(count));
    std::vector<bool> used(static_cast<size_t>(count), false);
    for (int row = 0; row < settings_layout_->count(); ++row) {
        auto *panel = qobject_cast<FxmCollapsiblePanel *>(settings_layout_->itemAt(row)->widget());
        if (!panel || !panel->property("fxmEffectIndex").isValid())
            continue;
        const int source_index = panel->property("fxmEffectIndex").toInt();
        if (source_index < 0 || source_index >= count || used[static_cast<size_t>(source_index)])
            return;
        used[static_cast<size_t>(source_index)] = true;
        source_order.push_back(source_index);
        ordered_panels.push_back(panel);
    }
    if (static_cast<int>(source_order.size()) != count)
        return;

    bool changed = false;
    for (int index = 0; index < count; ++index)
        changed = changed || source_order[static_cast<size_t>(index)] != index;
    if (!changed)
        return;

    QScopedValueRollback<bool> ordering_guard(applying_panel_order_, true);
    std::vector<LayerEffect> reordered;
    reordered.reserve(static_cast<size_t>(count));
    std::vector<int> old_to_new(static_cast<size_t>(count), -1);
    for (int new_index = 0; new_index < count; ++new_index) {
        const int old_index = source_order[static_cast<size_t>(new_index)];
        reordered.push_back(layer_->effects[static_cast<size_t>(old_index)]);
        old_to_new[static_cast<size_t>(old_index)] = new_index;
    }
    layer_->effects = std::move(reordered);
    if (selected_index_ >= 0 && selected_index_ < count)
        selected_index_ = old_to_new[static_cast<size_t>(selected_index_)];

    for (int new_index = 0; new_index < count; ++new_index) {
        if (ordered_panels[static_cast<size_t>(new_index)]) {
            ordered_panels[static_cast<size_t>(new_index)]->setProperty("fxmEffectIndex", new_index);
            if (QWidget *content = ordered_panels[static_cast<size_t>(new_index)]->contentWidget())
                content->setProperty("fxmEffectIndex", new_index);
        }
    }
    auto remap_binding = [&old_to_new](auto &bindings) {
        for (auto &binding : bindings) {
            if (binding.effect_index >= 0 &&
                binding.effect_index < static_cast<int>(old_to_new.size()))
                binding.effect_index = old_to_new[static_cast<size_t>(binding.effect_index)];
        }
    };
    remap_binding(numeric_bindings_);
    remap_binding(color_bindings_);
    remap_binding(bool_bindings_);
    remap_binding(combo_bindings_);
    remap_binding(keyframe_bindings_);
    effect_panels_ = std::move(ordered_panels);

    sync_legacy_enabled_flags();
    emit_effect_changed();
    publish_canvas_handles(true);
    if (!panel_rebuild_pending_) {
        panel_rebuild_pending_ = true;
        QTimer::singleShot(0, this, [this]() {
            panel_rebuild_pending_ = false;
            if (!shutting_down_)
                load_settings();
        });
    }
}

void EffectsPanel::duplicate_effect(int effect_index)
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    layer_->effects.insert(layer_->effects.begin() + effect_index + 1,
                           layer_->effects[static_cast<size_t>(effect_index)]);
    selected_index_ = effect_index + 1;
    sync_legacy_enabled_flags();
    rebuild_stack();
    emit_effect_changed();
}

void EffectsPanel::delete_effect(int effect_index)
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    const LayerEffect &effect = layer_->effects[static_cast<size_t>(effect_index)];
    if (fxm::effects::animation::effect_has_any_keyframes(effect)) {
        const auto answer = QMessageBox::question(
            this,
            fxm_tr("OBSTitles.RemoveEffectWithKeyframesTitle"),
            fxm_tr("OBSTitles.RemoveEffectWithKeyframesQuestion"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    layer_->effects.erase(layer_->effects.begin() + effect_index);
    selected_index_ = layer_->effects.empty()
        ? -1
        : std::clamp(effect_index, 0, static_cast<int>(layer_->effects.size()) - 1);
    sync_legacy_enabled_flags();
    rebuild_stack();
    emit_effect_changed();
}

void EffectsPanel::move_effect(int effect_index, int delta)
{
    if (!layer_ || effect_index < 0 || effect_index >= static_cast<int>(layer_->effects.size()))
        return;
    const int target = effect_index + delta;
    if (target < 0 || target >= static_cast<int>(layer_->effects.size()))
        return;
    std::swap(layer_->effects[static_cast<size_t>(effect_index)],
              layer_->effects[static_cast<size_t>(target)]);
    selected_index_ = target;
    rebuild_stack();
    emit_effect_changed();
}

void EffectsPanel::rebuild_stack()
{
    if (layer_ && !layer_->effects.empty())
        selected_index_ = std::clamp(selected_index_, 0, static_cast<int>(layer_->effects.size()) - 1);
    else
        selected_index_ = -1;

    if (btn_respect_masks_) {
        QSignalBlocker blocker(btn_respect_masks_);
        btn_respect_masks_->setEnabled(layer_ != nullptr);
        btn_respect_masks_->setChecked(layer_ && layer_->effect_stack_respects_masks);
    }
    if (btn_stack_enabled_) {
        QSignalBlocker blocker(btn_stack_enabled_);
        const bool has_stack = layer_ && (!layer_->effects.empty() || !layer_->audio_effects.empty());
        bool all_enabled = has_stack;
        if (layer_) {
            const double time = current_local_time();
            for (const LayerEffect &effect : layer_->effects)
                all_enabled = all_enabled && eval_effect_enabled(effect, time);
            for (const AudioEffect &effect : layer_->audio_effects)
                all_enabled = all_enabled && effect.enabled;
        }
        btn_stack_enabled_->setEnabled(has_stack && !layer_->locked);
        btn_stack_enabled_->setChecked(all_enabled);
    }
    if (btn_stack_menu_)
        btn_stack_menu_->setEnabled(layer_ != nullptr && !layer_->locked);
    load_settings();
}

void EffectsPanel::build_settings()
{
    numeric_bindings_.clear();
    color_bindings_.clear();
    bool_bindings_.clear();
    combo_bindings_.clear();
    keyframe_bindings_.clear();
    while (QLayoutItem *item = settings_layout_->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void EffectsPanel::load_settings()
{
    build_settings();
    effect_panels_.clear();
    if (layer_ && !layer_->audio_effects.empty() &&
        (layer_->type == LayerType::Audio || layer_->type == LayerType::Video || layer_type_is_container(layer_->type))) {
        auto audio_name = [](AudioEffectType type) {
            switch (type) {
            case AudioEffectType::Gain: return fxm_tr("OBSTitles.AudioEffectGain");
            case AudioEffectType::Fade: return fxm_tr("OBSTitles.AudioEffectFade");
            case AudioEffectType::HighPass: return fxm_tr("OBSTitles.AudioEffectHighPass");
            case AudioEffectType::LowPass: return fxm_tr("OBSTitles.AudioEffectLowPass");
            case AudioEffectType::CompressorLimiter: return fxm_tr("OBSTitles.AudioEffectCompressorLimiter");
            }
            return fxm_tr("OBSTitles.AudioEffects");
        };
        for (int i = 0; i < static_cast<int>(layer_->audio_effects.size()); ++i) {
            const QString title = audio_name(layer_->audio_effects[static_cast<size_t>(i)].type);
            auto *box = new QGroupBox(title, settings_container_);
            auto *form = new QFormLayout(box);
            auto add_spin = [this, form, box, i](const QString &label_text, double value,
                                                 double minimum, double maximum,
                                                 std::function<void(AudioEffect &, double)> setter) {
                auto *spin = new QDoubleSpinBox(box);
                spin->setRange(minimum, maximum);
                spin->setDecimals(2);
                spin->setValue(value);
                auto *label = new NumericDragLabel(label_text, spin, box,
                    [this]() { numeric_label_dragging_ = true; },
                    [this]() { numeric_label_dragging_ = false; emit audio_property_changed(true); });
                label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                form->addRow(label, spin);
                connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                        [this, i, setter](double v) {
                    if (!layer_ || loading_values_ ||
                        i >= static_cast<int>(layer_->audio_effects.size())) return;
                    setter(layer_->audio_effects[static_cast<size_t>(i)], v);
                    emit audio_property_changed(false);
                });
                connect(spin, &QDoubleSpinBox::editingFinished, this,
                        [this]() { emit audio_property_changed(true); });
            };
            AudioEffect &fx = layer_->audio_effects[static_cast<size_t>(i)];
            if (fx.type == AudioEffectType::Gain)
                add_spin(QStringLiteral("Gain (dB)"), fx.gain_db, -60, 24,
                         [](AudioEffect &e,double v){e.gain_db=float(v);});
            else if (fx.type == AudioEffectType::Fade) {
                add_spin(QStringLiteral("Fade in (s)"), fx.fade_in, 0, 60,
                         [](AudioEffect &e,double v){e.fade_in=v;});
                add_spin(QStringLiteral("Fade out (s)"), fx.fade_out, 0, 60,
                         [](AudioEffect &e,double v){e.fade_out=v;});
            } else if (fx.type == AudioEffectType::HighPass ||
                       fx.type == AudioEffectType::LowPass) {
                add_spin(QStringLiteral("Frequency (Hz)"), fx.frequency_hz, 20, 20000,
                         [](AudioEffect &e,double v){e.frequency_hz=float(v);});
            } else {
                add_spin(QStringLiteral("Threshold (dB)"), fx.threshold_db, -60, 0,
                         [](AudioEffect &e,double v){e.threshold_db=float(v);});
                add_spin(QStringLiteral("Ratio"), fx.ratio, 1, 20,
                         [](AudioEffect &e,double v){e.ratio=float(v);});
                add_spin(QStringLiteral("Attack (ms)"), fx.attack_ms, 0.1, 200,
                         [](AudioEffect &e,double v){e.attack_ms=float(v);});
                add_spin(QStringLiteral("Release (ms)"), fx.release_ms, 1, 2000,
                         [](AudioEffect &e,double v){e.release_ms=float(v);});
            }

            FxmCollapsiblePanel *panel = fxm_add_panel_section(settings_layout_, box);
            if (!panel) continue;
            panel->setProperty("fxmAudioEffectIndex", i);
            panel->setOrderPersistenceEnabled(false);
            effect_panels_.push_back(panel);

            auto *enabled = new FxmSwitch(panel);
            enabled->setToolTip(fxm_tr("OBSTitles.Enabled"));
            enabled->setChecked(fx.enabled);
            panel->addHeaderLeadingWidget(enabled);
            connect(enabled, &QCheckBox::toggled, this, [this, panel](bool value) {
                if (!layer_ || !panel) return;
                const int index = panel->property("fxmAudioEffectIndex").toInt();
                if (index < 0 || index >= static_cast<int>(layer_->audio_effects.size())) return;
                layer_->audio_effects[static_cast<size_t>(index)].enabled = value;
                emit audio_property_changed(true);
            });

            auto *more = new QToolButton(panel);
            more->setText(QStringLiteral("⋮"));
            more->setAutoRaise(true);
            more->setFixedSize(20, 20);
            panel->addHeaderWidget(more);
            connect(panel, &FxmCollapsiblePanel::orderChanged, this, [this]() {
                if (!layer_ || layer_->audio_effects.empty()) return;
                std::vector<AudioEffect> reordered;
                reordered.reserve(layer_->audio_effects.size());
                std::set<int> used;
                for (const auto &candidate : effect_panels_) {
                    if (!candidate) continue;
                    const QVariant value = candidate->property("fxmAudioEffectIndex");
                    if (!value.isValid()) continue;
                    const int source = value.toInt();
                    if (source < 0 || source >= static_cast<int>(layer_->audio_effects.size()) ||
                        !used.insert(source).second) continue;
                    reordered.push_back(layer_->audio_effects[static_cast<size_t>(source)]);
                }
                if (reordered.size() != layer_->audio_effects.size()) return;
                layer_->audio_effects = std::move(reordered);
                rebuild_stack();
                emit audio_property_changed(true);
            });
            connect(more, &QToolButton::clicked, this, [this, panel, more]() {
                if (!layer_ || !panel) return;
                const int index = panel->property("fxmAudioEffectIndex").toInt();
                if (index < 0 || index >= static_cast<int>(layer_->audio_effects.size())) return;
                QMenu menu(more);
                QAction *duplicate = menu.addAction(obs_icon("duplicate.svg"), fxm_tr("OBSTitles.DuplicateEffect"));
                QAction *remove = menu.addAction(obs_icon("delete.svg"), fxm_tr("OBSTitles.DeleteEffect"));
                menu.addSeparator();
                QAction *up = menu.addAction(obs_icon("move-up.svg"), fxm_tr("OBSTitles.MoveEffectUp"));
                QAction *down = menu.addAction(obs_icon("move-down.svg"), fxm_tr("OBSTitles.MoveEffectDown"));
                up->setEnabled(index > 0);
                down->setEnabled(index + 1 < static_cast<int>(layer_->audio_effects.size()));
                QAction *chosen = menu.exec(more->mapToGlobal(QPoint(more->width(), more->height())));
                if (chosen == duplicate) {
                    layer_->audio_effects.insert(layer_->audio_effects.begin() + index + 1,
                                                 layer_->audio_effects[static_cast<size_t>(index)]);
                } else if (chosen == remove) {
                    layer_->audio_effects.erase(layer_->audio_effects.begin() + index);
                } else if (chosen == up) {
                    std::swap(layer_->audio_effects[static_cast<size_t>(index)],
                              layer_->audio_effects[static_cast<size_t>(index - 1)]);
                } else if (chosen == down) {
                    std::swap(layer_->audio_effects[static_cast<size_t>(index)],
                              layer_->audio_effects[static_cast<size_t>(index + 1)]);
                } else return;
                rebuild_stack();
                emit audio_property_changed(true);
            });
        }
        settings_layout_->addStretch(1);
        return;
    }

    if (!layer_ || layer_->effects.empty()) {
        auto *label = new QLabel(layer_ ? fxm_tr("OBSTitles.AddEffectSettingsHint")
                                        : fxm_tr("OBSTitles.SelectLayerEditEffectsHint"),
                                 settings_container_);
        label->setWordWrap(true);
        settings_layout_->addWidget(label);
        settings_layout_->addStretch(1);
        return;
    }

    for (int index = 0; index < static_cast<int>(layer_->effects.size()); ++index)
        build_effect_settings_panel(index);
    settings_layout_->addStretch(1);
    update_bound_controls();
}

void EffectsPanel::build_effect_settings_panel(int effect_index)
{
    if (!layer_ || effect_index < 0 ||
        effect_index >= static_cast<int>(layer_->effects.size()))
        return;

    QScopedValueRollback<int> build_index_guard(building_effect_index_, effect_index);
    QScopedValueRollback<bool> loading_guard(loading_values_, true);
    const size_t numeric_start = numeric_bindings_.size();
    const size_t color_start = color_bindings_.size();
    const size_t bool_start = bool_bindings_.size();
    const size_t combo_start = combo_bindings_.size();
    const size_t keyframe_start = keyframe_bindings_.size();

    const double lt = std::clamp(playhead_ - layer_->in_time, 0.0,
                                 std::max(0.0, layer_->out_time - layer_->in_time));
    const LayerEffect &panel_effect = layer_->effects[static_cast<size_t>(effect_index)];
    const QString panel_title = effect_display_name(panel_effect);
    const QString identity = !panel_effect.extension_id.empty()
        ? QStringLiteral("extension_%1").arg(QString::fromStdString(panel_effect.extension_id))
        : QStringLiteral("builtin_%1").arg(static_cast<int>(panel_effect.type));
    int occurrence = 0;
    for (int prior = 0; prior < effect_index; ++prior) {
        const LayerEffect &candidate = layer_->effects[static_cast<size_t>(prior)];
        const QString candidate_identity = !candidate.extension_id.empty()
            ? QStringLiteral("extension_%1").arg(QString::fromStdString(candidate.extension_id))
            : QStringLiteral("builtin_%1").arg(static_cast<int>(candidate.type));
        if (candidate_identity == identity)
            ++occurrence;
    }

    auto *box = new QGroupBox(panel_title, settings_container_);
    box->setObjectName(QStringLiteral("EffectPanelContent_%1_%2").arg(identity).arg(occurrence));
    box->setProperty("fxmEffectIndex", effect_index);
    box->setProperty("fxmPanelPersistenceGroup", QStringLiteral("EffectsStack"));
    box->setProperty("fxmPanelPersistenceKey",
                     QStringLiteral("%1_%2").arg(identity).arg(occurrence));
    box->setProperty("fxmPersistPanelOrder", false);
    box->setProperty("fxmPanelDefaultsDisabled", true);
    // Effect headers already provide the visual separation. Preserve an
    // intentionally flush body top while keeping the common side/bottom inset.
    box->setProperty("fxmPreservePanelMargins", true);
    auto *form = new QFormLayout(box);
    form->setContentsMargins(10, 0, 10, 10);
    form->setHorizontalSpacing(6);
    form->setVerticalSpacing(4);
    auto spin = [box](double min, double max, double step) { auto *s = new QDoubleSpinBox(box); s->setRange(min, max); s->setSingleStep(step); s->setFixedHeight(20); s->setStyleSheet(fxm_theme_control_style()); return s; };
    auto combo = [box]() { auto *c = new QComboBox(box); c->setFixedHeight(20); c->setStyleSheet(fxm_theme_control_style()); return c; };
    auto color_button = [this, box](uint32_t argb, auto setter) {
        auto *button = new QPushButton(box);
        set_color_button_argb(button, argb);
        connect(button, &QPushButton::clicked, this, [this, button, setter]() {
            QColor picked = fxm_pick_color(color_from_argb(color_button_argb(button)), this, fxm_tr("OBSTitles.ChooseColor"));
            if (!picked.isValid()) return;
            uint32_t argb = argb_from_color(picked);
            set_color_button_argb(button, argb);
            setter(argb);
            emit_effect_changed();
        });
        return button;
    };
    auto bind_numeric = [this](QDoubleSpinBox *spin,
                               std::function<double(const LayerEffect &, double)> value) {
        if (spin && value)
            numeric_bindings_.push_back({spin, std::move(value)});
    };
    auto bind_color = [this](QPushButton *button,
                             std::function<uint32_t(const LayerEffect &, double)> value) {
        if (button && value)
            color_bindings_.push_back({button, std::move(value)});
    };

    auto wrap_scalar_keyframe = [this, box, lt](QWidget *field,
            AnimatedProperty LayerEffect::*property,
            std::function<double()> current_value = {}) -> QWidget * {
        if (!field || !property)
            return field;
        auto *row = new QWidget(box);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(field, 1);
        auto *button = make_effect_keyframe_button(row);
        button->setToolTip(tr("Toggle keyframe at the current timeline position"));
        button->setAccessibleName(button->toolTip());
        keyframe_bindings_.push_back({button,
            [property](const LayerEffect &effect, double time) {
                return effect_property_has_keyframe_at(effect.*property, time);
            },
            [property](const LayerEffect &effect) {
                return (effect.*property).is_animated();
            },
            [property](LayerEffect &effect) {
                (effect.*property).keyframes.clear();
            },
            [property](const LayerEffect &effect) {
                std::vector<double> times;
                for (const Keyframe &key : (effect.*property).keyframes)
                    times.push_back(key.time);
                return times;
            }});
        layout->addWidget(make_keyframe_controls(button, row));
        connect(button, &QPushButton::clicked, this,
                [this, property, current_value, field]() {
            LayerEffect *effect = selected_effect();
            if (!effect || !layer_)
                return;
            const double time = std::clamp(playhead_ - layer_->in_time, 0.0,
                std::max(0.0, layer_->out_time - layer_->in_time));
            AnimatedProperty &animated = effect->*property;
            if (effect_property_has_keyframe_at(animated, time)) {
                remove_keyframe_at(animated, time);
            } else {
                double value = animated.evaluate(time);
                if (current_value)
                    value = current_value();
                else if (auto *double_spin = qobject_cast<QDoubleSpinBox *>(field))
                    value = double_spin->value();
                else if (auto *int_spin = qobject_cast<QSpinBox *>(field))
                    value = int_spin->value();
                add_or_replace_keyframe(animated, time, value);
            }
            emit_effect_changed();
            update_bound_controls();
        });
        field->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(field, &QWidget::customContextMenuRequested, this,
                [this, property, field](const QPoint &position) {
            QMenu menu(field);
            QAction *reset = menu.addAction(tr("Reset Parameter"));
            if (menu.exec(field->mapToGlobal(position)) != reset)
                return;
            LayerEffect *effect = selected_effect();
            if (!effect || !reset_effect_scalar_parameter(*effect, property))
                return;
            emit_effect_changed();
            update_bound_controls();
        });
        return row;
    };

    auto wrap_color_keyframe = [this, box, lt](QPushButton *field,
            AnimatedProperty LayerEffect::*a,
            AnimatedProperty LayerEffect::*r,
            AnimatedProperty LayerEffect::*g,
            AnimatedProperty LayerEffect::*b) -> QWidget * {
        if (!field || !a || !r || !g || !b)
            return field;
        auto *row = new QWidget(box);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addWidget(field, 1);
        auto *button = make_effect_keyframe_button(row);
        button->setToolTip(tr("Toggle color keyframe at the current timeline position"));
        button->setAccessibleName(button->toolTip());
        keyframe_bindings_.push_back({button,
            [a, r, g, b](const LayerEffect &effect, double time) {
                return effect_property_has_keyframe_at(effect.*a, time) ||
                       effect_property_has_keyframe_at(effect.*r, time) ||
                       effect_property_has_keyframe_at(effect.*g, time) ||
                       effect_property_has_keyframe_at(effect.*b, time);
            },
            [a, r, g, b](const LayerEffect &effect) {
                return (effect.*a).is_animated() || (effect.*r).is_animated() ||
                       (effect.*g).is_animated() || (effect.*b).is_animated();
            },
            [a, r, g, b](LayerEffect &effect) {
                (effect.*a).keyframes.clear();
                (effect.*r).keyframes.clear();
                (effect.*g).keyframes.clear();
                (effect.*b).keyframes.clear();
            },
            [a, r, g, b](const LayerEffect &effect) {
                std::vector<double> times;
                for (const AnimatedProperty *property : {&(effect.*a), &(effect.*r),
                                                         &(effect.*g), &(effect.*b)})
                    for (const Keyframe &key : property->keyframes)
                        times.push_back(key.time);
                return times;
            }});
        layout->addWidget(make_keyframe_controls(button, row));
        connect(button, &QPushButton::clicked, this,
                [this, field, a, r, g, b]() {
            LayerEffect *effect = selected_effect();
            if (!effect || !layer_)
                return;
            const double time = std::clamp(playhead_ - layer_->in_time, 0.0,
                std::max(0.0, layer_->out_time - layer_->in_time));
            const bool keyed = effect_property_has_keyframe_at(effect->*a, time) ||
                               effect_property_has_keyframe_at(effect->*r, time) ||
                               effect_property_has_keyframe_at(effect->*g, time) ||
                               effect_property_has_keyframe_at(effect->*b, time);
            if (keyed) {
                remove_keyframe_at(effect->*a, time);
                remove_keyframe_at(effect->*r, time);
                remove_keyframe_at(effect->*g, time);
                remove_keyframe_at(effect->*b, time);
            } else {
                const uint32_t argb = field->property("argb").toUInt();
                add_or_replace_keyframe(effect->*a, time, (argb >> 24) & 0xFF);
                add_or_replace_keyframe(effect->*r, time, (argb >> 16) & 0xFF);
                add_or_replace_keyframe(effect->*g, time, (argb >> 8) & 0xFF);
                add_or_replace_keyframe(effect->*b, time, argb & 0xFF);
            }
            emit_effect_changed();
            update_bound_controls();
        });
        field->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(field, &QWidget::customContextMenuRequested, this,
                [this, a, field](const QPoint &position) {
            QMenu menu(field);
            QAction *reset = menu.addAction(tr("Reset Parameter"));
            if (menu.exec(field->mapToGlobal(position)) != reset)
                return;
            LayerEffect *effect = selected_effect();
            if (!effect || !reset_effect_color_parameter(*effect, a))
                return;
            emit_effect_changed();
            update_bound_controls();
        });
        return row;
    };

    auto add_effect_row = [this, box, form](const QString &label_text, QWidget *field) {
        if (label_text.isEmpty()) {
            form->addRow(label_text, field);
            return;
        }
        auto *label = new NumericDragLabel(label_text, field, box,
                                           [this]() {
                                                if (loading_values_) return;
                                                numeric_label_dragging_ = true;
                                            },
                                           [this]() {
                                                if (loading_values_) return;
                                                numeric_label_dragging_ = false;
                                                emit property_changed(true);
                                           });
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setStyleSheet(QStringLiteral("color:%1;background:transparent;font-size:10px;")
            .arg(qApp->palette().color(QPalette::WindowText).name(QColor::HexRgb)));
        form->addRow(label, field);
    };

    const FxmEffectExtensionDefinition *extension_definition = nullptr;
    if (!selected_effect()->extension_id.empty()) {
        auto &catalog = FxmEffectExtensionCatalog::instance();
        if (catalog.effects().empty()) catalog.reload();
        extension_definition = catalog.find(QString::fromStdString(selected_effect()->extension_id));
    }
    if (extension_definition && (!extension_definition->builtIn ||
                                 selected_effect()->type == LayerEffectType::FourColorGradient)) {
        LayerEffect *effect = selected_effect();
        QJsonDocument state_doc = QJsonDocument::fromJson(QByteArray::fromStdString(effect->extension_parameters_json));
        QJsonObject state = state_doc.isObject() ? state_doc.object() : extension_definition->defaults;
        const QJsonArray preset_items = extension_definition->presetIndex.value(QStringLiteral("items")).toArray();
        if (!preset_items.isEmpty()) {
            auto *preset = combo();
            preset->addItem(tr("Custom"), QString());
            for (const auto &value : preset_items) {
                const QJsonObject item = value.toObject();
                preset->addItem(item.value(QStringLiteral("name")).toString(), item.value(QStringLiteral("file")).toString());
            }
            add_effect_row(tr("Preset"), preset);
            connect(preset, QOverload<int>::of(&QComboBox::activated), this,
                    [this, preset, extensionBasePath = extension_definition->basePath](int index) {
                if (loading_values_ || index <= 0 || !selected_effect()) return;
                const QString relative = preset->itemData(index).toString();
                const QString presetPath = QFileInfo(relative).isAbsolute()
                    ? relative : QDir(extensionBasePath).filePath(relative);
                QFile file(presetPath);
                if (!file.open(QIODevice::ReadOnly)) return;
                const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
                if (!doc.isObject()) return;
                const QJsonObject presetObject = doc.object();
                const QJsonObject parameters = presetObject.value(QStringLiteral("parameters")).isObject()
                    ? presetObject.value(QStringLiteral("parameters")).toObject()
                    : presetObject;
                selected_effect()->extension_parameters_json = QJsonDocument(parameters).toJson(QJsonDocument::Compact).toStdString();
                emit_effect_changed();
                load_settings();
            });
        }

        for (auto it = extension_definition->parameterSchema.begin(); it != extension_definition->parameterSchema.end(); ++it) {
            const QString key = it.key();
            const QJsonObject meta = it.value().toObject();
            const QString type = meta.value(QStringLiteral("type")).toString();
            const QString label = meta.value(QStringLiteral("label")).toString(key);
            if (type == QStringLiteral("float") || type == QStringLiteral("int")) {
                auto *field = spin(meta.value(QStringLiteral("min")).toDouble(-100000.0),
                                   meta.value(QStringLiteral("max")).toDouble(100000.0),
                                   meta.value(QStringLiteral("step")).toDouble(type == QStringLiteral("int") ? 1.0 : 0.01));
                field->setDecimals(type == QStringLiteral("int") ? 0 : 3);
                const QJsonValue numericFallback = state.value(key).isUndefined()
                    ? meta.value(QStringLiteral("default")) : state.value(key);
                field->setValue(evaluate_extension_track(*effect, key, lt, numericFallback).toDouble(numericFallback.toDouble()));
                bind_numeric(field, [key, numericFallback](const LayerEffect &current, double time) {
                    return evaluate_extension_track(current, key, time, numericFallback).toDouble(numericFallback.toDouble());
                });
                QWidget *valueWidget = field;
                QPushButton *keyframeButton = nullptr;
                if (meta.value(QStringLiteral("animatable")).toBool(false)) {
                    auto *row = new QWidget(box);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    layout->addWidget(field, 1);
                    keyframeButton = make_effect_keyframe_button(row);
                    keyframeButton->setToolTip(tr("Toggle extension keyframe at the current timeline position"));
                    keyframeButton->setAccessibleName(keyframeButton->toolTip());
                    keyframe_bindings_.push_back({keyframeButton,
                        [key](const LayerEffect &current, double time) {
                            return extension_track_has_keyframe_at(current, key, time);
                        },
                        [key](const LayerEffect &current) {
                            return !fxm::effects::animation::track_keys(current, key).isEmpty();
                        },
                        [key](LayerEffect &current) {
                            fxm::effects::animation::write_track_keys(current, key, {});
                        },
                        [key](const LayerEffect &current) {
                            return extension_track_keyframe_times(current, key);
                        }});
                    layout->addWidget(make_keyframe_controls(keyframeButton, row));
                    valueWidget = row;
                }
                add_effect_row(label, valueWidget);
                connect(field, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                        [this, key, type](double value) {
                    LayerEffect *active = selected_effect();
                    if (loading_values_ || !active) return;
                    const QJsonValue encoded = type == QStringLiteral("int")
                        ? QJsonValue(static_cast<int>(value)) : QJsonValue(value);
                    fxm::effects::animation::set_animated_value(
                        *active, key, current_local_time(), encoded);
                    emit_effect_changed();
                });
                if (keyframeButton) connect(keyframeButton, &QPushButton::clicked, this,
                        [this, key, field, type]() {
                    LayerEffect *active = selected_effect();
                    if (!active) return;
                    const QJsonValue value = type == QStringLiteral("int")
                        ? QJsonValue(static_cast<int>(field->value()))
                        : QJsonValue(field->value());
                    fxm::effects::animation::toggle_keyframe(
                        *active, key, current_local_time(), value);
                    emit_effect_changed();
                    update_bound_controls();
                });
            } else if (type == QStringLiteral("point")) {
                const QJsonArray current = state.value(key).toArray();
                auto *row = new QWidget(box);
                auto *layout = new QHBoxLayout(row);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(4);
                auto *x = spin(meta.value(QStringLiteral("minX")).toDouble(0.0),
                               meta.value(QStringLiteral("maxX")).toDouble(1.0),
                               meta.value(QStringLiteral("step")).toDouble(0.01));
                auto *y = spin(meta.value(QStringLiteral("minY")).toDouble(0.0),
                               meta.value(QStringLiteral("maxY")).toDouble(1.0),
                               meta.value(QStringLiteral("step")).toDouble(0.01));
                const QJsonArray evaluatedPoint = evaluate_extension_track(
                    *effect, key, lt, current).toArray();
                x->setValue(evaluatedPoint.size() > 0 ? evaluatedPoint.at(0).toDouble(0.5) : 0.5);
                y->setValue(evaluatedPoint.size() > 1 ? evaluatedPoint.at(1).toDouble(0.5) : 0.5);
                bind_numeric(x, [key, current](const LayerEffect &active, double time) {
                    const QJsonArray value = evaluate_extension_track(active, key, time, current).toArray();
                    return value.size() > 0 ? value.at(0).toDouble(0.5) : 0.5;
                });
                bind_numeric(y, [key, current](const LayerEffect &active, double time) {
                    const QJsonArray value = evaluate_extension_track(active, key, time, current).toArray();
                    return value.size() > 1 ? value.at(1).toDouble(0.5) : 0.5;
                });
                auto drag_started = [this]() {
                    if (!loading_values_)
                        numeric_label_dragging_ = true;
                };
                auto drag_finished = [this]() {
                    if (loading_values_)
                        return;
                    numeric_label_dragging_ = false;
                    emit property_changed(true);
                };
                auto *xLabel = new NumericDragLabel(QStringLiteral("X"), x, row,
                                                     drag_started, drag_finished);
                auto *yLabel = new NumericDragLabel(QStringLiteral("Y"), y, row,
                                                     drag_started, drag_finished);
                xLabel->setAlignment(Qt::AlignCenter);
                yLabel->setAlignment(Qt::AlignCenter);
                xLabel->setMinimumWidth(12);
                yLabel->setMinimumWidth(12);
                layout->addWidget(xLabel);
                layout->addWidget(x, 1);
                layout->addWidget(yLabel);
                layout->addWidget(y, 1);
                QPushButton *keyframeButton = nullptr;
                if (meta.value(QStringLiteral("animatable")).toBool(false)) {
                    keyframeButton = make_effect_keyframe_button(row);
                    keyframeButton->setToolTip(tr("Toggle extension keyframe at the current timeline position"));
                    keyframeButton->setAccessibleName(keyframeButton->toolTip());
                    keyframe_bindings_.push_back({keyframeButton,
                        [key](const LayerEffect &active, double time) {
                            return extension_track_has_keyframe_at(active, key, time);
                        },
                        [key](const LayerEffect &active) {
                            return !fxm::effects::animation::track_keys(active, key).isEmpty();
                        },
                        [key](LayerEffect &active) {
                            fxm::effects::animation::write_track_keys(active, key, {});
                        },
                        [key](const LayerEffect &active) {
                            return extension_track_keyframe_times(active, key);
                        }});
                    layout->addWidget(make_keyframe_controls(keyframeButton, row));
                }
                add_effect_row(label, row);
                auto writePoint = [this, key, x, y]() {
                    LayerEffect *active = selected_effect();
                    if (loading_values_ || !active) return;
                    fxm::effects::animation::set_animated_value(
                        *active, key, current_local_time(),
                        QJsonArray{x->value(), y->value()});
                    emit_effect_changed();
                };
                connect(x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [writePoint](double) { writePoint(); });
                connect(y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [writePoint](double) { writePoint(); });
                if (keyframeButton) connect(keyframeButton, &QPushButton::clicked, this,
                        [this, key, x, y]() {
                    LayerEffect *active = selected_effect();
                    if (!active) return;
                    fxm::effects::animation::toggle_keyframe(
                        *active, key, current_local_time(),
                        QJsonArray{x->value(), y->value()});
                    emit_effect_changed();
                    update_bound_controls();
                });
            } else if (type == QStringLiteral("color")) {
                const QJsonValue fallback = state.value(key).isUndefined()
                    ? meta.value(QStringLiteral("default")) : state.value(key);
                const QJsonValue evaluated = evaluate_extension_track(
                    *effect, key, lt, fallback);
                auto *field = color_button(extension_json_color_to_argb(evaluated),
                    [this, key](uint32_t argb) {
                        LayerEffect *active = selected_effect();
                        if (!active) return;
                        fxm::effects::animation::set_animated_value(
                            *active, key, current_local_time(),
                            extension_argb_to_json_color(argb));
                    });
                bind_color(field, [key, fallback](const LayerEffect &active, double time) {
                    return extension_json_color_to_argb(
                        evaluate_extension_track(active, key, time, fallback));
                });
                QWidget *valueWidget = field;
                if (meta.value(QStringLiteral("animatable")).toBool(false)) {
                    auto *row = new QWidget(box);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    layout->addWidget(field, 1);
                    auto *button = make_effect_keyframe_button(row);
                    button->setToolTip(tr("Toggle extension color keyframe at the current timeline position"));
                    button->setAccessibleName(button->toolTip());
                    keyframe_bindings_.push_back({button,
                        [key](const LayerEffect &active, double time) {
                            return extension_track_has_keyframe_at(active, key, time);
                        },
                        [key](const LayerEffect &active) {
                            return !fxm::effects::animation::track_keys(active, key).isEmpty();
                        },
                        [key](LayerEffect &active) {
                            fxm::effects::animation::write_track_keys(active, key, {});
                        },
                        [key](const LayerEffect &active) {
                            return extension_track_keyframe_times(active, key);
                        }});
                    layout->addWidget(make_keyframe_controls(button, row));
                    connect(button, &QPushButton::clicked, this,
                            [this, key, field]() {
                        LayerEffect *active = selected_effect();
                        if (!active) return;
                        fxm::effects::animation::toggle_keyframe(
                            *active, key, current_local_time(),
                            extension_argb_to_json_color(color_button_argb(field)));
                        emit_effect_changed();
                        update_bound_controls();
                    });
                    valueWidget = row;
                }
                add_effect_row(label, valueWidget);
            } else if (type == QStringLiteral("bool")) {
                const QJsonValue fallback = state.value(key).isUndefined()
                    ? meta.value(QStringLiteral("default")) : state.value(key);
                auto *field = new FxmSwitch(box);
                field->setChecked(evaluate_extension_track(
                    *effect, key, lt, fallback).toBool(fallback.toBool()));
                bool_bindings_.push_back({field,
                    [key, fallback](const LayerEffect &active, double time) {
                        return evaluate_extension_track(active, key, time, fallback)
                            .toBool(fallback.toBool());
                    }});

                QWidget *valueWidget = field;
                QPushButton *keyframeButton = nullptr;
                if (meta.value(QStringLiteral("animatable")).toBool(false)) {
                    auto *row = new QWidget(box);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    layout->addWidget(field, 1);
                    keyframeButton = make_effect_keyframe_button(row);
                    keyframeButton->setToolTip(
                        tr("Toggle extension hold keyframe at the current timeline position"));
                    keyframeButton->setAccessibleName(keyframeButton->toolTip());
                    keyframe_bindings_.push_back({keyframeButton,
                        [key](const LayerEffect &active, double time) {
                            return extension_track_has_keyframe_at(active, key, time);
                        },
                        [key](const LayerEffect &active) {
                            return !fxm::effects::animation::track_keys(active, key).isEmpty();
                        },
                        [key](LayerEffect &active) {
                            fxm::effects::animation::write_track_keys(active, key, {});
                        },
                        [key](const LayerEffect &active) {
                            return extension_track_keyframe_times(active, key);
                        }});
                    layout->addWidget(make_keyframe_controls(keyframeButton, row));
                    valueWidget = row;
                }
                add_effect_row(label, valueWidget);

                connect(field, &QCheckBox::toggled, this,
                        [this, key](bool value) {
                    LayerEffect *active = selected_effect();
                    if (loading_values_ || !active) return;
                    if (fxm::effects::animation::track_keys(*active, key).isEmpty())
                        fxm::effects::animation::set_state_path_value(*active, key, value);
                    else
                        fxm::effects::animation::add_or_replace_keyframe(
                            *active, key, current_local_time(), value, EasingType::Hold);
                    emit_effect_changed();
                });

                if (keyframeButton) {
                    connect(keyframeButton, &QPushButton::clicked, this,
                            [this, key, field]() {
                        LayerEffect *active = selected_effect();
                        if (!active) return;
                        fxm::effects::animation::toggle_keyframe(
                            *active, key, current_local_time(), field->isChecked(),
                            EasingType::Hold);
                        emit_effect_changed();
                        update_bound_controls();
                    });
                }
            } else if (type == QStringLiteral("enum")) {
                const QJsonValue fallback = state.value(key).isUndefined()
                    ? meta.value(QStringLiteral("default")) : state.value(key);
                auto *field = combo();
                for (const QJsonValue &option : meta.value(QStringLiteral("options")).toArray()) {
                    if (option.isObject()) {
                        const QJsonObject object = option.toObject();
                        field->addItem(object.value(QStringLiteral("label")).toString(),
                                       object.value(QStringLiteral("value")).toVariant());
                    } else {
                        field->addItem(option.toString(), option.toVariant());
                    }
                }
                const QVariant evaluated = evaluate_extension_track(
                    *effect, key, lt, fallback).toVariant();
                int initialIndex = field->findData(evaluated);
                if (initialIndex < 0 && field->count() > 0)
                    initialIndex = 0;
                field->setCurrentIndex(initialIndex);
                combo_bindings_.push_back({field,
                    [key, fallback](const LayerEffect &active, double time) {
                        return evaluate_extension_track(active, key, time, fallback).toVariant();
                    }});

                QWidget *valueWidget = field;
                QPushButton *keyframeButton = nullptr;
                if (meta.value(QStringLiteral("animatable")).toBool(false)) {
                    auto *row = new QWidget(box);
                    auto *layout = new QHBoxLayout(row);
                    layout->setContentsMargins(0, 0, 0, 0);
                    layout->setSpacing(4);
                    layout->addWidget(field, 1);
                    keyframeButton = make_effect_keyframe_button(row);
                    keyframeButton->setToolTip(
                        tr("Toggle extension hold keyframe at the current timeline position"));
                    keyframeButton->setAccessibleName(keyframeButton->toolTip());
                    keyframe_bindings_.push_back({keyframeButton,
                        [key](const LayerEffect &active, double time) {
                            return extension_track_has_keyframe_at(active, key, time);
                        },
                        [key](const LayerEffect &active) {
                            return !fxm::effects::animation::track_keys(active, key).isEmpty();
                        },
                        [key](LayerEffect &active) {
                            fxm::effects::animation::write_track_keys(active, key, {});
                        },
                        [key](const LayerEffect &active) {
                            return extension_track_keyframe_times(active, key);
                        }});
                    layout->addWidget(make_keyframe_controls(keyframeButton, row));
                    valueWidget = row;
                }
                add_effect_row(label, valueWidget);

                connect(field, QOverload<int>::of(&QComboBox::activated), this,
                        [this, field, key](int) {
                    LayerEffect *active = selected_effect();
                    if (loading_values_ || !active) return;
                    const QJsonValue value = QJsonValue::fromVariant(field->currentData());
                    if (fxm::effects::animation::track_keys(*active, key).isEmpty())
                        fxm::effects::animation::set_state_path_value(*active, key, value);
                    else
                        fxm::effects::animation::add_or_replace_keyframe(
                            *active, key, current_local_time(), value, EasingType::Hold);
                    emit_effect_changed();
                });

                if (keyframeButton) {
                    connect(keyframeButton, &QPushButton::clicked, this,
                            [this, key, field]() {
                        LayerEffect *active = selected_effect();
                        if (!active || field->currentIndex() < 0) return;
                        fxm::effects::animation::toggle_keyframe(
                            *active, key, current_local_time(),
                            QJsonValue::fromVariant(field->currentData()), EasingType::Hold);
                        emit_effect_changed();
                        update_bound_controls();
                    });
                }
            }
        }

        if (extension_definition->capabilities.value(QStringLiteral("compoundGraph")).toBool()) {
            auto *studio = new QWidget(box);
            auto *studioLayout = new QVBoxLayout(studio);
            studioLayout->setContentsMargins(0, 0, 0, 0);
            studioLayout->setSpacing(6);

            auto *hint = new QLabel(tr("Build the flare from optical elements. Select an element to edit it; drag the main Light Position above to animate the complete flare."), studio);
            hint->setWordWrap(true);
            studioLayout->addWidget(hint);

            auto *elementsList = new QListWidget(studio);
            elementsList->setMinimumHeight(145);
            elementsList->setSelectionMode(QAbstractItemView::SingleSelection);
            studioLayout->addWidget(elementsList);

            auto *toolbar = new QWidget(studio);
            auto *toolbarLayout = new QHBoxLayout(toolbar);
            toolbarLayout->setContentsMargins(0, 0, 0, 0);
            toolbarLayout->setSpacing(4);
            auto *addElement = new QPushButton(tr("Add"), toolbar);
            auto *duplicateElement = new QPushButton(tr("Duplicate"), toolbar);
            auto *removeElement = new QPushButton(tr("Remove"), toolbar);
            auto *moveUp = new QPushButton(QStringLiteral("↑"), toolbar);
            auto *moveDown = new QPushButton(QStringLiteral("↓"), toolbar);
            toolbarLayout->addWidget(addElement);
            toolbarLayout->addWidget(duplicateElement);
            toolbarLayout->addWidget(removeElement);
            toolbarLayout->addStretch(1);
            toolbarLayout->addWidget(moveUp);
            toolbarLayout->addWidget(moveDown);
            studioLayout->addWidget(toolbar);

            auto *properties = new QGroupBox(tr("Selected element"), studio);
            auto *propertiesForm = new QFormLayout(properties);
            propertiesForm->setContentsMargins(8, 8, 8, 8);
            propertiesForm->setSpacing(5);
            auto *elementType = combo();
            elementType->addItem(tr("Glow / Disc"), 0);
            elementType->addItem(tr("Ring"), 1);
            elementType->addItem(tr("Polygon Ghost"), 2);
            elementType->addItem(tr("Anamorphic Streak"), 3);
            elementType->addItem(tr("Soft Iris"), 4);
            auto *elementPosition = spin(-2.0, 2.0, 0.01);
            auto *elementSize = spin(0.001, 2.0, 0.01);
            auto *elementOpacity = spin(0.0, 5.0, 0.01);
            auto *elementSoftness = spin(0.0, 2.0, 0.01);
            auto *elementAspect = spin(0.005, 20.0, 0.01);
            auto *elementRotation = spin(-1000000000.0, 1000000000.0, 1.0);
            auto *elementColor = new QPushButton(properties);
            elementColor->setFixedHeight(20);
            auto element_keyframe_row = [this, elementsList, properties](
                    QWidget *field, const QString &property,
                    std::function<QJsonValue()> value) -> QWidget * {
                auto *row = new QWidget(properties);
                auto *layout = new QHBoxLayout(row);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(4);
                layout->addWidget(field, 1);
                auto *button = make_effect_keyframe_button(row);
                button->setToolTip(tr("Toggle element keyframe at the current timeline position"));
                button->setAccessibleName(button->toolTip());
                keyframe_bindings_.push_back({button,
                    [elementsList, property](const LayerEffect &effect, double time) {
                        const int index = elementsList->currentRow();
                        return index >= 0 && extension_track_has_keyframe_at(
                            effect, QStringLiteral("elements.%1.%2").arg(index).arg(property), time);
                    },
                    [elementsList, property](const LayerEffect &effect) {
                        const int index = elementsList->currentRow();
                        return index >= 0 && !fxm::effects::animation::track_keys(
                            effect, QStringLiteral("elements.%1.%2").arg(index).arg(property)).isEmpty();
                    },
                    [elementsList, property](LayerEffect &effect) {
                        const int index = elementsList->currentRow();
                        if (index >= 0)
                            fxm::effects::animation::write_track_keys(
                                effect, QStringLiteral("elements.%1.%2").arg(index).arg(property), {});
                    },
                    [elementsList, property](const LayerEffect &effect) {
                        const int index = elementsList->currentRow();
                        return index < 0 ? std::vector<double>{}
                            : extension_track_keyframe_times(
                                effect, QStringLiteral("elements.%1.%2")
                                    .arg(index).arg(property));
                    }});
                layout->addWidget(make_keyframe_controls(button, row));
                connect(button, &QPushButton::clicked, this,
                        [this, elementsList, property, value]() {
                    LayerEffect *effect = selected_effect();
                    const int index = elementsList->currentRow();
                    if (!effect || index < 0) return;
                    const QString path = QStringLiteral("elements.%1.%2")
                        .arg(index).arg(property);
                    fxm::effects::animation::toggle_keyframe(
                        *effect, path, current_local_time(), value());
                    emit_effect_changed();
                    update_bound_controls();
                });
                return row;
            };
            auto element_animated_value = [elementsList](
                    const LayerEffect &effect, const QString &property,
                    double time, double fallback) {
                const int index = elementsList->currentRow();
                if (index < 0) return fallback;
                const QString path = QStringLiteral("elements.%1.%2").arg(index).arg(property);
                const QJsonValue base = extension_state_path_value(effect, path);
                return evaluate_extension_track(effect, path, time, base).toDouble(fallback);
            };
            bind_numeric(elementPosition, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("position"), time, 0.0);
            });
            bind_numeric(elementSize, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("size"), time, 0.1);
            });
            bind_numeric(elementOpacity, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("opacity"), time, 1.0);
            });
            bind_numeric(elementSoftness, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("softness"), time, 0.25);
            });
            bind_numeric(elementAspect, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("aspect"), time, 1.0);
            });
            bind_numeric(elementRotation, [element_animated_value](const LayerEffect &effect, double time) {
                return element_animated_value(effect, QStringLiteral("rotation"), time, 0.0);
            });
            bind_color(elementColor, [elementsList](const LayerEffect &effect, double time) {
                const int index = elementsList->currentRow();
                if (index < 0) return uint32_t{0xFFFFFFFFu};
                const QString path = QStringLiteral("elements.%1.color").arg(index);
                const QJsonValue base = extension_state_path_value(effect, path);
                const QJsonArray value = evaluate_extension_track(effect, path, time, base).toArray();
                QColor color;
                color.setRgbF(value.size() > 0 ? value.at(0).toDouble(1.0) : 1.0,
                              value.size() > 1 ? value.at(1).toDouble(1.0) : 1.0,
                              value.size() > 2 ? value.at(2).toDouble(1.0) : 1.0,
                              value.size() > 3 ? value.at(3).toDouble(1.0) : 1.0);
                return argb_from_color(color);
            });
            propertiesForm->addRow(tr("Type"), elementType);
            propertiesForm->addRow(tr("Axis Position"), element_keyframe_row(elementPosition, QStringLiteral("position"), [elementPosition]() { return QJsonValue(elementPosition->value()); }));
            propertiesForm->addRow(tr("Size"), element_keyframe_row(elementSize, QStringLiteral("size"), [elementSize]() { return QJsonValue(elementSize->value()); }));
            propertiesForm->addRow(tr("Brightness"), element_keyframe_row(elementOpacity, QStringLiteral("opacity"), [elementOpacity]() { return QJsonValue(elementOpacity->value()); }));
            propertiesForm->addRow(tr("Softness"), element_keyframe_row(elementSoftness, QStringLiteral("softness"), [elementSoftness]() { return QJsonValue(elementSoftness->value()); }));
            propertiesForm->addRow(tr("Aspect"), element_keyframe_row(elementAspect, QStringLiteral("aspect"), [elementAspect]() { return QJsonValue(elementAspect->value()); }));
            propertiesForm->addRow(tr("Rotation"), element_keyframe_row(elementRotation, QStringLiteral("rotation"), [elementRotation]() { return QJsonValue(elementRotation->value()); }));
            propertiesForm->addRow(tr("Color"), element_keyframe_row(elementColor, QStringLiteral("color"), [elementColor]() {
                const QColor color = color_from_argb(color_button_argb(elementColor));
                return QJsonValue(QJsonArray{color.redF(), color.greenF(), color.blueF(), color.alphaF()});
            }));
            fxm_add_panel_section(studioLayout, properties);
            add_effect_row(tr("Flare Designer"), studio);

            auto readState = [this]() {
                if (!selected_effect()) return QJsonObject{};
                const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(selected_effect()->extension_parameters_json));
                return doc.isObject() ? doc.object() : QJsonObject{};
            };
            auto writeState = [this](const QJsonObject &object) {
                if (!selected_effect()) return;
                selected_effect()->extension_parameters_json = QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
                emit_effect_changed();
            };
            auto elementName = [this](const QJsonObject &element, int index) {
                static const char *names[] = {"Glow / Disc", "Ring", "Polygon Ghost", "Anamorphic Streak", "Soft Iris"};
                const int type = std::clamp(element.value(QStringLiteral("type")).toInt(), 0, 4);
                return tr("%1. %2").arg(index + 1).arg(tr(names[type]));
            };
            auto rebuildList = [elementsList, readState, elementName](int preferredRow) {
                const QJsonArray elements = readState().value(QStringLiteral("elements")).toArray();
                QSignalBlocker blocker(elementsList);
                elementsList->clear();
                for (int i = 0; i < elements.size(); ++i)
                    elementsList->addItem(elementName(elements.at(i).toObject(), i));
                if (!elements.isEmpty()) {
                    const int lastRow = static_cast<int>(elements.size()) - 1;
                    elementsList->setCurrentRow(std::clamp(preferredRow, 0, lastRow));
                }
            };
            auto loadElement = [=](int row) {
                const QJsonArray elements = readState().value(QStringLiteral("elements")).toArray();
                const bool valid = row >= 0 && row < elements.size();
                properties->setEnabled(valid);
                if (!valid) return;
                const QJsonObject element = elements.at(row).toObject();
                QSignalBlocker b0(elementType), b1(elementPosition), b2(elementSize), b3(elementOpacity), b4(elementSoftness), b5(elementAspect), b6(elementRotation);
                elementType->setCurrentIndex(std::max(0, elementType->findData(element.value(QStringLiteral("type")).toInt(0))));
                elementPosition->setValue(element.value(QStringLiteral("position")).toDouble(0.0));
                elementSize->setValue(element.value(QStringLiteral("size")).toDouble(0.1));
                elementOpacity->setValue(element.value(QStringLiteral("opacity")).toDouble(1.0));
                elementSoftness->setValue(element.value(QStringLiteral("softness")).toDouble(0.25));
                elementAspect->setValue(element.value(QStringLiteral("aspect")).toDouble(1.0));
                elementRotation->setValue(element.value(QStringLiteral("rotation")).toDouble(0.0));
                const QJsonArray color = element.value(QStringLiteral("color")).toArray();
                QColor qcolor;
                qcolor.setRgbF(color.size() > 0 ? color.at(0).toDouble(1.0) : 1.0,
                               color.size() > 1 ? color.at(1).toDouble(1.0) : 1.0,
                               color.size() > 2 ? color.at(2).toDouble(1.0) : 1.0,
                               color.size() > 3 ? color.at(3).toDouble(1.0) : 1.0);
                set_color_button_argb(elementColor, argb_from_color(qcolor));
                update_bound_controls();
            };
            auto updateElement = [=](const QString &key, const QJsonValue &value) {
                const int row = elementsList->currentRow();
                LayerEffect *effect = selected_effect();
                if (!effect || row < 0)
                    return;
                const QString path = QStringLiteral("elements.%1.%2").arg(row).arg(key);
                fxm::effects::animation::set_animated_value(
                    *effect, path, current_local_time(), value);
                rebuildList(row);
                update_bound_controls();
                emit_effect_changed();
            };

            connect(elementsList, &QListWidget::currentRowChanged, this, loadElement);
            connect(elementType, QOverload<int>::of(&QComboBox::activated), this, [=](int) { updateElement(QStringLiteral("type"), elementType->currentData().toInt()); });
            connect(elementPosition, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("position"), v); });
            connect(elementSize, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("size"), v); });
            connect(elementOpacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("opacity"), v); });
            connect(elementSoftness, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("softness"), v); });
            connect(elementAspect, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("aspect"), v); });
            connect(elementRotation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [=](double v) { if (!loading_values_) updateElement(QStringLiteral("rotation"), v); });
            connect(elementColor, &QPushButton::clicked, this, [=]() {
                const QColor current = color_from_argb(color_button_argb(elementColor));
                const QColor picked = fxm_pick_color(current, this, tr("Element color"));
                if (!picked.isValid()) return;
                set_color_button_argb(elementColor, argb_from_color(picked));
                QJsonArray encoded; encoded.append(picked.redF()); encoded.append(picked.greenF()); encoded.append(picked.blueF()); encoded.append(picked.alphaF());
                updateElement(QStringLiteral("color"), encoded);
            });
            connect(addElement, &QPushButton::clicked, this, [=]() {
                QJsonObject object = readState();
                QJsonArray elements = object.value(QStringLiteral("elements")).toArray();
                if (elements.size() >= 16) return;
                QJsonObject element{{QStringLiteral("type"), 0}, {QStringLiteral("position"), 0.0}, {QStringLiteral("size"), 0.1}, {QStringLiteral("opacity"), 1.0}, {QStringLiteral("softness"), 0.35}, {QStringLiteral("aspect"), 1.0}, {QStringLiteral("rotation"), 0.0}};
                element.insert(QStringLiteral("color"), QJsonArray{1.0, 0.8, 0.55, 1.0});
                elements.append(element); object.insert(QStringLiteral("elements"), elements); writeState(object); rebuildList(elements.size() - 1);
            });
            connect(duplicateElement, &QPushButton::clicked, this, [=]() {
                QJsonObject object = readState(); QJsonArray elements = object.value(QStringLiteral("elements")).toArray(); const int row = elementsList->currentRow();
                if (row < 0 || row >= elements.size() || elements.size() >= 16) return;
                elements.insert(row + 1, elements.at(row)); object.insert(QStringLiteral("elements"), elements); writeState(object); rebuildList(row + 1);
            });
            connect(removeElement, &QPushButton::clicked, this, [=]() {
                QJsonObject object = readState(); QJsonArray elements = object.value(QStringLiteral("elements")).toArray(); const int row = elementsList->currentRow();
                if (row < 0 || row >= elements.size()) return;
                elements.removeAt(row);
                object.insert(QStringLiteral("elements"), elements);
                writeState(object);
                const int lastRow = static_cast<int>(elements.size()) - 1;
                rebuildList(std::min(row, lastRow));
            });
            auto moveElement = [=](int delta) {
                QJsonObject object = readState(); QJsonArray elements = object.value(QStringLiteral("elements")).toArray(); const int row = elementsList->currentRow(); const int target = row + delta;
                if (row < 0 || target < 0 || target >= elements.size()) return; const QJsonValue value = elements.at(row); elements.removeAt(row); elements.insert(target, value); object.insert(QStringLiteral("elements"), elements); writeState(object); rebuildList(target);
            };
            connect(moveUp, &QPushButton::clicked, this, [=]() { moveElement(-1); });
            connect(moveDown, &QPushButton::clicked, this, [=]() { moveElement(1); });
            rebuildList(0);
            loadElement(elementsList->currentRow());
        }
    } else if (selected_effect()->type == LayerEffectType::TrimPaths) {
        LayerEffect *effect = selected_effect();
        auto *start = spin(0.0, 100.0, 0.1);
        start->setDecimals(2);
        start->setSuffix(QStringLiteral(" %"));
        start->setValue(panel_eval_effect_property(effect->trim_start_prop, effect->effect_trim_start, lt));
        auto *end = spin(0.0, 100.0, 0.1);
        end->setDecimals(2);
        end->setSuffix(QStringLiteral(" %"));
        end->setValue(panel_eval_effect_property(effect->trim_end_prop, effect->effect_trim_end, lt));
        auto *trim_offset = spin(-1000000000.0, 1000000000.0, 1.0);
        trim_offset->setDecimals(2);
        trim_offset->setSuffix(QStringLiteral("°"));
        trim_offset->setValue(panel_eval_effect_property(effect->trim_offset_prop, effect->effect_trim_offset, lt));
        auto *multiple = combo();
        multiple->addItem(tr("Simultaneously"), 0);
        multiple->addItem(tr("Individually"), 1);
        multiple->setCurrentIndex(std::clamp(effect->effect_trim_multiple_shapes, 0, 1));

        bind_numeric(start, [](const LayerEffect &value, double t) {
            return panel_eval_effect_property(value.trim_start_prop, value.effect_trim_start, t);
        });
        bind_numeric(end, [](const LayerEffect &value, double t) {
            return panel_eval_effect_property(value.trim_end_prop, value.effect_trim_end, t);
        });
        bind_numeric(trim_offset, [](const LayerEffect &value, double t) {
            return panel_eval_effect_property(value.trim_offset_prop, value.effect_trim_offset, t);
        });

        add_effect_row(tr("Start"), wrap_scalar_keyframe(start, &LayerEffect::trim_start_prop));
        add_effect_row(tr("End"), wrap_scalar_keyframe(end, &LayerEffect::trim_end_prop));
        add_effect_row(tr("Trim Offset"), wrap_scalar_keyframe(trim_offset, &LayerEffect::trim_offset_prop));
        add_effect_row(tr("Trim Multiple Shapes"), multiple);

        connect(start, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_trim_start = static_cast<float>(value);
                set_animated_value(selected_effect()->trim_start_prop, current_local_time(), value);
                emit_effect_changed();
            }
        });
        connect(end, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_trim_end = static_cast<float>(value);
                set_animated_value(selected_effect()->trim_end_prop, current_local_time(), value);
                emit_effect_changed();
            }
        });
        connect(trim_offset, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_trim_offset = static_cast<float>(value);
                set_animated_value(selected_effect()->trim_offset_prop, current_local_time(), value);
                emit_effect_changed();
            }
        });
        connect(multiple, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, multiple](int) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_trim_multiple_shapes = multiple->currentData().toInt();
                emit_effect_changed();
            }
        });
    } else if (selected_effect()->type == LayerEffectType::BackgroundColor) {
        LayerEffect *effect = selected_effect();
        auto section_label = [box](const QString &text) {
            auto *label = new QLabel(text, box);
            QFont f = label->font();
            f.setBold(true);
            label->setFont(f);
            return label;
        };
        auto *fill = combo();
        fill->addItem(fxm_tr("OBSTitles.Solid"), 0);
        fill->addItem(fxm_tr("OBSTitles.Gradient"), 1);
        fill->setCurrentIndex(fill->findData(effect->effect_fill_type));
        auto *fill_color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){
            if (!selected_effect()) return;
            selected_effect()->effect_color = argb;
            set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
        });
        auto *opacity = spin(0.0, 1.0, 0.05);
        opacity->setDecimals(2);
        opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);

        auto *stroke_color = color_button(panel_eval_effect_stroke_color(*effect, lt), [this, lt](uint32_t argb){
            if (!selected_effect()) return;
            selected_effect()->effect_stroke_color = argb;
            set_effect_stroke_color_channels_at(*selected_effect(), current_local_time(), argb);
        });
        auto *stroke_width = spin(0.0, 1000.0, 1.0);
        stroke_width->setValue(effect->stroke_width_prop.is_animated() ? effect->stroke_width_prop.evaluate(lt) : effect->effect_stroke_width);
        auto *stroke_opacity = spin(0.0, 1.0, 0.05);
        stroke_opacity->setDecimals(2);
        stroke_opacity->setValue(effect->stroke_opacity_prop.is_animated() ? effect->stroke_opacity_prop.evaluate(lt) : effect->effect_stroke_opacity);

        auto *pad_left = spin(-1000.0, 1000.0, 1.0); pad_left->setValue(effect->padding_left_prop.is_animated() ? effect->padding_left_prop.evaluate(lt) : effect->effect_padding_left);
        auto *pad_right = spin(-1000.0, 1000.0, 1.0); pad_right->setValue(effect->padding_right_prop.is_animated() ? effect->padding_right_prop.evaluate(lt) : effect->effect_padding_right);
        auto *pad_top = spin(-1000.0, 1000.0, 1.0); pad_top->setValue(effect->padding_top_prop.is_animated() ? effect->padding_top_prop.evaluate(lt) : effect->effect_padding_top);
        auto *pad_bottom = spin(-1000.0, 1000.0, 1.0); pad_bottom->setValue(effect->padding_bottom_prop.is_animated() ? effect->padding_bottom_prop.evaluate(lt) : effect->effect_padding_bottom);

        auto *corner_tl = spin(0.0, 1000.0, 1.0); corner_tl->setValue(effect->corner_radius_tl_prop.is_animated() ? effect->corner_radius_tl_prop.evaluate(lt) : effect->effect_corner_radius_tl);
        auto *corner_tr = spin(0.0, 1000.0, 1.0); corner_tr->setValue(effect->corner_radius_tr_prop.is_animated() ? effect->corner_radius_tr_prop.evaluate(lt) : effect->effect_corner_radius_tr);
        auto *corner_br = spin(0.0, 1000.0, 1.0); corner_br->setValue(effect->corner_radius_br_prop.is_animated() ? effect->corner_radius_br_prop.evaluate(lt) : effect->effect_corner_radius_br);
        auto *corner_bl = spin(0.0, 1000.0, 1.0); corner_bl->setValue(effect->corner_radius_bl_prop.is_animated() ? effect->corner_radius_bl_prop.evaluate(lt) : effect->effect_corner_radius_bl);
        auto *corner_row = new QWidget(box);
        auto *corner_grid = new QGridLayout(corner_row);
        corner_grid->setContentsMargins(0, 0, 0, 0);
        corner_grid->setHorizontalSpacing(6);
        corner_grid->setVerticalSpacing(4);
        corner_grid->addWidget(new QLabel(fxm_tr("OBSTitles.TL"), corner_row), 0, 0);
        corner_grid->addWidget(wrap_scalar_keyframe(corner_tl, &LayerEffect::corner_radius_tl_prop), 0, 1);
        corner_grid->addWidget(new QLabel(fxm_tr("OBSTitles.TR"), corner_row), 0, 2);
        corner_grid->addWidget(wrap_scalar_keyframe(corner_tr, &LayerEffect::corner_radius_tr_prop), 0, 3);
        corner_grid->addWidget(new QLabel(fxm_tr("OBSTitles.BL"), corner_row), 1, 0);
        corner_grid->addWidget(wrap_scalar_keyframe(corner_bl, &LayerEffect::corner_radius_bl_prop), 1, 1);
        corner_grid->addWidget(new QLabel(fxm_tr("OBSTitles.BR"), corner_row), 1, 2);
        corner_grid->addWidget(wrap_scalar_keyframe(corner_br, &LayerEffect::corner_radius_br_prop), 1, 3);

        auto *grad_type = combo();
        grad_type->addItem(fxm_tr("OBSTitles.LinearGradient"), 0);
        grad_type->addItem(fxm_tr("OBSTitles.RadialGradient"), 1);
        grad_type->addItem(fxm_tr("OBSTitles.ConicalGradient"), 2);
        grad_type->setCurrentIndex(std::max(0, grad_type->findData(effect->effect_gradient_type)));
        auto *grad_spread = combo();
        grad_spread->addItem(fxm_tr("OBSTitles.No"), 0);
        grad_spread->addItem(fxm_tr("OBSTitles.Repeat"), 2);
        grad_spread->addItem(fxm_tr("OBSTitles.Reflect"), 1);
        grad_spread->setCurrentIndex(std::max(0, grad_spread->findData(effect->effect_gradient_spread)));
        auto *grad_start = color_button(panel_eval_gradient_start_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_gradient_start_color = argb; set_gradient_start_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *grad_end = color_button(panel_eval_gradient_end_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_gradient_end_color = argb; set_gradient_end_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *grad_start_pos = spin(0.0, 1.0, 0.01); grad_start_pos->setDecimals(3); grad_start_pos->setValue(panel_eval_effect_property(effect->gradient_start_pos_prop, effect->effect_gradient_start_pos, lt));
        auto *grad_end_pos = spin(0.0, 1.0, 0.01); grad_end_pos->setDecimals(3); grad_end_pos->setValue(panel_eval_effect_property(effect->gradient_end_pos_prop, effect->effect_gradient_end_pos, lt));
        auto *grad_start_opacity = spin(0.0, 1.0, 0.01); grad_start_opacity->setDecimals(3); grad_start_opacity->setValue(panel_eval_effect_property(effect->gradient_start_opacity_prop, effect->effect_gradient_start_opacity, lt));
        auto *grad_end_opacity = spin(0.0, 1.0, 0.01); grad_end_opacity->setDecimals(3); grad_end_opacity->setValue(panel_eval_effect_property(effect->gradient_end_opacity_prop, effect->effect_gradient_end_opacity, lt));
        auto *grad_opacity = spin(0.0, 1.0, 0.01); grad_opacity->setDecimals(3); grad_opacity->setValue(panel_eval_effect_property(effect->gradient_opacity_prop, effect->effect_gradient_opacity, lt));
        auto *grad_angle = spin(-1000000000.0, 1000000000.0, 1.0); grad_angle->setValue(panel_eval_effect_property(effect->gradient_angle_prop, effect->effect_gradient_angle, lt));
        auto *grad_center_x = spin(-100.0, 100.0, 0.01); grad_center_x->setDecimals(3); grad_center_x->setValue(panel_eval_effect_property(effect->gradient_center_x_prop, effect->effect_gradient_center_x, lt));
        auto *grad_center_y = spin(-100.0, 100.0, 0.01); grad_center_y->setDecimals(3); grad_center_y->setValue(panel_eval_effect_property(effect->gradient_center_y_prop, effect->effect_gradient_center_y, lt));
        auto *grad_scale = spin(0.01, 100.0, 0.01); grad_scale->setDecimals(3); grad_scale->setValue(panel_eval_effect_property(effect->gradient_scale_prop, effect->effect_gradient_scale, lt));
        auto *grad_focal_x = spin(-100.0, 100.0, 0.01); grad_focal_x->setDecimals(3); grad_focal_x->setValue(panel_eval_effect_property(effect->gradient_focal_x_prop, effect->effect_gradient_focal_x, lt));
        auto *grad_focal_y = spin(-100.0, 100.0, 0.01); grad_focal_y->setDecimals(3); grad_focal_y->setValue(panel_eval_effect_property(effect->gradient_focal_y_prop, effect->effect_gradient_focal_y, lt));

        bind_color(fill_color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_color(stroke_color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_stroke_color(effect, t);
        });
        bind_color(grad_start, [](const LayerEffect &effect, double t) {
            return panel_eval_gradient_start_color(effect, t);
        });
        bind_color(grad_end, [](const LayerEffect &effect, double t) {
            return panel_eval_gradient_end_color(effect, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(stroke_width, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.stroke_width_prop, effect.effect_stroke_width, t);
        });
        bind_numeric(stroke_opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.stroke_opacity_prop, effect.effect_stroke_opacity, t);
        });
        bind_numeric(pad_left, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.padding_left_prop, effect.effect_padding_left, t);
        });
        bind_numeric(pad_right, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.padding_right_prop, effect.effect_padding_right, t);
        });
        bind_numeric(pad_top, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.padding_top_prop, effect.effect_padding_top, t);
        });
        bind_numeric(pad_bottom, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.padding_bottom_prop, effect.effect_padding_bottom, t);
        });
        bind_numeric(corner_tl, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.corner_radius_tl_prop, effect.effect_corner_radius_tl, t);
        });
        bind_numeric(corner_tr, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.corner_radius_tr_prop, effect.effect_corner_radius_tr, t);
        });
        bind_numeric(corner_br, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.corner_radius_br_prop, effect.effect_corner_radius_br, t);
        });
        bind_numeric(corner_bl, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.corner_radius_bl_prop, effect.effect_corner_radius_bl, t);
        });
        bind_numeric(grad_start_pos, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_start_pos_prop, effect.effect_gradient_start_pos, t);
        });
        bind_numeric(grad_end_pos, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_end_pos_prop, effect.effect_gradient_end_pos, t);
        });
        bind_numeric(grad_start_opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_start_opacity_prop, effect.effect_gradient_start_opacity, t);
        });
        bind_numeric(grad_end_opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_end_opacity_prop, effect.effect_gradient_end_opacity, t);
        });
        bind_numeric(grad_opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_opacity_prop, effect.effect_gradient_opacity, t);
        });
        bind_numeric(grad_angle, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_angle_prop, effect.effect_gradient_angle, t);
        });
        bind_numeric(grad_center_x, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_center_x_prop, effect.effect_gradient_center_x, t);
        });
        bind_numeric(grad_center_y, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_center_y_prop, effect.effect_gradient_center_y, t);
        });
        bind_numeric(grad_scale, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_scale_prop, effect.effect_gradient_scale, t);
        });
        bind_numeric(grad_focal_x, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_focal_x_prop, effect.effect_gradient_focal_x, t);
        });
        bind_numeric(grad_focal_y, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.gradient_focal_y_prop, effect.effect_gradient_focal_y, t);
        });

        form->addRow(section_label(fxm_tr("OBSTitles.Appearance")));
        add_effect_row(fxm_tr("OBSTitles.Fill"), fill);
        add_effect_row(fxm_tr("OBSTitles.FillColor"), wrap_color_keyframe(fill_color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.StrokeColor"), wrap_color_keyframe(stroke_color, &LayerEffect::stroke_color_a, &LayerEffect::stroke_color_r, &LayerEffect::stroke_color_g, &LayerEffect::stroke_color_b));
        add_effect_row(fxm_tr("OBSTitles.StrokeWidth"), wrap_scalar_keyframe(stroke_width, &LayerEffect::stroke_width_prop));
        add_effect_row(fxm_tr("OBSTitles.StrokeOpacity"), wrap_scalar_keyframe(stroke_opacity, &LayerEffect::stroke_opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.GradientTypeLabel"), grad_type);
        add_effect_row(fxm_tr("OBSTitles.SpreadLabel"), grad_spread);
        add_effect_row(fxm_tr("OBSTitles.StartColorLabel"), wrap_color_keyframe(grad_start, &LayerEffect::gradient_start_color_a, &LayerEffect::gradient_start_color_r, &LayerEffect::gradient_start_color_g, &LayerEffect::gradient_start_color_b));
        add_effect_row(fxm_tr("OBSTitles.EndColorLabel"), wrap_color_keyframe(grad_end, &LayerEffect::gradient_end_color_a, &LayerEffect::gradient_end_color_r, &LayerEffect::gradient_end_color_g, &LayerEffect::gradient_end_color_b));
        add_effect_row(fxm_tr("OBSTitles.StartStopLabel"), wrap_scalar_keyframe(grad_start_pos, &LayerEffect::gradient_start_pos_prop));
        add_effect_row(fxm_tr("OBSTitles.EndStopLabel"), wrap_scalar_keyframe(grad_end_pos, &LayerEffect::gradient_end_pos_prop));
        add_effect_row(fxm_tr("OBSTitles.StartOpacityLabel"), wrap_scalar_keyframe(grad_start_opacity, &LayerEffect::gradient_start_opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.EndOpacityLabel"), wrap_scalar_keyframe(grad_end_opacity, &LayerEffect::gradient_end_opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.GradientOpacityLabel"), wrap_scalar_keyframe(grad_opacity, &LayerEffect::gradient_opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(grad_angle, &LayerEffect::gradient_angle_prop));
        add_effect_row(fxm_tr("OBSTitles.CenterXLabel"), wrap_scalar_keyframe(grad_center_x, &LayerEffect::gradient_center_x_prop));
        add_effect_row(fxm_tr("OBSTitles.CenterYLabel"), wrap_scalar_keyframe(grad_center_y, &LayerEffect::gradient_center_y_prop));
        add_effect_row(fxm_tr("OBSTitles.ScaleLabel"), wrap_scalar_keyframe(grad_scale, &LayerEffect::gradient_scale_prop));
        add_effect_row(fxm_tr("OBSTitles.FocalXLabel"), wrap_scalar_keyframe(grad_focal_x, &LayerEffect::gradient_focal_x_prop));
        add_effect_row(fxm_tr("OBSTitles.FocalYLabel"), wrap_scalar_keyframe(grad_focal_y, &LayerEffect::gradient_focal_y_prop));
        form->addRow(section_label(fxm_tr("OBSTitles.Padding")));
        add_effect_row(fxm_tr("OBSTitles.LeftPadding"), wrap_scalar_keyframe(pad_left, &LayerEffect::padding_left_prop));
        add_effect_row(fxm_tr("OBSTitles.RightPadding"), wrap_scalar_keyframe(pad_right, &LayerEffect::padding_right_prop));
        add_effect_row(fxm_tr("OBSTitles.TopPadding"), wrap_scalar_keyframe(pad_top, &LayerEffect::padding_top_prop));
        add_effect_row(fxm_tr("OBSTitles.BottomPadding"), wrap_scalar_keyframe(pad_bottom, &LayerEffect::padding_bottom_prop));
        form->addRow(section_label(fxm_tr("OBSTitles.Corners")));
        add_effect_row(fxm_tr("OBSTitles.CornerInitials"), corner_row);

        connect(fill, QOverload<int>::of(&QComboBox::activated), this, [this, fill](int){ if (selected_effect()) { selected_effect()->effect_fill_type = fill->currentData().toInt(); emit_effect_changed(); }});
        connect(grad_type, QOverload<int>::of(&QComboBox::activated), this, [this, grad_type](int){ if (selected_effect()) { selected_effect()->effect_gradient_type = grad_type->currentData().toInt(); emit_effect_changed(); }});
        connect(grad_spread, QOverload<int>::of(&QComboBox::activated), this, [this, grad_spread](int){ if (selected_effect()) { selected_effect()->effect_gradient_spread = grad_spread->currentData().toInt(); emit_effect_changed(); }});
        connect(grad_start_pos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_start_pos = (float)v; set_animated_value(selected_effect()->gradient_start_pos_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_end_pos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_end_pos = (float)v; set_animated_value(selected_effect()->gradient_end_pos_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_start_opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_start_opacity = (float)v; set_animated_value(selected_effect()->gradient_start_opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_end_opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_end_opacity = (float)v; set_animated_value(selected_effect()->gradient_end_opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_opacity = (float)v; set_animated_value(selected_effect()->gradient_opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_angle = (float)v; set_animated_value(selected_effect()->gradient_angle_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_center_x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_center_x = (float)v; set_animated_value(selected_effect()->gradient_center_x_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_center_y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_center_y = (float)v; set_animated_value(selected_effect()->gradient_center_y_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_scale = (float)v; set_animated_value(selected_effect()->gradient_scale_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_focal_x, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_focal_x = (float)v; set_animated_value(selected_effect()->gradient_focal_x_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(grad_focal_y, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_gradient_focal_y = (float)v; set_animated_value(selected_effect()->gradient_focal_y_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(stroke_width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_stroke_width = v; set_animated_value(selected_effect()->stroke_width_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(stroke_opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_stroke_opacity = v; set_animated_value(selected_effect()->stroke_opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(pad_left, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_padding_left = v; set_animated_value(selected_effect()->padding_left_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(pad_right, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_padding_right = v; set_animated_value(selected_effect()->padding_right_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(pad_top, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_padding_top = v; set_animated_value(selected_effect()->padding_top_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(pad_bottom, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_padding_bottom = v; set_animated_value(selected_effect()->padding_bottom_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(corner_tl, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_corner_radius_tl = v; set_animated_value(selected_effect()->corner_radius_tl_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(corner_tr, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_corner_radius_tr = v; set_animated_value(selected_effect()->corner_radius_tr_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(corner_br, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_corner_radius_br = v; set_animated_value(selected_effect()->corner_radius_br_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(corner_bl, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_corner_radius_bl = v; set_animated_value(selected_effect()->corner_radius_bl_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Outline) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){
            if (!selected_effect()) return;
            selected_effect()->effect_color = argb;
            set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
        });
        auto *width = spin(0.0, 200.0, 1.0); width->setValue(effect->size_prop.is_animated() ? effect->size_prop.evaluate(lt) : effect->effect_size);
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);
        bind_color(color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_numeric(width, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.ThicknessLabel"), wrap_scalar_keyframe(width, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        connect(width, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::DropShadow) {
        LayerEffect *effect = selected_effect();
        auto *preset = combo(); preset->addItems({fxm_tr("OBSTitles.Custom"), fxm_tr("OBSTitles.Soft"), fxm_tr("OBSTitles.Medium"), fxm_tr("OBSTitles.Strong"), fxm_tr("OBSTitles.Broadcast")});
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);
        auto *dist = spin(0.0, 4096.0, 1.0); dist->setValue(effect->distance_prop.is_animated() ? effect->distance_prop.evaluate(lt) : effect->effect_distance);
        auto *angle = spin(-1000000000.0, 1000000000.0, 5.0); angle->setValue(effect->angle_prop.is_animated() ? effect->angle_prop.evaluate(lt) : effect->effect_angle);
        auto *blur = spin(0.0, 512.0, 1.0); blur->setValue(effect->size_prop.is_animated() ? effect->size_prop.evaluate(lt) : effect->effect_size);
        auto *spread = spin(0.0, 512.0, 1.0); spread->setValue(effect->spread_prop.is_animated() ? effect->spread_prop.evaluate(lt) : effect->effect_spread);
        bind_color(color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(dist, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.distance_prop, effect.effect_distance, t);
        });
        bind_numeric(angle, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.angle_prop, effect.effect_angle, t);
        });
        bind_numeric(blur, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        bind_numeric(spread, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.spread_prop, effect.effect_spread, t);
        });
        auto *angle_field = fxm_make_angle_field(angle, box);
        add_effect_row(fxm_tr("OBSTitles.PresetLabel"), preset);
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.DistanceLabel"), wrap_scalar_keyframe(dist, &LayerEffect::distance_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(angle_field, &LayerEffect::angle_prop, [angle]() { return angle->value(); }));
        add_effect_row(fxm_tr("OBSTitles.BlurLabel"), wrap_scalar_keyframe(blur, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.SpreadLabel"), wrap_scalar_keyframe(spread, &LayerEffect::spread_prop));
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(dist, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; set_animated_value(selected_effect()->distance_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; set_animated_value(selected_effect()->angle_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(blur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(spread, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_spread = (float)v; set_animated_value(selected_effect()->spread_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(preset, QOverload<int>::of(&QComboBox::activated), this, [this, preset]() {
            auto *effect = selected_effect();
            if (!effect) return;
            switch (preset->currentIndex()) {
            case 1: effect->effect_opacity = 0.35f; effect->effect_distance = 4.0f; effect->effect_size = 10.0f; effect->effect_spread = 0.0f; break;
            case 2: effect->effect_opacity = 0.55f; effect->effect_distance = 8.0f; effect->effect_size = 8.0f; effect->effect_spread = 0.0f; break;
            case 3: effect->effect_opacity = 0.75f; effect->effect_distance = 12.0f; effect->effect_size = 6.0f; effect->effect_spread = 0.0f; break;
            case 4: effect->effect_opacity = 0.6f; effect->effect_distance = 6.0f; effect->effect_size = 3.0f; effect->effect_spread = 2.0f; break;
            default: return;
            }
            const double t = layer_ ? std::clamp(playhead_ - layer_->in_time, 0.0, std::max(0.0, layer_->out_time - layer_->in_time)) : 0.0;
            set_animated_value(effect->opacity_prop, t, effect->effect_opacity);
            set_animated_value(effect->distance_prop, t, effect->effect_distance);
            set_animated_value(effect->size_prop, t, effect->effect_size);
            set_animated_value(effect->spread_prop, t, effect->effect_spread);
            emit_effect_changed();
            load_settings();
        });
    } else if (selected_effect()->type == LayerEffectType::BrightnessContrast) {
        LayerEffect *effect = selected_effect();
        auto *brightness = spin(-1.0, 1.0, 0.01); brightness->setDecimals(2); brightness->setValue(panel_eval_effect_property(effect->brightness_prop, effect->brightness, lt));
        auto *contrast = spin(0.0, 4.0, 0.05); contrast->setDecimals(2); contrast->setValue(panel_eval_effect_property(effect->contrast_prop, effect->contrast, lt));
        bind_numeric(brightness, [](const LayerEffect &effect, double t) { return panel_eval_effect_property(effect.brightness_prop, effect.brightness, t); });
        bind_numeric(contrast, [](const LayerEffect &effect, double t) { return panel_eval_effect_property(effect.contrast_prop, effect.contrast, t); });
        add_effect_row(fxm_tr("OBSTitles.BrightnessLabel"), wrap_scalar_keyframe(brightness, &LayerEffect::brightness_prop));
        add_effect_row(fxm_tr("OBSTitles.ContrastLabel"), wrap_scalar_keyframe(contrast, &LayerEffect::contrast_prop));
        connect(brightness, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->brightness = (float)v; set_animated_value(selected_effect()->brightness_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(contrast, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->contrast = (float)v; set_animated_value(selected_effect()->contrast_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Saturation) {
        LayerEffect *effect = selected_effect();
        auto *saturation = spin(0.0, 4.0, 0.05); saturation->setDecimals(2); saturation->setValue(panel_eval_effect_property(effect->saturation_prop, effect->saturation, lt));
        bind_numeric(saturation, [](const LayerEffect &effect, double t) { return panel_eval_effect_property(effect.saturation_prop, effect.saturation, t); });
        add_effect_row(fxm_tr("OBSTitles.SaturationLabel"), wrap_scalar_keyframe(saturation, &LayerEffect::saturation_prop));
        connect(saturation, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->saturation = (float)v; set_animated_value(selected_effect()->saturation_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::ColorOverlay) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; selected_effect()->tint_color = argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *blend = combo(); add_blend_mode_items(blend); blend->setCurrentIndex(blend->findData((int)effect->blend_mode));
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        add_effect_row(fxm_tr("OBSTitles.ColorOverlayColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.BlendingModeLabel"), blend);
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; selected_effect()->tint_amount = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(blend, QOverload<int>::of(&QComboBox::activated), this, [this, blend](int){ if (!loading_values_ && selected_effect()) { selected_effect()->blend_mode = (EffectBlendMode)blend->currentData().toInt(); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Glow || selected_effect()->type == LayerEffectType::InnerGlow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(size, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(size, &LayerEffect::size_prop));
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Blur) {
        LayerEffect *effect = selected_effect();
        auto *amount = spin(0.0, 1.0, 0.05); amount->setDecimals(2); amount->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        bind_numeric(amount, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(size, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        add_effect_row(fxm_tr("OBSTitles.AmountLabel"), wrap_scalar_keyframe(amount, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(size, &LayerEffect::size_prop));
        connect(amount, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::MotionBlur) {
        LayerEffect *effect = selected_effect();
        auto *amount = spin(0.0, 1.0, 0.05); amount->setDecimals(2); amount->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *shutter = spin(0.0, 720.0, 5.0); shutter->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        auto *samples = new QSpinBox(box); samples->setRange(2, 64); samples->setValue(effect->effect_samples); samples->setFixedHeight(22);
        auto *centered = new FxmSwitch(fxm_tr("OBSTitles.MotionBlurCentered"), box); centered->setChecked(effect->effect_centered);
        bind_numeric(amount, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(shutter, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        add_effect_row(fxm_tr("OBSTitles.AmountLabel"), wrap_scalar_keyframe(amount, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.ShutterAngleLabel"), wrap_scalar_keyframe(shutter, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.SamplesLabel"), samples);
        add_effect_row(QString(), centered);
        connect(amount, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(shutter, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(samples, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_samples = v; emit_effect_changed(); }});
        connect(centered, &QCheckBox::toggled, this, [this](bool v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_centered = v; emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Halation) {
        LayerEffect *effect = selected_effect();
        auto *warm = color_button(panel_eval_effect_color(*effect, lt), [this](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_color = argb;
            set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
            emit_effect_changed();
        });
        auto *edge = color_button(panel_eval_effect_secondary_color(*effect, lt), [this](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_secondary_color = argb;
            set_effect_secondary_color_channels_at(*selected_effect(), current_local_time(), argb);
            emit_effect_changed();
        });
        auto *opacity = spin(0.0, 1.0, 0.01);
        auto *amount = spin(0.0, 8.0, 0.05);
        auto *threshold = spin(0.0, 1.0, 0.01);
        auto *radius = spin(0.0, 512.0, 1.0);
        auto *intensity = spin(0.0, 8.0, 0.05);
        auto *diffusion = spin(0.0, 1.0, 0.01);
        opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        threshold->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
        radius->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        intensity->setValue(panel_eval_effect_property(effect->falloff_prop, effect->effect_falloff, lt));
        diffusion->setValue(panel_eval_effect_property(effect->softness_prop, effect->effect_softness, lt));
        bind_color(warm, [](const LayerEffect &e, double t) { return panel_eval_effect_color(e, t); });
        bind_color(edge, [](const LayerEffect &e, double t) { return panel_eval_effect_secondary_color(e, t); });
        const auto bind_current = [&](QDoubleSpinBox *widget, const AnimatedProperty LayerEffect::*property, const float LayerEffect::*fallback) {
            bind_numeric(widget, [property, fallback](const LayerEffect &e, double t) {
                return panel_eval_effect_property(e.*property, e.*fallback, t);
            });
        };
        bind_current(opacity, &LayerEffect::opacity_prop, &LayerEffect::effect_opacity);
        bind_current(amount, &LayerEffect::amount_prop, &LayerEffect::effect_amount);
        bind_current(threshold, &LayerEffect::spread_prop, &LayerEffect::effect_spread);
        bind_current(radius, &LayerEffect::size_prop, &LayerEffect::effect_size);
        bind_current(intensity, &LayerEffect::falloff_prop, &LayerEffect::effect_falloff);
        bind_current(diffusion, &LayerEffect::softness_prop, &LayerEffect::effect_softness);
        add_effect_row(QStringLiteral("Warm Core"), wrap_color_keyframe(warm, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(QStringLiteral("Outer Spectral Color"), wrap_color_keyframe(edge, &LayerEffect::secondary_color_a, &LayerEffect::secondary_color_r, &LayerEffect::secondary_color_g, &LayerEffect::secondary_color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.Amount"), wrap_scalar_keyframe(amount, &LayerEffect::amount_prop));
        add_effect_row(fxm_tr("OBSTitles.ThresholdLabel"), wrap_scalar_keyframe(threshold, &LayerEffect::spread_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(radius, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.IntensityLabel"), wrap_scalar_keyframe(intensity, &LayerEffect::falloff_prop));
        add_effect_row(QStringLiteral("Diffusion"), wrap_scalar_keyframe(diffusion, &LayerEffect::softness_prop));
        const auto bind_value = [this](QDoubleSpinBox *widget, float LayerEffect::*fallback, AnimatedProperty LayerEffect::*property) {
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property, current_local_time(), value);
                    emit_effect_changed();
                }
            });
        };
        bind_value(opacity, &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
        bind_value(amount, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
        bind_value(threshold, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
        bind_value(radius, &LayerEffect::effect_size, &LayerEffect::size_prop);
        bind_value(intensity, &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
        bind_value(diffusion, &LayerEffect::effect_softness, &LayerEffect::softness_prop);
    } else if (selected_effect()->type == LayerEffectType::Bloom) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); emit_effect_changed(); } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);
        auto *threshold = spin(0.0, 1.0, 0.01); threshold->setDecimals(2); threshold->setValue(effect->spread_prop.is_animated() ? effect->spread_prop.evaluate(lt) : effect->effect_spread);
        auto *radius = spin(0.0, 512.0, 1.0); radius->setValue(effect->size_prop.is_animated() ? effect->size_prop.evaluate(lt) : effect->effect_size);
        auto *intensity = spin(0.0, 8.0, 0.1); intensity->setDecimals(2); intensity->setValue(effect->falloff_prop.is_animated() ? effect->falloff_prop.evaluate(lt) : effect->effect_falloff);
        bind_color(color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(threshold, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.spread_prop, effect.effect_spread, t);
        });
        bind_numeric(radius, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        bind_numeric(intensity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.falloff_prop, effect.effect_falloff, t);
        });
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.ThresholdLabel"), wrap_scalar_keyframe(threshold, &LayerEffect::spread_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(radius, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.IntensityLabel"), wrap_scalar_keyframe(intensity, &LayerEffect::falloff_prop));
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity=(float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(threshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_spread=(float)v; set_animated_value(selected_effect()->spread_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(radius, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size=(float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(intensity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_falloff=(float)v; set_animated_value(selected_effect()->falloff_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::Glare) {
        LayerEffect *effect = selected_effect();
        auto *primary = color_button(panel_eval_effect_color(*effect, lt), [this](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_color = argb;
            set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
            emit_effect_changed();
        });
        auto *secondary = color_button(panel_eval_effect_secondary_color(*effect, lt), [this](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_secondary_color = argb;
            set_effect_secondary_color_channels_at(*selected_effect(), current_local_time(), argb);
            emit_effect_changed();
        });
        auto *opacity = spin(0.0, 1.0, 0.01);
        auto *amount = spin(0.0, 8.0, 0.05);
        auto *threshold = spin(0.0, 1.0, 0.01);
        auto *radius = spin(0.0, 512.0, 1.0);
        auto *length = spin(0.0, 4096.0, 1.0);
        auto *angle = spin(-1000000000.0, 1000000000.0, 1.0);
        auto *intensity = spin(0.0, 8.0, 0.05);
        auto *dispersion = spin(0.0, 1.0, 0.01);
        opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        threshold->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
        radius->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        length->setValue(panel_eval_effect_property(effect->distance_prop, effect->effect_distance, lt));
        angle->setValue(panel_eval_effect_property(effect->angle_prop, effect->effect_angle, lt));
        intensity->setValue(panel_eval_effect_property(effect->falloff_prop, effect->effect_falloff, lt));
        dispersion->setValue(panel_eval_effect_property(effect->softness_prop, effect->effect_softness, lt));
        bind_color(primary, [](const LayerEffect &e, double t) { return panel_eval_effect_color(e, t); });
        bind_color(secondary, [](const LayerEffect &e, double t) { return panel_eval_effect_secondary_color(e, t); });
        const auto bind_current = [&](QDoubleSpinBox *widget, const AnimatedProperty LayerEffect::*property, const float LayerEffect::*fallback) {
            bind_numeric(widget, [property, fallback](const LayerEffect &e, double t) {
                return panel_eval_effect_property(e.*property, e.*fallback, t);
            });
        };
        bind_current(opacity, &LayerEffect::opacity_prop, &LayerEffect::effect_opacity);
        bind_current(amount, &LayerEffect::amount_prop, &LayerEffect::effect_amount);
        bind_current(threshold, &LayerEffect::spread_prop, &LayerEffect::effect_spread);
        bind_current(radius, &LayerEffect::size_prop, &LayerEffect::effect_size);
        bind_current(length, &LayerEffect::distance_prop, &LayerEffect::effect_distance);
        bind_current(angle, &LayerEffect::angle_prop, &LayerEffect::effect_angle);
        bind_current(intensity, &LayerEffect::falloff_prop, &LayerEffect::effect_falloff);
        bind_current(dispersion, &LayerEffect::softness_prop, &LayerEffect::effect_softness);
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(primary, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.SecondaryColor"), wrap_color_keyframe(secondary, &LayerEffect::secondary_color_a, &LayerEffect::secondary_color_r, &LayerEffect::secondary_color_g, &LayerEffect::secondary_color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.Amount"), wrap_scalar_keyframe(amount, &LayerEffect::amount_prop));
        add_effect_row(fxm_tr("OBSTitles.ThresholdLabel"), wrap_scalar_keyframe(threshold, &LayerEffect::spread_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(radius, &LayerEffect::size_prop));
        add_effect_row(QStringLiteral("Streak Length"), wrap_scalar_keyframe(length, &LayerEffect::distance_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(angle, &LayerEffect::angle_prop));
        add_effect_row(fxm_tr("OBSTitles.IntensityLabel"), wrap_scalar_keyframe(intensity, &LayerEffect::falloff_prop));
        add_effect_row(QStringLiteral("Chromatic Dispersion"), wrap_scalar_keyframe(dispersion, &LayerEffect::softness_prop));
        const auto bind_value = [this](QDoubleSpinBox *widget, float LayerEffect::*fallback, AnimatedProperty LayerEffect::*property) {
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property, current_local_time(), value);
                    emit_effect_changed();
                }
            });
        };
        bind_value(opacity, &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
        bind_value(amount, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
        bind_value(threshold, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
        bind_value(radius, &LayerEffect::effect_size, &LayerEffect::size_prop);
        bind_value(length, &LayerEffect::effect_distance, &LayerEffect::distance_prop);
        bind_value(angle, &LayerEffect::effect_angle, &LayerEffect::angle_prop);
        bind_value(intensity, &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
        bind_value(dispersion, &LayerEffect::effect_softness, &LayerEffect::softness_prop);
    } else if (selected_effect()->type == LayerEffectType::LensFlare) {
        LayerEffect *effect = selected_effect();
        auto *profile = combo();
        profile->addItem(QStringLiteral("Classic 35mm"), 0);
        profile->addItem(QStringLiteral("Anamorphic Blue"), 1);
        profile->addItem(QStringLiteral("Cinematic Warm"), 2);
        profile->addItem(QStringLiteral("Modern Sci-Fi"), 3);
        profile->addItem(QStringLiteral("Subtle Natural"), 4);
        profile->setCurrentIndex(profile->findData(effect->effect_profile));
        auto *primary = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_color = argb;
            set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
        });
        auto *secondary = color_button(panel_eval_effect_secondary_color(*effect, lt), [this, lt](uint32_t argb) {
            if (!selected_effect()) return;
            selected_effect()->effect_secondary_color = argb;
            set_effect_secondary_color_channels_at(*selected_effect(), current_local_time(), argb);
        });
        auto *amount = spin(0.0, 10.0, 0.05); amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *scale = spin(0.01, 20.0, 0.05); scale->setValue(panel_eval_effect_property(effect->scale_prop, effect->effect_scale, lt));
        auto *radius = spin(0.001, 4.0, 0.01); radius->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        auto *spread = spin(0.0, 4.0, 0.05); spread->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
        auto *falloff = spin(0.01, 16.0, 0.1); falloff->setValue(panel_eval_effect_property(effect->falloff_prop, effect->effect_falloff, lt));
        auto *angle = spin(-1000000000.0, 1000000000.0, 1.0); angle->setValue(panel_eval_effect_property(effect->angle_prop, effect->effect_angle, lt));
        auto *center_x = spin(-4.0, 4.0, 0.01); center_x->setValue(panel_eval_effect_property(effect->center_x_prop, effect->effect_center_x, lt));
        auto *center_y = spin(-4.0, 4.0, 0.01); center_y->setValue(panel_eval_effect_property(effect->center_y_prop, effect->effect_center_y, lt));
        auto *ghosts = spin(2.0, 12.0, 1.0);
        ghosts->setDecimals(0);
        ghosts->setValue(panel_eval_effect_property(
            effect->complexity_prop, effect->effect_complexity, lt));
        bind_color(primary, [](const LayerEffect &e, double t) { return panel_eval_effect_color(e, t); });
        bind_color(secondary, [](const LayerEffect &e, double t) { return panel_eval_effect_secondary_color(e, t); });
        bind_numeric(amount, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.amount_prop, e.effect_amount, t); });
        bind_numeric(opacity, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.opacity_prop, e.effect_opacity, t); });
        bind_numeric(scale, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.scale_prop, e.effect_scale, t); });
        bind_numeric(radius, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.size_prop, e.effect_size, t); });
        bind_numeric(spread, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.spread_prop, e.effect_spread, t); });
        bind_numeric(falloff, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.falloff_prop, e.effect_falloff, t); });
        bind_numeric(angle, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.angle_prop, e.effect_angle, t); });
        bind_numeric(center_x, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.center_x_prop, e.effect_center_x, t); });
        bind_numeric(center_y, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.center_y_prop, e.effect_center_y, t); });
        bind_numeric(ghosts, [](const LayerEffect &e, double t){ return panel_eval_effect_property(e.complexity_prop, e.effect_complexity, t); });
        add_effect_row(fxm_tr("OBSTitles.EffectProfile"), profile);
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(primary, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.SecondaryColor"), wrap_color_keyframe(secondary, &LayerEffect::secondary_color_a, &LayerEffect::secondary_color_r, &LayerEffect::secondary_color_g, &LayerEffect::secondary_color_b));
        add_effect_row(fxm_tr("OBSTitles.Amount"), wrap_scalar_keyframe(amount, &LayerEffect::amount_prop));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.Scale"), wrap_scalar_keyframe(scale, &LayerEffect::scale_prop));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(radius, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.SpreadLabel"), wrap_scalar_keyframe(spread, &LayerEffect::spread_prop));
        add_effect_row(fxm_tr("OBSTitles.FalloffLabel"), wrap_scalar_keyframe(falloff, &LayerEffect::falloff_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(angle, &LayerEffect::angle_prop));
        add_effect_row(fxm_tr("OBSTitles.CenterX"), wrap_scalar_keyframe(center_x, &LayerEffect::center_x_prop));
        add_effect_row(fxm_tr("OBSTitles.CenterY"), wrap_scalar_keyframe(center_y, &LayerEffect::center_y_prop));
        add_effect_row(fxm_tr("OBSTitles.Ghosts"), wrap_scalar_keyframe(ghosts, &LayerEffect::complexity_prop));
        connect(profile, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, profile](int){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_profile=profile->currentData().toInt(); emit_effect_changed(); }});
        connect(ghosts, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, lt](double v) {
                    if (!loading_values_ && selected_effect()) {
                        selected_effect()->effect_complexity = (float)v;
                        selected_effect()->effect_samples = (int)std::round(v);
                        set_animated_value(selected_effect()->complexity_prop, current_local_time(), v);
                        emit_effect_changed();
                    }
                });
        const auto bind_value = [this](QDoubleSpinBox *w, float LayerEffect::*field, AnimatedProperty LayerEffect::*prop) {
            connect(w, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, field, prop](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->*field=(float)v; set_animated_value(selected_effect()->*prop, current_local_time(), v); emit_effect_changed(); }});
        };
        bind_value(amount, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
        bind_value(opacity, &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
        bind_value(scale, &LayerEffect::effect_scale, &LayerEffect::scale_prop);
        bind_value(radius, &LayerEffect::effect_size, &LayerEffect::size_prop);
        bind_value(spread, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
        bind_value(falloff, &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
        bind_value(angle, &LayerEffect::effect_angle, &LayerEffect::angle_prop);
        bind_value(center_x, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
        bind_value(center_y, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
    } else if (selected_effect()->type == LayerEffectType::Vignette) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color=argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); }});
        auto *amount = spin(0.0, 2.0, 0.02); amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        auto *scale = spin(0.01, 4.0, 0.02); scale->setValue(panel_eval_effect_property(effect->scale_prop, effect->effect_scale, lt));
        auto *soft = spin(0.0, 1.0, 0.01); soft->setValue(panel_eval_effect_property(effect->softness_prop, effect->effect_softness, lt));
        auto *round = spin(-1.0, 1.0, 0.02); round->setValue(panel_eval_effect_property(effect->roundness_prop, effect->effect_roundness, lt));
        auto *cx = spin(-4.0, 4.0, 0.01); cx->setValue(panel_eval_effect_property(effect->center_x_prop, effect->effect_center_x, lt));
        auto *cy = spin(-4.0, 4.0, 0.01); cy->setValue(panel_eval_effect_property(effect->center_y_prop, effect->effect_center_y, lt));
        auto *invert = new FxmSwitch(fxm_tr("OBSTitles.Invert"), box); invert->setChecked(effect->effect_invert);
        bind_color(color, [](const LayerEffect &e,double t){ return panel_eval_effect_color(e,t); });
        const auto init_bind=[&](QDoubleSpinBox *w, const AnimatedProperty LayerEffect::*prop, const float LayerEffect::*field){ bind_numeric(w,[prop,field](const LayerEffect&e,double t){return panel_eval_effect_property(e.*prop,e.*field,t);});};
        init_bind(amount,&LayerEffect::amount_prop,&LayerEffect::effect_amount); init_bind(scale,&LayerEffect::scale_prop,&LayerEffect::effect_scale); init_bind(soft,&LayerEffect::softness_prop,&LayerEffect::effect_softness); init_bind(round,&LayerEffect::roundness_prop,&LayerEffect::effect_roundness); init_bind(cx,&LayerEffect::center_x_prop,&LayerEffect::effect_center_x); init_bind(cy,&LayerEffect::center_y_prop,&LayerEffect::effect_center_y);
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b)); add_effect_row(fxm_tr("OBSTitles.Amount"), wrap_scalar_keyframe(amount, &LayerEffect::amount_prop)); add_effect_row(fxm_tr("OBSTitles.Scale"), wrap_scalar_keyframe(scale, &LayerEffect::scale_prop)); add_effect_row(fxm_tr("OBSTitles.SoftnessLabel"), wrap_scalar_keyframe(soft, &LayerEffect::softness_prop)); add_effect_row(fxm_tr("OBSTitles.Roundness"), wrap_scalar_keyframe(round, &LayerEffect::roundness_prop)); add_effect_row(fxm_tr("OBSTitles.CenterX"), wrap_scalar_keyframe(cx, &LayerEffect::center_x_prop)); add_effect_row(fxm_tr("OBSTitles.CenterY"), wrap_scalar_keyframe(cy, &LayerEffect::center_y_prop)); add_effect_row(QString(),invert);
        const auto bind_value=[this](QDoubleSpinBox*w,float LayerEffect::*f,AnimatedProperty LayerEffect::*p){connect(w,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,f,p](double v){if(!loading_values_&&selected_effect()){selected_effect()->*f=(float)v;set_animated_value(selected_effect()->*p,current_local_time(),v);emit_effect_changed();}});};
        bind_value(amount,&LayerEffect::effect_amount,&LayerEffect::amount_prop); bind_value(scale,&LayerEffect::effect_scale,&LayerEffect::scale_prop); bind_value(soft,&LayerEffect::effect_softness,&LayerEffect::softness_prop); bind_value(round,&LayerEffect::effect_roundness,&LayerEffect::roundness_prop); bind_value(cx,&LayerEffect::effect_center_x,&LayerEffect::center_x_prop); bind_value(cy,&LayerEffect::effect_center_y,&LayerEffect::center_y_prop);
        connect(invert,&QCheckBox::toggled,this,[this](bool v){if(!loading_values_&&selected_effect()){selected_effect()->effect_invert=v;emit_effect_changed();}});
    } else if (selected_effect()->type == LayerEffectType::Sharpen ||
               selected_effect()->type == LayerEffectType::UnsharpMask ||
               selected_effect()->type == LayerEffectType::HighPass ||
               selected_effect()->type == LayerEffectType::Clarity ||
               selected_effect()->type == LayerEffectType::BilateralSharpen) {
        LayerEffect *effect = selected_effect();
        const LayerEffectType type = effect->type;
        auto *amount = spin(0.0, 4.0, 0.01);
        auto *radius = spin(0.25, 64.0, 0.05);
        auto *threshold = spin(0.0, 1.0, 0.005);
        amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        radius->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        threshold->setValue(panel_eval_effect_property(effect->softness_prop, effect->effect_softness, lt));
        bind_numeric(amount, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.amount_prop,e.effect_amount,t);});
        bind_numeric(radius, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.size_prop,e.effect_size,t);});
        bind_numeric(threshold, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.softness_prop,e.effect_softness,t);});
        add_effect_row(QStringLiteral("Amount"), wrap_scalar_keyframe(amount, &LayerEffect::amount_prop));
        add_effect_row(QStringLiteral("Radius"), wrap_scalar_keyframe(radius, &LayerEffect::size_prop));
        if (type != LayerEffectType::HighPass)
            add_effect_row(type == LayerEffectType::BilateralSharpen ? QStringLiteral("Range Threshold") : QStringLiteral("Threshold"),
                           wrap_scalar_keyframe(threshold, &LayerEffect::softness_prop));

        QDoubleSpinBox *spread = nullptr;
        QDoubleSpinBox *falloff = nullptr;
        QDoubleSpinBox *midtone = nullptr;
        if (type == LayerEffectType::UnsharpMask || type == LayerEffectType::Clarity) {
            spread = spin(0.0, 1.0, 0.01);
            falloff = spin(0.0, 1.0, 0.01);
            spread->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
            falloff->setValue(panel_eval_effect_property(effect->falloff_prop, effect->effect_falloff, lt));
            bind_numeric(spread, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.spread_prop,e.effect_spread,t);});
            bind_numeric(falloff, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.falloff_prop,e.effect_falloff,t);});
            add_effect_row(QStringLiteral("Highlight Protection"), wrap_scalar_keyframe(spread, &LayerEffect::spread_prop));
            add_effect_row(QStringLiteral("Shadow Protection"), wrap_scalar_keyframe(falloff, &LayerEffect::falloff_prop));
        } else if (type == LayerEffectType::BilateralSharpen) {
            spread = spin(0.0, 1.0, 0.01);
            spread->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
            bind_numeric(spread, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.spread_prop,e.effect_spread,t);});
            add_effect_row(QStringLiteral("Edge Protection"), wrap_scalar_keyframe(spread, &LayerEffect::spread_prop));
        }
        if (type == LayerEffectType::Clarity) {
            midtone = spin(-1.0, 1.0, 0.01);
            midtone->setValue(panel_eval_effect_property(effect->brightness_prop, effect->brightness, lt));
            bind_numeric(midtone, [](const LayerEffect &e,double t){return panel_eval_effect_property(e.brightness_prop,e.brightness,t);});
            add_effect_row(QStringLiteral("Midtone Bias"), wrap_scalar_keyframe(midtone, &LayerEffect::brightness_prop));
        }
        auto *luminance = new FxmSwitch(QStringLiteral("Luminance Only"), box);
        luminance->setChecked(effect->effect_monochrome);
        auto *protect_alpha = new FxmSwitch(QStringLiteral("Protect Alpha"), box);
        protect_alpha->setChecked(effect->effect_affect_alpha);
        add_effect_row(QString(), luminance);
        add_effect_row(QString(), protect_alpha);
        QCheckBox *overlay = nullptr;
        if (type == LayerEffectType::HighPass) {
            overlay = new FxmSwitch(QStringLiteral("Overlay Preview"), box);
            overlay->setChecked(effect->effect_invert);
            add_effect_row(QString(), overlay);
        }
        const auto bind_value=[this](QDoubleSpinBox*w,float LayerEffect::*f,AnimatedProperty LayerEffect::*p){
            if (!w) return;
            connect(w,QOverload<double>::of(&QDoubleSpinBox::valueChanged),this,[this,f,p](double v){
                if(!loading_values_&&selected_effect()){selected_effect()->*f=(float)v;set_animated_value(selected_effect()->*p,current_local_time(),v);emit_effect_changed();}});
        };
        bind_value(amount,&LayerEffect::effect_amount,&LayerEffect::amount_prop);
        bind_value(radius,&LayerEffect::effect_size,&LayerEffect::size_prop);
        bind_value(threshold,&LayerEffect::effect_softness,&LayerEffect::softness_prop);
        bind_value(spread,&LayerEffect::effect_spread,&LayerEffect::spread_prop);
        bind_value(falloff,&LayerEffect::effect_falloff,&LayerEffect::falloff_prop);
        bind_value(midtone,&LayerEffect::brightness,&LayerEffect::brightness_prop);
        connect(luminance,&QCheckBox::toggled,this,[this](bool v){if(!loading_values_&&selected_effect()){selected_effect()->effect_monochrome=v;emit_effect_changed();}});
        connect(protect_alpha,&QCheckBox::toggled,this,[this](bool v){if(!loading_values_&&selected_effect()){selected_effect()->effect_affect_alpha=v;emit_effect_changed();}});
        if (overlay) connect(overlay,&QCheckBox::toggled,this,[this](bool v){if(!loading_values_&&selected_effect()){selected_effect()->effect_invert=v;emit_effect_changed();}});
    } else if (selected_effect()->type == LayerEffectType::ChromaKey ||
               selected_effect()->type == LayerEffectType::LumaKey ||
               selected_effect()->type == LayerEffectType::ColorRange ||
               selected_effect()->type == LayerEffectType::SpillSuppression ||
               selected_effect()->type == LayerEffectType::MatteChoker) {
        LayerEffect *effect = selected_effect();
        const LayerEffectType type = effect->type;
        const auto add_numeric = [&](const QString &label, double minimum, double maximum, double step,
                                     float LayerEffect::*fallback, AnimatedProperty LayerEffect::*property) {
            auto *widget = spin(minimum, maximum, step);
            widget->setValue(panel_eval_effect_property(effect->*property, effect->*fallback, lt));
            bind_numeric(widget, [property, fallback](const LayerEffect &e, double t) {
                return panel_eval_effect_property(e.*property, e.*fallback, t);
            });
            add_effect_row(label, wrap_scalar_keyframe(widget, property));
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property, current_local_time(), value);
                    emit_effect_changed();
                }
            });
            return widget;
        };

        if (type == LayerEffectType::ChromaKey ||
            type == LayerEffectType::ColorRange ||
            type == LayerEffectType::SpillSuppression) {
            auto *color = color_button(panel_eval_effect_color(*effect, lt),
                                       [this](uint32_t argb) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_color = argb;
                    set_effect_color_channels_at(*selected_effect(),
                                                 current_local_time(), argb);
                    emit_effect_changed();
                }
            });
            bind_color(color, [](const LayerEffect &e, double t) {
                return panel_eval_effect_color(e, t);
            });
            add_effect_row(type == LayerEffectType::SpillSuppression
                               ? QStringLiteral("Spill Color")
                               : QStringLiteral("Key Color"),
                           wrap_color_keyframe(color, &LayerEffect::color_a,
                                               &LayerEffect::color_r,
                                               &LayerEffect::color_g,
                                               &LayerEffect::color_b));
        }

        if (type == LayerEffectType::ChromaKey) {
            add_numeric(QStringLiteral("Similarity"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Smoothness"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_softness, &LayerEffect::softness_prop);
            add_numeric(QStringLiteral("Spill Suppression"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_spread, &LayerEffect::spread_prop);
            add_numeric(QStringLiteral("Edge Recovery"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
        } else if (type == LayerEffectType::LumaKey) {
            add_numeric(QStringLiteral("Threshold"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Feather"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_softness, &LayerEffect::softness_prop);
        } else if (type == LayerEffectType::ColorRange) {
            add_numeric(QStringLiteral("Tolerance"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Feather"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_softness, &LayerEffect::softness_prop);
        } else if (type == LayerEffectType::SpillSuppression) {
            add_numeric(QStringLiteral("Strength"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Range"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_softness, &LayerEffect::softness_prop);
            auto *preserve_luminance = new FxmSwitch(QStringLiteral("Preserve Luminance"), box);
            preserve_luminance->setChecked(effect->effect_monochrome);
            add_effect_row(QString(), preserve_luminance);
            connect(preserve_luminance, &QCheckBox::toggled, this, [this](bool enabled) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_monochrome = enabled;
                    emit_effect_changed();
                }
            });
        } else if (type == LayerEffectType::MatteChoker) {
            add_numeric(QStringLiteral("Choke"), -1.0, 1.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Radius"), 0.0, 64.0, 0.1,
                        &LayerEffect::effect_size, &LayerEffect::size_prop);
            add_numeric(QStringLiteral("Feather"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_softness, &LayerEffect::softness_prop);
        }

        if (type == LayerEffectType::ChromaKey ||
            type == LayerEffectType::LumaKey ||
            type == LayerEffectType::ColorRange) {
            auto *invert = new FxmSwitch(fxm_tr("OBSTitles.Invert"), box);
            invert->setChecked(effect->effect_invert);
            add_effect_row(QString(), invert);
            connect(invert, &QCheckBox::toggled, this, [this](bool enabled) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_invert = enabled;
                    emit_effect_changed();
                }
            });
        }
        add_numeric(fxm_tr("OBSTitles.OpacityLabel"), 0.0, 1.0, 0.01,
                    &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
    } else if (selected_effect()->type == LayerEffectType::LightWrap ||
               selected_effect()->type == LayerEffectType::DisplacementMap) {
        LayerEffect *effect = selected_effect();
        const bool light_wrap = effect->type == LayerEffectType::LightWrap;

        auto *source_mode = combo();
        if (light_wrap) {
            source_mode->addItem(tr("Composition"), 0);
            source_mode->addItem(tr("Layer"), 1);
            source_mode->setCurrentIndex(std::max(0, source_mode->findData(effect->effect_source_mode)));
            add_effect_row(tr("Background Source"), source_mode);
        }

        auto *source_layer = combo();
        source_layer->addItem(tr("None"), QString());
        if (title_) {
            for (const auto &candidate : title_->layers) {
                if (!candidate || candidate->id == layer_->id)
                    continue;
                source_layer->addItem(QString::fromStdString(candidate->name),
                                      QString::fromStdString(candidate->id));
            }
        }
        int source_index = source_layer->findData(
            QString::fromStdString(effect->effect_source_layer_id));
        source_layer->setCurrentIndex(std::max(0, source_index));
        source_layer->setEnabled(!light_wrap || effect->effect_source_mode == 1);
        add_effect_row(light_wrap ? tr("Background Layer") : tr("Displacement Source"),
                       source_layer);

        connect(source_layer, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, source_layer](int) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_source_layer_id =
                    source_layer->currentData().toString().toStdString();
                emit_effect_changed();
            }
        });
        if (light_wrap) {
            connect(source_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, source_mode, source_layer](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_source_mode = source_mode->currentData().toInt();
                    source_layer->setEnabled(selected_effect()->effect_source_mode == 1);
                    emit_effect_changed();
                }
            });
        }

        auto add_numeric = [this, spin, bind_numeric, wrap_scalar_keyframe,
                            add_effect_row, lt](const QString &label,
                                               double minimum, double maximum,
                                               double step, float LayerEffect::*fallback,
                                               AnimatedProperty LayerEffect::*property) {
            LayerEffect *active = selected_effect();
            auto *widget = spin(minimum, maximum, step);
            widget->setDecimals(step < 0.1 ? 3 : 2);
            widget->setValue(panel_eval_effect_property(active->*property,
                                                         active->*fallback, lt));
            bind_numeric(widget, [property, fallback](const LayerEffect &candidate,
                                                       double time) {
                return panel_eval_effect_property(candidate.*property,
                                                  candidate.*fallback, time);
            });
            add_effect_row(label, wrap_scalar_keyframe(widget, property));
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property,
                                       current_local_time(), value);
                    emit_effect_changed();
                }
            });
        };

        if (light_wrap) {
            add_numeric(tr("Radius"), 0.0, 512.0, 0.5,
                        &LayerEffect::effect_size, &LayerEffect::size_prop);
            add_numeric(tr("Intensity"), 0.0, 8.0, 0.01,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(tr("Edge Width"), 0.0, 256.0, 0.25,
                        &LayerEffect::effect_spread, &LayerEffect::spread_prop);
            auto *spill_color = color_button(panel_eval_effect_color(*effect, lt),
                [this](uint32_t argb) {
                    if (!selected_effect()) return;
                    selected_effect()->effect_color = argb;
                    set_effect_color_channels_at(*selected_effect(),
                                                 current_local_time(), argb);
                });
            bind_color(spill_color, [](const LayerEffect &candidate, double time) {
                return panel_eval_effect_color(candidate, time);
            });
            add_effect_row(tr("Spill Color"),
                wrap_color_keyframe(spill_color, &LayerEffect::color_a,
                    &LayerEffect::color_r, &LayerEffect::color_g,
                    &LayerEffect::color_b));
            add_numeric(tr("Foreground Luminance Protection"), 0.0, 1.0, 0.01,
                        &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
            auto *alpha_aware = new FxmSwitch(tr("Alpha-aware Edge Extraction"), box);
            alpha_aware->setChecked(effect->effect_alpha_aware);
            add_effect_row(QString(), alpha_aware);
            connect(alpha_aware, &QCheckBox::toggled, this, [this](bool enabled) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_alpha_aware = enabled;
                    emit_effect_changed();
                }
            });
        } else {
            auto make_channel = [combo](int current) {
                QComboBox *field = combo();
                field->addItem(QObject::tr("Luminance"), 0);
                field->addItem(QObject::tr("Red"), 1);
                field->addItem(QObject::tr("Green"), 2);
                field->addItem(QObject::tr("Blue"), 3);
                field->addItem(QObject::tr("Alpha"), 4);
                field->setCurrentIndex(std::max(0, field->findData(current)));
                return field;
            };
            auto *x_channel = make_channel(effect->effect_x_channel);
            auto *y_channel = make_channel(effect->effect_y_channel);
            add_effect_row(tr("X Channel"), x_channel);
            add_effect_row(tr("Y Channel"), y_channel);
            connect(x_channel, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, x_channel](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_x_channel = x_channel->currentData().toInt();
                    emit_effect_changed();
                }
            });
            connect(y_channel, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, y_channel](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_y_channel = y_channel->currentData().toInt();
                    emit_effect_changed();
                }
            });
            add_numeric(tr("Horizontal Amount"), -4096.0, 4096.0, 0.5,
                        &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(tr("Vertical Amount"), -4096.0, 4096.0, 0.5,
                        &LayerEffect::effect_distance, &LayerEffect::distance_prop);

            auto *wrap_mode = combo();
            wrap_mode->addItem(tr("Clamp"), 0);
            wrap_mode->addItem(tr("Repeat"), 1);
            wrap_mode->addItem(tr("Mirror"), 2);
            wrap_mode->addItem(tr("Transparent"), 3);
            wrap_mode->setCurrentIndex(std::max(0, wrap_mode->findData(effect->effect_wrap_mode)));
            add_effect_row(tr("Wrap Mode"), wrap_mode);
            connect(wrap_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, wrap_mode](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_wrap_mode = wrap_mode->currentData().toInt();
                    emit_effect_changed();
                }
            });

            auto *mapping = combo();
            mapping->addItem(tr("Source Space"), 0);
            mapping->addItem(tr("Composition Space"), 1);
            mapping->setCurrentIndex(std::max(0, mapping->findData(effect->effect_mapping_space)));
            add_effect_row(tr("Mapping Space"), mapping);
            connect(mapping, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this, mapping](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_mapping_space = mapping->currentData().toInt();
                    emit_effect_changed();
                }
            });
        }
        add_numeric(fxm_tr("OBSTitles.OpacityLabel"), 0.0, 1.0, 0.01,
                    &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
    } else if (selected_effect()->type >= LayerEffectType::LensDistortion &&
               selected_effect()->type <= LayerEffectType::Scanlines) {
        LayerEffect *effect = selected_effect();
        const LayerEffectType type = effect->type;
        const auto add_numeric = [&](const QString &label, double minimum, double maximum, double step,
                                     float LayerEffect::*fallback, AnimatedProperty LayerEffect::*property) {
            auto *widget = spin(minimum, maximum, step);
            widget->setValue(panel_eval_effect_property(effect->*property, effect->*fallback, lt));
            bind_numeric(widget, [property, fallback](const LayerEffect &e, double t) {
                return panel_eval_effect_property(e.*property, e.*fallback, t);
            });
            add_effect_row(label, wrap_scalar_keyframe(widget, property));
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property, current_local_time(), value);
                    emit_effect_changed();
                }
            });
        };
        if (type == LayerEffectType::LensDistortion) {
            add_numeric(QStringLiteral("Distortion"), -3.0, 3.0, 0.01, &LayerEffect::effect_roundness, &LayerEffect::roundness_prop);
            add_numeric(fxm_tr("OBSTitles.CenterX"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
            add_numeric(fxm_tr("OBSTitles.CenterY"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
        } else if (type == LayerEffectType::ChromaticAberration) {
            add_numeric(QStringLiteral("Separation"), 0.0, 256.0, 0.1, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(fxm_tr("OBSTitles.CenterX"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
            add_numeric(fxm_tr("OBSTitles.CenterY"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
        } else if (type == LayerEffectType::DirectionalBlur) {
            add_numeric(fxm_tr("OBSTitles.SizeRadiusLabel"), 0.0, 2048.0, 1.0, &LayerEffect::effect_size, &LayerEffect::size_prop);
            add_numeric(fxm_tr("OBSTitles.AngleLabel"), -1000000000.0, 1000000000.0, 1.0, &LayerEffect::effect_angle, &LayerEffect::angle_prop);
        } else if (type == LayerEffectType::ZoomBlur || type == LayerEffectType::RadialBlur) {
            add_numeric(fxm_tr("OBSTitles.Amount"), 0.0, 1024.0, 1.0, &LayerEffect::effect_size, &LayerEffect::size_prop);
            add_numeric(fxm_tr("OBSTitles.CenterX"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
            add_numeric(fxm_tr("OBSTitles.CenterY"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
        } else if (type == LayerEffectType::Ripple || type == LayerEffectType::WaveWarp) {
            add_numeric(QStringLiteral("Amplitude"), -256.0, 256.0, 0.1, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Frequency"), 0.01, 256.0, 0.1, &LayerEffect::effect_scale, &LayerEffect::scale_prop);
            add_numeric(fxm_tr("OBSTitles.Evolution"), -1000000000.0, 1000000000.0, 0.1, &LayerEffect::effect_evolution, &LayerEffect::evolution_prop);
            if (type == LayerEffectType::Ripple) {
                add_numeric(fxm_tr("OBSTitles.CenterX"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
                add_numeric(fxm_tr("OBSTitles.CenterY"), -2.0, 2.0, 0.01, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
            } else {
                add_numeric(fxm_tr("OBSTitles.AngleLabel"), -1000000000.0, 1000000000.0, 1.0, &LayerEffect::effect_angle, &LayerEffect::angle_prop);
            }
        } else if (type == LayerEffectType::Pixelate) {
            add_numeric(QStringLiteral("Block Size"), 1.0, 1024.0, 1.0, &LayerEffect::effect_size, &LayerEffect::size_prop);
        } else if (type == LayerEffectType::EdgeDetect) {
            add_numeric(QStringLiteral("Strength"), 0.0, 16.0, 0.05, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Radius"), 1.0, 16.0, 1.0, &LayerEffect::effect_size, &LayerEffect::size_prop);
            add_numeric(QStringLiteral("Threshold"), 0.0, 1.0, 0.01, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
        } else if (type == LayerEffectType::Posterize) {
            add_numeric(QStringLiteral("Levels"), 2.0, 64.0, 1.0, &LayerEffect::effect_complexity, &LayerEffect::complexity_prop);
        } else if (type == LayerEffectType::Threshold) {
            add_numeric(QStringLiteral("Threshold"), 0.0, 1.0, 0.01, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
            add_numeric(QStringLiteral("Feather"), 0.0, 1.0, 0.01, &LayerEffect::effect_softness, &LayerEffect::softness_prop);
        } else if (type == LayerEffectType::Scanlines) {
            add_numeric(QStringLiteral("Strength"), 0.0, 1.0, 0.01, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
            add_numeric(QStringLiteral("Spacing"), 1.0, 256.0, 1.0, &LayerEffect::effect_scale, &LayerEffect::scale_prop);
            add_numeric(QStringLiteral("Softness"), 0.0, 1.0, 0.01, &LayerEffect::effect_softness, &LayerEffect::softness_prop);
            add_numeric(fxm_tr("OBSTitles.AngleLabel"), -1000000000.0, 1000000000.0, 1.0, &LayerEffect::effect_angle, &LayerEffect::angle_prop);
            add_numeric(fxm_tr("OBSTitles.Evolution"), -1000000000.0, 1000000000.0, 0.1, &LayerEffect::effect_evolution, &LayerEffect::evolution_prop);
        }
        add_numeric(fxm_tr("OBSTitles.OpacityLabel"), 0.0, 1.0, 0.01, &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
    } else if (selected_effect()->type == LayerEffectType::Noise ||
               selected_effect()->type == LayerEffectType::Grain ||
               selected_effect()->type == LayerEffectType::FilmDistortion ||
               selected_effect()->type == LayerEffectType::AnalogDistortion ||
               selected_effect()->type == LayerEffectType::DigitalDistortion ||
               selected_effect()->type == LayerEffectType::RoughenEdges) {
        LayerEffect *effect = selected_effect();
        const bool procedural_noise = effect->type == LayerEffectType::Noise ||
            effect->type == LayerEffectType::Grain;
        const bool damage_distortion = effect->type == LayerEffectType::FilmDistortion ||
            effect->type == LayerEffectType::AnalogDistortion ||
            effect->type == LayerEffectType::DigitalDistortion;
        QComboBox *profile = nullptr;
        if (procedural_noise) {
            profile = combo();
            profile->addItem(QStringLiteral("Fine Grain"), 0);
            profile->addItem(QStringLiteral("Film Grain"), 1);
            profile->addItem(QStringLiteral("Digital Sensor"), 2);
            profile->addItem(QStringLiteral("Clouds / fBM"), 3);
            profile->addItem(QStringLiteral("Turbulence"), 4);
            profile->addItem(QStringLiteral("Ridged"), 5);
            profile->addItem(QStringLiteral("Cellular"), 6);
            profile->addItem(QStringLiteral("Blue-noise Dither"), 7);
            profile->setCurrentIndex(profile->findData(effect->effect_profile));
            if (profile->currentIndex() < 0)
                profile->setCurrentIndex(0);
            add_effect_row(fxm_tr("OBSTitles.EffectProfile"), profile);
        }

        auto *opacity = spin(0.0, 1.0, 0.01);
        opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *amount = spin(0.0, 4.0, 0.01);
        amount->setValue(panel_eval_effect_property(effect->amount_prop, effect->effect_amount, lt));
        auto *scale = spin(0.001, (procedural_noise || damage_distortion) ? 4096.0 : 1000.0, 0.1);
        scale->setValue(panel_eval_effect_property(effect->scale_prop, effect->effect_scale, lt));
        auto *soft = spin(0.0, 1.0, 0.01);
        soft->setValue(panel_eval_effect_property(effect->softness_prop, effect->effect_softness, lt));
        auto *complexity = spin(1.0, procedural_noise ? 8.0 : 24.0, procedural_noise ? 1.0 : 0.25);
        complexity->setValue(panel_eval_effect_property(effect->complexity_prop, effect->effect_complexity, lt));
        auto *evolution = spin(-1000000000.0, 1000000000.0, 1.0);
        evolution->setValue(panel_eval_effect_property(effect->evolution_prop, effect->effect_evolution, lt));
        auto *seed = new QSpinBox(box);
        seed->setRange(0, 1000000);
        seed->setValue(effect->effect_seed);
        QDoubleSpinBox *speed = nullptr;
        QCheckBox *animated = nullptr;
        QCheckBox *mono = nullptr;
        if (procedural_noise || damage_distortion) {
            speed = spin(-100.0, 100.0, 0.1);
            speed->setValue(panel_eval_effect_property(effect->speed_prop, effect->effect_speed, lt));
            animated = new FxmSwitch(fxm_tr("OBSTitles.Animated"), box);
            animated->setChecked(effect->effect_animated);
            if (procedural_noise) {
                mono = new FxmSwitch(fxm_tr("OBSTitles.Monochrome"), box);
                mono->setChecked(effect->effect_monochrome);
            }
        }
        auto *invert = new FxmSwitch(fxm_tr("OBSTitles.Invert"), box);
        invert->setChecked(effect->effect_invert);

        const auto init_bind = [&](QDoubleSpinBox *widget,
                                   const AnimatedProperty LayerEffect::*property,
                                   const float LayerEffect::*fallback) {
            if (widget)
                bind_numeric(widget, [property, fallback](const LayerEffect &e, double t) {
                    return panel_eval_effect_property(e.*property, e.*fallback, t);
                });
        };
        init_bind(opacity, &LayerEffect::opacity_prop, &LayerEffect::effect_opacity);
        init_bind(amount, &LayerEffect::amount_prop, &LayerEffect::effect_amount);
        init_bind(scale, &LayerEffect::scale_prop, &LayerEffect::effect_scale);
        init_bind(soft, &LayerEffect::softness_prop, &LayerEffect::effect_softness);
        init_bind(complexity, &LayerEffect::complexity_prop, &LayerEffect::effect_complexity);
        init_bind(evolution, &LayerEffect::evolution_prop, &LayerEffect::effect_evolution);
        init_bind(speed, &LayerEffect::speed_prop, &LayerEffect::effect_speed);

        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(damage_distortion ? QStringLiteral("Damage") : fxm_tr("OBSTitles.Amount"),
                       wrap_scalar_keyframe(amount, &LayerEffect::amount_prop));
        add_effect_row(procedural_noise ? fxm_tr("OBSTitles.NoiseGrainSize")
                       : (damage_distortion ? QStringLiteral("Artifact Size") : fxm_tr("OBSTitles.Scale")),
                       wrap_scalar_keyframe(scale, &LayerEffect::scale_prop));
        add_effect_row(damage_distortion ? QStringLiteral("Blend / Smear") : fxm_tr("OBSTitles.SoftnessLabel"),
                       wrap_scalar_keyframe(soft, &LayerEffect::softness_prop));
        add_effect_row(procedural_noise ? fxm_tr("OBSTitles.NoiseOctaves")
                       : (damage_distortion ? QStringLiteral("Density") : fxm_tr("OBSTitles.Complexity")),
                       wrap_scalar_keyframe(complexity, &LayerEffect::complexity_prop));
        add_effect_row(fxm_tr("OBSTitles.Evolution"), wrap_scalar_keyframe(evolution, &LayerEffect::evolution_prop));
        if (speed)
            add_effect_row(fxm_tr("OBSTitles.Speed"), wrap_scalar_keyframe(speed, &LayerEffect::speed_prop));
        add_effect_row(fxm_tr("OBSTitles.Seed"), seed);

        QDoubleSpinBox *aspect = nullptr;
        QDoubleSpinBox *offset_x = nullptr;
        QDoubleSpinBox *offset_y = nullptr;
        QDoubleSpinBox *lacunarity = nullptr;
        QDoubleSpinBox *gain = nullptr;
        QDoubleSpinBox *noise_brightness = nullptr;
        QDoubleSpinBox *noise_contrast = nullptr;
        QPushButton *channel_intensity = nullptr;
        QCheckBox *affect_alpha = nullptr;
        QCheckBox *clamp_output = nullptr;
        QCheckBox *temporal_stability = nullptr;
        if (procedural_noise || damage_distortion) {
            aspect = spin(-3.0, 3.0, 0.05);
            aspect->setValue(panel_eval_effect_property(effect->roundness_prop, effect->effect_roundness, lt));
            offset_x = spin(-100000.0, 100000.0, 1.0);
            offset_x->setValue(panel_eval_effect_property(effect->center_x_prop, effect->effect_center_x, lt));
            offset_y = spin(-100000.0, 100000.0, 1.0);
            offset_y->setValue(panel_eval_effect_property(effect->center_y_prop, effect->effect_center_y, lt));
            lacunarity = spin(1.01, 8.0, 0.01);
            lacunarity->setValue(panel_eval_effect_property(effect->spread_prop, effect->effect_spread, lt));
            gain = spin(0.0, 1.0, 0.01);
            gain->setValue(panel_eval_effect_property(effect->falloff_prop, effect->effect_falloff, lt));
            noise_brightness = spin(-1.0, 1.0, 0.01);
            noise_brightness->setValue(panel_eval_effect_property(effect->brightness_prop, effect->brightness, lt));
            noise_contrast = spin(0.0, 4.0, 0.01);
            noise_contrast->setValue(panel_eval_effect_property(effect->contrast_prop, effect->contrast, lt));
            channel_intensity = color_button(panel_eval_effect_color(*effect, lt), [this](uint32_t argb) {
                if (selected_effect()) {
                    selected_effect()->effect_color = argb;
                    set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
                    emit_effect_changed();
                }
            });
            if (procedural_noise) {
                affect_alpha = new FxmSwitch(fxm_tr("OBSTitles.NoiseAffectAlpha"), box);
                affect_alpha->setChecked(effect->effect_affect_alpha);
            }
            clamp_output = new FxmSwitch(fxm_tr("OBSTitles.NoiseClampOutput"), box);
            clamp_output->setChecked(effect->effect_clamp_output);
            temporal_stability = new FxmSwitch(fxm_tr("OBSTitles.NoiseTemporalStability"), box);
            temporal_stability->setChecked(effect->effect_temporal_stability);

            init_bind(aspect, &LayerEffect::roundness_prop, &LayerEffect::effect_roundness);
            init_bind(offset_x, &LayerEffect::center_x_prop, &LayerEffect::effect_center_x);
            init_bind(offset_y, &LayerEffect::center_y_prop, &LayerEffect::effect_center_y);
            init_bind(lacunarity, &LayerEffect::spread_prop, &LayerEffect::effect_spread);
            init_bind(gain, &LayerEffect::falloff_prop, &LayerEffect::effect_falloff);
            init_bind(noise_brightness, &LayerEffect::brightness_prop, &LayerEffect::brightness);
            init_bind(noise_contrast, &LayerEffect::contrast_prop, &LayerEffect::contrast);

            add_effect_row(damage_distortion ? QStringLiteral("Direction / Aspect") : fxm_tr("OBSTitles.NoiseAspect"),
                           wrap_scalar_keyframe(aspect, &LayerEffect::roundness_prop));
            add_effect_row(damage_distortion ? QStringLiteral("Offset X") : fxm_tr("OBSTitles.NoiseOffsetX"),
                           wrap_scalar_keyframe(offset_x, &LayerEffect::center_x_prop));
            add_effect_row(damage_distortion ? QStringLiteral("Offset Y") : fxm_tr("OBSTitles.NoiseOffsetY"),
                           wrap_scalar_keyframe(offset_y, &LayerEffect::center_y_prop));
            add_effect_row(damage_distortion ? QStringLiteral("Element Spread") : fxm_tr("OBSTitles.NoiseLacunarity"),
                           wrap_scalar_keyframe(lacunarity, &LayerEffect::spread_prop));
            add_effect_row(damage_distortion ? QStringLiteral("Damage Falloff") : fxm_tr("OBSTitles.NoiseGain"),
                           wrap_scalar_keyframe(gain, &LayerEffect::falloff_prop));
            add_effect_row(fxm_tr("OBSTitles.BrightnessLabel"), wrap_scalar_keyframe(noise_brightness, &LayerEffect::brightness_prop));
            add_effect_row(fxm_tr("OBSTitles.ContrastLabel"), wrap_scalar_keyframe(noise_contrast, &LayerEffect::contrast_prop));
            add_effect_row(damage_distortion ? QStringLiteral("Damage Color") : fxm_tr("OBSTitles.NoiseChannelIntensity"),
                           wrap_color_keyframe(channel_intensity, &LayerEffect::color_a,
                                               &LayerEffect::color_r, &LayerEffect::color_g,
                                               &LayerEffect::color_b));
            if (damage_distortion) {
                auto *secondary_damage = color_button(panel_eval_effect_secondary_color(*effect, lt), [this](uint32_t argb) {
                    if (selected_effect()) {
                        selected_effect()->effect_secondary_color = argb;
                        set_effect_secondary_color_channels_at(*selected_effect(), current_local_time(), argb);
                        emit_effect_changed();
                    }
                });
                bind_color(secondary_damage, [](const LayerEffect &e, double t) { return panel_eval_effect_secondary_color(e, t); });
                add_effect_row(QStringLiteral("Secondary Damage Color"),
                               wrap_color_keyframe(secondary_damage, &LayerEffect::secondary_color_a,
                                                   &LayerEffect::secondary_color_r, &LayerEffect::secondary_color_g,
                                                   &LayerEffect::secondary_color_b));
            }
        }

        if (animated)
            add_effect_row(QString(), animated);
        if (mono)
            add_effect_row(QString(), mono);
        if (temporal_stability)
            add_effect_row(QString(), temporal_stability);
        if (affect_alpha)
            add_effect_row(QString(), affect_alpha);
        if (clamp_output)
            add_effect_row(QString(), clamp_output);
        add_effect_row(QString(), invert);

        const auto bind_value = [this](QDoubleSpinBox *widget, float LayerEffect::*fallback,
                                       AnimatedProperty LayerEffect::*property) {
            if (!widget)
                return;
            connect(widget, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                    [this, fallback, property](double value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->*fallback = static_cast<float>(value);
                    set_animated_value(selected_effect()->*property, current_local_time(), value);
                    emit_effect_changed();
                }
            });
        };
        bind_value(opacity, &LayerEffect::effect_opacity, &LayerEffect::opacity_prop);
        bind_value(amount, &LayerEffect::effect_amount, &LayerEffect::amount_prop);
        bind_value(scale, &LayerEffect::effect_scale, &LayerEffect::scale_prop);
        bind_value(soft, &LayerEffect::effect_softness, &LayerEffect::softness_prop);
        bind_value(complexity, &LayerEffect::effect_complexity, &LayerEffect::complexity_prop);
        bind_value(evolution, &LayerEffect::effect_evolution, &LayerEffect::evolution_prop);
        bind_value(speed, &LayerEffect::effect_speed, &LayerEffect::speed_prop);
        bind_value(aspect, &LayerEffect::effect_roundness, &LayerEffect::roundness_prop);
        bind_value(offset_x, &LayerEffect::effect_center_x, &LayerEffect::center_x_prop);
        bind_value(offset_y, &LayerEffect::effect_center_y, &LayerEffect::center_y_prop);
        bind_value(lacunarity, &LayerEffect::effect_spread, &LayerEffect::spread_prop);
        bind_value(gain, &LayerEffect::effect_falloff, &LayerEffect::falloff_prop);
        bind_value(noise_brightness, &LayerEffect::brightness, &LayerEffect::brightness_prop);
        bind_value(noise_contrast, &LayerEffect::contrast, &LayerEffect::contrast_prop);

        if (profile) {
            connect(profile, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this, profile](int) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_profile = profile->currentData().toInt();
                    emit_effect_changed();
                }
            });
        }
        connect(seed, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int value) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_seed = value;
                emit_effect_changed();
            }
        });
        if (animated)
            connect(animated, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_animated = value;
                    emit_effect_changed();
                }
            });
        if (mono)
            connect(mono, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_monochrome = value;
                    emit_effect_changed();
                }
            });
        if (affect_alpha)
            connect(affect_alpha, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_affect_alpha = value;
                    emit_effect_changed();
                }
            });
        if (clamp_output)
            connect(clamp_output, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_clamp_output = value;
                    emit_effect_changed();
                }
            });
        if (temporal_stability)
            connect(temporal_stability, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_temporal_stability = value;
                    emit_effect_changed();
                }
            });
        connect(invert, &QCheckBox::toggled, this, [this](bool value) {
            if (!loading_values_ && selected_effect()) {
                selected_effect()->effect_invert = value;
                emit_effect_changed();
            }
        });
    } else if (selected_effect()->type == LayerEffectType::Emboss) {
        LayerEffect *effect = selected_effect();
        auto *depth = spin(0.1, 32.0, 0.1); depth->setDecimals(2); depth->setValue(effect->size_prop.is_animated() ? effect->size_prop.evaluate(lt) : effect->effect_size);
        auto *height = spin(0.1, 32.0, 0.1); height->setDecimals(2); height->setValue(effect->distance_prop.is_animated() ? effect->distance_prop.evaluate(lt) : effect->effect_distance);
        auto *angle = spin(-1000000000.0, 1000000000.0, 5.0); angle->setValue(effect->angle_prop.is_animated() ? effect->angle_prop.evaluate(lt) : effect->effect_angle);
        auto *softness = spin(0.0, 16.0, 0.1); softness->setDecimals(2); softness->setValue(effect->spread_prop.is_animated() ? effect->spread_prop.evaluate(lt) : effect->effect_spread);
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);
        bind_numeric(depth, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        bind_numeric(height, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.distance_prop, effect.effect_distance, t);
        });
        bind_numeric(angle, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.angle_prop, effect.effect_angle, t);
        });
        bind_numeric(softness, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.spread_prop, effect.effect_spread, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        add_effect_row(fxm_tr("OBSTitles.DepthLabel"), wrap_scalar_keyframe(depth, &LayerEffect::size_prop));
        add_effect_row(fxm_tr("OBSTitles.HeightLabel"), wrap_scalar_keyframe(height, &LayerEffect::distance_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(angle, &LayerEffect::angle_prop));
        add_effect_row(fxm_tr("OBSTitles.SoftnessLabel"), wrap_scalar_keyframe(softness, &LayerEffect::spread_prop));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        connect(depth, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size=(float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(height, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance=(float)v; set_animated_value(selected_effect()->distance_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle=(float)v; set_animated_value(selected_effect()->angle_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(softness, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_spread=(float)v; set_animated_value(selected_effect()->spread_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity=(float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::InnerShadow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){
            if (selected_effect()) {
                selected_effect()->effect_color = argb;
                set_effect_color_channels_at(*selected_effect(), current_local_time(), argb);
            }
        });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(panel_eval_effect_property(effect->opacity_prop, effect->effect_opacity, lt));
        auto *dist = spin(0.0, 4096.0, 1.0); dist->setValue(panel_eval_effect_property(effect->distance_prop, effect->effect_distance, lt));
        auto *angle = spin(-1000000000.0, 1000000000.0, 5.0); angle->setValue(panel_eval_effect_property(effect->angle_prop, effect->effect_angle, lt));
        auto *size = spin(0.0, 512.0, 1.0); size->setValue(panel_eval_effect_property(effect->size_prop, effect->effect_size, lt));
        bind_color(color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(dist, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.distance_prop, effect.effect_distance, t);
        });
        bind_numeric(angle, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.angle_prop, effect.effect_angle, t);
        });
        bind_numeric(size, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        auto *angle_field = fxm_make_angle_field(angle, box);
        add_effect_row(fxm_tr("OBSTitles.ColorLabel"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.OpacityLabel"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.DistanceLabel"), wrap_scalar_keyframe(dist, &LayerEffect::distance_prop));
        add_effect_row(fxm_tr("OBSTitles.AngleLabel"), wrap_scalar_keyframe(angle_field, &LayerEffect::angle_prop, [angle]() { return angle->value(); }));
        add_effect_row(fxm_tr("OBSTitles.SizeRadiusLabel"), wrap_scalar_keyframe(size, &LayerEffect::size_prop));
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(dist, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; set_animated_value(selected_effect()->distance_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; set_animated_value(selected_effect()->angle_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(size, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
    } else if (selected_effect()->type == LayerEffectType::LongShadow) {
        LayerEffect *effect = selected_effect();
        auto *color = color_button(panel_eval_effect_color(*effect, lt), [this, lt](uint32_t argb){ if (selected_effect()) { selected_effect()->effect_color = argb; set_effect_color_channels_at(*selected_effect(), current_local_time(), argb); } });
        auto *opacity = spin(0.0, 1.0, 0.05); opacity->setDecimals(2); opacity->setValue(effect->opacity_prop.is_animated() ? effect->opacity_prop.evaluate(lt) : effect->effect_opacity);
        auto *length = spin(0.0, 4096.0, 5.0); length->setValue(effect->distance_prop.is_animated() ? effect->distance_prop.evaluate(lt) : effect->effect_distance);
        auto *angle = spin(-1000000000.0, 1000000000.0, 5.0); angle->setValue(effect->angle_prop.is_animated() ? effect->angle_prop.evaluate(lt) : effect->effect_angle);
        auto *falloff = spin(0.0, 8.0, 0.1); falloff->setDecimals(2); falloff->setValue(effect->falloff_prop.is_animated() ? effect->falloff_prop.evaluate(lt) : effect->effect_falloff);
        auto *blur = spin(0.0, 512.0, 1.0); blur->setValue(effect->size_prop.is_animated() ? effect->size_prop.evaluate(lt) : effect->effect_size);
        bind_color(color, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_color(effect, t);
        });
        bind_numeric(opacity, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.opacity_prop, effect.effect_opacity, t);
        });
        bind_numeric(length, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.distance_prop, effect.effect_distance, t);
        });
        bind_numeric(angle, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.angle_prop, effect.effect_angle, t);
        });
        bind_numeric(falloff, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.falloff_prop, effect.effect_falloff, t);
        });
        bind_numeric(blur, [](const LayerEffect &effect, double t) {
            return panel_eval_effect_property(effect.size_prop, effect.effect_size, t);
        });
        auto *angle_field = fxm_make_angle_field(angle, box);
        add_effect_row(fxm_tr("OBSTitles.LongShadowColor"), wrap_color_keyframe(color, &LayerEffect::color_a, &LayerEffect::color_r, &LayerEffect::color_g, &LayerEffect::color_b));
        add_effect_row(fxm_tr("OBSTitles.LongShadowOpacity"), wrap_scalar_keyframe(opacity, &LayerEffect::opacity_prop));
        add_effect_row(fxm_tr("OBSTitles.LongShadowLength"), wrap_scalar_keyframe(length, &LayerEffect::distance_prop));
        add_effect_row(fxm_tr("OBSTitles.LongShadowAngle"), wrap_scalar_keyframe(angle_field, &LayerEffect::angle_prop, [angle]() { return angle->value(); }));
        add_effect_row(fxm_tr("OBSTitles.LongShadowFalloff"), wrap_scalar_keyframe(falloff, &LayerEffect::falloff_prop));
        add_effect_row(fxm_tr("OBSTitles.LongShadowBlur"), wrap_scalar_keyframe(blur, &LayerEffect::size_prop));
        connect(opacity, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_opacity = (float)v; set_animated_value(selected_effect()->opacity_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(length, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_distance = (float)v; set_animated_value(selected_effect()->distance_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(angle, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_angle = (float)v; set_animated_value(selected_effect()->angle_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(falloff, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_falloff = (float)v; set_animated_value(selected_effect()->falloff_prop, current_local_time(), v); emit_effect_changed(); }});
        connect(blur, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, lt](double v){ if (!loading_values_ && selected_effect()) { selected_effect()->effect_size = (float)v; set_animated_value(selected_effect()->size_prop, current_local_time(), v); emit_effect_changed(); }});
    }
    if (LayerEffect *effect = selected_effect()) {
        const bool outside_capable = effect->type == LayerEffectType::DropShadow ||
                                     effect->type == LayerEffectType::LongShadow ||
                                     effect->type == LayerEffectType::Glow;
        if (outside_capable) {
            auto *outside = new FxmSwitch(fxm_tr("OBSTitles.EffectOutsideHardAlpha"), box);
            auto *outside_invert = new FxmSwitch(fxm_tr("OBSTitles.EffectMaskInvert"), box);
            outside->setChecked(effect->effect_outside_hard_alpha);
            outside_invert->setChecked(effect->effect_outside_hard_alpha_invert);
            outside_invert->setEnabled(effect->effect_outside_hard_alpha);
            add_effect_row(QString(), outside);
            add_effect_row(QString(), outside_invert);
            connect(outside, &QCheckBox::toggled, this, [this, outside_invert](bool value) {
                outside_invert->setEnabled(value);
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_outside_hard_alpha = value;
                    emit_effect_changed();
                }
            });
            connect(outside_invert, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->effect_outside_hard_alpha_invert = value;
                    emit_effect_changed();
                }
            });
        }
        const bool backdrop_capable = effect->type != LayerEffectType::BackgroundColor &&
                                      effect->type != LayerEffectType::Outline &&
                                      effect->type != LayerEffectType::DropShadow &&
                                      effect->type != LayerEffectType::LongShadow &&
                                      effect->type != LayerEffectType::Glow &&
                                      effect->type != LayerEffectType::InnerGlow &&
                                      effect->type != LayerEffectType::InnerShadow &&
                                      effect->type != LayerEffectType::MotionBlur;
        if (backdrop_capable) {
            auto *behind = new FxmSwitch(fxm_tr("OBSTitles.EffectAffectLayersBehind"), box);
            auto *behind_invert = new FxmSwitch(fxm_tr("OBSTitles.EffectMaskInvert"), box);
            behind->setChecked(effect->affect_layers_behind);
            behind_invert->setChecked(effect->affect_layers_behind_invert);
            behind_invert->setEnabled(effect->affect_layers_behind);
            add_effect_row(QString(), behind);
            add_effect_row(QString(), behind_invert);
            connect(behind, &QCheckBox::toggled, this, [this, behind_invert](bool value) {
                behind_invert->setEnabled(value);
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->affect_layers_behind = value;
                    emit_effect_changed();
                }
            });
            connect(behind_invert, &QCheckBox::toggled, this, [this](bool value) {
                if (!loading_values_ && selected_effect()) {
                    selected_effect()->affect_layers_behind_invert = value;
                    emit_effect_changed();
                }
            });
        }
    }
    FxmCollapsiblePanel *panel = fxm_add_panel_section(settings_layout_, box);
    if (!panel)
        return;
    panel->setProperty("fxmEffectIndex", effect_index);
    panel->setOrderPersistenceEnabled(false);
    effect_panels_.push_back(panel);

    auto *enabled_switch = new FxmSwitch(panel);
    enabled_switch->setToolTip(fxm_tr("OBSTitles.Enabled"));
    enabled_switch->setAccessibleName(QStringLiteral("%1: %2")
                                          .arg(panel_title, fxm_tr("OBSTitles.Enabled")));
    enabled_switch->setChecked(eval_effect_enabled(panel_effect, lt));
    panel->addHeaderLeadingWidget(enabled_switch);
    bool_bindings_.push_back({enabled_switch,
        [](const LayerEffect &effect, double time) {
            return eval_effect_enabled(effect, time);
        }, effect_index});
    connect(enabled_switch, &QCheckBox::toggled, this,
            [this, panel](bool enabled) {
        if (loading_values_ || !layer_ || !panel)
            return;
        const int index = panel->property("fxmEffectIndex").toInt();
        if (index < 0 || index >= static_cast<int>(layer_->effects.size()))
            return;
        set_active_effect_index(index);
        LayerEffect &effect = layer_->effects[static_cast<size_t>(index)];
        effect.enabled = enabled;
        set_animated_value(effect.enabled_prop, current_local_time(),
                           enabled ? 1.0 : 0.0);
        emit_effect_changed();
    });

    {
        QStringList badges;
        if (!panel_effect.extension_id.empty()) {
            auto &catalog = FxmEffectExtensionCatalog::instance();
            if (catalog.effects().empty()) catalog.reload();
            const auto *definition = catalog.find(
                QString::fromStdString(panel_effect.extension_id));
            if (definition && !definition->builtIn)
                badges << QStringLiteral("PLUGIN");
        }
        if (const EffectDescriptor *descriptor = effect_descriptor(panel_effect)) {
            badges << (descriptor->backend == EffectExecutionBackend::Cpu
                           ? QStringLiteral("CPU") : QStringLiteral("GPU"));
            if (descriptor->supports_hdr) badges << QStringLiteral("HDR");
            if (descriptor->execution_space == LayerEffectSpace::ScreenSpace)
                badges << QStringLiteral("SCREEN");
            if (!descriptor->cacheable_when_static)
                badges << QStringLiteral("CACHE");
        }
        for (const QString &badge : badges) {
            auto *label = new QLabel(badge, panel);
            label->setStyleSheet(QStringLiteral(
                "QLabel{font-size:8px;font-weight:600;padding:1px 3px;"
                "border:1px solid palette(mid);border-radius:2px;}"));
            label->setToolTip(badge == QStringLiteral("CACHE")
                ? tr("Cache-breaking effect")
                : badge == QStringLiteral("SCREEN")
                    ? tr("Screen-space effect") : badge);
            panel->addHeaderWidget(label);
        }
    }

    auto *more_button = new QToolButton(panel);
    more_button->setText(QStringLiteral("⋮"));
    more_button->setAutoRaise(true);
    more_button->setFixedSize(20, 20);
    more_button->setToolTip(tr("Effect actions"));
    panel->addHeaderWidget(more_button);
    connect(more_button, &QToolButton::clicked, this,
            [this, panel, more_button]() {
        if (!layer_ || !panel)
            return;
        const int index = panel->property("fxmEffectIndex").toInt();
        if (index < 0 || index >= static_cast<int>(layer_->effects.size()))
            return;
        set_active_effect_index(index);
        QMenu menu(more_button);
        QAction *copy = menu.addAction(tr("Copy effect"));
        QAction *paste = menu.addAction(tr("Paste effect"));
        paste->setEnabled(QApplication::clipboard()->mimeData()->hasFormat(
            QString::fromUtf8(kEffectStackMimeType)));
        QAction *replace = menu.addAction(tr("Replace effect…"));
        QAction *reset = menu.addAction(panel->style()->standardIcon(QStyle::SP_BrowserReload), tr("Defaults"));
        menu.addSeparator();
        QAction *duplicate = menu.addAction(obs_icon("duplicate.svg"),
                                             fxm_tr("OBSTitles.DuplicateEffect"));
        QAction *remove = menu.addAction(obs_icon("delete.svg"),
                                          fxm_tr("OBSTitles.DeleteEffect"));
        menu.addSeparator();
        QAction *move_up = menu.addAction(obs_icon("move-up.svg"),
                                           fxm_tr("OBSTitles.MoveEffectUp"));
        QAction *move_down = menu.addAction(obs_icon("move-down.svg"),
                                             fxm_tr("OBSTitles.MoveEffectDown"));
        move_up->setEnabled(index > 0);
        move_down->setEnabled(index + 1 < static_cast<int>(layer_->effects.size()));
        QAction *chosen = menu.exec(more_button->mapToGlobal(
            QPoint(more_button->width(), more_button->height())));
        if (chosen == copy)
            copy_effect_to_clipboard(index);
        else if (chosen == paste)
            paste_effect_from_clipboard(index);
        else if (chosen == replace) {
            LayerEffect replacement;
            if (choose_effect(&replacement, index) && layer_ &&
                index < static_cast<int>(layer_->effects.size())) {
                layer_->effects[static_cast<size_t>(index)] = std::move(replacement);
                selected_index_ = index;
                rebuild_stack();
                emit_effect_changed();
            }
        } else if (chosen == reset)
            reset_effect(index);
        else if (chosen == duplicate)
            duplicate_effect(index);
        else if (chosen == remove)
            delete_effect(index);
        else if (chosen == move_up)
            move_effect(index, -1);
        else if (chosen == move_down)
            move_effect(index, 1);
    });

    connect(panel, &FxmCollapsiblePanel::activated, this, [this, panel]() {
        if (panel)
            set_active_effect_index(panel->property("fxmEffectIndex").toInt());
    });
    connect(panel, &FxmCollapsiblePanel::orderChanged,
            this, &EffectsPanel::apply_effect_panel_order);

    box->installEventFilter(this);
    const auto effect_widgets = box->findChildren<QWidget *>();
    for (QWidget *widget : effect_widgets)
        widget->installEventFilter(this);

    for (size_t index = numeric_start; index < numeric_bindings_.size(); ++index)
        numeric_bindings_[index].effect_index = effect_index;
    for (size_t index = color_start; index < color_bindings_.size(); ++index)
        color_bindings_[index].effect_index = effect_index;
    for (size_t index = bool_start; index < bool_bindings_.size(); ++index)
        bool_bindings_[index].effect_index = effect_index;
    for (size_t index = combo_start; index < combo_bindings_.size(); ++index)
        combo_bindings_[index].effect_index = effect_index;
    for (size_t index = keyframe_start; index < keyframe_bindings_.size(); ++index)
        keyframe_bindings_[index].effect_index = effect_index;

    /* Effect keyframe diamonds intentionally use the same three-state and
     * right-click behaviour as the main Properties panel. */
    for (size_t binding_index = keyframe_start;
         binding_index < keyframe_bindings_.size(); ++binding_index) {
        const KeyframeBinding &binding = keyframe_bindings_[binding_index];
        if (!binding.button || !binding.has_keyframes || !binding.clear_keyframes)
            continue;
        binding.button->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(binding.button, &QWidget::customContextMenuRequested, this,
                [this, binding](const QPoint &position) {
            LayerEffect *active = selected_effect();
            if (!active || !binding.button)
                return;
            QMenu menu(binding.button);
            QAction *clear = menu.addAction(fxm_tr("OBSTitles.DeleteAllKeyframes"));
            clear->setEnabled(binding.has_keyframes(*active));
            if (menu.exec(binding.button->mapToGlobal(position)) != clear)
                return;
            binding.clear_keyframes(*active);
            emit_effect_changed();
            update_bound_controls();
        });
    }

}
