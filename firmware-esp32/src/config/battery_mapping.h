// ==================================================================
// Sprint 1 + Sprint 3 — config/battery_mapping.h
//
// Spec NI §11 A: "config.h + load batteryMappings[]{serial, unitId}".
//
// Sprint 1 mock: mỗi pin có batteryAssetId (GUID seed BatteryService dev).
// Sprint 3 (S3-FW-04): production contract dùng `batteryAssetSerial` thay Guid
//                      → backend tự resolve serial → BatteryAssetId tại §52.5.
// Sprint 5 (modbus_bms): sẽ dùng `unitId` thật cho Modbus RS485 multi-drop.
//
// Backend dev seed phải khớp battery serial / Guid ở đây.
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace config {

struct BatteryMapping {
  const char* batteryAssetId;       // GUID — Sprint 1 legacy (fallback)
  const char* batteryAssetSerial;   // Serial — Sprint 3 production (priority)
  const char* serial;               // BAT-MOCK-NNN — alias dùng cho log + sensorSourceCode mapping
  uint8_t     unitId;               // Modbus RS485 unitId (Sprint 5); Sprint 1 unused
  uint16_t    initialCycleCount;
};

// Sprint 1+3 mock — 8 pin slot.
// Sprint 3: backend resolve `batteryAssetSerial` → BatteryAssetId qua endpoint #IoT2-18.
//           Nếu backend chưa seed serial, fallback `batteryAssetId` Guid (Sprint 1 mode).
inline constexpr BatteryMapping kBatteryMappings[] = {
  {"11111111-1111-4111-8111-000000000001", "BAT-MOCK-001", "BAT-MOCK-001", 1, 100},
  {"11111111-1111-4111-8111-000000000002", "BAT-MOCK-002", "BAT-MOCK-002", 2, 112},
  {"11111111-1111-4111-8111-000000000003", "BAT-MOCK-003", "BAT-MOCK-003", 3, 124},
  {"11111111-1111-4111-8111-000000000004", "BAT-MOCK-004", "BAT-MOCK-004", 4, 136},
  {"11111111-1111-4111-8111-000000000005", "BAT-MOCK-005", "BAT-MOCK-005", 5, 148},
  {"11111111-1111-4111-8111-000000000006", "BAT-MOCK-006", "BAT-MOCK-006", 6, 160},
  {"11111111-1111-4111-8111-000000000007", "BAT-MOCK-007", "BAT-MOCK-007", 7, 172},
  {"11111111-1111-4111-8111-000000000008", "BAT-MOCK-008", "BAT-MOCK-008", 8, 184},
};

inline constexpr size_t kBatteryMappingsCount =
    sizeof(kBatteryMappings) / sizeof(kBatteryMappings[0]);

}  // namespace config
