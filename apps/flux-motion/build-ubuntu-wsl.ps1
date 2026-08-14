# build-ubuntu-wsl.ps1
# Builds Flux Motion inside the OBS Linux baseline (Ubuntu 24.04) on WSL2 and copies the
# resulting Linux distribution archives back into the Windows project build folder.

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$ConfigPath,
    [string]$DistroName,
    [string]$BuildType,
    [string]$Workspace,
    [string]$PackageOutputDirectory,
    [string]$PackageName,
    [string]$PackagePlatform,
    [ValidateSet("zip", "tar.gz", "both")]
    [string]$ArchiveFormat,
    [switch]$BuildTests,
    [switch]$SkipDependencies,
    [switch]$Clean,
    [switch]$InstallIntoWslObs
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ScriptRevision = "2026-06-28.9-obs-runtime-linking"
Write-Host "WSL builder revision: $ScriptRevision" -ForegroundColor DarkGray

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ConfigPath)) {
    $ConfigPath = Join-Path $ScriptDir "build-ubuntu-wsl.config.json"
} elseif (-not [IO.Path]::IsPathRooted($ConfigPath)) {
    $ConfigPath = Join-Path $ScriptDir $ConfigPath
}
$ConfigPath = [IO.Path]::GetFullPath($ConfigPath)

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

    $Property = $Config.PSObject.Properties[$Name]
    if ($null -eq $Property -or $null -eq $Property.Value) {
        return $DefaultValue
    }
    return $Property.Value
}

function Expand-WindowsPath {
    param(
        [string]$Value,
        [string]$BasePath
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }
    $Expanded = [Environment]::ExpandEnvironmentVariables($Value)
    if ([IO.Path]::IsPathRooted($Expanded)) {
        return [IO.Path]::GetFullPath($Expanded)
    }
    return [IO.Path]::GetFullPath((Join-Path $BasePath $Expanded))
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$ArgumentList,
        [switch]$Capture
    )

    if ($Capture) {
        $Output = @(& $FilePath @ArgumentList 2>&1)
        $ExitCode = $LASTEXITCODE
        if ($ExitCode -ne 0) {
            throw "$FilePath failed with exit code $ExitCode.`n$($Output -join [Environment]::NewLine)"
        }
        return @($Output | ForEach-Object { [string]$_ })
    }

    & $FilePath @ArgumentList
    $ExitCode = $LASTEXITCODE
    if ($ExitCode -ne 0) {
        throw "$FilePath failed with exit code $ExitCode."
    }
}

function Invoke-ElevatedWsl {
    param([string[]]$Arguments)

    $WslPath = Join-Path $env:SystemRoot "System32\wsl.exe"
    $Process = Start-Process -FilePath $WslPath -ArgumentList $Arguments -Verb RunAs -Wait -PassThru
    if ($Process.ExitCode -notin @(0, 3010)) {
        throw "Elevated wsl.exe command failed with exit code $($Process.ExitCode)."
    }
    return $Process.ExitCode
}

