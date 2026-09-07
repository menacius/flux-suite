#!/usr/bin/env python3
"""Development Version 411: Flux Motion theme, Satoshi UI and About identity."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SHARED_FONTS = ROOT.parents[1] / "packages" / "flux-common" / "resources" / "fonts"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


theme_header = read("Editor/editor-theme.h")
theme_source = read("Editor/editor-theme.cpp")
assets = read("Editor/title-assets.cpp")
main = read("Editor/main.cpp")
dock = read("src/editor/title-dock/dock-lifecycle.inc")
editor = read("src/editor/title-editor/window-session.inc")
preferences = read("src/editor/title-editor/signal-handlers.inc")
locale = read("data/locale/en-US.ini")
svg = read("data/icons/flux-motion-about.svg")
splash = read("Editor/startup-progress-screen.cpp")

assert "EditorTheme::FluxMotion" in theme_source and "FluxMotion" in theme_header
assert "AdobeDark" not in theme_header
assert "EditorTheme::Light" not in theme_source
assert "EditorTheme::System" not in theme_source
assert 'QStringLiteral("flux-motion")' in theme_source
assert theme_source.count("QColor(0x78, 0x38, 0xf5)") >= 2
assert "#7838f5" in theme_source and "#7838f5" in splash
assert "OBSTitles.EditorThemeFluxMotion" in preferences
assert 'OBSTitles.EditorThemeFluxMotion="Flux Motion (Default)"' in locale
assert "OBSTitles.EditorThemeAdobeDark" not in locale
assert "OBSTitles.EditorThemeLight" not in locale
assert "OBSTitles.EditorThemeSystem" not in locale

font_names = [
    "Satoshi-Regular.otf",
    "Satoshi-Italic.otf",
    "Satoshi-Medium.otf",
    "Satoshi-MediumItalic.otf",
    "Satoshi-Bold.otf",
    "Satoshi-BoldItalic.otf",
    "Satoshi-Black.otf",
    "Satoshi-BlackItalic.otf",
]
for name in font_names:
    assert name in assets
    assert (SHARED_FONTS / name).is_file()
assert "application.setFont(fxm_satoshi_ui_font())" in main
assert "fxm_apply_satoshi_ui_font(this);" not in dock
assert "fxm_apply_satoshi_ui_font(this);" in editor

assert 'id="fxm-version"' in svg
assert 'id="fxm-development-version"' in svg
assert 'id="fxm-copyright"' in svg
assert "2026 - v0.8.19-2-alpha" in svg
assert "&#169; 2026 OMNIATV. ALL RIGHTS RESERVED." in svg
assert 'font-family: Satoshi-Medium, Satoshi;' in svg
assert 'transform="translate(284.18 254.71)"' in svg
assert "QStringLiteral(FXM_VERSION_LABEL).toHtmlEscaped()" in editor
assert 'replace_text_content("fxm-development-version", QByteArray())' in editor

print("Development Version 411 Flux Motion identity contract passed")
