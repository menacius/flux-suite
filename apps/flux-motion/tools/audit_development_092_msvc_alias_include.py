#!/usr/bin/env python3
"""Structural regression audit for Development Version 092."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source.cpp").read_text(encoding="utf-8")
alias = (ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source/gpu-frame-cache-alias.inc").read_text(encoding="utf-8")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build_info = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")

checks = []
def check(name, condition):
    checks.append((name, bool(condition)))

fragment_order = [
    '#include "title-source/gpu-session-lifecycle.inc"',
    '#include "title-source/source-lifecycle-playback.inc"',
    '#include "title-source/source-registration.inc"',
    '#include "title-source/gpu-frame-cache-alias.inc"',
]
positions = [source.find(item) for item in fragment_order]
check("all implementation includes exist", all(pos >= 0 for pos in positions))
check("split function fragments remain contiguous and alias follows them",
      positions == sorted(positions) and all(pos >= 0 for pos in positions))

declaration = re.search(
    r"static\s+bool\s+alias_global_gpu_frame_locked\s*\(\s*"
    r"const\s+std::string\s*&\s*cache_key\s*,\s*"
    r"const\s+std::string\s*&\s*canonical_cache_key\s*\)\s*;",
    source,
    re.S,
)
check("top-level alias forward declaration exists", declaration is not None)
check("alias implementation remains in dedicated module",
      "static bool alias_global_gpu_frame_locked(" in alias)
cmake_version = re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION "([0-9]{3})"\)', cmake)
build_version = re.search(r'#define FXM_DEVELOPMENT_VERSION "([0-9]{3})"', build_info)
check("development version is 092 or later",
      cmake_version is not None and build_version is not None and
      cmake_version.group(1) == build_version.group(1) and
      int(cmake_version.group(1)) >= 92)

# Crude but effective delimiter audit over the exact implementation include order.
def strip_non_code(text):
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    text = re.sub(r"'(?:\\.|[^'\\])*'", "''", text)
    return text

modules = []
for line in source.splitlines():
    match = re.match(r'#include "title-source/(.+\.inc)"', line)
    if match:
        modules.append(ROOT / "../../packages/flux-common/Shared/rendering-engine/title-source" / match.group(1))
combined = "\n".join(path.read_text(encoding="utf-8") for path in modules)
balance = 0
minimum = 0
for char in strip_non_code(combined):
    if char == "{":
        balance += 1
    elif char == "}":
        balance -= 1
        minimum = min(minimum, balance)
check("combined implementation braces are balanced", balance == 0 and minimum >= 0)

failed = [name for name, ok in checks if not ok]
for name, ok in checks:
    print(("PASS" if ok else "FAIL"), name)
if failed:
    sys.exit(1)
print(f"PASS Development 092 MSVC alias include audit ({len(checks)}/{len(checks)})")
