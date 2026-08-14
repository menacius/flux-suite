#!/usr/bin/env python3
from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
source = read_source_bundle(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
cache_h = (ROOT / "src/cache/cache-manager.h").read_text(encoding="utf-8")
cache = read_source_bundle(ROOT / "src/cache/cache-manager.cpp")
canvas_h = (ROOT / "src/canvas/canvas-preview.h").read_text(encoding="utf-8")
canvas = read_source_bundle(ROOT / "src/canvas/canvas-preview.cpp")
logger_h = (ROOT / "src/core/title-logger.h").read_text(encoding="utf-8")
logger = (ROOT / "src/core/title-logger.cpp").read_text(encoding="utf-8")
prefs_h = (ROOT / "src/core/title-preferences.h").read_text(encoding="utf-8")
prefs = (ROOT / "src/core/title-preferences.cpp").read_text(encoding="utf-8")
editor = read_source_bundle(ROOT / "src/editor/title-editor.cpp")
plugin = (ROOT / "../../plugins/obs/flux-motion/src/plugin-main.cpp").read_text(encoding="utf-8")
locale = (ROOT / "data/locale/en-US.ini").read_text(encoding="utf-8")

category_keys = set(re.findall(
    r'\{QStringLiteral\("([^"]+)"\), QStringLiteral', logger))
used_category_keys = set()
category_call = re.compile(
    r'FXM_LOG_(?:ERROR|WARNING|INFO|DEBUG|TRACE)\("([^"]+)"')
for source_file in (ROOT / "src").rglob("*"):
    if source_file.suffix not in {".cpp", ".h", ".inc"}:
        continue
    used_category_keys.update(category_call.findall(
        source_file.read_text(encoding="utf-8", errors="ignore")))
required_instrumented_categories = category_keys - {"General"}

checks = [
    ("first-frame publication is transactional",
     "if (!all_required_rasters_ready)" in source and
     "GPU raster set is incomplete; frame publication deferred." in source),
    ("complete cached finals bypass unrelated live-raster readiness",
     source.find("if (session->use_gpu_cached_final)") <
         source.find("bool all_required_rasters_ready = true;") and
     source.find("if (session->use_submitted_final)") <
         source.find("bool all_required_rasters_ready = true;") and
     "stage=gpu-publish-ram-final" in source),
    ("auxiliary scene-mask rasters cannot block main-frame publication",
     "if (is_gpu_scene_mask_raster_id(pair.first))" in source and
     "title_gpu_render_session_render_auxiliary_layer()" in source),
    ("async prerender refuses dirty or revision-mismatched frames",
     "!texture || session->frame_dirty ||" in source and
     "!gpu_session_final_matches_model(session)" in source and
     "stage=readback-submit action=reject-incomplete" in source),
    ("synchronous readback uses the same complete-current-model contract",
     "const bool complete_current_frame = texture && !session->frame_dirty" in source and
     "stage=sync-readback action=reject-incomplete" in source),
    ("editor queues one GPU-model recovery snapshot after a failed first draw",
     "std::atomic_bool gpu_recovery_queued_" in canvas_h and
     "compare_exchange_strong" in canvas and
     "Qt::QueuedConnection" in canvas),
    ("cache jobs carry bounded retry state",
     "int retry_count = 0;" in cache_h and
     "kMaximumGpuReadbackRetries = 4" in cache),
    ("submit, resolve and payload failures all use the retry path",
     all(f'retryFailedJob(job, live_state_key, "{stage}")' in cache
         for stage in ("submit", "resolve", "payload"))),
    ("renderer cache ABI invalidates previously published blank frames",
     "gpu-renderer-v31-lens-flare-dx11-keyword-fix" in cache),
    ("logger exposes categories and session lifecycle",
     "struct TitleLogCategory" in logger_h and
     "void startSession();" in logger_h and
     "QString currentSessionFilePath();" in logger_h),
    ("session filenames contain date and time and remain stable during a run",
     "yyyy-MM-dd_HH-mm-ss-zzz" in logger and
     "flux-motion_%1.log" in logger),
    ("category preferences are persisted independently",
     "logging_category_enabled" in prefs_h and
     "kLoggingCategoriesGroup" in prefs),
    ("every selectable application section has concrete instrumentation",
     required_instrumented_categories <= used_category_keys and
     used_category_keys <= category_keys),
    ("preferences UI uses a scrollable checkbox list for all logger categories",
     "auto *category_scroll = new QScrollArea" in editor and
     "for (const TitleLogCategory &category : TitleLogger::categories())" in editor and
     "set_logging_category_enabled" in editor),
    ("OBS module starts and closes exactly one logging session",
     "TitleLogger::startSession();" in plugin and
     "TitleLogger::endSession();" in plugin),
    ("session-log UI localization keys exist",
     all(key in locale for key in (
         "OBSTitles.LogFolder=", "OBSTitles.CurrentSessionLog=",
         "OBSTitles.LoggingCategories=", "OBSTitles.LoggingSessionHint="))),
]

passed = 0
for name, ok in checks:
    print(f"  {'PASS' if ok else 'FAIL'}: {name}")
    passed += bool(ok)
print(f"Result: {passed} passed, {len(checks) - passed} failed")
sys.exit(0 if passed == len(checks) else 1)
