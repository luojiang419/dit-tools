param(
    [ValidateSet('flat', 'regular', 'deep')]
    [string]$Shape = 'regular',

    [ValidateRange(1, 2000000)]
    [int]$FileCount = 100000,

    [string]$OutputPath = '',

    [ValidateRange(1, 10000)]
    [int]$FilesPerDirectory = 1000,

    [ValidateRange(1, 32)]
    [int]$DeepDirectoryDepth = 8,

    [switch]$Resume
)

$ErrorActionPreference = 'Stop'

function Get-NormalizedFullPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    return [System.IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

$repositoryRoot = Get-NormalizedFullPath (Join-Path $PSScriptRoot '..')
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $fixtureName = 'cinevault-large-catalog-{0}-{1}' -f $Shape, (Get-Date -Format 'yyyyMMdd-HHmmss')
    $OutputPath = Join-Path ([System.IO.Path]::GetTempPath()) $fixtureName
}
$outputRoot = Get-NormalizedFullPath $OutputPath

if ($outputRoot.Equals($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
    $outputRoot.StartsWith($repositoryRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "合成素材目录必须位于仓库外：$outputRoot"
}

if (Test-Path -LiteralPath $outputRoot) {
    $existingEntry = Get-ChildItem -LiteralPath $outputRoot -Force | Select-Object -First 1
    if ($null -ne $existingEntry -and -not $Resume) {
        throw "输出目录非空；为保护现有文件，不会覆盖。若要续建请显式使用 -Resume：$outputRoot"
    }
} else {
    New-Item -ItemType Directory -Path $outputRoot | Out-Null
}

$extensions = @('jpg', 'mov', 'wav', 'dng', 'cr3', 'nef', 'arw', 'txt')
$createdCount = 0
$skippedCount = 0
$startedAt = [System.Diagnostics.Stopwatch]::StartNew()

for ($index = 0; $index -lt $FileCount; $index++) {
    switch ($Shape) {
        'flat' {
            $directory = $outputRoot
        }
        'regular' {
            $bucket = [long][Math]::Floor($index / $FilesPerDirectory)
            $directory = Join-Path $outputRoot ('bucket-{0:D6}' -f $bucket)
        }
        'deep' {
            $leaf = [long][Math]::Floor($index / $FilesPerDirectory)
            $directory = $outputRoot
            for ($depth = 0; $depth -lt $DeepDirectoryDepth; $depth++) {
                $segmentValue = [long]([Math]::Floor($leaf / [Math]::Pow(10, $depth)) % 10)
                $directory = Join-Path $directory ('d{0:D2}-{1:D2}' -f $depth, $segmentValue)
            }
            $directory = Join-Path $directory ('leaf-{0:D6}' -f $leaf)
        }
    }

    if (-not (Test-Path -LiteralPath $directory)) {
        New-Item -ItemType Directory -Path $directory | Out-Null
    }
    $extension = $extensions[$index % $extensions.Count]
    $filePath = Join-Path $directory ('asset-{0:D8}.{1}' -f $index, $extension)
    if ($Resume -and (Test-Path -LiteralPath $filePath)) {
        $skippedCount++
        continue
    }

    $stream = [System.IO.File]::Open(
        $filePath,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::Read)
    $stream.Dispose()
    $createdCount++

    if (($index + 1) % 10000 -eq 0) {
        Write-Progress -Activity '生成 CineVault 巨量素材压力目录' `
            -Status ("已处理 {0:N0}/{1:N0}" -f ($index + 1), $FileCount) `
            -PercentComplete ((($index + 1) * 100) / $FileCount)
    }
}

$startedAt.Stop()
Write-Progress -Activity '生成 CineVault 巨量素材压力目录' -Completed
$manifestPath = "$outputRoot.manifest.json"
$manifest = [ordered]@{
    schema_version = 1
    generated_at = (Get-Date).ToUniversalTime().ToString('o')
    output_path = $outputRoot
    shape = $Shape
    requested_file_count = $FileCount
    created_file_count = $createdCount
    skipped_file_count = $skippedCount
    files_per_directory = $FilesPerDirectory
    deep_directory_depth = $DeepDirectoryDepth
    elapsed_ms = $startedAt.ElapsedMilliseconds
}
[System.IO.File]::WriteAllText(
    $manifestPath,
    ($manifest | ConvertTo-Json -Depth 3),
    [System.Text.UTF8Encoding]::new($false))

Write-Output ($manifest | ConvertTo-Json -Compress)
