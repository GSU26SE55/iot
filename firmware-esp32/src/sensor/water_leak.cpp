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
#include "config/device_identity.h"
#include "core/elapsed.h"
#include "core/retry_gate.h"
#include "net/backoff.h"

#include <Arduino.h>

#include <cstring>

namespace sensor {

namespace {

bool     s_inited       = false;
bool     s_wet          = false;
bool     s_wetLatched   = false;
bool     s_hasSample    = false;
uint32_t s_lastPollMs   = 0;
uint32_t s_reportCount  = 0;


// Pending-retry: cạnh lên detect nhưng report FAIL (offline/NTP/backend) → giữ để
// retry tick sau, không mất incident.
bool     s_pendingReport = false;
// GH-736 — mốc millis() lúc PHÁT HIỆN, để báo đúng thời điểm dù gửi được muộn hơn.
uint32_t s_pendingDetectedMs = 0;
// GH-741 — backoff cho lỗi TẠM THỜI. Trước đây retry mỗi tick nên một lỗi 403
// sinh ra bão request vô hạn (MQ-2 1s, rò nước 0,5s).
net::Backoff s_reportBackoff;
uint32_t     s_nextReportAtMs = 0;

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
  s_wet         = false;
  s_wetLatched  = false;
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
  const bool rawWet = (level == (WATER_LEAK_ACTIVE_HIGH ? HIGH : LOW));

  // Debounce: cần duy trì liên tục mức WET ít nhất 3 mẫu (300ms) để loại bỏ nhiễu điện/rung dây.
  // Đảm bảo chỉ khi đèn LED trên module sáng rõ (nước thật) thì mới kích hoạt WET.
  static uint8_t s_wetStreak = 0;
  if (rawWet) {
    if (s_wetStreak < 255) s_wetStreak++;
  } else {
    s_wetStreak = 0;
  }
  const bool wet = (s_wetStreak >= 3);

  // Log ngay mẫu đầu tiên và mỗi lần đổi trạng thái để kiểm tra mạch trực tiếp.
  // Không log mọi 100 ms, tránh làm nghẽn UART khi cảm biến giữ nguyên mức.
  if (!s_hasSample || wet != s_wet) {
    Serial.printf("[water] GPIO%d level=%d state=%s\n",
                  static_cast<int>(WATER_LEAK_GPIO), level, wet ? "WET" : "DRY");
  }
  s_wet = wet;
  if (wet) {
    s_wetLatched = true;
  }
  s_hasSample = true;

  // Cạnh khô→ướt → cần report (1 lần / cooldown).
  if (s_trigger.update(s_wet, now)) {
    s_pendingReport = true;
    s_pendingDetectedMs = now;
    // GH-741 — sự cố MỚI phải được báo NGAY, không chờ hết backoff của lần trước.
    // Đây là cảm biến an toàn: bắt một xung khí mới đợi 5 phút vì lần trước backend
    // lỗi là biến sự cố mạng thành sự cố an toàn.
    s_reportBackoff.reset();
    s_nextReportAtMs = now;
    Serial.printf("[water] LEAK detected GPIO%d level=%d → report\n",
                  static_cast<int>(WATER_LEAK_GPIO), level);
  }

  // GH-741 — lỗi tạm thời phải đợi hết backoff mới thử lại.
  if (core::shouldAttemptReport(s_pendingReport, now, s_nextReportAtMs)) {
    char notes[64];
    snprintf(notes, sizeof(notes), "water leak GPIO%d", static_cast<int>(WATER_LEAK_GPIO));
    const auto result = envIncidentReport(IncidentType::Flood, IncidentSeverity::Critical, notes,
                          core::elapsedSeconds(s_pendingDetectedMs, now));
    if (result == IncidentReportResult::Success) {
      s_pendingReport = false;
      s_reportCount++;
      s_reportBackoff.reset();
    } else if (result == IncidentReportResult::Permanent) {
      // Gửi lại vẫn hỏng (sai scope, chưa provision, payload sai) ⇒ DỪNG.
      // Sự cố vẫn được ghi nhận cục bộ qua log + envIncidentDroppedCount().
      Serial.println("[water] BỎ report (lỗi vĩnh viễn) — xem log env-incident");
      s_pendingReport = false;
      s_reportBackoff.reset();
      s_nextReportAtMs = now;
    } else {
      const uint32_t waitMs = s_reportBackoff.recordFailure();
      s_nextReportAtMs = now + waitMs;
      Serial.printf("[water] report lỗi tạm thời → thử lại sau %lums\n",
                    static_cast<unsigned long>(waitMs));
    }
  }
}

bool waterLeakIsWet() {
  if (!s_inited) return false;
  const bool wasWet = s_wet || s_wetLatched;
  s_wetLatched = false;
  return wasWet;
}

uint32_t waterLeakReportCount() { return s_reportCount; }

bool waterLeakHasSample() { return s_inited && s_hasSample; }

}  // namespace sensor
