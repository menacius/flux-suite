# build-windows.ps1 - Windows build helper for flux-motion.
# Validates prerequisites, configures CMake, builds, and installs the plugin.

param(
    [string]$BuildDir,
    [string]$VcpkgDir,
    [string]$VcpkgInstalledDir,
    [string]$ObsSdkDir,
    [string]$InstallRoot,
    [string]$Generator = "Visual Studio 17 2022",
    [string]$Architecture = "x64",
    [string]$Configuration = "Release",
    [string]$PackageName = "Flux_Motion_Plugin_for_OBS",
    [string]$PackagePlatform,
    [string]$PackageOutputDir,
    [switch]$BuildTests,
    [switch]$Clean,
    [switch]$SkipInstall,
    [switch]$SkipPackage
)

$ErrorActionPreference = "Stop"

# Paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepositoryRoot = [IO.Path]::GetFullPath((Join-Path $ScriptDir '..\..'))
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ScriptDir "build"
} elseif (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $ScriptDir $BuildDir
}
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
if ([string]::IsNullOrWhiteSpace($VcpkgDir)) {
    $VcpkgDir = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "C:\vcpkg" }
}

# -Clean removes only generated CMake/plugin outputs. The manifest install tree
# and vcpkg binary cache intentionally remain intact, so a clean plugin rebuild
# does not rebuild Cairo/Pango/GLib and their transitive dependencies.
if ($Clean -and (Test-Path $BuildDir)) {
    $ResolvedBuildDir = [System.IO.Path]::GetFullPath($BuildDir)
    $ResolvedScriptDir = [System.IO.Path]::GetFullPath($ScriptDir)
    if ($ResolvedBuildDir -eq $ResolvedScriptDir -or
        $ResolvedBuildDir -eq [System.IO.Path]::GetPathRoot($ResolvedBuildDir)) {
        Write-Error "Refusing to clean unsafe build path: $ResolvedBuildDir"
        exit 1
    }
    Write-Host "Cleaning generated build directory: $ResolvedBuildDir"
    Remove-Item -Recurse -Force $ResolvedBuildDir
}

# vcpkg's Autotools/MSYS ports (notably libiconv) cannot reliably configure
# when the manifest install root contains spaces. CMake's default manifest
# location is <build>/vcpkg_installed, which inherits spaces from the project
# path. Keep manifest packages in a project-specific directory below VCPKG_ROOT
# instead. This also avoids mixing the manifest with classic-mode packages.
if ([string]::IsNullOrWhiteSpace($VcpkgInstalledDir)) {
    if ($env:OBS_FXM_VCPKG_INSTALLED_DIR) {
        $VcpkgInstalledDir = $env:OBS_FXM_VCPKG_INSTALLED_DIR
    } else {
        $VcpkgInstalledDir = Join-Path $VcpkgDir "manifest-installed\flux-motion"
    }
}
$VcpkgInstalledDir = [System.IO.Path]::GetFullPath($VcpkgInstalledDir)
if ($VcpkgInstalledDir -match "\s") {
    Write-Error "The vcpkg manifest install path contains whitespace: $VcpkgInstalledDir. Pass -VcpkgInstalledDir with a path that has no spaces, for example C:\vcpkg-installed\obs-fxm."
    exit 1
}
New-Item -ItemType Directory -Force -Path $VcpkgInstalledDir | Out-Null

if ([string]::IsNullOrWhiteSpace($ObsSdkDir) -and $env:OBS_SDK_DIR) {
    $ObsSdkDir = $env:OBS_SDK_DIR
}
if ([string]::IsNullOrWhiteSpace($ObsSdkDir) -and $env:OBS_STUDIO_DIR) {
    $ObsSdkDir = $env:OBS_STUDIO_DIR
}
if ([string]::IsNullOrWhiteSpace($InstallRoot)) {
    if ($env:OBS_PLUGINS_PATH) {
        $InstallRoot = $env:OBS_PLUGINS_PATH
    } elseif ($env:ProgramData) {
        $InstallRoot = Join-Path $env:ProgramData "obs-studio\plugins"
    } else {
        $InstallRoot = Join-Path $env:APPDATA "obs-studio\plugins"
    }
}

$PluginName = "flux-motion"

