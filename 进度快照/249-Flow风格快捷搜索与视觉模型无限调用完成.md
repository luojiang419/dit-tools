# 249 - Flow 风格快捷搜索与视觉模型无限调用完成

## 当前状态

本轮功能开发、正式构建、全量测试与本地安装包生成均已完成。尚未提交 Git、推送分支或发布 GitHub Release；这些外部发布动作不在本轮授权范围内。

## 任务目标

1. 移除本地视觉模型的每日调用限制、扣减逻辑和次数显示。
2. 实现接近 Flow Launcher 使用体验的 Windows 全局快捷搜索窗口。
3. 复用影资管家现有自然语言、FTS5、BGE、结构化 SQL 与视觉模型搜索链路。
4. 在设置页提供快捷搜索启停、自定义全局快捷键和开机启动设置。
5. 更新可运行构建与本地安装包，并完成回归验证。

## 已完成模块

### 1. 本地视觉模型无限调用

- 删除 `AppSettings` 中每日额度、当日计数、可用性检查和请求扣减 API。
- 删除设置页的“每日搜索模型调用上限”和“今日已使用”显示。
- 删除 `MaterialCenterViewModel` 查询理解及候选帧复核前的预算门禁。
- 保留用户主动配置的功能开关、仅本地模式、图片发送授权、超时和模型配置检查。

关键代码前后对比：

```cpp
// 修改前
if (!m_settings.canUseSearchModel()) {
    return;
}
if (!m_settings.tryConsumeSearchModelCall()) {
    return;
}

// 修改后
// 不再进行额度检查或扣减；只依据功能开关、隐私授权和模型配置决定是否调用。
```

### 2. Windows 全局快捷键与托盘生命周期

- 新增 `QuickSearchController`，通过 Windows `RegisterHotKey` 注册默认 `Alt+Space`。
- 处理 `WM_HOTKEY` 并发出 `quickSearchRequested`，支持运行时更换、冲突提示与恢复。
- 快捷键必须包含修饰键，避免误把单个字母注册为系统级快捷键。
- 主窗口关闭后驻留系统托盘；托盘菜单支持“快捷搜索”“显示主窗口”“退出影资管家”。
- 增加开机启动注册表设置，默认关闭，以 `--background` 后台模式启动。

关键代码前后对比：

```cpp
// 修改前
// 工程没有全局热键注册、原生事件过滤器或托盘入口。

// 修改后
RegisterHotKey(nullptr, kQuickSearchHotkeyId, nativeModifiers, virtualKey);
// nativeEventFilter 中识别 WM_HOTKEY 后发出 quickSearchRequested()。
```

### 3. Flow 风格独立快捷搜索窗

- 新增 `QuickSearchWindow.qml`：无边框、置顶、居中显示，尺寸为 820 x 650。
- 顶部为自然语言输入框，结果按文件夹、画面帧和素材分区显示。
- 结果展示缩略图、名称、路径与命中摘要，并复用 `MaterialCenterViewModel` 的完整搜索链路。
- 键盘交互：`↑/↓`、`Tab/Shift+Tab` 切换，`Enter` 打开，`Ctrl+Enter` 定位目录，`Ctrl+R` 刷新，`Ctrl+I` 打开设置，`Esc` 收起。
- 输入防抖为 80 ms；搜索模型原有 250 ms 防抖保留，避免高频重复查询。
- 窗口失焦后延迟 350 ms 收起，避免菜单或窗口切换时误关闭。

### 4. 设置页面与主窗口入口

- 设置页新增“快捷搜索”卡片：启停开关、只读按键录制框、恢复 `Alt+Space`、注册状态、开机启动开关。
- `SettingsViewModel` 保存时立即应用快捷键并返回冲突/失败信息，成功后再持久化。
- 主窗口增加应用级 `Ctrl+K`，用于聚焦顶部搜索框。
- `ShellViewModel::enterProjectFromLibrary` 暴露为 `Q_INVOKABLE`，快捷搜索结果可直接进入项目和素材中心。

