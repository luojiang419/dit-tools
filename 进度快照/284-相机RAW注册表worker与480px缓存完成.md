# 284-相机 RAW 注册表、worker 与 480px 缓存完成

## 接力说明

本快照承接 `283-巨量扫描流式化与后处理分页完成.md`。模块 0、1、2 在上一轮已经完成，本轮未重做；本轮按既定顺序完成模块 2A-1～2A-5。

根 `VERSION` 仍为 `0.1.181`。尚未形成最终可发布安装包，严禁在下一轮提前递增版本。

## 本轮任务目标

1. 建立唯一 RAW 格式注册表及强签名识别。
2. 以固定依赖锁合法引入 LibRaw 0.22.2，并支持离线构建。
3. 用独立串行 worker 隔离原生解码器崩溃、超时和重启。
4. 完成 LibRaw → ExifTool → WIC → FFmpeg → RAW 占位图的 provider 链。
5. 输出最长边 480px、保持比例、不裁切、应用方向、sRGB、质量 85 JPEG，并使用可失效的版本化缓存键和原子写入。

## 已完成内容

### 模块 2A-1：RAW 格式注册表和路由

新增：

- `src/core/thumbnail/RawFormatRegistry.h/.cpp`
- `tests/unit/RawFormatRegistryTest.cpp`

注册表集中覆盖 42 个既定扩展名：

`3FR ARI ARW BAY BMQ CAP CINE CR2 CR3 CRW CS1 DC2 DCR DNG ERF FFF GPR IA IIQ KC2 KDC MDC MEF MOS MRW NEF NRW ORF PEF PTX PXN QTK RAF RAW RDC RW2 RWL SR2 SRF SRW STI X3F`

每个条目保存格式族、是否优先内嵌预览，以及统一 provider 顺序：

```text
LibRawEmbeddedPreview
→ LibRawRenderedImage
→ ExifToolEmbeddedPreview
→ WIC
→ FFmpeg
→ Placeholder
```

修改 `FileTypeService.h/.cpp`：

- 移除重复 RAW 扩展集合，改为复用 `RawFormatRegistry`。
- 新增可选文件头参数，但扫描器仍只按文件名调用，避免恢复逐文件打开。
- 强签名支持 RAF、X3F、CR2、DNG TIFF 标签 `0xc612`、CR3 ISO-BMFF `crx ` 品牌。
- 普通 TIFF/MP4 不会被误判为 RAW。

### 模块 2A-2：依赖锁、许可证和离线准备

新增：

- `cmake/raw-preview-dependencies.lock.json`
- `cmake/PrepareRawPreviewDependencies.cmake`
- CTest：`RawDependencyLockTest`

锁定官方 LibRaw 0.22.2 Windows x64 包：

```text
文件：LibRaw-0.22.2-Win64.zip
大小：3495702 bytes
SHA-256：AC64FA12BB00A7581332D4C6AB918C0533FB3F119D6B668D47A6875410DCA948
选定分发许可证：CDDL-1.0
```

依赖缓存位于源码树外：

`C:\Users\jiang\AppData\Local\CineVault\BuildCache\libraw-v1`

准备脚本校验 schema、profile、版本、大小、哈希、归档布局、许可证和 ready marker；支持离线模式。扩展相机厂商 SDK 默认关闭，若没有经过许可证锁批准则 CMake 明确拒绝启用。

修改 `CMakeLists.txt`、`CMakePresets.json`、`tool/build_windows.ps1`：

- 配置、链接、构建后复制及安装 `libraw.dll`。
- 随 worker 安装 `licenses/libraw`。
- 发布脚本检查 CDDL 许可证文件存在。

### 模块 2A-3：独立 RAW worker、协议、超时和重启

新增：

- `src/infrastructure/raw/RawPreviewProtocol.h/.cpp`
- `src/infrastructure/raw/RawWorkerClient.h/.cpp`
- `src/raw-worker/main.cpp`
- `tests/unit/RawWorkerClientTest.cpp`
- CMake 目标：`CineVaultRawWorker.exe`

