// ==================================================================
// Sprint 1 + Sprint 2 + Sprint 3 + Sprint 4 — Main loop
//
// Pipeline:
//   setup():
//     WiFi → NTP → NVS → identity → provisioned config → HTTP → mock → heartbeat
//     → CLI → queue (Sprint 3) → LED (Sprint 3) → MQTT + cmd handler (Sprint 4)
//
//   loop():
//     wifi tick → ntp tick → cli tick → mqtt tick (Sprint 4)
//     → ensureProvisioned
//     → ingest (pollingInterval):
//          mock multi-source → split per battery
//          MQTT primary: publish solar/{dev}/{serial}/telemetry per battery
//          fallback HTTPS nếu MQTT disconnect / consecutive fail ≥ threshold (S4-FW-06)
//          fail → enqueue + backoff (luôn HTTPS cho flush queue)
//     → flushQueue: HTTPS-only (idempotency dedup ở backend)
//     → heartbeat (heartbeatInterval) — HTTPS POST
//     → updateStatusLed
//
// Tham chiếu: tasksprint S1-FW-* + S2-FW-* + S3-FW-* + S4-FW-*
// ==================================================================
#include <Arduino.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
  #warning "config.h not found — fallback config.example.h"
#endif

#include "cli/serial_cli.h"
#include "cmd/command_handler.h"
#include "config/device_identity.h"
#include "config/nvs_store.h"
#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "net/http_client.h"
#include "net/mqtt_client.h"
#include "net/backoff.h"
#include "bms/mock_bms.h"
#include "core/payload.h"
#include "core/idempotency_key.h"
#include "queue/local_queue.h"
#include "provision/provision.h"
#include "telemetry/heartbeat.h"
#include "ui/status_led.h"

