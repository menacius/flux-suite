[CmdletBinding()]
param(
    [string]$HostName = '51.83.140.130',
    [int]$Port = 22,
    [string]$UserName = 'omniatv',
    [string]$KeyPath = $env:FLUX_DEPLOY_KEY_PATH,
    [string]$ExpectedHostFingerprint = 'SHA256:UbyJOF/2StMDGGBQOSLM2tkVQWZG56TxcJ1qBdDosaA',
    [string]$RemoteRoot = '/home/omniatv/domains/software.omniatv.com/public_html/flux-suite/windows-x64',
    [string]$PublicBaseUrl = 'https://software.omniatv.com/flux-suite/windows-x64',
    [string]$LocalRoot = (Join-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) 'out\deploy\software.omniatv.com\flux-suite\windows-x64'),
    [switch]$SkipPublish,
    [switch]$PruneRemoteArchives,
    [switch]$SkipPublicVerification
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command was not found: $Name"
    }
}

function Quote-SftpPath([string]$Path) {
    return '"' + ($Path -replace '\\', '/' -replace '"', '\"') + '"'
}

function Invoke-NativeChecked([string]$FilePath, [string[]]$ArgumentList) {
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

Assert-Command 'ssh.exe'
Assert-Command 'ssh-keygen.exe'
Assert-Command 'sftp.exe'

if ([string]::IsNullOrWhiteSpace($KeyPath)) {
    throw 'Set FLUX_DEPLOY_KEY_PATH or pass -KeyPath with the deployment private key.'
}
$KeyPath = [IO.Path]::GetFullPath($KeyPath)
$LocalRoot = [IO.Path]::GetFullPath($LocalRoot)
$RemoteRoot = $RemoteRoot.TrimEnd('/')
$PublicBaseUrl = $PublicBaseUrl.TrimEnd('/')

if (-not (Test-Path -LiteralPath $KeyPath -PathType Leaf)) {
    throw "Deployment private key is missing: $KeyPath"
}
if (-not (Test-Path -LiteralPath $LocalRoot -PathType Container)) {
    throw "Deployment tree is missing: $LocalRoot"
}

if (-not $SkipPublish) {
    $Publisher = Join-Path $PSScriptRoot 'publish-update-feed.ps1'
    Write-Host 'Preparing and signing the update feed...' -ForegroundColor Cyan
    & $Publisher -OutputRoot $LocalRoot
}

$ManifestPath = Join-Path $LocalRoot 'manifest.json'
$SignaturePath = Join-Path $LocalRoot 'manifest.json.sig'
if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $SignaturePath -PathType Leaf)) {
    throw 'The signed manifest files are missing from the deployment tree.'
}

