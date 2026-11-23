# 影资管家官网赞助服务

该服务只负责赞助申报、状态查询、公开赞助榜和管理员审核，不处理 Demo 素材，也不托管安装包。

## 本地运行

```bash
python -m venv .venv
./.venv/bin/pip install -r requirements.txt
cp .env.example .env
./.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port 3413
```

Windows PowerShell 中可使用：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
Copy-Item .env.example .env
.\.venv\Scripts\python.exe -m uvicorn app.main:app --host 127.0.0.1 --port 3413
```

管理员令牌必须改成不可猜测的长随机值，不能提交到 Git、网页源码或公开文档。管理员页面地址为 `/sponsors-admin.html`。

## 接口

- `GET /api/cinevault-support/v1/health`
- `GET /api/cinevault-support/v1/sponsors`
- `POST /api/cinevault-support/v1/claims`
- `GET /api/cinevault-support/v1/claims/{public_id}`
- `GET /api/cinevault-support/v1/admin/claims`
- `POST /api/cinevault-support/v1/admin/claims/{public_id}`

## 测试

```bash
python -m pip install -r requirements-dev.txt
python -m pytest -q
```
