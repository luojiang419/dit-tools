# 289 - Release、安装包与最终验收完成

## 接力约束执行结果

- 本轮首次且只读取了 `进度快照/288-规模RAW真实卷与全量回归完成进入Release.md`，未读取更早快照。
- 未重跑已通过的 10 万/100 万规模、18 格式 RAW 矩阵、真实磁盘卷、全量 CTest/QML。
- 未使用 `reset`、`checkout`、`clean`；全部既有未提交和未跟踪改动保持原状。
- 根版本先保持 `0.1.181` 完成候选安装包与安装后验收，门禁通过后仅递增一次为 `0.1.182`，没有第二次递增。

## 已完成内容

### 1. `v0.1.181` 发布门禁

- 使用 `tool/build_windows.ps1 -Configuration Release -Version 0.1.181 -RealWorkflow` 完成正式 Release、Qt 部署、模型禁入检查、部署后 QML 启动烟测和 Inno Setup 构建。
- `verify_release_artifact.ps1` 通过：
  - 安装包：`G:\data\app\DIT-tools\output\v0.1.181\CineVault-Setup-v0.1.181.exe`
  - 大小：`210,614,732` 字节
  - SHA-256：`4ad76af0ffa74b7663659150798a992bdb2c40ee5d5c73fe848f899f7f7db4d1`
- `test_installer_startup.ps1` 通过：旧模型哨兵保留、安装后 QML 启动、真实卸载器清理均成功。
- 安装目录逐项确认包含 `CineVault.exe`、`CineVaultRawWorker.exe`、`libraw.dll`、`gpr/gpr_tools.exe`、LibRaw/GPR 许可、ExifTool、ONNX/BGE、本地搜索助手及 FFmpeg CLI，且没有 Qwen/GGUF 禁入资源。
- 使用安装目录 worker 解码真实 `gpr-GOPR8921.GPR`：`gopro_gpr_sdk`、非占位、JPEG 480×360、二次命中缓存。
- 证据目录：`G:\data\app\DIT-tools-validation\evidence\installed-gpr-v0.1.181-20260721T125201Z`。

### 2. 版本唯一递增

```diff
-0.1.181
+0.1.182
```

- 除根 `VERSION` 上述单行变更外，本轮没有修改产品源码或测试源码。

### 3. 最终 `v0.1.182` Release 与安装包

- `windows-msvc-release-real` 因应用版本变化完整重编译 127 个目标并成功链接。
- 正式构建日志确认：模型禁入检查通过、部署后 QML 启动烟测通过、Inno Setup 成功。
- 最终安装包：`G:\data\app\DIT-tools\output\v0.1.182\CineVault-Setup-v0.1.182.exe`
- 产品版本：`0.1.182`
- 大小：`210,664,018` 字节
- SHA-256：`029fa7867c4122510481100f85692ea06f60c577850fd5c5955caad7e0dd653f`
- 校验文件：`G:\data\app\DIT-tools\output\v0.1.182\CineVault-Setup-v0.1.182.exe.sha256`
- `test_release_version.ps1`、`verify_release_artifact.ps1`、`test_installer_startup.ps1` 全部通过。
- 最终安装目录再次逐项核验全部主程序、RAW、许可、ExifTool、搜索与 FFmpeg 负载，并确认模型禁入。

### 4. 最终安装后真实 GPR 验收

- 安装负载 worker SHA-256：`7af169d7bde11c4f3b0285ff6c3b160008dfe023bbc7939f93ff216dcd0c97b2`。
- 真实样本 SHA-256：`17a24d42735464525773048172c888ea24d13ba4b907e64580b63c3bab987ae5`。
- 首次解码：`gopro_gpr_sdk`，142ms，非占位 JPEG，480×360，30,705 字节，灰度范围 6～254。
- 二次解码：51ms，provider=`cache`，缓存键一致。
- 完整报告：`G:\data\app\DIT-tools-validation\evidence\installed-gpr-v0.1.182-20260721T125931Z\installed-gpr-preview-report.json`。
- 安装负载报告：同目录 `installed-payload-report.json`；安装日志：同目录 `installer.log`。

### 5. 清理与最终状态

- 两轮手工临时安装均使用安装包自带卸载器移除；本轮临时安装目录与临时安装日志不存在。
- `output/_installer_staging` 为空。
- 保留可复用依赖缓存与构建输出，三处主要缓存合计 `10,395,656,930` 字节（约 9.682GiB），低于 20GiB。
- 最终汇总：`G:\data\app\DIT-tools-validation\evidence\final-release-v0.1.182-summary.json`。
- 最终构建日志：`G:\data\app\DIT-tools-validation\evidence\release-v0.1.182-build.log`。
- 本机未配置签名证书哈希或证书选择器，最终安装包 Authenticode 状态为 `NotSigned`；本轮按项目未强制签名验证模式完成，其余验收全部通过。

## 待办清单

- [x] `v0.1.181` 正式 Release 与负载检查
- [x] `v0.1.181` 安装包产物、升级/启动、安装负载与真实 GPR 验收
- [x] 根版本只递增一次到 `0.1.182`
- [x] `v0.1.182` 最终 Release 与安装包重建
- [x] 最终产物、安装/升级/启动、负载与真实 GPR 完整复验
- [x] 临时缓存清理、缓存总量与 Git 状态复核

## 下一步

- 当前委托任务已全部完成，没有必须继续的开发项。
- 若正式对外分发要求 Authenticode，需在具备可信证书的发布机配置 `CINEVAULT_UPDATE_SIGNER_SHA256` 与 `CINEVAULT_SIGNING_CERT_SHA1`，以同一最终版本执行 `-RequireSigning` 后再做签名门禁；不得因此再次递增版本。
