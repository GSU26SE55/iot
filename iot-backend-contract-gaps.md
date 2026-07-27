# Audit contract IoT ↔ Backend — Điểm lệch & Điểm khớp

> **Ngày audit:** 2026-07-08
> **Phạm vi:** `capstone/iot` (firmware ESP32 + simulator) @ `8ac2de9` ↔ `capstone/backend` (BatteryService, branch `dev`) @ `e9b47a1`
> **Phương pháp:** trích xuất contract độc lập từ 2 repo (endpoint, DTO, enum, MQTT topic, auth, status code) → đối chiếu từng điểm → **verify trực tiếp trên source code** mọi điểm nghi lệch (không kết luận từ docs/comment).
>
> Ký hiệu mức độ: 🔴 chặn chức năng / mất data · 🟡 rủi ro vận hành / cần biết · 🟢 nợ kỹ thuật, không vỡ · ✅ đã giải quyết

---

## Tóm tắt nhanh

| # | Điểm lệch | Mức độ | Hệ quả nếu bỏ qua |
|---|-----------|--------|--------------------|
| 1 | MQTT bridge backend mặc định **TẮT**, firmware coi publish OK = đã gửi | 🔴 | Mất toàn bộ telemetry **im lặng** |
| 2 | API key scope mặc định **thiếu `EnvironmentalIngest`** | 🔴 | Ambient (SHT31) + incident (MQ-2, rò nước) bị 403, firmware drop luôn |
| 3 | Từ vựng lệnh downlink (cmd type) 2 bên không khớp | 🟡 | Admin gửi lệnh → device ack "unknown", không làm gì |
| 4 | MQTT publish thực tế QoS 0 (thiết kế nói QoS 1) | 🟡 | Rơi message trên mạng chập chờn, firmware không biết |
| 5 | Outlier auto-decommission >50/h — bẫy với INA226 chưa gắn shunt | 🟡 | Device bị backend tự vô hiệu hóa khi test thật |
| 6 | Reading bị "skipped" âm thầm — firmware không đọc `inserted/skipped` | 🟡 | Data mất từng phần mà không ai biết |
| 7 | MQTT telemetry không có cơ chế idempotency (chỉ HTTPS có) | 🟡 | Có thể trùng row ở edge case retry |
| 8 | Casing JSON không nhất quán (PascalCase vs camelCase) | 🟢 | Chạy được nhờ case-insensitive; là nợ kỹ thuật |
| 9 | `cmd/ack` backend chỉ log, không lưu DB | 🟢 | Không trace được lịch sử lệnh qua API |
| 10 | Lệch nhỏ về docs/default (comment route cũ, ntpServer) | 🟢 | Gây nhầm lẫn khi đọc code |
| ✅ | Bug idempotency `Id = Guid.Empty` | ✅ | **Đã fix** — xem mục Đã giải quyết |

---

## 🔴 #1 — MQTT bridge backend mặc định TẮT, trong khi firmware tin "publish thành công = data đã tới"

**Điểm lệch nghiêm trọng nhất của toàn hệ thống.**

**Phía backend:**
- `MqttOptions.Enabled` là `bool` không có initializer → mặc định `false`
  (`backend/services/BatteryService/src/BatteryService.Infrastructure/Mqtt/MqttOptions.cs`)
- Cả 3 file `appsettings*.json` của BatteryService.Api đều **không có section `"Mqtt"`**
- `docker-compose.yml:327` default: `Mqtt__Enabled: ${Mqtt__Enabled:-false}`

**Phía firmware** (`firmware-esp32/src/main.cpp` — hàm `ingestOnce()`, ~dòng 258–271):
- Ưu tiên MQTT khi connected; `ingestViaMqtt()` OK → `return true` — **không queue LittleFS, không gửi HTTPS nữa**
- Fallback HTTPS chỉ kích hoạt khi **publish FAIL** (streak ≥ `MQTT_PUBLISH_FAIL_THRESHOLD`)

**Vì sao thành mất data im lặng:** broker Mosquitto nhận publish thành công **bất kể backend có subscribe hay không** (MQTT không có ack end-to-end). Bridge tắt → firmware log `MQTT posted OK`, mosquitto nhận message, backend không consume → data biến mất, không có lỗi ở bất kỳ đâu.

