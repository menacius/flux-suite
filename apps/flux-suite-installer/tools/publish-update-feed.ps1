[CmdletBinding()]
param(
    [string]$DistRoot = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'out\dist\windows-x64'),
    [string]$OutputRoot = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'out\deploy\software.omniatv.com\flux-suite\windows-x64'),
    [string]$KeyPath = (Join-Path $env:LOCALAPPDATA 'FluxSuiteBuild\Signing\flux-update-key.dpapi'),
    [int]$ValidityDays = 45
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SourceRoot = Split-Path $PSScriptRoot -Parent
$DistRoot = [IO.Path]::GetFullPath($DistRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$OfflineManifestPath = Join-Path $DistRoot 'Flux Installer\manifest.json'
$InstallerPath = Join-Path $DistRoot 'Flux Installer\Flux Suite.exe'
$InstallerVersion = (Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'VERSION.txt')).Trim()
$SetupFileName = 'Flux_Suite_Setup_' + ($InstallerVersion -replace '[\\/:*?"<>|\s]+', '_') + '_windows-x64.exe'
$SetupPath = Join-Path $DistRoot "Flux Installer Setup\$SetupFileName"
if (-not (Test-Path -LiteralPath $OfflineManifestPath)) { throw "Offline manifest missing: $OfflineManifestPath" }
if (-not (Test-Path -LiteralPath $InstallerPath)) { throw "Installer missing: $InstallerPath" }
if (-not (Test-Path -LiteralPath $SetupPath)) { throw "First-run setup missing: $SetupPath" }

function ConvertTo-SafeVersion([string]$Version) {
    return (($Version -replace '[^A-Za-z0-9._-]', '_').Trim('_'))
}

function Get-VersionIdentity([string]$Version) {
    $Match = [regex]::Match($Version, '(?i)v?(\d+\.\d+\.\d+(?:-[0-9a-z.-]+)?)')
    if ($Match.Success) { return $Match.Groups[1].Value.ToLowerInvariant() }
    return $Version.Trim().ToLowerInvariant()
}

function Get-FileMetadata([string]$Path) {
    $File = Get-Item -LiteralPath $Path
    return [ordered]@{
        size = $File.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    }
}

function Publish-CurrentArtifact(
    [string]$Id,
    [string]$Version,
    [string]$Source,
    [string]$Extension,
    [string]$PackageRoot = ''
) {
    $CurrentDirectory = Join-Path $OutputRoot "packages\$Id"
    $ArchiveDirectory = Join-Path $OutputRoot "archive\$Id"
    New-Item -ItemType Directory -Force -Path $CurrentDirectory,$ArchiveDirectory | Out-Null
    $CurrentArtifact = Join-Path $CurrentDirectory "current$Extension"
    $CurrentMetadata = Join-Path $CurrentDirectory 'current.metadata.json'
    $NewMetadata = Get-FileMetadata $Source

    if ((Test-Path -LiteralPath $CurrentArtifact) -and (Test-Path -LiteralPath $CurrentMetadata)) {
        $Previous = Get-Content -Raw -LiteralPath $CurrentMetadata | ConvertFrom-Json
        $PreviousHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $CurrentArtifact).Hash.ToLowerInvariant()
        if ((Get-VersionIdentity ([string]$Previous.version)) -ne (Get-VersionIdentity $Version)) {
            $ArchiveName = "$(ConvertTo-SafeVersion $Previous.version)-$($PreviousHash.Substring(0,12))"
            Move-Item -Force -LiteralPath $CurrentArtifact -Destination (Join-Path $ArchiveDirectory "$ArchiveName$Extension")
            Move-Item -Force -LiteralPath $CurrentMetadata -Destination (Join-Path $ArchiveDirectory "$ArchiveName.metadata.json")
        }
    }

    Copy-Item -Force -LiteralPath $Source -Destination $CurrentArtifact
    $Record = [ordered]@{
        version = $Version
        sha256 = $NewMetadata.sha256
        size = $NewMetadata.size
        packageRoot = $PackageRoot
    }
    $Record | ConvertTo-Json | Set-Content -LiteralPath $CurrentMetadata -Encoding utf8

    $Archives = @()
    Get-ChildItem -LiteralPath $ArchiveDirectory -Filter '*.metadata.json' -File | Sort-Object LastWriteTime -Descending | ForEach-Object {
        $Metadata = Get-Content -Raw -LiteralPath $_.FullName | ConvertFrom-Json
        $BaseName = $_.Name.Substring(0, $_.Name.Length - '.metadata.json'.Length)
        $Artifact = Join-Path $ArchiveDirectory "$BaseName$Extension"
        if (Test-Path -LiteralPath $Artifact) {
            $Archives += [ordered]@{
                version = $Metadata.version
                url = "archive/$Id/$BaseName$Extension"
                sha256 = $Metadata.sha256
                size = [int64]$Metadata.size
                packageRoot = $Metadata.packageRoot
            }
        }
    }
    return [ordered]@{ current = $Record; archives = $Archives }
}

