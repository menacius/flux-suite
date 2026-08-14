#pragma once

#include "animation.h"
#include "title-text-layout.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/* Unified, renderer-neutral text animation model.
 *
 * The model is intentionally independent from Qt/OBS. It consumes the immutable
 * shaped TextLayoutData used by both editor and source rendering and emits one
 * deterministic visual state per shaped cluster. Render backends may fan the
 * cluster state out to glyph quads without reshaping or splitting UTF code units.
 */

inline constexpr int kTextAnimatorSchemaVersion = 2;
inline constexpr int kTextAnimatorLegacyMigrationVersion = 2;

enum class TextAnimatorUnit {
    Grapheme = 0,
    Character = 1,
    CharacterExcludingSpaces = 2,
    Word = 3,
    Line = 4,
    Paragraph = 5,
    RichTextRun = 6,
    WholeLayer = 7,
    /* Appended to preserve the serialized values of the original units. */
    Sentence = 8,
};

enum class TextAnimatorPropertyType {
    Position = 0,
    AnchorPoint,
    Scale,
    Rotation,
    CharacterRotation,
    Skew,
    SkewAxis,
    Opacity,
    FontSize,
    HorizontalScale,
    VerticalScale,
    Tracking,
    BaselineShift,
    FillColor,
    FillOpacity,
    StrokeColor,
    StrokeWidth,
    StrokeOpacity,
    LineSpacing,
    SpaceBeforeParagraph,
    SpaceAfterParagraph,
    WordSpacing,
    TextBoxPositionOffset,
    TextBoxScale,
    LineAlignmentOffset,
    Blur,
    GlowIntensity,
    ShadowOffset,
    ShadowOpacity,
    CharacterVisibility,
    CharacterReveal,
    CharacterReplacement,
    ScrambleAmount,
};

enum class TextAnimatorBlendMode {
    Add = 0,
    Replace,
    Multiply,
};

enum class TextSelectorType {
    Range = 0,
    Procedural,
    TextBased,
    Wiggly,
    /* Exact legacy-compatible per-unit transition timing, implemented as a
     * generic selector rather than a preset-specific renderer. */
    Staggered,
};

enum class TextSelectorCombinationMode {
    Add = 0,
    Subtract,
    Intersect,
    Difference,
    Minimum,
    Maximum,
    Multiply,
};

enum class TextRangeUnits {
    Percentage = 0,
    Index,
};

enum class TextRangeShape {
    Square = 0,
    RampUp,
    RampDown,
    Triangle,
    Round,
    Smooth,
};

enum class TextProceduralMode {
    Random = 0,
    Noise,
    Wave,
    Sine,
    Sawtooth,
    Pulse,
    Alternating,
    DistanceFromStart,
    DistanceFromEnd,
    DistanceFromCentre,
    DistanceFromCustomIndex,
};

enum class TextSelectorDirection {
    Forward = 0,
    Reverse,
    Bidirectional,
};

enum class TextStaggerMode {
    Entrance = 0,
    Exit,
};

enum class TextRevealDirection {
    None = 0,
    Left,
    Right,
    Up,
    Down,
};

enum class TextMatchMode {
    CharacterRange = 0,
    WordRange,
    LineRange,
    ParagraphRange,
    ExactText,
    RegularExpression,
    Whitespace,
    Numbers,
    Uppercase,
    Lowercase,
    Punctuation,
    RichTextStyleRun,
    ExternalDataRange,
    NewlyAddedText,
    ChangedText,
};

enum class TextChangeBehaviour {
    Restart = 0,
    ContinueLocalTime,
    PreserveCompletion,
    AnimateNewOnly,
    AnimateRemovedOnly,
    AnimateChangedOnly,
    ReevaluateFullText,
};

enum class TextAnimatorPlaybackRole {
    General = 0,
    Entrance,
    Exit,
    Continuous,
};

struct TextAnimatorProperty {
    std::string id;
    std::string name;
    TextAnimatorPropertyType type = TextAnimatorPropertyType::Opacity;
    bool enabled = true;

    /* Scalar properties use value. 2D properties use value + secondary.
     * Color properties use value=A, secondary=R, tertiary=G, quaternary=B.
     * This keeps every component in the existing keyframe system. */
    AnimatedProperty value {"value", 0.0};
    AnimatedProperty secondary {"secondary", 0.0};
    AnimatedProperty tertiary {"tertiary", 0.0};
    AnimatedProperty quaternary {"quaternary", 0.0};
};

struct TextSelector {
    std::string id;
    std::string name;
    TextSelectorType type = TextSelectorType::Range;
    TextSelectorCombinationMode combination = TextSelectorCombinationMode::Intersect;
    TextAnimatorUnit based_on = TextAnimatorUnit::Grapheme;
    bool enabled = true;
    bool expanded = true;

