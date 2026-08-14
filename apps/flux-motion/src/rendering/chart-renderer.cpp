#include "chart-renderer.h"

#include "chart-data.h"

#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QGradient>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace fxm::chart {
namespace {

QColor color(uint32_t argb, double opacity = 1.0)
{
    QColor result(static_cast<int>((argb >> 16) & 0xff),
                  static_cast<int>((argb >> 8) & 0xff),
                  static_cast<int>(argb & 0xff),
                  static_cast<int>((argb >> 24) & 0xff));
    result.setAlphaF(std::clamp(result.alphaF() * opacity, 0.0, 1.0));
    return result;
}

QGradient::Spread gradient_spread(int spread)
{
    if (spread == 1)
        return QGradient::ReflectSpread;
    if (spread == 2)
        return QGradient::RepeatSpread;
    return QGradient::PadSpread;
}

void add_gradient_stops(QGradient &gradient, const Layer &layer,
                        double opacity)
{
    const double overall = std::clamp<double>(
        layer.gradient_opacity * opacity, 0.0, 1.0);
    gradient.setColorAt(std::clamp<double>(layer.gradient_start_pos, 0.0, 1.0),
                        color(layer.gradient_start_color,
                              layer.gradient_start_opacity * overall));
    gradient.setColorAt(std::clamp<double>(layer.gradient_end_pos, 0.0, 1.0),
                        color(layer.gradient_end_color,
                              layer.gradient_end_opacity * overall));
    for (const GradientStop &stop : layer.gradient_stops) {
        gradient.setColorAt(std::clamp<double>(stop.position, 0.0, 1.0),
                            color(stop.color, stop.opacity * overall));
    }
}

QBrush chart_fill_brush(const Layer &layer, const QRectF &bounds,
                        uint32_t solid, double opacity)
{
    if (layer.fill_type != 1 || bounds.isEmpty())
        return QBrush(color(solid, opacity));

    if (layer.gradient_type == 1) {
        const QPointF center(
            bounds.left() + bounds.width() * layer.gradient_center_x,
            bounds.top() + bounds.height() * layer.gradient_center_y);
        const QPointF focal(
            bounds.left() + bounds.width() * layer.gradient_focal_x,
            bounds.top() + bounds.height() * layer.gradient_focal_y);
        QRadialGradient gradient(
            center,
            std::max<qreal>(1.0, std::max(bounds.width(), bounds.height()) *
                                     0.5 * std::max(0.01f, layer.gradient_scale)),
            focal);
        gradient.setSpread(gradient_spread(layer.gradient_spread));
        add_gradient_stops(gradient, layer, opacity);
        return QBrush(gradient);
    }
    if (layer.gradient_type == 2) {
        const QPointF center(
            bounds.left() + bounds.width() * layer.gradient_center_x,
            bounds.top() + bounds.height() * layer.gradient_center_y);
        QConicalGradient gradient(center, layer.gradient_angle);
        gradient.setSpread(gradient_spread(layer.gradient_spread));
        add_gradient_stops(gradient, layer, opacity);
        return QBrush(gradient);
    }
    const double radians = layer.gradient_angle *
        3.14159265358979323846 / 180.0;
    const QPointF center = bounds.center();
    const QPointF axis(std::cos(radians) * bounds.width() * 0.5,
                       std::sin(radians) * bounds.height() * 0.5);
    QLinearGradient gradient(center - axis, center + axis);
    gradient.setSpread(gradient_spread(layer.gradient_spread));
    add_gradient_stops(gradient, layer, opacity);
    return QBrush(gradient);
}

QString formatted_number(const Layer &layer, double value)
{
    const int precision = std::clamp(layer.chart_decimal_precision, 0, 12);
    QString number = layer.chart_thousands_separator
        ? QLocale().toString(value, 'f', precision)
        : QString::number(value, 'f', precision);
    return QString::fromStdString(layer.chart_number_prefix) + number +
           QString::fromStdString(layer.chart_number_suffix);
}

struct Range {
    double minimum = 0.0;
    double maximum = 1.0;
};

Range value_range(const DataSet &data)
{
    Range result;
    for (const Datum &datum : data.values) {
        result.minimum = std::min(result.minimum, datum.value);
        result.maximum = std::max(result.maximum, datum.value);
    }
    if (std::abs(result.maximum - result.minimum) < 1.0e-9) {
        result.minimum -= 0.5;
        result.maximum += 0.5;
    }
    return result;
}

const Datum *find_value(const DataSet &data, const std::string &series_id,
                        const std::string &category)
{
    auto found = std::find_if(data.values.begin(), data.values.end(),
        [&](const Datum &datum) {
            return datum.series_id == series_id && datum.category == category;
        });
    return found == data.values.end() ? nullptr : &*found;
}

double mapped_y(const QRectF &plot, const Range &range, double value)
{
    return plot.bottom() - (value - range.minimum) /
        (range.maximum - range.minimum) * plot.height();
}

void draw_grid_axes(QPainter &painter, const Layer &layer, const QRectF &plot,
                    const Range &range, const DataSet &data, double label_opacity)
{
    constexpr int ticks = 5;
    if (layer.chart_grid_enabled) {
        painter.setPen(QPen(color(layer.chart_grid_color), 1.0));
        for (int tick = 0; tick <= ticks; ++tick) {
            const double y = plot.top() + plot.height() * tick / ticks;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
    }
    if (layer.chart_axes_enabled) {
        painter.setPen(QPen(color(layer.chart_axis_color), 1.25));
        painter.drawLine(plot.bottomLeft(), plot.topLeft());
        painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    }
    if (!layer.chart_axis_labels_enabled || label_opacity <= 0.0001)
        return;
    painter.setPen(color(layer.chart_label_color, label_opacity));
    const QFontMetricsF metrics(painter.font());
    for (int tick = 0; tick <= ticks; ++tick) {
        const double ratio = 1.0 - static_cast<double>(tick) / ticks;
        const QString label = formatted_number(
            layer, range.minimum + ratio * (range.maximum - range.minimum));
        const double y = plot.top() + plot.height() * tick / ticks;
        painter.drawText(QRectF(plot.left() - 66.0, y - metrics.height() * 0.5,
                                60.0, metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter, label);
    }
    const int category_count = static_cast<int>(data.categories.size());
    if (category_count <= 0)
        return;
    const int stride = std::max(1, static_cast<int>(std::ceil(
        category_count * std::max(28.0, metrics.averageCharWidth() * 6.0) /
        std::max(1.0, plot.width()))));
    for (int category = 0; category < category_count; category += stride) {
        const double x = plot.left() + plot.width() *
            (category + 0.5) / category_count;
        painter.drawText(QRectF(x - plot.width() / category_count * 0.5,
                                plot.bottom() + 5.0,
                                plot.width() / category_count,
                                metrics.height() * 2.0),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         QString::fromStdString(data.categories[category]));
    }
}

void draw_legend(QPainter &painter, const Layer &layer, const QRectF &bounds,
                 const DataSet &data, double opacity)
{
    if (!layer.chart_legend_enabled || data.series.empty() || opacity <= 0.0001)
        return;
    const QFontMetricsF metrics(painter.font());
    double x = bounds.left();
    const double y = bounds.top();
    for (const ChartSeries &series : data.series) {
        if (!series.visible)
            continue;
        painter.fillRect(QRectF(x, y + 3.0, 12.0, 12.0), color(series.color, opacity));
        x += 17.0;
        const QString label = QString::fromStdString(series.name);
        painter.setPen(color(layer.chart_label_color, opacity));
        painter.drawText(QPointF(x, y + metrics.ascent() + 2.0), label);
        x += metrics.horizontalAdvance(label) + 18.0;
        if (x > bounds.right() - 80.0)
            break;
    }
}

void draw_vertical_bars(QPainter &painter, const Layer &layer, const QRectF &plot,
                        const Range &range, const DataSet &data,
                        double element_opacity, double label_opacity,
                        double progress, double local_time)
{
    const int categories = static_cast<int>(data.categories.size());
    const int visible_series = static_cast<int>(std::count_if(
        data.series.begin(), data.series.end(), [](const ChartSeries &series) {
            return series.visible;
        }));
    if (categories == 0 || visible_series == 0)
        return;
    const double category_width = plot.width() / categories;
    const double group_width = category_width * std::clamp(
        layer.chart_bar_width.evaluate(local_time), 0.05, 1.0);
    const double spacing = std::clamp(layer.chart_bar_spacing.evaluate(local_time), 0.0, 0.9);
    const double slot_width = group_width / visible_series;
    const double bar_width = slot_width * (1.0 - spacing);
    const double zero_y = mapped_y(plot, range, 0.0);
    const double radius = std::max(0.0, layer.chart_corner_radius.evaluate(local_time));
    int series_index = 0;
    for (const ChartSeries &series : data.series) {
        if (!series.visible)
            continue;
        for (int category = 0; category < categories; ++category) {
            const Datum *datum = find_value(data,
                series.id.empty() ? series.name : series.id,
                data.categories[category]);
            if (!datum)
                continue;
            const double target_y = mapped_y(plot, range, datum->value);
            const double y = zero_y + (target_y - zero_y) * progress;
            const double x = plot.left() + category * category_width +
                (category_width - group_width) * 0.5 + series_index * slot_width +
                (slot_width - bar_width) * 0.5;
            QRectF rect(x, std::min(y, zero_y), bar_width,
                        std::max(0.5, std::abs(zero_y - y)));
            painter.setPen(datum->outline_enabled && layer.chart_outline_enabled
                ? QPen(color(datum->color, element_opacity), 1.0)
                : QPen(Qt::NoPen));
            painter.setBrush(datum->fill_enabled && layer.chart_fill_enabled
                ? chart_fill_brush(layer, rect, datum->fill_color, element_opacity)
                : QBrush(Qt::NoBrush));
            painter.drawRoundedRect(rect, std::min(radius, rect.width() * 0.5),
                                    std::min(radius, rect.height() * 0.5));
            if (layer.chart_data_labels_enabled && label_opacity > 0.0001) {
                painter.setPen(color(layer.chart_label_color, label_opacity));
                painter.drawText(QRectF(rect.left() - 20.0, rect.top() - 24.0,
                                        rect.width() + 40.0, 20.0),
                                 Qt::AlignCenter,
                                 formatted_number(layer, datum->value * progress));
            }
        }
        ++series_index;
    }
}

void draw_horizontal_bars(QPainter &painter, const Layer &layer,
                          const QRectF &plot, const Range &range,
                          const DataSet &data, double element_opacity,
                          double label_opacity, double progress,
                          double local_time)
{
    const int categories = static_cast<int>(data.categories.size());
    const int visible_series = static_cast<int>(std::count_if(
        data.series.begin(), data.series.end(), [](const ChartSeries &series) {
            return series.visible;
        }));
    if (categories == 0 || visible_series == 0)
        return;
    const double category_height = plot.height() / categories;
    const double group_height = category_height * std::clamp(
        layer.chart_bar_width.evaluate(local_time), 0.05, 1.0);
    const double slot_height = group_height / visible_series;
    const double bar_height = slot_height * (1.0 - std::clamp(
        layer.chart_bar_spacing.evaluate(local_time), 0.0, 0.9));
    const double zero_x = plot.left() + (0.0 - range.minimum) /
        (range.maximum - range.minimum) * plot.width();
    const double radius = std::max(0.0, layer.chart_corner_radius.evaluate(local_time));
    int series_index = 0;
    for (const ChartSeries &series : data.series) {
        if (!series.visible)
            continue;
        for (int category = 0; category < categories; ++category) {
            const Datum *datum = find_value(data,
                series.id.empty() ? series.name : series.id,
                data.categories[category]);
            if (!datum)
                continue;
            const double target_x = plot.left() + (datum->value - range.minimum) /
                (range.maximum - range.minimum) * plot.width();
            const double x = zero_x + (target_x - zero_x) * progress;
            const double y = plot.top() + category * category_height +
                (category_height - group_height) * 0.5 + series_index * slot_height +
                (slot_height - bar_height) * 0.5;
            QRectF rect(std::min(x, zero_x), y, std::max(0.5, std::abs(x - zero_x)),
                        bar_height);
            painter.setPen(datum->outline_enabled && layer.chart_outline_enabled
                ? QPen(color(datum->color, element_opacity), 1.0)
                : QPen(Qt::NoPen));
            painter.setBrush(datum->fill_enabled && layer.chart_fill_enabled
                ? chart_fill_brush(layer, rect, datum->fill_color, element_opacity)
                : QBrush(Qt::NoBrush));
            painter.drawRoundedRect(rect, std::min(radius, rect.width() * 0.5),
                                    std::min(radius, rect.height() * 0.5));
            if (layer.chart_data_labels_enabled && label_opacity > 0.0001) {
                painter.setPen(color(layer.chart_label_color, label_opacity));
                painter.drawText(QRectF(rect.right() + 5.0, rect.top(), 100.0,
                                        rect.height()), Qt::AlignLeft | Qt::AlignVCenter,
                                 formatted_number(layer, datum->value * progress));
            }
        }
        ++series_index;
    }
}

void draw_line_area(QPainter &painter, const Layer &layer, const QRectF &plot,
                    const Range &range, const DataSet &data,
                    double element_opacity, double label_opacity,
                    double progress, bool area, double local_time)
{
    const int categories = static_cast<int>(data.categories.size());
    if (categories == 0)
        return;
    painter.save();
    painter.setClipRect(QRectF(plot.left(), plot.top(),
                              plot.width() * progress, plot.height()));
    for (const ChartSeries &series : data.series) {
        if (!series.visible)
            continue;
        QPainterPath path;
        std::vector<QPointF> points;
        std::vector<const Datum *> point_data;
        for (int category = 0; category < categories; ++category) {
            const Datum *datum = find_value(data,
                series.id.empty() ? series.name : series.id,
                data.categories[category]);
            if (!datum)
                continue;
            const double x = categories == 1 ? plot.center().x()
                : plot.left() + plot.width() * category / (categories - 1);
            points.emplace_back(x, mapped_y(plot, range, datum->value));
            point_data.push_back(datum);
        }
        if (points.empty())
            continue;
        path.moveTo(points.front());
        for (std::size_t i = 1; i < points.size(); ++i)
            path.lineTo(points[i]);
        if (area && series.fill_enabled && layer.chart_fill_enabled) {
            QPainterPath fill = path;
            fill.lineTo(points.back().x(), mapped_y(plot, range, 0.0));
            fill.lineTo(points.front().x(), mapped_y(plot, range, 0.0));
            fill.closeSubpath();
            painter.fillPath(fill, chart_fill_brush(
                layer, fill.boundingRect(), series.fill_color, element_opacity));
        }
        const double width = series.line_thickness >= 0.0
            ? series.line_thickness
            : std::max(0.25, layer.chart_line_thickness.evaluate(local_time));
        if (series.outline_enabled && layer.chart_outline_enabled) {
            painter.setBrush(Qt::NoBrush);
            if (layer.fill_type == 1) {
                painter.setPen(QPen(chart_fill_brush(
                    layer, plot, series.color, element_opacity), width,
                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
            } else {
                for (std::size_t i = 1; i < points.size(); ++i) {
                    painter.setPen(QPen(color(point_data[i - 1]->color,
                                              element_opacity), width,
                                        Qt::SolidLine, Qt::RoundCap,
                                        Qt::RoundJoin));
                    painter.drawLine(points[i - 1], points[i]);
                }
            }
            for (std::size_t i = 0; i < points.size(); ++i) {
                painter.setPen(QPen(color(point_data[i]->color, element_opacity),
                                    width));
                painter.setBrush(chart_fill_brush(
                    layer, QRectF(points[i].x() - width * 1.4,
                                  points[i].y() - width * 1.4,
                                  width * 2.8, width * 2.8),
                    point_data[i]->fill_color, element_opacity));
                painter.drawEllipse(points[i], width * 1.4, width * 1.4);
            }
        }
    }
    painter.restore();
    if (!layer.chart_data_labels_enabled || label_opacity <= 0.0001)
        return;
    painter.setPen(color(layer.chart_label_color, label_opacity));
    for (const Datum &datum : data.values) {
        auto category = std::find(data.categories.begin(), data.categories.end(),
                                  datum.category);
        if (category == data.categories.end())
            continue;
        const int index = static_cast<int>(category - data.categories.begin());
        const double x = categories == 1 ? plot.center().x()
            : plot.left() + plot.width() * index / (categories - 1);
        const double y = mapped_y(plot, range, datum.value);
        painter.drawText(QRectF(x - 50.0, y - 24.0, 100.0, 20.0),
                         Qt::AlignCenter, formatted_number(layer, datum.value));
    }
}

void draw_pie(QPainter &painter, const Layer &layer, const QRectF &plot,
              const DataSet &data, double element_opacity,
              double label_opacity, double progress, bool donut)
{
    if (data.values.empty())
        return;
    const std::string first_series = data.series.empty() ? std::string()
        : (data.series.front().id.empty() ? data.series.front().name
                                         : data.series.front().id);
    std::vector<const Datum *> values;
    double total = 0.0;
    for (const Datum &datum : data.values) {
        if ((!first_series.empty() && datum.series_id != first_series) ||
            datum.value <= 0.0)
            continue;
        values.push_back(&datum);
        total += datum.value;
    }
    if (total <= 0.0)
        return;
    const double diameter = std::max(1.0, std::min(plot.width(), plot.height()));
    const QRectF pie(plot.center().x() - diameter * 0.5,
                     plot.center().y() - diameter * 0.5, diameter, diameter);
    double start = 90.0;
    for (const Datum *datum : values) {
        const double span = 360.0 * datum->value / total * progress;
        QPainterPath segment;
        segment.moveTo(pie.center());
        segment.arcTo(pie, start, -span);
        segment.closeSubpath();
        painter.setPen(layer.chart_outline_enabled && datum->outline_enabled
            ? QPen(color(layer.chart_background_enabled
                             ? layer.chart_background_color : 0xFF202020u,
                         element_opacity), 1.0)
            : QPen(Qt::NoPen));
        painter.setBrush(datum->fill_enabled && layer.chart_fill_enabled
            ? chart_fill_brush(layer, segment.boundingRect(), datum->color,
                               element_opacity)
            : QBrush(Qt::NoBrush));
        painter.drawPath(segment);
        if (layer.chart_data_labels_enabled && label_opacity > 0.0001 && span > 8.0) {
            const double radians = (start - span * 0.5) * 3.14159265358979323846 / 180.0;
            const double radius = diameter * (donut ? 0.38 : 0.34);
            const QPointF point(pie.center().x() + std::cos(radians) * radius,
                                pie.center().y() - std::sin(radians) * radius);
            painter.setPen(color(layer.chart_label_color, label_opacity));
            painter.drawText(QRectF(point.x() - 55.0, point.y() - 12.0, 110.0, 24.0),
                             Qt::AlignCenter, formatted_number(layer, datum->value));
        }
        start -= span;
    }
    if (donut) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(layer.chart_background_enabled
            ? QBrush(color(layer.chart_background_color))
            : QBrush(QColor(Qt::transparent)));
        painter.setCompositionMode(layer.chart_background_enabled
            ? QPainter::CompositionMode_SourceOver
            : QPainter::CompositionMode_Clear);
        painter.drawEllipse(pie.center(), diameter * 0.24, diameter * 0.24);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}

void draw_scatter(QPainter &painter, const Layer &layer, const QRectF &plot,
                  const Range &y_range, const DataSet &data,
                  double element_opacity, double label_opacity, double progress)
{
    Range x_range;
    int ordinal = 0;
    for (const Datum &datum : data.values) {
        const double x = datum.has_secondary_value ? datum.secondary_value : ordinal++;
        x_range.minimum = std::min(x_range.minimum, x);
        x_range.maximum = std::max(x_range.maximum, x);
    }
    if (std::abs(x_range.maximum - x_range.minimum) < 1.0e-9)
        x_range.maximum = x_range.minimum + 1.0;
    ordinal = 0;
    for (const Datum &datum : data.values) {
        const double x_value = datum.has_secondary_value ? datum.secondary_value : ordinal++;
        const QPointF point(
            plot.left() + (x_value - x_range.minimum) /
                (x_range.maximum - x_range.minimum) * plot.width(),
            mapped_y(plot, y_range, datum.value));
        painter.setPen(datum.outline_enabled && layer.chart_outline_enabled
            ? QPen(color(datum.color, element_opacity), 1.0)
            : QPen(Qt::NoPen));
        const QRectF marker(point.x() - 5.0 * progress,
                            point.y() - 5.0 * progress,
                            10.0 * progress, 10.0 * progress);
        painter.setBrush(datum.fill_enabled && layer.chart_fill_enabled
            ? chart_fill_brush(layer, marker, datum.fill_color, element_opacity)
            : QBrush(Qt::NoBrush));
        painter.drawEllipse(point, 5.0 * progress, 5.0 * progress);
        if (layer.chart_data_labels_enabled && label_opacity > 0.0001) {
            painter.setPen(color(layer.chart_label_color, label_opacity));
            painter.drawText(QRectF(point.x() + 7.0, point.y() - 10.0, 100.0, 20.0),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             formatted_number(layer, datum.value));
        }
    }
}

} // namespace

void render(QPainter &painter, const Layer &layer, const QRectF &bounds,
            double local_time)
{
    if (layer.type != LayerType::Chart || bounds.isEmpty())
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    if (layer.chart_background_enabled)
        painter.fillRect(bounds, color(layer.chart_background_color));

    QFont font(QString::fromStdString(layer.chart_font_family));
    font.setPixelSize(std::max(6, static_cast<int>(std::lround(
        layer.chart_font_size.evaluate(local_time)))));
    painter.setFont(font);

    const double padding = std::max(0.0, layer.chart_padding.evaluate(local_time));
    QRectF content = bounds.adjusted(padding, padding, -padding, -padding);
    const double left = std::max(0.0, layer.chart_margin_left.evaluate(local_time));
    const double top = std::max(0.0, layer.chart_margin_top.evaluate(local_time));
    const double right = std::max(0.0, layer.chart_margin_right.evaluate(local_time));
    const double bottom = std::max(0.0, layer.chart_margin_bottom.evaluate(local_time));
    QRectF plot = content.adjusted(left, top, -right, -bottom);
    if (plot.width() < 2.0 || plot.height() < 2.0) {
        painter.restore();
        return;
    }

    const DataSet data = resolved_layer_data(layer, local_time);
    const double overall = std::clamp(
        layer.chart_animation_progress.evaluate(local_time), 0.0, 1.0);
    const double reveal = std::clamp(
        layer.chart_reveal_progress.evaluate(local_time), 0.0, 1.0) * overall;
    const double element_opacity = std::clamp(
        layer.chart_element_opacity.evaluate(local_time), 0.0, 1.0) * overall;
    const double label_opacity = std::clamp(
        layer.chart_label_opacity.evaluate(local_time), 0.0, 1.0) * overall;

    draw_legend(painter, layer, content, data, label_opacity);
    if (data.values.empty()) {
        painter.setPen(color(layer.chart_label_color, label_opacity * 0.65));
        painter.drawText(plot, Qt::AlignCenter, QStringLiteral("No chart data"));
        painter.restore();
        return;
    }

    const Range range = value_range(data);
    if (layer.chart_type != ChartType::Pie && layer.chart_type != ChartType::Donut)
        draw_grid_axes(painter, layer, plot, range, data, label_opacity);
    switch (layer.chart_type) {
    case ChartType::Bar:
        draw_vertical_bars(painter, layer, plot, range, data, element_opacity,
                           label_opacity, reveal, local_time);
        break;
    case ChartType::HorizontalBar:
        draw_horizontal_bars(painter, layer, plot, range, data, element_opacity,
                             label_opacity, reveal, local_time);
        break;
    case ChartType::Line:
        draw_line_area(painter, layer, plot, range, data, element_opacity,
                       label_opacity, reveal, false, local_time);
        break;
    case ChartType::Area:
        draw_line_area(painter, layer, plot, range, data, element_opacity,
                       label_opacity, reveal, true, local_time);
        break;
    case ChartType::Pie:
        draw_pie(painter, layer, plot, data, element_opacity, label_opacity,
                 reveal, false);
        break;
    case ChartType::Donut:
        draw_pie(painter, layer, plot, data, element_opacity, label_opacity,
                 reveal, true);
        break;
    case ChartType::Scatter:
        draw_scatter(painter, layer, plot, range, data, element_opacity,
                     label_opacity, reveal);
        break;
    }
    painter.restore();
}

} // namespace fxm::chart
