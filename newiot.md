# IoT Implementation Plan (v2) — ESP32 + MQTT

> **Document type:** Thiết kế hệ thống IoT thế hệ 2 — chuyển hướng từ Raspberry Pi (Python) sang **ESP32 (firmware C++)** và bổ sung **MQTT** cho streaming realtime.
> **Scope:** Toàn bộ luồng từ pin/BMS → ESP32 → broker/backend → TimescaleDB → anomaly → alert/ticket/notification → web/mobile.
> **Liên quan:** `backend/iot.md` (bản v1 cho Raspberry Pi), `backend/overall.md` §52/§52bis, ADR-016.
> **Cập nhật:** 2026-05-29

---

## 0. Tóm tắt quyết định

| Hạng mục | Quyết định |
|----------|-----------|
| Edge device | **ESP32-S3** (thay Raspberry Pi 4) |
| Giao thức đọc BMS | **RS485 / Modbus RTU** (multi-drop, hỗ trợ nhiều pin/1 node) |
| Giao thức gửi backend | **MQTT** (realtime, 2 chiều) **+ HTTPS REST** (bulk/admin) — hybrid |
| Backend | Thêm IoT module vào **BatteryService có sẵn** (KHÔNG tạo service mới) |
| Nguyên tắc triển khai | **MVP simulator-first** — chạy mock trước khi có pin thật |

> **Lưu ý quan trọng:** Code Python trong folder `iot/` (bản v1, cho Raspberry Pi) **KHÔNG chạy trên ESP32**. Chọn ESP32 nghĩa là **viết firmware mới bằng C++/Arduino**, nhưng **toàn bộ thiết kế backend §52 giữ nguyên**, chỉ khác `Model="ESP32-WROOM"` và `DeviceType=StandaloneSensor`.

---

## 1. Bốn nguyên tắc cốt lõi (không đổi so với v1)

1. **Pin/BMS không gọi backend trực tiếp — luôn qua edge device (ESP32).**
   - BMS không nói được HTTP/TLS; chỉ giao tiếp qua giao thức nhúng (Modbus/CAN/UART).
   - Tách domain phần cứng khỏi domain nghiệp vụ; ESP32 là điểm kiểm soát bảo mật duy nhất (1 device = 1 key + TLS).
   - 1 ESP32 có thể quản nhiều pin (`IotDevice.BatteryAssetIds` là mảng).

2. **ESP32 lo: đọc sensor, normalize, retry, queue local.**
   - Đọc BMS theo register map (polling — Modbus là request/response).
   - Normalize: raw → đơn vị vật lý (`value = raw*scale + offset`).
   - Queue local (NVS/LittleFS) khi mất mạng + retry exponential backoff.
   - Không xóa khỏi queue cho tới khi backend/broker xác nhận nhận.

3. **Backend lo: định danh device, validate, lưu, phát hiện anomaly.**
   - Định danh: API key per-device (lưu hash), validate `X-Device-Code`/credential MQTT, device phải Active.
   - Validate: clock skew ≤ 5 phút, reject outlier, apply calibration.
   - Lưu TimescaleDB hypertable, update `LastSeenAt`.
   - Anomaly detection → Alert → outbox event.

4. **MVP phải chạy bằng simulator/mock trước, không phụ thuộc phần cứng thật.**
   - Firmware có `mock_bms` sinh data giả → chứng minh flow end-to-end.
   - Phần cứng thật (BMS + RS485) đưa vào sau cùng.

---

## 2. So sánh ESP32 vs Raspberry Pi (lý do chọn ESP32)

| | Raspberry Pi 4 (v1, folder `iot/`) | ESP32-S3 (v2, hướng này) |
|--|-----------------------------------|--------------------------|
| Bản chất | Máy tính Linux đầy đủ | Vi điều khiển (microcontroller) |
| Code gateway | Python — dùng lại được | **Phải viết firmware C++ mới** |
| RAM | 2–8 GB | ~512 KB (S3 có PSRAM thêm) |
| Lưu trữ | thẻ SD lớn → SQLite queue mạnh | Flash nhỏ → NVS/LittleFS queue hạn chế |
| Giá | ~$50–80 | ~$5–10 |
| Điện năng | cao | rất thấp (deep sleep được) |
| Kết nối | WiFi/Ethernet | WiFi + Bluetooth tích hợp |
| Số pin/node | nhiều (10–20) | ít–vừa (multi-drop vài pin) |
| Backend | không đổi | **không đổi** (chỉ Model/DeviceType khác) |

→ ESP32 hợp với "node rẻ, ít điện, đặt rải rác mỗi điểm 1–vài pin". RPi hợp với "1 gateway tập trung đọc nhiều pin + offline buffer dài".

---

## 3. Streaming data — HTTPS polling vs MQTT

### 3.1. Bản chất luồng dữ liệu

**Không có "dòng analog vô hạn"** — mọi cảm biến số đều là **lấy mẫu rời rạc (sampling)**. ESP32 luôn phải **poll BMS** theo chu kỳ (Modbus là request/response, BMS không tự đẩy).

Điểm khác nhau nằm ở **chặng ESP32 → backend**:

