[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [string]$InstallRoot = (Join-Path 'D:\Program Files' ([string]([char]0x5F71) + [char]0x8D44 + [char]0x7BA1 + [char]0x5BB6)),

    [Parameter(Mandatory = $true)]
    [string]$ExpectedExecutablePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath -ErrorAction Stop).Path
$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$expectedInstallRoot = [System.IO.Path]::GetFullPath(
    (Join-Path 'D:\Program Files' ([string]([char]0x5F71) + [char]0x8D44 + [char]0x7BA1 + [char]0x5BB6))
).TrimEnd('\')
if ($resolvedInstallRoot -ine $expectedInstallRoot) {
    throw "The verification script only allows the known CineVault install root: $expectedInstallRoot"
}

$applicationPath = Join-Path $resolvedInstallRoot 'CineVault.exe'
if (-not (Test-Path -LiteralPath $applicationPath -PathType Leaf)) {
    throw "The existing CineVault.exe was not found: $applicationPath"
}

$oldProcess = Start-Process `
    -FilePath $applicationPath `
    -WorkingDirectory $resolvedInstallRoot `
    -WindowStyle Hidden `
    -PassThru
Start-Sleep -Seconds 6

$runningBeforeInstall = @(Get-Process -Name CineVault -ErrorAction SilentlyContinue)
if ($runningBeforeInstall.Count -eq 0) {
    throw 'The existing CineVault process did not start; the force-close path cannot be verified.'
}
Write-Host ('Existing process before install: ' + (($runningBeforeInstall | ForEach-Object { "$($_.Id) $($_.Path)" }) -join '; '))

$installerArguments = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /FORCECLOSEAPPLICATIONS /DIR="' + $resolvedInstallRoot + '"'
$installerProcess = Start-Process `
    -FilePath $resolvedInstaller `
    -ArgumentList $installerArguments `
    -WorkingDirectory (Split-Path -Parent $resolvedInstaller) `
    -WindowStyle Hidden `
    -PassThru

if (-not $installerProcess.WaitForExit(600000)) {
    throw 'The installer did not finish within 600 seconds.'
}
Write-Host ('Installer exit code: ' + $installerProcess.ExitCode)
if ($installerProcess.ExitCode -ne 0) {
    throw "The installer failed with exit code $($installerProcess.ExitCode)."
}

Start-Sleep -Seconds 2
$runningAfterInstall = @(Get-Process -Name CineVault -ErrorAction SilentlyContinue)
if ($runningAfterInstall.Count -ne 0) {
    throw ('A CineVault process remained after installation: ' + (($runningAfterInstall | ForEach-Object { "$($_.Id) $($_.Path)" }) -join '; '))
}

$expectedExecutableHash = (Get-FileHash -LiteralPath $ExpectedExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant()
$installedExecutableHash = (Get-FileHash -LiteralPath $applicationPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host ('Installed executable SHA-256: ' + $installedExecutableHash)
if ($installedExecutableHash -ne $expectedExecutableHash) {
    throw "Installed executable hash is incorrect. Expected $expectedExecutableHash, got $installedExecutableHash."
}

$newProcess = Start-Process `
    -FilePath $applicationPath `
    -WorkingDirectory $resolvedInstallRoot `
    -WindowStyle Hidden `
    -PassThru
$health = $null
for ($attempt = 1; $attempt -le 30; $attempt++) {
    Start-Sleep -Seconds 1
    try {
        $health = Invoke-RestMethod -Uri 'http://127.0.0.1:17890/api/health' -TimeoutSec 2
        if ($health.ok -eq $true) {
            break
        }
    } catch {
        $health = $null
    }
}

if ($null -eq $health -or $health.ok -ne $true) {
    Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    throw 'The new application did not return ok=true from /api/health.'
}
Write-Host ('Web health check passed: ok=' + $health.ok)

Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
$listeners = @(Get-NetTCPConnection -LocalPort 17890 -State Listen -ErrorAction SilentlyContinue)
if ($listeners.Count -ne 0) {
    throw 'Port 17890 remained in LISTEN state after the new process ended.'
}
Write-Host 'The new process ended and port 17890 was released.'
