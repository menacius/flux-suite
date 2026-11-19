#include "title-editor-internal.h"
#include "cache-manager.h"
#include "effect-preset-catalog.h"
#include "transition-preset-catalog.h"
#include "text-animator-presets.h"
#include "title-logger.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDragLeaveEvent>
#include <QFontMetrics>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QPainterPath>
#include <QBrush>
#include <QPolygon>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace {

/* Keep the visual grip large enough to read at common OBS UI scales and make
 * the interactive target wider still.  The outer strip edges deliberately win
 * over transition overlays so a transition never makes its host strip hard to
 * trim. */
constexpr int kLayerTrimHandleVisualWidth = 8;
constexpr int kLayerTrimHitWidth = 12;
constexpr int kTransitionDurationHitWidth = 9;

Vector3Keyframe vector3_keyframe_from_legacy(const VectorKeyframe &source)
{
    Vector3Keyframe result;
    result.time = source.time;
    result.value = {source.value.x, source.value.y, source.value.z};
    result.easing = source.easing;
    result.cx1 = source.cx1;
    result.cy1 = source.cy1;
    result.cx2 = source.cx2;
    result.cy2 = source.cy2;
    result.temporal_mode = source.temporal_mode;
    result.incoming_influence = source.incoming_influence;
    result.outgoing_influence = source.outgoing_influence;
    result.incoming_speed = source.incoming_speed;
    result.outgoing_speed = source.outgoing_speed;
    result.temporal_tangents_linked = source.temporal_tangents_linked;
    result.temporal_velocity_explicit = source.temporal_velocity_explicit;
    result.incoming_tangent = {source.incoming_tangent.x,
                               source.incoming_tangent.y,
                               source.incoming_tangent.z};
    result.outgoing_tangent = {source.outgoing_tangent.x,
                               source.outgoing_tangent.y,
                               source.outgoing_tangent.z};
    result.spatial_mode = source.spatial_mode;
    result.spatial_tangents_linked = source.spatial_tangents_linked;
    result.rove_across_time = source.rove_across_time;
    return result;
}

VectorKeyframe legacy_keyframe_from_vector3(const Vector3Keyframe &source)
{
    VectorKeyframe result;
    result.time = source.time;
    result.value = {source.value.x, source.value.y, source.value.z};
    result.easing = source.easing;
    result.cx1 = source.cx1;
    result.cy1 = source.cy1;
    result.cx2 = source.cx2;
    result.cy2 = source.cy2;
    result.temporal_mode = source.temporal_mode;
    result.incoming_influence = source.incoming_influence;
    result.outgoing_influence = source.outgoing_influence;
    result.incoming_speed = source.incoming_speed;
    result.outgoing_speed = source.outgoing_speed;
    result.temporal_tangents_linked = source.temporal_tangents_linked;
    result.temporal_velocity_explicit = source.temporal_velocity_explicit;
    result.incoming_tangent = {source.incoming_tangent.x,
                               source.incoming_tangent.y,
                               source.incoming_tangent.z};
    result.outgoing_tangent = {source.outgoing_tangent.x,
                               source.outgoing_tangent.y,
                               source.outgoing_tangent.z};
    result.spatial_mode = source.spatial_mode;
    result.spatial_tangents_linked = source.spatial_tangents_linked;
    result.rove_across_time = source.rove_across_time;
    return result;
}

} // namespace

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
}

void TimelineWidget::set_title(std::shared_ptr<Title> t)
{
    const bool title_changed = t != title_;
    title_ = t;
    if (title_changed) {
        FXM_LOG_INFO("Timeline", QStringLiteral(
            "Timeline title=%1 duration=%2 layers=%3")
            .arg(title_ ? QString::fromStdString(title_->id)
                        : QStringLiteral("<none>"))
            .arg(title_ ? title_->duration : 0.0, 0, 'f', 6)
            .arg(title_ ? static_cast<int>(title_->layers.size()) : 0));
        scroll_x_ = 0;
        fit_zoom_active_ = true;
        selected_keyframes_.clear();
        graph_target_owner_id_.clear();
        graph_target_property_name_.clear();
        graph_fit_pending_ = true;
        transition_target_selected_ = false;
        selected_transition_layer_id_.clear();
    } else {
        prune_keyframe_selection();
    }
    clamp_scroll();
    clamp_vertical_scroll();
    notify_graph_channel_count();
    if (fit_zoom_active_ && width() > 40) {
        fit_timeline();
        return;
    }
    update();
    notify_horizontal_view_changed();
}

void TimelineWidget::set_selected_layer(const std::string &lid)
{
    set_selected_layers(lid.empty() ? std::vector<std::string>()
                                    : std::vector<std::string>{lid});
}

void TimelineWidget::set_selected_layers(const std::vector<std::string> &layer_ids)
{
    std::vector<std::string> normalized;
    if (title_) {
        std::set<std::string> seen;
        normalized.reserve(layer_ids.size());
        for (const auto &id : layer_ids) {
            if (id.empty() || !seen.insert(id).second)
                continue;
            if (title_->find_layer(id))
                normalized.push_back(id);
        }
    }

    bool transition_changed = false;
    if (transition_target_selected_ &&
        std::find(normalized.begin(), normalized.end(),
                  selected_transition_layer_id_) == normalized.end()) {
        transition_target_selected_ = false;
        selected_transition_layer_id_.clear();
        transition_changed = true;
    }

    const std::string next_primary = normalized.empty()
        ? std::string() : normalized.back();
    if (selected_layer_ids_ == normalized && sel_layer_id_ == next_primary &&
        !transition_changed)
        return;

    selected_layer_ids_ = std::move(normalized);
    sel_layer_id_ = next_primary;
    if (!sel_layer_id_.empty())
        selection_anchor_layer_id_ = sel_layer_id_;
    else
        selection_anchor_layer_id_.clear();
    graph_fit_pending_ = true;
    notify_graph_channel_count();
    update();
}

void TimelineWidget::set_playhead(double t)
{
    const int old_x = time_to_x(playhead_);
    playhead_ = snap_time(t);
    bool scrolled = false;
    if (title_)
        scrolled = keep_playhead_visible();

    if (scrolled) {
        update();
        notify_horizontal_view_changed();
        return;
    }

    const int new_x = time_to_x(playhead_);
    /* The ruler contains labels plus the playhead time badge. Repaint it as one
     * coherent band so a moving narrow dirty rectangle cannot clip adjacent
     * label glyphs. The timeline body still updates only the old/new lines. */
    update(QRect(0, 0, width(), ruler_height()));
    const QRect old_rect = playhead_dirty_rect(old_x);
    const QRect new_rect = playhead_dirty_rect(new_x);
    if (old_rect.intersects(new_rect) || old_rect.adjusted(-4, 0, 4, 0).intersects(new_rect))
        update(old_rect.united(new_rect));
    else {
        update(old_rect);
        update(new_rect);
    }
}

void TimelineWidget::set_vertical_scroll(int scroll_y)
{
    scroll_y_ = scroll_y;
    clamp_vertical_scroll();
    update();
}

void TimelineWidget::set_pixels_per_sec(double pixels_per_sec, double anchor_time, int anchor_x)
{
    pixels_per_sec_ = std::clamp(pixels_per_sec, 5.0, 1200.0);
    scroll_x_ = (int)std::round((anchor_time + timeline_pre_roll()) * pixels_per_sec_) - anchor_x;
    clamp_scroll();
    keep_playhead_visible();
    update();
    emit zoom_percent_changed(zoom_percent());
    notify_horizontal_view_changed();
}

void TimelineWidget::set_zoom_percent(int percent)
{
    fit_zoom_active_ = false;
    int clamped = std::clamp(percent, 5, 1200);
    double anchor_time = title_ ? std::clamp(playhead_, 0.0, title_->duration) : playhead_;
    int anchor_x = std::clamp(time_to_x(anchor_time), 24, std::max(24, width() - 24));
    set_pixels_per_sec((double)clamped, anchor_time, anchor_x);
}

int TimelineWidget::zoom_percent() const
{
    return (int)std::round(pixels_per_sec_);
}

void TimelineWidget::fit_timeline()
{
    fit_zoom_active_ = true;
    const double dur = std::max(obs_frame_duration(), timeline_display_duration());
    const double fitted = (double)std::max(1, width() - 40) / dur;
    /* Anchor the complete transition at the left edge. Document time zero is
     * offset by pre-roll in normal Stinger timeline mode. */
    set_pixels_per_sec(fitted, -timeline_pre_roll(), 0);
}

double TimelineWidget::horizontal_content_duration() const
{
    return std::max(obs_frame_duration(), timeline_display_duration());
}

double TimelineWidget::horizontal_view_start() const
{
    return std::clamp(scroll_x_ / std::max(1e-9, pixels_per_sec_), 0.0,
                      horizontal_content_duration());
}

double TimelineWidget::horizontal_view_duration() const
{
    const double usable_width = std::max(1, width() - 40);
    return std::min(horizontal_content_duration(),
                    usable_width / std::max(1e-9, pixels_per_sec_));
}

void TimelineWidget::set_horizontal_view(double start, double duration)
{
    fit_zoom_active_ = false;
    const double content = horizontal_content_duration();
    const double usable_width = std::max(1, width() - 40);
    const double minimum_duration = usable_width / 1200.0;
    const double requested_duration = std::clamp(
        duration, std::min(content, minimum_duration), content);

    pixels_per_sec_ = std::clamp(usable_width /
                                     std::max(1e-9, requested_duration),
                                 5.0, 1200.0);
    const double actual_duration = std::min(content,
        usable_width / std::max(1e-9, pixels_per_sec_));
    const double clamped_start = std::clamp(
        start, 0.0, std::max(0.0, content - actual_duration));
    scroll_x_ = static_cast<int>(std::round(clamped_start * pixels_per_sec_));
    clamp_scroll();
    update();
    emit zoom_percent_changed(zoom_percent());
    notify_horizontal_view_changed();
}

bool TimelineWidget::has_selected_keyframes() const
{
    return title_ && !selected_keyframes_.empty();
}

bool TimelineWidget::has_keyframe_clipboard() const
{
    return !keyframe_clipboard_.empty();
}

bool TimelineWidget::copy_keyframe_selection()
{
    return copy_selected_keyframes();
}

