#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QIODevice>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QString>
#include <QSvgRenderer>
#include <QWidget>

namespace fxm {
class IAssetPathProvider;
}

void fxm_set_asset_path_provider(
    const fxm::IAssetPathProvider *provider) noexcept;
QString fxm_asset_path(const char *relative_path);
QFont fxm_satoshi_ui_font();

static inline void fxm_apply_satoshi_ui_font(QWidget *widget)
{
    if (widget)
        widget->setFont(fxm_satoshi_ui_font());
}

static inline QColor fxm_icon_color()
{
    const QPalette palette = qApp ? qApp->palette() : QPalette();
    const QColor button_text = palette.color(QPalette::Active, QPalette::ButtonText);
    return button_text.isValid() ? button_text : QColor(0x20, 0x20, 0x20);
}

static inline double fxm_linear_color_channel(double channel)
{
    channel = std::clamp(channel, 0.0, 1.0);
    return channel <= 0.04045
        ? channel / 12.92
        : std::pow((channel + 0.055) / 1.055, 2.4);
}

static inline double fxm_relative_luminance(const QColor &color)
{
    return 0.2126 * fxm_linear_color_channel(color.redF()) +
           0.7152 * fxm_linear_color_channel(color.greenF()) +
           0.0722 * fxm_linear_color_channel(color.blueF());
}

static inline double fxm_contrast_ratio(const QColor &a, const QColor &b)
{
    const double la = fxm_relative_luminance(a);
    const double lb = fxm_relative_luminance(b);
    const double lighter = std::max(la, lb);
    const double darker = std::min(la, lb);
    return (lighter + 0.05) / (darker + 0.05);
}

static inline QColor fxm_composite_over(const QColor &foreground,
                                        const QColor &background)
{
    const double alpha = std::clamp<double>(static_cast<double>(foreground.alphaF()), 0.0, 1.0);
    const double inverse = 1.0 - alpha;
    return QColor::fromRgbF(
        foreground.redF() * alpha + background.redF() * inverse,
        foreground.greenF() * alpha + background.greenF() * inverse,
        foreground.blueF() * alpha + background.blueF() * inverse,
        1.0);
}

static inline QColor fxm_mix_colors(const QColor &foreground,
                                    const QColor &background,
                                    double foreground_weight)
{
    foreground_weight = std::clamp(foreground_weight, 0.0, 1.0);
    const double background_weight = 1.0 - foreground_weight;
    return QColor::fromRgbF(
        foreground.redF() * foreground_weight +
            background.redF() * background_weight,
        foreground.greenF() * foreground_weight +
            background.greenF() * background_weight,
        foreground.blueF() * foreground_weight +
            background.blueF() * background_weight,
        1.0);
}

/* Resolve the darkest and lightest semantic foregrounds supplied by the
 * current OBS palette.  Keeping both candidates lets the layer UI use the
 * host theme while applying a deliberately conservative brightness switch. */
static inline std::array<QColor, 2> fxm_semantic_foreground_extremes(
    const QPalette &palette)
{
    const std::array<QColor, 6> candidates = {
        palette.color(QPalette::Active, QPalette::WindowText),
        palette.color(QPalette::Active, QPalette::Text),
        palette.color(QPalette::Active, QPalette::ButtonText),
        palette.color(QPalette::Active, QPalette::BrightText),
        palette.color(QPalette::Active, QPalette::HighlightedText),
        palette.color(QPalette::Active, QPalette::Shadow),
    };

    QColor darkest;
    QColor lightest;
    double darkest_luminance = 2.0;
    double lightest_luminance = -1.0;
    for (const QColor &candidate : candidates) {
        if (!candidate.isValid())
            continue;
        const double luminance = fxm_relative_luminance(candidate);
        if (luminance < darkest_luminance) {
            darkest = candidate;
            darkest_luminance = luminance;
        }
        if (luminance > lightest_luminance) {
            lightest = candidate;
            lightest_luminance = luminance;
        }
    }
    if (!darkest.isValid())
        darkest = QColor(24, 24, 24);
    if (!lightest.isValid())
        lightest = QColor(245, 245, 245);
    darkest.setAlpha(255);
    lightest.setAlpha(255);
    return {darkest, lightest};
}

/* The former maximum-contrast comparison switched to dark text on many
 * medium-saturation colors.  Use dark UI content only on genuinely bright
 * surfaces; everything below this relative-luminance threshold stays light. */
static inline bool fxm_background_prefers_dark_foreground(
    const QColor &background)
{
    constexpr double kDarkForegroundLuminanceThreshold = 0.42;
    return fxm_relative_luminance(background) >=
           kDarkForegroundLuminanceThreshold;
}

static inline QColor fxm_background_aware_foreground(
    const QColor &background, const QPalette &palette)
{
    const auto extremes = fxm_semantic_foreground_extremes(palette);
    return fxm_background_prefers_dark_foreground(background)
        ? extremes[0]
        : extremes[1];
}

/* Waveforms intentionally use the opposite semantic polarity from the
 * background-aware strip label/icon so they remain visually distinct. */
static inline QColor fxm_background_aware_opposite_foreground(
    const QColor &background, const QPalette &palette)
{
    const auto extremes = fxm_semantic_foreground_extremes(palette);
    return fxm_background_prefers_dark_foreground(background)
        ? extremes[1]
        : extremes[0];
}

static inline QColor fxm_background_aware_opposite_foreground(
    const QColor &background)
{
    return fxm_background_aware_opposite_foreground(
        background, qApp ? qApp->palette() : QPalette());
}

static inline QColor fxm_background_aware_foreground(
    const QColor &background)
{
    return fxm_background_aware_foreground(
        background, qApp ? qApp->palette() : QPalette());
}

static inline QColor fxm_background_aware_muted_foreground(
    const QColor &background, const QPalette &palette)
{
    return fxm_mix_colors(
        fxm_background_aware_foreground(background, palette), background, 0.62);
}

static inline QIcon fxm_tinted_svg_icon(const QString &icon_path,
                                        const QColor &color)
{
    QFile file(icon_path);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon(icon_path);

    QByteArray svg = file.readAll();
    svg.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon(icon_path);

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        renderer.render(&painter);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        icon.addPixmap(pixmap);
    }
    return icon;
}

static inline QIcon fxm_icon(const char *file_name, const QColor &color)
{
    const QString relative =
        QStringLiteral("icons/") + QString::fromUtf8(file_name);
    const QString icon_path =
        fxm_asset_path(relative.toUtf8().constData());
    if (icon_path.isEmpty())
        return QIcon();
    return fxm_tinted_svg_icon(icon_path, color);
}

static inline QIcon fxm_icon(const char *file_name)
{
    return fxm_icon(file_name, fxm_icon_color());
}

static inline QIcon fxm_brand_icon()
{
    const QString icon_path =
        fxm_asset_path("icons/flux-motion-icon.svg");
    if (icon_path.isEmpty())
        return QIcon();

    QFile file(icon_path);
    if (!file.open(QIODevice::ReadOnly))
        return QIcon(icon_path);

    const QByteArray svg = file.readAll();
    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return QIcon(icon_path);

    QIcon icon;
    const int sizes[] = {16, 20, 24, 32, 48, 64, 96, 128, 256};
    for (int size : sizes) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.render(&painter, pixmap.rect());
        icon.addPixmap(pixmap);
    }
    return icon;
}

static inline void fxm_apply_brand_icon(QWidget *window)
{
    if (window)
        window->setWindowIcon(fxm_brand_icon());
}
