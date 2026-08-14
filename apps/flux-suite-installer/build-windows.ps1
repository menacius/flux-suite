[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    [string]$QtDir = '',
    [string]$BuildDir = '',
    [string]$OutputDir = '',
    [string]$SetupOutputDir = '',
    [string]$InnoCompiler = '',
    [switch]$SkipSetup,
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SourceDir = [IO.Path]::GetFullPath($PSScriptRoot)
$WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $SourceDir '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildCacheRoot = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [IO.Path]::GetTempPath() }
    $BuildDir = Join-Path $BuildCacheRoot 'FluxSuiteBuild\Installer'
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $WorkspaceRoot 'out\dist\windows-x64\Flux Installer'
}
if ([string]::IsNullOrWhiteSpace($SetupOutputDir)) {
    $SetupOutputDir = Join-Path $WorkspaceRoot 'out\dist\windows-x64\Flux Installer Setup'
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$SetupOutputDir = [IO.Path]::GetFullPath($SetupOutputDir)

if ([string]::IsNullOrWhiteSpace($QtDir)) {
    $candidates = @(
        $env:Qt6_DIR,
        (Join-Path $WorkspaceRoot 'apps\flux-encoder\.deps\windows\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6'),
        'C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6'
    ) | Where-Object { $_ -and (Test-Path (Join-Path $_ 'Qt6Config.cmake')) }
    $QtDir = $candidates | Select-Object -First 1
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir 'Qt6Config.cmake'))) {
    throw 'Qt 6 MSVC SDK not found. Pass -QtDir <path>\lib\cmake\Qt6.'
}
$QtDir = [IO.Path]::GetFullPath($QtDir)
$QtRoot = [IO.Path]::GetFullPath((Join-Path $QtDir '..\..\..'))
$DeployTool = Join-Path $QtRoot 'bin\windeployqt.exe'
if (-not (Test-Path $DeployTool)) {
    throw "windeployqt.exe not found: $DeployTool"
}

function Assert-SafeGeneratedPath([string]$Path, [string]$Label) {
    $resolved = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ($resolved.Length -lt 12 -or $resolved -eq [IO.Path]::GetPathRoot($resolved)) {
        throw "Unsafe $Label path: $resolved"
    }
}

Assert-SafeGeneratedPath $BuildDir 'build'
Assert-SafeGeneratedPath $OutputDir 'output'
Assert-SafeGeneratedPath $SetupOutputDir 'setup output'
if ($SetupOutputDir.Equals($OutputDir, [StringComparison]::OrdinalIgnoreCase) -or
    $SetupOutputDir.StartsWith($OutputDir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'SetupOutputDir must not be inside the deployed installer runtime directory.'
}

if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "Configuring Flux Suite Installer" -ForegroundColor Cyan
& cmake -S $SourceDir -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
    "-DQt6_DIR=$($QtDir -replace '\\','/')" -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

Write-Host "Building $Configuration" -ForegroundColor Cyan
& cmake --build $BuildDir --config $Configuration --target flux-suite-installer -- /m
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$Executable = Join-Path $BuildDir "$Configuration\Flux Suite.exe"
if (-not (Test-Path $Executable)) { throw "Built executable not found: $Executable" }

if (Test-Path -LiteralPath $OutputDir) {
    Remove-Item -LiteralPath $OutputDir -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
Copy-Item -LiteralPath $Executable -Destination $OutputDir
Copy-Item -LiteralPath (Join-Path $SourceDir 'resources\manifest.json') -Destination $OutputDir

Write-Host 'Deploying Qt runtime' -ForegroundColor Cyan
& $DeployTool --release --compiler-runtime --no-translations --no-opengl-sw `
    --dir $OutputDir (Join-Path $OutputDir 'Flux Suite.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }

$ValidationProcess = Start-Process -FilePath (Join-Path $OutputDir 'Flux Suite.exe') `
    -ArgumentList '--validate-manifest' -Wait -PassThru -WindowStyle Hidden
if ($ValidationProcess.ExitCode -ne 0) { throw 'Bundled manifest validation failed.' }

if (-not $SkipSetup) {
    if ([string]::IsNullOrWhiteSpace($InnoCompiler)) {
        $InnoCommand = Get-Command iscc.exe -ErrorAction SilentlyContinue
        $InnoCompiler = if ($InnoCommand) { $InnoCommand.Source } else { 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe' }
    }
    if (-not (Test-Path -LiteralPath $InnoCompiler)) {
        throw "Inno Setup 6 compiler not found: $InnoCompiler"
    }
    if (Test-Path -LiteralPath $SetupOutputDir) {
        Remove-Item -LiteralPath $SetupOutputDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $SetupOutputDir | Out-Null
    $VersionLabel = (Get-Content -Raw -LiteralPath (Join-Path $SourceDir 'VERSION.txt')).Trim()
    $OutputBaseFilename = 'Flux_Suite_Setup_' + ($VersionLabel -replace '[\\/:*?"<>|\s]+', '_') + '_windows-x64'
    $SetupScript = Join-Path $SourceDir 'setup\flux-suite.iss'
    Write-Host 'Building first-run Flux Suite Setup' -ForegroundColor Cyan
    & $InnoCompiler "/DFluxSourceDir=$OutputDir" "/DFluxSetupOutputDir=$SetupOutputDir" `
        "/DFluxAppVersion=$VersionLabel" "/DFluxNumericVersion=0.8.19.0" `
        "/DFluxOutputBaseFilename=$OutputBaseFilename" $SetupScript
    if ($LASTEXITCODE -ne 0) { throw 'First-run setup build failed.' }
    $SetupExecutable = Join-Path $SetupOutputDir "$OutputBaseFilename.exe"
    if (-not (Test-Path -LiteralPath $SetupExecutable)) {
        throw "First-run setup executable was not created: $SetupExecutable"
    }
    Write-Host "First-run setup ready: $SetupExecutable" -ForegroundColor Green
}

Write-Host "Flux Suite Installer ready: $OutputDir" -ForegroundColor Green
