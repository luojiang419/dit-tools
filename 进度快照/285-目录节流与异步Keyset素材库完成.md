# 进度快照 285：目录节流与异步 Keyset 素材库完成

## 接力说明

- 当前任务：继续完成“巨量素材源防卡死与相机 RAW 480px 预览”。
- 本轮严格从快照 284 指定的模块 4 开始，没有读取或重做更早快照中的模块 0、1、2、2A。
- 已按顺序完成模块 4、模块 5；模块 3 已完成现状审计，尚未开始写实现代码。
- 当前上下文已到接力阈值，因此在此生成纯递增快照。新任务必须只读取本文件，从“下一步必须从这里继续”开始。
- 严禁 `git reset`、`git checkout`、`git clean`；当前工作区所有已跟踪和未跟踪改动均须原样保护。
- 根目录 `VERSION` 仍为 `0.1.181`。只有最终形成可发布安装包后才允许递增一次。

## 总体任务顺序与状态

1. 模块 4：写入、进度通知与目录刷新节流——已完成并回归通过。
2. 模块 5：素材库首屏异步 200 条与 Keyset Load More——已完成并回归通过。
3. 模块 3：全局并发预算与状态机——审计完成，实现尚未开始。
4. 模块 6：待开始。
5. 模块 7：待开始。
6. 模块 8：待开始。
7. 模块 9：待开始。
8. 最终验收：10 万/100 万、RAW 样本矩阵、真实磁盘卷、全量 CTest、QML、Release、安装包——待开始。

## 模块 4：已完成

### 目标

降低大量素材导入期间的数据库写放大、进度信号风暴和素材库整表刷新，保证完成/失败边界仍可立即观测。

### 修改文件

- `dit-tools-src/cinevault-pro/src/application/MediaTaskService.h`
- `dit-tools-src/cinevault-pro/src/application/MediaTaskService.cpp`
- `dit-tools-src/cinevault-pro/src/app/AppContext.cpp`
- `dit-tools-src/cinevault-pro/src/ui/viewmodels/LibraryWorkspaceViewModel.h`
- `dit-tools-src/cinevault-pro/src/ui/viewmodels/LibraryWorkspaceViewModel.cpp`
- `dit-tools-src/cinevault-pro/tests/unit/MediaTaskServiceRecoveryTest.cpp`
- `dit-tools-src/cinevault-pro/CMakeLists.txt`
- `避坑指南/Windows-CTest缺少Qt运行时会伪装成超时.md`

### 核心代码前后对比

#### 进度更新

修改前：后台循环可按高频进度直接写任务状态并发出 UI 更新，大素材源会造成大量数据库写和信号排队。

修改后：

- 第一次进度立即发布。
- 普通过程进度按 250–500ms 时间窗合并。
- 完成和失败边界前强制刷新最后一份进度上下文。
- 完成/失败信号仍立即排队，不被节流吞掉。

#### 目录变更通知

修改前：缩略图过程中每处理 6 个素材就触发一次目录刷新，容易把素材库查询和 QML 重建放大成主线程压力。

修改后：

- 删除“每 6 个素材刷新一次”的策略。
- 第一个可见结果可以立即发布，之后目录变化按 500ms 合并。
- 服务主线程用 `QTimer` 和待刷新项目数据库路径集合进行合并。
- 阶段结束时显式 flush，确保最终状态不会遗漏。

#### 素材库刷新入口

修改前：导入、媒体任务、元数据任务的目录变化可直接触发立即 reload。

修改后：统一连接到 `LibraryWorkspaceViewModel::scheduleReload()`，目录变化先经过 500ms 合并窗口；模块 5 又把实际 reload 改成异步分页查询。

### 测试增强

`MediaTaskServiceRecoveryTest` 增加高频分页场景：

- 生成 300 个素材并预填元数据。
- 用 SQLite trigger 统计真实 job 写入次数。
- 用 `QSignalSpy` 统计 jobsChanged 和目录通知次数。
- 验证队列峰值 128、最终进度/上下文为 300、数据库最终进度 100%。
- 验证数据库写、UI 通知和目录通知均受到上界控制。

### 模块 4 编译与回归

