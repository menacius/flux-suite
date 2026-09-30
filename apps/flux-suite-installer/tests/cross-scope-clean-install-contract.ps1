$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$engine = Get-Content -Raw -LiteralPath (Join-Path $root 'src\installengine.cpp')
$window = Get-Content -Raw -LiteralPath (Join-Path $root 'src\installerwindow.cpp')
$setup = Get-Content -Raw -LiteralPath (Join-Path $root 'setup\flux-suite.iss')

foreach ($token in @('installationCandidates', 'ProgramFiles', 'LOCALAPPDATA',
                      'ProgramData', 'APPDATA', 'broadcast-graphics-live',
                      'safelyIdentifiesInstallation')) {
    if (-not $engine.Contains($token)) { throw "Install cleanup contract missing: $token" }
}
foreach ($token in @('installedPathsFor', 'installedPathFor', 'duplicateInstall',
                      'oldSystemInstall')) {
    if (-not $window.Contains($token)) { throw "Install discovery contract missing: $token" }
}
foreach ($token in @('PrepareToInstall', 'RemovePreviousSuite', 'HKCU', 'HKLM64',
                      '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART')) {
    if (-not $setup.Contains($token)) { throw "Suite bootstrap cleanup contract missing: $token" }
}
Write-Host 'Cross-scope clean installation contract passed.' -ForegroundColor Green

