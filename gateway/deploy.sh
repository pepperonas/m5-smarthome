#!/usr/bin/env bash
# Deploy the gateway to raspi5. Idempotent; safe to re-run.
set -euo pipefail
HOST="${1:-raspi5}"
DEST=/home/pi/apps/m5-gateway
HERE="$(cd "$(dirname "$0")" && pwd)"

echo "==> tests"
( cd "$HERE" && python3 -m pytest tests/ -q )

echo "==> sync to $HOST:$DEST"
ssh "$HOST" "mkdir -p $DEST"
rsync -az --delete \
  --exclude '__pycache__' --exclude '.pytest_cache' --exclude '.env' \
  "$HERE/m5gw" "$HERE/tests" "$HERE/requirements.txt" "$HOST:$DEST/"

echo "==> venv"
ssh "$HOST" "test -d $DEST/venv || python3 -m venv --system-site-packages $DEST/venv; \
             $DEST/venv/bin/pip -q install -r $DEST/requirements.txt"

echo "==> token"
ssh "$HOST" "test -f $DEST/.env || { \
   printf 'M5GW_TOKEN=%s\n' \"\$(openssl rand -hex 24)\" > $DEST/.env; \
   chmod 600 $DEST/.env; echo '    generated a new token'; }"

echo "==> mDNS announcement"
scp -q "$HERE/m5-gateway.avahi.service" "$HOST:/tmp/m5-gateway.avahi.service"
ssh "$HOST" "sudo mv /tmp/m5-gateway.avahi.service /etc/avahi/services/m5-gateway.service"

echo "==> service"
scp -q "$HERE/m5-gateway.service" "$HOST:/tmp/m5-gateway.service"
ssh "$HOST" "sudo mv /tmp/m5-gateway.service /etc/systemd/system/ && \
             sudo systemctl daemon-reload && \
             sudo systemctl enable --now m5-gateway && \
             sleep 1 && systemctl is-active m5-gateway"

echo "==> health"
ssh "$HOST" "curl -s -m 3 http://127.0.0.1:5010/api/health"
echo
