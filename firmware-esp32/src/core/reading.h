// ==================================================================
// Sprint 1 + Sprint 3 — core/reading.h
//
// SensorReading struct — dùng chung cho mock_bms, modbus_bms (Sprint 5),
// ina226 / ds18b20 (Sprint 5 redundant sensors).
//
// Sprint 1 chỉ điền field bắt buộc theo legacy contract (NI §7.4 backward compat).
// Sprint 3 (S3-FW-04) mở rộng với:
//   - batteryAssetSerial (thay batteryAssetId Guid)
//   - sourceType, sensorSourceCode (cross-source validation §1.6.6)
//   - sohPercent, chargingState, bmsErrorCode (production payload optional)
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

// Sprint 3 — Source type tags backend `SensorReadingSourceTypeEnum`.
enum class SourceType : uint8_t {
  Bms        = 1,    // Đọc qua RS485/Modbus từ BMS — Sprint 5
  IotGateway = 2,    // ESP32 sensor ngoài (INA226 redundant, DS18B20 external-temp)
  External   = 3,    // Manual import (chưa dùng)
};

// Sprint 3 — Backend `ChargingStateEnum`.
enum class ChargingState : uint8_t {
  Idle        = 1,
  Charging    = 2,
  Discharging = 3,
  Float       = 4,
  Bypass      = 5,
};

struct SensorReading {
  // ---- Legacy (Sprint 1) ----
  // Định danh pin trên backend. Sprint 1 legacy: batteryAssetId Guid.
  char batteryAssetId[37];      // 36 chars UUID + null
  // Tham chiếu Modbus serial từ batteryMappings[] (config/battery_mapping.h).
  char serial[24];              // "BAT-MOCK-001" etc.

  float    voltage;             // V
  float    current;              // A  (+ = charge, − = discharge)
  float    temperature;          // °C
  float    socPercent;           // 0..100
  uint16_t cycleCount;           // legacy: optional

  // ---- Sprint 3 production extensions (S3-FW-04) ----
  // sensorSourceCode: "primary" / "redundant" / "external-temp" (≤ 20 chars).
  char sensorSourceCode[24];
  SourceType   sourceType;
  ChargingState chargingState;
  float    sohPercent;          // 0..100 (set NaN/<0 nếu unknown — skip serialize)
  // bmsErrorCode: BMS raw error string (≤ 64 chars per spec MO §52.5 + backend
  // validation #IoT2-17). Buffer = 65 = 64 chars + null terminator.
  char     bmsErrorCode[65];
  // Flag: có gửi sohPercent/chargingState/bmsErrorCode trong payload không?
  bool     hasSoh;
  bool     hasChargingState;
  bool     hasBmsError;
};

}  // namespace core
