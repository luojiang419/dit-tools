# 进度快照 286：协调器完成与 Generation 同步主体完成

## 接力说明

- 当前总任务：继续完成“巨量素材源防卡死与相机 RAW 480px 预览”。
- 本轮首次只读取了 `进度快照/285-目录节流与异步Keyset素材库完成.md`，没有读取或重做更早快照中的模块 0、1、2、2A、4、5。
- 模块 3 已全部完成并通过主程序、QML 和全局回归。
- 模块 6 已完成全局库 schema v14、generation 全量同步主体、500 条 keyset 批次、失败代际恢复和定向测试；`catalog_change_log` 与正常路径 delta 尚未实现，因此模块 6 仍是当前唯一进行中的模块，不得误标完成。
- 当前上下文已到接力阈值；新任务必须首次只读取本文件，从“下一步必须从这里继续”开始，不得读取任何更早快照。
- 严禁 `git reset`、`git checkout`、`git clean`；当前工作区所有已跟踪和未跟踪改动均须保护。
- 根目录 `VERSION` 仍为 `0.1.181`。只有最终形成并验证可发布安装包后才允许递增一次。

## 总体模块状态

1. 模块 0、1、2、2A、4、5：更早轮次已完成，本轮未读取、未重做。
2. 模块 3：统一资源调度、优先级、背压和取消——已完成。
3. 模块 6：全局素材索引 generation/keyset 主体——已完成；change log/delta——待完成。
4. 模块 7：搜索文档与语义索引增量化——待开始。
5. 模块 8：增量变化监测与磁盘卷规则——待开始。
6. 模块 9：缓存配额、退出屏障与异常降级——待开始。
7. 最终验收：10 万/100 万、RAW 样本矩阵、真实磁盘卷、全量 CTest、QML、Release、安装包——待开始。

## 模块 3：已完成

### 新增文件

- `dit-tools-src/cinevault-pro/src/application/IndexingWorkCoordinator.h`
- `dit-tools-src/cinevault-pro/src/application/IndexingWorkCoordinator.cpp`
- `dit-tools-src/cinevault-pro/tests/unit/IndexingWorkCoordinatorTest.cpp`

### 修改文件

- `dit-tools-src/cinevault-pro/src/core/scan/ScanEngine.h/.cpp`
- `dit-tools-src/cinevault-pro/src/application/MediaTaskService.h/.cpp`
- `dit-tools-src/cinevault-pro/src/application/MetadataExtractionService.h/.cpp`
- `dit-tools-src/cinevault-pro/src/application/MaterialCatalogSyncService.h/.cpp`
- `dit-tools-src/cinevault-pro/src/application/SearchDocumentSyncService.h/.cpp`
- `dit-tools-src/cinevault-pro/src/app/AppContext.h/.cpp`
- `dit-tools-src/cinevault-pro/CMakeLists.txt`

### 状态机与 API

修改前：扫描、缩略图、FFprobe、ExifTool 和语义/全局同步分别启动，无共享重 I/O token、SQLite writer token、有界等待、前台优先、项目代际取消或 shutdown 唤醒。

修改后：

- `IndexingWorkCoordinator::acquire(Request)` 返回不可复制、可移动的 RAII `Lease`。
- 资源只有 `HeavyIo` 与 `SqliteWriter`，各自全局 1 token，彼此独立。
- 等待队列默认上限 64；资源内前台 FIFO 优先后台 FIFO。
- 队列满时新后台请求立即失败；新前台请求可替换一个尚未派发的后台等待者。
- `requiresIdle` 只门控后台重 I/O；用户活跃时扫描和前台缩略图仍可派发，FFprobe、ExifTool、语义同步等待系统空闲。
- `advanceGeneration()` 唤醒并取消旧项目等待者；`shutdown()` 唤醒并取消全部等待者。
- 扫描按目录获取重 I/O，缩略图/FFprobe 按 128 条页，ExifTool 按 32 条批次；writer 租约只包住实际事务或写语句。
- `MaterialCatalogSyncService` 在实际全局库事务获取 writer；`SearchDocumentSyncService` 的后台语义同步获取空闲重 I/O 和 writer。
- `SearchDocumentSyncService` 新增 future synchronizer 和 `waitForIdle()`，避免异步任务越过析构。
- `AppContext` 在各服务前创建协调器，最先连接 `ProjectService::projectChanged`，复用 `SystemIdleMonitor::becameIdle/activityResumed`；析构时先 shutdown、停止后台维护，再等待所有 future。

