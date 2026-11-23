# Windows RAW worker 管道与原子写入避坑

## 适用范围

CineVault 的 `CineVaultRawWorker.exe`、Qt 进程间二进制协议、RAW 占位图生成，以及 JPEG 缓存原子落盘。

## 1. 不要用 `QFile(FILE *)` 包装 Windows 匿名管道

- 症状：父进程和测试夹具都等待到超时，worker 没有返回协议帧。
- 原因：Windows 下由 `QProcess` 连接的标准输入输出与 `QFile(FILE *)` 的缓冲/句柄语义组合不可靠。
- 已验证做法：启动时用 `_setmode(_fileno(stdin/stdout), _O_BINARY)`，使用 `fread`、`fwrite`、`fflush` 读写长度前缀帧。

## 2. 原子替换前必须释放图片读写句柄

- 症状：临时 JPEG 已成功生成和校验，但 `MoveFileExW` 返回错误 32（共享冲突）。
- 原因：`QImageWriter` 或用于二次校验的 `QImageReader` 仍在作用域内，文件句柄尚未释放。
- 已验证做法：分别用独立作用域完成写入和校验，两个对象销毁后再调用 `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`。

## 3. 使用 `QPainter` 绘制占位图时必须初始化 GUI 应用

- 症状：损坏 RAW 走到占位图 provider 时，worker 以 `0xC0000409` 退出。
- 原因：带文本的 `QPainter` 依赖字体/GUI 初始化，只有 `QCoreApplication` 不够。
- 已验证做法：worker 使用 `QGuiApplication`；仍保持无窗口运行。

## 4. 原生解码器崩溃不能依赖 C++ 异常捕获

- 症状：某些畸形 RAW 可触发原生库访问异常，`catch (...)` 无法保证拦截。
- 已验证做法：把 LibRaw/WIC/FFmpeg 解码隔离到 worker 进程；父进程在超时或进程异常退出后重启，并通过 `providerStartIndex` 从下一 provider 继续，主进程不崩溃。

## 5. MSVC 命令行构建前要加载开发环境

- 症状：`cl.exe` 可以启动，但报找不到 `utility`、`type_traits` 等标准库头文件。
- 原因：普通 PowerShell 没有设置 MSVC/Windows SDK 的 INCLUDE、LIB 等环境变量。
- 已验证做法：先运行 VS 2022 的 `Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`，再执行 `cmake --build`。

## 回归检查

至少执行 `RawWorkerClientTest`：它覆盖真实 worker ping、并发串行化、超时重启、provider 回退、损坏 RAW 占位图、480px/sRGB JPEG、版本化缓存键与缓存命中。
