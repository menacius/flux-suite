# update-and-build.ps1
# Applies the newest complete source ZIP without touching unchanged files, then
# invokes build-windows.ps1. Unchanged timestamps and the existing build tree
# are preserved, allowing CMake/MSBuild to perform a real incremental build.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch]$Clean,
    [switch]$BuildOnly,
    [switch]$UpdateOnly,
    [switch]$NoArchive,
    [string]$ZipPath,
    [string]$ConfigPath,

    # Allows the .bat wrapper to accept friendly forms such as /clean or clean.
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$RemainingArguments
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$ScriptRevision = '2026-06-28.3-null-manifest-diagnostics'

function Write-FatalError {
    param([object]$ErrorRecord)

    $ExceptionMessage = 'Unknown PowerShell error.'
    if ($null -ne $ErrorRecord) {
        $ExceptionObject = $ErrorRecord.Exception
        if ($null -ne $ExceptionObject -and
            -not [string]::IsNullOrWhiteSpace([string]$ExceptionObject.Message)) {
            $ExceptionMessage = [string]$ExceptionObject.Message
        } elseif (-not [string]::IsNullOrWhiteSpace([string]$ErrorRecord)) {
            $ExceptionMessage = [string]$ErrorRecord
        }
    }

    Write-Host "`nUpdate/build failed: $ExceptionMessage" -ForegroundColor Red

    $Invocation = $null
    if ($null -ne $ErrorRecord) {
        $Invocation = $ErrorRecord.InvocationInfo
    }
    if ($null -ne $Invocation) {
        $InvocationScript = [string]$Invocation.ScriptName
        $InvocationLineNumber = [int]$Invocation.ScriptLineNumber
        if (-not [string]::IsNullOrWhiteSpace($InvocationScript) -and $InvocationLineNumber -gt 0) {
            Write-Host "Location: $InvocationScript`:$InvocationLineNumber" -ForegroundColor DarkYellow
        } elseif ($InvocationLineNumber -gt 0) {
            Write-Host "Line: $InvocationLineNumber" -ForegroundColor DarkYellow
        }

        $InvocationCommand = [string]$Invocation.Line
        if (-not [string]::IsNullOrWhiteSpace($InvocationCommand)) {
            Write-Host ("Command: " + ([string]$InvocationCommand).Trim()) -ForegroundColor DarkYellow
        }
    }

    if ($null -ne $ErrorRecord -and $null -ne $ErrorRecord.ScriptStackTrace) {
        $StackText = [string]$ErrorRecord.ScriptStackTrace
        if (-not [string]::IsNullOrWhiteSpace($StackText)) {
            Write-Host "Stack: $StackText" -ForegroundColor DarkGray
        }
    }
}

function Write-Step {
    param([string]$Message)
    Write-Host "`n=== $Message ===" -ForegroundColor Cyan
}

function Get-ConfigValue {
    param(
        [object]$Config,
        [string]$Name,
        [object]$DefaultValue
    )

    if ($null -eq $Config) {
        return $DefaultValue
    }

    $Property = $Config.PSObject.Properties[$Name]
    if ($null -eq $Property -or $null -eq $Property.Value) {
        return $DefaultValue
    }
    return $Property.Value
}

function Expand-ConfiguredPath {
    param(
        [string]$Value,
        [string]$BasePath
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }

    $Expanded = [Environment]::ExpandEnvironmentVariables([string]$Value)
    if ([string]::IsNullOrWhiteSpace($Expanded)) {
        return ""
    }
    if ([IO.Path]::IsPathRooted($Expanded)) {
        return [IO.Path]::GetFullPath($Expanded)
    }
    if ([string]::IsNullOrWhiteSpace($BasePath)) {
        throw "Cannot resolve relative path '$Expanded' because the base path is empty."
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Expanded))
}

