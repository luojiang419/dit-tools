# 影资管家官网

这是与桌面端源码隔离的自托管官网项目，包含静态官网、高保真安全 Demo、安装包下载配置和赞助服务。

## 目录职责

- `index.html`：产品官网首页，包含统一的“免费下载 + 自愿赞助 + 联系交流”组合弹窗。
- `support-shared.js`：首页和赞助页共用的赞助 API、待确认记录与提交逻辑。
- `demo/`：按桌面端 1600×980 QML 布局重建的交互 Demo。
- `sponsors.html`：扫码赞助、申报和公开赞助榜。
- `sponsors-admin.html`：管理员审核页，不应出现在公开导航中。
- `downloads/release.json`：官网唯一版本信息来源。
- `server/`：FastAPI + SQLite 赞助后端。
- `deploy/`：Nginx、systemd 与服务器同步模板。

## 本地预览

静态官网：

```powershell
python -m http.server 4173 --bind 127.0.0.1 --directory tools-web
```

赞助 API：

```powershell
Set-Location tools-web\server
python -m uvicorn app.main:app --host 127.0.0.1 --port 3413
```

打开 `http://127.0.0.1:4173/`。静态官网与 API 必须同时运行，赞助提交和榜单才可联调。

首页任意“立即下载”按钮都会打开统一组合弹窗：安装包下载始终位于首屏，赞助二维码和联系方式直接可见，并明确说明不赞助也能免费下载全部功能。开发者联系方式与交流群为微信 `15085152352`、QQ `419773176`、邮箱 `419773176@qq.com`、QQ群 `912211398`。

本地或线上验收时可访问 `/?download=1` 直接打开组合下载弹窗，访问 `/?support=1` 则会同时展开赞助申报表单。

## Demo 真实性边界

Demo 使用与桌面端相同的 1600×980 基准尺寸，并同步顶部栏、侧栏、检查器和主题色。浏览器只模拟页面状态，不读取、上传、移动、解析或删除访客的本地文件。

## 自有服务器部署

1. 准备带 Python 3、Nginx、systemd、rsync 的 Linux 服务器。
2. 在本机 Git Bash/WSL 中运行：

   ```bash
   ./tools-web/deploy/deploy_to_server.sh root@your-server /opt/cinevault-web https://your-domain.example
   ```

3. 将 `deploy/cinevault-web.service.example` 复制到 `/etc/systemd/system/cinevault-web.service`；如果安装目录不是 `/opt/cinevault-web`，先替换路径。
4. 将 `deploy/nginx.conf.example` 复制到 Nginx 的 `conf.d`，替换域名并配置 TLS 证书。
5. 确保 `/opt/cinevault-web/data` 可由 `www-data` 写入，然后启用服务：

   ```bash
   sudo chown -R www-data:www-data /opt/cinevault-web/data
   sudo systemctl daemon-reload
   sudo systemctl enable --now cinevault-web
   sudo nginx -t
   sudo systemctl reload nginx
   ```

6. 从服务器的 `server/.env` 读取管理员令牌，只在 `/sponsors-admin.html` 当前会话中输入。

官网并不要求部署到任何公开云托管平台。Nginx 负责静态文件和大安装包，FastAPI 只监听 `127.0.0.1:3413`。

## dit.ee2x.cn 的 1Panel 正式部署

正式站点与 `pd.ee2x.cn` 位于同一台 1Panel/OpenResty 服务器，并使用完全独立的目录、服务、数据库和 Nginx 配置：

| 项目 | 正式值 |
| --- | --- |
| 域名 | `https://dit.ee2x.cn` |
| 静态站点 | `/opt/1panel/www/sites/dit.ee2x.cn` |
| 赞助后端 | `/opt/ee2x/cinevault_support_site` |
| systemd 服务 | `cinevault-support-site.service` |
| 后端监听 | `127.0.0.1:3413` |
| Nginx 配置 | `/opt/1panel/www/conf.d/dit.ee2x.cn.conf` |
| TLS 证书 | `/opt/1panel/www/sites/dit.ee2x.cn/ssl` |

后续更新网站、后端或正式安装包时，在仓库根目录执行：

```powershell
pwsh -File .\tools-web\deploy\deploy_to_1panel_server.ps1
```

脚本会核对安装包 SHA-256、扫描 `3413–3499` 的可用端口、备份已有站点和后端、保留 `.env` 管理员令牌与 SQLite 数据库、更新 systemd/Nginx，并验证证书、首页、API 和安装包。连接参数均可通过脚本参数覆盖。

默认会在同级项目中自动发现 `公钥\id_ed25519_1panel`；也可以设置 `DIT_1PANEL_SSH_KEY` 环境变量或传入 `-KeyPath` 指定密钥，不会把私钥复制进项目或部署包。

常用远端管理命令：

```bash
systemctl status cinevault-support-site.service
journalctl -u cinevault-support-site.service -n 100 --no-pager
docker exec 1Panel-openresty-Z7GQ openresty -t
grep '^CINEVAULT_WEB_PORT=' /opt/ee2x/cinevault_support_site/.env
grep '^CINEVAULT_SPONSOR_ADMIN_TOKEN=' /opt/ee2x/cinevault_support_site/.env
```

管理员令牌只保存在服务器 `.env`，不会写入静态站点或本地部署包。证书由 Certbot 自动续期，并通过 `/etc/letsencrypt/renewal-hooks/deploy/cinevault-dit.ee2x.cn.sh` 同步给 OpenResty。

## 更新安装包

1. 把新安装包放到 `downloads/` 或服务器的 `/opt/cinevault-web/site/downloads/`。
2. 更新 `downloads/release.json` 的全部元数据。
3. 验证 SHA-256 与安装包一致。
4. 重新同步官网文件。若服务器没有本地安装包，前端会自动使用 GitHub Release 备用地址。