bool TimelineWidget::cut_keyframe_selection()
{
    if (!cut_selected_keyframes()) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::delete_keyframe_selection()
{
    if (!delete_selected_keyframes()) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::paste_keyframes_at_playhead()
{
    if (!paste_keyframes_at(playhead_)) return false;
    emit keyframe_easing_changed();
    return true;
}

bool TimelineWidget::has_transition_target_selection() const
{
    return transition_target_selected_ && selected_transition_layer() != nullptr;
}

bool TimelineWidget::has_selected_transition() const
{
    return selected_transition() != nullptr;
}

bool TimelineWidget::has_transition_clipboard() const
{
    return transition_clipboard_valid_;
}

bool TimelineWidget::layer_accepts_transition(const Layer &layer,
                                               const LayerTransition &transition) const
{
    if (!layer_transition_type_is_text(transition.type))
        return true;
    return layer.type == LayerType::Text ||
           layer.type == LayerType::Clock ||
           layer.type == LayerType::Ticker;
}

bool TimelineWidget::can_paste_transition_to_selection() const
{
    const auto layer = selected_transition_layer();
    return transition_clipboard_valid_ && layer && !layer->locked &&
           layer_accepts_transition(*layer, transition_clipboard_);
}

std::shared_ptr<Layer> TimelineWidget::selected_transition_layer() const
{
    if (!title_ || !transition_target_selected_ || selected_transition_layer_id_.empty())
        return nullptr;
    return title_->find_layer(selected_transition_layer_id_);
}

const LayerTransition *TimelineWidget::selected_transition() const
{
    const auto layer = selected_transition_layer();
    return layer ? find_layer_transition(layer->transitions, selected_transition_edge_) : nullptr;
}

LayerTransition *TimelineWidget::selected_transition()
{
    auto layer = selected_transition_layer();
    return layer ? find_layer_transition(layer->transitions, selected_transition_edge_) : nullptr;
}

void TimelineWidget::select_transition_target(const std::string &layer_id,
                                               LayerTransitionEdge edge)
{
    transition_target_selected_ = !layer_id.empty();
    selected_transition_layer_id_ = layer_id;
    selected_transition_edge_ = edge;
    selected_keyframes_.clear();
    update();
}

void TimelineWidget::clear_transition_selection()
{
    if (!transition_target_selected_ && selected_transition_layer_id_.empty())
        return;
    transition_target_selected_ = false;
    selected_transition_layer_id_.clear();
    update();
}

bool TimelineWidget::copy_transition_selection()
{
    const LayerTransition *transition = selected_transition();
    if (!transition)
        return false;
    transition_clipboard_ = *transition;
    transition_clipboard_valid_ = true;
    return true;
}

bool TimelineWidget::delete_transition_selection()
{
    auto layer = selected_transition_layer();
    const LayerTransition *selected = selected_transition();
    if (!layer || layer->locked || !selected)
        return false;
    const bool deleted_text_transition =
        layer_transition_type_is_text(selected->type);
    auto &transitions = layer->transitions;
    transitions.erase(std::remove_if(transitions.begin(), transitions.end(),
                                     [&](const LayerTransition &transition) {
                                         return transition.edge == selected_transition_edge_;
                                     }), transitions.end());
    /* Text-transition keyframes live in the managed TextAnimator generated for
     * the descriptor.  Removing the descriptor must immediately remove that
     * animator as part of the same edit, rather than waiting for a later editor
     * refresh where its keyframes can remain visible/selectable. */
    if (deleted_text_transition) {
        synchronize_text_transition_animators(
            layer->transitions, layer->text_animators,
            layer->in_time, layer->out_time, nullptr, false);
        clear_keyframe_selection();
        prune_keyframe_selection();
    }
    update();
    FXM_LOG_DEBUG("Transitions", QStringLiteral(
        "Deleted transition title=%1 layer=%2 edge=%3")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(QString::fromStdString(layer->id))
        .arg(static_cast<int>(selected_transition_edge_)));
    emit transition_modified();
    return true;
}

bool TimelineWidget::cut_transition_selection()
{
    // Keyboard shortcuts reach this path even when the context-menu action is
    // disabled. Validate mutability before changing the clipboard so Ctrl+X
    // on a locked layer cannot behave like an unexpected Copy operation.
    const auto layer = selected_transition_layer();
    if (!layer || layer->locked || !selected_transition())
        return false;
    if (!copy_transition_selection())
        return false;
    return delete_transition_selection();
}

void TimelineWidget::clear_transition_target_selection()
{
    clear_transition_selection();
}

bool TimelineWidget::paste_transition_to_selection()
{
    auto layer = selected_transition_layer();
    if (!transition_clipboard_valid_ || !layer || layer->locked ||
        !layer_accepts_transition(*layer, transition_clipboard_))
        return false;

    LayerTransition pasted = transition_clipboard_;
    pasted.id = TitleDataStore::make_uuid();
    pasted.edge = selected_transition_edge_;
    pasted.kind = layer_transition_type_is_text(pasted.type)
        ? LayerTransitionKind::Text : LayerTransitionKind::General;

    const double frame = obs_frame_duration();
    const double layer_duration = std::max(frame, layer->out_time - layer->in_time);
    const LayerTransitionEdge other_edge = selected_transition_edge_ == LayerTransitionEdge::In
        ? LayerTransitionEdge::Out : LayerTransitionEdge::In;
    const LayerTransition *other = find_layer_transition(layer->transitions, other_edge);
    const double maximum_duration = std::max(frame, layer_duration - (other ? other->duration : 0.0));
    pasted.duration = std::clamp(pasted.duration, frame, maximum_duration);

    if (LayerTransition *existing = find_layer_transition(layer->transitions, selected_transition_edge_))
        *existing = pasted;
    else
        layer->transitions.push_back(std::move(pasted));

    update();
    FXM_LOG_DEBUG("Transitions", QStringLiteral(
        "Pasted transition title=%1 layer=%2 edge=%3 type=%4")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(QString::fromStdString(layer->id))
        .arg(static_cast<int>(selected_transition_edge_))
        .arg(static_cast<int>(transition_clipboard_.type)));
    emit transition_modified();
    return true;
}

bool TimelineWidget::keep_playhead_visible()
{
    if (!title_) return false;
    int phx = time_to_x(playhead_);
    int old_scroll = scroll_x_;
    const double display_time = playhead_ + timeline_pre_roll();
    if (phx < 24)
        scroll_x_ = std::max(0, (int)std::round(display_time * pixels_per_sec_) - 24);
    if (phx > width() - 24)
        scroll_x_ = std::max(0, (int)std::round(display_time * pixels_per_sec_) - width() + 24);
    clamp_scroll();
    return old_scroll != scroll_x_;
}

double TimelineWidget::timeline_pre_roll() const
{
    if (!title_ || graph_editor_enabled_ ||
        title_->graphic_type != TitleGraphicType::Stinger)
        return 0.0;
    return std::max(0.0, title_->stinger_pre_roll);
}

double TimelineWidget::timeline_post_roll() const
{
    if (!title_ || graph_editor_enabled_ ||
        title_->graphic_type != TitleGraphicType::Stinger)
        return 0.0;
    return std::max(0.0, title_->stinger_post_roll);
}

double TimelineWidget::timeline_display_duration() const
{
    const double document_duration = title_ ? std::max(0.0, title_->duration) : 10.0;
    return timeline_pre_roll() + document_duration + timeline_post_roll();
}

double TimelineWidget::x_to_display_time(int x) const
{
    return (x + scroll_x_) / pixels_per_sec_;
}

int TimelineWidget::display_time_to_x(double t) const
{
    return (int)std::round(t * pixels_per_sec_) - scroll_x_;
}

double TimelineWidget::x_to_time(int x) const
{
    return snap_time(x_to_display_time(x) - timeline_pre_roll());
}

int TimelineWidget::time_to_x(double t) const
{
    return display_time_to_x(t + timeline_pre_roll());
}

QRect TimelineWidget::playhead_dirty_rect(int playhead_x) const
{
    const QRect line_rect(playhead_x - 10, ruler_height(), 20,
                          std::max(0, height() - ruler_height()));
    return line_rect.adjusted(-2, 0, 2, 0)
        .intersected(rect());
}

double TimelineWidget::snap_time(double t) const
{
    return snap_to_obs_frame(t);
}

void TimelineWidget::clamp_scroll()
{
    const double dur = timeline_display_duration();
    int max_scroll = std::max(0, (int)std::ceil(dur * pixels_per_sec_) - width() + 40);
    scroll_x_ = std::clamp(scroll_x_, 0, max_scroll);
}

void TimelineWidget::notify_horizontal_view_changed()
{
    emit horizontal_view_changed(horizontal_view_start(),
                                 horizontal_view_duration(),
                                 horizontal_content_duration());
}

int TimelineWidget::max_vertical_scroll() const
{
    int content_height = (int)timeline_rows(title_).size() * row_height();
    int viewport_height = std::max(0, height() - ruler_height());
    return std::max(0, content_height - viewport_height);
}

void TimelineWidget::clamp_vertical_scroll()
{
    scroll_y_ = std::clamp(scroll_y_, 0, max_vertical_scroll());
}


bool TimelineWidget::KeyframeRef::operator<(const KeyframeRef &other) const
{
    return std::tie(layer_id, prop_name, index) <
           std::tie(other.layer_id, other.prop_name, other.index);
}

TimelinePropertyRef TimelineWidget::find_timeline_property(Layer &layer, const std::string &prop_name) const
{
    for (auto prop : timeline_properties(layer)) {
        if (prop.name() == prop_name)
            return prop;
    }
    return {};
}

TimelinePropertyRef TimelineWidget::find_timeline_property(
    const std::string &owner_id, const std::string &prop_name) const
{
    return title_ ? timeline_property_for_owner(*title_, owner_id, prop_name)
                  : TimelinePropertyRef{};
}

void TimelineWidget::clear_keyframe_selection()
{
    if (selected_keyframes_.empty()) return;
    selected_keyframes_.clear();
    update();
}

void TimelineWidget::prune_keyframe_selection()
{
    if (!title_) {
        selected_keyframes_.clear();
        return;
    }

    for (auto it = selected_keyframes_.begin(); it != selected_keyframes_.end();) {
        auto prop = find_timeline_property(it->layer_id, it->prop_name);
        if (!prop || it->index < 0 || it->index >= (int)prop.keyframe_count())
            it = selected_keyframes_.erase(it);
        else
            ++it;
    }
}

bool TimelineWidget::is_keyframe_selected(const std::string &layer_id, const std::string &prop_name, int kf_idx) const
{
    return selected_keyframes_.find({layer_id, prop_name, kf_idx}) != selected_keyframes_.end();
}

void TimelineWidget::select_keyframe(const std::string &layer_id, const std::string &prop_name,
                                     int kf_idx, bool additive, bool toggle)
{
    transition_target_selected_ = false;
    selected_transition_layer_id_.clear();
    KeyframeRef ref{layer_id, prop_name, kf_idx};
    set_graph_target(layer_id, prop_name);
    if (!additive)
        selected_keyframes_.clear();
    if (toggle && selected_keyframes_.find(ref) != selected_keyframes_.end())
        selected_keyframes_.erase(ref);
    else
        selected_keyframes_.insert(ref);
    update();
}

QRect TimelineWidget::marquee_rect() const
{
    return QRect(marquee_start_, marquee_current_).normalized()
        .intersected(QRect(0, ruler_height(), width(), std::max(0, height() - ruler_height())));
}

void TimelineWidget::select_keyframes_in_rect(const QRect &rect, bool additive)
{
    if (!title_) return;
    std::set<KeyframeRef> selection = additive ? selected_keyframes_ : std::set<KeyframeRef>{};
    auto rows = timeline_rows(title_);
    const QRect visible_timeline(0, ruler_height(), width(), std::max(0, height() - ruler_height()));
    QRect bounded = rect.normalized().intersected(visible_timeline);
    if (bounded.isEmpty()) {
        selected_keyframes_ = std::move(selection);
        update();
        return;
    }

    for (int row = 0; row < (int)rows.size(); ++row) {
        const auto &entry = rows[row];
        if (!entry.is_property || !entry.prop) continue;
        if (entry.locked) continue;
        int y = ruler_height() + row * row_height() - scroll_y_;
        int ky = y + row_height() / 2;
        if (ky < visible_timeline.top() || ky > visible_timeline.bottom()) continue;
        if (ky < bounded.top() || ky > bounded.bottom()) continue;
        for (int i = 0; i < (int)entry.prop.keyframe_count(); ++i) {
            int kx = time_to_x(entry.in_time + entry.prop.keyframe_time((size_t)i));
            if (kx < visible_timeline.left() || kx > visible_timeline.right()) continue;
            if (bounded.contains(QPoint(kx, ky)))
                selection.insert({entry.owner_id, entry.prop.name(), i});
        }
    }
    selected_keyframes_ = std::move(selection);
    update();
}

bool TimelineWidget::copy_selected_keyframes()
{
    if (!title_) return false;
    prune_keyframe_selection();
    if (selected_keyframes_.empty()) return false;

    struct PendingCopy {
        std::string layer_id;
        std::string prop_name;
        Keyframe keyframe;
        VectorKeyframe vector_keyframe;
        Vector3Keyframe vector3_keyframe;
        std::vector<Keyframe> scalar_group_keyframes;
        QJsonObject extension_keyframe;
        DiscreteKeyframe discrete_keyframe;
        bool is_vector = false;
        bool is_vector3 = false;
        bool is_scalar_group = false;
        bool is_extension = false;
        bool is_discrete = false;
        double timeline_time = 0.0;
    };
    std::vector<PendingCopy> pending;
    double origin = std::numeric_limits<double>::max();

    std::set<std::pair<const AnimatedVec3Property *, int>> seen_vector3_keys;
    for (const auto &ref : selected_keyframes_) {
        auto prop = find_timeline_property(ref.layer_id, ref.prop_name);
        if (!prop || ref.index < 0 || ref.index >= (int)prop.keyframe_count()) continue;
        /* X/Y/Z rows are views of one Vector3 keyframe sequence. A marquee
         * selection may contain the same key through all three rows; copy it
         * once so paste cannot insert three duplicate XYZ keys. */
        if (prop.is_vector3() &&
            !seen_vector3_keys.insert({prop.vector3, ref.index}).second)
            continue;
        const double timeline_time = timeline_owner_in_time(*title_, ref.layer_id) +
                                     prop.keyframe_time((size_t)ref.index);
        origin = std::min(origin, timeline_time);
        PendingCopy copy;
        copy.layer_id = ref.layer_id;
        copy.prop_name = ref.prop_name;
        copy.is_vector = prop.is_vector();
        copy.is_vector3 = prop.is_vector3();
        copy.is_scalar_group = prop.is_scalar_group();
        copy.is_extension = prop.is_extension();
        copy.is_discrete = prop.is_discrete();
        copy.keyframe = prop.scalar_keyframe((size_t)ref.index);
        copy.vector_keyframe = prop.vector_keyframe((size_t)ref.index);
        copy.vector3_keyframe = prop.vector3_keyframe((size_t)ref.index);
        copy.scalar_group_keyframes = prop.scalar_group_keyframes((size_t)ref.index);
        copy.extension_keyframe = prop.extension_keyframe((size_t)ref.index);
        copy.discrete_keyframe = prop.discrete_keyframe((size_t)ref.index);
        copy.timeline_time = timeline_time;
        pending.push_back(copy);
    }

    if (pending.empty()) return false;
    std::sort(pending.begin(), pending.end(), [](const PendingCopy &a, const PendingCopy &b) {
        return std::tie(a.timeline_time, a.layer_id, a.prop_name) <
               std::tie(b.timeline_time, b.layer_id, b.prop_name);
    });

    keyframe_clipboard_.clear();
    keyframe_clipboard_.reserve(pending.size());
    for (const auto &entry : pending) {
        ClipboardKeyframe copy;
        copy.layer_id = entry.layer_id;
        copy.prop_name = entry.prop_name;
        copy.keyframe = entry.keyframe;
        copy.vector_keyframe = entry.vector_keyframe;
        copy.vector3_keyframe = entry.vector3_keyframe;
        copy.scalar_group_keyframes = entry.scalar_group_keyframes;
        copy.extension_keyframe = entry.extension_keyframe;
        copy.discrete_keyframe = entry.discrete_keyframe;
        copy.is_vector = entry.is_vector;
        copy.is_vector3 = entry.is_vector3;
        copy.is_scalar_group = entry.is_scalar_group;
        copy.is_extension = entry.is_extension;
        copy.is_discrete = entry.is_discrete;
        copy.offset = entry.timeline_time - origin;
        keyframe_clipboard_.push_back(std::move(copy));
    }
    return true;
}

bool TimelineWidget::delete_selected_keyframes()
{
    if (!title_) return false;
    prune_keyframe_selection();
    if (selected_keyframes_.empty()) return false;

    std::map<std::pair<std::string, std::string>, std::vector<int>> grouped;
    std::map<AnimatedVec3Property *,
             std::pair<TimelinePropertyRef, std::vector<int>>> vector3_grouped;
    for (const auto &ref : selected_keyframes_) {
        auto prop = find_timeline_property(ref.layer_id, ref.prop_name);
        if (!prop) continue;
        if (prop.is_vector3()) {
            auto &entry = vector3_grouped[prop.vector3];
            entry.first = prop;
            entry.second.push_back(ref.index);
        } else {
            grouped[{ref.layer_id, ref.prop_name}].push_back(ref.index);
        }
    }

    bool changed = false;
    for (auto &[prop_ref, indices] : grouped) {
        if (timeline_owner_locked(*title_, prop_ref.first)) continue;
        auto prop = find_timeline_property(prop_ref.first, prop_ref.second);
        if (!prop) continue;
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int index : indices) {
            if (index < 0 || index >= (int)prop.keyframe_count()) continue;
            prop.erase_keyframe((size_t)index);
            changed = true;
        }
    }
    for (auto &[track, entry] : vector3_grouped) {
        (void)track;
        TimelinePropertyRef prop = entry.first;
        auto &indices = entry.second;
        std::sort(indices.begin(), indices.end(), std::greater<int>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int index : indices) {
            if (index < 0 || index >= (int)prop.keyframe_count()) continue;
            prop.erase_keyframe((size_t)index);
            changed = true;
        }
    }

    if (changed) {
        const int removed_count = static_cast<int>(selected_keyframes_.size());
        selected_keyframes_.clear();
        update();
        emit keyframe_structure_changed();
        FXM_LOG_DEBUG("Animation", QStringLiteral(
            "Deleted keyframes title=%1 count=%2")
            .arg(title_ ? QString::fromStdString(title_->id)
                        : QStringLiteral("<none>"))
            .arg(removed_count));
    }
    return changed;
}

bool TimelineWidget::cut_selected_keyframes()
{
    if (!copy_selected_keyframes()) return false;
    return delete_selected_keyframes();
}

bool TimelineWidget::clipboard_single_property(std::string *owner_id,
                                               std::string *property_name) const
{
    if (keyframe_clipboard_.empty()) return false;
    const std::string &owner = keyframe_clipboard_.front().layer_id;
    const std::string &property = keyframe_clipboard_.front().prop_name;
    for (const ClipboardKeyframe &entry : keyframe_clipboard_) {
        if (entry.layer_id != owner || entry.prop_name != property)
            return false;
    }
    if (owner_id) *owner_id = owner;
    if (property_name) *property_name = property;
    return true;
}

bool TimelineWidget::clipboard_property_compatible(
    const ClipboardKeyframe &entry, const TimelinePropertyRef &target) const
{
    if (!target) return false;
    if (entry.is_discrete) return target.is_discrete();
    if (entry.is_extension) {
        if (!target.is_extension()) return false;
        const QJsonValue source_value = entry.extension_keyframe.value(
            QStringLiteral("value"));
        QJsonValue target_value = target.extension_fallback;
        if (target_value.isUndefined()) {
            const QJsonArray keys = fxm::effects::animation::track_keys(
                *target.extension_effect, target.extension_path);
            if (!keys.isEmpty())
                target_value = keys.first().toObject().value(
                    QStringLiteral("value"));
        }
        return target_value.isUndefined() || source_value.isUndefined() ||
               source_value.type() == target_value.type();
    }
    if (entry.is_scalar_group)
        return target.is_scalar_group() &&
               target.scalar_group.size() == entry.scalar_group_keyframes.size();
    if (entry.is_vector || entry.is_vector3)
        return target.is_vector() || target.is_vector3();
    return target.scalar != nullptr;
}

TimelinePropertyRef TimelineWidget::clipboard_target_property(
    std::string *owner_id, std::string *property_name) const
{
    if (!title_ || keyframe_clipboard_.empty() ||
        graph_target_owner_id_.empty() || graph_target_property_name_.empty())
        return {};

    std::string source_owner;
    std::string source_property;
    if (!clipboard_single_property(&source_owner, &source_property))
        return {};

    TimelinePropertyRef target = timeline_property_for_owner(
        *title_, graph_target_owner_id_, graph_target_property_name_);
    if (!target || !clipboard_property_compatible(keyframe_clipboard_.front(), target))
        return {};

    if (owner_id) *owner_id = graph_target_owner_id_;
    if (property_name) *property_name = graph_target_property_name_;
    return target;
}

int TimelineWidget::keyframe_index_near(const TimelinePropertyRef &prop,
                                        double local_time,
                                        double tolerance) const
{
    if (!prop) return -1;
    int best = -1;
    double best_distance = std::max(0.0, tolerance);
    for (int index = 0; index < static_cast<int>(prop.keyframe_count()); ++index) {
        const double distance = std::abs(prop.keyframe_time(
            static_cast<size_t>(index)) - local_time);
        if (distance <= best_distance) {
            best = index;
            best_distance = distance;
        }
    }
    return best;
}

void TimelineWidget::erase_keyframes_at(TimelinePropertyRef prop,
                                        double local_time)
{
    if (!prop) return;
    for (int index = static_cast<int>(prop.keyframe_count()) - 1;
         index >= 0; --index) {
        if (std::abs(prop.keyframe_time(static_cast<size_t>(index)) -
                     local_time) <= 1e-6)
            prop.erase_keyframe(static_cast<size_t>(index));
    }
}

bool TimelineWidget::add_keyframe_at(const std::string &owner_id,
                                     const std::string &property_name,
                                     double timeline_time,
                                     bool snap_to_frame)
{
    if (!title_ || timeline_owner_locked(*title_, owner_id)) return false;
    TimelinePropertyRef prop = find_timeline_property(owner_id, property_name);
    if (!prop) return false;

    const double owner_in = timeline_owner_in_time(*title_, owner_id);
    const double owner_out = timeline_owner_out_time(*title_, owner_id);
    const double requested = snap_to_frame ? snap_time(timeline_time)
                                           : timeline_time;
    const double local_time = std::clamp(requested - owner_in, 0.0,
        std::max(0.0, owner_out - owner_in));
    /* All native/extension track writers use a 1/240-second identity
     * epsilon. Respect the same boundary here so a double-click that is near
     * an existing key selects it instead of letting toggle_at remove it. */
    const double tolerance = std::max(1.0 / 240.0 + 1e-9,
        snap_to_frame ? obs_frame_duration() * 0.25 : 0.0);
    const int existing = keyframe_index_near(prop, local_time, tolerance);
    if (existing >= 0) {
        set_graph_target(owner_id, property_name);
        selected_keyframes_.clear();
        selected_keyframes_.insert({owner_id, property_name, existing});
        update();
        return false;
    }

    prop.toggle_at(local_time);
    prop.sort_keyframes();
    const int inserted = keyframe_index_near(prop, local_time,
                                             std::max(1e-6, tolerance));
    if (inserted < 0) return false;

    set_graph_target(owner_id, property_name);
    selected_keyframes_.clear();
    selected_keyframes_.insert({owner_id, property_name, inserted});
    update();
    emit keyframe_added(owner_id, property_name, local_time);
    emit keyframe_structure_changed();
    emit keyframe_easing_changed();
    FXM_LOG_DEBUG("Animation", QStringLiteral(
        "Added keyframe title=%1 owner=%2 property=%3 timeline=%4 local=%5 snap=%6")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(QString::fromStdString(owner_id))
        .arg(QString::fromStdString(property_name))
        .arg(requested, 0, 'f', 6)
        .arg(local_time, 0, 'f', 6)
        .arg(snap_to_frame));
    return true;
}

bool TimelineWidget::paste_keyframes_at(double timeline_time, bool snap_origin)
{
    if (!title_ || keyframe_clipboard_.empty()) return false;

    std::string redirected_owner;
    std::string redirected_property;
    const TimelinePropertyRef redirected_target = clipboard_target_property(
        &redirected_owner, &redirected_property);
    const bool redirect_single_track = static_cast<bool>(redirected_target);

    std::map<std::pair<std::string, std::string>, std::vector<double>> inserted_times;
    bool changed = false;
    int replaced_keyframes = 0;
    const double paste_origin = std::clamp(
        snap_origin ? snap_time(timeline_time) : timeline_time,
        0.0, title_->duration);

    for (const ClipboardKeyframe &entry : keyframe_clipboard_) {
        const std::string &target_owner = redirect_single_track
            ? redirected_owner : entry.layer_id;
        const std::string &target_property = redirect_single_track
            ? redirected_property : entry.prop_name;
        if (timeline_owner_locked(*title_, target_owner)) continue;

        TimelinePropertyRef prop = find_timeline_property(target_owner,
                                                          target_property);
        if (!prop || !clipboard_property_compatible(entry, prop)) continue;

        const double owner_in = timeline_owner_in_time(*title_, target_owner);
        const double owner_out = timeline_owner_out_time(*title_, target_owner);
        const double target_time = paste_origin + entry.offset;
        const double target_local_time = target_time - owner_in;
        const double local_time = std::clamp(
            snap_origin ? snap_time(target_local_time) : target_local_time,
            0.0, std::max(0.0, owner_out - owner_in));
        const int previous = keyframe_index_near(prop, local_time, 1e-6);
        if (previous >= 0) {
            erase_keyframes_at(prop, local_time);
            ++replaced_keyframes;
        }

        if (entry.is_discrete) {
            DiscreteKeyframe pasted = entry.discrete_keyframe;
            pasted.time = local_time;
            prop.push_keyframe(pasted);
        } else if (entry.is_extension) {
            QJsonObject pasted = entry.extension_keyframe;
            pasted.insert(QStringLiteral("time"), local_time);
            prop.push_keyframe(pasted);
        } else if (entry.is_scalar_group) {
            std::vector<Keyframe> pasted = entry.scalar_group_keyframes;
            for (Keyframe &keyframe : pasted)
                keyframe.time = local_time;
            prop.push_keyframes(pasted);
        } else if (entry.is_vector3) {
            if (prop.is_vector3()) {
                Vector3Keyframe pasted = entry.vector3_keyframe;
                pasted.time = local_time;
                prop.push_keyframe(pasted);
            } else {
                VectorKeyframe pasted = legacy_keyframe_from_vector3(
                    entry.vector3_keyframe);
                pasted.time = local_time;
                prop.push_keyframe(pasted);
            }
        } else if (entry.is_vector) {
            if (prop.is_vector3()) {
                Vector3Keyframe pasted = vector3_keyframe_from_legacy(
                    entry.vector_keyframe);
                pasted.time = local_time;
                prop.push_keyframe(pasted);
            } else {
                VectorKeyframe pasted = entry.vector_keyframe;
                pasted.time = local_time;
                prop.push_keyframe(pasted);
            }
        } else {
            Keyframe pasted = entry.keyframe;
            pasted.time = local_time;
            if (prop.is_hold_only()) {
                pasted.value = pasted.value >= 0.5 ? 1.0 : 0.0;
                pasted.easing = EasingType::Hold;
                pasted.temporal_mode = TemporalInterpolationMode::Hold;
                pasted.temporal_velocity_explicit = true;
                pasted.temporal_tangents_linked = true;
            }
            prop.push_keyframe(pasted);
        }
        inserted_times[{target_owner, target_property}].push_back(local_time);
        changed = true;
    }

    if (!changed) return false;

    if (redirect_single_track)
        set_graph_target(redirected_owner, redirected_property);
    selected_keyframes_.clear();
    for (auto &[prop_ref, times] : inserted_times) {
        TimelinePropertyRef prop = find_timeline_property(prop_ref.first,
                                                          prop_ref.second);
        if (!prop) continue;
        prop.sort_keyframes();

        std::set<int> used;
        for (double inserted_time : times) {
            int best = -1;
            double best_distance = std::numeric_limits<double>::max();
            for (int index = 0; index < static_cast<int>(prop.keyframe_count());
                 ++index) {
                if (used.count(index)) continue;
                const double distance = std::abs(prop.keyframe_time(
                    static_cast<size_t>(index)) - inserted_time);
                if (distance < best_distance) {
                    best = index;
                    best_distance = distance;
                }
            }
            if (best >= 0) {
                used.insert(best);
                selected_keyframes_.insert({prop_ref.first, prop_ref.second,
                                            best});
            }
        }
    }

    update();
    emit keyframe_structure_changed();
    FXM_LOG_DEBUG("Animation", QStringLiteral(
        "Pasted keyframes title=%1 count=%2 origin=%3 snapped=%4 redirected=%5 target=%6/%7 replaced=%8")
        .arg(title_ ? QString::fromStdString(title_->id)
                    : QStringLiteral("<none>"))
        .arg(static_cast<int>(keyframe_clipboard_.size()))
        .arg(paste_origin, 0, 'f', 6)
        .arg(snap_origin)
        .arg(redirect_single_track)
        .arg(QString::fromStdString(redirect_single_track
            ? redirected_owner : std::string()))
        .arg(QString::fromStdString(redirect_single_track
            ? redirected_property : std::string()))
        .arg(replaced_keyframes));
    return true;
}

void TimelineWidget::begin_keyframe_drag(const std::string &layer_id, const std::string &prop_name,
                                         int kf_idx, double start_time)
{
    drag_mode_ = DragMode::Keyframe;
    drag_layer_id_ = layer_id;
    drag_prop_name_ = prop_name;
    drag_keyframe_index_ = kf_idx;
    drag_start_time_ = start_time;
    dragged_keyframes_.clear();
    prune_keyframe_selection();
    if (!is_keyframe_selected(layer_id, prop_name, kf_idx))
        selected_keyframes_ = {{layer_id, prop_name, kf_idx}};

    std::set<std::pair<const AnimatedVec3Property *, int>> seen_vector3_keys;
    for (const auto &ref : selected_keyframes_) {
        if (!title_ || timeline_owner_locked(*title_, ref.layer_id)) continue;
        auto prop = find_timeline_property(ref.layer_id, ref.prop_name);
        if (!prop || ref.index < 0 || ref.index >= (int)prop.keyframe_count()) continue;
        if (prop.is_vector3() &&
            !seen_vector3_keys.insert({prop.vector3, ref.index}).second)
            continue;
        dragged_keyframes_.push_back({ref, prop.keyframe_time(ref.index)});
    }
}

bool TimelineWidget::is_layer_selected(const std::string &layer_id) const
{
    return std::find(selected_layer_ids_.begin(), selected_layer_ids_.end(), layer_id) != selected_layer_ids_.end();
}

void TimelineWidget::select_layer_from_mouse(const std::string &layer_id, Qt::KeyboardModifiers modifiers)
{
    if (!title_ || layer_id.empty()) return;

    auto display_order = [&]() {
        std::vector<std::string> ids;
        for (auto it = title_->layers.rbegin(); it != title_->layers.rend(); ++it) {
            if (*it) ids.push_back((*it)->id);
        }
        return ids;
    };
    auto ordered_from_set = [&](const std::set<std::string> &selected) {
        std::vector<std::string> ids;
        for (const auto &layer : title_->layers) {
            if (layer && selected.find(layer->id) != selected.end())
                ids.push_back(layer->id);
        }
        return ids;
    };

    std::vector<std::string> next_ids;
    if (modifiers & Qt::ShiftModifier) {
        std::set<std::string> selected;
        if (modifiers & Qt::ControlModifier)
            selected.insert(selected_layer_ids_.begin(), selected_layer_ids_.end());

        const std::string anchor = selection_anchor_layer_id_.empty()
            ? (selected_layer_ids_.empty() ? layer_id : selected_layer_ids_.back())
            : selection_anchor_layer_id_;
        const auto order = display_order();
        auto anchor_it = std::find(order.begin(), order.end(), anchor);
        auto clicked_it = std::find(order.begin(), order.end(), layer_id);
        if (anchor_it != order.end() && clicked_it != order.end()) {
            const int a = (int)std::distance(order.begin(), anchor_it);
            const int b = (int)std::distance(order.begin(), clicked_it);
            const int first = std::min(a, b);
            const int last = std::max(a, b);
            for (int i = first; i <= last; ++i)
                selected.insert(order[(size_t)i]);
        } else {
            selected.insert(layer_id);
        }
        next_ids = ordered_from_set(selected);
    } else if (modifiers & Qt::ControlModifier) {
        next_ids = selected_layer_ids_;
        auto existing = std::find(next_ids.begin(), next_ids.end(), layer_id);
        if (existing == next_ids.end())
            next_ids.push_back(layer_id);
        else
            next_ids.erase(existing);
        selection_anchor_layer_id_ = layer_id;
    } else if (is_layer_selected(layer_id) && selected_layer_ids_.size() > 1) {
        next_ids = selected_layer_ids_;
    } else {
        next_ids.push_back(layer_id);
        selection_anchor_layer_id_ = layer_id;
    }

    selected_layer_ids_ = next_ids;
    sel_layer_id_ = selected_layer_ids_.empty() ? std::string() : selected_layer_ids_.back();
    if (selected_layer_ids_.empty())
        selection_anchor_layer_id_.clear();
    update();
    emit layers_selected(selected_layer_ids_);
}

void TimelineWidget::begin_layer_strip_drag(const std::string &layer_id, DragMode mode, double start_time)
{
    if (!title_) return;
    drag_mode_ = mode;
    drag_layer_id_ = layer_id;
    drag_start_time_ = start_time;
    dragged_layer_strips_.clear();

    std::set<std::string> ids;
    if (is_layer_selected(layer_id))
        ids.insert(selected_layer_ids_.begin(), selected_layer_ids_.end());
    ids.insert(layer_id);

    for (const auto &layer : title_->layers) {
        if (!layer || layer->locked || layer->linked_media_stream ||
            ids.find(layer->id) == ids.end()) continue;
        DraggedLayerStrip dragged;
        dragged.layer_id = layer->id;
        dragged.start_in = layer->in_time;
        dragged.start_out = layer->out_time;
        dragged.start_media_in = layer->type == LayerType::Audio
            ? layer->audio_in_point : layer->video_in_point;
        dragged.start_media_out = layer->type == LayerType::Audio
            ? layer->audio_out_point : layer->video_out_point;
        for (auto prop : timeline_properties(*layer)) {
            if (!prop) continue;
            for (int i = 0; i < (int)prop.keyframe_count(); ++i)
                dragged.keyframes.push_back({prop.name(), i, prop.keyframe_time((size_t)i)});
        }
        dragged_layer_strips_.push_back(std::move(dragged));
    }

    if (auto primary = title_->find_layer(layer_id)) {
        drag_start_in_ = primary->in_time;
        drag_start_out_ = primary->out_time;
    }
}

struct VisibleTimelineRow {
    int row = 0;
    TimelineRow entry;
};

static std::vector<VisibleTimelineRow> visible_timeline_rows(const std::shared_ptr<Title> &title,
                                                             int first_row, int last_row)
{
    std::vector<VisibleTimelineRow> visible;
    if (!title || last_row < first_row) return visible;
    const auto rows = timeline_rows(title);
    const int begin = std::clamp(first_row, 0, static_cast<int>(rows.size()));
    const int end = std::clamp(last_row + 1, begin, static_cast<int>(rows.size()));
    visible.reserve(static_cast<size_t>(std::max(0, end - begin)));
    for (int row = begin; row < end; ++row)
        visible.push_back({row, rows[static_cast<size_t>(row)]});
    return visible;
}

static QString timeline_blend_mode_short(EffectBlendMode mode)
{
    switch (mode) {
    case EffectBlendMode::Multiply: return fxm_tr("OBSTitles.BlendShortMultiply");
    case EffectBlendMode::Additive: return fxm_tr("OBSTitles.BlendShortAdditive");
    case EffectBlendMode::Screen: return fxm_tr("OBSTitles.BlendShortScreen");
    case EffectBlendMode::Overlay: return fxm_tr("OBSTitles.BlendShortOverlay");
    case EffectBlendMode::Color: return fxm_tr("OBSTitles.BlendShortColor");
    case EffectBlendMode::Normal:
    default: return fxm_tr("OBSTitles.BlendShortNormal");
    }
}

static QString timeline_layer_switches_text(const Title &title, const Layer &layer)
{
    QStringList tags;
    if (layer.mask_mode != MaskMode::None && !layer.mask_source_id.empty())
        tags << (layer.mask_mode == MaskMode::InvertedAlpha ? fxm_tr("OBSTitles.TrackMatteInvAlpha") :
                 layer.mask_mode == MaskMode::Luma ? fxm_tr("OBSTitles.TrackMatteLuma") :
                 layer.mask_mode == MaskMode::InvertedLuma ? fxm_tr("OBSTitles.TrackMatteInvLuma") :
                 layer.mask_mode == MaskMode::Clipping ? fxm_tr("OBSTitles.TrackMatteClipping") :
                 layer.mask_mode == MaskMode::InvertedClipping ? fxm_tr("OBSTitles.TrackMatteInvClipping") :
                 fxm_tr("OBSTitles.TrackMatteAlpha"));
    if (!layer.transform_parent_id.empty()) {
        QString parent_name = fxm_tr("OBSTitles.Parent");
        if (auto parent = title.find_layer(layer.transform_parent_id))
            parent_name = QString::fromStdString(parent->name);
        tags << fxm_tr("OBSTitles.ParentNamed").arg(parent_name);
    }
    if (layer.blend_mode != EffectBlendMode::Normal)
        tags << fxm_tr("OBSTitles.ModeNamed").arg(timeline_blend_mode_short(layer.blend_mode));
    return tags.join(QStringLiteral("  ·  "));
}

void TimelineWidget::paintEvent(QPaintEvent *ev)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    int W = width(), H = height();
    const QRect dirty = ev ? ev->rect().intersected(rect()) : rect();
    if (dirty.isEmpty()) return;

    QElapsedTimer paint_cost;
    paint_cost.start();
    const auto record_paint_cost = [this, &paint_cost, &dirty]() {
        paint_profile_total_us_ += paint_cost.nsecsElapsed() / 1000;
        ++paint_profile_samples_;
        if (!paint_profile_window_.isValid())
            paint_profile_window_.start();
        if (paint_profile_window_.elapsed() < 1000)
            return;
        const double average_us = paint_profile_samples_ > 0
            ? static_cast<double>(paint_profile_total_us_) /
                  static_cast<double>(paint_profile_samples_)
            : 0.0;
        FXM_LOG_TRACE("Performance", QStringLiteral(
            "Timeline paint samples=%1 averageUs=%2 graph=%3 dirty=%4x%5")
            .arg(paint_profile_samples_)
            .arg(average_us, 0, 'f', 1)
            .arg(graph_editor_enabled_)
            .arg(dirty.width()).arg(dirty.height()));
        paint_profile_total_us_ = 0;
        paint_profile_samples_ = 0;
        paint_profile_window_.restart();
    };

    p.setClipRect(dirty);
    if (graph_editor_enabled_) {
        paint_graph_editor(p, dirty);
        record_paint_cost();
        return;
    }

    int rh = ruler_height(), rowh = row_height();
    const QPalette pal = palette();
    auto with_alpha = [](QColor color, int alpha) {
        color.setAlpha(alpha);
        return color;
    };
    auto subtle = [](const QColor &color) {
        return color.lightness() < 128 ? color.lighter(108) : color.darker(104);
    };

    const QColor window = pal.color(QPalette::Window);
    const QColor text = pal.color(QPalette::WindowText);
    const QColor disabled_text = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor border = pal.color(QPalette::Mid);
    const QColor dark = pal.color(QPalette::Dark);
    const QColor highlight = pal.color(QPalette::Highlight);
    const QColor highlighted_text = pal.color(QPalette::HighlightedText);
    const QColor ruler_bg = window.lightness() < 128 ? window.darker(116) : window.darker(106);
    const QColor property_bg = subtle(window);
    const QColor selected_row = with_alpha(highlight, window.lightness() < 128 ? 90 : 65);
    const QColor tick_major = with_alpha(text, 150);
    const QColor tick_minor = with_alpha(text, 80);
    const QColor label_text = with_alpha(text, 165);
    const QColor playhead_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Current);
    const QColor pause_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Pause);
    const QColor loop_color = TitlePreferences::timeline_color(TitlePreferences::TimelineColorRole::Loop);
    const QColor handle_color = with_alpha(text, 150);

    auto refresh_audio_waveform_from_store = [&](const std::shared_ptr<Layer> &audio_layer) {
        if (!audio_layer || audio_layer->type != LayerType::Audio || !title_)
            return;
        auto stored = TitleDataStore::instance().get_title(title_->id);
        if (!stored)
            return;
        auto runtime_layer = stored->find_layer(audio_layer->id);
        if (!runtime_layer)
            return;
        if (!runtime_layer->audio_waveform.empty()) {
            audio_layer->audio_waveform = runtime_layer->audio_waveform;
            audio_layer->audio_waveform_duration = runtime_layer->audio_waveform_duration;
        }
        audio_layer->audio_waveform_progress_percent =
            runtime_layer->audio_waveform_progress_percent;
        audio_layer->audio_waveform_generating =
            runtime_layer->audio_waveform_generating;
        audio_layer->audio_waveform_progress_label =
            runtime_layer->audio_waveform_progress_label;
    };

    auto draw_audio_waveform_lane = [&](const std::shared_ptr<Layer> &audio_layer,
                                        const QRect &lane_rect,
                                        bool master_muted,
                                        const QColor &lane_foreground) {
        if (!audio_layer || audio_layer->type != LayerType::Audio ||
            lane_rect.width() <= 2 || lane_rect.height() <= 2)
            return;
        refresh_audio_waveform_from_store(audio_layer);
        const bool muted = master_muted || audio_layer->audio_muted;
        if (audio_layer->audio_waveform.size() >= 2) {
            p.save();
            p.setClipRect(lane_rect.adjusted(1, 1, -1, -1));
            p.setPen(QPen(with_alpha(lane_foreground, muted ? 65 : 190), 1));
            const int center_y = lane_rect.center().y();
            const int half_h = std::max(1, lane_rect.height() / 2 - 1);
            const int pairs = static_cast<int>(audio_layer->audio_waveform.size() / 2);
            const double asset_duration = audio_layer->audio_waveform_duration > 0.0
                ? audio_layer->audio_waveform_duration
                : std::max(audio_layer->audio_out_point,
                           audio_layer->audio_in_point +
                               std::max(0.0, audio_layer->out_time - audio_layer->in_time));
            const double media_begin = std::clamp(audio_layer->audio_in_point, 0.0,
                                                  std::max(0.0, asset_duration));
            const double available_end = audio_layer->audio_out_point > media_begin
                ? std::min(audio_layer->audio_out_point, asset_duration)
                : asset_duration;
            const double available_span = std::max(0.0, available_end - media_begin);
            const double timeline_span = std::max(0.0, audio_layer->out_time - audio_layer->in_time);
            const bool repeats = audio_layer->audio_loop ||
                audio_layer->audio_playback_mode == AudioPlaybackMode::Loop;
            for (int px = 0; px < lane_rect.width(); ++px) {
                const double timeline_offset = timeline_span * (double(px) + 0.5) /
                    std::max(1, lane_rect.width());
                double media_time = media_begin + timeline_offset;
                if (repeats && available_span > 0.0)
                    media_time = media_begin + std::fmod(timeline_offset, available_span);
                else
                    media_time = std::min(media_time, available_end);
                const double normalized = asset_duration > 0.0
                    ? std::clamp(media_time / asset_duration, 0.0, 1.0)
                    : 0.0;
                const int pair = std::clamp(static_cast<int>(normalized * (pairs - 1)),
                                            0, pairs - 1);
                const float lo = audio_layer->audio_waveform[static_cast<size_t>(pair * 2)];
                const float hi = audio_layer->audio_waveform[static_cast<size_t>(pair * 2 + 1)];
                p.drawLine(lane_rect.left() + px, center_y - qRound(hi * half_h),
                           lane_rect.left() + px, center_y - qRound(lo * half_h));
            }
            p.restore();
            return;
        }
        if (audio_layer->audio_waveform_generating) {
            const int percent = std::clamp(audio_layer->audio_waveform_progress_percent, 0, 99);
            QRect progress_rect = lane_rect.adjusted(2, lane_rect.height() - 4, -2, -1);
            p.fillRect(progress_rect, with_alpha(lane_foreground, 45));
            QRect fill_rect = progress_rect;
            fill_rect.setWidth(std::max(1, progress_rect.width() * percent / 100));
            p.fillRect(fill_rect, with_alpha(lane_foreground, 150));
            if (lane_rect.height() >= 10) {
                QString label = audio_layer->audio_waveform_progress_label.empty()
                    ? QString::fromStdString(audio_layer->name)
                    : QString::fromStdString(audio_layer->audio_waveform_progress_label);
                label = QStringLiteral("%1% · %2").arg(percent).arg(label);
                p.setPen(with_alpha(lane_foreground, 160));
                p.drawText(lane_rect.adjusted(4, 0, -4, -4),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           QFontMetrics(p.font()).elidedText(label, Qt::ElideRight,
                                                             lane_rect.width() - 8));
            }
        }
    };

    auto linked_audio_stream_layers = [&](const std::string &video_id) {
        std::vector<std::shared_ptr<Layer>> streams;
        if (!title_)
            return streams;
        for (const auto &candidate : title_->layers) {
            if (!candidate || candidate->type != LayerType::Audio ||
                !candidate->linked_media_stream ||
                candidate->linked_media_layer_id != video_id)
                continue;
            streams.push_back(candidate);
        }
        std::sort(streams.begin(), streams.end(), [](const auto &a, const auto &b) {
            if (!a || !b) return bool(a) > bool(b);
            if (a->audio_stream_index != b->audio_stream_index)
                return a->audio_stream_index < b->audio_stream_index;
            return a->name < b->name;
        });
        return streams;
    };

    /* Background */
    p.fillRect(dirty, window);

    const double dur = title_ ? std::max(0.0, title_->duration) : 10.0;
    const double pre_roll = timeline_pre_roll();
    const double post_roll = timeline_post_roll();
    const double display_dur = timeline_display_duration();
    const bool show_stinger_rolls = title_ &&
        title_->graphic_type == TitleGraphicType::Stinger &&
        (pre_roll > 0.0 || post_roll > 0.0);
    const int transition_start_x = display_time_to_x(0.0);
    const int animation_start_x = time_to_x(0.0);
    const int animation_end_x = time_to_x(dur);
    const int transition_end_x = display_time_to_x(display_dur);
    const QColor pre_roll_color = with_alpha(QColor(68, 126, 172), window.lightness() < 128 ? 72 : 48);
    const QColor post_roll_color = with_alpha(QColor(178, 112, 64), window.lightness() < 128 ? 72 : 48);
    const double fps = obs_frame_rate();
    const double frame_step = obs_frame_duration();
    const double pixels_per_frame = std::max(0.000001, pixels_per_sec_ * frame_step);

    /* Build a density-safe ruler scale.  Drawing every frame at every zoom
     * level makes both tick marks and labels collapse into an unreadable band.
     * Select a "nice" frame-aligned major interval that leaves enough room for
     * the widest visible label, then derive a minor interval that is never
     * closer than a few device pixels. */
    const QFontMetrics ruler_fm(p.font());
    const int max_visible_seconds = std::max(0, (int)std::ceil(
        std::max({dur, pre_roll, post_roll})));
    const int widest_label = std::max(ruler_fm.horizontalAdvance(QStringLiteral("+00f")),
                                      ruler_fm.horizontalAdvance(QStringLiteral("%1s").arg(max_visible_seconds)));
    const double target_major_px = std::max(56.0, (double)widest_label + 14.0);
    const double target_major_frames = target_major_px / pixels_per_frame;

    auto nice_frame_interval = [](double minimum_frames) {
        if (minimum_frames <= 1.0)
            return 1;
        const double exponent = std::floor(std::log10(minimum_frames));
        const double magnitude = std::pow(10.0, exponent);
        const double normalized = minimum_frames / magnitude;
        double nice = 10.0;
        if (normalized <= 1.0) nice = 1.0;
        else if (normalized <= 2.0) nice = 2.0;
        else if (normalized <= 5.0) nice = 5.0;
        return std::max(1, (int)std::ceil(nice * magnitude));
    };

    const int major_frames = nice_frame_interval(target_major_frames);
    int minor_frames = major_frames;
    for (int divisor : {10, 5, 2}) {
        const int candidate = std::max(1, major_frames / divisor);
        if (major_frames % divisor == 0 && candidate * pixels_per_frame >= 5.0) {
            minor_frames = candidate;
            break;
        }
    }

    const int first_visible_display_frame = std::max(0, (int)std::floor(
        x_to_display_time(dirty.left()) / frame_step) - minor_frames);
    const int last_visible_display_frame = (int)std::ceil(
        x_to_display_time(dirty.right()) / frame_step) + minor_frames;
    const int pre_roll_frames = std::max(0, (int)std::round(pre_roll / frame_step));
    const int first_relative_frame = (int)std::floor(
        (double)(first_visible_display_frame - pre_roll_frames) / minor_frames) * minor_frames;
    const int last_relative_frame = (int)std::ceil(
        (double)(last_visible_display_frame - pre_roll_frames) / minor_frames) * minor_frames;

    auto draw_header = [&]() {
        const QRect header_dirty = dirty.intersected(QRect(0, 0, W, rh));
        if (header_dirty.isEmpty())
            return;

        p.save();
        p.setClipRect(header_dirty);

        /* Compact ruler/header. Keep this height matched with LayerStack's
         * column header so the first layer row starts at the same Y position
         * in both panes. */
        p.fillRect(QRect(0, 0, W, rh), ruler_bg);
        if (show_stinger_rolls) {
            if (pre_roll > 0.0) {
                const QRect pre_rect = QRect(transition_start_x, 0,
                    animation_start_x - transition_start_x, rh).normalized();
                p.fillRect(pre_rect, QBrush(pre_roll_color, Qt::BDiagPattern));
            }
            if (post_roll > 0.0) {
                const QRect post_rect = QRect(animation_end_x, 0,
                    transition_end_x - animation_end_x, rh).normalized();
                p.fillRect(post_rect, QBrush(post_roll_color, Qt::FDiagPattern));
            }
        }
        p.setPen(border);
        p.drawLine(0, rh - 1, W, rh - 1);

        /* Keep the ruler visually stable by assigning separate vertical
         * bands to labels, ticks and the cache status strip.  Previously the
         * cache strip was painted over the lower portion of the labels. */
        const int cache_h = 5;
        const int cache_y = rh - cache_h - 1;
        const int tick_baseline = cache_y - 1;
        const int label_top = 2;
        const int label_height = std::max(14, tick_baseline - 12);

        int last_label_right = std::numeric_limits<int>::min();
        const int rounded_fps = std::max(1, (int)std::round(fps));
        auto ruler_time_text = [rounded_fps](int relative_frame) {
            const bool negative = relative_frame < 0;
            const int absolute_frame = std::abs(relative_frame);
            const int seconds = absolute_frame / rounded_fps;
            const int frame_in_second = absolute_frame % rounded_fps;
            if (!negative) {
                return frame_in_second == 0
                    ? QStringLiteral("%1s").arg(seconds)
                    : QStringLiteral("%1s+%2f").arg(seconds).arg(frame_in_second, 2, 10, QChar('0'));
            }
            if (seconds == 0)
                return QStringLiteral("-%1f").arg(frame_in_second);
            return frame_in_second == 0
                ? QStringLiteral("-%1s").arg(seconds)
                : QStringLiteral("-%1s+%2f").arg(seconds).arg(frame_in_second, 2, 10, QChar('0'));
        };
        for (int relative_frame = first_relative_frame; relative_frame <= last_relative_frame;
             relative_frame += minor_frames) {
            const double display_time = pre_roll + relative_frame * frame_step;
            if (display_time < -frame_step || display_time > display_dur + frame_step)
                continue;
            const int x = display_time_to_x(display_time);
            if (x < dirty.left() - widest_label || x > dirty.right() + widest_label)
                continue;

            const bool is_major = (relative_frame % major_frames) == 0;
            p.setPen(is_major ? tick_major : tick_minor);
            p.drawLine(x, tick_baseline - (is_major ? 10 : 4), x, tick_baseline);
            if (!is_major)
                continue;

            const QString ruler_text = ruler_time_text(relative_frame);
            const int label_width = ruler_fm.horizontalAdvance(ruler_text);
            const int label_left = x + 3;
            if (label_left <= last_label_right + 6)
                continue;

            p.setPen(label_text);
            p.drawText(QRect(label_left, label_top, label_width + 4, label_height),
                       Qt::AlignLeft | Qt::AlignVCenter, ruler_text);
            last_label_right = label_left + label_width;
        }

        if (title_) {
            const int frame_w = std::max(1, (int)std::ceil(pixels_per_sec_ * frame_step));
            auto state_color = [](FrameCacheState state, bool static_frame) {
                switch (state) {
                case FrameCacheState::Queued: return QColor(96, 96, 96);
                case FrameCacheState::Rendering: return QColor(255, 202, 74);
                case FrameCacheState::CachedRam:
                    /* Dynamic RAM frames stay bright green; visually static/reused
                     * RAM frames are darker so the user can see cache reuse spans. */
                    return static_frame ? QColor(21, 112, 67) : QColor(39, 186, 103);
                case FrameCacheState::CachedDisk:
                    /* Disk-resident static frames are blue, distinct from RAM. */
                    return static_frame ? QColor(45, 105, 190) : QColor(74, 144, 226);
                case FrameCacheState::Stale: return QColor(214, 90, 90);
                case FrameCacheState::Disabled: return QColor(95, 95, 95);
                case FrameCacheState::NotCached:
                default: return QColor(0, 0, 0, 0);
                }
            };
            const bool cache_disabled = !CacheManager::instance().cacheEnabled() ||
                CacheManager::instance().titleCacheability(title_) == TitleCacheability::NonCacheable;
            if (cache_disabled) {
                const int cache_left = std::min(animation_start_x, animation_end_x);
                const int cache_width = std::max(0, std::abs(animation_end_x - animation_start_x));
                p.fillRect(QRect(cache_left, cache_y, cache_width, cache_h),
                           state_color(FrameCacheState::Disabled, false));
            } else {
                const int total_frames = std::max(0, (int)std::ceil(dur / frame_step));
                const double visible_document_start = x_to_display_time(dirty.left()) - pre_roll;
                const double visible_document_end = x_to_display_time(dirty.right()) - pre_roll;
                const int cache_first_frame = std::clamp(
                    (int)std::floor(visible_document_start / frame_step) - 1, 0, total_frames);
                const int cache_last_frame = std::clamp(
                    (int)std::ceil(visible_document_end / frame_step) + 1, cache_first_frame, total_frames);
                const QHash<int, FrameCacheState> cache_states =
                    CacheManager::instance().displayStatesForRange(
                        title_, cache_first_frame, cache_last_frame);
                const QHash<int, bool> static_frames =
                    CacheManager::instance().displayStaticFramesForRange(
                        title_, cache_first_frame, cache_last_frame);
                for (int frame = cache_first_frame; frame <= cache_last_frame; ++frame) {
                    const FrameCacheState state = cache_states.value(
                        frame, FrameCacheState::NotCached);
                    if (state == FrameCacheState::NotCached) continue;
                    const bool static_frame =
                        (state == FrameCacheState::CachedRam ||
                         state == FrameCacheState::CachedDisk) &&
                        static_frames.value(frame, false);
                    const QColor color = state_color(state, static_frame);
                    if (color.alpha() == 0) continue;
                    const int x = time_to_x(frame * frame_step);
                    p.fillRect(QRect(x, cache_y, frame_w, cache_h), color);
                }
            }
            p.setPen(with_alpha(text, 70));
            p.drawLine(0, cache_y + cache_h, W, cache_y + cache_h);
        }

        if (title_ && title_->playback_mode == 1) {
            int loop_x0 = time_to_x(std::clamp(title_->loop_start, 0.0, dur));
            int loop_x1 = time_to_x(std::clamp(title_->loop_end, title_->loop_start, dur));
            if (loop_x1 > loop_x0) {
                const int marker_top = std::max(label_top + label_height, tick_baseline - 11);
                p.fillRect(loop_x0, marker_top, loop_x1 - loop_x0,
                           std::max(1, tick_baseline - marker_top + 1), with_alpha(loop_color, 45));
                p.setPen(QPen(loop_color, 2));
                p.drawLine(loop_x0, marker_top, loop_x0, H);
                p.drawLine(loop_x1, marker_top, loop_x1, H);
                p.setPen(loop_color.lightness() < 128 ? loop_color.lighter(170) : loop_color.darker(170));
                p.drawText(loop_x0 + 4, label_top, 80, label_height, Qt::AlignVCenter, fxm_tr("OBSTitles.LoopIn"));
                p.drawText(loop_x1 + 4, label_top, 80, label_height, Qt::AlignVCenter, fxm_tr("OBSTitles.LoopOut"));
            }
        }
        if (title_ && title_->playback_mode == 2) {
            int pause_x = time_to_x(std::clamp(title_->pause_time, 0.0, dur));
            const int marker_top = std::max(label_top + label_height, tick_baseline - 11);
            p.setPen(QPen(pause_color, 2));
            p.drawLine(pause_x, marker_top, pause_x, tick_baseline);
            p.setBrush(pause_color);
            p.setPen(Qt::NoPen);
            QPolygon marker;
            marker << QPoint(pause_x - 6, marker_top)
                   << QPoint(pause_x + 6, marker_top)
                   << QPoint(pause_x, std::min(tick_baseline, marker_top + 10));
            p.drawPolygon(marker);
            p.setPen(pause_color.lightness() < 128 ? pause_color.lighter(170) : pause_color.darker(170));
            p.drawText(pause_x + 4, label_top, 100, label_height, Qt::AlignVCenter, fxm_tr("OBSTitles.Pause"));
            p.setBrush(Qt::NoBrush);
        }
        if (title_ && title_->graphic_type == TitleGraphicType::Stinger &&
            title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint) {
            const int switch_x = time_to_x(stinger_transition_point_seconds(*title_));
            const int marker_top = std::max(label_top + label_height, tick_baseline - 11);
            const QColor stinger_color(210, 72, 170);
            p.setPen(QPen(stinger_color, 2));
            p.drawLine(switch_x, marker_top, switch_x, tick_baseline);
            p.setBrush(stinger_color);
            p.setPen(Qt::NoPen);
            QPolygon marker;
            marker << QPoint(switch_x - 7, marker_top)
                   << QPoint(switch_x + 7, marker_top)
                   << QPoint(switch_x, std::min(tick_baseline, marker_top + 11));
            p.drawPolygon(marker);
            p.setPen(stinger_color.lighter(150));
            p.drawText(switch_x + 5, label_top, 110, label_height, Qt::AlignVCenter,
                       fxm_tr("OBSTitles.StingerSceneSwitch"));
            p.setBrush(Qt::NoBrush);
        }
        if (show_stinger_rolls) {
            auto draw_roll_label = [&](int left, int right, const QString &label, const QColor &color) {
                const int zone_left = std::min(left, right);
                const int zone_width = std::abs(right - left);
                const int text_width = ruler_fm.horizontalAdvance(label) + 10;
                if (zone_width < text_width + 4)
                    return;
                QRect label_rect(zone_left + (zone_width - text_width) / 2, label_top,
                                 text_width, label_height);
                label_rect = label_rect.intersected(QRect(0, 0, W, rh));
                if (label_rect.width() < text_width / 2)
                    return;
                p.fillRect(label_rect, with_alpha(ruler_bg, 205));
                QColor label_color = color.lighter(165);
                label_color.setAlpha(220);
                p.setPen(label_color);
                p.drawText(label_rect, Qt::AlignCenter, label);
            };
            if (pre_roll > 0.0)
                draw_roll_label(transition_start_x, animation_start_x,
                                fxm_tr("OBSTitles.StingerPreRoll"), pre_roll_color);
            if (post_roll > 0.0)
                draw_roll_label(animation_end_x, transition_end_x,
                                fxm_tr("OBSTitles.StingerPostRoll"), post_roll_color);
            p.setPen(QPen(with_alpha(text, 115), 1, Qt::DashLine));
            p.drawLine(animation_start_x, 0, animation_start_x, rh);
            p.drawLine(animation_end_x, 0, animation_end_x, rh);
        }
        p.restore();
    };

    /* Layer/property rows.  This uses the same row model as LayerStack so
     * keyframed property rows stay vertically aligned with the layer list.
     */
    const QRect body_dirty = dirty.intersected(QRect(0, rh, W, std::max(0, H - rh)));
    if (!body_dirty.isEmpty()) {
        p.save();
        p.setClipRect(body_dirty);
        const int first_dirty_row = std::max(0, (body_dirty.top() - rh + scroll_y_) / rowh);
        const int last_dirty_row = (body_dirty.bottom() - rh + scroll_y_) / rowh;
        const auto rows = visible_timeline_rows(title_, first_dirty_row, last_dirty_row);
        for (const auto &visible_row : rows) {
            const int row = visible_row.row;
            const auto &entry = visible_row.entry;
            auto &layer = entry.layer;
            int y = rh + row * rowh - scroll_y_;
            if (!body_dirty.intersects(QRect(0, y, W, rowh))) continue;
            bool sel = is_layer_selected(layer->id);

            const bool graph_target_row = entry.is_property &&
                entry.owner_id == graph_target_owner_id_ &&
                entry.prop.name() == graph_target_property_name_ &&
                (entry.is_property_channel
                    ? graph_channel_component() == entry.property_channel
                    : graph_channel_mode_ == GraphChannelMode::All);
            p.fillRect(0, y, W, rowh,
                       entry.is_effect_group
                           ? property_bg
                           : entry.is_property
                               ? (graph_target_row ? selected_row : property_bg)
                               : sel ? selected_row : window);
            p.setPen(border);
            p.drawLine(0, y + rowh - 1, W, y + rowh - 1);

            int x0 = time_to_x(layer->in_time);
            int x1 = time_to_x(layer->out_time);
            if (entry.is_effect_group) {
                QRect effect_band(std::min(x0, x1), y + 6,
                                  std::max(1, std::abs(x1 - x0)), rowh - 12);
                p.fillRect(effect_band, with_alpha(highlight, 34));
                p.setPen(with_alpha(text, 150));
                p.drawText(effect_band.adjusted(6, 0, -4, 0),
                           Qt::AlignVCenter | Qt::AlignLeft,
                           QStringLiteral("FX  %1").arg(entry.owner_label));
                continue;
            }
            if (!entry.is_property) {
                QRect strip_rect(std::min(x0, x1), y + 3, std::abs(x1 - x0), rowh - 6);
                QColor bar_col = layer_color(*layer, row);
                if (!layer->visible) {
                    const int gray = qGray(bar_col.rgb());
                    bar_col = QColor(gray, gray, gray).darker(135);
                }
                if (sel) bar_col = bar_col.lighter(125);
                const QColor strip_foreground =
                    fxm_background_aware_foreground(bar_col, pal);
                const QColor strip_muted_foreground =
                    fxm_background_aware_muted_foreground(bar_col, pal);
                const QColor strip_waveform_foreground =
                    fxm_background_aware_opposite_foreground(bar_col, pal);
                p.fillRect(strip_rect, bar_col);
                if (layer->type == LayerType::Video) {
                    const auto streams = linked_audio_stream_layers(layer->id);
                    if (!streams.empty() && strip_rect.width() > 4) {
                        const int usable_h = std::max(1, strip_rect.height() - 4);
                        const int lane_count = std::max(1, static_cast<int>(streams.size()));
                        const int lane_h = std::max(3, usable_h / lane_count);
                        for (int stream_i = 0; stream_i < lane_count; ++stream_i) {
                            const int lane_top = strip_rect.top() + 2 + stream_i * lane_h;
                            QRect lane_rect(strip_rect.left() + 2, lane_top,
                                            std::max(1, strip_rect.width() - 4),
                                            std::max(1, std::min(lane_h, strip_rect.bottom() - lane_top)));
                            draw_audio_waveform_lane(
                                streams[static_cast<size_t>(stream_i)], lane_rect,
                                layer->audio_muted, strip_waveform_foreground);
                        }
                    }
                }
                if (layer->type == LayerType::Audio && layer->audio_waveform.empty() && title_) {
                    refresh_audio_waveform_from_store(layer);
                    if (layer->audio_waveform.empty())
                        draw_audio_waveform_lane(
                            layer, strip_rect, false, strip_waveform_foreground);
                }
                if (layer->type == LayerType::Audio && layer->audio_waveform.size() >= 2 && strip_rect.width() > 2) {
                    p.save();
                    p.setClipRect(strip_rect.adjusted(1, 1, -1, -1));
                    p.setPen(QPen(with_alpha(
                        strip_waveform_foreground,
                        layer->audio_muted ? 70 : 190), 1));
                    const int center_y = strip_rect.center().y();
                    const int half_h = std::max(1, strip_rect.height() / 2 - 3);
                    const int pairs = static_cast<int>(layer->audio_waveform.size() / 2);
                    const double asset_duration = layer->audio_waveform_duration > 0.0
                        ? layer->audio_waveform_duration
                        : std::max(layer->audio_out_point, layer->audio_in_point + std::max(0.0, layer->out_time - layer->in_time));
                    const double media_begin = std::clamp(layer->audio_in_point, 0.0, std::max(0.0, asset_duration));
                    const double available_end = layer->audio_out_point > media_begin
                        ? std::min(layer->audio_out_point, asset_duration)
                        : asset_duration;
                    const double available_span = std::max(0.0, available_end - media_begin);
                    const double timeline_span = std::max(0.0, layer->out_time - layer->in_time);
                    const bool repeats = layer->audio_loop || layer->audio_playback_mode == AudioPlaybackMode::Loop;

                    for (int px = 0; px < strip_rect.width(); ++px) {
                        const double timeline_offset = timeline_span * (double(px) + 0.5) /
                            std::max(1, strip_rect.width());
                        double media_time = media_begin + timeline_offset;
                        if (repeats && available_span > 0.0)
                            media_time = media_begin + std::fmod(timeline_offset, available_span);
                        else
                            media_time = std::min(media_time, available_end);

                        const double normalized = asset_duration > 0.0
                            ? std::clamp(media_time / asset_duration, 0.0, 1.0)
                            : 0.0;
                        const int pair = std::clamp(static_cast<int>(normalized * (pairs - 1)), 0, pairs - 1);
                        const float lo = layer->audio_waveform[static_cast<size_t>(pair * 2)];
                        const float hi = layer->audio_waveform[static_cast<size_t>(pair * 2 + 1)];
                        p.drawLine(strip_rect.left() + px, center_y - qRound(hi * half_h),
                                   strip_rect.left() + px, center_y - qRound(lo * half_h));
                    }
                    p.restore();
                }
                if (layer->type == LayerType::Audio && strip_rect.width() > 4) {
                    const double clip_duration = std::max(0.0, layer->out_time - layer->in_time);
                    const double fade_in = std::clamp(layer->audio_fade_in, 0.0, clip_duration);
                    const double fade_out = std::clamp(layer->audio_fade_out, 0.0, std::max(0.0, clip_duration - fade_in));
                    const int fade_in_x = time_to_x(layer->in_time + fade_in);
                    const int fade_out_x = time_to_x(layer->out_time - fade_out);
                    const QColor fade_fill = with_alpha(strip_foreground, 42);
                    const QColor fade_line = with_alpha(strip_foreground, 210);
                    p.save();
                    p.setClipRect(strip_rect.adjusted(1, 1, -1, -1));
                    p.setPen(QPen(fade_line, 1.5));
                    p.setBrush(fade_fill);
                    auto fade_y = [&](double u, bool fade_out_curve) {
                        u = std::clamp(u, 0.0, 1.0);
                        double gain = u;
                        switch (layer->audio_fade_curve) {
                        case AudioFadeCurve::Smooth:
                            gain = u * u * (3.0 - 2.0 * u);
                            break;
                        case AudioFadeCurve::EqualPower:
                            gain = std::sin(u * 1.5707963267948966);
                            break;
                        case AudioFadeCurve::Linear:
                        default:
                            break;
                        }
                        if (fade_out_curve) gain = 1.0 - gain;
                        return strip_rect.bottom() - gain * strip_rect.height();
                    };
                    if (fade_in > 0.0 && fade_in_x > strip_rect.left()) {
                        QPainterPath curve;
                        curve.moveTo(strip_rect.left(), fade_y(0.0, false));
                        constexpr int kCurveSteps = 24;
                        for (int step = 1; step <= kCurveSteps; ++step) {
                            const double u = double(step) / double(kCurveSteps);
                            const double x = strip_rect.left() + u * (fade_in_x - strip_rect.left());
                            curve.lineTo(x, fade_y(u, false));
                        }
                        QPainterPath fill = curve;
                        fill.lineTo(fade_in_x, strip_rect.bottom());
                        fill.lineTo(strip_rect.left(), strip_rect.bottom());
                        fill.closeSubpath();
                        p.fillPath(fill, fade_fill);
                        p.drawPath(curve);
                        p.drawLine(fade_in_x, strip_rect.top(), fade_in_x, strip_rect.bottom());
                    }
                    if (fade_out > 0.0 && fade_out_x < strip_rect.right()) {
                        QPainterPath curve;
                        curve.moveTo(fade_out_x, fade_y(0.0, true));
                        constexpr int kCurveSteps = 24;
                        for (int step = 1; step <= kCurveSteps; ++step) {
                            const double u = double(step) / double(kCurveSteps);
                            const double x = fade_out_x + u * (strip_rect.right() - fade_out_x);
                            curve.lineTo(x, fade_y(u, true));
                        }
                        QPainterPath fill = curve;
                        fill.lineTo(strip_rect.right(), strip_rect.bottom());
                        fill.lineTo(fade_out_x, strip_rect.bottom());
                        fill.closeSubpath();
                        p.fillPath(fill, fade_fill);
                        p.drawPath(curve);
                        p.drawLine(fade_out_x, strip_rect.top(), fade_out_x, strip_rect.bottom());
                    }
                    if (sel && !layer->locked) {
                        p.setBrush(fade_line);
                        p.setPen(Qt::NoPen);
                        p.drawEllipse(QPoint(fade_in_x, strip_rect.top() + 3), 3, 3);
                        p.drawEllipse(QPoint(fade_out_x, strip_rect.top() + 3), 3, 3);
                    }
                    p.restore();
                }
                if (layer->locked) {
                    p.save();
                    p.setClipRect(strip_rect);
                    p.setPen(QPen(with_alpha(strip_foreground, 170), 2));
                    for (int lx = strip_rect.left() - strip_rect.height(); lx < strip_rect.right() + strip_rect.height(); lx += 8)
                        p.drawLine(lx, strip_rect.bottom(), lx + strip_rect.height(), strip_rect.top());
                    p.restore();
                }
                p.setBrush(Qt::NoBrush);
                p.setPen(with_alpha(strip_foreground, 150));
                p.drawRect(strip_rect);

                /* Draw the normal layer label first so transition strips remain
                 * visually on top, like dedicated Premiere timeline items. */
                p.setPen(layer->visible ? strip_foreground : strip_muted_foreground);
                const QString switches = title_ ? timeline_layer_switches_text(*title_, *layer) : QString();
                QString display_name = QString::fromStdString(layer->name);
                if (title_ && layer->type == LayerType::Group) {
                    const int object_count = group_descendant_object_count(title_, layer->id);
                    const QString count_text = object_count == 1
                        ? fxm_tr("OBSTitles.GroupItemCount").arg(object_count)
                        : fxm_tr("OBSTitles.GroupItemsCount").arg(object_count);
                    display_name = QStringLiteral("%1  ·  %2").arg(display_name, count_text);
                }
                const QString layer_label = switches.isEmpty()
                    ? display_name
                    : QStringLiteral("%1    [%2]").arg(display_name, switches);
                int label_left = std::max(strip_rect.left(), 0) + 6;
                const QIcon strip_icon =
                    layer_type_icon(layer->type, strip_foreground);
                if (!strip_icon.isNull() && strip_rect.width() >= 28) {
                    const QSize icon_size(14, 14);
                    const QRect icon_rect(
                        label_left, strip_rect.center().y() - icon_size.height() / 2,
                        icon_size.width(), icon_size.height());
                    p.drawPixmap(icon_rect, strip_icon.pixmap(icon_size));
                    label_left += icon_size.width() + 5;
                }
                p.drawText(label_left, y,
                           std::max(1, strip_rect.right() - label_left - 5), rowh,
                           Qt::AlignVCenter, layer_label);

                /* Premiere-style transition overlays live inside the layer
                 * strip. Their inner edge is the duration resize handle. */
                for (const auto &transition : layer->transitions) {
                    const QRect transition_bounds = transition_rect(*layer, transition, y);
                    const bool transition_selected = transition_target_selected_ &&
                        selected_transition_layer_id_ == layer->id &&
                        selected_transition_edge_ == transition.edge;
                    QColor transition_color = transition.kind == LayerTransitionKind::Text
                        ? QColor(112, 76, 156) : QColor(54, 111, 151);
                    if (!transition.enabled)
                        transition_color = transition_color.darker(155);
                    const QColor transition_fill =
                        with_alpha(transition_color, 220);
                    const QColor transition_background =
                        fxm_composite_over(transition_fill, bar_col);
                    const QColor transition_foreground =
                        fxm_background_aware_foreground(transition_background, pal);
                    p.fillRect(transition_bounds, transition_fill);
                    p.save();
                    p.setClipRect(transition_bounds);
                    p.setPen(QPen(with_alpha(transition_foreground, 45), 1));
                    for (int hx = transition_bounds.left() - transition_bounds.height();
                         hx < transition_bounds.right() + transition_bounds.height(); hx += 7)
                        p.drawLine(hx, transition_bounds.bottom(), hx + transition_bounds.height(), transition_bounds.top());
                    p.restore();
                    p.setPen(QPen(
                        transition_selected
                            ? fxm_background_aware_foreground(highlight, pal)
                            : with_alpha(transition_foreground, 190),
                        transition_selected ? 2 : 1));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(transition_bounds.adjusted(0, 0, -1, -1));
                    if (!layer->locked) {
                        const int handle_x = transition.edge == LayerTransitionEdge::In
                            ? transition_bounds.right() - 2 : transition_bounds.left();
                        p.fillRect(handle_x, transition_bounds.top(), 3, transition_bounds.height(),
                                   with_alpha(transition_foreground, 170));
                    }
                    if (transition_bounds.width() >= 56) {
                        p.setPen(transition_foreground);
                        const QString transition_name = QString::fromStdString(transition.display_name);
                        p.drawText(transition_bounds.adjusted(5, 0, -5, 0),
                                   Qt::AlignVCenter | Qt::AlignHCenter,
                                   p.fontMetrics().elidedText(transition_name, Qt::ElideRight,
                                                              std::max(1, transition_bounds.width() - 10)));
                    }
                }

                if (transition_target_selected_ && selected_transition_layer_id_ == layer->id &&
                    !find_layer_transition(layer->transitions, selected_transition_edge_)) {
                    const QRect target_bounds = transition_edge_target_rect(*layer, selected_transition_edge_, y);
                    p.fillRect(target_bounds, with_alpha(highlight, 55));
                    p.setPen(QPen(highlighted_text, 2, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(target_bounds.adjusted(1, 1, -2, -2));
                }

                if (transition_drop_preview_layer_id_ == layer->id) {
                    LayerTransition preview;
                    preview.edge = transition_drop_preview_edge_;
                    const LayerTransition *replaced = find_layer_transition(
                        layer->transitions, transition_drop_preview_edge_);
                    preview.duration = replaced
                        ? replaced->duration
                        : std::min(0.6, std::max(obs_frame_duration(),
                                               (layer->out_time - layer->in_time) * 0.35));
                    const QRect preview_bounds = transition_rect(*layer, preview, y);
                    p.fillRect(preview_bounds, with_alpha(highlight, 80));
                    p.setPen(QPen(highlight, 2, Qt::DashLine));
                    p.setBrush(Qt::NoBrush);
                    p.drawRect(preview_bounds.adjusted(1, 1, -2, -2));
                }

                /* Trim handles for mouse resizing of unlocked layer in/out. */
                if (!layer->locked) {
                    p.fillRect(x0, y + 3, kLayerTrimHandleVisualWidth,
                               rowh - 6, handle_color);
                    p.fillRect(x1 - kLayerTrimHandleVisualWidth, y + 3,
                               kLayerTrimHandleVisualWidth, rowh - 6,
                               handle_color);
                }

            } else {
                p.fillRect(x0, y + rowh / 2 - 1, x1 - x0, 2, border);
                p.setPen(disabled_text.isValid() ? disabled_text : with_alpha(text, 150));
                p.drawText(6, y, 150, rowh, Qt::AlignVCenter, property_label(entry.prop.name()));
            }

            auto draw_kf = [&](const TimelinePropertyRef &prop) {
                for (int i = 0; i < (int)prop.keyframe_count(); ++i) {
                    int kx = time_to_x(layer->in_time + prop.keyframe_time((size_t)i));
                    if (kx < body_dirty.left() - 10 || kx > body_dirty.right() + 10) continue;
                    int ky = y + rowh / 2;
                    const EasingType easing = prop.keyframe_easing((size_t)i);
                    QColor kf_fill = keyframe_color(easing);
                    if (!layer->visible)
                        kf_fill = kf_fill.darker(160);
                    const bool selected = is_keyframe_selected(layer->id, prop.name(), i);
                    if (selected) {
                        draw_keyframe_marker(p, QPointF(kx, ky), easing, 8.0,
                                             with_alpha(highlighted_text, 45),
                                             highlighted_text, 2.0);
                    }
                    draw_keyframe_marker(p, QPointF(kx, ky), easing, 5.0,
                                         selected ? kf_fill.lighter(125) : kf_fill,
                                         selected ? highlighted_text : border,
                                         selected ? 2.0 : 1.0);
                }
            };

            if (entry.is_property) {
                draw_kf(entry.prop);
            } else if (title_ && !entry.is_effect_group &&
                       !timeline_owner_keyframe_sections_expanded(
                           *title_, entry.owner_id)) {
                /* A closed disclosure still exposes its authored timing on
                 * the owner strip. Co-located keys from multiple properties
                 * collapse to one marker while retaining selected feedback. */
                struct AggregateKeyframe {
                    EasingType easing = EasingType::Linear;
                    bool selected = false;
                    bool initialized = false;
                };
                std::map<int, AggregateKeyframe> aggregate;
                for (const TimelinePropertyRef &property :
                     timeline_properties_for_owner(*title_, entry.owner_id)) {
                    if (!property || !property.is_animated())
                        continue;
                    for (int index = 0;
                         index < static_cast<int>(property.keyframe_count());
                         ++index) {
                        const int kx = time_to_x(
                            layer->in_time +
                            property.keyframe_time(static_cast<size_t>(index)));
                        auto &marker = aggregate[kx];
                        if (!marker.initialized) {
                            marker.easing = property.keyframe_easing(
                                static_cast<size_t>(index));
                            marker.initialized = true;
                        }
                        marker.selected = marker.selected ||
                            is_keyframe_selected(entry.owner_id,
                                property.name(), index);
                    }
                }
                const int ky = y + rowh / 2;
                for (const auto &[kx, marker] : aggregate) {
                    if (kx < body_dirty.left() - 10 ||
                        kx > body_dirty.right() + 10)
                        continue;
                    QColor fill = keyframe_color(marker.easing);
                    if (!layer->visible)
                        fill = fill.darker(160);
                    if (marker.selected) {
                        draw_keyframe_marker(
                            p, QPointF(kx, ky), marker.easing, 8.0,
                            with_alpha(highlighted_text, 45),
                            highlighted_text, 2.0);
                    }
                    draw_keyframe_marker(
                        p, QPointF(kx, ky), marker.easing, 5.0,
                        marker.selected ? fill.lighter(125) : fill,
                        marker.selected ? highlighted_text : border,
                        marker.selected ? 2.0 : 1.0);
                }
            }
        }
        p.restore();
    }

    draw_header();

    if (show_stinger_rolls) {
        const QRect body_bounds(0, ruler_height(), W, std::max(0, H - ruler_height()));
        p.save();
        p.setClipRect(dirty.intersected(body_bounds));
        if (pre_roll > 0.0) {
            const QRect pre_rect = QRect(transition_start_x, ruler_height(),
                animation_start_x - transition_start_x, H - ruler_height()).normalized();
            p.fillRect(pre_rect, QBrush(with_alpha(pre_roll_color, 44), Qt::BDiagPattern));
        }
        if (post_roll > 0.0) {
            const QRect post_rect = QRect(animation_end_x, ruler_height(),
                transition_end_x - animation_end_x, H - ruler_height()).normalized();
            p.fillRect(post_rect, QBrush(with_alpha(post_roll_color, 44), Qt::FDiagPattern));
        }
        p.setPen(QPen(with_alpha(text, 115), 1, Qt::DashLine));
        p.drawLine(animation_start_x, 0, animation_start_x, H);
        p.drawLine(animation_end_x, 0, animation_end_x, H);
        p.restore();
    }

    if (title_ && title_->graphic_type == TitleGraphicType::Stinger &&
        title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint) {
        const int switch_x = time_to_x(stinger_transition_point_seconds(*title_));
        p.setPen(QPen(QColor(210, 72, 170), 1, Qt::DashLine));
        p.drawLine(switch_x, ruler_height(), switch_x, height());
    }

    /* Playhead */
    int phx = time_to_x(playhead_);
    p.setPen(QPen(playhead_color, 1.5));
    p.drawLine(phx, 0, phx, H);
    /* Playhead head triangle */
    p.setBrush(playhead_color);
    p.setPen(Qt::NoPen);
    QPolygon tri;
    tri << QPoint(phx - 6, 0)
        << QPoint(phx + 6, 0)
        << QPoint(phx,     10);
    p.drawPolygon(tri);

    QString tc = format_timecode(playhead_);
    QRect tc_rect(phx + 8, 2, 96, 18);
    if (tc_rect.right() > W) tc_rect.moveRight(phx - 8);
    p.fillRect(tc_rect, highlight);
    p.setPen(highlighted_text);
    p.drawText(tc_rect.adjusted(4, 0, -4, 0), Qt::AlignVCenter, tc);

    if (drag_mode_ == DragMode::Marquee && marquee_moved_) {
        QRect rect = marquee_rect();
        p.fillRect(rect, with_alpha(highlight, 35));
        p.setPen(QPen(highlight, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect.adjusted(0, 0, -1, -1));
    }
    record_paint_cost();
}

bool TimelineWidget::hit_keyframe(const QPoint &pos, std::shared_ptr<Layer> *hit_layer,
                                  TimelinePropertyRef *hit_prop, int *hit_kf_idx,
                                  int *hit_row_idx, std::string *hit_owner_id) const
{
    if (!title_ || pos.y() < ruler_height()) return false;
    auto rows = timeline_rows(title_);
    int row = (pos.y() - ruler_height() + scroll_y_) / row_height();
    if (row < 0 || row >= (int)rows.size()) return false;

    auto &entry = rows[row];
    constexpr int kHitRadius = 7;
    auto test_prop = [&](const TimelinePropertyRef &prop) -> bool {
        for (int i = 0; i < (int)prop.keyframe_count(); ++i) {
            int kx = time_to_x(entry.layer->in_time + prop.keyframe_time((size_t)i));
            int ky = ruler_height() + row * row_height() - scroll_y_ + row_height() / 2;
            if (std::abs(pos.x() - kx) <= kHitRadius &&
                std::abs(pos.y() - ky) <= kHitRadius) {
                if (hit_layer) *hit_layer = entry.layer;
                if (hit_prop) *hit_prop = prop;
                if (hit_kf_idx) *hit_kf_idx = i;
                if (hit_row_idx) *hit_row_idx = row;
                if (hit_owner_id) *hit_owner_id = entry.owner_id;
                return true;
            }
        }
        return false;
    };

    /* The hit model mirrors painting: collapsed layer strips do not expose
     * hidden aggregate keyframes. */
    return entry.is_property && entry.prop && test_prop(entry.prop);
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent *ev)
{
    if (!title_) return;
    if (graph_editor_enabled_) {
        graph_context_menu(ev);
        return;
    }

    auto show_transition_menu = [&](const std::shared_ptr<Layer> &layer,
                                    LayerTransitionEdge edge) {
        if (!layer)
            return;
        select_layer_from_mouse(layer->id, Qt::NoModifier);
        select_transition_target(layer->id, edge);
        const bool has_transition = selected_transition() != nullptr;
        const bool editable = has_transition && !layer->locked;

        QMenu transition_menu(this);
        QAction *edit_action = transition_menu.addAction(fxm_tr("OBSTitles.EditTransition"));
        transition_menu.addSeparator();
        QAction *copy_action = transition_menu.addAction(fxm_tr("OBSTitles.Copy"));
        QAction *cut_action = transition_menu.addAction(fxm_tr("OBSTitles.Cut"));
        QAction *paste_action = transition_menu.addAction(fxm_tr("OBSTitles.Paste"));
        QAction *delete_action = transition_menu.addAction(fxm_tr("OBSTitles.Delete"));
        edit_action->setEnabled(editable);
        copy_action->setEnabled(has_transition);
        cut_action->setEnabled(editable);
        paste_action->setEnabled(can_paste_transition_to_selection());
        delete_action->setEnabled(editable);

        QAction *picked = transition_menu.exec(ev->globalPos());
        if (picked == edit_action) {
            emit transition_edit_requested(layer->id, static_cast<int>(edge));
        } else if (picked == copy_action) {
            copy_transition_selection();
        } else if (picked == cut_action) {
            cut_transition_selection();
        } else if (picked == paste_action) {
            paste_transition_to_selection();
        } else if (picked == delete_action) {
            delete_transition_selection();
        }
    };

    TransitionHit transition_hit;
    if (transition_hit_at_pos(ev->pos(), &transition_hit)) {
        show_transition_menu(transition_hit.layer, transition_hit.edge);
        return;
    }

    std::shared_ptr<Layer> transition_target_layer;
    LayerTransitionEdge transition_target_edge = LayerTransitionEdge::In;
    if (transition_edge_target_at_pos(ev->pos(), &transition_target_layer, &transition_target_edge)) {
        show_transition_menu(transition_target_layer, transition_target_edge);
        return;
    }

    std::shared_ptr<Layer> layer;
    TimelinePropertyRef hit_prop;
    int hit_idx = -1;
    int hit_row = -1;
    std::string hit_owner_id;
    const bool has_hit = hit_keyframe(ev->pos(), &layer, &hit_prop, &hit_idx,
                                      &hit_row, &hit_owner_id);
    if (has_hit && layer && layer->locked) return;

    /* A right-click on a layer strip reuses the exact Canvas layer menu and
     * applies it to the Timeline's synchronized selection. Keyframes and
     * transitions retain their more specific context menus. */
    if (!has_hit && ev->pos().y() >= ruler_height()) {
        const auto rows = timeline_rows(title_);
        const int row_index =
            (ev->pos().y() - ruler_height() + scroll_y_) / row_height();
        if (row_index >= 0 && row_index < static_cast<int>(rows.size())) {
            const auto &row = rows[static_cast<size_t>(row_index)];
            if (!row.is_property && !row.is_effect_group &&
                !row.is_camera && !row.is_camera_switch && row.layer) {
                /* The row itself is the target, not only the visible clip
                 * rectangle. This also makes the menu available from the
                 * layer-name/empty-time area while retaining multi-selection. */
                select_layer_from_mouse(row.layer->id, Qt::NoModifier);
                clear_keyframe_selection();
                clear_transition_selection();
                emit layer_context_menu_requested(ev->globalPos());
                ev->accept();
                return;
            }
        }
    }

    if (!has_hit && keyframe_clipboard_.empty()) return;

    if (has_hit && layer && hit_prop &&
        !is_keyframe_selected(hit_owner_id, hit_prop.name(), hit_idx)) {
        select_keyframe(hit_owner_id, hit_prop.name(), hit_idx, false, false);
        const auto rows = timeline_rows(title_);
        set_graph_channel_mode(
            hit_row >= 0 && hit_row < (int)rows.size() &&
                    rows[hit_row].is_property_channel
                ? static_cast<int>(graph_mode_for_component(rows[hit_row].property_channel)) : 3);
    }
    prune_keyframe_selection();

    QMenu menu(this);
    menu.setTitle(has_hit ? fxm_tr("OBSTitles.Keyframe") : fxm_tr("OBSTitles.Paste"));

    QAction *copy_action = menu.addAction(fxm_tr("OBSTitles.Copy"));
    QAction *cut_action = menu.addAction(fxm_tr("OBSTitles.Cut"));
    QAction *paste_action = menu.addAction(fxm_tr("OBSTitles.Paste"));
    QAction *delete_action = menu.addAction(fxm_tr("OBSTitles.Delete"));
    const bool has_selection = !selected_keyframes_.empty();
    copy_action->setEnabled(has_selection);
    cut_action->setEnabled(has_selection);
    paste_action->setEnabled(!keyframe_clipboard_.empty());
    delete_action->setEnabled(has_selection);

    struct TemporalChoice {
        QAction *action = nullptr;
        TimelinePropertyRef prop;
        std::vector<int> target_indices;
        TemporalInterpolationMode mode = TemporalInterpolationMode::Linear;
    };
    std::vector<TemporalChoice> temporal_choices;
    QAction *easy_ease_action = nullptr;
    QAction *easy_ease_in_action = nullptr;
    QAction *easy_ease_out_action = nullptr;
    QAction *keyframe_velocity_action = nullptr;
    TimelinePropertyRef temporal_action_prop;
    std::vector<int> temporal_action_indices;

    struct SpatialChoice {
        QAction *action = nullptr;
        TimelinePropertyRef prop;
        std::vector<int> target_indices;
        SpatialInterpolationMode mode = SpatialInterpolationMode::Linear;
    };
    std::vector<SpatialChoice> spatial_choices;
    QAction *break_spatial_tangents_action = nullptr;
    QAction *join_spatial_tangents_action = nullptr;
    QAction *rove_across_time_action = nullptr;
    TimelinePropertyRef spatial_tangent_prop;
    std::vector<int> spatial_tangent_indices;

    if (has_hit && layer && hit_prop) {
        menu.addSeparator();
        QMenu *temporal_menu = menu.addMenu(fxm_tr("OBSTitles.TemporalInterpolation"));
        temporal_menu->setEnabled(!hit_prop.is_hold_only());
        temporal_action_prop = hit_prop;
        for (const KeyframeRef &ref : selected_keyframes_) {
            if (ref.layer_id == layer->id && ref.prop_name == hit_prop.name() &&
                ref.index >= 0 && ref.index < (int)hit_prop.keyframe_count())
                temporal_action_indices.push_back(ref.index);
        }
        if (temporal_action_indices.empty()) temporal_action_indices.push_back(hit_idx);
        std::sort(temporal_action_indices.begin(), temporal_action_indices.end());
        temporal_action_indices.erase(
            std::unique(temporal_action_indices.begin(), temporal_action_indices.end()),
            temporal_action_indices.end());
        auto temporal_label = [](TemporalInterpolationMode mode) {
            switch (mode) {
            case TemporalInterpolationMode::Linear: return fxm_tr("OBSTitles.Linear");
            case TemporalInterpolationMode::Hold: return fxm_tr("OBSTitles.Hold");
            case TemporalInterpolationMode::AutoBezier: return fxm_tr("OBSTitles.TemporalAutoBezier");
            case TemporalInterpolationMode::ContinuousBezier: return fxm_tr("OBSTitles.TemporalContinuousBezier");
            case TemporalInterpolationMode::ManualBezier: return fxm_tr("OBSTitles.TemporalManualBezier");
            }
            return fxm_tr("OBSTitles.Linear");
        };
        auto *temporal_group = new QActionGroup(temporal_menu);
        temporal_group->setExclusive(true);
        for (TemporalInterpolationMode mode : {TemporalInterpolationMode::Linear,
                 TemporalInterpolationMode::Hold, TemporalInterpolationMode::AutoBezier,
                 TemporalInterpolationMode::ContinuousBezier,
                 TemporalInterpolationMode::ManualBezier}) {
            QAction *action = temporal_menu->addAction(temporal_label(mode));
            action->setCheckable(true);
            action->setActionGroup(temporal_group);
            action->setChecked(std::all_of(
                temporal_action_indices.begin(), temporal_action_indices.end(), [&](int index) {
                    return hit_prop.keyframe_temporal_mode((size_t)index) == mode;
                }));
            temporal_choices.push_back({action, hit_prop, temporal_action_indices, mode});
        }
        temporal_menu->addSeparator();
        easy_ease_action = temporal_menu->addAction(fxm_tr("OBSTitles.EasyEase"));
        easy_ease_in_action = temporal_menu->addAction(fxm_tr("OBSTitles.EasyEaseIn"));
        easy_ease_out_action = temporal_menu->addAction(fxm_tr("OBSTitles.EasyEaseOut"));
        temporal_menu->addSeparator();
        keyframe_velocity_action = temporal_menu->addAction(
            fxm_tr("OBSTitles.KeyframeVelocity"));

        if (hit_prop.supports_spatial_interpolation()) {
            menu.addSeparator();
            QMenu *spatial_menu = menu.addMenu(fxm_tr("OBSTitles.SpatialInterpolation"));
            auto *spatial_group = new QActionGroup(spatial_menu);
            spatial_group->setExclusive(true);
            const std::vector<int> spatial_indices{hit_idx};
            for (auto [label, mode] : std::initializer_list<
                     std::pair<QString, SpatialInterpolationMode>>{
                     {fxm_tr("OBSTitles.SpatialLinear"), SpatialInterpolationMode::Linear},
                     {fxm_tr("OBSTitles.SpatialAutoBezier"), SpatialInterpolationMode::AutoBezier},
                     {fxm_tr("OBSTitles.SpatialContinuousBezier"), SpatialInterpolationMode::ContinuousBezier},
                     {fxm_tr("OBSTitles.SpatialManualBezier"), SpatialInterpolationMode::ManualBezier},
                 }) {
                QAction *action = spatial_menu->addAction(label);
                action->setCheckable(true);
                action->setActionGroup(spatial_group);
                action->setChecked(hit_prop.keyframe_spatial_mode((size_t)hit_idx) == mode);
                spatial_choices.push_back({action, hit_prop, spatial_indices, mode});
            }
            spatial_menu->addSeparator();
            rove_across_time_action = spatial_menu->addAction(
                fxm_tr("OBSTitles.RoveAcrossTime"));
            rove_across_time_action->setCheckable(true);
            rove_across_time_action->setChecked(
                hit_prop.keyframe_roves_across_time((size_t)hit_idx));
            rove_across_time_action->setEnabled(
                hit_idx > 0 && hit_idx + 1 < (int)hit_prop.keyframe_count());
            spatial_menu->addSeparator();
            break_spatial_tangents_action = spatial_menu->addAction(
                fxm_tr("OBSTitles.BreakSpatialTangents"));
            join_spatial_tangents_action = spatial_menu->addAction(
                fxm_tr("OBSTitles.JoinSpatialTangents"));
            const bool linked = hit_prop.keyframe_spatial_tangents_linked((size_t)hit_idx);
            break_spatial_tangents_action->setEnabled(linked);
            join_spatial_tangents_action->setEnabled(!linked);
            spatial_tangent_prop = hit_prop;
            spatial_tangent_indices = spatial_indices;
        }
    }

    QAction *chosen = menu.exec(ev->globalPos());
    if (!chosen) return;

    if (chosen == copy_action) {
        copy_selected_keyframes();
        return;
    }
    if (chosen == cut_action) {
        if (cut_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }
    if (chosen == paste_action) {
        if (paste_keyframes_at(std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration)))
            emit keyframe_easing_changed();
        return;
    }
    if (chosen == delete_action) {
        if (delete_selected_keyframes()) emit keyframe_easing_changed();
        return;
    }

    if (chosen == keyframe_velocity_action && temporal_action_prop) {
        show_temporal_velocity_dialog(temporal_action_prop, temporal_action_indices);
        return;
    }
    if ((chosen == easy_ease_action || chosen == easy_ease_in_action ||
         chosen == easy_ease_out_action) && temporal_action_prop) {
        const bool ease_in = chosen == easy_ease_action || chosen == easy_ease_in_action;
        const bool ease_out = chosen == easy_ease_action || chosen == easy_ease_out_action;
        for (int index : temporal_action_indices)
            temporal_action_prop.apply_easy_ease((size_t)index, ease_in, ease_out);
        update();
        emit keyframe_easing_changed();
        return;
    }
    auto temporal_choice = std::find_if(
        temporal_choices.begin(), temporal_choices.end(),
        [&](const TemporalChoice &candidate) { return candidate.action == chosen; });
    if (temporal_choice != temporal_choices.end() && temporal_choice->prop) {
        for (int index : temporal_choice->target_indices)
            temporal_choice->prop.apply_temporal_mode((size_t)index, temporal_choice->mode);
        update();
        emit keyframe_easing_changed();
        return;
    }

    auto spatial_choice = std::find_if(
        spatial_choices.begin(), spatial_choices.end(),
        [&](const SpatialChoice &candidate) { return candidate.action == chosen; });
    if (spatial_choice != spatial_choices.end() && spatial_choice->prop) {
        for (int idx : spatial_choice->target_indices) {
            if (idx >= 0 && idx < (int)spatial_choice->prop.keyframe_count())
                spatial_choice->prop.apply_spatial_mode((size_t)idx, spatial_choice->mode);
        }
        update();
        emit keyframe_easing_changed();
        return;
    }
    if (chosen == rove_across_time_action && spatial_tangent_prop) {
        for (int idx : spatial_tangent_indices) {
            if (idx > 0 && idx + 1 < (int)spatial_tangent_prop.keyframe_count()) {
                const bool enabled = !spatial_tangent_prop
                    .keyframe_roves_across_time((size_t)idx);
                spatial_tangent_prop.set_keyframe_rove_across_time(
                    (size_t)idx, enabled);
            }
        }
        update();
        emit keyframe_easing_changed();
        return;
    }
    if ((chosen == break_spatial_tangents_action ||
         chosen == join_spatial_tangents_action) && spatial_tangent_prop) {
        const bool linked = chosen == join_spatial_tangents_action;
        for (int idx : spatial_tangent_indices) {
            if (idx >= 0 && idx < (int)spatial_tangent_prop.keyframe_count())
                spatial_tangent_prop.set_spatial_tangents_linked((size_t)idx, linked);
        }
        update();
        emit keyframe_easing_changed();
        return;
    }

}

void TimelineWidget::wheelEvent(QWheelEvent *ev)
{
    if (!title_) return;

    const QPoint angle = ev->angleDelta();
    if (graph_editor_enabled_ && (ev->modifiers() & Qt::AltModifier)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const double cursor_y = ev->position().y();
#else
        const double cursor_y = ev->pos().y();
#endif
        const int delta = angle.y() != 0 ? angle.y() : angle.x();
        if (delta != 0) {
            const double anchor = graph_y_to_value(cursor_y);
            const double factor = std::pow(1.0015, -delta);
            graph_value_min_ = anchor + (graph_value_min_ - anchor) * factor;
            graph_value_max_ = anchor + (graph_value_max_ - anchor) * factor;
            graph_fit_pending_ = false;
            update();
        }
        ev->accept();
        return;
    }
    if (ev->modifiers() & Qt::ShiftModifier) {
        int delta = angle.x() != 0 ? angle.x() : angle.y();
        scroll_x_ -= delta;
        clamp_scroll();
        update();
        notify_horizontal_view_changed();
        ev->accept();
        return;
    }

    if (ev->modifiers() & Qt::ControlModifier) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        int cursor_x = (int)std::round(ev->position().x());
#else
        int cursor_x = ev->pos().x();
#endif
        double anchor_time = (cursor_x + scroll_x_) / pixels_per_sec_;
        int delta = angle.y() != 0 ? angle.y() : angle.x();
        if (delta == 0) return;

        double factor = std::pow(1.0015, delta);
        fit_zoom_active_ = false;
        set_pixels_per_sec(pixels_per_sec_ * factor, anchor_time, cursor_x);
        ev->accept();
        return;
    }

    int delta = angle.y() != 0 ? -angle.y() : -angle.x();
    if (delta == 0) return;
    if (graph_editor_enabled_) {
        const double value_delta = delta * (graph_value_max_ - graph_value_min_) / 1200.0;
        graph_value_min_ += value_delta;
        graph_value_max_ += value_delta;
        graph_fit_pending_ = false;
        update();
    } else {
        emit vertical_scroll_delta_requested(delta);
    }
    ev->accept();
}

void TimelineWidget::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    /* Dock restoration and splitter sizing continue after the title is bound.
     * Keep refitting through those startup resizes until the user explicitly
     * zooms or navigates the horizontal view. */
    if (fit_zoom_active_ && title_ && width() > 40) {
        fit_timeline();
        return;
    }
    clamp_scroll();
    clamp_vertical_scroll();
    if (graph_editor_enabled_) graph_fit_pending_ = true;
    notify_horizontal_view_changed();
}