### 5. 测试与诊断

- `AppSettingsBudgetTest` 已替换为 `AppSettingsSearchTest`，覆盖无限调用后的设置结构及快捷搜索持久化。
- 新增 `QuickSearchControllerTest`，真实注册 `Alt+Space`，投递 `WM_HOTKEY` 并确认信号触发。
- 更新 `MaterialCenterUiContractTest`，覆盖预算 API 移除、设置参数、托盘、全局热键和快捷搜索键盘链路。
- 新增 `--quick-search-probe` 内部运行探针，确认 QML 窗口在真实应用启动后能够创建并显示。

验证结果：

- Windows MSVC Release 正式工作流构建成功。
- 精简 GUI 构建成功。
- CTest：28/28 通过，0 失败。
- 快捷搜索探针：`windowFound=1 visible=1 title=影资管家快捷搜索`，退出码 0。
- Windows 可视化捕获接口返回 `0x80004002`，因此没有继续进行盲目鼠标输入；改用应用内部窗口探针、QML 日志和真实全局热键测试完成验证。

## 文件/模块清单

新增：

- `src/ui/window/QuickSearchController.h`
- `src/ui/window/QuickSearchController.cpp`
- `src/ui/qml/components/QuickSearchWindow.qml`
- `tests/unit/AppSettingsSearchTest.cpp`
- `tests/unit/QuickSearchControllerTest.cpp`

主要修改：

- `CMakeLists.txt`
- `src/app/AppBootstrap.cpp`
- `src/app/AppContext.h/.cpp`
- `src/app/main.cpp`
- `src/infrastructure/config/AppSettings.h/.cpp`
- `src/ui/qml/Main.qml`
- `src/ui/qml/components/SettingsDialog.qml`
- `src/ui/qml/components/TopCommandBar.qml`
- `src/ui/viewmodels/MaterialCenterViewModel.cpp`
- `src/ui/viewmodels/SettingsViewModel.h/.cpp`
- `src/ui/viewmodels/ShellViewModel.h`
- `tests/unit/MaterialCenterUiContractTest.cpp`
- `docs/视觉模型辅助模糊搜索方案.md`

删除：

- `tests/unit/AppSettingsBudgetTest.cpp`

## 构建产物

- 版本：`v0.1.151`
- 安装包：`G:\data\app\DIT-tools\output\v0.1.151\CineVault-Setup-v0.1.151.exe`
- 大小：187,140,883 字节。
- SHA-256：`DABFD175F3F8E356059B4780A10CE6878B1CF382354E3579B146C5B93B1CE643`。
- 正式 EXE：`G:\data\app\DIT-tools\dit-tools-src\cinevault-pro\build\windows-msvc-release-real\CineVault.exe`
- 正式 EXE SHA-256：`7C2A78F8C9DEA9FD0F9755FBB128097295A84E0A41B30575C69B74497835588A`。

## 待办清单

- [x] 移除视觉模型调用次数限制与显示。
- [x] 实现独立快捷搜索窗口。
- [x] 实现默认 `Alt+Space` 全局热键与自定义设置。
- [x] 复用自然语言模糊搜索链路。
- [x] 实现托盘驻留与可选开机启动。
- [x] 完成正式/精简构建和 28 项测试。
- [x] 生成 `v0.1.151` 本地安装包。
- [ ] 用户安装本地验收包并使用真实素材库体验结果排序。
- [ ] 如用户确认，再执行 Git 提交、推送与 GitHub Release 发布。

## 下一步

安装 `v0.1.151` 后，在“设置 → 快捷搜索”确认快捷搜索已开启且状态为已注册。默认按 `Alt+Space` 拉起；若被其他程序占用，可直接在按键录制框中设置新的组合键并保存。

## 工作区保护说明

根目录 `.gitignore`、网站目录、历史快照和压缩包等既有未提交内容均视为用户改动，本轮未覆盖、未删除，也未执行 Git 暂存、提交、推送或重置。
