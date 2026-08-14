from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
layer_cpp = (ROOT / "src/layers/layer-stack-widget.cpp").read_text(encoding="utf-8")
layer_h = (ROOT / "src/layers/layer-stack-widget.h").read_text(encoding="utf-8")
hierarchy = (ROOT / "src/editor/title-editor-internal/hierarchy-model.inc").read_text(encoding="utf-8")
graph = (ROOT / "src/timeline/temporal-graph-editor.inc").read_text(encoding="utf-8")
timeline_h = (ROOT / "src/timeline/timeline-widget.h").read_text(encoding="utf-8")
commands = (ROOT / "src/editor/title-editor/commands-docks.inc").read_text(encoding="utf-8")
title_h = (ROOT / "Core/title-data.h").read_text(encoding="utf-8")

required_layer_tokens = [
    'QStringLiteral("property_channel")',
    'QStringLiteral("propertyChannelCaret")',
    'QStringLiteral("keyframeValueSummary")',
    'property_graph_target_requested',
    'property_channel_value_changed',
    'QStringLiteral("All")',
    'timeline_property_channel_label',
]
for token in required_layer_tokens:
    assert token in layer_cpp or token in layer_h, token

assert "expanded_property_channels" in title_h

assert 'if (vector3 || vector) return 3;' in hierarchy
assert 'int vector_component = -1;' in hierarchy
assert 'result.vector_component = component;' in hierarchy
assert 'else v.z = value;' in hierarchy

for token in [
    'GraphChannelMode { X = 0, Y = 1, Z = 2, All = 3, Fourth = 4 }',
    'select_graph_property',
    'set_graph_channel_mode',
    'active_graph_channels',
]:
    assert token in timeline_h or token in graph, token

assert '&LayerStack::property_graph_target_requested' in commands
assert '&LayerStack::property_channel_value_changed' in commands
assert 'next.z = value;' in commands

print("layer-list Vector3 disclosure and Graph Editor channel contract passed")