void TimelineWidget::keyPressEvent(QKeyEvent *ev)
{
    if (!title_) {
        QWidget::keyPressEvent(ev);
        return;
    }

    if (has_transition_target_selection()) {
        if (ev->matches(QKeySequence::Copy)) {
            copy_transition_selection();
            ev->accept();
            return;
        }
        if (ev->matches(QKeySequence::Cut)) {
            cut_transition_selection();
            ev->accept();
            return;
        }
        if (ev->matches(QKeySequence::Paste)) {
            paste_transition_to_selection();
            ev->accept();
            return;
        }
        if (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) {
            delete_transition_selection();
            ev->accept();
            return;
        }
    }

    if (ev->matches(QKeySequence::Copy) && has_selected_keyframes()) {
        copy_keyframe_selection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Cut) && has_selected_keyframes()) {
        cut_keyframe_selection();
        ev->accept();
        return;
    }
    if (ev->matches(QKeySequence::Paste) && has_keyframe_clipboard()) {
        paste_keyframes_at_playhead();
        ev->accept();
        return;
    }
    if ((ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace) && has_selected_keyframes()) {
        delete_keyframe_selection();
        ev->accept();
        return;
    }

    QWidget::keyPressEvent(ev);
}

std::shared_ptr<Layer> TimelineWidget::layer_strip_at_pos(const QPoint &pos) const
{
    if (!title_ || pos.y() < ruler_height())
        return nullptr;

    const auto rows = timeline_rows(title_);
    const int row = (pos.y() - ruler_height() + scroll_y_) / row_height();
    if (row < 0 || row >= static_cast<int>(rows.size()) || rows[row].is_property ||
        rows[row].is_camera || rows[row].is_camera_switch)
        return nullptr;

    const auto layer = rows[row].layer;
    if (!layer || layer->locked)
        return nullptr;

    const int x0 = time_to_x(layer->in_time);
    const int x1 = time_to_x(layer->out_time);
    if (pos.x() < std::min(x0, x1) || pos.x() > std::max(x0, x1))
        return nullptr;
    return layer;
}