### 模块 3 测试

`IndexingWorkCoordinatorTest` 覆盖 8 类状态：

- 重 I/O 互斥。
- SQLite writer 互斥，且与重 I/O token 相互独立。
- 前台等待者先于后台。
- 活跃时 idle-required 工作不派发，空闲后恢复。
- 队列满时后台请求快速失败。
- 队列满时前台替换后台。
- generation 变化取消旧等待者。
- shutdown 唤醒并取消等待者。

### 模块 3 编译与回归

- `CineVault.exe` 编译成功，QML 缓存编译通过。
- 14 项回归全部通过，总耗时约 10.74 秒：
  - `CineVaultSmokeTest`
  - `IndexingWorkCoordinatorTest`
  - `MetadataExtractionServiceTest`
  - `PerformanceTelemetryTest`
  - `RawFormatRegistryTest`
  - `RawDependencyLockTest`
  - `RawWorkerClientTest`
  - `SearchDocumentSyncServiceTest`
  - `MaterialCatalogSyncServiceTest`
  - `ImportServiceLegacyRescanTest`
  - `LargeCatalogStressTest`
  - `MediaTaskServiceRecoveryTest`
  - `LibraryWorkspacePaginationTest`
  - `MaterialCenterUiContractTest`
- `git diff --check` 通过，仅有既有 LF/CRLF 转换提示。

### 模块 3 已解决的小问题

- `QFuture::result()` 不能复制不可复制 RAII 租约：测试线程改为在工作线程内持有/析构租约，只返回 `bool`。这是普通 C++ 所有权约束，没有新增项目级避坑文档。

## 模块 6：已完成的 generation/keyset 主体

### 当前修改文件

- `dit-tools-src/cinevault-pro/src/infrastructure/db/GlobalDatabaseManager.h`
- `dit-tools-src/cinevault-pro/src/infrastructure/db/GlobalDatabaseManager.cpp`
- `dit-tools-src/cinevault-pro/src/application/MaterialCatalogSyncService.cpp`
- `dit-tools-src/cinevault-pro/tests/unit/MaterialCatalogSyncServiceTest.cpp`
- `dit-tools-src/cinevault-pro/tests/unit/FolderDatabaseMigrationTest.cpp`

### 全局库 schema v14

修改前：全局库 `CurrentSchemaVersion = 13`，项目、全局素材、全局目录没有同步 generation。

修改后：

- `GlobalDatabaseManager::CurrentSchemaVersion = 14`。
- `project_registry.active_sync_generation INTEGER NOT NULL DEFAULT 0`。
- `global_video_asset.sync_generation INTEGER NOT NULL DEFAULT 0`。
- `global_folder_node.sync_generation INTEGER NOT NULL DEFAULT 0`。
- 新增索引：
  - `(project_uuid, sync_generation)` 素材索引。
  - `(project_uuid, sync_generation)` 目录索引。
  - `(project_uuid, absolute_path COLLATE NOCASE)` 路径重映射索引。
- v14 迁移可重复修复缺列；历史素材/目录回填 generation 1，存在历史行的项目 active generation 回填 1。
- `FolderDatabaseMigrationTest` 已验证三处新列与当前版本升级。

### 全量同步前后对比

修改前：

- `fetchProjectAssets()` 一次加载全项目素材到 `QVector`。
- `fetchProjectFolders()` 一次加载全项目目录到 `QVector`。
- 再把全局旧素材加载为多个整项目 `QHash/QSet`。
- 一个全项目长事务 upsert、比较和清理，峰值内存与素材总数线性增长。

修改后：

