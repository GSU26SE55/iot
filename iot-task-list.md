# IoT Task List — Danh sách 86 task IoT-side

> **Nguồn:** trích từ `tasksprint.md` — chỉ liệt kê task **IoT track** (FW / INF / HW / FE / QA). Task Backend đã chuyển sang `backend/overall.md` Sprint IoT-2 (`#IoT2-01..38`).
> **Tổng:** 86 task / 8 sprint / 5 track.
> **Cập nhật:** 2026-06-12

---

## Thống kê nhanh

| Track | Count | Sprint xuất hiện |
|-------|-------|------------------|
| **FW** Firmware ESP32 | 37 | S0, S1, S2, S3, S4, S5, S6, S7 |
| **QA** Test/Demo/Runbook | 16 | S0, S1, S2, S3, S4, S5, S6, S7, S8 |
| **HW** Hardware | 15 | S0, S5, S8 |
| **FE** Web/Mobile | 10 | S1, S2, S6, S7, S8 |
| **INF** Infra (broker/Docker/Grafana) | 8 | S0, S4, S7 |
| **Tổng** | **86** | |

| Sprint | Tổng task | Track |
|--------|-----------|-------|
| S0 Foundation | 10 | INF 3, FW 3, HW 3, QA 1 |
| S1 MVP Mock | 10 | FW 7, FE 2, QA 1 |
| S2 Device Mgmt | 7 | FW 4, FE 2, QA 1 |
| S3 Resilience | 7 | FW 5, QA 2 |
| S4 MQTT | 12 | INF 4, FW 6, QA 2 |
| S5 Hardware | 15 | HW 7, FW 7, QA 1 |
| S6 Anomaly | 8 | FW 3, FE 2, QA 3 |
| S7 Calib/OTA/Obs | 7 | FW 2, FE 3, INF 1, QA 1 |
| S8 Pilot/Demo | 10 | HW 5, FE 1, QA 4 |

---

## Sprint 0 — Foundation & Procurement Cấp 0 (10 task)

### Infra (3)
- [ ] **S0-INF-01** — Tạo monorepo skeleton: `firmware-esp32/`, `infra/mqtt/`, `infra/db/`, `docs/`
- [ ] **S0-INF-02** — `infra/docker-compose.dev.yml` chạy Postgres+Timescale + Redis + RabbitMQ + EMQX/Mosquitto
- [ ] **S0-INF-03** — Seed Postgres extension `timescaledb`, tạo DB `battery_service_dev`

### Firmware (3)
- [ ] **S0-FW-01** — Cài PlatformIO + tạo project `firmware-esp32/` board `esp32-s3-devkitc-1` + libs (PubSubClient, ModbusMaster, ArduinoJson, OneWire, DallasTemperature, INA226, SHT31)
- [ ] **S0-FW-02** — Flash sketch "blink + Serial.println('hello')" lên ESP32-S3 thật
- [ ] **S0-FW-03** — Sketch WiFi connect + NTP sync + Serial in giờ ISO8601

### Hardware (3)
- [ ] **S0-HW-01** — Đặt mua **Cấp 0**: 1× ESP32-S3 DevKitC-1, 3× cáp USB-C, 1× breadboard
- [ ] **S0-HW-02** — Đặt order **Cấp 1**: 2× MAX485 (XY-017), 120Ω×4, 4.7kΩ×4, 20m cáp đôi xoắn shielded, domino
- [ ] **S0-HW-03** — Đặt order **BMS-RS485**: 3–4 pin LiFePO4 12V (Daly/JBD/JK-BMS) + xác nhận register map + đổi unitId

### QA (1)
- [ ] **S0-QA-01** — Tạo `docs/glossary.md` thuật ngữ (BMS, Modbus unitId, LWT, hypertable, idempotency, clock skew...)

---

## Sprint 1 — MVP Mock HTTPS (10 task)

### Firmware (7)
- [ ] **S1-FW-01** — `include/config.h` chứa `WIFI_SSID/PASS, BACKEND_URL, DEVICE_CODE, API_KEY`
- [ ] **S1-FW-02** — `src/net/wifi_manager.cpp` — connect + auto reconnect
- [ ] **S1-FW-03** — `src/net/time_sync.cpp` — NTP `configTime("pool.ntp.org")` + util `isoNow()`
- [ ] **S1-FW-04** — `src/bms/mock_bms.cpp` sinh reading giả N pin + scenario `overheat`/`low_soc`
- [ ] **S1-FW-05** — `src/core/payload.cpp` build JSON **legacy contract** `items[].batteryAssetId`
- [ ] **S1-FW-06** — `src/net/http_client.cpp` POST batch sang `/api/sensor-readings/batch`
- [ ] **S1-FW-07** — Loop chính: mỗi 5s đọc mock → build payload → POST → log