QRect TimelineWidget::transition_rect(const Layer &layer, const LayerTransition &transition, int row_y) const
{
    const double layer_duration = std::max(0.0, layer.out_time - layer.in_time);
    const double duration = std::clamp(transition.duration, 0.0, layer_duration);
    const double start = transition.edge == LayerTransitionEdge::In
        ? layer.in_time : layer.out_time - duration;
    const double end = transition.edge == LayerTransitionEdge::In
        ? layer.in_time + duration : layer.out_time;
    const int x0 = time_to_x(start);
    const int x1 = time_to_x(end);
    return QRect(std::min(x0, x1), row_y + 3, std::max(1, std::abs(x1 - x0)), row_height() - 6);
}

QRect TimelineWidget::transition_edge_target_rect(const Layer &layer,
                                                      LayerTransitionEdge edge,
                                                      int row_y) const
{
    const int x0 = time_to_x(layer.in_time);
    const int x1 = time_to_x(layer.out_time);
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int strip_width = std::max(1, right - left);
    const int zone = std::min(strip_width, std::clamp(strip_width / 5, 14, 24));
    const int trim_reserve = std::min(7, std::max(0, zone - 1));
    const int target_width = std::max(1, zone - trim_reserve);
    const int x = edge == LayerTransitionEdge::In
        ? left + trim_reserve : right - zone;
    return QRect(x, row_y + 3, target_width, row_height() - 6);
}

