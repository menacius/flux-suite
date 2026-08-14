[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'RelWithDebInfo',

    [string]$QtVersion = '6.8.3',

    [string]$BuildDir = '',
    [string]$DependenciesDir = '',
    [string]$PackageDir = '',

    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$NoDownload
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

if ($env:OS -ne 'Windows_NT') {
    throw 'This script must be run on Windows.'
}

$SourceDir = [System.IO.Path]::GetFullPath($PSScriptRoot)
$RepositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $SourceDir '..\..'))
if (-not (Test-Path (Join-Path $SourceDir 'CMakeLists.txt'))) {
    throw "CMakeLists.txt was not found in: $SourceDir"
}
$ReleaseLabel = (Get-Content -Raw -LiteralPath (Join-Path $SourceDir 'VERSION.txt')).Trim()
if ($ReleaseLabel -ne '2026 - v0.8.18-alpha') {
    throw "Unexpected Flux Suite version label: $ReleaseLabel"
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $SourceDir 'build\windows-msvc-release'
}
if ([string]::IsNullOrWhiteSpace($DependenciesDir)) {
    $DependenciesDir = Join-Path $SourceDir '.deps\windows'
}
if ([string]::IsNullOrWhiteSpace($PackageDir)) {
    $PackageDir = Join-Path $RepositoryRoot 'out\dist\windows-x64\Flux Encoder'
}

$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
$DependenciesDir = [System.IO.Path]::GetFullPath($DependenciesDir)
$PackageDir = [System.IO.Path]::GetFullPath($PackageDir)

New-Item -ItemType Directory -Force -Path $DependenciesDir | Out-Null

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$WebHeaders = @{ 'User-Agent' = 'Flux-Encoder-Windows-Build-Script' }

function Write-Step {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host "    $Message" -ForegroundColor Green
}

function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath $($ArgumentList -join ' ')"
    }
}

function Download-File {
    param(
        [Parameter(Mandatory)][string]$Url,
        [Parameter(Mandatory)][string]$Destination
    )

    if ($NoDownload) {
        throw "A required dependency is missing and -NoDownload was specified: $Url"
    }

    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Write-Host "    Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing -Headers $WebHeaders
}

function Expand-ZipFresh {
    param(
        [Parameter(Mandatory)][string]$Archive,
        [Parameter(Mandatory)][string]$Destination
    )

    if (Test-Path $Destination) {
        Remove-Item -Recurse -Force $Destination
    }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Expand-Archive -Path $Archive -DestinationPath $Destination -Force
}

function Get-ExecutableVersion {
    param([Parameter(Mandatory)][string]$Executable)

    $text = (& $Executable --version 2>&1 | Out-String)
    if ($text -match '(?im)version\s+([0-9]+(?:\.[0-9]+){1,3})') {
        return [version]$Matches[1]
    }
    return $null
}

function Find-VSWhere {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )

    $command = Get-Command vswhere.exe -ErrorAction SilentlyContinue
    if ($command) {
        $candidates += $command.Source
    }

    return $candidates | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
}

function Find-VisualStudio {
    $vswhere = Find-VSWhere
    if (-not $vswhere) {
        return $null
    }

    $installation = (& $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null | Select-Object -First 1)

    if ($installation -and (Test-Path $installation)) {
        return $installation.Trim()
    }
    return $null
}

