#!/usr/bin/env python3
"""Dependency-free structural audit for the prerender presentation contract."""

from __future__ import annotations

from pathlib import Path
from source_bundle import read_source_bundle
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return read_source_bundle(ROOT / path)


errors: list[str] = []
notes: list[str] = []
warnings: list[str] = []

source_roots = (ROOT / "src", ROOT / "Core", ROOT / "Shared")
source_files = [
    path
    for source_root in source_roots
    for pattern in ("*.cpp", "*.h", "*.inc")
    for path in source_root.rglob(pattern)
]
all_source = "\n".join(path.read_text(encoding="utf-8", errors="replace") for path in source_files)

for path in source_files:
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        if re.match(r"^(<<<<<<<|=======|>>>>>>>)", line):
            errors.append(
                f"merge conflict marker remains in {path.relative_to(ROOT)}:{line_number}"
            )

metadata_key_occurrences = len(re.findall(r'"obs_fxm_canvas_(?:x|y|width|height)"', all_source))
if metadata_key_occurrences != 4:
    errors.append(
        f"cache placement metadata must have one definition per key; found {metadata_key_occurrences} literals"
    )
else:
    notes.append("single source of truth for sparse-frame placement metadata")

cache_manager = read("src/cache/cache-manager.cpp")
editor = read("src/editor/title-editor.cpp")
canvas = read("src/canvas/canvas-preview.cpp")
source = read("../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
source_header = read("../../packages/flux-common/Shared/rendering-engine/title-source.h")

for forbidden in (
    "std::round(t * cache_obs_frame_rate())",
    "std::round(playhead_ * CacheManager::instance().effectiveFrameRate())",
):
    if forbidden in cache_manager or forbidden in editor:
        errors.append(f"conflicting time-to-frame conversion remains: {forbidden}")

if cache_manager.count("cache_frame_index_for_time(") < 2:
    errors.append("lookup and invalidation do not share cache_frame_index_for_time")
else:
    notes.append("lookup and invalidation share frame interval ownership")

if not re.search(r"frameIndexForTitleTime\s*\(\s*\*title_\s*,\s*playhead_\s*\)", editor):
    errors.append("editor frame-ready matching ignores static-title frame collapsing")
if not re.search(r"frameIndexForTitleTime\s*\(\s*\*title\s*,", source):
    errors.append("source frame-ready matching ignores static-title frame collapsing")
if cache_manager.count("frameIndexForTitleTime(") < 2:
    errors.append("title-aware frame ownership is not centralized in CacheManager")
else:
    notes.append("editor/source wakeups use title-aware cache frame ownership")

if "static Title snapshot_title_for_render" in source or "immutable_title_snapshot" in cache_manager:
    errors.append("duplicate deep-title snapshot implementation remains")
if all_source.count("inline Title clone_title_snapshot") != 1:
    errors.append("title snapshot contract is not defined exactly once")
else:
    notes.append("single deep-title snapshot implementation")

if "title_gpu_render_session_submit_final_frame(" not in canvas:
    errors.append("editor canvas still does not submit completed prerender frames")
if "title_gpu_render_session_submit_cached_prefix(" not in canvas:
    errors.append("editor canvas still does not submit partial-cache prefixes")
if "title_gpu_render_session_set_preview_quality(\n                    gpu_render_session_, 1.0, false)" not in canvas:
    errors.append("editor cached payload is not forced to full-canvas coordinate space")
else:
    notes.append("editor and source submit the same full-quality cache payload contract")

if "bool title_gpu_render_session_submit_final_frame" not in source_header:
    errors.append("final-frame submission does not report rejection to callers")
if "bool title_gpu_render_session_submit_cached_prefix" not in source_header:
    errors.append("prefix submission does not report rejection to callers")
if "reason=invalid-placement" not in source:
    errors.append("invalid sparse payload placement is not rejected explicitly")
else:
    notes.append("invalid or stale sparse crops are rejected instead of stretched")

if "cache.rejectFramePayload(" not in canvas or "cache.rejectFramePayload(" not in source:
    errors.append("editor and source do not both evict/requeue rejected cache payloads")
else:
    notes.append("rejected cache payloads self-heal through eviction and realtime requeue")

if "&CacheManager::frameReady" not in source:
    errors.append("OBS sources are not notified when a missing static cache frame completes")
if "action=frame-ready-wakeup" not in source:
    errors.append("source cache-completion wakeup has no runtime diagnostic")
if "take_source_cache_wake_frame(" not in source:
    errors.append("static source does not consume exact frame-ready notifications")
else:
    notes.append("static OBS sources wake and adopt completed prerender frames")

if "gpu-renderer-v31-lens-flare-dx11-keyword-fix" not in cache_manager:
    errors.append("renderer cache ABI was not bumped for the Phase 15 visibility-recovery contract")
else:
    notes.append("pre-recovery blank/legacy frame cache generations are invalidated by current renderer ABI")

if "add(QString::fromStdString(p.name))" in cache_manager:
    errors.append("animation editor labels still pollute the visual cache hash")
if "add(rt.has_typing_format)" in cache_manager:
    errors.append("caret typing format still pollutes the visual cache hash")
if "rule.display_name" in cache_manager:
    errors.append("auto-style display labels still pollute the visual cache hash")
else:
    notes.append("non-pixel editor metadata removed from visual cache identity")

live_text_utils = read("src/core/live-text-cue-utils.h")
order_definition_count = len(re.findall(
    r"(?m)^(?:inline|static)\s+[^\n]*order_exposed_text_layers\s*\(",
    all_source,
))
normalize_definition_count = len(re.findall(
    r"(?m)^(?:inline|static)\s+[^\n]*normalize_live_text_rows\s*\(",
    all_source,
))
if order_definition_count != 1:
    errors.append("exposed live-text layer ordering is not defined exactly once")
if normalize_definition_count != 1:
    errors.append("live-text row normalization is not defined exactly once")
if "namespace fxm::live_text" not in live_text_utils:
    errors.append("shared live-text cue contract header is missing its namespace")
else:
    notes.append("dock, hotkeys, source, and cache share one live-text cue ordering contract")


# Build graph/source duplication audit.
cmake = read("CMakeLists.txt")
source_groups: list[tuple[str, str]] = []
for group_name, body in re.findall(
    r"set\((OBS_FXM_[A-Z_]+_(?:SOURCES|HEADERS))\s+(.*?)\n\)", cmake, re.S
):
    for source_path in re.findall(r"(?m)^\s*(src/\S+)", body):
        source_groups.append((source_path, group_name))

groups_by_path: dict[str, list[str]] = {}
for source_path, group_name in source_groups:
    groups_by_path.setdefault(source_path, []).append(group_name)
for source_path, groups in groups_by_path.items():
    if len(groups) > 1:
        errors.append(f"source appears in multiple plugin groups: {source_path} -> {groups}")

listed_compilation_units = {
    source_path
    for source_path, _ in source_groups
    if source_path.endswith((".cpp", ".c", ".cc"))
}
actual_compilation_units = {
    str(path.relative_to(ROOT)).replace("\\", "/")
    for path in (ROOT / "src").rglob("*")
    if path.is_file() and path.suffix in {".cpp", ".c", ".cc"}
}
for source_path in sorted(actual_compilation_units - listed_compilation_units):
    errors.append(f"compilation unit is missing from plugin CMake groups: {source_path}")
for source_path in sorted(listed_compilation_units - actual_compilation_units):
    errors.append(f"CMake references a missing compilation unit: {source_path}")
if not (actual_compilation_units - listed_compilation_units) and not (
    listed_compilation_units - actual_compilation_units
):
    notes.append("all compilation units are listed once in plugin build groups")

# Exact duplicate files are almost always accidental after a split/refactor.
import hashlib
hashes: dict[str, list[str]] = {}
for path in (ROOT / "src").rglob("*"):
    if path.is_file() and path.suffix in {".cpp", ".h", ".hpp", ".c", ".inc"}:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        hashes.setdefault(digest, []).append(str(path.relative_to(ROOT)))
for duplicate_paths in hashes.values():
    if len(duplicate_paths) > 1:
        errors.append(f"exact duplicate source files: {duplicate_paths}")
if all(len(paths) == 1 for paths in hashes.values()):
    notes.append("no exact duplicate source/header files")

# Repeated includes and adjacent identical assignments are common merge/refactor
# leftovers. Limit this to exact matches so intentional overloads are not
# misclassified as duplicates.
duplicate_include_found = False
duplicate_assignment_found = False
assignment_pattern = re.compile(r"^[A-Za-z_][\w:>\.\-\[\]]*\s*=\s*.+;$")
for path in source_files:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    includes: dict[str, int] = {}
    previous_assignment: tuple[str, int] | None = None
    for line_number, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("#include "):
            if stripped in includes:
                duplicate_include_found = True
                errors.append(
                    f"duplicate include in {path.relative_to(ROOT)} at lines "
                    f"{includes[stripped]} and {line_number}: {stripped}"
                )
            else:
                includes[stripped] = line_number
        if assignment_pattern.match(stripped):
            if previous_assignment and previous_assignment[0] == stripped:
                duplicate_assignment_found = True
                errors.append(
                    f"adjacent duplicate assignment in {path.relative_to(ROOT)} at lines "
                    f"{previous_assignment[1]} and {line_number}: {stripped}"
                )
            previous_assignment = (stripped, line_number)
        elif stripped and not stripped.startswith(("//", "/*", "*")):
            previous_assignment = None
if not duplicate_include_found:
    notes.append("no duplicate includes")
if not duplicate_assignment_found:
    notes.append("no adjacent duplicate assignments")

# Duplicate locale keys silently shadow earlier labels at runtime.
locale_duplicates = False
for locale_path in (ROOT / "data" / "locale").glob("*.ini"):
    seen: dict[str, int] = {}
    for line_number, line in enumerate(
        locale_path.read_text(encoding="utf-8", errors="replace").splitlines(), 1
    ):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", "[")) or "=" not in stripped:
            continue
        key = stripped.split("=", 1)[0].strip()
        if key in seen:
            locale_duplicates = True
            errors.append(
                f"duplicate locale key {key} in {locale_path.relative_to(ROOT)} "
                f"at lines {seen[key]} and {line_number}"
            )
        else:
            seen[key] = line_number
if not locale_duplicates:
    notes.append("no duplicate localization keys")

for required in (
    "src/core/live-text-cue-utils.h",
    "src/cache/cache-frame-payload.h",
    "src/cache/cache-time.h",
    "Core/title-snapshot.h",
    "tests/cache_time_contract_test.cpp",
    "tests/cache_frame_payload_test.cpp",
    "tests/live_text_cue_utils_test.cpp",
    "tests/title_snapshot_test.cpp",
    "tests/gpu_prerender_phase14_contract_test.cpp",
):
    if required not in cmake:
        errors.append(f"CMake does not track {required}")

if all_source.count("class LongPressToolButton final") > 1:
    warnings.append(
        "LongPressToolButton remains duplicated in dock/editor UI; low-risk UI refactor debt"
    )
if all_source.count("auto add_open_color =") > 1:
    warnings.append(
        "the built-in open-color palette remains duplicated in properties/editor UI"
    )
if "static QFont font_for_layer" in read("src/editor/title-editor-internal.h") and \
        "static QFont font_for_layer" in source:
    warnings.append(
        "editor utility and OBS source retain parallel text/vector helper implementations; "
        "a dedicated renderer-parity refactor needs pixel regression fixtures"
    )

print("Prerender/cache structural audit")
for note in notes:
    print(f"  PASS: {note}")
for warning in warnings:
    print(f"  WARN: {warning}")
for error in errors:
    print(f"  FAIL: {error}")

if errors:
    sys.exit(1)
print(f"  RESULT: PASS ({len(notes)} contract checks)")
