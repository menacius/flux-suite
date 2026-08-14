#!/usr/bin/env python3
"""Structural audit for Development Version 106 External Data Core."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
checks: list[tuple[str, bool]] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def check(name: str, condition: bool) -> None:
    checks.append((name, condition))


def read_bundle(rel: str) -> str:
    path = ROOT / rel
    output: list[str] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.match(r'\s*#\s*include\s+"([^"]+\.inc)"\s*$', line)
        if match:
            output.append(read_bundle(str(Path(rel).parent / match.group(1))))
        else:
            output.append(line)
    return "\n".join(output)


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
types = read("src/core/external-data-types.h")
manager_h = read("src/core/external-data.h")
manager_cpp = read("src/core/external-data.cpp")
layer_model = read("Core/layer-model.h")
title_h = read("Core/title-data.h")
title_cpp = read("src/core/title-data.cpp")
editor = read("src/editor/title-editor/window-session.inc")
source = read_bundle("../../packages/flux-common/Shared/rendering-engine/title-source.cpp")
cache = read_bundle("src/cache/cache-manager.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check(
    "development version is synchronized at current 115",
    'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake
    and '#define FXM_DEVELOPMENT_VERSION "115"' in build_info,
)
check(
    "provider-neutral typed model covers all required field types",
    all(token in types for token in (
        "String = 0", "Integer = 1", "Float = 2", "Boolean = 3",
        "Color = 4", "DateTime = 5", "FilePath = 6", "Url = 7",
    )),
)
check(
    "title and layer authored models own schemas and bindings",
    "external_data_sources" in title_h
    and "external_bindings" in layer_model
    and all(token in types for token in (
        "property_path", "source_id", "field_path", "formatter",
        "fallback_value",
    )),
)
check(
    "manager owns current values, timestamps, and connection errors",
    all(token in manager_h for token in (
        "current_value", "last_update_timestamp_ms", "connection_state",
        "error_message", "ExternalDataManager",
    )),
)
check(
    "same values do not publish or advance manager revision",
    "field.current_value == value" in manager_cpp
    and re.search(r"field\.current_value == value\)\s*return false;", manager_cpp) is not None
    and "revision_.fetch_add" in manager_cpp,
)
check(
    "render queue is thread-safe, bounded, and coalescing",
    "std::lock_guard<std::mutex> lock(mutex_)" in manager_cpp
    and "render_queue_index_" in manager_h
    and "render_queue_[queued->second] = update" in manager_cpp
    and "take_render_updates" in manager_cpp,
)
check(
    "external updates never mutate or persist authored title state",
    "touch_runtime_change" not in manager_cpp
    and "notify_change()" not in manager_cpp
    and "save_async" not in manager_cpp
    and "return authored_value" in manager_cpp,
)
check(
    "fallback order and provider-free mock path are implemented",
    "binding.has_fallback_value" in manager_cpp
    and "definition->has_default_value" in manager_cpp
    and "update_mock_value" in manager_cpp,
)
check(
    "serialization is optional and backward compatible",
    'j["external_bindings"]' in title_cpp
    and 'j.contains("external_bindings")' in title_cpp
    and 'jt["external_data_sources"]' in title_cpp
    and 'jt.contains("external_data_sources")' in title_cpp,
)
check(
    "editor and OBS source update live through effective values",
    "ExternalDataManager::instance().on_change" in editor
    and "external_data_visual_hash_" in editor
    and "seen_external_data_revision" in source
    and "external_visual_change" in source
    and "effective_external_string" in source,
)
check(
    "cache identity includes bindings and effective values",
    "external_bindings" in cache
    and "effective_external_string" in cache,
)
check(
    "behavioral and source-contract tests are registered",
    "external_data_runtime_test" in cmake
    and "external_data_core_contract_test" in cmake
    and (ROOT / "tests/external_data_runtime_test.cpp").is_file()
    and (ROOT / "tests/external_data_core_contract_test.cpp").is_file(),
)
check(
    "canonical documentation and changelog describe Development 106",
    "External data now has a provider-neutral core" in docs
    and "Development Version 106" in changelog,
)

for name, passed in checks:
    print(("PASS" if passed else "FAIL"), name)
failed = [name for name, passed in checks if not passed]
if failed:
    print(f"RESULT: FAIL ({len(checks) - len(failed)} passed, {len(failed)} failed)")
    sys.exit(1)
print(f"RESULT: PASS ({len(checks)} passed, 0 failed)")
