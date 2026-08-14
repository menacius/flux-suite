#!/usr/bin/env python3
"""Source contract for Development Version 217."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
schema = read("Core/title-serialization-schema.h")
graph = read("src/timeline/temporal-graph-editor.inc")
commands = read("src/editor/title-editor/commands-docks.inc")
canvas_h = read("src/canvas/canvas-preview.h")
gizmo = read("src/canvas/canvas-preview/editor-3d-tools.inc")
mouse = read("src/canvas/canvas-preview/gpu-frame-rendering.inc")
overlay = read("src/canvas/canvas-preview/transform-snap.inc")
cache_h = read("src/cache/cache-manager.h")
cache_state = read("src/cache/cache-state-tracker.cpp")
cache_policy = read("src/cache/cache-manager/cache-policy-invalidation.inc")
timeline = read("src/timeline/timeline-widget.cpp")
perf = read("../../packages/flux-common/Shared/performance-counters.h")
queue = read("src/cache/render-queue-manager.cpp")
worker = read("src/cache/cache-manager/worker-publication.inc")
notifications = read("src/cache/cache-manager/disk-cache-storage.inc")
readme = read("README.md")
changelog = read("docs/CHANGELOG.md")
doc = read("docs/RENDERING_AND_CACHE.md")

assert 'set(OBS_FXM_DEVELOPMENT_VERSION "243")' in cmake
assert '#define FXM_DEVELOPMENT_VERSION "243"' in build
assert 'kCurrentDevelopmentVersion = 243' in schema
assert 'case 217:' in schema

# Graph toggle color is authored from the exact same component palette as curves.
assert 'QColor TimelineWidget::graph_channel_color(int component) const' in graph
for token in ('QStringLiteral("A")', 'QStringLiteral("R")',
              'QStringLiteral("G")', 'QStringLiteral("B")'):
    assert token in graph
assert 'return graph_channel_color(authored_component);' in graph
assert 'auto apply_graph_channel_color' in commands
assert 'QPushButton:checked{background:%1;color:%2;border-color:%1;}' in commands
assert commands.count('apply_graph_channel_color(') >= 8

# Hover identifies and highlights the exact gizmo target without rebuilding geometry twice.
assert 'GizmoAxis gizmo_hover_axis_ = GizmoAxis::None;' in canvas_h
assert 'mutable GizmoGeometry gizmo_geometry_cache_' in canvas_h
assert 'void CanvasPreview::update_3d_gizmo_hover' in gizmo
assert 'next = hit_test_3d_gizmo(view_point);' in gizmo
assert 'base_color.lighter(165)' in gizmo
assert 'fill.lighter(190)' in gizmo
assert 'update_3d_gizmo_hover(ev->position())' in mouse
assert 'gizmo_hover_axis_ = GizmoAxis::None;' in mouse
assert 'gizmo_geometry_cache_valid_ = false;' in overlay

# Timeline cache visualization uses indexed/batched reads instead of one global scan per frame.
assert 'QHash<QString, QHash<int, FrameCacheState>> frame_states_' in cache_h
assert 'rebuildTitleIndexLocked' in cache_h
assert 'return title_it.value().value(frame, FrameCacheState::NotCached);' in cache_state
state_for_frame = cache_state[cache_state.index('FrameCacheState CacheStateTracker::stateForFrame'):
                              cache_state.index('void CacheStateTracker::setState')]
assert 'for (' not in state_for_frame
assert 'statesForRange' in cache_h
assert 'CacheStateTracker::statesForRange' in cache_state
assert 'displayStatesForRange' in cache_h
assert 'displayStaticFramesForRange' in cache_h
assert 'state_tracker_.statesForRange(' in cache_policy
assert 'const QString content_hash = contentHash(*title);' in cache_policy
assert 'QHash<int, QString> visual_hashes;' in cache_policy
assert 'displayStatesForRange(' in timeline
assert 'displayStaticFramesForRange(' in timeline

# New debug diagnostics cover actual accepted work and bounded/coalesced UI publication.
for counter in (
    'CacheStateFrameLookups', 'CacheStateIndexRebuilds',
    'TimelineCacheFramesInspected', 'CacheRenderJobs',
    'CacheRenderNanoseconds', 'GpuReadbackJobs', 'GpuReadbackNanoseconds',
    'UiNotificationFlushes', 'UiNotificationsCoalesced', 'RenderQueuePeak',
    'GizmoGeometryCacheHits', 'GizmoGeometryCacheMisses',
    'BackgroundJobsActive'):
    assert counter in perf, counter
assert 'inline void set_max' in perf
assert 'Counter::RenderQueuePeak' in queue
assert 'Counter::CacheRenderNanoseconds' in worker
assert 'Counter::GpuReadbackNanoseconds' in worker
assert 'Counter::UiNotificationsCoalesced' in notifications
assert 'Counter::UiNotificationFlushes' in notifications

assert 'indexed and batched state reads' in readme
assert changelog.startswith('# v0.8.11-alpha — Development Version 243')
assert 'Timeline/cache paint audit' in doc
assert 'Threading and lifetime contract' in doc

print('Development Version 217 performance/cache/threading contract passed')
