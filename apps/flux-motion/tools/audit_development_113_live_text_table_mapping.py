#!/usr/bin/env python3
"""Structural audit for Development Version 113 table-to-live-cue mapping."""
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
manager_h = read("src/core/external-data.h")
manager = read("src/core/external-data.cpp")
providers = read("src/core/external-data-provider.cpp")
title_h = read("Core/title-data.h")
title_cpp = read("src/core/title-data.cpp")
dialog_h = read("Editor/external-data-table-mapping-dialog.h")
dialog = read("Editor/external-data-table-mapping-dialog.cpp")
binding = read("Editor/external-data-binding-dialog.cpp") + read("Editor/external-data-binding-dialog.h")
lifecycle = read("src/editor/title-dock/dock-lifecycle.inc")
dock = read("src/editor/title-dock/live-text-cache-playlist.inc")
runtime_test = read("tests/external_data_runtime_test.cpp")
contract_test = read("tests/live_text_table_mapping_contract_test.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md") + read("docs/USER_GUIDE.md")
changelog = read("docs/CHANGELOG.md")

check(
    "development version is synchronized as 115",
    'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake
    and '#define FXM_DEVELOPMENT_VERSION "115"' in build,
)
check(
    "table snapshots and row mappings are modeled",
    all(token in types for token in (
        "ExternalDataTableSnapshot", "ExternalDataTableRow", "LiveTextTableBinding",
        "LiveTextTableColumnBinding", "table_binding_id",
    )) and "live_text_table_bindings" in title_h,
)
check(
    "replace append and synchronize behaviors are represented",
    all(token in types for token in ("ReplaceRows", "AppendRows", "SynchronizeRows"))
    and all(token in dialog for token in ("Replace rows", "Append rows", "Synchronize rows")),
)
check(
    "JSON arrays and nested row fields are discovered as tables",
    "discover_json_tables" in providers
    and "flatten_json_table_row" in providers
    and 'table.path = path.empty() ? "$" : path' in providers,
)
check(
    "CSV text and manual providers expose row tables",
    providers.count('table.path = "$rows"') >= 3
    and "source_field_paths" in providers,
)
check(
    "providers publish complete coalesced table snapshots",
    "pending_tables_" in providers
    and "pending_tables_complete_" in providers
    and "synchronize_tables" in providers
    and "rate_limit_ms" in providers,
)
check(
    "manager suppresses unchanged tables and publishes table-specific updates",
    "table_snapshot_equal" in manager
    and "changed_paths.empty()" in manager
    and "update.table_changed = true" in manager
    and "table_path" in manager_h,
)
check(
    "table rows materialize as ordinary live cue cell bindings",
    "materialize_table_rows" in manager
    and "generated.field_path = source_path->second" in manager
    and "cell.table_binding_id = mapping.id" in manager
    and "effective_live_text_cue_value" in manager,
)
check(
    "stable IDs preserve cue identity and manual cells may override generated bindings",
    "table_managed_row_id" in manager
    and "row_id_field" in manager
    and "has_user_override" in manager
    and "active_row_id" in manager
    and "pending_row_id" in manager,
)
check(
    "append and synchronize cleanup semantics are explicit",
    "Append mode retains source-managed rows" in manager
    and "preserve_manual_rows" in manager
    and "remove_live_text_table_binding" in manager,
)
check(
    "mapping and generated-cell metadata serialize backward compatibly",
    '"live_text_table_bindings"' in title_cpp
    and '"table_binding_id"' in title_cpp
    and 'jt.contains("live_text_table_bindings")' in title_cpp,
)
check(
    "mapping UI includes source table columns row behavior formatter and preview",
    all(token in dialog for token in (
        "Populate Live Text Cues from Table", "Table / array", "Stable row ID field",
        "Cue column mapping", "Configure…", "Result preview", "Refresh provider",
    )) and "ExternalDataBindingDialog" in dialog and "preview_values" in binding,
)
check(
    "dock action and async table callback update cue structure on the UI thread",
    "Populate from external table…" in lifecycle
    and "update.table_changed" in lifecycle
    and "QTimer::singleShot" in lifecycle
    and "on_map_external_table" in dock
    and "refreshLiveCueStructureAsync" in dock,
)
check(
    "split title-dock implementation remains structurally valid",
    dock.find("void TitleDock::on_map_external_table()") >= 0
    and dock.find("void TitleDock::on_map_external_table()") < dock.find("TitleDock::create_template_title"),
)
check(
    "runtime and source contracts cover table synchronization",
    all(token in runtime_test for token in (
        "table snapshot publishes its row structure",
        "table mapping materializes source-managed cue rows",
        "generated table cells resolve through the shared formatter pipeline",
        "synchronize mode removes source rows that disappeared",
    ))
    and "live_text_table_mapping_contract_test" in cmake
    and "handler stays outside split template factory" in contract_test,
)
check(
    "documentation describes direct table-to-cue setup",
    "Populate from external table" in docs
    and "Replace rows" in docs
    and "Synchronize rows" in docs
    and "Development Version 113" in changelog,
)

failed = [name for name, passed in checks if not passed]
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
if failed:
    print(f"\n{len(failed)} Development 113 audit check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll {len(checks)} Development 113 audit checks passed.")
