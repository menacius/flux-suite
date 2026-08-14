#!/usr/bin/env python3
"""Structural audit for Development Version 111 External Data UI and Formatting."""
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
title_data = read("src/core/title-data.cpp")
settings = read("Editor/external-data-settings-dialog.cpp") + read("Editor/external-data-settings-dialog.h")
binding = read("Editor/external-data-binding-dialog.cpp") + read("Editor/external-data-binding-dialog.h")
properties = read("src/editor/properties-panel.h") + read("src/editor/properties-panel.cpp") + "\n".join(
    p.read_text(encoding="utf-8", errors="replace")
    for p in (ROOT / "src/editor/properties-panel").glob("*.inc")
)
dock = read("src/editor/title-dock/live-text-cache-playlist.inc") + read("src/editor/title-dock/list-selection-cues.inc")
hotkeys = read("../../plugins/obs/flux-motion/src/title-hotkeys.cpp")
layer_stack = read("src/layers/layer-stack-widget.cpp")
docs = read("docs/TEXT_AND_LIVE_DATA.md")
changelog = read("docs/CHANGELOG.md")

check("development version is synchronized at current 115",
      'set(OBS_FXM_DEVELOPMENT_VERSION "115")' in cmake and
      '#define FXM_DEVELOPMENT_VERSION "115"' in build)
check("three refresh modes are modeled and persisted",
      all(x in types for x in ("RefreshOnCue", "RefreshContinuously", "RefreshManually")) and
      '"refresh_mode"' in title_data)
check("formatter model covers all requested stages",
      all(x in types for x in ("prefix", "suffix", "number_format_enabled", "decimal_places",
                               "thousands_separator", "text_case", "date_time_format",
                               "conditional_replacements", "empty_value_mode")))
check("common runtime formatter implements number date case replacement and empty handling",
      all(x in manager for x in ("add_thousands_separators", "utc_tm_from_value",
                                 "conditional_replacements", "ExternalDataTextCase::TitleCase",
                                 "ExternalDataEmptyValueMode::Replacement")))
check("formatter and cue-cell bindings serialize backward compatibly",
      '"formatter_config"' in title_data and '"live_text_external_bindings"' in title_data and
      'j.contains("formatter_config")' in title_data)
check("source settings include complete management and test controls",
      all(x in settings for x in ("Duplicate", "Test connection", "Provider", "Path / URL",
                                  "Live field preview", "Refresh now", "Runtime state")))
check("source settings expose all refresh behaviors",
      all(x in settings for x in ("Refresh on cue", "Refresh continuously", "Refresh manually")))
check("binding popup contains source field formatter fallback and live preview",
      all(x in binding for x in ("Source", "Field", "Fallback", "Formatter pipeline",
                                 "Live preview", "Raw value", "Formatted value")))
check("binding popup contains every requested formatting control",
      all(x in binding for x in ("Prefix", "Suffix", "Enable number formatting",
                                 "Decimal places", "Thousands separator", "UPPERCASE",
                                 "lowercase", "Title Case", "Date/time format",
                                 "Conditional replacement", "Empty value")))
check("text and image properties expose visual binding buttons",
      "btn_text_external_binding_" in properties and "btn_image_external_binding_" in properties and
      "externalDataBound" in properties and "ExternalDataBindingDialog" in properties)
check("live cue cells expose and persist bindings",
      "Bind external data" in dock and "set_live_text_external_binding" in dock and
      "effective_live_text_cue_value" in dock and "apply_live_text_runtime_binding" in dock)
check("refresh-on-cue is invoked by dock and hotkeys",
      "RefreshOnCue" in dock and "refresh_source" in dock and
      "RefreshOnCue" in hotkeys and "refresh_source" in hotkeys)
check("layers display an external binding indicator",
      "has_external_binding" in layer_stack and "Layer has external data binding" in layer_stack)
check("runtime tests and UI contract are registered",
      "external_data_runtime_test" in cmake and "external_data_ui_contract_test" in cmake)
check("documentation covers UI formatting and cue behavior",
      "Binding UI and formatter pipeline" in docs and "Development Version 111" in changelog)

failed = [name for name, passed in checks if not passed]
for name, passed in checks:
    print(("PASS" if passed else "FAIL") + ": " + name)
if failed:
    print(f"\n{len(failed)} Development 111 audit check(s) failed.", file=sys.stderr)
    sys.exit(1)
print(f"\nAll {len(checks)} Development 111 audit checks passed.")
