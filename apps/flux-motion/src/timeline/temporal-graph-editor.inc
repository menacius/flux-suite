/* AE-style temporal Graph Editor.  This module owns only timeline UI state;
 * all values are sampled and edited through TimelinePropertyRef so editor,
 * prerender/cache and OBS continue to share animation.cpp evaluation. */

void TimelineWidget::set_graph_editor_enabled(bool enabled)
{
    if (graph_editor_enabled_ == enabled) return;
    const double old_offset = timeline_pre_roll();
    graph_editor_enabled_ = enabled;
    const double new_offset = timeline_pre_roll();
    /* Graph Editor uses document time only. Preserve the on-screen position of
     * document time zero while entering or leaving the Stinger roll view. */
    scroll_x_ += (int)std::round((new_offset - old_offset) * pixels_per_sec_);
    clamp_scroll();
    drag_mode_ = DragMode::None;
    if (!enabled && graph_channel_mode_ != GraphChannelMode::All) {
        temporal_drag_snapshots_.clear();
        graph_drag_hit_ = {};
        graph_channel_mode_ = GraphChannelMode::All;
        emit graph_channel_mode_changed((int)graph_channel_mode_);
    }
    graph_fit_pending_ = true;
    notify_graph_channel_count();
    unsetCursor();
    update();
    notify_horizontal_view_changed();
    emit graph_editor_enabled_changed(enabled);
}

void TimelineWidget::set_graph_view_mode(int mode)
{
    const GraphViewMode next = mode == (int)GraphViewMode::Speed
        ? GraphViewMode::Speed : GraphViewMode::Value;
    if (graph_view_mode_ == next) return;
    graph_view_mode_ = next;
    graph_fit_pending_ = true;
    update();
    emit graph_view_mode_changed((int)graph_view_mode_);
}

int TimelineWidget::graph_channel_component() const
{
    if (graph_channel_mode_ == GraphChannelMode::All) return -1;
    if (graph_channel_mode_ == GraphChannelMode::Fourth) return 3;
    return static_cast<int>(graph_channel_mode_);
}

TimelineWidget::GraphChannelMode TimelineWidget::graph_mode_for_component(
    int component) const
{
    if (component == 3) return GraphChannelMode::Fourth;
    if (component >= 0 && component <= 2)
        return static_cast<GraphChannelMode>(component);
    return GraphChannelMode::All;
}

void TimelineWidget::set_graph_channel_mode(int mode)
{
    const int count = graph_channel_count();
    GraphChannelMode next = GraphChannelMode::All;
    const GraphChannelMode requested = mode == 4
        ? GraphChannelMode::Fourth
        : mode >= 0 && mode <= 2
            ? static_cast<GraphChannelMode>(mode)
            : GraphChannelMode::All;
    const int component = requested == GraphChannelMode::Fourth
        ? 3 : requested == GraphChannelMode::All
            ? -1 : static_cast<int>(requested);
    if (component >= 0 && component < count)
        next = requested;
    if (graph_channel_mode_ == next) return;
    graph_channel_mode_ = next;
    /* X/Y/Z/All are views of the same authored keyframe sequence. Switching
     * channels must not discard the user's keyframe selection or break a
     * multi-key edit merely because a different component is being inspected. */
    temporal_drag_snapshots_.clear();
    graph_drag_hit_ = {};
    drag_mode_ = DragMode::None;
    graph_fit_pending_ = true;
    update();
    emit graph_channel_mode_changed((int)graph_channel_mode_);
}

void TimelineWidget::select_graph_property(const std::string &owner_id,
                                           const std::string &property_name,
                                           int channel_mode)
{
    set_graph_target(owner_id, property_name);
    set_graph_channel_mode(channel_mode);
}

QRect TimelineWidget::graph_rect() const
{
    return QRect(0, ruler_height(), width(), std::max(0, height() - ruler_height()));
}

bool TimelineWidget::graph_ref_property(const KeyframeRef &ref,
                                        TimelinePropertyRef *prop,
                                        std::shared_ptr<Layer> *layer) const
{
    if (!title_) return false;
    TimelinePropertyRef found = const_cast<TimelineWidget *>(this)
        ->find_timeline_property(ref.layer_id, ref.prop_name);
    if (!found || !found.graph_eligible()) return false;
    const int count = found.graph_channel_count();
    if (count > 1) {
        if (graph_channel_mode_ == GraphChannelMode::All)
            found = found.graph_all_channels();
        else
            found = found.graph_channel(std::clamp(graph_channel_component(), 0, count - 1));
    }
    if (ref.index < 0 || ref.index >= (int)found.keyframe_count()) return false;
    if (prop) *prop = found;
    if (layer) {
        const QString label = timeline_camera_from_owner(*title_, ref.layer_id)
            ? QString::fromStdString(timeline_camera_from_owner(*title_, ref.layer_id)->name)
            : ref.layer_id == kTitleCameraSwitchOwner
                ? QStringLiteral("Camera Switches") : QString();
        *layer = timeline_owner_proxy(*title_, ref.layer_id, label);
    }
    return true;
}

void TimelineWidget::set_graph_target(const std::string &owner_id,
                                      const std::string &property_name)
{
    if (graph_target_owner_id_ == owner_id &&
        graph_target_property_name_ == property_name) {
        notify_graph_channel_count();
        return;
    }
    graph_target_owner_id_ = owner_id;
    graph_target_property_name_ = property_name;
    selected_keyframes_.clear();
    graph_channel_mode_ = GraphChannelMode::All;
    graph_fit_pending_ = true;
    notify_graph_channel_count();
    emit graph_channel_mode_changed((int)graph_channel_mode_);
    update();
}

TimelinePropertyRef TimelineWidget::active_graph_base_property(
    std::shared_ptr<Layer> *layer) const
{
    if (!title_) return {};
    if (!graph_target_owner_id_.empty() &&
        !graph_target_property_name_.empty()) {
        TimelinePropertyRef target = timeline_property_for_owner(
            *title_, graph_target_owner_id_, graph_target_property_name_);
        if (target && target.graph_eligible()) {
            if (layer) {
                const TitleCamera *camera = timeline_camera_from_owner(
                    *title_, graph_target_owner_id_);
                const QString label = camera
                    ? QString::fromStdString(camera->name)
                    : graph_target_owner_id_ == kTitleCameraSwitchOwner
                        ? QStringLiteral("Camera Switches") : QString();
                *layer = timeline_owner_proxy(*title_, graph_target_owner_id_, label);
            }
            return target;
        }
    }
    auto choose_layer = [&](const std::string &id) -> TimelinePropertyRef {
        auto owner = title_->find_layer(id);
        if (!owner) return {};
        for (TimelinePropertyRef prop : timeline_properties(*owner)) {
            if (prop && prop.is_animated() && prop.keyframe_count() > 0) {
                if (layer) *layer = owner;
                return prop;
            }
        }
        return {};
    };
    if (!sel_layer_id_.empty()) {
        TimelinePropertyRef prop = choose_layer(sel_layer_id_);
        if (prop) return prop;
    }
    for (const std::string &id : selected_layer_ids_) {
        TimelinePropertyRef prop = choose_layer(id);
        if (prop) return prop;
    }
    return {};
}

int TimelineWidget::graph_channel_count() const
{
    TimelinePropertyRef base = active_graph_base_property();
    return base ? base.graph_channel_count() : 0;
}

QString TimelineWidget::graph_channel_label(int component) const
{
    TimelinePropertyRef base = active_graph_base_property();
    if (!base) {
        static const char *fallback[] = {"X", "Y", "Z", "W"};
        return QString::fromLatin1(fallback[std::clamp(component, 0, 3)]);
    }
    return timeline_property_channel_label(base, component);
}

