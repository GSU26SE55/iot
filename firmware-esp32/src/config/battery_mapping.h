// ==================================================================
// Sprint 1 — S1-FW-04 / NI §9.1 + §11 A: batteryMappings[]
//
// Spec NI §11 A: "config.h + load batteryMappings[]{serial, unitId}"
//
// Sprint 1 (mock): mỗi pin có thêm batteryAssetId (GUID seed của BatteryService dev).
// Sprint 5 (modbus_bms): sẽ dùng `unitId` thật cho Modbus RS485 multi-drop.
//
// Backend dev seed phải khớp `batteryAssetId` ở đây (#IoT2-02 phase A).
// Nếu Thắng seed GUID khác → sửa mảng kBatteryMappings[].
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace config {

struct BatteryMapping {
  const char* batteryAssetId;   // GUID seed của BatteryService (Sprint 1 legacy contract)
  const char* serial;           // BAT-MOCK-NNN — định danh fallback dùng cho log
  uint8_t     unitId;           // Modbus RS485 unitId (Sprint 5); Sprint 1 unused
  uint16_t    initialCycleCount;
};

// Sprint 1 mock — 8 pin slot (firmware chọn dùng MOCK_BATTERY_COUNT đầu tiên).
inline constexpr BatteryMapping kBatteryMappings[] = {
  {"11111111-1111-4111-8111-000000000001", "BAT-MOCK-001", 1, 100},
  {"11111111-1111-4111-8111-000000000002", "BAT-MOCK-002", 2, 112},
  {"11111111-1111-4111-8111-000000000003", "BAT-MOCK-003", 3, 124},
  {"11111111-1111-4111-8111-000000000004", "BAT-MOCK-004", 4, 136},
  {"11111111-1111-4111-8111-000000000005", "BAT-MOCK-005", 5, 148},
  {"11111111-1111-4111-8111-000000000006", "BAT-MOCK-006", 6, 160},
  {"11111111-1111-4111-8111-000000000007", "BAT-MOCK-007", 7, 172},
  {"11111111-1111-4111-8111-000000000008", "BAT-MOCK-008", 8, 184},
};

inline constexpr size_t kBatteryMappingsCount =
    sizeof(kBatteryMappings) / sizeof(kBatteryMappings[0]);

}  // namespace config
