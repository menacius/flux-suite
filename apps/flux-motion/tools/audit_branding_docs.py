#!/usr/bin/env python3
"""Structural audit for current Flux Motion branding and documentation."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
errors: list[str] = []
passes: list[str] = []


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def check(name: str, condition: bool) -> None:
    (passes if condition else errors).append(name)

cmake = read("CMakeLists.txt")
build_info = read("src/core/build-info.h")
plugin_main = read("../../plugins/obs/flux-motion/src/plugin-main.h")
readme = read("README.md")
window = read("src/editor/title-editor/window-session.inc")
assets = read("Editor/title-assets.h")
transition = read("src/transitions/transition-editor-dialog.cpp")
gitignore = read(".gitignore")

check(
    "public and development versions are synchronized",
    (project := re.search(r"project\(flux-motion VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)", cmake)) is not None
    and (prerelease := re.search(r'set\(OBS_FXM_PRERELEASE "([^"]*)"\)', cmake)) is not None
    and (cmake_dev := re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION "([0-9]+)"\)', cmake)) is not None
    and (header_dev := re.search(r'#define FXM_DEVELOPMENT_VERSION "([0-9]+)"', build_info)) is not None
    and cmake_dev.group(1) == header_dev.group(1)
    and int(cmake_dev.group(1)) >= 417
    and (public_version := project.group(1) + (f"-{prerelease.group(1)}" if prerelease.group(1) else ""))
    and f'#define PLUGIN_VERSION "{public_version}"' in build_info
    and f'#define PLUGIN_VERSION "{public_version}"' in plugin_main
    and f"v{public_version}" in readme
    and "Development Version" in readme,
)

check(
    "Flux Motion application and About artwork are installed",
    (ROOT / "data/icons/flux-motion-icon.svg").is_file()
    and (ROOT / "data/icons/flux-motion-about.svg").is_file()
    and (ROOT / "data/fonts/Satoshi-Medium.otf").is_file()
    and 'fxm_brand_icon()' in assets
    and 'fxm_apply_brand_icon(this);' in window
    and 'fxm_apply_brand_icon(this);' in transition
    and sum(1 for p in (ROOT / "src").rglob("*") if p.is_file() and "fxm_apply_brand_icon" in p.read_text(encoding="utf-8", errors="replace")) >= 4,
)

canonical_docs = {
    "README.md",
    "USER_GUIDE.md",
    "EDITOR_WORKFLOW.md",
    "TEXT_AND_LIVE_DATA.md",
    "RENDERING_AND_CACHE.md",
    "EFFECTS_AND_EXTENSIONS.md",
    "ARCHITECTURE_AND_BUILD.md",
    "CHANGELOG.md",
    "PACKED-TITLE-FORMAT.md",
    "FLUX-ENCODER-IPC.md",
    "MULTI-TITLE-PROJECTS.md",
    "visual-effects-sdk.md",
    "ARCHITECTURE_MIGRATION_HISTORY.md",
    "VALIDATION_HISTORY.md",
}
actual_docs = {p.name for p in (ROOT / "docs").iterdir() if p.is_file()}
check("documentation inventory matches the maintained canonical and audit files", actual_docs == canonical_docs)
check(
    "machine-readable module map moved out of documentation",
    (ROOT / "tools/modular-source-map.json").is_file()
    and 'ROOT / "tools/modular-source-map.json"' in read("tools/audit_modularity_performance.py"),
)

all_text = "\n".join(
    p.read_text(encoding="utf-8", errors="replace")
    for p in ROOT.rglob("*")
    if p.is_file() and p != Path(__file__).resolve() and p.suffix.lower() in {".md", ".txt", ".ini", ".h", ".cpp", ".inc", ".py", ".json", ".cmake"}
)
check(
    "previous personal-credit wording is removed",
    all(token.lower() not in all_text.lower() for token in ("Ant" + "onios", "Dimo" + "poulos", "Vibe" + " coder")),
)

required_ignore_tokens = (
    "/build-*/", "CMakeCache.txt", "*.dll", "*.so", "*.zip", ".vs/", ".vscode/",
    ".idea/", "__pycache__/", ".flatpak-builder/", "*.log", ".DS_Store", "Thumbs.db",
)
check("gitignore covers build, IDE, package, cache, log, and OS artifacts",
      all(token in gitignore for token in required_ignore_tokens))

print("Flux Motion branding/documentation audit")
for item in passes:
    print(f"  PASS: {item}")
for item in errors:
    print(f"  FAIL: {item}")
print(f"RESULT: {'PASS' if not errors else 'FAIL'} ({len(passes)} passed, {len(errors)} failed)")
sys.exit(0 if not errors else 1)
