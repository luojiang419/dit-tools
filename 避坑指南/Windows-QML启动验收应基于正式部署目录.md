# Windows QML 启动验收应基于正式部署目录

## 现象

直接对 CMake 裸构建目录中的 `CineVault.exe` 运行 `--qml-startup-probe` 时，日志可能已经记录：

```text
[qml-startup-probe] root-loaded
process_exit reason=event_loop_return code=0
```

但外层 `WaitForExit` 仍可能超时。此时容易误判为 QML 加载失败或产品安装包无法启动。

## 原因边界

裸构建目录不是最终发布运行环境：

- Qt DLL、QML 模块、插件和运行时资源仍依赖开发机搜索路径；
- `windeployqt` 尚未生成与用户安装环境一致的目录结构；
- 裸构建进程的退出表现不能替代发布载荷验收。

如果日志已经出现 `root-loaded`，说明 QML 根对象已成功创建；仍需在正式部署目录复测，才能判断是否为产品启动问题。

## 正确处理

1. 先运行静态检查：

```powershell
.\tool\check_qml_warnings.ps1 `
  -QmllintPath 'C:\Qt\6.6.3\msvc2019_64\bin\qmllint.exe' `
  -ModuleImportPath '.\dit-tools-src\cinevault-pro\build\windows-msvc-release-ffmpeg'
```

2. 使用正式构建脚本完成 `windeployqt` 和部署目录组装：

```powershell
.\tool\build_windows.ps1 -EnableFfmpeg
```

3. 以脚本内部对 `output/_installer_staging/<版本>/CineVault.exe` 的启动探测结果作为发布验收依据。

4. 只有正式部署目录也超时，或日志没有出现 `root-loaded` 时，才继续按 QML 导入、插件缺失、单实例锁和退出链路问题调查。

## 本次验证

2026-07-30 的 v0.1.188 构建中：

- 裸构建目录探测出现退出等待超时；
- 正式部署目录 QML 启动探测通过；
- Inno Setup 随后成功生成安装包；
- 因此没有把裸构建环境差异误当成抽帧修复回归。
