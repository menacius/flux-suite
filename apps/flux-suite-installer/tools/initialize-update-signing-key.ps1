[CmdletBinding()]
param(
    [string]$KeyPath = (Join-Path $env:LOCALAPPDATA 'FluxSuiteBuild\Signing\flux-update-key.dpapi'),
    [string]$PublicKeyPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'resources\update-public-key.json')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
    throw 'LOCALAPPDATA is required to store the protected update signing key.'
}
if (Test-Path -LiteralPath $KeyPath) {
    throw "A signing key already exists at $KeyPath. Refusing to overwrite it."
}

$KeyDirectory = Split-Path $KeyPath -Parent
New-Item -ItemType Directory -Force -Path $KeyDirectory | Out-Null

$Ecdsa = [Security.Cryptography.ECDsa]::Create([Security.Cryptography.ECCurve+NamedCurves]::nistP256)
try {
    $PrivateKey = $Ecdsa.ExportPkcs8PrivateKey()
    $ProtectedKey = [Security.Cryptography.ProtectedData]::Protect(
        $PrivateKey,
        [Text.Encoding]::UTF8.GetBytes('Flux Suite update feed signing key v1'),
        [Security.Cryptography.DataProtectionScope]::CurrentUser)
    [IO.File]::WriteAllBytes($KeyPath, $ProtectedKey)

    $Parameters = $Ecdsa.ExportParameters($false)
    $PublicKey = [ordered]@{
        algorithm = 'ECDSA-P256-SHA256'
        keyId = 'flux-suite-update-2026-01'
        x = [Convert]::ToBase64String($Parameters.Q.X)
        y = [Convert]::ToBase64String($Parameters.Q.Y)
    }
    $PublicKey | ConvertTo-Json | Set-Content -LiteralPath $PublicKeyPath -Encoding utf8
} finally {
    if ($null -ne $PrivateKey) { [Array]::Clear($PrivateKey, 0, $PrivateKey.Length) }
    $Ecdsa.Dispose()
}

Write-Host "Protected private key: $KeyPath" -ForegroundColor Green
Write-Host "Public trust anchor: $PublicKeyPath" -ForegroundColor Green
Write-Warning 'Back up the protected key securely. Production signing should ultimately use a certificate vault or HSM.'
