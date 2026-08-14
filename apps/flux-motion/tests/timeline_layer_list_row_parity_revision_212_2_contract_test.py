from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
hierarchy = (ROOT / "src/editor/title-editor-internal/hierarchy-model.inc").read_text(encoding="utf-8")
layer_cpp = (ROOT / "src/layers/layer-stack-widget.cpp").read_text(encoding="utf-8")
layer_h = (ROOT / "src/layers/layer-stack-widget.h").read_text(encoding="utf-8")
timeline = (ROOT / "src/timeline/timeline-widget.cpp").read_text(encoding="utf-8")
title_h = (ROOT / "Core/title-data.h").read_text(encoding="utf-8")
title_cpp = (ROOT / "src/core/title-data.cpp").read_text(encoding="utf-8")
commands = (ROOT / "src/editor/title-editor/commands-docks.inc").read_text(encoding="utf-8")

for token in [
    "is_property_channel",
    "property_channel = -1",
    "append_timeline_property_rows",
    "property.graph_channel(channel)",
    "timeline_property_channels_expanded",
]:
    assert token in hierarchy, token

assert "const auto shared_timeline_rows = timeline_rows(title_);" in layer_cpp
assert "timeline_row.is_property_channel" in layer_cpp
assert "Q_ASSERT(list_->count() == static_cast<int>(shared_timeline_rows.size()))" in layer_cpp
assert "property_channels_expanded_changed" in layer_h
assert "&LayerStack::property_channels_expanded_changed" in commands
assert "expanded_property_channels" in title_h
assert 'jt["expanded_property_channels"]' in title_cpp
assert 'jt.contains("expanded_property_channels")' in title_cpp

# Clicking either an aggregate row or a channel row selects the matching graph target.
assert "rows[row].is_property_channel" in timeline
assert "graph_mode_for_component(rows[row].property_channel)" in timeline
assert "set_graph_channel_mode(graph_channel);" in timeline

# The removed widget-local expansion set is the old source of row-count drift.
assert "expanded_property_channels_" not in layer_cpp
assert "expanded_property_channels_" not in layer_h

print("timeline/layer-list shared row parity contract passed")
