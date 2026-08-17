# IoT System — Hardware BOM & Operation Flows

> **Document type:** Danh sách phần cứng đầy đủ (Bill of Materials) + toàn bộ luồng vận hành hệ thống IoT.
> **Hướng triển khai:** ESP32-S3 + RS485/Modbus (multi-drop) + MQTT (hybrid với HTTPS) → BatteryService.
> **Quy mô tham chiếu:** 1 site · ~4 cục pin · 2 node ESP32 · full tính năng.
> **Liên quan:** `newiot.md` (thiết kế tổng thể ESP32+MQTT), `backend/iot.md` (bản v1 Raspberry Pi), `backend/overall.md` §52/§52bis.
> **Cập nhật:** 2026-05-30

---

## Mục lục

- [A. Bảng phần cứng (BOM)](#a-bảng-phần-cứng-bom)
  - [A1. Edge compute — bộ não & đế lắp ráp](#a1-edge-compute--bộ-não--đế-lắp-ráp)
  - [A2. Giao tiếp RS485 / Modbus](#a2-giao-tiếp-rs485--modbus-đường-chính)
  - [A3. Giao tiếp CAN bus](#a3-giao-tiếp-can-bus-hỗ-trợ-bms-dùng-can)
  - [A4. Pin + BMS](#a4-pin--bms-đối-tượng-giám-sát)
  - [A5. Sensor đo độc lập (redundant / cross-source)](#a5-sensor-đo-độc-lập-redundant--cross-source-sensormismatch)
  - [A6. Cảm biến môi trường](#a6-cảm-biến-môi-trường-environmental-monitoring)
  - [A7. Hệ thống nguồn](#a7-hệ-thống-nguồn)
  - [A8. Kết nối mạng](#a8-kết-nối-mạng)
  - [A9. Dây điện & đầu nối](#a9-dây-điện--đầu-nối)
  - [A10. Đóng gói & lắp đặt](#a10-đóng-gói--lắp-đặt-thực-địa)
  - [A11. Calibration & thiết bị đo chuẩn](#a11-calibration--thiết-bị-đo-chuẩn)
  - [A12. Dụng cụ & debug](#a12-dụng-cụ--debug)
  - [A13. Hạ tầng backend](#a13-hạ-tầng-backend-dockercloud)
  - [A14. Hệ solar thật (tùy chọn)](#a14-tùy-chọn-hệ-solar-thật--demo-energy-metrics)
- [B. Luồng chạy của hệ thống](#b-luồng-chạy-của-hệ-thống)
- [C. Ánh xạ phần cứng ↔ tính năng](#c-ánh-xạ-phần-cứng--tính-năng)
- [D. 4 điều quyết định thành bại](#d-4-điều-quyết-định-thành-bại)

---

# A. BẢNG PHẦN CỨNG (BOM)

> **Lưu ý "mainboard":** Dự án ESP32 **không có mainboard riêng** như máy tính. Bản thân **ESP32-S3 DevKit chính là "mainboard"** (CPU + GPIO + WiFi + nguồn). Cần thêm **đế lắp ráp** (breadboard / perfboard / base board) để đấu nối các module.

## A1. Edge compute — bộ não & đế lắp ráp

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 1 | ESP32-S3 DevKitC-1 | 2 | N16R8 (16MB flash, 8MB PSRAM) | Node chính chạy firmware |
| 2 | ESP32-S3 dự phòng | 1 | N16R8 | Backup khi hỏng |
| 3 | Breadboard 830 điểm | 2 | — | Cắm thử mạch (giai đoạn dev) |
| 4 | Perfboard / PCB lỗ vạn năng | 3 | 7×9cm | Hàn bản cố định (demo/pilot) |
| 5 | ESP32 base board terminal vít | 2 | (tùy chọn) | Đế lắp gọn, có domino đấu dây |
| 6 | Header pin cái 2.54mm | 1 bộ | — | Cắm ESP32 lên perfboard (tháo được) |
| 7 | Thẻ microSD + module SD (SPI) | 2 | 8–16GB | Mở rộng local queue buffer mất mạng |
| 8 | Cáp USB-C | 3 | — | Nạp firmware + cấp nguồn dev |

> Vì sao 2 node: 1 node multi-drop nhiều pin, 1 node làm **nguồn đo thứ 2** cho cross-source validation (SensorMismatch).

## A2. Giao tiếp RS485 / Modbus (đường chính)

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 9 | Module RS485 auto-direction | 2 | XY-017 / XY-K485 (chip MAX485) | Chuyển TTL (ESP32) ↔ RS485 (BMS) |
| 10 | Điện trở 120Ω | 4 | 1/4W | Terminating 2 đầu mỗi bus |
| 11 | Cáp đôi xoắn có shield | 20 m | 2 lõi 24AWG shielded | Bus A/B chống nhiễu, đi xa |
| 12 | Terminal block / domino | 6 | 2–3 chân | Đấu nhiều BMS chung bus |

> Loại auto-direction (XY-017) tự đảo hướng truyền/nhận → đỡ phải code chân DE/RE, giảm lỗi.

## A3. Giao tiếp CAN bus (hỗ trợ BMS dùng CAN)

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 13 | Transceiver CAN | 2 | SN65HVD230 | ESP32 (TWAI) ↔ CAN bus |
| 14 | Điện trở 120Ω (CAN) | 2 | 1/4W | Terminating CAN bus |

## A4. Pin + BMS (đối tượng giám sát)

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 15 | Pin LiFePO4 12V + BMS RS485 | 3–4 | Daly / JBD / JK-BMS | Đối tượng đo chính |
| 16 | Pin + BMS dùng CAN | 1 | (tùy chọn) | Test đường CAN |

**Yêu cầu bắt buộc khi mua BMS:**
- [ ] Có cổng RS485/Modbus (hoặc CAN)?
- [ ] Có tài liệu register map / protocol?
- [ ] **Đổi được slave address (unitId)?** (bắt buộc cho multi-drop)
- [ ] Đọc được: voltage, current, temperature, SOC?
- [ ] Có SOH / cycle count / error code?
- [ ] Có checksum/CRC? Baud rate bao nhiêu?

> Không có register map → vẫn có BMS thật nhưng rất khó đưa data vào hệ thống.

## A5. Sensor đo độc lập (redundant / cross-source SensorMismatch)

> Phục vụ `SensorSourceCode` (primary/redundant/external-temp) + cross-source validation BMS vs IoT (§1.6.6).

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 17 | Module đo V + I (I2C) | 2–4 | INA226 | Đo voltage/current độc lập với BMS |
| 18 | Cảm biến dòng | 2 | ACS712 30A / SCT-013 | Đo current rời (redundant) |
| 19 | Cảm biến nhiệt chống nước | 6 | DS18B20 | Nhiệt thân pin (external-temp) |
| 20 | Điện trở pull-up | 4 | 4.7kΩ | Cho 1-Wire DS18B20 |

> INA226 đo cả V + I + power qua I2C — gọn cho redundant + phục vụ energy metrics (§53: charge/discharge kWh).

## A6. Cảm biến môi trường (environmental monitoring)

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 21 | Cảm biến nhiệt-ẩm | 2 | SHT31 / DHT22 | Ambient temp + humidity (AmbientReading) |
| 22 | Cảm biến khí cháy | 1 | MQ-2 / MQ-135 | GasLeak incident → EnvironmentalIncident (MQ-2 là gas sensor; báo `GasLeak`, không phải `Smoke` — NS-24 #664. `Smoke` dành cho cảm biến khói quang học tương lai) |
| 23 | Cảm biến rò nước | 1 | Water leak sensor | Water incident |

## A7. Hệ thống nguồn

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 24 | Adapter 5V | 2 | 5V / 3A | Cấp nguồn ESP32 ổn định |
| 25 | Module hạ áp DC-DC | 2 | LM2596 / MP1584 | 12V pin → 5V cho ESP32 |
| 26 | Mạch sạc + pin dự phòng | 2 | TP4056 + pin 18650 | Node sống khi cúp điện (offline báo đúng) |
| 27 | Cầu chì + giá đỡ | 1 bộ | 2–5A | Bảo vệ quá dòng |

## A8. Kết nối mạng

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 28 | Router WiFi tại site | 1 | — | ESP32 lên Internet |
| 29 | Module/Router 4G + SIM | 1 | SIM7600 / router 4G | Fallback khi không có WiFi |
| 30 | Anten WiFi/4G ngoài | 1 | (tùy chọn) | Tăng sóng trong tủ kim loại |

## A9. Dây điện & đầu nối

| STT | Vật tư | SL | Thông số / Model | Mục đích |
|-----|--------|-----|------------------|----------|
| 31 | Dây Dupont đực-đực | 40 | 2.54mm | Cắm breadboard |
| 32 | Dây Dupont đực-cái | 40 | 2.54mm | ESP32 (đực) ↔ module (cái) |
| 33 | Dây Dupont cái-cái | 40 | 2.54mm | Module ↔ module |
| 34 | Dây hook-up nhiều màu | 1 cuộn | 22AWG | Đi dây perfboard |
| 35 | Dây nguồn 2 lõi | vài m | 18AWG | Cấp nguồn dòng cao |
| 36 | Jack DC cái | 2 | 5.5×2.1mm | Cắm adapter 5V |
| 37 | Đầu cos ferrule + kìm bấm | 1 bộ | — | Đầu dây gọn vào domino |
| 38 | Ống gen co nhiệt + dây rút | 1 bộ | — | Bọc mối nối, đi dây gọn |
| 39 | Đầu nối JST / Molex | vài | (theo cổng BMS) | Nối tới cổng giao tiếp BMS |

> ⚠️ **Common ground bắt buộc:** ESP32, MAX485, các cảm biến, và BMS phải nối chung GND thì tín hiệu mới đúng. Đây là lỗi đấu dây hay gặp nhất.

## A10. Đóng gói & lắp đặt thực địa

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 40 | Hộp enclosure IP65 | 2 | — | Chống nước/bụi cho node |
| 41 | DIN rail + đế gắn | 1 | — | Lắp gọn trong tủ điện |
| 42 | Quạt tản nhiệt mini | 1 | 5V | Tránh node quá nóng |

## A11. Calibration & thiết bị đo chuẩn

> Tính năng calibration (§52.8) cần thiết bị chuẩn để lấy giá trị tham chiếu (CalibrationStandard).

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 43 | Đồng hồ vạn năng chuẩn | 1 | Fluke 87V / tương đương | Chuẩn hiệu chuẩn V/I/temp |
| 44 | Nhiệt kế tham chiếu | 1 | — | Calibration temperature |

## A12. Dụng cụ & debug

| STT | Dụng cụ | SL | Thông số / Model | Mục đích |
|-----|---------|-----|------------------|----------|
| 45 | Mỏ hàn + thiếc + flux | 1 | — | Hàn chân header, mối nối |
| 46 | Kìm tuốt/cắt + tua vít set | 1 bộ | — | Đấu dây |
| 47 | USB-RS485 adapter | 1 | — | Test BMS bằng Modbus Poll/QModMaster trên laptop |
| 48 | USB-CAN adapter | 1 | CANable | Test/sniff đường CAN |
| 49 | Logic analyzer mini | 1 | (tùy chọn) | Debug tín hiệu serial |

## A13. Hạ tầng backend (Docker/cloud — không phải linh kiện)

| STT | Thành phần | SL | Thông số / Model | Mục đích |
|-----|-----------|-----|------------------|----------|
| 50 | MQTT Broker | 1 | EMQX / Mosquitto (Docker) | Streaming realtime, LWT |
| 51 | PostgreSQL + TimescaleDB | 1 | Docker | sensor_readings, heartbeat hypertable |
| 52 | Redis | 1 | Docker | Cache, pub/sub alert |
| 53 | RabbitMQ | 1 | Docker | Event bus (anomaly→ticket→notification) |
| 54 | VPS / cloud server (hoặc k3s/minikube) | 1 | 2–4 vCPU, 4–8GB RAM | Chạy 4 microservice + broker + DB |
| 55 | Domain + TLS cert | 1 | Let's Encrypt | HTTPS + MQTT-over-TLS (8883) |
| 56 | Grafana + Prometheus | 1 | Docker | Dashboard IoT metrics |

> MVP có thể chạy tất cả bằng **Docker Compose trên laptop** — không cần mua server.

## A14. (Tùy chọn) Hệ solar thật — demo energy metrics

| STT | Thiết bị | SL | Thông số / Model | Mục đích |
|-----|----------|-----|------------------|----------|
| 57 | Tấm pin mặt trời mini | 1–2 | 10–50W | Nguồn sạc thật → có chu kỳ charge |
| 58 | Solar charge controller | 1 | PWM/MPPT | Sạc pin từ tấm solar |
| 59 | Tải giả (bóng đèn/điện trở công suất) | 1 | — | Tạo discharge → demo energy + cycle |

---

# B. LUỒNG CHẠY CỦA HỆ THỐNG

## B0. Sơ đồ tầng vật lý (dây nối)

```
   [Pin1 BMS]──┐
   [Pin2 BMS]──┤ RS485 A/B          ┌─ DS18B20 (1-Wire) ── nhiệt thân pin
   [Pin3 BMS]──┼──[MAX485]──UART──[ESP32-S3]──I2C──[INA226] ── V/I độc lập
   [Pin4 BMS]──┘   (120Ω 2 đầu)     │           └─I2C──[SHT31] ── ambient
                                     │           └─GPIO─[MQ-2/water] ── môi trường
   Nguồn: 12V──[LM2596]──5V──────────┤
   Pin dự phòng [TP4056+18650]───────┘
                                     │ WiFi/4G
                                     ▼
                          (lên MQTT broker / backend)
```

> Tất cả module **chung GND** với ESP32 và BMS.

### Bản đồ đấu nối chân (pin-to-pin)

**ESP32 ↔ MAX485 (RS485):**
```
ESP32 3V3   → MAX485 VCC
ESP32 GND   → MAX485 GND
ESP32 TX    → MAX485 DI  (data in)
ESP32 RX    → MAX485 RO  (data out)
ESP32 GPIO  → MAX485 DE+RE  (chỉ khi KHÔNG dùng loại auto-direction)
```

**MAX485 ↔ BMS (bus RS485):**
```
MAX485 A  ─── A của tất cả BMS  (cáp đôi xoắn)
MAX485 B  ─── B của tất cả BMS
+ 120Ω terminating ở 2 đầu bus
```

**ESP32 ↔ SN65HVD230 (CAN):**
```
ESP32 3V3 → VCC | ESP32 GND → GND
ESP32 GPIO(TX) → CTX | ESP32 GPIO(RX) → CRX
CANH/CANL ─── CAN bus BMS
```

**DS18B20 (nhiệt độ):**
```
VCC → 3V3 | GND → GND | DATA → GPIO  (+ 4.7kΩ kéo lên 3V3)
```

**I2C (INA226 / SHT31):**
```
VCC → 3V3 | GND → GND | SDA → ESP32 SDA | SCL → ESP32 SCL
(nhiều cảm biến I2C chung 1 cặp SDA/SCL, khác địa chỉ)
```

**Nguồn:**
```
Adapter 5V → ESP32 5V/VIN + GND
HOẶC: Pin 12V → DC-DC buck (LM2596) → 5V → ESP32
```

---

## B1. LUỒNG KHỞI ĐỘNG & PROVISION (chạy 1 lần khi setup)

```
┌─ Admin (web) ──────────────────────────────────────────────┐
│ 1. Tạo IotDevice → backend sinh deviceCode + API key (1 lần)│
│ 2. Nạp deviceCode + apiKey + WiFi + brokerUrl vào ESP32     │
└────────────────────────────────────────────────────────────┘
        │
        ▼ ESP32 boot
3. Kết nối WiFi
4. Đồng bộ giờ NTP   ← BẮT BUỘC (lệch giờ → backend reject reading)
5. POST /api/v1/iot-devices/provision  (X-Api-Key)
        │
        ▼ Backend
6. Validate key + deviceCode → set Status=Active
7. Trả configJson (pollingInterval, batteryMappings, heartbeatInterval...)
        │
        ▼ ESP32
8. Lưu config → kết nối MQTT broker (đăng ký LWT "offline")
9. Publish status="online" → bắt đầu vòng lặp chính
```

---

## B2. LUỒNG DỮ LIỆU BÌNH THƯỜNG (lặp liên tục)

```
        ┌──────────────── ESP32 (mỗi pollingInterval, vd 5s) ───────────────┐
        │                                                                   │
  (a) Đọc BMS qua RS485: loop unitId 1→2→3→4                                 │
      readHoldingRegisters() → raw voltage/current/temp/soc/soh             │
  (b) Đọc sensor phụ: DS18B20 (temp), INA226 (V/I redundant), SHT31 (ambient)│
  (c) Normalize + calibration: value = raw*scale + offset                    │
  (d) Validate sơ bộ: loại reading vô lý (V âm, temp ngoài giới hạn)         │
  (e) Gom thành 1 batch nhiều pin                                           │
  (f) Ghi vào local queue (NVS/SD) TRƯỚC                                     │
  (g) Publish MQTT: solar/{site}/{dev}/telemetry  (QoS 1)                    │
        │         │ broker/mạng OK → broker nhận <100ms                      │
        │         │ FAIL → giữ trong queue, retry backoff                   │
        └─────────┼─────────────────────────────────────────────────────────┘
                  ▼
        ┌──────── MQTT Broker (EMQX) ────────┐
        │ giữ message, push cho subscriber   │
        └──────────────┬─────────────────────┘
                       ▼
   ┌──────────── Backend: MqttBridgeBackgroundService ───────────┐
   │ (h) Nhận message topic telemetry                            │
   │ (i) Validate: clock skew ≤5min? outlier? device Active?     │
   │ (j) Apply calibration phía server                          │
   │ (k) INSERT TimescaleDB (sensor_readings hypertable)         │
   │ (l) Update IotDevice.LastSeenAt + BatteryAsset.LastReadingAt│
   │ (m) Tăng metric iot_sensor_readings_ingested_total          │
   └──────────────────────────┬──────────────────────────────────┘
                              ▼
   ┌── ThresholdCheckBackgroundService (quét reading mới) ──┐
   │ (n) So sánh với ThresholdConfig                        │
   │     → trong ngưỡng: không làm gì                       │
   │     → vượt ngưỡng: sang LUỒNG ANOMALY (B3)             │
   │ (o) Cross-source: |V_bms − V_iot|>0.5V → SensorMismatch│
   └───────────────────────────────────────────────────────┘
                              ▼
   ┌── Web/Mobile (polling 5–30s hoặc websocket) ──┐
   │ (p) GET latest reading → hiển thị dashboard    │
   │ (q) GET aggregate → vẽ chart SOH/V/I/temp      │
   └────────────────────────────────────────────────┘
```

**Độ trễ:** đo (a) → publish (g) → broker → bridge ghi DB (k): **<100ms–1s**. Dashboard cập nhật theo nhịp polling của FE.

---

## B3. LUỒNG ANOMALY → ALERT → TICKET → NOTIFICATION

```
Reading vượt threshold (vd temperature = 50°C — overheat)
        │
        ▼ ThresholdCheckBackgroundService
1. Tạo Alert (severity Warning/Critical) + dedup (tránh spam)
2. Nếu Critical → ghi outbox BatteryAnomalyDetectedEvent
        │
        ▼ RabbitMQ / MassTransit
3. TicketService consume event
   → auto-tạo Ticket (state NEW) + gán SLA theo Priority Matrix
        │
        ▼
4. NotificationService consume event
   → push (Expo) + email + SMS tới Customer/Staff
        │                              (Critical bypass quiet hours)
        ▼
5. Web/Mobile: hiện alert đỏ + ticket mới + notification
        │
        ▼ (tùy chọn 2 chiều qua MQTT)
6. Backend publish lệnh downlink solar/{dev}/cmd
   → vd: "tăng tần suất đo pin đang overheat lên 1s"
   → ESP32 nhận, đổi pollingInterval ngay lập tức
```

---

## B4. LUỒNG PHÁT HIỆN OFFLINE (2 cơ chế)

```
Cơ chế 1 — MQTT Last Will (tức thì):
  ESP32 mất kết nối đột ngột (mất điện/mạng)
   → broker phát hiện qua keep-alive (~60s)
   → tự publish status="offline" (đã đăng ký LWT lúc connect)
   → Bridge nhận → mark IotDevice.Offline NGAY → tạo alert DeviceOffline

Cơ chế 2 — Background job (backup, 2 phút/lần):
  IotDeviceOfflineDetectionBackgroundService
   → quét device Active có LastSeenAt < now − 5 phút
   → mark Offline + publish IotDeviceWentOfflineEvent
   → tạo alert DeviceOffline cho mọi battery thuộc device
   → NotificationService báo Customer/Staff
```

---

## B5. LUỒNG CALIBRATION (hiệu chuẩn sensor)

```
1. Staff đo giá trị thật bằng Fluke 87V (vd điện áp pin thực = 12.60V)
2. Đọc giá trị ESP32 báo (vd 12.45V) → lệch 0.15V
3. POST /api/v1/iot-devices/{id}/calibrations
   { sensorMetric: Voltage, offsetValue: 0.15, scaleFactor: 1.0, validUntil: +1 năm }
4. Backend lưu IotDeviceCalibration
5. Lần ingest sau: calibrated = raw*1.0 + 0.15 → đúng giá trị thật
6. CalibrationExpiryNotificationService báo Manager khi sắp hết hạn
```

---

## B6. LUỒNG OTA FIRMWARE (cập nhật từ xa)

```
1. Admin upload firmware mới: POST /api/v1/admin/iot-firmware-releases (.bin + sha256)
2. ESP32 định kỳ: GET /api/v1/iot-devices/firmware-check
   → backend trả { hasUpdate, version, downloadUrl, sha256, isRequired }
3. ESP32 tải .bin qua HTTPS (signed URL)
4. Verify sha256 → ghi vào partition OTA
5. PUT firmware-update-log/{id}  { status: Installing → Success }
6. Reboot sang firmware mới
   → nếu fail → rollback firmware cũ (lưu local) → report RolledBack → alert Admin
```

---

## B7. LUỒNG CHỐNG MẤT DỮ LIỆU (mất mạng)

```
ESP32 đọc BMS đều đặn dù mất mạng
   → mỗi batch ghi local queue (NVS/SD) + gắn Idempotency-Key
   → publish/POST fail → GIỮ trong queue, retry exponential backoff
Khi có mạng lại:
   → flush toàn bộ queue lên backend (kèm Idempotency-Key cũ)
   → backend dedup theo Idempotency-Key → KHÔNG tạo bản ghi trùng
   → message chỉ xóa khỏi queue khi backend trả 2xx
```

---

# C. ÁNH XẠ PHẦN CỨNG ↔ TÍNH NĂNG

| Tính năng / Luồng | Phần cứng tham gia |
|-------------------|--------------------|
| B2 Dữ liệu bình thường | ESP32 + MAX485 + BMS + INA226 + DS18B20 |
| B3 Anomaly → Ticket → Notification | (logic backend, không thêm hardware) |
| B4 Offline detection | UPS/TP4056 (node sống báo đúng) + broker (LWT) |
| B5 Calibration | Fluke 87V + nhiệt kế chuẩn |
| B6 OTA firmware | (chỉ ESP32 flash, không thêm hardware) |
| B7 Chống mất data | microSD + module SD (queue lớn) |
| Multi-battery (multi-drop) | thêm pin + đổi unitId, dùng chung 1 MAX485 |
| Hỗ trợ CAN | SN65HVD230 + USB-CAN |
| Cross-source SensorMismatch | BMS (nguồn 1) + INA226/DS18B20 (nguồn 2) |
| Environmental monitoring | SHT31 + MQ-2 + water sensor |
| Energy business metrics (§53) | INA226 (đo power) + solar + tải giả |
| MQTT realtime streaming | Broker EMQX (Docker) |
| Heartbeat | (ESP32 tự đọc chip temp/heap/RSSI) |

---

# D. 4 ĐIỀU QUYẾT ĐỊNH THÀNH BẠI

1. **BMS phải có RS485/CAN + register map + đổi được unitId** — thứ dễ mua sai nhất. Không có register map → không đọc được data dù pin chạy tốt.
2. **ESP32-S3 (N16R8)** chứ không phải ESP32 đời cũ — MQTT-over-TLS + nhiều sensor ngốn RAM, S3 có PSRAM đỡ crash.
3. **Cần ≥2 nguồn đo độc lập** (BMS + sensor ngoài INA226/DS18B20) thì mới demo được cross-source validation (SensorMismatch).
4. **INA226 + solar + tải giả** là bộ ba để demo energy metrics (charge/discharge kWh, cost saved, CO2) — tính năng "giá trị kinh doanh" §53.

> **Common ground:** mọi module + BMS + ESP32 phải nối chung GND — lỗi đấu dây phổ biến nhất khiến đọc sai/không đọc được.

---

## Phụ lục — Phân cấp mua sắm theo ngân sách

| Cấp | Gồm | Mục tiêu |
|-----|-----|----------|
| **Cấp 0 — MVP mock** | 1 ESP32-S3 + cáp USB + laptop | Chạy firmware mock, demo toàn bộ flow phần mềm, không cần BMS |
| **Cấp 1 — Đọc 1 pin thật** | + MAX485 + 120Ω + dây + 1 pin BMS RS485 + nguồn | Đọc BMS thật qua Modbus |
| **Cấp 2 — Multi-drop** | + 2–3 pin (đổi unitId) + domino | 1 ESP32 đọc nhiều pin |
| **Cấp 3 — Full sensor** | + INA226 + DS18B20 + SHT31 + MQ-2 + water | Redundant + cross-source + environmental |
| **Cấp 4 — Pilot hoàn chỉnh** | + enclosure + UPS + 4G + calibration + solar | Triển khai thực địa + energy metrics |
