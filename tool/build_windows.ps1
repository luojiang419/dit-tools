param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$FfmpegDevRoot = $env:FFMPEG_DEV_ROOT,
    [string]$Configuration = "Release",
    [string]$Version,
    [string]$UpdateSignerSha256 = $env:CINEVAULT_UPDATE_SIGNER_SHA256,
    [string]$SigningCertificateSha1 = $env:CINEVAULT_SIGNING_CERT_SHA1,
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [switch]$RealWorkflow,
    [switch]$EnableFfmpeg,
    [switch]$RequireSigning
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot\windows_toolchain.ps1"
. "$PSScriptRoot\version_helpers.ps1"

$normalizedUpdateSignerSha256 = ([string]$UpdateSignerSha256).Replace(' ', '').Trim().ToLowerInvariant()
$normalizedSigningCertificateSha1 = ([string]$SigningCertificateSha1).Replace(' ', '').Trim().ToLowerInvariant()
if (-not [string]::IsNullOrWhiteSpace($normalizedUpdateSignerSha256) -and
    $normalizedUpdateSignerSha256 -notmatch '^[0-9a-f]{64}$') {
    throw "Update signer SHA-256 must contain exactly 64 hexadecimal characters."
}
if (-not [string]::IsNullOrWhiteSpace($normalizedSigningCertificateSha1) -and
    $normalizedSigningCertificateSha1 -notmatch '^[0-9a-f]{40}$') {
    throw "Signing certificate SHA-1 selector must contain exactly 40 hexadecimal characters."
}
if ($RequireSigning -and
    ([string]::IsNullOrWhiteSpace($normalizedUpdateSignerSha256) -or
     [string]::IsNullOrWhiteSpace($normalizedSigningCertificateSha1))) {
    throw "A trusted Authenticode certificate is required for a production release."
}

function Get-CineVaultSignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $candidate = Get-ChildItem -LiteralPath $kitsRoot -Recurse -Filter signtool.exe -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'x64' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "signtool.exe was not found."
    }
    return $candidate.FullName
}

function Assert-CineVaultSignature {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerSha256
    )

    $signature = Get-AuthenticodeSignature -LiteralPath $Path
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid -or
        $null -eq $signature.SignerCertificate) {
        throw "Authenticode signature verification failed for ${Path}: $($signature.StatusMessage)"
    }
    $actualSignerSha256 = $signature.SignerCertificate.GetCertHashString('SHA256').ToLowerInvariant()
    if ($actualSignerSha256 -cne $ExpectedSignerSha256) {
        throw "Authenticode signer mismatch for $Path."
    }
}

function Invoke-CineVaultSigning {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$SignToolPath,
        [Parameter(Mandatory = $true)][string]$CertificateSha1,
        [Parameter(Mandatory = $true)][string]$ExpectedSignerSha256,
        [Parameter(Mandatory = $true)][string]$Rfc3161TimestampUrl
    )

    foreach ($attempt in 1..3) {
        & $SignToolPath sign /q /sha1 $CertificateSha1 /s My /fd SHA256 /tr $Rfc3161TimestampUrl /td SHA256 $Path
        if ($LASTEXITCODE -eq 0) {
            Assert-CineVaultSignature -Path $Path -ExpectedSignerSha256 $ExpectedSignerSha256
            return
        }
        if ($attempt -lt 3) {
            Start-Sleep -Seconds (5 * $attempt)
        }
    }
    throw "Authenticode signing failed after three attempts: $Path"
}

if ($RealWorkflow -and $EnableFfmpeg) {
    throw "-RealWorkflow uses the real project/import workflow without FFmpeg. Use -EnableFfmpeg by itself for the FFmpeg build."
}

$context = Get-CineVaultBuildContext -QtRoot $QtRoot -FfmpegDevRoot $FfmpegDevRoot -RequireFfmpeg:$EnableFfmpeg
$projectRoot = Join-Path $context.RepoRoot "dit-tools-src\cinevault-pro"
$outputRoot = Join-Path $context.RepoRoot "output"
$sourceVersion = Get-CineVaultSourceVersion -RepositoryRoot $context.RepoRoot
$sourceVersionTag = Get-NormalizedCineVaultVersionTag -Version $sourceVersion
$version = if ([string]::IsNullOrWhiteSpace($Version)) {
    $sourceVersionTag
} else {
    Get-NormalizedCineVaultVersionTag -Version $Version
}
if ($version -cne $sourceVersionTag) {
    throw "Requested build version $version does not match repository VERSION $sourceVersionTag."
}
$appVersion = $version.TrimStart("v")

$isDebug = $Configuration -ieq "Debug"
$configurePreset = if ($EnableFfmpeg) {
    if ($isDebug) { "windows-msvc-debug-ffmpeg" } else { "windows-msvc-release-ffmpeg" }
} elseif ($RealWorkflow) {
    if ($isDebug) { "windows-msvc-debug-real" } else { "windows-msvc-release-real" }
} else {
    if ($isDebug) { "windows-msvc-debug" } else { "windows-msvc-release" }
}
$buildPreset = $configurePreset
$buildDirName = $configurePreset
$buildDir = Join-Path $projectRoot "build\$buildDirName"

