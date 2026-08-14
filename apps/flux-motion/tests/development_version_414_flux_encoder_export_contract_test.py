#!/usr/bin/env python3
"""Dev414: Flux Encoder owns media export through stable local IPC."""

from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
build = (ROOT / "src/core/build-info.h").read_text(encoding="utf-8")
header = (ROOT / "../../packages/flux-common/Shared/flux-encoder-client.h").read_text(encoding="utf-8")
client = (ROOT / "../../packages/flux-common/Shared/flux-encoder-client.cpp").read_text(encoding="utf-8")
editor_header = (ROOT / "src/editor/title-editor.h").read_text(encoding="utf-8")
menu = (ROOT / "src/editor/title-editor/panels-colors.inc").read_text(encoding="utf-8")
workflow = (ROOT / "src/editor/title-editor/playback-cache-preferences.inc").read_text(encoding="utf-8")
locale = (ROOT / "data/locale/en-US.ini").read_text(encoding="utf-8")
protocol = (ROOT / "docs/FLUX-ENCODER-IPC.md").read_text(encoding="utf-8")
manifest = json.loads((ROOT / "tests/test-suite-manifest.json").read_text(encoding="utf-8"))

assert int(re.search(r'OBS_FXM_DEVELOPMENT_VERSION "(\d+)"', cmake).group(1)) >= 414
assert int(re.search(r'FXM_DEVELOPMENT_VERSION "(\d+)"', build).group(1)) >= 414
assert manifest["development_version"] >= 414

for token in [
    "../../packages/flux-common/Shared/flux-encoder-client.cpp",
    "../../packages/flux-common/Shared/flux-encoder-client.h",
]:
    assert token in cmake, token

for token in [
    'kProtocolName = "com.fluxsuite.encoder.ipc"',
    "kProtocolVersion = 1",
    'kLocalServerName =',
    "struct ExportRequest",
    "refreshAvailability()",
    "openExport(const ExportRequest &request",
    "buildOpenExportRequest",
]:
    assert token in header, token

for token in [
    "QLocalSocket",
    'QStringLiteral("export.open")',
    'QStringLiteral("project_id")',
    'QStringLiteral("project_store_path")',
    'QStringLiteral("project_scope")',
    'qEnvironmentVariable("FLUX_ENCODER_EXECUTABLE")',
    'QStringLiteral("installation/executable")',
    'QStringLiteral("--single-instance")',
    'QStringLiteral("--ipc-endpoint")',
    "QProcess::startDetached",
    "SendResult::NoService",
    "response_error(response)",
]:
    assert token in client, token

# The existing endpoint is tried before process launch, enforcing reuse.
assert client.index("send_request(envelope") < client.index("QProcess::startDetached")

for token in [
    "void export_as_media();",
    "void refresh_flux_encoder_export_action();",
    "QAction         *act_export_media_",
]:
    assert token in editor_header, token

for token in [
    'fxm_tr("OBSTitles.ExportAsMedia")',
    "FileCommand::ExportMedia",
    "&QMenu::aboutToShow",
    "refresh_flux_encoder_export_action();",
]:
    assert token in menu, token

for token in [
    "encoder_available && title_",
    '"OBSTitles.ExportAsMediaRequiresEncoder"',
    "!save_title())",
    "TitleDataStore::instance().save();",
    "request.project_id",
    "provider->config_path(\"\")",
    "FluxEncoderClient::instance().openExport",
    "QMessageBox::critical",
]:
    assert token in workflow, token

assert workflow.index("!save_title())") < workflow.index(
    "FluxEncoderClient::instance().openExport"
)

for key in [
    "OBSTitles.ExportAsMedia=",
    "OBSTitles.ExportAsMediaRequiresEncoder=",
    "OBSTitles.FluxEncoderRequiredTooltip=",
    "OBSTitles.FluxEncoderLaunchFailedFormat=",
]:
    assert key in locale, key

for token in [
    "com.fluxsuite.encoder.ipc.v1",
    '"command": "export.open"',
    '"ok": true',
    "queue.add",
    "batch.add",
]:
    assert token in protocol, token

print("Dev414 Flux Encoder media-export contract passed")