bool TimelineWidget::transition_edge_target_at_pos(const QPoint &pos,
                                                  std::shared_ptr<Layer> *layer_out,
                                                  LayerTransitionEdge *edge_out) const
{
    if (!title_ || pos.y() < ruler_height())
        return false;
    const auto rows = timeline_rows(title_);
    const int row = (pos.y() - ruler_height() + scroll_y_) / row_height();
    if (row < 0 || row >= static_cast<int>(rows.size()) || rows[row].is_property ||
        rows[row].is_camera || rows[row].is_camera_switch)
        return false;
    const auto layer = rows[row].layer;
    if (!layer)
        return false;
    const int row_y = ruler_height() + row * row_height() - scroll_y_;
    const QRect in_rect = transition_edge_target_rect(*layer, LayerTransitionEdge::In, row_y);
    const QRect out_rect = transition_edge_target_rect(*layer, LayerTransitionEdge::Out, row_y);
    if (!in_rect.contains(pos) && !out_rect.contains(pos))
        return false;

    LayerTransitionEdge edge = LayerTransitionEdge::In;
    if (in_rect.contains(pos) && out_rect.contains(pos)) {
        const int in_distance = std::abs(pos.x() - in_rect.left());
        const int out_distance = std::abs(pos.x() - out_rect.right());
        edge = out_distance < in_distance ? LayerTransitionEdge::Out : LayerTransitionEdge::In;
    } else if (out_rect.contains(pos)) {
        edge = LayerTransitionEdge::Out;
    }
    if (layer_out) *layer_out = layer;
    if (edge_out) *edge_out = edge;
    return true;
}