$env:QT_ROOT = $context.QtRoot
if ($context.HasFfmpeg) {
    $env:FFMPEG_DEV_ROOT = $context.FfmpegDevRoot
} elseif (Test-Path env:FFMPEG_DEV_ROOT) {
    Remove-Item env:FFMPEG_DEV_ROOT
}

Push-Location $projectRoot
try {
    $exifToolCacheRoot = Join-Path $env:LOCALAPPDATA "CineVault\BuildCache\exiftool-v1"
    $exifToolPrepareScript = Join-Path $projectRoot "cmake\PrepareExifToolDependency.cmake"
    Invoke-VcVarsCommand "cmake -DOUTPUT_ROOT=`"$exifToolCacheRoot`" -P `"$exifToolPrepareScript`""
    if ($RealWorkflow -or $EnableFfmpeg) {
        $localSearchCacheRoot = Join-Path $env:LOCALAPPDATA "CineVault\BuildCache\local-search-v1"
        $localSearchPrepareScript = Join-Path $projectRoot "cmake\PrepareLocalSearchDependencies.cmake"
        Invoke-VcVarsCommand "cmake -DOUTPUT_ROOT=`"$localSearchCacheRoot`" -P `"$localSearchPrepareScript`""

        $assistantCacheRoot = Join-Path $env:LOCALAPPDATA "CineVault\BuildCache\search-assistant-v1"
        $assistantPrepareScript = Join-Path $projectRoot "cmake\PrepareSearchAssistantDependencies.cmake"
        Invoke-VcVarsCommand "cmake -DOUTPUT_ROOT=`"$assistantCacheRoot`" -P `"$assistantPrepareScript`""
    }
    Invoke-VcVarsCommand "cmake --preset $configurePreset -DCINEVAULT_APP_VERSION=$appVersion -DCINEVAULT_UPDATE_SIGNER_SHA256=$normalizedUpdateSignerSha256"
    Invoke-VcVarsCommand "cmake --build --preset $buildPreset --config $Configuration"
} finally {
    Pop-Location
}

$installerOutputDir = Join-Path $outputRoot $version
$stagingRoot = Join-Path $outputRoot "_installer_staging"
$stagingDir = Join-Path $stagingRoot $version

