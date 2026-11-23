# 272 - 快捷搜索托盘恢复升级为 Windows 原生激活完成

## 本轮目标

根据 v0.1.167 实机反馈继续修复：主窗口隐藏到系统托盘后，全局快捷键可以打开快捷搜索，但双击结果卡片仍无法稳定把主窗口拉到前台。

## v0.1.167 未解决的原因

- v0.1.167 已调整为“先恢复主窗口、再隐藏搜索框、下一事件循环再次激活”。
- 但恢复动作仍只使用 Qt/QML 的 `showNormal/raise/requestActivate`。
- `requestActivate()` 只是跨平台激活请求；Windows 前台窗口保护可以直接忽略该请求，因此调用顺序正确仍不能保证真正置前。
- 本快照取代 271 中“仅靠 QML 激活已经完成”的结论，旧快照保留用于问题追踪，不回改。

## 已完成内容

### 1. QuickSearchController 新增原生恢复接口

- 新增 `Q_INVOKABLE bool restoreMainWindow(QObject *windowObject)`。
- 接收 QML 主窗口对象并转换为 `QWindow`，先执行 Qt 层显示恢复，再取得 Windows HWND。
- Windows 原生恢复链路包含：
  1. `ShowWindow(SW_RESTORE/SW_SHOW)`；
  2. 获取当前前台窗口线程；
  3. 必要时使用 `AttachThreadInput` 临时附加前台线程输入；
  4. `SetWindowPos(HWND_TOP)`；
  5. `BringWindowToTop`；
  6. `SetActiveWindow/SetFocus/SetForegroundWindow`；
  7. 如果系统仍未切换前台，使用 `HWND_TOPMOST -> HWND_NOTOPMOST` 瞬时提升 Z 序后再次激活；
  8. 立即解除线程输入附加，不让主窗口永久置顶。

### 2. QML 全面接入原生入口

- `Main.qml::restoreToForeground()` 优先调用 `quickSearchController.restoreMainWindow(root)`，仅在接口不可用时才使用旧 Qt 回退。
- `QuickSearchWindow.qml::activateMainWindow()` 直接调用控制器原生接口。
- 双击素材、帧和文件夹结果仍共用 `showMainWindow(true)`：
  - 原生恢复主窗口；
  - 关闭快捷搜索；
  - 下一事件循环再次执行原生恢复。
- 工程切换、素材管理中心切页、搜索条件恢复、滚动选中目标的业务链未改变。

### 3. 增加真实隐藏窗口恢复测试

- `QuickSearchControllerTest` 新增真实 `QWindow` 用例：
  - 创建 Windows 窗口；
  - 主动隐藏；
  - 调用原生控制器恢复；
  - 验证窗口重新可见且不处于最小化状态；
  - 验证 HWND 在 Windows 层可见。
- 同时验证空指针和非窗口 QObject 会被安全拒绝。
- `MaterialCenterUiContractTest` 增加 Win32 API 和 QML 接入契约，避免后续退回单纯 `requestActivate()`。

## 文件与模块清单

- `src/ui/window/QuickSearchController.h`
- `src/ui/window/QuickSearchController.cpp`
- `src/ui/qml/Main.qml`
- `src/ui/qml/components/QuickSearchWindow.qml`
- `tests/unit/QuickSearchControllerTest.cpp`
- `tests/unit/MaterialCenterUiContractTest.cpp`

## 具体修改前后对比

修改前：

```qml
mainWindow.showNormal()
mainWindow.raise()
mainWindow.requestActivate()
```

修改后：

```qml
if (controller
        && typeof controller.restoreMainWindow === "function"
        && controller.restoreMainWindow(mainWindow)) {
    return
}
```

原生层核心：

```cpp
ShowWindow(windowHandle, IsIconic(windowHandle) ? SW_RESTORE : SW_SHOW);
AttachThreadInput(currentThreadId, foregroundThreadId, TRUE);
BringWindowToTop(windowHandle);
SetForegroundWindow(windowHandle);
```

## 验证结果

- 新增隐藏原生窗口恢复测试：通过。
- `QuickSearchControllerTest`：通过。
- `MaterialCenterUiContractTest`：通过。
- `CineVaultSmokeTest`：通过。
- 全量 CTest：36/36 通过，0 失败，总耗时 16.68 秒。
- Release QML 全量缓存编译与主程序链接：通过。
- 完整应用快捷搜索运行探针：退出码 0。
- 打包后再次运行 `QuickSearchControllerTest`：通过。
- 安装暂存敏感数据扫描由打包脚本通过，未混入用户数据库或索引。

## 发布产物

- 安装包：`G:\data\app\DIT-tools\output\v0.1.168\CineVault-Setup-v0.1.168.exe`
- 大小：823,784,780 字节（约 785.62 MiB）。
- SHA-256：`C347E78A1C28D837121204F24508E3B502DF2135EE4025B84A443B340FF60E4A`。
- 产品版本：`0.1.168`。
- 当前未配置 Authenticode 数字签名证书。

## 缓存与清理

- Release 构建目录约 0.91 GiB。
- CineVault 可复用依赖缓存约 1.31 GiB。
- 合计约 2.21 GiB，低于 20 GiB 限制。
- 已删除空的 `output/_installer_staging`。
- 已删除本轮 Local/Roaming 下的 qttest 测试数据。
- 保留可复用构建、ExifTool 和模型缓存。

## 待办清单

- [x] 将主窗口恢复从 QML 请求升级到 Windows 原生激活。
- [x] 保留双击结果后的业务导航和目标选中链路。
- [x] 增加真实隐藏窗口恢复测试。
- [x] 完成定向测试、全量回归、QML 编译与运行探针。
- [x] 构建 v0.1.168 安装包并清理临时缓存。

## 下一步

安装 v0.1.168 后进行真实交互验收：主窗口关闭到托盘，使用全局快捷键打开搜索，双击素材/帧/文件夹结果；预期快捷搜索关闭，主窗口通过 Windows 原生激活恢复到最前方，并在素材管理中心选中目标。