协议为 4 字节大端长度前缀 + JSON，单帧上限 1 MiB，协议版本 1，严格匹配 `requestId`。

客户端拥有独立 I/O `QThread` 和 `QProcess`，调用串行化。默认 20 秒超时；超时或进程异常退出时终止并重启 worker。RAW 解码失败后通过 `providerStartIndex` 从下一 provider 继续，避免原生 provider 崩溃后盲目重放同一输入。

### 模块 2A-4：真实 provider 链与崩溃隔离

新增：

- `src/infrastructure/raw/RawPreviewDecoder.h/.cpp`

已落地 provider：

1. LibRaw 内嵌预览。
2. LibRaw 显影：相机白平衡、sRGB、half-size、8 bit。
3. ExifTool：依次尝试 `JpgFromRaw`、`PreviewImage`、`ThumbnailImage`。
4. Windows Imaging Component（WIC）。
5. FFmpeg `image2pipe`。
6. 带 RAW 扩展名提示的明确占位图。

修改 `ThumbnailEngine.h/.cpp`：

- 持有 `RawWorkerClient`，RAW 交给 worker，普通媒体继续走原 FFmpeg 路径。
- 扩展名直接查询注册表；仅 TIFF/通用容器候选在后处理阶段读取最多 64 KiB 文件头识别，不污染扫描流式路径。
- worker 可用时 RAW 预览能力独立于 FFmpeg 可用性。

### 模块 2A-5：480px JPEG 与版本化缓存

新增：

- `src/infrastructure/raw/RawPreviewCacheKey.h/.cpp`

输出契约：