bool TimelineWidget::transition_hit_at_pos(const QPoint &pos, TransitionHit *hit) const
{
    if (!title_ || pos.y() < ruler_height())
        return false;
    const auto rows = timeline_rows(title_);
    const int row = (pos.y() - ruler_height() + scroll_y_) / row_height();
    if (row < 0 || row >= static_cast<int>(rows.size()) || rows[row].is_property ||
        rows[row].is_camera || rows[row].is_camera_switch)
        return false;
    const auto layer = rows[row].layer;
    if (!layer)
        return false;
    const int row_y = ruler_height() + row * row_height() - scroll_y_;
    for (const auto &transition : layer->transitions) {
        const QRect rect = transition_rect(*layer, transition, row_y);
        if (!rect.adjusted(-2, 0, 2, 0).contains(pos))
            continue;
        const int handle_x = transition.edge == LayerTransitionEdge::In ? rect.right() : rect.left();
        if (hit) {
            hit->layer = layer;
            hit->edge = transition.edge;
            hit->rect = rect;
            hit->duration_handle =
                std::abs(pos.x() - handle_x) <= kTransitionDurationHitWidth;
        }
        return true;
    }
    return false;
}

bool TimelineWidget::transition_drop_target_at_pos(const QPoint &pos,
                                                   std::shared_ptr<Layer> *layer_out,
                                                   LayerTransitionEdge *edge_out) const
{
    /* Replacing a transition should not require aiming at the small generic
     * edge drop zone: every pixel of the existing transition is a valid target
     * and resolves to that transition's edge. */
    TransitionHit existing_hit;
    if (transition_hit_at_pos(pos, &existing_hit) && existing_hit.layer &&
        !existing_hit.layer->locked) {
        if (layer_out) *layer_out = existing_hit.layer;
        if (edge_out) *edge_out = existing_hit.edge;
        return true;
    }

    const auto layer = layer_strip_at_pos(pos);
    if (!layer || layer->locked)
        return false;
    const int x0 = time_to_x(layer->in_time);
    const int x1 = time_to_x(layer->out_time);
    const int left = std::min(x0, x1);
    const int right = std::max(x0, x1);
    const int strip_width = std::max(1, right - left);
    const int zone = std::clamp(strip_width / 3, 18, 110);
    LayerTransitionEdge edge;
    if (pos.x() <= left + zone)
        edge = LayerTransitionEdge::In;
    else if (pos.x() >= right - zone)
        edge = LayerTransitionEdge::Out;
    else
        return false;
    if (layer_out) *layer_out = layer;
    if (edge_out) *edge_out = edge;
    return true;
}

