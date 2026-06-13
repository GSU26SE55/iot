// ==================================================================
// Sprint 1 — S1-FW-04: Mock BMS implementation
//
// Acceptance (tasksprint.md S1-FW-04):
//   - Trả mảng SensorReading đúng struct cho N pin
//   - Voltage sine 12.0–13.0V + noise nhỏ
//   - Switch scenario qua compile flag (overheat / low_soc / normal)
//
// Lưu ý:
//   - GUID + serial hard-coded cho 8 pin mock. Backend dev seed sẵn 4 pin BAT-MOCK-001..004
//     trong Sprint IoT-2 Phase A (#IoT2-02). Nếu seed khác → đổi mảng kMockBatteries.
//   - Scenario state là static — reset chỉ khi reboot.
// ==================================================================
#include "bms/mock_bms.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "config/battery_mapping.h"   // NI §11 A: batteryMappings[]

namespace bms {

namespace {

static_assert(::config::kBatteryMappingsCount >= kMockMaxBatteries,
              "batteryMappings[] phải có ít nhất kMockMaxBatteries phần tử");

// Scenario name (compile-time).
#if defined(MOCK_SCENARIO_OVERHEAT)
  constexpr const char* kScenario = "overheat";
#elif defined(MOCK_SCENARIO_LOW_SOC)
  constexpr const char* kScenario = "low_soc";
#else
  constexpr const char* kScenario = "normal";
#endif

// Per-battery scenario state. Phase lệch nhau để chart 4 pin không trùng dạng sine.
struct BatteryState {
  float phaseOffsetRad;     // lệch sine voltage
  float tempC;              // temp hiện tại (overheat scenario tăng dần)
  float socPercent;         // SOC hiện tại (low_soc scenario giảm dần)
  uint16_t cycleCount;
};

BatteryState s_state[kMockMaxBatteries];

uint32_t s_tickCount = 0;          // đếm số lần generate (dùng làm pha cho sine)

float randFloat(float lo, float hi) {
  return lo + (hi - lo) * (static_cast<float>(esp_random()) / static_cast<float>(UINT32_MAX));
}

}  // namespace

void mockBegin() {
  // Khởi tạo state per-battery với scenario tương ứng.
  for (size_t i = 0; i < kMockMaxBatteries; ++i) {
    s_state[i].phaseOffsetRad = (PI / 4.0f) * static_cast<float>(i);
    s_state[i].cycleCount     = ::config::kBatteryMappings[i].initialCycleCount;

#if defined(MOCK_SCENARIO_OVERHEAT)
    s_state[i].tempC      = 30.0f + randFloat(-1.0f, 1.0f);   // start ~30°C
    s_state[i].socPercent = 75.0f + randFloat(-5.0f, 5.0f);
#elif defined(MOCK_SCENARIO_LOW_SOC)
    s_state[i].tempC      = 26.0f + randFloat(-2.0f, 2.0f);
    s_state[i].socPercent = 60.0f + randFloat(-2.0f, 2.0f);   // start cao, giảm dần
#else
    s_state[i].tempC      = 26.0f + randFloat(-2.0f, 2.0f);
    s_state[i].socPercent = 80.0f + randFloat(-5.0f, 5.0f);
#endif
  }

  Serial.printf("[mock] initialized count=%u scenario=%s\n",
                static_cast<unsigned>(kMockMaxBatteries), kScenario);
}

size_t mockGenerate(core::SensorReading* out, size_t count) {
  if (out == nullptr) return 0;
  if (count > kMockMaxBatteries) count = kMockMaxBatteries;

  s_tickCount++;
  // 1 chu kỳ sine = 60 tick ≈ 5 phút (với INGEST_INTERVAL_MS=5000) — đủ chậm để chart nhìn được.
  const float phaseBase = (TWO_PI * static_cast<float>(s_tickCount % 60)) / 60.0f;

  for (size_t i = 0; i < count; ++i) {
    BatteryState& st = s_state[i];

    // ---- Voltage: sine 12.0–13.0V + noise ±20mV (S1-FW-04 spec) ----
    // Sine amplitude 0.48 + noise 0.02 → range strict [12.00, 13.00] (không vượt biên).
    const float vBase  = 12.5f + 0.48f * sinf(phaseBase + st.phaseOffsetRad);
    const float vNoise = randFloat(-0.02f, 0.02f);
    const float voltage = vBase + vNoise;

    // ---- Current: discharge ngẫu nhiên −5..+2A (âm = đang xả) ----
    const float current = randFloat(-5.0f, 2.0f);

    // ---- Temperature: scenario-dependent ----
#if defined(MOCK_SCENARIO_OVERHEAT)
    // Tăng đều 0.4°C mỗi tick, cap ở 75°C
    st.tempC += 0.4f;
    if (st.tempC > 75.0f) st.tempC = 75.0f;
    const float temperature = st.tempC + randFloat(-0.3f, 0.3f);
#elif defined(MOCK_SCENARIO_LOW_SOC)
    const float temperature = 26.0f + 2.0f * sinf(phaseBase) + randFloat(-0.5f, 0.5f);
#else
    const float temperature = 27.0f + 1.5f * sinf(phaseBase + st.phaseOffsetRad)
                            + randFloat(-0.3f, 0.3f);
#endif

    // ---- SOC: scenario-dependent ----
#if defined(MOCK_SCENARIO_LOW_SOC)
    // Giảm 1%/tick → ~5 phút xuống dưới ngưỡng 30%
    st.socPercent -= 1.0f;
    if (st.socPercent < 5.0f) st.socPercent = 5.0f;
    const float socPercent = st.socPercent + randFloat(-0.2f, 0.2f);
#elif defined(MOCK_SCENARIO_OVERHEAT)
    const float socPercent = 75.0f + randFloat(-1.5f, 1.5f);
#else
    // Random walk ±0.5% mỗi tick quanh 80%.
    st.socPercent += randFloat(-0.5f, 0.5f);
    if (st.socPercent < 70.0f) st.socPercent = 70.0f;
    if (st.socPercent > 92.0f) st.socPercent = 92.0f;
    const float socPercent = st.socPercent;
#endif

    // ---- Fill output ---- (lấy serial + guid từ batteryMappings[] — NI §11 A)
    core::SensorReading& r = out[i];
    const auto& mapping = ::config::kBatteryMappings[i];
    strncpy(r.batteryAssetId, mapping.batteryAssetId, sizeof(r.batteryAssetId) - 1);
    r.batteryAssetId[sizeof(r.batteryAssetId) - 1] = '\0';
    strncpy(r.serial, mapping.serial, sizeof(r.serial) - 1);
    r.serial[sizeof(r.serial) - 1] = '\0';
    r.voltage     = voltage;
    r.current     = current;
    r.temperature = temperature;
    r.socPercent  = socPercent;
    r.cycleCount  = st.cycleCount;
  }

  return count;
}

const char* mockScenarioName() { return kScenario; }

}  // namespace bms
