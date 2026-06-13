// ==================================================================
// Sprint 1 — S1-FW-01: Skeleton config (commit file này)
// Copy thành include/config.h và điền giá trị thật (config.h KHÔNG commit).
//
// Authority:
//   - tasksprint.md S1-FW-01:
//     "include/config.h chứa WIFI_SSID/PASS, BACKEND_URL, DEVICE_CODE, API_KEY placeholder"
//   - newiot.md §9.1 (firmware modules)
// ==================================================================
#pragma once

// --------- WiFi ---------- (S1-FW-01 placeholder)
#define WIFI_SSID       "your-wifi"
#define WIFI_PASS   "your-password"

// --------- NTP ---------- (S1-FW-03)
// Pool gần Việt Nam — fallback sang pool.ntp.org nếu fail.
#define NTP_SERVER_1    "vn.pool.ntp.org"
#define NTP_SERVER_2    "asia.pool.ntp.org"
#define NTP_SERVER_3    "pool.ntp.org"

// Múi giờ: UTC (lưu DB và log đều dùng UTC — KHÔNG đổi sang +7).
// Lý do: tránh skew với backend timestamp (newiot.md §12 bẫy NTP #1).
#define NTP_GMT_OFFSET_SEC      0
#define NTP_DAYLIGHT_OFFSET_SEC 0

// --------- Serial ----------
#define SERIAL_BAUD     115200

// ============== Sprint 1 — Backend ingest (S1-FW-01 placeholder) =================

// `BACKEND_URL` — gốc backend BatteryService (placeholder theo tasksprint S1-FW-01).
// Dev local:   "https://10.0.0.10:7200" (laptop chạy docker compose, ESP32 cùng LAN)
// Staging:     "https://api-dev.gsu26se55.com"
// LƯU Ý: HTTPS scheme — Sprint 1 dùng setInsecure() trong dev, Sprint 3 thay bằng CA cert.
#define BACKEND_URL         "https://10.0.0.10:7200"

// Endpoint ingest theo legacy contract (Sprint 1 MVP — backward compat NI §7.4).
// Sprint 3 (S3-FW-04) giữ URL nhưng đổi schema sang production contract.
#define BACKEND_INGEST_PATH "/api/sensor-readings/batch"

// `DEVICE_CODE` — định danh device (placeholder theo tasksprint S1-FW-01).
// Admin tạo trên web rồi đưa cho team firmware (Sprint 2 S2-BE-03).
// Sprint 1: hard-code; Sprint 2 chuyển sang đọc từ NVS (S2-FW-01).
#define DEVICE_CODE         "GW-ESP32-MVP-001"

// `API_KEY` — API key plaintext (placeholder theo tasksprint S1-FW-01).
// Format: "iotk_<base62>" (≥ 32 chars). Backend lưu hash.
// ⚠️  KHÔNG commit giá trị thật vào git — chỉ đặt trong include/config.h (đã .gitignore).
#define API_KEY             "iotk_DEV_PLACEHOLDER_REPLACE_ME"

// --------- Ingest loop --------- (S1-FW-07)
// Chu kỳ poll mock BMS + POST batch lên backend. Sprint 1 = 5s (tasksprint S1-FW-07).
// Sprint 2+ sẽ đọc từ field `pollingInterval` của provision response.
#define INGEST_INTERVAL_MS  5000UL

// Số pin mock sinh ra mỗi batch (S1-FW-04, S1-FW-07 AC = "posted 4 readings").
#define MOCK_BATTERY_COUNT  4

// HTTP timeout (ms).
#define HTTP_TIMEOUT_MS     5000

// --------- Mock BMS scenario flag --------- (S1-FW-04)
// Compile flag để switch scenario mà không sửa code. Mặc định = normal.
//
//   MOCK_SCENARIO_OVERHEAT  — temp tăng dần 30→75°C
//   MOCK_SCENARIO_LOW_SOC   — SOC giảm dần 60→5%
//
// Bỏ comment 1 dòng dưới để bật scenario (Sprint 6 alert detection test).
// #define MOCK_SCENARIO_OVERHEAT
// #define MOCK_SCENARIO_LOW_SOC

// --------- Firmware metadata ---------
#ifndef FW_VERSION
  #define FW_VERSION "unknown"
#endif
#ifndef FW_BUILD_ENV
  #define FW_BUILD_ENV "unknown"
#endif