| | HTTPS REST batch (v1) | MQTT (v2) |
|--|----------------------|-----------|
| Kết nối | mở/gửi/đóng mỗi lần | **thường trực (persistent)** |
| Độ trễ | 1–2 giây | **<100 ms** |
| Mô hình | request/response | **publish/subscribe** |
| 2 chiều | ❌ | ✅ backend đẩy lệnh xuống |
| Offline detection | chờ heartbeat timeout 5 phút | ✅ **tức thì (Last Will)** |
| Hạ tầng | không cần broker | **cần MQTT broker** |
| Overhead/pin | cao hơn | thấp hơn |

### 3.2. "Có streaming liên tục được chưa?" — Có (với MQTT)

- ✅ **Pipeline ESP32 → backend là dòng liên tục:** kết nối thường trực, mỗi reading publish lên backend nhận trong <100ms.
- ⚠️ **Tần suất mẫu vẫn do tốc độ poll BMS quyết định.** Poll nhanh (1–5s) + publish tức thì → cảm giác realtime hoàn toàn.
- → Với pin (biến đổi chậm theo phút), poll 1–5s + MQTT là **realtime đúng nghĩa** cho giám sát.

### 3.3. ADR-016 (đã chốt)

- **v1:** HTTPS REST batch — đơn giản, đủ cho monitoring, latency 1–2s OK.
- **v2 (MQTT):** nâng cấp khi cần latency <100ms hoặc bidirectional command. Đây chính là tài liệu này.

---

## 4. Sơ đồ tổng thể toàn bộ hoạt động

```
┌──────────────────────────── HIỆN TRƯỜNG (Site) ────────────────────────────┐
│                                                                             │
│  ┌──────────┐                          ┌─────────────────────────────┐      │
│  │ BMS pin 1│──┐                        │          ESP32 node          │     │
│  │ unitId=1 │  │  RS485 bus (A/B)       │  ┌────────────────────────┐ │      │
│  ├──────────┤  │   (Modbus RTU)         │  │ poll BMS (Modbus)      │ │      │
│  │ BMS pin 2│──┼────────[MAX485]────────┼─▶│ normalize+calibration  │ │      │
│  │ unitId=2 │  │                        │  │ local queue (mất mạng) │ │      │
│  ├──────────┤  │                        │  │ NTP sync (chống skew)  │ │      │
│  │ BMS pin N│──┘                        │  └───────────┬────────────┘ │      │
│  └──────────┘                           └──────────────┼──────────────┘      │
│                                          WiFi/4G        │                     │
└─────────────────────────────────────────────────────────┼─────────────────────┘
                                                           │
                ┌──────────────────────────────────────────┼──────────────────────────┐
                │                                           ▼                           │
       MQTT (realtime, 2 chiều)                    HTTPS REST (bulk/admin)              │
                │                                           │                           │
                ▼                                           ▼                           │
   ┌─────────────────────────┐                  ┌────────────────────────────────────┐ │
   │      MQTT BROKER         │                  │         BatteryService (C#)         │ │
   │   (EMQX / Mosquitto)     │                  │                                     │ │
   │  solar/{site}/{dev}/     │                  │  POST /api/sensor-readings/batch    │ │
   │    telemetry  (uplink)   │                  │  POST /api/v1/iot-devices/provision │ │
   │    heartbeat             │                  │  POST .../heartbeat                 │ │
   │    status (LWT offline)  │                  │  GET  .../firmware-check            │ │
   │    cmd        (downlink) │                  │  + Admin device/firmware CRUD       │ │
   └───────────┬─────────────┘                  └──────────────────┬──────────────────┘ │
               │ subscribe/push                                    │                     │
               ▼                                                   │                     │
   ┌─────────────────────────┐                                     │                     │
   │   MQTT Bridge Service    │─────────────────────────────────────┤                     │
   │   (background subscriber)│        cùng ghi vào ↓               │                     │
   │  ├ validate (skew/outlier)                                     │             │       │
   │  ├ apply calibration     │                                     │             │       │
   │  ├ insert TimescaleDB ◀──┼──── sensor_readings (hypertable) ───┼─────────────┐       │
   │  ├ update LastSeenAt     │                                     │             │       │
   │  ├ LWT → mark Offline     │                                    │             │       │
   │  └ trigger anomaly        │                                    │             ▼       │
   └───────────┬──────────────┘                                    │      ┌──────────┐   │
               │                                                    │      │TimescaleDB│  │
               ▼                                                    │      │PostgreSQL │  │
   ThresholdCheckBackgroundService                                  │      │  Redis    │  │
   ├ tạo Alert + dedup                                              │      └──────────┘   │
   └ outbox BatteryAnomalyDetectedEvent ──┐                         │                     │
                                          │ RabbitMQ / MassTransit  │                     │
              ┌───────────────────────────┼─────────────────────────┘                     │
              ▼                            ▼                            ▼                  │
      ┌──────────────┐            ┌────────────────────┐        ┌─────────────────┐        │
      │ TicketService│            │ NotificationService│        │  Web / Mobile    │       │
      │ auto-tạo     │            │ push/email/SMS     │        │ dashboard realtime│      │
      │ ticket (SLA) │            │ alert tới Customer │        │ + lịch sử + chart │      │
      └──────────────┘            └────────────────────┘        └─────────────────┘        │
                                                                                          │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

### Luồng 1 dòng

```
BMS ──Modbus──▶ ESP32 ──MQTT publish──▶ Broker ──▶ Bridge ──▶ TimescaleDB ──▶ Anomaly ──▶ Alert ──▶ Ticket/Notification ──▶ Web/Mobile
                          (HTTPS song song: provision, firmware, flush queue, admin)