function Ensure-VisualStudio {
    Write-Step 'Checking the Visual Studio 2022 C++ toolchain'

    $installation = Find-VisualStudio
    if ($installation) {
        Write-Ok "Using Visual Studio: $installation"
        return $installation
    }

    if ($NoDownload) {
        throw 'Visual Studio 2022 with the Desktop C++ workload was not found.'
    }

    $installer = Join-Path $DependenciesDir 'vs_BuildTools.exe'
    Download-File 'https://aka.ms/vs/17/release/vs_BuildTools.exe' $installer

    Write-Host '    Installing Visual Studio Build Tools. A UAC prompt may appear.'
    $arguments = @(
        '--quiet', '--wait', '--norestart', '--nocache',
        '--add', 'Microsoft.VisualStudio.Workload.VCTools',
        '--includeRecommended'
    )
    $process = Start-Process -FilePath $installer -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -notin @(0, 3010)) {
        throw "Visual Studio Build Tools installation failed with exit code $($process.ExitCode)."
    }

    $installation = Find-VisualStudio
    if (-not $installation) {
        throw 'Visual Studio Build Tools finished installing, but the C++ toolchain could not be located.'
    }

    Write-Ok "Installed Visual Studio Build Tools: $installation"
    return $installation
}

function Find-CMakeCandidate {
    param([string]$VisualStudioRoot)

    $candidates = @()
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        $candidates += $command.Source
    }

    if ($VisualStudioRoot) {
        $candidates += Join-Path $VisualStudioRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
    }

    $local = Get-ChildItem -Path (Join-Path $DependenciesDir 'cmake') -Filter cmake.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName
    $candidates += $local

    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (-not (Test-Path $candidate)) {
            continue
        }
        $version = Get-ExecutableVersion $candidate
        if ($version -and $version -ge [version]'3.21.0') {
            return $candidate
        }
    }
    return $null
}

function Ensure-CMake {
    param([string]$VisualStudioRoot)

    Write-Step 'Checking CMake 3.21 or newer'

    $cmake = Find-CMakeCandidate $VisualStudioRoot
    if ($cmake) {
        Write-Ok "Using CMake: $cmake"
        return $cmake
    }

    if ($NoDownload) {
        throw 'CMake 3.21 or newer was not found.'
    }

    Write-Host '    Resolving the latest official CMake portable package...'
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/Kitware/CMake/releases/latest' -Headers $WebHeaders
    $asset = $release.assets |
        Where-Object { $_.name -match '^cmake-[0-9.]+-windows-x86_64\.zip$' } |
        Select-Object -First 1
    if (-not $asset) {
        throw 'Could not locate the Windows x86_64 CMake ZIP in the latest CMake release.'
    }

    $archive = Join-Path $DependenciesDir $asset.name
    $destination = Join-Path $DependenciesDir 'cmake'
    Download-File $asset.browser_download_url $archive
    Expand-ZipFresh $archive $destination

    $cmake = Get-ChildItem -Path $destination -Filter cmake.exe -Recurse |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $cmake) {
        throw 'CMake was downloaded, but cmake.exe could not be found.'
    }

    Write-Ok "Installed CMake: $cmake"
    return $cmake
}

function Find-Uv {
    $command = Get-Command uv.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return Get-ChildItem -Path (Join-Path $DependenciesDir 'uv') -Filter uv.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
}

function Ensure-Uv {
    $uv = Find-Uv
    if ($uv) {
        return $uv
    }

    if ($NoDownload) {
        throw 'Qt is missing and uv/aqtinstall is not available to install it.'
    }

    Write-Host '    Resolving the latest uv portable package for aqtinstall...'
    $release = Invoke-RestMethod -Uri 'https://api.github.com/repos/astral-sh/uv/releases/latest' -Headers $WebHeaders
    $asset = $release.assets |
        Where-Object { $_.name -eq 'uv-x86_64-pc-windows-msvc.zip' } |
        Select-Object -First 1
    if (-not $asset) {
        throw 'Could not locate the Windows x64 uv package.'
    }

    $archive = Join-Path $DependenciesDir $asset.name
    $destination = Join-Path $DependenciesDir 'uv'
    Download-File $asset.browser_download_url $archive
    Expand-ZipFresh $archive $destination

    $uv = Find-Uv
    if (-not $uv) {
        throw 'uv was downloaded, but uv.exe could not be found.'
    }
    return $uv
}