    /* Range selector. */
    TextRangeUnits range_units = TextRangeUnits::Percentage;
    TextRangeShape range_shape = TextRangeShape::Square;
    AnimatedProperty start {"start", 0.0};
    AnimatedProperty end {"end", 100.0};
    AnimatedProperty offset {"offset", 0.0};
    AnimatedProperty amount {"amount", 100.0};
    AnimatedProperty ease_high {"ease_high", 0.0};
    AnimatedProperty ease_low {"ease_low", 0.0};
    AnimatedProperty smoothness {"smoothness", 100.0};
    bool randomize_order = false;
    int random_seed = 1;
    bool invert = false;

    /* Staggered selector. The completion track is layer-local and uses the
     * existing FXM keyframe system. unit_easing reproduces the historical
     * second easing pass applied independently to every selected unit. */
    AnimatedProperty completion {"completion", 0.0};
    AnimatedProperty stagger_percent {"stagger_percent", 35.0};
    EasingType unit_easing = EasingType::EaseInOut;
    TextStaggerMode stagger_mode = TextStaggerMode::Entrance;
    bool exclude_whitespace = true;

    /* Procedural selector. */
    TextProceduralMode procedural_mode = TextProceduralMode::Random;
    AnimatedProperty amplitude {"amplitude", 100.0};
    AnimatedProperty frequency {"frequency", 1.0};
    AnimatedProperty phase {"phase", 0.0};
    AnimatedProperty speed {"speed", 0.0};
    AnimatedProperty falloff {"falloff", 1.0};
    AnimatedProperty minimum {"minimum", 0.0};
    AnimatedProperty maximum {"maximum", 100.0};
    AnimatedProperty custom_index {"custom_index", 0.0};
    TextSelectorDirection direction = TextSelectorDirection::Forward;

    /* Text-based selector. Byte ranges always refer to canonical UTF-8 source
     * boundaries. Layout clusters remain the authority for visible units. */
    TextMatchMode match_mode = TextMatchMode::ExactText;
    size_t range_start = 0;
    size_t range_end = 0;
    std::string match_text;
    std::string regular_expression;
    bool case_sensitive = true;
    int rich_text_run_index = -1;
    std::vector<std::pair<size_t, size_t>> tagged_byte_ranges;

    /* Wiggly selector. */
    AnimatedProperty wiggly_amount {"wiggly_amount", 100.0};
    AnimatedProperty wiggly_frequency {"wiggly_frequency", 1.0};
    AnimatedProperty correlation {"correlation", 50.0};
    AnimatedProperty temporal_phase {"temporal_phase", 0.0};
    AnimatedProperty spatial_phase {"spatial_phase", 0.0};
    AnimatedProperty minimum_influence {"minimum_influence", 0.0};
    AnimatedProperty maximum_influence {"maximum_influence", 100.0};
    int wiggly_seed = 1;
    bool lock_dimensions = true;
    bool per_character_random = true;
};

struct TextAnimator {
    std::string id;
    std::string name = "Animator";
    bool enabled = true;
    bool expanded = true;
    TextAnimatorBlendMode blend_mode = TextAnimatorBlendMode::Add;
    TextAnimatorUnit granularity = TextAnimatorUnit::Grapheme;
    /* Apply geometric properties around the bounds of the selected unit
     * instead of each glyph's own centre. This is generic animator behaviour
     * and preserves historical word/sentence transition transforms. */
    bool transform_as_unit = false;
    /* Clip before applying the animator transform, so a sliding glyph/word/
     * sentence enters through its stationary authored unit bounds. */
    bool clip_to_unit_bounds = false;
    TextChangeBehaviour change_behaviour = TextChangeBehaviour::ReevaluateFullText;
    TextAnimatorPlaybackRole playback_role = TextAnimatorPlaybackRole::General;
    double local_time_offset = 0.0;
    std::string preset_id;
    int preset_schema_version = 0;
    /* Transition descriptors remain authoring/timeline metadata. A managed
     * animator is the sole runtime implementation and is rebound by id when
     * the transition editor or timeline changes its duration/settings. */
    bool transition_managed = false;
    std::string transition_id;
    /* Hash of the transition descriptor and layer timing from which this
     * managed animator was last synchronized. Manual animator edits do not
     * change it, so unrelated refreshes cannot overwrite editable data. */
    uint64_t transition_binding_signature = 0;
    std::vector<TextAnimatorProperty> properties;
    std::vector<TextSelector> selectors;
};

struct TextAnimatorStack {
    int schema_version = kTextAnimatorSchemaVersion;
    int legacy_migration_version = 0;
    std::vector<TextAnimator> animators;
};

struct TextAnimatorUnitRange {
    size_t byte_start = 0;
    size_t byte_length = 0;
    size_t cluster_begin = 0;
    size_t cluster_count = 0;
    size_t source_index = 0;
    bool whitespace = false;
    /* Shaping backends may emit rich-text/font runs in an order that is not
     * logical text order. Explicit membership keeps one animator unit correct
     * even when its clusters are not contiguous in the layout vector. */
    std::vector<size_t> cluster_indices;
};

