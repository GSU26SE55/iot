// ==================================================================
// Sprint 1 + Sprint 3 — Mock BMS sensor reading
//
// Tham chiếu:
//   - tasksprint.md S1-FW-04 + S3-FW-04
//   - newiot.md §9.1 (sensor driver layer), §11 A (anomaly scenarios)
//   - MO §52.9 (sourceType per nguồn vật lý)
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

// Sprint 3 (S3-FW-04): mỗi pin sinh 3 reading khớp đủ 3 nguồn vật lý spec MO §52.9:
//   1. BMS primary       — sourceType=Bms (1),         sensorSourceCode="primary"
//   2. INA226 redundant  — sourceType=IotGateway (2),  sensorSourceCode="redundant"
//   3. DS18B20 external  — sourceType=IotGateway (2),  sensorSourceCode="external-temp"
//
// Cross-source validation §1.6.6 cần ≥ 2 reading khác sourceType — với 3 nguồn,
// Sprint 6 detection có thể so BMS temp vs DS18B20 temp (cùng IotGateway type
// nhưng khác sensorSourceCode → phát hiện BMS sensor hỏng).
constexpr size_t kSourcesPerBattery = 3;

// Khởi tạo mock (seed RNG, chuẩn bị state per-battery).
// Gọi 1 lần trong setup().
void mockBegin();

// Sprint 1 (legacy): sinh `count` readings 1-source per pin (BMS primary only).
// Trả số reading thực sự sinh ra (luôn = min(count, kMockMaxBatteries)).
size_t mockGenerate(core::SensorReading* out, size_t count);

// Sprint 3 (S3-FW-04): sinh `batteryCount * kSourcesPerBattery` (= 3) readings per pin.
// `batteryCount` ≤ kMockMaxBatteries. Output buffer phải có ≥ `batteryCount * 3` slot.
// Sắp xếp: [p0.primary, p0.redundant, p0.external-temp, p1.primary, p1.redundant, p1.external-temp, ...]
//
// 1. Primary BMS (sourceType=Bms, sensorSourceCode="primary"):
//   - Full data: voltage/current/temperature/soc + sohPercent + chargingState + bmsErrorCode (overheat)
//
// 2. Redundant INA226 (sourceType=IotGateway, sensorSourceCode="redundant"):
//   - Voltage + current drift ±0.02V/±0.05A so BMS — sensor riêng cho cross-source §1.6.6
//   - Temperature mirror BMS (INA226 không đo temp)
//   - KHÔNG có sohPercent/chargingState/bmsErrorCode (sensor ngoài không biết)
//
// 3. External-temp DS18B20 (sourceType=IotGateway, sensorSourceCode="external-temp"):
//   - Temperature drift ±0.3°C so BMS — chính xác hơn BMS internal temp
//   - Voltage/current mirror BMS (DS18B20 chỉ đo nhiệt qua 1-Wire)
//   - KHÔNG có sohPercent/chargingState/bmsErrorCode
//
// Trả số reading thực sự sinh ra.
size_t mockGenerateMultiSource(core::SensorReading* out, size_t batteryCount);

// In compile-time scenario hiện tại ra Serial — debug.
const char* mockScenarioName();

// In compile-time scenario hiện tại ra Serial — debug.
const char* mockScenarioName();

}  // namespace bms
