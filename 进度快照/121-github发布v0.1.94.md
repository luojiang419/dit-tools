# 121-github发布v0.1.94

日期：2026-06-29

## 已完成内容

- 已读取上一轮最新快照：`120-视觉返回容错发布打包.md`。
- 已读取 GitHub 发布流程说明，并确认 `gh` 已登录账号 `luojiang419`。
- 已确认当前分支为 `main`，远端为 `https://github.com/luojiang419/dit-tools.git`。
- 已确认发布源码基线提交为 `2712f1523f46dddc873495bd2fbb0f41edb04637`，本地 `main` 与 `origin/main` 在发布核对时一致。
- 已创建阶段备份：`backup/v0.1.103`。
- 已同步远端 tags，本地新增 `v0.1.94` tag。
- 已确认 GitHub Release `v0.1.94` 已存在，且指向发布源码基线提交。
- 已确认安装包资产已上传到 GitHub Release：
  - `CineVault-Setup-v0.1.94.exe`
  - 大小：57,797,137 字节
  - SHA256：`B55DBC514856317164F9FDF54D8E987F6EB6EAB89829DC54EAC820A8E7348C22`
  - 下载地址：`https://github.com/luojiang419/dit-tools/releases/download/v0.1.94/CineVault-Setup-v0.1.94.exe`

## 当前修改到哪个模块

当前完成模块：GitHub 发布核对与快照模块。

源码、tag、Release 和安装包资产已经完成对齐验证：

1. `origin/main` 在发布核对时指向源码基线提交 `2712f1523f46dddc873495bd2fbb0f41edb04637`。
2. `v0.1.94` tag 指向同一提交。
3. GitHub Release `v0.1.94` 已发布。
4. Release 资产哈希与本地安装包哈希一致。

## 具体修改的代码前后对比

本模块不修改业务代码，只新增发布进度快照。

### 发布核对前

```text
- 最新快照显示待办：提交当前变更、推送 GitHub、创建 Release v0.1.94 并上传安装包。
- 本地尚未同步远端 v0.1.94 tag。
```

### 发布核对后

```text
- 已确认 origin/main 与本地 main 一致。
- 已同步 v0.1.94 tag。
- 已确认 GitHub Release v0.1.94 存在并包含安装包。
- 已记录本次发布核对快照：进度快照/121-github发布v0.1.94.md。
```

## 验证结果

- `gh auth status`：通过。
- `git status -sb`：发布核对前工作区干净。
- `git ls-remote origin refs/heads/main refs/tags/v0.1.94`：发布核对时 `main` 与 `v0.1.94` 均指向 `2712f1523f46dddc873495bd2fbb0f41edb04637`。
- `gh release view v0.1.94`：Release 已存在，安装包资产已上传。
- `Get-FileHash output/v0.1.94/CineVault-Setup-v0.1.94.exe -Algorithm SHA256`：本地哈希与 Release digest 一致。

## 待办清单（未完成）

- 将本快照提交并推送到 `origin/main`。
- 使用真实视觉接口复核 CSV、纯文本、Markdown 表格返回时的素材管理中心解析表现。

## 下一步要做什么

提交并推送本次发布快照。推送后 `main` 会新增一笔文档提交，`v0.1.94` tag 仍锁定安装包对应的源码基线。