function Get-CMakeQuotedValue {
    param(
        [string]$Text,
        [string]$VariableName
    )

    $Pattern = '(?im)^\s*set\s*\(\s*' + [regex]::Escape($VariableName) + '\s+"([^"]*)"\s*\)'
    $Match = [regex]::Match($Text, $Pattern)
    if (-not $Match.Success) {
        return ""
    }
    return $Match.Groups[1].Value
}

function ConvertTo-PackageComponent {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "Package name components cannot be empty."
    }

    $Sanitized = $Value.Trim() -replace '[\\/:*?"<>|\s]+', '_'
    $Sanitized = $Sanitized.Trim([char[]]@('_', '.'))
    if ([string]::IsNullOrWhiteSpace($Sanitized)) {
        throw "Package name component '$Value' does not contain any valid filename characters."
    }
    return $Sanitized
}

$CMakeListsPath = Join-Path $ScriptDir "CMakeLists.txt"
if (-not (Test-Path $CMakeListsPath)) {
    Write-Error "CMakeLists.txt was not found at: $CMakeListsPath"
    exit 1
}
$CMakeListsText = Get-Content -Raw -LiteralPath $CMakeListsPath
$ProjectVersionMatch = [regex]::Match(
    $CMakeListsText,
    '(?is)project\s*\(\s*[^\s\)]+\s+VERSION\s+([0-9]+(?:\.[0-9]+)*)'
)
if (-not $ProjectVersionMatch.Success) {
    Write-Error "Could not read the project version from CMakeLists.txt."
    exit 1
}
$ProjectVersion = $ProjectVersionMatch.Groups[1].Value
$DevelopmentVersion = Get-CMakeQuotedValue -Text $CMakeListsText -VariableName "OBS_FXM_DEVELOPMENT_VERSION"
if ([string]::IsNullOrWhiteSpace($DevelopmentVersion)) {
    Write-Error "Could not read OBS_FXM_DEVELOPMENT_VERSION from CMakeLists.txt."
    exit 1
}
$VersionFilePath = Join-Path $ScriptDir "VERSION.txt"
if (-not (Test-Path -LiteralPath $VersionFilePath)) {
    Write-Error "VERSION.txt was not found at: $VersionFilePath"
    exit 1
}
$ReleaseLabel = (Get-Content -Raw -LiteralPath $VersionFilePath).Trim()
$ReleaseVersionMatch = [regex]::Match(
    $ReleaseLabel,
    '(?i)(?:^|\s-\s)v?(\d+\.\d+\.\d+(?:-[0-9a-z.-]+)?)$'
)
if (-not $ReleaseVersionMatch.Success -or
    -not $ReleaseVersionMatch.Groups[1].Value.StartsWith(
        "$ProjectVersion-", [StringComparison]::OrdinalIgnoreCase) -and
    $ReleaseVersionMatch.Groups[1].Value -ne $ProjectVersion) {
    Write-Error "Flux Suite version label '$ReleaseLabel' does not match project version '$ProjectVersion'."
    exit 1
}

if ([string]::IsNullOrWhiteSpace($PackagePlatform)) {
    switch ($Architecture.ToLowerInvariant()) {
        'x64'   { $PackagePlatform = 'windows-x64' }
        'amd64' { $PackagePlatform = 'windows-x64' }
        'win32' { $PackagePlatform = 'windows-x86' }
        'x86'   { $PackagePlatform = 'windows-x86' }
        'arm64' { $PackagePlatform = 'windows-arm64' }
        default { $PackagePlatform = "windows-$($Architecture.ToLowerInvariant())" }
    }
}
if ([string]::IsNullOrWhiteSpace($PackageOutputDir)) {
    $PackageOutputDir = Join-Path $RepositoryRoot "out\dist\$PackagePlatform"
} elseif (-not [IO.Path]::IsPathRooted($PackageOutputDir)) {
    $PackageOutputDir = Join-Path $ScriptDir $PackageOutputDir
}
$PackageOutputDir = [IO.Path]::GetFullPath($PackageOutputDir)
$PluginDistributionDir = Join-Path $PackageOutputDir "Flux Motion Plugin for OBS"
$EditorDistributionDir = Join-Path $PackageOutputDir "Flux Motion"
$PackageFileName = "$(ConvertTo-PackageComponent $PackageName)_$(ConvertTo-PackageComponent $ReleaseLabel)_$(ConvertTo-PackageComponent $PackagePlatform).zip"
$PackageZipPath = Join-Path $PluginDistributionDir $PackageFileName
$EditorPackageFileName = "Flux_Motion_$(ConvertTo-PackageComponent $ReleaseLabel)_$(ConvertTo-PackageComponent $PackagePlatform).zip"
$EditorPackageZipPath = Join-Path $EditorDistributionDir $EditorPackageFileName

