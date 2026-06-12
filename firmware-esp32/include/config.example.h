// ==================================================================
// Sprint 0 — Skeleton config
// Copy file này thành include/config.h (KHÔNG commit config.h, chỉ commit config.example.h)
// Sprint 1 sẽ thêm: BACKEND_URL, DEVICE_CODE, API_KEY ...
// ==================================================================
#pragma once

// --------- WiFi ----------
#define WIFI_SSID       "your-wifi"
#define WIFI_PASSWORD   "your-password"

// --------- NTP ----------
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
