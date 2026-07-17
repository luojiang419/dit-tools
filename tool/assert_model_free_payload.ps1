[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PayloadRoot
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$resolvedRoot = (Resolve-Path -LiteralPath $PayloadRoot -ErrorAction Stop).Path
$forbidden = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -Force | Where-Object {
    ($_.PSIsContainer -and $_.Name -ieq "qwen3-0.6b") -or
    (-not $_.PSIsContainer -and $_.Extension -ieq ".gguf")
})

if ($forbidden.Count -gt 0) {
    $relativePaths = $forbidden |
        ForEach-Object { [System.IO.Path]::GetRelativePath($resolvedRoot, $_.FullName) } |
        Sort-Object
    throw "Qwen model assets are forbidden in release payloads:`n$($relativePaths -join [Environment]::NewLine)"
}

$payloadBytes = (Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Force |
    Measure-Object -Property Length -Sum).Sum
if ($null -eq $payloadBytes) {
    $payloadBytes = 0
}
Write-Host "Model-free payload verified: $resolvedRoot ($payloadBytes bytes)"
