// ==================================================================
// Sprint 2 — S2-FW-03: Heartbeat telemetry task
//
// Gửi heartbeat lên POST /api/iot-devices/heartbeat mỗi 60s (đồng bộ MO §52.4).
// Body fields ESP32 (per MO §52.2 mapping):
//   - Temperature       → chip temp (°C) — internal sensor
//   - MemoryUsageMb     → free heap (MB)
//   - SignalStrengthDbm → WiFi.RSSI()
//   - LocalQueueDepth   → 0 (Sprint 2 chưa có NVS queue — Sprint 3 cập nhật)
//   - FirmwareVersion   → từ FW_VERSION
//   - DeviceTimestamp   → isoNow()
//   - UptimeSeconds     → millis()/1000
//   - Cpu / DiskFreeMb  → null (ESP32 không có khái niệm Linux CPU/disk)
//
// Tham chiếu:
//   - tasksprint.md S2-FW-03 (AC: backend hypertable có row mỗi 60s)
//   - backend IotDeviceHeartbeatCommand
// ==================================================================
#pragma once
#include <cstdint>

namespace telemetry {

void heartbeatBegin(uint32_t intervalMs);

// Đổi interval runtime (sau provision response trả về `heartbeatIntervalSeconds` khác default).
void heartbeatSetInterval(uint32_t intervalMs);

// Gọi mỗi loop tick. Tự check elapsed, gọi POST khi đủ interval.
// Caller PHẢI đảm bảo WiFi connected + NTP synced trước khi gọi.
void heartbeatTick();

// Force gửi heartbeat ngay (debug / serial command). Trả true nếu HTTP 2xx.
bool heartbeatSendNow();

// Counters cho stats log.
uint32_t heartbeatOkCount();
uint32_t heartbeatFailCount();

}  // namespace telemetry
