// ==================================================================
// Sprint 5 — S5-FW-01: BMS register map decoders (pure C++).
// ==================================================================
#include "bms/bms_register_map.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include <cstdio>
#include <cstring>

namespace bms {

const BmsRegisterMap& selectedBmsMap() {
#if BMS_MODEL == 1
  return kDalyBmsMap;
#elif BMS_MODEL == 2
  return kJbdBmsMap;
#elif BMS_MODEL == 3
  return kJkBmsMap;
#elif BMS_MODEL == 4
  return kGenericBmsMap;
#else
  #error "BMS_MODEL phải là 1 (Daly), 2 (JBD), 3 (JK), 4 (Generic)"
#endif
}

float decodeVoltage(const BmsRegisterMap& m, const uint16_t* raw) {
  if (m.voltageOffset == kRegMissing) return 0.0f;
  uint16_t r = raw[m.voltageOffset];
  return static_cast<float>(r) * m.voltageScale;
}

float decodeCurrent(const BmsRegisterMap& m, const uint16_t* raw) {
  if (m.currentOffset == kRegMissing) return 0.0f;
  uint16_t r = raw[m.currentOffset];
  if (m.currentSigned) {
    // Treat as int16 — high bit = negative (discharge).
    int16_t s = static_cast<int16_t>(r);
    return static_cast<float>(s) * m.currentScale;
  }
  return static_cast<float>(r) * m.currentScale;
}

float decodeTemperature(const BmsRegisterMap& m, const uint16_t* raw) {
  if (m.temperatureOffset == kRegMissing) return 0.0f;
  uint16_t r = raw[m.temperatureOffset];
  // Temp có thể signed (int16) cho âm độ — sử dụng int16 conversion.
  int16_t s = static_cast<int16_t>(r);
  return static_cast<float>(s) * m.temperatureScale + m.temperatureBias;
}

float decodeSoc(const BmsRegisterMap& m, const uint16_t* raw) {
  if (m.socOffset == kRegMissing) return 0.0f;
  uint16_t r = raw[m.socOffset];
  float soc = static_cast<float>(r) * m.socScale;
  // Clamp [0, 100] — defensive (BMS có thể trả raw out-of-range nếu chưa calibrate).
  if (soc < 0.0f)   soc = 0.0f;
  if (soc > 100.0f) soc = 100.0f;
  return soc;
}

uint32_t decodeJkUnsigned32(uint16_t highWord, uint16_t lowWord) {
  return (static_cast<uint32_t>(highWord) << 16) |
         static_cast<uint32_t>(lowWord);
}

int32_t decodeJkSigned32(uint16_t highWord, uint16_t lowWord) {
  return static_cast<int32_t>(decodeJkUnsigned32(highWord, lowWord));
}

float decodeJkPackVoltage(uint16_t highWord, uint16_t lowWord) {
  return static_cast<float>(decodeJkUnsigned32(highWord, lowWord)) / 1000.0f;
}

float decodeJkPackCurrent(uint16_t highWord, uint16_t lowWord) {
  return static_cast<float>(decodeJkSigned32(highWord, lowWord)) / 1000.0f;
}

float decodeJkTemperature(uint16_t raw) {
  return static_cast<float>(static_cast<int16_t>(raw)) / 10.0f;
}

float decodeJkSoc(uint16_t packedBalanceAndSoc) {
  return static_cast<float>(packedBalanceAndSoc & 0x00FFu);
}

float decodeJkSoh(uint16_t packedSohAndPrecharge) {
  return static_cast<float>((packedSohAndPrecharge >> 8) & 0x00FFu);
}

bool jkChargeEnabled(uint16_t packedSwitches) {
  return ((packedSwitches >> 8) & 0x00FFu) != 0;
}

bool jkDischargeEnabled(uint16_t packedSwitches) {
  return (packedSwitches & 0x00FFu) != 0;
}

uint8_t decodeJkChargingState(uint16_t packedSwitches, float currentAmps) {
  const bool chargeEnabled = jkChargeEnabled(packedSwitches);
  const bool dischargeEnabled = jkDischargeEnabled(packedSwitches);
  constexpr float kActiveCurrentAmps = 0.05f;

  if (chargeEnabled && currentAmps > kActiveCurrentAmps) return 2;
  if (dischargeEnabled && currentAmps < -kActiveCurrentAmps) return 3;
  return 1;
}

uint8_t decodeChargingState(uint16_t raw) {
  // Mapping tùy BMS — Sprint 5 dùng heuristic chung:
  //   0/1 = Idle
  //   2/0x10 = Charging
  //   3/0x20 = Discharging
  //   4/0x40 = Float
  //   5/0x80 = Bypass
  // BMS thật phải override mapping trong driver per model.
  switch (raw) {
    case 0:
    case 1:    return 1; // Idle
    case 2:
    case 0x10: return 2; // Charging
    case 3:
    case 0x20: return 3; // Discharging
    case 4:
    case 0x40: return 4; // Float
    case 5:
    case 0x80: return 5; // Bypass
    default:   return 1; // Unknown → Idle (defensive)
  }
}

// Map bitmask sang short string. Bit names theo JBD convention; JK-BMS / Daly
// có thể khác bit assignment — kiểm tra datasheet + override per-model nếu cần.
//
// Backend nhận `bmsErrorCode` string ≤ 64 chars (MO §52.5, #IoT2-17 validation).
// Mỗi flag ngắn (3-4 chars) để fit nhiều flag trong 64 chars.
size_t decodeErrorCode(uint16_t bitmask, char* outBuf, size_t outBufLen) {
  if (outBuf == nullptr || outBufLen == 0) return 0;
  if (bitmask == 0) { outBuf[0] = '\0'; return 0; }

  // Common JBD protection bits — short codes.
  struct Flag { uint16_t bit; const char* code; };
  static constexpr Flag kFlags[] = {
    { 0x0001, "OVP"  },   // Cell over-voltage
    { 0x0002, "UVP"  },   // Cell under-voltage
    { 0x0004, "POV"  },   // Pack over-voltage
    { 0x0008, "PUV"  },   // Pack under-voltage
    { 0x0010, "OTP"  },   // Charge over-temp
    { 0x0020, "UTP"  },   // Charge under-temp
    { 0x0040, "OTPD" },   // Discharge over-temp
    { 0x0080, "UTPD" },   // Discharge under-temp
    { 0x0100, "OCC"  },   // Charge over-current
    { 0x0200, "OCD"  },   // Discharge over-current
    { 0x0400, "SCD"  },   // Short circuit
    { 0x0800, "AFE"  },   // AFE error
    { 0x1000, "SWLK" },   // MOSFET lock
  };

  outBuf[0] = '\0';
  size_t used = 0;
  for (const auto& f : kFlags) {
    if ((bitmask & f.bit) == 0) continue;
    size_t codeLen = strlen(f.code);
    size_t needed = (used == 0) ? codeLen : codeLen + 1;  // +1 cho ',' separator
    if (used + needed + 1 > outBufLen) break;             // +1 cho null terminator
    if (used > 0) { outBuf[used++] = ','; }
    memcpy(outBuf + used, f.code, codeLen);
    used += codeLen;
    outBuf[used] = '\0';
  }
  return used;
}

}  // namespace bms