function Remove-InstallerStagingDir {
    param(
        [string]$Path,
        [string]$Root
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $resolvedRoot = [System.IO.Path]::GetFullPath($Root)
    $rootPrefix = $resolvedRoot
    if (-not $rootPrefix.EndsWith([System.IO.Path]::DirectorySeparatorChar)) {
        $rootPrefix += [System.IO.Path]::DirectorySeparatorChar
    }

    if ($resolvedPath.Equals($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase) -or
        -not $resolvedPath.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside installer staging root: $resolvedPath"
    }

    if (Test-Path $resolvedPath) {
        Remove-Item -LiteralPath $resolvedPath -Recurse -Force
    }
}

Remove-InstallerStagingDir -Path $stagingDir -Root $stagingRoot
New-Item -ItemType Directory -Force -Path $stagingDir, $installerOutputDir | Out-Null

$binaryCandidates = @(
    (Join-Path $buildDir "CineVault.exe"),
    (Join-Path $buildDir "src\app\CineVault.exe")
)

$exePath = $null
foreach ($candidate in $binaryCandidates) {
    if (Test-Path $candidate) {
        $exePath = $candidate
        break
    }
}

if ($null -eq $exePath) {
    throw "Build completed but CineVault.exe was not found."
}

Copy-Item $exePath -Destination $stagingDir -Force
$deployedExe = Join-Path $stagingDir "CineVault.exe"

$onnxRuntimeSource = Join-Path $buildDir "onnxruntime.dll"
$modelsSource = Join-Path $buildDir "data\models"
$localSearchModelSource = Join-Path $modelsSource "bge-small-zh-v1.5\onnx\model_quantized.onnx"
$hasOnnxRuntime = Test-Path -LiteralPath $onnxRuntimeSource -PathType Leaf
$hasLocalSearchModel = Test-Path -LiteralPath $localSearchModelSource -PathType Leaf
if ($hasOnnxRuntime -xor $hasLocalSearchModel) {
    throw "Local-search runtime assets are incomplete in the build directory. Expected both onnxruntime.dll and data/models/."
}
if ($hasOnnxRuntime -and $hasLocalSearchModel) {
    Copy-Item -LiteralPath $onnxRuntimeSource -Destination $stagingDir -Force
}

$assistantRuntimeSource = Join-Path $buildDir "search-assistant"
$assistantExecutableSource = Join-Path $assistantRuntimeSource "llama-server.exe"
$assistantModelSource = Join-Path $modelsSource "qwen3-0.6b\Qwen3-0.6B-Q8_0.gguf"
$hasAssistantRuntime = Test-Path -LiteralPath $assistantExecutableSource -PathType Leaf
$hasAssistantModel = Test-Path -LiteralPath $assistantModelSource -PathType Leaf
if ($hasAssistantRuntime -xor $hasAssistantModel) {
    throw "Search-assistant assets are incomplete in the build directory. Expected both search-assistant/llama-server.exe and the Qwen GGUF model."
}
if ($hasAssistantRuntime -and $hasAssistantModel) {
    Copy-Item -LiteralPath $assistantRuntimeSource -Destination $stagingDir -Recurse -Force
}

if ($hasLocalSearchModel -or $hasAssistantModel) {
    $localSearchDataTarget = Join-Path $stagingDir "data"
    New-Item -ItemType Directory -Force -Path $localSearchDataTarget | Out-Null
    Copy-Item -LiteralPath $modelsSource -Destination $localSearchDataTarget -Recurse -Force
}

$exifToolSource = Join-Path $buildDir "exiftool"
$exifToolExecutable = Join-Path $exifToolSource "exiftool.exe"
$exifToolLibrary = Join-Path $exifToolSource "exiftool_files\exiftool.pl"
if ((Test-Path -LiteralPath $exifToolExecutable -PathType Leaf) -and
    (Test-Path -LiteralPath $exifToolLibrary -PathType Leaf)) {
    Copy-Item -LiteralPath $exifToolSource -Destination $stagingDir -Recurse -Force
} elseif ($RealWorkflow -or $EnableFfmpeg) {
    throw "ExifTool runtime is missing from the build directory."
}

$deployMode = if ($Configuration -ieq "Debug") { "--debug" } else { "--release" }
Invoke-VcVarsCommand "`"$($context.WindeployQt)`" $deployMode --qmldir `"$projectRoot\src\ui\qml`" `"$deployedExe`""

$signToolPath = $null
if ($RequireSigning) {
    $signToolPath = Get-CineVaultSignTool
    Invoke-CineVaultSigning `
        -Path $deployedExe `
        -SignToolPath $signToolPath `
        -CertificateSha1 $normalizedSigningCertificateSha1 `
        -ExpectedSignerSha256 $normalizedUpdateSignerSha256 `
        -Rfc3161TimestampUrl $TimestampUrl
}

if ($context.HasFfmpegCli) {
    $ffmpegTargetRoot = Join-Path $stagingDir "ffmpeg"
    $ffmpegTargetBinDir = Join-Path $ffmpegTargetRoot "bin"
    $ffmpegSourceBinDir = Join-Path $context.FfmpegCliRoot "bin"

    New-Item -ItemType Directory -Force -Path $ffmpegTargetBinDir | Out-Null

    foreach ($exeName in @("ffmpeg.exe", "ffprobe.exe")) {
        Copy-Item (Join-Path $ffmpegSourceBinDir $exeName) -Destination (Join-Path $ffmpegTargetBinDir $exeName) -Force
    }

    foreach ($supportFile in @("LICENSE", "README.txt")) {
        $sourcePath = Join-Path $context.FfmpegCliRoot $supportFile
        if (Test-Path $sourcePath) {
            Copy-Item $sourcePath -Destination (Join-Path $ffmpegTargetRoot $supportFile) -Force
        }
    }
} else {
    Write-Warning "FFmpeg CLI runtime root was not found. Installer will not include ffmpeg.exe or ffprobe.exe."
}

# User databases and generated indexes can be created beside test binaries. They
# must never become installer resources, otherwise an upgrade can overwrite the
# installed user's parsed material data.
$forbiddenInstallerFiles = Get-ChildItem -LiteralPath $stagingDir -Recurse -File | Where-Object {
    $_.Name -match '\.(sqlite|sqlite3|db)(-.+|\..+)?$' -or
    $_.Extension -in @('.usearch', '.wal', '.shm')
}
if ($forbiddenInstallerFiles) {
    $forbiddenList = ($forbiddenInstallerFiles.FullName | Sort-Object) -join [Environment]::NewLine
    throw "Installer staging contains mutable user data and publishing was blocked:`n$forbiddenList"
}

& (Join-Path $context.RepoRoot "tool\test_cinevault_startup.ps1") `
    -ApplicationPath $deployedExe

$installerScript = Join-Path $context.RepoRoot "installer\windows\cinevault.iss"
if (-not (Test-Path $installerScript)) {
    throw "Installer script was not found: $installerScript"
}

& $context.InnoSetupCompiler "/DAppVersion=$appVersion" "/DVersionTag=$version" "/DSourceDir=$stagingDir" "/DOutputDir=$installerOutputDir" $installerScript
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup installer build failed."
}

$installerPath = Join-Path $installerOutputDir "CineVault-Setup-$version.exe"
if (-not (Test-Path $installerPath)) {
    throw "Installer build completed but output was not found: $installerPath"
}
if ($RequireSigning) {
    Invoke-CineVaultSigning `
        -Path $installerPath `
        -SignToolPath $signToolPath `
        -CertificateSha1 $normalizedSigningCertificateSha1 `
        -ExpectedSignerSha256 $normalizedUpdateSignerSha256 `
        -Rfc3161TimestampUrl $TimestampUrl
}

Remove-InstallerStagingDir -Path $stagingDir -Root $stagingRoot

Write-Host $installerPath
