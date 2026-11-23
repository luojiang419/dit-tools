function Get-NextDistVersion {
    param(
        [string]$DistRoot,
        [string[]]$ReferenceRoots = @()
    )

    $roots = @($DistRoot) + $ReferenceRoots
    $versions = @()

    foreach ($root in $roots) {
        if (-not (Test-Path $root)) {
            continue
        }

        $versions += Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^v(\d+)\.(\d+)\.(\d+)$' } |
            ForEach-Object {
                [PSCustomObject]@{
                    Major = [int]$Matches[1]
                    Minor = [int]$Matches[2]
                    Patch = [int]$Matches[3]
                }
            }
    }

    $versions = $versions | Sort-Object Major, Minor, Patch

    if (-not $versions) {
        return "v0.1.0"
    }

    $latest = $versions[-1]
    return "v{0}.{1}.{2}" -f $latest.Major, $latest.Minor, ($latest.Patch + 1)
}

function ConvertTo-CineVaultVersionParts {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Version
    )

    $match = [regex]::Match($Version.Trim(), '^v?(\d+)\.(\d+)\.(\d+)$')
    if (-not $match.Success) {
        throw "Invalid CineVault semantic version: $Version"
    }

    return [PSCustomObject]@{
        Major = [long]$match.Groups[1].Value
        Minor = [long]$match.Groups[2].Value
        Patch = [long]$match.Groups[3].Value
    }
}

function Format-CineVaultVersionTag {
    param(
        [Parameter(Mandatory = $true)]
        $VersionParts
    )

    return "v{0}.{1}.{2}" -f $VersionParts.Major, $VersionParts.Minor, $VersionParts.Patch
}

function Get-NormalizedCineVaultVersionTag {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Version
    )

    return Format-CineVaultVersionTag (ConvertTo-CineVaultVersionParts -Version $Version)
}

function Get-CineVaultSourceVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $versionPath = Join-Path $RepositoryRoot 'VERSION'
    if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
        throw "CineVault version source is missing: $versionPath"
    }

    $sourceVersion = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    $parts = ConvertTo-CineVaultVersionParts -Version $sourceVersion
    $normalizedVersion = "{0}.{1}.{2}" -f $parts.Major, $parts.Minor, $parts.Patch
    if ($sourceVersion -cne $normalizedVersion) {
        throw "VERSION must contain exactly one normalized semantic version without a v prefix: $sourceVersion"
    }
    return $normalizedVersion
}

function Compare-CineVaultVersions {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Left,

        [Parameter(Mandatory = $true)]
        [string]$Right
    )

    $leftParts = ConvertTo-CineVaultVersionParts -Version $Left
    $rightParts = ConvertTo-CineVaultVersionParts -Version $Right

    foreach ($property in @('Major', 'Minor', 'Patch')) {
        if ($leftParts.$property -lt $rightParts.$property) {
            return -1
        }
        if ($leftParts.$property -gt $rightParts.$property) {
            return 1
        }
    }

    return 0
}

function Get-NextCineVaultReleaseVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$LatestVersion,

        [string]$MinimumVersion = 'v0.1.169'
    )

    $latest = ConvertTo-CineVaultVersionParts -Version $LatestVersion
    if ($latest.Patch -eq [long]::MaxValue) {
        throw "Patch version cannot be incremented: $LatestVersion"
    }

    $candidate = Format-CineVaultVersionTag ([PSCustomObject]@{
        Major = $latest.Major
        Minor = $latest.Minor
        Patch = $latest.Patch + 1
    })
    $minimum = Get-NormalizedCineVaultVersionTag -Version $MinimumVersion

    if ((Compare-CineVaultVersions -Left $candidate -Right $minimum) -lt 0) {
        return $minimum
    }
    return $candidate
}
