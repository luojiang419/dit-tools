# 127-github发布v0.1.96

日期：2026-06-30

## 已完成内容

- 已读取上一轮最新快照：`126-修复素材管理中心迁移顺序.md`。
- 已创建新的发布阶段备份：`backup/v0.1.108`。
- 已将项目默认版本与安装器默认版本升级到 `v0.1.96`。
- 已执行真实工作流打包：
  - `powershell -ExecutionPolicy Bypass -File tool/build_windows.ps1 -RealWorkflow`
- 已完成构建和测试验证：
  - `ctest --test-dir G:/data/app/DIT-tools/dit-tools-src/cinevault-pro/build/windows-msvc-release-real -C Release --output-on-failure`
  - `6/6` 通过
- 已生成安装包：
  - `CineVault-Setup-v0.1.96.exe`
  - 大小：`57,816,688` 字节
  - SHA256：`CAD47FF83AFB2241D3F45B7CBC926B2469F85129AB31751DA99376A959F0D730`

## 当前修改到哪个模块

当前完成模块：修复版安装包构建模块。

本轮修复版已经具备发布条件，下一步只剩 Git 提交、推送、打 tag 与 GitHub Release 上传。

## 具体修改的代码前后对比

### 发布前

```text
- 迁移顺序修复已完成，但用户还拿不到新安装包。
- GitHub latest release 仍停留在 v0.1.95。
```

### 发布后（待执行）

```text
- 将推送 main 到 GitHub。
- 将创建 v0.1.96 tag。
- 将上传 CineVault-Setup-v0.1.96.exe 到公开 Release。
```

## 验证结果

- `tool/build_windows.ps1 -RealWorkflow`：通过
- `ctest ... -C Release --output-on-failure`：通过
- `Get-FileHash output/v0.1.96/CineVault-Setup-v0.1.96.exe -Algorithm SHA256`：通过

## 待办清单（未完成）

- 提交当前修复与快照
- 推送 `origin/main`
- 创建并推送 `v0.1.96`
- 创建 GitHub Release `v0.1.96`
- 让你下载安装测试

## 下一步要做什么

继续做 GitHub 发布收尾，把这版修复包真正推送出去。