QColor TimelineWidget::graph_channel_color(int component) const
{
    const int safe_component = std::clamp(component, 0, 3);
    const QString label = graph_channel_label(safe_component).trimmed().toUpper();
    if (label == QStringLiteral("A")) return QColor(205, 205, 205);
    if (label == QStringLiteral("R")) return QColor(224, 82, 89);
    if (label == QStringLiteral("G")) return QColor(91, 190, 105);
    if (label == QStringLiteral("B")) return QColor(75, 139, 230);
    if (safe_component == 0) return QColor(224, 82, 89);
    if (safe_component == 1) return QColor(91, 190, 105);
    if (safe_component == 2) return QColor(75, 139, 230);
    return QColor(190, 110, 225);
}

void TimelineWidget::notify_graph_channel_count()
{
    const int count = graph_channel_count();
    if (count <= 1 && graph_channel_mode_ != GraphChannelMode::All) {
        graph_channel_mode_ = GraphChannelMode::All;
        emit graph_channel_mode_changed((int)graph_channel_mode_);
    } else if (count > 1 && graph_channel_mode_ != GraphChannelMode::All &&
               (graph_channel_component() < 0 ||
                graph_channel_component() >= count)) {
        graph_channel_mode_ = GraphChannelMode::All;
        emit graph_channel_mode_changed((int)graph_channel_mode_);
    }
    emit graph_channel_count_changed(count);
}

TimelinePropertyRef TimelineWidget::active_graph_property(
    std::shared_ptr<Layer> *layer) const
{
    TimelinePropertyRef base = active_graph_base_property(layer);
    if (!base) return {};
    const int count = base.graph_channel_count();
    if (count <= 1) return base;
    if (graph_channel_mode_ == GraphChannelMode::All)
        return base.graph_all_channels();
    return base.graph_channel(std::clamp(graph_channel_component(), 0, count - 1));
}

std::vector<TimelinePropertyRef> TimelineWidget::active_graph_channels(
    std::shared_ptr<Layer> *layer) const
{
    TimelinePropertyRef base = active_graph_base_property(layer);
    if (!base) return {};
    const int count = base.graph_channel_count();
    if (count <= 1) return {base};
    if (graph_channel_mode_ != GraphChannelMode::All)
        return {base.graph_channel(std::clamp(graph_channel_component(), 0, count - 1))};
    std::vector<TimelinePropertyRef> channels;
    channels.reserve(static_cast<size_t>(count));
    for (int component = 0; component < count; ++component)
        channels.push_back(base.graph_channel(component));
    return channels;
}

double TimelineWidget::graph_value_to_y(double value) const
{
    const QRect area = graph_rect().adjusted(0, 8, 0, -8);
    const double span = std::max(1e-12, graph_value_max_ - graph_value_min_);
    return area.bottom() - (value - graph_value_min_) / span * area.height();
}

double TimelineWidget::graph_y_to_value(double y) const
{
    const QRect area = graph_rect().adjusted(0, 8, 0, -8);
    const double span = std::max(1e-12, graph_value_max_ - graph_value_min_);
    return graph_value_min_ + (area.bottom() - y) / std::max(1, area.height()) * span;
}

double TimelineWidget::graph_x_to_time(double x) const
{
    return (x + scroll_x_) / std::max(1e-9, pixels_per_sec_);
}

