// ==================================================================
// Sprint 1 + Sprint 2 — Main loop
//
// Build:  pio run
// Flash:  pio run -t upload
// Mon:    pio device monitor -b 115200
//
// Pipeline:
//   setup():
//     1. WiFi connect (wifi_manager S1-FW-02)
//     2. NTP begin (time_sync S1-FW-03)
//     3. NVS mount (nvs_store S2-FW-01)
//     4. Identity load (apiKey + deviceCode từ NVS hoặc compile-time)
//     5. Mock BMS init (mock_bms S1-FW-04)
//     6. HTTP client init (http_client S1-FW-06 + S2-FW-04 X-Device-Code)
//     7. Heartbeat init (heartbeat S2-FW-03)
//     8. Serial CLI ready (S2-FW-01 hot reload commands)
//
//   loop():
//     - wifi tick + ntp tick + cli tick
//     - (1 lần) provision flow (S2-FW-02) — sau NTP synced
//     - mỗi pollingInterval (default 5s): mock generate → build → POST ingest
//     - mỗi heartbeatInterval (default 60s): heartbeat (S2-FW-03)
//
// Tham chiếu:
//   - tasksprint.md S1-FW-01..07 + S2-FW-01..04
//   - newiot.md §8.3 (loop), §9.1 (firmware structure)
// ==================================================================
#include <Arduino.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
  #warning "config.h not found — fallback config.example.h"
#endif

#include "cli/serial_cli.h"
#include "config/device_identity.h"
#include "config/nvs_store.h"
#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "net/http_client.h"
#include "bms/mock_bms.h"
#include "core/payload.h"
#include "provision/provision.h"
#include "telemetry/heartbeat.h"

namespace {

// ---- Runtime config (sau provision) ----
provision::ProvisionedConfig s_provCfg;
bool     s_provisionDone   = false;   // đã gọi runProvisionFlow chưa
uint32_t s_lastIngestMs    = 0;
uint32_t s_batchSeq        = 0;
uint32_t s_okCount         = 0;
uint32_t s_failCount       = 0;

// JSON payload buffer
char s_payloadBuf[core::payloadBufferSize(bms::kMockMaxBatteries) + 64];

void printBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Sprint 1+2 — Firmware ESP32-S3");
  Serial.printf("  Version : %s\n",   FW_VERSION);
  Serial.printf("  Env     : %s\n",   FW_BUILD_ENV);
  Serial.printf("  Backend : %s\n",   BACKEND_URL);
  Serial.printf("  Pins    : %u (mock)\n",  static_cast<unsigned>(MOCK_BATTERY_COUNT));
  Serial.printf("  Scenario: %s\n", bms::mockScenarioName());
  Serial.println("========================================");
}

void ensureProvisioned() {
  if (s_provisionDone) return;
  if (s_provCfg.provisioned) {
    s_provisionDone = true;
    return;
  }
  if (!net::wifiIsConnected() || !net::timeIsSynced()) return;

  // Cooldown — check TRƯỚC khi gọi runProvisionFlow để không retry liên tục.
  static uint32_t s_nextAttemptMs = 0;
  if (millis() < s_nextAttemptMs) return;

  Serial.println("[main] device chưa provisioned — chạy provision flow...");
  if (provision::runProvisionFlow(FW_VERSION, HARDWARE_REVISION, s_provCfg)) {
    s_provisionDone = true;
    telemetry::heartbeatSetInterval(s_provCfg.heartbeatIntervalMs);
  } else {
    s_nextAttemptMs = millis() + 30000;       // retry sau 30s
    Serial.println("[main] provision fail — retry sau 30s");
  }
}

bool ingestOnce() {
  core::SensorReading readings[bms::kMockMaxBatteries];
  const size_t nReq = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::mockGenerate(readings, nReq);
  if (n == 0) return false;

  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[ingest] NTP not synced — skip POST");
    return false;
  }

  size_t bodyLen = core::buildLegacyBatchPayload(readings, n, ts,
                                                 identity::deviceCode(),
                                                 s_payloadBuf, sizeof(s_payloadBuf));
  if (bodyLen == 0) return false;

  s_batchSeq++;
  net::PostResult res = net::httpPostJson(BACKEND_INGEST_PATH, s_payloadBuf, bodyLen);

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
  Serial.printf("[stats] uptime=%lus ingest ok=%lu fail=%lu / hb ok=%lu fail=%lu "
                "/ heap=%u rssi=%ddBm wifi=%s prov=%s\n",
                static_cast<unsigned long>(now / 1000),
                static_cast<unsigned long>(s_okCount),
                static_cast<unsigned long>(s_failCount),
                static_cast<unsigned long>(telemetry::heartbeatOkCount()),
                static_cast<unsigned long>(telemetry::heartbeatFailCount()),
                static_cast<unsigned>(ESP.getFreeHeap()),
                net::wifiRssi(),
                net::wifiIsConnected() ? "UP" : "DOWN",
                s_provisionDone ? "yes" : "no");
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  printBanner();

  // 1. WiFi (S1-FW-02)
  net::wifiBegin(WIFI_SSID, WIFI_PASS);

  // 2. NTP (S1-FW-03)
  net::timeSyncBegin();

  // 3. NVS (S2-FW-01 foundation)
  storage::nvsBegin();

  // 4. Identity (S2-FW-01)
  identity::identityBegin();

  // 5. Load provisioned config từ NVS (S2-FW-02)
  provision::loadProvisioned(s_provCfg);
  if (s_provCfg.provisioned) {
    Serial.printf("[main] loaded provisioned config: polling=%lums hb=%lums site=%s ntp=%s\n",
                  static_cast<unsigned long>(s_provCfg.pollingIntervalMs),
                  static_cast<unsigned long>(s_provCfg.heartbeatIntervalMs),
                  s_provCfg.siteId, s_provCfg.ntpServer);
    s_provisionDone = true;
  }

  // 6. HTTP client (S1-FW-06 + S2-FW-04)
  net::httpClientBegin();

  // 7. Mock BMS (S1-FW-04)
  bms::mockBegin();

  // 8. Heartbeat (S2-FW-03)
  telemetry::heartbeatBegin(s_provCfg.heartbeatIntervalMs);

  // 9. Serial CLI (S2-FW-01 hot reload)
  cli::cliBegin();

  Serial.println("[setup] done — entering loop()");
}

void loop() {
  net::wifiTick();
  net::timeSyncTick();
  cli::cliTick();

  // Provision (1 lần, sau khi WiFi + NTP sẵn sàng)
  ensureProvisioned();

  const uint32_t now = millis();
  const uint32_t pollInterval = s_provCfg.provisioned ? s_provCfg.pollingIntervalMs
                                                      : INGEST_INTERVAL_MS;

  // Ingest loop (pollingInterval) — S1-FW-07
  if (now - s_lastIngestMs >= pollInterval) {
    s_lastIngestMs = now;

    if (!net::wifiIsConnected()) {
      s_failCount++;
    } else if (!net::timeIsSynced()) {
      s_failCount++;
    } else {
      if (ingestOnce()) s_okCount++; else s_failCount++;
    }
  }

  // Heartbeat (S2-FW-03 — interval set by provision response)
  if (net::wifiIsConnected() && net::timeIsSynced()) {
    telemetry::heartbeatTick();
  }

  logStatsPeriodic();
  delay(10);
}
