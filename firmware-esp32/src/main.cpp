// ==================================================================
// Sprint 1 — S1-FW-07: Main loop
//
// Build:  pio run
// Flash:  pio run -t upload
// Mon:    pio device monitor -b 115200
//
// Acceptance (tasksprint.md S1-FW-07):
//   - Mỗi 5s đọc mock → build payload → POST → log status
//   - Serial in 1 dòng "posted 4 readings (200 OK)" mỗi 5s
//     (S1-FW-07 acceptance — tasksprint.md)
//
// Pipeline mỗi tick:
//   wifi tick → ntp tick → (mỗi INGEST_INTERVAL_MS) mock generate → build payload → POST → log
//
// Module map:
//   include/config.h                — credentials + endpoint + scenario flag
//   src/net/wifi_manager.cpp        — S1-FW-02
//   src/net/time_sync.cpp           — S1-FW-03
//   src/bms/mock_bms.cpp            — S1-FW-04
//   src/core/payload.cpp            — S1-FW-05
//   src/net/http_client.cpp         — S1-FW-06
//
// Tham chiếu:
//   - newiot.md §8.3 (loop), §9.1 (firmware structure)
//   - tasksprint.md S1-FW-07
// ==================================================================
#include <Arduino.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
  #warning "config.h not found — fallback config.example.h"
#endif

#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "net/http_client.h"
#include "bms/mock_bms.h"
#include "core/payload.h"

namespace {

// ---- Runtime counters ----
uint32_t s_lastIngestMs   = 0;
uint32_t s_batchSeq       = 0;     // tăng dần mỗi POST OK — dùng cho log
uint32_t s_okCount        = 0;
uint32_t s_failCount      = 0;

// Buffer JSON tái sử dụng — đủ lớn cho MOCK_BATTERY_COUNT readings.
// payloadBufferSize(N) = 192 + N*256, kMockMaxBatteries=8 → ~2.2kB.
char s_payloadBuf[core::payloadBufferSize(bms::kMockMaxBatteries) + 64];

void printBanner() {
  Serial.println();
  Serial.println("==============================");
  Serial.println(" Sprint 1 — MVP Mock HTTPS");
  Serial.printf(" Version : %s\n",   FW_VERSION);
  Serial.printf(" Env     : %s\n",   FW_BUILD_ENV);
  Serial.printf(" Device  : %s\n",   DEVICE_CODE);
  Serial.printf(" Backend : %s%s\n", BACKEND_URL, BACKEND_INGEST_PATH);
  Serial.printf(" Pins    : %u (mock)\n",  static_cast<unsigned>(MOCK_BATTERY_COUNT));
  Serial.printf(" Interval: %lums\n",
                static_cast<unsigned long>(INGEST_INTERVAL_MS));
  Serial.printf(" Scenario: %s\n", bms::mockScenarioName());
  Serial.println("==============================");
}

// Gửi 1 batch ingest. Return true nếu HTTP code 2xx.
bool ingestOnce() {
  // 1) Sinh reading mock
  core::SensorReading readings[bms::kMockMaxBatteries];
  const size_t nReq = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::mockGenerate(readings, nReq);
  if (n == 0) {
    Serial.println("[ingest] mockGenerate returned 0 — skip");
    return false;
  }

  // 2) Lấy timestamp ISO
  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[ingest] no NTP sync yet — skip POST");
    return false;
  }

  // 3) Build payload legacy
  size_t bodyLen = core::buildLegacyBatchPayload(readings, n, ts, DEVICE_CODE,
                                                 s_payloadBuf, sizeof(s_payloadBuf));
  if (bodyLen == 0) {
    Serial.println("[ingest] buildLegacyBatchPayload failed");
    return false;
  }

  // 4) POST
  s_batchSeq++;
  net::PostResult res = net::httpPostJson(BACKEND_INGEST_PATH, s_payloadBuf, bodyLen);

  // 5) Log format khớp S1-FW-07 acceptance: "posted 4 readings (200 OK)"
  const bool ok = (res.httpCode >= 200 && res.httpCode < 300);
  if (ok) {
    Serial.printf("[ingest] batch#%lu posted %u readings (%d OK) [%lums]\n",
                  static_cast<unsigned long>(s_batchSeq),
                  static_cast<unsigned>(n),
                  res.httpCode,
                  static_cast<unsigned long>(res.durationMs));
  } else {
    Serial.printf("[ingest] batch#%lu FAILED %u readings (code=%d) [%lums] resp=\"%s\"\n",
                  static_cast<unsigned long>(s_batchSeq),
                  static_cast<unsigned>(n),
                  res.httpCode,
                  static_cast<unsigned long>(res.durationMs),
                  res.responseSnippet);
  }
  return ok;
}

void logStatsPeriodic() {
  static uint32_t s_lastStatsMs = 0;
  const uint32_t now = millis();
  if (now - s_lastStatsMs < 60000) return;
  s_lastStatsMs = now;
  Serial.printf("[stats] uptime=%lus ok=%lu fail=%lu heap=%u rssi=%ddBm wifi=%s\n",
                static_cast<unsigned long>(now / 1000),
                static_cast<unsigned long>(s_okCount),
                static_cast<unsigned long>(s_failCount),
                static_cast<unsigned>(ESP.getFreeHeap()),
                net::wifiRssi(),
                net::wifiIsConnected() ? "UP" : "DOWN");
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  printBanner();

  net::wifiBegin(WIFI_SSID, WIFI_PASS);
  net::timeSyncBegin();
  net::httpClientBegin();
  bms::mockBegin();

  Serial.println("[setup] done — entering loop()");
}

void loop() {
  net::wifiTick();
  net::timeSyncTick();

  const uint32_t now = millis();
  if (now - s_lastIngestMs >= INGEST_INTERVAL_MS) {
    s_lastIngestMs = now;

    // Tiền điều kiện: WiFi + NTP.
    // Không có WiFi → skip POST, sẽ thử lại sau INGEST_INTERVAL_MS.
    // Sprint 3 sẽ thêm local queue NVS để buffer khi mất mạng.
    if (!net::wifiIsConnected()) {
      Serial.println("[ingest] WiFi DOWN — skip (no queue in S1)");
      s_failCount++;
    } else if (!net::timeIsSynced()) {
      Serial.println("[ingest] NTP not synced — skip");
      s_failCount++;
    } else {
      if (ingestOnce()) s_okCount++; else s_failCount++;
    }
  }

  logStatsPeriodic();
  delay(10);   // nhường CPU cho IDF tasks (WiFi, SNTP)
}
