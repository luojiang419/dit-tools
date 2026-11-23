# 234 - 影资管家官网与高保真 Demo 完成

## 本轮任务目标

为影资管家新增一个可部署到用户自有服务器的独立官网项目，包含：

- 产品官网首页。
- 与当前 Qt/QML 桌面端视觉和操作路径高度一致的在线 Demo。
- 最新 Windows 安装包下载功能。
- 从 Pond5 官网迁移适配的赞助二维码、赞助申报、赞助榜和管理员确认功能。
- Nginx、systemd 和服务器同步文件。

## 本轮保护范围

- 桌面端自然语言搜索模块仍处于其他开发任务的未提交状态，本轮没有修改、覆盖或清理这些文件。
- 原有未跟踪文件 `DIT-tools-source-sanitized-20260714.zip` 未读取、修改、移动或删除。
- 本轮只新增/修改 `.gitignore`、`docs/官网项目开发规划.md`、`tools-web/` 和本快照。

## 已完成内容

### 模块 0：整体规划与可行性分析

新增 `docs/官网项目开发规划.md`，固化任务目标、模块拆分、文件清单、迁移映射和验收标准。

核心结论：

- 可以实现视觉和主要操作路径高度 1:1。
- 不能在普通网页里原封不动执行 Windows 文件系统、SQLite、FFmpeg、Qt Multimedia 和本地模型能力。
- Demo 使用模拟数据复现状态变化，明确不访问、不上传、不删除访客本地文件。

### 模块 1：官网基础

新增：

- `tools-web/index.html`
- `tools-web/styles.css`
- `tools-web/script.js`
- `tools-web/assets/logo.png`
- `tools-web/assets/support/*.jpg`

完成首页导航、产品首屏、核心能力、工作流、隐私边界、Demo 入口、下载弹窗、赞助入口、明暗主题、移动端布局和键盘焦点处理。

### 模块 2：高保真在线 Demo

新增：

- `tools-web/demo/index.html`
- `tools-web/demo/demo.css`
- `tools-web/demo/demo.js`

已复现页面和流程：

- 项目库：选择项目并进入素材库。
- 素材库：素材源切换、卡片/技术表格切换、仅收藏、搜索、素材详情。
- 素材管理中心：筛选、素材详情、内容摘要、关键词、确认结果、单条/批量模拟解析。
- 任务页：批次统计、任务进度、完成状态、清理已完成任务。
- 报表页：表头配置、PDF 预览和分页操作。
- 反馈页：软件内对话布局与“不会真正发送”的安全反馈。
- 设置弹窗：软件更新、主题、视觉解析、缩略图配置和即时主题切换。

布局基线直接对应 QML：

- 设计画布：1600×980。
- 顶部命令栏：64px。
- 素材源栏：270px。
- 素材检查器：330px。
- 标签页宽度和名称来自 `ShellViewModel::workspaceTabs()`。

### 模块 3：赞助功能

新增前端：

- `sponsors.html/css/js`
- `sponsors-admin.html/css/js`

新增后端：

- `server/app/config.py`
- `server/app/db.py`
- `server/app/sponsors.py`
- `server/app/main.py`
- `server/tests/test_sponsors.py`

API 命名空间：

```text
/api/cinevault-support/v1
```

赞助状态严格执行：

```text
用户提交 pending
  → 管理员核实到账
  → confirmed
  → 公开赞助榜
```

安全措施：

- 昵称、金额、支付方式和公共 ID 校验。
- 表单蜜罐。
- 同一来源 10 分钟最多提交 3 次。
- 管理员令牌使用常量时间比较。
- 管理员令牌只放在请求头，并由管理页保存到当前标签页的 sessionStorage。
- CORS 来源白名单、请求大小限制、Nginx 二次限流和安全响应头。

### 模块 4：下载与自托管

新增 `downloads/release.json` 作为官网唯一版本信息来源。

当前正式版：

```text
版本：v0.1.146
文件：CineVault-Setup-v0.1.146.exe
大小：167490065 字节
SHA-256：219880a6a0c4f1f42636a94da7b58a8fdddba6d89263fe41ea267140de8b6ddd
发布日期：2026-07-13T14:28:22Z
```

下载逻辑：

1. 首页读取 `release.json`。
2. HEAD 探测服务器本地安装包。
3. 服务器安装包存在时使用本地直连。
4. 不存在或探测失败时自动回退 GitHub Release。

新增自托管文件：

- `deploy/deploy_to_server.sh`
- `deploy/nginx.conf.example`
- `deploy/cinevault-web.service.example`
- `server/.env.example`
- `tools-web/README.md`
- `server/README.md`

没有执行公开云部署。

## 代码前后对比

### 1. 官网项目

修改前：

```text
当前仓库没有 tools-web 官网目录。
```

修改后：