namespace {

provision::ProvisionedConfig s_provCfg;
bool     s_provisionDone   = false;

uint32_t s_lastIngestMs    = 0;
uint32_t s_batchSeq        = 0;
uint32_t s_okCount         = 0;
uint32_t s_failCount       = 0;
uint32_t s_queuedCount     = 0;
uint32_t s_flushedCount    = 0;

net::Backoff s_backoff;

// Payload buffer — Sprint 3 multi-source = 2 readings/battery, max 8 batteries
//                  → 16 readings. payloadBufferSize(16) ≈ 6 KB.
constexpr size_t kMaxReadings = bms::kMockMaxBatteries * bms::kSourcesPerBattery;
char s_payloadBuf[core::payloadBufferSize(kMaxReadings) + 128];
char s_idempBuf[core::kIdempotencyKeyBuf];

void printBanner() {
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Sprint 1+2+3 — Firmware ESP32-S3");
  Serial.printf("  Version : %s\n",   FW_VERSION);
  Serial.printf("  Env     : %s\n",   FW_BUILD_ENV);
  Serial.printf("  Backend : %s\n",   BACKEND_URL);
  Serial.printf("  Pins    : %u × %u sources\n",
                static_cast<unsigned>(MOCK_BATTERY_COUNT),
                static_cast<unsigned>(bms::kSourcesPerBattery));
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

  static uint32_t s_nextAttemptMs = 0;
  if (millis() < s_nextAttemptMs) return;

  Serial.println("[main] device chưa provisioned — chạy provision flow...");
  ui::ledSet(ui::LedState::Provisioning);
  if (provision::runProvisionFlow(FW_VERSION, HARDWARE_REVISION, s_provCfg)) {
    s_provisionDone = true;
    telemetry::heartbeatSetInterval(s_provCfg.heartbeatIntervalMs);
  } else {
    s_nextAttemptMs = millis() + 30000;
    Serial.println("[main] provision fail — retry sau 30s");
  }
}

// Sprint 3 (S3-FW-04): build production payload với multi-source mock.
size_t buildBatchPayload(const char* isoTs, size_t* outReadingCount) {
  core::SensorReading readings[kMaxReadings];
  const size_t nBat = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::mockGenerateMultiSource(readings, nBat);
  if (n == 0) return 0;

  size_t bodyLen = core::buildProductionBatchPayload(
      readings, n, isoTs, identity::deviceCode(),
      s_payloadBuf, sizeof(s_payloadBuf));
  if (outReadingCount) *outReadingCount = n;
  return bodyLen;
}

// Sprint 3: outcome enum cho retry decision.
enum class PostOutcome : uint8_t {
  Success,
  TransientFailure,    // retry với backoff (5xx, network err, 408, 429)
  PermanentFailure,    // drop khỏi queue (4xx data sai)
};

// Sprint 3: POST 1 batch. Trả PostOutcome để caller quyết định retry/drop.
PostOutcome postBatch(const char* body, size_t bodyLen, const char* idempKey) {
  s_batchSeq++;
  net::PostResult res = net::httpPostJsonWithIdempotency(
      BACKEND_INGEST_PATH, body, bodyLen, idempKey);
  if (res.httpCode >= 200 && res.httpCode < 300) {
    Serial.printf("[ingest] batch#%lu posted (%d OK) [%lums] idem=%.8s...\n",
                  static_cast<unsigned long>(s_batchSeq),
                  res.httpCode,
                  static_cast<unsigned long>(res.durationMs),
                  idempKey ? idempKey : "(none)");
    return PostOutcome::Success;
  }
  bool transient = net::isTransientFailure(res.httpCode);
  const char* kind = transient ? "TRANSIENT" : "PERMANENT";
  Serial.printf("[ingest] batch#%lu %s FAIL (code=%d) [%lums] resp=\"%s\"\n",
                static_cast<unsigned long>(s_batchSeq),
                kind,
                res.httpCode,
                static_cast<unsigned long>(res.durationMs),
                res.responseSnippet);
  return transient ? PostOutcome::TransientFailure : PostOutcome::PermanentFailure;
}

// Sprint 4 (S4-FW-04): MQTT-first publish path.
// Sinh readings 1 lần, split theo battery, publish từng pin trên topic riêng.
// Trả true nếu TẤT CẢ pin publish OK; false nếu ANY pin fail → caller fallback HTTPS.
//
// Note: build payload PER battery (count=kSourcesPerBattery) — KHÔNG dùng full
// batch buffer s_payloadBuf để tránh xung đột với HTTPS path khi fallback.
bool ingestViaMqtt(const char* isoTs, size_t batteryCount,
                   const core::SensorReading* readings, size_t readingCount) {
  if (!net::mqttIsConnected()) return false;

  // Buffer per-battery: 3 source × ~384B = ~1.2KB + envelope.
  static char perPinBuf[core::payloadBufferSize(bms::kSourcesPerBattery) + 128];

  size_t allOk = 0;
  for (size_t b = 0; b < batteryCount; ++b) {
    const size_t off = b * bms::kSourcesPerBattery;
    if (off + bms::kSourcesPerBattery > readingCount) break;

    const core::SensorReading* pinReadings = &readings[off];
    const char* serial = pinReadings[0].serial;
    if (!serial || serial[0] == '\0') {
      Serial.printf("[mqtt-ingest] battery #%u thiếu serial — skip\n",
                    static_cast<unsigned>(b));
      continue;
    }

    size_t bodyLen = core::buildProductionBatchPayload(
        pinReadings, bms::kSourcesPerBattery, isoTs, identity::deviceCode(),
        perPinBuf, sizeof(perPinBuf));
    if (bodyLen == 0) {
      Serial.printf("[mqtt-ingest] battery #%u payload build FAIL\n",
                    static_cast<unsigned>(b));
      return false;
    }

    bool pubOk = net::mqttPublishTelemetry(serial, perPinBuf, bodyLen);
    Serial.printf("[mqtt-ingest] pub %s (%u bytes) → %s\n",
                  serial, static_cast<unsigned>(bodyLen),
                  pubOk ? "OK" : "FAIL");
    if (!pubOk) return false;
    allOk++;
  }
  return allOk == batteryCount;
}

bool ingestOnce() {
  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[ingest] NTP not synced — skip");
    return false;
  }

  // Sinh readings 1 lần (dùng chung cho cả MQTT + HTTPS fallback).
  core::SensorReading readings[kMaxReadings];
  const size_t nBat = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::mockGenerateMultiSource(readings, nBat);
  if (n == 0) return false;

  // ---- Sprint 4 (S4-FW-04 + S4-FW-06): chọn transport ----
  // Ưu tiên MQTT trừ khi: chưa connect / streak fail ≥ threshold.
  // mqtt_client tự reset streak khi reconnect thành công.
  bool tryMqtt = net::mqttIsConnected() &&
                 (net::mqttConsecutiveFailCount() < MQTT_PUBLISH_FAIL_THRESHOLD);

