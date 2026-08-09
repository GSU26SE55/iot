# firmware-esp32 — ESP32-S3 firmware

Roadmap firmware:
- **Sprint 0** ✅ — skeleton + 2 sketch demo (blink, NTP sync)
- **Sprint 1** ✅ — MVP Mock HTTPS ingest (mock BMS → HTTPS POST → backend)
- **Sprint 2** ✅ — provision + heartbeat (load API key từ NVS)
- **Sprint 3** ✅ — production contract + local queue + idempotency
- **Sprint 4** ✅ — MQTT-over-TLS + LWT + downlink command + HTTPS fallback
- **Sprint 5** ← **HIỆN TẠI** — Modbus RTU BMS + INA226 + DS18B20 + SHT31 + USE_MOCK_BMS dispatcher
- Sprint 7 — OTA

## Browser setup portal

Firmware exposes a small configuration app directly from the ESP32:

- Connected to the router: `http://<ESP32-IP>:8080` or `http://solar-gateway.local:8080`
- Cannot connect to the router: join the `SolarBMS-xxxxxx` Wi-Fi network, then open `http://192.168.4.1:8080`
- Default example login: `admin` / `change-me-now` (change `CONFIG_PORTAL_PASSWORD` in `include/config.h` before flashing)

The portal can update Wi-Fi, Backend URL, device credentials and MQTT settings. Values are stored in NVS and override `config.h` on later boots. Passwords are write-only: the API only returns flags that indicate whether a password exists.

The fallback AP starts immediately after an initial Wi-Fi failure and also starts when a previously connected station stays offline for `CONFIG_PORTAL_AP_FALLBACK_MS`. The configuration server remains available through the LAN IP while normal telemetry is running.

## Sprint 4 quick reference

```
MQTT publish (FW → backend):
  solar/{deviceCode}/{batterySerial}/telemetry    retain false
  solar/{deviceCode}/status                       retain true  (LWT + connect "online")
  solar/{deviceCode}/cmd/ack                      retain false

MQTT subscribe (backend → FW):
  solar/{deviceCode}/cmd                          QoS 1
```

**Setup MQTT broker + cert (1 lần):**
```bash
cd ../infra/mqtt
./mosquitto/bootstrap.sh                                    # tạo passwd backend-bridge
./scripts/gen-certs.sh                                      # sinh CA + server cert
./scripts/add-device.sh <username> <plaintext-password>     # plaintext từ IotDeviceCreatedDto

cp mosquitto/certs/ca.crt ../../firmware-esp32/data/ca_cert.pem
cd ../../firmware-esp32
pio run -t uploadfs                                         # upload LittleFS chứa CA cert
pio run -t upload                                           # flash firmware
```

**Config bắt buộc trong `include/config.h`:**
- `DEVICE_CODE` — **PHẢI LOWERCASE** (xem `warnIfCaseMismatch` trong `src/net/mqtt_client.cpp`)
- `MQTT_BROKER_HOST/PORT/USE_TLS`
- `MQTT_USERNAME = lowercase(DEVICE_CODE)`, `MQTT_PASSWORD = plaintext` từ admin (trả 1 lần)

**Behavior matrix:**

| State | Telemetry transport | Heartbeat | Queue flush |
|-------|--------------------|-----------|-------------|
| MQTT connected, streak < 3 fail | **MQTT** (< 1s latency) | HTTPS | HTTPS |
| MQTT disconnected hoặc streak ≥ 3 fail | HTTPS + idempotency | HTTPS | HTTPS |
| MQTT reconnect | Streak reset → quay lại MQTT | HTTPS | HTTPS |

## Sprint 5 quick reference

