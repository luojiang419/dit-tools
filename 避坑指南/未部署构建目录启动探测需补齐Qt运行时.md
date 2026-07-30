# 未部署构建目录启动探测需补齐 Qt 运行时

## 现象

直接对 `build/windows-msvc-release-real/CineVault.exe` 执行 QML 启动探测时，进程可能以
`-1073741515`（`0xC0000135`）退出；如果已有同路径进程正在运行，单实例机制还可能让探测等待错误的进程并最终超时。

## 根因

- CMake 构建目录不是正式部署目录，不保证 Qt DLL 已复制到可执行文件旁边；
- 普通 PowerShell 的 `PATH` 不一定包含所用 Qt kit 的 `bin`；
- 启动探测依赖新进程真正处理 `--qml-startup-probe`，已有同路径实例会干扰单实例判断；
- Windows PowerShell 5.1 不支持某些环境下的 `.Kill($true)` 重载，超时清理脚本可能继续报兼容性错误。

## 正确做法

优先对 `windeployqt` 后的正式部署目录运行探测。必须测试构建目录时：

```powershell
$env:Path = "C:\Qt\6.6.3\msvc2019_64\bin;$env:Path"
powershell -ExecutionPolicy Bypass `
  -File .\tool\test_cinevault_startup.ps1 `
  -ApplicationPath .\dit-tools-src\cinevault-pro\build\windows-msvc-release-real\CineVault.exe `
  -TimeoutSeconds 30
```

执行前先用只读方式检查是否已有 `CineVault` 进程。不要自动结束用户正在使用的实例；应改用正式部署探测或在明确可控的测试环境中执行。

## 本次验证

- 未补 Qt 运行时时返回 `0xC0000135`；
- 将当前 Qt kit 的 `bin` 放入 `PATH` 后，QML root 加载并正常以 0 退出；
- 本问题与抽帧业务代码无关。
