[CmdletBinding()]
param()

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$RequiredVersionFiles = @(
    'apps\flux-motion\VERSION.txt',
    'apps\flux-encoder\VERSION.txt',
    'apps\flux-suite-installer\VERSION.txt',
    'plugins\obs\flux-motion\VERSION.txt',
    'packages\flux-common\VERSION.txt'
)

$Errors = [System.Collections.Generic.List[string]]::new()
foreach ($RelativePath in $RequiredVersionFiles) {
    $Path = Join-Path $RepositoryRoot $RelativePath
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $Errors.Add("Missing version file: $RelativePath")
        continue
    }
    $Label = (Get-Content -Raw -LiteralPath $Path).Trim()
    if ($Label -notmatch '^\d{4} - v\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$') {
        $Errors.Add("Invalid version label in ${RelativePath}: $Label")
    }
}

$ReparsePoints = Get-ChildItem -LiteralPath $RepositoryRoot -Recurse -Force `
    -Attributes ReparsePoint -ErrorAction SilentlyContinue
foreach ($Item in $ReparsePoints) {
    $Errors.Add("Repository link is not allowed: $($Item.FullName)")
}

if ($Errors.Count -gt 0) {
    $Errors | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "Flux Suite layout validation passed."
