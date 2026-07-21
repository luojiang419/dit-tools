# Windows CTest 缺少 Qt 运行时会伪装成超时

## 现象

新增 Qt 单元测试后，目标可以正常编译和链接，但通过 CTest 运行时没有任何测试输出，并一直等到外层超时。直接从 PowerShell 启动可执行文件时，`$LASTEXITCODE` 为 `-1073741515`，即 Windows 状态码 `0xC0000135`。

## 根因

测试可执行文件依赖 `Qt6Core.dll`、`Qt6Test.dll` 等运行时，但测试没有加入项目的 `CINEVAULT_PATH_AWARE_TESTS` 清单。构建目录不复制这些 Qt DLL，当前终端的 `PATH` 也不一定包含 Qt `bin`，所以进程在进入 QtTest 之前就启动失败。通过 CTest 管道运行时，这种失败有时只表现为无输出超时。

## 处理

1. 用 `dumpbin /dependents <测试.exe>` 确认 Qt DLL 依赖。
2. 直接运行后检查 `$LASTEXITCODE`，确认是否为 `0xC0000135`。
3. 将测试目标加入 `CMakeLists.txt` 的 `CINEVAULT_PATH_AWARE_TESTS`，复用现有的：

```cmake
set_tests_properties(${CINEVAULT_PATH_AWARE_TESTS} PROPERTIES
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${CINEVAULT_TEST_QT_BIN_DIR}"
)
```

4. 重新运行 CMake 配置；只重新链接目标不足以更新 CTest 的环境属性。
5. 用 `ctest -N -V -R '^测试名$'` 检查测试环境，再执行带内部超时的单测。

## 避免复发

Windows 下每新增一个直接依赖 Qt 的 CTest 目标，都必须同步检查它是否进入 `CINEVAULT_PATH_AWARE_TESTS`。遇到“测试零输出并超时”时，先排查进程启动状态和 DLL 搜索路径，再按代码死锁调查。
