#include "text-animator.h"
#include "pattern-resource-cache.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <limits>
#include <deque>
#include <numeric>
#include <unordered_map>
#include <regex>
#include <sstream>

namespace {
constexpr double kPi = 3.1415926535897932384626433832795;

uint64_t fnv1a_append(uint64_t hash, const void *data, size_t size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

template<typename T>
uint64_t hash_value(uint64_t hash, const T &value)
{
    return fnv1a_append(hash, &value, sizeof(value));
}

uint64_t hash_string(uint64_t hash, const std::string &value)
{
    hash = fnv1a_append(hash, value.data(), value.size());
    const unsigned char separator = 0xff;
    return fnv1a_append(hash, &separator, 1);
}

uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

double random01(uint64_t seed)
{
    return static_cast<double>(mix64(seed) >> 11) * (1.0 / 9007199254740992.0);
}

double clamp01(double value)
{
    if (!std::isfinite(value))
        return 0.0;
    return std::clamp(value, 0.0, 1.0);
}

double text_selector_ease(double value, EasingType easing)
{
    const double x = clamp01(value);
    switch (easing) {
    case EasingType::Linear: return x;
    case EasingType::EaseIn: return x * x;
    case EasingType::EaseOut: return x * (2.0 - x);
    case EasingType::EaseInOut:
    case EasingType::Bezier:
        return x < 0.5 ? 2.0 * x * x
                       : -1.0 + (4.0 - 2.0 * x) * x;
    case EasingType::Hold: return x >= 1.0 ? 1.0 : 0.0;
    default: return x;
    }
}

double reverse_text_selector_ease(double value, EasingType easing)
{
    return 1.0 - text_selector_ease(1.0 - clamp01(value), easing);
}

bool utf8_decode(const std::string &text, size_t offset, uint32_t &cp, size_t &length)
{
    if (offset >= text.size())
        return false;
    const unsigned char c0 = static_cast<unsigned char>(text[offset]);
    if (c0 < 0x80) {
        cp = c0; length = 1; return true;
    }
    int count = 0;
    uint32_t value = 0;
    if ((c0 & 0xe0) == 0xc0) { count = 2; value = c0 & 0x1f; }
    else if ((c0 & 0xf0) == 0xe0) { count = 3; value = c0 & 0x0f; }
    else if ((c0 & 0xf8) == 0xf0) { count = 4; value = c0 & 0x07; }
    else { cp = 0xfffd; length = 1; return true; }
    if (offset + static_cast<size_t>(count) > text.size()) {
        cp = 0xfffd; length = 1; return true;
    }
    for (int i = 1; i < count; ++i) {
        const unsigned char cx = static_cast<unsigned char>(text[offset + i]);
        if ((cx & 0xc0) != 0x80) { cp = 0xfffd; length = 1; return true; }
        value = (value << 6) | (cx & 0x3f);
    }
    cp = value; length = static_cast<size_t>(count); return true;
}

bool unicode_whitespace(uint32_t cp)
{
    return cp == 0x09 || cp == 0x0a || cp == 0x0b || cp == 0x0c ||
           cp == 0x0d || cp == 0x20 || cp == 0x85 || cp == 0xa0 ||
           cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) ||
           cp == 0x2028 || cp == 0x2029 || cp == 0x202f || cp == 0x205f ||
           cp == 0x3000;
}

bool unicode_digit(uint32_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) ||
           (cp >= 0x06f0 && cp <= 0x06f9) || (cp >= 0x0966 && cp <= 0x096f) ||
           (cp >= 0xff10 && cp <= 0xff19);
}

bool unicode_upper(uint32_t cp)
{
    return (cp >= 'A' && cp <= 'Z') ||
           (cp >= 0x0391 && cp <= 0x03a1) || (cp >= 0x03a3 && cp <= 0x03ab) ||
           (cp >= 0x0410 && cp <= 0x042f) || (cp >= 0x0400 && cp <= 0x040f);
}

bool unicode_lower(uint32_t cp)
{
    return (cp >= 'a' && cp <= 'z') ||
           (cp >= 0x03b1 && cp <= 0x03c1) || (cp >= 0x03c2 && cp <= 0x03cb) ||
           (cp >= 0x0430 && cp <= 0x044f) || (cp >= 0x0450 && cp <= 0x045f);
}

bool unicode_punctuation(uint32_t cp)
{
    return (cp >= 0x21 && cp <= 0x2f) || (cp >= 0x3a && cp <= 0x40) ||
           (cp >= 0x5b && cp <= 0x60) || (cp >= 0x7b && cp <= 0x7e) ||
           (cp >= 0x037e && cp <= 0x0387) || (cp >= 0x055a && cp <= 0x055f) ||
           (cp >= 0x0609 && cp <= 0x061f) || (cp >= 0x2000 && cp <= 0x206f) ||
           (cp >= 0x3001 && cp <= 0x303f);
}

bool byte_range_all(const std::string &text, size_t start, size_t length,
                    bool (*predicate)(uint32_t), bool require_nonempty = true)
{
    const size_t end = std::min(text.size(), start + length);
    bool seen = false;
    for (size_t pos = start; pos < end;) {
        uint32_t cp = 0; size_t count = 1;
        utf8_decode(text, pos, cp, count);
        if (!predicate(cp))
            return false;
        seen = true;
        pos += std::max<size_t>(1, count);
    }
    return seen || !require_nonempty;
}

bool byte_range_intersects(size_t a0, size_t a1, size_t b0, size_t b1)
{
    return a0 < b1 && b0 < a1;
}

TextAnimatorUnitRange cluster_range(const TextLayoutCluster &cluster, size_t index,
                                    const std::string &text)
{
    TextAnimatorUnitRange range;
    range.byte_start = cluster.byte_start;
    range.byte_length = cluster.byte_length;
    range.cluster_begin = index;
    range.cluster_count = 1;
    range.source_index = index;
    range.whitespace = byte_range_all(text, range.byte_start, range.byte_length,
                                      unicode_whitespace, false);
    return range;
}

size_t unit_index_for_cluster(const std::vector<TextAnimatorUnitRange> &units,
                              size_t cluster_index)
{
    for (size_t i = 0; i < units.size(); ++i) {
        const auto &unit = units[i];
        const bool explicit_member = !unit.cluster_indices.empty() &&
            std::find(unit.cluster_indices.begin(), unit.cluster_indices.end(),
                      cluster_index) != unit.cluster_indices.end();
        const bool contiguous_member = unit.cluster_indices.empty() &&
            cluster_index >= unit.cluster_begin &&
            cluster_index < unit.cluster_begin + unit.cluster_count;
        if (explicit_member || contiguous_member)
            return i;
    }
    return units.size();
}

std::vector<size_t> unit_cluster_indices(const TextAnimatorUnitRange &unit,
                                         size_t cluster_limit)
{
    if (!unit.cluster_indices.empty()) {
        std::vector<size_t> result;
        result.reserve(unit.cluster_indices.size());
        for (size_t index : unit.cluster_indices) {
            if (index < cluster_limit)
                result.push_back(index);
        }
        return result;
    }
    const size_t begin = std::min(unit.cluster_begin, cluster_limit);
    const size_t end = std::min(begin + unit.cluster_count, cluster_limit);
    std::vector<size_t> result;
    result.reserve(end - begin);
    for (size_t index = begin; index < end; ++index)
        result.push_back(index);
    return result;
}