function ConvertTo-QtRoot {
    param([string]$Candidate)

    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        return $null
    }

    try {
        $path = [System.IO.Path]::GetFullPath($Candidate.Trim('"'))
    } catch {
        return $null
    }

    if (Test-Path (Join-Path $path 'lib\cmake\Qt6\Qt6Config.cmake')) {
        return $path
    }

    if ((Split-Path $path -Leaf) -eq 'Qt6' -and (Test-Path (Join-Path $path 'Qt6Config.cmake'))) {
        $root = $path
        1..3 | ForEach-Object { $root = Split-Path $root -Parent }
        if (Test-Path (Join-Path $root 'lib\cmake\Qt6\Qt6Config.cmake')) {
            return $root
        }
    }

    if ((Split-Path $path -Leaf) -eq 'bin' -and (Test-Path (Join-Path $path 'windeployqt.exe'))) {
        $root = Split-Path $path -Parent
        if (Test-Path (Join-Path $root 'lib\cmake\Qt6\Qt6Config.cmake')) {
            return $root
        }
    }

    return $null
}

function Get-QtRootVersion {
    param([string]$QtRoot)

    $tools = @(
        (Join-Path $QtRoot 'bin\qtpaths6.exe'),
        (Join-Path $QtRoot 'bin\qmake6.exe'),
        (Join-Path $QtRoot 'bin\qmake.exe')
    )

    foreach ($tool in $tools) {
        if (-not (Test-Path $tool)) {
            continue
        }
        $value = (& $tool -query QT_VERSION 2>$null | Select-Object -First 1)
        if ($value) {
            try { return [version]$value.Trim() } catch { }
        }
    }

    $versionFile = Join-Path $QtRoot 'lib\cmake\Qt6\Qt6ConfigVersion.cmake'
    if (Test-Path $versionFile) {
        $text = Get-Content $versionFile -Raw
        if ($text -match 'PACKAGE_VERSION\s+"([0-9.]+)"') {
            try { return [version]$Matches[1] } catch { }
        }
    }

    return $null
}

function Find-QtRoot {
    $candidates = @()

    if ($env:Qt6_DIR) {
        $candidates += $env:Qt6_DIR
    }
    if ($env:CMAKE_PREFIX_PATH) {
        $candidates += $env:CMAKE_PREFIX_PATH -split ';'
    }

    foreach ($name in @('qtpaths6.exe', 'qmake6.exe', 'qmake.exe', 'windeployqt.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($command) {
            $candidates += Split-Path $command.Source -Parent
        }
    }

    if (Test-Path 'C:\Qt') {
        $candidates += Get-ChildItem 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue |
                    Where-Object { $_.Name -match '^msvc20(19|22)_64$' } |
                    Select-Object -ExpandProperty FullName
            }
    }

    $localQt = Join-Path $DependenciesDir 'Qt'
    if (Test-Path $localQt) {
        $candidates += Get-ChildItem $localQt -Filter Qt6Config.cmake -Recurse -ErrorAction SilentlyContinue |
            ForEach-Object {
                $root = $_.Directory.FullName
                1..3 | ForEach-Object { $root = Split-Path $root -Parent }
                $root
            }
    }

    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $root = ConvertTo-QtRoot $candidate
        if (-not $root) {
            continue
        }
        $version = Get-QtRootVersion $root
        if ($version -and $version -ge [version]'6.5.0' -and
            (Test-Path (Join-Path $root 'bin\windeployqt.exe'))) {
            return $root
        }
    }

    return $null
}

