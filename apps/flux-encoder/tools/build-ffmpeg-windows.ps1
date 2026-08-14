[CmdletBinding()]
param(
    [string]$OutputDir = '',
    [string]$DependenciesDir = '',
    [string]$FfmpegRef = 'n8.0',
    [string]$NvCodecHeadersRef = 'n13.0.19.0',
    [switch]$Clean,
    [switch]$NoDownload
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ($env:OS -ne 'Windows_NT') {
    throw 'This script must be run on Windows.'
}

$SourceDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($DependenciesDir)) {
    $DependenciesDir = Join-Path $SourceDir '.deps\windows\ffmpeg-build'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $SourceDir '.deps\windows\ffmpeg-custom\bin'
}
$DependenciesDir = [System.IO.Path]::GetFullPath($DependenciesDir)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$MsysRoot = Join-Path $DependenciesDir 'msys64'
$BuildRoot = Join-Path $DependenciesDir 'work'
$BootstrapScript = Join-Path $SourceDir 'tools\build-ffmpeg-msys2.sh'
$Headers = @{ 'User-Agent' = 'Flux-Encoder-FFmpeg-Builder' }

# Create diagnostics before any network, discovery, or extraction step. Earlier
# versions created logs only after Ensure-Msys2, hiding bootstrap failures.
$LogDir = Join-Path $DependenciesDir 'logs'
$BuildLog = Join-Path $LogDir 'ffmpeg-build.log'
$UpdateLog = Join-Path $LogDir 'msys2-update.log'
$BootstrapLog = Join-Path $LogDir 'bootstrap.log'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
Remove-Item $BuildLog, $UpdateLog, $BootstrapLog -Force -ErrorAction SilentlyContinue

function Write-BootstrapLog([string]$Message) {
    $timestamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'
    $line = "[$timestamp] $Message"
    Add-Content -LiteralPath $BootstrapLog -Value $line -Encoding UTF8
    Write-Host $Message
}

Write-BootstrapLog "Flux Encoder FFmpeg builder started."
Write-BootstrapLog "PowerShell: $($PSVersionTable.PSVersion)"
Write-BootstrapLog "SourceDir: $SourceDir"
Write-BootstrapLog "DependenciesDir: $DependenciesDir"
Write-BootstrapLog "OutputDir: $OutputDir"

function Write-Step([string]$Message) {
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Download-File([string]$Url, [string]$Destination) {
    if ($NoDownload) {
        throw "A required download is missing and -NoDownload was specified: $Url"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -Headers $Headers
}

function Convert-ToMsysPath([string]$Path) {
    $full = [System.IO.Path]::GetFullPath($Path).Replace('\', '/')
    if ($full -match '^([A-Za-z]):/(.*)$') {
        return "/$($Matches[1].ToLower())/$($Matches[2])"
    }
    throw "Cannot convert path to MSYS2 format: $Path"
}

function Find-MsysRoot {
    $candidates = @(
        $MsysRoot,
        'C:\msys64',
        (Join-Path $env:LOCALAPPDATA 'Programs\msys64')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and
            (Test-Path (Join-Path $candidate 'usr\bin\bash.exe')) -and
            (Test-Path (Join-Path $candidate 'msys2_shell.cmd'))) {
            return $candidate
        }
    }
    return $null
}

function Invoke-Msys2Command {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Command,
        [Parameter(Mandatory = $true)][string]$LogPath,
        [switch]$AllowFailure
    )

    $shell = Join-Path $Root 'msys2_shell.cmd'
    if (-not (Test-Path $shell)) {
        throw "MSYS2 shell launcher was not found: $shell"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $LogPath -Parent) | Out-Null
    $arguments = @('-defterm', '-no-start', '-ucrt64', '-here', '-c', $Command)
    Write-Host "MSYS2 command: $Command" -ForegroundColor DarkGray

    # Windows PowerShell 5.1 promotes any native stderr output to a
    # NativeCommandError when the script-wide ErrorActionPreference is Stop.
    # pacman writes harmless warnings (for example, "package is up to date")
    # to stderr even when it exits successfully, so capture native output with
    # Continue and decide success exclusively from the process exit code.
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $shell @arguments 2>&1 | ForEach-Object { $_.ToString() }
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }

    $output | Tee-Object -FilePath $LogPath -Append | ForEach-Object { Write-Host $_ }

    if ($exitCode -ne 0 -and -not $AllowFailure) {
        $tail = if (Test-Path $LogPath) {
            (Get-Content $LogPath -Tail 40 -ErrorAction SilentlyContinue) -join [Environment]::NewLine
        } else { '' }
        throw "MSYS2 command failed with exit code ${exitCode}.`nLog: $LogPath`n`nLast output:`n$tail"
    }
    return $exitCode
}

