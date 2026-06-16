// ==================================================================
// Sprint 5 — S5-FW-04: INA226 sensor implementation.
//
// Driver: robtillaart/INA226 (platformio.ini lib_deps).
// ==================================================================
#include "sensor/ina226.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/battery_mapping.h"

#include <Arduino.h>
#include <INA226.h>
#include <Wire.h>

#include <cstring>

namespace sensor {

namespace {

INA226   s_ina226(INA226_I2C_ADDRESS);
bool     s_inited       = false;
uint32_t s_readOk       = 0;
uint32_t s_readFail     = 0;

}  // namespace

bool ina226Begin() {
  if (s_inited) return true;

  // Init Wire bus — idempotent if SHT31 also calls Wire.begin().
  // ESP32-S3 Wire.begin(sda, scl, frequency).
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY_HZ);

  if (!s_ina226.begin()) {
    Serial.printf("[ina226] init FAIL — không respond tại 0x%02X (check wiring/I2C)\n",
                  INA226_I2C_ADDRESS);
    return false;
  }

  // Config shunt + max current → INA226 tự tính LSB.
  // setMaxCurrentShunt(maxCurrent, shuntResistance).
  int err = s_ina226.setMaxCurrentShunt(INA226_MAX_CURRENT_A, INA226_SHUNT_OHM);
  if (err != INA226_ERR_NONE) {
    Serial.printf("[ina226] setMaxCurrentShunt FAIL err=%d (max %.1fA, shunt %.3fΩ)\n",
                  err, INA226_MAX_CURRENT_A, INA226_SHUNT_OHM);
    return false;
  }

  s_inited = true;
  Serial.printf("[ina226] init OK addr=0x%02X shunt=%.3fΩ max=%.1fA\n",
                INA226_I2C_ADDRESS, INA226_SHUNT_OHM, INA226_MAX_CURRENT_A);
  return true;
}

bool ina226ReadOnce(float* voltageOut, float* currentOut) {
  if (!s_inited) return false;

  float v = s_ina226.getBusVoltage();        // V
  float i = s_ina226.getCurrent();           // A (signed)

  // INA226 không có explicit error code per-read — check via reasonable range.
  // Bus voltage 0V có thể là pin dead OR I2C lỗi. Để defensive, accept range
  // [-50, 50]V và [-INA226_MAX_CURRENT_A * 1.2, +max*1.2]A.
  if (v < -50.0f || v > 50.0f) {
    Serial.printf("[ina226] read voltage OUT OF RANGE = %.2fV (I2C lỗi?)\n", v);
    s_readFail++;
    return false;
  }
  float maxI = INA226_MAX_CURRENT_A * 1.2f;
  if (i < -maxI || i > maxI) {
    Serial.printf("[ina226] read current OUT OF RANGE = %.2fA (max ±%.1fA)\n", i, maxI);
    s_readFail++;
    return false;
  }

  if (voltageOut) *voltageOut = v;
  if (currentOut) *currentOut = i;
  s_readOk++;
  return true;
}

size_t ina226BuildRedundantReadings(const char* const* serials, size_t n,
                                    core::SensorReading* out, size_t maxOut) {
  if (!s_inited || serials == nullptr || out == nullptr) return 0;
  if (n == 0 || maxOut == 0) return 0;

  float v = 0.0f, i = 0.0f;
  if (!ina226ReadOnce(&v, &i)) return 0;

  // Replicate cùng V/I cho mỗi battery serial.
  // ⚠ Đây là approximation Sprint 5: 1 INA226 đo bus chung → cross-source
  // validation chỉ catch lỗi BMS aggregate level. Sprint 6+ mở rộng hardware
  // 1 INA226/battery cho per-cell mismatch detection.
  size_t produced = 0;
  for (size_t k = 0; k < n && produced < maxOut; ++k) {
    if (serials[k] == nullptr || serials[k][0] == '\0') continue;

    core::SensorReading& r = out[produced];
    memset(&r, 0, sizeof(r));

    strncpy(r.serial, serials[k], sizeof(r.serial) - 1);

    // Lookup batteryAssetId từ mapping (fallback nếu backend chưa seed serial).
    for (const auto& m : config::kBatteryMappings) {
      if (strcmp(m.serial, r.serial) == 0) {
        strncpy(r.batteryAssetId, m.batteryAssetId, sizeof(r.batteryAssetId) - 1);
        break;
      }
    }

    r.voltage     = v;
    r.current     = i;
    // INA226 KHÔNG đo temp — mirror BMS temp (mock 25°C) hoặc để DS18B20 cover.
    // Để cross-source validation chỉ so V/I, temp set 0 + sensorSourceCode lookup
    // sẽ skip temp comparison ở backend §52.6.
    r.temperature = 0.0f;
    r.socPercent  = 0.0f;   // INA226 không trả SOC

    r.sourceType = core::SourceType::IotGateway;
    strncpy(r.sensorSourceCode, "redundant", sizeof(r.sensorSourceCode) - 1);

    // Optional fields KHÔNG có (sensor ngoài không biết SOH/charging/error).
    r.hasSoh = false;
    r.hasChargingState = false;
    r.hasBmsError = false;

    produced++;
  }
  return produced;
}

uint32_t ina226ReadOkCount()   { return s_readOk; }
uint32_t ina226ReadFailCount() { return s_readFail; }

}  // namespace sensor
