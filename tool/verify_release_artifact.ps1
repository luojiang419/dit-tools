[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$VersionTag,

    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [string]$GitHubOutput = $env:GITHUB_OUTPUT,

    [string]$ExpectedSignerSha256 = $env:CINEVAULT_UPDATE_SIGNER_SHA256,

    [switch]$RequireSignature
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. "$PSScriptRoot\version_helpers.ps1"

$normalizedVersionTag = Get-NormalizedCineVaultVersionTag -Version $VersionTag
$appVersion = $normalizedVersionTag.TrimStart('v')
$expectedName = "CineVault-Setup-$normalizedVersionTag.exe"

$installer = Get-Item -LiteralPath $InstallerPath -ErrorAction Stop
if ($installer.Name -cne $expectedName) {
    throw "Installer name mismatch. Expected '$expectedName', got '$($installer.Name)'."
}
if ($installer.Length -lt 10MB) {
    throw "Installer is unexpectedly small: $($installer.Length) bytes."
}

$productVersion = $installer.VersionInfo.ProductVersion.Trim()
if ($productVersion -ne $appVersion) {
    throw "Installer product version mismatch. Expected '$appVersion', got '$productVersion'."
}

if ($RequireSignature) {
    $normalizedExpectedSigner = ([string]$ExpectedSignerSha256).Replace(' ', '').Trim().ToLowerInvariant()
    if ($normalizedExpectedSigner -notmatch '^[0-9a-f]{64}$') {
        throw 'A valid expected signer SHA-256 is required for release verification.'
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $installer.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $null -eq $signature.SignerCertificate) {
        throw "Installer Authenticode signature is invalid: $($signature.StatusMessage)"
    }
    $actualSigner = $signature.SignerCertificate.GetCertHashString('SHA256').ToLowerInvariant()
    if ($actualSigner -cne $normalizedExpectedSigner) {
        throw 'Installer Authenticode signer does not match the pinned release signer.'
    }
}

$hash = (Get-FileHash -LiteralPath $installer.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumPath = "$($installer.FullName).sha256"
Set-Content `
    -LiteralPath $checksumPath `
    -Value "$hash *$($installer.Name)" `
    -Encoding ASCII

if (-not [string]::IsNullOrWhiteSpace($GitHubOutput)) {
    Add-Content -LiteralPath $GitHubOutput -Value "installer_path=$($installer.FullName)" -Encoding UTF8
    Add-Content -LiteralPath $GitHubOutput -Value "checksum_path=$checksumPath" -Encoding UTF8
    Add-Content -LiteralPath $GitHubOutput -Value "installer_size=$($installer.Length)" -Encoding UTF8
    Add-Content -LiteralPath $GitHubOutput -Value "installer_sha256=$hash" -Encoding UTF8
}

Write-Host "Verified installer: $($installer.FullName)"
Write-Host "Product version:    $productVersion"
Write-Host "Size:               $($installer.Length) bytes"
Write-Host "SHA-256:            $hash"
