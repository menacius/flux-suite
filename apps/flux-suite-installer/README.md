# Flux Suite Installer

A native Windows installer, downloader and updater for Flux Encoder, Flux Motion,
and Flux Motion Plugin for OBS. The interface is a compact creative-suite
dashboard with per-product state, install/update actions and a shared queue.
The supplied Flux Suite and product SVG artwork is embedded in the application,
along with Satoshi Regular, Medium, Bold and Black for consistent typography.

## First download

New users download the single first-run setup executable from:

```text
https://software.omniatv.com/flux-suite/windows-x64/packages/bootstrap/current.exe
```

`Flux Suite Setup` asks whether Flux Suite should be installed for the current
user or system-wide for everyone on the computer. System-wide setup requests
administrator approval through UAC. It creates the matching Start Menu entry,
offers an optional Desktop shortcut, registers a standard Windows uninstaller
and starts Flux Suite. Managed deployments can select the scope through Inno
Setup's `/ALLUSERS` or `/CURRENTUSER` switches. Updates remember the existing
installation scope. After this first installation,
`Flux Suite.exe` updates through the signed, self-contained Setup package, so
the updater always carries the complete Qt runtime and preserves the same
installation scope and visual language as first-run Setup.

## Build

```powershell
.\build-windows.ps1 -Clean
```

The script uses the Qt 6.8.3 MSVC SDK already provisioned by Flux Encoder,
builds outside the source tree, deploys the Qt runtime and writes the final
application and first-run setup to:

```text
..\Flux Suite\Dist\windows-x64\Flux Installer\
..\Flux Suite\Dist\windows-x64\Flux Installer Setup\
```

## Secure update service

The deployed `manifest.json` remains an offline fallback. On startup and whenever
**Check for updates** is selected, the installer checks the production feed:

```text
https://software.omniatv.com/flux-suite/windows-x64/manifest.json
```

Remote catalogs are accepted only from that HTTPS origin and only when the
detached `manifest.json.sig` ECDSA P-256 signature validates against the public
key embedded in the installer. Same-origin redirects, catalog expiry, monotonic
publication timestamps, response size limits, exact package sizes and SHA-256
checks prevent downgrade, redirect and package-substitution attacks. A failed
network or trust check leaves the last bundled/verified catalog active.

Initialize the feed signing identity once (the private key is DPAPI-protected
outside the repository), then publish a signed server tree:

```powershell
.\tools\initialize-update-signing-key.ps1
.\tools\publish-update-feed.ps1
```

The publisher writes to
`..\Flux Suite\Deploy\software.omniatv.com\flux-suite\windows-x64` by default.
Deploy the contents of that directory at the matching path on the server. Keep
the signing key backed up securely; for production operations, a certificate
vault or HSM is preferable.

## Self-update and uninstall

The signed catalog has a separate installer release. When it is newer, Flux
Suite downloads and verifies the new executable, launches it as an isolated
handoff helper, waits for the old process to exit, atomically replaces it and
restarts.

Signed installer rebuilds published under the same version are also detected by
SHA-256, allowing a repair release to reach existing installations without a
version bump.

Product updates use a temporary rollback directory only while activation is in
progress and remove it after success. No application or installer archives are
retained locally. Older signed releases remain available from the server-side
catalog when needed.

Installed products expose **Uninstall…**. Uninstall validates the Flux
receipt/product before removing files, file associations and Start Menu
shortcuts.

Installing Flux Motion registers `.fxmt` and `.fxmp` title/graphic files plus
`.fxmproj` projects, with their supplied document artwork and Flux Motion as the
open command. Installing Flux Encoder similarly registers `.fxe` queue files.
The associations and vendor MIME content types follow the selected per-user or
system-wide install scope and are removed with the owning application.

Applications can install per-user under `%LOCALAPPDATA%\Flux Suite` or
system-wide under `%ProgramFiles%\Flux Suite`. The OBS plugin installs in OBS
Studio's recommended `%ProgramData%\obs-studio\plugins\flux-motion` structure.
System-wide writes request administrator approval through UAC. Scope and the
current-user destination can be changed from the gear button.

Updates are extracted into an adjacent staging directory, verified, and then
atomically exchanged with the installed version. If activation fails, the
previous installation is restored; successful updates remove the temporary
rollback copy.

## Diagnostic commands

```powershell
& '.\Flux Suite.exe' --validate-manifest
& '.\Flux Suite.exe' --verify-packages
& '.\Flux Suite.exe' --verify-feed .\tests\fixtures\signed-feed.json
& '.\Flux Suite.exe' --screenshot .\flux-suite.png
```
