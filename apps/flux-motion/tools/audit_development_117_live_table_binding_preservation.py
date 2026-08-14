#!/usr/bin/env python3
"""Structural audit for Development Version 117 live table binding preservation."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
checks = []

def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")

def check(name, condition):
    checks.append((name, bool(condition)))

cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
readme = read("README.md")
utils = read("src/core/live-text-cue-utils.h")
runtime = read("tests/external_data_runtime_test.cpp")
dock = read("src/editor/title-dock/live-text-cache-playlist.inc")
changelog = read("docs/CHANGELOG.md")

check("development version is synchronized as 117",
      'set(OBS_FXM_DEVELOPMENT_VERSION "117")' in cmake and
      '#define FXM_DEVELOPMENT_VERSION "117"' in build and
      'Development Version 117' in readme)
check("binding pruning uses the active title column order",
      'title->live_text_column_order.begin()' in utils and
      'title->live_text_column_order.end()' in utils)
check("binding pruning no longer captures the moved-from new_order",
      '[&title, &new_order](const LiveTextExternalBinding &cell)' not in utils)
check("source documents the moved-from regression",
      'Never validate bindings against the moved-from local vector' in utils)
check("runtime test exercises actual dock normalization sequence",
      'normalize_live_text_rows(normalized_title, normalized_exposed)' in runtime and
      'dock column normalization preserves generated table-cell bindings' in runtime)
check("runtime regression verifies managed state and resolved value",
      'LiveTextCueCellState::ExternalTableManaged' in runtime and
      '== "TABLE VALUE"' in runtime)
check("dock still resolves displayed cells through the shared live path",
      'effective_live_text_cue_value(' in dock and
      'displayed_cell_value' in dock)
check("changelog documents the blank-row root fix",
      'Development Version 117' in changelog and
      'moved-from temporary vector' in changelog)

failed = 0
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
    failed += 0 if passed else 1
print(f"\nDevelopment 117 audit: {len(checks)-failed}/{len(checks)} passed")
sys.exit(1 if failed else 0)