function Get-InstalledDistroNames {
    try {
        $Lines = Invoke-Native -FilePath "wsl.exe" -ArgumentList @("--list", "--quiet") -Capture
        return @($Lines | ForEach-Object { $_.Trim([char]0).Trim() } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    } catch {
        return @()
    }
}


function Remove-NulCharacters {
    param([AllowNull()][object]$Value)

    if ($null -eq $Value) {
        return ""
    }
    return ([string]$Value).Replace([string][char]0, "")
}


function Convert-ToNormalizedOutputLines {
    param([AllowNull()][object]$InputObject)

    $Result = New-Object System.Collections.Generic.List[string]
    foreach ($Item in @($InputObject)) {
        if ($null -eq $Item) {
            continue
        }
        $Text = (Remove-NulCharacters -Value $Item).Trim()
        if (-not [string]::IsNullOrWhiteSpace($Text)) {
            [void]$Result.Add($Text)
        }
    }
    return $Result.ToArray()
}

function Get-LastOutputLine {
    param(
        [AllowNull()][object]$InputObject,
        [Parameter(Mandatory = $true)][string]$Description
    )

    $Lines = @(Convert-ToNormalizedOutputLines -InputObject $InputObject)
    if ($Lines.Count -eq 0) {
        throw "$Description returned no output."
    }
    return [string]$Lines[$Lines.Count - 1]
}

function Get-DistroWslVersionFromList {
    param([Parameter(Mandatory = $true)][string]$Distribution)

    try {
        $Lines = Invoke-Native -FilePath "wsl.exe" -ArgumentList @("--list", "--verbose") -Capture
    } catch {
        return $null
    }

    $EscapedName = [regex]::Escape($Distribution)
    foreach ($RawLine in $Lines) {
        $Line = (Remove-NulCharacters -Value $RawLine).Trim()
        if ([string]::IsNullOrWhiteSpace($Line)) {
            continue
        }

        # The state text and headers may be localized. Match only the exact distro
        # name and the final numeric VERSION column.
        $Line = $Line -replace '^\s*\*?\s*', ''
        if ($Line -match ("^" + $EscapedName + "(?:\s+.*?)?\s+([12])\s*$")) {
            return [int]$Matches[1]
        }

        $Columns = @($Line -split '\s{2,}' | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
        if ($Columns.Count -ge 2 -and $Columns[0] -eq $Distribution) {
            $LastColumn = $Columns[$Columns.Count - 1]
            if ($LastColumn -in @('1', '2')) {
                return [int]$LastColumn
            }
        }
    }
    return $null
}

function Get-DistroWslVersionFromRuntime {
    param([Parameter(Mandatory = $true)][string]$Distribution)

    try {
        $Output = Invoke-Native -FilePath "wsl.exe" -ArgumentList @(
            "--distribution", $Distribution,
            "--user", "root",
            "--", "sh", "-lc",
            'release=$(uname -r 2>/dev/null || true); case "$release" in *WSL2*|*wsl2*|*microsoft-standard*) printf 2 ;; *) printf 1 ;; esac'
        ) -Capture

        $Text = (($Output | ForEach-Object { Remove-NulCharacters -Value $_ }) -join "").Trim()
        if ($Text -match '^[12]$') {
            return [int]$Text
        }
    } catch {
        Write-Verbose "Runtime WSL version probe failed: $($_.Exception.Message)"
    }
    return $null
}

function Get-DistroWslVersion {
    param([Parameter(Mandatory = $true)][string]$Distribution)

    $Version = Get-DistroWslVersionFromList -Distribution $Distribution
    if ($null -ne $Version) {
        return $Version
    }

    # Locale/encoding-independent fallback. This starts the distro briefly; callers
    # terminate it before a real WSL1 -> WSL2 conversion.
    return Get-DistroWslVersionFromRuntime -Distribution $Distribution
}

function Convert-ToWslPath {
    param(
        [string]$WindowsPath,
        [string]$Distribution
    )

    $Output = @(Invoke-Native -FilePath "wsl.exe" -ArgumentList @(
        "--distribution", $Distribution,
        "--user", "root",
        "--", "wslpath", "-a", "-u", $WindowsPath
    ) -Capture)
    return Get-LastOutputLine -InputObject $Output -Description "wslpath for '$WindowsPath'"
}

if (-not (Test-Path -LiteralPath $ConfigPath)) {
    throw "WSL build configuration was not found: $ConfigPath"
}
$Config = Get-Content -Raw -LiteralPath $ConfigPath | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($DistroName)) {
    $DistroName = [string](Get-ConfigValue -Config $Config -Name "DistroName" -DefaultValue "Ubuntu-24.04")
}
$ExpectedVersion = [string](Get-ConfigValue -Config $Config -Name "ExpectedUbuntuVersion" -DefaultValue "24.04")
$MaxGlibcVersion = [string](Get-ConfigValue -Config $Config -Name "MaximumGlibcVersion" -DefaultValue "2.39")
if ([string]::IsNullOrWhiteSpace($BuildType)) {
    $BuildType = [string](Get-ConfigValue -Config $Config -Name "BuildType" -DefaultValue "Release")
}
if ([string]::IsNullOrWhiteSpace($Workspace)) {
    $Workspace = [string](Get-ConfigValue -Config $Config -Name "Workspace" -DefaultValue "/root/.cache/flux-motion/obs-linux-ubuntu-24.04")
}
if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $PackageName = [string](Get-ConfigValue -Config $Config -Name "PackageName" -DefaultValue "Flux_Motion")
}
if ([string]::IsNullOrWhiteSpace($PackagePlatform)) {
    $PackagePlatform = [string](Get-ConfigValue -Config $Config -Name "PackagePlatform" -DefaultValue "")
}
if ([string]::IsNullOrWhiteSpace($ArchiveFormat)) {
    $ArchiveFormat = [string](Get-ConfigValue -Config $Config -Name "ArchiveFormat" -DefaultValue "both")
}
if ($ArchiveFormat -notin @("zip", "tar.gz", "both")) {
    throw "ArchiveFormat must be zip, tar.gz, or both."
}
if (-not $PSBoundParameters.ContainsKey("BuildTests")) {
    $BuildTests = [bool](Get-ConfigValue -Config $Config -Name "BuildTests" -DefaultValue $false)
}
$InstallDependencies = [bool](Get-ConfigValue -Config $Config -Name "InstallDependencies" -DefaultValue $true)
if ($SkipDependencies) {
    $InstallDependencies = $false
}
if (-not $PSBoundParameters.ContainsKey("InstallIntoWslObs")) {
    $InstallIntoWslObs = [bool](Get-ConfigValue -Config $Config -Name "InstallIntoWslObs" -DefaultValue $false)
}
$WslObsPluginDirectory = [string](Get-ConfigValue -Config $Config -Name "WslObsPluginDirectory" -DefaultValue "/root/.config/obs-studio/plugins")
if ([string]::IsNullOrWhiteSpace($PackageOutputDirectory)) {
    $PackageOutputDirectory = [string](Get-ConfigValue -Config $Config -Name "PackageOutputDirectory" -DefaultValue "build")
}
$PackageOutputDirectory = Expand-WindowsPath -Value $PackageOutputDirectory -BasePath $ScriptDir
New-Item -ItemType Directory -Force -Path $PackageOutputDirectory | Out-Null