```
USE_MOCK_BMS=1 (default dev)  → bms_source dispatch → mock_bms (no hardware)
USE_MOCK_BMS=0 (real BMS)     → bms_source dispatch → modbus_bms + ina226 + ds18b20 + sht31

Per-battery reading slots:
  primary       (sourceType=Bms=1)         — Modbus RTU từ BMS qua RS485
  redundant     (sourceType=IotGateway=2)  — INA226 V/I qua I2C
  external-temp (sourceType=IotGateway=2)  — DS18B20 1-Wire qua GPIO

Ambient (separate endpoint):
  POST /api/ambient/readings/batch
  Headers: X-Api-Key + X-Device-Code, scope EnvironmentalIngest (1<<2=4)
  Items: { siteId, time, ambientTemperature, humidity, source=1 (IotSensor), sourceDeviceId }
```

**Pin layout (WD §3 + §4):**

| Pin | Function | Notes |
|-----|----------|-------|
| GPIO17 | RS485 TX2 → MAX485 DI | 9600 baud default |
| GPIO18 | RS485 RX2 → MAX485 RO | |
| GPIO16 | RS485 DE+RE tied | -1 nếu MAX485 auto-direction |
| GPIO4 | DS18B20 1-Wire | + 4.7kΩ pull-up đến 3.3V |
| GPIO8 | I2C SDA (INA226 + SHT31) | 100kHz |
| GPIO9 | I2C SCL | |
| GPIO48 | Status LED (Sprint 3) | onboard RGB |

**Build:**
```bash
pio run -e esp32-s3-devkitc-1   # mock mode (default dev)
pio run -e esp32-s3-real        # real BMS path (USE_MOCK_BMS=0)
pio run -e example-blink        # Sprint 0 blink demo
```

**BMS model selection** trong `include/config.h`:
- `BMS_MODEL=1` Daly stub
- `BMS_MODEL=2` JBD (LiFePO4 phổ biến — default)
- `BMS_MODEL=3` JK-BMS multi-cell
- `BMS_MODEL=4` Generic (user define `kGenericBmsMap` custom)

⚠ **DEPLOYMENT — Admin tạo IoT device PHẢI:**
- Set `DeviceCode` lowercase (vd `gw-esp32-001`) — match MQTT username convention
- Set `ApiKeyScopes` bitmask include `SensorIngest(1)` + `DeviceHeartbeat(2)` + `EnvironmentalIngest(4)` + `FirmwareCheck(8)` = `15` (full)
- Set `SiteId` — required cho SHT31 ambient POST (S5-FW-06)

## Yêu cầu môi trường

```bash
# macOS
brew install platformio
# hoặc
pipx install platformio
```

Hoặc dùng VSCode extension **PlatformIO IDE**.

```bash
pio --version          # ≥ 6.x
```

## Chuẩn bị config

```bash
cd firmware-esp32
cp include/config.example.h include/config.h
# Sửa các giá trị ĐÁNH DẤU "<-- ĐỔI" trong include/config.h:
#   - WIFI_SSID, WIFI_PASS
#   - BACKEND_URL (URL backend BatteryService dev — vd "https://192.168.1.50:7200")
#   - DEVICE_CODE, API_KEY (Admin tạo trên web — Sprint 2; Sprint 1 dùng placeholder OK)
```

> `include/config.h` đã add vào `.gitignore` — không bao giờ commit WiFi password / API key.

## Build & flash

### Sprint 1 — Mock HTTPS ingest (env mặc định)

```bash
pio run                                         # build
pio run -t upload                               # flash
pio device monitor -b 115200                    # xem Serial
```

Output mong đợi (sau ~10s):
```
==============================
 Sprint 1 — MVP Mock HTTPS
 Version : 0.1.0-sprint1
 Env     : esp32-s3-devkitc-1
 Device  : GW-ESP32-MVP-001
 Backend : https://192.168.1.50:7200/api/sensor-readings/batch
 Pins    : 4 (mock)
 Interval: 5000ms
 Scenario: normal
==============================
[wifi] connecting to "MyWiFi" .....
[wifi] connected ip=192.168.1.42 rssi=-58dBm mac=...
[ntp] sync requested servers=vn.pool.ntp.org,...
[http] TLS configured (INSECURE — dev only)
[mock] initialized count=8 scenario=normal
[setup] done — entering loop()
[ntp] synced first time = 2026-06-13T08:15:42Z (epoch=...)
[http] POST /api/sensor-readings/batch → 200 (234ms, 772 bytes sent)
[ingest] batch#1 posted 4 readings (200 OK, 234ms)
[http] POST /api/sensor-readings/batch → 200 (210ms, 772 bytes sent)
[ingest] batch#2 posted 4 readings (200 OK, 210ms)
...
[stats] uptime=60s ok=12 fail=0 heap=287456 rssi=-58dBm wifi=UP
```