```

### Hai kênh — vai trò

| Kênh | Dùng cho |
|------|----------|
| **MQTT** | telemetry realtime (<100ms), heartbeat, offline tức thì (LWT), lệnh downlink (đổi config/OTA) |
| **HTTPS REST** | provision device, tải firmware, flush queue tồn đọng khi mất mạng dài, admin CRUD |

> **Khuyến nghị hybrid:** đừng bỏ HTTPS. MQTT tốt cho luồng nhỏ-nhanh-realtime; HTTPS hợp cho file firmware lớn và flush hàng nghìn record tồn đọng. Hai kênh bổ trợ.

---

## 5. Đọc nhiều pin bằng 1 ESP32 (multi-drop RS485)

RS485 là **bus đa điểm**: nhiều BMS cùng nối 1 cặp dây A/B, mỗi BMS có **slave address (unitId)** khác nhau. ESP32 + **1 module MAX485** quét lần lượt từng unitId.

```
ESP32 ──UART──> MAX485 ──┬── BMS pin 1 (unitId=1)
                         ├── BMS pin 2 (unitId=2)
                         └── BMS pin N (unitId=N)
```

- Giới hạn chuẩn RS485: ≤ 32 thiết bị/bus (thực tế nên ≤ 10–20).
- 1 bus chỉ chung 1 giao thức + 1 baud rate.
- BMS phải **đổi được unitId** (hỏi rõ khi mua).
- Firmware loop từng unitId → tạo 1 reading/pin → gom thành 1 batch → publish 1 lần.

Config mẫu (nhiều pin):
```json
"batteryMappings": [
  { "batteryAssetSerial": "BAT-2026-001", "unitId": 1, "sensorSourceCode": "primary" },
  { "batteryAssetSerial": "BAT-2026-002", "unitId": 2, "sensorSourceCode": "primary" },
  { "batteryAssetSerial": "BAT-2026-003", "unitId": 3, "sensorSourceCode": "primary" }
]
```

---

## 6. Thiết bị cần chuẩn bị

### 6.1. Mỗi ESP32 node (tại site)

| Thiết bị | Vai trò | Ghi chú |
|----------|---------|---------|
| **ESP32-S3** (DevKit) | Bộ não node | Ưu tiên S3 vì RAM nhiều → MQTT-over-TLS ổn |
| **Module MAX485 / MAX3485** | TTL ↔ RS485 | 1 module đủ cho nhiều pin (multi-drop) |
| **Điện trở 120Ω × 2** | Terminating bus RS485 | gắn 2 đầu bus nếu dây dài |
| **Nguồn 5V ổn định** (≥2A) | Cấp điện ESP32 | ESP32 nhạy sụt áp khi WiFi phát |
| **WiFi / 4G** | Kết nối Internet | site phải có sóng |
| (tùy chọn) **UPS/pin dự phòng** | Chống mất điện node | giữ node sống khi cúp điện |
| (tùy chọn) **DS18B20** | Đo nhiệt thân pin | nếu BMS không trả temperature |

### 6.2. Pin & BMS (yêu cầu bắt buộc khi mua)

- Pin LiFePO4/NMC có **BMS tích hợp**.
- BMS phải có **cổng RS485/Modbus** + **tài liệu register map** (địa chỉ thanh ghi voltage/current/temp/soc/soh).
- BMS phải **đổi được slave address (unitId)** → multi-drop.
- Biết **baud rate** + có **CRC/checksum**.
- Tiêu chí hỏi khi mua BMS:
  - [ ] Có RS485/Modbus không?
  - [ ] Có register map/protocol không?
  - [ ] Đọc được pack voltage / current / temperature / SOC?
  - [ ] Có SOH / cycle count / error code?
  - [ ] Protocol có checksum/CRC?
  - [ ] Baud rate? Đổi unitId được không?

> Không có register map → vẫn có BMS thật nhưng rất khó đưa data vào hệ thống.

### 6.3. Hạ tầng backend (server/cloud)

| Thành phần | Vai trò |
|-----------|---------|
| **MQTT Broker** (EMQX hoặc Mosquitto, Docker) | Trung tâm pub/sub realtime |
| **PostgreSQL + TimescaleDB** | Lưu sensor_readings (hypertable), device, heartbeat |
| **Redis** | Cache, pub/sub real-time alert |
| **RabbitMQ** | Event bus giữa microservices |
| Server/VM hoặc K8s | Chạy BatteryService + broker + DB |
| **TLS cert** | MQTT-over-TLS (8883) + HTTPS |

### 6.4. MVP (không cần phần cứng thật)

- Chỉ cần **1 ESP32** (hoặc mock publisher trên laptop) + broker + backend.
- Firmware gửi reading **giả** (`mock_bms`) → chứng minh toàn bộ flow trước khi có pin thật.

---

## 7. Backend — thêm vào BatteryService (§52)

### 7.1. Data model (migration `AddIotDeviceManagement`)

| Entity | Vai trò chính |
|--------|--------------|
| `IotDevice` | DeviceCode, DeviceType (Gateway=1/StandaloneSensor=2), Model, FirmwareVersion, MacAddress, SiteId, Status (Provisioning=1/Active=2/Offline=3/Decommissioned=4), ApiKeyId, LastSeenAt, BatteryAssetIds (jsonb), ConfigJson |
| `IotDeviceHeartbeat` | Hypertable, retention 30 ngày: Cpu, MemoryUsageMb, DiskFreeMb, Temperature, ConnectedSensorCount, LocalQueueDepth, IpAddress, SignalStrengthDbm |
| `IotDeviceCalibration` | SensorMetric (Voltage=1/Current=2/Temperature=3), OffsetValue, ScaleFactor, CalibratedAt, CalibratedByUserId, CalibrationStandard, ValidUntil |
| `IotFirmwareRelease` | Version, DeviceModel, Channel (Stable=1/Beta=2), FileId, Sha256, ReleaseNotes, IsRequired, ReleasedAt |
| `IotFirmwareUpdateLog` | FromVersion, ToVersion, Status (Pending/Downloading/Installing/Success/Failed/RolledBack), timestamps |

Bổ sung vào `SensorReading`:
- `SourceType` enum (Bms=1, IotGateway=2, External=3) — NOT NULL default IotGateway (**B9**).
- `SensorSourceCode` string(20)? — "primary"/"redundant"/"external-temp".

Công thức calibration: `calibrated_value = raw_value * scale_factor + offset_value`.

### 7.2. API key per-device

- Sinh key khi admin tạo device, **chỉ lưu hash** (không plaintext).
- Key chỉ hiện **1 lần** khi tạo/provision.
- Hỗ trợ **rotate/revoke**.
- Scope: `sensor.ingest` + `device.heartbeat`.
- (MQTT) cấp thêm credential MQTT per-device, gắn với `IotDevice`.

### 7.3. Endpoints

```
# Admin
POST   /api/v1/admin/iot-devices                  (tạo device + sinh key)
GET    /api/v1/admin/iot-devices?status=&siteId=
GET    /api/v1/admin/iot-devices/{id}
PUT    /api/v1/admin/iot-devices/{id}/config       (push config update)
DELETE /api/v1/admin/iot-devices/{id}              (decommission)
POST   /api/v1/admin/iot-firmware-releases
GET    /api/v1/admin/iot-firmware-releases

