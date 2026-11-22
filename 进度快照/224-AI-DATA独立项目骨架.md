# 224-AI-DATA独立项目骨架

## 已完成内容

- 已读取最新快照：`进度快照/223-发布v0.1.142到GitHub.md`。
- 已确认拆分边界：只迁移“素材备份”，保留 DIT-tools 的素材管理中心、素材解析和全局索引。
- 已创建阶段备份：`backup/v0.1.172`。
- 已创建独立项目：`G:\data\app\AI-DATA`。
- 已初始化 AI-DATA 独立 Git 仓库，默认分支为 `main`，尚未提交。
- 已迁入并独立化：
  - 备份计划与源文件枚举。
  - 多目的地直接/级联复制。
  - 大小、SHA-256、MD5 校验。
  - 磁盘卷安全弹出。
  - 备份服务、任务队列、列表模型和备份工作台。
  - 备份计划单元测试。
- 已解除 AI-DATA 对 `ProjectService`、`ImportService`、`AppSettings`、`JobEngine`、CineVault QML 模块和项目数据库的依赖。
- AI-DATA 队列改用独立 QSettings 命名空间保存。
- 备份日志品牌已改为 `AI-DATA`，日志文件名改为 `AIDataBackupLog.json`。

## 当前修改到哪个模块

- 当前模块：模块 1，AI-DATA 独立项目骨架与素材备份代码迁移。
- 模块 1 已完成。
- DIT-tools 业务源码尚未摘除素材备份，等待模块 2。

## 文件/模块清单

AI-DATA 新项目：

- `CMakeLists.txt`
- `CMakePresets.json`
- `README.md`
- `docs/拆分边界.md`
- `src/app/main.cpp`
- `src/domain/BackupTypes.h`
- `src/shared/Formatters.*`
- `src/core/backup/*`
- `src/application/BackupService.*`
- `src/ui/models/Backup*ListModel.*`
- `src/ui/viewmodels/BackupViewModel.*`
- `src/ui/qml/Main.qml`
- `src/ui/qml/workspaces/BackupWorkspace.qml`
- 必需的 QML 主题与基础控件
- `tests/unit/BackupPlannerTest.cpp`

DIT-tools 本轮新增记录：

- `backup/v0.1.172/AI-DATA独立项目模块1-代码对比.md`
- `进度快照/224-AI-DATA独立项目骨架.md`

## 具体修改的代码前后对比

详见：`backup/v0.1.172/AI-DATA独立项目模块1-代码对比.md`。

核心依赖变化：

```text
拆分前：
MaterialBackupViewModel
  -> ProjectService
  -> MaterialBackupService
  -> ImportService
  -> AppSettings
MaterialBackupService -> JobEngine

拆分后：
BackupViewModel -> BackupService -> BackupCopyEngine
BackupViewModel -> AI-DATA QSettings
```

## 验证记录

- `cmake --preset windows-msvc-release`：通过。
- `cmake --build --preset windows-msvc-release --config Release`：通过。
- 构建产物：`G:\data\app\AI-DATA\build\windows-msvc-release\AI-DATA.exe`。
- `ctest --test-dir build/windows-msvc-release -C Release --output-on-failure`：1/1 通过。
- 应用隐藏启动烟测：持续运行 3 秒后由验证脚本主动结束，未启动即崩溃。

## 待办清单（未完成）

- 模块 2：从 DIT-tools 移除素材备份 UI、主窗口路由、AppContext 装配、设置项和 CMake 清单。
- 模块 3：清理 DIT-tools 中备份专属领域实体和残留依赖，确认素材管理中心不受影响。
- 模块 4：分别执行 DIT-tools 全量构建/测试与 AI-DATA 回归测试。
- 模块 5：更新 DIT-tools 文档、安装包和最终拆分说明，清理临时缓存。
- AI-DATA 尚未创建首个 Git 提交。

## 下一步要做什么

进入模块 2。先枚举 DIT-tools 中素材备份的所有装配入口，再按“QML 路由 -> ViewModel/服务装配 -> CMake 源码与测试清单 -> 设置项”的顺序摘除；本模块不删除素材管理中心代码。
