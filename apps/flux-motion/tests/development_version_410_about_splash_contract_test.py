#!/usr/bin/env python3
"""Development Version 410: the About artwork is the Editor splash screen."""

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
header = read("src/editor/title-editor.h")
window = read("src/editor/title-editor/window-session.inc")
main = read("Editor/main.cpp")
manifest = read("tests/test-suite-manifest.json")

cmake_version = re.search(r'set\(OBS_FXM_DEVELOPMENT_VERSION "(\d+)"\)', cmake)
build_version = re.search(r'#define FXM_DEVELOPMENT_VERSION "(\d+)"', build)
assert cmake_version and int(cmake_version.group(1)) >= 410
assert build_version and int(build_version.group(1)) >= 410
assert json.loads(manifest)["development_version"] >= 410

assert "QPixmap fxm_about_graphic_pixmap" in header
assert 'fxm_asset_path("icons/flux-motion-about.svg")' in window
assert "QStringLiteral(FXM_VERSION_LABEL).toHtmlEscaped()" in window
assert "fxm_about_graphic_pixmap(" in window

assert "std::unique_ptr<QSplashScreen> splash" in main
assert "fxm_about_graphic_pixmap(QSize(760, 292), dpr)" in main
assert "splash->show()" in main
assert "application.processEvents(QEventLoop::ExcludeUserInputEvents)" in main
assert "splash->finish(editor.get())" in main
assert "parser.isSet(graphics_smoke_option)" in main

print("Development Version 410 About splash contract passed")