### Switch scenario (test alert Overheat / LowSoc — Sprint 6 dùng)

Sửa `include/config.h`, bỏ comment 1 trong 2 dòng:
```c
// #define MOCK_SCENARIO_OVERHEAT   // temp tăng dần 30 → 75°C
// #define MOCK_SCENARIO_LOW_SOC    // SOC giảm dần 60 → 5%
```
Rồi `pio run -t upload`.

### Test reconnect WiFi (S1-FW-02)

- Tắt WiFi router → Serial log:
  ```
  [wifi] DISCONNECTED (auto-reconnect armed)
  [wifi] reconnecting...
  [ingest] WiFi DOWN — skip (no queue in S1)
  ```
- Bật WiFi router lại → reconnect ≤ 30s:
  ```
  [wifi] GOT_IP  ip=192.168.1.42 rssi=-58dBm
  [ingest] batch#N posted 4 readings (200 OK, ...)
  ```

> Sprint 1 chưa có local queue — mất mạng = mất các batch trong khoảng đó. Sprint 3 (`S3-FW-01`) sẽ thêm NVS queue.

### Sprint 0 sketch (env `example-blink`)

```bash
pio run -e example-blink -t upload && pio device monitor -b 115200
```

## Test (Sprint 1)

### Unit test — `core::buildLegacyBatchPayload`

Chạy trên laptop, không cần ESP32 hay backend:

```bash
pio test -e native -f test_payload
# 6 test cases: 6 succeeded
```

### Integration test end-to-end (không cần ESP32, không cần backend thật)

Pipeline: payload builder → libcurl POST → **mock backend Python** → assert 200 + 422.

```bash
../tools/sprint1-integration-test.sh
# ✅ Sprint 1 integration test PASS (payload → mock backend → 200/422 OK)
```

Test này chứng minh JSON sinh ra **được backend chấp nhận** mà không cần
chờ Thắng có backend BatteryService dev. Nếu sau này backend strict-mode
(case-sensitive, schema khác) → test sẽ fail và ta sửa payload.cpp.

### Test với mock backend thay vì backend thật

```bash
# Terminal 1 — chạy mock backend
cd ../tools/mock-backend
python3 mock_backend.py             # HTTPS port 7200 (self-signed)
# hoặc: python3 mock_backend.py --no-tls --port 8080

# Terminal 2 — flash firmware (sửa BACKEND_URL trong include/config.h)
pio run -t upload && pio device monitor -b 115200
```

Mock backend in dashboard live + log mỗi batch. Browser mở
`https://localhost:7200/` (bỏ qua warning self-signed) — xem live counter.

## Cấu trúc thư mục

```
firmware-esp32/
├── platformio.ini              ← 3 env: esp32-s3-devkitc-1 / example-blink / native (test)
├── include/
│   ├── config.example.h        ← template (commit)
│   ├── config.h                ← cá nhân (KHÔNG commit — .gitignore)
│   ├── net/
│   │   ├── wifi_manager.h      ← S1-FW-02
│   │   ├── time_sync.h         ← S1-FW-03
│   │   └── http_client.h       ← S1-FW-06
│   ├── bms/
│   │   └── mock_bms.h          ← S1-FW-04
│   └── core/
│       └── payload.h           ← S1-FW-05
├── src/
│   ├── main.cpp                ← S1-FW-07 (loop chính)
│   ├── net/
│   │   ├── wifi_manager.cpp
│   │   ├── time_sync.cpp
│   │   └── http_client.cpp
│   ├── bms/
│   │   └── mock_bms.cpp
│   └── core/
│       └── payload.cpp
├── test/
│   └── test_payload/
│       └── test_payload.cpp    ← 6 unit test cho payload builder
└── examples/
    ├── 01-blink-hello/         ← S0 reference
    └── 02-modbus-https-arduino/← Sprint 5 reference (Modbus RTU real BMS)
```