- `fetchProjectAssetPage()` 按 `asset_file.id > last_id ORDER BY id LIMIT 500` keyset 分页。
- `fetchProjectFolderPage()` 按 `folder_node.id > last_id ORDER BY id LIMIT 500` keyset 分页。
- 每页最多 500 条，每页独立 writer 租约与短事务。
- 不再存在整项目素材/目录/旧状态容器；旧状态按 `(project_uuid, video_key/absolute_path)` 索引逐项查询，内存由页大小决定。
- 素材 `ON CONFLICT` 在 SQL 内根据 `size_bytes/modified_at` 保留或重置 `analysis_status`、`confirmation_status`、`source_text` 和错误状态。
- 素材 ID 改变但路径不变时仍迁移 `video_analysis_result`、帧分析、计划、任务、维度分析和 FTS，已有兼容测试保持通过。
- 同步开始分配高于 active 和所有残留行的 generation，并把状态设为 `syncing`。
- 各页写入新 generation；完成阶段删除该项目 `sync_generation != current` 的素材/目录，清理 FTS 和孤儿 folder link，然后才切换 `active_sync_generation` 并设为 `ok`。
- 中途失败时已完成页可保留为恢复中间态，但 active generation 不切换，项目状态设为 `failed`；下一次成功同步选择更高 generation 并清理中间态。
- 项目离线仍保留旧行并标记不可用，既有行为未回归。
- 原整库同步实现已从源码删除，没有留下 `#if 0` 死代码。

### 模块 6 当前测试

- `MaterialCatalogSyncServiceTest` 新增 `generationSync_pagesLargeProjectsAndKeepsCompletedGenerationOnFailure`：
  - 合成 1205 条素材，跨越 3 个 500 条页。
  - 首次同步验证 1205 条全部落入同一 active generation。
  - SQLite trigger 在 `asset_id > 500` 时注入第二页失败。
  - 验证失败后 active generation 保持首次完成值，状态为 `failed`，且恰有首 500 条进入中间 generation。
  - 删除故障 trigger 后重试，验证 generation 继续递增、1205 条统一到新 generation、状态恢复 `ok`。
- 既有测试继续通过：空字段、真实拍摄时间、素材 ID 重映射分析迁移、目录改名/删除/孤儿清理、离线项目、旧帧状态迁移。
- 测试清理补充 `.pre-v13.bak`、`.pre-v14.bak`。

### 模块 6 当前编译/测试状态

- `CineVault.exe` 编译成功。
- `FolderDatabaseMigrationTest` 通过，约 0.22 秒。
- `MaterialCatalogSyncServiceTest` 通过，约 1.77～1.88 秒。
- 两项组合约 2.00 秒。
- 新分页代码曾命中快照 285 已记录的 MSVC `qBound` 类型歧义，已使用 `qsizetype{1}` / `qsizetype{1000}`，不重复写避坑文档。
- 曾有一次在项目根上层执行组合验收，工具链相对路径没有命中，但最后的 `git diff --check` 掩盖了前序错误；已在正确 `cinevault-pro` 工作目录用严格失败检查重跑成功，源码没有受影响。

## 下一步必须从这里继续

### 当前唯一进行中的模块：模块 6

1. 新任务首次只读取本快照，不读取快照 285 或任何更早快照。
2. 先重新编译 `CineVault`、`MaterialCatalogSyncServiceTest`、`FolderDatabaseMigrationTest`，确认共享工作区状态与本快照一致。
3. 在项目数据库新增 schema v8（当前 `DatabaseManager::CurrentSchemaVersion = 7`）的有界 `catalog_change_log` 或等价变更表：
   - 记录 asset/folder 的 added/updated/removed、entity id/key 和单调日志 id。
   - 同一实体高频更新需要 SQL coalesce，不在信号参数里传巨大 ID 列表。
   - 扫描发布、thumbnail、media metadata、embedded metadata 写入必须能留下 delta；首次巨量扫描不可产生无界内存容器。
   - 增加必要索引、可重复迁移和迁移测试。
4. 为 `MaterialCatalogSyncService` 增加正常路径 delta 消费：
   - active generation 为 0、迁移/溢出/显式 `rebuildAllProjects()` 才走现有全量 generation。
   - 正常 `syncCurrentProject/syncProject` 按 change log id keyset 每页最多 500 条，只读/写变化实体。
   - 删除项只删除对应 global asset/folder 与相关 FTS/分析级联。
   - 成功消费后按水位清理 change log；失败保留未消费日志。
   - 同一项目高频请求继续只保留一个最新 pending，不累计重复任务。
