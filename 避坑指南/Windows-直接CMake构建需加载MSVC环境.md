# Windows：直接 CMake 构建前必须加载 MSVC 环境

## 现象

在普通 PowerShell 中直接运行 `cmake --build` 时，MSVC 可能报错：

```text
fatal error C1083: 无法打开包括文件: “type_traits”: No such file or directory
```

随后直接执行 `ctest` 仍可能运行上一次生成的旧测试二进制，造成“测试通过但本次代码未被编译”的假象。

## 原因

构建目录已配置为 MSVC，但当前 PowerShell 会话没有执行 `vcvars64.bat`，因此 `INCLUDE`、`LIB` 等标准库环境变量缺失。

## 处理方式

从仓库根目录加载项目提供的工具链包装器，再通过 `Invoke-VcVarsCommand` 执行构建：

```powershell
. .\tool\windows_toolchain.ps1
Invoke-VcVarsCommand 'cmake --build "G:\data\app\DIT-tools\dit-tools-src\cinevault-pro\build\windows-msvc-release-real" --target CineVault --config Release'
```

只有构建命令成功后，才可以将后续 `ctest` 结果视为本次修改的有效验证。
