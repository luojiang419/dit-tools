[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ApplicationPath,

    [ValidateRange(5, 120)]
    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$resolvedApplication = (Resolve-Path -LiteralPath $ApplicationPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedApplication -PathType Leaf)) {
    throw "CineVault startup probe executable was not found: $resolvedApplication"
}

$workingDirectory = Split-Path -Parent $resolvedApplication
$process = Start-Process `
    -FilePath $resolvedApplication `
    -ArgumentList "--qml-startup-probe" `
    -WorkingDirectory $workingDirectory `
    -WindowStyle Hidden `
    -PassThru

if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
    try {
        $process.Kill($true)
        $process.WaitForExit()
    } catch {
        Write-Warning "Failed to stop timed-out CineVault startup probe: $($_.Exception.Message)"
    }
    throw "CineVault startup probe timed out after $TimeoutSeconds seconds."
}

if ($process.ExitCode -ne 0) {
    throw "CineVault startup probe failed with exit code $($process.ExitCode)."
}

Write-Host "CineVault QML startup probe passed: $resolvedApplication"
