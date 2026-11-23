# 2026-09-05 架构审计复现实验

仅操作新建的临时合成素材/数据库，不连接视觉API、不写用户素材库，不修改产品源码。

## 工具范围

- `ArchitectureAudit`：直接编译当前 `FFmpegAdapter.cpp`，观测静态视频、A→B→A、极短视频、请求尺寸和Qt/SQLite同步写锁等待。
- `CurrentFrameExtractionTest`：重编译当前产品的抽帧单元测试，证明测试对应当前源码，避免旧exe假通过。
- `CurrentSourceMonitorProbe`：编译当前 `SourceChangeMonitor.cpp`，通过已有 `CINEVAULT_TESTING` 接口注入盘符根/普通目录路径，不启动系统目录监听。
- `sql_scale_probe.py`：从当前 `ScanEngine.cpp` 提取局部目录统计SQL，以临时简化表与当前相关索引测试规模增长。
- `collect_evidence.py`：记录源码、诊断文件和产物SHA-256，不收集环境变量值、凭据或用户数据库内容。

诊断程序退出0表示实验完成并成功写报告，**不代表产品功能正确或Bug已修复**。具体观察值保存在JSON中；CurrentFrameExtractionTest的QtTest结果另行判读。

## 基线要求

审计基于 `8114b71` 加本轮开始时已有的未提交修改。只检出审计分支不足以恢复同一产品源码，请先核对 `docs/架构审计证据清单-2026-09-05.json` 的源码指纹。审计分支仅提交诊断与文档，未纳入用户已有业务改动。

清单同时记录原始字节SHA-256和文本去BOM/统一LF后的SHA-256；Git自动转换CRLF时用后者判断源码内容是否一致。二进制仅比较原始字节指纹。

本机已验证：Windows、MSVC19.44、Qt6.6.3、FFmpeg8.0.1、Python3.12.7；Python SQLite为3.45.3。Python SQL结果不应伪称是Qt插件同版本的性能结果。

## 本机复现

在 `G:\app\DIT-tools` 执行以下PowerShell命令。其它机器应先调整Qt、Ninja和FFmpeg路径，不要沿用失效的旧CMake缓存。构建输出留在已忽略的项目build目录。

```powershell
. ./tool/windows_toolchain.ps1
Invoke-VcVarsCommand 'cmake -S "G:\app\DIT-tools\tool\architecture_audit_20260905" -B "G:\app\DIT-tools\dit-tools-src\cinevault-pro\build\architecture-audit-20260905" -G Ninja "-DCMAKE_MAKE_PROGRAM=C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DCMAKE_PREFIX_PATH=C:/Qt/6.6.3/msvc2019_64 -DCMAKE_BUILD_TYPE=Release'
Invoke-VcVarsCommand 'cmake --build "G:\app\DIT-tools\dit-tools-src\cinevault-pro\build\architecture-audit-20260905" --parallel 2'

$env:PATH='C:\Qt\6.6.3\msvc2019_64\bin;' + $env:PATH
$env:QT_PLUGIN_PATH='C:\Qt\6.6.3\msvc2019_64\plugins'
$env:CINEVAULT_FFMPEG_PATH='C:\Program Files\影资管家\ffmpeg\bin\ffmpeg.exe'
$env:CINEVAULT_FFPROBE_PATH='C:\Program Files\影资管家\ffmpeg\bin\ffprobe.exe'

# 重新运行使用独立结果文件，避免覆盖本次交付的历史证据。
& ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/ArchitectureAudit.exe ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/frame-rerun.json
if ($LASTEXITCODE -ne 0) { throw '抽帧诊断执行失败' }
& ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/CurrentFrameExtractionTest.exe
if ($LASTEXITCODE -ne 0) { throw '当前抽帧单测失败' }
& ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/CurrentSourceMonitorProbe.exe ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/monitor-rerun.json
if ($LASTEXITCODE -ne 0) { throw '监听诊断执行失败' }
python ./tool/architecture_audit_20260905/sql_scale_probe.py --output ./dit-tools-src/cinevault-pro/build/architecture-audit-20260905/sql-rerun.json
if ($LASTEXITCODE -ne 0) { throw 'SQL诊断执行失败' }
```

## 观察与解释

- 静态7.3秒视频末PTS=7200ms，当前SceneAndInterval候选至6000ms，筛后仅0ms。
- A→B→A使用明确按帧序N生成的两个图案，最低清晰度设0以隔离去重；当前保留0ms与1000ms，丢失2秒后的返回事件。
- 极短视频间隔大于时长时，原始fps过滤器没有输出帧；本机适配器另有MJPEG初始化报错。不要把这一运行时错误推广为所有短视频/版本的固定表现。
- 64×64约束被当前抽帧实现忽略，实际256×128。
- 写锁实验刻意让一个连接持有WAL写事务，另一个连接在事件线程写入，展示busy_timeout如何阻止心跳；不是用户卡死现场调用栈。
- SQL实验只复现局部扫描的目录统计，包含EXPLAIN QUERY PLAN和计数校验。GROUP BY对照是平面数据的单次聚合，不是完整目录树重构。
- 当前抽帧单测虽然通过，但没有验证上述覆盖与尺寸契约。

## 已知边界

未运行完整GUI、真实模型、百万磁盘文件或网络盘故障注入。合成输入和SQLite页缓存不能代表真实素材目录的硬件吞吐。不要把一次时延结果当作稳态P95/P99；正式性能门禁需重复测量和固定参考机。
