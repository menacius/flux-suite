$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $root '..\..'))

$required = @(
    'CMakeLists.txt',
    'VERSION.txt',
    'resources\manifest.json',
    'resources\update-public-key.json',
    'setup\flux-suite.iss',
    'tests\fixtures\signed-feed.json',
    'tests\fixtures\signed-feed.json.sig',
    'resources\icons\flux-suite.svg',
    'resources\icons\flux-encoder.svg',
    'resources\icons\flux-motion.svg',
    'resources\icons\flux-motion-obs.svg',
    'resources\icons\mime-flux-motion-title-graphics.svg',
    'resources\icons\mime-flux-motion-project.svg',
    'resources\icons\mime-flux-encoder-queue.svg',
    'resources\icons\flux-suite.ico',
    'resources\icons\flux-suite-wizard.png',
    'src\main.cpp',
    'src\fileassociations.h',
    'src\fileassociations.cpp',
    'src\installengine.cpp',
    'src\installerwindow.cpp',
    'src\selfupdater.cpp',
    'src\update-security.cpp',
    'src\network-utils.cpp'
)
foreach ($relative in $required) {
    $path = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing required source: $relative"
    }
}

foreach ($font in @('Satoshi-Regular.otf', 'Satoshi-Medium.otf',
                     'Satoshi-Bold.otf', 'Satoshi-Black.otf',
                     'Satoshi-LICENSE.txt')) {
    $fontPath = Join-Path $repositoryRoot "packages\flux-common\resources\fonts\$font"
    if (-not (Test-Path -LiteralPath $fontPath -PathType Leaf)) {
        throw "Missing shared font asset: $font"
    }
}

$manifest = Get-Content -Raw -LiteralPath (Join-Path $root 'resources\manifest.json') | ConvertFrom-Json
if ($manifest.schema -ne 2) { throw 'Unexpected manifest schema.' }
if ($manifest.products.Count -ne 3) { throw 'Expected exactly three Flux products.' }
foreach ($product in $manifest.products) {
    if (-not $product.id -or -not $product.version -or -not $product.sha256 -or -not $product.icon) {
        throw "Incomplete product manifest entry: $($product.name)"
    }
    if ($product.sha256 -notmatch '^[0-9a-f]{64}$') {
        throw "Invalid SHA-256 for $($product.name)"
    }
}

$publicKey = Get-Content -Raw -LiteralPath (Join-Path $root 'resources\update-public-key.json') | ConvertFrom-Json
if ($publicKey.algorithm -ne 'ECDSA-P256-SHA256' -or -not $publicKey.keyId) {
    throw 'Invalid update signing trust anchor.'
}
if ([Convert]::FromBase64String($publicKey.x).Length -ne 32 -or
    [Convert]::FromBase64String($publicKey.y).Length -ne 32) {
    throw 'Update public key must contain P-256 coordinates.'
}
if ((Get-Content -Raw -LiteralPath (Join-Path $root 'src\installerwindow.cpp')) -match 'class HeroArt') {
    throw 'The retired circles hero artwork is still present.'
}
$installerWindow = Get-Content -Raw -LiteralPath (Join-Path $root 'src\installerwindow.cpp')
$installEngine = Get-Content -Raw -LiteralPath (Join-Path $root 'src\installengine.cpp')
$manifestSource = Get-Content -Raw -LiteralPath (Join-Path $root 'src\manifest.cpp')
foreach ($contract in @('Uninstall…', 'Update Flux Suite')) {
    if ($installerWindow -notmatch [regex]::Escape($contract)) { throw "Missing UI contract: $contract" }
}
foreach ($retiredContract in @('Uninstall + archives', 'Restore archived version')) {
    if ($installerWindow -match [regex]::Escape($retiredContract)) {
        throw "Retired local archive UI is still present: $retiredContract"
    }
}
if ($installerWindow -notmatch 'uninstall && selected == uninstall') {
    throw 'Closing the product menu can still trigger a null uninstall action.'
}