$VcpkgToolchain = Join-Path $VcpkgDir "scripts\buildsystems\vcpkg.cmake"
$ObsArchDir = if ($Architecture -eq "Win32" -or $Architecture -eq "x86") { "32bit" } else { "64bit" }
$VcpkgTriplet = if ($Architecture -eq "Win32" -or $Architecture -eq "x86") { "x86-windows" } else { "x64-windows" }
$PluginDllName = "$PluginName.dll"
$ObsPluginRoot = Join-Path $InstallRoot $PluginName
$ObsPluginBin = Join-Path $ObsPluginRoot "bin\$ObsArchDir"
$ObsPluginData = Join-Path $ObsPluginRoot "data\locale"

Write-Host "=== Starting Flux Motion build process ==="

# Source files now live in ownership-oriented module folders. Keep Windows-only
# preflight checks pointed at the same paths CMake builds so stale flat-tree
# paths fail here instead of later in MSVC.
$ModuleSources = @{
    CoreTitleData = "src\core\title-data.cpp"
    TextRichText = "src\text\title-rich-text.cpp"
    ObsPluginMain = "..\..\plugins\obs\flux-motion\src\plugin-main.cpp"
    ObsTitleSource = "..\..\packages\flux-common\Shared\rendering-engine\title-source.cpp"
    EditorTitleDock = "src\editor\title-dock.cpp"
    EditorTitleEditor = "src\editor\title-editor.cpp"
    TimelineAnimation = "Core\animation.cpp"
}

function Resolve-RepoPath {
    param([string]$RelativePath)
    return Join-Path $ScriptDir $RelativePath
}

function Assert-RepoFileExists {
    param(
        [string]$Name,
        [string]$RelativePath
    )

    $FullPath = Resolve-RepoPath $RelativePath
    if (-not (Test-Path $FullPath)) {
        Write-Error "Expected $Name at $RelativePath, but the file was not found. The Windows build script may be out of sync with the module layout."
        exit 1
    }
    return $FullPath
}

foreach ($Entry in $ModuleSources.GetEnumerator()) {
    [void](Assert-RepoFileExists -Name $Entry.Key -RelativePath $Entry.Value)
}

# Guard against accidental duplicate out-of-class bodies in large UI
# translation units. MSVC reports these late during compilation, so fail early
# with the exact repeated definitions that have previously broken Windows builds.
function Assert-UniqueSourceDefinition {
    param(
        [string]$File,
        [string[]]$Definitions
    )

    if (-not (Test-Path $File)) {
        Write-Error "Source file not found: $File"
        exit 1
    }

    $Text = Get-Content -Raw -Path $File
    foreach ($Definition in $Definitions) {
        $Count = ([regex]::Matches($Text, [regex]::Escape($Definition))).Count
        if ($Count -gt 1) {
            Write-Error "Duplicate definition detected in ${File}: '$Definition' appears $Count times. Remove the duplicate body before building."
            exit 1
        }
    }
}

$TitleEditorSource = Assert-RepoFileExists -Name "EditorTitleEditor" -RelativePath $ModuleSources.EditorTitleEditor
Assert-UniqueSourceDefinition -File $TitleEditorSource -Definitions @(
    "void TitleEditor::keyPressEvent(",
    "void CanvasPreview::set_safe_guides_visible(",
    "void CanvasPreview::refresh_preview(",
    "std::shared_ptr<Layer> CanvasPreview::selected_layer(",
    "QRectF CanvasPreview::layer_local_rect(",
    "double CanvasPreview::view_scale(",
    "QPointF CanvasPreview::view_origin(",
    "QPointF CanvasPreview::view_to_canvas(",
    "QPointF CanvasPreview::canvas_to_view(",
    "QPointF CanvasPreview::canvas_to_layer(",
    "QPointF CanvasPreview::layer_to_canvas(",
    "CanvasPreview::DragMode CanvasPreview::hit_test_selected(",
    "void CanvasPreview::apply_drag(",
    "void TimelineWidget::contextMenuEvent(",
    "void TimelineWidget::wheelEvent(",
    "TitlePropertiesPanel::TitlePropertiesPanel(",
    "void TitlePropertiesPanel::set_title(",
    "void TitlePropertiesPanel::load_values("
)

