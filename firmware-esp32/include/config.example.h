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
#define BACKEND_INGEST_PATH    "/api/sensor-readings/batch"

// Sprint 2 endpoints (backend route prefix `/api/iot-devices/`, KHÔNG có `/v1/`).
#define BACKEND_PROVISION_PATH "/api/iot-devices/provision"
#define BACKEND_HEARTBEAT_PATH "/api/iot-devices/heartbeat"

// Hardware revision string — backend dùng để track HW version per device.
#define HARDWARE_REVISION      "ESP32-S3-DevKitC-1-N16R8"

// `DEVICE_CODE` — định danh device (placeholder theo tasksprint S1-FW-01).
// Admin tạo trên web rồi đưa cho team firmware (Sprint 2 S2-BE-03).
// Sprint 1: hard-code; Sprint 2 chuyển sang đọc từ NVS (S2-FW-01).
//
// ⚠ Sprint 4 CONVENTION: DEVICE_CODE PHẢI LOWERCASE.
//   Backend IotApiKeyService → mqtt_username = lowercase(DeviceCode).
//   ACL `solar/%u/...` match username (lowercase). Nếu DeviceCode mixed-case,
//   backend publish `solar/{DEV_RAW}/cmd` KHÔNG match ACL → mất downlink.
//   FW boot warn nếu mismatch (mqtt_client::warnIfCaseMismatch).
#define DEVICE_CODE         "gw-esp32-mvp-001"

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

// ============== Sprint 4 — MQTT broker (S4-FW-01..06) =================
//
// MQTT-over-TLS: broker info + per-device credential (cấp lúc admin create
// device, backend trả 1 lần qua IotDeviceCreatedDto.MqttUsername/MqttPassword).
//
// Username/Password placeholder — Sprint 4 sẽ chuyển sang đọc từ NVS qua
// `set mqttuser <username>` + `set mqttpass <password>` (Serial CLI), tương
// tự apiKey/deviceCode (S2-FW-01). Hot-reload, không cần reflash.

#define MQTT_BROKER_HOST    "10.0.0.10"      // hostname / IP broker (dev: laptop chạy mosquitto)
#define MQTT_BROKER_PORT    8883             // 1883 plain | 8883 TLS
#define MQTT_USE_TLS        1                // 0 = plain (chỉ dev), 1 = TLS (production)
#define MQTT_USERNAME       "gw-esp32-mvp-001"   // backend lower-case deviceCode
#define MQTT_PASSWORD       "mqtt_DEV_PLACEHOLDER_REPLACE_ME"   // backend trả 1 lần khi create device

// Topic prefix theo overall.md §52.14 (backend MqttTopicMap). Đổi prefix
// chỉ khi backend đồng bộ — bridge subscribe wildcard `solar/#`.
#define MQTT_TOPIC_PREFIX   "solar"

// Client ID — gửi cho broker. Đảm bảo unique trong cluster.
#define MQTT_CLIENT_ID      DEVICE_CODE

// Keep-alive (sec) — broker ngắt session sau ~1.5x giá trị này nếu không
// nhận PING. Đặt 30s cho ESP32 (battery friendly + LWT trigger nhanh).
#define MQTT_KEEPALIVE_SEC  30

// CA cert path trên LittleFS (S4-FW-01): upload qua `pio run -t uploadfs`
// sau khi `infra/mqtt/scripts/gen-certs.sh` xong. File text PEM.
#define MQTT_CA_CERT_PATH   "/ca_cert.pem"

// Max packet size — buffer PubSubClient phải đủ chứa payload telemetry.
// Sprint 3 production payload ~384 bytes/reading × 3 sources/pin = 1152 + envelope ≈ 1.5KB.
// Đặt 4096 để dư cho command payload hoặc burst response.
#define MQTT_MAX_PACKET_SIZE 4096

// Reconnect / backoff broker: nếu connect fail, thử lại sau N ms.
// Sprint 3 net::Backoff dùng cho HTTP — MQTT có cơ chế riêng đơn giản hơn.
#define MQTT_RECONNECT_INTERVAL_MS 5000UL

// Fallback HTTPS — nếu MQTT publish telemetry FAIL liên tiếp `N` lần,
// chuyển transport sang HTTPS cho batch đó (S4-FW-06). Queue luôn ưu tiên
// MQTT trở lại sau khi reconnect.
#define MQTT_PUBLISH_FAIL_THRESHOLD 3

// --------- Firmware metadata ---------
#ifndef FW_VERSION
  #define FW_VERSION "unknown"
#endif
#ifndef FW_BUILD_ENV
  #define FW_BUILD_ENV "unknown"
#endif