function Get-RelativeProjectPath {
    param(
        [string]$BasePath,
        [string]$FullPath
    )

    if ([string]::IsNullOrWhiteSpace($BasePath) -or [string]::IsNullOrWhiteSpace($FullPath)) {
        throw 'BasePath and FullPath must both be non-empty.'
    }

    $Base = ([IO.Path]::GetFullPath($BasePath)).TrimEnd([char[]]@('\', '/'))
    $Path = [IO.Path]::GetFullPath($FullPath)
    $IsExactBase = $Path.Equals($Base, [StringComparison]::OrdinalIgnoreCase)
    $IsChildPath = $Path.StartsWith($Base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                   $Path.StartsWith($Base + [IO.Path]::AltDirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
    if (-not $IsExactBase -and -not $IsChildPath) {
        throw "Path is outside project root: $Path"
    }

    return $Path.Substring($Base.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
}

function Test-IsProtectedPath {
    param(
        [string]$RelativePath,
        [string]$ConfigRelativePath,
        [string]$BuildRelativePath
    )

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $true
    }
    $Path = ([string]$RelativePath).Replace('\', '/').TrimStart('/')
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $true
    }

    if ($Path -match '^(\.git|\.vs|\.fxm-update)(/|$)') {
        return $true
    }
    if ($Path -match '^(build|build-[^/]+|cmake-build-[^/]+)(/|$)') {
        return $true
    }
    if (-not [string]::IsNullOrWhiteSpace($BuildRelativePath) -and
        ($Path -eq $BuildRelativePath -or $Path.StartsWith($BuildRelativePath + '/', [StringComparison]::OrdinalIgnoreCase))) {
        return $true
    }
    if (-not [string]::IsNullOrWhiteSpace($ConfigRelativePath) -and
        $Path.Equals($ConfigRelativePath, [StringComparison]::OrdinalIgnoreCase)) {
        return $true
    }

    return $false
}


function Test-IsUpdaterInfrastructurePath {
    param([string]$RelativePath)

    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return $false
    }
    $Path = ([string]$RelativePath).Replace('\', '/').TrimStart('/').ToLowerInvariant()
    return $Path -in @(
        'update-and-build.ps1',
        'update-and-build.bat',
        'update-and-build.config.json'
    )
}

function Get-Sha256 {
    param([string]$Path)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Cannot hash missing file: $Path"
    }

    $HashResult = Get-FileHash -Algorithm SHA256 -LiteralPath $Path
    if ($null -eq $HashResult -or [string]::IsNullOrWhiteSpace([string]$HashResult.Hash)) {
        throw "Get-FileHash did not return a SHA-256 value for: $Path"
    }
    return ([string]$HashResult.Hash).ToLowerInvariant()
}

function Read-JsonFile {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $null
    }

    $JsonText = Get-Content -Raw -LiteralPath $Path
    if ([string]::IsNullOrWhiteSpace($JsonText)) {
        throw "JSON file is empty: $Path"
    }

    try {
        return ($JsonText | ConvertFrom-Json)
    } catch {
        throw "Invalid JSON in '$Path': $($_.Exception.Message)"
    }
}

function Write-JsonFile {
    param(
        [object]$Value,
        [string]$Path,
        [int]$Depth = 8
    )

    $Parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($Parent)) {
        New-Item -ItemType Directory -Force -Path $Parent | Out-Null
    }

    $Temporary = "$Path.tmp"
    ConvertTo-Json -InputObject $Value -Depth $Depth | Set-Content -LiteralPath $Temporary -Encoding UTF8
    Copy-Item -Force -LiteralPath $Temporary -Destination $Path
    Remove-Item -Force -LiteralPath $Temporary
}

function Find-PackageRoot {
    param([string]$ExtractedPath)

    if ((Test-Path (Join-Path $ExtractedPath 'CMakeLists.txt')) -and
        (Test-Path (Join-Path $ExtractedPath 'src'))) {
        return $ExtractedPath
    }

    $Candidates = @(Get-ChildItem -LiteralPath $ExtractedPath -Filter 'build-windows.ps1' -File -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Directory.FullName } |
        Where-Object {
            (Test-Path (Join-Path $_ 'CMakeLists.txt')) -and
            (Test-Path (Join-Path $_ 'src'))
        } |
        Sort-Object { $_.Length })

    if ($Candidates.Count -eq 0) {
        throw "Could not locate the project root inside the ZIP. Expected CMakeLists.txt, build-windows.ps1, and src/."
    }

    return $Candidates[0]
}

function Get-PackageManifest {
    param(
        [string]$SourceRoot,
        [string]$ConfigRelativePath,
        [string]$BuildRelativePath
    )

    $Manifest = New-Object System.Collections.Generic.List[object]
    $Files = Get-ChildItem -LiteralPath $SourceRoot -File -Recurse | Sort-Object FullName
    foreach ($File in $Files) {
        $Relative = Get-RelativeProjectPath -BasePath $SourceRoot -FullPath $File.FullName
        if (Test-IsProtectedPath -RelativePath $Relative -ConfigRelativePath $ConfigRelativePath -BuildRelativePath $BuildRelativePath) {
            continue
        }

        $Manifest.Add([pscustomobject]@{
            Path   = $Relative
            Length = [long]$File.Length
            Hash   = Get-Sha256 -Path $File.FullName
        })
    }
    # Windows PowerShell 5.1 can throw "Argument types do not match" when
    # @() is applied directly to a generic List[object]. Convert it to a
    # real CLR object array before returning it to the pipeline.
    return $Manifest.ToArray()
}

