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

#include "config/battery_map_runtime.h"
#include "core/source_tags.h"   // S6-FW-03 (#65): canonical cross-source tags

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
  // setMaxCurrentShunt(maxCurrent, shuntResistance, normalize).
  //
  // IOT3-07 — normalize=false BẮT BUỘC với maxCurrent = 200 A.
  //
  // Tham số thứ ba mặc định `true`, và khối normalize của thư viện làm tròn
  // current_LSB về dạng 1/2/5 × 10ⁿ µA bằng một vòng lặp chỉ chạy `i < 4`
  // ⇒ giá trị lớn nhất nó tạo được là 5 000 µA. Truy ngược:
  //     current_LSB = maxCurrent / 32768
  //     cần current_LSB × 1e6 + 1 ≤ 5000  ⇒  maxCurrent ≤ 163,8 A
  // Với 200 A thì LSB = 6 103,5 µA, vượt trần ⇒ vòng lặp thoát với result=false
  // ⇒ trả INA226_ERR_NORMALIZE_FAILED (0x8003) và INA226 không bao giờ init.
  //
  // Bỏ normalize thì LSB = 200/32768 = 6,104 mA (không tròn nhưng dùng tốt),
  // calib = round(0,00512 / (6,104e-3 × 3,75e-4)) = 2 237 ≤ 32 767 (không phải
  // tự chia đôi), _maxCurrent = 200,0 A. Không nhánh nào trả lỗi.
  int err = s_ina226.setMaxCurrentShunt(INA226_MAX_CURRENT_A, INA226_SHUNT_OHM, false);
  if (err != INA226_ERR_NONE) {
    // IOT3-08 — %.6f cho shunt: với 0,000375 Ω thì %.3f in ra "0.000",
    // nhìn hệt như chưa cấu hình gì và làm người đọc log đi sai hướng.
    Serial.printf("[ina226] setMaxCurrentShunt FAIL err=0x%04X (max %.1fA, shunt %.6fΩ)\n",
                  err, INA226_MAX_CURRENT_A, INA226_SHUNT_OHM);
    Serial.println("[ina226]   0x8000 SHUNTVOLTAGE_HIGH  — maxCurrent × shunt > 81,90 mV");
    Serial.println("[ina226]   0x8002 SHUNT_LOW          — thiếu -DINA226_MINIMAL_SHUNT_OHM=0.0001");
    Serial.println("[ina226]   0x8003 NORMALIZE_FAILED   — thiếu tham số normalize=false");
    return false;
  }

  s_inited = true;
  Serial.printf("[ina226] init OK addr=0x%02X shunt=%.6fΩ max=%.1fA\n",
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
    // IOT3-49 — qua bảng runtime (tự lui về bảng cứng khi chưa provision).
    strncpy(r.batteryAssetId, batmap::assetIdForSerial(r.serial),
            sizeof(r.batteryAssetId) - 1);

    r.voltage     = v;
    r.current     = i;
    // INA226 KHÔNG đo temp — mirror BMS temp (mock 25°C) hoặc để DS18B20 cover.
    // Để cross-source validation chỉ so V/I, temp set 0 + sensorSourceCode lookup
    // sẽ skip temp comparison ở backend §52.6.
    r.temperature = 0.0f;
    r.socPercent  = 0.0f;   // INA226 không trả SOC

    // S6-FW-03 (#65): canonical constant — IotGateway + "redundant" để cross-source
    // pair với BMS primary cùng battery (SensorMismatch detection).
    r.sourceType = core::kSourceTypeRedundant;
    strncpy(r.sensorSourceCode, core::kSourceCodeRedundant, sizeof(r.sensorSourceCode) - 1);

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
