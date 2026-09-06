"""Welcome overlay and first-install prerender default regression contract."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


header = read("Editor/standalone-welcome-screen.h")
welcome = read("Editor/standalone-welcome-screen.cpp")
preferences = read("src/core/title-preferences.cpp")
cache = read("src/cache/cache-manager.h")
cmake = read("CMakeLists.txt")
version = read("VERSION.txt").strip()

# The welcome surface is an owned child overlay, not the central stacked page
# and not an independent native window.
assert "QWidget *home_backdrop_" in header
assert "QWidget *home_" in header
assert "void layoutWelcomePopup();" in header
assert "home_->setParent(pages_);" in welcome
assert "home_->raise();" in welcome
assert "pages_->setCurrentWidget(home_backdrop_);" in welcome
assert "pages_->addWidget(home);" not in welcome
assert "Qt::Dialog" not in welcome
assert "FluxMotionWelcomePopup" in welcome
assert "QGraphicsDropShadowEffect" in welcome
assert "welcomeBrandLogo" in welcome
assert "fxm_brand_icon().pixmap(QSize(48, 48))" in welcome
assert "Flux Motion logo" in welcome

# The visual vocabulary intentionally matches Flux Installer surfaces.
for token in ("#141316", "#1a191d", "#7255f5", "#8369f8", "#554a87"):
    assert token in welcome

# Missing settings mean a first install, while an explicitly stored false is
# still returned by QSettings and remains respected.
assert "kCacheEnabledKey), true).toBool()" in preferences
assert "std::atomic_bool cache_enabled_{true}" in cache

# This delivery explicitly keeps the existing public/development version.
assert "project(flux-motion VERSION 0.8.19)" in cmake
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "420")' in cmake
assert version == "2026 - v0.8.19-1-alpha"

print("Welcome popup and first-install prerender default contract passed")