```text
tools-web/
├─ 官网首页
├─ 1600×980 高保真 Demo
├─ 下载版本清单
├─ 赞助前端与管理页
├─ FastAPI/SQLite 赞助服务
├─ 10 个自动化测试
└─ Nginx/systemd/服务器同步文件
```

### 2. Demo 布局

桌面端源码基线：

```qml
width: 1600
height: 980
implicitHeight: 64
sourceRail: 270
inspector: 330
```

Web Demo 对应实现：

```css
.app { width: 1600px; height: 980px; }
.top-command-bar { height: 64px; }
.source-rail { flex: 0 0 270px; }
.inspector { flex: 0 0 330px; }
```

### 3. 赞助命名空间

迁移前参考实现：

```text
/api/pond5-support/v1
POND5_SPONSOR_ADMIN_TOKEN
pond5-pending-support-claims
```

迁移后：

```text
/api/cinevault-support/v1
CINEVAULT_SPONSOR_ADMIN_TOKEN
cinevault-pending-support-claims
```

### 4. 下载配置

修改前：

```text
版本、下载地址、发布日期和大小写死在 HTML 中。
```

修改后：

```text
downloads/release.json 统一驱动页面展示、服务器直连和 GitHub 回退。
```

## 验收结果

### 自动测试

```text
10 passed in 0.84s
```

覆盖：

- HTML 本地资源与重复 ID。
- 全部 JavaScript 语法。
- QML 与 Demo 的 1600×980 / 64 / 270 / 330 布局契约。
- Demo 无文件上传控件、无 fetch/XMLHttpRequest/WebSocket 数据传输。
- 下载清单字段与哈希格式。
- 前后端 API 命名空间一致。
- 新站点无旧 Pond5 产品命名泄漏。
- 赞助 pending/confirmed 公开规则、错误令牌、蜜罐、输入校验与频率限制。

### 独立进程烟测

```text
HEALTH cinevault-support 1.0.0
CREATED pending
WRONG_TOKEN 401
CONFIRMED confirmed PUBLIC=1
```

### 静态入口烟测

下列地址均返回 200：

- `/`
- `/demo/`
- `/sponsors.html`
- `/sponsors-admin.html`
- `/downloads/release.json`
- logo 与赞助二维码资源。

服务器安装包当前未放入 Git，HEAD 返回 404 符合预期，页面会自动使用 GitHub 备用地址。

### 部署包

```text
路径：output/cinevault-web-v0.1.146.zip
大小：566333 字节
SHA-256：4bfdba521920f1417ca4d4ef48afa2ad3ca4420d763a5ae4b3141f685022555f
文件数：32
```

部署包不包含：

- SQLite 运行数据库。
- `.venv`。
- `__pycache__`。
- `.pytest_cache`。
- 大型安装包。

### 可视检查说明

本轮尝试使用应用内浏览器做截图与点击检查，但当前桌面会话的浏览器运行时与此前 Windows 控制运行时发生 `Cannot redefine property: process` 兼容错误。没有安装或调用外部 Playwright/Chromium 规避该限制。本轮使用 QML 源码常量、静态 HTML 资源检查、JavaScript 语法和可执行 API 烟测收口。部署前仍建议人工打开一次本地预览，确认字体在目标浏览器中的最终观感。

## 缓存与环境清理

- 已停止本轮启动的 `4173` 本地静态预览服务。
- 已停止独立烟测用 FastAPI 进程。
- 已清理烟测数据库、`__pycache__` 和 `.pytest_cache`。
- `tools-web` 源码总体积低于 1 MB（不含部署包和安装包），远低于 20 GB 限制。
- 未停止或修改用户原先占用 `127.0.0.1:3013` 的其他服务。
- 新官网赞助服务已统一改用空闲的 `127.0.0.1:3413`，避免本地预览冲突。

## 当前模块状态

官网开发模块 0–6 已完成。没有待开发代码。

## 用户部署前待办

1. 确定正式域名。
2. 在 `nginx.conf.example` 中替换域名并配置 TLS。
3. 执行 `deploy_to_server.sh` 同步文件。
4. 启用 systemd 服务并确保 `/opt/cinevault-web/data` 可由 `www-data` 写入。
5. 如需服务器直连下载，把 `CineVault-Setup-v0.1.146.exe` 放到服务器 `site/downloads/`；否则 GitHub 回退已经可用。
6. 从服务器 `.env` 读取管理员令牌，在 `/sponsors-admin.html` 审核赞助记录。
7. 正式上线前人工完成一次桌面与手机浏览器视觉检查。

## 下一步入口

如继续部署，先读取本快照，然后按 `tools-web/README.md` 的“自有服务器部署”章节执行。不要重新扫描旧快照，也不要覆盖当前桌面端自然语言搜索模块的未提交改动。
