# 128-github发布v0.1.96完成

日期：2026-06-30

## 已完成内容

- 已读取上一轮最新快照：`127-github发布v0.1.96.md`。
- 已将修复提交推送到 `origin/main`。
- 已创建并推送 `v0.1.96` tag。
- 已创建 GitHub Release：
  - `https://github.com/luojiang419/dit-tools/releases/tag/v0.1.96`
- 已上传安装包资产：
  - `CineVault-Setup-v0.1.96.exe`
  - 大小：`57,816,688` 字节
  - SHA256：`CAD47FF83AFB2241D3F45B7CBC926B2469F85129AB31751DA99376A959F0D730`
  - 下载地址：`https://github.com/luojiang419/dit-tools/releases/download/v0.1.96/CineVault-Setup-v0.1.96.exe`
- 已确认远端对齐：
  - `origin/main` 指向 `62cfd00fe9763d6594914399bc75c407da2df576`
  - `v0.1.96` tag 指向同一提交
  - Release 资产 digest 与本地哈希一致

## 当前修改到哪个模块

当前完成模块：GitHub 修复版发布完成模块。

这次针对“素材管理中心加载不了素材”的修复已经全部对齐完成：

1. 修复代码已进入 `origin/main`
2. `v0.1.96` tag 已创建
3. GitHub Release 已公开发布
4. 安装包已可下载安装或应用内检测更新

## 具体修改的代码前后对比

### 发布前

```text
- 修复代码仅在本地生效。
- 用户无法通过安装包或自动更新获取本次修复。
```

### 发布后

```text
- 用户可直接下载安装 CineVault-Setup-v0.1.96.exe。
- 应用内 latest release 也会命中 v0.1.96。
- 修复内容已进入正式测试流。
```

## 验证结果

- `git push origin main`：通过
- `git push origin v0.1.96`：通过
- `gh release create v0.1.96 ...`：通过
- `gh release view v0.1.96 --json url,assets,name,tagName,targetCommitish`：通过
- `git ls-remote origin refs/heads/main refs/tags/v0.1.96`：通过

## 待办清单（未完成）

- 由你下载安装或自动更新到 `v0.1.96`
- 重点复测素材管理中心加载恢复情况
- 继续验证解析续跑与单帧重试主路径

## 下一步要做什么

你现在可以直接安装 `v0.1.96` 测试。如果还有异常，我会从这份 `128` 快照继续接着修。
