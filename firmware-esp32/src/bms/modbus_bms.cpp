// ==================================================================
// Sprint 5 — S5-FW-01/02/03: Modbus RTU BMS driver implementation.
// ==================================================================
#include "bms/modbus_bms.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/battery_mapping.h"
#include "core/source_tags.h"   // S6-FW-03 (#65): canonical cross-source tags

#include <Arduino.h>
#include <HardwareSerial.h>
#include <ModbusMaster.h>

#include <cstring>

namespace bms {

namespace {

// UART2 trên ESP32-S3 DevKitC-1 — GPIO17 TX, GPIO18 RX (configurable via begin).
HardwareSerial s_rs485Serial(2);
ModbusMaster   s_modbus;

uint32_t s_pollOk   = 0;
uint32_t s_pollFail = 0;
bool     s_inited   = false;

// S5-FW-02: DE/RE callbacks. ModbusMaster lib gọi pre/postTransmission để
// switch direction trước/sau send.
// DE+RE tied: HIGH = driver enabled (TX), LOW = receiver enabled (RX).
void preTransmission() {
#if BMS_RS485_DE_PIN >= 0
  digitalWrite(BMS_RS485_DE_PIN, HIGH);
  // Delay nhỏ để MAX485 chuyển direction (datasheet: ~50ns, nhưng GPIO
  // propagation + driver enable ≈ vài µs — add 10µs để chắc chắn).
  delayMicroseconds(10);
#endif
}

void postTransmission() {
#if BMS_RS485_DE_PIN >= 0
  // Đợi UART flush hết byte cuối trước khi switch về RX.
  // ESP32 HardwareSerial.flush() block tới TX_DONE.
  s_rs485Serial.flush();
  delayMicroseconds(50);  // thêm guard time (Modbus T3.5 = 3.5 char times)
  digitalWrite(BMS_RS485_DE_PIN, LOW);
#endif
}

// Convert serial → unitId thông qua battery_mapping.
uint8_t serialToUnitId(const char* serial) {
  if (!serial) return 0;
  for (const auto& m : config::kBatteryMappings) {
    if (strcmp(m.serial, serial) == 0) return m.unitId;
  }
  return 0;
}

// Convert unitId → battery serial thông qua battery_mapping.
const char* unitIdToSerial(uint8_t unitId) {
  for (const auto& m : config::kBatteryMappings) {
    if (m.unitId == unitId) return m.serial;
  }
  return nullptr;
}

void initializeReading(const char* batteryAssetSerial, core::SensorReading& out) {
  memset(&out, 0, sizeof(out));

  if (batteryAssetSerial) {
    strncpy(out.serial, batteryAssetSerial, sizeof(out.serial) - 1);
  }
  for (const auto& m : config::kBatteryMappings) {
    if (strcmp(m.serial, out.serial) == 0) {
      strncpy(out.batteryAssetId, m.batteryAssetId, sizeof(out.batteryAssetId) - 1);
      out.cycleCount = m.initialCycleCount;
      break;
    }
  }
}

void tagPrimarySource(core::SensorReading& out) {
  out.sourceType = core::kSourceTypePrimary;
  strncpy(out.sensorSourceCode, core::kSourceCodePrimary,
          sizeof(out.sensorSourceCode) - 1);
}

bool readJkRegisters(uint16_t address, uint16_t count, uint16_t* out) {
  uint8_t result = s_modbus.readHoldingRegisters(address, count);
  for (uint8_t retry = 0;
       retry < BMS_POLL_RETRY && result != s_modbus.ku8MBSuccess;
       ++retry) {
    delay(20);
    result = s_modbus.readHoldingRegisters(address, count);
  }

  if (result != s_modbus.ku8MBSuccess) {
    Serial.printf("[modbus-jk] addr=0x%04X count=%u FAIL code=0x%02X\n",
                  address, count, result);
    return false;
  }

  for (uint16_t i = 0; i < count; ++i) {
    out[i] = s_modbus.getResponseBuffer(i);
  }
  return true;
}

bool readJkRealtime(core::SensorReading& out) {
  uint16_t mosTemp[1] = {0};
  uint16_t voltage[2] = {0};
  uint16_t current[2] = {0};
  uint16_t batteryTemps[2] = {0};
  uint16_t soc[1] = {0};

  if (!readJkRegisters(kJkMosTemperatureAddress, 1, mosTemp) ||
      !readJkRegisters(kJkPackVoltageAddress, 2, voltage) ||
      !readJkRegisters(kJkPackCurrentAddress, 2, current) ||
      !readJkRegisters(kJkBatteryTempsAddress, 2, batteryTemps) ||
      !readJkRegisters(kJkSocAddress, 1, soc)) {
    return false;
  }

  out.voltage = decodeJkPackVoltage(voltage[0], voltage[1]);
  out.current = decodeJkPackCurrent(current[0], current[1]);
  out.temperature = decodeJkTemperature(mosTemp[0]);
  out.socPercent = decodeJkSoc(soc[0]);

  uint16_t cycleCount[2] = {0};
  if (readJkRegisters(kJkCycleCountAddress, 2, cycleCount)) {
    uint32_t cycles = decodeJkUnsigned32(cycleCount[0], cycleCount[1]);
    out.cycleCount = cycles > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(cycles);
  }

  uint16_t soh[1] = {0};
  if (readJkRegisters(kJkSohAddress, 1, soh)) {
    out.sohPercent = decodeJkSoh(soh[0]);
    if (out.sohPercent > 100.0f) out.sohPercent = 100.0f;
    out.hasSoh = true;
  }

  uint16_t switches[1] = {0};
  if (readJkRegisters(kJkSwitchStatusAddress, 1, switches)) {
    out.chargingState = static_cast<core::ChargingState>(
        decodeJkChargingState(switches[0], out.current));
    out.hasChargingState = true;
  }

  uint16_t alarm[2] = {0};
  if (readJkRegisters(kJkAlarmAddress, 2, alarm)) {
    uint32_t alarmRaw = decodeJkUnsigned32(alarm[0], alarm[1]);
    if (alarmRaw != 0) {
      snprintf(out.bmsErrorCode, sizeof(out.bmsErrorCode),
               "JK:0x%08lX", static_cast<unsigned long>(alarmRaw));
      out.hasBmsError = true;
    }
  }

  Serial.printf("[modbus-jk] Tmos=%.1f T1=%.1f T2=%.1f SOC=%.0f%%\n",
                out.temperature,
                decodeJkTemperature(batteryTemps[0]),
                decodeJkTemperature(batteryTemps[1]),
                out.socPercent);
  return true;
}

}  // namespace

bool modbusBegin() {
  if (s_inited) return true;

  // UART2 begin với pin custom — ESP32 Arduino: begin(baud, config, rxPin, txPin).
  s_rs485Serial.begin(BMS_RS485_BAUD, SERIAL_8N1,
                      BMS_RS485_RX_PIN, BMS_RS485_TX_PIN);

#if BMS_RS485_DE_PIN >= 0
  pinMode(BMS_RS485_DE_PIN, OUTPUT);
  digitalWrite(BMS_RS485_DE_PIN, LOW);  // default = receive
#endif

  // ModbusMaster: slave ID set per request — chỉ init transport ở đây.
  // begin(slaveID, stream) — slaveID sẽ override trong modbusReadOne.
  s_modbus.begin(1 /*placeholder*/, s_rs485Serial);
  s_modbus.preTransmission(preTransmission);
  s_modbus.postTransmission(postTransmission);

  s_inited = true;
  Serial.printf("[modbus] init OK — baud=%lu tx=%d rx=%d de=%d model=%s\n",
                static_cast<unsigned long>(BMS_RS485_BAUD),
                static_cast<int>(BMS_RS485_TX_PIN),
                static_cast<int>(BMS_RS485_RX_PIN),
                static_cast<int>(BMS_RS485_DE_PIN),
                selectedBmsMap().name);
  return true;
}

bool modbusReadOne(uint8_t unitId,
                   const char* batteryAssetSerial,
                   core::SensorReading& out) {
  if (!s_inited) return false;
  if (unitId == 0) return false;

  const BmsRegisterMap& map = selectedBmsMap();

  // ModbusMaster có private setter cho slave — workaround:
  // re-call begin() với slave mới (mainstream lib cách thường dùng).
  s_modbus.begin(unitId, s_rs485Serial);

#if BMS_MODEL == 3
  initializeReading(batteryAssetSerial, out);
  if (!readJkRealtime(out)) {
    s_pollFail++;
    return false;
  }
  tagPrimarySource(out);

  s_pollOk++;
  Serial.printf("[modbus] unitId=%u OK V=%.3f I=%.3f T=%.1f SOC=%.1f%%%s\n",
                unitId, out.voltage, out.current, out.temperature, out.socPercent,
                out.hasBmsError ? " ⚠ ERR" : "");
  return true;
#endif

  // Issue read theo function code.
  uint8_t result;
  if (map.functionCode == ModbusFunc::HoldingRegisters) {
    result = s_modbus.readHoldingRegisters(map.startAddress, map.registerCount);
  } else {
    result = s_modbus.readInputRegisters(map.startAddress, map.registerCount);
  }

  // Retry 1 lần nếu fail (S5-FW-01 acceptance: ổn định ±0.2V — cần retry để
  // tránh false alarm trong môi trường nhiễu RS485).
  for (uint8_t r = 0; r < BMS_POLL_RETRY && result != s_modbus.ku8MBSuccess; ++r) {
    delay(20);  // wait cho bus settle
    if (map.functionCode == ModbusFunc::HoldingRegisters) {
      result = s_modbus.readHoldingRegisters(map.startAddress, map.registerCount);
    } else {
      result = s_modbus.readInputRegisters(map.startAddress, map.registerCount);
    }
  }

  if (result != s_modbus.ku8MBSuccess) {
    Serial.printf("[modbus] unitId=%u read FAIL code=0x%02X (timeout/CRC)\n",
                  unitId, result);
    s_pollFail++;
    return false;
  }

  // Copy raw buffer ra array để decode.
  uint16_t raw[32];
  size_t n = map.registerCount > 32 ? 32 : map.registerCount;
  for (size_t i = 0; i < n; ++i) {
    raw[i] = s_modbus.getResponseBuffer(i);
  }

  // ---- Fill SensorReading ----
  initializeReading(batteryAssetSerial, out);

  // Decode physical units.
  out.voltage     = decodeVoltage(map, raw);
  out.current     = decodeCurrent(map, raw);
  out.temperature = decodeTemperature(map, raw);
  out.socPercent  = decodeSoc(map, raw);

  // Sprint 3 production tags — BMS primary source.
  // S6-FW-03 (#65): dùng canonical constant (single source of truth) — tránh typo
  // làm vỡ cross-source pairing với INA226/DS18B20 (sourceType phải KHÁC IotGateway).
  tagPrimarySource(out);

  // Optional fields — chỉ điền nếu BMS map support.
  if (hasSoh(map)) {
    uint16_t sohRaw = raw[map.sohOffset];
    out.sohPercent = static_cast<float>(sohRaw) * map.sohScale;
    if (out.sohPercent < 0.0f)   out.sohPercent = 0.0f;
    if (out.sohPercent > 100.0f) out.sohPercent = 100.0f;
    out.hasSoh = true;
  }

  if (hasCycle(map)) {
    uint16_t cyc = raw[map.cycleOffset];
    // Override mapping value với raw từ BMS (chính xác hơn initialCycleCount).
    out.cycleCount = cyc;
  }

  if (hasChargingState(map)) {
    uint16_t csRaw = raw[map.chargingStateOffset];
    out.chargingState = static_cast<core::ChargingState>(decodeChargingState(csRaw));
    out.hasChargingState = true;
  }

  if (hasErrorCode(map)) {
    uint16_t errRaw = raw[map.errorCodeOffset];
    size_t errLen = decodeErrorCode(errRaw, out.bmsErrorCode, sizeof(out.bmsErrorCode));
    out.hasBmsError = (errLen > 0);
  }

  s_pollOk++;
  Serial.printf("[modbus] unitId=%u OK V=%.2f I=%.2f T=%.1f SOC=%.1f%%%s\n",
                unitId, out.voltage, out.current, out.temperature, out.socPercent,
                out.hasBmsError ? " ⚠ ERR" : "");
  return true;
}

size_t modbusReadMultiDrop(core::SensorReading* out, size_t maxOut) {
  if (!s_inited || out == nullptr || maxOut == 0) return 0;

  size_t produced = 0;
  for (uint8_t i = 0; i < BMS_UNIT_ID_COUNT; ++i) {
    if (produced >= maxOut) break;
    uint8_t unitId = BMS_UNIT_ID_START + i;
    const char* serial = unitIdToSerial(unitId);
    if (!serial) {
      Serial.printf("[modbus] unitId=%u không có trong battery_mapping — skip\n", unitId);
      continue;
    }
    if (modbusReadOne(unitId, serial, out[produced])) {
      produced++;
    }
    // Spacing giữa các poll để bus settle + không spam (Modbus T3.5).
    delay(20);
  }

  return produced;
}

uint32_t modbusPollOkCount()   { return s_pollOk; }
uint32_t modbusPollFailCount() { return s_pollFail; }

}  // namespace bms
