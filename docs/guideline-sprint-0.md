# Guideline — Chạy Sprint 0

> **Đối tượng:** Mọi thành viên team (BE/FE/AI/IoT) clone repo về máy lần đầu.
> **Thời gian:** ~30 phút (tải Docker image + PlatformIO toolchain — phụ thuộc internet).
> **Mục tiêu cuối:** Bạn có thể demo Serial NTP từ ESP32 + `docker compose up` 4 service healthy.

---

## 0. Yêu cầu môi trường

| Phần mềm | Phiên bản | Cài thế nào (macOS) |
|----------|-----------|---------------------|
| Git | ≥ 2.30 | có sẵn / `brew install git` |
| Docker Desktop | ≥ 4.30 | [docker.com/products/docker-desktop](https://www.docker.com/products/docker-desktop/) |
| Homebrew | ≥ 4.0 | `/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"` |
| PlatformIO CLI | ≥ 6.x | `brew install platformio` |
| `lsof`, `curl`, `python3` | builtin | có sẵn macOS |

> **Windows / Linux:** Docker Desktop có bản tương ứng. PlatformIO cài qua `pipx install platformio`. Còn lại lệnh giống.

---

## 1. Clone repo

```bash
git clone <repo-url> capstone-iot
cd capstone-iot
git checkout dev
```

Đảm bảo branch là `dev` — không làm việc trực tiếp trên `main`.

---

## 2. Sprint 0 gồm những gì?

10 task, chia 4 nhóm:

| Nhóm | Task | Việc của ai |
|------|------|-------------|
| **INF** (infrastructure) | S0-INF-01, 02, 03 | Mọi member chạy `infra/setup.sh` |
| **FW** (firmware) | S0-FW-01, 02, 03 | Member có ESP32-S3 flash + verify |
| **HW** (hardware) | S0-HW-01, 02, 03 | Leader/IoT lead đặt mua theo checklist |
| **QA** (docs) | S0-QA-01 | Đọc `docs/glossary.md` để hiểu thuật ngữ |

---

## 3. INF — Dựng hạ tầng dev (10 phút)

### 3.1. Chạy bootstrap script

```bash
cd infra
./setup.sh
```

Script này tự động:
1. Tạo `.env` từ `env.example.txt` (nếu chưa có)
2. Cảnh báo nếu port quan trọng đang bị process khác chiếm
3. `docker compose up -d` 4 service: TimescaleDB, Redis, RabbitMQ, EMQX
4. Đợi healthy + verify từng service

### 3.2. Output mong đợi

```
✓ .env đã có  (hoặc: ✓ Tạo .env từ env.example.txt)
--- Port check ---
✓ Port 5432 (TimescaleDB) free
✓ Port 6379 (Redis) free
... (warn nếu có port chiếm — xem Troubleshooting)
--- docker compose up ---
 Container iot-redis Started
 Container iot-timescaledb Started
 Container iot-rabbitmq Started
 Container iot-emqx Started
--- Verify ---
✓ TimescaleDB extension OK trên battery_service_dev
✓ Redis PONG
✓ RabbitMQ Mgmt OK (http://localhost:15672)
✓ EMQX Dashboard OK (http://localhost:18083)
✓ Sprint 0 infra ready.
```

### 3.3. Verify thủ công (acceptance Sprint 0)

```bash
# S0-INF-02 — 4 service healthy
docker compose -f docker-compose.dev.yml ps
# Tất cả phải có "Up X seconds (healthy)"

# S0-INF-03 — TimescaleDB extension
docker compose -f docker-compose.dev.yml exec timescaledb \
  psql -U postgres -d battery_service_dev -c '\dx'
# Phải thấy: timescaledb 2.15.2

# Mở UI để xem trực quan
open http://localhost:15672    # RabbitMQ — login: guest / guest
open http://localhost:18083    # EMQX     — login: admin / public
```

### 3.4. Port mapping

| Service | Host port | Trong container | Mục đích |
|---------|-----------|-----------------|----------|
| TimescaleDB | 5432 | 5432 | Postgres + extension timescaledb |
| Redis | 6379 | 6379 | Cache + pub/sub |
| RabbitMQ AMQP | 5672 | 5672 | Message bus |
| RabbitMQ Mgmt UI | 15672 | 15672 | http://localhost:15672 |
| EMQX MQTT | **11883** | 1883 | Shift để tránh đụng Mosquitto local |
| EMQX MQTT/TLS | **18883** | 8883 | Dành cho S4 |
| EMQX WS | **18030** | 8083 | MQTT over WebSocket |
| EMQX Dashboard | 18083 | 18083 | http://localhost:18083 |

### 3.5. Tắt khi không dùng

```bash
docker compose -f docker-compose.dev.yml down       # giữ data
docker compose -f docker-compose.dev.yml down -v    # XÓA data (cần khi muốn rerun init SQL)
```

---

## 4. FW — Build & flash firmware ESP32 (15 phút, cần board)

> Nếu **chưa có ESP32-S3** trên tay → bỏ qua bước flash, chỉ làm bước build verify compile.

### 4.1. Cài config local

```bash
cd firmware-esp32
cp include/config.example.h include/config.h
```

Sửa `include/config.h`:
```cpp
#define WIFI_SSID       "tên-wifi-thật"     // ĐỔI
#define WIFI_PASSWORD   "password-thật"     // ĐỔI
```

> `include/config.h` đã có trong `.gitignore` — KHÔNG commit password.

### 4.2. Build (S0-FW-01 acceptance)

```bash
pio run
```

Lần đầu sẽ tải ~500MB toolchain ESP32-S3 + 8 thư viện (~5-10 phút). Cuối cùng phải in:
```
================================ [SUCCESS] Took XX.XX seconds ================================
```

### 4.3. Flash sketch blink (S0-FW-02 acceptance)

Cắm ESP32-S3 vào USB-C của Mac (cổng gần chip, không phải cổng UART). Sau đó:

```bash
# Verify Mac thấy device
ls /dev/cu.usbmodem* /dev/cu.usbserial*

# Flash + monitor
pio run -e example-blink -t upload
pio device monitor -b 115200
```

Mong đợi: LED on-board nhấp nháy + Serial in:
```
==============================
 Sprint 0 — S0-FW-02
==============================
[1] hello (uptime=1s)
[2] hello (uptime=2s)
...
```

Bấm `Ctrl+C` rồi `Ctrl+T Q` để thoát monitor.

### 4.4. Flash main sketch WiFi+NTP (S0-FW-03 acceptance)

```bash
pio run -t upload
pio device monitor -b 115200
```

Mong đợi (sau ~10s):
```
[wifi] connecting to "tên-wifi-thật" ......
[wifi] connected ip=192.168.1.42 rssi=-58dBm
[ntp] sync requested
[ntp] synced first time = 2026-06-12T08:15:42Z (utc)
[12s] isoNow=2026-06-12T08:15:43Z rssi=-58dBm wifi=OK
[13s] isoNow=2026-06-12T08:15:44Z rssi=-58dBm wifi=OK
```

**Test acceptance:**
- ☐ So với [time.is](https://time.is) — lệch ≤ 1 giây
- ☐ Tắt WiFi router 10s → Serial in `[wifi] reconnecting...` → bật lại → tự connect lại trong 30s
- ☐ Quay video 30 giây làm bằng chứng DoD sprint

---

## 5. HW — Procurement (Leader/IoT lead)

Mở `docs/procurement-checklist.md`. Đặt theo thứ tự ưu tiên:

| Đợt | Lead time | Khi nào cần | Ghi chú |
|-----|-----------|-------------|---------|
| **BMS-RS485** | ≤ 4 tuần | S5 | **Critical path** — đặt ngày đầu Sprint 0, xác nhận register map với seller |
| **Cấp 1** | ≤ 2 tuần | S5 | MAX485 + RS485 accessories |
| **Cấp 0** | ≤ 1 tuần | Cuối S0 | ESP32-S3 + cáp + breadboard |

Cập nhật cột **Trạng thái** trong checklist mỗi tuần.

---

## 6. QA — Đọc glossary

Trước khi vào Sprint 1, **mỗi member đọc** `docs/glossary.md` (30 thuật ngữ, ~10 phút):
- BMS, Modbus unitId, RS485, DE/RE, common ground (S5 cần hiểu trước)
- LWT, QoS, retained, ACL, mTLS (S4)
- Hypertable, chunk, Idempotency-Key, clock skew (S2-S3)
- SOH, SOC, sourceType, sensorSourceCode (S5-S6)
- ADR-017, scope guard (luôn luôn)

Member mới gặp thuật ngữ không có trong glossary → thêm vào ngay (PR `docs: add term X`).

---

## 7. Definition of Done — Sprint 0

Họp 30 phút cuối sprint, mỗi member chứng minh:

- [ ] **INF** — `docker compose -f infra/docker-compose.dev.yml ps` → 4 healthy
- [ ] **INF** — Mở `http://localhost:18083` (EMQX) + `http://localhost:15672` (RabbitMQ) thành công
- [ ] **FW build** — `cd firmware-esp32 && pio run` → SUCCESS
- [ ] **FW flash** (≥ 1 người có board) — Serial in ISO8601 mỗi giây, lệch time.is ≤ 1s
- [ ] **HW** — `docs/procurement-checklist.md` đã tick **Đặt** cho Cấp 0/1/BMS
- [ ] **QA** — Mỗi member đọc `docs/glossary.md`, hiểu 30 thuật ngữ cơ bản

Khi 6 mục trên ✅ → Sprint 0 đóng → bắt đầu Sprint 1 (MVP Mock HTTPS).

---

## 8. Troubleshooting nhanh

| Vấn đề | Cách xử |
|--------|---------|
| `setup.sh: Permission denied` | `chmod +x infra/setup.sh` |
| TimescaleDB `role "postgres" does not exist` | `docker compose -f infra/docker-compose.dev.yml down -v && cd infra && ./setup.sh` |
| Port 5432 / 1883 / 6379 đã chiếm | `lsof -nP -iTCP:<port> -sTCP:LISTEN` → tắt process kia (vd `brew services stop postgresql`) hoặc đổi port trong `.env` |
| `pio: command not found` | `brew install platformio` hoặc dùng VSCode PlatformIO IDE extension |
| `pio run` báo `config.h: No such file` | Chưa copy `include/config.example.h` → `include/config.h` |
| ESP32 không thấy `/dev/cu.usbmodem*` | Cắm vào cổng USB **gần chip** (không phải UART). Windows cần driver CP2102/CH340 |
| `pio device monitor` không có Serial | Đợi 2-3s sau khi flash xong; bấm nút RESET trên board |
| NTP không sync (`waiting for ntp sync`) | Router chặn UDP port 123 — đổi sang hotspot điện thoại để test |
| Lệch giờ > 1s | DNS chậm — sửa `NTP_SERVER_1` trong `config.h` sang IP NTP local trường nếu có |

Lỗi khác → đăng vào Slack `#capstone-iot` + paste log.

---

## 9. Tiếp theo (Sprint 1)

Sau khi Sprint 0 DoD ✅:
- Đọc `tasksprint.md` § Sprint 1 — MVP Mock HTTPS
- Issue mới sẽ được leader phân công với label `status: init`, milestone Sprint 1
- Mỗi task chạy luồng chuẩn: `/kltn-plan <issue>` → approve → `/kltn-implement <issue>` → ... → `/kltn-complete`
