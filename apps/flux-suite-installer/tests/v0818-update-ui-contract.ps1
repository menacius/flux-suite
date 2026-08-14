$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$window = Get-Content -Raw -LiteralPath (Join-Path $root 'src\installerwindow.cpp')
$manifest = Get-Content -Raw -LiteralPath (Join-Path $root 'resources\manifest.json') | ConvertFrom-Json

foreach ($text in @('Update All', 'Queued to update', 'Currently updating',
                     'Successfully updated', 'Failed to update', 'Latest changelog')) {
    if (-not $window.Contains($text)) { throw "Missing update UI contract: $text" }
}
if (-not $window.Contains('card.changelog->setVisible(!product.changelog.isEmpty());')) {
    throw 'Latest changelog link must remain visible after install and update state changes.'
}
if ($manifest.suiteVersion -ne '2026 - v0.8.19-alpha') {
    throw "Stale suite version: $($manifest.suiteVersion)"
}
$expectedVersions = @{
    encoder = '2026 - v0.8.18-alpha'
    'motion-editor' = '2026 - v0.8.19-alpha'
    'motion-obs' = '2026 - v0.8.19-alpha'
}
foreach ($product in $manifest.products) {
    if ($product.version -ne $expectedVersions[$product.id]) { throw "Stale product version: $($product.id)" }
    if (-not $product.changelog -or $product.changelog.entries.Count -lt 1) {
        throw "Missing changelog: $($product.id)"
    }
}
Write-Host 'v0.8.19 update UI contract passed.' -ForegroundColor Green
