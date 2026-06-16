// ==================================================================
// Sprint 5 — S5-FW-05: DS18B20 1-Wire implementation.
// Driver: paulstoffregen/OneWire + milesburton/DallasTemperature.
// ==================================================================
#include "sensor/ds18b20.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/battery_mapping.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#include <cstring>

namespace sensor {

namespace {

OneWire           s_oneWire(DS18B20_GPIO);
DallasTemperature s_sensors(&s_oneWire);
DeviceAddress     s_addresses[DS18B20_MAX_SENSORS];
size_t            s_detected   = 0;
bool              s_inited     = false;
uint32_t          s_readOk     = 0;
uint32_t          s_readFail   = 0;

}  // namespace

size_t ds18b20Begin() {
  if (s_inited) return s_detected;

  s_sensors.begin();
  s_sensors.setResolution(DS18B20_RESOLUTION);
  // Non-blocking mode: trigger conversion + đọc sau khi đợi.
  // Sprint 5 dùng blocking đơn giản — requestTemperatures() block 750ms cho 12-bit.
  s_sensors.setWaitForConversion(true);

  // Enumerate addresses.
  size_t count = s_sensors.getDeviceCount();
  if (count > DS18B20_MAX_SENSORS) count = DS18B20_MAX_SENSORS;
  for (size_t i = 0; i < count; ++i) {
    if (s_sensors.getAddress(s_addresses[i], i)) {
      s_detected++;
    }
  }

  Serial.printf("[ds18b20] init OK — gpio=%d resolution=%dbit detected=%u sensors\n",
                static_cast<int>(DS18B20_GPIO),
                static_cast<int>(DS18B20_RESOLUTION),
                static_cast<unsigned>(s_detected));
  if (s_detected == 0) {
    Serial.println("[ds18b20] ⚠ Không detect sensor — check 4.7kΩ pull-up + wiring");
  }

  s_inited = true;
  return s_detected;
}

float ds18b20ReadByIndex(size_t index) {
  if (!s_inited || index >= s_detected) return DEVICE_DISCONNECTED_C;
  s_sensors.requestTemperatures();
  float t = s_sensors.getTempC(s_addresses[index]);
  if (t == DEVICE_DISCONNECTED_C) {
    s_readFail++;
    return t;
  }
  s_readOk++;
  return t;
}

size_t ds18b20BuildExternalTempReadings(const char* const* serials, size_t n,
                                        core::SensorReading* out, size_t maxOut) {
  if (!s_inited || serials == nullptr || out == nullptr) return 0;
  if (s_detected == 0) return 0;

  // Trigger conversion 1 lần cho tất cả sensors (tiết kiệm 750ms × N).
  s_sensors.requestTemperatures();

  size_t produced = 0;
  size_t limit = (n < s_detected) ? n : s_detected;
  for (size_t i = 0; i < limit && produced < maxOut; ++i) {
    if (serials[i] == nullptr || serials[i][0] == '\0') continue;

    float t = s_sensors.getTempC(s_addresses[i]);
    if (t == DEVICE_DISCONNECTED_C) {
      Serial.printf("[ds18b20] sensor #%u disconnect — skip battery %s\n",
                    static_cast<unsigned>(i), serials[i]);
      s_readFail++;
      continue;
    }
    s_readOk++;

    core::SensorReading& r = out[produced];
    memset(&r, 0, sizeof(r));

    strncpy(r.serial, serials[i], sizeof(r.serial) - 1);
    for (const auto& m : config::kBatteryMappings) {
      if (strcmp(m.serial, r.serial) == 0) {
        strncpy(r.batteryAssetId, m.batteryAssetId, sizeof(r.batteryAssetId) - 1);
        break;
      }
    }

    r.voltage     = 0.0f;   // DS18B20 không đo điện
    r.current     = 0.0f;
    r.temperature = t;
    r.socPercent  = 0.0f;

    r.sourceType = core::SourceType::IotGateway;
    strncpy(r.sensorSourceCode, "external-temp", sizeof(r.sensorSourceCode) - 1);

    r.hasSoh = false;
    r.hasChargingState = false;
    r.hasBmsError = false;

    produced++;
  }
  return produced;
}

uint32_t ds18b20ReadOkCount()   { return s_readOk; }
uint32_t ds18b20ReadFailCount() { return s_readFail; }

}  // namespace sensor
