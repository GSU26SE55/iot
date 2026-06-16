#!/usr/bin/env bash
# ==================================================================
# Sprint 4 — Xóa per-device MQTT credential khỏi Mosquitto passwd.
#
# Dùng khi:
#   - Admin gọi `POST /api/admin/iot-devices/{id}/revoke-key` (rotate scenario)
#   - Device bị retired
#
# Cách dùng:
#   ./infra/mqtt/scripts/remove-device.sh <username>
#
# Effect:
#   - Xóa entry `<username>:...` khỏi passwd
#   - SIGHUP mosquitto reload — device đang connect sẽ bị disconnect lần keepalive tới
# ==================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASSWD_PATH="$SCRIPT_DIR/../mosquitto/passwd"
CONTAINER_NAME="${MQTT_CONTAINER:-iot-mosquitto}"

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <username>" >&2
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "[remove-device] ERROR: docker không có trong PATH" >&2
  exit 1
fi

USERNAME="$1"

if [[ "$USERNAME" == "backend-bridge" ]]; then
  echo "[remove-device] ERROR: không xóa backend-bridge (bridge service cần)." >&2
  echo "[remove-device] Dùng bootstrap.sh --reset nếu muốn rotate password." >&2
  exit 1
fi

if [[ ! -f "$PASSWD_PATH" ]]; then
  echo "[remove-device] passwd file không tồn tại — không có gì để xóa." >&2
  exit 0
fi

if ! grep -q "^${USERNAME}:" "$PASSWD_PATH"; then
  echo "[remove-device] user '$USERNAME' không có trong passwd — skip."
  exit 0
fi

docker run --rm \
  -v "$(dirname "$PASSWD_PATH"):/c" \
  eclipse-mosquitto:2.0 \
  mosquitto_passwd -D "/c/$(basename "$PASSWD_PATH")" "$USERNAME"

chmod 0644 "$PASSWD_PATH"

if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
  docker kill -s HUP "$CONTAINER_NAME" >/dev/null
  echo "[remove-device] ✓ Broker reloaded — '$USERNAME' bị revoke."
else
  echo "[remove-device] ✓ Đã xóa khỏi passwd (broker chưa chạy)."
fi
