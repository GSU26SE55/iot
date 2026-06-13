// ==================================================================
// Sprint 1 — S1-FW-03: NTP time sync implementation
//
// Acceptance (tasksprint.md S1-FW-03):
//   - isoNow() trả đúng RFC3339 UTC (vd "2026-06-13T08:15:42Z")
//   - Lệch giờ thực ≤ 1s
//
// Thiết kế:
//   - configTime() pool 3 server (vn → asia → global) để failover khi pool VN down.
//   - Coi là synced khi getLocalTime() trả về year ≥ 2024 (đảm bảo không phải epoch 1970).
//   - Tick re-request configTime() nếu chưa sync — bẫy NTP §12 #1.
// ==================================================================
#include "net/time_sync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace net {

namespace {
constexpr uint32_t kRetryThrottleMs = 5000;
constexpr int      kMinYear         = 2024;

bool     s_synced            = false;
uint32_t s_lastRetryMs       = 0;
uint32_t s_firstSyncEpoch    = 0;

void requestSync() {
  configTime(NTP_GMT_OFFSET_SEC,
             NTP_DAYLIGHT_OFFSET_SEC,
             NTP_SERVER_1,
             NTP_SERVER_2,
             NTP_SERVER_3);
}

bool checkSynced() {
  struct tm tm;
  if (!getLocalTime(&tm, 100 /*ms*/)) return false;
  return (tm.tm_year + 1900) >= kMinYear;
}
}  // namespace

void timeSyncBegin() {
  requestSync();
  Serial.printf("[ntp] sync requested servers=%s,%s,%s\n",
                NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
}

void timeSyncTick() {
  if (s_synced) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - s_lastRetryMs < kRetryThrottleMs) return;
  s_lastRetryMs = now;

  if (checkSynced()) {
    s_synced = true;
    time_t epoch; time(&epoch);
    s_firstSyncEpoch = static_cast<uint32_t>(epoch);

    char buf[24];
    isoNow(buf, sizeof(buf));
    Serial.printf("[ntp] synced first time = %s (epoch=%lu)\n",
                  buf, static_cast<unsigned long>(s_firstSyncEpoch));
  } else {
    Serial.println("[ntp] not synced yet, requesting again...");
    requestSync();
  }
}

bool timeIsSynced() { return s_synced; }

uint32_t timeEpoch() {
  if (!s_synced) return 0;
  time_t now; time(&now);
  return static_cast<uint32_t>(now);
}

bool isoNow(char* outBuf, size_t outBufLen) {
  if (outBufLen < 21 || outBuf == nullptr) return false;
  outBuf[0] = '\0';

  struct tm tm;
  if (!getLocalTime(&tm, 50 /*ms*/)) return false;
  if ((tm.tm_year + 1900) < kMinYear) return false;

  // Format: 2026-06-13T08:15:42Z (20 chars + null)
  strftime(outBuf, outBufLen, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return true;
}

}  // namespace net