# Device-side (X-Api-Key + X-Device-Code)
POST   /api/v1/iot-devices/provision               (one-time)
POST   /api/v1/iot-devices/heartbeat
GET    /api/v1/iot-devices/firmware-check
PUT    /api/v1/iot-devices/firmware-update-log/{id}
POST   /api/sensor-readings/batch                  (cập nhật contract)

# Calibration
POST   /api/v1/iot-devices/{id}/calibrations       (Staff/Admin)
GET    /api/v1/iot-devices/{id}/calibrations
GET    /api/v1/iot-devices/calibrations-expiring?within=30d

# Monitoring
GET    /api/v1/iot-devices/{id}/heartbeat-history?from=&to=
GET    /api/v1/iot-devices/{id}/uptime-stats
```

### 7.4. Ingest contract (production)

```http
POST /api/sensor-readings/batch
X-Api-Key: <device-key>
X-Device-Code: GW-ESP32-001
Idempotency-Key: <uuid sinh trên ESP32>
Content-Type: application/json

{
  "deviceTimestamp": "2026-05-29T10:15:30Z",
  "readings": [
    {
      "batteryAssetSerial": "BAT-2026-001",
      "time": "2026-05-29T10:15:30Z",
      "voltage": 12.6, "current": -5.2, "temperature": 35.4,
      "socPercent": 78.5, "cycleCount": 120, "sohPercent": 94.2,
      "chargingState": 3, "bmsErrorCode": null,
      "sensorSourceCode": "primary", "sourceType": 2
    }
  ]
}
```

**Validation bắt buộc:**
- `deviceTimestamp` lệch server ≤ 5 phút (clock skew → reject + log + alert).
- voltage không âm, không vượt hard-limit (>1000V reject).
- temperature trong giới hạn vật lý (-50°C..150°C).
- socPercent 0–100; sohPercent 0–100 nếu có.
- BmsErrorCode ≤ 64 ký tự nếu có.
- Device phải Active + có quyền với battery (mapping).
- Apply calibration trước khi insert.

**Backward compatibility (MVP):** vẫn chấp nhận payload cũ dùng `items[].batteryAssetId` (legacy mode) để demo nhanh.

### 7.5. Background jobs

- `IotDeviceOfflineDetectionBackgroundService` (2 phút/lần): Active + `LastSeenAt < now-5min` → Offline → publish `IotDeviceWentOfflineEvent` + tạo alert `DeviceOffline` cho battery liên quan.
- (MQTT) LWT `status=offline` → mark Offline **tức thì** (nhanh hơn job 5 phút).
- `CalibrationExpiryNotificationService`: alert Manager khi calibration sắp hết hạn.

### 7.6. Cross-source validation (B10, §1.6.6)

Khi 1 battery có cả BMS và IoT cùng đẩy reading trong cửa sổ 60s:
- |Voltage_bms − Voltage_iot| > 0.5V → anomaly `SensorMismatch` (Warning).
- |Temperature_bms − Temperature_iot| > 5°C → `SensorMismatch` (Warning).
- `AnomalyTypeEnum.SensorMismatch = 15`.

### 7.7. Observability (metrics)

```
iot_device_heartbeats_total{device_id, status}
iot_devices_online_count                     gauge
iot_devices_offline_total                    counter
iot_sensor_readings_ingested_total{device_id}
iot_sensor_readings_rejected_total{reason=clock_drift|sensor_outlier|...}
iot_firmware_updates_total{from_version, to_version, status}
```

---

## 8. MQTT — viết như thế nào, ở đâu

MQTT sống ở **3 nơi**, bản chất khác nhau:

| Nơi | Vai trò | Viết code? |
|-----|---------|-----------|
| **MQTT Broker** (EMQX/Mosquitto) | Trung tâm trung chuyển message | ❌ KHÔNG viết — chỉ deploy + config |
| **ESP32 firmware** | Client publisher (telemetry) + subscriber (cmd) | ✅ Viết C++ (PubSubClient) |
| **Backend bridge** | Client subscriber (telemetry) + publisher (cmd) | ✅ Viết C# (MQTTnet) |

> "Viết MQTT" = viết **client ở 2 đầu**. Broker là phần mềm có sẵn (như database), chỉ dựng + cấu hình.

### 8.1. Topic design

```
solar/{siteId}/{deviceCode}/telemetry   ← ESP32 publish reading (uplink)
solar/{deviceCode}/heartbeat            ← ESP32 publish trạng thái thiết bị
solar/{deviceCode}/status               ← Last Will: "online"/"offline" tự động
solar/{deviceCode}/cmd                  ← Backend publish lệnh xuống (downlink)
solar/{deviceCode}/cmd/ack              ← ESP32 báo đã thực thi lệnh
```

**Last Will & Testament (LWT):** ESP32 đăng ký với broker "nếu tôi mất kết nối, publish `offline` lên `status`". Broker tự phát hiện rớt (keep-alive ~60s) → backend biết NGAY, không chờ job 5 phút.

### 8.2. Broker — deploy (`infra/mqtt/`)

```yaml
# infra/mqtt/docker-compose.yml
services:
  mqtt:
    image: eclipse-mosquitto:2
    ports:
      - "1883:1883"    # MQTT thường (dev)
      - "8883:8883"    # MQTT-over-TLS (production)
    volumes:
      - ./mosquitto.conf:/mosquitto/config/mosquitto.conf
      - ./certs:/mosquitto/certs
