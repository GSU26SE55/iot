// ==================================================================
// Sprint 6 — S6-FW-01 (#63): MQ-2 smoke/gas sensor implementation.
//
// Pipeline: warm-up → đọc ADC GPIO1 → so threshold → IncidentTrigger (edge +
// cooldown) → environmental_incident reporter (HTTPS, type=Smoke).
//
// Xem mq2.h cho hardware note + chống spam. Reporter dùng chung với water_leak.
// ==================================================================
#include "sensor/mq2.h"

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

bool     s_inited        = false;
uint32_t s_warmupStartMs = 0;
uint32_t s_lastPollMs    = 0;
uint32_t s_lastRaw       = 0;
uint32_t s_reportCount   = 0;

// Khi cạnh lên detect nhưng report FAIL (offline / NTP chưa sync / backend 5xx) →
// giữ pending để retry tick sau, tránh mất incident vì lỗi tạm thời.
bool     s_pendingReport = false;
int      s_pendingRaw    = 0;

// Cooldown 5 phút giữa 2 report (config). prevActive bắt đầu false → mẫu active
// đầu tiên sau warm-up = cạnh lên → fire.
IncidentTrigger s_trigger(MQ2_REARM_COOLDOWN_MS);

bool inWarmup(uint32_t now) {
  return (now - s_warmupStartMs) < MQ2_WARMUP_MS;
}

}  // namespace

bool mq2Begin() {
#if !MQ2_ENABLED
  Serial.println("[mq2] disabled (MQ2_ENABLED=0) — skip");
  return false;
#else
  // ESP32 Arduino mặc định 12-bit ADC (0-4095). Set explicit để khớp threshold doc.
  analogReadResolution(12);
  // Full-scale attenuation → đọc được dải ~0-3.3V trên chân ADC.
  analogSetPinAttenuation(MQ2_ADC_PIN, ADC_11db);

  s_warmupStartMs = millis();
  s_lastPollMs    = 0;
  s_inited        = true;
  Serial.printf("[mq2] init OK pin=GPIO%d threshold=%d warmup=%lums poll=%lums\n",
                static_cast<int>(MQ2_ADC_PIN),
                static_cast<int>(MQ2_THRESHOLD_RAW),
                static_cast<unsigned long>(MQ2_WARMUP_MS),
                static_cast<unsigned long>(MQ2_POLL_INTERVAL_MS));
  return true;
#endif
}

int mq2ReadRaw() {
  if (!s_inited) return -1;
  return analogRead(MQ2_ADC_PIN);
}

void mq2Tick() {
  if (!s_inited) return;

  const uint32_t now = millis();
  if (now - s_lastPollMs < MQ2_POLL_INTERVAL_MS) return;
  s_lastPollMs = now;

  const int raw = analogRead(MQ2_ADC_PIN);
  s_lastRaw = static_cast<uint32_t>(raw);

  // Warm-up: cảm biến chưa ổn định → chỉ đọc/log, KHÔNG đánh giá incident.
  if (inWarmup(now)) {
    Serial.printf("[mq2] warm-up... raw=%d (%lus còn lại)\n",
                  raw,
                  static_cast<unsigned long>((MQ2_WARMUP_MS - (now - s_warmupStartMs)) / 1000));
    return;
  }

  // Cạnh lên (bình thường→vượt ngưỡng) → cần report (1 lần / cooldown).
  const bool active = raw > static_cast<int>(MQ2_THRESHOLD_RAW);
  if (s_trigger.update(active, now)) {
    s_pendingReport = true;
    s_pendingRaw    = raw;
    Serial.printf("[mq2] SMOKE detected raw=%d > thr=%d → report\n",
                  raw, static_cast<int>(MQ2_THRESHOLD_RAW));
  }

  // Report (hoặc retry nếu pending). Reporter tự skip + trả false nếu siteId/NTP
  // chưa sẵn → giữ pending cho tick sau.
  if (s_pendingReport) {
    char notes[64];
    snprintf(notes, sizeof(notes), "MQ-2 raw=%d > thr=%d (GPIO%d)",
             s_pendingRaw, static_cast<int>(MQ2_THRESHOLD_RAW),
             static_cast<int>(MQ2_ADC_PIN));
    if (envIncidentReport(IncidentType::Smoke, IncidentSeverity::Critical, notes)) {
      s_pendingReport = false;
      s_reportCount++;
    }
  }
}

uint32_t mq2ReportCount() { return s_reportCount; }
uint32_t mq2LastRaw()     { return s_lastRaw; }

}  // namespace sensor