$TitleDockSource = Assert-RepoFileExists -Name "EditorTitleDock" -RelativePath $ModuleSources.EditorTitleDock
Assert-UniqueSourceDefinition -File $TitleDockSource -Definitions @(
    "void TitleDock::select_title(",
    "std::shared_ptr<Title> TitleDock::create_template_title(",
    "void TitleDock::create_title_from_template("
)

# 1. Verify CMake and Visual Studio
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake and add it to your PATH."
    exit 1
}

# 2. Verify vcpkg toolchain
if (-not (Test-Path $VcpkgToolchain)) {
    Write-Error "vcpkg toolchain not found at $VcpkgToolchain. Pass -VcpkgDir or set VCPKG_ROOT."
    exit 1
}

# 3. Detect OBS SDK/build dependencies without machine-specific paths.
function Test-ObsSdkDir {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
        return $false
    }

    $HasObsLib = (Test-Path (Join-Path $Path "lib\obs.lib")) -or
                 (Test-Path (Join-Path $Path "lib\obs\obs.lib"))
    $HasObsFrontendLib = (Test-Path (Join-Path $Path "lib\obs-frontend-api.lib")) -or
                         (Test-Path (Join-Path $Path "lib\obs\obs-frontend-api.lib"))
    $HasObsHeader = (Test-Path (Join-Path $Path "include\obs.h")) -or
                    (Test-Path (Join-Path $Path "include\obs\obs.h"))
    return ($HasObsLib -and $HasObsFrontendLib -and $HasObsHeader)
}

if (-not (Test-ObsSdkDir $ObsSdkDir)) {
    $CandidateRoots = @(
        (Join-Path $env:USERPROFILE "Desktop\obs-build-dependencies"),
        (Join-Path $env:USERPROFILE "Downloads\obs-build-dependencies"),
        (Join-Path $ScriptDir "obs-build-dependencies"),
        (Join-Path $env:ProgramFiles "obs-studio")
    )

    if ($env:ProgramW6432) {
        $CandidateRoots += (Join-Path $env:ProgramW6432 "obs-studio")
    }

    foreach ($Root in $CandidateRoots) {
        if ([string]::IsNullOrWhiteSpace($Root) -or -not (Test-Path $Root)) {
            continue
        }

        $Candidates = @($Root)
        $Candidates += @(Get-ChildItem -Path $Root -Directory -Filter "plugin-deps-*" -ErrorAction SilentlyContinue |
                         Sort-Object Name -Descending |
                         ForEach-Object { $_.FullName })

        foreach ($Candidate in $Candidates) {
            if (Test-ObsSdkDir $Candidate) {
                $ObsSdkDir = $Candidate
                break
            }
        }

        if (Test-ObsSdkDir $ObsSdkDir) {
            break
        }
    }
}

if (-not (Test-ObsSdkDir $ObsSdkDir)) {
    Write-Error "Could not locate an OBS SDK/install tree. Pass -ObsSdkDir or set OBS_SDK_DIR/OBS_STUDIO_DIR."
    exit 1
}
Write-Host "Found OBS SDK: $ObsSdkDir"
Write-Host "vcpkg manifest install root: $VcpkgInstalledDir"

# Resolve Qt from the same OBS dependency bundle. Reusing a CMake cache after
# the OBS SDK changes can otherwise mix headers, moc output and libraries from
# two Qt ABIs (for example Qt 6.8.3 and Qt 6.11.1).
$Qt6ConfigCandidates = @(
    (Join-Path $ObsSdkDir "lib\cmake\Qt6\Qt6Config.cmake"),
    (Join-Path $ObsSdkDir "cmake\Qt6\Qt6Config.cmake"),
    (Join-Path $ObsSdkDir "share\cmake\Qt6\Qt6Config.cmake"),
    (Join-Path $ObsSdkDir "lib64\cmake\Qt6\Qt6Config.cmake")
)
$Qt6ConfigPath = $Qt6ConfigCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (-not $Qt6ConfigPath) {
    Write-Error "Qt6Config.cmake was not found inside the selected OBS SDK: $ObsSdkDir"
    exit 1
}
$Qt6Dir = Split-Path -Parent $Qt6ConfigPath
$QtVersion = "unknown"
$QtVersionImpl = Join-Path $Qt6Dir "Qt6ConfigVersionImpl.cmake"
if (Test-Path -LiteralPath $QtVersionImpl) {
    $QtVersionText = Get-Content -Raw -LiteralPath $QtVersionImpl
    $QtVersionMatch = [regex]::Match(
        $QtVersionText,
        '(?im)^\s*set\s*\(\s*PACKAGE_VERSION\s+"([^"]+)"\s*\)'
    )
    if ($QtVersionMatch.Success) {
        $QtVersion = $QtVersionMatch.Groups[1].Value
    }
}
Write-Host "Found Qt: $QtVersion at $Qt6Dir"

