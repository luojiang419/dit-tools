# 104-GitHub更新服务与下载

日期：2026-06-27

## 已完成内容

- 已在本阶段开始前执行本地备份：`backup/v0.1.87`。
- 已新增轻量更新服务 `UpdateService`，职责只包含版本读取、GitHub `latest release` 查询、版本比较、安装包选择、下载缓存、待安装状态记录与安装脚本触发入口。
- 已将更新状态持久化到 `AppSettings`，新增待安装版本、待安装路径、已下载版本三个字段。
- 已在 `Paths` 中新增更新缓存目录 `cache/updates`，并确保基础目录初始化时自动创建。
- 已把应用运行时版本统一接入构建链路：`tool/build_windows.ps1` 注入版本号，`main.cpp` 设置 `applicationVersion`，更新比较统一按 `vX.Y.Z` 处理。
- 已新增更新相关单测，覆盖版本比较、Release JSON 解析、安装包资产匹配与无 Release 中文状态映射。

## 当前修改到哪个模块

当前完成到模块 3：GitHub 更新基础设施。

修改文件：
- `dit-tools-src/cinevault-pro/src/application/UpdateService.h`
- `dit-tools-src/cinevault-pro/src/application/UpdateService.cpp`
- `dit-tools-src/cinevault-pro/src/infrastructure/config/AppSettings.h`
- `dit-tools-src/cinevault-pro/src/infrastructure/config/AppSettings.cpp`
- `dit-tools-src/cinevault-pro/src/shared/Paths.h`
- `dit-tools-src/cinevault-pro/src/shared/Paths.cpp`
- `dit-tools-src/cinevault-pro/CMakeLists.txt`
- `dit-tools-src/cinevault-pro/src/app/main.cpp`
- `tool/build_windows.ps1`
- `dit-tools-src/cinevault-pro/tests/unit/UpdateServiceTest.cpp`

## 具体修改的代码前后对比

### 更新服务入口

修改前：

```cpp
// 项目中没有独立的 GitHub 更新服务。
```

修改后：

```cpp
class UpdateService : public QObject {
    Q_OBJECT

public:
    static QString normalizeVersionTag(const QString &versionTag);
    static int compareVersionTags(const QString &left, const QString &right);
    static QString expectedInstallerName(const QString &versionTag);
    static bool parseLatestRelease(const QByteArray &payload, UpdateReleaseInfo *info, QString *errorMessage);
    static QString latestReleaseStatusMessage(int statusCode, const QString &networkErrorString);

    void beginStartupFlow();
    void checkForUpdates(bool manual);
    bool installPendingUpdateNow(QString *errorMessage);
};
```

### AppSettings 更新状态持久化

修改前：

```cpp
// 设置里没有待安装更新的持久化字段。
```

修改后：

```cpp
QString AppSettings::pendingUpdateVersion() const
{
    return value(QStringLiteral("updates/pendingVersion")).toString().trimmed();
}

void AppSettings::clearPendingUpdate()
{
    remove(QStringLiteral("updates/pendingVersion"));
    remove(QStringLiteral("updates/pendingInstallerPath"));
}
```

### 构建版本注入

修改前：

```powershell
Invoke-VcVarsCommand "cmake --preset $configurePreset"
```

修改后：

```powershell
$version = Get-NextDistVersion -DistRoot $outputRoot -ReferenceRoots @((Join-Path $context.RepoRoot "dist"))
$appVersion = $version.TrimStart("v")
Invoke-VcVarsCommand "cmake --preset $configurePreset -DCINEVAULT_APP_VERSION=$appVersion"
```

## 验证结果

- `UpdateServiceTest` 已覆盖：
  - 版本比较
  - Release JSON 解析
  - 安装包资产匹配
  - 无 Release 中文状态映射
- GitHub 最新发布检查地址固定为：
  - `https://api.github.com/repos/luojiang419/dit-tools/releases/latest`
- 安装包命名规则固定为：
  - `CineVault-Setup-vX.Y.Z.exe`

## 待办清单（未完成）

- 将更新服务接入 `AppContext`、`SettingsViewModel` 和设置弹窗入口。
- 在主界面加载完成后异步触发启动自动检查。
- 实现下载完成确认框、立即更新、下次启动更新三段闭环。
- 完成最终构建、Release 发布与完整回归记录。

## 下一步要做什么

进入模块 4，将更新基础设施接入 UI 与启动链路：
- 启动后自动检查并后台下载。
- 设置弹窗顶部增加“检查更新”按钮。
- 下载完成后统一弹出“立即更新 / 下次启动更新”确认框。