- `CineVault.exe` 编译成功。
- `MediaTaskServiceRecoveryTest` 编译并通过。
- 全局回归通过 8 项：
  - `MetadataExtractionServiceTest`
  - `PerformanceTelemetryTest`
  - `RawFormatRegistryTest`
  - `RawDependencyLockTest`
  - `RawWorkerClientTest`
  - `ImportServiceLegacyRescanTest`
  - `LargeCatalogStressTest`
  - `MediaTaskServiceRecoveryTest`
- 总耗时约 9.79 秒。

### 本模块踩坑

- `PerformanceTelemetryTest` 原本未纳入 Windows 路径感知测试清单，直接运行退出码 `0xC0000135`，CTest 表现成类似超时。已补进 `CINEVAULT_PATH_AWARE_TESTS`，并记录到 `避坑指南/Windows-CTest缺少Qt运行时会伪装成超时.md`。
- MSVC 编译必须通过现有 `tool/windows_toolchain.ps1` 的 `Invoke-VcVarsCommand` 进入开发环境；该问题已有 RAW worker 避坑文档，不重复创建同类文档。

## 模块 5：已完成

### 目标

素材库不得同步加载全表；首屏最多异步加载 200 条，后续采用稳定 Keyset 分页，避免 offset 在百万级目录上的退化和 UI 卡死。

### 修改文件

- `dit-tools-src/cinevault-pro/src/application/LibraryQueryService.h`
- `dit-tools-src/cinevault-pro/src/application/LibraryQueryService.cpp`
- `dit-tools-src/cinevault-pro/src/ui/models/AssetListModel.h`
- `dit-tools-src/cinevault-pro/src/ui/models/AssetListModel.cpp`
- `dit-tools-src/cinevault-pro/src/ui/viewmodels/LibraryWorkspaceViewModel.h`
- `dit-tools-src/cinevault-pro/src/ui/viewmodels/LibraryWorkspaceViewModel.cpp`
- `dit-tools-src/cinevault-pro/src/ui/qml/workspaces/LibraryWorkspace.qml`
- `dit-tools-src/cinevault-pro/src/app/AppContext.cpp`
- `dit-tools-src/cinevault-pro/tests/unit/LibraryWorkspacePaginationTest.cpp`
- `dit-tools-src/cinevault-pro/CMakeLists.txt`

### 核心代码前后对比

#### 查询接口

修改前：

- `fetchAssets(...)` 和 `assetCount(...)` 在调用线程执行。
- 素材库可一次读取整个结果集。
- 分页没有稳定的复合游标。

修改后：

- 新增 `LibraryAssetPageRequest`、`LibraryAssetPageResult`、`LibraryAssetCountResult`。
- 新增 `fetchAssetPageForPath(...)` 和 `assetCountForPath(...)`。
- 每次后台查询通过 `DatabaseManager::openThreadConnectionForPath` 建立唯一只读连接，执行 `PRAGMA query_only=ON`，并用作用域守卫关闭和移除连接。
- 页大小在 1–1000 之间限制，ViewModel 固定请求 200 条；SQL 实际取 `limit + 1` 判断 `hasMore`。
- 使用稳定复合 Keyset：`(asset_file.modified_at, asset_file.id)`；升序使用 `>`，降序使用 `<`，同时间戳时以 ID 保证无遗漏、无重复。
- 无关键字/类型/收藏过滤时，总数从 `source_root.total_files` 聚合；需要过滤时才异步执行 COUNT 和必要的嵌入元数据 join。

#### ViewModel 状态机

修改前：reload 会同步取全量资产并整体 `setItems`，主线程容易阻塞；搜索、计数、项目切换没有独立的异步代际保护。

修改后：

- 新增 `loading`、`hasMore`、`loadedAssetCount` 属性。
- 首屏和后续页通过 `QtConcurrent` 异步查询。
- 使用 query generation 丢弃项目、过滤或排序变化前的陈旧结果。
- 搜索防抖 250ms、总数防抖 300ms、目录 reload 合并 500ms。
- `loadMore()` 使用最后一项的 `modifiedAt + id` 作为游标。
- 对追加结果再做 ID 去重防御。
- 总数查询使用独立 generation，不允许旧 COUNT 覆盖当前查询。
- 析构和 AppContext 停止阶段等待已登记后台任务，防止悬挂访问。
- 键盘选择靠近当前页尾部时也会触发继续加载。

#### Model 更新

修改前：细小数据更新也可能通过 `setItems` 整体 reset。

修改后：

