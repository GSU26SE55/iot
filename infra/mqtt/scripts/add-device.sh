#!/usr/bin/env bash
# ==================================================================
# Sprint 4 — Sync per-device MQTT credential vào Mosquitto passwd file.
#
# Bối cảnh (gap đã verify):
#   Backend `IotApiKeyService.GenerateMqttCredential()` lưu hash format
#     `PBKDF2$sha256$10000$<salt-b64>$<hash-b64>`
#   trong DB column `iot_devices.mqtt_password_hash`. Format KHÔNG khớp với
#   Mosquitto 2.0 (`$7$<iter>$<salt-b64>$<hash-b64>` + SHA512). Vì vậy hash
#   không paste thẳng được — phải tái-hash từ plaintext.
#
#   Plaintext password chỉ trả 1 lần qua `POST /api/admin/iot-devices` →
#   `IotDeviceCreatedDto.MqttPassword`. Admin COPY ngay, paste vào script này.
#
# Cách dùng:
#   ./infra/mqtt/scripts/add-device.sh <username> <plaintext-password>
#   ./infra/mqtt/scripts/add-device.sh gw-esp32-001 'kX9...random...'
#
# Effect:
#   - Append (hoặc cập nhật) entry `<username>:<mosquitto-hash>` vào passwd
#   - SIGHUP container mosquitto (nếu đang chạy) để reload passwd ngay,
#     KHÔNG cần restart.
#
# Audit:
#   - Plaintext password CHỈ truyền qua argv → KHÔNG ghi vào log/disk.
#   - Hash format: `$7$<iter>$<salt>$<hash>` (PBKDF2-SHA512, Mosquitto chuẩn).
#
# Roadmap (Sprint 5+):
#   - Backend tự sync qua HTTP hook (mosquitto-go-auth plugin) hoặc đổi hash
#     sang format `$7$` SHA512 + ghi thẳng passwd file qua bind mount.
# ==================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASSWD_PATH="$SCRIPT_DIR/../mosquitto/passwd"
CONTAINER_NAME="${MQTT_CONTAINER:-iot-mosquitto}"

if [[ $# -lt 1 ]]; then
  echo "Usage:" >&2
  echo "  $0 <username> <plaintext-password>     # argv mode (password vào shell history!)" >&2
  echo "  $0 <username>                          # stdin mode (recommended — không log)" >&2
  echo "  $0 <username> --stdin                  # alias stdin mode" >&2
  echo "" >&2
  echo "Example interactive:" >&2
  echo "  $0 gw-esp32-001            # prompt 'Password:' (echo off)" >&2
  echo "" >&2
  echo "Security note: argv mode lưu password vào ~/.bash_history. Cho ad-hoc, prefix space:" >&2
  echo "  ' $0 gw-esp32-001 secret'  (với HISTCONTROL=ignorespace)" >&2
  exit 1
fi

# Pre-flight: docker bắt buộc (dùng mosquitto_passwd trong image).
if ! command -v docker >/dev/null 2>&1; then
  echo "[add-device] ERROR: docker không có trong PATH" >&2
  echo "[add-device]   Cài Docker Desktop hoặc dùng mosquitto_passwd native:" >&2
  echo "[add-device]   brew install mosquitto && mosquitto_passwd -b $PASSWD_PATH <user> <pass>" >&2
  exit 1
fi

USERNAME="$1"

# Stdin mode: nếu chỉ 1 arg, hoặc arg #2 = "--stdin", prompt password (echo off).
if [[ $# -eq 1 || "${2:-}" == "--stdin" ]]; then
  # Disable echo + prompt qua stderr (giữ stdout sạch cho pipe).
  printf "Password (sẽ KHÔNG echo): " >&2
  stty -echo 2>/dev/null
  read -r PASSWORD
  stty echo 2>/dev/null
  echo "" >&2
  if [[ -z "$PASSWORD" ]]; then
    echo "[add-device] ERROR: password rỗng — abort" >&2
    exit 1
  fi
else
  PASSWORD="$2"
fi

# Validate username (Mosquitto username constraints — chỉ allow [a-z0-9-_])
if ! [[ "$USERNAME" =~ ^[a-z0-9._-]+$ ]]; then
  echo "[add-device] ERROR: username '$USERNAME' invalid (chỉ [a-z0-9._-])" >&2
  echo "[add-device] Backend lower-case deviceCode → đảm bảo deviceCode chỉ ASCII alnum + - _" >&2
  exit 1
fi

if [[ ! -f "$PASSWD_PATH" ]]; then
  echo "[add-device] ERROR: passwd file chưa tồn tại tại $PASSWD_PATH" >&2
  echo "[add-device] Chạy bootstrap.sh trước để tạo backend-bridge user." >&2
  exit 1
fi

# Add / update entry qua mosquitto_passwd trong container image (tránh cài tool host).
# Flag `-b` = batch mode (đọc password từ argv, KHÔNG prompt).
echo "[add-device] adding user='$USERNAME' to $PASSWD_PATH ..."
docker run --rm \
  -v "$(dirname "$PASSWD_PATH"):/c" \
  eclipse-mosquitto:2.0 \
  mosquitto_passwd -b "/c/$(basename "$PASSWD_PATH")" "$USERNAME" "$PASSWORD"

# Mosquitto warn nếu file world-readable — match permission của bootstrap.sh (0644 dev).
chmod 0644 "$PASSWD_PATH"

# SIGHUP reload nếu broker đang chạy. Mosquitto 2.0 reload passwd + acl ngay.
if docker ps --format '{{.Names}}' | grep -q "^${CONTAINER_NAME}$"; then
  echo "[add-device] SIGHUP container '$CONTAINER_NAME' để reload passwd ..."
  docker kill -s HUP "$CONTAINER_NAME" >/dev/null
  echo "[add-device] ✓ Broker reloaded."
else
  echo "[add-device] container '$CONTAINER_NAME' KHÔNG chạy — passwd sẽ load khi start tiếp."
fi

echo ""
echo "=============================================================="
echo "[add-device] ✓ Done"
echo "  Username:    $USERNAME"
echo "  Passwd file: $PASSWD_PATH"
echo ""
echo "  ESP32 firmware/include/config.h:"
echo "    MQTT_USERNAME  \"$USERNAME\""
echo "    MQTT_PASSWORD  \"<plaintext truyền qua argv lúc nãy>\""
echo ""
echo "  Test auth:"
echo "    mosquitto_pub -h localhost -p 1883 -u $USERNAME -P '<password>' \\"
echo "      -t solar/$USERNAME/status -m online -r"
echo "=============================================================="
