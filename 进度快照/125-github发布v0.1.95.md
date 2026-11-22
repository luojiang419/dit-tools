# 125-github发布v0.1.95

日期：2026-06-30

## 已完成内容

- 已读取上一轮最新快照：`124-界面续跑与构建验证.md`。
- 已读取 GitHub 发布流程说明，并确认 `gh` 已登录账号 `luojiang419`。
- 已确认当前分支为 `main`，远端为 `https://github.com/luojiang419/dit-tools.git`。
- 已创建阶段备份：`backup/v0.1.106`。
- 已同步项目默认版本与安装器默认版本到 `v0.1.95`。
- 已执行真实工作流打包：
  - `powershell -ExecutionPolicy Bypass -File tool/build_windows.ps1 -RealWorkflow`
- 已完成构建和测试验证：
  - `ctest --test-dir dit-tools-src/cinevault-pro/build/windows-msvc-release-real -C Release --output-on-failure`
  - 6/6 通过
- 已生成安装包资产：
  - `CineVault-Setup-v0.1.95.exe`
  - 大小：`57,818,929` 字节
  - SHA256：`7B542673592342736351D032C74EAEFD2FE6C7C3D21883309B3056DF83086CC9`
  - 下载地址：`https://github.com/luojiang419/dit-tools/releases/download/v0.1.95/CineVault-Setup-v0.1.95.exe`
- 已提交发布源码基线：
  - `ac2047eb7ab7286b0d0ef26fc758e288a69a4941`
- 已推送 `origin/main`。
- 已创建并推送 `v0.1.95` tag。
- 已创建 GitHub Release：
  - `https://github.com/luojiang419/dit-tools/releases/tag/v0.1.95`

## 当前修改到哪个模块

当前完成模块：GitHub 发布与安装包分发模块。

本轮“解析续跑与单帧重试”相关功能已经完成以下对齐：

1. `origin/main` 指向发布源码基线提交 `ac2047eb7ab7286b0d0ef26fc758e288a69a4941`
2. `v0.1.95` tag 指向同一提交
3. GitHub Release `v0.1.95` 已公开发布
4. Release 资产哈希与本地安装包哈希一致

## 具体修改的代码前后对比

### 发布前

```text
- 续跑与单帧重试代码已经完成，但还未提交到 GitHub。
- 项目默认版本和安装器默认版本仍是 0.1.0 / v0.1.0。
- GitHub latest release 仍停留在 v0.1.94。
```

### 发布后

```text
- 发布源码已提交并推送到 origin/main。
- 已生成并上传 v0.1.95 安装包到公开 GitHub Release。
- 自动更新读取 latest release 时会命中新版本 v0.1.95。
- 最新进度快照已推进到 125，可直接从本次发布结果继续接力。
```

## 验证结果

- `gh auth status`：通过。
- `git push origin main`：通过。
- `git push origin v0.1.95`：通过。
- `gh release create v0.1.95 ...`：通过。
- `gh release view v0.1.95 --json url,assets,name,tagName,targetCommitish`：通过，资产 digest 为 `sha256:7b542673592342736351d032c74eaefd2fe6c7c3d21883309b3056df83086cc9`。
- `git ls-remote origin refs/heads/main refs/tags/v0.1.95`：通过，`main` 与 `v0.1.95` 均指向 `ac2047eb7ab7286b0d0ef26fc758e288a69a4941`。

## 待办清单（未完成）

- 由你执行真实业务回归测试。
- 重点验证中断续跑、跳过帧补解析、90% 汇总失败后继续解析三条主路径。

## 下一步要做什么

你可以直接下载安装包或在旧版本里触发自动更新，然后按上述三个关键场景做测试。如果测试里再出现异常，我会直接从这份 `125` 快照继续修正。
