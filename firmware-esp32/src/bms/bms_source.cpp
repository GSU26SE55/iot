// ==================================================================
// Sprint 5 — S5-FW-07: bms_source dispatcher implementation.
// ==================================================================
#include "bms/bms_source.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "bms/mock_bms.h"
#include "config/battery_mapping.h"

#include <Arduino.h>
#include <cstring>

#if !USE_MOCK_BMS
#include "bms/modbus_bms.h"
#include "sensor/ina226.h"
#include "sensor/ds18b20.h"
#include "sensor/sht31.h"
#endif

namespace bms {

bool bmsSourceBegin() {
#if USE_MOCK_BMS
  mockBegin();
  Serial.println("[bms-source] MOCK mode — Sprint 1 mock_bms (no hardware)");
  return true;
#else
  Serial.println("[bms-source] REAL mode — Modbus + INA226 + DS18B20 (SHT31 disabled)");

  bool ok = true;
  if (!modbusBegin()) {
    Serial.println("[bms-source] modbus init FAIL — BMS readings KHÔNG khả dụng");
    ok = false;
  }
  if (!sensor::ina226Begin()) {
    Serial.println("[bms-source] ina226 init FAIL — redundant readings SKIP");
    // Non-critical — vẫn poll BMS được, chỉ thiếu cross-source pair
  }
  size_t dsCount = sensor::ds18b20Begin();
  if (dsCount == 0) {
    Serial.println("[bms-source] ds18b20 detect 0 sensor — external-temp SKIP");
    // Non-critical
  }
#if SHT31_ENABLED
  if (!sensor::sht31Begin()) {
    Serial.println("[bms-source] sht31 init FAIL — ambient readings SKIP");
    // Non-critical (ambient endpoint khác — không ảnh hưởng telemetry chính)
  }
#else
  Serial.println("[bms-source] sht31 disabled by firmware config");
#endif
  return ok;
#endif
}

size_t bmsSourcePollAll(core::SensorReading* out, size_t batteryCount) {
  if (out == nullptr || batteryCount == 0) return 0;

#if USE_MOCK_BMS
  // Mock đã sinh đúng layout 3 sources/battery interleaved.
  const size_t nBat = (batteryCount > kMockMaxBatteries) ? kMockMaxBatteries : batteryCount;
  return mockGenerateMultiSource(out, nBat);

#else
  // Real path: merge từ 3 source khác nhau theo battery serial.
  const size_t maxSlots = batteryCount * kSourcesPerBattery;

  // Buffer trung gian per-source — tránh interleave-while-poll phức tạp.
  core::SensorReading bmsReadings[BMS_UNIT_ID_COUNT];
  core::SensorReading inaReadings[BMS_UNIT_ID_COUNT];
  core::SensorReading dsReadings[BMS_UNIT_ID_COUNT];

  // ---- Step 1: BMS multi-drop ----
  size_t nBms = modbusReadMultiDrop(bmsReadings, BMS_UNIT_ID_COUNT);

  // ---- Step 2: INA226 redundant (replicate cho N battery) ----
  // Build array serial từ BMS results (chỉ pin có BMS reading mới có pair).
  const char* serials[BMS_UNIT_ID_COUNT];
  for (size_t i = 0; i < nBms; ++i) {
    serials[i] = bmsReadings[i].serial;
  }
  size_t nIna = sensor::ina226BuildRedundantReadings(
      serials, nBms, inaReadings, BMS_UNIT_ID_COUNT);

  // ---- Step 3: DS18B20 external-temp ----
  size_t nDs = sensor::ds18b20BuildExternalTempReadings(
      serials, nBms, dsReadings, BMS_UNIT_ID_COUNT);

  // ---- Step 4: Merge interleaved theo battery ----
  // Layout: [b0.bms, b0.ina, b0.ds, b1.bms, b1.ina, b1.ds, ...]
  // Slot KHÔNG dùng (vd INA226 fail) sẽ skip — total < batteryCount * 3.
  size_t produced = 0;
  for (size_t b = 0; b < nBms && produced < maxSlots; ++b) {
    // BMS primary (luôn có)
    out[produced++] = bmsReadings[b];

    // INA226 redundant — chỉ thêm nếu có (match serial)
    if (b < nIna && produced < maxSlots) {
      // Verify match serial (defensive — order should match).
      if (strcmp(inaReadings[b].serial, bmsReadings[b].serial) == 0) {
        out[produced++] = inaReadings[b];
      }
    }

    // DS18B20 external-temp — same
    if (b < nDs && produced < maxSlots) {
      if (strcmp(dsReadings[b].serial, bmsReadings[b].serial) == 0) {
        out[produced++] = dsReadings[b];
      }
    }
  }

  return produced;
#endif
}

const char* bmsSourceMode() {
#if USE_MOCK_BMS
  return "MOCK";
#else
  return "REAL";
#endif
}

}  // namespace bms
