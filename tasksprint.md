# IoT Capstone — Task Sprint Plan

> **Document type:** Kế hoạch sprint + phân rã task chi tiết cho toàn bộ hệ thống IoT.
> **Nguồn:** Tổng hợp & phân rã từ `newiot.md` (thiết kế ESP32+MQTT, viết tắt **NI**), `overall.iot.md` (BOM + 7 luồng vận hành B1–B7, viết tắt **OV**), `wiring-diagram.md` (sơ đồ GPIO + đấu nối, viết tắt **WD**). **Đồng bộ với `backend/overall.md`** (master roadmap backend, viết tắt **MO**) — đặc biệt §52 (IoT Edge Device), §52bis (implementation plan), §53 (scope reduction + Alert–Ticket Saga), Sprint IoT-1 (§17).
> **Triết lý:** **Simulator-first** — phần mềm chạy end-to-end bằng mock trước khi đụng vào pin/BMS thật. Phần cứng vào muộn để không block flow chính.
> **Scope guard (ADR-017, MO §53.1/§53.2bis):** TUYỆT ĐỐI không thêm Energy/CO2/cost analytics (kWh, savings, carbon, charge cycle dashboard). INA226 chỉ dùng cross-source validation, KHÔNG tích phân năng lượng. CI backend chặn keyword `EnergySession|EnergyKwh|CapacityKw|CarbonEmission|...`
> **Cập nhật:** 2026-06-12 (sync với MO sau gap analysis)

---

## Mục lục