```

```conf
# infra/mqtt/mosquitto.conf
listener 8883
cafile   /mosquitto/certs/ca.crt
certfile /mosquitto/certs/server.crt
keyfile  /mosquitto/certs/server.key
allow_anonymous false
password_file /mosquitto/config/passwd
```

### 8.3. ESP32 publisher (`firmware-esp32/src/net/mqtt_client.cpp`)

Thư viện: `PubSubClient` + `WiFiClientSecure`.

```cpp
WiFiClientSecure net;
PubSubClient mqtt(net);

void mqttSetup() {
  net.setCACert(CA_CERT);
  mqtt.setServer(BROKER_HOST, 8883);
  mqtt.setCallback(onCommand);          // nhận lệnh downlink
}

void mqttConnect() {
  String willTopic = "solar/" + DEVICE_CODE + "/status";
  while (!mqtt.connected()) {
    mqtt.connect(DEVICE_CODE.c_str(), MQTT_USER, MQTT_PASS,
                 willTopic.c_str(), 1, true, "offline");   // Last Will
  }
  mqtt.publish(willTopic.c_str(), "online", true);
  mqtt.subscribe(("solar/" + DEVICE_CODE + "/cmd").c_str());
}

void publishTelemetry(const SensorReading readings[], int n) {
  StaticJsonDocument<1024> doc;
  doc["deviceTimestamp"] = isoNow();     // NTP time
  JsonArray arr = doc.createNestedArray("readings");
  for (int i = 0; i < n; i++) {
    JsonObject r = arr.createNestedObject();
    r["batteryAssetSerial"] = readings[i].serial;
    r["voltage"]     = readings[i].voltage;
    r["current"]     = readings[i].current;
    r["temperature"] = readings[i].temperature;
    r["socPercent"]  = readings[i].soc;
  }
  char buf[1024]; serializeJson(doc, buf);
  String topic = "solar/" + SITE_ID + "/" + DEVICE_CODE + "/telemetry";
  mqtt.publish(topic.c_str(), buf, false);
}

void onCommand(char* topic, byte* payload, unsigned int len) {
  // parse JSON: {"action":"set_interval","value":5} / trigger OTA / calibration
}
```

Vòng lặp chính (`firmware-esp32/src/main.cpp`):
```cpp
void loop() {
  if (!mqtt.connected()) mqttConnect();
  mqtt.loop();                            // giữ kết nối + xử lý lệnh

  if (millis() - lastPoll > POLL_MS) {
    int n = readBms(readings);            // poll BMS qua Modbus
    publishTelemetry(readings, n);        // stream lên broker NGAY
    lastPoll = millis();
  }
  if (millis() - lastHb > HB_MS) {
    publishHeartbeat();                   // chip temp, free heap, RSSI
    lastHb = millis();
  }
}
```

### 8.4. Backend subscriber (`BatteryService.Infrastructure/Mqtt/MqttBridgeBackgroundService.cs`)

Thư viện: `MQTTnet`.

```csharp
public class MqttBridgeBackgroundService : BackgroundService
{
    private readonly IServiceScopeFactory _scopeFactory;
    private IManagedMqttClient _client = null!;

