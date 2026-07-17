param(
    [Parameter(Mandatory = $true)]
    [string]$QmllintPath,
    [Parameter(Mandatory = $true)]
    [string]$ModuleImportPath,
    [string]$QmlSourceRoot = "",
    [int]$MaxWarnings = 731
)

$ErrorActionPreference = "Stop"

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($QmlSourceRoot)) {
    $QmlSourceRoot = Join-Path $repositoryRoot "dit-tools-src/cinevault-pro/src/ui/qml"
}

$resolvedQmllint = (Resolve-Path -LiteralPath $QmllintPath).Path
$resolvedImportPath = (Resolve-Path -LiteralPath $ModuleImportPath).Path
$resolvedQmlRoot = (Resolve-Path -LiteralPath $QmlSourceRoot).Path
$qmlFiles = @(Get-ChildItem -LiteralPath $resolvedQmlRoot -Recurse -Filter *.qml |
    Sort-Object FullName |
    ForEach-Object FullName)
if ($qmlFiles.Count -eq 0) {
    throw "没有找到待检查的 QML 文件：$resolvedQmlRoot"
}

$previousNativePreference = $PSNativeCommandUseErrorActionPreference
$PSNativeCommandUseErrorActionPreference = $false
try {
    $jsonLines = & $resolvedQmllint --json - -I $resolvedImportPath @qmlFiles 2>$null
} finally {
    $PSNativeCommandUseErrorActionPreference = $previousNativePreference
}
if (-not $jsonLines) {
    throw "qmllint 没有返回 JSON 诊断"
}

$document = (($jsonLines -join "`n") | ConvertFrom-Json)
$warnings = @($document.files | ForEach-Object { $_.warnings })
$errors = @($warnings | Where-Object { $_.type -eq "error" })
$counts = @{}
foreach ($warning in $warnings) {
    $id = [string]$warning.id
    if (-not $counts.ContainsKey($id)) {
        $counts[$id] = 0
    }
    $counts[$id]++
}

Write-Host "QML lint：文件=$($document.files.Count)，diagnostic=$($warnings.Count)，error=$($errors.Count)，上限=$MaxWarnings"
$counts.GetEnumerator() |
    Sort-Object Value -Descending |
    ForEach-Object { Write-Host ("  {0}={1}" -f $_.Key, $_.Value) }

$forbiddenCategories = @("incompatible-type", "layout", "missing-property")
$forbiddenCount = 0
foreach ($category in $forbiddenCategories) {
    if ($counts.ContainsKey($category)) {
        $forbiddenCount += $counts[$category]
    }
}
if ($forbiddenCount -gt 0) {
    throw "QML 仍包含 $forbiddenCount 条类型、布局或缺失属性诊断"
}
if ($errors.Count -gt 0) {
    $errorSummary = ($errors | ForEach-Object { "[$($_.id)] $($_.message)" }) -join "`n"
    throw "QML 仍包含 $($errors.Count) 条 error：`n$errorSummary"
}
if ($warnings.Count -gt $MaxWarnings) {
    throw "QML warning 增加：实际 $($warnings.Count)，允许最多 $MaxWarnings"
}
