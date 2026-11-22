[CmdletBinding()]
param(
    [string]$InstallRoot = (Join-Path 'D:\Program Files' ([string]([char]0x5F71) + [char]0x8D44 + [char]0x7BA1 + [char]0x5BB6))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedInstallRoot = [System.IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$expectedInstallRoot = [System.IO.Path]::GetFullPath(
    (Join-Path 'D:\Program Files' ([string]([char]0x5F71) + [char]0x8D44 + [char]0x7BA1 + [char]0x5BB6))
).TrimEnd('\')
if ($resolvedInstallRoot -ine $expectedInstallRoot) {
    throw "The verification script only allows the known CineVault install root: $expectedInstallRoot"
}

$applicationPath = Join-Path $resolvedInstallRoot 'CineVault.exe'
$process = Start-Process -FilePath $applicationPath -WorkingDirectory $resolvedInstallRoot -WindowStyle Hidden -PassThru
$health = $null
for ($attempt = 1; $attempt -le 30; $attempt++) {
    Start-Sleep -Seconds 1
    try {
        $health = Invoke-RestMethod -Uri 'http://127.0.0.1:17890/api/health' -TimeoutSec 2
        if ($health.ok -eq $true) { break }
    } catch {
        $health = $null
    }
}
if ($null -eq $health -or $health.ok -ne $true) {
    Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    throw 'The installed application did not expose a healthy Web service.'
}

$response = Invoke-RestMethod -Uri 'http://127.0.0.1:17890/api/search?q=%E5%8E%BB%E5%B9%B4%E7%9A%84%E8%A7%86%E9%A2%91&limit=10' -TimeoutSec 10
Write-Host ('Interpretation: ' + (@($response.interpretation) -join ' | '))
Write-Host ('Total: ' + $response.total + '; Returned: ' + $response.returned)
foreach ($item in @($response.results)) {
    Write-Host ("Result: {0}; captureDate={1}; source={2}; modifiedAt={3}" -f `
        $item.fileName, $item.captureDate, $item.captureTimeSource, $item.modifiedAt)
}

if (-not (@($response.interpretation) -match '2025-01-01')) {
    Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    throw 'The installed Web search response did not expose the expected 2025 date range.'
}

Get-Process -Name CineVault -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if (@(Get-NetTCPConnection -LocalPort 17890 -State Listen -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Port 17890 remained in LISTEN state after the installed process ended.'
}
Write-Host 'Installed Web search verification passed and port 17890 was released.'
