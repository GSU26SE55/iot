// ==================================================================
// Sprint 1 — S1-FW-04: Mock BMS — sinh sensor reading giả
//
// Tham chiếu:
//   - tasksprint.md S1-FW-04
//   - newiot.md §9.1 (sensor driver layer), §11 A (anomaly scenarios)
//
// Scenario qua compile flag (define trong include/config.h hoặc build_flags):
//   MOCK_SCENARIO_OVERHEAT  — temp tăng dần 30 → 70°C
//   MOCK_SCENARIO_LOW_SOC   — SOC giảm dần 60 → 10%
//   (mặc định)              — normal: V sine 12.0–13.0V, temp 25–30°C, SOC 70–90%
//
// SensorReading struct nằm ở `src/core/reading.h` (NI §9.1 — dùng chung cho
// modbus_bms + ina226 + ds18b20 ở Sprint 5).
// ==================================================================
#pragma once
#include <cstddef>
#include "core/reading.h"

namespace bms {

// Số pin tối đa mock có thể sinh (giới hạn để biết dimension static buffer).
constexpr size_t kMockMaxBatteries = 8;

// Khởi tạo mock (seed RNG, chuẩn bị state per-battery).
// Gọi 1 lần trong setup().
void mockBegin();

// Sinh ra `count` readings và ghi vào `out[]`. `count` ≤ kMockMaxBatteries.
// Trả số reading thực sự sinh ra (luôn = min(count, kMockMaxBatteries)).
size_t mockGenerate(core::SensorReading* out, size_t count);

// In compile-time scenario hiện tại ra Serial — debug.
const char* mockScenarioName();

}  // namespace bms
