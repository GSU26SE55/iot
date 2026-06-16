// ==================================================================
// Sprint 5 — S5-FW-04 (#58): INA226 current + voltage sensor (redundant).
//
// INA226 cảm biến điện áp bus + dòng điện qua shunt, kết quả I2C 16-bit.
// Vai trò Sprint 5: redundant với BMS để cross-source validation (Sprint 6
// SensorMismatch). KHÔNG dùng cho energy/kWh metrics (ADR-017 / MO §53.1).
//
// Hardware (WD §4.2):
//   I2C bus shared với SHT31. ESP32-S3 GPIO8 SDA, GPIO9 SCL.
//   INA226 ADDR pins: AD0=GND, AD1=GND → 0x40 (default).
//
// Sprint 5 hardware giả định: 1 INA226 đo bus điện áp + tổng dòng. Reading
// được replicate cho mỗi battery serial trong batch (cross-source pair với
// từng BMS reading). Hardware mở rộng (4 INA226 — 1/battery) sẽ Sprint 6+.
//
// Tham chiếu:
//   - tasksprint.md S5-FW-04
//   - OV §A5 (INA226 + DS18B20 + SHT31 checklist)
//   - MO §52.9 (sourceType per nguồn vật lý)
//   - MO §53.1 (ADR-017: KHÔNG energy metrics)
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/reading.h"

namespace sensor {

// Init I2C bus (gọi 1 lần — chia sẻ với SHT31 init nếu cùng bus) + config INA226.
// Trả false nếu INA226 không respond (sensor chưa cắm hoặc địa chỉ sai).
bool ina226Begin();

// Đọc 1 sample (V + I). Trả false nếu I2C error.
// `voltageOut` = bus voltage (V), `currentOut` = current (A, signed).
bool ina226ReadOnce(float* voltageOut, float* currentOut);

// S5-FW-04 helper: tạo N reading "redundant" — replicate cùng 1 INA226 read cho
// N battery serial (hardware Sprint 5: 1 INA226 chung). Mỗi reading tag:
//   sourceType=IotGateway (2), sensorSourceCode="redundant"
//
// `serials` = array of N battery serial strings (caller cấp).
// `out` buffer ≥ N readings. Returns số reading thực sự sinh (= N nếu read OK, 0 nếu fail).
size_t ina226BuildRedundantReadings(const char* const* serials, size_t n,
                                    core::SensorReading* out, size_t maxOut);

// Stats cho logStatsPeriodic.
uint32_t ina226ReadOkCount();
uint32_t ina226ReadFailCount();

}  // namespace sensor
