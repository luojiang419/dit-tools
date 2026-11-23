#!/usr/bin/env bash
set -Eeuo pipefail

SITE_ARCHIVE="${1:?missing site archive}"
BACKEND_ARCHIVE="${2:?missing backend archive}"
NGINX_TEMPLATE="${3:?missing nginx template}"
SITE_ROOT="${4:?missing site root}"
BACKEND_ROOT="${5:?missing backend root}"
SERVICE_NAME="${6:?missing service name}"
ACTIVE_CONFIG="${7:?missing active nginx config}"
DOMAIN="${8:?missing domain}"
CONTACT_EMAIL="${9:?missing contact email}"
PORT_START="${10:?missing port start}"
PORT_END="${11:?missing port end}"

if [[ ! "$DOMAIN" =~ ^[A-Za-z0-9.-]+$ ]]; then
  echo "invalid domain" >&2
  exit 2
fi
if [[ ! "$CONTACT_EMAIL" =~ ^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+$ ]]; then
  echo "invalid contact email" >&2
  exit 2
fi
if [[ ! "$PORT_START" =~ ^[0-9]+$ || ! "$PORT_END" =~ ^[0-9]+$ ]]; then
  echo "invalid port range" >&2
  exit 2
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
WORK_ROOT="/tmp/cinevault_web_deploy_${STAMP}"
SITE_BACKUP=""
BACKEND_BACKUP=""
BACKUP_PARENT="$(dirname "$BACKEND_ROOT")/backups"
CONFIG_BACKUP="$WORK_ROOT/original-nginx.conf"
OPENRESTY_CONTAINER=""

cleanup() {
  rm -rf "$WORK_ROOT"
  rm -f "$SITE_ARCHIVE" "$BACKEND_ARCHIVE" "$NGINX_TEMPLATE"
  rm -f "$0"
}
trap cleanup EXIT

port_is_used() {
  local port="$1"
  ss -ltnH 2>/dev/null | awk '{print $4}' | grep -Eq ":${port}$"
}

choose_port() {
  local existing=""
  if [[ -f "$BACKEND_ROOT/.env" ]]; then
    existing="$(sed -n 's/^CINEVAULT_WEB_PORT=//p' "$BACKEND_ROOT/.env" | tail -n 1)"
  fi
  if [[ "$existing" =~ ^[0-9]+$ ]]; then
    if systemctl is-active --quiet "$SERVICE_NAME" || ! port_is_used "$existing"; then
      printf '%s' "$existing"
      return
    fi
  fi
  local candidate
  for ((candidate = PORT_START; candidate <= PORT_END; candidate++)); do
    if ! port_is_used "$candidate"; then
      printf '%s' "$candidate"
      return
    fi
  done
  echo "no free port in ${PORT_START}-${PORT_END}" >&2
  exit 3
}

set_env_value() {
  local env_path="$1"
  local key="$2"
  local value="$3"
  if grep -q "^${key}=" "$env_path"; then
    sed -i "s|^${key}=.*|${key}=${value}|" "$env_path"
  else
    printf '%s=%s\n' "$key" "$value" >> "$env_path"
  fi
}

restore_nginx_config() {
  if [[ -f "$CONFIG_BACKUP" ]]; then
    cp -f "$CONFIG_BACKUP" "$ACTIVE_CONFIG"
  else
    rm -f "$ACTIVE_CONFIG"
  fi
  if [[ -n "$OPENRESTY_CONTAINER" ]]; then
    docker exec "$OPENRESTY_CONTAINER" openresty -t >/dev/null 2>&1 || true
    docker exec "$OPENRESTY_CONTAINER" openresty -s reload >/dev/null 2>&1 || true
  fi
}

activate_nginx_config() {
  local candidate="$1"
  cp -f "$candidate" "$ACTIVE_CONFIG"
  if ! docker exec "$OPENRESTY_CONTAINER" openresty -t; then
    restore_nginx_config
    return 1
  fi
  docker exec "$OPENRESTY_CONTAINER" openresty -s reload
}

CHOSEN_PORT="$(choose_port)"
mkdir -p "$WORK_ROOT/site" "$WORK_ROOT/backend" "$BACKUP_PARENT"
tar -xzf "$SITE_ARCHIVE" -C "$WORK_ROOT/site"
tar -xzf "$BACKEND_ARCHIVE" -C "$WORK_ROOT/backend"

test -f "$WORK_ROOT/site/index.html"
test -f "$WORK_ROOT/site/downloads/release.json"
test -f "$WORK_ROOT/backend/app/main.py"
test -f "$WORK_ROOT/backend/requirements.txt"

available_kb="$(df -Pk "$(dirname "$SITE_ROOT")" | awk 'NR == 2 {print $4}')"
if [[ -z "$available_kb" || "$available_kb" -lt 700000 ]]; then
  echo "insufficient disk space for safe deployment" >&2
  exit 4
