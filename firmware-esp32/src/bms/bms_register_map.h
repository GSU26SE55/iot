// ==================================================================
// Sprint 5 — S5-FW-01 (#55): BMS Modbus RTU register map.
//
// Mỗi model BMS có layout register khác nhau. Module này định nghĩa struct
// chung + 3 preset (Daly stub, JBD, JK-BMS) — chọn qua macro BMS_MODEL.
//
// Lưu ý độc lập với ESP32/Modbus lib (chỉ struct + constexpr) — test native được.
//
// Tham chiếu:
//   - tasksprint.md S5-FW-01
//   - OV §A4 (BMS-RS485 checklist)
//   - MO §52.5 (cycleCount, sohPercent, chargingState, bmsErrorCode)
//
// ⚠ QUAN TRỌNG: Register layout phải verify với từng model thực tế bằng
//   USB-RS485 + Modbus Poll trên laptop TRƯỚC khi flash ESP32 (S5-HW-02).
//   Sai register → ESP32 đọc số rác → backend reject hoặc lưu data sai.
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace bms {

// Modbus function code — phần lớn BMS dùng 0x03 (Holding Registers).
// Một số JK-BMS / Daly mới dùng 0x04 (Input Registers).
enum class ModbusFunc : uint8_t {
  HoldingRegisters = 0x03,
  InputRegisters   = 0x04,
};

// Struct chung — null register (0xFFFF) = field không có trên BMS đó.
struct BmsRegisterMap {
  const char*  name;                 // debug log

  ModbusFunc   functionCode;
  uint16_t     startAddress;         // base address để issue 1 read query lớn
  uint16_t     registerCount;        // số 16-bit register đọc per query

  // ---- Field offsets (relative to startAddress) ----
  // 0xFFFF = field không support (BMS không trả) → reading sẽ skip / mock 0.

  // Voltage (V) — uint16 raw × scale
  uint16_t     voltageOffset;
  float        voltageScale;         // raw × scale = V. Vd JBD 1/100 = 0.01

  // Current (A) — int16 raw × scale (positive = charge, negative = discharge)
  uint16_t     currentOffset;
  float        currentScale;
  bool         currentSigned;        // true: int16 (Daly/JBD); false: uint16

  // Temperature (°C) — int16 raw × scale, offset (Kelvin → C trừ 273.15)
  uint16_t     temperatureOffset;
  float        temperatureScale;
  float        temperatureBias;      // °C bias (JBD: -273.15 nếu raw là Kelvin × 10)

  // SOC (%) — uint16 raw × scale
  uint16_t     socOffset;
  float        socScale;             // 1.0 nếu raw là %, 0.1 nếu × 10

  // SOH (%) — optional (MO §52.5)
  uint16_t     sohOffset;            // 0xFFFF nếu BMS không có
  float        sohScale;

  // Cycle count — optional
  uint16_t     cycleOffset;          // 0xFFFF nếu không có

  // Charging state enum (MO §52.5 ChargingStateEnum: 1=Idle,2=Charging,3=Discharging,4=Float,5=Bypass)
  uint16_t     chargingStateOffset;  // 0xFFFF nếu không có

  // BMS error code — uint16 raw bitmask, ESP32 sẽ convert sang string ≤ 64 chars
  uint16_t     errorCodeOffset;      // 0xFFFF nếu không có
};

// Sentinel cho field không support.
constexpr uint16_t kRegMissing = 0xFFFF;

// ============== Preset register maps ==============

// JBD BMS (LiFePO4 phổ biến) — verify on JBD-SP04S028 (4S 100A).
// Reference: JBD Modbus protocol v1.x.
// Single query 0x0000-0x0009 (10 registers) đủ pack-level data.
inline constexpr BmsRegisterMap kJbdBmsMap = {
  /* name */              "JBD",
  /* functionCode */      ModbusFunc::HoldingRegisters,
  /* startAddress */      0x0000,
  /* registerCount */     10,
  /* voltageOffset */     0,            // 0x0000: pack voltage × 0.01V
  /* voltageScale */      0.01f,
  /* currentOffset */     1,            // 0x0001: pack current × 0.01A (int16 signed)
  /* currentScale */      0.01f,
  /* currentSigned */     true,
  /* temperatureOffset */ 4,            // 0x0004: temp probe 1 (Kelvin × 10) — JBD format
  /* temperatureScale */  0.1f,
  /* temperatureBias */   -273.15f,
  /* socOffset */         3,            // 0x0003: SOC %
  /* socScale */          1.0f,
  /* sohOffset */         kRegMissing,  // JBD không expose SOH qua Modbus chuẩn
  /* sohScale */          1.0f,
  /* cycleOffset */       8,            // 0x0008: cycle count
  /* chargingStateOffset */ kRegMissing,
  /* errorCodeOffset */   5,            // 0x0005: protection status bitmask
};

// JK-BMS — register layout khác JBD.
// Reference: JK-BMS Modbus protocol v3.x.
// Verify on JK-B2A24S20P.
inline constexpr BmsRegisterMap kJkBmsMap = {
  /* name */              "JK-BMS",
  /* functionCode */      ModbusFunc::HoldingRegisters,
  /* startAddress */      0x1200,
  /* registerCount */     16,
  /* voltageOffset */     0,            // 0x1200: total voltage × 0.001V
  /* voltageScale */      0.001f,
  /* currentOffset */     2,            // 0x1202: total current × 0.001A (int32 — chỉ đọc low 16 bit cho Sprint 5)
  /* currentScale */      0.001f,
  /* currentSigned */     true,
  /* temperatureOffset */ 6,            // 0x1206: MOS temp °C × 0.1
  /* temperatureScale */  0.1f,
  /* temperatureBias */   0.0f,
  /* socOffset */         8,            // 0x1208: SOC %
  /* socScale */          1.0f,
  /* sohOffset */         9,            // 0x1209: SOH %
  /* sohScale */          1.0f,
  /* cycleOffset */       10,
  /* chargingStateOffset */ 12,         // 0x120C: state enum (cần map sang ChargingStateEnum)
  /* errorCodeOffset */   14,
};

// Daly — Daly dùng custom protocol (NOT chuẩn Modbus RTU). Stub này dùng cho
// Daly-modbus-bridge module riêng. Sprint 5 KHUYẾN NGHỊ JBD/JK trực tiếp.
inline constexpr BmsRegisterMap kDalyBmsMap = {
  /* name */              "Daly (stub — verify với hardware)",
  /* functionCode */      ModbusFunc::HoldingRegisters,
  /* startAddress */      0x0000,
  /* registerCount */     8,
  /* voltageOffset */     0,
  /* voltageScale */      0.1f,
  /* currentOffset */     1,
  /* currentScale */      0.1f,
  /* currentSigned */     true,
  /* temperatureOffset */ 3,
  /* temperatureScale */  1.0f,
  /* temperatureBias */   -40.0f,        // Daly offset −40°C
  /* socOffset */         2,
  /* socScale */          0.1f,
  /* sohOffset */         kRegMissing,
  /* sohScale */          1.0f,
  /* cycleOffset */       kRegMissing,
  /* chargingStateOffset */ kRegMissing,
  /* errorCodeOffset */   kRegMissing,
};

// Generic — placeholder cho user tự define ở `bms_register_map_custom.h`.
inline constexpr BmsRegisterMap kGenericBmsMap = {
  "Generic",
  ModbusFunc::HoldingRegisters,
  0x0000, 4,
  0, 0.01f,
  1, 0.01f, true,
  2, 0.1f, 0.0f,
  3, 1.0f,
  kRegMissing, 1.0f,
  kRegMissing,
  kRegMissing,
  kRegMissing,
};

// Lookup chính bằng macro BMS_MODEL (1=Daly, 2=JBD, 3=JK, 4=Generic).
const BmsRegisterMap& selectedBmsMap();

// Pure helpers — TEST được native (không cần ESP32).
// Decode 1 raw register value → physical unit theo offset trong map.
// raw[0..registerCount-1] = buffer ModbusMaster trả về.
float decodeVoltage    (const BmsRegisterMap& m, const uint16_t* raw);
float decodeCurrent    (const BmsRegisterMap& m, const uint16_t* raw);
float decodeTemperature(const BmsRegisterMap& m, const uint16_t* raw);
float decodeSoc        (const BmsRegisterMap& m, const uint16_t* raw);

// Returns true nếu map có field (offset != kRegMissing).
inline bool hasSoh           (const BmsRegisterMap& m) { return m.sohOffset           != kRegMissing; }
inline bool hasCycle         (const BmsRegisterMap& m) { return m.cycleOffset         != kRegMissing; }
inline bool hasChargingState (const BmsRegisterMap& m) { return m.chargingStateOffset != kRegMissing; }
inline bool hasErrorCode     (const BmsRegisterMap& m) { return m.errorCodeOffset     != kRegMissing; }

// Convert error code bitmask (uint16) → human-readable string ≤ 64 chars.
// Trả số chars ghi vào outBuf (null-terminated). 0 nếu bitmask=0 (no error).
size_t decodeErrorCode(uint16_t bitmask, char* outBuf, size_t outBufLen);

// Convert raw charging state register → backend ChargingStateEnum (1-5).
// Returns 1 (Idle) nếu unknown.
uint8_t decodeChargingState(uint16_t raw);

}  // namespace bms