### Frontend (2)
- [ ] **S1-FE-01** — Trang "Battery list" hiển thị `LastReadingAt`, V/I/temp/SOC, refresh 5s
- [ ] **S1-FE-02** — Chart lịch sử voltage 1 giờ gần nhất

### QA (1)
- [ ] **S1-QA-01** — Quay video 60s demo "ESP32 → dashboard"

---

## Sprint 2 — Device Management (7 task)

### Firmware (4)
- [ ] **S2-FW-01** — Load `apiKey + deviceCode` từ NVS (đổi key không cần reflash)
- [ ] **S2-FW-02** — Provision flow boot: nếu Status=Provisioning gọi `/provision` 1 lần, lưu configJson về NVS
- [ ] **S2-FW-03** — Heartbeat task riêng (mỗi **60s**): Temperature, MemoryUsageMb (free heap), SignalStrengthDbm (RSSI), LocalQueueDepth
- [ ] **S2-FW-04** — Header chuẩn mọi request: `X-Api-Key`, `X-Device-Code`

### Frontend (2)
- [ ] **S2-FE-01** — Trang Admin "IoT Devices" — list + create modal hiện API key đúng 1 lần (copy-to-clipboard)
- [ ] **S2-FE-02** — Trang detail device: status (Provisioning/Active/Offline), LastSeenAt, mappings, nút rotate key

### QA (1)
- [ ] **S2-QA-01** — Test scenario: tắt ESP32 5 phút → expect alert DeviceOffline + email/push

---

## Sprint 3 — Resilience & Production Contract (7 task)

### Firmware (5)
- [ ] **S3-FW-01** — `src/queue/local_queue.cpp` — buffer NVS (FIFO, max 200 batch)
- [ ] **S3-FW-02** — Mỗi batch sinh `Idempotency-Key` (UUIDv4 hoặc deviceCode+epoch+seq)
- [ ] **S3-FW-03** — Retry exponential backoff (base 2s, max 5 phút, jitter ±20%)
- [ ] **S3-FW-04** — Đổi contract **production**: `deviceTimestamp` ISO8601, `batteryAssetSerial`, **per-source `sensorSourceCode` + `sourceType`** (BMS→Bms/primary, INA226→IotGateway/redundant, DS18B20→IotGateway/external-temp)
- [ ] **S3-FW-05** — Status LED RGB (GPIO48): xanh=online, vàng=queue có data, đỏ=mất mạng

### QA (2)
- [ ] **S3-QA-01** — Test resilience: tắt WiFi 5 phút → bật lại → đếm row DB == số batch ESP32 đã sinh
- [ ] **S3-QA-02** — Test clock skew: lệch 10 phút → reading bị reject

---

## Sprint 4 — MQTT Broker & Bridge (12 task)

### Infra (4) — ✅ Done (live verified with broker)
- [x] **S4-INF-01** — `infra/mqtt/docker-compose.yml` chạy Mosquitto (chọn thay EMQX); expose 1883 + 8883
- [x] **S4-INF-02** — Sinh CA tự ký + server cert (`infra/mqtt/scripts/gen-certs.sh`) — fix CA `basicConstraints=CA:TRUE`
- [x] **S4-INF-03** — `acl.conf` per-device: pub `solar/%u/+/telemetry`, `heartbeat`, `status`, `cmd/ack`; sub `solar/%u/cmd` — verified 4 ACL tests PASS
- [x] **S4-INF-04** — Bridge backend user `backend-bridge`: `topic readwrite solar/#` + `topic read $SYS/#`

### Firmware (6) — ✅ Done (compile + native tests; ESP32 hardware verify pending)
- [x] **S4-FW-01** — `src/net/mqtt_client.{h,cpp}` PubSubClient + WiFiClientSecure + CA cert LittleFS `/ca_cert.pem` + NTP gate
- [x] **S4-FW-02** — LWT: `willTopic=solar/{dev}/status`, payload `offline`, QoS 1, retain=true
- [x] **S4-FW-03** — Sau connect: publish `online` retained + subscribe `solar/{dev}/cmd` QoS 1
- [x] **S4-FW-04** — `ingestViaMqtt()` per-battery publish — HTTPS giữ cho queue flush + firmware-check (S7)
- [x] **S4-FW-05** — `src/cmd/command_handler.{h,cpp}` + `cmd_logic.{h,cpp}` (pure) parse downlink + publish ack `{cmdId,status,error}` — 23 native tests
- [x] **S4-FW-06** — Fallback MQTT consecutive fail ≥ `MQTT_PUBLISH_FAIL_THRESHOLD=3` → HTTPS; auto-reset on reconnect

