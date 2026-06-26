# Plan — GH-64: [S6-FW-02] Water leak đọc GPIO2 digital → publish EnvironmentalIncident(Flood)

## Metadata
- Status: PLANNING | Role: FW | Ngày: 2026-06-24
- Issue: #64 — https://github.com/GSU26SE55/iot/issues/64
- Sprint: Sprint 6
- Spec: WD §4.3, tasksprint S6-FW-02

## Mục tiêu
ESP32 đọc cảm biến rò nước (digital) qua GPIO2. Khi chuyển cạnh khô→ướt → report
`EnvironmentalIncident(type=Flood, severity=Critical)` qua reporter dùng chung. AC:
nhúng đầu cảm biến vào nước → backend nhận incident.

## Quyết định kỹ thuật
- Dùng chung `IncidentTrigger` (edge+cooldown) + `environmental_incident` reporter
  như MQ-2 (#63) — chỉ khác `IncidentType::Flood` và đọc digital thay vì ADC.
- `WATER_LEAK_ACTIVE_HIGH` config hoá: module DO có loại ướt→HIGH, loại ướt→LOW.
- `INPUT_PULLUP` để tránh chân float khi sensor chưa cắm.
- Pending-retry giống MQ-2 để không mất event khi report lỗi tạm thời.
- siteId dùng chung qua `envIncidentSetSiteId` (1 siteId/device) — không cần setter riêng.

## Files
| File | Action | Ghi chú |
|------|--------|---------|
| `src/sensor/water_leak.h` | **create** | API: waterLeakBegin/Tick/IsWet + counter |
| `src/sensor/water_leak.cpp` | **create** | implement |
| `include/config.h` | modify | thêm `WATER_LEAK_*` |
| `include/config.example.h` | modify | mirror |
| `src/main.cpp` | modify | wire waterLeakBegin/Tick |

## Config thêm
```c
#define WATER_LEAK_ENABLED            1
#define WATER_LEAK_GPIO               2        // GPIO2 digital (WD §4.3)
#define WATER_LEAK_ACTIVE_HIGH        1        // 1 = ướt→HIGH; 0 = ướt→LOW
#define WATER_LEAK_POLL_INTERVAL_MS   500UL
#define WATER_LEAK_REARM_COOLDOWN_MS  300000UL // 5 phút
```

## Workflow (waterLeakTick)
```
throttle POLL_INTERVAL_MS → digitalRead(GPIO2)
wet = (read == (ACTIVE_HIGH ? HIGH : LOW))
if trigger.update(wet, now): pending=true
if pending: envIncidentReport(Flood, Critical, "water leak GPIO2")
            OK → pending=false, count++ ; FAIL → retry
```
main.cpp gọi `waterLeakTick()` chỉ khi wifi+ntp synced.

## Steps
- [ ] Thêm config knobs
- [ ] Viết water_leak.h + water_leak.cpp
- [ ] Wire main.cpp (begin + tick)
- [ ] Test IncidentTrigger dùng chung với #63 (test_incident_trigger)
- [ ] compile PASS cả 2 env esp32
