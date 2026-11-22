# 130-github发布v0.1.97完成

日期：2026-06-30

## 已完成内容

- 已读取上一轮最新快照：`129-修复旧库启动阶段索引迁移.md`。
- 已将修复提交推送到 `origin/main`。
- 已创建并推送 `v0.1.97` tag。
- 已创建 GitHub Release：
  - `https://github.com/luojiang419/dit-tools/releases/tag/v0.1.97`
- 已上传安装包资产：
  - `CineVault-Setup-v0.1.97.exe`
  - 大小：`57,817,064` 字节
  - SHA256：`70BE7E1254E920A992D2C540DADB7D1D1B6BCAA450A22AC4271E492B9D949E75`
  - 下载地址：`https://github.com/luojiang419/dit-tools/releases/download/v0.1.97/CineVault-Setup-v0.1.97.exe`
- 已确认远端对齐：
  - `origin/main` 指向 `a0968964890a1f678de2955200a46e2a3846bad6`
  - `v0.1.97` tag 指向同一提交
  - Release 资产 digest 与本地哈希一致

## 当前修改到哪个模块

当前完成模块：002 项目素材管理中心显示修复版发布模块。

本轮围绕“002 项目素材仍不显示”的完整闭环已经完成：

1. 确认 002 项目素材其实已在全局库中
2. 找到启动阶段索引迁移再次提前失败的根因
3. 修复代码已进入 `origin/main`
4. `v0.1.97` 安装包已公开发布

## 具体修改的代码前后对比

### 发布前

```text
- v0.1.96 只修到了兼容迁移后半段。
- createSchema() 仍在旧库启动前半段提前创建 analysis_state 相关索引。
- 002 项目素材依然无法在素材管理中心显示。
```

### 发布后

```text
- v0.1.97 已移除启动前半段的提前建索引动作。
- 旧库可以走完整条迁移链路。
- 用户可通过安装包或应用内更新获取修复。
```

## 验证结果

- `git push origin main`：通过
- `git push origin v0.1.97`：通过
- `gh release create v0.1.97 ...`：通过
- `gh release view v0.1.97 --json url,assets,name,tagName,targetCommitish`：通过
- `git ls-remote origin refs/heads/main refs/tags/v0.1.97`：通过

## 待办清单（未完成）

- 由用户更新到 `v0.1.97`
- 重点复测 002 项目素材在素材管理中心的恢复情况
- 继续复测解析续跑与单帧重试主路径

## 下一步要做什么

现在直接更新到 `v0.1.97` 测试 002 项目。如果还有异常，下一轮从这份 `130` 快照继续修。