- EXIF/LibRaw 方向已应用。
- 转换并标记 sRGB。
- `Qt::KeepAspectRatio`，最长边最多 480px，不裁切。
- RGB888 JPEG，质量 85。
- 写同目录 `<目标>.tmp`，用 `QImageReader` 二次校验，释放读写句柄后使用 Windows `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 原子替换。

缓存身份包含：

```text
源文件大小
源文件 UTC mtime（毫秒）
decoder package version = libraw-0.22.2
profile version = raw-preview-v1
maxEdge
```

上述字段规范化后计算 SHA-256；输出名为：

```text
<原缓存基名>.raw-<SHA256前24位>.jpg
```

同一身份的有效 JPEG 直接返回 provider=`cache`。源大小、mtime、解码包版本、profile 或最大边变化都会自然生成新路径，不覆盖旧身份缓存。

## 具体代码前后对比

### RAW 识别

修改前：

```cpp
static const QSet<QString> rawExtensions = { /* 分散维护 */ };
return rawExtensions.contains(extension);
```

修改后：

```cpp
const auto format = RawFormatRegistry::findByPath(filePath, header);
return format.has_value();
```

### 缩略图解码

修改前：

```cpp
return ffmpegAdapter.extractThumbnail(sourcePath, outputPath);
```

修改后：

```cpp
if (isRawSource(sourcePath)) {
    return rawWorkerClient_->decode({
        {"sourcePath", sourcePath},
        {"baseCachePath", outputPath},
        {"maxEdge", 480},
    });
}
return ffmpegAdapter.extractThumbnail(sourcePath, outputPath);
```

### 缓存失效

修改前：

```text
固定 outputPath；无法区分源文件被替换或解码参数升级。
```

修改后：

```cpp
identity.cacheKey = sha256(size, mtimeMs, decoderVersion, profileVersion, maxEdge);
identity.outputPath = baseName + ".raw-" + identity.cacheKey.left(24) + ".jpg";
```

## 已验证结果

构建目录：`dit-tools-src/cinevault-pro/build/windows-msvc-release-real`

成功编译：

- `CineVaultRawWorker.exe`
- `RawWorkerClientTest.exe`
- `MediaTaskServiceRecoveryTest.exe`
- `CineVault.exe`

通过测试：

```text
RawFormatRegistryTest             Passed
RawDependencyLockTest             Passed
RawWorkerClientTest               Passed（约 4.8 秒）
MediaTaskServiceRecoveryTest      Passed（约 0.5 秒）
100% tests passed, 0 failed
```

`RawWorkerClientTest` 已覆盖：

- 分片帧与超大帧拒绝。
- 真实 worker ping。
- 并发请求严格串行。
- 超时后 worker PID 变化。
- Unicode `.dng` 的 provider 回退和 480px、16:9、sRGB 输出。
- 损坏 `.NEF` 最终生成明确占位图。
- 缓存键长度、版本字段、源大小变化失效、二次命中 provider=`cache`。
- 原子落盘后不存在 `.tmp` 残留。

其他门禁：

- `git diff --check` 通过。
- 根版本保持 `0.1.181`。

## 本轮踩坑及文档

新增 `避坑指南/Windows-RAW-worker管道与原子写入.md`，记录：

1. Windows 匿名管道不要使用 `QFile(FILE *)`，应使用二进制 CRT I/O。
2. `MoveFileExW` 前必须销毁 `QImageWriter/QImageReader`，否则错误 32。
3. `QPainter` 绘制占位图的 worker 必须用 `QGuiApplication`，否则可能以 `0xC0000409` 退出。
4. 原生解码崩溃不能依赖 C++ catch，应通过进程隔离和 provider 起点续跑恢复。
5. 裸 PowerShell 构建前必须加载 MSVC 开发环境。

## 当前工作区保护

所有模块 0～2A 改动仍是未提交状态，下一轮必须完整保留，不得 reset、checkout、clean 或覆盖。主要状态：

- 模块 0～2 的原有修改仍在 `AppContext`、`MediaTaskService`、`MetadataExtractionService`、`ScanEngine` 及其测试中。
- 本轮新增/修改集中在 `RawFormatRegistry`、`FileTypeService`、`ThumbnailEngine`、`src/infrastructure/raw`、`src/raw-worker`、CMake 和构建脚本。
- 方案文档、压力工具、快照 282/283 及本快照均未删除。
- 构建目录保留用于增量编译，不是无用缓存；未生成新的巨量临时素材目录。

## 未完成待办

- [ ] 模块 4：目录刷新与任务进度聚合节流。
- [ ] 模块 5：素材库异步首屏 200 条与 Keyset 加载更多。
- [ ] 模块 3：统一资源协调、前台优先、暂停和背压。
- [ ] 模块 6：全局素材索引 generation + delta。
- [ ] 模块 7：搜索文档和语义索引消费 delta。
- [ ] 模块 8：变化监测脏路径、重叠检测、整卷排除。
- [ ] 模块 9：缩略图 LRU/20 GiB 上限、退出屏障、异常降级。
- [ ] 10 万/100 万、真实 RAW 样本矩阵、真实磁盘卷、全量 CTest、QML、Release 和安装包验收。
- [ ] 只有形成可发布安装包时才递增根 `VERSION`。

## 下一步必须从这里继续

从模块 4 开始，不要重做模块 0、1、2 或 2A：

1. 先审计目录变化/刷新入口、`MediaTaskService` 进度信号与 QML 消费点，明确当前每文件刷新和每文件进度更新链。
2. 把目录刷新聚合为固定时间窗或批次刷新；不要在每个文件完成时触发完整模型重载。
3. 把任务进度写库、日志和 UI 信号节流，保留阶段切换、完成、失败、取消等边界事件立即送达。
4. 增加高频进度输入测试：验证最终值不丢、边界事件不延迟、UI 信号/写库次数有明确上界。
5. 编译主程序并运行模块 0～2A 与模块 4 相关回归，然后自动进入模块 5。

下一轮首次只读取本快照，不要遍历旧快照；按模块逐个实现、验证并自动推进。若上下文再次接近 70%，创建下一份纯递增快照并自动新建接力任务。