    protected override async Task ExecuteAsync(CancellationToken ct)
    {
        var factory = new MqttFactory();
        _client = factory.CreateManagedMqttClient();

        var options = new ManagedMqttClientOptionsBuilder()
            .WithClientOptions(new MqttClientOptionsBuilder()
                .WithTcpServer("mqtt-broker", 8883)
                .WithTls()
                .WithCredentials("backend-bridge", "secret")
                .Build())
            .Build();

        await _client.SubscribeAsync("solar/+/+/telemetry");   // + = wildcard
        await _client.SubscribeAsync("solar/+/heartbeat");
        await _client.SubscribeAsync("solar/+/status");

        _client.ApplicationMessageReceivedAsync += async e =>
        {
            var topic   = e.ApplicationMessage.Topic;
            var payload = e.ApplicationMessage.ConvertPayloadToString();

            using var scope = _scopeFactory.CreateScope();
            var mediator = scope.ServiceProvider.GetRequiredService<IMediator>();

            if (topic.EndsWith("/telemetry"))
            {
                var dto = JsonSerializer.Deserialize<SensorBatchDto>(payload);
                await mediator.Send(new SensorReadingBatchIngestCommand { /* ... */ });
            }
            else if (topic.EndsWith("/status") && payload == "offline")
            {
                await mediator.Send(new IotDeviceMarkOfflineCommand { /* deviceCode */ });
            }
        };

        await _client.StartAsync(options);
        await Task.Delay(Timeout.Infinite, ct);
    }
}
```

Đăng ký DI (`ManageDependencyInjection.cs`):
```csharp
services.AddHostedService<MqttBridgeBackgroundService>();
```

Gửi lệnh downlink:
```csharp
await _client.EnqueueAsync(new MqttApplicationMessageBuilder()
    .WithTopic($"solar/{deviceCode}/cmd")
    .WithPayload("""{"action":"set_interval","value":5}""")
    .Build());
```

> **Tái dùng:** bridge gọi đúng `SensorReadingBatchIngestCommand` đã có — logic validate/insert/anomaly không viết lại, chỉ thay "nguồn vào" từ HTTP sang MQTT.

---

## 9. Source tree code

Có **2 codebase** + **broker config**.

### 9.1. Firmware ESP32 — codebase MỚI (PlatformIO/Arduino C++)

```
firmware-esp32/
├── platformio.ini              # board=esp32-s3; libs: PubSubClient, ModbusMaster, ArduinoJson
├── include/
│   └── config.h                # WiFi, brokerUrl, backendUrl, deviceCode, apiKey
├── data/                       # LittleFS: CA cert, config runtime
│   └── ca_cert.pem
├── src/
│   ├── main.cpp                # setup() + loop(): poll → publish → flush → heartbeat
│   ├── config/
│   │   ├── config.cpp          # load config + batteryMappings[]{serial, unitId}
│   │   └── battery_mapping.h
│   ├── net/
│   │   ├── wifi_manager.cpp     # kết nối WiFi + reconnect
│   │   ├── time_sync.cpp        # NTP — BẮT BUỘC (chống clock skew)
│   │   ├── mqtt_client.cpp      # connect broker + LWT + publish/subscribe
│   │   └── http_client.cpp      # HTTPS: provision, firmware download, flush queue
│   ├── bms/
│   │   ├── bms_adapter.h        # interface đọc BMS
│   │   ├── modbus_bms.cpp       # đọc RS485: loop từng unitId → 1 reading/pin
│   │   └── mock_bms.cpp         # sinh data giả (MVP; scenario overheat/low_soc)
│   ├── core/
│   │   ├── reading.h            # struct SensorReading
│   │   ├── calibration.cpp      # value = raw*scale + offset
│   │   ├── validation.cpp       # loại reading vô lý trước khi gửi
│   │   └── payload.cpp          # build JSON (telemetry + heartbeat)
│   ├── queue/
│   │   └── local_queue.cpp      # buffer NVS/LittleFS khi mất mạng + retry backoff
│   ├── telemetry/
│   │   └── heartbeat.cpp        # chip temp, free heap, RSSI, queue depth
│   ├── cmd/
│   │   └── command_handler.cpp  # nhận lệnh downlink (đổi config, OTA)
│   └── ota/
│       └── ota_update.cpp       # esp_https_ota
└── test/
    └── test_payload.cpp
