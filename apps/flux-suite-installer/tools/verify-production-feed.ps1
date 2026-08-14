[CmdletBinding()]
param(
    [string]$PublicBaseUrl = 'https://software.omniatv.com/flux-suite/windows-x64',
    [string]$LocalRoot = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'out\deploy\software.omniatv.com\flux-suite\windows-x64'),
    [string]$VerifierPath = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'out\dist\windows-x64\Flux Installer\Flux Suite.exe')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PublicBaseUrl = $PublicBaseUrl.TrimEnd('/')
$LocalRoot = [IO.Path]::GetFullPath($LocalRoot)
$VerifierPath = [IO.Path]::GetFullPath($VerifierPath)
$TempRoot = Join-Path ([IO.Path]::GetTempPath()) ('flux-production-verify-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TempRoot | Out-Null

try {
    $Nonce = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
    foreach ($Name in @('manifest.json', 'manifest.json.sig')) {
        $Downloaded = Join-Path $TempRoot $Name
        $Uri = $PublicBaseUrl + '/' + $Name + '?deployment=' + $Nonce
        Invoke-WebRequest -UseBasicParsing -Headers @{ 'Cache-Control' = 'no-cache' } `
            -Uri $Uri -OutFile $Downloaded
        $LocalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $LocalRoot $Name)).Hash
        $RemoteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Downloaded).Hash
        if ($LocalHash -cne $RemoteHash) {
            throw "$Name differs from the locally signed deployment file."
        }
    }

    $SignatureCheck = Start-Process -FilePath $VerifierPath `
        -ArgumentList @('--verify-feed', ('"' + (Join-Path $TempRoot 'manifest.json') + '"')) `
        -Wait -PassThru -WindowStyle Hidden
    if ($SignatureCheck.ExitCode -ne 0) {
        throw "Production feed signature verification failed with exit code $($SignatureCheck.ExitCode)."
    }

    $Manifest = Get-Content -Raw -LiteralPath (Join-Path $TempRoot 'manifest.json') | ConvertFrom-Json
    $Artifacts = @(
        [pscustomobject]@{ id = 'installer'; url = $Manifest.installer.url; sha256 = $Manifest.installer.sha256; size = [int64]$Manifest.installer.size },
        [pscustomobject]@{ id = 'bootstrap'; url = $Manifest.bootstrap.url; sha256 = $Manifest.bootstrap.sha256; size = [int64]$Manifest.bootstrap.size }
    )
    $Artifacts += $Manifest.products | ForEach-Object {
        [pscustomobject]@{ id = $_.id; url = $_.url; sha256 = $_.sha256; size = [int64]$_.size }
    }
    foreach ($Product in $Manifest.products) {
        $archiveIndex = 0
        foreach ($Archive in @($Product.archives)) {
            $archiveIndex++
            $Artifacts += [pscustomobject]@{
                id = "$($Product.id)-archive-$archiveIndex"
                url = $Archive.url
                sha256 = $Archive.sha256
                size = [int64]$Archive.size
            }
        }
    }

    $Results = foreach ($Artifact in $Artifacts) {
        $Destination = Join-Path $TempRoot ($Artifact.id + '.download')
        $Uri = $PublicBaseUrl + '/' + $Artifact.url + '?deployment=' + $Nonce
        Write-Host "Downloading and verifying $($Artifact.id)..." -ForegroundColor Cyan
        Invoke-WebRequest -UseBasicParsing -Headers @{ 'Cache-Control' = 'no-cache' } `
            -Uri $Uri -OutFile $Destination
        $File = Get-Item -LiteralPath $Destination
        $Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash.ToLowerInvariant()
        $Valid = $File.Length -eq $Artifact.size -and $Hash -eq $Artifact.sha256
        [pscustomobject]@{
            id = $Artifact.id
            bytes = $File.Length
            sha256 = $Hash
            valid = $Valid
        }
        if (-not $Valid) {
            throw "Production artifact verification failed: $($Artifact.id)"
        }
        Remove-Item -LiteralPath $Destination -Force
    }

    [pscustomobject]@{
        publishedAt = $Manifest.publishedAt
        signatureValid = $true
        manifestMatches = $true
        artifacts = $Results
    } | ConvertTo-Json -Depth 5
} finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