double smoothstep(double edge0, double edge1, double x)
{
    if (std::abs(edge1 - edge0) < 1e-12)
        return x >= edge1 ? 1.0 : 0.0;
    const double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

double shaped_range_value(TextRangeShape shape, double normalized)
{
    switch (shape) {
    case TextRangeShape::Square:
        return normalized >= 0.0 && normalized <= 1.0 ? 1.0 : 0.0;
    case TextRangeShape::RampUp:
        return clamp01(normalized);
    case TextRangeShape::RampDown:
        return 1.0 - clamp01(normalized);
    case TextRangeShape::Triangle:
        return clamp01(1.0 - std::abs(normalized * 2.0 - 1.0));
    case TextRangeShape::Round: {
        const double x = normalized * 2.0 - 1.0;
        return std::abs(x) >= 1.0 ? 0.0 : std::sqrt(std::max(0.0, 1.0 - x * x));
    }
    case TextRangeShape::Smooth:
        return smoothstep(0.0, 1.0, normalized);
    }
    return 0.0;
}

double apply_selector_ease(double value, double ease_low, double ease_high)
{
    value = clamp01(value);
    const double low = std::clamp(ease_low / 100.0, -1.0, 1.0);
    const double high = std::clamp(ease_high / 100.0, -1.0, 1.0);
    if (low > 0.0)
        value = std::pow(value, 1.0 + low * 3.0);
    else if (low < 0.0)
        value = 1.0 - std::pow(1.0 - value, 1.0 + (-low) * 3.0);
    if (high > 0.0)
        value = 1.0 - std::pow(1.0 - value, 1.0 + high * 3.0);
    else if (high < 0.0)
        value = std::pow(value, 1.0 + (-high) * 3.0);
    return clamp01(value);
}

double combine_influence(double current, double next,
                         TextSelectorCombinationMode mode, bool first)
{
    current = clamp01(current);
    next = clamp01(next);
    if (first)
        return mode == TextSelectorCombinationMode::Subtract ? clamp01(1.0 - next) : next;
    switch (mode) {
    case TextSelectorCombinationMode::Add: return clamp01(current + next);
    case TextSelectorCombinationMode::Subtract: return clamp01(current - next);
    case TextSelectorCombinationMode::Intersect: return std::min(current, next);
    case TextSelectorCombinationMode::Difference: return std::abs(current - next);
    case TextSelectorCombinationMode::Minimum: return std::min(current, next);
    case TextSelectorCombinationMode::Maximum: return std::max(current, next);
    case TextSelectorCombinationMode::Multiply: return current * next;
    }
    return next;
}

uint32_t color_from_channels(double a, double r, double g, double b)
{
    auto byte = [](double value) {
        return static_cast<uint32_t>(std::clamp(std::llround(value), 0LL, 255LL));
    };
    return (byte(a) << 24) | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

void apply_property(TextAnimatorClusterState &state,
                    const TextAnimatorProperty &property,
                    double influence, double time,
                    TextAnimatorBlendMode blend)
{
    if (!property.enabled || influence <= 0.0)
        return;
    const double a = property.value.evaluate(time);
    const double b = property.secondary.evaluate(time);
    const double c = property.tertiary.evaluate(time);
    const double d = property.quaternary.evaluate(time);
    auto additive = [&](double &target, double value) { target += value * influence; };
    auto multiplicative = [&](double &target, double value, double neutral) {
        const double mixed = neutral + (value - neutral) * influence;
        target *= mixed;
    };
    auto replacement = [&](double &target, double value) {
        target = target * (1.0 - influence) + value * influence;
    };
    auto scalar = [&](double &target, double value, double neutral = 0.0) {
        if (blend == TextAnimatorBlendMode::Replace) replacement(target, value);
        else if (blend == TextAnimatorBlendMode::Multiply) multiplicative(target, value, neutral);
        else additive(target, value);
    };

    switch (property.type) {
    case TextAnimatorPropertyType::Position:
        scalar(state.position_x, a); scalar(state.position_y, b); break;
    case TextAnimatorPropertyType::AnchorPoint:
        scalar(state.anchor_x, a); scalar(state.anchor_y, b); break;
    case TextAnimatorPropertyType::Scale:
        multiplicative(state.scale_x, a, 1.0); multiplicative(state.scale_y, b, 1.0); break;
    case TextAnimatorPropertyType::Rotation: scalar(state.rotation, a); break;
    case TextAnimatorPropertyType::CharacterRotation: scalar(state.character_rotation, a); break;
    case TextAnimatorPropertyType::Skew: scalar(state.skew, a); break;
    case TextAnimatorPropertyType::SkewAxis: scalar(state.skew_axis, a); break;
    case TextAnimatorPropertyType::Opacity:
        multiplicative(state.opacity, a, 1.0); break;
    case TextAnimatorPropertyType::FontSize: scalar(state.font_size_delta, a); break;
    case TextAnimatorPropertyType::HorizontalScale:
        multiplicative(state.horizontal_scale, a, 1.0); break;
    case TextAnimatorPropertyType::VerticalScale:
        multiplicative(state.vertical_scale, a, 1.0); break;
    case TextAnimatorPropertyType::Tracking: scalar(state.tracking, a); break;
    case TextAnimatorPropertyType::BaselineShift: scalar(state.baseline_shift, a); break;
    case TextAnimatorPropertyType::FillColor:
        state.has_fill_color = true;
        state.fill_color = color_from_channels(a, b, c, d);
        state.fill_color_mix = std::max(state.fill_color_mix, influence);
        break;
    case TextAnimatorPropertyType::FillOpacity:
        multiplicative(state.fill_opacity, a, 1.0); break;
    case TextAnimatorPropertyType::StrokeColor:
        state.has_stroke_color = true;
        state.stroke_color = color_from_channels(a, b, c, d);
        state.stroke_color_mix = std::max(state.stroke_color_mix, influence);
        break;
    case TextAnimatorPropertyType::StrokeWidth: scalar(state.stroke_width_delta, a); break;
    case TextAnimatorPropertyType::StrokeOpacity:
        multiplicative(state.stroke_opacity, a, 1.0); break;
    case TextAnimatorPropertyType::Blur: scalar(state.blur, a); break;
    case TextAnimatorPropertyType::GlowIntensity: scalar(state.glow_intensity, a); break;
    case TextAnimatorPropertyType::ShadowOffset:
        scalar(state.shadow_offset_x, a); scalar(state.shadow_offset_y, b); break;
    case TextAnimatorPropertyType::ShadowOpacity:
        multiplicative(state.shadow_opacity, a, 1.0); break;
    case TextAnimatorPropertyType::CharacterVisibility:
        multiplicative(state.visibility, a, 1.0); break;
    case TextAnimatorPropertyType::CharacterReveal:
        multiplicative(state.reveal, a, 1.0);
        if (static_cast<int>(std::llround(b)) >=
                static_cast<int>(TextRevealDirection::Left) &&
            static_cast<int>(std::llround(b)) <=
                static_cast<int>(TextRevealDirection::Down))
            state.reveal_direction = static_cast<TextRevealDirection>(
                static_cast<int>(std::llround(b)));
        break;
    case TextAnimatorPropertyType::CharacterReplacement:
        state.replacement = std::max(state.replacement, a * influence); break;
    case TextAnimatorPropertyType::ScrambleAmount:
        state.scramble = std::max(state.scramble, a * influence); break;
    case TextAnimatorPropertyType::LineSpacing:
    case TextAnimatorPropertyType::SpaceBeforeParagraph:
    case TextAnimatorPropertyType::SpaceAfterParagraph:
    case TextAnimatorPropertyType::WordSpacing:
    case TextAnimatorPropertyType::TextBoxPositionOffset:
    case TextAnimatorPropertyType::TextBoxScale:
    case TextAnimatorPropertyType::LineAlignmentOffset:
        /* These are evaluated by the layout-animation path. They remain in
         * the shared model/signature even when the quad compositor is used. */
        break;
    }
}

bool unit_text_matches(const TextSelector &selector,
                       const TextLayoutData &layout,
                       const TextAnimatorUnitRange &unit)
{
    const size_t start = std::min(unit.byte_start, layout.text.size());
    const size_t end = std::min(layout.text.size(), start + unit.byte_length);
    const std::string value = layout.text.substr(start, end - start);
    switch (selector.match_mode) {
    case TextMatchMode::CharacterRange:
    case TextMatchMode::WordRange:
    case TextMatchMode::LineRange:
    case TextMatchMode::ParagraphRange:
        return unit.source_index >= selector.range_start &&
               unit.source_index < selector.range_end;
    case TextMatchMode::ExactText:
        if (selector.case_sensitive)
            return value == selector.match_text;
        else {
            auto lower_ascii = [](std::string s) {
                for (char &ch : s)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                return s;
            };
            return lower_ascii(value) == lower_ascii(selector.match_text);
        }
    case TextMatchMode::RegularExpression: {
        const auto expression = fxm::text::cached_pattern(
            selector.regular_expression, selector.case_sensitive);
        return expression && std::regex_search(value, *expression);
    }
    case TextMatchMode::Whitespace:
        return byte_range_all(layout.text, start, end - start, unicode_whitespace, false);
    case TextMatchMode::Numbers:
        return byte_range_all(layout.text, start, end - start, unicode_digit);
    case TextMatchMode::Uppercase:
        return byte_range_all(layout.text, start, end - start, unicode_upper);
    case TextMatchMode::Lowercase:
        return byte_range_all(layout.text, start, end - start, unicode_lower);
    case TextMatchMode::Punctuation:
        return byte_range_all(layout.text, start, end - start, unicode_punctuation);
    case TextMatchMode::RichTextStyleRun:
        return selector.rich_text_run_index < 0 ||
               static_cast<int>(unit.source_index) == selector.rich_text_run_index;
    case TextMatchMode::ExternalDataRange:
    case TextMatchMode::NewlyAddedText:
    case TextMatchMode::ChangedText:
        for (const auto &tagged : selector.tagged_byte_ranges) {
            if (byte_range_intersects(start, end, tagged.first,
                                      tagged.first + tagged.second))
                return true;
        }
        return false;
    }
    return false;
}

void add_animated_property_hash(uint64_t &hash, const AnimatedProperty &property)
{
    hash = hash_string(hash, property.name);
    hash = hash_value(hash, property.static_value);
    const uint64_t count = property.keyframes.size();
    hash = hash_value(hash, count);
    for (const auto &key : property.keyframes) {
        hash = hash_value(hash, key.time); hash = hash_value(hash, key.value);
        hash = hash_value(hash, key.easing); hash = hash_value(hash, key.cx1);
        hash = hash_value(hash, key.cy1); hash = hash_value(hash, key.cx2);
        hash = hash_value(hash, key.cy2); hash = hash_value(hash, key.temporal_mode);
        hash = hash_value(hash, key.incoming_influence);
        hash = hash_value(hash, key.outgoing_influence);
        hash = hash_value(hash, key.incoming_speed);
        hash = hash_value(hash, key.outgoing_speed);
        hash = hash_value(hash, key.temporal_tangents_linked);
        hash = hash_value(hash, key.temporal_velocity_explicit);
    }
}

} // namespace

const std::vector<TextAnimatorUnitRange> &TextAnimatorUnitMap::units(TextAnimatorUnit unit) const
{
    switch (unit) {
    case TextAnimatorUnit::CharacterExcludingSpaces: return characters_excluding_spaces;
    case TextAnimatorUnit::Word: return words;
    case TextAnimatorUnit::Sentence: return sentences;
    case TextAnimatorUnit::Line: return lines;
    case TextAnimatorUnit::Paragraph: return paragraphs;
    case TextAnimatorUnit::RichTextRun: return rich_text_runs;
    case TextAnimatorUnit::WholeLayer: return whole_layer;
    case TextAnimatorUnit::Grapheme:
    case TextAnimatorUnit::Character:
    default: return graphemes;
    }
}


namespace {
std::string text_animator_cluster_token(const TextLayoutData &layout, size_t index)
{
    if (index >= layout.clusters.size())
        return {};
    const TextLayoutCluster &cluster = layout.clusters[index];
    const size_t start = std::min(cluster.byte_start, layout.text.size());
    const size_t length = std::min(cluster.byte_length, layout.text.size() - start);
    return layout.text.substr(start, length);
}

std::pair<size_t, size_t> text_animator_cluster_byte_span(
    const TextLayoutData &layout, size_t begin, size_t end)
{
    if (begin >= end || begin >= layout.clusters.size())
        return {0, 0};
    end = std::min(end, layout.clusters.size());
    const size_t byte_begin = std::min(layout.clusters[begin].byte_start,
                                       layout.text.size());
    const TextLayoutCluster &last = layout.clusters[end - 1];
    const size_t byte_end = std::min(layout.text.size(),
                                     last.byte_start + last.byte_length);
    return {byte_begin, byte_end > byte_begin ? byte_end - byte_begin : 0};
}

void text_animator_append_coalesced_range(
    std::vector<std::pair<size_t, size_t>> &ranges,
    std::pair<size_t, size_t> range)
{
    if (range.second == 0)
        return;
    if (!ranges.empty()) {
        auto &back = ranges.back();
        const size_t back_end = back.first + back.second;
        if (range.first <= back_end) {
            back.second = std::max(back_end, range.first + range.second) - back.first;
            return;
        }
    }
    ranges.push_back(range);
}

void text_animator_map_large_middle(
    const std::vector<std::string> &previous_tokens,
    const std::vector<std::string> &current_tokens,
    size_t previous_begin, size_t previous_end,
    size_t current_begin, size_t current_end,
    std::vector<int64_t> &previous_to_current,
    std::vector<int64_t> &current_to_previous)
{
    std::unordered_map<std::string, std::deque<size_t>> positions;
    for (size_t i = previous_begin; i < previous_end; ++i)
        positions[previous_tokens[i]].push_back(i);

    size_t minimum_previous = previous_begin;
    for (size_t current = current_begin; current < current_end; ++current) {
        auto found = positions.find(current_tokens[current]);
        if (found == positions.end())
            continue;
        auto &queue = found->second;
        while (!queue.empty() && queue.front() < minimum_previous)
            queue.pop_front();
        if (queue.empty())
            continue;
        const size_t previous = queue.front();
        queue.pop_front();
        previous_to_current[previous] = static_cast<int64_t>(current);
        current_to_previous[current] = static_cast<int64_t>(previous);
        minimum_previous = previous + 1;
    }
}
}

TextAnimatorContentChange map_text_animator_content_change(
    const TextLayoutData &previous, const TextLayoutData &current)
{
    TextAnimatorContentChange result;
    const size_t previous_count = previous.clusters.size();
    const size_t current_count = current.clusters.size();
    result.previous_to_current.assign(previous_count, -1);
    result.current_to_previous.assign(current_count, -1);

    std::vector<std::string> previous_tokens(previous_count);
    std::vector<std::string> current_tokens(current_count);
    for (size_t i = 0; i < previous_count; ++i)
        previous_tokens[i] = text_animator_cluster_token(previous, i);
    for (size_t i = 0; i < current_count; ++i)
        current_tokens[i] = text_animator_cluster_token(current, i);

    size_t prefix = 0;
    while (prefix < previous_count && prefix < current_count &&
           previous_tokens[prefix] == current_tokens[prefix]) {
        result.previous_to_current[prefix] = static_cast<int64_t>(prefix);
        result.current_to_previous[prefix] = static_cast<int64_t>(prefix);
        ++prefix;
    }

    size_t previous_suffix = previous_count;
    size_t current_suffix = current_count;
    while (previous_suffix > prefix && current_suffix > prefix &&
           previous_tokens[previous_suffix - 1] == current_tokens[current_suffix - 1]) {
        --previous_suffix;
        --current_suffix;
        result.previous_to_current[previous_suffix] =
            static_cast<int64_t>(current_suffix);
        result.current_to_previous[current_suffix] =
            static_cast<int64_t>(previous_suffix);
    }

    const size_t previous_middle = previous_suffix - prefix;
    const size_t current_middle = current_suffix - prefix;
    constexpr size_t kMaximumLcsCells = 4u * 1024u * 1024u;
    if (previous_middle > 0 && current_middle > 0 &&
        previous_middle <= kMaximumLcsCells / current_middle) {
        const size_t columns = current_middle + 1;
        std::vector<uint32_t> lcs((previous_middle + 1) * columns, 0);
        for (size_t pi = previous_middle; pi-- > 0;) {
            for (size_t ci = current_middle; ci-- > 0;) {
                const size_t cell = pi * columns + ci;
                if (previous_tokens[prefix + pi] == current_tokens[prefix + ci])
                    lcs[cell] = 1 + lcs[(pi + 1) * columns + ci + 1];
                else
                    lcs[cell] = std::max(lcs[(pi + 1) * columns + ci],
                                         lcs[pi * columns + ci + 1]);
            }
        }
        size_t pi = 0;
        size_t ci = 0;
        while (pi < previous_middle && ci < current_middle) {
            if (previous_tokens[prefix + pi] == current_tokens[prefix + ci]) {
                const size_t previous_index = prefix + pi;
                const size_t current_index = prefix + ci;
                result.previous_to_current[previous_index] =
                    static_cast<int64_t>(current_index);
                result.current_to_previous[current_index] =
                    static_cast<int64_t>(previous_index);
                ++pi;
                ++ci;
            } else if (lcs[(pi + 1) * columns + ci] >=
                       lcs[pi * columns + ci + 1]) {
                ++pi;
            } else {
                ++ci;
            }
        }
    } else if (previous_middle > 0 && current_middle > 0) {
        text_animator_map_large_middle(
            previous_tokens, current_tokens, prefix, previous_suffix,
            prefix, current_suffix, result.previous_to_current,
            result.current_to_previous);
    }

    std::vector<std::pair<size_t, size_t>> matches;
    matches.reserve(std::min(previous_count, current_count) + 2);
    matches.emplace_back(static_cast<size_t>(-1), static_cast<size_t>(-1));
    for (size_t current_index = 0; current_index < current_count; ++current_index) {
        if (result.current_to_previous[current_index] >= 0)
            matches.emplace_back(
                static_cast<size_t>(result.current_to_previous[current_index]),
                current_index);
    }
    matches.emplace_back(previous_count, current_count);

    for (size_t i = 1; i < matches.size(); ++i) {
        const size_t previous_begin = matches[i - 1].first == static_cast<size_t>(-1)
            ? 0 : matches[i - 1].first + 1;
        const size_t current_begin = matches[i - 1].second == static_cast<size_t>(-1)
            ? 0 : matches[i - 1].second + 1;
        const size_t previous_end = matches[i].first;
        const size_t current_end = matches[i].second;
        const bool has_previous = previous_begin < previous_end;
        const bool has_current = current_begin < current_end;
        if (has_current) {
            auto range = text_animator_cluster_byte_span(current,
                                                         current_begin,
                                                         current_end);
            if (has_previous)
                text_animator_append_coalesced_range(result.changed_byte_ranges, range);
            else
                text_animator_append_coalesced_range(result.added_byte_ranges, range);
        }
        if (has_previous) {
            text_animator_append_coalesced_range(
                result.removed_byte_ranges,
                text_animator_cluster_byte_span(previous,
                                                previous_begin,
                                                previous_end));
        }
    }

    result.identical = result.added_byte_ranges.empty() &&
                       result.changed_byte_ranges.empty() &&
                       result.removed_byte_ranges.empty() &&
                       previous_count == current_count;
    return result;
}

TextAnimatorStack text_animator_stack_for_content_change(
    const TextAnimatorStack &stack,
    const TextAnimatorContentChange &change,
    double content_change_local_time)
{
    TextAnimatorStack derived = stack;
    if (change.identical)
        return derived;

    for (size_t animator_index = 0;
         animator_index < derived.animators.size(); ++animator_index) {
        TextAnimator &animator = derived.animators[animator_index];
        for (TextSelector &selector : animator.selectors) {
            if (selector.type != TextSelectorType::TextBased)
                continue;
            if (selector.match_mode == TextMatchMode::NewlyAddedText)
                selector.tagged_byte_ranges = change.added_byte_ranges;
            else if (selector.match_mode == TextMatchMode::ChangedText)
                selector.tagged_byte_ranges = change.changed_byte_ranges;
        }

        TextMatchMode gate_mode = TextMatchMode::ChangedText;
        const std::vector<std::pair<size_t, size_t>> *gate_ranges = nullptr;
        if (animator.change_behaviour == TextChangeBehaviour::AnimateNewOnly) {
            gate_mode = TextMatchMode::NewlyAddedText;
            gate_ranges = &change.added_byte_ranges;
        } else if (animator.change_behaviour == TextChangeBehaviour::AnimateChangedOnly) {
            gate_mode = TextMatchMode::ChangedText;
            gate_ranges = &change.changed_byte_ranges;
        } else if (animator.change_behaviour == TextChangeBehaviour::AnimateRemovedOnly) {
            /* Removed clusters have no glyphs in the current layout. Keep the
             * animator deterministic and select nothing rather than retaining
             * stale glyph references from the previous layout. */
            gate_mode = TextMatchMode::ChangedText;
            static const std::vector<std::pair<size_t, size_t>> empty_ranges;
            gate_ranges = &empty_ranges;
        }
        if (gate_ranges) {
            TextSelector gate;
            gate.id = make_text_animator_id("selector", animator.id + ":content-change",
                                            animator_index);
            gate.name = animator.change_behaviour == TextChangeBehaviour::AnimateNewOnly
                ? "Newly Added Text" :
                animator.change_behaviour == TextChangeBehaviour::AnimateRemovedOnly
                    ? "Removed Text (not present)" : "Changed Text";
            gate.type = TextSelectorType::TextBased;
            gate.combination = TextSelectorCombinationMode::Intersect;
            gate.based_on = TextAnimatorUnit::Grapheme;
            gate.match_mode = gate_mode;
            gate.tagged_byte_ranges = *gate_ranges;
            animator.selectors.push_back(std::move(gate));
        }

        switch (animator.change_behaviour) {
        case TextChangeBehaviour::Restart:
        case TextChangeBehaviour::AnimateNewOnly:
        case TextChangeBehaviour::AnimateRemovedOnly:
        case TextChangeBehaviour::AnimateChangedOnly:
            animator.local_time_offset += content_change_local_time;
            break;
        case TextChangeBehaviour::ContinueLocalTime:
        case TextChangeBehaviour::PreserveCompletion:
        case TextChangeBehaviour::ReevaluateFullText:
            break;
        }
    }
    return derived;
}

TextAnimatorUnitMap build_text_animator_unit_map(const TextLayoutData &layout)
{
    TextAnimatorUnitMap map;
    map.graphemes.reserve(layout.clusters.size());
    for (size_t i = 0; i < layout.clusters.size(); ++i) {
        auto range = cluster_range(layout.clusters[i], i, layout.text);
        map.graphemes.push_back(range);
        if (!range.whitespace) {
            range.source_index = map.characters_excluding_spaces.size();
            map.characters_excluding_spaces.push_back(range);
        }
    }

    std::vector<size_t> logical_clusters(layout.clusters.size());
    std::iota(logical_clusters.begin(), logical_clusters.end(), size_t{0});
    std::stable_sort(
        logical_clusters.begin(), logical_clusters.end(),
        [&](size_t left, size_t right) {
            const TextLayoutCluster &a = layout.clusters[left];
            const TextLayoutCluster &b = layout.clusters[right];
            if (a.line_index != b.line_index)
                return a.line_index < b.line_index;
            if (a.byte_start != b.byte_start)
                return a.byte_start < b.byte_start;
            if (a.x != b.x)
                return a.x < b.x;
            return left < right;
        });

    /* Words are built in logical reading order from shaped clusters, never
     * raw code units. A visual-line boundary is also a separator: layout
     * engines are allowed to omit the wrapping space/newline cluster. */
    bool in_word = false;
    TextAnimatorUnitRange current;
    uint32_t current_line = 0;
    const auto finish_word = [&]() {
        if (!in_word)
            return;
        current.source_index = map.words.size();
        current.cluster_count = current.cluster_indices.size();
        map.words.push_back(current);
        in_word = false;
    };
    for (size_t cluster_index : logical_clusters) {
        const auto &cluster = map.graphemes[cluster_index];
        const uint32_t line = layout.clusters[cluster_index].line_index;
        if (in_word && line != current_line)
            finish_word();
        if (cluster.whitespace) {
            if (in_word) {
                finish_word();
            }
            continue;
        }
        if (!in_word) {
            current = cluster;
            current.cluster_begin = cluster_index;
            current.cluster_count = 1;
            current.cluster_indices = {cluster_index};
            current_line = line;
            in_word = true;
        } else {
            const size_t byte_end = std::max(
                current.byte_start + current.byte_length,
                cluster.byte_start + cluster.byte_length);
            current.byte_start = std::min(current.byte_start,
                                          cluster.byte_start);
            current.byte_length = byte_end - current.byte_start;
            current.cluster_begin = std::min(current.cluster_begin,
                                             cluster_index);
            current.cluster_indices.push_back(cluster_index);
            current.cluster_count = current.cluster_indices.size();
        }
    }
    finish_word();

    /* Legacy FXM sentence transitions grouped shaped clusters until sentence
     * punctuation or a hard line break. Keep the grouping Unicode-safe while
     * preserving the historical . ! ? boundaries. Greek and Arabic question
     * marks are accepted as additional deterministic terminators. */
    bool in_sentence = false;
    TextAnimatorUnitRange sentence;
    auto cluster_terminates_sentence = [&](const TextAnimatorUnitRange &range) {
        const size_t end = std::min(layout.text.size(),
                                    range.byte_start + range.byte_length);
        for (size_t pos = range.byte_start; pos < end;) {
            uint32_t cp = 0; size_t length = 1;
            utf8_decode(layout.text, pos, cp, length);
            if (cp == '.' || cp == '!' || cp == '?' || cp == 0x037e ||
                cp == 0x061f || cp == '\n' || cp == '\r')
                return true;
            pos += std::max<size_t>(1, length);
        }
        return false;
    };
    for (size_t cluster_index : logical_clusters) {
        const auto &cluster = map.graphemes[cluster_index];
        if (!in_sentence) {
            sentence = cluster;
            sentence.cluster_begin = cluster_index;
            sentence.cluster_count = 1;
            sentence.cluster_indices = {cluster_index};
            in_sentence = true;
        } else {
            const size_t byte_end = std::max(
                sentence.byte_start + sentence.byte_length,
                cluster.byte_start + cluster.byte_length);
            sentence.byte_start = std::min(sentence.byte_start,
                                           cluster.byte_start);
            sentence.byte_length = byte_end - sentence.byte_start;
            sentence.cluster_begin = std::min(sentence.cluster_begin,
                                              cluster_index);
            sentence.cluster_indices.push_back(cluster_index);
            sentence.cluster_count = sentence.cluster_indices.size();
            /* A sentence may begin with the whitespace/newline that followed
             * the preceding terminator. Treat the aggregate unit as whitespace
             * only when every shaped cluster in it is whitespace; otherwise
             * exclude_whitespace would incorrectly drop the complete sentence. */
            sentence.whitespace = sentence.whitespace && cluster.whitespace;
        }
        if (cluster_terminates_sentence(cluster)) {
            sentence.source_index = map.sentences.size();
            map.sentences.push_back(sentence);
            in_sentence = false;
        }
    }
    if (in_sentence) {
        sentence.source_index = map.sentences.size();
        map.sentences.push_back(sentence);
    }

    map.lines.reserve(layout.lines.size());
    for (size_t i = 0; i < layout.lines.size(); ++i) {
        const auto &line = layout.lines[i];
        map.lines.push_back({line.byte_start, line.byte_length,
                             line.cluster_begin, line.cluster_count, i, false});
    }

    /* Paragraph indices already come from the bidi/layout engine. Merge lines
     * with the same paragraph index so wrapped lines remain one paragraph. */
    for (size_t i = 0; i < layout.lines.size(); ++i) {
        const auto &line = layout.lines[i];
        if (map.paragraphs.empty() ||
            map.paragraphs.back().source_index != line.paragraph_index) {
            map.paragraphs.push_back({line.byte_start, line.byte_length,
                                      line.cluster_begin, line.cluster_count,
                                      line.paragraph_index, false});
        } else {
            auto &paragraph = map.paragraphs.back();
            paragraph.byte_length = line.byte_start + line.byte_length - paragraph.byte_start;
            paragraph.cluster_count = line.cluster_begin + line.cluster_count - paragraph.cluster_begin;
        }
    }

    map.rich_text_runs.reserve(layout.runs.size());
    for (size_t i = 0; i < layout.runs.size(); ++i) {
        const auto &run = layout.runs[i];
        map.rich_text_runs.push_back({run.byte_start, run.byte_length,
                                      run.cluster_begin, run.cluster_count, i, false});
    }

    if (!layout.clusters.empty()) {
        map.whole_layer.push_back({0, layout.text.size(), 0,
                                   layout.clusters.size(), 0, false});
    }
    return map;
}

double evaluate_text_selector_for_cluster(const TextSelector &selector,
                                          const TextLayoutData &layout,
                                          const TextAnimatorUnitMap &map,
                                          size_t cluster_index,
                                          double local_time)
{
    if (!selector.enabled || cluster_index >= layout.clusters.size())
        return 0.0;
    const auto &units = map.units(selector.based_on);
    const size_t unit_index = unit_index_for_cluster(units, cluster_index);
    if (unit_index >= units.size())
        return 0.0;
    const auto &unit = units[unit_index];
    const double count = static_cast<double>(std::max<size_t>(1, units.size()));
    double influence = 0.0;

    if (selector.type == TextSelectorType::Range) {
        double index = static_cast<double>(unit_index);
        if (selector.direction == TextSelectorDirection::Reverse)
            index = count - 1.0 - index;
        else if (selector.direction == TextSelectorDirection::Bidirectional) {
            const double centre = (count - 1.0) * 0.5;
            index = std::abs(index - centre) * 2.0;
        }
        if (selector.randomize_order) {
            std::vector<std::pair<uint64_t, size_t>> order;
            order.reserve(units.size());
            for (size_t i = 0; i < units.size(); ++i)
                order.emplace_back(mix64(static_cast<uint64_t>(selector.random_seed) ^ i), i);
            std::sort(order.begin(), order.end());
            for (size_t i = 0; i < order.size(); ++i) {
                if (order[i].second == unit_index) { index = static_cast<double>(i); break; }
            }
            if (selector.direction == TextSelectorDirection::Reverse)
                index = count - 1.0 - index;
        }
        const double start = selector.start.evaluate(local_time);
        const double end = selector.end.evaluate(local_time);
        const double offset = selector.offset.evaluate(local_time);
        const double domain = selector.range_units == TextRangeUnits::Percentage ? 100.0 : count;
        double coordinate = selector.range_units == TextRangeUnits::Percentage
            ? ((index + 0.5) / count) * 100.0 : index;
        coordinate -= offset;
        if (domain > 0.0) {
            coordinate = std::fmod(coordinate, domain);
            if (coordinate < 0.0) coordinate += domain;
        }
        const double low = std::min(start, end);
        const double high = std::max(start, end);
        const double span = std::max(1e-9, high - low);
        const bool selects_full_domain = low <= 0.0 && high >= domain;
        const bool selects_empty_domain = high <= 0.0 || low >= domain;
        if (selects_full_domain) {
            influence = 1.0;
        } else if (selects_empty_domain) {
            influence = 0.0;
        } else {
            const double normalized = (coordinate - low) / span;
            influence = shaped_range_value(selector.range_shape, normalized);
            const double smoothing = clamp01(selector.smoothness.evaluate(local_time) / 100.0);
            if (selector.range_shape == TextRangeShape::Square && smoothing > 0.0) {
                const double feather = std::max(1e-9, span * 0.5 * smoothing);
                const double left = smoothstep(low - feather, low + feather, coordinate);
                const double right = 1.0 - smoothstep(high - feather, high + feather, coordinate);
                influence = clamp01(left * right);
            }
        }
        influence = apply_selector_ease(influence,
                                        selector.ease_low.evaluate(local_time),
                                        selector.ease_high.evaluate(local_time));
        influence *= selector.amount.evaluate(local_time) / 100.0;
    } else if (selector.type == TextSelectorType::Staggered) {
        std::vector<size_t> ordered_units;
        ordered_units.reserve(units.size());
        for (size_t i = 0; i < units.size(); ++i) {
            if (!selector.exclude_whitespace || !units[i].whitespace)
                ordered_units.push_back(i);
        }
        if (ordered_units.empty())
            return 0.0;
        if (selector.randomize_order) {
            std::stable_sort(ordered_units.begin(), ordered_units.end(),
                [&](size_t lhs, size_t rhs) {
                    return mix64(static_cast<uint64_t>(selector.random_seed) ^ lhs) <
                           mix64(static_cast<uint64_t>(selector.random_seed) ^ rhs);
                });
        } else {
            /* Animator order is text order, not backend run-allocation order.
             * Mixed font sizes can produce QGlyphRuns in a different internal
             * sequence even on one visual line. */
            std::stable_sort(
                ordered_units.begin(), ordered_units.end(),
                [&](size_t lhs, size_t rhs) {
                    if (units[lhs].byte_start != units[rhs].byte_start)
                        return units[lhs].byte_start < units[rhs].byte_start;
                    return units[lhs].source_index < units[rhs].source_index;
                });
        }
        auto found = std::find(ordered_units.begin(), ordered_units.end(),
                               unit_index);
        if (found == ordered_units.end())
            return 0.0;
        size_t rank = static_cast<size_t>(found - ordered_units.begin());
        if (selector.direction == TextSelectorDirection::Reverse)
            rank = ordered_units.size() - 1 - rank;
        else if (selector.direction == TextSelectorDirection::Bidirectional) {
            const double centre = (static_cast<double>(ordered_units.size()) - 1.0) * 0.5;
            rank = static_cast<size_t>(std::llround(
                std::abs(static_cast<double>(rank) - centre) * 2.0));
        }
        const double completion = clamp01(
            selector.completion.evaluate(local_time) / 100.0);
        const double stagger = std::clamp(
            selector.stagger_percent.evaluate(local_time) / 100.0,
            0.0, 0.95);
        const double delay = ordered_units.size() <= 1 ? 0.0
            : stagger * static_cast<double>(rank) /
              static_cast<double>(ordered_units.size() - 1);
        const double span = std::max(0.05, 1.0 - stagger);
        const double phase = clamp01((completion - delay) / span);
        if (selector.stagger_mode == TextStaggerMode::Entrance)
            influence = 1.0 - text_selector_ease(phase, selector.unit_easing);
        else
            influence = reverse_text_selector_ease(phase, selector.unit_easing);
        influence *= selector.amount.evaluate(local_time) / 100.0;
    } else if (selector.type == TextSelectorType::Procedural) {
        double x = count <= 1.0 ? 0.0 : static_cast<double>(unit_index) / (count - 1.0);
        if (selector.direction == TextSelectorDirection::Reverse)
            x = 1.0 - x;
        const double frequency = selector.frequency.evaluate(local_time);
        const double phase = selector.phase.evaluate(local_time) +
                             selector.speed.evaluate(local_time) * local_time;
        const double p = x * frequency + phase;
        switch (selector.procedural_mode) {
        case TextProceduralMode::Random:
            influence = random01(static_cast<uint64_t>(selector.random_seed) ^ unit_index); break;
        case TextProceduralMode::Noise: {
            const double cell = std::floor(p);
            const double frac = p - cell;
            const double a = random01(static_cast<uint64_t>(selector.random_seed) ^ static_cast<uint64_t>(cell));
            const double b = random01(static_cast<uint64_t>(selector.random_seed) ^ static_cast<uint64_t>(cell + 1.0));
            influence = a + (b - a) * smoothstep(0.0, 1.0, frac); break;
        }
        case TextProceduralMode::Wave:
        case TextProceduralMode::Sine: influence = 0.5 + 0.5 * std::sin(p * 2.0 * kPi); break;
        case TextProceduralMode::Sawtooth: influence = p - std::floor(p); break;
        case TextProceduralMode::Pulse: influence = (p - std::floor(p)) < 0.5 ? 1.0 : 0.0; break;
        case TextProceduralMode::Alternating: influence = unit_index % 2 == 0 ? 1.0 : 0.0; break;
        case TextProceduralMode::DistanceFromStart: influence = 1.0 - x; break;
        case TextProceduralMode::DistanceFromEnd: influence = x; break;
        case TextProceduralMode::DistanceFromCentre: influence = 1.0 - std::abs(x * 2.0 - 1.0); break;
        case TextProceduralMode::DistanceFromCustomIndex: {
            const double custom = selector.custom_index.evaluate(local_time);
            influence = 1.0 - std::min(1.0, std::abs(static_cast<double>(unit_index) - custom) /
                                      std::max(1.0, count - 1.0)); break;
        }
        }
        const double min_value = selector.minimum.evaluate(local_time) / 100.0;
        const double max_value = selector.maximum.evaluate(local_time) / 100.0;
        influence = min_value + (max_value - min_value) * clamp01(influence);
        influence *= selector.amplitude.evaluate(local_time) / 100.0;
        const double falloff = std::max(0.0, selector.falloff.evaluate(local_time));
        if (falloff != 1.0)
            influence = std::pow(clamp01(influence), falloff);
    } else if (selector.type == TextSelectorType::TextBased) {
        influence = unit_text_matches(selector, layout, unit) ? 1.0 : 0.0;
    } else if (selector.type == TextSelectorType::Wiggly) {
        const double spatial = selector.spatial_phase.evaluate(local_time) +
                               static_cast<double>(unit_index) *
                               (1.0 - clamp01(selector.correlation.evaluate(local_time) / 100.0));
        const double temporal = selector.temporal_phase.evaluate(local_time) +
                                selector.wiggly_frequency.evaluate(local_time) * local_time;
        const double base = selector.per_character_random
            ? random01(static_cast<uint64_t>(selector.wiggly_seed) ^ unit_index ^
                       static_cast<uint64_t>(std::floor(temporal * 997.0)))
            : 0.5 + 0.5 * std::sin((spatial + temporal) * 2.0 * kPi);
        const double minimum = selector.minimum_influence.evaluate(local_time) / 100.0;
        const double maximum = selector.maximum_influence.evaluate(local_time) / 100.0;
        influence = minimum + (maximum - minimum) * base;
        influence *= selector.wiggly_amount.evaluate(local_time) / 100.0;
    }

    if (selector.invert)
        influence = 1.0 - influence;
    return clamp01(influence);
}

namespace {
bool text_animator_unit_bounds(const TextLayoutData &layout,
                               const TextAnimatorUnitMap &map,
                               TextAnimatorUnit unit,
                               size_t cluster_index,
                               bool restrict_to_cluster_line,
                               double &x0, double &y0,
                               double &x1, double &y1)
{
    if (cluster_index >= layout.clusters.size())
        return false;
    const auto &units = map.units(unit);
    const size_t index = unit_index_for_cluster(units, cluster_index);
    if (index >= units.size())
        return false;
    const TextAnimatorUnitRange &range = units[index];
    const std::vector<size_t> cluster_indices =
        unit_cluster_indices(range, layout.clusters.size());
    if (cluster_indices.empty())
        return false;
    const uint32_t target_line = layout.clusters[cluster_index].line_index;
    x0 = y0 = std::numeric_limits<double>::max();
    x1 = y1 = std::numeric_limits<double>::lowest();
    for (size_t i : cluster_indices) {
        const TextLayoutCluster &cluster = layout.clusters[i];
        if (restrict_to_cluster_line && cluster.line_index != target_line)
            continue;
        x0 = std::min(x0, static_cast<double>(cluster.x));
        y0 = std::min(y0, static_cast<double>(cluster.y));
        x1 = std::max(x1, static_cast<double>(cluster.x + cluster.width));
        y1 = std::max(y1, static_cast<double>(cluster.y + cluster.height));
    }
    return x1 > x0 && y1 > y0;
}

bool text_animator_unit_ink_bounds(const TextLayoutData &layout,
                                   const TextAnimatorUnitMap &map,
                                   TextAnimatorUnit unit,
                                   size_t cluster_index,
                                   bool restrict_to_cluster_line,
                                   double &x0, double &y0,
                                   double &x1, double &y1)
{
    if (cluster_index >= layout.clusters.size())
        return false;
    const auto &units = map.units(unit);
    const size_t index = unit_index_for_cluster(units, cluster_index);
    if (index >= units.size())
        return false;
    const TextAnimatorUnitRange &range = units[index];
    const std::vector<size_t> cluster_indices =
        unit_cluster_indices(range, layout.clusters.size());
    const uint32_t target_line = layout.clusters[cluster_index].line_index;
    x0 = y0 = std::numeric_limits<double>::max();
    x1 = y1 = std::numeric_limits<double>::lowest();
    bool found = false;
    for (size_t i : cluster_indices) {
        const TextLayoutCluster &cluster = layout.clusters[i];
        if (restrict_to_cluster_line && cluster.line_index != target_line)
            continue;
        const size_t glyph_begin =
            std::min<size_t>(cluster.glyph_begin, layout.glyphs.size());
        const size_t glyph_end = std::min(
            glyph_begin + cluster.glyph_count, layout.glyphs.size());
        for (size_t glyph_index = glyph_begin;
             glyph_index < glyph_end; ++glyph_index) {
            const TextLayoutGlyph &glyph = layout.glyphs[glyph_index];
            if (glyph.cluster_index != i ||
                glyph.ink_width <= 0.0f || glyph.ink_height <= 0.0f) {
                continue;
            }
            x0 = std::min(x0, static_cast<double>(glyph.ink_x));
            y0 = std::min(y0, static_cast<double>(glyph.ink_y));
            x1 = std::max(
                x1, static_cast<double>(glyph.ink_x + glyph.ink_width));
            y1 = std::max(
                y1, static_cast<double>(glyph.ink_y + glyph.ink_height));
            found = true;
        }
    }
    return found && x1 > x0 && y1 > y0;
}

void extend_clipped_position_to_hide_ink(
    TextAnimatorClusterState &state, const TextAnimator &animator,
    double animator_time, double influence,
    double clip_x0, double clip_y0, double clip_x1, double clip_y1,
    double ink_x0, double ink_y0, double ink_x1, double ink_y1)
{
    if (animator.blend_mode != TextAnimatorBlendMode::Add ||
        influence <= 0.0) {
        return;
    }

    /* Crop-enabled slides must finish completely outside their stationary
     * unit bound. A single authored pixel offset is insufficient when a rich
     * textbox contains runs with different font sizes. Preserve that offset
     * as the minimum travel, and add only the per-unit distance needed to move
     * the actual shaped ink beyond the relevant crop edge. */
    constexpr double edge_guard = 1.0;
    for (const TextAnimatorProperty &property : animator.properties) {
        if (!property.enabled ||
            property.type != TextAnimatorPropertyType::Position) {
            continue;
        }
        const double authored_x = property.value.evaluate(animator_time);
        const double authored_y = property.secondary.evaluate(animator_time);
        if (std::abs(authored_x) > 1.0e-9) {
            const double required = authored_x > 0.0
                ? clip_x1 - ink_x0 + edge_guard
                : ink_x1 - clip_x0 + edge_guard;
            const double extra =
                std::max(0.0, required - std::abs(authored_x));
            state.position_x += std::copysign(extra, authored_x) * influence;
        }
        if (std::abs(authored_y) > 1.0e-9) {
            const double required = authored_y > 0.0
                ? clip_y1 - ink_y0 + edge_guard
                : ink_y1 - clip_y0 + edge_guard;
            const double extra =
                std::max(0.0, required - std::abs(authored_y));
            state.position_y += std::copysign(extra, authored_y) * influence;
        }
    }
}
} // namespace

TextAnimatorEvaluation evaluate_text_animators(const TextAnimatorStack &stack,
                                               const TextLayoutData &layout,
                                               double local_time)
{
    TextAnimatorEvaluation result;
    result.clusters.resize(layout.clusters.size());
    result.signature = text_animator_stack_signature(stack);
    if (layout.clusters.empty() || stack.animators.empty())
        return result;

    const TextAnimatorUnitMap map = build_text_animator_unit_map(layout);
    for (const TextAnimator &animator : stack.animators) {
        if (!animator.enabled)
            continue;
        const double animator_time = local_time - animator.local_time_offset;
        for (size_t cluster_index = 0; cluster_index < layout.clusters.size(); ++cluster_index) {
            double influence = animator.selectors.empty() ? 1.0 : 0.0;
            bool first = true;
            for (const TextSelector &selector : animator.selectors) {
                if (!selector.enabled)
                    continue;
                const double next = evaluate_text_selector_for_cluster(
                    selector, layout, map, cluster_index, animator_time);
                influence = combine_influence(influence, next, selector.combination, first);
                first = false;
            }
            if (first && !animator.selectors.empty())
                influence = 0.0;
            bool directional_reveal = false;
            bool grouped_transform = false;
            for (const TextAnimatorProperty &property : animator.properties) {
                result.layout_affecting = result.layout_affecting ||
                                          text_animator_property_affects_layout(property.type);
                apply_property(result.clusters[cluster_index], property,
                               influence, animator_time, animator.blend_mode);
                directional_reveal = directional_reveal ||
                    (property.enabled &&
                     property.type == TextAnimatorPropertyType::CharacterReveal &&
                     static_cast<int>(std::llround(
                         property.secondary.evaluate(animator_time))) !=
                         static_cast<int>(TextRevealDirection::None));
                grouped_transform = grouped_transform ||
                    (property.enabled && animator.transform_as_unit &&
                     (property.type == TextAnimatorPropertyType::Scale ||
                      property.type == TextAnimatorPropertyType::Rotation ||
                      property.type == TextAnimatorPropertyType::CharacterRotation ||
                      property.type == TextAnimatorPropertyType::Skew));
            }
            if (grouped_transform) {
                TextAnimatorClusterState &state = result.clusters[cluster_index];
                double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
                if (text_animator_unit_bounds(layout, map, animator.granularity,
                                              cluster_index, false,
                                              x0, y0, x1, y1)) {
                    /* Stack order remains deterministic: when multiple grouped
                     * transforms target one cluster, the later animator owns
                     * the common origin used by the flattened compositor. */
                    state.has_transform_origin = true;
                    state.transform_origin_x = (x0 + x1) * 0.5;
                    state.transform_origin_y = (y0 + y1) * 0.5;
                }
            }
            if (animator.clip_to_unit_bounds) {
                TextAnimatorClusterState &state = result.clusters[cluster_index];
                double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
                if (text_animator_unit_bounds(layout, map, animator.granularity,
                                              cluster_index, true,
                                              x0, y0, x1, y1)) {
                    state.has_unit_clip_bounds = true;
                    state.unit_clip_x0 = x0;
                    state.unit_clip_y0 = y0;
                    state.unit_clip_x1 = x1;
                    state.unit_clip_y1 = y1;
                    double ink_x0 = x0, ink_y0 = y0;
                    double ink_x1 = x1, ink_y1 = y1;
                    double shaped_x0 = 0.0, shaped_y0 = 0.0;
                    double shaped_x1 = 0.0, shaped_y1 = 0.0;
                    if (text_animator_unit_ink_bounds(
                            layout, map, animator.granularity, cluster_index,
                            true, shaped_x0, shaped_y0,
                            shaped_x1, shaped_y1)) {
                        ink_x0 = shaped_x0;
                        ink_y0 = shaped_y0;
                        ink_x1 = shaped_x1;
                        ink_y1 = shaped_y1;
                    }
                    extend_clipped_position_to_hide_ink(
                        state, animator, animator_time, influence,
                        x0, y0, x1, y1,
                        ink_x0, ink_y0, ink_x1, ink_y1);
                }
            }
            if (directional_reveal) {
                TextAnimatorClusterState &state = result.clusters[cluster_index];
                double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
                if (text_animator_unit_bounds(layout, map, animator.granularity,
                                              cluster_index, false,
                                              x0, y0, x1, y1)) {
                    state.has_reveal_bounds = true;
                    state.reveal_x0 = x0; state.reveal_y0 = y0;
                    state.reveal_x1 = x1; state.reveal_y1 = y1;
                }
            }
        }
    }

    /* Tracking is a layout-affecting property, but it can be composed without
     * reshaping when glyph choice and line breaks are unchanged. Accumulate the
     * evaluated per-cluster spacing inside each shaped line. */
    for (const TextLayoutLine &line : layout.lines) {
        double accumulated_tracking = 0.0;
        const size_t begin = std::min<size_t>(line.cluster_begin,
                                              result.clusters.size());
        const size_t end = std::min<size_t>(
            begin + line.cluster_count, result.clusters.size());
        for (size_t cluster_index = begin; cluster_index < end; ++cluster_index) {
            result.clusters[cluster_index].position_x += accumulated_tracking;
            accumulated_tracking += result.clusters[cluster_index].tracking;
        }
    }
    return result;
}

bool text_animator_property_affects_layout(TextAnimatorPropertyType type)
{
    switch (type) {
    case TextAnimatorPropertyType::FontSize:
    case TextAnimatorPropertyType::HorizontalScale:
    case TextAnimatorPropertyType::VerticalScale:
    case TextAnimatorPropertyType::Tracking:
    case TextAnimatorPropertyType::BaselineShift:
    case TextAnimatorPropertyType::LineSpacing:
    case TextAnimatorPropertyType::SpaceBeforeParagraph:
    case TextAnimatorPropertyType::SpaceAfterParagraph:
    case TextAnimatorPropertyType::WordSpacing:
    case TextAnimatorPropertyType::TextBoxPositionOffset:
    case TextAnimatorPropertyType::TextBoxScale:
    case TextAnimatorPropertyType::LineAlignmentOffset:
        return true;
    default:
        return false;
    }
}

bool text_animator_stack_has_enabled_animators(const TextAnimatorStack &stack)
{
    return std::any_of(stack.animators.begin(), stack.animators.end(),
                       [](const TextAnimator &animator) {
                           return animator.enabled && !animator.properties.empty();
                       });
}

namespace {
bool animated_property_is_time_dependent(const AnimatedProperty &property)
{
    return !property.keyframes.empty();
}

double animated_property_max_abs(const AnimatedProperty &property)
{
    double result = std::abs(property.static_value);
    for (const Keyframe &keyframe : property.keyframes)
        result = std::max(result, std::abs(keyframe.value));
    return std::isfinite(result) ? result : 0.0;
}

double animated_property_max_value(const AnimatedProperty &property,
                                   double fallback)
{
    double result = std::isfinite(property.static_value)
        ? property.static_value : fallback;
    for (const Keyframe &keyframe : property.keyframes) {
        if (std::isfinite(keyframe.value))
            result = std::max(result, keyframe.value);
    }
    return result;
}
}

bool text_animator_stack_is_time_dependent(const TextAnimatorStack &stack)
{
    for (const TextAnimator &animator : stack.animators) {
        if (!animator.enabled)
            continue;
        for (const TextAnimatorProperty &property : animator.properties) {
            if (!property.enabled)
                continue;
            if (animated_property_is_time_dependent(property.value) ||
                animated_property_is_time_dependent(property.secondary) ||
                animated_property_is_time_dependent(property.tertiary) ||
                animated_property_is_time_dependent(property.quaternary))
                return true;
        }
        for (const TextSelector &selector : animator.selectors) {
            if (!selector.enabled)
                continue;
            const AnimatedProperty *properties[] = {
                &selector.start, &selector.end, &selector.offset, &selector.amount,
                &selector.ease_high, &selector.ease_low, &selector.smoothness,
                &selector.completion, &selector.stagger_percent,
                &selector.amplitude, &selector.frequency, &selector.phase,
                &selector.speed, &selector.falloff, &selector.minimum,
                &selector.maximum, &selector.custom_index,
                &selector.wiggly_amount, &selector.wiggly_frequency,
                &selector.correlation, &selector.temporal_phase,
                &selector.spatial_phase, &selector.minimum_influence,
                &selector.maximum_influence};
            for (const AnimatedProperty *property : properties) {
                if (property && animated_property_is_time_dependent(*property))
                    return true;
            }
            if (selector.type == TextSelectorType::Procedural &&
                std::abs(selector.speed.static_value) > 1e-12)
                return true;
            if (selector.type == TextSelectorType::Wiggly &&
                std::abs(selector.wiggly_frequency.static_value) > 1e-12)
                return true;
        }
    }
    return false;
}

double text_animator_stack_visual_padding(const TextAnimatorStack &stack,
                                          double box_width,
                                          double box_height)
{
    double translation = 0.0;
    double blur = 0.0;
    double maximum_scale = 1.0;
    bool rotates = false;
    for (const TextAnimator &animator : stack.animators) {
        if (!animator.enabled)
            continue;
        for (const TextAnimatorProperty &property : animator.properties) {
            if (!property.enabled)
                continue;
            switch (property.type) {
            case TextAnimatorPropertyType::Position:
            case TextAnimatorPropertyType::AnchorPoint:
            case TextAnimatorPropertyType::ShadowOffset:
                translation = std::max(
                    translation,
                    std::hypot(animated_property_max_abs(property.value),
                               animated_property_max_abs(property.secondary)));
                break;
            case TextAnimatorPropertyType::Scale:
                maximum_scale = std::max(
                    maximum_scale,
                    std::max(animated_property_max_value(property.value, 1.0),
                             animated_property_max_value(property.secondary, 1.0)));
                break;
            case TextAnimatorPropertyType::HorizontalScale:
            case TextAnimatorPropertyType::VerticalScale:
                maximum_scale = std::max(
                    maximum_scale,
                    animated_property_max_value(property.value, 1.0));
                break;
            case TextAnimatorPropertyType::Rotation:
            case TextAnimatorPropertyType::CharacterRotation:
            case TextAnimatorPropertyType::Skew:
                rotates = rotates || animated_property_max_abs(property.value) > 1e-9;
                break;
            case TextAnimatorPropertyType::Blur:
            case TextAnimatorPropertyType::GlowIntensity:
                blur = std::max(blur, animated_property_max_abs(property.value));
                break;
            default:
                break;
            }
        }
    }
    const double half_diagonal = 0.5 * std::hypot(
        std::max(0.0, box_width), std::max(0.0, box_height));
    const double scale_padding = std::max(0.0, maximum_scale - 1.0) * half_diagonal;
    const double rotation_padding = rotates ? half_diagonal : 0.0;
    return std::ceil(std::max(0.0, translation + scale_padding +
                                   rotation_padding + blur * 2.0 + 2.0));
}

uint64_t text_animator_stack_signature(const TextAnimatorStack &stack)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = hash_value(hash, stack.schema_version);
    hash = hash_value(hash, stack.legacy_migration_version);
    for (const auto &animator : stack.animators) {
        hash = hash_string(hash, animator.id); hash = hash_string(hash, animator.name);
        hash = hash_value(hash, animator.enabled); hash = hash_value(hash, animator.blend_mode);
        hash = hash_value(hash, animator.granularity);
        hash = hash_value(hash, animator.transform_as_unit);
        hash = hash_value(hash, animator.clip_to_unit_bounds);
        hash = hash_value(hash, animator.change_behaviour);
        hash = hash_value(hash, animator.playback_role); hash = hash_value(hash, animator.local_time_offset);
        hash = hash_string(hash, animator.preset_id); hash = hash_value(hash, animator.preset_schema_version);
        hash = hash_value(hash, animator.transition_managed);
        hash = hash_string(hash, animator.transition_id);
        hash = hash_value(hash, animator.transition_binding_signature);
        for (const auto &property : animator.properties) {
            hash = hash_string(hash, property.id); hash = hash_string(hash, property.name);
            hash = hash_value(hash, property.type); hash = hash_value(hash, property.enabled);
            add_animated_property_hash(hash, property.value);
            add_animated_property_hash(hash, property.secondary);
            add_animated_property_hash(hash, property.tertiary);
            add_animated_property_hash(hash, property.quaternary);
        }
        for (const auto &selector : animator.selectors) {
            hash = hash_string(hash, selector.id); hash = hash_string(hash, selector.name);
            hash = hash_value(hash, selector.type); hash = hash_value(hash, selector.combination);
            hash = hash_value(hash, selector.based_on); hash = hash_value(hash, selector.enabled);
            hash = hash_value(hash, selector.range_units); hash = hash_value(hash, selector.range_shape);
            add_animated_property_hash(hash, selector.start); add_animated_property_hash(hash, selector.end);
            add_animated_property_hash(hash, selector.offset); add_animated_property_hash(hash, selector.amount);
            add_animated_property_hash(hash, selector.ease_high); add_animated_property_hash(hash, selector.ease_low);
            add_animated_property_hash(hash, selector.smoothness);
            add_animated_property_hash(hash, selector.completion);
            add_animated_property_hash(hash, selector.stagger_percent);
            hash = hash_value(hash, selector.unit_easing);
            hash = hash_value(hash, selector.stagger_mode);
            hash = hash_value(hash, selector.exclude_whitespace);
            hash = hash_value(hash, selector.randomize_order); hash = hash_value(hash, selector.random_seed);
            hash = hash_value(hash, selector.invert); hash = hash_value(hash, selector.procedural_mode);
            add_animated_property_hash(hash, selector.amplitude); add_animated_property_hash(hash, selector.frequency);
            add_animated_property_hash(hash, selector.phase); add_animated_property_hash(hash, selector.speed);
            add_animated_property_hash(hash, selector.falloff); add_animated_property_hash(hash, selector.minimum);
            add_animated_property_hash(hash, selector.maximum); add_animated_property_hash(hash, selector.custom_index);
            hash = hash_value(hash, selector.direction); hash = hash_value(hash, selector.match_mode);
            hash = hash_value(hash, selector.range_start); hash = hash_value(hash, selector.range_end);
            hash = hash_string(hash, selector.match_text); hash = hash_string(hash, selector.regular_expression);
            hash = hash_value(hash, selector.case_sensitive); hash = hash_value(hash, selector.rich_text_run_index);
            for (const auto &range : selector.tagged_byte_ranges) {
                hash = hash_value(hash, range.first); hash = hash_value(hash, range.second);
            }
            add_animated_property_hash(hash, selector.wiggly_amount);
            add_animated_property_hash(hash, selector.wiggly_frequency);
            add_animated_property_hash(hash, selector.correlation);
            add_animated_property_hash(hash, selector.temporal_phase);
            add_animated_property_hash(hash, selector.spatial_phase);
            add_animated_property_hash(hash, selector.minimum_influence);
            add_animated_property_hash(hash, selector.maximum_influence);
            hash = hash_value(hash, selector.wiggly_seed);
            hash = hash_value(hash, selector.lock_dimensions);
            hash = hash_value(hash, selector.per_character_random);
        }
    }
    return hash;
}

std::string make_text_animator_id(const std::string &prefix,
                                  const std::string &seed,
                                  size_t ordinal)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = hash_string(hash, prefix);
    hash = hash_string(hash, seed);
    hash = hash_value(hash, ordinal);
    std::ostringstream out;
    out << prefix << '-' << std::hex << hash;
    return out.str();
}
