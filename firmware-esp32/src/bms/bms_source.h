// ==================================================================
// Sprint 5 — S5-FW-07 (#61): BMS source orchestrator (mock vs real switch).
//
// Compile flag `USE_MOCK_BMS`:
//   = 1 (default) → mock_bms::mockGenerateMultiSource (Sprint 1)
//   = 0           → modbus_bms + ina226 + ds18b20 thật (Sprint 5)
//
// API ổn định cho main.cpp — không cần biết đang dùng mock hay real.
// Layout output GIỐNG mock: [b0.bms, b0.ina, b0.ds, b1.bms, ...] = 3 reading/battery.
//
// Tham chiếu: tasksprint.md S5-FW-07
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "bms/mock_bms.h"     // kSourcesPerBattery + kMockMaxBatteries
#include "core/reading.h"

namespace bms {

// Số source per battery — re-use mock_bms::kSourcesPerBattery (=3) cho consistency.
// Real path: BMS primary + INA226 redundant + DS18B20 external-temp = 3.

// Init tất cả subsystem (modbus + I2C + 1-Wire) — gọi 1 lần trong setup().
// Mock mode: chỉ delegate mock_bms init.
// Real mode: modbusBegin + ina226Begin + ds18b20Begin + sht31Begin.
//
// Trả false nếu CRITICAL subsystem fail (vd Wire bus down).
// Cảnh báo non-critical: nếu DS18B20 detect 0 sensor → log warning, vẫn return true.
bool bmsSourceBegin();

// Poll all sources for all batteries — output tương thích mock layout.
// `out` ≥ batteryCount * kSourcesPerBattery slots.
// Returns số reading thực sự sinh (≤ batteryCount * 3 cho mock, ≤ batteryCount * 3 cho real).
//
// Mock mode: trả thẳng mock_bms::mockGenerateMultiSource.
// Real mode:
//   1. modbus_bms multi-drop → N BMS readings (primary)
//   2. ina226 → replicate cho N battery (redundant)
//   3. ds18b20 → N temp readings (external-temp)
//   4. Merge interleaved theo battery serial: [b0.bms, b0.ina, b0.ds, b1.bms, ...]
size_t bmsSourcePollAll(core::SensorReading* out, size_t batteryCount);

// Tên mode hiện tại (debug log).
const char* bmsSourceMode();

}  // namespace bms