$TempRoot = Join-Path ([IO.Path]::GetTempPath()) ('flux-deploy-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $TempRoot | Out-Null
$KnownHostsPath = Join-Path $TempRoot 'known_hosts'
$ProbeOutputPath = Join-Path $TempRoot 'host-probe.txt'
$BatchPath = Join-Path $TempRoot 'deploy.sftp'
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

try {
    # Windows OpenSSH 9.5 does not advertise the server's preferred hybrid KEX,
    # so pin a mutually supported modern curve for all deployment connections.
    $CommonSshOptions = @(
        '-o', 'KexAlgorithms=curve25519-sha256',
        '-o', 'HostKeyAlgorithms=ssh-ed25519',
        '-o', 'BatchMode=yes',
        '-o', 'ConnectTimeout=15'
    )

    # Acquire the key in an isolated known_hosts file, then compare its SHA-256
    # fingerprint before any authenticated upload is attempted.
    $ProbeArguments = @(
        '-p', $Port,
        '-i', $KeyPath,
        '-o', 'StrictHostKeyChecking=accept-new',
        '-o', "UserKnownHostsFile=$KnownHostsPath"
    ) + $CommonSshOptions + @("$UserName@$HostName", 'exit')
    & ssh.exe @ProbeArguments *> $ProbeOutputPath

    if (-not (Test-Path -LiteralPath $KnownHostsPath -PathType Leaf)) {
        throw 'The server did not provide an SSH host key.'
    }
    $FingerprintOutput = & ssh-keygen.exe -lf $KnownHostsPath -E sha256
    $FingerprintMatch = [regex]::Match(
        ($FingerprintOutput -join "`n"), 'SHA256:[A-Za-z0-9+/=]+'
    )
    if (-not $FingerprintMatch.Success -or
        $FingerprintMatch.Value -cne $ExpectedHostFingerprint) {
        throw "SSH host fingerprint mismatch. Expected $ExpectedHostFingerprint; received $($FingerprintMatch.Value)."
    }
    Write-Host "Verified SSH host fingerprint: $ExpectedHostFingerprint" -ForegroundColor Green

    $Files = Get-ChildItem -LiteralPath $LocalRoot -Recurse -File |
        ForEach-Object {
            $relative = $_.FullName.Substring($LocalRoot.Length).TrimStart([char[]]@('\', '/')) -replace '\\', '/'
            [pscustomobject]@{ File = $_; Relative = $relative }
        }
    $PayloadFiles = $Files | Where-Object {
        $_.Relative -notin @('manifest.json', 'manifest.json.sig')
    } | Sort-Object Relative

    $Directories = New-Object System.Collections.Generic.HashSet[string]
    [void]$Directories.Add($RemoteRoot)
    foreach ($entry in $Files) {
        $parent = [IO.Path]::GetDirectoryName($entry.Relative)
        if ([string]::IsNullOrWhiteSpace($parent)) { continue }
        $parts = ($parent -replace '\\', '/').Split('/')
        $current = $RemoteRoot
        foreach ($part in $parts) {
            if ([string]::IsNullOrWhiteSpace($part)) { continue }
            $current += '/' + $part
            [void]$Directories.Add($current)
        }
    }

    $Batch = New-Object System.Collections.Generic.List[string]
    if ($PruneRemoteArchives) {
        $ArchiveIds = Get-ChildItem -LiteralPath (Join-Path $LocalRoot 'archive') -Directory |
            Select-Object -ExpandProperty Name
        foreach ($archiveId in $ArchiveIds) {
            # OpenSSH sftp only expands remote globs when the wildcard is not
            # quoted. RemoteRoot is administrator-configured and contains no
            # whitespace, so this remains a single, scoped batch argument.
            $Batch.Add('-rm ' + $RemoteRoot + '/archive/' + $archiveId + '/*')
        }
    }
    foreach ($directory in ($Directories | Sort-Object { ($_ -split '/').Count }, { $_ })) {
        # Existing directories are expected on later deployments.
        $Batch.Add('-mkdir ' + (Quote-SftpPath $directory))
    }
    foreach ($entry in $PayloadFiles) {
        $remote = $RemoteRoot + '/' + $entry.Relative
        $temporary = $remote + '.uploading'
        $Batch.Add('put ' + (Quote-SftpPath $entry.File.FullName) + ' ' + (Quote-SftpPath $temporary))
        $Batch.Add('rename ' + (Quote-SftpPath $temporary) + ' ' + (Quote-SftpPath $remote))
    }

    # Publish the detached signature first and the signed manifest last. Clients
    # therefore never see a catalog that references packages still in transit.
    foreach ($name in @('manifest.json.sig', 'manifest.json')) {
        $local = Join-Path $LocalRoot $name
        $remote = $RemoteRoot + '/' + $name
        $temporary = $remote + '.uploading'
        $Batch.Add('put ' + (Quote-SftpPath $local) + ' ' + (Quote-SftpPath $temporary))
        $Batch.Add('rename ' + (Quote-SftpPath $temporary) + ' ' + (Quote-SftpPath $remote))
    }
    $Batch.Add('bye')
    [IO.File]::WriteAllLines($BatchPath, $Batch, $Utf8NoBom)

    Write-Host "Uploading signed deployment tree to $UserName@$HostName..." -ForegroundColor Cyan
    $SftpArguments = @(
        '-b', $BatchPath,
        '-P', $Port,
        '-i', $KeyPath,
        '-o', 'KexAlgorithms=curve25519-sha256',
        '-o', 'HostKeyAlgorithms=ssh-ed25519',
        '-o', 'BatchMode=yes',
        '-o', 'ConnectTimeout=15',
        '-o', 'StrictHostKeyChecking=yes',
        '-o', "UserKnownHostsFile=$KnownHostsPath",
        "$UserName@$HostName"
    )
    Invoke-NativeChecked 'sftp.exe' $SftpArguments

    if (-not $SkipPublicVerification) {
        Write-Host 'Verifying the published manifest over HTTPS...' -ForegroundColor Cyan
        $Nonce = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
        foreach ($name in @('manifest.json.sig', 'manifest.json')) {
            $Downloaded = Join-Path $TempRoot ('published-' + $name)
            Invoke-WebRequest -UseBasicParsing -Headers @{ 'Cache-Control' = 'no-cache' } `
                -Uri "$PublicBaseUrl/${name}?deployment=$Nonce" -OutFile $Downloaded
            $LocalHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $LocalRoot $name)).Hash
            $RemoteHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Downloaded).Hash
            if ($LocalHash -cne $RemoteHash) {
                throw "HTTPS verification failed for $name. A CDN or proxy may still be serving an older file."
            }
        }
    }

    Write-Host 'Flux Suite deployment completed successfully.' -ForegroundColor Green
} finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
