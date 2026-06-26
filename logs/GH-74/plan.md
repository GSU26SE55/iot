# Plan — GH-74: [S7-FW-02] OTA rollback khi boot mới fail health

## Metadata
- Status: PLANNING | Role: FW | Ngày: 2026-06-26
- Issue: #74 — https://github.com/GSU26SE55/iot/issues/74
- Sprint: Sprint 7
- Spec: OV §B6 #6, tasksprint S7-FW-02

## Mục tiêu
Nếu firmware mới boot lên nhưng fail health (không connect WiFi/broker trong 2 phút)
→ tự rollback về partition cũ + report `RolledBack`. AC: cố tình build firmware lỗi → rollback đúng.

## Quyết định kỹ thuật
- **App-level rollback** (KHÔNG dựa bootloader rollback của IDF) — vì Arduino-esp32
  build sẵn KHÔNG bật `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. Dùng `esp_ota_ops`:
  `esp_ota_get_next_update_partition(NULL)` (= slot firmware cũ) → `esp_ota_set_boot_partition()`
  → `esp_restart()`.
- State persist qua reboot bằng NVS (do #73 ghi lúc OTA): `otaPend, otaLogId, otaToVer, otaFromVer`.
- Health proof = WiFi connected + NTP synced + **PUT Success trả 2xx** (backend reachable).
  Dùng chính PUT Success làm bằng chứng khỏe (gộp health-check + report).

## Files (mở rộng từ #73 — cùng module ota_update)
| File | Action | Ghi chú |
|------|--------|---------|
| `src/ota/ota_update.h/.cpp` | modify | thêm `otaVerifyAfterBootBegin()` + state machine verify trong otaTick |
| `src/main.cpp` | modify | gọi verify-after-boot trong setup (đọc NVS otaPend) |

## Luồng verify-after-boot
```
setup(): đọc NVS otaPend
  otaPend == 0 → boot bình thường
  otaPend == 1 → vào "verify mode", deadline = now + OTA_HEALTH_TIMEOUT_MS (2')
                 (firmware mới đang chạy = otaToVer)

loop() (otaTick verify branch):
  nếu verify mode:
    nếu WiFi+NTP up → thử PUT update-log Success
        2xx → mark valid (esp_ota_mark_app_valid_cancel_rollback() — safe no-op nếu
              rollback disabled) → clear NVS otaPend → thoát verify mode ✅
    nếu now > deadline && chưa Success:
        PUT RolledBack (best-effort — nếu WiFi xuống thì fail, chấp nhận)
        clear NVS otaPend  (để firmware cũ boot bình thường, không loop)
        esp_ota_set_boot_partition(slot cũ) → esp_restart()  → boot firmware cũ
```

## Edge cases đã cân nhắc
- WiFi là nguyên nhân fail → PUT RolledBack cũng fail → log Pending/Installing tồn đọng
  cho admin (best-effort, không thể tránh — không có mạng thì không report được).
- Clear NVS otaPend TRƯỚC khi restart rollback → firmware cũ không lặp verify.
- Backend: handler chấp nhận RolledBack=7 nhưng KHÔNG stamp CompletedAt (gap BE đã biết) —
  không ảnh hưởng hành vi FW.

## Steps
- [ ] mở rộng ota_update: verify-after-boot state machine
- [ ] esp_ota_set_boot_partition rollback path
- [ ] wire setup() đọc otaPend
- [ ] build esp32 ×2 PASS (rollback flash thật → QA #72 trên ESP32 vật lý)

> ⚠ Giới hạn: rollback THẬT (cố tình flash firmware lỗi → tự về bản cũ) chỉ test được
> trên ESP32 vật lý. Ở đây chỉ verify compile + logic.