function Ensure-Qt {
    Write-Step 'Checking Qt 6.5 or newer (MSVC x64)'

    $qtRoot = Find-QtRoot
    if ($qtRoot) {
        Write-Ok "Using Qt $(Get-QtRootVersion $qtRoot): $qtRoot"
        return $qtRoot
    }

    if ($NoDownload) {
        throw 'Qt 6.5 or newer with the MSVC x64 kit was not found.'
    }

    $uv = Ensure-Uv
    $qtInstallRoot = Join-Path $DependenciesDir 'Qt'
    New-Item -ItemType Directory -Force -Path $qtInstallRoot | Out-Null

    Write-Host "    Installing Qt $QtVersion (win64_msvc2022_64) with aqtinstall..."
    Invoke-Native -FilePath $uv -ArgumentList @(
        'tool', 'run', '--from', 'aqtinstall', 'aqt',
        'install-qt', 'windows', 'desktop', $QtVersion,
        'win64_msvc2022_64', '-O', $qtInstallRoot
    )

    $qtRoot = Find-QtRoot
    if (-not $qtRoot) {
        throw "Qt $QtVersion was installed, but a valid Qt 6 MSVC kit could not be located."
    }

    Write-Ok "Installed Qt $(Get-QtRootVersion $qtRoot): $qtRoot"
    return $qtRoot
}

function Test-PortableFfmpegPair {
    param([string]$Ffmpeg, [string]$Ffprobe)

    if (-not $Ffmpeg -or -not $Ffprobe -or -not (Test-Path $Ffmpeg) -or -not (Test-Path $Ffprobe)) {
        return $false
    }

    # Very small executables are commonly package-manager shims and are not portable.
    return ((Get-Item $Ffmpeg).Length -gt 1MB -and (Get-Item $Ffprobe).Length -gt 1MB)
}

function Find-FfmpegPair {
    $ffmpegCommand = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    $ffprobeCommand = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
    if ($ffmpegCommand -and $ffprobeCommand -and
        (Test-PortableFfmpegPair $ffmpegCommand.Source $ffprobeCommand.Source)) {
        return @{
            Ffmpeg = $ffmpegCommand.Source
            Ffprobe = $ffprobeCommand.Source
            Directory = Split-Path $ffmpegCommand.Source -Parent
        }
    }

    $localRoot = Join-Path $DependenciesDir 'ffmpeg'
    $ffmpeg = Get-ChildItem $localRoot -Filter ffmpeg.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    $ffprobe = Get-ChildItem $localRoot -Filter ffprobe.exe -Recurse -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName
    if (Test-PortableFfmpegPair $ffmpeg $ffprobe) {
        return @{
            Ffmpeg = $ffmpeg
            Ffprobe = $ffprobe
            Directory = Split-Path $ffmpeg -Parent
        }
    }

    return $null
}

function Ensure-Ffmpeg {
    Write-Step 'Checking FFmpeg and FFprobe'

    $pair = Find-FfmpegPair
    if ($pair) {
        Write-Ok "Using FFmpeg: $($pair.Ffmpeg)"
        return $pair
    }

    if ($NoDownload) {
        throw 'A portable FFmpeg/FFprobe pair was not found.'
    }

    $archive = Join-Path $DependenciesDir 'ffmpeg-release-essentials.zip'
    $destination = Join-Path $DependenciesDir 'ffmpeg'
    Download-File 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' $archive
    Expand-ZipFresh $archive $destination

    $pair = Find-FfmpegPair
    if (-not $pair) {
        throw 'FFmpeg was downloaded, but ffmpeg.exe and ffprobe.exe could not be found.'
    }

    Write-Ok "Installed FFmpeg: $($pair.Ffmpeg)"
    return $pair
}

function Copy-ExecutableAndRuntime {
    param(
        [Parameter(Mandatory)][string]$Executable,
        [Parameter(Mandatory)][string]$Destination
    )

    Copy-Item $Executable -Destination $Destination -Force
    $sourceDirectory = Split-Path $Executable -Parent
    Get-ChildItem $sourceDirectory -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Copy-Item $_.FullName -Destination $Destination -Force }
}

$visualStudio = Ensure-VisualStudio
$cmake = Ensure-CMake $visualStudio
$ctest = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
$qtRoot = Ensure-Qt
$ffmpeg = Ensure-Ffmpeg

if ($Clean) {
    Write-Step 'Cleaning previous build and package directories'
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $PackageDir -ErrorAction SilentlyContinue
}

