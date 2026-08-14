[CmdletBinding()]
param(
    [string]$Distro = "Ubuntu-24.04",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "RelWithDebInfo",
    [switch]$Clean,
    [string]$QtVersion = "6.8.3"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Invoke-Native {
    param([string]$FilePath, [string[]]$ArgumentList)
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($ArgumentList -join ' ')"
    }
}

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "WSL is not installed. Enable Windows Subsystem for Linux and install an Ubuntu LTS distribution first."
}

$installed = @(& wsl.exe --list --quiet | ForEach-Object { ($_ -replace "`0", "").Trim() } | Where-Object { $_ })
if ($installed -notcontains $Distro) {
    Write-Host "Ubuntu LTS distribution '$Distro' is not installed." -ForegroundColor Yellow
    Write-Host "Starting WSL installation. Windows may require a restart before this script can continue."
    Invoke-Native "wsl.exe" @("--install", "-d", $Distro)
    throw "Finish the WSL/Ubuntu first-run setup, then execute this script again."
}

$sourceWindows = (Resolve-Path $PSScriptRoot).Path
$sourceWsl = (& wsl.exe -d $Distro -- wslpath -a $sourceWindows).Trim()
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($sourceWsl)) {
    throw "Could not convert the source path to a WSL path: $sourceWindows"
}

$environment = @(
    "FLUX_ENCODER_BUILD_CONFIG=$Configuration",
    "FLUX_ENCODER_QT_VERSION=$QtVersion",
    "FLUX_ENCODER_CLEAN_BUILD=$(if ($Clean) { '1' } else { '0' })"
)
$command = "$(($environment -join ' ')) bash '$sourceWsl/tools/build-wsl-ubuntu-lts.sh' '$sourceWsl'"
Write-Host "Building Flux Encoder in $Distro..." -ForegroundColor Cyan
Invoke-Native "wsl.exe" @("-d", $Distro, "--", "bash", "-lc", $command)
