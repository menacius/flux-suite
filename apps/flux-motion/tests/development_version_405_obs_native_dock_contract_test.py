#!/usr/bin/env python3
"""Dev405: OBS-native dock icons, host style metrics, and version contract."""

from pathlib import Path
import json


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
plugin = read("../../plugins/obs/flux-motion/src/plugin-main.h")
helpers = read("src/editor/title-dock/template-library-helpers.inc")
dock = read("src/editor/title-dock/dock-lifecycle.inc")
list_icons = read("src/editor/title-dock/import-export-helpers.inc")
header = read("src/editor/title-dock.h")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert "project(flux-motion VERSION 0.8.15)" in cmake
assert 'set(OBS_FXM_DEVELOPMENT_VERSION "405")' in cmake
assert '#define PLUGIN_VERSION "0.8.15-alpha"' in build
assert '#define FXM_DEVELOPMENT_VERSION "405"' in build
assert '#define PLUGIN_VERSION "0.8.15-alpha"' in plugin
assert manifest["development_version"] == 405

for native_mapping in (
    '{"add.svg", ":/res/images/plus.svg"}',
    '{"delete.svg", ":/res/images/trash.svg"}',
    '{"move-up.svg", ":/res/images/up.svg"}',
    '{"move-down.svg", ":/res/images/down.svg"}',
    '{"settings.svg", ":/res/images/settings/general.svg"}',
    '{"play.svg", ":/res/images/media/media_play.svg"}',
):
    assert native_mapping in helpers

for semantic_class in (
    '{"add.svg", "icon-plus"}',
    '{"delete.svg", "icon-trash"}',
    '{"move-up.svg", "icon-up"}',
    '{"move-down.svg", "icon-down"}',
    '{"settings.svg", "icon-gear"}',
    '{"play.svg", "icon-media-play"}',
):
    assert semantic_class in helpers

for non_action_icon in ("text.svg", "image.svg", "layer-visible.svg", "globe.svg"):
    assert f'{{"{non_action_icon}", ":/res/' not in helpers
assert "return fxm_icon(icon_name);" in list_icons
assert "return fxm_icon(file_name);" in helpers
assert "static QIcon obs_action_icon" in helpers
assert "static QSize obs_native_dock_icon_size" in helpers
assert 'QStringLiteral("scenesToolbar")' in helpers
assert 'QStringLiteral("sourcesToolbar")' in helpers
assert "return QSize(16, 16);" in helpers
assert '"QToolButton { icon-size: %1px; }"' in helpers
assert "apply_obs_native_toolbar_icon_size(toolbar)" in dock
assert "qobject_cast<QToolBar *>(watched)" in dock
assert "btn_external_refresh_->setMinimumWidth" not in dock
assert "btn_persistence_settings_->setMinimumWidth" not in dock

for style_metric in (
    "QStyle::PM_SmallIconSize",
    "QStyle::PM_ButtonMargin",
    "QStyle::PM_LayoutVerticalSpacing",
    "QStyle::PM_ToolBarItemMargin",
    "QStyle::PM_ToolBarFrameWidth",
):
    assert style_metric in helpers

for action in (
    'fxm_tr("OBSTitles.Add"), "add.svg"',
    'fxm_tr("OBSTitles.Delete"), "delete.svg"',
    'fxm_tr("OBSTitles.MoveUp"), "move-up.svg"',
    'fxm_tr("OBSTitles.MoveDown"), "move-down.svg"',
    'fxm_tr("OBSTitles.Settings"),',
):
    assert action in dock

assert "void changeEvent(QEvent *event) override;" in header
for change_event in (
    "QEvent::StyleChange",
    "QEvent::FontChange",
    "QEvent::PaletteChange",
    "QEvent::ApplicationFontChange",
    "QEvent::ApplicationPaletteChange",
):
    assert change_event in dock

print("Dev405 OBS-native dock and version contract passed")
