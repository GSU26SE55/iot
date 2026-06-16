// ==================================================================
// Sprint 5 — S5-FW-01/02/03 (#55/#56/#57): Modbus RTU BMS driver.
//
// Đọc BMS thật qua RS485 (MAX485 transceiver). Multi-drop bus với 4 BMS
// chia sẻ cùng A/B, mỗi BMS unitId riêng (S5-HW-03).
//
// Sprint 1 mock_bms vẫn được retain — bms_source.cpp dispatch giữa mock và
// modbus theo `USE_MOCK_BMS` (S5-FW-07).
//
// Hardware (WD §3):
//   ESP32-S3 GPIO17 (TX2) → MAX485 DI
//   ESP32-S3 GPIO18 (RX2) → MAX485 RO
//   ESP32-S3 GPIO16        → MAX485 DE+RE tied (driver enable)
//   120Ω terminator 2 đầu bus + biasing (optional).
//
// Tham chiếu:
//   - tasksprint.md S5-FW-01/02/03
//   - OV §A4 (BMS-RS485 checklist)
//   - NI §12 #3 (RS485 timing — DE/RE switching)
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "bms/bms_register_map.h"
#include "core/reading.h"

namespace bms {

// Init UART2 + ModbusMaster + DE/RE pin direction.
// Gọi 1 lần trong setup(). Trả false nếu UART begin fail.
bool modbusBegin();

// Đọc 1 BMS theo unitId. Map register sang `core::SensorReading` với:
//   sourceType=Bms (1), sensorSourceCode="primary"
// `serial` được caller set (từ battery_mapping.h theo unitId).
//
// Returns true nếu Modbus read OK + decode OK. False nếu timeout/CRC error.
bool modbusReadOne(uint8_t unitId,
                   const char* batteryAssetSerial,
                   core::SensorReading& outReading);

// S5-FW-03: Multi-drop poll toàn bộ BMS_UNIT_ID_START..START+COUNT-1.
// Caller cấp `out` buffer ≥ BMS_UNIT_ID_COUNT. Trả số reading thành công
// (skip BMS timeout, vẫn poll tiếp BMS khác để không bị block 1 BMS hỏng).
size_t modbusReadMultiDrop(core::SensorReading* out, size_t maxOut);

// Stats — đếm số poll thành công / timeout cho logStatsPeriodic.
uint32_t modbusPollOkCount();
uint32_t modbusPollFailCount();

}  // namespace bms