- [0. Quy ước chung](#0-quy-ước-chung)
- [1. Vai trò & người phụ trách](#1-vai-trò--người-phụ-trách)
- [2. Tổng quan timeline (8 sprint)](#2-tổng-quan-timeline-8-sprint)
- [3. Dependency graph giữa các sprint](#3-dependency-graph-giữa-các-sprint)
- [4. Chi tiết từng sprint](#4-chi-tiết-từng-sprint)
  - [Sprint 0 — Foundation & Procurement Cấp 0](#sprint-0--foundation--procurement-cấp-0-1-tuần)
  - [Sprint 1 — MVP Mock HTTPS (P0)](#sprint-1--mvp-mock-https-p0-2-tuần)
  - [Sprint 2 — Backend Device Management (P2)](#sprint-2--backend-device-management-p2-2-tuần)
  - [Sprint 3 — Resilience & Production Contract (P1)](#sprint-3--resilience--production-contract-p1-2-tuần)
  - [Sprint 4 — MQTT Broker & Bridge (P3)](#sprint-4--mqtt-broker--bridge-p3-2-tuần)
  - [Sprint 5 — Hardware Integration (P4)](#sprint-5--hardware-integration-p4-2-tuần)
  - [Sprint 6 — Anomaly · Cross-source · Notification](#sprint-6--anomaly--cross-source--notification-2-tuần)
  - [Sprint 7 — Calibration · OTA · Observability](#sprint-7--calibration--ota--observability-2-tuần)
  - [Sprint 8 — Pilot · Demo · Runbook (P5)](#sprint-8--pilot--demo--runbook-p5-1-2-tuần)
- [5. Cross-cutting tracks (chạy song song)](#5-cross-cutting-tracks-chạy-song-song)
- [6. Procurement timeline (mua sắm theo sprint)](#6-procurement-timeline-mua-sắm-theo-sprint)
- [7. Risk register & mitigation](#7-risk-register--mitigation)
- [8. Definition of Done — checklist toàn hệ thống](#8-definition-of-done--checklist-toàn-hệ-thống)
- [9. Đồng bộ với `backend/overall.md` (master roadmap)](#9-đồng-bộ-với-backendoverallmd-master-roadmap)

---

## 0. Quy ước chung

- **Mỗi task có ID** dạng `S{sprint}-{track}-{seq}` (vd `S2-BE-03` = Sprint 2, Backend, task 03). Dùng ID này khi tạo issue/PR.
- **Track tag:**
  - `FW` = Firmware ESP32 (C++/PlatformIO)
  - ~~`BE` = Backend BatteryService (.NET/C#)~~ → **TOÀN BỘ task BE đã chuyển sang `backend/overall.md` Sprint IoT-2** (`#IoT2-01..38`). tasksprint.md chỉ MENTION BE để FW/INF/QA biết dependency, KHÔNG có table BE chi tiết nữa.
  - `INF` = Infrastructure (broker, DB, Docker, TLS)
  - `HW` = Hardware (mua, lắp ráp, đấu dây, calibration)
  - `FE` = Web/Mobile UI
  - `QA` = Test, demo, runbook, docs
- **Definition of Done (DoD) toàn cục cho mọi task:**
  1. Code merge vào `dev` qua PR có review.
  2. Có unit/integration test hoặc kịch bản thủ công verify được.
  3. Tài liệu thay đổi (README hoặc inline) đã update.
  4. Demo được trên môi trường dev (laptop + Docker compose).
- **Spec gốc** luôn reference về `newiot.md` (NI), `overall.iot.md` (OV), `wiring-diagram.md` (WD) — định dạng `NI §7.1` = section 7.1 của newiot.md.

---

## 1. Vai trò & người phụ trách

| Role | Tag | Trách nhiệm chính | Skill cần |
|------|-----|-------------------|-----------|
| **Firmware Lead** | FW | Firmware ESP32-S3 (C++/PlatformIO), đọc Modbus/CAN/I2C, MQTT client, queue local, OTA | C++/Arduino, PlatformIO, hiểu UART/I2C/SPI |
| **Backend Lead (Thắng)** | BE → owner ngoài tasksprint | **TOÀN BỘ task BE chuyển sang `backend/overall.md` Sprint IoT-2** (`#IoT2-01..38`). tasksprint chỉ giữ tag BE để mention dependency. Thắng đồng thời là single point of contact cho IoT track khi cần làm rõ contract / endpoint. | .NET/C#, EF Core, TimescaleDB, MediatR, MassTransit Saga |
| **Infra/DevOps** | INF | MQTT broker (EMQX/Mosquitto) + TLS, Docker compose, Postgres/Timescale, Redis, RabbitMQ, Grafana | Docker, mTLS, EMQX/Mosquitto config |
| **Hardware/Embedded** | HW | Procurement, đấu dây theo WD, calibration (Fluke), enclosure, đo nguồn | Đọc datasheet, hàn, đồng hồ vạn năng |
| **Frontend** | FE | Trang Admin IoT device + dashboard gateway + upload firmware | React/Next, biểu đồ |
| **QA/Demo Owner** | QA | Test scenario B1–B7 (OV §B), runbook, seed data, video demo | Black-box test, viết runbook |

> Team capstone 4–5 người thường gộp: 1 người ôm FW + HW, 1 ôm BE + INF, 1 ôm FE, 1 ôm QA/PM. Cột tag dùng để gắn assignee cho từng task.

---

## 2. Tổng quan timeline (8 sprint)

| Sprint | Tên | Thời lượng | Mục tiêu khả nghiệm cuối sprint | Map roadmap NI §10 |
|--------|-----|-----------|--------------------------------|---------------------|
| **S0** | Foundation & Procurement Cấp 0 | 1 tuần | Repo + Docker infra chạy, 1 ESP32 trên bàn nháy "Hello WiFi" | (chuẩn bị) |
| **S1** | MVP Mock HTTPS | 2 tuần | ESP32 mock publish → dashboard hiện data realtime, không cần BMS | **P0** |
| **S2** | Backend Device Management | 2 tuần | Admin tạo device + API key; firmware provision/heartbeat; offline detection | **P2** |
| **S3** | Resilience & Production Contract | 2 tuần | Tắt WiFi 5 phút → bật lại, 0 record mất; contract mới (`X-Device-Code`, idempotency) | **P1** |
| **S4** | MQTT Broker & Bridge | 2 tuần | ESP32 publish telemetry qua MQTT-over-TLS; LWT mark offline tức thì; bridge ghi DB | **P3** |
| **S5** | Hardware Integration | 2 tuần | 1 ESP32 đọc 3–4 pin BMS thật qua RS485 multi-drop + sensor phụ INA226/DS18B20 | **P4** |
| **S6** | Anomaly · Cross-source · Notification | 2 tuần | Reading vượt ngưỡng → alert → ticket auto → notification push/email; SensorMismatch chạy | **P5a** |
| **S7** | Calibration · OTA · Observability | 2 tuần | Admin upload firmware → ESP32 OTA thành công; Grafana dashboard live | **P5b** |
| **S8** | Pilot · Demo · Runbook | 1–2 tuần | Enclosure + UPS + 4G; kịch bản demo B1–B7 chạy mượt; video + runbook | (đóng gói) |

**Tổng:** ~15–17 tuần (3.5–4 tháng) cho lộ trình đầy đủ. Capstone 14 tuần có thể cắt S7 (chỉ làm calibration + dashboard, bỏ OTA) hoặc gộp S8 vào S7.

---

## 3. Dependency graph giữa các sprint

```
S0 ──► S1 ──► S2 ──► S3 ──► S4 ──► (S5 song song với S6/S7) ──► S8
              │       │              │
              │       └──► S6 (sau khi S3 có contract + S2 có entity)
              └──► (FE track bắt đầu sau S2 — entity đã có)
              └──► (Infra MQTT broker dựng sớm trong S4, nhưng có thể dựng từ S0 cho song song)
```

**Đường găng (critical path):** S0 → S1 → S2 → S3 → S4 → S6 → S8.
S5 (hardware) **chạy song song** với S6/S7 vì firmware đã ổn ở S4, BMS thật chỉ thay nguồn data — không block backend.
S7 (OTA + observability) có thể trễ vào S8 nếu thiếu thời gian.

---

## 4. Chi tiết từng sprint

> Mỗi task có format: **ID** — *mô tả ngắn*. Reference spec. Acceptance criteria. *(Track / estimate giờ)*

---

### Sprint 0 — Foundation & Procurement Cấp 0 (1 tuần)

**Mục tiêu:** Mọi người clone repo về chạy được Docker compose; có 1 ESP32-S3 trên tay flash chương trình WiFi mẫu; backend BatteryService build pass; broker MQTT có thể dựng (chưa bắt buộc dùng).

**Pre-requisite:** Tài khoản GitHub, máy có Docker, link mua linh kiện Cấp 0 (OV phụ lục).

#### Backlog

> **Trạng thái:** ✅ Done · 🟡 Code OK / cần verify hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **1. S0-INF-01** (#2) | Tạo monorepo skeleton: `firmware-esp32/`, `infra/mqtt/`, `infra/db/`, `docs/` (BE đã có repo riêng) | NI §9 | `tree -L 2` thấy đúng cấu trúc; README root mô tả 3 codebase | INF | ✅ |
| **2. S0-INF-02** (#3) | `infra/docker-compose.dev.yml` chạy Postgres+Timescale + Redis + RabbitMQ + EMQX/Mosquitto | OV §A13 | `docker compose up -d` → 4 service healthy, port 1883/5432/6379/5672/15672/18083 OK | INF | ✅ (Mosquitto thay EMQX → không có port 18083) |
| **3. S0-INF-03** (#4) | Seed Postgres extension `timescaledb`, tạo DB `battery_service_dev` | NI §7.1 | `psql -c '\dx'` thấy `timescaledb` | INF | ✅ |
| **4. S0-FW-01** (#5) | Cài PlatformIO + tạo project `firmware-esp32/` board `esp32-s3-devkitc-1`, lib: PubSubClient, ModbusMaster, ArduinoJson, OneWire, DallasTemperature, Adafruit INA226, Adafruit SHT31 | NI §9.1 | `pio run` compile pass với main.cpp trống | FW | ✅ (thừa lib `adafruit/Adafruit INA219` không trong spec) |
| **5. S0-FW-02** (#6) | Flash sketch "blink + Serial.println('hello')" lên ESP32-S3 thật | — | LED nhấp nháy, Serial Monitor in chuỗi | FW | 🟡 Code có (`examples/01-blink-hello/`); chưa flash ESP32 verify |
| **6. S0-FW-03** (#7) | Sketch WiFi connect + NTP sync + Serial in giờ ISO8601 | NI §12 (bẫy NTP) | Serial in `2026-06-12T...Z` đúng giờ thực, lệch ≤ 1s | FW | 🟡 Functionality nằm trong Sprint 1 `src/main.cpp` (đã overwrite); sketch standalone biến mất; chưa flash verify |
| **7. S0-HW-01** (#8) | Đặt mua **Cấp 0**: 1× ESP32-S3 DevKitC-1 (N16R8), 3× cáp USB-C, 1× breadboard | OV phụ lục | Có linh kiện trên tay cuối sprint | HW | 🔴 Checklist có, status thực `☐` |
| **8. S0-HW-02** (#9) | Đặt order **Cấp 1**: 2× MAX485 (XY-017), điện trở 120Ω × 4, 4.7kΩ × 4, 20m cáp đôi xoắn shielded, domino | OV §A2, §A9 | Đã đặt; ETA trước S5 | HW | 🔴 Checklist có, status thực `☐` |
| **9. S0-HW-03** (#10) | Đặt order **BMS-RS485**: 3–4 pin LiFePO4 12V có BMS Daly/JBD/JK-BMS, **hỏi rõ register map + đổi unitId** | OV §A4 checklist | Có xác nhận seller về register map | HW | 🔴 Critical path; chưa đặt |
| **10. S0-QA-01** (#11) | Tạo trang `docs/glossary.md` thuật ngữ (BMS, Modbus unitId, LWT, hypertable, idempotency, clock skew...) để team mới đọc nhanh | NI §0 | Cover ≥ 15 thuật ngữ | QA | ✅ 31 thuật ngữ (≥ 15 spec) |

**Backend (BE):** task `S0-BE-01` (pull BatteryService, build pass, migration cũ) đã chuyển sang **`backend/overall.md` Sprint IoT-2 Phase A** (`#IoT2-01`). IoT track không tự build BE — chỉ verify Swagger UI mở được trước S1. **Status:** 🔴 chưa biết Thắng đã làm chưa.

**DoD sprint:** Họp 30 phút, mọi member demo được sketch ESP32 in giờ NTP + `docker compose up` trên máy mình.

##### Sprint 0 — Tổng kết gap

| Nhóm | Items |
|------|-------|
| ✅ Đã làm (6) | #2, #3, #4, #5 (lib INA219 thừa), #11 ─ + procurement checklist file |
| 🟡 Code OK, cần ESP32 (2) | #6 blink, #7 wifi+ntp (Sprint 1 đã overwrite `src/main.cpp` → mất isolation demo) |
| 🔴 Procurement chưa làm (3) | #8 Cấp 0, #9 Cấp 1, #10 BMS — leader/HW owner update `docs/procurement-checklist.md` |
| 🔴 Backend dep (1) | `#IoT2-01` Swagger UI verify (hỏi Thắng) |
| 🔴 Cleanup code/doc (3) | Restore `examples/02-wifi-ntp/` từ git, remove lib INA219, update README outdated "Quick start (Sprint 0)" |

---

### Sprint 1 — MVP Mock HTTPS (P0) (2 tuần)

**Mục tiêu:** Chứng minh flow end-to-end **không có pin thật**: ESP32 sinh data giả → HTTPS POST → backend ghi DB → FE hiển thị. Đây là khoảnh khắc cả team thấy "hệ thống sống".

**Pre-requisite:** S0 xong; backend BatteryService cũ vẫn có endpoint `POST /api/sensor-readings/batch` (legacy).

#### Backlog

> **Trạng thái:** ✅ Done · 🟡 Code OK / cần verify hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **11. S1-FW-01** (#12) | `include/config.h` chứa `WIFI_SSID/PASS, BACKEND_URL, DEVICE_CODE, API_KEY` placeholder | NI §9.1 | Build pass; đọc được trong main | FW | ✅ 5 macros literal match spec |
| **12. S1-FW-02** (#13) | `src/net/wifi_manager.cpp` — connect + auto reconnect khi rớt | NI §9.1 | Tắt WiFi router → ESP32 log "reconnecting"; bật lại → reconnect ≤ 30s | FW | 🟡 Code có (log "reconnecting" throttle 5s); chưa flash ESP32 + router để đo ≤ 30s |
| **13. S1-FW-03** (#14) | `src/net/time_sync.cpp` — NTP `configTime("pool.ntp.org")` + util `isoNow()` | NI §12 #1 | `isoNow()` trả đúng RFC3339 UTC | FW | 🟡 Code có (`%Y-%m-%dT%H:%M:%SZ`, 3 NTP server fallback); chưa verify trên ESP32 thật |
| **14. S1-FW-04** (#15) | `src/bms/mock_bms.cpp` sinh reading giả cho N pin (config: serial + cycle voltage 12.0–13.0V, dao động sine + noise nhỏ); scenario flag `overheat`, `low_soc` | NI §9.1, §11 A | Mock trả mảng `SensorReading` đúng struct; switch scenario qua compile flag | FW | ✅ Struct ở `src/core/reading.h`, `batteryMappings[]` ở `src/config/`, voltage strict `[12.0, 13.0]V` (sine 0.48 + noise 0.02), scenarios `MOCK_SCENARIO_{OVERHEAT,LOW_SOC}` |
| **15. S1-FW-05** (#16) | `src/core/payload.cpp` build JSON theo **legacy contract** `items[].batteryAssetId` (MVP backward compat NI §7.4) | NI §7.4 | JSON output match schema legacy; có test unit | FW | ✅ `{items[].batteryAssetId,...}` camelCase, 6/6 unit tests PASS |
| **16. S1-FW-06** (#17) | `src/net/http_client.cpp` POST batch sang `/api/sensor-readings/batch` (HTTPS, `setInsecure()` chỉ cho dev) | NI §7.4 | Backend nhận 200; có log response | FW | 🟡 Code có (`setInsecure()`, X-Api-Key header, log response snippet); test với mock backend trả 200; chưa verify với BE thật |
| **17. S1-FW-07** (#18) | Loop chính: mỗi 5s đọc mock → build payload → POST → log status | NI §8.3 loop | Quan sát Serial: cứ 5s in 1 dòng "posted 4 readings (200 OK)" | FW | 🟡 Code có (`INGEST_INTERVAL_MS=5000`, log substring `posted N readings (200 OK)`); chưa flash ESP32 + monitor |
| **18. S1-FE-01** (#19) | Trang "Battery list" hiển thị `LastReadingAt`, V/I/temp/SOC mới nhất, refresh 5s | NI §4 sơ đồ tổng | Mở web → thấy số nhảy mỗi 5s khớp ESP32 | FE | 🔴 Repo riêng (`gsu26se55/web-frontend`) — không trong `iot/` |
| **19. S1-FE-02** (#20) | Chart lịch sử voltage 1 giờ gần nhất | — | Đường line cập nhật mỗi 5s | FE | 🔴 Repo riêng — same |
| **20. S1-QA-01** (#21) | Quay video 60s demo "ESP32 → dashboard" | — | Video share team | QA | 🔴 Cần ESP32 thật + dashboard FE + record |

**Backend (BE):** task `S1-BE-01` (seed) + `S1-BE-02` (legacy endpoint verify) đã chuyển sang **`backend/overall.md` Sprint IoT-2 Phase A** (`#IoT2-02`, `#IoT2-03`). IoT track cần chốt với Thắng trước S1 ngày bắt đầu: seed data sẵn sàng + endpoint chấp nhận format ESP32 mock. **Status:** 🔴 chưa biết Thắng đã làm chưa.

**DoD sprint:** Mọi người trong team mở dashboard, thấy chart voltage nhảy realtime từ ESP32 mock. Không cần pin thật.

**Rủi ro:** Nếu backend hiện tại đòi `batteryAssetId` (GUID) thay vì serial, cần shim hoặc cập nhật sớm endpoint — không để kéo dài.

##### Sprint 1 — Tổng kết gap

| Nhóm | Items |
|------|-------|
| ✅ Đã làm (4) | #12 config.h, #15 mock_bms, #16 payload.cpp + 6 unit test, + extras (mock backend, integration test, NI §9.1 structure refactor) |
| 🟡 Code OK, cần ESP32 + monitor (4) | #13 wifi reconnect, #14 NTP sync, #17 HTTPS 200 (chỉ test với mock backend), #18 main loop log format |
| 🔴 Frontend repo khác (2) | #19 Battery list page, #20 Voltage chart — FE team làm |
| 🔴 Manual demo (1) | #21 Video 60s — cần ESP32 + FE dashboard chạy được trước |
| 🔴 Backend dep (2) | `#IoT2-02` seed data, `#IoT2-03` endpoint verify (hỏi Thắng) |
| 🔴 Config GUID mismatch (1) | Firmware dùng `11111111-1111-4111-8111-00000000000{1..4}` placeholder; backend thật có 3 pin với GUID `54754d04-...`, `2810f7d9-...`, `5e2116ec-...` (xem `iot-simulator/config/seed.yaml`). POST 4 → backend skip cả 4. Cần chọn: (A) sync firmware GUID + giảm `MOCK_BATTERY_COUNT=3`, (B) Thắng seed thêm pin 4, hoặc (C) Thắng seed lại 4 pin với GUID placeholder |

---

### Sprint 2 — Backend Device Management (P2) (2 tuần)

**Mục tiêu:** Có khái niệm "IoT device" trong hệ thống. Admin tự tạo device + sinh API key; firmware provision; backend phát hiện offline.

**Pre-requisite:** S1 chạy được.

#### Backend (BE) — chuyển sang Sprint IoT-2

> 10 task BE của Sprint 2 (entity migration, hypertable, API key + scope, admin/device endpoints, offline detection + dedup vai Customer/Staff, ApiGateway route, `IotDeviceWentOfflineEvent` SharedContracts + NotificationService consumer/template) **đã chuyển sang `backend/overall.md` Sprint IoT-2 Phase B** (`#IoT2-04..13`). **Status BE (verified 2026-06-13):** ✅ **10/10 task ĐÃ implement trong backend repo** — migration `AddIotDeviceManagement` + `AddIotSprint2Schema`, hypertable `iot_device_heartbeats` retention 30d, `IotApiKeyService` + scope, `AdminIotDevicesController` (CRUD + rotate-key + revoke-key), `IotDevicesController` (provision + heartbeat), `IotDeviceOfflineDetectionBackgroundService`, ApiGateway routes, `IotDeviceWentOfflineEvent` + `IotDeviceWentOfflineConsumer`. Label GitHub `status: init` outdated (backend devs chưa update).
>
> **Lưu ý route thực:** Backend dùng `/api/iot-devices/{provision,heartbeat}` KHÔNG có `/v1/` như tasksprint mô tả. Firmware đã align route thực.

#### Backlog — Firmware

> **Trạng thái:** ✅ Done · 🟡 Code OK / cần verify hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **21. S2-FW-01** (#22) | Hỗ trợ load `apiKey + deviceCode` từ NVS (đầu tiên flash hard-coded, sau đổi qua config portal hoặc Serial) | NI §9.1 | Đổi key trong NVS → firmware dùng key mới mà không reflash | FW | 🟡 Code: `src/config/{nvs_store, device_identity}.{h,cpp}` + `src/cli/serial_cli.{h,cpp}` Serial CLI commands (`show`/`set apikey`/`set devcode`/`clear`/`reboot`). Hot reload runtime: next POST đọc `identity::apiKey()` lấy giá trị mới. **Cần ESP32 verify** `set apikey iotk_xxx` không cần reflash |
| **22. S2-FW-02** (#23) | Provision flow boot: nếu Status=Provisioning thì gọi `/provision` 1 lần, lưu configJson về NVS | OV §B1 | Sau provision: log "provisioned, polling=5s" | FW | 🟡 Code: `src/provision/provision.{h,cpp}` flow `loadProvisioned → POST /api/iot-devices/provision → parse IotDeviceProvisionResultDto → lưu NVS (provd, pollIntS, hbIntS, siteid, ntpsv) → set provd=1`. Retry 30s cooldown. Test mock backend OK. **Cần backend dev** verify log đúng "provisioned, polling=5s" |
| **23. S2-FW-03** (#24) | Heartbeat task riêng (mỗi **60s** — đồng bộ MO §52.4): gửi chip temp → `Temperature`, free heap → `MemoryUsageMb`, WiFi RSSI → `SignalStrengthDbm`, độ sâu queue NVS → `LocalQueueDepth`. `Cpu`/`DiskFreeMb` để null vì ESP32 không có khái niệm Linux CPU/disk (MO §52.2 field mapping) | NI §7.1 heartbeat, MO §52.4 | Backend hypertable có row mới mỗi 60s với đúng tập field ESP32 | FW | 🟡 Code: `src/telemetry/heartbeat.{h,cpp}` POST `/api/iot-devices/heartbeat` với 10 field (gồm bonus `FreeMemoryPercent` cho board không PSRAM). Bounds `[10s, 1h]` defensive. Test mock backend OK. **Cần backend dev** verify hypertable `iot_device_heartbeats` có row mỗi 60s |
| **24. S2-FW-04** (#25) | Header chuẩn cho mọi request: `X-Api-Key`, `X-Device-Code` | NI §7.4 | Backend log thấy header | FW | 🟡 Code: `src/net/http_client.cpp` `addHeader` cả 2 header runtime từ identity store. Bonus `httpPostJsonRecv()` variant cho response > 256 chars. **Cần backend dev** tail log verify 2 header xuất hiện |

#### Backlog — Frontend

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **25. S2-FE-01** (#26) | Trang Admin "IoT Devices" — list + create modal hiện API key đúng 1 lần (copy-to-clipboard) | NI §11 E | Admin tạo device → hiện key → reload không thấy lại key | FE | 🔴 Repo riêng (`gsu26se55/web-frontend` hoặc tương đương) — không trong `iot/`. Backend admin endpoint `POST /api/admin/iot-devices` đã có. |
| **26. S2-FE-02** (#27) | Trang detail device: trạng thái (Provisioning/Active/Offline), LastSeenAt, mappings, nút rotate key | — | Bấm rotate → key mới, key cũ 401 | FE | 🔴 Repo riêng. Backend đã có `POST /api/admin/iot-devices/{id}/rotate-key` + `revoke-key`. |

#### Backlog — QA

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **27. S2-QA-01** (#28) | Test scenario: tắt ESP32 5 phút → expect alert DeviceOffline + email/push | OV §B4 | Pass | QA | 🔴 Cần ESP32 thật + backend dev + NotificationService chạy. Backend `IotDeviceOfflineDetectionBackgroundService` đã có (chạy mỗi 2 phút quét `LastSeenAt < now-5min`). |

**DoD sprint:** Admin có thể tự tạo device, đưa key cho team firmware, ESP32 provision tự động và bị mark Offline sau khi rút điện.

##### Sprint 2 — Tổng kết gap

| Nhóm | Items |
|------|-------|
| 🟡 Code OK, cần ESP32 + backend (4) | #22 S2-FW-01 (Serial CLI hot reload), #23 S2-FW-02 (provision log), #24 S2-FW-03 (hypertable row mỗi 60s), #25 S2-FW-04 (header xuất hiện trong BE log) |
| 🔴 Frontend repo khác (2) | #26 S2-FE-01 Admin IoT Devices page, #27 S2-FE-02 Detail + rotate — backend admin endpoints đã ready |
| 🔴 Manual scenario test (1) | #28 S2-QA-01 — cần ESP32 + backend dev + NotificationService |
| ✅ Backend dep verified | `#IoT2-04..13` (10 task) đã implement xong trong backend repo — KHÔNG block FW |
| 🔴 Bonus — files mới cần code review | `nvs_store`, `device_identity`, `serial_cli`, `provision`, `heartbeat` + http_client mở rộng + mock-backend Sprint 2 endpoints |
| 🔧 8 bug fix sau 2 review pass | (1) nvsBegin RO→RW, (2) MemoryUsageMb PSRAM+round, (3) provision retry cooldown ordering, (4) heartbeatBegin bounds, (5) device_identity constants+length check, (6) loadProvisioned defensive bounds, (7) FreeMemoryPercent field mới, (8) Serial.flush trước ESP.restart |

---

### Sprint 3 — Resilience & Production Contract (P1) (2 tuần)

**Mục tiêu:** Mất mạng/mất broker không mất data. Contract production chuẩn (`Idempotency-Key`, `deviceTimestamp`, clock skew, outlier reject, calibration apply).

**Pre-requisite:** S2 xong.

#### Backlog — Firmware

> **Trạng thái:** ✅ Done · 🟡 Code OK / cần verify hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **28. S3-FW-01** (#29) | `src/queue/local_queue.cpp` — buffer NVS (FIFO, key = epoch), max 200 batch | NI §9.1, OV §B7 | Unit test: enqueue 200 → 201 đẩy ra cái cũ nhất | FW | 🟡 Code: `src/queue/{local_queue,queue_index}.{h,cpp}` — **LittleFS-backed** (NI §9.1 cho phép "NVS/LittleFS"; NVS default partition 24KB không fit 200 batch × ~1KB). Pure FIFO logic extracted ra `queue_index.h` để test native. **14 unit tests PASS** gồm AC trọng tâm `test_enqueue_200_then_201st_drops_oldest`. LittleFS persist verify trên ESP32 còn pending. |
| **29. S3-FW-02** (#30) | Mỗi batch sinh `Idempotency-Key` (UUIDv4 hoặc deviceCode+epoch+seq) lưu kèm trong queue | NI §7.4 | Hai lần POST cùng key → backend chỉ ghi 1 lần (xem S3-BE-03) | FW | 🟡 Code: `src/core/idempotency_key.{h,cpp}` — UUIDv4 (default) + alt `deviceCode-epoch-seq`. Lưu kèm trong queue (`<epoch>.idem` file). HTTP `httpPostJsonWithIdempotency` thêm header. **5 unit tests + Sprint 3 integration test** verify replay (POST 2 lần cùng key → mock backend cache → cùng response). |
| **30. S3-FW-03** (#31) | Retry exponential backoff (base 2s, max 5 phút, jitter ±20%) | NI §1 #2 | Tắt backend → log thấy backoff tăng dần; bật lại → flush hết | FW | 🟡 Code: `src/net/backoff.{h,cpp}` — `kBackoffBaseMs=2000, kBackoffMaxMs=300000, kBackoffJitterPct=0.20`. Bonus `isTransientFailure()` phân biệt 4xx permanent (drop) vs 5xx/network transient (retry) — tránh infinite retry loop khi backend trả validation error. **8 unit tests PASS** (growth, cap, jitter, transient/permanent classification). |
| **31. S3-FW-04** (#32) | Đổi contract sang **production** (NI §7.4 + MO §52.5): payload có `deviceTimestamp` (ISO8601), readings dùng `batteryAssetSerial`, mỗi reading tag `sensorSourceCode` + `sourceType` **theo đúng nguồn vật lý** (MO §52.9 bảng): BMS-relay qua RS485 → `sourceType=Bms (1)`, `sensorSourceCode="primary"`; INA226 → `sourceType=IotGateway (2)`, `sensorSourceCode="redundant"`; DS18B20 → `sourceType=IotGateway (2)`, `sensorSourceCode="external-temp"`. **KHÔNG hard-code `sourceType=2` cho tất cả** — sẽ phá cross-source validation §1.6.6 (so Bms vs IotGateway). Field optional khác: `cycleCount`, `sohPercent`, `chargingState`, `bmsErrorCode` (≤ 64 chars) | NI §7.4, MO §52.5, §52.9 | Payload có ít nhất 2 reading cùng battery, khác `sourceType`/`sensorSourceCode`; backend nhận đúng schema | FW | 🟡 Code: `src/core/payload.cpp::buildProductionBatchPayload` + extend `core::SensorReading` (sohPercent, chargingState, bmsErrorCode[65], sensorSourceCode, sourceType). Mock sinh **3 nguồn per battery**: BMS primary (sourceType=1) + INA226 redundant (sourceType=2) + DS18B20 external-temp (sourceType=2 / sensorSourceCode khác). **8 unit tests + Sprint 3 integration** verify cross-source pair + 3-source mapping + 64-char bmsErrorCode boundary. Backend dev (#IoT2-14..20) verified ready. |
| **32. S3-FW-05** (#33) | Status LED (GPIO48 RGB): xanh = online, vàng = queue có data, đỏ = mất mạng | WD §1 | Quan sát LED trực quan đúng trạng thái | FW | 🟡 Code: `src/ui/{status_led,led_palette}.{h,cpp}` — GPIO48 WS2812 với 5 state (Off/Online/Queued/Offline/Provisioning). Priority `main.cpp::updateStatusLed`: !wifi→red > queue>0→yellow > else→green. Brightness ≤ 32/255 (tránh chói). **7 unit tests palette** verify color mapping + distinguishability. Quan sát LED thực trên ESP32 pending. |

#### Backend (BE) — chuyển sang Sprint IoT-2

> 7 task BE của Sprint 3 (contract production + backward compat, clock skew, idempotency, outlier + auto-disable, mapping check, calibration apply + Redis cache, LastSeenAt update) **đã chuyển sang `backend/overall.md` Sprint IoT-2 Phase C** (`#IoT2-14..20`). **Status BE (verified 2026-06-14):** ✅ **7/7 task ĐÃ implement trong backend repo** — `BatchIngestSensorReadingsCommandHandler.cs` đầy đủ: `ClockSkewMaxMinutes=5` + metric `clock_drift` (#IoT2-15), `IdempotencyTtl=24h` + dedup (#IoT2-16), `OutlierThresholdPerHour=50` + auto-Decommissioned (#IoT2-17), serial→Id resolve (#IoT2-18), `IIotCalibrationCache` Redis (#IoT2-19), `LastSeenAt` update (#IoT2-20). DTO `SensorReadingItem` full Sprint 3 fields.
>
> **Lưu ý cho FW:** route thực tế là `/api/sensor-readings/batch` (giữ nguyên từ Sprint 1), response **201 Created** (KHÔNG phải 200). Firmware đã handle 2xx range.

#### Backlog — QA

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **33. S3-QA-01** (#34) | Test resilience: tắt WiFi 5 phút giữa luồng ingest → bật lại → đếm row DB == số batch ESP32 đã sinh | OV §B7 | Pass, không trùng, không mất | QA | 🔴 Cần ESP32 thật + backend dev + DB access — manual test. Firmware đã có queue + idempotency + backoff sẵn sàng cover scenario này. |
| **34. S3-QA-02** (#35) | Test clock skew: chỉnh ESP32 NTP lệch 10 phút → reading bị reject | NI §12 #1 | Backend trả 400; metric tăng | QA | 🔴 Cần ESP32 + manipulate NTP. Firmware đã handle 400 permanent fail (drop batch). Sprint 3 integration test đã verify backend trả 400 cho clock_drift. |

**DoD sprint:** "Tôi rút WiFi router 5 phút, cắm lại — backend nhận đủ data, không trùng" — kiểm chứng trực tiếp.

##### Sprint 3 — Tổng kết gap

| Nhóm | Items |
|------|-------|
| ✅ Backend dep verified | `#IoT2-14..20` (7 task) đã implement đầy đủ trong backend repo — KHÔNG block FW |
| 🟡 Code OK, cần ESP32 + backend (5) | #29 S3-FW-01 (LittleFS persist), #30 S3-FW-02 (idempotency 24h cross-session), #31 S3-FW-03 (backoff + flush khi backend back up), #32 S3-FW-04 (backend accept full Sprint 3 schema), #33 S3-FW-05 (LED visual) |
| 🔴 Manual scenario test (2) | #34 S3-QA-01 (WiFi 5 phút resilience), #35 S3-QA-02 (clock skew NTP) — cần ESP32 + backend dev + manual manipulation |
| 🔧 Refactor/extras Sprint 3 không yêu cầu nhưng đã làm | Tách `queue/queue_index.h` (testable native), tách `ui/led_palette.h` (testable native), `isTransientFailure()` phân biệt 4xx/5xx fix infinite retry loop, 3-source mock generation (BMS+INA226+DS18B20) |
| 📊 Test coverage | **56 test cases** PASS — test_payload (6) + test_payload_v3 (8) + test_idempotency (5) + test_backoff (8) + test_queue (14) + test_led_palette (7) + S1 integration (4) + S3 integration (4) |

---

### Sprint 4 — MQTT Broker & Bridge (P3) (2 tuần)

**Mục tiêu:** Streaming realtime <100ms qua MQTT-over-TLS; offline detection tức thì qua LWT; downlink command từ backend xuống firmware.

**Pre-requisite:** S3 xong (firmware đã có contract production + queue).

#### Backlog — Infra

> **Trạng thái:** ✅ Done (live verified với Mosquitto broker thật) · 🟡 Code OK / cần verify hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **35. S4-INF-01** (#36) | `infra/mqtt/docker-compose.yml` chạy EMQX (khuyến nghị, dashboard sẵn) hoặc Mosquitto; expose 1883 (dev) + 8883 (TLS) | NI §8.2 | `docker compose up` → EMQX dashboard `localhost:18083` truy cập | INF | ✅ Standalone compose `infra/mqtt/docker-compose.yml` + dev compose có Mosquitto (chọn thay EMQX → khớp backend `Mqtt__Host=mosquitto`, mất dashboard 18083). Healthcheck TCP probe. Live verify: broker start OK port 1883 + 8883 |
| **36. S4-INF-02** (#37) | Sinh CA tự ký + server cert cho broker (script `infra/mqtt/scripts/gen-certs.sh`) | NI §8.2 | TLS handshake test bằng `mosquitto_pub -p 8883 --cafile ca.crt` pass | INF | ✅ Script + CA `basicConstraints=CA:TRUE` extension (fix bug round 2 — thiếu extension gây "invalid CA certificate"). SAN: localhost/mosquitto/iot-mosquitto/127.0.0.1 + optional `MQTT_SAN_IP`. Live verify: TLS 8883 handshake PASS + openssl verify chain OK |
| **37. S4-INF-03** (#38) | `acl.conf` (MO §52.14): publish `solar/+/{deviceCode}/telemetry`, `solar/{deviceCode}/heartbeat`, `solar/{deviceCode}/status`, `solar/{deviceCode}/cmd/ack`; subscribe `solar/{deviceCode}/cmd` | NI §8.1, §11 C, MO §52.14 | Test ACL: device A không pub được topic của device B; device pub được ack lên `cmd/ack` của chính nó | INF | ✅ Rewrite từ schema cũ `telemetry/%u` → `solar/%u/+/telemetry` khớp backend `MqttTopicMap`. Pattern per-device: write telemetry/heartbeat/status/cmd-ack, read cmd. Live verify 4/4 ACL tests: device A pub topic B → DENIED; device pub own cmd-ack → ALLOWED; device pub own cmd (read-only) → DENIED; retained "online" subscribed |
| **38. S4-INF-04** (#39) | Bridge service backend dùng user `backend-bridge`: subscribe `solar/#` + publish `solar/+/cmd` | NI §8.4 | Login bằng `backend-bridge` qua MQTT client → list topic OK | INF | ✅ `user backend-bridge` + `topic readwrite solar/#` + `topic read $SYS/#`. Bootstrap.sh sinh PBKDF2 hash. Live verify: backend-bridge auth + subscribe `solar/#` nhận tất cả message từ device test |

#### Backend (BE) — đã verify implement trong `backend/` repo

> 6 task BE Sprint 4 (`#IoT2-21..26` Phase D) **đã implement xong** trong `capstone/backend/services/BatteryService/src/BatteryService.Infrastructure/Mqtt/`:
> - ✅ `#IoT2-21` MQTTnet 4.3.6.1152 + ManagedClient NuGet
> - ✅ `#IoT2-22` `MqttBridgeBackgroundService.cs` subscribe 4 wildcard topic (`solar/+/+/telemetry`, `solar/+/heartbeat`, `solar/+/status`, `solar/+/cmd/ack`)
> - ✅ `#IoT2-23` `DispatchTelemetryAsync` reuse `BatchIngestSensorReadingsCommand`
> - ✅ `#IoT2-24` `DispatchStatusAsync` (LWT) mark device Offline + publish `IotDeviceWentOfflineEvent` qua outbox
> - ✅ `#IoT2-25` `IMqttBridgePublisher.PublishCommandAsync` + endpoint `POST /api/admin/iot-devices/{id}/command`
> - ✅ `#IoT2-26` MQTT credential per-device (PBKDF2 hash `PBKDF2$sha256$10000$...`, plaintext return 1 lần qua `IotDeviceCreatedDto.MqttPassword`)
>
> ⚠ **Gap deployment thực tế (catch qua audit):**
> 1. Backend `Mqtt__Enabled` default `false` trong cả 3 appsettings → bridge silent skip nếu deployer quên enable. Documented prominent trong `infra/mqtt/README.md` "⚠ BẮT BUỘC — Enable backend MQTT bridge"
> 2. Backend hash format `PBKDF2$sha256$...` (SHA256) KHÔNG khớp Mosquitto `$7$<iter>$...` (SHA512) → không paste thẳng vào passwd file → workaround `scripts/add-device.sh` re-hash plaintext qua `mosquitto_passwd`

#### Backlog — Firmware

> **Trạng thái:** ✅ Done · 🟡 Code OK / cần verify ESP32 hardware · 🔴 Gap chưa làm

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **39. S4-FW-01** (#40) | `src/net/mqtt_client.{h,cpp}` dùng `PubSubClient` + `WiFiClientSecure` (CA cert nạp qua LittleFS `data/ca_cert.pem`) | NI §8.3 | Connect 8883 thành công | FW | 🟡 Code: 319 dòng + `setBufferSize(MQTT_MAX_PACKET_SIZE=4096)` + `setKeepAlive(30)` + PEM marker validation + 9-state human-readable error map. NTP gate `timeIsSynced()` trước TLS connect (fix round 6 — tránh cert "not yet valid" loop spam khi ESP32 boot time=1970). `warnIfCaseMismatch()` chống deviceCode mixed-case (round 3). Compile OK; **cần ESP32 thật + LittleFS ca_cert.pem để verify 8883 connect** |
| **40. S4-FW-02** (#41) | LWT: `willTopic=solar/{dev}/status`, payload `offline`, QoS 1, retain | NI §8.3 | Rút điện → broker push `offline` ngay; backend mark Offline | FW | 🟡 Code: `s_mqtt.connect(..., willTopic, 1, true, "offline")` trong tryConnect. Backend `LastWillHandler` xử lý "offline" payload + tạo `Alert(DeviceOffline)` + publish `IotDeviceWentOfflineEvent`. **Cần rút điện ESP32 thật để verify timing < 90s** (S4-QA-02 hardware) |
| **41. S4-FW-03** (#42) | Sau connect: publish `online` retained lên `status` + subscribe `solar/{dev}/cmd` | NI §8.3 | EMQX dashboard thấy retained `online` | FW | 🟡 Code: tryConnect post-connect publish `"online"` retain=true → override LWT; subscribe `solar/{dev}/cmd` QoS 1. Verified subscriber nhận "online" retained khi join sau publish (live broker test). **Cần ESP32 thật + dashboard để full verify** |
| **42. S4-FW-04** (#43) | Đổi `publishTelemetry()` thay HTTPS POST; HTTPS vẫn giữ cho flush queue + firmware-check | NI §3 hybrid | Quan sát latency < 1s từ poll đến row DB | FW | 🟡 Code: `ingestViaMqtt()` per-battery split (kSourcesPerBattery=3) → `mqttPublishTelemetry(serial, payload, len)` trên topic `solar/{dev}/{serial}/telemetry`. Fallback HTTPS nếu disconnect hoặc streak ≥ 3. Heartbeat + queue flush vẫn HTTPS (đúng spec). Compile OK. **Latency p95 < 500ms cần lab test** (S4-QA-01) |
| **43. S4-FW-05** (#44) | `src/cmd/command_handler.cpp` parse downlink (`set_interval`, `trigger_ota`, `request_heartbeat`) + publish ack `{cmdId, status, message}` | NI §8.3, MO §52.14 | Backend POST cmd `set_interval=2` → ESP32 đổi nhịp poll → bridge log ack `{cmdId, status:"ok"}` | FW | 🟡 Code: refactor 2 modules — `cmd_logic.{h,cpp}` (pure, testable native — 23 tests PASS) + `command_handler.{h,cpp}` (side-effect wrappers gọi `mqttPublishCmdAck` + `telemetry::heartbeatSendNow` + `setPollingHandler` lambda). Ack JSON 2-tier buffer overflow safety (256 → snprintf minimal fallback). Type matching BC `_` và `-`. **Cần backend POST cmd thật để verify ack flow E2E** |
| **44. S4-FW-06** (#45) | Fallback MQTT fail N → HTTPS cho batch đó (queue vẫn ưu tiên MQTT lại sau) | NI §13 #5 SPOF | Tắt broker → vẫn ingest qua HTTPS; bật broker → quay lại MQTT | FW | 🟡 Code: `MQTT_PUBLISH_FAIL_THRESHOLD=3` consecutive streak counter; auto-reset on `tryConnect` success. ingestOnce check `mqttIsConnected() && consecutiveFail < threshold` → MQTT, else HTTPS path với idempotency. Compile OK. **Cần lab test on/off broker để verify switch behavior** |

#### Backlog — QA

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **45. S4-QA-01** (#46) | Đo latency end-to-end MQTT (publish → DB row): kỳ vọng p95 < 500ms | NI §3.2 | Có log đo, đạt | QA | 🔴 **GAP** — cần ESP32 + broker + backend + DB chạy thật trong lab. Tooling chuẩn bị xong (broker + scripts). FW config sẵn. Chỉ cần phần cứng + setup |
| **46. S4-QA-02** (#47) | Test LWT vs job 5 phút: rút điện → so sánh thời gian alert | OV §B4 | LWT nhanh hơn rõ rệt | QA | 🔴 **GAP** — cần ESP32 thật + rút phích cắm + monitor backend log `IotDeviceWentOfflineEvent`. Kỳ vọng LWT trigger ~45-90s (1.5x keepalive 30s) vs job 5 phút |

**DoD sprint:** Demo "rút phích cắm ESP32 → trong 90s dashboard hiện 'OFFLINE'" + "FE admin gửi command → ESP32 đổi tần suất ngay".

##### Sprint 4 — Tổng kết gap

| Nhóm | Items |
|------|-------|
| ✅ Done — live verified với broker (4) | #36 docker-compose, #37 gen-certs + TLS handshake, #38 ACL 4/4 test, #39 backend-bridge user |
| 🟡 Code OK, cần ESP32 + integration test (6) | #40 mqtt_client 8883 connect, #41 LWT timing, #42 retained online + sub cmd, #43 latency < 1s, #44 cmd ack flow E2E, #45 broker on/off fallback |
| 🔴 Hardware lab pending (2) | #46 S4-QA-01 latency p95 (cần ESP32 + bench), #47 S4-QA-02 LWT timing rút điện |
| ✅ Backend dep verified | `#IoT2-21..26` đã implement trong backend repo |
| 🟡 Bug catch qua 14 vòng review (8) — đã fix | (1) Backend hash `PBKDF2$sha256$` ≠ Mosquitto `$7$` → `add-device.sh` workaround; (2) CA cert thiếu `basicConstraints=CA:TRUE` → gen-certs.sh fix; (3) Case mismatch deviceCode/mqtt_username → `warnIfCaseMismatch` + lowercase placeholder; (4) NTP→TLS handshake race → `mqttTick` NTP gate; (5) Backend `Mqtt__Enabled=false` silent default → README prominent warning; (6) Root `.gitignore data/` over-broad ignore placeholder → `/data/` anchored; (7) Ack JSON 256-byte buffer tight margin → 2-tier defensive fallback; (8) Sprint 2 `show` CLI thiếu MQTT status → mở rộng với pub/fail/streak |
| 🔐 Security hardening (3) | `add-device.sh` stdin mode (chống shell history leak); `gen-certs.sh` cert expiry warning + key handling block; `SECURITY.md` consolidate threat model + audit queries |
| 🤖 Automation guard (1) | `.github/workflows/firmware-ci.yml` — native tests + multi-env build + shellcheck + secret audit + size guard (regression bảo vệ dài hạn) |

**Files mới (11):**
- `firmware-esp32/src/net/mqtt_client.{h,cpp}` (S4-FW-01/02/03/06)
- `firmware-esp32/src/cmd/command_handler.{h,cpp}` (S4-FW-05 wrapper)
- `firmware-esp32/src/cmd/cmd_logic.{h,cpp}` (pure decision logic)
- `firmware-esp32/test/test_cmd_logic/test_cmd_logic.cpp` (23 native tests)
- `firmware-esp32/data/ca_cert.pem.placeholder` (LittleFS slot hint)
- `infra/mqtt/docker-compose.yml` (S4-INF-01)
- `infra/mqtt/scripts/gen-certs.sh` (S4-INF-02)
- `infra/mqtt/scripts/add-device.sh` (gap fix backend hash format)
- `infra/mqtt/scripts/remove-device.sh` (rotate/retire flow)
- `infra/mqtt/SECURITY.md` (security audit)
- `.github/workflows/firmware-ci.yml` (CI automation)

**Files modified (9):**
- `firmware-esp32/{include/config.h,include/config.example.h,platformio.ini,src/main.cpp,src/cli/serial_cli.cpp,.gitignore,README.md}`
- `infra/mqtt/{README.md,mosquitto/config/acl.conf}`
- `.gitignore` root (fix `/data/` anchor)
- `iot-task-list.md` (mark Sprint 4 done)

**Test coverage:** 71/71 native tests PASS (Sprint 1: 6, Sprint 3: +30, Sprint 4 cmd_logic: +23, S2 LED: +7, idempotency: +5). ESP32 build SUCCESS multi-env (esp32-s3-devkitc-1 + example-blink).

---

### Sprint 5 — Hardware Integration (P4) (2 tuần) *— có thể song song với S6*

**Mục tiêu:** Đưa pin/BMS thật vào hệ thống. ESP32 đọc 1 pin BMS qua Modbus → mở rộng multi-drop 3–4 pin → bổ sung sensor phụ INA226/DS18B20/SHT31 theo `wiring-diagram.md`.

**Pre-requisite:** Linh kiện Cấp 1+2 đã về (đặt từ S0); S4 xong (firmware MQTT ổn) hoặc S3 (HTTPS ổn) nếu chưa làm S4.

#### Backlog — Hardware lab

> **Trạng thái:** 🔴 **TOÀN BỘ GAP** — chờ procurement linh kiện Cấp 1/2 (đặt từ S0 #9/#10/#11) + lab session. FW driver đã sẵn sàng (S5-FW-01..07) chỉ chờ hardware connect.

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **47. S5-HW-01** (#48) | Đấu ESP32 ↔ MAX485 theo WD §3 (GPIO17 TX→DI, GPIO18 RX→RO, GPIO16 DE+RE nếu không phải auto-direction), chung GND | WD §3, OV nguyên tắc common ground | Bằng đồng hồ kiểm tra liên tục VCC, không ngắn mạch | HW | 🔴 **GAP** — cần MAX485 module (đặt S0 #9), breadboard hoặc PCB, đồng hồ |
| **48. S5-HW-02** (#49) | Lập bảng register map cho từng model BMS đang dùng (voltage, current, temp, soc, soh, cycle, error, scale, offset) — verify bằng USB-RS485 + Modbus Poll trên laptop trước | OV §A4, §A12 #47 | Có file `docs/bms-register-map-{model}.md` | HW | 🔴 **GAP** — cần BMS thật (S0 #10) + USB-RS485 + Modbus Poll software. **Critical**: FW có sẵn 3 preset (Daly/JBD/JK) trong `src/bms/bms_register_map.h` nhưng PHẢI verify đúng model BMS thực tế nhà cung cấp trước khi flash, nếu khác → tạo `kGenericBmsMap` custom + đổi `BMS_MODEL=4` |
| **49. S5-HW-03** (#50) | Đổi `unitId` của từng pin (1, 2, 3, 4) bằng phần mềm hãng | WD §3 quy tắc | Từng BMS phản hồi đúng unitId mới | HW | 🔴 **GAP** — cần BMS thật + phần mềm vendor (Daly Smart App / JBD Bluetooth / JK-BMS) để đổi unitId mặc định (thường = 1) |
| **50. S5-HW-04** (#51) | Đấu RS485 multi-drop: 4 BMS song song cùng A/B + 120Ω 2 đầu bus | WD §3 | Modbus Poll quét được cả 4 unitId | HW | 🔴 **GAP** — cần 4 BMS + cáp đôi xoắn shielded (S0 #9) + 2 điện trở 120Ω terminator |
| **51. S5-HW-05** (#52) | Đấu DS18B20 (GPIO4 + 4.7kΩ pull-up) gắn vào thân pin | WD §4.1 | Quét bus 1-Wire ra được 64-bit address | HW | 🔴 **GAP** — cần DS18B20 sensor + 4.7kΩ pull-up. **Note FW**: `ds18b20Begin()` đã enumerate sensor address tự động, mapping theo thứ tự = battery serial |
| **52. S5-HW-06** (#53) | Đấu INA226 + SHT31 chung I2C (GPIO8 SDA, GPIO9 SCL) | WD §4.2 | I2C scanner thấy 0x40 + 0x44 | HW | 🔴 **GAP** — cần INA226 module + SHT31 module + shunt resistor 10mΩ. FW dùng default address 0x40 (INA226) + 0x44 (SHT31) |
| **53. S5-HW-07** (#54) | Đo điện áp pin thật bằng Fluke 87V (làm chuẩn cho calibration sprint sau) | OV §A11 | Ghi số vào sổ tay | HW | 🔴 **GAP** — cần Fluke 87V (mượn hoặc mua); input cho QA #62 + calibration Sprint 7 |

#### Backlog — Firmware

> **Trạng thái:** ✅ **TOÀN BỘ DONE** — code complete + 94/94 native tests PASS + 3 envs build SUCCESS (mock + real + blink) + CI guard + backend contract verified. Hardware integration verify thuộc Hardware track (S5-HW-01..07).

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **54. S5-FW-01** (#55) | `src/bms/modbus_bms.cpp` — đọc 1 BMS đúng register map; thêm `bmsErrorCode` ≤ 64 chars + `cycleCount`/`sohPercent`/`chargingState`; tag `sourceType=Bms (1)`, `sensorSourceCode="primary"` | NI §9.1, OV §A4, MO §52.5 | Serial in voltage thực, khớp Fluke ±0.2V; payload có `bmsErrorCode` khi BMS lỗi | FW | ✅ **DONE** — 3 preset Daly/JBD/JK + `decodeErrorCode` short codes (13 flags fit ≤ 64 chars) + retry 1x. 23 native test PASS (decode V/I/T/SOC bounds + signed current + Kelvin→C bias). GitHub issue #55 → In Review |
| **55. S5-FW-02** (#56) | Điều khiển DE/RE (nếu không auto-direction): HIGH trước gửi, LOW sau nhận | NI §12 #3, WD §3 | Modbus reply ổn định | FW | ✅ **DONE** — `preTransmission` HIGH + `delayMicroseconds(10)` guard; `postTransmission` flush() + `delayMicroseconds(50)` T3.5 inter-frame. ModbusMaster lib re-call `begin()` safety verified (chỉ update `_u8MBSlave` + tx buffer, KHÔNG touch pre/post callbacks). GitHub issue #56 → In Review |
| **56. S5-FW-03** (#57) | Multi-drop: loop unitId 1→N, gom 1 batch nhiều `SensorReading` | NI §5, OV §B2 (a) | 1 batch chứa 4 reading khác serial | FW | ✅ **DONE** — `modbusReadMultiDrop` loop `BMS_UNIT_ID_START..START+COUNT-1`, skip-not-fail logic (1 BMS chết không block 3 BMS khác). 20ms inter-poll spacing. GitHub issue #57 → In Review |
| **57. S5-FW-04** (#58) | `src/sensor/ina226.cpp` đọc V/I I2C → batch với `sourceType=IotGateway (2)` + `sensorSourceCode="redundant"`. **⚠ ADR-017:** CHỈ cross-source, KHÔNG energy metrics | OV §A5, MO §52.9, §53.1 | Reading redundant xuất hiện DB cùng timestamp BMS; cross-source pair tạo (S6) | FW | ✅ **DONE** — INA226 lib (robtillaart) + replicate readings cho N battery serial. Defensive range check ±50V/±max×1.2A (catch I2C lỗi). GitHub issue #58 → In Review |
| **58. S5-FW-05** (#59) | `src/sensor/ds18b20.cpp` đọc nhiệt thân pin (mỗi pin 1 con DS18B20) → `sourceType=IotGateway (2)`, `sensorSourceCode="external-temp"` | OV §A5, MO §52.9 | Backend nhận temp khác source; cross-source pair BMS temp tạo | FW | ✅ **DONE** — OneWire + DallasTemperature, enumerate addresses tự động, trigger 1 conversion cho all sensors (tiết kiệm 750ms × N). `DEVICE_DISCONNECTED_C` detection. GitHub issue #59 → In Review |
| **59. S5-FW-06** (#60) | `src/sensor/sht31.cpp` ambient → POST `/api/ambient/readings/batch` với `Source=IotSensor`, `SourceDeviceId=<DeviceCode>`, scope `EnvironmentalIngest` | OV §A6, MO §52.9bis | Reading xuất hiện trong `ambient_readings` với đúng `Source/SourceDeviceId` | FW | ✅ **DONE** — Adafruit_SHT31 + I2C shared. **4 critical bugs phát hiện + fix qua audit**: (1) sai endpoint `/api/ambient-readings/batch` → `/api/ambient/readings/batch` (verified controller); (2) thiếu required `siteId` (wire `sht31SetSiteId()` từ provision); (3) sai field `temperature` → `ambientTemperature`; (4) sai enum format string `"IotSensor"` → integer `1` (no JsonStringEnumConverter trên backend). **⚠ Deployment**: admin tạo IoT device PHẢI set `ApiKeyScopes` bitmask include `EnvironmentalIngest=4`. GitHub issue #60 → In Review |
| **60. S5-FW-07** (#61) | Compile flag chuyển giữa `mock_bms` và `modbus_bms` | NI §1 #4 | Flag `USE_MOCK_BMS=1` → cùng binary chạy mock | FW | ✅ **DONE** — `src/bms/bms_source.{h,cpp}` dispatcher; `[env:esp32-s3-real]` (USE_MOCK_BMS=0) trong `platformio.ini`. CI build cả 2 env (mock + real) catch regression dài hạn. example-blink env vẫn backward compat. GitHub issue #61 → In Review |

#### Backlog — QA

| ID | Task | Spec | Acceptance | Track | Status |
|----|------|------|-----------|-------|--------|
| **61. S5-QA-01** (#62) | So sánh số ESP32 vs Fluke trên 4 pin → ghi lệch số → input cho calibration S7 | OV §B5 | Bảng số liệu lệch | QA | 🔴 **GAP** — cần ESP32 + 4 BMS thật + Fluke 87V + sổ tay. Pending until Hardware (S5-HW-07) đã đo Fluke baseline xong |

**DoD sprint:** ESP32 đọc 4 pin LiFePO4 thật, dashboard thấy voltage thực, không còn dùng `mock_bms` trong môi trường lab.

##### Sprint 5 — Tổng kết gap

| Nhóm | Count | Items |
|------|-------|-------|
| ✅ **DONE — Firmware** | **7/7** | #55 S5-FW-01 modbus driver + register map, #56 S5-FW-02 DE/RE, #57 S5-FW-03 multi-drop, #58 S5-FW-04 INA226 redundant, #59 S5-FW-05 DS18B20 external-temp, #60 S5-FW-06 SHT31 ambient (+ 4 critical bug fixes), #61 S5-FW-07 USE_MOCK_BMS dispatcher. **GitHub Project Board: 7/7 issues → In Review** |
| 🔴 **GAP — Hardware** | **7/7** | #48 đấu MAX485, #49 lập register map BMS thực tế, #50 đổi unitId, #51 RS485 multi-drop, #52 DS18B20 pull-up, #53 INA226+SHT31 I2C, #54 đo Fluke baseline. **Block reason**: cần procurement linh kiện Cấp 1+2 + lab session |
| 🔴 **GAP — QA** | **1/1** | #62 S5-QA-01 so sánh ESP32 vs Fluke. **Block reason**: cần Hardware S5-HW-07 xong trước |
| 🚨 **Bug catch qua audit (4)** — đã fix | 4 | (1) SHT31 endpoint route wrong → `/api/ambient/readings/batch`; (2) SHT31 missing required `siteId` (wire từ provision); (3) SHT31 field `temperature` → `ambientTemperature` + enum INT format (no JsonStringEnumConverter); (4) `ingestViaMqtt` slot indexing `b * 3` BROKE real path khi battery thiếu sensor → refactor group-by-serial dynamic |
| 🔧 **Polish + CI + docs (5)** — đã làm | 5 | CI thêm `esp32-s3-real` env build + threshold 94 tests; `show` CLI Sprint 5 sensor section; logStatsPeriodic sensor counters; dedup `kSourcesPerBatterySprint5`; doc sync README/iot-task-list/tasksprint với Status column |

**Files mới (10):**
- `firmware-esp32/src/bms/{bms_register_map,modbus_bms,bms_source}.{h,cpp}` (S5-FW-01/02/03/07)
- `firmware-esp32/src/sensor/{ina226,ds18b20,sht31}.{h,cpp}` (S5-FW-04/05/06)
- `firmware-esp32/test/test_bms_register_map/test_bms_register_map.cpp` (23 native tests)

**Files modified (6):**
- `firmware-esp32/{include/config.h,include/config.example.h,platformio.ini,src/main.cpp,src/cli/serial_cli.cpp}`
- `.github/workflows/firmware-ci.yml` (thêm esp32-s3-real env)

**Test coverage:** 94/94 native tests PASS (71 Sprint 1-4 + 23 Sprint 5 BMS register decode). ESP32 build SUCCESS 3 envs (mock + real + blink).

**Backend contract verified** với code thực tế: `SensorReadingItem` 17 field, `AmbientReadingItem` 7 field (incl required siteId), enums INT format (ChargingState/SourceType/AmbientReadingSource), camelCase JSON.

---

### Sprint 6 — Anomaly · Cross-source · Notification (2 tuần)

**Mục tiêu:** Reading vượt ngưỡng → Alert → Ticket → Notification chạy hoàn chỉnh end-to-end. SensorMismatch (BMS vs INA226/DS18B20) hoạt động. Cảm biến môi trường (MQ-2, water) cũng vào pipeline.

**Pre-requisite:** S3 (contract production), S4 (MQTT) hoặc HTTPS. S5 song song — không bắt buộc có BMS thật, có thể demo bằng mock.

#### Backend (BE) — chuyển sang Sprint IoT-2

> 5 task BE của Sprint 6 (`AnomalyType.SensorMismatch=15`, `CrossSourceValidationService` BMS vs IotGateway, verify `ThresholdCheckBackgroundService`, publish `BatteryAnomalyDetectedEvent` V2 cho Alert–Ticket Saga, AmbientReading + EnvironmentalIncident endpoints với Critical bypass quiet hours) **đã chuyển sang `backend/overall.md` Sprint IoT-2 Phase E** (`#IoT2-27..31`).
>
> **Quan trọng cho FW (S6-FW-03):** đảm bảo INA226/DS18B20 reading có `sourceType=IotGateway (2)` để cross-source pair với BMS (`sourceType=Bms (1)`) — không có cặp 2 source thì `SensorMismatch` không trigger.

#### Backlog — Firmware

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **62. S6-FW-01** (#63) | `src/sensor/mq2.cpp` đọc ADC GPIO1, threshold lấy từ config → publish event qua MQTT khi vượt | WD §4.3, OV §A6 | Hơ bật lửa gần → backend nhận | FW |
| **63. S6-FW-02** (#64) | `src/sensor/water_leak.cpp` đọc GPIO2 (digital); thay đổi cạnh → publish event | WD §4.3 | Nhúng đầu cảm biến vào nước → backend nhận | FW |
| **64. S6-FW-03** (#65) | Đảm bảo INA226 + DS18B20 reading có `sensorSourceCode` đúng để cross-source pair với BMS | OV §B2 (o) | DB query có cặp `primary` + `redundant`/`external-temp` cùng battery + cùng phút | FW |

#### Backlog — FE

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **65. S6-FE-01** (#66) | Trang Alert/Ticket: filter theo `AnomalyType`, hiển thị icon riêng cho SensorMismatch, DeviceOffline, Overheat, LowSoc, Smoke, WaterLeak | OV §B3 | UX rõ ràng | FE |
| **66. S6-FE-02** (#67) | Mobile push test (Expo) khi Critical | OV §B3 #4 | Nhận notification trên điện thoại | FE |

#### Backlog — QA

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **67. S6-QA-01** (#68) | Kịch bản "overheat → alert → ticket → push" end-to-end < 30s | OV §B3 | Pass có timing | QA |
| **68. S6-QA-02** (#69) | Kịch bản SensorMismatch: cấu hình INA226 báo lệch BMS → alert xuất hiện | NI §7.6 | Pass | QA |
| **69. S6-QA-03** (#70) | Kịch bản Smoke: trigger MQ-2 → EnvironmentalIncident + push | OV §A6 | Pass | QA |

**DoD sprint:** Một loạt kịch bản anomaly trong checklist demo đều ra alert + ticket + notification đúng.

---

### Sprint 7 — Calibration · OTA · Observability (2 tuần)

**Mục tiêu:** Hoàn thiện 3 tính năng "polish" quan trọng — Calibration đầy đủ flow, OTA firmware từ xa, và quan sát hệ thống bằng Grafana.

**Pre-requisite:** S5 (có Fluke + reading lệch để calibrate), S2 (entity calibration sẵn).

#### Backend (BE) — chuyển sang Sprint IoT-2

> 7 task BE của Sprint 7 **đã chuyển sang `backend/overall.md` Sprint IoT-2 Phase F** (`#IoT2-32..38`):
> - **Calibration** (3 task): POST/GET endpoints + `CalibrationExpiryNotificationService` + Redis cache invalidate — `#IoT2-32..34`
> - **OTA** (3 task): upload firmware release + firmware-check + update-log — `#IoT2-35..37`
> - **Observability** (1 task): Prometheus metrics đầy đủ label — `#IoT2-38`
>
> IoT track Sprint 7 chỉ giữ: FW (OTA `esp_https_ota` + rollback), FE (calibration UI + firmware upload UI + gateway dashboard), INF (Grafana compose + 6 panel), QA (calibration field test với Fluke).

#### Backlog — Calibration

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **70. S7-FE-01** (#71) | UI Calibration: form nhập (sensorMetric, offset, scale, validUntil, standard), bảng list, badge "sắp hết hạn" | NI §7.3 | UX hoạt động | FE |
| **71. S7-QA-01** (#72) | Quy trình calibration thực địa: đo Fluke → nhập web → reading khớp Fluke ±0.05V | OV §B5 | Pass | QA |

#### Backlog — OTA

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **72. S7-FW-01** (#73) | `src/ota/ota_update.cpp` dùng `esp_https_ota` — download, verify sha256, write OTA partition, reboot | NI §9.1, OV §B6 | OTA chạy thành công; reboot vào firmware mới | FW |
| **73. S7-FW-02** (#74) | Rollback: nếu boot mới fail health (không connect WiFi/broker trong 2 phút) → tự rollback partition cũ + report | OV §B6 #6 | Cố tình build firmware lỗi → rollback đúng | FW |
| **74. S7-FE-02** (#75) | UI upload firmware + bảng firmware-update-log per device | NI §11 E | UX hoạt động | FE |

#### Backlog — Observability

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **75. S7-INF-01** (#76) | `infra/grafana/` compose + dashboard JSON: 6 panel (online count, ingest rate, reject reasons, latency p95, heartbeat freshness, firmware status) | NI §11 F | Dashboard load đẹp | INF |
| **76. S7-FE-03** (#77) | Trang "Gateway dashboard" trên admin: list device + online/offline, queue depth, heartbeat history sparkline, uptime % | NI §11 E | UX hoạt động | FE |

**DoD sprint:** Admin upload firmware mới → ESP32 trong lab tự update; Grafana hiện 6 panel với số liệu thực.

---

### Sprint 8 — Pilot · Demo · Runbook (P5) (1–2 tuần)

**Mục tiêu:** Triển khai 1 node ESP32 trong enclosure ngoài lab + UPS + 4G fallback. Toàn bộ kịch bản demo B1–B7 chạy mượt. Có video + runbook.

**Pre-requisite:** S7 xong (hoặc đa số). Linh kiện Cấp 3+4 đã về.

#### Backlog

| ID | Task | Spec | Acceptance | Track |
|----|------|------|-----------|-------|
| **77. S8-HW-01** (#78) | Hàn perfboard cố định: ESP32 + MAX485 + INA226 + DS18B20 + SHT31 + MQ-2 + water + power | OV §A1, A10, WD §7 | Node 1 build xong, không hở | HW |
| **78. S8-HW-02** (#79) | Lắp vào enclosure IP65 + DIN rail + quạt | OV §A10 | Đậy kín, chạy 24h ổn | HW |
| **79. S8-HW-03** (#80) | UPS: TP4056 + 18650 + boost 5V — test cúp điện AC → ESP32 sống tiếp ≥ 30 phút | OV §A7, WD §6 phương án C | Đo bằng đồng hồ thời gian sống | HW |
| **80. S8-HW-04** (#81) | 4G fallback: router 4G SIM hoặc SIM7600 — test tắt WiFi → tự chuyển 4G | OV §A8 | Vẫn ingest data | HW |
| **81. S8-HW-05** (#82) | (Tùy chọn §A14, **hardware-only**) Hệ solar mini + tải giả **chỉ để nuôi node ESP32 ngoài trời** (thay thế nguồn AC). **KHÔNG demo energy metrics / kWh / charge cycle dashboard** — vi phạm ADR-017 + scope guard CI (MO §53.1, §53.2bis). INA226 trên node vẫn chỉ dùng cross-source validation. Nếu muốn demo energy metrics trong tương lai → phải mở ADR mới + service riêng | OV §A14, MO §53.1, §53.2bis | Node chạy được bằng solar power 24h; KHÔNG có panel kWh/CO2/savings trên dashboard | HW |
| **82. S8-QA-01** (#83) | Chạy toàn bộ kịch bản B1–B7 và checklist 8 (WD §8) + tick lần lượt | OV §B, WD §8 | 100% pass | QA |
| **83. S8-QA-02** (#84) | Runbook setup ESP32: nạp firmware, đổi config qua portal, đấu RS485, đổi unitId | NI §11 F | File `docs/runbook-setup-node.md` | QA |
| **84. S8-QA-03** (#85) | Runbook xử lý sự cố: device Offline, reading lệch, OTA fail, broker down | OV §B4–B7, NI §12 | File `docs/runbook-troubleshooting.md` | QA |
| **85. S8-QA-04** (#86) | Video demo 5 phút end-to-end | — | Upload | QA |
| **86. S8-FE-01** (#87) | Polish UI: loading state, error message rõ, mobile responsive | — | Manual QA pass | FE |

**DoD sprint:** Toàn bộ DoD tổng (mục 8) tick xanh; video demo hoàn chỉnh; mọi runbook đầy đủ.

---

## 5. Cross-cutting tracks (chạy song song)

Các việc không thuộc riêng sprint nào — phân ra để không quên:

### Tài liệu (mọi sprint)
- Mỗi sprint kết thúc cập nhật `CHANGELOG.md` của repo BE và FW.
- Update `docs/architecture.md` khi cấu trúc đổi.
- Update ADR mới (vd ADR-017 nếu chọn EMQX thay vì Mosquitto, ghi lý do).

### Security (S2 trở đi)
- API key chỉ lưu hash; key plaintext chỉ trả 1 lần.
- MQTT-over-TLS bắt buộc khi ra ngoài LAN.
- ACL broker per-device (S4).
- Rate limit ingest per-device để chống DoS (BE, S6 hoặc S8).

### Testing (mọi sprint)
- Unit test coverage tối thiểu: validation logic, calibration, idempotency, ACL.
- Integration test: 1 happy path + 1 failure path per command.
- Load test trước S8: 50 device giả publish 1 msg/s, đo throughput.

### CI/CD
- S0 set up GH Actions: build + test cho FW (`pio run`), BE (`dotnet build && dotnet test`).
- S4 trở đi: tự động build Docker image BE + push registry.
- S7: tự động build firmware `.bin` artifact cho mỗi tag.

---

## 6. Procurement timeline (mua sắm theo sprint)

> Lý do tách riêng: đồ điện tử có lead time, mua trễ là kẹt sprint.

| Đặt mua trong | Hàng | Cần cho sprint nào | Lý do trước |
|---------------|------|-------------------|-------------|
| **S0** | **Cấp 0**: ESP32-S3 (3 con dự phòng), cáp USB, breadboard | S0–S4 (mock firmware) | Có ngay ngày đầu để FW chạy |
| **S0** | **Cấp 1**: MAX485, điện trở 120Ω/4.7kΩ, cáp đôi xoắn shield 20m, domino, dây Dupont, jack DC | S5 | Lead time 1–2 tuần, đặt sớm |
| **S0** | **BMS-RS485**: 3–4 pin LiFePO4 12V có BMS Daly/JBD/JK + xác nhận register map | S5 | Lead time dài (2–4 tuần) — đặt sớm nhất |
| **S2** | **Cấp 3 sensor**: INA226 ×3, DS18B20 ×6, SHT31, MQ-2, water leak sensor | S5–S6 | 1–2 tuần |
| **S2** | **Calibration**: Fluke 87V hoặc tương đương + nhiệt kế chuẩn | S5 (measure baseline), S7 (calibration UI) | Có thể mượn lab; nếu mua mất 1–2 tuần |
| **S4** | **Cấp 4 đóng gói**: enclosure IP65, DIN rail, quạt, perfboard 7×9 ×3, header pin, mỏ hàn + thiếc | S8 | Mua sau khi chốt layout |
| **S4** | **UPS + 4G**: TP4056 + 18650 ×2, boost 5V, router 4G + SIM hoặc SIM7600 | S8 | 1–2 tuần |
| **S5** | **Solar tùy chọn**: tấm solar 10–50W, charge controller PWM/MPPT, tải giả | S8 | Optional |

Theo `hardware-bom-budget.csv` (nếu có) — verify ngân sách từng nhóm trước khi đặt.

---

## 7. Risk register & mitigation

| Rủi ro | Sprint impact | Mức | Mitigation |
|--------|---------------|-----|-----------|
| BMS mua về không có register map / không đổi unitId | S5 kẹt | **Cao** | Xác nhận seller bằng văn bản trước khi chuyển tiền (S0); nếu fail → quay về mock firmware cho demo |
| Backend BatteryService legacy đòi `batteryAssetId` GUID thay vì serial | S1 kẹt | Trung | Shim sớm trong S1; nếu phức tạp → seed firmware bằng GUID thật |
| MQTT-over-TLS crash heap trên ESP32 (RAM hạn chế) | S4 kẹt | Trung | Dùng ESP32-**S3** (PSRAM); fallback MQTT 1883 trong LAN cho dev (NI §12 #2) |
| Pre-commit hook backend chặn migration lớn | S2 trễ | Thấp | Test migration trên branch riêng, chia nhỏ nếu cần |
| Đấu sai chân MAX485 DE/RE → đọc Modbus lỗi | S5 trễ | Trung | Khuyến nghị mua module **auto-direction** XY-017 (NI §12 #3, WD §3) |
| Common ground bị quên → reading sai/0 | S5 trễ | Cao | Checklist WD §8 phải tick trước khi cấp nguồn |
| EMQX/Mosquitto SPOF → broker down | S4+ | Trung | Firmware fallback HTTPS (S4-FW-06); broker chạy với restart=always |
| OTA fail → ESP32 brick | S7 critical | Trung | Verify sha256 trước khi flash; rollback partition (S7-FW-02); thử trên 1 con dự phòng trước |
| Procurement BMS trễ qua S5 | Trượt 2 tuần | Cao | Mua S0; backup: chạy mock_bms tới S6 vẫn demo được nhờ compile flag (S5-FW-07) |
| Clock skew firmware (NTP fail) | S3 reject hết reading | Cao | NTP retry mỗi 5 phút; nếu lệch > 5 phút thì pause publish + log lỗi (NI §12 #1) |
| TicketService consume `BatteryAnomalyDetectedEvent` trực tiếp (cũ) → tạo Ticket trùng khi Saga đã active | S6 sai luồng | **Cao** | Sprint 5B backend đã active Alert–Ticket Saga (MO §53). IoT track CHỈ emit event V2 đúng schema, không động vào TicketService consumer. Kiểm tra trước S6: TicketService không còn `BatteryAnomalyDetectedConsumer` direct |
| Vô tình thêm Energy/CO2 dashboard ở S8 (charge cycle, kWh) | Vi phạm ADR-017 | **Cao** | CI scope-guard backend (MO §53.2bis) sẽ fail PR. Local pre-commit hook cũng chặn. Bất kỳ ai có ý định "demo nhẹ kWh" phải mở ADR mới trước |
| ESP32 hard-code `sourceType=2` cho mọi reading | Cross-source validation §1.6.6 không trigger | Cao | Code review S3-FW-04: BMS reading PHẢI tag `sourceType=Bms(1)`. Unit test firmware kiểm tra payload có ít nhất 2 `sourceType` khác nhau khi đọc BMS + INA226 |

---

## 8. Definition of Done — checklist toàn hệ thống

> Tick toàn bộ trước khi báo "hệ thống xương sống hoàn thiện".

### Firmware ESP32 (NI §11 A)
- [ ] WiFi reconnect tự động (S1-FW-02)
- [ ] NTP sync ổn định, không publish nếu lệch > 5 phút (S1-FW-03, S3)
- [ ] Mock BMS scenario overheat/low_soc (S1-FW-04)
- [ ] HTTPS ingest legacy + production contract (S1, S3) — backward compat song song
- [ ] Local queue NVS + retry exponential backoff (S3-FW-01..03)
- [ ] `Idempotency-Key`, `deviceTimestamp`, `bmsErrorCode` (≤64 chars) trong payload (S3-FW-02, 04, S5-FW-01)
- [ ] **`sourceType` per-source đúng MO §52.9** — BMS=Bms(1)/primary, INA226=IotGateway(2)/redundant, DS18B20=IotGateway(2)/external-temp (S3-FW-04, S5-FW-04/05)
- [ ] MQTT-over-TLS publish telemetry + LWT + subscribe cmd + **publish ack lên `cmd/ack`** (S4-FW-05)
- [ ] Modbus driver đọc đúng register map, multi-drop nhiều unitId (S5-FW)
- [ ] Sensor phụ INA226 + DS18B20 + SHT31 (S5-FW) — SHT31 đi vào `/api/ambient-readings/batch` với `Source=IotSensor`
- [ ] MQ-2 + water leak event publish → `/api/environmental-incidents` (S6-FW)
- [ ] OTA `esp_https_ota` + rollback (S7-FW)
- [ ] Compile flag mock/real BMS (S5-FW-07)
- [ ] Status LED RGB chỉ trạng thái (S3-FW-05)

### Backend (NI §11 B + MO §52/§53) — owner Thắng, track riêng

> **Toàn bộ DoD Backend đã chuyển sang `backend/overall.md` Sprint IoT-2 Acceptance** (38 task `#IoT2-01..38` + 7 acceptance criteria cuối sprint). Reference checklist tóm tắt phụ thuộc:

- [ ] Phase A (`#IoT2-01..03`) — BatteryService build + seed + legacy shim — block S0/S1 IoT track
- [ ] Phase B (`#IoT2-04..13`) — Device management + offline detection + NotificationService consumer — block S2 IoT track
- [ ] Phase C (`#IoT2-14..20`) — Production contract + resilience — block S3 IoT track
- [ ] Phase D (`#IoT2-21..26`) — MQTT bridge (P3, có thể trượt) — block S4 IoT track nếu làm MQTT
- [ ] Phase E (`#IoT2-27..31`) — Cross-source + Saga + Environmental — block S6 IoT track
- [ ] Phase F (`#IoT2-32..38`) — Calibration + OTA + Observability — block S7 IoT track

IoT track cần lập **standup hằng tuần với Thắng** để theo dõi tiến độ Sprint IoT-2 — nếu Phase X trễ, sprint tương ứng trong tasksprint trượt theo.

### MQTT (NI §11 C + MO §52.14)
- [ ] Broker EMQX/Mosquitto Docker + TLS 8883 (S4-INF-01..02)
- [ ] ACL per-device — 5 topic: `telemetry`, `heartbeat`, `status`, `cmd`, `cmd/ack` (S4-INF-03)
- [ ] MqttBridgeBackgroundService subscribe telemetry/heartbeat/status/**cmd/ack** (S4-BE-02)
- [ ] LWT handler mark Offline (S4-BE-04)
- [ ] Downlink publisher + ack-trace log (S4-BE-05, S4-FW-05)
- [ ] MQTT credential per-device (S4-BE-06)

### Hardware (NI §11 D + WD §8)
- [ ] BMS xác nhận register map + đổi unitId (S0-HW-03, S5-HW-03)
- [ ] Wiring đúng GPIO map WD §1; common ground; trở 120Ω; pull-up DS18B20 4.7kΩ (S5-HW-01..06)
- [ ] Đo nguồn ≥ 2A; cầu chì khi lấy từ pin (S8-HW-03)
- [ ] Enclosure IP65, DIN rail, quạt (S8-HW-02)
- [ ] UPS sống ≥ 30 phút (S8-HW-03)
- [ ] 4G fallback test pass (S8-HW-04)

### FE (NI §11 E)
- [ ] IoT Device CRUD + key 1 lần (S2-FE-01..02)
- [ ] Gateway dashboard (S7-FE-03)
- [ ] Firmware release upload + log (S7-FE-02)
- [ ] Alert/Ticket có icon riêng theo type (S6-FE-01)
- [ ] Mobile push (S6-FE-02)
- [ ] Calibration UI (S7-FE-01)

### Demo & runbook (NI §11 F)
- [ ] Seed script + kịch bản B1–B7 chạy mượt (S1-BE-01, S8-QA-01)
- [ ] Grafana dashboard (S7-INF-01)
- [ ] Runbook setup node (S8-QA-02)
- [ ] Runbook troubleshooting (S8-QA-03)
- [ ] Video demo 5 phút (S8-QA-04)

---

## 9. Đồng bộ với `backend/overall.md` (master roadmap)

> Bảng ánh xạ task IoT track ↔ Sprint IoT-2 (BE owner Thắng) ↔ section MO. Đọc trước khi review PR để đảm bảo không lệch.

### 9.1. Ánh xạ chủ đề → Sprint IoT-2 task ID + MO section

| Chủ đề | tasksprint.md (FW/INF/HW/FE/QA) | Sprint IoT-2 BE task | MO section |
|--------|----------------------------------|---------------------|-----------|
| 5 entity IoT + enum `SensorReadingSourceTypeEnum` | — | `#IoT2-04` | §52.2 |
| Hypertable heartbeat (retention 30 ngày) | — | `#IoT2-05` | §52.2 |
| API key per-device + scope `sensor.ingest`/`device.heartbeat`/`environmental.ingest` | S2-FW-04 (gửi header) | `#IoT2-06` | §52.2, §7.2 |
| Admin device CRUD + provisioning | S2-FW-02 (provision flow) | `#IoT2-07..09` | §52.3, §52.11 |
| Heartbeat endpoint (60s) | S2-FW-03 | `#IoT2-10` | §52.4 |
| Offline detection 2 cơ chế (LWT + job 2 phút) | S4-FW-02 (LWT firmware) | `#IoT2-11`, `#IoT2-24` | §52.6 |
| `IotDeviceWentOfflineEvent` + NotificationService consumer + dedup Customer/Staff | — | `#IoT2-13` | §52.6, §3.4 |
| `AnomalyType=7 DeviceOffline (Warning)` | — (mention S6-FE-01 hiện icon) | `#IoT2-11` | §1.3.6 |
| ApiGateway routes IoT | — | `#IoT2-12` | §0bis.3 |
| Ingest production contract + backward compat | S3-FW-04 (build payload), S5-FW-01 (BmsErrorCode) | `#IoT2-14` | §52.5 |
| Clock skew 5 phút | S2-FW-03/S3-FW-... (NTP sync) | `#IoT2-15` | §52.5 |
| Idempotency-Key | S3-FW-02 (gen UUID) | `#IoT2-16` | §8.6 |
| Outlier reject + auto-disable N=50/h | — | `#IoT2-17` | §52.5, §52.15 |
| Mapping batteryAssetSerial → BatteryAssetId | — | `#IoT2-18` | §52.5 |
| Calibration apply + Redis cache + expiry service | — | `#IoT2-19`, `#IoT2-32..34` | §52.8 |
| LastSeenAt update | — | `#IoT2-20` | §52.5 |
| `sourceType` per-source mapping (Bms vs IotGateway) | S3-FW-04, S5-FW-04/05 | `#IoT2-14` (validate) | §52.9 |
| MQTT broker + TLS + ACL per-device + 5 topic (incl. `cmd/ack`) | S4-INF-01..04, S4-FW-01..06 | `#IoT2-21..26` | §52.14 |
| `AnomalyType=15 SensorMismatch` + cross-source validation BMS vs IotGateway | S6-FW-03 (đảm bảo source code đúng) | `#IoT2-27..28` | §1.3.6, §1.6.6 |
| ThresholdCheck → publish `BatteryAnomalyDetectedEvent` V2 → **Alert–Ticket Saga** (KHÔNG direct consumer) | — | `#IoT2-29..30` | §53 (Sprint 5B), §52.12bis |
| AmbientReading `Source=IotSensor` + EnvironmentalIncident Critical bypass quiet hours | S5-FW-06 (SHT31), S6-FW-01/02 (MQ-2/water) | `#IoT2-31` | §52.9bis, §1.7, §3.4 |
| OTA flow (upload + check + log + rollback) | S7-FW-01/02 (firmware OTA) | `#IoT2-35..37` | §52.7 |
| Metrics Prometheus đầy đủ label | S7-INF-01 (Grafana panel) | `#IoT2-38` | §52.12 |
| Failure modes (queue đầy, NTP fail, mapping sai, sensor outlier disable) | S3-FW-01 (queue), S4-FW-06 (fallback), Risk register | `#IoT2-17` (auto-disable) | §52.15 |
| **OUT OF SCOPE:** Energy/CO2 dashboard, charge cycle kWh, ESG report | S8-HW-05 (negative — hardware-only) | — (CI scope-guard) | §53.1, §53.2bis (ADR-017) |

### 9.2. Quy tắc đụng chạm

- **Schema entity / migration** → Thắng update MO §52.2 + tạo migration trong Sprint IoT-2 Phase B; IoT track KHÔNG tự viết migration.
- **Contract ingest** (header, body shape) → Thắng update MO §52.5 + `newiot.md` §7.4 trước; FW track update payload builder sau.
- **Saga / event V2** → BatteryService Sprint 5B `#237` chịu trách nhiệm. IoT track chỉ emit `BatteryAnomalyDetectedEvent` V2 đúng schema; không động state machine.
- **Endpoint mới** (vd thêm command type qua MQTT) → tạo task mới trong Sprint IoT-2; KHÔNG thêm task BE trở lại tasksprint.

### 9.3. Standup tuần BE↔IoT

Mọi thứ Hai: Thắng báo cáo Sprint IoT-2 Phase đang ở task nào → nếu task block IoT track sprint hiện tại bị trễ → IoT track switch sang work backlog (FW/HW/QA) không phụ thuộc BE.

| Phase IoT-2 block sprint IoT-track nào | Workaround nếu trễ |
|---------------------------------------|--------------------|
| Phase A trễ → block S0/S1 | FW vẫn build sketch + WiFi + NTP độc lập (S0-FW); mock backend bằng `python -m http.server` để verify payload format |
| Phase B trễ → block S2 | FW vẫn build NVS + heartbeat task local (S2-FW-01..03); test với mock server |
| Phase C trễ → block S3 | FW build queue + retry logic độc lập (S3-FW-01..03); test với endpoint cũ |
| Phase D trễ → block S4 | FW vẫn dùng HTTPS (S3 đã có); pilot MQTT chuyển sang Sprint 7 hoặc backlog |
| Phase E trễ → block S6 | FW vẫn publish MQ-2/water event local; FE polish UI |
| Phase F trễ → block S7 | FW làm OTA local test với GitHub release URL; Grafana wait |

---

## Phụ lục — Mẹo chạy sprint hiệu quả

1. **Standup 15 phút mỗi sáng:** mỗi người 1 câu — hôm qua làm gì (ID task), hôm nay làm gì, blocker gì.
2. **PR nhỏ:** mỗi PR cover 1–2 task ID; không gộp cả sprint.
3. **Demo cuối sprint:** demo trên dev environment đúng theo DoD của sprint đó; không demo "code chạy được" mà phải "kịch bản hoạt động".
4. **Buffer 20% timeline:** nếu sprint 2 tuần thực, thì planning 8 ngày làm + 2 ngày buffer cho bug fix + demo.
5. **Quy tắc đụng spec:** không tự ý đổi schema / contract — đụng thì phải update lại `newiot.md`/`overall.iot.md` trước, không tự edit code rồi sửa spec sau.
6. **Phần cứng đặt trước phần mềm cần:** mọi linh kiện phải có **trước** sprint dùng tới ít nhất 1 tuần. Không bao giờ "vừa code vừa chờ hàng".
