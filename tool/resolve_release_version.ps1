[CmdletBinding()]
param(
    [string]$Repository = $env:GITHUB_REPOSITORY,
    [string]$LatestVersion,
    [string]$MinimumVersion = 'v0.1.169',
    [string]$CommitSha = $env:GITHUB_SHA,
    [string]$GitHubOutput = $env:GITHUB_OUTPUT,
    [string]$RepositoryRoot = (Split-Path -Parent $PSScriptRoot),
    [switch]$SkipExistingCommitCheck
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

. "$PSScriptRoot\version_helpers.ps1"

$sourceVersion = Get-CineVaultSourceVersion -RepositoryRoot $RepositoryRoot
$sourceVersionTag = Get-NormalizedCineVaultVersionTag -Version $sourceVersion

function Write-WorkflowOutput {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    if (-not [string]::IsNullOrWhiteSpace($GitHubOutput)) {
        Add-Content -LiteralPath $GitHubOutput -Value "$Name=$Value" -Encoding UTF8
    }
}

if (-not $SkipExistingCommitCheck -and -not [string]::IsNullOrWhiteSpace($CommitSha)) {
    $existingTags = @(& git tag --points-at $CommitSha --list 'v*')
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect release tags for commit $CommitSha."
    }

    foreach ($existingTagValue in $existingTags) {
        $existingTag = $existingTagValue.Trim()
        if ([string]::IsNullOrWhiteSpace($existingTag)) {
            continue
        }

        try {
            $existingTag = Get-NormalizedCineVaultVersionTag -Version $existingTag
        } catch {
            continue
        }

        if ([string]::IsNullOrWhiteSpace($Repository)) {
            throw "Repository is required to inspect existing release $existingTag."
        }

        $releaseJson = & gh release view $existingTag --repo $Repository --json isDraft,isPrerelease 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Commit $CommitSha already has tag $existingTag, but no readable GitHub Release. Resolve the tag before publishing again."
        }

        $release = $releaseJson | ConvertFrom-Json
        if ($release.isDraft) {
            throw "Commit $CommitSha already has stale draft release $existingTag. Delete or publish it before retrying."
        }
        if ($existingTag -cne $sourceVersionTag) {
            throw "Commit tag $existingTag does not match repository VERSION $sourceVersionTag."
        }

        Write-WorkflowOutput -Name 'should_publish' -Value 'false'
        Write-WorkflowOutput -Name 'version_bump_required' -Value 'false'
        Write-WorkflowOutput -Name 'version_tag' -Value $existingTag
        Write-WorkflowOutput -Name 'app_version' -Value $existingTag.TrimStart('v')
        Write-Host "Commit $CommitSha is already published as $existingTag; release work will be skipped."
        return
    }
}

if ([string]::IsNullOrWhiteSpace($LatestVersion)) {
    if ([string]::IsNullOrWhiteSpace($Repository)) {
        throw 'Repository is required when LatestVersion is not provided.'
    }

    $LatestVersion = (& gh api "repos/$Repository/releases/latest" --jq '.tag_name').Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($LatestVersion)) {
        throw "Unable to resolve the latest published release for $Repository."
    }
}

$versionTag = Get-NextCineVaultReleaseVersion `
    -LatestVersion $LatestVersion `
    -MinimumVersion $MinimumVersion
$appVersion = $versionTag.TrimStart('v')

$normalizedLatestVersion = Get-NormalizedCineVaultVersionTag -Version $LatestVersion
$comparisonWithLatest = Compare-CineVaultVersions -Left $sourceVersionTag -Right $normalizedLatestVersion
if ($comparisonWithLatest -eq 0) {
    Write-WorkflowOutput -Name 'should_publish' -Value 'false'
    Write-WorkflowOutput -Name 'version_bump_required' -Value 'true'
    Write-WorkflowOutput -Name 'version_tag' -Value $versionTag
    Write-WorkflowOutput -Name 'app_version' -Value $appVersion
    Write-Host "Repository VERSION matches latest release; a version-only commit is required: $versionTag"
    return
}
if ($sourceVersionTag -cne $versionTag) {
    throw "Repository VERSION $sourceVersionTag must equal latest release $normalizedLatestVersion or the next release $versionTag."
}

Write-WorkflowOutput -Name 'should_publish' -Value 'true'
Write-WorkflowOutput -Name 'version_bump_required' -Value 'false'
Write-WorkflowOutput -Name 'version_tag' -Value $sourceVersionTag
Write-WorkflowOutput -Name 'app_version' -Value $sourceVersion

Write-Host "Latest release:    $normalizedLatestVersion"
Write-Host "Repository VERSION: $sourceVersionTag"
