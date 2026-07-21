# 288 - 规模、RAW、真实卷与全量回归完成，进入 Release

## 接力约束

- 工作区：`G:\data\app\DIT-tools`
- 本轮首次且只读取了 `进度快照/287-模块6至9完成进入最终验收.md`；后续接力只读取本文件，禁止读取更早快照。
- 模块 0～9 已完成，禁止重做。
- 保护全部未提交、未跟踪改动；禁止 `reset`、`checkout`、`clean`。
- 根 `VERSION` 当前仍必须保持 `0.1.181`。只有最终可发布安装包形成并完成验证时，才允许递增一次。
- 最终验收顺序已执行到“全量 CTest/QML”完成。下一步必须从“Release”开始，然后才是安装包。

## 本轮已完成

### 1. 10 万 / 100 万真实规模验证

- 10 万目录：100,000 文件，扫描成功；报告：
  - `G:\data\app\DIT-tools-validation\evidence\large-catalog-100k-scan.json`
  - 扫描约 95.849 秒；峰值工作集约 18.70MiB；队列峰值 256，结束为 0；SQLite busy=0。
- 100 万目录：1,000,000 文件，QtTest 默认 5 分钟和人为 1,400 秒两次仅因函数超时终止，临时库分别推进到 292,512 和 952,000 条，持续有进度，不是卡死。
- 使用 `QTEST_FUNCTION_TIMEOUT=3500000` 直接运行成功：
  - 扫描 2,149,381ms；峰值工作集约 22.18MiB；队列峰值 256；4,004 个枚举批次与 4,004 个写入批次；SQLite busy=0。
  - 主报告：`G:\data\app\DIT-tools-validation\evidence\large-catalog-1m-scan.json`
  - 进程报告：`large-catalog-1m-direct-run.json`、stdout/stderr、CSV 采样均在同一 evidence 目录。
- 结论：文件数放大 10 倍时峰值工作集约放大 1.19 倍，批次数约线性放大，内存与队列有界。

### 2. 真实 RAW 矩阵与 480px 预览

- 真实样本共 18 个格式：`3fr arw cr2 cr3 dng fff gpr iiq mos nef nrw orf pef raf rw2 rwl srw x3f`。
- 17 个样本来自 raw.pixls.us CC0 仓库并校验 SHA-256，CR3 使用本地只读真实样本。
- 原链路 17/18 能生成真实预览，唯一 GPR 落入占位；又验证 4 个 GPR 替代样本，结果相同。
- 官方 GoPro GPR SDK：
  - 仓库：`https://github.com/gopro/gpr`
  - 固定提交：`446c736a38fb14f51343605c0780d347dc602f89`
  - 许可：`MIT OR Apache-2.0`，选择 MIT，允许再分发。
  - 源码包大小 `25,283,787` 字节，SHA-256 `61ED13AB955C37CAD0AFD9B218281B241B00C541A945722F2E6979BDED905658`。
- 在工作区外完成原型：现代 MSVC 所需的最小兼容补丁是 Expat 的 `__attribute` 和 `main_c.c` 的 `strings.h/_stricmp`；5/5 真实 GPR 均在约 31～54ms 输出可读非空 JPEG。
- 项目内必要修复：
  - `cmake/raw-preview-dependencies.lock.json`：加入固定 GPR SDK、许可门和源码包哈希。
  - `cmake/PrepareRawPreviewDependencies.cmake`：哈希校验、外部解压、两个精确 MSVC 补丁、Ninja 构建 `/MT` 静态运行时、打包许可。
  - `CMakeLists.txt` / `CMakePresets.json` / `tool/build_windows.ps1`：构建目录与安装包携带 `gpr/gpr_tools.exe` 及 `licenses/gpr-sdk`。
  - `RawPreviewDecoder.cpp`：`.gpr` 优先调用官方工具，10 秒超时、临时目录、64MiB 输出上限，最终仍走统一 sRGB/JPEG/≤480px 归一化。
  - `RawWorkerClient.cpp`：GPR 专用 provider 的重启索引与解码器一致。
  - `RawPreviewCacheKey.cpp`：解码器身份从 `libraw-0.22.2` 变更为 `libraw-0.22.2+gpr-sdk-446c736`，避免旧占位缓存污染。
  - `RawFormatRegistry.*`：仅 GoPro 家族把 `GoProGprSdk` 放在候选链首位，其他 17 类顺序不变。
  - `RawWorkerClientTest.cpp`、`RawFormatRegistryTest.cpp`：新增 GPR mock provider、480×360、缓存命中及注册表顺序断言。
- 准备出的 `gpr_tools.exe` 大小 1,346,560 字节，`dumpbin /dependents` 仅依赖 `KERNEL32.dll`。
- 相关单测：`RawWorkerClientTest`、`RawFormatRegistryTest` 全部通过。
- 新鲜缓存重跑真实矩阵：18/18 非占位、JPEG 独立可读且非空、长边全部 ≤480、18/18 二次命中缓存。
  - 报告：`G:\data\app\DIT-tools-validation\raw-samples\raw-preview-matrix-report-gpr-sdk-446c736.json`
  - GPR provider=`gopro_gpr_sdk`，输出 480×360。

### 3. 真实磁盘卷验收及必要修复

