// ==================================================================
// Sprint 6 — S6-FW-02 (#64): Water leak sensor implementation.
//
// Pipeline: đọc digital GPIO2 → cạnh khô→ướt → IncidentTrigger (edge + cooldown)
// → environmental_incident reporter (HTTPS, type=Flood). Reporter dùng chung MQ-2.
// ==================================================================
#include "sensor/water_leak.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "sensor/environmental_incident.h"
#include "sensor/incident_trigger.h"

#include <Arduino.h>

namespace sensor {

namespace {

bool     s_inited       = false;
bool     s_wet          = false;
bool     s_hasSample    = false;
uint32_t s_lastPollMs   = 0;
uint32_t s_reportCount  = 0;

// Pending-retry: cạnh lên detect nhưng report FAIL (offline/NTP/backend) → giữ để
// retry tick sau, không mất incident.
bool     s_pendingReport = false;

IncidentTrigger s_trigger(WATER_LEAK_REARM_COOLDOWN_MS);

}  // namespace

bool waterLeakBegin() {
#if !WATER_LEAK_ENABLED
  Serial.println("[water] disabled (WATER_LEAK_ENABLED=0) — skip");
  return false;
#else
  // INPUT_PULLUP tránh chân float khi sensor chưa cắm (đọc nhiễu → false trigger).
  pinMode(WATER_LEAK_GPIO, INPUT_PULLUP);
  s_inited      = true;
  s_hasSample   = false;
  s_lastPollMs  = 0;
  Serial.printf("[water] init OK pin=GPIO%d active=%s poll=%lums\n",
                static_cast<int>(WATER_LEAK_GPIO),
                WATER_LEAK_ACTIVE_HIGH ? "HIGH" : "LOW",
                static_cast<unsigned long>(WATER_LEAK_POLL_INTERVAL_MS));
  return true;
#endif
}

void waterLeakTick() {
  if (!s_inited) return;

  const uint32_t now = millis();
  if (now - s_lastPollMs < WATER_LEAK_POLL_INTERVAL_MS) return;
  s_lastPollMs = now;

  const int level = digitalRead(WATER_LEAK_GPIO);
  const bool wet = (level == (WATER_LEAK_ACTIVE_HIGH ? HIGH : LOW));

  // Log ngay mẫu đầu tiên và mỗi lần đổi trạng thái để kiểm tra mạch trực tiếp.
  // Không log mọi 100 ms, tránh làm nghẽn UART khi cảm biến giữ nguyên mức.
  if (!s_hasSample || wet != s_wet) {
    Serial.printf("[water] GPIO%d level=%d state=%s\n",
                  static_cast<int>(WATER_LEAK_GPIO), level, wet ? "WET" : "DRY");
  }
  s_wet = wet;
  s_hasSample = true;

  // Cạnh khô→ướt → cần report (1 lần / cooldown).
  if (s_trigger.update(s_wet, now)) {
    s_pendingReport = true;
    Serial.printf("[water] LEAK detected GPIO%d level=%d → report\n",
                  static_cast<int>(WATER_LEAK_GPIO), level);
  }

  if (s_pendingReport) {
    char notes[64];
    snprintf(notes, sizeof(notes), "water leak GPIO%d", static_cast<int>(WATER_LEAK_GPIO));
    if (envIncidentReport(IncidentType::Flood, IncidentSeverity::Critical, notes)) {
      s_pendingReport = false;
      s_reportCount++;
    }
  }
}

bool waterLeakIsWet() { return s_inited && s_wet; }

uint32_t waterLeakReportCount() { return s_reportCount; }

}  // namespace sensor