## Library đã pin

| Lib | Version | Mục đích | Sprint dùng |
|-----|---------|----------|-------------|
| ArduinoJson | ^7.1 | JSON build/parse | **S1** |
| PubSubClient | ^2.8 | MQTT client | S4 |
| ModbusMaster | ^2.0.1 | RS485 Modbus RTU | S5 |
| OneWire | ^2.3.8 | 1-Wire bus | S5 |
| DallasTemperature | ^3.11 | DS18B20 | S5 |
| INA226 (robtillaart) | ^0.6 | Current + voltage redundant | S5 |
| Adafruit SHT31 | ^2.2.2 | Ambient temp/humidity | S5 |
| Adafruit BusIO | ^1.16 | Transitive | S5 |

## Payload contract Sprint 1 — LEGACY (NI §7.4 backward compat)

```http
POST /api/sensor-readings/batch
X-Api-Key: iotk_...
Content-Type: application/json
```

```json
{
  "items": [
    {
      "batteryAssetId": "11111111-1111-4111-8111-000000000001",
      "time":           "2026-06-13T08:15:42Z",
      "voltage":        12.51,
      "current":        -3.5,
      "temperature":    26.5,
      "socPercent":     81.5,
      "cycleCount":     100
    }
  ]
}
```

**Validation (NI §7.4):**
- voltage: `0..1000`
- temperature: `-50..150`
- socPercent: `0..100`

**Expected response (S1-FW-06 AC):** `200 OK` + `{isSuccess: true, data: {accepted: N}}`

> Sprint 3 (`S3-FW-04`) sẽ đổi sang **production contract**:
> - top-level wrapper: `deviceTimestamp`, `readings[]` (thay `items[]`)
> - per item: `batteryAssetSerial` (thay `batteryAssetId`), `sourceType`, `sensorSourceCode`, `sohPercent`, `chargingState`, `bmsErrorCode`
> - header thêm: `X-Device-Code`, `Idempotency-Key`

### ⚠️ Cần phối hợp với Backend

Trước khi flash để test thật:
1. **Seed 4 Guid** trong `kMockBatteries` (`src/bms/mock_bms.cpp`) phải khớp seed của BatteryService. Nếu Thắng dùng Guid khác → sửa mảng cho khớp.
2. **Endpoint** `POST /api/sensor-readings/batch` chấp nhận shape legacy ở trên (`#IoT2-03` của backend roadmap).
3. **Auth:** chỉ cần header `X-Api-Key` (Sprint 1). `X-Device-Code` xuất hiện ở Sprint 2.

## Troubleshooting

| Vấn đề | Cách xử |
|--------|---------|
| `pio device monitor` không có Serial | Đảm bảo board ESP32-S3 (USB-CDC) và driver USB OK |
| Build báo `config.h: No such file` | Chưa copy `config.example.h` → `config.h` |
| NTP không sync | Router chặn UDP port 123 — đổi sang hotspot điện thoại để test |
| HTTPS POST trả -1 (transport fail) | (a) Backend URL sai (kiểm tra `ping`), (b) TLS handshake fail (Sprint 1 dùng setInsecure() → bypass cert; check backend cert path) |
| HTTP 401 | Backend đã enforce API key (Sprint 2+) — cần đổi `API_KEY` |
| HTTP 400 / 422 | Sai schema — log response snippet sẽ in lỗi cụ thể |
| Hết heap | Giảm `MOCK_BATTERY_COUNT` xuống 2 hoặc 1 |
