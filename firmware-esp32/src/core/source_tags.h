// ==================================================================
// Sprint 6 — S6-FW-03 (#65): Canonical cross-source tags.
//
// Cross-source validation (backend #IoT2-28 CrossSourceValidationService) so
// 2 reading CÙNG battery + CÙNG khung thời gian nhưng KHÁC `sourceType`
// (Bms vs IotGateway). Nếu lệch quá ngưỡng → Alert(SensorMismatch=15).
//
// Để cặp được tạo, firmware PHẢI tag nhất quán:
//   - BMS (RS485/Modbus)        → sourceType=Bms,        code="primary"
//   - INA226 (V/I redundant)    → sourceType=IotGateway, code="redundant"
//   - DS18B20 (temp ngoài pin)  → sourceType=IotGateway, code="external-temp"
//
// File này là single source of truth cho 3 chuỗi tag — modbus_bms / ina226 /
// ds18b20 tham chiếu constant thay vì hard-code literal rời rạc (tránh typo
// làm vỡ cross-source pairing mà không ai phát hiện). Pure header — test native
// (test_source_tags) verify invariant.
//
// Tham chiếu:
//   - tasksprint.md S6-FW-03
//   - overall.iot.md §B2 (o) — cross-source pair
//   - backend overall.md §1.6.6, §52.9 (sourceType per nguồn vật lý)
// ==================================================================
#pragma once

#include "core/reading.h"

namespace core {

// Canonical sensorSourceCode strings (≤ 20 chars — khớp SensorReading.sensorSourceCode[24]).
inline constexpr const char* kSourceCodePrimary      = "primary";        // BMS qua RS485
inline constexpr const char* kSourceCodeRedundant    = "redundant";      // INA226 V/I
inline constexpr const char* kSourceCodeExternalTemp = "external-temp";  // DS18B20 temp

// sourceType tương ứng — phải KHÁC nhau giữa BMS và sensor gateway để
// CrossSourceValidationService tạo được cặp so sánh.
inline constexpr SourceType kSourceTypePrimary      = SourceType::Bms;
inline constexpr SourceType kSourceTypeRedundant    = SourceType::IotGateway;
inline constexpr SourceType kSourceTypeExternalTemp = SourceType::IotGateway;

}  // namespace core
