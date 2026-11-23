$ErrorActionPreference = "Stop"

. "$PSScriptRoot\windows_toolchain.ps1"

$repoRoot = Get-CineVaultRepoRoot
$expectedRepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $repoRoot.Equals($expectedRepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Repository root mismatch. Expected '$expectedRepoRoot', got '$repoRoot'."
}

$missingDrive = @("Z", "Y", "X", "W", "V") |
    Where-Object { $null -eq (Get-PSDrive -Name $_ -ErrorAction SilentlyContinue) } |
    Select-Object -First 1

if ([string]::IsNullOrWhiteSpace($missingDrive)) {
    throw "No unused drive letter was available for the missing-drive regression test."
}

$missingPath = "${missingDrive}:\cinevault-ci-path-must-not-exist"
if (Test-CineVaultPath -Path $missingPath) {
    throw "Missing-drive path unexpectedly exists: $missingPath"
}

# This call used to terminate on GitHub-hosted runners because local G: fallback
# candidates were evaluated while ErrorActionPreference was set to Stop.
$null = Resolve-FfmpegDevRoot -FfmpegDevRoot $missingPath

Write-Host "Windows toolchain path tests passed."
