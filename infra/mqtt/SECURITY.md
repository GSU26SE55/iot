# MQTT Security Notes — Sprint 4

Consolidated security guidance cho file/cert/credential handling trong `infra/mqtt/`.
Tách khỏi README để team review nhanh khi onboard hoặc audit.

---

## 1. File classification — secret vs public

| File | Loại | Permission | Commit? | Cần khi nào |
|------|------|-----------|---------|-------------|
| `mosquitto/certs/ca.key` | **SECRET** (CA private key) | 0600 | ❌ NO | Sinh server cert mới |
| `mosquitto/certs/server.key` | **SECRET** (Server private key) | 0600 | ❌ NO | Broker chạy TLS handshake |
| `mosquitto/certs/ca.crt` | Public (CA cert) | 0644 | ❌ NO¹ | ESP32 verify server cert |
| `mosquitto/certs/server.crt` | Public (Server cert) | 0644 | ❌ NO¹ | Broker TLS handshake |
| `mosquitto/certs/*.cnf` | Input templates (DN info) | 0600 | ❌ NO | gen-certs.sh regen |
| `mosquitto/passwd` | **SECRET** (PBKDF2 hashes) | 0644² | ❌ NO | Mosquitto auth |
| `firmware-esp32/data/ca_cert.pem` | Public (same as ca.crt) | 0644 | ❌ NO³ | ESP32 LittleFS upload |
| `firmware-esp32/data/ca_cert.pem.placeholder` | Doc hint | 0644 | ✅ YES | Setup guidance cho dev mới |

¹ Self-signed dev CA — không commit để mỗi deploy fresh
² Mosquitto warn 0644 — production set 0700 với proper uid mapping
³ Project-specific dev cert — không commit

## 2. Anti-patterns đã catch trong Sprint 4 audit

| Anti-pattern | Detected | Fix |
|--------------|----------|-----|
| Root `.gitignore` `data/` unanchored ignore luôn `firmware-esp32/data/` | Vòng 11 | Đổi `/data/` (anchored) |
| `server.key` permission 0644 (default umask) | Vòng 1 | `gen-certs.sh` chmod 0600 explicit |
| `add-device.sh` plaintext password vào shell history | Vòng 10 | Thêm stdin mode (`stty -echo`) |
| `bootstrap.sh` plaintext password ra stdout | Documented | User cần copy nhanh + clear terminal scrollback |
| User copy nhầm `server.key` → `firmware-esp32/data/` | Defensive | `gen-certs.sh` output warn "× Copy server.key hoặc ca.key vào firmware-esp32/data/" |
| Backend hash format `PBKDF2$sha256$...` không tương thích Mosquitto `$7$` | Vòng 2 | Workaround `add-device.sh` (re-hash plaintext) |
| CA cert thiếu `basicConstraints=CA:TRUE` | Vòng 2 | `gen-certs.sh` thêm `[v3_ca]` section |
| TLS handshake trước NTP sync → "cert not yet valid" | Vòng 6 | `mqtt_client.cpp` NTP gate |
| Backend `Mqtt:Enabled=false` default → silent fail | Vòng 9 | Document prominent trong README |

## 3. Threat model — dev/capstone scope

| Threat | Mitigation hiện tại | Production thêm |
|--------|--------------------|--------------------|
| Eavesdrop MQTT traffic | TLS 8883 + self-signed CA | Let's Encrypt + cert rotation cron |
| Device impersonation | Username + PBKDF2 password per-device, ACL `solar/%u/...` cô lập | mTLS — yêu cầu client cert |
| MQTT cmd spoofing (device A gửi cmd cho device B) | ACL `pattern read solar/%u/cmd` only — block write | (already strong) |
| LWT spoofing (device gửi "offline" giả) | ACL `pattern write solar/%u/status` chỉ device tự gửi của mình | (already strong) |
| Bridge user (backend-bridge) bị leak password | Password sinh ngẫu nhiên 32 chars, không log | Rotate định kỳ, vault store |
| Server key leak (đọc qua bind mount) | 0600 + .gitignore + warning ko commit | Docker secrets thay bind mount |
| CA key leak → tạo cert giả mạo broker | 0600 + .gitignore + chỉ regen khi cần | HSM hoặc PKI tách biệt |
| Backend hash format khác Mosquitto → workaround manual sync | add-device.sh | Sprint 5+ backend đổi `$7$` SHA512 hoặc mosquitto-go-auth plugin |

## 4. Cert rotation policy

```
Self-signed CA:    valid 10 years  (gen-certs.sh DAYS_CA=3650)
Self-signed server: valid 825 days (CAB Forum max)

Trigger regen:
  • notAfter < 30 ngày (cron reminder)
  • CA private key suspected leak (regen CA + redeploy all devices via OTA)
  • Server private key leak (regen server only, devices không cần update CA)
```

`gen-certs.sh` output in rõ `notAfter` date + impact của mỗi loại regen.

## 5. Key generation reproducibility

`gen-certs.sh` sinh **mới hoàn toàn** mỗi lần (random key). KHÔNG có flag `--keep-key`. Lý do:
- Capstone scope nhỏ (≤ 5 devices), regen + reupload không tốn nhiều
- Production cần key rotation strategy phức tạp hơn (Sprint 7+ với OTA)

## 6. Audit log — verify hệ thống không bị compromised

```bash
# Keys nào commit nhầm vào git history?
git log --all --diff-filter=A --name-only | grep -E '\.key$|passwd$'

# File permission audit
find infra/mqtt/mosquitto -name '*.key' -not -perm 600 -print

# Untracked secret files chưa được gitignore?
git status --ignored | grep -E '\.key|passwd|\.pem'

# Hardcoded credentials trong tracked code?
git grep -E 'password\s*=\s*"[a-zA-Z0-9]' -- ':!*.example.*' ':!*.md'
```

Tất cả 4 query trên PHẢI trả empty (hoặc chỉ trả file đã được gitignore/explicit safe).
