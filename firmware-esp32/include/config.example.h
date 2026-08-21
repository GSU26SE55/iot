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

// --------- WiFi ---------- ⚠️ CHỈ LÀ ĐƯỜNG LUI (IOT3-48)
//
// Từ Sprint IoT-3, SSID/mật khẩu thật nằm trong NVS ("wifissid"/"wifipass"), đặt bằng
// trang cấu hình `SolarGW-xxxx` hoặc Serial CLI `set wifi <ssid> <mật khẩu>`.
// Hai macro dưới chỉ được dùng khi NVS TRỐNG — tức bàn thợ, không phải hiện trường.
//
// Đã chốt dùng WiFi CỦA KHÁCH HÀNG, mà mỗi nhà một mạng: nhúng cứng ở đây nghĩa là mỗi
// thiết bị một bản build, và khách đổi mật khẩu là phải mang cáp ra tận nơi nạp lại.
//
// Để nguyên "your-wifi" là hợp lệ và nên làm — firmware sẽ tự mở trang cấu hình.
#define WIFI_SSID       "your-wifi"
#define WIFI_PASS   "your-password"

// --------- Authenticated LAN administration app ----------
// Open http://<ESP32-IP>:8080 or http://solar-gateway.local:8080 after Wi-Fi connects.
// First-time/recovery setup uses SolarGW-XXXX and the web portal on port 8080.
#define CONFIG_PORTAL_PORT            8080
#define CONFIG_PORTAL_USER            "admin"
#define CONFIG_PORTAL_PASSWORD        "12345678"
#define CONFIG_PORTAL_HOSTNAME        "solar-gateway"
#define SETUP_AP_PASSWORD             "12345678"
// Có Wi-Fi cũ nhưng mất kết nối lâu hơn ngưỡng này thì mở AP recovery.
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
#define BACKEND_URL         "https://api.solars.io.vn"

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
// Production target: one telemetry sample per second. Provision/downlink may
// request a faster interval, but main.cpp caps slower values at this target.
#define INGEST_INTERVAL_MS  1000UL

// Số pin mock sinh ra mỗi batch (S1-FW-04, S1-FW-07 AC = "posted 4 readings").
#define MOCK_BATTERY_COUNT  4

// HTTP timeout (ms).
#define HTTP_TIMEOUT_MS     5000

// Telemetry fallback must not stall the 1-second sampling loop for the generic
// 5-second timeout. Queue-first delivery keeps the payload safe while this
// shorter request probes whether the backend is reachable.
#define HTTP_INGEST_TIMEOUT_MS  1500

// --------- Mock BMS scenario flag --------- (S1-FW-04)
// Compile flag để switch scenario mà không sửa code. Mặc định = normal.
//
//   MOCK_SCENARIO_OVERHEAT  — temp tăng dần 30→75°C
//   MOCK_SCENARIO_LOW_SOC   — SOC giảm dần 60→5%
//
// Bỏ comment 1 dòng dưới để bật scenario (Sprint 6 alert detection test).
// #define MOCK_SCENARIO_OVERHEAT
// #define MOCK_SCENARIO_LOW_SOC

// ============== Sprint IoT-3 — trang cấu hình tại chỗ (IOT3-52) ==============
//
// Chưa có WiFi trong NVS (hoặc mất mạng ≥ 5 phút) thì thiết bị tự phát điểm phát sóng
// `SolarGW-XXXX` (XXXX = 4 ký tự cuối MAC) để nạp SSID/mật khẩu + mã thiết bị + API key.
//
// SETUP_AP_PASSWORD và CONFIG_PORTAL_PASSWORD được khai báo một lần ở phần
// Browser setup portal phía trên. App Android phải dùng đúng cùng giá trị đó.

