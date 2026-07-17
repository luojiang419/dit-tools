$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. "$PSScriptRoot\version_helpers.ps1"

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]
        $Actual,

        [Parameter(Mandatory = $true)]
        $Expected,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if ($Actual -ne $Expected) {
        throw "$Message Expected '$Expected', got '$Actual'."
    }
}

Assert-Equal `
    (Get-NormalizedCineVaultVersionTag -Version '0.1.168') `
    'v0.1.168' `
    'Version normalization failed.'
Assert-Equal `
    (Get-NextCineVaultReleaseVersion -LatestVersion 'v0.1.156') `
    'v0.1.169' `
    'The first automated release floor was not applied.'
Assert-Equal `
    (Get-NextCineVaultReleaseVersion -LatestVersion 'v0.1.168') `
    'v0.1.169' `
    'Patch version did not increment.'
Assert-Equal `
    (Get-NextCineVaultReleaseVersion -LatestVersion 'v0.2.9') `
    'v0.2.10' `
    'Multi-digit patch version did not increment.'
Assert-Equal `
    (Compare-CineVaultVersions -Left 'v1.0.0' -Right 'v0.99.99') `
    1 `
    'Semantic version comparison failed.'

$invalidVersionWasRejected = $false
try {
    Get-NormalizedCineVaultVersionTag -Version 'latest' | Out-Null
} catch {
    $invalidVersionWasRejected = $true
}
Assert-Equal $invalidVersionWasRejected $true 'Invalid version was not rejected.'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$rawSourceVersion = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
Assert-Equal `
    (Get-CineVaultSourceVersion -RepositoryRoot $repositoryRoot) `
    $rawSourceVersion `
    'The repository VERSION file was not read as the canonical application version.'

$cmakeContents = Get-Content -LiteralPath (Join-Path $repositoryRoot 'dit-tools-src\cinevault-pro\CMakeLists.txt') -Raw
$installerContents = Get-Content -LiteralPath (Join-Path $repositoryRoot 'installer\windows\cinevault.iss') -Raw
$buildScriptContents = Get-Content -LiteralPath (Join-Path $repositoryRoot 'tool\build_windows.ps1') -Raw
if ($cmakeContents -match 'project\(CineVault VERSION 0\.1\.' -or
    $installerContents -match '#define\s+(AppVersion|VersionTag)\s+"v?\d' -or
    $buildScriptContents -notmatch 'Get-CineVaultSourceVersion') {
    throw 'A stale independent application version source still exists.'
}

Write-Host 'Release version tests passed.'
