#!/usr/bin/env python3
"""Structural audit for Development Version 115 live table value resolution."""
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
types = read("src/core/external-data-types.h")
manager = read("src/core/external-data.cpp")
dock = read("src/editor/title-dock/live-text-cache-playlist.inc")
runtime = read("tests/external_data_runtime_test.cpp")
contract = read("tests/live_text_table_mapping_contract_test.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check("development version is synchronized as 115",
      'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake and
      '#define FXM_DEVELOPMENT_VERSION "115"' in build)
check("runtime-only property value is modeled",
      "runtime_value" in types and "has_runtime_value" in types and
      "intentionally omitted from title serialization" in types)
check("shared manager resolution accepts authoritative runtime values",
      "binding.has_runtime_value" in manager and
      "binding.runtime_value" in manager and
      "ExternalDataValueOrigin::LiveExternal" in manager)
check("table materialization no longer requires scalar row paths",
      "generated.field_path = column.binding.field_path" in manager and
      "cell.binding.runtime_value" in manager)
check("value-only table updates compare runtime values",
      "a.has_runtime_value == b.has_runtime_value" in manager and
      "a.runtime_value == b.runtime_value" in manager)
check("cue and OBS paths share the runtime binding",
      "apply_live_text_runtime_binding" in manager and
      "layer.runtime_external_bindings.push_back" in manager)
check("table-managed cue cells are visually italic only in the dock",
      "externalTableManaged" in dock and "setItalic(table_managed)" in dock)
check("authored cue storage remains separate",
      "authored_cell_value" in dock and "displayed_cell_value" in dock)
check("runtime regression covers missing scalar registry paths",
      "table-only snapshot publishes without scalar row paths" in runtime and
      "OBS/source runtime binding carries the authoritative table value" in runtime)
check("source contract protects the new behavior",
      "runtime-only authoritative table cell value" in contract and
      "mapped table values render in italics" in contract)
check("documentation explains authoritative snapshot and UI-only italics",
      "shown in *italics*" in docs and "table snapshot itself is treated as authoritative" in docs)
check("changelog documents Development 115",
      "Development Version 115" in changelog and "remaining blank" in changelog)

failed = 0
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
    failed += 0 if passed else 1
print(f"\nDevelopment 115 audit: {len(checks)-failed}/{len(checks)} passed")
sys.exit(1 if failed else 0)