function Get-CMakeCachePathValue {
    param(
        [string]$CachePath,
        [string]$VariableName
    )

    if (-not (Test-Path -LiteralPath $CachePath)) {
        return ""
    }
    $CacheText = Get-Content -Raw -LiteralPath $CachePath
    $Match = [regex]::Match(
        $CacheText,
        '(?im)^' + [regex]::Escape($VariableName) + ':[^=]*=(.*)$'
    )
    if (-not $Match.Success) {
        return ""
    }
    return $Match.Groups[1].Value.Trim()
}

function Test-SameFullPath {
    param(
        [string]$Left,
        [string]$Right
    )

    if ([string]::IsNullOrWhiteSpace($Left) -or
        [string]::IsNullOrWhiteSpace($Right)) {
        return $false
    }
    return [IO.Path]::GetFullPath($Left).Equals(
        [IO.Path]::GetFullPath($Right),
        [StringComparison]::OrdinalIgnoreCase)
}

$CMakeCachePath = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path -LiteralPath $CMakeCachePath) {
    $CachedObsSdkDir = Get-CMakeCachePathValue -CachePath $CMakeCachePath -VariableName "OBS_SDK_DIR"
    $CachedQt6Dir = Get-CMakeCachePathValue -CachePath $CMakeCachePath -VariableName "Qt6_DIR"
    $SdkChanged = -not (Test-SameFullPath -Left $CachedObsSdkDir -Right $ObsSdkDir)
    $QtChanged = -not (Test-SameFullPath -Left $CachedQt6Dir -Right $Qt6Dir)

    if ($SdkChanged -or $QtChanged) {
        $ResolvedBuildDir = [IO.Path]::GetFullPath($BuildDir)
        $ResolvedScriptDir = [IO.Path]::GetFullPath($ScriptDir)
        if ($ResolvedBuildDir -eq $ResolvedScriptDir -or
            $ResolvedBuildDir -eq [IO.Path]::GetPathRoot($ResolvedBuildDir)) {
            Write-Error "Refusing to reset unsafe build path: $ResolvedBuildDir"
            exit 1
        }
        Write-Host "OBS/Qt ABI changed; resetting generated build directory:"
        Write-Host "  cached OBS SDK: $CachedObsSdkDir"
        Write-Host "  selected OBS SDK: $ObsSdkDir"
        Write-Host "  cached Qt: $CachedQt6Dir"
        Write-Host "  selected Qt: $Qt6Dir"
        Remove-Item -Recurse -Force -LiteralPath $ResolvedBuildDir
    }
}

# 4. Configure CMake
Write-Host "`n=== Configuring CMake ==="
$CmakeArgs = @(
    "-S", $ScriptDir,
    "-B", $BuildDir,
    "-G", $Generator,
    "-A", $Architecture,
    "-DCMAKE_TOOLCHAIN_FILE=$($VcpkgToolchain.Replace('\', '/'))",
    "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet",
    "-DVCPKG_INSTALLED_DIR=$($VcpkgInstalledDir.Replace('\', '/'))",
    "-DOBS_SDK_DIR=$($ObsSdkDir.Replace('\', '/'))",
    "-DQt6_DIR=$($Qt6Dir.Replace('\', '/'))",
    "-DOBS_FXM_BUILD_TESTS=$(if ($BuildTests) { 'ON' } else { 'OFF' })"
)
& cmake @CmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed."
    exit 1
}

