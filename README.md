# Solar Battery IoT Capstone — Monorepo

Hệ thống IoT giám sát pin LiFePO4 cho năng lượng mặt trời. Kiến trúc **ESP32-S3 → MQTT/HTTPS → Backend microservices → AI**.

> **Spec gốc:** `newiot.md` (firmware + MQTT), `overall.iot.md` (BOM + 7 luồng vận hành), `wiring-diagram.md` (sơ đồ GPIO), `tasksprint.md` (kế hoạch 8 sprint).

---

## Cấu trúc monorepo

```
iot/
├── firmware-esp32/           ← Firmware ESP32-S3 (C++/PlatformIO)
│   ├── platformio.ini
│   ├── include/              ← config header
│   ├── src/                  ← main firmware (S0-FW-03: WiFi+NTP)
│   ├── examples/             ← sketch demo (S0-FW-02: blink)
│   ├── lib/                  ← lib local
│   ├── data/                 ← LittleFS payload (CA cert sau S4)
│   └── test/                 ← unit test
│
├── infra/                    ← Docker compose dev (S0-INF-02/03)
│   ├── docker-compose.dev.yml
│   ├── env.example.txt
│   ├── db/init/              ← SQL auto-run TimescaleDB init
│   └── mqtt/                 ← EMQX/Mosquitto config (S4)
│
├── docs/                     ← Tài liệu chung
│   ├── glossary.md           ← Thuật ngữ (S0-QA-01)
│   └── procurement-checklist.md  ← Tracking đặt mua (S0-HW)
│
├── newiot.md                 ← Spec firmware + MQTT design
├── overall.iot.md            ← BOM + luồng vận hành B1–B7
├── wiring-diagram.md         ← Sơ đồ đấu nối GPIO
├── tasksprint.md             ← Kế hoạch 8 sprint chi tiết
├── iot-task-list.md          ← Danh sách task tóm tắt
├── hardware-bom.csv          ← BOM linh kiện
└── hardware-bom-budget.csv   ← BOM kèm giá
```

> **BatteryService backend** (ASP.NET Core) nằm ở **repo riêng** — không trong monorepo này. IoT track chỉ tương tác qua HTTPS/MQTT API. Việc backend đã chuyển sang `backend/overall.md` Sprint IoT-2 (xem `tasksprint.md §4`).

---

## Quick start (Sprint 0)

> 👉 **Hướng dẫn đầy đủ:** `docs/guideline-sprint-0.md` — step-by-step cho member mới clone repo.

### 1. Hạ tầng dev — Docker

```bash
cd infra
./setup.sh    # auto: tạo .env + check port + up + verify
```

Xem `infra/README.md` để verify từng service + troubleshooting (TimescaleDB / Redis / RabbitMQ / EMQX).

### 2. Firmware ESP32

```bash
cd firmware-esp32
cp include/config.example.h include/config.h    # sửa WIFI_SSID/PASSWORD
pio run                                          # build (S0-FW-03 WiFi+NTP)
pio run -t upload                                # flash
pio device monitor -b 115200                     # xem Serial in ISO8601 mỗi giây
```

Sketch blink tối giản (S0-FW-02):
```bash
pio run -e example-blink -t upload
```

Xem `firmware-esp32/README.md` để chi tiết.

### 3. Procurement

Track đặt hàng theo `docs/procurement-checklist.md`. Đặt **BMS-RS485 ngay sprint 0** vì lead time dài nhất (≤ 4 tuần).

---

## Roadmap 8 sprint

| Sprint | Tên | Mục tiêu khả nghiệm |
|--------|-----|---------------------|
| **S0** ✅ | Foundation & Procurement | Repo + Docker chạy, ESP32 in giờ NTP |
| **S1** | MVP Mock HTTPS (P0) | Mock data ESP32 → dashboard realtime |
| **S2** | Backend Device Management (P2) | Admin tạo device, firmware provision/heartbeat |
| **S3** | Resilience & Production Contract (P1) | Mất mạng 5 phút không mất data, Idempotency-Key |
| **S4** | MQTT Broker & Bridge (P3) | MQTT-over-TLS, LWT offline detection tức thì |
| **S5** | Hardware Integration (P4) | Đọc 4 pin BMS thật qua RS485 multi-drop |
| **S6** | Anomaly · Cross-source · Notification | Reading vượt ngưỡng → alert → ticket → push |
| **S7** | Calibration · OTA · Observability | OTA firmware từ xa, Grafana 6 panel |
| **S8** | Pilot · Demo · Runbook (P5) | Enclosure + UPS + 4G + video demo |

Chi tiết task theo sprint: `tasksprint.md §4`.

---

## ⚠️ Scope guard (ADR-017)

**KHÔNG** thêm Energy / CO2 / cost analytics (kWh, savings, carbon, charge cycle dashboard). INA226 chỉ dùng cross-source validation, KHÔNG tích phân năng lượng. CI backend chặn keyword `EnergySession|EnergyKwh|CapacityKw|CarbonEmission|...`. Chi tiết: `tasksprint.md §0`.

---

## Đóng góp

- Workflow chi tiết: `.claude/rules/workflow.md`
- Tech rules per role: `.claude/rules/tech/{be,fe,mobile,ai}.md`
- Commit message: `type(#<issue>): mô tả` (vd `feat(#42): add Battery CRUD`)
- 1 issue = 1 branch (`feat/GH-<number>-slug`, `fix/...`, `chore/...`)
- Plan trước khi code: `.claude/skills/dev/<role>/implement/SKILL.md`

---

## Liên hệ

GVHD: **Trương Long** · Capstone GSU26SE55 · 4 thành viên BE/FE/AI/IoT