- 本机产品代码实际枚举到 7 个就绪卷：
  - 本地固定 NTFS：`C: D: E: G:`
  - 可移动 exFAT 相机卡：`H:`，卷标 `EOS_DIGITAL`
  - SMB 网络盘：`X:`、`Y:`
- 首次 `H:` 整卷扫描仅 6ms 且 `asset_count=0`，与卷上 1,958 个文件矛盾。
- 诊断探针使用项目原始 `StorageVolumeService`、`ScanPathPolicy` 源码，确认卷枚举正确，但 `H:/DCIM`、`H:/MISC` 被误判为根外路径。
- 根因代码前后：

```cpp
// 修复前：卷根 H:/ 会拼成 H://
return candidate == root || candidate.startsWith(root + QLatin1Char('/'));

// 修复后：保留已有尾斜杠
const auto rootPrefix = root.endsWith(QLatin1Char('/'))
    ? root
    : root + QLatin1Char('/');
return candidate == root || candidate.startsWith(rootPrefix);
```

- `BackgroundMonitoringTest` 新增卷根子路径正向断言并通过。
- 修复后的 `H:` 整卷只读扫描：
  - 发现 1,955 个业务文件；PowerShell 的 1,958 包含受保护系统目录中的文件，产品正确排除该目录。
  - 2,009ms；峰值工作集 17,838,080 字节；队列峰值 256、结束为 0；SQLite busy=0。
  - 报告：`G:\data\app\DIT-tools-validation\evidence\real-volume-H-scan.json`
- 产品源码探针报告：
  - `real-volume-H-probe.json`：顶层 3 项，业务目录允许 2、系统目录排除 1。
  - `real-volume-X-probe.json`：顶层 5 项可访问。
  - `real-volume-Y-probe.json`：顶层 31 项可访问，识别 1 个重解析点。
  - 均位于 `G:\data\app\DIT-tools-validation\evidence`。
- 未对相机卡或网络盘写入任何文件；扫描数据库位于系统临时目录，证据报告位于外部 validation 目录。

### 4. 全量 CTest / QML

- `windows-msvc-release-real` 已完整构建。
- CTest：47/47 通过，0 失败，总耗时 42.31 秒；按 CI 相同策略单并发并排除 `gpu-integration` 标签。
- QML 首次为 735 warning、0 error，超过既有上限 731；新增 4 条全部来自分页监听中有意访问外层 `libraryRoot`。
- 对新增的两组监听增加精确的 `qmllint disable/enable unqualified` 作用域，不提高基线、不忽略其他类别。
- 最终 QML：33 文件，731 diagnostic，0 error；`unqualified=697`、`import=28`、`unused-imports=5`、`use-proper-function=1`；禁用类别计数为 0。

## 本轮避坑文档

- `避坑指南/Windows-QtTest巨量压力测试默认5分钟函数超时.md`
- `避坑指南/Windows-GoPro-GPR-SDK现代MSVC构建兼容.md`
- `避坑指南/Windows-磁盘卷根路径尾斜杠导致整卷扫描为空.md`

## 当前修改到的模块

- 最终验收中的“全量 CTest/QML”已完成。
- 当前尚未开始最终 Release 构建，也没有形成安装包。
- 根 `VERSION` 经再次确认仍为 `0.1.181`，禁止现在递增。

## 下一步必须从这里继续

严格按以下顺序，不得回头重跑已通过的 10万/100万、RAW、真实卷或全量回归，除非后续 Release/安装包失败所做修复直接影响相应链路：

1. **Release**
   - 以根 `VERSION=0.1.181` 执行正式 `windows-msvc-release-real` Release 构建/部署准备。
   - 验证 `CineVault.exe`、`CineVaultRawWorker.exe`、`libraw.dll`、`gpr/gpr_tools.exe`、`licenses/libraw`、`licenses/gpr-sdk`、ExifTool、本地搜索运行时等完整。
   - 执行现有启动烟测；失败只做必要修复并回归。
2. **安装包**
   - 在形成并验证可发布安装包前，`VERSION` 不得变化。
   - 安装包形成且通过 `verify_release_artifact.ps1`、`test_installer_startup.ps1`（以及项目现有升级/启动验证）后，按约定把版本只递增一次，再以新版本重新形成最终安装包并完整验证。不要产生两个未解释的“最终版本”。
   - 验证安装后目录确实包含 GPR 工具和许可，并用安装负载中的 worker 对真实 GPR 样本生成 ≤480px 非占位预览。
3. **最终清理与交付**
   - 只清理本轮明确创建的无用临时缓存；不得 `git clean`，不得触碰用户未提交/未跟踪改动。
   - 保留可复用依赖缓存；总缓存不得超过 20GiB。
   - 复核 `git status`、版本、安装包哈希与证据路径，更新计划并提交中文最终结果。

## 待办清单

- [x] 10 万规模验证
- [x] 100 万规模验证
- [x] 真实 RAW 样本矩阵与 480px 验证
- [x] GPR 唯一失败格式必要修复与回归
- [x] 真实固定/移动/网络卷枚举与移动卷整卷扫描
- [x] 卷根尾斜杠缺陷修复与回归
- [x] 全量 47 项 CTest
- [x] QML 731 警告基线 / 0 error
- [ ] 最终 Release 构建与负载检查
- [ ] 最终安装包、版本只递增一次、校验与安装启动验证
- [ ] 安装负载真实 GPR 解码验证
- [ ] 临时缓存清理、最终证据汇总与交付
