// ==================================================================
// Sprint 1 — S1-FW-04 / NI §9.1 core/reading.h
//
// SensorReading struct — 1 reading "vật lý" cho 1 pin tại 1 thời điểm.
// Spec NI §9.1: file riêng `src/core/reading.h` chứa struct này (KHÔNG để
// trong mock_bms.h vì các sensor source khác — modbus_bms, ina226, ds18b20
// — Sprint 3/5 — đều dùng cùng struct).
//
// Sprint 1 chỉ điền field bắt buộc theo legacy contract (NI §7.4 backward compat).
// Sprint 3 (S3-FW-04) sẽ mở rộng (sohPercent, chargingState, bmsErrorCode,
// sourceType, sensorSourceCode).
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

struct SensorReading {
  // Định danh pin trên backend. Legacy contract dùng GUID (`batteryAssetId`).
  // 36 chars + null terminator (UUID dạng "xxxxxxxx-xxxx-...").
  char batteryAssetId[37];

  // Tham chiếu Modbus unitId / serial từ batteryMappings[] (config/battery_mapping.h).
  char serial[20];                    // vd "BAT-MOCK-001"

  // ---- Sensor values ----
  float voltage;        // V
  float current;        // A   (dấu + = charge, − = discharge — quy ước backend)
  float temperature;    // °C
  float socPercent;     // 0..100
  uint16_t cycleCount;  // legacy: optional
};

}  // namespace core
