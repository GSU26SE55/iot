// ==================================================================
// Sprint 1 + Sprint 3 — Mock BMS implementation
// ==================================================================
#include "bms/mock_bms.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config/battery_mapping.h"

namespace bms {

namespace {

static_assert(::config::kBatteryMappingsCount >= kMockMaxBatteries,
              "batteryMappings[] phải có ít nhất kMockMaxBatteries phần tử");

#if defined(MOCK_SCENARIO_OVERHEAT)
  constexpr const char* kScenario = "overheat";
#elif defined(MOCK_SCENARIO_LOW_SOC)
  constexpr const char* kScenario = "low_soc";
#else
  constexpr const char* kScenario = "normal";
#endif

struct BatteryState {
  float    phaseOffsetRad;
  float    tempC;
  float    socPercent;
  uint16_t cycleCount;
  float    sohPercent;          // Sprint 3 — slow degradation từ 100%
};

BatteryState s_state[kMockMaxBatteries];
uint32_t     s_tickCount = 0;

float randFloat(float lo, float hi) {
  return lo + (hi - lo) * (static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX));
}

void copyStr(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

// Tính (voltage, current, temperature, socPercent) cho 1 pin tại tick hiện tại.
// Tách helper để mockGenerate (Sprint 1) + mockGenerateMultiSource (Sprint 3) reuse.
struct PhysicalSample {
  float voltage;
  float current;
  float temperature;
  float socPercent;
};

PhysicalSample sampleBmsForBattery(size_t i, float phaseBase) {
  BatteryState& st = s_state[i];

  // ---- Voltage: sine 12.0-13.0V + noise ±20mV (S1-FW-04 spec) ----
  const float vBase  = 12.5f + 0.48f * sinf(phaseBase + st.phaseOffsetRad);
  const float voltage = vBase + randFloat(-0.02f, 0.02f);

  // ---- Current: discharge ngẫu nhiên ----
  const float current = randFloat(-5.0f, 2.0f);

  // ---- Temperature ----
#if defined(MOCK_SCENARIO_OVERHEAT)
  st.tempC += 0.4f;
  if (st.tempC > 75.0f) st.tempC = 75.0f;
  const float temperature = st.tempC + randFloat(-0.3f, 0.3f);
#elif defined(MOCK_SCENARIO_LOW_SOC)
  const float temperature = 26.0f + 2.0f * sinf(phaseBase) + randFloat(-0.5f, 0.5f);
#else
  const float temperature = 27.0f + 1.5f * sinf(phaseBase + st.phaseOffsetRad)
                          + randFloat(-0.3f, 0.3f);
#endif

  // ---- SOC ----
#if defined(MOCK_SCENARIO_LOW_SOC)
  st.socPercent -= 1.0f;
  if (st.socPercent < 5.0f) st.socPercent = 5.0f;
  const float socPercent = st.socPercent + randFloat(-0.2f, 0.2f);
#elif defined(MOCK_SCENARIO_OVERHEAT)
  const float socPercent = 75.0f + randFloat(-1.5f, 1.5f);
#else
  st.socPercent += randFloat(-0.5f, 0.5f);
  if (st.socPercent < 70.0f) st.socPercent = 70.0f;
  if (st.socPercent > 92.0f) st.socPercent = 92.0f;
  const float socPercent = st.socPercent;
#endif

  return {voltage, current, temperature, socPercent};
}

// Fill base/legacy fields (Sprint 1 compat) + zero-init Sprint 3 fields.
void fillBaseFields(core::SensorReading& r, size_t i, const PhysicalSample& s) {
  const auto& mapping = ::config::kBatteryMappings[i];
  copyStr(r.batteryAssetId, sizeof(r.batteryAssetId), mapping.batteryAssetId);
  copyStr(r.serial,         sizeof(r.serial),         mapping.serial);
  r.voltage     = s.voltage;
  r.current     = s.current;
  r.temperature = s.temperature;
  r.socPercent  = s.socPercent;
  r.cycleCount  = s_state[i].cycleCount;
  // Sprint 3 default: clear flags + empty strings (mockGenerate khỏi gửi production fields).
  r.sensorSourceCode[0] = '\0';
  r.sourceType          = core::SourceType::IotGateway;
  r.chargingState       = core::ChargingState::Idle;
  r.sohPercent          = 0.0f;
  r.bmsErrorCode[0]     = '\0';
  r.hasSoh              = false;
  r.hasChargingState    = false;
  r.hasBmsError         = false;
}

core::ChargingState inferChargingState(float current, float soc) {
  // current convention: + = charging, − = discharging
  if (soc >= 99.5f && fabsf(current) < 0.5f) return core::ChargingState::Float;
  if (current >  0.3f) return core::ChargingState::Charging;
  if (current < -0.3f) return core::ChargingState::Discharging;
  return core::ChargingState::Idle;
}

}  // namespace

void mockBegin() {
  for (size_t i = 0; i < kMockMaxBatteries; ++i) {
    s_state[i].phaseOffsetRad = (PI / 4.0f) * static_cast<float>(i);
    s_state[i].cycleCount     = ::config::kBatteryMappings[i].initialCycleCount;
    s_state[i].sohPercent     = 95.0f + randFloat(-2.0f, 2.0f);   // start 93-97%

#if defined(MOCK_SCENARIO_OVERHEAT)
    s_state[i].tempC      = 30.0f + randFloat(-1.0f, 1.0f);
    s_state[i].socPercent = 75.0f + randFloat(-5.0f, 5.0f);
#elif defined(MOCK_SCENARIO_LOW_SOC)
    s_state[i].tempC      = 26.0f + randFloat(-2.0f, 2.0f);
    s_state[i].socPercent = 60.0f + randFloat(-2.0f, 2.0f);
#else
    s_state[i].tempC      = 26.0f + randFloat(-2.0f, 2.0f);
    s_state[i].socPercent = 80.0f + randFloat(-5.0f, 5.0f);
#endif
  }

  Serial.printf("[mock] initialized count=%u scenario=%s sources_per_battery=%u\n",
                static_cast<unsigned>(kMockMaxBatteries),
                kScenario,
                static_cast<unsigned>(kSourcesPerBattery));
}

size_t mockGenerate(core::SensorReading* out, size_t count) {
  if (out == nullptr) return 0;
  if (count > kMockMaxBatteries) count = kMockMaxBatteries;

  s_tickCount++;
  const float phaseBase = (TWO_PI * static_cast<float>(s_tickCount % 60)) / 60.0f;

  for (size_t i = 0; i < count; ++i) {
    PhysicalSample s = sampleBmsForBattery(i, phaseBase);
    fillBaseFields(out[i], i, s);
  }
  return count;
}

size_t mockGenerateMultiSource(core::SensorReading* out, size_t batteryCount) {
  if (out == nullptr) return 0;
  if (batteryCount > kMockMaxBatteries) batteryCount = kMockMaxBatteries;

  s_tickCount++;
  const float phaseBase = (TWO_PI * static_cast<float>(s_tickCount % 60)) / 60.0f;

  size_t outIdx = 0;
  for (size_t i = 0; i < batteryCount; ++i) {
    PhysicalSample bmsSample = sampleBmsForBattery(i, phaseBase);

    // ---- Primary BMS reading (Sprint 3: sourceType=Bms, sensorSourceCode="primary") ----
    {
      core::SensorReading& r = out[outIdx++];
      fillBaseFields(r, i, bmsSample);
      copyStr(r.sensorSourceCode, sizeof(r.sensorSourceCode), "primary");
      r.sourceType    = core::SourceType::Bms;
      r.chargingState = inferChargingState(bmsSample.current, bmsSample.socPercent);
      r.hasChargingState = true;
      // SOH slow degradation: -0.0001% per tick (cosmetic)
      s_state[i].sohPercent -= 0.0001f;
      if (s_state[i].sohPercent < 50.0f) s_state[i].sohPercent = 50.0f;
      r.sohPercent = s_state[i].sohPercent;
      r.hasSoh     = true;
      // bmsErrorCode: trigger only trên scenario overheat (BMS protect)
#if defined(MOCK_SCENARIO_OVERHEAT)
      if (bmsSample.temperature > 60.0f) {
        copyStr(r.bmsErrorCode, sizeof(r.bmsErrorCode), "OVT-PROTECT");
        r.hasBmsError = true;
      }
#endif
    }

    // ---- Redundant INA226 reading (Sprint 3: sourceType=IotGateway, sensorSourceCode="redundant") ----
    // INA226 chip đo voltage + current độc lập BMS — drift ±0.02V/±0.05A.
    // Temperature copy từ BMS (INA226 không đo nhiệt — sẽ thay bằng giá trị BMS).
    {
      core::SensorReading& r = out[outIdx++];
      PhysicalSample inaSample;
      inaSample.voltage     = bmsSample.voltage + randFloat(-0.02f, 0.02f);
      inaSample.current     = bmsSample.current + randFloat(-0.05f, 0.05f);
      inaSample.temperature = bmsSample.temperature;   // INA226 không đo temp — mirror BMS
      inaSample.socPercent  = bmsSample.socPercent;    // required field — mirror BMS

      fillBaseFields(r, i, inaSample);
      copyStr(r.sensorSourceCode, sizeof(r.sensorSourceCode), "redundant");
      r.sourceType       = core::SourceType::IotGateway;
      // KHÔNG set chargingState/sohPercent/bmsErrorCode — sensor ngoài không biết.
      r.hasSoh           = false;
      r.hasChargingState = false;
      r.hasBmsError      = false;
    }

    // ---- External-temp DS18B20 reading (Sprint 3: sourceType=IotGateway, sensorSourceCode="external-temp") ----
    // DS18B20 chip nhiệt độ riêng (1-Wire) gắn ngoài thân pin → đo CHỈ temperature.
    // Voltage/current không đo — set bằng BMS để qua validation required.
    // Temperature drift ±0.3°C so BMS (DS18B20 chính xác hơn BMS internal temp).
    {
      core::SensorReading& r = out[outIdx++];
      PhysicalSample dsSample;
      dsSample.voltage     = bmsSample.voltage;        // required — mirror BMS
      dsSample.current     = bmsSample.current;        // required — mirror BMS
      dsSample.temperature = bmsSample.temperature + randFloat(-0.3f, 0.3f);
      dsSample.socPercent  = bmsSample.socPercent;     // required — mirror BMS

      fillBaseFields(r, i, dsSample);
      copyStr(r.sensorSourceCode, sizeof(r.sensorSourceCode), "external-temp");
      r.sourceType       = core::SourceType::IotGateway;
      r.hasSoh           = false;
      r.hasChargingState = false;
      r.hasBmsError      = false;
    }
  }
  return outIdx;
}

const char* mockScenarioName() { return kScenario; }

}  // namespace bms