### QA (2) — ⏳ Pending lab (cần ESP32 + broker + backend + admin dashboard)
- [ ] **S4-QA-01** — Đo latency end-to-end MQTT (publish → DB row), kỳ vọng p95 < 500ms
- [ ] **S4-QA-02** — Test LWT vs job 5 phút: rút điện → so sánh thời gian alert

### Out-of-spec gaps đã fix qua audit (11 vòng review)
- ⚠ Backend hash `PBKDF2$sha256$...` ≠ Mosquitto `$7$` → `scripts/add-device.sh` re-hash plaintext + SIGHUP
- ⚠ Backend `Mqtt:Enabled=false` default → silent fail; documented prominent trong README
- ⚠ Case mismatch deviceCode/mqtt_username phá downlink → `warnIfCaseMismatch()` + lowercase placeholder convention
- ⚠ TLS handshake trước NTP sync → `mqtt_client::mqttTick()` NTP gate
- ⚠ Root `.gitignore` `data/` over-broad ignore `firmware-esp32/data/ca_cert.pem.placeholder` → đổi `/data/` anchored
- 🔐 Security guidance consolidate trong `infra/mqtt/SECURITY.md` (file classification + threat model + audit queries)
- 🔐 `add-device.sh` thêm stdin mode (tránh password vào shell history)
- 🔐 `gen-certs.sh` cert expiry awareness + key handling warnings

---

## Sprint 5 — Hardware Integration (15 task)

### Hardware (7)
- [ ] **S5-HW-01** — Đấu ESP32 ↔ MAX485 (GPIO17 TX→DI, GPIO18 RX→RO, GPIO16 DE+RE) + chung GND
- [ ] **S5-HW-02** — Lập bảng register map cho từng model BMS (verify bằng USB-RS485 + Modbus Poll)
- [ ] **S5-HW-03** — Đổi `unitId` từng pin (1, 2, 3, 4) bằng phần mềm hãng
- [ ] **S5-HW-04** — Đấu RS485 multi-drop: 4 BMS song song A/B + 120Ω 2 đầu
- [ ] **S5-HW-05** — Đấu DS18B20 (GPIO4 + 4.7kΩ pull-up) gắn thân pin
- [ ] **S5-HW-06** — Đấu INA226 + SHT31 chung I2C (GPIO8 SDA, GPIO9 SCL)
- [ ] **S5-HW-07** — Đo điện áp pin thật bằng Fluke 87V (input cho calibration)

### Firmware (7)
- [ ] **S5-FW-01** — `src/bms/modbus_bms.cpp` đọc 1 BMS đúng register map + `bmsErrorCode`, `cycleCount`, `sohPercent`, `chargingState`; tag `sourceType=Bms(1)`, `sensorSourceCode="primary"`
- [ ] **S5-FW-02** — Điều khiển DE/RE (nếu không auto-direction): HIGH trước gửi, LOW sau nhận
- [ ] **S5-FW-03** — Multi-drop: loop unitId 1→N, gom 1 batch nhiều reading
- [ ] **S5-FW-04** — `src/sensor/ina226.cpp` V/I qua I2C → `sourceType=IotGateway(2)`, `sensorSourceCode="redundant"` (CHỈ cross-source, KHÔNG kWh)
- [ ] **S5-FW-05** — `src/sensor/ds18b20.cpp` nhiệt thân pin → `sourceType=IotGateway(2)`, `sensorSourceCode="external-temp"`
- [ ] **S5-FW-06** — `src/sensor/sht31.cpp` ambient temp+humidity → POST `/api/ambient-readings/batch` với `Source=IotSensor`, `SourceDeviceId=<DeviceCode>`
- [ ] **S5-FW-07** — Compile flag `USE_MOCK_BMS=1` chuyển giữa `mock_bms` và `modbus_bms`

### QA (1)
- [ ] **S5-QA-01** — So sánh số ESP32 vs Fluke trên 4 pin → bảng số liệu lệch (input cho calibration S7)

---

## Sprint 6 — Anomaly · Cross-source · Notification (8 task)

### Firmware (3)
- [ ] **S6-FW-01** — `src/sensor/mq2.cpp` đọc ADC GPIO1, threshold từ config → publish event MQTT khi vượt
- [ ] **S6-FW-02** — `src/sensor/water_leak.cpp` đọc GPIO2 digital; thay đổi cạnh → publish event
- [ ] **S6-FW-03** — Đảm bảo INA226 + DS18B20 reading có `sensorSourceCode` đúng để cross-source pair với BMS