fi

if [[ -d "$SITE_ROOT" && -n "$(find "$SITE_ROOT" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
  SITE_BACKUP="${SITE_ROOT}_backup_${STAMP}_codex_full_site"
  cp -a "$SITE_ROOT" "$SITE_BACKUP"
fi
if [[ -d "$BACKEND_ROOT" ]]; then
  BACKEND_BACKUP="${BACKUP_PARENT}/cinevault-support-site-${STAMP}"
  cp -a "$BACKEND_ROOT" "$BACKEND_BACKUP"
fi
if [[ -f "$ACTIVE_CONFIG" ]]; then
  cp -a "$ACTIVE_CONFIG" "$CONFIG_BACKUP"
fi

if ! command -v rsync >/dev/null 2>&1; then
  apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq rsync
fi

mkdir -p "$SITE_ROOT" "$BACKEND_ROOT" "$BACKEND_ROOT/data"
rsync -a --delete --exclude '/ssl/' --exclude '/.well-known/' "$WORK_ROOT/site/" "$SITE_ROOT/"
rsync -a --delete --exclude '/.env' --exclude '/.venv/' --exclude '/data/' "$WORK_ROOT/backend/" "$BACKEND_ROOT/"
chmod -R a+rX "$SITE_ROOT"

if ! python3 -m venv "$BACKEND_ROOT/.venv"; then
  apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq python3-venv
  rm -rf "$BACKEND_ROOT/.venv"
  python3 -m venv "$BACKEND_ROOT/.venv"
fi
"$BACKEND_ROOT/.venv/bin/pip" install --disable-pip-version-check --quiet -r "$BACKEND_ROOT/requirements.txt"

ENV_PATH="$BACKEND_ROOT/.env"
if [[ ! -f "$ENV_PATH" ]]; then
  umask 077
  : > "$ENV_PATH"
fi
if ! grep -q '^CINEVAULT_SPONSOR_ADMIN_TOKEN=' "$ENV_PATH"; then
  set_env_value "$ENV_PATH" CINEVAULT_SPONSOR_ADMIN_TOKEN "$(python3 -c 'import secrets; print(secrets.token_urlsafe(40))')"
fi
set_env_value "$ENV_PATH" CINEVAULT_WEB_HOST 127.0.0.1
set_env_value "$ENV_PATH" CINEVAULT_WEB_PORT "$CHOSEN_PORT"
set_env_value "$ENV_PATH" CINEVAULT_SPONSOR_DB_PATH "$BACKEND_ROOT/data/sponsors.sqlite3"
set_env_value "$ENV_PATH" CINEVAULT_ALLOWED_ORIGINS "https://${DOMAIN}"
set_env_value "$ENV_PATH" CINEVAULT_TRUST_PROXY_HEADERS true
chown -R www-data:www-data "$BACKEND_ROOT/data"
chown root:www-data "$ENV_PATH"
chmod 640 "$ENV_PATH"

cat > "/etc/systemd/system/$SERVICE_NAME" <<EOF
[Unit]
Description=CineVault website sponsor API
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=www-data
Group=www-data
UMask=0027
WorkingDirectory=$BACKEND_ROOT
EnvironmentFile=$ENV_PATH
ExecStart=$BACKEND_ROOT/.venv/bin/uvicorn app.main:app --host 127.0.0.1 --port $CHOSEN_PORT --proxy-headers --forwarded-allow-ips=127.0.0.1
Restart=always
RestartSec=3
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=$BACKEND_ROOT/data
NoNewPrivileges=true

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable "$SERVICE_NAME" >/dev/null
systemctl restart "$SERVICE_NAME"

for _ in 1 2 3 4 5 6 7 8 9 10; do
  if curl -fsS "http://127.0.0.1:${CHOSEN_PORT}/api/cinevault-support/v1/health" >/dev/null 2>&1; then
    break
  fi
  sleep 1
done
systemctl is-active --quiet "$SERVICE_NAME"
curl -fsS "http://127.0.0.1:${CHOSEN_PORT}/api/cinevault-support/v1/health" >/dev/null
curl -fsS "http://127.0.0.1:${CHOSEN_PORT}/api/cinevault-support/v1/sponsors" >/dev/null
chmod 750 "$BACKEND_ROOT/data"
find "$BACKEND_ROOT/data" -maxdepth 1 -type f -exec chmod 640 {} +

OPENRESTY_CONTAINER="$(docker ps --format '{{.Names}}' | grep -i openresty | head -n 1)"
if [[ -z "$OPENRESTY_CONTAINER" ]]; then
  echo "openresty container not found" >&2
  exit 5
fi

mkdir -p "$(dirname "$ACTIVE_CONFIG")" "$SITE_ROOT/.well-known/acme-challenge"
BOOTSTRAP_CONFIG="$WORK_ROOT/bootstrap-nginx.conf"
cat > "$BOOTSTRAP_CONFIG" <<EOF
server {
    listen 80;
    server_name $DOMAIN;
    root /www/sites/$DOMAIN;
    location ^~ /.well-known/acme-challenge/ { allow all; try_files \$uri =404; }
    location / { try_files \$uri \$uri/ =404; }
}
EOF
activate_nginx_config "$BOOTSTRAP_CONFIG"

certbot certonly --webroot -w "$SITE_ROOT" -d "$DOMAIN" \
  --non-interactive --agree-tos --email "$CONTACT_EMAIL" --keep-until-expiring

CERT_LINEAGE="/etc/letsencrypt/live/$DOMAIN"
mkdir -p "$SITE_ROOT/ssl"
install -m 644 "$CERT_LINEAGE/fullchain.pem" "$SITE_ROOT/ssl/fullchain.pem"
install -m 600 "$CERT_LINEAGE/privkey.pem" "$SITE_ROOT/ssl/privkey.pem"

RENDERED_CONFIG="$WORK_ROOT/${DOMAIN}.conf"
sed -e "s/__DOMAIN__/${DOMAIN}/g" -e "s/__CINEVAULT_PORT__/${CHOSEN_PORT}/g" \
  "$NGINX_TEMPLATE" > "$RENDERED_CONFIG"
grep -q "server_name ${DOMAIN};" "$RENDERED_CONFIG"
grep -q "proxy_pass http://127.0.0.1:${CHOSEN_PORT};" "$RENDERED_CONFIG"
activate_nginx_config "$RENDERED_CONFIG"

RENEW_HOOK="/etc/letsencrypt/renewal-hooks/deploy/cinevault-${DOMAIN}.sh"
mkdir -p "$(dirname "$RENEW_HOOK")"
cat > "$RENEW_HOOK" <<EOF
#!/usr/bin/env bash
set -euo pipefail
[[ "\${RENEWED_LINEAGE:-}" == "/etc/letsencrypt/live/$DOMAIN" ]] || exit 0
install -m 644 "\$RENEWED_LINEAGE/fullchain.pem" "$SITE_ROOT/ssl/fullchain.pem"
install -m 600 "\$RENEWED_LINEAGE/privkey.pem" "$SITE_ROOT/ssl/privkey.pem"
container="\$(docker ps --format '{{.Names}}' | grep -i openresty | head -n 1)"
docker exec "\$container" openresty -t
docker exec "\$container" openresty -s reload
EOF
chmod 700 "$RENEW_HOOK"
systemctl enable --now certbot.timer >/dev/null 2>&1 || true

curl -fsSI -H "Host: $DOMAIN" http://127.0.0.1/ | grep -qE '^HTTP/.* 301'
curl -fsS --resolve "$DOMAIN:443:127.0.0.1" "https://$DOMAIN/" -o "$WORK_ROOT/home-check.html"
grep -q '影资管家' "$WORK_ROOT/home-check.html"
curl -fsS --resolve "$DOMAIN:443:127.0.0.1" "https://$DOMAIN/api/cinevault-support/v1/health" -o "$WORK_ROOT/api-check.json"
grep -q '"ok":true' "$WORK_ROOT/api-check.json"

INSTALLER_NAME="$(python3 -c 'import json; print(json.load(open("'"$SITE_ROOT"'/downloads/release.json", encoding="utf-8"))["fileName"])')"
INSTALLER_SHA="$(python3 -c 'import json; print(json.load(open("'"$SITE_ROOT"'/downloads/release.json", encoding="utf-8"))["sha256"])')"
test -f "$SITE_ROOT/downloads/$INSTALLER_NAME"
printf '%s  %s\n' "$INSTALLER_SHA" "$SITE_ROOT/downloads/$INSTALLER_NAME" | sha256sum -c - >/dev/null

printf 'DEPLOY_DOMAIN=%s\n' "$DOMAIN"
printf 'DEPLOY_PORT=%s\n' "$CHOSEN_PORT"
printf 'SITE_ROOT=%s\n' "$SITE_ROOT"
printf 'BACKEND_ROOT=%s\n' "$BACKEND_ROOT"
printf 'SERVICE_NAME=%s\n' "$SERVICE_NAME"
printf 'SITE_BACKUP=%s\n' "${SITE_BACKUP:-none-new-site}"
printf 'BACKEND_BACKUP=%s\n' "${BACKEND_BACKUP:-none-new-backend}"
printf 'ADMIN_TOKEN_FILE=%s\n' "$ENV_PATH"