Write-Step 'Configuring Flux Encoder'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Invoke-Native -FilePath $cmake -ArgumentList @(
    '-S', $SourceDir,
    '-B', $BuildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64',
    "-DCMAKE_PREFIX_PATH=$qtRoot",
    '-DFLUX_ENCODER_BUILD_TESTS=ON'
)

Write-Step "Building Flux Encoder ($Configuration)"
Invoke-Native -FilePath $cmake -ArgumentList @(
    '--build', $BuildDir, '--config', $Configuration, '--parallel'
)

$configurationDir = Join-Path $BuildDir $Configuration

if (-not $SkipTests) {
    Write-Step 'Running tests'
    if (-not (Test-Path $ctest)) {
        throw "ctest.exe was not found beside CMake: $ctest"
    }

    # CTest launches the test executables directly from the build tree. On
    # Windows, Qt DLLs are not discoverable there until windeployqt runs, so
    # expose the selected Qt runtime (plus local build/FFmpeg runtimes) only
    # for the duration of the test run.
    $originalPath = $env:PATH
    try {
        $runtimePaths = @(
            (Join-Path $qtRoot 'bin'),
            $configurationDir,
            $ffmpeg.Directory
        ) | Where-Object { $_ -and (Test-Path $_) }
        $env:PATH = (($runtimePaths + @($originalPath)) -join ';')

        Invoke-Native -FilePath $ctest -ArgumentList @(
            '--test-dir', $BuildDir, '-C', $Configuration, '--output-on-failure'
        )
    }
    finally {
        $env:PATH = $originalPath
    }
}

Write-Step 'Creating the portable Windows package'
if (Test-Path $PackageDir) {
    Remove-Item -Recurse -Force $PackageDir
}
New-Item -ItemType Directory -Force -Path $PackageDir | Out-Null

$guiExecutable = Join-Path $configurationDir 'Flux Encoder.exe'
$workerExecutable = Join-Path $configurationDir 'flux-encoder-worker.exe'
if (-not (Test-Path $guiExecutable)) {
    throw "The GUI executable was not produced: $guiExecutable"
}
if (-not (Test-Path $workerExecutable)) {
    throw "The worker executable was not produced: $workerExecutable"
}

Copy-Item $guiExecutable -Destination $PackageDir -Force
Copy-Item $workerExecutable -Destination $PackageDir -Force
Copy-ExecutableAndRuntime $ffmpeg.Ffmpeg $PackageDir
Copy-ExecutableAndRuntime $ffmpeg.Ffprobe $PackageDir

Copy-Item (Join-Path $RepositoryRoot 'LICENSE') -Destination $PackageDir -Force
foreach ($document in @('README.md', 'VALIDATION.md', 'VERSION.txt', 'CHANGELOG.md')) {
    $source = Join-Path $SourceDir $document
    if (Test-Path $source) {
        Copy-Item $source -Destination $PackageDir -Force
    }
}

$providerSource = Join-Path $SourceDir 'providers'
if (Test-Path $providerSource) {
    Copy-Item $providerSource -Destination (Join-Path $PackageDir 'providers') -Recurse -Force
}

# Bundle the standalone Flux Motion runtime as the out-of-process renderer.
# It owns the same libobs GPU compositor used by the OBS plugin and can render
# saved projects while Flux Motion and OBS are closed. A CI/release build may
# provide an explicit directory; the sibling development build is the fallback.
$motionRendererCandidates = @()
if (-not [string]::IsNullOrWhiteSpace($env:FLUX_MOTION_RENDERER_DIR)) {
    $motionRendererCandidates += $env:FLUX_MOTION_RENDERER_DIR
}
$motionRendererCandidates += Join-Path $RepositoryRoot 'out\build\motion-windows\flux-motion-editor\bin'
$motionRendererCandidates += Join-Path $RepositoryRoot 'apps\flux-motion\build\flux-motion-editor\bin'
$motionRendererSource = $motionRendererCandidates |
    Where-Object { Test-Path (Join-Path $_ 'flux-motion-renderer.exe') } |
    Select-Object -First 1