### Frontend (2)
- [ ] **S6-FE-01** — Trang Alert/Ticket: filter `AnomalyType`, icon riêng cho SensorMismatch, DeviceOffline, Overheat, LowSoc, Smoke, WaterLeak
- [ ] **S6-FE-02** — Mobile push test (Expo) khi Critical

### QA (3)
- [ ] **S6-QA-01** — Kịch bản "overheat → alert → ticket → push" end-to-end < 30s
- [ ] **S6-QA-02** — Kịch bản SensorMismatch: INA226 báo lệch BMS → alert
- [ ] **S6-QA-03** — Kịch bản Smoke: trigger MQ-2 → EnvironmentalIncident + push

---

## Sprint 7 — Calibration · OTA · Observability (7 task)

### Firmware (2)
- [ ] **S7-FW-01** — `src/ota/ota_update.cpp` dùng `esp_https_ota` (download, verify sha256, write partition, reboot)
- [ ] **S7-FW-02** — Rollback: nếu boot mới fail health 2 phút → tự rollback partition cũ + report

### Frontend (3)
- [ ] **S7-FE-01** — UI Calibration: form (sensorMetric, offset, scale, validUntil, standard) + bảng list + badge "sắp hết hạn"
- [ ] **S7-FE-02** — UI upload firmware + bảng firmware-update-log per device
- [ ] **S7-FE-03** — Trang "Gateway dashboard" admin: list device + online/offline + queue depth + heartbeat sparkline + uptime %

### Infra (1)
- [ ] **S7-INF-01** — `infra/grafana/` compose + dashboard JSON 6 panel (online count, ingest rate, reject reasons, latency p95, heartbeat freshness, firmware status)

### QA (1)
- [ ] **S7-QA-01** — Quy trình calibration thực địa: đo Fluke → nhập web → reading khớp Fluke ±0.05V

---

## Sprint 8 — Pilot · Demo · Runbook (10 task)

### Hardware (5)
- [ ] **S8-HW-01** — Hàn perfboard cố định: ESP32 + MAX485 + INA226 + DS18B20 + SHT31 + MQ-2 + water + power
- [ ] **S8-HW-02** — Lắp vào enclosure IP65 + DIN rail + quạt
- [ ] **S8-HW-03** — UPS: TP4056 + 18650 + boost 5V — test cúp điện AC → ESP32 sống ≥ 30 phút
- [ ] **S8-HW-04** — 4G fallback: router 4G SIM hoặc SIM7600 — test tắt WiFi → tự chuyển 4G
- [ ] **S8-HW-05** — (Tùy chọn) Solar mini + tải giả **chỉ để nuôi node ESP32** (KHÔNG kWh/CO2 dashboard — ADR-017)

### Frontend (1)
- [ ] **S8-FE-01** — Polish UI: loading state, error message rõ, mobile responsive

### QA (4)
- [ ] **S8-QA-01** — Chạy toàn bộ kịch bản B1–B7 + checklist WD §8 → 100% pass
- [ ] **S8-QA-02** — Runbook setup ESP32: `docs/runbook-setup-node.md`
- [ ] **S8-QA-03** — Runbook xử lý sự cố: `docs/runbook-troubleshooting.md`
- [ ] **S8-QA-04** — Video demo 5 phút end-to-end

---

## Phụ lục — Cross-reference Backend (không nằm trong file này)

Toàn bộ task BE đã chuyển sang `backend/overall.md` Sprint IoT-2 — chỉ list pointer để FW/INF/QA biết dependency:

| Phase | Range | Block IoT sprint nào |
|-------|-------|----------------------|
| A — BatteryService build + seed + legacy shim | `#IoT2-01..03` | S0, S1 |
| B — Device mgmt + offline detection + Notification consumer | `#IoT2-04..13` | S2 |
| C — Production contract + resilience | `#IoT2-14..20` | S3 |
| D — MQTT bridge | `#IoT2-21..26` | S4 |
| E — Cross-source + Saga + Environmental | `#IoT2-27..31` | S6 |
| F — Calibration + OTA + Observability | `#IoT2-32..38` | S7 |

> Nếu cần chi tiết task BE → mở `backend/overall.md` tra theo `#IoT2-XX`. Owner: Thắng.

---

## Cách dùng file này

- **Theo dõi progress:** tick `[x]` khi task done. Mỗi tuần đếm tick để báo cáo.
- **Tạo issue GitHub:** copy ID task vào title (vd `S3-FW-04: Đổi contract production`).
- **Tham chiếu chi tiết:** spec đầy đủ + acceptance criteria xem `tasksprint.md`.
- **Phân công:** tag từng task theo người (vd thêm `@firmware-lead` vào dòng).
