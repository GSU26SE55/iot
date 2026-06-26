// ==================================================================
// Sprint 7 — S7-FW-01 (#73) + S7-FW-02 (#74): OTA firmware update + rollback.
//
// Luồng (OV §B6):
//   1. otaTick() định kỳ GET /api/iot-devices/firmware-check?currentVersion=FW_VERSION
//   2. Có update → persist NVS state → PUT log Downloading → stream .bin qua HTTPS
//   3. Tính SHA-256 (mbedTLS) song song → so với backend → khớp: flash + reboot;
//      lệch: abort + PUT Failed
//   4. Sau reboot vào firmware mới: health-check 2' (WiFi+NTP + PUT Success 2xx).
//      Khỏe → mark valid + PUT Success. Fail → rollback partition cũ + PUT RolledBack.
//
// Rollback dùng esp_ota_ops app-level (Arduino-esp32 KHÔNG bật bootloader rollback
// mặc định). State qua reboot lưu trong NVS namespace "iot".
//
// ⚠ otaTick() PHẢI gọi UNCONDITIONALLY trong loop (kể cả offline) — verify-mode cần
//   chạy để bắt deadline rollback ngay cả khi mất mạng. Network ops tự gate bên trong.
//
// Tham chiếu: tasksprint S7-FW-01/02, OV §B6, NI §9.1.
// ==================================================================
#pragma once

#include <cstdint>

namespace ota {

// Gọi 1 lần trong setup() SAU nvsBegin() + identityBegin().
// Đọc NVS: nếu vừa boot sau OTA (otaPend=1) → vào verify-mode (health-check 2').
// Trả false nếu OTA_ENABLED=0.
bool otaBegin();

// Gọi mỗi loop tick — UNCONDITIONALLY (không gate online).
//   - verify-mode: health-check / rollback (chạy cả khi offline để bắt deadline).
//   - bình thường: throttle OTA_CHECK_INTERVAL_MS, check+download+flash khi online.
void otaTick();

// True nếu đang trong verify-mode sau OTA (chưa confirm health).
bool otaInVerifyMode();

uint32_t otaCheckCount();      // số lần đã gọi firmware-check
uint32_t otaUpdateOkCount();   // số firmware đã apply + confirm Success

}  // namespace ota