# 5. Build the Plugin
Write-Host "`n=== Building Flux Motion ($Configuration) ==="
# Build only the plugin target. CMake/MSBuild keeps object-file dependency
# tracking, so unchanged translation units are reused. Do not pass -Clean for
# normal development builds; -Clean intentionally discards incremental state.
& cmake --build $BuildDir --config $Configuration `
    --target flux-motion-obs-plugin flux-motion-editor flux-motion-renderer -- /m
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed."
    exit 1
}

if ($BuildTests) {
    Write-Host "`n=== Building Flux Motion validation targets ==="
    & cmake --build $BuildDir --config $Configuration --target ALL_BUILD -- /m
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Test build failed."
        exit 1
    }
    Write-Host "`n=== Running Flux Motion tests ==="
    & ctest --test-dir $BuildDir -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Tests failed."
        exit 1
    }
}

# 6. Assemble the final OBS plugin package in the build tree. The same staged
# tree is used for both the OBS installation and the distribution ZIP so the
# two deliverables cannot silently diverge.
Write-Host "`n=== Assembling final OBS plugin package ==="
$StagedPluginRoot = Join-Path $BuildDir $PluginName
$StagedPluginBin = Join-Path $StagedPluginRoot "bin\$ObsArchDir"
$StagedPluginData = Join-Path $StagedPluginRoot "data"
$StagedPluginLocale = Join-Path $StagedPluginData "locale"
if (Test-Path -LiteralPath $StagedPluginRoot) {
    $ResolvedStage = [IO.Path]::GetFullPath($StagedPluginRoot)
    if (-not $ResolvedStage.StartsWith(([IO.Path]::GetFullPath($BuildDir).TrimEnd('\') + '\'), [StringComparison]::OrdinalIgnoreCase)) {
        Write-Error "Refusing to clean unsafe staged plugin path: $ResolvedStage"
        exit 1
    }
    Remove-Item -Recurse -Force -LiteralPath $StagedPluginRoot
}
New-Item -ItemType Directory -Force -Path $StagedPluginBin | Out-Null
New-Item -ItemType Directory -Force -Path $StagedPluginLocale | Out-Null

$BuiltDllCandidates = @(
    (Join-Path $BuildDir "flux-motion-obs-plugin\bin\$ObsArchDir\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\$Configuration\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\Release\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\$PluginDllName"),
    (Join-Path $BuildDir "obs-plugins\$ObsArchDir\$PluginDllName"),
    (Join-Path $BuildDir "$Configuration\$PluginDllName"),
    (Join-Path $BuildDir "Release\$PluginDllName"),
    (Join-Path $BuildDir "RelWithDebInfo\$PluginDllName"),
    (Join-Path $BuildDir "Debug\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\$Configuration\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\Release\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\RelWithDebInfo\$PluginDllName"),
    (Join-Path $StagedPluginRoot "bin\$ObsArchDir\Debug\$PluginDllName")
)
$BuiltDll = $BuiltDllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $BuiltDll) {
    $RecursiveMatch = Get-ChildItem -Path $BuildDir -Filter $PluginDllName -Recurse -File -ErrorAction SilentlyContinue |
        Sort-Object @{ Expression = { if ($_.FullName -like "*\flux-motion-obs-plugin\bin\$ObsArchDir*") { 0 } else { 1 } } }, FullName |
        Select-Object -First 1
    if ($RecursiveMatch) {
        $BuiltDll = $RecursiveMatch.FullName
    }
}

if (-not $BuiltDll) {
    Write-Error "Could not find built $PluginDllName. Checked known locations: $($BuiltDllCandidates -join ', '). Also searched recursively under $BuildDir."
    exit 1
}

$StagedDllPath = Join-Path $StagedPluginBin $PluginDllName
if (-not ([IO.Path]::GetFullPath($BuiltDll).Equals([IO.Path]::GetFullPath($StagedDllPath), [StringComparison]::OrdinalIgnoreCase))) {
    Copy-Item -Force -LiteralPath $BuiltDll -Destination $StagedDllPath
}
Write-Host "Staged plugin DLL: $StagedDllPath"

$SourceData = Join-Path $ScriptDir "data"
if (Test-Path $SourceData) {
    Copy-Item -Force -Recurse (Join-Path $SourceData "*") $StagedPluginData
    Write-Host "Staged plugin data: $StagedPluginData"
}

# 7. Copy runtime DLL dependencies into the staged package. A plugin can compile
# and still fail to load in OBS if Qt/Cairo/Pango DLLs are missing.
Write-Host "`n=== Staging runtime DLL dependencies ==="
$RuntimeDllDirs = @()
$VcpkgTriplets = @($Architecture.ToLower())
if ($Architecture -eq "x64") {
    $VcpkgTriplets += "x64-windows"
} elseif ($Architecture -eq "Win32" -or $Architecture -eq "x86") {
    $VcpkgTriplets += "x86-windows"
} elseif ($Architecture -eq "arm64") {
    $VcpkgTriplets += "arm64-windows"
}

$VcpkgRuntimeRoots = @(
    $VcpkgInstalledDir,
    (Join-Path $VcpkgDir "installed")
)

foreach ($RuntimeRoot in ($VcpkgRuntimeRoots | Select-Object -Unique)) {
    foreach ($Triplet in ($VcpkgTriplets | Select-Object -Unique)) {
        $CandidateBin = Join-Path $RuntimeRoot "$Triplet\bin"
        if (Test-Path $CandidateBin) {
            $RuntimeDllDirs += $CandidateBin
        }
    }
}

$CopiedCount = 0
foreach ($RuntimeDllDir in ($RuntimeDllDirs | Select-Object -Unique)) {
    Write-Host "Copying DLLs from: $RuntimeDllDir"
    $RuntimeDlls = Get-ChildItem -Path $RuntimeDllDir -Filter "*.dll" -File -ErrorAction SilentlyContinue
    foreach ($Dll in $RuntimeDlls) {
        Copy-Item -Force -LiteralPath $Dll.FullName -Destination $StagedPluginBin
        $CopiedCount++
    }
}

if ($CopiedCount -eq 0) {
    Write-Warning "No vcpkg runtime DLLs were staged. If OBS says $PluginName failed to load, check for missing Qt/Cairo/Pango DLLs in $StagedPluginBin."
} else {
    Write-Host "Staged $CopiedCount runtime DLL dependencies."
}

$ExpectedDlls = @(
    $PluginDllName,
    "cairo-2.dll",
    "pango-1.0-0.dll",
    "pangocairo-1.0-0.dll"
)
$MissingExpectedDlls = @()
foreach ($Dll in $ExpectedDlls) {
    if (-not (Test-Path (Join-Path $StagedPluginBin $Dll))) {
        $MissingExpectedDlls += $Dll
    }
}
if ($MissingExpectedDlls.Count -gt 0) {
    Write-Warning "The staged package is missing expected DLL(s): $($MissingExpectedDlls -join ', '). OBS may report that $PluginName failed to load."
}

# 8. Install the exact staged package into the configured OBS plugins folder.
if ($SkipInstall) {
    Write-Host "`n=== Skipping OBS install because -SkipInstall was set. ==="
} else {
    Write-Host "`n=== Installing final package to OBS ==="
    Write-Host "Install root: $InstallRoot"
    New-Item -ItemType Directory -Force -Path $ObsPluginRoot | Out-Null
    Copy-Item -Force -Recurse (Join-Path $StagedPluginRoot "*") $ObsPluginRoot
    Write-Host "Installed package to: $ObsPluginRoot"
}

# 9. Create a redistributable ZIP directly inside the build folder (or the
# configured output directory) using the requested deterministic naming scheme:
# <name>_<version>_<development-version>_<platform>.zip
if ($SkipPackage) {
    Write-Host "`n=== Skipping distribution ZIP because -SkipPackage was set. ==="
} else {
    Write-Host "`n=== Creating distribution ZIP ==="
    $ResolvedStagedRoot = [IO.Path]::GetFullPath($StagedPluginRoot).TrimEnd([char[]]@('\', '/'))
    $ResolvedPackageOutput = [IO.Path]::GetFullPath($PackageOutputDir).TrimEnd([char[]]@('\', '/'))
    if ($ResolvedPackageOutput.Equals($ResolvedStagedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        $ResolvedPackageOutput.StartsWith($ResolvedStagedRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        Write-Error "PackageOutputDir cannot be inside the staged plugin folder: $ResolvedStagedRoot"
        exit 1
    }
    # Product IDs remain stable in the installer, but retired public folder
    # names must not survive in a freshly assembled distribution tree.
    foreach ($LegacyDistributionDir in @(
        (Join-Path $PackageOutputDir "Flux Motion Editor"),
        (Join-Path $PackageOutputDir "Flux Motion OBS Plugin"))) {
        $ResolvedLegacyDir = [IO.Path]::GetFullPath($LegacyDistributionDir)
        if (-not $ResolvedLegacyDir.StartsWith(
                $ResolvedPackageOutput + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            Write-Error "Refusing to remove unsafe legacy distribution path: $ResolvedLegacyDir"
            exit 1
        }
        if (Test-Path -LiteralPath $ResolvedLegacyDir) {
            Remove-Item -Recurse -Force -LiteralPath $ResolvedLegacyDir
        }
    }
    foreach ($DistributionDir in @($PluginDistributionDir, $EditorDistributionDir)) {
        if (Test-Path -LiteralPath $DistributionDir) {
            Remove-Item -Recurse -Force -LiteralPath $DistributionDir
        }
        New-Item -ItemType Directory -Force -Path $DistributionDir | Out-Null
    }
    Copy-Item -Force -LiteralPath (Join-Path $ScriptDir "VERSION.txt") -Destination $StagedPluginRoot
    $ObsPluginChangelog = Join-Path $RepositoryRoot "plugins\obs\flux-motion\CHANGELOG.md"
    Copy-Item -Force -LiteralPath $ObsPluginChangelog -Destination $StagedPluginRoot
    Copy-Item -Recurse -Force -LiteralPath $StagedPluginRoot -Destination $PluginDistributionDir
    Copy-Item -Force -LiteralPath (Join-Path $ScriptDir "VERSION.txt") -Destination $PluginDistributionDir

    $EditorRuntimeSource = Join-Path $BuildDir "flux-motion-editor\bin"
    if (-not (Test-Path (Join-Path $EditorRuntimeSource "Flux Motion.exe"))) {
        Write-Error "Standalone Editor runtime was not found: $EditorRuntimeSource"
        exit 1
    }
    Copy-Item -Recurse -Force (Join-Path $EditorRuntimeSource "*") $EditorDistributionDir
    Copy-Item -Force -LiteralPath (Join-Path $ScriptDir "VERSION.txt") -Destination $EditorDistributionDir
    Copy-Item -Force -LiteralPath (Join-Path $ScriptDir "docs\CHANGELOG.md") -Destination $EditorDistributionDir
    foreach ($ExcludedEditorArtifact in @(
        "Flux Motion.pdb",
        "flux-motion-renderer.exe",
        "flux-motion-renderer.pdb",
        "flux-motion-editor.exe",
        "flux-motion-editor.pdb")) {
        $ExcludedPath = Join-Path $EditorDistributionDir $ExcludedEditorArtifact
        if (Test-Path -LiteralPath $ExcludedPath) { Remove-Item -Force -LiteralPath $ExcludedPath }
    }
    if (Test-Path -LiteralPath $PackageZipPath) {
        Remove-Item -Force -LiteralPath $PackageZipPath
    }
    Compress-Archive -LiteralPath $StagedPluginRoot -DestinationPath $PackageZipPath -CompressionLevel Optimal -Force
    if (-not (Test-Path -LiteralPath $PackageZipPath)) {
        Write-Error "Distribution ZIP was not created: $PackageZipPath"
        exit 1
    }
    Write-Host "Created package ZIP: $PackageZipPath"
    $EditorArchiveItems = Get-ChildItem -LiteralPath $EditorDistributionDir -Force |
        Select-Object -ExpandProperty FullName
    Compress-Archive -LiteralPath $EditorArchiveItems -DestinationPath $EditorPackageZipPath -CompressionLevel Optimal -Force
    Write-Host "Created Editor ZIP: $EditorPackageZipPath"
}

Write-Host "`nFinal staged OBS plugin layout:"
Write-Host "  $StagedPluginRoot"
Write-Host "  $StagedPluginBin\$PluginDllName"
Write-Host "  $StagedPluginLocale\en-US.ini"
if (-not $SkipInstall) {
    Write-Host "Installed to OBS: $ObsPluginRoot"
}
if (-not $SkipPackage) {
    Write-Host "Distribution ZIP: $PackageZipPath"
    Write-Host "Editor distribution: $EditorDistributionDir"
    Write-Host "Editor ZIP: $EditorPackageZipPath"
}
Write-Host "`n=== Flux Motion build workflow completed successfully! ==="
