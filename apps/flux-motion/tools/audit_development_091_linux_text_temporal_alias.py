#!/usr/bin/env python3
"""Structural audit for development version 091 Linux text/cache fixes."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8", errors="replace")

text_renderer = read("src/rendering/title-gpu-text-renderer.cpp")
inline_text = read("src/canvas/canvas-preview-inline-text.cpp")
cache_header = read("src/cache/cache-manager.h")
cache_worker = read("src/cache/cache-manager/worker-publication.inc")
disk_cache = read("src/cache/cache-manager/visual-hash-keying.inc")
gpu_lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/gpu-frame-cache-alias.inc")
source_header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")
source_lifecycle = read("../../packages/flux-common/Shared/rendering-engine/title-source/source-lifecycle-playback.inc")
cmake = read("CMakeLists.txt")
doc = read("docs/CHANGELOG.md")

checks = [
    (
        "Linux Alpha8 glyph coverage is copied as coverage data",
        "glyph_coverage_grayscale8" in text_renderer
        and "QImage::Format_Alpha8" in text_renderer
        and "std::memcpy(coverage.scanLine(y), source.constScanLine(y)" in text_renderer,
    ),
    (
        "glyph atlas uses bounded 1 MiB pages and complete-page mapped uploads",
        "constexpr int kAtlasSize = 1024;" in text_renderer
        and "constexpr int kAtlasMaxPages = 32;" in text_renderer
        and "for (int y = 0; y < kAtlasSize; ++y)" in text_renderer
        and "mapped + static_cast<size_t>(y) * linesize" in text_renderer,
    ),
    (
        "inline edits force an immediate full-canvas presentation",
        all(token in inline_text for token in (
            "playback_frame_pending_ = false;",
            "playback_present_pending_ = false;",
            "editing_present_pending_ = true;",
            "force_present_pending_ = true;",
            "render_coalesce_timer_->stop();",
            "present_coalesce_timer_->stop();",
            "update();",
        )),
    ),
    (
        "GPU cache exposes shared-frame aliasing",
        "alias_global_gpu_frame_locked" in gpu_lifecycle
        and "bool title_gpu_frame_cache_alias(" in source_header
        and "aliased = alias_global_gpu_frame_locked(" in source_lifecycle,
    ),
    (
        "disk cache exposes metadata-only frame aliases",
        "void enqueueAlias(const CacheFrameKey &key," in cache_header
        and "WriteJob::Kind::Alias" in disk_cache
        and "aliasForGeneration(job.key, job.canonical_key," in disk_cache
        and "bool DiskFrameCache::aliasLocked(" in disk_cache,
    ),
    (
        "worker resolves in-flight canonical states and aliases equivalent frames",
        "matching_readback_pending" in cache_worker
        and "resolveOldestGpuReadback(true);" in cache_worker
        and "title_gpu_frame_cache_alias(" in cache_worker
        and "disk_cache_.enqueueAlias(job.key, canonical_key);" in cache_worker
        and "temporal_canonical_keys_[pending.temporal_state_key]" in cache_worker,
    ),
    (
        "temporal reuse is restricted to deterministic fully cacheable titles",
        "const bool has_animated_asset = std::any_of(" in cache_worker
        and "titleCacheability(job.title) == TitleCacheability::Cacheable" in cache_worker
        and "layer->type == LayerType::Asset" in cache_worker
        and "layer->asset_animated" in cache_worker,
    ),
    (
        "old Linux text/cache payloads are invalidated",
        "v34-linux-alpha8-text-temporal-frame-alias" in disk_cache,
    ),
    (
        "development version is 091 or later",
        bool((match := re.search(
            r'set\(OBS_FXM_DEVELOPMENT_VERSION "([0-9]{3})"\)', cmake
        )) and int(match.group(1)) >= 91),
    ),
    (
        "version 091 implementation and validation contract are documented",
        "Development Version 091" in doc
        and "Temporal Cache Reuse" in doc
        and "Linux" in doc,
    ),
]

passed = 0
for name, ok in checks:
    print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    passed += int(ok)

failed = len(checks) - passed
print(f"Result: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
