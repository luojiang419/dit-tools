#!/usr/bin/env bash
set -euo pipefail

TARGET="${1:-}"
INSTALL_ROOT="${2:-/opt/cinevault-web}"
PUBLIC_ORIGIN="${3:-}"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SITE_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

if [[ -z "$TARGET" || -z "$PUBLIC_ORIGIN" ]]; then
  echo "用法: $0 <user@server> [安装目录] <公开站点 Origin>" >&2
  echo "示例: $0 root@example.com /opt/cinevault-web https://cinevault.example.com" >&2
  exit 2
fi
if [[ ! "$TARGET" =~ ^[A-Za-z0-9_.@:-]+$ ]]; then
  echo "服务器目标包含不安全字符。" >&2
  exit 2
fi
if [[ ! "$INSTALL_ROOT" =~ ^/[A-Za-z0-9_./-]+$ ]]; then
  echo "安装目录必须是只含常见安全字符的绝对路径。" >&2
  exit 2
fi
if [[ ! "$PUBLIC_ORIGIN" =~ ^https?://[A-Za-z0-9_.:-]+$ ]]; then
  echo "公开 Origin 格式无效，不能包含路径。" >&2
  exit 2
fi

ssh "$TARGET" "mkdir -p '$INSTALL_ROOT/site' '$INSTALL_ROOT/server' '$INSTALL_ROOT/config' '$INSTALL_ROOT/data'"

rsync -az --delete-delay \
  --exclude '/server/' \
  --exclude '/deploy/' \
  --exclude '*.pyc' \
  --exclude '.pytest_cache/' \
  --exclude '__pycache__/' \
  "$SITE_DIR/" "$TARGET:$INSTALL_ROOT/site/"

rsync -az --delete-delay \
  --exclude '/.venv/' \
  --exclude '/.env' \
  --exclude '/data/' \
  --exclude '*.pyc' \
  --exclude '.pytest_cache/' \
  --exclude '__pycache__/' \
  "$SITE_DIR/server/" "$TARGET:$INSTALL_ROOT/server/"

rsync -az "$SCRIPT_DIR/nginx.conf.example" "$SCRIPT_DIR/cinevault-web.service.example" "$TARGET:$INSTALL_ROOT/config/"

ssh "$TARGET" "INSTALL_ROOT='$INSTALL_ROOT' PUBLIC_ORIGIN='$PUBLIC_ORIGIN' bash -s" <<'REMOTE_SETUP'
set -euo pipefail
python3 -m venv "$INSTALL_ROOT/server/.venv"
"$INSTALL_ROOT/server/.venv/bin/pip" install --disable-pip-version-check -r "$INSTALL_ROOT/server/requirements.txt"
if [[ ! -f "$INSTALL_ROOT/server/.env" ]]; then
  token="$(python3 -c 'import secrets; print(secrets.token_urlsafe(40))')"
  umask 077
  {
    echo "CINEVAULT_WEB_HOST=127.0.0.1"
    echo "CINEVAULT_WEB_PORT=3413"
    echo "CINEVAULT_SPONSOR_DB_PATH=$INSTALL_ROOT/data/sponsors.sqlite3"
    echo "CINEVAULT_SPONSOR_ADMIN_TOKEN=$token"
    echo "CINEVAULT_ALLOWED_ORIGINS=$PUBLIC_ORIGIN"
    echo "CINEVAULT_TRUST_PROXY_HEADERS=true"
  } > "$INSTALL_ROOT/server/.env"
fi
REMOTE_SETUP

echo "网站与后端文件已同步到 $TARGET:$INSTALL_ROOT"
echo "接下来按 README 替换 Nginx/systemd 模板中的域名和路径，再启用服务。"
echo "若需服务器直连下载，请把 release.json 对应的安装包放入 $INSTALL_ROOT/site/downloads/。"