$HelperPath = Join-Path $ScriptDir "scripts\wsl\build-obs-linux.sh"
if (-not (Test-Path -LiteralPath $HelperPath)) {
    throw "WSL build helper was not found: $HelperPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $ScriptDir "CMakeLists.txt"))) {
    throw "Run this script from the project package containing CMakeLists.txt."
}

$WslCommand = Get-Command "wsl.exe" -ErrorAction SilentlyContinue
if ($null -eq $WslCommand) {
    throw "wsl.exe is unavailable. Windows 10 2004+ or Windows 11 is required."
}

Write-Step "Checking WSL2 and $DistroName"
$InstalledDistros = Get-InstalledDistroNames
if ($InstalledDistros -notcontains $DistroName) {
    Write-Host "$DistroName is not installed. Requesting elevated automatic installation..." -ForegroundColor Yellow
    $InstallExit = Invoke-ElevatedWsl -Arguments @(
        "--install",
        "--distribution", $DistroName,
        "--no-launch",
        "--web-download"
    )

    $InstalledDistros = Get-InstalledDistroNames
    if ($InstallExit -eq 3010 -or $InstalledDistros -notcontains $DistroName) {
        throw "$DistroName was requested, but Windows must be restarted before WSL can use it. Restart Windows and run this script again."
    }
}

$CurrentWslVersion = Get-DistroWslVersion -Distribution $DistroName
if ($null -eq $CurrentWslVersion) {
    throw "Could not determine the WSL version of '$DistroName' from either the verbose distro list or the runtime kernel probe. Run 'wsl.exe --list --verbose' manually and verify that the distro can start."
}

