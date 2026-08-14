#!/usr/bin/env python3
"""Structural audit for Development Version 108 External Data Providers."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
checks: list[tuple[str, bool]] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def check(name: str, condition: bool) -> None:
    checks.append((name, condition))


cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
types = read("src/core/external-data-types.h")
provider_h = read("src/core/external-data-provider.h")
provider_cpp = read("src/core/external-data-provider.cpp")
manager_cpp = read("src/core/external-data.cpp")
title_cpp = read("src/core/title-data.cpp")
dialog_h = read("Editor/external-data-settings-dialog.h")
dialog_cpp = read("Editor/external-data-settings-dialog.cpp")
dock = read("src/editor/title-dock.cpp") + read("src/editor/title-dock/live-text-cache-playlist.inc")
plugin = read("../../plugins/obs/flux-motion/src/plugin-main.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check(
    "development version is synchronized at current 115",
    'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake
    and '#define FXM_DEVELOPMENT_VERSION "115"' in build_info,
)
check(
    "all requested provider types are modeled",
    all(token in types for token in (
        "JsonFile = 1", "CsvFile = 2", "HttpJson = 3", "WebSocket = 4",
        "LocalTextFile = 5", "ManualTable = 6",
    )),
)
check(
    "common provider interface exposes lifecycle validation state and errors",
    all(token in provider_h for token in (
        "class IExternalDataProvider", "connect_provider", "disconnect_provider",
        "refresh()", "validate()", "state()", "error_message()", "reconfigure",
    )),
)
check(
    "providers run on a dedicated worker thread with queued dispatch",
    "QThread thread" in provider_cpp
    and "moveToThread(&thread)" in provider_cpp
    and "Qt::QueuedConnection" in provider_cpp
    and "std::mutex mutex" in provider_cpp,
)
check(
    "JSON nested paths and array indexes are resolved",
    "parse_json_path" in provider_cpp
    and "resolve_json_path" in provider_cpp
    and "token.index" in provider_cpp,
)
check(
    "CSV supports headers selected rows and column mapping",
    all(token in provider_cpp for token in (
        "csv_first_row_headers", "csv_row_index", "csv_column_mapping",
        "parse_csv", "resolve_column",
    )),
)
check(
    "HTTP is asynchronous and includes headers auth timeout and retries",
    all(token in provider_cpp for token in (
        "QNetworkAccessManager", "setRawHeader", "authentication_token",
        "timeout_timer_", "retry_or_fail", "retry_backoff_ms",
    )),
)
check(
    "WebSocket reconnects and preserves actionable errors and last-known state",
    "class WebSocketProvider" in provider_cpp
    and "schedule_reconnect" in provider_cpp
    and "reconnect_delay_ms_" in provider_cpp
    and "state() != ExternalDataConnectionState::Error" in provider_cpp
    and "keep_last_value" in manager_cpp,
)
check(
    "rate limiting coalesces field updates before manager publication",
    "pending_values_" in provider_cpp
    and "publish_timer_" in provider_cpp
    and "rate_limit_ms" in provider_cpp
    and "render_queue_index_" in read("src/core/external-data.h"),
)
check(
    "provider definitions and settings are backward-compatible serialized data",
    "external_provider_to_json" in title_cpp
    and "external_provider_from_json" in title_cpp
    and 'j.contains("provider")' in title_cpp,
)
check(
    "settings UI exposes provider config status errors fields and layer bindings",
    all(token in dialog_cpp + dialog_h for token in (
        "External Data Providers", "Runtime state", "error_value_", "Fields",
        "Bindings", "save_bindings_for_source", "pending_bindings_",
        "external_bindings.push_back",
    )),
)
check(
    "title store and dock synchronize and refresh providers",
    "ExternalDataProviderService::instance().synchronize" in title_cpp
    and "refresh_source" in dock,
)
check(
    "provider shutdown is explicit before plugin teardown",
    "ExternalDataProviderService::instance().shutdown" in plugin,
)
check(
    "Qt Network and WebSockets plus provider tests are registered",
    "Qt${QT_VERSION_MAJOR}::Network" in cmake
    and "Qt${QT_VERSION_MAJOR}::WebSockets" in cmake
    and "external_data_providers_contract_test" in cmake
    and "external_data_runtime_test" in cmake,
)
check(
    "OBS Qt6 dependency layout is detected without silent Qt5 fallback",
    '"${OBS_SDK_DIR}/lib/cmake/Qt6"' in cmake
    and 'find_package(Qt6 COMPONENTS Core Widgets Svg Network QUIET)' in cmake
    and 'elseif(OBS_SDK_DIR)' in cmake
    and 'expected to use Qt6' in cmake,
)

check(
    "Development 108 documentation describes providers and last-known behavior",
    "Development Version 108" in changelog
    and "JSON file" in docs
    and "WebSocket" in docs
    and "last-known" in docs.lower(),
)

for name, passed in checks:
    print(("PASS" if passed else "FAIL"), name)
failed = [name for name, passed in checks if not passed]
if failed:
    print(f"RESULT: FAIL ({len(checks) - len(failed)} passed, {len(failed)} failed)")
    sys.exit(1)
print(f"RESULT: PASS ({len(checks)} passed, 0 failed)")
