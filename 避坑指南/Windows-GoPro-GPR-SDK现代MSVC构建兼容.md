# Windows 下构建 GoPro GPR SDK 的现代 MSVC 兼容处理

## 现象

官方 GoPro GPR SDK 固定提交 `446c736a38fb14f51343605c0780d347dc602f89` 在 VS 2022 17.14 / MSVC 19.44 下构建 `gpr_tools` 时会遇到两类旧代码兼容错误：

1. 内置 Expat 的 `xmltok.c` / `xmltok_impl.c` 使用 GCC 形式的 `__attribute((fallthrough));`，MSVC 将其解析为未知标识符。
2. `source/app/gpr_tools/main_c.c` 无条件包含 POSIX 头 `strings.h`，Windows SDK 不提供该头；MSVC 应使用 `_stricmp`。

仅通过 `CL` 环境变量传入函数式宏定义不可靠：经 `vcvars`、CMake 和 Ninja 多层转发后，定义可能未出现在实际编译命令中。

## 已验证处理

依赖准备脚本在校验源码包 SHA-256 后、构建前对外部解压目录应用两个精确补丁：

- `_MSC_VER` 下把 `__attribute(x)` 定义为空；
- MSVC 下不包含 `strings.h`，并把 `stricmp` 映射为 `_stricmp`。

构建时显式使用 `/MT`，最终 `gpr_tools.exe` 仅依赖 `KERNEL32.dll`，避免安装环境缺少 VC 运行库。源码包、提交、大小与 SHA-256 全部记录在 `cmake/raw-preview-dependencies.lock.json`，运行包同时携带 MIT、Apache 2.0 与汇总许可文件。

## 验证结果

- 官方 SDK 原型对 5 个 CC0 真实 GPR 样本全部成功输出非空 JPEG，单次约 31～54ms。
- 项目 worker 的 18 格式真实 RAW 矩阵为 18/18 非占位、长边不超过 480px、18/18 二次缓存命中。
- 不要改用来源不明的预编译 `gpr_tools.exe`，也不要只记录短提交号而不锁定完整提交和源码包哈希。