```

### 9.2. Backend — THÊM vào BatteryService có sẵn (KHÔNG tạo project mới)

```
backend/services/BatteryService/
├── BatteryService.Domain/
│   ├── Entities/
│   │   ├── IotDevice.cs                       # MỚI
│   │   ├── IotDeviceHeartbeat.cs              # MỚI (hypertable)
│   │   ├── IotDeviceCalibration.cs            # MỚI
│   │   ├── IotFirmwareRelease.cs              # MỚI
│   │   └── IotFirmwareUpdateLog.cs            # MỚI
│   └── Enums/
│       ├── IotDeviceTypeEnum.cs               # Gateway=1, StandaloneSensor=2
│       ├── IotDeviceStatusEnum.cs             # Provisioning/Active/Offline/Decommissioned
│       └── SensorReadingSourceTypeEnum.cs     # Bms=1, IotGateway=2, External=3 (B9)
│
├── BatteryService.Application/
│   ├── Commands/
│   │   ├── IotDeviceCreate/                   # admin tạo device + sinh API key
│   │   ├── IotDeviceProvision/
│   │   ├── IotDeviceHeartbeat/
│   │   ├── IotDeviceUpdateConfig/
│   │   ├── IotDeviceMarkOffline/              # dùng cho LWT
│   │   ├── IotDeviceCalibrationCreate/
│   │   ├── IotFirmwareReleaseCreate/
│   │   └── SensorReadingBatchIngest/          # CẬP NHẬT: X-Device-Code, Idempotency, deviceTimestamp
│   ├── Queries/
│   │   ├── IotDeviceGetList/  IotDeviceGetById/
│   │   ├── IotDeviceHeartbeatHistory/  IotDeviceUptimeStats/
│   │   ├── IotDeviceFirmwareCheck/
│   │   └── IotCalibrationsExpiring/
│   ├── DTOs/Response/                          # IotDeviceDTO, HeartbeatDTO...
│   └── Interfaces/
│       └── IMqttBridgePublisher.cs            # publish lệnh downlink
│
├── BatteryService.Infrastructure/
│   ├── Persistence/
│   │   ├── Configurations/                     # EF config + create_hypertable
│   │   └── Migrations/
│   │       └── *_AddIotDeviceManagement.cs    # MỚI: 5 entity + hypertable + SourceType
│   ├── Repositories/                           # IotDeviceRepository...
│   ├── Mqtt/                                    # MỚI (MQTT)
│   │   ├── MqttBridgeBackgroundService.cs      # subscribe telemetry/heartbeat/status
│   │   ├── MqttTopicMap.cs                      # solar/{site}/{dev}/...
│   │   ├── TelemetryMessageHandler.cs           # validate → insert → anomaly
│   │   └── LastWillHandler.cs                   # status=offline → mark Offline + alert
│   ├── BackgroundJobs/
│   │   ├── IotDeviceOfflineDetectionBackgroundService.cs   # backup cho LWT
│   │   └── CalibrationExpiryNotificationService.cs
│   ├── Security/
│   │   └── DeviceApiKeyService.cs              # sinh/hash/rotate/revoke key per-device
│   └── DependencyInjection/
│       └── ManageDependencyInjection.cs        # đăng ký MQTT bridge + jobs
│
└── BatteryService.Api/
    └── Controllers/
        ├── IotDeviceController.cs              # device-side: provision/heartbeat/firmware
        ├── AdminIotDeviceController.cs         # admin CRUD device + firmware
        ├── IotCalibrationController.cs
        └── SensorReadingController.cs          # CẬP NHẬT ingest contract
```

### 9.3. MQTT Broker config (hạ tầng)

```
infra/mqtt/
├── docker-compose.yml          # EMQX hoặc Mosquitto
├── mosquitto.conf  (hoặc emqx.conf)
├── acl.conf                    # phân quyền topic per-device
├── certs/                      # TLS cert cho 8883
└── README.md                   # cách cấp credential per-device
```

### 9.4. (Tham khảo) Folder Python `iot/` hiện tại

```
iot/   ← codebase Raspberry Pi (Python, bản v1). Nếu chốt ESP32 thì KHÔNG dùng để chạy.
       Giữ lại tham khảo logic (queue, calibration, validation) khi viết firmware C++.
```

### 9.5. Quan hệ các phần

```
┌─────────────────────┐     ┌──────────────────┐     ┌────────────────────────┐
│  firmware-esp32/     │     │   infra/mqtt/     │     │  BatteryService (+IoT)  │
│  (codebase MỚI, C++) │────▶│   MQTT Broker     │────▶│  (thêm vào service sẵn) │
│  đọc BMS, publish    │     │   (Docker)        │     │  bridge + entity + API  │
└─────────────────────┘     └──────────────────┘     └────────────────────────┘
        cũng gọi ───────────── HTTPS REST ──────────────────▶ (provision/firmware/flush)