struct TextAnimatorUnitMap {
    std::vector<TextAnimatorUnitRange> graphemes;
    std::vector<TextAnimatorUnitRange> characters_excluding_spaces;
    std::vector<TextAnimatorUnitRange> words;
    std::vector<TextAnimatorUnitRange> sentences;
    std::vector<TextAnimatorUnitRange> lines;
    std::vector<TextAnimatorUnitRange> paragraphs;
    std::vector<TextAnimatorUnitRange> rich_text_runs;
    std::vector<TextAnimatorUnitRange> whole_layer;

    const std::vector<TextAnimatorUnitRange> &units(TextAnimatorUnit unit) const;
};

struct TextAnimatorClusterState {
    double position_x = 0.0;
    double position_y = 0.0;
    double anchor_x = 0.0;
    double anchor_y = 0.0;
    bool has_transform_origin = false;
    double transform_origin_x = 0.0;
    double transform_origin_y = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0;
    double character_rotation = 0.0;
    double skew = 0.0;
    double skew_axis = 0.0;
    double opacity = 1.0;
    double blur = 0.0;
    double glow_intensity = 0.0;
    double shadow_offset_x = 0.0;
    double shadow_offset_y = 0.0;
    double shadow_opacity = 1.0;
    double visibility = 1.0;
    double reveal = 1.0;
    TextRevealDirection reveal_direction = TextRevealDirection::None;
    bool has_reveal_bounds = false;
    double reveal_x0 = 0.0;
    double reveal_y0 = 0.0;
    double reveal_x1 = 0.0;
    double reveal_y1 = 0.0;
    bool has_unit_clip_bounds = false;
    double unit_clip_x0 = 0.0;
    double unit_clip_y0 = 0.0;
    double unit_clip_x1 = 0.0;
    double unit_clip_y1 = 0.0;
    double replacement = 0.0;
    double scramble = 0.0;
    double tracking = 0.0;
    double baseline_shift = 0.0;
    double font_size_delta = 0.0;
    double horizontal_scale = 1.0;
    double vertical_scale = 1.0;
    double fill_opacity = 1.0;
    double stroke_width_delta = 0.0;
    double stroke_opacity = 1.0;
    bool has_fill_color = false;
    uint32_t fill_color = 0xFFFFFFFFu;
    double fill_color_mix = 0.0;
    bool has_stroke_color = false;
    uint32_t stroke_color = 0xFFFFFFFFu;
    double stroke_color_mix = 0.0;
};

struct TextAnimatorEvaluation {
    std::vector<TextAnimatorClusterState> clusters;
    bool deterministic = true;
    bool layout_affecting = false;
    uint64_t signature = 0;
};

/* Content-change mappings are expressed in shaped-cluster space and exposed as
 * canonical UTF-8 byte ranges. Runtime code never carries glyph indices from
 * an old layout into a new layout. */
struct TextAnimatorContentChange {
    std::vector<int64_t> previous_to_current;
    std::vector<int64_t> current_to_previous;
    std::vector<std::pair<size_t, size_t>> added_byte_ranges;
    std::vector<std::pair<size_t, size_t>> changed_byte_ranges;
    std::vector<std::pair<size_t, size_t>> removed_byte_ranges;
    bool identical = true;
};

TextAnimatorContentChange map_text_animator_content_change(
    const TextLayoutData &previous, const TextLayoutData &current);

/* Produces a transient evaluation stack for a concrete content revision. The
 * serialized stack remains untouched; tagged selectors and restart offsets are
 * derived deterministically from the old/new shaped layouts. */
TextAnimatorStack text_animator_stack_for_content_change(
    const TextAnimatorStack &stack,
    const TextAnimatorContentChange &change,
    double content_change_local_time);

TextAnimatorUnitMap build_text_animator_unit_map(const TextLayoutData &layout);

double evaluate_text_selector_for_cluster(const TextSelector &selector,
                                          const TextLayoutData &layout,
                                          const TextAnimatorUnitMap &map,
                                          size_t cluster_index,
                                          double local_time);

TextAnimatorEvaluation evaluate_text_animators(const TextAnimatorStack &stack,
                                               const TextLayoutData &layout,
                                               double local_time);

bool text_animator_property_affects_layout(TextAnimatorPropertyType type);
bool text_animator_stack_has_enabled_animators(const TextAnimatorStack &stack);
bool text_animator_stack_is_time_dependent(const TextAnimatorStack &stack);
double text_animator_stack_visual_padding(const TextAnimatorStack &stack,
                                          double box_width,
                                          double box_height);
uint64_t text_animator_stack_signature(const TextAnimatorStack &stack);

/* Stable ids are generated without Qt so project migration and unit tests use
 * the same deterministic identifier policy. */
std::string make_text_animator_id(const std::string &prefix,
                                  const std::string &seed,
                                  size_t ordinal);