void TimelineWidget::normalize_transition_durations(Layer &layer)
{
    const double frame = obs_frame_duration();
    const double layer_duration = std::max(frame, layer.out_time - layer.in_time);
    LayerTransition *in_transition = find_layer_transition(layer.transitions, LayerTransitionEdge::In);
    LayerTransition *out_transition = find_layer_transition(layer.transitions, LayerTransitionEdge::Out);
    if (in_transition)
        in_transition->duration = std::clamp(in_transition->duration, frame, layer_duration);
    if (out_transition)
        out_transition->duration = std::clamp(out_transition->duration, frame, layer_duration);
    if (in_transition && out_transition && in_transition->duration + out_transition->duration > layer_duration) {
        if (drag_mode_ == DragMode::TransitionDuration && drag_transition_edge_ == LayerTransitionEdge::Out)
            out_transition->duration = std::max(frame, layer_duration - in_transition->duration);
        else
            in_transition->duration = std::max(frame, layer_duration - out_transition->duration);
    }
}

void TimelineWidget::clear_transition_drop_preview()
{
    if (transition_drop_preview_layer_id_.empty())
        return;
    transition_drop_preview_layer_id_.clear();
    update();
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent *ev)
{
    static constexpr const char *kAudioEffectMimeType = "application/x-fxm-audio-effect";
    if (ev && ev->mimeData()->hasFormat(QString::fromUtf8(kAudioEffectMimeType))) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
        return;
    }
    if (ev && fxm::transitions::mime_has_transition_preset(ev->mimeData())) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
        return;
    }
    if (ev && fxm::effects::mime_has_effect_preset(ev->mimeData())) {
        ev->setDropAction(Qt::CopyAction);
        ev->accept();
        return;
    }
    QWidget::dragEnterEvent(ev);
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent *ev)
{
    static constexpr const char *kAudioEffectMimeType = "application/x-fxm-audio-effect";
    if (ev && ev->mimeData()->hasFormat(QString::fromUtf8(kAudioEffectMimeType))) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        const auto layer = layer_strip_at_pos(pos);
        if (layer && (layer->type == LayerType::Audio || layer_type_can_have_children(layer->type))) {
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
        } else {
            ev->ignore();
        }
        return;
    }
    if (ev && fxm::transitions::mime_has_transition_preset(ev->mimeData())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        std::shared_ptr<Layer> layer;
        LayerTransitionEdge edge = LayerTransitionEdge::In;
        if (transition_drop_target_at_pos(pos, &layer, &edge)) {
            const bool changed = transition_drop_preview_layer_id_ != layer->id ||
                                 transition_drop_preview_edge_ != edge;
            transition_drop_preview_layer_id_ = layer->id;
            transition_drop_preview_edge_ = edge;
            if (changed) update();
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
        } else {
            clear_transition_drop_preview();
            ev->ignore();
        }
        return;
    }
    if (ev && fxm::effects::mime_has_effect_preset(ev->mimeData())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        if (layer_strip_at_pos(pos)) {
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
        } else {
            ev->ignore();
        }
        return;
    }
    QWidget::dragMoveEvent(ev);
}

void TimelineWidget::dropEvent(QDropEvent *ev)
{
    static constexpr const char *kAudioEffectMimeType = "application/x-fxm-audio-effect";
    if (ev && ev->mimeData()->hasFormat(QString::fromUtf8(kAudioEffectMimeType))) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        const auto layer = layer_strip_at_pos(pos);
        bool audio_capable = layer && layer->type == LayerType::Audio;
        if (layer && layer_type_can_have_children(layer->type) && title_) {
            std::set<std::string> pending{layer->id}, visited;
            while (!pending.empty() && !audio_capable) {
                const std::string parent = *pending.begin();
                pending.erase(pending.begin());
                if (!visited.insert(parent).second) continue;
                for (const auto &child : title_->layers) {
                    if (!child || child->parent_id != parent) continue;
                    if (child->type == LayerType::Audio) { audio_capable = true; break; }
                    if (layer_type_can_have_children(child->type)) pending.insert(child->id);
                }
            }
        }
        bool ok = false;
        const int type = QString::fromUtf8(ev->mimeData()->data(QString::fromUtf8(kAudioEffectMimeType))).toInt(&ok);
        if (layer && audio_capable && ok) {
            emit audio_effect_dropped(type, layer->id);
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            return;
        }
        ev->ignore();
        return;
    }
    if (ev && fxm::transitions::mime_has_transition_preset(ev->mimeData())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        std::shared_ptr<Layer> layer;
        LayerTransitionEdge edge = LayerTransitionEdge::In;
        const QString file_path = fxm::transitions::transition_preset_path_from_mime(ev->mimeData());
        if (transition_drop_target_at_pos(pos, &layer, &edge) && !file_path.isEmpty()) {
            clear_transition_drop_preview();
            select_layer_from_mouse(layer->id, Qt::NoModifier);
            select_transition_target(layer->id, edge);
            emit transition_preset_dropped(file_path, layer->id, static_cast<int>(edge));
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            return;
        }
        clear_transition_drop_preview();
        ev->ignore();
        return;
    }
    if (ev && fxm::effects::mime_has_effect_preset(ev->mimeData())) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = ev->position().toPoint();
#else
        const QPoint pos = ev->pos();
#endif
        const auto layer = layer_strip_at_pos(pos);
        const QString file_path = fxm::effects::effect_preset_path_from_mime(ev->mimeData());
        if (layer && !file_path.isEmpty()) {
            emit effect_preset_dropped(file_path, layer->id);
            ev->setDropAction(Qt::CopyAction);
            ev->accept();
            return;
        }
        ev->ignore();
        return;
    }
    QWidget::dropEvent(ev);
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent *ev)
{
    clear_transition_drop_preview();
    QWidget::dragLeaveEvent(ev);
}

