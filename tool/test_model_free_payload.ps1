[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("cinevault-model-free-test-" + [guid]::NewGuid().ToString("N"))
$gateScript = Join-Path $PSScriptRoot "assert_model_free_payload.ps1"

try {
    $safeModelRoot = Join-Path $testRoot "data\models\bge-small-zh-v1.5\onnx"
    New-Item -ItemType Directory -Force -Path $safeModelRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $safeModelRoot "model_quantized.onnx") -Value "test" -Encoding ASCII
    & $gateScript -PayloadRoot $testRoot

    $qwenRoot = Join-Path $testRoot "data\models\qwen3-0.6b"
    New-Item -ItemType Directory -Force -Path $qwenRoot | Out-Null
    Set-Content -LiteralPath (Join-Path $qwenRoot "model.bin") -Value "test" -Encoding ASCII
    $qwenBlocked = $false
    try {
        & $gateScript -PayloadRoot $testRoot
    } catch {
        $qwenBlocked = $_.Exception.Message -match "qwen3-0.6b"
    }
    if (-not $qwenBlocked) {
        throw "The payload gate did not reject the Qwen model directory."
    }

    Remove-Item -LiteralPath $qwenRoot -Recurse -Force
    $ggufPath = Join-Path $testRoot "unexpected.GGUF"
    Set-Content -LiteralPath $ggufPath -Value "test" -Encoding ASCII
    $ggufBlocked = $false
    try {
        & $gateScript -PayloadRoot $testRoot
    } catch {
        $ggufBlocked = $_.Exception.Message -match "unexpected.GGUF"
    }
    if (-not $ggufBlocked) {
        throw "The payload gate did not reject a GGUF file."
    }
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

Write-Host "Model-free payload gate tests passed."
