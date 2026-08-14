#!/usr/bin/env python3
"""Dev337: separate selective packed-title container infrastructure."""

from pathlib import Path
import json
import re


ROOT = Path(__file__).resolve().parents[1]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


cmake = read("CMakeLists.txt")
build = read("src/core/build-info.h")
header = read("Core/packed-title-format.h")
container = read("Core/packed-title-format.cpp")
model = read("Core/title-data.h")
serialization = read("src/core/title-data.cpp")
dock = read("src/editor/title-dock/title-actions.inc")
editor = read("src/editor/title-editor/playback-cache-preferences.inc")
locale = read("data/locale/en-US.ini")
manifest = json.loads(read("tests/test-suite-manifest.json"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 337
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 337
assert manifest["development_version"] >= 337

for path in ("Core/packed-title-format.cpp", "Core/packed-title-format.h"):
    assert path in cmake
assert "FXMPACK1" in container.replace("', '", "")
for token in (
    "kContainerVersion = 1",
    "LZ4_compress_default",
    "LZ4_decompress_safe",
    "QCryptographicHash::Sha256",
    "QSaveFile",
    "safe_archive_path",
    "Packed resource integrity check failed",
):
    assert token in container

for token in (
    "struct PackedTitleExportOptions",
    "pack_images = true",
    "pack_media = true",
    "pack_fonts = true",
    "export_packed_title",
):
    assert token in model

for token in (
    '"packed://"',
    'QStringLiteral("images")',
    'QStringLiteral("media")',
    'QStringLiteral("fonts")',
    "packed_font_files",
    "QFontDatabase::addApplicationFont",
    "resolve_packed_uri_json",
    "has_packed_signature",
):
    assert token in serialization

assert 'export_title(' in serialization  # legacy JSON path remains present
assert 'QStringLiteral("fxmp")' in dock
assert "prompt_packed_title_options" in dock
assert "prompt_editor_packed_title_options" in editor
for key in ("OBSTitles.PackImages", "OBSTitles.PackMedia", "OBSTitles.PackFonts"):
    assert key in locale

test_name = "tests/development_version_337_packed_title_format_contract_test.py"
for area in ("serialization_migration", "editor_gui", "platform_build"):
    assert test_name in manifest["areas"][area]["python"]
assert "packed_title_format_test" in manifest["areas"]["serialization_migration"]["native"]

print("Dev337 packed title format contract passed")