**Cách xử lý:**
- Vận hành: **LUÔN** set `Mqtt__Enabled=true` (env var hoặc appsettings) khi chạy backend cùng firmware/simulator thật. Cân nhắc thêm section `"Mqtt"` mặc định vào `appsettings.Development.json`.
- Dài hạn (đề xuất): firmware chỉ nên coi MQTT là "đã gửi" khi hệ thống có cơ chế xác nhận (vd. backend echo lên topic ack, hoặc chấp nhận rủi ro có ghi chú rõ trong docs).

---

## 🔴 #2 — Scope mặc định của API key thiếu `EnvironmentalIngest`

**Phía backend** (`backend/.../BatteryService.Domain/Enums/IotApiKeyScopeEnum.cs`):

```csharp
SensorIngest        = 1 << 0  // 1
DeviceHeartbeat     = 1 << 1  // 2
EnvironmentalIngest = 1 << 2  // 4   ← cần cho ambient + incident
FirmwareCheck       = 1 << 3  // 8
EdgeDeviceDefault   = SensorIngest | DeviceHeartbeat | FirmwareCheck  // = 11, KHÔNG có 4
```

**Phía firmware:** 2 tính năng cần scope 4:
- `POST /api/ambient/readings/batch` — SHT31 ambient (S5-FW-06, `src/sensors/sht31.cpp`)
- `POST /api/environmental-incidents` — khói MQ-2 / rò nước (S6-FW-01/02, `src/core/environmental_incident.cpp`)

**Hệ quả kép:** device cấp key `EdgeDeviceDefault` → 2 endpoint trên trả **403** → firmware xếp 4xx vào **permanent failure → drop, không retry, không queue** (`main.cpp` postBatch outcome logic). Tính năng môi trường chết im lặng.

**Cách xử lý:** khi tạo/provision key cho edge device phải cấp thêm `EnvironmentalIngest` (tổng = 15), **hoặc** backend sửa `EdgeDeviceDefault |= EnvironmentalIngest`. Cần thống nhất với team BE.

---

## 🟡 #3 — Từ vựng lệnh downlink không khớp

| | Phía backend (admin gửi) | Phía firmware (hiểu được) |
|---|---|---|
| Nguồn | `IotDeviceCommandDtos.cs:11` — comment gợi ý `reboot \| ota \| calibrate \| sample-now \| set-config`; `Type` là string tự do, `Params` là object tự do | `cmd_logic.cpp:33-39` — chỉ xử lý `set_interval`, `request_heartbeat`, `trigger_ota` (case-insensitive) |

Backend **không validate** type → admin gửi `reboot` hợp lệ ở phía backend, device trả ack `{"status":"unknown"}` và không làm gì. Shape envelope `{cmdId, type, params}` thì khớp ✅.

**Cách xử lý:** thống nhất danh sách command hai bên (đề xuất: backend validate `Type` theo whitelist khớp firmware; hoặc bổ sung handler firmware cho các type backend cần). Lưu ý param: firmware đọc `pollingSeconds` *hoặc* `pollingIntervalSeconds` trong `params` cho `set_interval` (range [1, 3600]).

---

## 🟡 #4 — QoS thực tế là 0, thiết kế nói QoS 1

