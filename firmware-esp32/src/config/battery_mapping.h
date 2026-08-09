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
//
// Slot 1-4 khớp BatteryDataSeeder backend (BAT-2026-001..004, type 12V — hợp dải mock
// 12.0-13.0V). GUID cột 1 là legacy fallback, production contract resolve bằng serial.
// ⚠ Khi chuyển sang BMS thật (USE_MOCK_BMS=0, pack 8S 24V): thay slot 1 bằng dòng
//   "BAT-2026-REAL-001" bên dưới (asset 24V đã tạo sẵn trên backend 27/07/2026 —
//   type "LiFePO4 24V 30Ah", ngưỡng 20-29.5V). KHÔNG dùng BAT-2026-001 cho pack thật
//   (type 12V, ngưỡng 11-14V → 25.6V sẽ breach liên tục).
//   {"8590baf1-b628-46d5-9f97-ca29fa523ce5", "BAT-2026-REAL-001", "BAT-2026-REAL-001", 1, 0},
inline constexpr BatteryMapping kBatteryMappings[] = {
  // 4 slot đầu khớp serial seed thật trong battery_assets của backend local.
  // Sai serial thì backend vẫn trả 201 nhưng bỏ qua reading, DB trống trơn.
  // Slot 1 = pack THẬT (JK-BMS 8S 24V 30Ah), unitId 1, khớp BMS_UNIT_ID_COUNT=1.
  // Các slot dưới chỉ dùng ở chế độ mock (MOCK_BATTERY_COUNT).
  {"11111111-1111-4111-8111-000000000001", "BAT-2026-REAL-001", "BAT-2026-REAL-001", 1, 0},
  {"11111111-1111-4111-8111-000000000002", "BAT-2026-002", "BAT-2026-002", 2, 112},
  {"11111111-1111-4111-8111-000000000003", "BAT-2026-003", "BAT-2026-003", 3, 124},
  {"11111111-1111-4111-8111-000000000004", "BAT-2026-004", "BAT-2026-004", 4, 136},
  {"11111111-1111-4111-8111-000000000005", "BAT-MOCK-005", "BAT-MOCK-005", 5, 148},
  {"11111111-1111-4111-8111-000000000006", "BAT-MOCK-006", "BAT-MOCK-006", 6, 160},
  {"11111111-1111-4111-8111-000000000007", "BAT-MOCK-007", "BAT-MOCK-007", 7, 172},
  {"11111111-1111-4111-8111-000000000008", "BAT-MOCK-008", "BAT-MOCK-008", 8, 184},
};

inline constexpr size_t kBatteryMappingsCount =
    sizeof(kBatteryMappings) / sizeof(kBatteryMappings[0]);

}  // namespace config
