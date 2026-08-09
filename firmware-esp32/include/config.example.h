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

// --------- Browser setup portal ----------
// LAN: http://<ESP32-IP>:8080 or http://solar-gateway.local:8080
// Wi-Fi failure fallback: join SolarBMS-xxxxxx, then open http://192.168.4.1:8080
#define CONFIG_PORTAL_PORT            8080
#define CONFIG_PORTAL_USER            "admin"
#define CONFIG_PORTAL_PASSWORD        "change-me-now"
#define CONFIG_PORTAL_AP_PREFIX       "SolarBMS"
#define CONFIG_PORTAL_HOSTNAME        "solar-gateway"
#define CONFIG_PORTAL_AP_FALLBACK_MS  30000UL

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

// ============== Sprint 5 — Hardware Integration (S5-FW-01..07) =================

// Compile-time switch giữa mock_bms (Sprint 1) và modbus_bms (Sprint 5 real BMS).
// USE_MOCK_BMS=1 → ESP32 generate mock readings (dev workflow không cần BMS).
// USE_MOCK_BMS=0 → ESP32 đọc BMS thật qua RS485 + INA226 + DS18B20 + SHT31.
// Override qua build_flag `-DUSE_MOCK_BMS=0` trong env riêng (`pio run -e esp32-s3-real`).
#ifndef USE_MOCK_BMS
  #define USE_MOCK_BMS  1
#endif

// --------- RS485 Modbus BMS pins (WD §3) ---------
// ESP32-S3 DevKitC-1 → MAX485 XY-017:
//   GPIO17 (TX2/U2TXD) → DI (driver input)
//   GPIO18 (RX2/U2RXD) → RO (receiver output)
//   GPIO16             → DE+RE tied (direction control). Set -1 nếu module auto-direction.
//   GND chung
#define BMS_RS485_TX_PIN     17
#define BMS_RS485_RX_PIN     18
#define BMS_RS485_DE_PIN     16        // -1 = auto-direction module (KHÔNG cần GPIO)
#define BMS_RS485_BAUD       9600UL    // Daly/JBD default; JK-BMS có thể 19200

// BMS model — chọn register map preset (xem src/bms/bms_register_map.h).
//   1 = Daly Smart BMS (custom protocol — Sprint 5 chỉ stub, dùng JBD nếu thật sự cần)
//   2 = JBD BMS (LiFePO4 phổ biến — register map chuẩn Modbus RTU)
//   3 = JK-BMS (multi-cell — register map khác JBD)
//   4 = Generic (user tự định nghĩa register trong bms_register_map_custom.h)
#define BMS_MODEL            2          // JBD default

// Multi-drop config (S5-FW-03): loop unitId từ kBmsUnitIdStart đến +Count-1.
// Phải khớp số pin trong config/battery_mapping.h.
#define BMS_UNIT_ID_START    1
#define BMS_UNIT_ID_COUNT    4
#define BMS_POLL_TIMEOUT_MS  500UL      // Modbus request timeout per battery
#define BMS_POLL_RETRY       1          // retry 1 lần nếu timeout

// --------- I2C bus shared INA226 + SHT31 (WD §4.2) ---------
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define I2C_FREQUENCY_HZ     100000UL   // 100kHz standard (an toàn cable dài)

// INA226 (S5-FW-04) — current + voltage redundant
#define INA226_I2C_ADDRESS   0x40       // default; AD0=GND, AD1=GND
#define INA226_SHUNT_OHM     0.1f       // shunt resistor value (10mΩ phổ biến với pin nhỏ)
#define INA226_MAX_CURRENT_A 20.0f      // expected max current — calc current LSB

// --------- DS18B20 1-Wire (WD §4.1) ---------
#define DS18B20_GPIO         4
#define DS18B20_RESOLUTION   12         // bits: 9-12, càng cao càng chính xác + chậm
#define DS18B20_MAX_SENSORS  8          // max sensors trên bus (≥ BMS_UNIT_ID_COUNT)

// --------- SHT31 ambient (WD §4.2 cùng I2C) ---------
#define SHT31_I2C_ADDRESS    0x44       // default; ADDR=GND
#define SHT31_POLL_INTERVAL_MS 60000UL  // 1 phút (ambient không cần realtime)
// Backend route thật: `[Route("api/ambient")] + [HttpPost("readings/batch")]`
// → full path `/api/ambient/readings/batch` (verified với AmbientReadingsController.cs).
#define BACKEND_AMBIENT_PATH "/api/ambient/readings/batch"

// ============== Sprint 6 — Environmental incidents (S6-FW-01/02) =================
//
// MQ-2 (khói/gas) + water leak → report `EnvironmentalIncident` lên backend qua
// REST `POST /api/environmental-incidents` (KHÔNG qua MQTT — backend MQTT bridge chỉ
// subscribe telemetry/heartbeat/status/cmd-ack, không có topic incident).
//
// ⚠ DEPLOYMENT: ApiKey của device PHẢI có scope `EnvironmentalIngest` (bitmask=4),
//   giống SHT31 (S5-FW-06). Thiếu scope → backend trả 403.
#define BACKEND_ENV_INCIDENT_PATH "/api/environmental-incidents"

