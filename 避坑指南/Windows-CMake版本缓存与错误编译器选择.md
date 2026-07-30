# Windows CMake 版本缓存与错误编译器选择

## 现象

- 根 `VERSION` 已递增，但增量构建在 CMake 重新生成阶段报错：
  `CINEVAULT_APP_VERSION (旧版本) must match VERSION (新版本)`。
- 直接执行 `cmake --preset ... --fresh` 后，可能进一步出现找不到 Qt，或缓存里的编译器变成 `clang++.exe`，继而在既有 MSVC 代码上出现无关的 Clang 兼容错误。

## 根因

- `CINEVAULT_APP_VERSION` 是构建目录里的旧 CMake cache 值，不是 `CMakeLists.txt` 中的独立版本源。
- 未加载项目 Windows 工具链时执行 `--fresh`，会丢失既有 Qt/MSVC 配置，并按当前 PATH 误选 LLVM Clang。

## 正确处理

从仓库根目录执行：

```powershell
. .\tool\windows_toolchain.ps1
$buildContext = Get-CineVaultBuildContext
$env:QT_ROOT = $buildContext.QtRoot
$version = (Get-Content .\VERSION -Raw).Trim()
Invoke-VcVarsCommand "cmake --preset windows-msvc-release-real -DCINEVAULT_APP_VERSION=$version"
Invoke-VcVarsCommand "cmake --build --preset windows-msvc-release-real --config Release --parallel 2"
```

只有确实需要重建缓存时才使用 `--fresh`，并且必须在 `Invoke-VcVarsCommand` 环境内显式保证 Qt 与 MSVC 可用。

## 验证

```powershell
Select-String `
  -Path .\dit-tools-src\cinevault-pro\build\windows-msvc-release-real\CMakeCache.txt `
  -Pattern '^CMAKE_(C|CXX)_COMPILER:FILEPATH=|^CINEVAULT_APP_VERSION:'
```

预期编译器为 Visual Studio Build Tools 下的 `cl.exe`，缓存版本与根 `VERSION` 一致。

## 结论

遇到版本不一致时，先更新目标构建目录的 cache 参数；不要把旧 cache 误判为源码版本错误，也不要在普通终端用 `--fresh` 破坏已验证的 MSVC/Qt 构建基线。