$Offline = Get-Content -Raw -LiteralPath $OfflineManifestPath | ConvertFrom-Json
$Products = @()
foreach ($Product in $Offline.products) {
    $PackagePath = [IO.Path]::GetFullPath((Join-Path (Split-Path $OfflineManifestPath -Parent) $Product.package))
    if (-not (Test-Path -LiteralPath $PackagePath)) { throw "Product package missing: $PackagePath" }
    $Published = Publish-CurrentArtifact $Product.id $Product.version $PackagePath '.zip' $Product.packageRoot
    $Products += [ordered]@{
        id = $Product.id
        name = $Product.name
        tagline = $Product.tagline
        description = $Product.description
        version = $Product.version
        accent = $Product.accent
        glyph = $Product.glyph
        icon = $Product.icon
        url = "packages/$($Product.id)/current.zip"
        sha256 = $Published.current.sha256
        size = [int64]$Published.current.size
        installFolder = $Product.installFolder
        executable = $Product.executable
        packageRoot = $Product.packageRoot
        kind = $Product.kind
        archiveLimit = if ($null -ne $Product.archiveLimit) { [int]$Product.archiveLimit } else { 3 }
        changelog = if ($null -ne $Product.changelog) {
            [ordered]@{
                fromVersion = $Product.changelog.fromVersion
                version = $Product.changelog.version
                entries = @($Product.changelog.entries)
            }
        } else { $null }
        archives = @($Published.archives)
    }
}

$PublishedInstaller = Publish-CurrentArtifact 'installer' $InstallerVersion $InstallerPath '.exe'
$PublishedBootstrap = Publish-CurrentArtifact 'bootstrap' $InstallerVersion $SetupPath '.exe'
$Now = [DateTimeOffset]::UtcNow
$Feed = [ordered]@{
    schema = 2
    suiteVersion = $Offline.suiteVersion
    channel = $Offline.channel
    publishedAt = $Now.ToString('o')
    expiresAt = $Now.AddDays($ValidityDays).ToString('o')
    installer = [ordered]@{
        version = $InstallerVersion
        # Existing Flux Suite builds understand only this entry. Point it at
        # the self-contained Setup so their updater no longer launches a bare
        # Qt executable without its runtime DLLs.
        url = 'packages/bootstrap/current.exe'
        sha256 = $PublishedBootstrap.current.sha256
        size = [int64]$PublishedBootstrap.current.size
        applicationSha256 = $PublishedInstaller.current.sha256
        applicationSize = [int64]$PublishedInstaller.current.size
    }
    bootstrap = [ordered]@{
        version = $InstallerVersion
        url = 'packages/bootstrap/current.exe'
        sha256 = $PublishedBootstrap.current.sha256
        size = [int64]$PublishedBootstrap.current.size
    }
    products = $Products
}

New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
$ManifestPath = Join-Path $OutputRoot 'manifest.json'
$Feed | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ManifestPath -Encoding utf8
& (Join-Path $PSScriptRoot 'sign-update-feed.ps1') -ManifestPath $ManifestPath -KeyPath $KeyPath

Write-Host "Signed software.omniatv.com deployment tree ready: $OutputRoot" -ForegroundColor Green