// --------- MQ-2 smoke/gas (S6-FW-01, WD §4.3) ---------
// Analog AO → ADC GPIO1 (ADC1_CH0). ⚠ MQ-2 chạy 5V → chân AO PHẢI qua bộ chia áp
// về ≤3.3V trước khi vào GPIO1, nếu không hỏng chân ADC ESP32.
#define MQ2_ENABLED              1          // 0 = tắt (vd dev mock không gắn sensor)
#define MQ2_ADC_PIN              1          // GPIO1 = ADC1_CH0
#define MQ2_THRESHOLD_RAW        2000       // 0-4095 (12-bit); raw > = phát hiện khói
                                            // Calibrate: đọc raw không khói rồi đặt cao hơn ~500
#define MQ2_WARMUP_MS            30000UL    // 30s warm-up sau cấp nguồn mới đọc tin cậy
#define MQ2_POLL_INTERVAL_MS     1000UL     // đọc ADC mỗi 1s
#define MQ2_REARM_COOLDOWN_MS    300000UL   // 5 phút tối thiểu giữa 2 report (chống spam)

// --------- Water leak (S6-FW-02, WD §4.3) ---------
// Digital DO/AO → GPIO2. Module comparator onboard: có loại ướt→HIGH, loại ướt→LOW.
// ⚠ Firmware dùng INPUT_PULLUP (chân idle = HIGH khi sensor chưa cắm) → mặc định
//   ACTIVE_HIGH=0 (ướt→LOW) để sensor rút ra không bị đọc nhầm "ướt" (false alarm).
//   Đa số module nước/mưa LM393 xuất DO LOW khi ướt → khớp mặc định này.
//   Chỉ đổi sang 1 nếu đo đồng hồ thấy module xuất HIGH khi ướt.
#define WATER_LEAK_ENABLED            1
#define WATER_LEAK_GPIO               2
#define WATER_LEAK_ACTIVE_HIGH        0        // 0 = ướt→LOW (mặc định); 1 = ướt→HIGH
#define WATER_LEAK_POLL_INTERVAL_MS   100UL
#define WATER_LEAK_REARM_COOLDOWN_MS  300000UL // 5 phút

// ============== Sprint 7 — OTA firmware update (S7-FW-01/02) =================
//
// ESP32 định kỳ GET firmware-check → nếu có bản mới: download .bin qua HTTPS →
// verify SHA-256 → ghi OTA partition → reboot. Nếu firmware mới fail health trong
// 2 phút → tự rollback về partition cũ + report (S7-FW-02).
//
// ⚠ Backend route THẬT KHÔNG có `/v1/` (verified controller — khớp provision/heartbeat).
// ⚠ ApiKey device PHẢI có scope `FirmwareCheck` (bitmask=8) — đã nằm trong EdgeDeviceDefault.
// ⚠ Partition `default_16MB.csv` đã có OTA slots (app0/app1) — không cần đổi.
// ⚠⚠ QUAN TRỌNG khi release: `FW_VERSION` (build flag -DFW_VERSION trong platformio.ini)
//    PHẢI khớp CHÍNH XÁC `Version` của firmware release đăng ký trên backend. Vì:
//    (1) firmware-check so version bằng chuỗi tuyệt đối — lệch thì offer update vô hạn;
//    (2) sau OTA, verify-mode so FW_VERSION với target — lệch → báo Failed dù flash OK.
#define OTA_ENABLED            1
#define BACKEND_FW_CHECK_PATH  "/api/iot-devices/firmware-check"       // + ?currentVersion=
#define BACKEND_FW_LOG_PATH    "/api/iot-devices/firmware-update-log/" // + {updateLogId}
#define OTA_CHECK_INTERVAL_MS  3600000UL   // poll mỗi 1h
#define OTA_HEALTH_TIMEOUT_MS  120000UL    // 2 phút health-check sau OTA → quá thì rollback
#define OTA_HTTP_TIMEOUT_MS    20000UL     // timeout check/log + connect download
// Chống boot-loop brick (S7-FW-02): FW mới boot quá N lần mà chưa confirm health
// (vd crash trong setup trước khi tới verify) → rollback ngay ở lần boot kế.
#define OTA_MAX_BOOT_ATTEMPTS  5
// 1 version fail verify N chu kỳ OTA→rollback → mark bad (phân biệt mất mạng transient
// vs binary thực sự hỏng kết nối → chống re-OTA loop vô hạn).
#define OTA_MAX_VERSION_FAILS  3

// --------- Firmware metadata ---------
#ifndef FW_VERSION
  #define FW_VERSION "unknown"
#endif
#ifndef FW_BUILD_ENV
  #define FW_BUILD_ENV "unknown"
#endif