// ============== Sprint 4 — MQTT broker (S4-FW-01..06) =================
//
// ⚠️ NĂM MACRO DƯỚI CHỈ LÀ ĐƯỜNG LUI (IOT3-48).
//
// Nguồn chân lý là NVS ("mqhost"/"mqport"/"mqtls"/"mquser"/"mqpass"/"mqprefix"), do
// `POST /api/iot-devices/provision` cấp lúc chạy và `config/mqtt_config.cpp` nạp lúc boot.
// Chúng chỉ được dùng khi NVS TRỐNG. Không cần — và KHÔNG NÊN — sửa cho từng thiết bị:
// mỗi thiết bị một credential mà nhúng vào firmware thì mỗi thiết bị một bản build.
//
// NGOẠI LỆ: `MQTT_USE_TLS` VẪN LÀ COMPILE-TIME THẬT (quyết định Q2 của sprint).
// `WiFiClientSecure` và `WiFiClient` là hai KIỂU khác nhau, chọn bằng `#if` lúc biên dịch.
// Trường `mqttUseTls` backend gửi xuống chỉ được lưu để ĐỐI CHIẾU và cảnh báo khi lệch.
//
// Việc duy nhất phải điền tay trong cả file này: `BACKEND_URL`, `DEVICE_CODE`, `API_KEY`
// (hoặc nạp qua CLI/trang cấu hình) và mật khẩu AP setup `SETUP_AP_PASSWORD`.

#define MQTT_BROKER_HOST    "mqtt.solars.io.vn" // Production public broker
#define MQTT_BROKER_PORT    8883             // 1883 plain | 8883 TLS
#define MQTT_USE_TLS        1                // 0 = plain (chỉ dev), 1 = TLS (production)
#define MQTT_USERNAME       "gw-esp32-mvp-001"   // backend lower-case deviceCode
#define MQTT_PASSWORD       "mqtt_DEV_PLACEHOLDER_REPLACE_ME"   // backend trả 1 lần khi create device

// Topic prefix theo overall.md §52.14 (backend MqttTopicMap). Đổi prefix
// chỉ khi backend đồng bộ — bridge subscribe wildcard `solar/#`.
// ⚠️ KHÔNG CÒN ĐƯỢC DÙNG (IOT3-39): tiền tố đầy đủ (`solar/<mã thiết bị chữ thường>`) do
//    `mqttcfg::topicPrefix()` cấp — backend trả về, hoặc suy từ deviceCode. Một nơi duy nhất
//    quyết định chuỗi này, nên lớp lỗi hoa/thường ở phía thiết bị đã bị xoá hẳn.
#define MQTT_TOPIC_PREFIX   "solar"

// Client ID — gửi cho broker. Đảm bảo unique trong cluster.
// ⚠️ KHÔNG CÒN ĐƯỢC DÙNG (IOT3-38): `mqtt_client.cpp` lấy `identity::deviceCode()` lúc chạy.
//    Giữ macro để không vỡ bản build cũ; đổi giá trị ở đây KHÔNG có tác dụng gì.
#define MQTT_CLIENT_ID      DEVICE_CODE

// Keep-alive (sec) — broker ngắt session sau ~1.5x giá trị này nếu không
// nhận PING. 10s giúp broker phát LWT sau khoảng 15s khi gateway mất điện,
// đủ nhanh cho trạng thái realtime nhưng vẫn chịu được jitter mạng ngắn.
#define MQTT_KEEPALIVE_SEC  10

// CA cert path trên LittleFS (S4-FW-01): upload qua `pio run -t uploadfs`
// sau khi `infra/mqtt/scripts/gen-certs.sh` xong. File text PEM.
#define MQTT_CA_CERT_PATH   "/ca_cert.pem"

// GH-735 — CA cert dùng chung cho HTTPS ingest + OTA (trước đây hai đường này gọi
// setInsecure(), chấp nhận MỌI chứng chỉ). Mặc định trỏ cùng file với MQTT.
#ifndef TLS_CA_CERT_PATH
#define TLS_CA_CERT_PATH    MQTT_CA_CERT_PATH
#endif

// Lối thoát cho dev: 1 = cho phép TLS không verify khi KHÔNG nạp được CA.
// PHẢI là 0 ở mọi bản build thật. Bật lên thì firmware in cảnh báo mỗi request.
// Cố ý làm nó "đắt và có dấu vết": không có lối thoát thì người ta sẽ xoá thẳng
// phần verify khi bị kẹt, và lần đó không để lại dấu gì.
#ifndef TLS_ALLOW_INSECURE
#define TLS_ALLOW_INSECURE  0
#endif

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
//   3 = JK-BMS (Modbus V1.1 — sparse block, decoder riêng ở bms_register_map.cpp)
//   4 = Generic (user tự định nghĩa register trong bms_register_map_custom.h)
//
// IOT3-02 — phần cứng thật của dự án là JK-BD6A24S10P ⇒ PHẢI là 3.
// Đường JK gate bằng `#if BMS_MODEL == 3` (modbus_bms.cpp), để 2 thì toàn bộ
// decoder JK không được biên dịch vào và Modbus đọc 0x0000 trên JK sẽ timeout.
// `platformio.ini` env `esp32-s3-real*` còn ép -DBMS_MODEL=3 để không phụ thuộc file này.
#define BMS_MODEL            3          // JK-BMS Modbus V1.1

