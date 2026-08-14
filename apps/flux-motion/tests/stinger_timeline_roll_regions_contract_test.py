from pathlib import Path

root = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (root / path).read_text(encoding="utf-8")

header = read("src/timeline/timeline-widget.h")
timeline = read("src/timeline/timeline-widget.cpp")
graph = read("src/timeline/temporal-graph-editor.inc")
locale = read("data/locale/en-US.ini")
cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")

for token in (
    "timeline_pre_roll() const",
    "timeline_post_roll() const",
    "timeline_display_duration() const",
    "x_to_display_time(int x) const",
    "display_time_to_x(double t) const",
):
    assert token in header, token

for token in (
    "title_->stinger_pre_roll",
    "title_->stinger_post_roll",
    "timeline_pre_roll() + document_duration + timeline_post_roll()",
    "x_to_display_time(x) - timeline_pre_roll()",
    "display_time_to_x(t + timeline_pre_roll())",
    "Qt::BDiagPattern",
    "Qt::FDiagPattern",
    'fxm_tr("OBSTitles.StingerPreRoll")',
    'fxm_tr("OBSTitles.StingerPostRoll")',
    "animation_start_x",
    "animation_end_x",
    "first_relative_frame",
    "ruler_time_text",
):
    assert token in timeline, token

# Layer/keyframe data remains in document time and is only visually offset by time_to_x().
assert "time_to_x(layer->in_time)" in timeline
assert "time_to_x(layer->out_time)" in timeline
assert "time_to_x(layer->in_time + prop.keyframe_time" in timeline
assert "time_to_x(stinger_transition_point_seconds(*title_))" in timeline

# Graph Editor intentionally excludes roll regions and preserves document-zero position.
assert "graph_editor_enabled_" in timeline
assert "const double old_offset = timeline_pre_roll();" in graph
assert "const double new_offset = timeline_pre_roll();" in graph

assert "OBSTitles.StingerPreRoll=" in locale
assert "OBSTitles.StingerPostRoll=" in locale
assert 'OBS_FXM_DEVELOPMENT_VERSION "243"' in cmake
assert 'FXM_DEVELOPMENT_VERSION "243"' in build_info

print("Stinger timeline roll-regions contract passed")
