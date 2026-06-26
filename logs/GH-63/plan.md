# Plan — GH-63: [S6-FW-01] MQ-2 đọc ADC GPIO1 → publish EnvironmentalIncident(Smoke)

## Metadata
- Status: PLANNING | Role: FW | Ngày: 2026-06-24
- Issue: #63 — https://github.com/GSU26SE55/iot/issues/63
- Sprint: Sprint 6
- Spec: WD §4.3, OV §A6, tasksprint S6-FW-01

## Mục tiêu
ESP32 đọc nồng độ khói/gas từ MQ-2 qua ADC GPIO1, so threshold (config). Khi vượt
(cạnh lên) → report `EnvironmentalIncident(type=Smoke, severity=Critical)` lên backend
qua `POST /api/environmental-incidents` (HTTPS, reporter dùng chung). AC: hơ bật lửa
gần → backend nhận incident.

## Quyết định kỹ thuật (giữ nguyên từ WIP)
- **HTTPS chứ không MQTT** dù issue title ghi "qua MQTT": backend MQTT bridge chỉ
  subscribe 4 wildcard telemetry/heartbeat/status/cmd-ack — KHÔNG có topic incident.
  Đường duy nhất là REST `/api/environmental-incidents` (tasksprint line 627 xác nhận).
  Nhất quán SHT31 ambient (S5-FW-06). Đã document trong `environmental_incident.h`.
- **Chống spam**: `IncidentTrigger` fire 1 lần ở cạnh lên + cooldown 5 phút; backend
  cũng dedup theo (site, type) cho incident Open/Acknowledged.
- **Warm-up 30s**: MQ-2 cần ấm sau cấp nguồn mới đọc tin cậy → trong warm-up chỉ
  đọc/log, không đánh giá incident.
- **Pending-retry**: nếu cạnh lên detect nhưng report FAIL (offline/NTP/backend 5xx)
  → giữ `s_pendingReport`, retry tick sau. Tránh mất incident vì lỗi tạm thời.

## Files
| File | Action | Ghi chú |
|------|--------|---------|
| `src/sensor/mq2.h` | (đã có) | API: mq2Begin/Tick/ReadRaw + counters |
| `src/sensor/incident_trigger.h` | (đã có) | edge+cooldown pure logic |
| `src/sensor/environmental_incident.h/.cpp` | (đã có) | reporter HTTPS dùng chung |
| `src/sensor/mq2.cpp` | **create** | implement theo mq2.h |
| `include/config.h` | modify | thêm `BACKEND_ENV_INCIDENT_PATH` + `MQ2_*` |
| `include/config.example.h` | modify | mirror config.h |
| `src/main.cpp` | modify | wire mq2Begin/Tick + envIncidentSetSiteId |
| `test/test_incident_trigger/test_incident_trigger.cpp` | **create** | native test pure logic |

## Config thêm
```c
#define BACKEND_ENV_INCIDENT_PATH "/api/environmental-incidents"
#define MQ2_ENABLED              1
#define MQ2_ADC_PIN              1          // GPIO1 = ADC1_CH0 (WD §4.3)
#define MQ2_THRESHOLD_RAW        2000       // 0-4095; > = smoke
#define MQ2_WARMUP_MS            30000UL    // 30s warm-up
#define MQ2_POLL_INTERVAL_MS     1000UL     // đọc ADC mỗi 1s
#define MQ2_REARM_COOLDOWN_MS    300000UL   // 5 phút giữa 2 report
```

## Workflow (mq2Tick)
```
throttle MQ2_POLL_INTERVAL_MS → analogRead(GPIO1) → s_lastRaw
if (now - warmupStart < WARMUP_MS): return            // warm-up, chỉ đọc
active = raw > THRESHOLD_RAW
if trigger.update(active, now):  s_pendingReport = true; snapshot raw
if s_pendingReport:
   envIncidentReport(Smoke, Critical, "MQ-2 raw=.. thr=..")
   OK  → pending=false, reportCount++
   FAIL→ giữ pending, retry tick sau
```
main.cpp gọi `mq2Tick()` chỉ khi wifi+ntp synced (giống sht31Tick). Offline → tick
dừng, trigger đóng băng; reconnect + smoke còn → cạnh lên re-detect → report.

## Steps
- [ ] Thêm config knobs (config.h + config.example.h)
- [ ] Viết mq2.cpp
- [ ] Wire main.cpp (begin + tick + envIncidentSetSiteId 2 chỗ provision)
- [ ] test_incident_trigger.cpp
- [ ] `pio test -e native -f test_incident_trigger` PASS
- [ ] `pio run -e esp32-s3-devkitc-1` + `-e esp32-s3-real` compile PASS
