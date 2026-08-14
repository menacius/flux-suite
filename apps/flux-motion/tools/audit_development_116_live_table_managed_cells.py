#!/usr/bin/env python3
"""Structural audit for Development Version 116 live table values and managed cells."""
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
readme = read("README.md")
types = read("src/core/external-data-types.h")
header = read("src/core/external-data.h")
manager = read("src/core/external-data.cpp")
dock = read("src/editor/title-dock/live-text-cache-playlist.inc")
commit = read("src/editor/title-dock/list-selection-cues.inc")
image_field = read("src/editor/title-dock/template-library-helpers.inc")
image_tail = read("src/editor/title-dock/import-export-helpers.inc")
runtime = read("tests/external_data_runtime_test.cpp")
contract = read("tests/live_text_table_mapping_contract_test.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check("development version is synchronized as 116",
      'set(OBS_FXM_DEVELOPMENT_VERSION "116")' in cmake and
      '#define FXM_DEVELOPMENT_VERSION "116"' in build and
      'Development Version 116' in readme)
check("row-specific runtime table value precedes scalar lookup",
      manager.find("if (current_value_is_allowed && binding.has_runtime_value") <
      manager.find("const auto field_it = source.fields.find(binding.field_path)"))
check("cue cells have explicit authored external managed and detached states",
      "enum class LiveTextCueCellState" in types and
      all(name in types for name in ("Authored", "ExternalBound", "ExternalTableManaged", "DetachedFromTable")))
check("managed cell state and detach restore APIs are public",
      "live_text_cue_cell_state" in header and
      "detach_live_text_table_cell" in header and
      "restore_live_text_table_cell" in header)
check("detached cells use persistent disabled table-origin markers",
      "Keep a disabled table-origin marker" in manager and
      "!cell.binding.enabled" in manager)
check("authored overrides survive table row rematerialization",
      "Preserve authored values for explicit per-cell overrides" in manager and
      "title.live_text_rows[previous->second]" in manager)
check("mapped text and image cells are genuinely read-only",
      "setReadOnly(table_managed)" in dock and
      "set_read_only(table_managed)" in dock and
      "button_->setVisible(!read_only_)" in image_field and
      "bool read_only_ = false" in image_tail)
check("commit path rejects managed cells including multi-row edits",
      commit.count("LiveTextCueCellState::ExternalTableManaged") >= 2)
check("cue UI exposes explicit cell state and visual origin",
      "liveCueCellState" in dock and
      "external-table-managed" in dock and
      "setItalic(table_managed)" in dock)
check("context menu supports explicit detach and restore",
      "Convert to editable value" in dock and
      "Restore table-managed value" in dock and
      "detach_live_text_table_cell" in dock and
      "restore_live_text_table_cell" in dock)
check("runtime regression covers managed detached refresh and restore behavior",
      "explicit read-only managed state" in runtime and
      "provider refresh preserves detached authored cell values" in runtime and
      "restoring reattaches the generated table-managed cell" in runtime)
check("source contract protects managed-cell UI and core behavior",
      "explicit cue cell state model" in contract and
      "mapped text cue cells are read-only" in contract and
      "explicit detach action" in contract)
check("documentation explains read-only and conversion workflow",
      "ExternalTableManaged" in docs and
      "Convert to editable value" in docs and
      "Restore table-managed value" in docs)
check("changelog documents Development 116",
      "Development Version 116" in changelog and
      "Managed Cell State" in changelog)

failed = 0
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
    failed += 0 if passed else 1
print(f"\nDevelopment 116 audit: {len(checks)-failed}/{len(checks)} passed")
sys.exit(1 if failed else 0)