void TimelineWidget::update_graph_fit(bool selection_only)
{
    std::shared_ptr<Layer> owner;
    TimelinePropertyRef prop = active_graph_property(&owner);
    const std::vector<TimelinePropertyRef> channels = active_graph_channels();
    if (!prop || channels.empty() || !title_ || !owner) {
        graph_value_min_ = -1.0;
        graph_value_max_ = 1.0;
        graph_fit_pending_ = false;
        return;
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    auto include = [&](double value) {
        if (!std::isfinite(value)) return;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    };
    auto include_channel_value = [&](const TimelinePropertyRef &channel,
                                     double local_time) {
        include(graph_view_mode_ == GraphViewMode::Speed
            ? channel.graph_speed(local_time) : channel.graph_value(local_time));
    };

    std::vector<int> selected_indices;
    for (const KeyframeRef &ref : selected_keyframes_) {
        if (ref.layer_id == owner->id && ref.prop_name == prop.name())
            selected_indices.push_back(ref.index);
    }
    if (selection_only && !selected_indices.empty()) {
        for (int index : selected_indices) {
            if (index < 0 || index >= (int)prop.keyframe_count()) continue;
            const double local_time = prop.keyframe_time((size_t)index);
            for (const TimelinePropertyRef &channel : channels)
                include_channel_value(channel, local_time);
            if (graph_view_mode_ == GraphViewMode::Speed) {
                include(prop.keyframe_temporal_speed((size_t)index, true));
                include(prop.keyframe_temporal_speed((size_t)index, false));
            }
        }
    } else {
        const int samples = std::clamp(width() / 3, 64, 1024);
        const double local_duration = std::max(0.0, owner->out_time - owner->in_time);
        for (int i = 0; i <= samples; ++i) {
            const double local_time = local_duration * (double)i / (double)samples;
            for (const TimelinePropertyRef &channel : channels)
                include_channel_value(channel, local_time);
        }
        for (size_t i = 0; i < prop.keyframe_count(); ++i) {
            const double key_time = prop.keyframe_time(i);
            for (const TimelinePropertyRef &channel : channels)
                include_channel_value(channel, key_time);
            if (graph_view_mode_ == GraphViewMode::Speed) {
                include(prop.keyframe_temporal_speed(i, true));
                include(prop.keyframe_temporal_speed(i, false));
            }
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        minimum = -1.0;
        maximum = 1.0;
    }
    if (std::abs(maximum - minimum) < 1e-9) {
        const double pad = std::max(1.0, std::abs(maximum) * 0.1);
        minimum -= pad;
        maximum += pad;
    } else {
        const double pad = (maximum - minimum) * 0.08;
        minimum -= pad;
        maximum += pad;
    }
    graph_value_min_ = minimum;
    graph_value_max_ = maximum;
    graph_fit_pending_ = false;
}

void TimelineWidget::fit_graph_time_range(bool selection_only)
{
    std::shared_ptr<Layer> owner;
    TimelinePropertyRef prop = active_graph_property(&owner);
    if (!prop || !owner || !title_) return;
    double first = std::numeric_limits<double>::infinity();
    double last = -std::numeric_limits<double>::infinity();
    auto include_time = [&](double local_time) {
        const double global_time = owner->in_time + local_time;
        first = std::min(first, global_time);
        last = std::max(last, global_time);
    };
    if (selection_only) {
        for (const KeyframeRef &ref : selected_keyframes_) {
            if (ref.layer_id != owner->id || ref.prop_name != prop.name() ||
                ref.index < 0 || ref.index >= (int)prop.keyframe_count()) continue;
            include_time(prop.keyframe_time((size_t)ref.index));
        }
    }
    if (!std::isfinite(first) || !std::isfinite(last)) {
        for (size_t index = 0; index < prop.keyframe_count(); ++index)
            include_time(prop.keyframe_time(index));
    }
    if (!std::isfinite(first) || !std::isfinite(last)) {
        first = owner->in_time;
        last = owner->out_time;
    }
    const double minimum_span = std::max(obs_frame_duration() * 4.0, 0.05);
    if (last - first < minimum_span) {
        const double center = 0.5 * (first + last);
        first = center - minimum_span * 0.5;
        last = center + minimum_span * 0.5;
    }
    first = std::max(0.0, first);
    last = std::min(title_->duration, std::max(first + minimum_span, last));
    const int margin = 28;
    const double usable_width = std::max(1, width() - margin * 2);
    pixels_per_sec_ = std::clamp(usable_width / std::max(minimum_span, last - first),
                                 5.0, 1200.0);
    scroll_x_ = (int)std::round(first * pixels_per_sec_) - margin;
    clamp_scroll();
    emit zoom_percent_changed(zoom_percent());
    notify_horizontal_view_changed();
}

void TimelineWidget::fit_graph_to_view()
{
    update_graph_fit(false);
    fit_graph_time_range(false);
    update();
}

void TimelineWidget::fit_graph_selection()
{
    update_graph_fit(true);
    fit_graph_time_range(true);
    update();
}

static QString temporal_mode_label(TemporalInterpolationMode mode)
{
    switch (mode) {
    case TemporalInterpolationMode::Linear: return fxm_tr("OBSTitles.Linear");
    case TemporalInterpolationMode::Hold: return fxm_tr("OBSTitles.Hold");
    case TemporalInterpolationMode::AutoBezier: return fxm_tr("OBSTitles.TemporalAutoBezier");
    case TemporalInterpolationMode::ContinuousBezier: return fxm_tr("OBSTitles.TemporalContinuousBezier");
    case TemporalInterpolationMode::ManualBezier: return fxm_tr("OBSTitles.TemporalManualBezier");
    }
    return fxm_tr("OBSTitles.Linear");
}

void TimelineWidget::paint_graph_editor(QPainter &p, const QRect &dirty)
{
    if (graph_fit_pending_) update_graph_fit(false);
    const QPalette pal = palette();
    const QColor background = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor grid = pal.color(QPalette::Mid);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor curve = pal.color(QPalette::Link);
    const QColor handle = pal.color(QPalette::BrightText);
    const QColor ruler = background.lightness() < 128 ? background.darker(116)
                                                      : background.darker(106);
    p.fillRect(dirty, background);
    const QRect header(0, 0, width(), ruler_height());
    p.fillRect(header.intersected(dirty), ruler);
    p.setPen(grid);
    p.drawLine(0, ruler_height() - 1, width(), ruler_height() - 1);

    TimelinePropertyRef prop;
    std::shared_ptr<Layer> owner;
    prop = active_graph_property(&owner);
    const std::vector<TimelinePropertyRef> channels = active_graph_channels();

    const QRect area = graph_rect();
    if (area.isEmpty()) return;

    /* Keep the ordinary time ruler visible in Graph Editor mode.  The graph
     * body uses its own clip, so ruler labels must be painted before that clip
     * is installed (the old code drew them outside the active clip). */
    const double seconds_per_major = std::max(1.0 / obs_frame_rate(),
        std::pow(10.0, std::floor(std::log10(std::max(1e-6, 80.0 / pixels_per_sec_)))));
    const double first_time = std::max(0.0, graph_x_to_time(0.0));
    double first_tick = std::floor(first_time / seconds_per_major) * seconds_per_major;
    const QFontMetrics ruler_metrics(p.font());
    int last_label_right = std::numeric_limits<int>::min();
    p.save();
    p.setClipRect(header.intersected(dirty));
    for (double tick = first_tick; tick <= (title_ ? title_->duration : 0.0);
         tick += seconds_per_major) {
        const int x = time_to_x(tick);
        if (x < 0 || x > width()) continue;
        p.setPen(QPen(grid, 0.8));
        p.drawLine(x, ruler_height() - 12, x, ruler_height() - 2);
        const QString label = format_timecode(tick);
        const int label_width = ruler_metrics.horizontalAdvance(label);
        const int label_left = x + 3;
        if (label_left > last_label_right + 6) {
            p.setPen(text);
            p.drawText(QRect(label_left, 18, label_width + 6, ruler_height() - 19),
                       Qt::AlignLeft | Qt::AlignVCenter, label);
            last_label_right = label_left + label_width;
        }
    }
    const int ruler_playhead_x = time_to_x(playhead_);
    const QColor playhead_color = TitlePreferences::timeline_color(
        TitlePreferences::TimelineColorRole::Current);
    p.setPen(QPen(playhead_color, 1.2));
    p.drawLine(ruler_playhead_x, 18, ruler_playhead_x, ruler_height() - 1);
    p.setBrush(playhead_color);
    p.setPen(Qt::NoPen);
    QPolygon playhead_marker;
    playhead_marker << QPoint(ruler_playhead_x - 5, 18)
                    << QPoint(ruler_playhead_x + 5, 18)
                    << QPoint(ruler_playhead_x, 25);
    p.drawPolygon(playhead_marker);
    p.restore();

    if (!prop) {
        p.setPen(text);
        p.drawText(graph_rect(), Qt::AlignCenter, fxm_tr("OBSTitles.GraphEditorNoProperty"));
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, true);
    p.save();
    p.setClipRect(area.intersected(dirty));

    for (int line = 0; line <= 5; ++line) {
        const double ratio = (double)line / 5.0;
        const int y = area.top() + (int)std::round(ratio * area.height());
        const double value = graph_value_max_ - ratio * (graph_value_max_ - graph_value_min_);
        p.setPen(QPen(grid, line == 5 ? 1.0 : 0.5));
        p.drawLine(area.left(), y, area.right(), y);
        p.setPen(text);
        p.drawText(area.left() + 4, y - 2, QString::number(value, 'g', 5));
    }
    if (graph_view_mode_ == GraphViewMode::Speed && graph_value_min_ <= 0.0 && graph_value_max_ >= 0.0) {
        p.setPen(QPen(grid.lighter(140), 1.2));
        const int zero_y = (int)std::round(graph_value_to_y(0.0));
        p.drawLine(area.left(), zero_y, area.right(), zero_y);
    }

    for (double tick = first_tick; tick <= (title_ ? title_->duration : 0.0);
         tick += seconds_per_major) {
        const int x = time_to_x(tick);
        if (x < area.left() || x > area.right()) continue;
        p.setPen(QPen(grid, 0.5));
        p.drawLine(x, area.top(), x, area.bottom());
    }

    auto channel_color = [&](int visible_component) {
        const int total_channels = graph_channel_count();
        if (total_channels <= 1) return curve;
        const int authored_component = graph_channel_mode_ == GraphChannelMode::All
            ? visible_component : std::clamp(graph_channel_component(), 0, total_channels - 1);
        return graph_channel_color(authored_component);
    };

    for (int component = 0; component < (int)channels.size(); ++component) {
        const TimelinePropertyRef &channel = channels[static_cast<size_t>(component)];
        QPainterPath path;
        bool started = false;
        for (int x = area.left(); x <= area.right(); x += 2) {
            const double global_time = std::clamp(graph_x_to_time(x), 0.0, title_->duration);
            if (global_time < owner->in_time || global_time > owner->out_time) {
                started = false;
                continue;
            }
            const double local_time = global_time - owner->in_time;
            const double value = graph_view_mode_ == GraphViewMode::Speed
                ? channel.graph_speed(local_time) : channel.graph_value(local_time);
            if (!std::isfinite(value)) { started = false; continue; }
            const QPointF point(x, graph_value_to_y(value));
            if (!started) { path.moveTo(point); started = true; }
            else path.lineTo(point);
        }
        p.setPen(QPen(channel_color(component), 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    const int handle_component = channels.empty() ? 0 : std::clamp(
        graph_drag_hit_.channel >= 0 ? graph_drag_hit_.channel : 0,
        0, (int)channels.size() - 1);
    for (size_t index = 0; index < prop.keyframe_count(); ++index) {
        const double time = prop.keyframe_time(index);
        const double global_time = owner->in_time + time;
        const KeyframeRef ref{owner ? owner->id : std::string(), prop.name(), (int)index};
        const bool selected = selected_keyframes_.count(ref) != 0;

        for (int component = 0; component < (int)channels.size(); ++component) {
            const TimelinePropertyRef &channel = channels[static_cast<size_t>(component)];
            const double key_value = channel.graph_value(time);
            const double in_speed = channel.graph_speed(time - 1e-6);
            const double out_speed = channel.graph_speed(time + 1e-6);
            const QColor component_color = channel_color(component);
            p.setPen(QPen(selected ? highlight : component_color.darker(140), 1.0));
            p.setBrush(selected ? highlight : background);
            const QPointF value_vertex(time_to_x(global_time), graph_value_to_y(key_value));
            const QPointF incoming_vertex(time_to_x(global_time), graph_value_to_y(in_speed));
            const QPointF outgoing_vertex(time_to_x(global_time), graph_value_to_y(out_speed));
            if (graph_view_mode_ == GraphViewMode::Value) {
                p.drawEllipse(value_vertex, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
            } else {
                if (index > 0)
                    p.drawEllipse(incoming_vertex, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
                if (index + 1 < prop.keyframe_count() &&
                    (index == 0 || std::abs(out_speed - in_speed) > 1e-9))
                    p.drawEllipse(outgoing_vertex, selected ? 4.5 : 3.5, selected ? 4.5 : 3.5);
            }

            if (!selected || component != handle_component) continue;
            if (index > 0) {
                const double influence = prop.keyframe_temporal_influence(index, true) / 100.0;
                const double seconds = (time - prop.keyframe_time(index - 1)) * influence;
                const double handle_value = graph_view_mode_ == GraphViewMode::Speed
                    ? in_speed : key_value - in_speed * seconds;
                const QPointF hp(time_to_x(global_time - seconds), graph_value_to_y(handle_value));
                const QPointF base = graph_view_mode_ == GraphViewMode::Speed
                    ? incoming_vertex : value_vertex;
                p.setPen(QPen(handle, 1.0)); p.drawLine(base, hp);
                p.setBrush(highlight); p.drawRect(QRectF(hp.x() - 3, hp.y() - 3, 6, 6));
            }
            if (index + 1 < prop.keyframe_count()) {
                const double influence = prop.keyframe_temporal_influence(index, false) / 100.0;
                const double seconds = (prop.keyframe_time(index + 1) - time) * influence;
                const double handle_value = graph_view_mode_ == GraphViewMode::Speed
                    ? out_speed : key_value + out_speed * seconds;
                const QPointF hp(time_to_x(global_time + seconds), graph_value_to_y(handle_value));
                const QPointF base = graph_view_mode_ == GraphViewMode::Speed
                    ? outgoing_vertex : value_vertex;
                p.setPen(QPen(handle, 1.0)); p.drawLine(base, hp);
                p.setBrush(highlight); p.drawRect(QRectF(hp.x() - 3, hp.y() - 3, 6, 6));
            }
        }
    }

    const int playhead_x = time_to_x(playhead_);
    p.setPen(QPen(TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Current), 1.2));
    p.drawLine(playhead_x, area.top(), playhead_x, area.bottom());
    if (playhead_ >= owner->in_time && playhead_ <= owner->out_time) {
        const double local_playhead = playhead_ - owner->in_time;
        for (int component = 0; component < (int)channels.size(); ++component) {
            const TimelinePropertyRef &channel = channels[static_cast<size_t>(component)];
            const double current = graph_view_mode_ == GraphViewMode::Speed
                ? channel.graph_speed(local_playhead) : channel.graph_value(local_playhead);
            p.setBrush(channel_color(component));
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPointF(playhead_x, graph_value_to_y(current)), 3.5, 3.5);
        }
    }

    if (drag_mode_ == DragMode::GraphMarquee) {
        p.setPen(QPen(highlight, 1.0, Qt::DashLine));
        QColor fill = highlight; fill.setAlpha(40); p.setBrush(fill);
        p.drawRect(QRect(graph_drag_start_, graph_marquee_current_).normalized());
    }
    p.restore();

    p.setPen(text);
    const QString view = graph_view_mode_ == GraphViewMode::Speed
        ? fxm_tr("OBSTitles.SpeedGraph") : fxm_tr("OBSTitles.ValueGraph");
    const int channel_count = graph_channel_count();
    TimelinePropertyRef base_channel_property = active_graph_base_property();
    const QString channel_name = channel_count <= 1 ? QString()
        : graph_channel_mode_ == GraphChannelMode::All ? QStringLiteral("All")
        : timeline_property_channel_label(base_channel_property,
                                          graph_channel_component());
    QString graph_title = QStringLiteral("%1 · %2 · %3").arg(view,
        owner ? QString::fromStdString(owner->name) : QString(),
        property_label(prop.name()));
    if (!channel_name.isEmpty()) graph_title += QStringLiteral(" · %1").arg(channel_name);
    p.drawText(8, 0, width() - 16, 18, Qt::AlignVCenter | Qt::AlignLeft, graph_title);
    if (prop.name() == "video_source_time" && graph_view_mode_ == GraphViewMode::Speed &&
        playhead_ >= owner->in_time && playhead_ <= owner->out_time) {
        const double local_playhead = playhead_ - owner->in_time;
        const double source_speed = prop.graph_speed(local_playhead);
        if (source_speed < -1e-6) {
            /* Clear indication when source time moves backward. */
            p.drawText(8, 18, width() - 16, 18, Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("◀ Reverse source time"));
        }
    }
}

TimelineWidget::GraphHit TimelineWidget::graph_hit_test(const QPoint &pos) const
{
    GraphHit result;
    if (!graph_editor_enabled_ || !graph_rect().contains(pos)) return result;
    std::shared_ptr<Layer> owner;
    TimelinePropertyRef prop = active_graph_property(&owner);
    const std::vector<TimelinePropertyRef> channels = active_graph_channels();
    if (!prop || !owner || channels.empty()) return result;
    const double radius2 = 64.0;
    auto consider = [&](GraphHitType type, const KeyframeRef &ref,
                        const QPointF &point, int channel) {
        const double dx = point.x() - pos.x(), dy = point.y() - pos.y();
        if (dx * dx + dy * dy <= radius2) {
            result.type = type;
            result.ref = ref;
            result.point = point;
            result.channel = channel;
            return true;
        }
        return false;
    };
    for (int index = (int)prop.keyframe_count() - 1; index >= 0; --index) {
        const double time = prop.keyframe_time((size_t)index);
        const double global_time = owner->in_time + time;
        const KeyframeRef ref{owner->id, prop.name(), index};
        const bool selected = selected_keyframes_.count(ref) != 0;
        for (int component = (int)channels.size() - 1; component >= 0; --component) {
            const TimelinePropertyRef &channel = channels[static_cast<size_t>(component)];
            const double key_value = channel.graph_value(time);
            const double in_speed = channel.graph_speed(time - 1e-6);
            const double out_speed = channel.graph_speed(time + 1e-6);
            if (selected) {
                if (index > 0) {
                    const double seconds = (time - prop.keyframe_time((size_t)index - 1)) *
                        prop.keyframe_temporal_influence((size_t)index, true) / 100.0;
                    const double value = graph_view_mode_ == GraphViewMode::Speed
                        ? in_speed : key_value - in_speed * seconds;
                    if (consider(GraphHitType::IncomingHandle, ref,
                            QPointF(time_to_x(global_time - seconds), graph_value_to_y(value)),
                            component)) return result;
                }
                if (index + 1 < (int)prop.keyframe_count()) {
                    const double seconds = (prop.keyframe_time((size_t)index + 1) - time) *
                        prop.keyframe_temporal_influence((size_t)index, false) / 100.0;
                    const double value = graph_view_mode_ == GraphViewMode::Speed
                        ? out_speed : key_value + out_speed * seconds;
                    if (consider(GraphHitType::OutgoingHandle, ref,
                            QPointF(time_to_x(global_time + seconds), graph_value_to_y(value)),
                            component)) return result;
                }
            }
            if (graph_view_mode_ == GraphViewMode::Speed) {
                if (index > 0 && consider(GraphHitType::Keyframe, ref,
                        QPointF(time_to_x(global_time), graph_value_to_y(in_speed)),
                        component)) return result;
                if (index + 1 < (int)prop.keyframe_count() &&
                    consider(GraphHitType::Keyframe, ref,
                        QPointF(time_to_x(global_time), graph_value_to_y(out_speed)),
                        component)) return result;
            } else if (consider(GraphHitType::Keyframe, ref,
                    QPointF(time_to_x(global_time), graph_value_to_y(key_value)),
                    component)) return result;
        }
    }
    return result;
}

std::vector<TimelineWidget::KeyframeRef> TimelineWidget::graph_edit_targets(
    const KeyframeRef &primary) const
{
    std::vector<KeyframeRef> targets;
    for (const KeyframeRef &ref : selected_keyframes_)
        if (ref.layer_id == primary.layer_id && ref.prop_name == primary.prop_name)
            targets.push_back(ref);
    if (targets.empty()) targets.push_back(primary);
    return targets;
}

bool TimelineWidget::graph_mouse_press(QMouseEvent *ev)
{
    if (!graph_editor_enabled_ || !title_) return false;
    /* The graph shares the ordinary timeline ruler.  Do not consume ruler
     * presses: the normal timeline handler owns playhead, loop and pause
     * marker interaction there. */
    if (ev->pos().y() < ruler_height()) return false;

    if (!graph_rect().contains(ev->pos())) return false;
    if (ev->button() == Qt::MiddleButton ||
        (ev->button() == Qt::LeftButton && (ev->modifiers() & Qt::AltModifier) &&
         graph_hit_test(ev->pos()).type == GraphHitType::None)) {
        drag_mode_ = DragMode::GraphPan;
        graph_drag_start_ = ev->pos();
        graph_drag_start_min_ = graph_value_min_;
        graph_drag_start_max_ = graph_value_max_;
        graph_drag_start_scroll_x_ = scroll_x_;
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return true;
    }
    if (ev->button() != Qt::LeftButton) return false;
    const GraphHit hit = graph_hit_test(ev->pos());

    /* The playhead line remains directly draggable inside the graph body.
     * Keyframes and temporal handles take priority when they overlap it. */
    if (hit.type == GraphHitType::None &&
        std::abs(ev->pos().x() - time_to_x(playhead_)) <= 6) {
        drag_mode_ = DragMode::Playhead;
        emit playhead_changed(std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration));
        setCursor(Qt::SizeHorCursor);
        ev->accept();
        return true;
    }

    graph_drag_changed_ = false;
    graph_drag_start_ = ev->pos();
    graph_marquee_current_ = ev->pos();
    temporal_drag_snapshots_.clear();
    graph_drag_hit_ = hit;
    if (hit.type == GraphHitType::None) {
        drag_mode_ = DragMode::GraphMarquee;
        marquee_additive_ = ev->modifiers() & Qt::ShiftModifier;
        if (!marquee_additive_) selected_keyframes_.clear();
        update();
        ev->accept();
        return true;
    }
    const bool additive = ev->modifiers() & Qt::ShiftModifier;
    if (additive && hit.type == GraphHitType::Keyframe) {
        select_keyframe(hit.ref.layer_id, hit.ref.prop_name, hit.ref.index, true, true);
        update(); ev->accept(); return true;
    }
    if (!is_keyframe_selected(hit.ref.layer_id, hit.ref.prop_name, hit.ref.index))
        select_keyframe(hit.ref.layer_id, hit.ref.prop_name, hit.ref.index, false, false);
    for (const KeyframeRef &ref : graph_edit_targets(hit.ref)) {
        TimelinePropertyRef prop;
        if (!graph_ref_property(ref, &prop)) continue;
        TemporalDragSnapshot snapshot;
        snapshot.ref = ref;
        TimelinePropertyRef display_prop = prop;
        if (graph_channel_mode_ == GraphChannelMode::All &&
            prop.graph_channel_count() > 1 && hit.channel >= 0)
            display_prop = prop.graph_channel(hit.channel);
        snapshot.incoming_influence = prop.keyframe_temporal_influence((size_t)ref.index, true);
        snapshot.outgoing_influence = prop.keyframe_temporal_influence((size_t)ref.index, false);
        snapshot.incoming_speed = display_prop.graph_speed(
            prop.keyframe_time((size_t)ref.index) - 1e-6);
        snapshot.outgoing_speed = display_prop.graph_speed(
            prop.keyframe_time((size_t)ref.index) + 1e-6);
        snapshot.time = prop.keyframe_time((size_t)ref.index);
        snapshot.value = display_prop.graph_value(snapshot.time);
        snapshot.channel_count = graph_channel_mode_ == GraphChannelMode::All
            ? prop.graph_channel_count() : 1;
        for (int component = 0; component < snapshot.channel_count && component < 4; ++component)
            snapshot.channel_values[static_cast<size_t>(component)] =
                prop.graph_channel(component).graph_value(snapshot.time);
        snapshot.linked = prop.keyframe_temporal_tangents_linked((size_t)ref.index);
        temporal_drag_snapshots_.push_back(snapshot);
    }
    drag_mode_ = hit.type == GraphHitType::IncomingHandle ? DragMode::GraphIncomingHandle
               : hit.type == GraphHitType::OutgoingHandle ? DragMode::GraphOutgoingHandle
               : DragMode::GraphKeyframe;
    setCursor(hit.type == GraphHitType::Keyframe ? Qt::ClosedHandCursor : Qt::CrossCursor);
    ev->accept();
    return true;
}

bool TimelineWidget::graph_mouse_move(QMouseEvent *ev)
{
    if (!graph_editor_enabled_ || !title_) return false;

    /* Non-graph drags (most importantly the shared playhead) must continue
     * through TimelineWidget::mouseMoveEvent instead of being swallowed by
     * graph hover handling. */
    const bool graph_drag = drag_mode_ == DragMode::GraphPan ||
                            drag_mode_ == DragMode::GraphMarquee ||
                            drag_mode_ == DragMode::GraphKeyframe ||
                            drag_mode_ == DragMode::GraphIncomingHandle ||
                            drag_mode_ == DragMode::GraphOutgoingHandle;
    if (!graph_drag && drag_mode_ != DragMode::None)
        return false;

    if (drag_mode_ == DragMode::GraphPan) {
        QPoint delta = ev->pos() - graph_drag_start_;
        if (ev->modifiers() & Qt::ShiftModifier) {
            if (std::abs(delta.x()) >= std::abs(delta.y())) delta.setY(0);
            else delta.setX(0);
        }
        scroll_x_ = graph_drag_start_scroll_x_ - delta.x();
        clamp_scroll();
        const double span = graph_drag_start_max_ - graph_drag_start_min_;
        const double value_delta = delta.y() * span / std::max(1, graph_rect().height());
        graph_value_min_ = graph_drag_start_min_ + value_delta;
        graph_value_max_ = graph_drag_start_max_ + value_delta;
        update();
        notify_horizontal_view_changed();
        ev->accept(); return true;
    }
    if (drag_mode_ == DragMode::GraphMarquee) {
        graph_marquee_current_ = ev->pos();
        update(); ev->accept(); return true;
    }
    if (drag_mode_ == DragMode::GraphKeyframe) {
        /* Graph editing is sub-frame by default. Ctrl/Command explicitly
         * requests project-frame snapping; the ordinary timeline remains
         * frame-snapped at all times. */
        double time_delta = graph_x_to_time(ev->pos().x()) -
                            graph_x_to_time(graph_drag_start_.x());
        double value_delta = graph_y_to_value(ev->pos().y()) - graph_y_to_value(graph_drag_start_.y());
        const QPoint pixel_delta = ev->pos() - graph_drag_start_;
        if (ev->modifiers() & Qt::ShiftModifier) {
            /* Shift constrains key movement to the dominant axis, matching the
             * ordinary canvas/timeline manipulation convention. */
            if (std::abs(pixel_delta.x()) >= std::abs(pixel_delta.y())) value_delta = 0.0;
            else time_delta = 0.0;
        }
        const bool snap_modifier =
            (ev->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) !=
            Qt::NoModifier;
        if (snap_modifier && !temporal_drag_snapshots_.empty() && std::abs(time_delta) > 1e-12) {
            const TemporalDragSnapshot &primary_snapshot = temporal_drag_snapshots_.front();
            std::shared_ptr<Layer> primary_layer;
            if (graph_ref_property(primary_snapshot.ref, nullptr, &primary_layer) && primary_layer) {
                const double desired_global = primary_layer->in_time +
                    primary_snapshot.time + time_delta;
                time_delta = snap_time(desired_global) -
                    (primary_layer->in_time + primary_snapshot.time);
            }
        }
        if (snap_modifier && std::abs(value_delta) > 1e-12) {
            const double span = std::max(1e-9, graph_value_max_ - graph_value_min_);
            const double raw_step = span / 100.0;
            const double value_step = std::pow(10.0, std::floor(std::log10(raw_step)));
            value_delta = std::round(value_delta / value_step) * value_step;
        }
        for (const TemporalDragSnapshot &snapshot : temporal_drag_snapshots_) {
            TimelinePropertyRef prop; std::shared_ptr<Layer> layer;
            if (!graph_ref_property(snapshot.ref, &prop, &layer) || !layer || layer->locked) continue;
            prop.set_keyframe_time((size_t)snapshot.ref.index,
                std::clamp(snapshot.time + time_delta, 0.0,
                           std::max(0.0, layer->out_time - layer->in_time)));
            if (graph_view_mode_ == GraphViewMode::Value) {
                if (snapshot.channel_count > 1 &&
                    graph_channel_mode_ == GraphChannelMode::All) {
                    for (int component = 0;
                         component < snapshot.channel_count && component < 4;
                         ++component) {
                        prop.set_keyframe_graph_channel_value(
                            (size_t)snapshot.ref.index, component,
                            snapshot.channel_values[static_cast<size_t>(component)] + value_delta);
                    }
                } else if (prop.graph_value_editable()) {
                    prop.set_keyframe_graph_value((size_t)snapshot.ref.index,
                                                  snapshot.value + value_delta);
                }
            }
        }
        graph_drag_changed_ = true;
        update(); ev->accept(); return true;
    }
    if (drag_mode_ == DragMode::GraphIncomingHandle ||
        drag_mode_ == DragMode::GraphOutgoingHandle) {
        const bool incoming = drag_mode_ == DragMode::GraphIncomingHandle;
        TimelinePropertyRef primary;
        if (!graph_ref_property(graph_drag_hit_.ref, &primary)) return true;
        TimelinePropertyRef display_primary = primary;
        if (graph_channel_mode_ == GraphChannelMode::All &&
            primary.graph_channel_count() > 1 && graph_drag_hit_.channel >= 0)
            display_primary = primary.graph_channel(graph_drag_hit_.channel);
        const int index = graph_drag_hit_.ref.index;
        const double key_time = primary.keyframe_time((size_t)index);
        std::shared_ptr<Layer> primary_owner;
        if (!graph_ref_property(graph_drag_hit_.ref, nullptr, &primary_owner) || !primary_owner)
            return true;
        const double global_key_time = primary_owner->in_time + key_time;
        double interval = 0.0;
        if (incoming && index > 0) interval = key_time - primary.keyframe_time((size_t)index - 1);
        if (!incoming && index + 1 < (int)primary.keyframe_count())
            interval = primary.keyframe_time((size_t)index + 1) - key_time;
        if (!(interval > 1e-10)) return true;
        const double pointer_time = graph_x_to_time(ev->pos().x());
        double influence = (incoming ? global_key_time - pointer_time
                                     : pointer_time - global_key_time) / interval * 100.0;
        influence = std::clamp(influence, 0.0, 100.0);
        const double seconds = std::max(1e-10, interval * influence / 100.0);
        const double graph_value = graph_y_to_value(ev->pos().y());
        const double key_value = display_primary.graph_value(key_time);
        double speed = graph_view_mode_ == GraphViewMode::Speed
            ? graph_value
            : incoming ? (key_value - graph_value) / seconds
                       : (graph_value - key_value) / seconds;
        const TemporalDragSnapshot *primary_snapshot = nullptr;
        for (const auto &snapshot : temporal_drag_snapshots_)
            if (!(snapshot.ref < graph_drag_hit_.ref) && !(graph_drag_hit_.ref < snapshot.ref)) {
                primary_snapshot = &snapshot; break;
            }
        if (!primary_snapshot) return true;
        const double original_influence = incoming
            ? primary_snapshot->incoming_influence : primary_snapshot->outgoing_influence;
        const double original_speed = incoming
            ? primary_snapshot->incoming_speed : primary_snapshot->outgoing_speed;
        const QPoint handle_delta = ev->pos() - graph_drag_start_;
        if (ev->modifiers() & Qt::ShiftModifier) {
            /* Shift locks handle editing to influence (horizontal) or speed
             * (vertical), preventing accidental changes to the other axis. */
            if (std::abs(handle_delta.x()) >= std::abs(handle_delta.y()))
                speed = original_speed;
            else
                influence = original_influence;
        }
        if (ev->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) {
            influence = std::round(influence / 5.0) * 5.0;
            const double speed_step = std::max(0.01,
                std::pow(10.0, std::floor(std::log10(std::max(0.01, std::abs(speed)))) - 1.0));
            speed = std::round(speed / speed_step) * speed_step;
        }
        influence = std::clamp(influence, 0.0, 100.0);
        const double influence_delta = influence - original_influence;
        const double speed_delta = speed - original_speed;
        for (const TemporalDragSnapshot &snapshot : temporal_drag_snapshots_) {
            TimelinePropertyRef prop; std::shared_ptr<Layer> layer;
            if (!graph_ref_property(snapshot.ref, &prop, &layer) || !layer || layer->locked) continue;
            const double next_influence = (incoming ? snapshot.incoming_influence
                                                    : snapshot.outgoing_influence) + influence_delta;
            const double next_speed = (incoming ? snapshot.incoming_speed
                                                : snapshot.outgoing_speed) + speed_delta;
            const bool break_pair = (ev->modifiers() & Qt::AltModifier) || !snapshot.linked;
            prop.set_temporal_handle((size_t)snapshot.ref.index, incoming,
                                     next_influence, next_speed, break_pair);
        }
        graph_drag_changed_ = true;
        graph_fit_pending_ = false;
        update(); ev->accept(); return true;
    }
    if (!graph_rect().contains(ev->pos()))
        return false;

    const GraphHit hover = graph_hit_test(ev->pos());
    setCursor(hover.type == GraphHitType::None ? Qt::ArrowCursor
              : hover.type == GraphHitType::Keyframe ? Qt::OpenHandCursor
              : Qt::CrossCursor);
    return true;
}

bool TimelineWidget::graph_mouse_release(QMouseEvent *ev)
{
    if (!graph_editor_enabled_) return false;
    const bool graph_drag = drag_mode_ == DragMode::GraphPan ||
                            drag_mode_ == DragMode::GraphMarquee ||
                            drag_mode_ == DragMode::GraphKeyframe ||
                            drag_mode_ == DragMode::GraphIncomingHandle ||
                            drag_mode_ == DragMode::GraphOutgoingHandle;
    if (!graph_drag) return false;

    if (drag_mode_ == DragMode::GraphMarquee) {
        const QRect marquee(graph_drag_start_, graph_marquee_current_);
        std::shared_ptr<Layer> owner;
        TimelinePropertyRef prop = active_graph_property(&owner);
        const std::vector<TimelinePropertyRef> channels = active_graph_channels();
        if (prop && owner && !channels.empty()) {
            for (size_t index = 0; index < prop.keyframe_count(); ++index) {
                const double time = prop.keyframe_time(index);
                const int x = time_to_x(owner->in_time + time);
                bool contained = false;
                for (const TimelinePropertyRef &channel : channels) {
                    const double incoming = channel.graph_speed(time - 1e-6);
                    const double outgoing = channel.graph_speed(time + 1e-6);
                    if (graph_view_mode_ == GraphViewMode::Speed) {
                        if (index > 0)
                            contained = marquee.normalized().contains(QPoint(x, (int)std::round(
                                graph_value_to_y(incoming))));
                        if (!contained && index + 1 < prop.keyframe_count())
                            contained = marquee.normalized().contains(QPoint(x, (int)std::round(
                                graph_value_to_y(outgoing))));
                    } else {
                        contained = marquee.normalized().contains(QPoint(x, (int)std::round(
                            graph_value_to_y(channel.graph_value(time)))));
                    }
                    if (contained) break;
                }
                if (contained)
                    selected_keyframes_.insert({owner->id, prop.name(), (int)index});
            }
        }
    }
    if (drag_mode_ == DragMode::GraphKeyframe && graph_drag_changed_) {
        // Capture the selected keyframes by their final times before sorting.
        // This preserves selection identity even when keys cross during a drag.
        std::map<KeyframeRef, double> selected_times;
        for (const KeyframeRef &ref : selected_keyframes_) {
            TimelinePropertyRef prop;
            if (graph_ref_property(ref, &prop) && ref.index >= 0 &&
                ref.index < (int)prop.keyframe_count())
                selected_times[ref] = prop.keyframe_time((size_t)ref.index);
        }

        std::set<std::pair<std::string, std::string>> touched;
        for (const auto &snapshot : temporal_drag_snapshots_)
            touched.insert({snapshot.ref.layer_id, snapshot.ref.prop_name});
        for (const auto &entry : touched) {
            TimelinePropertyRef prop = find_timeline_property(entry.first, entry.second);
            if (prop) prop.sort_keyframes();
        }

        std::set<KeyframeRef> remapped;
        std::map<std::pair<std::string, std::string>, std::set<int>> used_indices;
        for (const auto &[ref, selected_time] : selected_times) {
            TimelinePropertyRef prop = find_timeline_property(
                ref.layer_id, ref.prop_name);
            if (!prop) continue;
            const auto property_key = std::make_pair(ref.layer_id, ref.prop_name);
            int best = -1;
            double best_distance = std::numeric_limits<double>::max();
            for (int index = 0; index < (int)prop.keyframe_count(); ++index) {
                if (used_indices[property_key].count(index)) continue;
                const double distance = std::abs(prop.keyframe_time((size_t)index) -
                                                 selected_time);
                if (distance < best_distance) {
                    best = index;
                    best_distance = distance;
                }
            }
            if (best >= 0) {
                used_indices[property_key].insert(best);
                remapped.insert({ref.layer_id, ref.prop_name, best});
            }
        }
        selected_keyframes_ = std::move(remapped);
    }
    const bool changed = graph_drag_changed_;
    drag_mode_ = DragMode::None;
    graph_drag_hit_ = {};
    temporal_drag_snapshots_.clear();
    graph_drag_changed_ = false;
    unsetCursor();
    update();
    if (changed) emit keyframe_easing_changed();
    if (ev) ev->accept();
    return true;
}

void TimelineWidget::show_property_velocity_dialog(const std::string &layer_id,
                                                   const std::string &property_name,
                                                   double local_time)
{
    if (!title_) return;
    if (timeline_owner_locked(*title_, layer_id)) return;
    TimelinePropertyRef prop = find_timeline_property(layer_id, property_name);
    if (!prop || prop.is_hold_only() || prop.keyframe_count() == 0) return;
    const double tolerance = std::max(1e-6, obs_frame_duration() * 0.5 + 1e-6);
    int index = -1;
    double best_distance = std::numeric_limits<double>::infinity();
    for (int candidate = 0; candidate < static_cast<int>(prop.keyframe_count()); ++candidate) {
        const double distance = std::abs(prop.keyframe_time(static_cast<size_t>(candidate)) - local_time);
        if (distance < best_distance) {
            best_distance = distance;
            index = candidate;
        }
    }
    if (index < 0 || best_distance > tolerance) return;
    show_temporal_velocity_dialog(prop, {index});
}

void TimelineWidget::show_temporal_velocity_dialog(TimelinePropertyRef prop,
                                                   const std::vector<int> &indices)
{
    if (!prop || prop.is_hold_only() || indices.empty()) return;
    const int first = indices.front();
    if (first < 0 || first >= (int)prop.keyframe_count()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(fxm_tr("OBSTitles.KeyframeVelocity"));
    auto *layout = new QFormLayout(&dialog);
    auto *mode = new QComboBox(&dialog);
    for (TemporalInterpolationMode item : {TemporalInterpolationMode::Linear,
             TemporalInterpolationMode::Hold, TemporalInterpolationMode::AutoBezier,
             TemporalInterpolationMode::ContinuousBezier,
             TemporalInterpolationMode::ManualBezier})
        mode->addItem(temporal_mode_label(item), (int)item);
    mode->setCurrentIndex(std::max(0, mode->findData((int)prop.keyframe_temporal_mode((size_t)first))));
    auto make_spin = [&](double value, bool influence) {
        auto *spin = new QDoubleSpinBox(&dialog);
        spin->setDecimals(6);
        spin->setRange(influence ? 0.0 : -1.0e15, influence ? 100.0 : 1.0e15);
        spin->setValue(value);
        if (influence) spin->setSuffix(QStringLiteral(" %"));
        return spin;
    };
    auto *influence_in = make_spin(prop.keyframe_temporal_influence((size_t)first, true), true);
    auto *speed_in = make_spin(prop.keyframe_temporal_speed((size_t)first, true), false);
    auto *influence_out = make_spin(prop.keyframe_temporal_influence((size_t)first, false), true);
    auto *speed_out = make_spin(prop.keyframe_temporal_speed((size_t)first, false), false);
    layout->addRow(fxm_tr("OBSTitles.TemporalMode"), mode);
    layout->addRow(fxm_tr("OBSTitles.IncomingInfluence"), influence_in);
    layout->addRow(fxm_tr("OBSTitles.IncomingSpeed"), speed_in);
    layout->addRow(fxm_tr("OBSTitles.OutgoingInfluence"), influence_out);
    layout->addRow(fxm_tr("OBSTitles.OutgoingSpeed"), speed_out);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;

    const double base_in_inf = prop.keyframe_temporal_influence((size_t)first, true);
    const double base_out_inf = prop.keyframe_temporal_influence((size_t)first, false);
    const double base_in_speed = prop.keyframe_temporal_speed((size_t)first, true);
    const double base_out_speed = prop.keyframe_temporal_speed((size_t)first, false);
    const double delta_in_inf = influence_in->value() - base_in_inf;
    const double delta_out_inf = influence_out->value() - base_out_inf;
    const double delta_in_speed = speed_in->value() - base_in_speed;
    const double delta_out_speed = speed_out->value() - base_out_speed;
    const auto selected_mode = (TemporalInterpolationMode)mode->currentData().toInt();
    for (int index : indices) {
        if (index < 0 || index >= (int)prop.keyframe_count()) continue;
        const double next_in_inf = prop.keyframe_temporal_influence((size_t)index, true) + delta_in_inf;
        const double next_out_inf = prop.keyframe_temporal_influence((size_t)index, false) + delta_out_inf;
        const double next_in_speed = prop.keyframe_temporal_speed((size_t)index, true) + delta_in_speed;
        const double next_out_speed = prop.keyframe_temporal_speed((size_t)index, false) + delta_out_speed;
        prop.apply_temporal_mode((size_t)index, selected_mode);
        if (selected_mode == TemporalInterpolationMode::ManualBezier ||
            selected_mode == TemporalInterpolationMode::ContinuousBezier) {
            prop.set_temporal_handle((size_t)index, true, next_in_inf, next_in_speed, true);
            prop.set_temporal_handle((size_t)index, false, next_out_inf, next_out_speed, true);
            if (selected_mode == TemporalInterpolationMode::ContinuousBezier)
                prop.apply_temporal_mode((size_t)index, selected_mode);
        }
    }
    update();
    emit keyframe_easing_changed();
}

void TimelineWidget::graph_context_menu(QContextMenuEvent *ev)
{
    if (!title_) return;
    const GraphHit hit = graph_hit_test(ev->pos());
    if (hit.type != GraphHitType::None &&
        !is_keyframe_selected(hit.ref.layer_id, hit.ref.prop_name, hit.ref.index))
        select_keyframe(hit.ref.layer_id, hit.ref.prop_name, hit.ref.index, false, false);
    std::shared_ptr<Layer> owner;
    TimelinePropertyRef prop = active_graph_property(&owner);
    QMenu menu(this);
    QAction *copy_action = menu.addAction(fxm_tr("OBSTitles.Copy"));
    QAction *cut_action = menu.addAction(fxm_tr("OBSTitles.Cut"));
    QAction *paste_action = menu.addAction(fxm_tr("OBSTitles.Paste"));
    QAction *delete_action = menu.addAction(fxm_tr("OBSTitles.Delete"));
    const bool has_selection = !selected_keyframes_.empty();
    copy_action->setEnabled(has_selection);
    cut_action->setEnabled(has_selection);
    paste_action->setEnabled(!keyframe_clipboard_.empty());
    delete_action->setEnabled(has_selection);
    menu.addSeparator();
    QAction *value_graph = menu.addAction(fxm_tr("OBSTitles.ValueGraph"));
    QAction *speed_graph = menu.addAction(fxm_tr("OBSTitles.SpeedGraph"));
    value_graph->setCheckable(true); speed_graph->setCheckable(true);
    value_graph->setChecked(graph_view_mode_ == GraphViewMode::Value);
    speed_graph->setChecked(graph_view_mode_ == GraphViewMode::Speed);
    menu.addSeparator();
    QAction *fit_all = menu.addAction(fxm_tr("OBSTitles.FitGraphsToView"));
    QAction *fit_selection = menu.addAction(fxm_tr("OBSTitles.FitGraphSelection"));
    fit_selection->setEnabled(!selected_keyframes_.empty());
    menu.addSeparator();

    std::vector<int> indices;
    if (prop && owner) {
        for (const KeyframeRef &ref : selected_keyframes_)
            if (ref.layer_id == owner->id && ref.prop_name == prop.name()) indices.push_back(ref.index);
    }
    QAction *easy = nullptr, *easy_in = nullptr, *easy_out = nullptr, *velocity = nullptr;
    std::vector<std::pair<QAction *, TemporalInterpolationMode>> mode_actions;
    if (prop && !indices.empty() && !prop.is_hold_only()) {
        QMenu *interpolation = menu.addMenu(fxm_tr("OBSTitles.TemporalInterpolation"));
        for (TemporalInterpolationMode mode : {TemporalInterpolationMode::Linear,
                 TemporalInterpolationMode::Hold, TemporalInterpolationMode::AutoBezier,
                 TemporalInterpolationMode::ContinuousBezier,
                 TemporalInterpolationMode::ManualBezier}) {
            QAction *action = interpolation->addAction(temporal_mode_label(mode));
            action->setCheckable(true);
            action->setChecked(std::all_of(indices.begin(), indices.end(), [&](int index) {
                return prop.keyframe_temporal_mode((size_t)index) == mode;
            }));
            mode_actions.push_back({action, mode});
        }
        interpolation->addSeparator();
        easy = interpolation->addAction(fxm_tr("OBSTitles.EasyEase"));
        easy_in = interpolation->addAction(fxm_tr("OBSTitles.EasyEaseIn"));
        easy_out = interpolation->addAction(fxm_tr("OBSTitles.EasyEaseOut"));
        velocity = menu.addAction(fxm_tr("OBSTitles.KeyframeVelocity"));
    }
    QAction *chosen = menu.exec(ev->globalPos());
    if (!chosen) return;
    if (chosen == copy_action) { copy_selected_keyframes(); return; }
    if (chosen == cut_action) {
        if (cut_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }
    if (chosen == paste_action) {
        if (paste_keyframes_at(std::clamp(graph_x_to_time(ev->pos().x()),
                                          0.0, title_->duration), false))
            emit keyframe_easing_changed();
        return;
    }
    if (chosen == delete_action) {
        if (delete_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }
    if (chosen == value_graph) { set_graph_view_mode((int)GraphViewMode::Value); return; }
    if (chosen == speed_graph) { set_graph_view_mode((int)GraphViewMode::Speed); return; }
    if (chosen == fit_all) { fit_graph_to_view(); return; }
    if (chosen == fit_selection) { fit_graph_selection(); return; }
    if (chosen == velocity) { show_temporal_velocity_dialog(prop, indices); return; }
    for (const auto &[action, mode] : mode_actions) {
        if (chosen != action) continue;
        for (int index : indices) prop.apply_temporal_mode((size_t)index, mode);
        update(); emit keyframe_easing_changed(); return;
    }
    if (chosen == easy || chosen == easy_in || chosen == easy_out) {
        const bool in = chosen == easy || chosen == easy_in;
        const bool out = chosen == easy || chosen == easy_out;
        for (int index : indices) prop.apply_easy_ease((size_t)index, in, out);
        update(); emit keyframe_easing_changed();
    }
}
