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

function Invoke-ResolveReleaseVersionForTest {
    param(
        [Parameter(Mandatory = $true)]
        [string]$SourceVersion,

        [Parameter(Mandatory = $true)]
        [string]$LatestVersion
    )

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
        'cinevault-version-test-' + [System.Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempRoot | Out-Null
    try {
        Set-Content -LiteralPath (Join-Path $tempRoot 'VERSION') -Value $SourceVersion -Encoding ASCII -NoNewline
        $outputPath = Join-Path $tempRoot 'github-output.txt'
        & (Join-Path $PSScriptRoot 'resolve_release_version.ps1') `
            -LatestVersion $LatestVersion `
            -RepositoryRoot $tempRoot `
            -GitHubOutput $outputPath `
            -SkipExistingCommitCheck | Out-Null

        $result = @{}
        foreach ($line in Get-Content -LiteralPath $outputPath) {
            $parts = $line.Split('=', 2)
            $result[$parts[0]] = $parts[1]
        }
        return $result
    } finally {
        if (Test-Path -LiteralPath $tempRoot) {
            Remove-Item -LiteralPath $tempRoot -Recurse -Force
        }
    }
}

$explicitHigherVersion = Invoke-ResolveReleaseVersionForTest `
    -SourceVersion '0.1.186' `
    -LatestVersion 'v0.1.180'
Assert-Equal $explicitHigherVersion['should_publish'] 'true' 'Explicit source version above Latest should publish.'
Assert-Equal $explicitHigherVersion['version_bump_required'] 'false' 'Explicit source version should not request a bump commit.'
Assert-Equal $explicitHigherVersion['version_tag'] 'v0.1.186' 'Explicit source version tag was not used.'
Assert-Equal $explicitHigherVersion['app_version'] '0.1.186' 'Explicit source app version was not used.'

$matchingLatestVersion = Invoke-ResolveReleaseVersionForTest `
    -SourceVersion '0.1.180' `
    -LatestVersion 'v0.1.180'
Assert-Equal $matchingLatestVersion['should_publish'] 'false' 'Matching Latest should not publish the same version.'
Assert-Equal $matchingLatestVersion['version_bump_required'] 'true' 'Matching Latest should request a bump commit.'
Assert-Equal $matchingLatestVersion['version_tag'] 'v0.1.181' 'Matching Latest should propose the next patch.'

$lowerThanLatestWasRejected = $false
try {
    Invoke-ResolveReleaseVersionForTest `
        -SourceVersion '0.1.179' `
        -LatestVersion 'v0.1.180' | Out-Null
} catch {
    $lowerThanLatestWasRejected = $true
}
Assert-Equal $lowerThanLatestWasRejected $true 'Source version below Latest was not rejected.'

Write-Host 'Release version tests passed.'