Firmware dùng **PubSubClient v2.8** — thư viện này **không hỗ trợ publish QoS 1** (`mqtt_client.cpp:293-298` có ghi chú). Docs/tasksprint mô tả QoS 1. Backend bridge subscribe bình thường nên không "vỡ contract", nhưng: mạng chập chờn → message rơi giữa đường, firmware vẫn tin đã gửi (cộng hưởng với #1). LWT/status (retain=true) không bị ảnh hưởng.

**Cách xử lý:** chấp nhận rủi ro cho scope capstone (ghi rõ vào docs), hoặc đổi lib MQTT (vd. `espMqttClient` hỗ trợ QoS 1/2) nếu Sprint sau có thời gian.

---

## 🟡 #5 — Bẫy auto-decommission khi test với INA226 chưa gắn shunt

Backend (`BatchIngestSensorReadingsCommandHandler.cs:29-45`): reading ngoài bounds (V ∉ (0,1000], temp ∉ [-50,150], |I| > 1000, SOC/SOH ∉ [0,100]) bị đếm là **outlier**; **>50 outlier/giờ → device tự động chuyển `Decommissioned`** (không phải chỉ skip).

Rủi ro phía IoT: INA226 khi chưa gắn shunt ngoài (STT 17 hardware.csv — món Tùy chọn) có thể trả giá trị dòng rác; poll 5s = 720 reading/giờ → vượt ngưỡng 50 trong vài phút → **device bị khóa giữa chừng khi test**, mọi request sau trả 409.

**Cách xử lý:** khi test thật — hoặc gắn shunt, hoặc tắt nguồn đọc current của INA226 (chỉ đọc V), hoặc tạm nâng threshold ở backend dev. Nếu device đã bị decommission: cần admin re-activate.

---

## 🟡 #6 — Reading bị "skipped" âm thầm, firmware không nhìn thấy

Backend trả `201` kèm `data: { totalReceived, inserted, skipped }` — reading outlier/không resolve được serial sẽ nằm trong `skipped` nhưng **HTTP vẫn 2xx**. Firmware (`main.cpp:153`) chỉ check status code 2xx, **không parse `inserted/skipped`** → một phần data có thể bị loại mà không có log nào phía device.

**Cách xử lý:** chấp nhận được cho MVP (backend có Prometheus metric + audit), nhưng nên thêm 1 dòng log phía firmware parse `skipped > 0` khi debug integration. Tối thiểu: biết hành vi này khi thấy "gửi 6 reading mà DB chỉ có 4".

---

## 🟡 #7 — Đường MQTT không có idempotency

Header `Idempotency-Key` chỉ tồn tại ở đường **HTTPS** (`http_client.cpp:94-95`; backend dedup theo `(DeviceCode, Idempotency-Key)`). Payload MQTT telemetry **không mang key này**, và MQTT bridge ingest **không qua lớp dedup**. Edge case: firmware publish MQTT, kết nối đứt ngay sau khi broker đã nhận nhưng trước khi firmware nhận TCP ack → firmware coi là fail → gửi lại qua HTTPS → **cùng batch vào DB 2 lần** (2 đường không dedup chéo nhau).

**Cách xử lý:** xác suất thấp, chấp nhận cho capstone; nếu cần chặt chẽ — nhúng `idempotencyKey` vào payload MQTT và bridge check trước khi insert.

---

## 🟢 #8 — Casing JSON không nhất quán (chạy được, nhưng là nợ kỹ thuật)

| Nguồn gửi | Casing | Ví dụ |
|---|---|---|
| Firmware — ingest telemetry (`payload.cpp`) | camelCase | `batteryAssetSerial`, `voltage`, `socPercent` |
| Firmware — provision (`provision.cpp:79-82`) | **PascalCase** | `FirmwareVersion`, `DeviceTimestamp` |
| Firmware — heartbeat (`heartbeat.cpp:75-93`) | **PascalCase** | `Temperature`, `MemoryUsageMb` |
| Simulator (`esp32_simulator.py:53-78`) | **PascalCase toàn bộ** | `Items[]`, `BatteryAssetSerial`, `Voltage` |
| Backend serialize response | camelCase | `isSuccess`, `data` |

Không vỡ vì ASP.NET Core deserialize **case-insensitive** (mặc định MVC + `JsonSerializerDefaults.Web` ở MQTT bridge). Nhưng nếu backend một ngày bật strict naming policy, hoặc thêm gateway/validator khác — provision/heartbeat/simulator vỡ trước tiên. **Đề xuất:** thống nhất camelCase toàn bộ phía gửi.

---

## 🟢 #9 — `cmd/ack` backend chỉ log, không lưu

Firmware gửi ack `{"cmdId","status":"ok|failed|unknown","error"}` lên `solar/{deviceCode}/cmd/ack`; backend bridge (`MqttBridgeBackgroundService.cs:283-287`) chỉ `LogInformation` — **không persist**. Admin gửi lệnh nhận `202 Accepted` nhưng không có API nào để xem lệnh đã được device thực thi hay chưa (phải xem log container). Chấp nhận được cho MVP; ghi nhận là gap observability.

---

## 🟢 #10 — Lệch nhỏ trong docs/defaults (không ảnh hưởng runtime)

1. **Comment route cũ**: `IotApiKeyScopeEnum.cs` ghi scope cho "`POST /api/ambient/ingest`" — route thật là `/api/ambient/readings/batch`. Chỉ là comment drift.
2. **NTP server default**: backend provision trả default `time.google.com`; firmware fallback nội bộ là `pool.ntp.org`. Firmware luôn ưu tiên giá trị từ provision → không lệch thực tế.
3. **Heartbeat response không được firmware khai thác**: backend trả `nextHeartbeatInSeconds`, `firmwareUpdateAvailable`, `clockSkewWarning` — firmware bỏ qua (chỉ check 2xx); OTA check đi đường polling riêng. Capability thừa, không lỗi.
4. **Range temperature**: validation (`ValidateAsync`) không check range nhiệt độ (chỉ check `Voltage < 0`, độ dài string, enum defined) — outlier bounds nằm ở **handler** [-50..150]. Một số tài liệu ghi [-50..120] là sai — số đúng trong code là **150**.

---

## ✅ Đã giải quyết (từng là blocker)

**Bug idempotency `Id = Guid.Empty`** — `SensorIngestIdempotencyRecord.Id` không được sinh → request thứ 2 trở đi dính duplicate PK → **500** trên mọi ingest có `Idempotency-Key` (chặn cả firmware thật lẫn simulator, phát hiện 2026-06-26).
**Trạng thái: ĐÃ FIX** — verify trực tiếp 2026-07-08: `BatchIngestSensorReadingsCommandHandler.cs:382` có `Id = Guid.NewGuid()`.
⚠️ Lưu ý tồn dư: DB dev cũ có thể còn 1 row rác `id = 00000000-...` trong `sensor_ingest_idempotency_records` — nếu môi trường dev cũ vẫn 500, xóa row đó.

---

## ✅ Những gì đã đối chiếu và KHỚP (không cần audit lại)

**HTTP endpoints** — firmware gọi ↔ backend implement, khớp 100% đường dẫn:

| Firmware gọi | Backend controller | Ghi chú |
|---|---|---|
| `POST /api/sensor-readings/batch` | `SensorReadingsController` | trả **201** — firmware nhận mọi 2xx ✅; duplicate idempotency → 200 + cached ✅ |
| `POST /api/iot-devices/provision` | `IotDevicesController` | firmware parse đủ field response (pollingIntervalSeconds, siteId, ntpServer…) ✅ |
| `POST /api/iot-devices/heartbeat` | `IotDevicesController` | 12 field request khớp (kể cả alias `signalStrengthDbm`/`localQueueDepth`) ✅ |
| `GET /api/iot-devices/firmware-check?currentVersion=` | `IotDevicesController` | firmware đọc cả alias `updateAvailable/hasUpdate`, `downloadUrl/artifactUrl` ✅ |
| `PUT /api/iot-devices/firmware-update-log/{id}` | `IotDevicesController` | status enum 1-7 khớp ✅ |
| `POST /api/ambient/readings/batch` | `AmbientReadingsController` | field + enum source khớp (chỉ vướng scope — mục #2) |
| `POST /api/environmental-incidents` | `EnvironmentalIncidentsController` | incidentType 1-5+99, severity 1-3 khớp ✅ |

**MQTT topics** — khớp từng ký tự, cả 2 bên dùng deviceCode lowercase:
`solar/{deviceCode}/{serial}/telemetry` · `solar/{deviceCode}/heartbeat` · `solar/{deviceCode}/status` (LWT retain) · `solar/{deviceCode}/cmd` · `solar/{deviceCode}/cmd/ack` — backend subscribe đúng 4 wildcard tương ứng.

**Enums** — giá trị số khớp từng cặp: `SourceType` (Bms=1, IotGateway=2, External=3) · `ChargingState` (1-5) · `AmbientSource` (IotSensor=1, WeatherApi=2) · `IncidentType` (1-5, 99) · `Severity` (1-3) · `FirmwareUpdateStatus` (1-7).

**Giới hạn & format**: batch ≤ 1000 items · `bmsErrorCode` ≤ 64 · `sensorSourceCode` ≤ 20 (`primary`/`redundant`/`external-temp`) · `batteryAssetSerial` ≤ 64 · timestamp RFC3339 UTC `...Z` · API key prefix `iotk_` + header `X-Api-Key`/`X-Device-Code` · clock skew 5 phút (backend flag + metric, không reject).

---

## Checklist trước khi chạy end-to-end với phần cứng thật

- [ ] Backend: `Mqtt__Enabled=true` (mục #1)
- [ ] API key của device được cấp thêm scope `EnvironmentalIngest` nếu test SHT31/MQ-2/rò nước (mục #2)
- [ ] Site đã nhập lat/lon nếu muốn WeatherSync (Open-Meteo) có dữ liệu
- [ ] DB dev không còn row rác `id = Guid.Empty` trong bảng idempotency
- [ ] INA226: chưa có shunt thì đừng gửi current rác — tránh auto-decommission (mục #5)
- [ ] Khi thấy "gửi N reading, DB có ít hơn N" → check `skipped` trong response backend trước khi nghi firmware (mục #6)

---

*File này là snapshot tại thời điểm audit — 2 repo vẫn đang phát triển; khi backend đổi contract (thêm field, đổi scope) cần đối chiếu lại các mục 🔴 trước tiên.*