if ($CurrentWslVersion -eq 2) {
    Write-Host "$DistroName is already configured for WSL2." -ForegroundColor Green
} else {
    Write-Host "Converting $DistroName from WSL1 to WSL2..." -ForegroundColor Yellow

    # Conversion must happen while the distribution is stopped. Starting it first
    # can cause WSL_E_VM_MODE_INVALID_STATE during --set-version.
    try {
        Invoke-Native -FilePath "wsl.exe" -ArgumentList @("--terminate", $DistroName)
    } catch {
        Write-Verbose "The distribution was already stopped or could not be terminated: $($_.Exception.Message)"
    }

    try {
        Invoke-Native -FilePath "wsl.exe" -ArgumentList @("--set-version", $DistroName, "2")
    } catch {
        # Some WSL builds print a warning or return an unusual exit code even when
        # the requested version is already active. Re-read the authoritative list
        # before treating the command as a real failure.
        $VersionAfterFailure = Get-DistroWslVersion -Distribution $DistroName
        if ($VersionAfterFailure -ne 2) {
            throw
        }
        Write-Host "$DistroName now reports WSL2; continuing despite the set-version warning." -ForegroundColor Yellow
    }

    $CurrentWslVersion = Get-DistroWslVersion -Distribution $DistroName
    if ($CurrentWslVersion -ne 2) {
        throw "WSL conversion did not complete. '$DistroName' still reports version $CurrentWslVersion."
    }
}

# Initialize explicitly as root only after WSL2 status has been verified. This
# avoids the interactive first-launch user OOBE and keeps dependency setup fully
# automated without putting the distro into a running state before conversion.
Invoke-Native -FilePath "wsl.exe" -ArgumentList @(
    "--distribution", $DistroName,
    "--user", "root",
    "--", "sh", "-lc", "true"
)

# Read the complete os-release file instead of passing printf/backslash quoting
# through Windows PowerShell 5.1 and wsl.exe. Native argument quoting can turn
# "%s\n" into malformed output (for example a trailing standalone "n").
$OsReleaseOutput = Invoke-Native -FilePath "wsl.exe" -ArgumentList @(
    "--distribution", $DistroName,
    "--user", "root",
    "--", "cat", "/etc/os-release"
) -Capture
$OsReleaseText = ((Convert-ToNormalizedOutputLines -InputObject $OsReleaseOutput) -join "`n")
if ([string]::IsNullOrWhiteSpace($OsReleaseText)) {
    throw "Ubuntu version probe for '$DistroName' returned an empty /etc/os-release file."
}

$VersionMatch = [regex]::Match(
    $OsReleaseText,
    '(?m)^\s*VERSION_ID\s*=\s*["'']?([^"''\r\n]+)["'']?\s*$'
)
if (-not $VersionMatch.Success) {
    throw "Could not parse VERSION_ID from /etc/os-release in '$DistroName'. Raw output: $OsReleaseText"
}
$DetectedVersion = $VersionMatch.Groups[1].Value.Trim()
if ($DetectedVersion -ne $ExpectedVersion) {
    throw "$DistroName reports Ubuntu $DetectedVersion, but Ubuntu $ExpectedVersion is required."
}

$ProjectWslPath = Convert-ToWslPath -WindowsPath $ScriptDir -Distribution $DistroName
$OutputWslPath = Convert-ToWslPath -WindowsPath $PackageOutputDirectory -Distribution $DistroName
$HelperWslPath = "$ProjectWslPath/scripts/wsl/build-obs-linux.sh"

$BashArguments = @(
    "--distribution", $DistroName,
    "--user", "root",
    "--", "bash", $HelperWslPath,
    "--source", $ProjectWslPath,
    "--workspace", $Workspace,
    "--output-dir", $OutputWslPath,
    "--build-type", $BuildType,
    "--package-name", $PackageName,
    "--archive-format", $ArchiveFormat,
    "--expected-version", $ExpectedVersion,
    "--max-glibc", $MaxGlibcVersion
)
if (-not [string]::IsNullOrWhiteSpace($PackagePlatform)) {
    $BashArguments += @("--platform", $PackagePlatform)
}
if ($BuildTests) {
    $BashArguments += "--build-tests"
}
if (-not $InstallDependencies) {
    $BashArguments += "--skip-deps"
}
if ($Clean) {
    $BashArguments += "--clean"
}
if ($InstallIntoWslObs) {
    $BashArguments += @("--install-dir", $WslObsPluginDirectory)
}

Write-Step "Building Flux Motion inside Ubuntu $ExpectedVersion on WSL2"
Invoke-Native -FilePath "wsl.exe" -ArgumentList $BashArguments

Write-Step "WSL2 build completed"
Write-Host "Linux build workspace: $Workspace"
Write-Host "Windows package output: $PackageOutputDirectory"
Write-Host "Archive format: $ArchiveFormat"