- 新增 `clear()`。
- 新增 `appendItems()`，使用 `beginInsertRows/endInsertRows`。
- 新增 `updateItemsById()`，只发出目标行 `dataChanged`。

#### QML

GridView 和 ListView 在接近底部时调用 `loadMore()`；QML 缓存编译通过。

### 新增自动化测试

`LibraryWorkspacePaginationTest` 覆盖：

- 首屏严格为 200 条。
- 200 + 200 + 55 最终加载 455 条，无重复、无遗漏。
- 所有记录使用相同 `modified_at`，验证降序时 ID 严格降序、切换升序后 ID 严格升序。
- reload 调用耗时小于 50ms，证明不在调用线程执行数据库全表查询。
- 普通过滤的总数为 455。
- 修改 source totals 后验证快速总数路径直接聚合到 1004。
- 大源 1200 条查询后立即切换到小源 5 条，验证旧 generation 结果不会污染当前页面。
- 单项更新只发 `dataChanged`，不发 `modelReset`。

### 模块 5 编译与回归

- `LibraryWorkspacePaginationTest` 通过，约 0.70 秒。
- `CineVault.exe` 和 QML 缓存编译成功。
- 全局回归通过 11 项：
  - `CineVaultSmokeTest`
  - `MetadataExtractionServiceTest`
  - `PerformanceTelemetryTest`
  - `RawFormatRegistryTest`
  - `RawDependencyLockTest`
  - `RawWorkerClientTest`
  - `ImportServiceLegacyRescanTest`
  - `LargeCatalogStressTest`
  - `MediaTaskServiceRecoveryTest`
  - `LibraryWorkspacePaginationTest`
  - `MaterialCenterUiContractTest`
- 总耗时约 10.50 秒。
- `git diff --check` 通过；输出只有既有 LF/CRLF 转换警告。

### 编译期间的局部修正

- MSVC 对 `qBound` 参数类型推断歧义：改为显式 `qsizetype{1}` 和 `qsizetype{1000}`。
- 新测试链接缺少 `FileRevealService`：将对应实现加入测试 target。

## 模块 3：已完成的审计结论

### 当前冲突路径

- 扫描完成后，`MediaTaskService` 的缩略图/FFprobe 阶段和 `MetadataExtractionService` 的 ExifTool 阶段通过直接信号连接同时启动。
- `MaterialCatalogSyncService` 也由项目、导入、媒体和元数据目录信号触发。
- 现有 `BackgroundMaintenanceCoordinator` 只控制“系统空闲 1 小时”后的维护性重扫和单个视频分析，不约束普通扫描、缩略图、FFprobe、ExifTool 和全局目录同步。
- 当前不存在共享的重 I/O token、全局 SQLite writer token、有限等待队列和项目代际取消机制。

### 已确定的最小实现方向

新增一个轻量 `IndexingWorkCoordinator`，只负责 token、前后台状态、有限等待和取消，不扩展成通用任务框架。

资源策略：

- 重 I/O 全局 1 token：扫描、缩略图、FFprobe、ExifTool 共用，避免同时争抢磁盘。
- 用户活跃时：允许扫描和前台可见缩略图；FFprobe、ExifTool、语义后台工作不派发新批次。
- 用户空闲时：FFprobe、ExifTool、后台缩略图可以依次获得 token。
- SQLite writer 单独全局 1 token，和重 I/O token 分离。
- 等待队列必须有上界，并优先前台请求。
- 项目 generation 变化和应用退出时取消旧等待者并唤醒所有阻塞线程。
- 服务只在阶段/批次边界 acquire，使用作用域守卫 release；不得把 token 持有范围扩散到 UI。
- 扫描结束后应先允许用户浏览首屏，再推进深层 FFprobe/ExifTool 工作。

### 仍需读取和核对的代码

上一轮在精确审计以下文件时到达接力阈值，读取输出未完整保留。新任务应只做有针对性的较小范围读取：

