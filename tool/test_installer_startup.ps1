[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$InstallerPath,

    [Parameter(Mandatory = $true)]
    [string]$InstallRoot,

    [switch]$AllowInstallerRegistration,

    [ValidateRange(30, 900)]
    [int]$InstallTimeoutSeconds = 600
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (-not $AllowInstallerRegistration) {
    throw "Installer startup testing writes a temporary uninstall registration. Pass -AllowInstallerRegistration only on a disposable CI runner."
}

$resolvedInstaller = (Resolve-Path -LiteralPath $InstallerPath -ErrorAction Stop).Path
$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$allowedTemporaryRoots = @([System.IO.Path]::GetTempPath())
if (-not [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    $allowedTemporaryRoots += $env:RUNNER_TEMP
}
$installRootIsTemporary = $allowedTemporaryRoots |
    ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd('\') + '\' } |
    Where-Object {
        $resolvedInstallRoot.StartsWith($_, [System.StringComparison]::OrdinalIgnoreCase)
    } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($installRootIsTemporary)) {
    throw "Installer probe root must stay below an approved temporary directory: $resolvedInstallRoot"
}

if (Test-Path -LiteralPath $resolvedInstallRoot) {
    Remove-Item -LiteralPath $resolvedInstallRoot -Recurse -Force
}

# Simulate an upgrade from the one-time model-bearing installer. The new
# model-free installer must leave this existing asset untouched.
$preservedModelPath = Join-Path $resolvedInstallRoot "data\models\qwen3-0.6b\Qwen3-0.6B-Q8_0.gguf"
$preservedModelContents = "cinevault-preexisting-qwen-model-sentinel"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $preservedModelPath) | Out-Null
Set-Content -LiteralPath $preservedModelPath -Value $preservedModelContents -Encoding ASCII -NoNewline

$installerLog = "$resolvedInstallRoot-installer.log"
if (Test-Path -LiteralPath $installerLog) {
    Remove-Item -LiteralPath $installerLog -Force
}

$arguments = @(
    "/SP-",
    "/VERYSILENT",
    "/SUPPRESSMSGBOXES",
    "/NORESTART",
    "/NOCANCEL",
    "/CLOSEAPPLICATIONS",
    "/FORCECLOSEAPPLICATIONS",
    "/NOICONS",
    "/DIR=`"$resolvedInstallRoot`"",
    "/LOG=`"$installerLog`""
)

$installer = Start-Process `
    -FilePath $resolvedInstaller `
    -ArgumentList $arguments `
    -WindowStyle Hidden `
    -PassThru

if (-not $installer.WaitForExit($InstallTimeoutSeconds * 1000)) {
    try {
        $installer.Kill($true)
        $installer.WaitForExit()
    } catch {
        Write-Warning "Failed to stop timed-out installer probe: $($_.Exception.Message)"
    }
    throw "Installer startup probe timed out after $InstallTimeoutSeconds seconds. Log: $installerLog"
}

if ($installer.ExitCode -ne 0) {
    throw "Installer startup probe failed with exit code $($installer.ExitCode). Log: $installerLog"
}

if (-not (Test-Path -LiteralPath $preservedModelPath -PathType Leaf) -or
    (Get-Content -LiteralPath $preservedModelPath -Raw) -cne $preservedModelContents) {
    throw "The model-free installer removed or overwrote the Qwen model from an existing installation."
}

$applicationPath = Join-Path $resolvedInstallRoot "CineVault.exe"
$uninstallerPath = Join-Path $resolvedInstallRoot "unins000.exe"
& (Join-Path $PSScriptRoot "test_cinevault_startup.ps1") -ApplicationPath $applicationPath

if (-not (Test-Path -LiteralPath $uninstallerPath -PathType Leaf)) {
    throw "Installer probe did not create the expected uninstaller: $uninstallerPath"
}

$programGroupPath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::CommonPrograms)) "影资管家"
$applicationShortcutPath = Join-Path $programGroupPath "影资管家.lnk"
$uninstallShortcutPath = Join-Path $programGroupPath "卸载影资管家.lnk"
if (-not (Test-Path -LiteralPath $applicationShortcutPath -PathType Leaf) -or
    -not (Test-Path -LiteralPath $uninstallShortcutPath -PathType Leaf)) {
    throw "Silent upgrade did not restore the required Start menu shortcuts."
}
$shell = New-Object -ComObject WScript.Shell
$applicationShortcut = $shell.CreateShortcut($applicationShortcutPath)
if ([System.IO.Path]::GetFullPath($applicationShortcut.TargetPath) -ine [System.IO.Path]::GetFullPath($applicationPath)) {
    throw "Start menu application shortcut points to an unexpected target: $($applicationShortcut.TargetPath)"
}

$uninstallRegistryPath = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{F3C5C6D6-8B77-4F6A-9F6B-9EA5B6D6AA21}_is1"
$uninstallRegistration = Get-ItemProperty -LiteralPath $uninstallRegistryPath -ErrorAction Stop
if ($uninstallRegistration.DisplayName -cne "影资管家" -or
    [System.IO.Path]::GetFullPath($uninstallRegistration.InstallLocation) -ine ($resolvedInstallRoot + '\') -or
    $uninstallRegistration.UninstallString -notmatch [regex]::Escape($uninstallerPath)) {
    throw "Silent upgrade did not preserve the Windows Apps registration."
}

$uninstaller = Start-Process `
    -FilePath $uninstallerPath `
    -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART") `
    -WindowStyle Hidden `
    -PassThru
if (-not $uninstaller.WaitForExit($InstallTimeoutSeconds * 1000)) {
    try {
        $uninstaller.Kill($true)
        $uninstaller.WaitForExit()
    } catch {
        Write-Warning "Failed to stop timed-out uninstaller cleanup: $($_.Exception.Message)"
    }
    throw "Installer probe cleanup timed out after $InstallTimeoutSeconds seconds."
}
if ($uninstaller.ExitCode -ne 0) {
    throw "Installer probe cleanup failed with exit code $($uninstaller.ExitCode)."
}

# The sentinel predates this installer and is intentionally not in its uninstall
# log, so remove the remaining temporary probe directory after registration and
# shortcuts have been cleaned by the real uninstaller.
foreach ($cleanupAttempt in 1..20) {
    if (-not (Test-Path -LiteralPath $resolvedInstallRoot)) {
        break
    }
    try {
        Remove-Item -LiteralPath $resolvedInstallRoot -Recurse -Force
        break
    } catch [System.UnauthorizedAccessException], [System.IO.IOException] {
        if ($cleanupAttempt -eq 20) {
            throw
        }
        Start-Sleep -Milliseconds 500
    }
}
Remove-Item -LiteralPath $installerLog -Force
Write-Host "Installed CineVault startup probe passed."