function Copy-WithRollbackBackup {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$RelativePath,
        [string]$BackupRoot,
        [bool]$EnableBackup
    )

    $DestinationParent = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Force -Path $DestinationParent | Out-Null

    if ($EnableBackup -and (Test-Path -LiteralPath $Destination)) {
        $BackupPath = Join-Path $BackupRoot ($RelativePath.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $BackupPath) | Out-Null
        Copy-Item -Force -LiteralPath $Destination -Destination $BackupPath
    }

    $TemporaryDestination = "$Destination.fxm-update-new"
    Copy-Item -Force -LiteralPath $Source -Destination $TemporaryDestination
    Copy-Item -Force -LiteralPath $TemporaryDestination -Destination $Destination
    Remove-Item -Force -LiteralPath $TemporaryDestination
}

function Remove-WithRollbackBackup {
    param(
        [string]$Destination,
        [string]$RelativePath,
        [string]$BackupRoot,
        [bool]$EnableBackup
    )

    if (-not (Test-Path -LiteralPath $Destination)) {
        return
    }

    if ($EnableBackup) {
        $BackupPath = Join-Path $BackupRoot ($RelativePath.Replace('/', '\'))
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $BackupPath) | Out-Null
        Copy-Item -Force -LiteralPath $Destination -Destination $BackupPath
    }

    Remove-Item -Force -LiteralPath $Destination
}

function Remove-EmptyParentDirectories {
    param(
        [string]$StartPath,
        [string]$StopPath
    )

    $Current = Split-Path -Parent $StartPath
    $Stop = [IO.Path]::GetFullPath($StopPath).TrimEnd([char[]]@('\', '/'))
    while (-not [string]::IsNullOrWhiteSpace($Current)) {
        $FullCurrent = [IO.Path]::GetFullPath($Current).TrimEnd([char[]]@('\', '/'))
        if ($FullCurrent.Equals($Stop, [StringComparison]::OrdinalIgnoreCase)) {
            break
        }
        if ((Get-ChildItem -LiteralPath $FullCurrent -Force | Select-Object -First 1)) {
            break
        }
        Remove-Item -Force -LiteralPath $FullCurrent
        $Current = Split-Path -Parent $FullCurrent
    }
}

function Invoke-PackageSync {
    param(
        [string]$SourceRoot,
        [string]$ProjectRoot,
        [object[]]$NewManifest,
        [object[]]$PreviousManifest,
        [string]$BackupRoot,
        [bool]$EnableBackup,
        [string]$ConfigRelativePath,
        [string]$BuildRelativePath
    )

    $NewByPath = @{}
    foreach ($Item in @($NewManifest)) {
        if ($null -eq $Item) {
            continue
        }
        $ManifestPath = [string]$Item.Path
        if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
            throw 'The new package manifest contains an entry without a Path.'
        }
        $NewByPath[$ManifestPath.ToLowerInvariant()] = $Item
    }

    $Added = 0
    $Updated = 0
    $Unchanged = 0
    $Deleted = 0

    # Update the updater itself last, so the currently executing script is never
    # replaced before all other source files have been synchronized.
    $Deferred = @('update-and-build.ps1', 'update-and-build.bat')
    $OrderedManifest = @($NewManifest | Sort-Object -Property @(
        @{ Expression = {
            $SortPath = if ($null -eq $_) { '' } else { [string]$_.Path }
            if (-not [string]::IsNullOrWhiteSpace($SortPath) -and $Deferred -contains $SortPath.ToLowerInvariant()) { 1 } else { 0 }
        } },
        @{ Expression = { $_.Path } }
    ))

    foreach ($Item in $OrderedManifest) {
        if ($null -eq $Item) {
            continue
        }
        $Relative = [string]$Item.Path
        if ([string]::IsNullOrWhiteSpace($Relative)) {
            throw 'The ordered package manifest contains an entry without a Path.'
        }
        $Source = Join-Path $SourceRoot ($Relative.Replace('/', '\'))
        $Destination = Join-Path $ProjectRoot ($Relative.Replace('/', '\'))
        $NeedsCopy = $true
        $Existed = Test-Path -LiteralPath $Destination

        if ($Existed) {
            $DestinationInfo = Get-Item -LiteralPath $Destination
            if ([long]$DestinationInfo.Length -eq [long]$Item.Length) {
                $DestinationHash = Get-Sha256 -Path $Destination
                if ($DestinationHash -eq [string]$Item.Hash) {
                    $NeedsCopy = $false
                }
            }
        }

        if (-not $NeedsCopy) {
            $Unchanged++
            continue
        }

        Copy-WithRollbackBackup -Source $Source -Destination $Destination -RelativePath $Relative -BackupRoot $BackupRoot -EnableBackup $EnableBackup
        if ($Existed) { $Updated++ } else { $Added++ }
    }

    # Delete only files known to have belonged to the previous managed ZIP.
    # Local files that were never present in a package are never touched.
    foreach ($OldItem in @($PreviousManifest)) {
        if ($null -eq $OldItem) {
            continue
        }
        $Relative = [string]$OldItem.Path
        if ([string]::IsNullOrWhiteSpace($Relative)) {
            Write-Warning 'Ignoring an invalid previous-manifest entry without a Path.'
            continue
        }
        if ($NewByPath.ContainsKey($Relative.ToLowerInvariant())) {
            continue
        }
        if (Test-IsProtectedPath -RelativePath $Relative -ConfigRelativePath $ConfigRelativePath -BuildRelativePath $BuildRelativePath) {
            continue
        }
        # Keep the local updater available even if an older or third-party ZIP
        # does not contain the updater files.
        if (Test-IsUpdaterInfrastructurePath -RelativePath $Relative) {
            continue
        }

        $Destination = Join-Path $ProjectRoot ($Relative.Replace('/', '\'))
        if (Test-Path -LiteralPath $Destination) {
            Remove-WithRollbackBackup -Destination $Destination -RelativePath $Relative -BackupRoot $BackupRoot -EnableBackup $EnableBackup
            Remove-EmptyParentDirectories -StartPath $Destination -StopPath $ProjectRoot
            $Deleted++
        }
    }

    return [pscustomobject]@{
        Added     = $Added
        Updated   = $Updated
        Unchanged = $Unchanged
        Deleted   = $Deleted
    }
}

function Limit-BackupHistory {
    param(
        [string]$BackupsRoot,
        [int]$MaximumBackups
    )

    if ($MaximumBackups -lt 1 -or -not (Test-Path -LiteralPath $BackupsRoot)) {
        return
    }

    $Backups = @(Get-ChildItem -LiteralPath $BackupsRoot -Directory |
        Sort-Object LastWriteTimeUtc -Descending)
    if ($Backups.Count -le $MaximumBackups) {
        return
    }

    $Backups | Select-Object -Skip $MaximumBackups | ForEach-Object {
        Remove-Item -Recurse -Force -LiteralPath $_.FullName
    }
}

function Move-PackageToArchive {
    param(
        [string]$PackagePath,
        [string]$ProcessedDirectory
    )

    if (-not (Test-Path -LiteralPath $PackagePath)) {
        return $PackagePath
    }

    New-Item -ItemType Directory -Force -Path $ProcessedDirectory | Out-Null
    $Destination = Join-Path $ProcessedDirectory ([IO.Path]::GetFileName($PackagePath))
    if (Test-Path -LiteralPath $Destination) {
        $BaseName = [IO.Path]::GetFileNameWithoutExtension($PackagePath)
        $Extension = [IO.Path]::GetExtension($PackagePath)
        $Suffix = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        $Destination = Join-Path $ProcessedDirectory ("$BaseName-$Suffix$Extension")
    }

    Move-Item -LiteralPath $PackagePath -Destination $Destination
    return $Destination
}

function Complete-PendingUpdate {
    param(
        [object]$Pending,
        [string]$PendingPath,
        [string]$CompletedStatePath,
        [string]$CompletedManifestPath,
        [bool]$ArchivePackage,
        [string]$DefaultProcessedDirectory
    )

    $ManifestPath = [string]$Pending.ManifestPath
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "Pending update manifest is missing: $ManifestPath"
    }

    Copy-Item -Force -LiteralPath $ManifestPath -Destination $CompletedManifestPath
    $FinalPackagePath = [string]$Pending.PackagePath
    $ProcessedDirectory = [string]$Pending.ProcessedDirectory
    if ([string]::IsNullOrWhiteSpace($ProcessedDirectory)) {
        $ProcessedDirectory = $DefaultProcessedDirectory
    }

    if ($ArchivePackage) {
        $FinalPackagePath = Move-PackageToArchive -PackagePath $FinalPackagePath -ProcessedDirectory $ProcessedDirectory
    }

    $Completed = [pscustomobject]@{
        LastPackageName = [string]$Pending.PackageName
        LastPackageHash = [string]$Pending.PackageHash
        PackagePath     = $FinalPackagePath
        AppliedAt       = (Get-Date).ToString('o')
        ManifestPath    = $CompletedManifestPath
    }
    Write-JsonFile -Value $Completed -Path $CompletedStatePath

    Remove-Item -Force -LiteralPath $PendingPath -ErrorAction SilentlyContinue
    Remove-Item -Force -LiteralPath $ManifestPath -ErrorAction SilentlyContinue
}

try {
# Friendly command-line aliases accepted by update-and-build.bat.
# A no-argument invocation on Windows PowerShell 5.1 can expose a null entry in
# ValueFromRemainingArguments. Ignore empty entries before normalizing them.
foreach ($ArgumentValue in @($RemainingArguments)) {
    $Argument = [string]$ArgumentValue
    if ([string]::IsNullOrWhiteSpace($Argument)) {
        continue
    }
    $NormalizedArgument = ([string]$Argument).Trim().ToLowerInvariant()
    switch ($NormalizedArgument) {
        'clean'       { $Clean = $true }
        '/clean'      { $Clean = $true }
        'buildonly'   { $BuildOnly = $true }
        '/buildonly'  { $BuildOnly = $true }
        'updateonly'  { $UpdateOnly = $true }
        '/updateonly' { $UpdateOnly = $true }
        'noarchive'   { $NoArchive = $true }
        '/noarchive'  { $NoArchive = $true }
        default       { throw "Unknown argument: $Argument" }
    }
}

if ($BuildOnly -and $UpdateOnly) {
    throw '-BuildOnly and -UpdateOnly cannot be used together.'
}
if ($UpdateOnly -and $Clean) {
    Write-Warning '-Clean has no effect together with -UpdateOnly because no build will run.'
}

$ResolvedScriptPath = [string]$PSCommandPath
if ([string]::IsNullOrWhiteSpace($ResolvedScriptPath)) {
    $ResolvedScriptPath = [string]$MyInvocation.MyCommand.Path
}
if ([string]::IsNullOrWhiteSpace($ResolvedScriptPath)) {
    throw 'PowerShell could not determine the path of update-and-build.ps1.'
}
$ScriptDirectory = Split-Path -Parent $ResolvedScriptPath
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $ScriptDirectory 'update-and-build.config.json'
} else {
    $ConfigPath = Expand-ConfiguredPath -Value $ConfigPath -BasePath $ScriptDirectory
}

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "Configuration file not found: $ConfigPath"
}

$Config = Read-JsonFile -Path $ConfigPath
if ($null -eq $Config) {
    throw "Configuration could not be loaded: $ConfigPath"
}
$ProjectRoot = Expand-ConfiguredPath -Value ([string](Get-ConfigValue $Config 'ProjectRoot' '.')) -BasePath $ScriptDirectory
$UpdateInbox = Expand-ConfiguredPath -Value ([string](Get-ConfigValue $Config 'UpdateInbox' '%USERPROFILE%\Desktop\FXM Updates')) -BasePath $ProjectRoot
$ProcessedDirectory = Expand-ConfiguredPath -Value ([string](Get-ConfigValue $Config 'ProcessedDirectory' '%USERPROFILE%\Desktop\FXM Updates\Processed')) -BasePath $ProjectRoot
$ZipPattern = [string](Get-ConfigValue $Config 'ZipPattern' 'Flux_Motion_*.zip')
$BuildScriptPath = Expand-ConfiguredPath -Value ([string](Get-ConfigValue $Config 'BuildScript' 'build-windows.ps1')) -BasePath $ProjectRoot
$BuildDirectory = Expand-ConfiguredPath -Value ([string](Get-ConfigValue $Config 'BuildDirectory' 'build')) -BasePath $ProjectRoot
$Configuration = [string](Get-ConfigValue $Config 'Configuration' 'Release')
$MoveProcessedZip = [bool](Get-ConfigValue $Config 'MoveProcessedZip' $true)
$EnableBackups = [bool](Get-ConfigValue $Config 'EnableBackups' $true)
$MaximumBackups = [int](Get-ConfigValue $Config 'MaximumBackups' 5)
$BuildTests = [bool](Get-ConfigValue $Config 'BuildTests' $false)
$SkipInstall = [bool](Get-ConfigValue $Config 'SkipInstall' $false)
$CreatePackage = [bool](Get-ConfigValue $Config 'CreatePackage' $true)
$DistributionPackageName = [string](Get-ConfigValue $Config 'PackageName' 'Flux_Motion')
$DistributionPackagePlatform = [string](Get-ConfigValue $Config 'PackagePlatform' '')
$PackageOutputDirectoryValue = [string](Get-ConfigValue $Config 'PackageOutputDirectory' '')
$PackageOutputDirectory = ''
if (-not [string]::IsNullOrWhiteSpace($PackageOutputDirectoryValue)) {
    $PackageOutputDirectory = Expand-ConfiguredPath -Value $PackageOutputDirectoryValue -BasePath $ProjectRoot
}

if (-not (Test-Path -LiteralPath $ProjectRoot)) {
    throw "Project root does not exist: $ProjectRoot"
}
if (-not (Test-Path -LiteralPath (Join-Path $ProjectRoot 'CMakeLists.txt'))) {
    throw "Project root does not contain CMakeLists.txt: $ProjectRoot"
}
if (-not (Test-Path -LiteralPath $BuildScriptPath)) {
    throw "Build script not found: $BuildScriptPath"
}

$ConfigRelativePath = ''
try {
    $ConfigRelativePath = Get-RelativeProjectPath -BasePath $ProjectRoot -FullPath $ConfigPath
} catch {
    $ConfigRelativePath = ''
}
$BuildRelativePath = ''
try {
    $BuildRelativePath = Get-RelativeProjectPath -BasePath $ProjectRoot -FullPath $BuildDirectory
} catch {
    $BuildRelativePath = ''
}

$StateDirectory = Join-Path $ProjectRoot '.fxm-update'
$BackupsRoot = Join-Path $StateDirectory 'backups'
$CompletedStatePath = Join-Path $StateDirectory 'state.json'
$CompletedManifestPath = Join-Path $StateDirectory 'managed-manifest.json'
$PendingPath = Join-Path $StateDirectory 'pending.json'
$PendingManifestPath = Join-Path $StateDirectory 'pending-manifest.json'
New-Item -ItemType Directory -Force -Path $StateDirectory | Out-Null

Write-Host 'Flux Motion source updater and incremental builder' -ForegroundColor Green
Write-Host "Updater revision: $ScriptRevision" -ForegroundColor DarkGray
Write-Host "Project:       $ProjectRoot"
Write-Host "Build folder:  $BuildDirectory"
Write-Host "Configuration: $Configuration"
Write-Host "Build mode:    $(if ($Clean) { 'clean' } else { 'incremental' })"
Write-Host "OBS install:   $(if ($SkipInstall) { 'disabled' } else { 'enabled' })"
Write-Host "Package ZIP:   $(if ($CreatePackage) { 'enabled' } else { 'disabled' })"
if ($CreatePackage) {
    $PlatformDisplay = if ([string]::IsNullOrWhiteSpace($DistributionPackagePlatform)) { 'auto from architecture' } else { $DistributionPackagePlatform }
    Write-Host "Package name:  $DistributionPackageName"
    Write-Host "Platform tag:  $PlatformDisplay"
}

$CompletedState = Read-JsonFile -Path $CompletedStatePath
$PendingState = Read-JsonFile -Path $PendingPath
$NewPendingState = $null
$TemporaryExtractRoot = $null

try {
    if (-not $BuildOnly) {
        Write-Step 'Selecting update package'
        New-Item -ItemType Directory -Force -Path $UpdateInbox | Out-Null

        $SelectedZip = $null
        if (-not [string]::IsNullOrWhiteSpace($ZipPath)) {
            $SelectedZip = Expand-ConfiguredPath -Value $ZipPath -BasePath (Get-Location).Path
            if (-not (Test-Path -LiteralPath $SelectedZip)) {
                throw "ZIP package not found: $SelectedZip"
            }
        } else {
            $SelectedZipInfo = Get-ChildItem -LiteralPath $UpdateInbox -Filter $ZipPattern -File -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc, Name -Descending |
                Select-Object -First 1
            if ($null -eq $SelectedZipInfo) {
                throw "No update ZIP matching '$ZipPattern' was found in: $UpdateInbox"
            }
            $SelectedZip = $SelectedZipInfo.FullName
        }

        $PackageHash = Get-Sha256 -Path $SelectedZip
        $PackageName = [IO.Path]::GetFileName($SelectedZip)
        Write-Host "Selected: $PackageName"
        Write-Host "SHA-256:  $PackageHash"

        $AlreadyCompleted = $false
        if ($null -ne $CompletedState) {
            $PreviousHashProperty = $CompletedState.PSObject.Properties['LastPackageHash']
            if ($null -ne $PreviousHashProperty -and [string]$PreviousHashProperty.Value -eq $PackageHash) {
                $AlreadyCompleted = $true
            }
        }

        $AlreadyPending = $false
        if ($null -ne $PendingState) {
            $PendingHashProperty = $PendingState.PSObject.Properties['PackageHash']
            $PendingManifestProperty = $PendingState.PSObject.Properties['ManifestPath']
            if ($null -ne $PendingHashProperty -and
                [string]$PendingHashProperty.Value -eq $PackageHash -and
                $null -ne $PendingManifestProperty -and
                (Test-Path -LiteralPath ([string]$PendingManifestProperty.Value))) {
                $AlreadyPending = $true
            }
        }

        if ($AlreadyCompleted) {
            Write-Host 'This exact ZIP has already been applied. Source synchronization is skipped.' -ForegroundColor Yellow
        } elseif ($AlreadyPending) {
            Write-Host 'This ZIP is already synchronized and is awaiting a successful build. Source files are left untouched.' -ForegroundColor Yellow
        } else {
            Write-Step 'Extracting package'
            $TemporaryExtractRoot = Join-Path ([IO.Path]::GetTempPath()) ("fxm-update-" + [Guid]::NewGuid().ToString('N'))
            New-Item -ItemType Directory -Force -Path $TemporaryExtractRoot | Out-Null
            Expand-Archive -LiteralPath $SelectedZip -DestinationPath $TemporaryExtractRoot -Force
            $PackageRoot = Find-PackageRoot -ExtractedPath $TemporaryExtractRoot
            Write-Host "Package root: $PackageRoot"

            Write-Step 'Hashing package contents'
            $NewManifest = @(Get-PackageManifest -SourceRoot $PackageRoot -ConfigRelativePath $ConfigRelativePath -BuildRelativePath $BuildRelativePath)
            if ($NewManifest.Count -eq 0) {
                throw 'The update package does not contain any managed source files.'
            }

            $PreviousManifest = @()
            # A pending manifest reflects the current source tree more accurately
            # than the last completed package when a previous build failed.
            if ($null -ne $PendingState) {
                $PendingManifestProperty = $PendingState.PSObject.Properties['ManifestPath']
                if ($null -ne $PendingManifestProperty -and (Test-Path -LiteralPath ([string]$PendingManifestProperty.Value))) {
                    $PreviousManifest = @(Read-JsonFile -Path ([string]$PendingManifestProperty.Value))
                }
            }
            if ($PreviousManifest.Count -eq 0 -and (Test-Path -LiteralPath $CompletedManifestPath)) {
                $PreviousManifest = @(Read-JsonFile -Path $CompletedManifestPath)
            }

            $BackupName = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
            $BackupRoot = Join-Path $BackupsRoot $BackupName

            Write-Step 'Synchronizing changed files only'
            $SyncResult = Invoke-PackageSync `
                -SourceRoot $PackageRoot `
                -ProjectRoot $ProjectRoot `
                -NewManifest $NewManifest `
                -PreviousManifest $PreviousManifest `
                -BackupRoot $BackupRoot `
                -EnableBackup $EnableBackups `
                -ConfigRelativePath $ConfigRelativePath `
                -BuildRelativePath $BuildRelativePath

            Write-Host "Added:     $($SyncResult.Added)"
            Write-Host "Updated:   $($SyncResult.Updated)"
            Write-Host "Deleted:   $($SyncResult.Deleted)"
            Write-Host "Unchanged: $($SyncResult.Unchanged) (timestamps preserved)"

            if ($EnableBackups -and
                ($SyncResult.Updated -gt 0 -or $SyncResult.Deleted -gt 0)) {
                Write-Host "Rollback backup: $BackupRoot"
            } elseif (Test-Path -LiteralPath $BackupRoot) {
                Remove-Item -Recurse -Force -LiteralPath $BackupRoot
            }
            Limit-BackupHistory -BackupsRoot $BackupsRoot -MaximumBackups $MaximumBackups

            Write-JsonFile -Value $NewManifest -Path $PendingManifestPath
            $NewPendingState = [pscustomobject]@{
                PackageName       = $PackageName
                PackageHash       = $PackageHash
                PackagePath       = $SelectedZip
                ProcessedDirectory = $ProcessedDirectory
                ManifestPath      = $PendingManifestPath
                SynchronizedAt    = (Get-Date).ToString('o')
            }
            Write-JsonFile -Value $NewPendingState -Path $PendingPath
            $PendingState = $NewPendingState
        }
    }

    if ($UpdateOnly) {
        Write-Host "`nSource update complete. Build was skipped because -UpdateOnly was set." -ForegroundColor Green
        Write-Host 'Run update-and-build.bat again to build and finalize/archive the pending package.'
        return
    }

    Write-Step "Building project ($Configuration)"
    $BuildArguments = @(
        '-NoLogo',
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $BuildScriptPath,
        '-BuildDir', $BuildDirectory,
        '-Configuration', $Configuration
    )

    if ($Clean) { $BuildArguments += '-Clean' }
    if ($BuildTests) { $BuildArguments += '-BuildTests' }
    if ($SkipInstall) { $BuildArguments += '-SkipInstall' }
    if (-not $CreatePackage) { $BuildArguments += '-SkipPackage' }

    $OptionalStringSettings = @(
        @{ ConfigName = 'VcpkgDir';          Argument = '-VcpkgDir' },
        @{ ConfigName = 'VcpkgInstalledDir'; Argument = '-VcpkgInstalledDir' },
        @{ ConfigName = 'ObsSdkDir';          Argument = '-ObsSdkDir' },
        @{ ConfigName = 'InstallRoot';        Argument = '-InstallRoot' },
        @{ ConfigName = 'Generator';          Argument = '-Generator' },
        @{ ConfigName = 'Architecture';       Argument = '-Architecture' },
        @{ ConfigName = 'PackageName';        Argument = '-PackageName' },
        @{ ConfigName = 'PackagePlatform';    Argument = '-PackagePlatform' }
    )
    foreach ($Setting in $OptionalStringSettings) {
        $Value = [string](Get-ConfigValue $Config $Setting.ConfigName '')
        if (-not [string]::IsNullOrWhiteSpace($Value)) {
            $BuildArguments += $Setting.Argument
            $BuildArguments += [Environment]::ExpandEnvironmentVariables($Value)
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($PackageOutputDirectory)) {
        $BuildArguments += '-PackageOutputDir'
        $BuildArguments += $PackageOutputDirectory
    }

    $PowerShellExecutable = Join-Path ([string]$PSHOME) 'powershell.exe'
    if (-not (Test-Path -LiteralPath $PowerShellExecutable -PathType Leaf)) {
        $PowerShellExecutable = 'powershell.exe'
    }
    & $PowerShellExecutable @BuildArguments
    $BuildExitCode = $LASTEXITCODE
    if ($BuildExitCode -ne 0) {
        throw "Build failed with exit code $BuildExitCode. The ZIP remains pending and will not be archived."
    }

    $PendingState = Read-JsonFile -Path $PendingPath
    if ($null -ne $PendingState) {
        Write-Step 'Finalizing successful update'
        $ShouldArchive = $MoveProcessedZip -and (-not $NoArchive)
        Complete-PendingUpdate `
            -Pending $PendingState `
            -PendingPath $PendingPath `
            -CompletedStatePath $CompletedStatePath `
            -CompletedManifestPath $CompletedManifestPath `
            -ArchivePackage $ShouldArchive `
            -DefaultProcessedDirectory $ProcessedDirectory

        if ($ShouldArchive) {
            Write-Host "Package moved to: $ProcessedDirectory"
        } else {
            Write-Host 'Package archive move skipped.'
        }
    }

    Write-Host "`nUpdate and build completed successfully." -ForegroundColor Green
    if ($Clean) {
        Write-Host 'A clean configure/rebuild was performed.'
    } else {
        Write-Host 'The existing build directory was preserved for incremental compilation.'
    }
} finally {
    if (-not [string]::IsNullOrWhiteSpace([string]$TemporaryExtractRoot) -and
        (Test-Path -LiteralPath ([string]$TemporaryExtractRoot))) {
        Remove-Item -Recurse -Force -LiteralPath ([string]$TemporaryExtractRoot) -ErrorAction SilentlyContinue
    }
}
} catch {
    Write-FatalError -ErrorRecord $_
    exit 1
}