$AssociationSource = Get-Content -Raw -LiteralPath (Join-Path $root 'src\fileassociations.cpp')
foreach ($extension in @('.fxmt', '.fxmp', '.fxmproj', '.fxe')) {
    if (-not $AssociationSource.Contains("QStringLiteral(`"$extension`")")) {
        throw "Missing Windows file association contract for $extension."
    }
}
foreach ($mimeIcon in @(
    'mime-flux-motion-title-graphics.svg',
    'mime-flux-motion-project.svg',
    'mime-flux-encoder-queue.svg'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $root "resources\icons\$mimeIcon"))) {
        throw "Missing file association icon: $mimeIcon"
    }
}
if ($installerWindow -notmatch 'installedPackageHash' -or $installerWindow -notmatch 'sameBuild') {
    throw 'Same-version package hash update detection is missing.'
}
if ($installerWindow -notmatch 'sameVersionRebuild' -or
    $installerWindow -notmatch 'applicationFilePath') {
    throw 'Same-version installer hash update detection is missing.'
}
$selfUpdaterSource = Get-Content -Raw -LiteralPath (Join-Path $root 'src\selfupdater.cpp')
if ($selfUpdaterSource -notmatch 'Flux Suite Setup\.exe' -or
    $selfUpdaterSource -notmatch '/UPDATE=1' -or
    $selfUpdaterSource -match 'QStringLiteral\("Flux Suite\.update\.exe"\)') {
    throw 'Self-update must launch the complete signed Flux Suite Setup package.'
}
foreach ($contract in @('performUninstall', 'removeTree(backup)')) {
    if ($installEngine -notmatch [regex]::Escape($contract)) { throw "Missing install engine contract: $contract" }
}
if ($installEngine -match 'performRestore|archiveRootForProduct' -or
    (Get-Content -Raw -LiteralPath (Join-Path $root 'src\selfupdater.cpp')) -match 'Installer Archive') {
    throw 'Local application or installer archive support is still present.'
}
if ($AssociationSource -notmatch 'Qt::KeepAspectRatio') {
    throw 'MIME icon rendering does not preserve the supplied artwork aspect ratio.'
}
if ($manifestSource -notmatch 'verifyFeedSignature' -or $manifestSource -notmatch 'expiresAt') {
    throw 'Signed feed verification/expiry contract is missing.'
}
if ($manifestSource -notmatch 'QFileInfo\(source\)\.isAbsolute') {
    throw 'Absolute Windows manifest path handling is missing.'
}
$setupSource = Get-Content -Raw -LiteralPath (Join-Path $root 'setup\flux-suite.iss')
foreach ($contract in @('PrivilegesRequired=lowest', 'UninstallDisplayName=Flux Suite',
                         'DefaultDirName={autopf}\Flux Suite',
                         'PrivilegesRequiredOverridesAllowed=dialog commandline',
                         'WizardStyle=modern dark includetitlebar hidebevels',
                         'WizardSmallImageFile=..\resources\icons\flux-suite-wizard.png',
                         'PrivilegesRequiredOverrideAllUsers=Install for &all users',
                         "ExtractTemporaryFile('flux-suite.ico')",
                         'AppUpdatesURL=https://software.omniatv.com',
                         'FluxBackground', 'Update Flux Suite')) {
    if ($setupSource -notmatch [regex]::Escape($contract)) { throw "Missing first-run setup contract: $contract" }
}
$publisherSource = Get-Content -Raw -LiteralPath (Join-Path $root 'tools\publish-update-feed.ps1')
if ($publisherSource -notmatch "Publish-CurrentArtifact 'bootstrap'" -or
    $publisherSource -notmatch "packages/bootstrap/current.exe" -or
    $publisherSource -notmatch 'applicationSha256') {
    throw 'First-run setup publishing contract is missing.'
}

Write-Host 'Flux Suite Installer source validation passed.' -ForegroundColor Green
