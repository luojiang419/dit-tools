# 135-安装包补齐FFmpeg运行时并完成安装验证

日期：2026-06-30

## 已完成内容

- 已读取上一轮最新快照：`134-素材管理中心默认排序改为文件名升序.md`。
- 已确认本轮只处理一个模块：
  - Windows 安装包内置 FFmpeg 运行时模块
- 已完成源码阶段备份：
  - 备份目录：`G:\data\app\DIT-tools\backup\v0.1.112`
  - 差异文档：`安装包补齐FFmpeg运行时-前后对比.md`
- 已定位根因：
  - `FFmpegAdapter` 只查环境变量和开发机固定路径 `G:/data/app/DIT/ffmpeg`
  - `tool/build_windows.ps1` 未把 `ffmpeg.exe`、`ffprobe.exe` 拷入安装包 staging 目录
- 已完成最小范围修复：
  - 运行时优先查找 `<应用目录>\ffmpeg\bin\` 与 `<应用目录>\`
  - 打包阶段自动查找 `CINEVAULT_FFMPEG_BIN`、`CINEVAULT_FFMPEG_ROOT`、`FFMPEG_DEV_ROOT`、`G:\data\app\DIT\ffmpeg`
  - 安装包自动内置 `ffmpeg.exe`、`ffprobe.exe`、`LICENSE`、`README.txt`
- 已完成真实工作流构建与打包：
  - 生成安装包：`G:\data\app\DIT-tools\output\v0.1.100\CineVault-Setup-v0.1.100.exe`
  - SHA256：`8C6C6BAE8AE9D826ED664EF06C2C0E87D9E27A8AA2C37A1C139D2A7AC26D7E02`
- 已完成本机静默升级验证：
  - 当前安装路径：`D:\Program Files\影资管家\`
  - 已落地文件：
    - `D:\Program Files\影资管家\ffmpeg\bin\ffmpeg.exe`
    - `D:\Program Files\影资管家\ffmpeg\bin\ffprobe.exe`
  - 安装目录命令行自检通过：
    - `ffmpeg.exe -version`
    - `ffprobe.exe -version`

## 当前修改到哪个模块

当前完成模块：007 Windows 安装包内置 FFmpeg 运行时模块。

本轮没有改动视频分析流程、素材解析逻辑、数据库结构或 QML 界面，只修复安装包依赖补齐和运行时定位链路。

## 具体修改的代码前后对比

### 修改前

```cpp
const auto defaultRoot = QStringLiteral("G:/data/app/DIT/ffmpeg");

m_ffprobePath = existingFile({
    envPath("CINEVAULT_FFPROBE_PATH"),
    exeFromBin(ffmpegBinRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(ffmpegRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(legacyDevRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(defaultRoot, QStringLiteral("ffprobe.exe"))
});
```

```powershell
Invoke-VcVarsCommand "`"$($context.WindeployQt)`" $deployMode --qmldir `"$projectRoot\src\ui\qml`" `"$deployedExe`""
$installerScript = Join-Path $context.RepoRoot "installer\windows\cinevault.iss"
```

### 修改后

```cpp
const auto defaultRoot = QStringLiteral("G:/data/app/DIT/ffmpeg");
const auto bundledAppDir = appPath();
const auto bundledFfmpegRoot = bundledAppDir.isEmpty()
    ? QString()
    : QDir(bundledAppDir).filePath(QStringLiteral("ffmpeg"));

m_ffprobePath = existingFile({
    envPath("CINEVAULT_FFPROBE_PATH"),
    exeFromBin(ffmpegBinRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(ffmpegRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(bundledFfmpegRoot, QStringLiteral("ffprobe.exe")),
    exeFromBin(bundledAppDir, QStringLiteral("ffprobe.exe")),
    exeFromRoot(legacyDevRoot, QStringLiteral("ffprobe.exe")),
    exeFromRoot(defaultRoot, QStringLiteral("ffprobe.exe"))
});
```

```powershell
Invoke-VcVarsCommand "`"$($context.WindeployQt)`" $deployMode --qmldir `"$projectRoot\src\ui\qml`" `"$deployedExe`""

if ($context.HasFfmpegCli) {
    $ffmpegTargetRoot = Join-Path $stagingDir "ffmpeg"
    $ffmpegTargetBinDir = Join-Path $ffmpegTargetRoot "bin"
    $ffmpegSourceBinDir = Join-Path $context.FfmpegCliRoot "bin"
    New-Item -ItemType Directory -Force -Path $ffmpegTargetBinDir | Out-Null
}
```

## 验证结果

- `tool/build_windows.ps1 -RealWorkflow`：通过
- 安装包生成：通过
- 本机静默升级安装：通过
- 安装目录包含 `ffmpeg.exe`、`ffprobe.exe`：通过
- 安装目录 `ffmpeg.exe -version`：通过
- 安装目录 `ffprobe.exe -version`：通过

## 待办清单（未完成）

- 在 `v0.1.100` 安装版 UI 中按用户原始操作路径人工复测一次视频解析/抽帧/缩略图
- 让用户侧复测他们最初报错的安装环境

## 下一步要做什么

下一步优先在 `v0.1.100` 安装版中按用户报错路径做一遍人工点击复测。如果仍有提示，再继续沿具体功能入口与日志定位，而不是重复修改打包层。