function Ensure-Msys2 {
    Write-BootstrapLog 'Searching for an existing MSYS2 installation.'
    $root = Find-MsysRoot
    if ($root) {
        Write-BootstrapLog "Found MSYS2: $root"
        return $root
    }
    if ($NoDownload) { throw 'MSYS2 was not found.' }

    Write-Step 'Downloading the latest official MSYS2 base environment'
    Write-BootstrapLog 'Querying the latest MSYS2 release from GitHub.'
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/msys2/msys2-installer/releases/latest' -Headers $Headers
    $asset = $release.assets | Where-Object { $_.name -match '^msys2-base-x86_64-.*\.sfx\.exe$' } | Select-Object -First 1
    if (-not $asset) { throw 'Could not locate the official MSYS2 x64 self-extracting archive.' }

    $archive = Join-Path $DependenciesDir $asset.name
    Write-BootstrapLog "Downloading MSYS2 archive: $($asset.browser_download_url)"
    Write-BootstrapLog "Archive destination: $archive"
    Download-File $asset.browser_download_url $archive
    Write-BootstrapLog 'MSYS2 archive download completed.'
    if (Test-Path $MsysRoot) { Remove-Item -Recurse -Force $MsysRoot }
    New-Item -ItemType Directory -Force -Path $DependenciesDir | Out-Null

    Write-BootstrapLog "Extracting MSYS2 into: $DependenciesDir"
    $process = Start-Process -FilePath $archive -ArgumentList @('-y', "-o$DependenciesDir") -Wait -PassThru
    Write-BootstrapLog "MSYS2 extractor exit code: $($process.ExitCode)"
    if ($process.ExitCode -ne 0) { throw "MSYS2 extraction failed with exit code $($process.ExitCode)." }

    $extracted = Get-ChildItem $DependenciesDir -Directory | Where-Object {
        (Test-Path (Join-Path $_.FullName 'usr\bin\bash.exe')) -and
        (Test-Path (Join-Path $_.FullName 'msys2_shell.cmd'))
    } | Select-Object -First 1
    if (-not $extracted) { throw 'MSYS2 was extracted, but its shell launcher was not found.' }
    if ($extracted.FullName -ne $MsysRoot) {
        if (Test-Path $MsysRoot) { Remove-Item -Recurse -Force $MsysRoot }
        Move-Item $extracted.FullName $MsysRoot
    }
    return $MsysRoot
}

try {
if ($Clean) {
    Remove-Item -Recurse -Force $BuildRoot -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $OutputDir -ErrorAction SilentlyContinue
}
if (-not (Test-Path $BootstrapScript)) { throw "Missing FFmpeg bootstrap script: $BootstrapScript" }

$resolvedMsysRoot = Ensure-Msys2
Write-BootstrapLog "Using MSYS2 root: $resolvedMsysRoot"
New-Item -ItemType Directory -Force -Path $BuildRoot, $OutputDir | Out-Null

# MSYS2 may replace its own runtime during a full upgrade. Running updates in
# separate shell processes avoids continuing inside a process that has just
# replaced msys-2.0.dll.
if (-not $NoDownload) {
    Write-Step 'Updating the MSYS2 keyring and package database'
    [void](Invoke-Msys2Command -Root $resolvedMsysRoot -LogPath $UpdateLog -Command         "pacman -Sy --noconfirm msys2-keyring")

    Write-Step 'Updating the MSYS2 base system (stage 1)'
    [void](Invoke-Msys2Command -Root $resolvedMsysRoot -LogPath $UpdateLog -Command         "pacman -Syu --noconfirm" -AllowFailure)

    Write-Step 'Updating the MSYS2 base system (stage 2)'
    [void](Invoke-Msys2Command -Root $resolvedMsysRoot -LogPath $UpdateLog -Command         "pacman -Syu --noconfirm")
}

$msysScript = Convert-ToMsysPath $BootstrapScript
$msysBuild = Convert-ToMsysPath $BuildRoot
$msysOutput = Convert-ToMsysPath $OutputDir

Write-Step "Building FFmpeg $FfmpegRef with NVENC/NVDEC/CUDA support"
$command = "export FLUX_ENCODER_SKIP_MSYS_UPDATE=1; " +
           "export FFMPEG_REF='$FfmpegRef'; " +
           "export NV_CODEC_HEADERS_REF='$NvCodecHeadersRef'; " +
           "export WORK_ROOT='$msysBuild'; " +
           "export OUTPUT_ROOT='$msysOutput'; " +
           "exec bash '$msysScript'"

[void](Invoke-Msys2Command -Root $resolvedMsysRoot -LogPath $BuildLog -Command $command)

$ffmpeg = Join-Path $OutputDir 'ffmpeg.exe'
$ffprobe = Join-Path $OutputDir 'ffprobe.exe'
if (-not (Test-Path $ffmpeg) -or -not (Test-Path $ffprobe)) {
    throw 'FFmpeg completed without producing ffmpeg.exe and ffprobe.exe.'
}

$encoders = (& $ffmpeg -hide_banner -encoders 2>&1 | Out-String)
$hwaccels = (& $ffmpeg -hide_banner -hwaccels 2>&1 | Out-String)
$required = @('h264_nvenc', 'hevc_nvenc')
foreach ($encoder in $required) {
    if ($encoders -notmatch "\b$([regex]::Escape($encoder))\b") {
        throw "The compiled FFmpeg is missing required encoder: $encoder"
    }
}
if ($hwaccels -notmatch '(?m)^cuda\s*$') {
    throw 'The compiled FFmpeg is missing CUDA hardware acceleration.'
}

Write-Host "`nFFmpeg build completed successfully." -ForegroundColor Green
Write-Host "Output: $OutputDir"

}
catch {
    $message = $_.Exception.Message
    $position = $_.InvocationInfo.PositionMessage
    $stack = $_.ScriptStackTrace
    Write-BootstrapLog "FAILED: $message"
    if ($position) { Write-BootstrapLog $position }
    if ($stack) { Write-BootstrapLog "Script stack:`n$stack" }
    Write-Host "`nFFmpeg bootstrap failed." -ForegroundColor Red
    Write-Host "Diagnostics: $BootstrapLog" -ForegroundColor Yellow
    Write-Host "MSYS2 update log: $UpdateLog" -ForegroundColor Yellow
    Write-Host "FFmpeg build log: $BuildLog" -ForegroundColor Yellow
    throw
}