  if (tryMqtt) {
    if (ingestViaMqtt(ts, nBat, readings, n)) {
      s_backoff.reset();
      Serial.printf("[ingest] MQTT posted %u readings across %u pin\n",
                    static_cast<unsigned>(n), static_cast<unsigned>(nBat));
      return true;
    }
    Serial.printf("[ingest] MQTT FAIL (streak=%lu) → fallback HTTPS\n",
                  static_cast<unsigned long>(net::mqttConsecutiveFailCount()));
  } else if (net::mqttConsecutiveFailCount() >= MQTT_PUBLISH_FAIL_THRESHOLD) {
    Serial.printf("[ingest] skip MQTT (streak=%lu ≥ %u) → HTTPS\n",
                  static_cast<unsigned long>(net::mqttConsecutiveFailCount()),
                  static_cast<unsigned>(MQTT_PUBLISH_FAIL_THRESHOLD));
  }

  // ---- Fallback HTTPS path (giữ idempotency + queue logic Sprint 3) ----
  size_t bodyLen = core::buildProductionBatchPayload(
      readings, n, ts, identity::deviceCode(),
      s_payloadBuf, sizeof(s_payloadBuf));
  if (bodyLen == 0) return false;

  // Sprint 3 (S3-FW-02): sinh Idempotency-Key UUIDv4 cho batch này.
  if (!core::generateIdempotencyKeyV4(s_idempBuf, sizeof(s_idempBuf))) {
    Serial.println("[ingest] FAIL: idempotency key gen");
    return false;
  }

  PostOutcome outcome = postBatch(s_payloadBuf, bodyLen, s_idempBuf);
  if (outcome == PostOutcome::Success) {
    s_backoff.reset();
    Serial.printf("[ingest] HTTPS posted %u readings (multi-source)\n",
                  static_cast<unsigned>(n));
    return true;
  }

  // Permanent (4xx) → drop, KHÔNG enqueue, reset backoff (vì transient logic không apply).
  if (outcome == PostOutcome::PermanentFailure) {
    Serial.println("[ingest] DROPPED — permanent 4xx (data invalid)");
    s_backoff.reset();
    return false;
  }

  // Transient → enqueue + backoff để retry sau (queue luôn flush qua HTTPS)
  uint32_t epoch = net::timeEpoch();
  if (epoch != 0 && queue::queueEnqueue(epoch, s_payloadBuf, bodyLen, s_idempBuf)) {
    s_queuedCount++;
    uint32_t waitMs = s_backoff.recordFailure();
    Serial.printf("[ingest] queued (depth=%u) backoff=%lums\n",
                  static_cast<unsigned>(queue::queueSize()),
                  static_cast<unsigned long>(waitMs));
  }
  return false;
}

// Sprint 3 (S3-FW-03): flush 1 batch oldest từ queue nếu backoff allows.
void tryFlushQueue() {
  if (queue::queueSize() == 0) return;
  if (!net::wifiIsConnected() || !net::timeIsSynced()) return;
  if (millis() < s_backoff.nextRetryAt()) return;

  static char qBody[core::payloadBufferSize(kMaxReadings) + 128];
  static char qIdem[core::kIdempotencyKeyBuf];
  size_t bodyLen = 0;
  uint32_t epoch = 0;
  if (!queue::queuePeekOldest(qBody, sizeof(qBody), &bodyLen,
                              qIdem, sizeof(qIdem), &epoch)) {
    return;
  }
  Serial.printf("[flush] try epoch=%lu bytes=%u idem=%.8s...\n",
                static_cast<unsigned long>(epoch),
                static_cast<unsigned>(bodyLen),
                qIdem);
  PostOutcome outcome = postBatch(qBody, bodyLen, qIdem[0] ? qIdem : nullptr);
  if (outcome == PostOutcome::Success) {
    queue::queueDelete(epoch);
    s_flushedCount++;
    s_backoff.reset();
    Serial.printf("[flush] OK — queue depth=%u\n",
                  static_cast<unsigned>(queue::queueSize()));
    return;
  }
  if (outcome == PostOutcome::PermanentFailure) {
    // 4xx → data sai, retry vô ích → drop khỏi queue + reset backoff để các batch sau retry bình thường.
    queue::queueDelete(epoch);
    s_backoff.reset();
    Serial.printf("[flush] DROPPED epoch=%lu — permanent 4xx (data invalid). Queue depth=%u\n",
                  static_cast<unsigned long>(epoch),
                  static_cast<unsigned>(queue::queueSize()));
    return;
  }
  // Transient → giữ trong queue + backoff
  uint32_t waitMs = s_backoff.recordFailure();
  Serial.printf("[flush] transient fail — backoff=%lums (queue stays at %u)\n",
                static_cast<unsigned long>(waitMs),
                static_cast<unsigned>(queue::queueSize()));
}

void updateStatusLed() {
  if (!net::wifiIsConnected()) {
    ui::ledSet(ui::LedState::Offline);
  } else if (queue::queueSize() > 0) {
    ui::ledSet(ui::LedState::Queued);
  } else {
    ui::ledSet(ui::LedState::Online);
  }
}