```

---

## 10. Roadmap triển khai (giữ tinh thần simulator-first)

| Giai đoạn | Làm gì | Kết quả |
|-----------|--------|---------|
| **P0 — MVP mock (HTTPS)** | ESP32 + WiFi + NTP + gửi reading giả vào endpoint hiện có (legacy) | Dashboard thấy data realtime, không cần BMS |
| **P1 — Resilience** | Thêm queue local + retry trên ESP32 | Tắt WiFi/bật lại không mất data |
| **P2 — Backend device mgmt** | Entity, provision, heartbeat, per-device key, offline detection | Quản lý device thật, offline alert |
| **P3 — MQTT** | Dựng broker + bridge service + firmware publish MQTT + LWT | Streaming realtime <100ms, 2 chiều, offline tức thì |
| **P4 — Hardware** | MAX485 + BMS thật, đọc 1 pin → multi-drop nhiều pin | ESP32 đọc Modbus thật, gửi production contract |
| **P5 — Hardening/Demo** | Calibration, metrics, OTA, Grafana, runbook, kịch bản failure | Demo ổn định end-to-end |

> P0–P2 chạy được với backend hiện tại/cập nhật mà **chưa cần MQTT**. MQTT (P3) là nâng cấp; phần cứng thật (P4) vào sau cùng.

---

## 11. Checklist toàn bộ việc phải làm

### A. Firmware ESP32 (codebase mới)
- [ ] Setup PlatformIO/Arduino-ESP32 + thư viện (PubSubClient, ModbusMaster, ArduinoJson)
- [ ] `config.h` + load `batteryMappings[]{serial, unitId}`
- [ ] WiFi connect + reconnect
- [ ] **NTP sync** (configTime) — BẮT BUỘC
- [ ] HTTP client: POST batch (legacy mode → endpoint hiện có)
- [ ] Gửi reading **mock** trước (chưa cần BMS) → thấy data lên dashboard
- [ ] Local queue (NVS/LittleFS) + retry exponential backoff
- [ ] Driver Modbus + điều khiển chân DE/RE của MAX485
- [ ] Đọc 1 BMS thật theo register map → verify scale/offset
- [ ] Multi-drop: loop nhiều unitId → batch nhiều pin
- [ ] Production contract: `X-Device-Code` + `Idempotency-Key` + `deviceTimestamp`
- [ ] Heartbeat (chip temp / heap / RSSI / queue depth)
- [ ] MQTT client: connect broker + LWT + publish telemetry/heartbeat + subscribe cmd
- [ ] Command handler (downlink: đổi config, trigger OTA)
- [ ] OTA qua `esp_https_ota` (optional, cuối)

### B. Backend IoT device-management (BatteryService)
- [ ] Migration `AddIotDeviceManagement` — 5 entity + hypertable + `SourceType`
- [ ] API key per-device (sinh/hash/rotate/revoke, scope)
- [ ] Admin endpoints (CRUD device + firmware release)
- [ ] Device endpoints: provision, heartbeat, firmware-check, firmware-update-log
- [ ] Cập nhật `POST /api/sensor-readings/batch` (header + mapping serial, giữ legacy)
- [ ] Validate: clock skew, outlier, apply calibration, update LastSeenAt
- [ ] `IotDeviceOfflineDetectionBackgroundService` + alert + publish event
- [ ] Calibration endpoints + expiry notification
- [ ] `AnomalyTypeEnum.SensorMismatch = 15` + cross-source validation (B10)
- [ ] Metrics observability
- [ ] ApiGateway route `/api/v1/iot-devices/*` + `/api/v1/admin/iot-devices/*`
- [ ] Unit/integration tests (provision, heartbeat, offline, dedup, skew, outlier, calibration)

### C. MQTT (P3)
- [ ] Dựng broker (EMQX/Mosquitto) Docker + TLS (8883) + credential per-device
- [ ] `MqttBridgeBackgroundService` (subscribe telemetry/heartbeat/status)
- [ ] LWT handler `status=offline` → mark Offline + alert
- [ ] Publish downlink `cmd` (optional)
- [ ] ACL phân quyền topic per-device

### D. Phần cứng
- [ ] Mua ESP32-S3 + MAX485 + điện trở 120Ω + nguồn
- [ ] Xác nhận BMS có RS485/Modbus + register map + đổi được unitId
- [ ] Đấu nối + đổi unitId từng pin

### E. Web/Admin UI (FE)
- [ ] Trang quản lý IoT device (tạo/sửa/xóa, hiển thị key 1 lần)
- [ ] Dashboard gateway: online/offline, queue depth, heartbeat history, uptime
- [ ] Upload firmware release

### F. Demo & runbook
- [ ] Script seed: 1 site + vài battery + threshold
- [ ] Kịch bản: normal → overheat/low SOC (alert) → tắt ESP32 >5 phút (offline alert)
- [ ] Runbook setup ESP32 (nạp firmware, cấu hình, đấu RS485)
- [ ] Grafana IoT dashboard

---

## 12. Các bẫy ESP32 phải nhớ

1. **NTP bắt buộc** — ESP32 không có RTC; không sync → `deviceTimestamp` lệch → backend reject (clock skew > 5 phút). Lỗi hay gặp nhất.
2. **TLS/MQTT-over-TLS tốn RAM** — crash heap thì dùng ESP32-S3; production phải nạp CA cert (dev mới `setInsecure()`).
3. **Chân DE/RE của MAX485** — phải đảo hướng truyền/nhận đúng lúc, nếu không đọc RS485 lỗi (trừ module auto-direction).
4. **Queue flash hữu hạn** — ESP32 không buffer mất mạng dài như RPi; site rớt mạng nhiều giờ → chấp nhận mất data cũ hoặc giảm tần suất.
5. **Broker là điểm chết đơn (SPOF)** — broker down thì cả MQTT đứng; firmware vẫn phải queue local + fallback HTTPS flush.
6. **BMS phải đổi được unitId** — multi-drop chỉ chạy khi mỗi BMS có địa chỉ khác nhau.

---

## 13. Bảng đánh đổi MQTT (cân nhắc trước khi làm P3)

| Lợi | Cái giá |
|-----|--------|
| Realtime <100ms, 2 chiều, offline tức thì (LWT) | Thêm hạ tầng broker — deploy, bảo mật, monitor |
| Tiết kiệm băng thông/pin (kết nối thường trực) | Phức tạp hơn HTTP — học MQTT, QoS, retained, LWT |
| Scale nhiều device tốt (broker fanout) | MQTT-over-TLS setup khó hơn HTTPS |
| QoS đảm bảo gửi + retained message | ESP32 vẫn cần local queue cho lúc broker down |

→ **Với capstone:** MQTT làm hệ thống "xịn" hơn và đáng demo (đúng chất IoT), nhưng là **scope mở rộng (P3)** — chỉ nên làm sau khi flow HTTPS cơ bản (P0–P2) đã chạy ổn. Nếu thiếu thời gian, để MQTT là nâng cấp sprint sau; flow HTTPS vẫn đủ cho MVP và demo.