// Multi-drop config (S5-FW-03): loop unitId từ kBmsUnitIdStart đến +Count-1.
//
// IOT3-01 — PHẢI khớp số BMS THỰC SỰ có mặt trên bus RS485, KHÔNG phải số slot
// trong config/battery_mapping.h. Mỗi unitId vắng mặt tốn 2 lượt timeout 2000ms
// (xem BMS_POLL_TIMEOUT_MS bên dưới) ⇒ đặt 4 khi chỉ có 1 BMS làm chu kỳ đo
// phình từ ~1,0s lên ~13,2s. Lắp thêm BMS thì tăng con số này lên tương ứng.
#define BMS_UNIT_ID_START    1
#define BMS_UNIT_ID_COUNT    1
// IOT3-09 — ĐÃ BỎ `BMS_POLL_TIMEOUT_MS`.
//
// Hằng đó chưa bao giờ được dùng ở đâu trong src/ (grep ra 0 kết quả) nhưng nhìn
// như đang có tác dụng, khiến mọi tính toán thời gian dựa trên nó sai 4 lần.
// Timeout THẬT là `ModbusMaster::ku16MBResponseTimeout = 2000` — `static const`
// trong ModbusMaster.h, KHÔNG đổi được nếu không fork thư viện.
// Muốn giảm thời gian chờ thì hạ BMS_POLL_RETRY hoặc sửa BMS_UNIT_ID_COUNT cho đúng.
#define BMS_POLL_RETRY       1          // retry 1 lần nếu timeout (mỗi lượt 2000ms)

// --------- I2C bus shared INA226 + SHT31 (WD §4.2) ---------
#define I2C_SDA_PIN          8
#define I2C_SCL_PIN          9
#define I2C_FREQUENCY_HZ     100000UL   // 100kHz standard (an toàn cable dài)

// --------- HS0724 microSD offline queue (SPI) ---------
// These pins must match the physical wiring. GPIO10-13 are not used because
// they can conflict with octal flash/PSRAM on ESP32-S3 N16R8 boards.
#define SD_CARD_ENABLED      1
#define SD_CS_PIN            14
#define SD_MOSI_PIN          5
#define SD_SCK_PIN           21
#define SD_MISO_PIN          7
#define SD_SPI_FREQUENCY_HZ  4000000UL

// INA226 (S5-FW-04) — current + voltage redundant
#define INA226_I2C_ADDRESS   0x40       // default; AD0=GND, AD1=GND
// IOT3-04/05 — chọn theo DÒNG ĐỈNH của JK-BD6A24S10P (100A liên tục / 200A đỉnh).
//
// Ràng buộc phần cứng: INA226 đo điện áp shunt tối đa ±81,92 mV (datasheet TI);
// thư viện chốt ở 0,08190 V. Shunt 200A/75mV = 0,375 mΩ ⇒ ở 200A ra 75 mV (đạt),
// dải đo tới 218 A, toả 3,75 W ở 100A liên tục.
// KHÔNG dùng shunt 100A/75mV (0,75 mΩ): dải đo chỉ tới 109 A — mù ở đỉnh và toả gấp đôi.
//
// Hai ràng buộc THƯ VIỆN đi kèm, thiếu cái nào cũng không init được:
//   1. robtillaart/INA226 chặn shunt < 1 mΩ ⇒ platformio.ini phải có
//      -DINA226_MINIMAL_SHUNT_OHM=0.0001, nếu không trả INA226_ERR_SHUNT_LOW.
//   2. Khối normalize của setMaxCurrentShunt chỉ tạo được LSB tối đa 5000 µA
//      ⇒ chặn maxCurrent ở 163,8 A ⇒ ina226.cpp phải truyền normalize=false,
//      nếu không trả INA226_ERR_NORMALIZE_FAILED.
#define INA226_SHUNT_OHM     0.000375f  // shunt bắt bu-lông 200A/75mV = 0,375 mΩ
#define INA226_MAX_CURRENT_A 200.0f     // dòng đỉnh JK-BD6A24S10P

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