void logStatsPeriodic() {
  static uint32_t s_lastStatsMs = 0;
  const uint32_t now = millis();
  if (now - s_lastStatsMs < 60000) return;
  s_lastStatsMs = now;
  Serial.printf("[stats] uptime=%lus ingest ok=%lu fail=%lu queued=%lu flushed=%lu / "
                "hb ok=%lu fail=%lu / queue=%u backoff_attempt=%lu / "
                "mqtt conn=%s cn=%lu pubok=%lu pubfail=%lu streak=%lu / "
                "cmd rx=%lu ok=%lu fail=%lu unk=%lu / "
                "heap=%u rssi=%ddBm wifi=%s prov=%s\n",
                static_cast<unsigned long>(now / 1000),
                static_cast<unsigned long>(s_okCount),
                static_cast<unsigned long>(s_failCount),
                static_cast<unsigned long>(s_queuedCount),
                static_cast<unsigned long>(s_flushedCount),
                static_cast<unsigned long>(telemetry::heartbeatOkCount()),
                static_cast<unsigned long>(telemetry::heartbeatFailCount()),
                static_cast<unsigned>(queue::queueSize()),
                static_cast<unsigned long>(s_backoff.attemptCount()),
                net::mqttIsConnected() ? "UP" : "DOWN",
                static_cast<unsigned long>(net::mqttConnectCount()),
                static_cast<unsigned long>(net::mqttPublishOkCount()),
                static_cast<unsigned long>(net::mqttPublishFailCount()),
                static_cast<unsigned long>(net::mqttConsecutiveFailCount()),
                static_cast<unsigned long>(cmd::cmdReceivedCount()),
                static_cast<unsigned long>(cmd::cmdAckOkCount()),
                static_cast<unsigned long>(cmd::cmdAckFailedCount()),
                static_cast<unsigned long>(cmd::cmdUnknownTypeCount()),
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

  ui::ledBegin();
  ui::ledSet(ui::LedState::Offline);

  net::wifiBegin(WIFI_SSID, WIFI_PASS);
  net::timeSyncBegin();
  storage::nvsBegin();
  identity::identityBegin();
  provision::loadProvisioned(s_provCfg);
  if (s_provCfg.provisioned) {
    Serial.printf("[main] loaded provisioned config: polling=%lums hb=%lums site=%s ntp=%s\n",
                  static_cast<unsigned long>(s_provCfg.pollingIntervalMs),
                  static_cast<unsigned long>(s_provCfg.heartbeatIntervalMs),
                  s_provCfg.siteId, s_provCfg.ntpServer);
    s_provisionDone = true;
  }

  net::httpClientBegin();
  bms::mockBegin();
  telemetry::heartbeatBegin(s_provCfg.heartbeatIntervalMs);
  cli::cliBegin();

  // Sprint 3
  queue::queueBegin();

  // Sprint 4 (S4-FW-01/02/03/05): MQTT broker client + downlink handler.
  // mqttBegin() load CA cert từ LittleFS (queue đã mount LittleFS lúc queueBegin
  // — share an toàn vì LittleFS API idempotent).
  if (net::mqttBegin()) {
    cmd::handlerBegin();
    // S4-FW-05: wire polling setter — cmd `set_interval` cập nhật s_provCfg.
    cmd::setPollingHandler([](uint32_t newMs) -> bool {
      s_provCfg.pollingIntervalMs = newMs;
      Serial.printf("[main] polling interval đã đổi runtime → %lums\n",
                    static_cast<unsigned long>(newMs));
      return true;
    });
  } else {
    Serial.println("[setup] MQTT init FAIL — chạy HTTPS-only (S4-FW-06 fallback)");
  }

  Serial.println("[setup] done — entering loop()");
}

void loop() {
  net::wifiTick();
  net::timeSyncTick();
  cli::cliTick();
  // Sprint 4: MQTT tick (reconnect + poll inbound cmd). Tự skip nếu wifi down.
  net::mqttTick();

  ensureProvisioned();

  const uint32_t now = millis();
  const uint32_t pollInterval = s_provCfg.provisioned ? s_provCfg.pollingIntervalMs
                                                      : INGEST_INTERVAL_MS;

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

  // Sprint 3: flush queue khi backoff allow
  tryFlushQueue();

  // Sprint 2: heartbeat
  if (net::wifiIsConnected() && net::timeIsSynced()) {
    telemetry::heartbeatTick();
  }

  updateStatusLed();
  logStatsPeriodic();
  delay(10);
}
