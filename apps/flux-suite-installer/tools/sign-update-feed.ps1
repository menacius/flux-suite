[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$ManifestPath,
    [string]$KeyPath = (Join-Path $env:LOCALAPPDATA 'FluxSuiteBuild\Signing\flux-update-key.dpapi'),
    [string]$SignaturePath = "$ManifestPath.sig"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ManifestPath = [IO.Path]::GetFullPath($ManifestPath)
if (-not (Test-Path -LiteralPath $ManifestPath)) { throw "Manifest not found: $ManifestPath" }
if (-not (Test-Path -LiteralPath $KeyPath)) { throw "Protected signing key not found: $KeyPath" }

$ProtectedKey = [IO.File]::ReadAllBytes($KeyPath)
$PrivateKey = [Security.Cryptography.ProtectedData]::Unprotect(
    $ProtectedKey,
    [Text.Encoding]::UTF8.GetBytes('Flux Suite update feed signing key v1'),
    [Security.Cryptography.DataProtectionScope]::CurrentUser)
$Ecdsa = [Security.Cryptography.ECDsa]::Create()
try {
    [void]$Ecdsa.ImportPkcs8PrivateKey($PrivateKey, [ref]0)
    $Payload = [IO.File]::ReadAllBytes($ManifestPath)
    $Signature = $Ecdsa.SignData(
        $Payload,
        [Security.Cryptography.HashAlgorithmName]::SHA256,
        [Security.Cryptography.DSASignatureFormat]::IeeeP1363FixedFieldConcatenation)
    if ($Signature.Length -ne 64) { throw "Unexpected ECDSA signature length: $($Signature.Length)" }
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($SignaturePath), [Convert]::ToBase64String($Signature) + "`n")
} finally {
    if ($null -ne $PrivateKey) { [Array]::Clear($PrivateKey, 0, $PrivateKey.Length) }
    $Ecdsa.Dispose()
}

Write-Host "Signed update feed: $SignaturePath" -ForegroundColor Green
