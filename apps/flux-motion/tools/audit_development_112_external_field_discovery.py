#!/usr/bin/env python3
"""Structural audit for Development Version 112 automatic external field discovery."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
checks: list[tuple[str, bool]] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def check(name: str, condition: bool) -> None:
    checks.append((name, condition))


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
types = read("src/core/external-data.h")
manager = read("src/core/external-data.cpp")
providers = read("src/core/external-data-provider.cpp")
settings = read("Editor/external-data-settings-dialog.cpp") + read(
    "Editor/external-data-settings-dialog.h"
)
binding = read("Editor/external-data-binding-dialog.cpp") + read(
    "Editor/external-data-binding-dialog.h"
)
properties = read("src/editor/properties-panel/auto-style-and-property-actions.inc")
dock = read("src/editor/title-dock/live-text-cache-playlist.inc")
runtime_test = read("tests/external_data_runtime_test.cpp")
ui_test = read("tests/external_data_ui_contract_test.cpp")
provider_test = read("tests/external_data_providers_contract_test.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check(
    "development version is synchronized at current 115",
    'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake
    and '#define FXM_DEVELOPMENT_VERSION "115"' in build,
)
check(
    "runtime field state distinguishes authored and discovered schema",
    "authored_definition" in types
    and "field.authored_definition = false" in manager
    and "field.authored_definition = true" in manager,
)
check(
    "available-field union keeps runtime discovery and authored overrides",
    "available_external_data_fields" in types
    and "available_external_data_fields" in manager
    and "merged[field.path] = field" in manager,
)
check(
    "field pinning persists discovered schema without new serialization format",
    "pin_external_data_field" in types
    and "pin_external_data_field" in manager
    and "source.fields.push_back" in manager,
)
check(
    "complete provider synchronization releases removed overrides without losing discovery",
    "synchronize_source_definition" in types
    and "stop enforcing a stale alias/type override" in manager
    and "synchronize_source_definition(definition)" in providers,
)
check(
    "JSON discovery remains active when authored overrides exist",
    "Discovery is always performed" in providers
    and "flatten_json(*root, {}, values)" in providers,
)
check(
    "CSV native columns remain discoverable with mappings",
    "Native columns are always discovered first" in providers
    and "values[path] = ExternalDataValue::string" in providers,
)
check(
    "binding popup lists runtime-discovered fields immediately",
    "available_external_data_fields" in binding
    and "[discovered]" in binding
    and "last_field_revision_" in binding,
)
check(
    "settings presents Fields as an optional override layer",
    "Fields (optional)" in settings
    and "Pin / override" in settings
    and "discovered automatically" in settings,
)
check(
    "settings refreshes discovery and existing binding combos live",
    "refresh_discovered_fields" in settings
    and "Existing binding rows are updated in place" in settings,
)
check(
    "saving settings serializes only pinned/manual schema entries",
    "should_pin" in settings
    and "source.fields.clear()" in settings
    and "source.fields.push_back(field)" in settings,
)
check(
    "bindings auto-pin discovered fields in all UI entry points",
    "pin_external_data_field" in settings
    and "pin_external_data_field" in properties
    and "pin_external_data_field" in dock,
)
check(
    "runtime tests cover discovery type evolution and offline pinning",
    all(
        token in runtime_test
        for token in (
            "runtime discovery creates an undeclared field",
            "unpinned discovered fields can update their inferred scalar type",
            "binding can pin a discovered field into authored schema",
            "offline placeholder field",
            "removing an override releases the pinned field type",
        )
    ),
)
check(
    "source contracts prevent discovery regressions",
    "available_external_data_fields" in ui_test
    and "Discovery is always performed" in provider_test
    and "Native columns are always discovered first" in provider_test,
)
check(
    "documentation describes direct binding and optional schema overrides",
    "Fields (optional)" in docs
    and "automatically pins" in docs
    and "Development Version 112" in changelog,
)

failed = [name for name, passed in checks if not passed]
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
if failed:
    print(f"\n{len(failed)} Development 112 audit check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll {len(checks)} Development 112 audit checks passed.")