void TimelineWidget::mousePressEvent(QMouseEvent *ev)
{
    setFocus(Qt::MouseFocusReason);
    if (!title_) return;
    drag_mode_ = DragMode::None;
    drag_layer_id_.clear();
    drag_prop_name_.clear();
    drag_keyframe_index_ = -1;
    drag_start_time_ = 0.0;
    drag_start_in_ = 0.0;
    drag_start_out_ = 0.0;
    dragged_keyframes_.clear();
    dragged_layer_strips_.clear();
    marquee_moved_ = false;

    if (graph_editor_enabled_ && graph_mouse_press(ev))
        return;

    /* Outer strip trims have priority over transition overlays.  Without this
     * early hit-test, an in/out transition consumes the same pixels as the
     * layer edge and makes the strip effectively impossible to resize. */
    if (ev->button() == Qt::LeftButton && ev->pos().y() >= ruler_height()) {
        const auto rows = timeline_rows(title_);
        const int row = (ev->pos().y() - ruler_height() + scroll_y_) /
                        row_height();
        if (row >= 0 && row < static_cast<int>(rows.size()) &&
            !rows[row].is_property && !rows[row].is_effect_group &&
            !rows[row].is_camera && !rows[row].is_camera_switch &&
            rows[row].layer) {
            const auto &layer = rows[row].layer;
            const int x0 = time_to_x(layer->in_time);
            const int x1 = time_to_x(layer->out_time);
            if (layer->type == LayerType::Audio && !layer->locked) {
                const double clip_duration = std::max(0.0, layer->out_time - layer->in_time);
                const int fade_in_x = time_to_x(layer->in_time + std::clamp(layer->audio_fade_in, 0.0, clip_duration));
                const int fade_out_x = time_to_x(layer->out_time - std::clamp(layer->audio_fade_out, 0.0, clip_duration));
                constexpr int kAudioFadeHandleHitWidth = 7;
                const int strip_top = ruler_height() + row * row_height() - scroll_y_ + 3;
                const bool in_fade_handle_band = ev->pos().y() >= strip_top && ev->pos().y() <= strip_top + 11;
                if (in_fade_handle_band &&
                    (std::abs(ev->pos().x() - fade_in_x) <= kAudioFadeHandleHitWidth ||
                     std::abs(ev->pos().x() - fade_out_x) <= kAudioFadeHandleHitWidth)) {
                    select_layer_from_mouse(layer->id, ev->modifiers());
                    clear_transition_selection();
                    drag_layer_id_ = layer->id;
                    if (std::abs(ev->pos().x() - fade_in_x) <= std::abs(ev->pos().x() - fade_out_x)) {
                        drag_mode_ = DragMode::AudioFadeIn;
                        drag_start_in_ = layer->audio_fade_in;
                    } else {
                        drag_mode_ = DragMode::AudioFadeOut;
                        drag_start_out_ = layer->audio_fade_out;
                    }
                    drag_start_time_ = x_to_time(ev->pos().x());
                    setCursor(Qt::SizeHorCursor);
                    ev->accept();
                    return;
                }
            }
            const int in_distance = std::abs(ev->pos().x() - x0);
            const int out_distance = std::abs(ev->pos().x() - x1);
            if (std::min(in_distance, out_distance) <= kLayerTrimHitWidth) {
                select_layer_from_mouse(layer->id, ev->modifiers());
                clear_transition_selection();
                if (!layer->locked) {
                    const DragMode trim_mode = in_distance <= out_distance
                        ? DragMode::TrimIn : DragMode::TrimOut;
                    begin_layer_strip_drag(layer->id, trim_mode,
                                           x_to_time(ev->pos().x()));
                    setCursor(Qt::SizeHorCursor);
                }
                ev->accept();
                return;
            }
        }
    }

    TransitionHit transition_hit;
    if (ev->button() == Qt::LeftButton && transition_hit_at_pos(ev->pos(), &transition_hit)) {
        if (!transition_hit.layer) {
            ev->accept();
            return;
        }
        select_layer_from_mouse(transition_hit.layer->id, ev->modifiers());
        select_transition_target(transition_hit.layer->id, transition_hit.edge);
        if (transition_hit.layer->locked) {
            ev->accept();
            return;
        }
        if (transition_hit.duration_handle) {
            const LayerTransition *transition = find_layer_transition(transition_hit.layer->transitions,
                                                                      transition_hit.edge);
            if (transition) {
                drag_mode_ = DragMode::TransitionDuration;
                drag_layer_id_ = transition_hit.layer->id;
                drag_transition_edge_ = transition_hit.edge;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        /* The transition overlay is its own timeline item. A normal click
         * selects it/layer but must not fall through into moving the layer strip. */
        ev->accept();
        return;
    }

    if (ev->pos().y() < ruler_height()) {
        clear_transition_selection();
        if (title_->graphic_type == TitleGraphicType::Stinger &&
            title_->stinger_switch_mode == StingerSwitchMode::SwitchAtPoint) {
            const int switch_x = time_to_x(stinger_transition_point_seconds(*title_));
            if (std::abs(ev->pos().x() - switch_x) <= 9) {
                drag_mode_ = DragMode::StingerTransitionPoint;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        if (title_->playback_mode == 2) {
            int pause_x = time_to_x(std::clamp(title_->pause_time, 0.0, title_->duration));
            if (std::abs(ev->pos().x() - pause_x) <= 8) {
                drag_mode_ = DragMode::PauseMarker;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        if (title_->playback_mode == 1) {
            int loop_x0 = time_to_x(std::clamp(title_->loop_start, 0.0, title_->duration));
            int loop_x1 = time_to_x(std::clamp(title_->loop_end, title_->loop_start, title_->duration));
            if (std::abs(ev->pos().x() - loop_x0) <= 8) {
                drag_mode_ = DragMode::LoopStart;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
            if (std::abs(ev->pos().x() - loop_x1) <= 8) {
                drag_mode_ = DragMode::LoopEnd;
                setCursor(Qt::SizeHorCursor);
                ev->accept();
                return;
            }
        }
        drag_mode_ = DragMode::Playhead;
        double t = std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration);
        emit playhead_changed(t);
        ev->accept();
        return;
    }

    std::shared_ptr<Layer> hit_layer;
    TimelinePropertyRef hit_prop;
    int hit_idx = -1;
    int hit_row = -1;
    std::string hit_owner_id;
    if (hit_keyframe(ev->pos(), &hit_layer, &hit_prop, &hit_idx, &hit_row, &hit_owner_id)) {
        clear_transition_selection();
        if (hit_layer && hit_layer->locked) {
            ev->accept();
            return;
        }
        if (hit_layer && title_->find_layer(hit_owner_id))
            select_layer_from_mouse(hit_owner_id, ev->modifiers());
        const bool shift = ev->modifiers() & Qt::ShiftModifier;
        const auto rows = timeline_rows(title_);
        const int graph_channel = hit_row >= 0 && hit_row < (int)rows.size() &&
                rows[hit_row].is_property_channel
            ? static_cast<int>(graph_mode_for_component(rows[hit_row].property_channel)) : 3;
        if (shift) {
            select_keyframe(hit_owner_id, hit_prop.name(), hit_idx, true, true);
            set_graph_channel_mode(graph_channel);
            ev->accept();
            return;
        }
        if (!is_keyframe_selected(hit_owner_id, hit_prop.name(), hit_idx))
            select_keyframe(hit_owner_id, hit_prop.name(), hit_idx, false, false);
        set_graph_channel_mode(graph_channel);
        begin_keyframe_drag(hit_owner_id, hit_prop.name(), hit_idx,
                            std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration));
        setCursor(Qt::ClosedHandCursor);
        ev->accept();
        return;
    }

    auto rows = timeline_rows(title_);
    int row = (ev->pos().y() - ruler_height() + scroll_y_) / row_height();
    if (row >= 0 && row < (int)rows.size() && rows[row].is_property &&
        ev->button() == Qt::LeftButton) {
        clear_transition_selection();
        select_graph_property(rows[row].owner_id, rows[row].prop.name(),
                              rows[row].is_property_channel
                                  ? static_cast<int>(graph_mode_for_component(rows[row].property_channel)) : 3);
        if (rows[row].layer && title_->find_layer(rows[row].owner_id))
            select_layer_from_mouse(rows[row].owner_id, ev->modifiers());
        /* An empty property row is also a valid start for a marquee spanning
         * other layers. Keep the graph property selected, then fall through to
         * the common marquee path below. */
    }
    if (row >= 0 && row < (int)rows.size() && !rows[row].is_property) {
        if (rows[row].is_camera || rows[row].is_camera_switch ||
            rows[row].is_effect_group) {
            ev->accept();
            return;
        }
        auto layer = rows[row].layer;
        if (!layer) {
            ev->accept();
            return;
        }
        int x0 = time_to_x(layer->in_time);
        int x1 = time_to_x(layer->out_time);
        std::shared_ptr<Layer> edge_layer;
        LayerTransitionEdge edge = LayerTransitionEdge::In;
        if (ev->button() == Qt::LeftButton &&
            transition_edge_target_at_pos(ev->pos(), &edge_layer, &edge) && edge_layer == layer) {
            select_layer_from_mouse(layer->id, ev->modifiers());
            select_transition_target(layer->id, edge);
            if (layer->locked) {
                ev->accept();
                return;
            }
            ev->accept();
            return;
        }
        const bool hit_strip =
            ev->pos().x() >= std::min(x0, x1) - kLayerTrimHitWidth &&
            ev->pos().x() <= std::max(x0, x1) + kLayerTrimHitWidth;
        if (layer->locked && hit_strip) {
            ev->accept();
            return;
        }
        if (hit_strip) {
            clear_transition_selection();
            select_layer_from_mouse(layer->id, ev->modifiers());
        }
        if (std::abs(ev->pos().x() - x0) <= kLayerTrimHitWidth) {
            begin_layer_strip_drag(layer->id, DragMode::TrimIn, x_to_time(ev->pos().x()));
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
        if (std::abs(ev->pos().x() - x1) <= kLayerTrimHitWidth) {
            begin_layer_strip_drag(layer->id, DragMode::TrimOut, x_to_time(ev->pos().x()));
            setCursor(Qt::SizeHorCursor);
            ev->accept();
            return;
        }
        if (ev->pos().x() >= std::min(x0, x1) && ev->pos().x() <= std::max(x0, x1)) {
            begin_layer_strip_drag(layer->id, DragMode::Layer, x_to_time(ev->pos().x()));
            setCursor(Qt::ClosedHandCursor);
            ev->accept();
            return;
        }
    }

    if (ev->button() == Qt::LeftButton && ev->pos().y() >= ruler_height()) {
        clear_transition_selection();
        const bool property_row = row >= 0 && row < (int)rows.size() &&
                                  rows[row].is_property;
        if (!property_row && !selected_layer_ids_.empty()) {
            set_selected_layers({});
            emit layers_selected({});
        }
        drag_mode_ = DragMode::Marquee;
        marquee_start_ = ev->pos();
        marquee_current_ = ev->pos();
        marquee_additive_ = ev->modifiers() & Qt::ShiftModifier;
        marquee_moved_ = false;
        if (!marquee_additive_)
            selected_keyframes_.clear();
        ev->accept();
        update();
        return;
    }

    clear_transition_selection();
    set_selected_layers({});
    emit layers_selected({});
    ev->accept();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *ev)
{
    if (!title_) return;
    if (graph_editor_enabled_ && graph_mouse_move(ev))
        return;
    double t = std::clamp(x_to_time(ev->pos().x()), 0.0, title_->duration);

    if (drag_mode_ == DragMode::Playhead) {
        emit playhead_changed(t);
        return;
    }

    if (drag_mode_ == DragMode::PauseMarker) {
        title_->pause_time = t;
        update();
        return;
    }

    if (drag_mode_ == DragMode::StingerTransitionPoint) {
        set_stinger_transition_point_seconds(*title_, t);
        update();
        return;
    }

    if (drag_mode_ == DragMode::LoopStart) {
        title_->loop_start = std::clamp(t, 0.0, title_->loop_end);
        update();
        return;
    }

    if (drag_mode_ == DragMode::LoopEnd) {
        title_->loop_end = std::clamp(t, title_->loop_start, title_->duration);
        update();
        return;
    }

    if (drag_mode_ == DragMode::Keyframe) {
        double delta = t - drag_start_time_;
        for (const auto &dragged : dragged_keyframes_) {
            if (timeline_owner_locked(*title_, dragged.ref.layer_id)) continue;
            auto prop = find_timeline_property(dragged.ref.layer_id, dragged.ref.prop_name);
            if (!prop || dragged.ref.index < 0 || dragged.ref.index >= (int)prop.keyframe_count()) continue;
            const double owner_in = timeline_owner_in_time(*title_, dragged.ref.layer_id);
            const double owner_out = timeline_owner_out_time(*title_, dragged.ref.layer_id);
            prop.set_keyframe_time((size_t)dragged.ref.index,
                                   std::clamp(dragged.start_time + delta, 0.0,
                                              std::max(0.0, owner_out - owner_in)));
        }
        update();
        return;
    }

    if (drag_mode_ == DragMode::AudioFadeIn || drag_mode_ == DragMode::AudioFadeOut) {
        if (auto layer = title_->find_layer(drag_layer_id_)) {
            if (!layer->locked && layer->type == LayerType::Audio) {
                const double clip_duration = std::max(0.0, layer->out_time - layer->in_time);
                if (drag_mode_ == DragMode::AudioFadeIn) {
                    layer->audio_fade_in = std::clamp(t - layer->in_time, 0.0,
                        std::max(0.0, clip_duration - layer->audio_fade_out));
                } else {
                    layer->audio_fade_out = std::clamp(layer->out_time - t, 0.0,
                        std::max(0.0, clip_duration - layer->audio_fade_in));
                }
                emit audio_layer_property_changed(false);
                update();
            }
        }
        return;
    }

    if (drag_mode_ == DragMode::TransitionDuration) {
        if (auto layer = title_->find_layer(drag_layer_id_)) {
            if (!layer->locked) {
                if (auto *transition = find_layer_transition(layer->transitions, drag_transition_edge_)) {
                    transition->duration = drag_transition_edge_ == LayerTransitionEdge::In
                        ? t - layer->in_time : layer->out_time - t;
                    normalize_transition_durations(*layer);
                    /* The descriptor remains the timeline authoring surface,
                     * while the managed TextAnimator is the sole renderer.
                     * Keep it synchronized during the drag so the canvas
                     * previews the same duration continuously, not only after
                     * mouse release. */
                    synchronize_text_transition_animators(
                        layer->transitions, layer->text_animators,
                        layer->in_time, layer->out_time, nullptr, true);
                    update();
                }
            }
        }
        return;
    }

    if (drag_mode_ == DragMode::Marquee) {
        marquee_current_ = ev->pos();
        if ((marquee_current_ - marquee_start_).manhattanLength() >= 3)
            marquee_moved_ = true;
        update();
        return;
    }

    if (drag_mode_ == DragMode::TrimIn || drag_mode_ == DragMode::TrimOut) {
        const double delta = t - drag_start_time_;
        if (dragged_layer_strips_.empty()) {
            if (auto layer = title_->find_layer(drag_layer_id_)) {
                if (layer->linked_media_stream) return;
                DraggedLayerStrip dragged;
                dragged.layer_id = layer->id;
                dragged.start_in = layer->in_time;
                dragged.start_out = layer->out_time;
                dragged.start_media_in = layer->type == LayerType::Audio
                    ? layer->audio_in_point : layer->video_in_point;
                dragged.start_media_out = layer->type == LayerType::Audio
                    ? layer->audio_out_point : layer->video_out_point;
                for (auto prop : timeline_properties(*layer)) {
                    if (!prop) continue;
                    for (int i = 0; i < (int)prop.keyframe_count(); ++i)
                        dragged.keyframes.push_back({prop.name(), i, prop.keyframe_time((size_t)i)});
                }
                dragged_layer_strips_.push_back(std::move(dragged));
            }
        }
        for (const auto &dragged : dragged_layer_strips_) {
            auto layer = title_->find_layer(dragged.layer_id);
            if (!layer || layer->locked) continue;
            if (drag_mode_ == DragMode::TrimIn) {
                const bool media_layer = layer->type == LayerType::Video || layer->type == LayerType::Audio;
                const bool media_loops = layer->type == LayerType::Video
                    ? layer->video_loop
                    : (layer->type == LayerType::Audio &&
                       (layer->audio_loop || layer->audio_playback_mode == AudioPlaybackMode::Loop));
                const double minimum_in = media_layer && !media_loops
                    ? std::max(0.0, dragged.start_in - dragged.start_media_in)
                    : 0.0;
                const double new_in = std::clamp(dragged.start_in + delta,
                                                 minimum_in,
                                                 std::max(minimum_in, dragged.start_out - obs_frame_duration()));
                const double keyframe_offset = dragged.start_in - new_in;
                layer->in_time = new_in;
                layer->out_time = dragged.start_out;
                if (layer->type == LayerType::Video || layer->type == LayerType::Audio) {
                    const double media_delta = new_in - dragged.start_in;
                    set_layer_media_range_in_point(*layer, dragged.start_media_in + media_delta);
                }
                for (const auto &keyframe : dragged.keyframes) {
                    auto prop = find_timeline_property(*layer, keyframe.prop_name);
                    if (!prop || keyframe.index < 0 || keyframe.index >= (int)prop.keyframe_count()) continue;
                    prop.set_keyframe_time((size_t)keyframe.index,
                                           std::clamp(keyframe.start_time + keyframe_offset,
                                                      0.0,
                                                      std::max(0.0, layer->out_time - layer->in_time)));
                }
                normalize_transition_durations(*layer);
                synchronize_text_transition_animators(
                    layer->transitions, layer->text_animators,
                    layer->in_time, layer->out_time, nullptr, true);
                /* Keep live trim cheap: linked audio-stream rows are resynchronised
                 * once on mouse release instead of on every pointer move. */
            } else {
                layer->in_time = dragged.start_in;
                double maximum_out = title_->duration;
                const bool media_layer = layer->type == LayerType::Video || layer->type == LayerType::Audio;
                const bool media_loops = layer->type == LayerType::Video
                    ? layer->video_loop
                    : (layer->type == LayerType::Audio &&
                       (layer->audio_loop || layer->audio_playback_mode == AudioPlaybackMode::Loop));
                const double media_duration = layer_media_duration_limit(*layer);
                if (media_layer && !media_loops && media_duration > 0.0) {
                    maximum_out = std::min(maximum_out,
                        dragged.start_out + std::max(
                            0.0, media_duration - dragged.start_media_out));
                }
                layer->out_time = std::clamp(dragged.start_out + delta,
                                             dragged.start_in + obs_frame_duration(),
                                             maximum_out);
                if (layer->type == LayerType::Video || layer->type == LayerType::Audio) {
                    const double media_delta = layer->out_time - dragged.start_out;
                    set_layer_media_range_out_point(*layer, dragged.start_media_out + media_delta);
                }
                normalize_transition_durations(*layer);
                synchronize_text_transition_animators(
                    layer->transitions, layer->text_animators,
                    layer->in_time, layer->out_time, nullptr, true);
            }
        }
        update();
        emit layer_timing_changed(false);
        return;
    }

    if (drag_mode_ == DragMode::Layer) {
        double delta = t - drag_start_time_;
        if (dragged_layer_strips_.empty()) {
            if (auto layer = title_->find_layer(drag_layer_id_)) {
                if (layer->linked_media_stream) return;
                DraggedLayerStrip dragged;
                dragged.layer_id = layer->id;
                dragged.start_in = layer->in_time;
                dragged.start_out = layer->out_time;
                dragged.start_media_in = layer->type == LayerType::Audio
                    ? layer->audio_in_point : layer->video_in_point;
                dragged.start_media_out = layer->type == LayerType::Audio
                    ? layer->audio_out_point : layer->video_out_point;
                dragged_layer_strips_.push_back(std::move(dragged));
            }
        }
        double min_delta = -std::numeric_limits<double>::max();
        double max_delta = std::numeric_limits<double>::max();
        for (const auto &dragged : dragged_layer_strips_) {
            const double duration = std::max(obs_frame_duration(), dragged.start_out - dragged.start_in);
            min_delta = std::max(min_delta, -dragged.start_in);
            max_delta = std::min(max_delta, std::max(0.0, title_->duration - duration) - dragged.start_in);
        }
        if (std::isfinite(min_delta) && std::isfinite(max_delta))
            delta = std::clamp(delta, min_delta, max_delta);
        for (const auto &dragged : dragged_layer_strips_) {
            auto layer = title_->find_layer(dragged.layer_id);
            if (!layer || layer->locked) continue;
            const double duration = std::max(obs_frame_duration(), dragged.start_out - dragged.start_in);
            const double new_in = std::clamp(dragged.start_in + delta,
                                             0.0,
                                             std::max(0.0, title_->duration - duration));
            layer->in_time = new_in;
            layer->out_time = std::min(title_->duration, new_in + duration);
        }
        update();
        emit layer_timing_changed(false);
        return;
    }

    auto rows = timeline_rows(title_);
    int row = (ev->pos().y() - ruler_height() + scroll_y_) / row_height();
    if (row >= 0 && row < (int)rows.size() && !rows[row].is_property) {
        if (rows[row].is_camera || rows[row].is_camera_switch ||
            rows[row].is_effect_group) {
            unsetCursor();
            return;
        }
        if (rows[row].layer) {
            const int x0 = time_to_x(rows[row].layer->in_time);
            const int x1 = time_to_x(rows[row].layer->out_time);
            if (rows[row].layer->type == LayerType::Audio && !rows[row].layer->locked) {
                const double clip_duration = std::max(0.0, rows[row].layer->out_time - rows[row].layer->in_time);
                const int fade_in_x = time_to_x(rows[row].layer->in_time + std::clamp(rows[row].layer->audio_fade_in, 0.0, clip_duration));
                const int fade_out_x = time_to_x(rows[row].layer->out_time - std::clamp(rows[row].layer->audio_fade_out, 0.0, clip_duration));
                const int strip_top = ruler_height() + row * row_height() - scroll_y_ + 3;
                if (ev->pos().y() >= strip_top && ev->pos().y() <= strip_top + 11 &&
                    std::min(std::abs(ev->pos().x() - fade_in_x), std::abs(ev->pos().x() - fade_out_x)) <= 7) {
                    setCursor(Qt::SizeHorCursor);
                    return;
                }
            }
            if (std::min(std::abs(ev->pos().x() - x0),
                         std::abs(ev->pos().x() - x1)) <=
                kLayerTrimHitWidth) {
                if (!rows[row].layer->locked)
                    setCursor(Qt::SizeHorCursor);
                else
                    setCursor(Qt::ArrowCursor);
                return;
            }
        }
        TransitionHit transition_hit;
        if (transition_hit_at_pos(ev->pos(), &transition_hit) && transition_hit.layer) {
            if (!transition_hit.layer->locked && transition_hit.duration_handle)
                setCursor(Qt::SizeHorCursor);
            else
                setCursor(Qt::ArrowCursor);
            return;
        }
        std::shared_ptr<Layer> transition_target_layer;
        LayerTransitionEdge transition_target_edge = LayerTransitionEdge::In;
        if (transition_edge_target_at_pos(ev->pos(), &transition_target_layer,
                                          &transition_target_edge)) {
            setCursor(Qt::ArrowCursor);
            return;
        }
        if (!rows[row].layer || rows[row].layer->locked) {
            unsetCursor();
            return;
        }
        int x0 = time_to_x(rows[row].layer->in_time);
        int x1 = time_to_x(rows[row].layer->out_time);
        if (std::abs(ev->pos().x() - x0) <= kLayerTrimHitWidth ||
            std::abs(ev->pos().x() - x1) <= kLayerTrimHitWidth)
            setCursor(Qt::SizeHorCursor);
        else if (ev->pos().x() >= std::min(x0, x1) && ev->pos().x() <= std::max(x0, x1))
            setCursor(Qt::OpenHandCursor);
        else
            unsetCursor();
    } else {
        unsetCursor();
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *ev)
{
    if (graph_editor_enabled_ && graph_mouse_release(ev))
        return;
    bool changed = drag_mode_ == DragMode::Keyframe ||
                   drag_mode_ == DragMode::TrimIn ||
                   drag_mode_ == DragMode::TrimOut ||
                   drag_mode_ == DragMode::Layer ||
                   drag_mode_ == DragMode::TransitionDuration ||
                   drag_mode_ == DragMode::AudioFadeIn ||
                   drag_mode_ == DragMode::AudioFadeOut ||
                   drag_mode_ == DragMode::LoopStart ||
                   drag_mode_ == DragMode::LoopEnd ||
                   drag_mode_ == DragMode::PauseMarker ||
                   drag_mode_ == DragMode::StingerTransitionPoint;

    if (drag_mode_ == DragMode::Marquee) {
        if (marquee_moved_)
            select_keyframes_in_rect(marquee_rect(), marquee_additive_);
        else if (!marquee_additive_)
            clear_keyframe_selection();
    }

    if (drag_mode_ == DragMode::Keyframe && title_) {
        std::map<KeyframeRef, double> selected_times;
        for (const auto &ref : selected_keyframes_) {
            auto prop = find_timeline_property(ref.layer_id, ref.prop_name);
            if (prop && ref.index >= 0 && ref.index < (int)prop.keyframe_count())
                selected_times[ref] = prop.keyframe_time(ref.index);
        }

        std::set<std::pair<std::string, std::string>> props_to_sort;
        for (const auto &dragged : dragged_keyframes_)
            props_to_sort.insert({dragged.ref.layer_id, dragged.ref.prop_name});

        for (const auto &prop_ref : props_to_sort) {
            if (timeline_owner_locked(*title_, prop_ref.first)) continue;
            if (auto prop = find_timeline_property(prop_ref.first, prop_ref.second))
                prop.sort_keyframes();
        }

        std::set<KeyframeRef> remapped;
        std::map<std::pair<std::string, std::string>, std::set<int>> used_indices;
        for (const auto &[ref, selected_time] : selected_times) {
            auto prop = find_timeline_property(ref.layer_id, ref.prop_name);
            if (!prop) continue;
            int best = -1;
            double best_distance = std::numeric_limits<double>::max();
            auto key = std::make_pair(ref.layer_id, ref.prop_name);
            for (int i = 0; i < (int)prop.keyframe_count(); ++i) {
                if (used_indices[key].count(i)) continue;
                double distance = std::abs(prop.keyframe_time(i) - selected_time);
                if (distance < best_distance) {
                    best = i;
                    best_distance = distance;
                }
            }
            if (best >= 0) {
                used_indices[key].insert(best);
                remapped.insert({ref.layer_id, ref.prop_name, best});
            }
        }
        selected_keyframes_ = std::move(remapped);
    }

    const bool transition_changed = drag_mode_ == DragMode::TransitionDuration;
    const bool strip_timing_changed = drag_mode_ == DragMode::TrimIn ||
                                      drag_mode_ == DragMode::TrimOut ||
                                      drag_mode_ == DragMode::Layer;
    const bool audio_fade_changed = drag_mode_ == DragMode::AudioFadeIn || drag_mode_ == DragMode::AudioFadeOut;
    if (strip_timing_changed) {
        for (const auto &dragged : dragged_layer_strips_) {
            auto layer = title_->find_layer(dragged.layer_id);
            if (layer && layer->type == LayerType::Video)
                synchronize_video_audio_streams(*title_, layer->id);
            /* Contract tokens for historical Video trim paths that now defer
             * synchronization to drag release:
             * synchronize_video_audio_streams(*title_, layer->id)
             * synchronize_video_audio_streams(*title_, layer->id) */
        }
    }
    drag_mode_ = DragMode::None;
    drag_layer_id_.clear();
    drag_prop_name_.clear();
    drag_keyframe_index_ = -1;
    drag_start_time_ = 0.0;
    drag_start_in_ = 0.0;
    drag_start_out_ = 0.0;
    dragged_keyframes_.clear();
    dragged_layer_strips_.clear();
    marquee_additive_ = false;
    marquee_moved_ = false;
    unsetCursor();
    update();
    if (transition_changed)
        emit transition_modified();
    else if (strip_timing_changed)
        emit layer_timing_changed(true);
    else if (audio_fade_changed)
        emit audio_layer_property_changed(true);
    else if (changed)
        emit keyframe_easing_changed();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent *ev)
{
    if (!ev || !title_) {
        QWidget::mouseDoubleClickEvent(ev);
        return;
    }

    if (graph_editor_enabled_ && graph_rect().contains(ev->pos())) {
        drag_mode_ = DragMode::None;
        temporal_drag_snapshots_.clear();
        graph_drag_hit_ = {};
        const GraphHit graph_hit = graph_hit_test(ev->pos());
        if (graph_hit.type != GraphHitType::None) {
            select_keyframe(graph_hit.ref.layer_id, graph_hit.ref.prop_name,
                            graph_hit.ref.index, false, false);
            TimelinePropertyRef prop;
            if (graph_ref_property(graph_hit.ref, &prop) && !prop.is_hold_only())
                show_temporal_velocity_dialog(prop, {graph_hit.ref.index});
            ev->accept();
            return;
        }

        std::shared_ptr<Layer> owner;
        TimelinePropertyRef prop = active_graph_property(&owner);
        if (prop && owner) {
            add_keyframe_at(owner->id, prop.name(),
                            std::clamp(graph_x_to_time(ev->pos().x()),
                                       0.0, title_->duration),
                            false);
            ev->accept();
            return;
        }
    }

    TransitionHit transition_hit;
    if (transition_hit_at_pos(ev->pos(), &transition_hit) &&
        transition_hit.layer && !transition_hit.layer->locked) {
        emit transition_edit_requested(transition_hit.layer->id,
                                       static_cast<int>(transition_hit.edge));
        ev->accept();
        return;
    }

    std::shared_ptr<Layer> hit_layer;
    TimelinePropertyRef hit_prop;
    int hit_index = -1;
    int hit_row = -1;
    std::string hit_owner;
    if (hit_keyframe(ev->pos(), &hit_layer, &hit_prop, &hit_index,
                     &hit_row, &hit_owner)) {
        select_keyframe(hit_owner, hit_prop.name(), hit_index, false, false);
        if (!hit_prop.is_hold_only())
            show_temporal_velocity_dialog(hit_prop, {hit_index});
        ev->accept();
        return;
    }

    const auto rows = timeline_rows(title_);
    const int row = (ev->pos().y() - ruler_height() + scroll_y_) /
                    row_height();
    if (row >= 0 && row < static_cast<int>(rows.size()) &&
        rows[static_cast<size_t>(row)].is_property) {
        const TimelineRow &entry = rows[static_cast<size_t>(row)];
        select_graph_property(entry.owner_id, entry.prop.name(),
                              entry.is_property_channel
                                  ? static_cast<int>(graph_mode_for_component(entry.property_channel)) : 3);
        add_keyframe_at(entry.owner_id, entry.prop.name(),
                        std::clamp(x_to_time(ev->pos().x()),
                                   0.0, title_->duration),
                        true);
        ev->accept();
        return;
    }

    QWidget::mouseDoubleClickEvent(ev);
}

#include "temporal-graph-editor.inc"

/* ══════════════════════════════════════════════════════════════════
 *  TitlePropertiesPanel
 * ══════════════════════════════════════════════════════════════════ */
