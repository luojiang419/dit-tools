# MSVC 标准头缺失与构建程序占用

日期：2026-07-31

## 现象

1. 直接运行 CMake 构建时，MSVC 报错找不到标准头 `type_traits`。
2. 源码编译完成后，链接阶段报 `LNK1104: 无法打开文件 CineVault.exe`。

## 原因

1. 当前 PowerShell 会话没有加载 Visual Studio C++ 工具链和 Windows SDK 的 `INCLUDE/LIB/PATH` 环境。
2. `build/windows-msvc-release-real/CineVault.exe` 仍被该构建目录启动的旧进程占用。

## 解决方式

使用仓库工具链中同一套 Visual Studio Build Tools 环境运行构建：

```powershell
cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build --preset windows-msvc-release-real --target CineVault'
```

如果只在链接阶段失败，先确认进程路径：

```powershell
Get-Process CineVault -ErrorAction SilentlyContinue | Select-Object Id,Path
```

仅结束明确指向当前构建目录的旧实例后再重试构建，不要批量结束其他版本或安装版进程。

## 后续规避

- 运行 MSVC CMake 构建前先加载 `vcvars64.bat`。
- 链接失败时先检查构建目录下的 EXE 是否被运行实例锁定。
- 构建完成后再启动新的构建目录实例，避免下一次增量链接占用文件。
