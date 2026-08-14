#!/usr/bin/env python3
"""Structural modularity, duplicate-removal and hot-path regression audit."""
from __future__ import annotations

from pathlib import Path
import json
import re
import sys

from source_bundle import read_source_bundle

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "tools/modular-source-map.json").read_text())
errors: list[str] = []
passes: list[str] = []
warnings: list[str] = []

expected_facades = {
    "../../packages/flux-common/Shared/rendering-engine/title-source.cpp",
    "src/editor/title-editor.cpp",
    "src/canvas/canvas-preview.cpp",
    "src/editor/properties-panel.cpp",
    "src/editor/title-dock.cpp",
    "src/cache/cache-manager.cpp",
    "src/editor/title-editor-internal.h",
}
if set(MANIFEST) != expected_facades:
    errors.append("modular source map does not contain the seven audited monoliths")
else:
    passes.append("all seven audited monoliths have explicit module maps")

module_paths: list[str] = []
for facade, info in MANIFEST.items():
    facade_path = ROOT / facade
    line_count = len(facade_path.read_text(errors="replace").splitlines())
    if line_count > 200:
        errors.append(f"facade remains too large: {facade} ({line_count} lines)")
    for module in info["modules"]:
        rel = module["file"]
        module_paths.append(rel)
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"missing implementation module: {rel}")
            continue
        count = len(path.read_text(errors="replace").splitlines())
        if count > 1500:
            errors.append(f"implementation module exceeds 1500 lines: {rel} ({count})")
if not any("facade remains" in e or "implementation module exceeds" in e for e in errors):
    passes.append(f"{len(module_paths)} implementation modules keep facades under 200 and modules under 1500 lines")

cmake = (ROOT / "CMakeLists.txt").read_text()
module_cmake = (ROOT / "cmake/FxmSourceModules.cmake").read_text()
if "include(cmake/FxmSourceModules.cmake)" not in cmake or "${PLUGIN_MODULES}" not in cmake:
    errors.append("CMake does not expose implementation modules to the target/IDE graph")
missing_in_cmake = [path for path in module_paths if path not in module_cmake]
if missing_in_cmake:
    errors.append(f"implementation modules missing from CMake inventory: {missing_in_cmake[:5]}")
else:
    passes.append("CMake and IDE source groups track every implementation module")

# Every implementation module must be included exactly once by one facade.
include_counts = {path: 0 for path in module_paths}
for facade in expected_facades:
    text = (ROOT / facade).read_text(errors="replace")
    for path in module_paths:
        rel = Path(path).relative_to(Path(facade).parent).as_posix() if Path(path).is_relative_to(Path(facade).parent) else None
        if rel:
            include_counts[path] += text.count(f'#include "{rel}"')
wrong = [path for path, count in include_counts.items() if count != 1]
if wrong:
    errors.append(f"implementation modules not included exactly once: {wrong[:5]}")
else:
    passes.append("every implementation module has one ordered facade include")

# Facade readers must expand modules in structural tests/audits.
if not (ROOT / "tests/source_bundle_reader.h").is_file() or not (ROOT / "tools/source_bundle.py").is_file():
    errors.append("source-bundle readers are missing")
else:
    passes.append("tests and audits can inspect complete modular translation units")

# High-confidence duplicate removals.
all_code = "\n".join(
    p.read_text(encoding="utf-8", errors="replace")
    for p in (ROOT / "src").rglob("*")
    if p.is_file() and p.suffix in {".cpp", ".h", ".inc"}
)
checks = {
    "LongPressToolButton has one canonical implementation": all_code.count("class LongPressToolButton final") == 1,
    "Open Color palette has one canonical implementation": all_code.count("add_group(QStringLiteral(\"gray\")") == 1,
    "layer-to-rich-text default mapping is canonical": all_code.count("f.font_family = layer.font_family;") == 1,
    "preset category parser is canonical": all_code.count("if (parts.size() > 16)") == 1,
}
for name, ok in checks.items():
    (passes if ok else errors).append(name if ok else f"duplicate remains: {name}")

# RAM membership checks should not mutate LRU state or allocate a throwaway image.
cache_header = (ROOT / "src/cache/cache-manager.h").read_text()
ram_cache = (ROOT / "src/cache/ram-frame-cache.cpp").read_text()
cache_manager = read_source_bundle(ROOT / "src/cache/cache-manager.cpp")
if ("bool contains(const CacheFrameKey &key) const;" in cache_header and
        "bool RamFrameCache::contains(const CacheFrameKey &key) const" in ram_cache and
        "return cache.contains(key);" in cache_manager and
        "QImage ignored;" not in cache_manager):
    passes.append("RAM frame membership checks are non-mutating and allocation-free")
else:
    errors.append("RAM membership hot path still uses get()/temporary image/LRU mutation")

# Logger categories must match actual instrumentation, including modular files.
logger = (ROOT / "src/core/title-logger.cpp").read_text()
category_keys = set(re.findall(r'\{QStringLiteral\("([^"]+)"\), QStringLiteral', logger))
used = set(re.findall(r'FXM_LOG_(?:ERROR|WARNING|INFO|DEBUG|TRACE)\("([^"]+)"', all_code))
used.update(re.findall(r'TitleLogger::log\([^,\n]+,\s*"([^"]+)"', all_code))
if used <= category_keys and (category_keys - {"General"}) <= used:
    passes.append("all selectable logger categories have real instrumentation")
else:
    errors.append(f"logger category mismatch: missing={sorted(used-category_keys)}, unused={sorted((category_keys-{"General"})-used)}")

# Regression hygiene.
for path in ROOT.rglob("*"):
    if path.is_file() and path.suffix in {".cpp", ".h", ".inc", ".py", ".cmake"}:
        for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if re.match(r"^(<<<<<<<|=======|>>>>>>>)", line):
                errors.append(f"merge marker: {path.relative_to(ROOT)}:{number}")
if not any("merge marker" in e for e in errors):
    passes.append("no merge-conflict markers remain")

development_match = re.search(
    r'set\(OBS_FXM_DEVELOPMENT_VERSION "([0-9]{3})"\)', cmake)
if ('project(flux-motion VERSION 0.11.0)' in cmake and
        'set(OBS_FXM_PRERELEASE "")' in cmake and
        development_match):
    development_version = development_match.group(1)
    build_info = (ROOT / "src/core/build-info.h").read_text(errors="replace")
    readme = (ROOT / "README.md").read_text(errors="replace")
    if (f'#define FXM_DEVELOPMENT_VERSION "{development_version}"' in build_info and
            f'Development Version {development_version}' in readme):
        passes.append(
            f"public/development version identity is synchronized at "
            f"v0.11.0 / {development_version}")
    else:
        errors.append("development version is not synchronized across CMake/build-info/README")
else:
    errors.append("public or development version identity is invalid")

# Known risks intentionally not auto-fixed without runtime/pixel fixtures.
warnings.append("13 exact clone groups remain, dominated by editor/OBS renderer parity code")
warnings.append("compatibility masks/effects still allocate some full-canvas intermediate QImages")
warnings.append("several dock workflows still use synchronous TitleDataStore::save(); interaction paths should be profiled before conversion")

print("Modularity, duplicate and performance regression audit")
for item in passes:
    print(f"  PASS: {item}")
for item in warnings:
    print(f"  WARN: {item}")
for item in errors:
    print(f"  FAIL: {item}")
print(f"RESULT: {'PASS' if not errors else 'FAIL'} ({len(passes)} checks, {len(warnings)} warnings)")
sys.exit(0 if not errors else 1)