5. `MaterialCatalogSyncService` 应输出有界增量变更集或持久化 delta，供模块 7 的 `SearchDocumentSyncService` 使用；不要在本模块提前实现完整语义索引增量。
6. 增加测试：
   - 修改 1 个素材只更新该素材，不遍历/重写其余素材。
   - added/updated/removed 与 folder delta。
   - thumbnail 路径更新会产生素材目录 delta，但模块 7 必须保证不触发向量重建。
   - delta 第二页失败保留水位，重试不遗漏。
   - 显式 rebuild 强制全量 generation。
7. 模块 6 完成后编译主程序、QML，运行模块定向测试及模块 0–6 全局回归；通过后自动进入模块 7。

## 模块 7–9 已恢复的明确目标

### 模块 7：搜索文档与语义索引增量化

- 只消费 `MaterialCatalogSyncService` 的变化文档，不因每轮 thumbnail/metadata 执行全量收集。
- thumbnail 路径不进入语义内容哈希，不触发向量重建。
- metadata delta 合并 2～5 秒后提交。
- 文档收集 keyset 分页、有内存上限；保留旧索引可查询和锁竞争降级关键词。

### 模块 8：增量变化监测与磁盘卷规则

- `SourceChangeMonitor` 合并变化路径和父目录，只重扫脏目录；溢出才分片全量。
- 整卷排除回收站、System Volume Information、卷影、缓存、项目库/缩略图和系统临时目录。
- 添加源检测父子路径重叠并阻止重复索引；重解析点/符号链接/挂载点默认不跟随。

### 模块 9：缓存配额、退出屏障与异常降级

- thumbnail LRU：软上限为 10GiB 或项目盘剩余 5% 的较小值，硬上限不超过 20GiB。
- 只回收缓存，不删除索引数据库和用户产物。
- 退出/切项目停止派发、取消可取消任务、等小事务，超时保存恢复状态，不无限阻塞 UI。
- 单文件磁盘满、DB busy、断盘、路径消失、权限拒绝和外部工具崩溃不得终止整卷；增加进程级退出原因日志。

## 当前工作区与命令约定

- 项目根：`G:\data\app\DIT-tools`
- 主项目：`G:\data\app\DIT-tools\dit-tools-src\cinevault-pro`
- Release 构建目录：`dit-tools-src/cinevault-pro/build/windows-msvc-release-real`
- Flutter：`D:\flutter`，当前 C++/Qt 模块未使用。
- 网络遇阻时代理：本机 IP 的 7890 端口。
- MSVC 必须在 `cinevault-pro` 工作目录执行：

  ```powershell
  . '..\..\tool\windows_toolchain.ps1'
  Invoke-VcVarsCommand 'cmake --build build/windows-msvc-release-real --config Release --target ... --parallel 4'
  ```

- `ctest --test-dir build/windows-msvc-release-real -C Release ...` 同样必须从 `cinevault-pro` 目录执行。
- 当前工作区包含模块 0–6 的大量未提交和未跟踪源码；它们不是缓存，必须保留。
- 没有执行提交、暂存、分支切换、reset、checkout 或 clean。

## 剩余待办

- [ ] 模块 6：项目库 change log、delta 消费、变更集输出、测试、主程序/QML/全局回归。
- [ ] 模块 7：搜索文档和语义索引增量化、测试、全局回归。
- [ ] 模块 8：脏路径变化监测、整卷规则、路径重叠、分片溢出恢复、测试、全局回归。
- [ ] 模块 9：缓存配额、退出屏障、异常降级、退出日志、测试、全局回归。
- [ ] 10 万素材验收。
- [ ] 100 万素材验收。
- [ ] RAW 样本矩阵验收。
- [ ] 真实磁盘卷验收。
- [ ] 全量 CTest、QML、Release 验收。
- [ ] 生成并验证可发布安装包。
- [ ] 仅在安装包可发布时递增根 `VERSION`。
- [ ] 清理本轮临时无用缓存；保留可复用构建缓存且总量不得超过 20GiB。