if ($motionRendererSource) {
    $motionRendererDestination = Join-Path $PackageDir 'providers\org.fluxmotion.renderer\renderer'
    New-Item -ItemType Directory -Force -Path $motionRendererDestination | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $motionRendererSource) {
        if ($item.PSIsContainer) {
            Copy-Item -LiteralPath $item.FullName -Destination $motionRendererDestination -Recurse -Force
        }
        elseif ($item.Extension -ne '.pdb' -and
                $item.Name -ne 'flux-motion-editor.exe') {
            Copy-Item -LiteralPath $item.FullName -Destination $motionRendererDestination -Force
        }
    }
    Write-Ok "Bundled Flux Motion headless renderer: $motionRendererSource"
}
else {
    Write-Warning 'Flux Motion headless renderer was not found. Set FLUX_MOTION_RENDERER_DIR before packaging.'
}

$windeployqt = Join-Path $qtRoot 'bin\windeployqt.exe'
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe was not found: $windeployqt"
}

$deployMode = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
Invoke-Native -FilePath $windeployqt -ArgumentList @(
    $deployMode, '--compiler-runtime', '--no-translations',
    (Join-Path $PackageDir 'Flux Encoder.exe')
)
Invoke-Native -FilePath $windeployqt -ArgumentList @(
    $deployMode, '--compiler-runtime', '--no-translations',
    (Join-Path $PackageDir 'flux-encoder-worker.exe')
)

$qsqlite = Join-Path $PackageDir 'sqldrivers\qsqlite.dll'
if (-not (Test-Path $qsqlite)) {
    $sourceQsqlite = Join-Path $qtRoot 'plugins\sqldrivers\qsqlite.dll'
    if (-not (Test-Path $sourceQsqlite)) {
        throw 'The Qt SQLite driver qsqlite.dll was not found.'
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $qsqlite -Parent) | Out-Null
    Copy-Item $sourceQsqlite -Destination $qsqlite -Force
}

$encoders = (& $ffmpeg.Ffmpeg -hide_banner -encoders 2>&1 | Out-String)
if ($encoders -notmatch '\bh264_nvenc\b') {
    Write-Warning 'This FFmpeg build does not expose h264_nvenc. Software encoding will still work.'
}

$zipName = "Flux_Encoder_$($ReleaseLabel -replace '[\\/:*?\"<>|\s]+', '_')_windows-x64.zip"
$zipPath = Join-Path $PackageDir $zipName
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}
$packageArchiveItems = Get-ChildItem -LiteralPath $PackageDir -Force |
    Select-Object -ExpandProperty FullName
Compress-Archive -LiteralPath $packageArchiveItems -DestinationPath $zipPath -CompressionLevel Optimal

# Guard against publishing a stale archive when the unpacked package was
# refreshed independently. The installer consumes the ZIP, so its primary
# executable must be byte-identical to the just-built Release executable.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entry = $archive.Entries | Where-Object { $_.FullName -eq 'Flux Encoder.exe' } | Select-Object -First 1
    if (-not $entry) {
        throw 'Flux Encoder.exe is missing from the distribution ZIP.'
    }
    $entryStream = $entry.Open()
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try {
            $archiveHash = ([BitConverter]::ToString($sha.ComputeHash($entryStream)) -replace '-', '').ToLowerInvariant()
        }
        finally {
            $sha.Dispose()
        }
    }
    finally {
        $entryStream.Dispose()
    }
}
finally {
    $archive.Dispose()
}
$builtHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $guiExecutable).Hash.ToLowerInvariant()
if ($archiveHash -ne $builtHash) {
    throw "Distribution ZIP contains a stale Flux Encoder.exe ($archiveHash instead of $builtHash)."
}
Write-Ok "Verified ZIP executable: $archiveHash"

Write-Host "`nBuild completed successfully." -ForegroundColor Green
Write-Host "Portable application: $PackageDir"
Write-Host "ZIP package:          $zipPath"
