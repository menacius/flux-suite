#include "stroke-path-geometry.h"

#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

namespace fxm {
namespace {

constexpr double kEpsilon = 1.0e-7;

struct Polyline {
    std::vector<QPointF> points;
    bool closed = false;
    std::size_t owner = 0;
};

bool finite_point(const QPointF &p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

double length(const QPointF &v)
{
    return std::hypot(v.x(), v.y());
}

QPointF normalized(const QPointF &v)
{
    const double l = length(v);
    return l > kEpsilon ? QPointF(v.x() / l, v.y() / l) : QPointF();
}

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

double point_line_distance(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double denominator = d.x() * d.x() + d.y() * d.y();
    if (denominator <= kEpsilon)
        return length(p - a);
    const double t = std::clamp(((p.x() - a.x()) * d.x() +
                                 (p.y() - a.y()) * d.y()) / denominator,
                                0.0, 1.0);
    return length(p - (a + d * t));
}

void flatten_cubic(const QPointF &p0, const QPointF &p1,
                   const QPointF &p2, const QPointF &p3,
                   double tolerance, int depth,
                   std::vector<QPointF> *output)
{
    if (!output)
        return;
    const double flatness = std::max(point_line_distance(p1, p0, p3),
                                     point_line_distance(p2, p0, p3));
    if (depth >= 18 || flatness <= tolerance) {
        if (finite_point(p3))
            output->push_back(p3);
        return;
    }

    const QPointF p01 = (p0 + p1) * 0.5;
    const QPointF p12 = (p1 + p2) * 0.5;
    const QPointF p23 = (p2 + p3) * 0.5;
    const QPointF p012 = (p01 + p12) * 0.5;
    const QPointF p123 = (p12 + p23) * 0.5;
    const QPointF p0123 = (p012 + p123) * 0.5;
    flatten_cubic(p0, p01, p012, p0123, tolerance, depth + 1, output);
    flatten_cubic(p0123, p123, p23, p3, tolerance, depth + 1, output);
}

void finalize_polyline(Polyline *polyline, std::vector<Polyline> *result)
{
    if (!polyline || !result)
        return;
    auto &points = polyline->points;
    points.erase(std::unique(points.begin(), points.end(), [](const QPointF &a, const QPointF &b) {
        return length(a - b) <= kEpsilon;
    }), points.end());
    if (points.size() >= 3 && length(points.front() - points.back()) <= 1.0e-5) {
        points.pop_back();
        polyline->closed = true;
    }
    if (points.size() >= 2)
        result->push_back(*polyline);
    const std::size_t owner = polyline->owner;
    *polyline = Polyline{};
    polyline->owner = owner;
}

std::vector<Polyline> flatten_path(const QPainterPath &path, double tolerance,
                                   std::size_t owner)
{
    std::vector<Polyline> result;
    Polyline current;
    current.owner = owner;
    QPointF cursor;
    const double safe_tolerance = std::clamp(tolerance, 0.01, 8.0);

    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        const QPointF p(element.x, element.y);
        if (element.isMoveTo()) {
            finalize_polyline(&current, &result);
            if (finite_point(p)) {
                current.points.push_back(p);
                cursor = p;
            }
        } else if (element.isLineTo()) {
            if (current.points.empty())
                current.points.push_back(cursor);
            if (finite_point(p)) {
                current.points.push_back(p);
                cursor = p;
            }
        } else if (element.type == QPainterPath::CurveToElement &&
                   i + 2 < path.elementCount()) {
            const auto c2e = path.elementAt(i + 1);
            const auto ende = path.elementAt(i + 2);
            const QPointF c1(element.x, element.y);
            const QPointF c2(c2e.x, c2e.y);
            const QPointF end(ende.x, ende.y);
            if (current.points.empty())
                current.points.push_back(cursor);
            if (finite_point(c1) && finite_point(c2) && finite_point(end)) {
                flatten_cubic(cursor, c1, c2, end, safe_tolerance, 0,
                              &current.points);
                cursor = end;
            }
            i += 2;
        }
    }
    finalize_polyline(&current, &result);
    return result;
}

std::vector<Polyline> flatten_sources(const std::vector<QPainterPath> &sources,
                                      double tolerance)
{
    std::vector<Polyline> result;
    for (std::size_t owner = 0; owner < sources.size(); ++owner) {
        std::vector<Polyline> flattened = flatten_path(sources[owner], tolerance, owner);
        result.insert(result.end(), std::make_move_iterator(flattened.begin()),
                      std::make_move_iterator(flattened.end()));
    }
    return result;
}

double signed_area(const Polyline &line)
{
    if (!line.closed || line.points.size() < 3)
        return 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i < line.points.size(); ++i) {
        const QPointF &a = line.points[i];
        const QPointF &b = line.points[(i + 1) % line.points.size()];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

bool infinite_line_intersection(const QPointF &p, const QPointF &r,
                                const QPointF &q, const QPointF &s,
                                QPointF *intersection)
{
    const double cross = r.x() * s.y() - r.y() * s.x();
    if (std::abs(cross) <= kEpsilon)
        return false;
    const QPointF qp = q - p;
    const double t = (qp.x() * s.y() - qp.y() * s.x()) / cross;
    const QPointF value = p + r * t;
    if (!finite_point(value))
        return false;
    if (intersection)
        *intersection = value;
    return true;
}

int closed_outward_left_sign(const Polyline &line, const QPainterPath &source,
                             double offset)
{
    const double probe = std::max(0.5, std::min(8.0, std::abs(offset) * 0.25 + 0.5));
    for (std::size_t i = 0; i < line.points.size(); ++i) {
        const QPointF a = line.points[i];
        const QPointF b = line.points[(i + 1) % line.points.size()];
        const QPointF d = normalized(b - a);
        if (length(d) <= kEpsilon)
            continue;
        const QPointF left(-d.y(), d.x());
        const QPointF mid = (a + b) * 0.5;
        const bool left_inside = source.contains(mid + left * probe);
        const bool right_inside = source.contains(mid - left * probe);
        if (left_inside != right_inside)
            return left_inside ? -1 : 1;
    }
    /* In Qt's coordinate system positive Y points down. A positive signed area
     * is visually clockwise, whose outside lies on the left. */
    return signed_area(line) >= 0.0 ? 1 : -1;
}

Polyline offset_polyline(const Polyline &input, const QPainterPath &source,
                         double offset)
{
    if (std::abs(offset) <= kEpsilon || input.points.size() < 2)
        return input;

    Polyline output;
    output.closed = input.closed;
    output.owner = input.owner;
    output.points.resize(input.points.size());
    const std::size_t count = input.points.size();
    const int closed_sign = input.closed
        ? closed_outward_left_sign(input, source, offset)
        : 1;

    auto direction = [&](std::size_t from, std::size_t to) {
        return normalized(input.points[to] - input.points[from]);
    };
    auto normal_for = [&](const QPointF &d) {
        return QPointF(-d.y(), d.x()) * (offset * closed_sign);
    };

    for (std::size_t i = 0; i < count; ++i) {
        const bool first_open = !input.closed && i == 0;
        const bool last_open = !input.closed && i + 1 == count;
        if (first_open) {
            output.points[i] = input.points[i] + normal_for(direction(0, 1));
            continue;
        }
        if (last_open) {
            output.points[i] = input.points[i] +
                               normal_for(direction(count - 2, count - 1));
            continue;
        }

        const std::size_t previous = i == 0 ? count - 1 : i - 1;
        const std::size_t next = (i + 1) % count;
        const QPointF d0 = direction(previous, i);
        const QPointF d1 = direction(i, next);
        const QPointF n0 = normal_for(d0);
        const QPointF n1 = normal_for(d1);
        QPointF candidate;
        const bool intersects = infinite_line_intersection(
            input.points[i] + n0, d0, input.points[i] + n1, d1, &candidate);
        const double max_miter = std::max(8.0, std::abs(offset) * 8.0 + 2.0);
        if (!intersects || length(candidate - input.points[i]) > max_miter)
            candidate = input.points[i] + (n0 + n1) * 0.5;
        output.points[i] = finite_point(candidate) ? candidate : input.points[i];
    }
    return output;
}

double polyline_length(const Polyline &line)
{
    if (line.points.size() < 2)
        return 0.0;
    double total = 0.0;
    const std::size_t segment_count = line.closed
        ? line.points.size()
        : line.points.size() - 1;
    for (std::size_t i = 0; i < segment_count; ++i)
        total += length(line.points[(i + 1) % line.points.size()] - line.points[i]);
    return total;
}

void append_full_polyline(QPainterPath *path, const Polyline &line)
{
    if (!path || line.points.size() < 2)
        return;
    path->moveTo(line.points.front());
    for (std::size_t i = 1; i < line.points.size(); ++i)
        path->lineTo(line.points[i]);
    if (line.closed)
        path->closeSubpath();
}

void append_polyline_range(QPainterPath *path, const Polyline &line,
                           double range_start, double range_end)
{
    if (!path || range_end - range_start <= kEpsilon ||
        line.points.size() < 2)
        return;
    const std::size_t segment_count = line.closed
        ? line.points.size()
        : line.points.size() - 1;
    double cursor = 0.0;
    bool started = false;
    QPointF last;
    for (std::size_t i = 0; i < segment_count; ++i) {
        const QPointF a = line.points[i];
        const QPointF b = line.points[(i + 1) % line.points.size()];
        const double segment_length = length(b - a);
        if (segment_length <= kEpsilon)
            continue;
        const double segment_start = cursor;
        const double segment_end = cursor + segment_length;
        const double overlap_start = std::max(range_start, segment_start);
        const double overlap_end = std::min(range_end, segment_end);
        if (overlap_end - overlap_start > kEpsilon) {
            const QPointF from = lerp(
                a, b, (overlap_start - segment_start) / segment_length);
            const QPointF to = lerp(
                a, b, (overlap_end - segment_start) / segment_length);
            if (!started || length(from - last) > 1.0e-5) {
                path->moveTo(from);
                started = true;
            }
            path->lineTo(to);
            last = to;
        }
        cursor = segment_end;
        if (cursor >= range_end - kEpsilon)
            break;
    }
}

std::vector<std::pair<double, double>> normalized_intervals(
    double start_percent, double end_percent, double offset_degrees)
{
    const double start = std::clamp(start_percent, 0.0, 100.0);
    const double end = std::clamp(end_percent, 0.0, 100.0);
    if (std::abs(start - end) <= kEpsilon)
        return {};
    if (start <= kEpsilon && end >= 100.0 - kEpsilon)
        return {{0.0, 1.0}};

    const auto wrap01 = [](double value) {
        value = std::fmod(value, 1.0);
        return value < 0.0 ? value + 1.0 : value;
    };
    const double offset = offset_degrees / 360.0;
    const double s = wrap01(start / 100.0 + offset);
    const double e = wrap01(end / 100.0 + offset);
    if (std::abs(s - e) <= kEpsilon)
        return {};
    if (s < e)
        return {{s, e}};
    return {{s, 1.0}, {0.0, e}};
}

} // namespace

std::vector<QPainterPath> apply_stroke_offset_geometry_partitioned(
    const std::vector<QPainterPath> &sources, double offset,
    double flatten_tolerance)
{
    std::vector<QPainterPath> results(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i)
        results[i].setFillRule(sources[i].fillRule());
    if (sources.empty() || std::abs(offset) <= kEpsilon)
        return sources;

    std::vector<Polyline> lines = flatten_sources(sources, flatten_tolerance);
    for (Polyline &line : lines) {
        if (line.owner < sources.size())
            line = offset_polyline(line, sources[line.owner], offset);
    }
    for (const Polyline &line : lines) {
        if (line.owner < results.size())
            append_full_polyline(&results[line.owner], line);
    }
    return results;
}

QPainterPath apply_stroke_offset_geometry(const QPainterPath &source,
                                          double offset,
                                          double flatten_tolerance)
{
    const std::vector<QPainterPath> results =
        apply_stroke_offset_geometry_partitioned({source}, offset,
                                                 flatten_tolerance);
    return results.empty() ? QPainterPath{} : results.front();
}

std::vector<QPainterPath> apply_trim_paths_geometry_partitioned(
    const std::vector<QPainterPath> &sources,
    const TrimPathsGeometryOptions &options)
{
    std::vector<QPainterPath> results(sources.size());
    for (std::size_t i = 0; i < sources.size(); ++i)
        results[i].setFillRule(sources[i].fillRule());
    if (sources.empty())
        return results;

    const double clamped_start = std::clamp(options.start_percent, 0.0, 100.0);
    const double clamped_end = std::clamp(options.end_percent, 0.0, 100.0);
    if (clamped_start <= kEpsilon && clamped_end >= 100.0 - kEpsilon)
        return sources;

    std::vector<Polyline> lines = flatten_sources(sources,
                                                  options.flatten_tolerance);

    const auto intervals = normalized_intervals(options.start_percent,
                                                options.end_percent,
                                                options.trim_offset_degrees);
    if (intervals.empty())
        return results;

    if (intervals.size() == 1 && intervals.front().first <= kEpsilon &&
        intervals.front().second >= 1.0 - kEpsilon) {
        for (const Polyline &line : lines) {
            if (line.owner < results.size())
                append_full_polyline(&results[line.owner], line);
        }
        return results;
    }

    if (options.individually) {
        for (const Polyline &line : lines) {
            if (line.owner >= results.size())
                continue;
            const double total = polyline_length(line);
            if (total <= kEpsilon)
                continue;
            for (const auto &interval : intervals) {
                append_polyline_range(&results[line.owner], line,
                                      interval.first * total,
                                      interval.second * total);
            }
        }
        return results;
    }

    std::vector<double> lengths;
    lengths.reserve(lines.size());
    double grand_total = 0.0;
    for (const Polyline &line : lines) {
        const double value = polyline_length(line);
        lengths.push_back(value);
        grand_total += value;
    }
    if (grand_total <= kEpsilon)
        return results;

    for (const auto &interval : intervals) {
        const double global_start = interval.first * grand_total;
        const double global_end = interval.second * grand_total;
        double base = 0.0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const double local_start = std::max(0.0, global_start - base);
            const double local_end = std::min(lengths[i], global_end - base);
            if (local_end - local_start > kEpsilon &&
                lines[i].owner < results.size()) {
                append_polyline_range(&results[lines[i].owner], lines[i],
                                      local_start, local_end);
            }
            base += lengths[i];
            if (base >= global_end - kEpsilon)
                break;
        }
    }
    return results;
}

QPainterPath apply_trim_paths_geometry(const QPainterPath &source,
                                       const TrimPathsGeometryOptions &options)
{
    const std::vector<QPainterPath> results =
        apply_trim_paths_geometry_partitioned({source}, options);
    return results.empty() ? QPainterPath{} : results.front();
}

} // namespace fxm