- `src/core/scan/ScanEngine.h/.cpp`：扫描线程入口、批次边界、取消点和数据库写作用域。
- `src/application/MediaTaskService.h/.cpp`：缩略图与 FFprobe 阶段边界、队列循环、写入点。
- `src/application/MetadataExtractionService.h/.cpp`：ExifTool 批次边界、取消点、写入点。
- `src/application/MaterialCatalogSyncService.h/.cpp`：全局同步的读写行为和触发合并方式。
- `src/application/SearchDocumentSyncService.h/.cpp`：目录同步后的写入链路。
- `src/application/BackgroundMaintenanceCoordinator.h/.cpp`、`src/infrastructure/monitoring/SystemIdleMonitor.h/.cpp`：复用用户活跃/空闲信号，避免再建一套输入监控。
- `src/app/AppContext.h/.cpp`：协调器所有权、依赖注入、project generation 和 shutdown 顺序。
- 相关 CMake 与测试 target 的现有组织方式。

## 下一步必须从这里继续

### 当前唯一进行中的模块：模块 3

1. 只读取本快照以及上面列出的模块 3 相关代码小片段，不读取任何更早进度快照。
2. 先把 `IndexingWorkCoordinator` 的最小 API、状态转换、token 种类、等待队列上界和取消语义写成简短实现计划与验收点。
3. 实现协调器及单元测试，至少验证：
   - 同时只能有一个重 I/O 阶段。
   - SQLite writer 同时只能有一个。
   - 前台等待者优先于后台等待者。
   - 用户活跃时后台 FFprobe/ExifTool 不派发，空闲后恢复。
   - 队列满时新后台请求快速失败或退避，不可无限堆积。
   - project generation 变化会取消旧等待者。
   - shutdown 会唤醒并取消所有等待者，不死锁。
4. 把协调器注入 ScanEngine、MediaTaskService、MetadataExtractionService，以及实际需要 writer 串行化的同步服务；严格在阶段/批次边界获取和释放。
5. 编译 `CineVault.exe` 和新增测试；运行模块 3 定向测试，再运行至少模块 0–5 已有回归集和 QML 缓存编译。
6. 模块 3 完成后自动推进模块 6，再按 7 → 8 → 9 顺序继续；不得提前跳到最终规模验收。

## 待办清单

- [ ] 模块 3：实现全局并发预算、活跃/空闲状态机、有限队列、优先级和取消。
- [ ] 模块 3：接入扫描、缩略图、FFprobe、ExifTool 和必要的 SQLite writer 路径。
- [ ] 模块 3：单元测试、编译、QML 与全局回归。
- [ ] 模块 6：按原方案逐项规划、实现和验收。
- [ ] 模块 7：按原方案逐项规划、实现和验收。
- [ ] 模块 8：按原方案逐项规划、实现和验收。
- [ ] 模块 9：按原方案逐项规划、实现和验收。
- [ ] 10 万素材源验收。
- [ ] 100 万素材源验收。
- [ ] RAW 样本矩阵验收。
- [ ] 真实磁盘卷验收。
- [ ] 全量 CTest、QML、Release 验收。
- [ ] 生成并验证可发布安装包。
- [ ] 仅在可发布安装包形成时递增根 `VERSION`。
- [ ] 清理本轮临时无用缓存，保留可复用构建缓存且总量不得超过 20GB。

## 当前工作区与构建约定

- 项目根：`G:\data\app\DIT-tools`
- 主项目：`G:\data\app\DIT-tools\dit-tools-src\cinevault-pro`
- Release 构建目录：`dit-tools-src/cinevault-pro/build/windows-msvc-release-real`
- Flutter：`D:\flutter`（当前 C++/Qt 模块未使用）
- 网络遇阻时代理：本机 IP 的 7890 端口。
- Windows/MSVC 构建继续复用：

  ```powershell
  . '..\..\tool\windows_toolchain.ps1'
  Invoke-VcVarsCommand 'cmake --build ...'
  ```

- 当前工作区包含模块 0–5 的大量未提交修改和未跟踪新文件；这不是可清理缓存，必须保留。
- 未执行提交、暂存、分支切换、重置、检出或 clean。

## 接力验收标准

- 新任务只从模块 3 继续，不重做模块 0、1、2、2A、4、5。
- 每完成一个模块，都要编译、做模块定向测试和全局回归，再自动进入下一模块。
- 代码修改保持最小、模块化，并与现有风格一致。
- 明显逻辑冲突必须给用户确认选项，不能黑盒决定。
- 新遇到且解决的编译/运行坑，应写入 `避坑指南`。
- 接近下一次 70% 上下文阈值时，生成下一份纯递增快照并自动创建新接力任务，不允许主动压缩上下文。
