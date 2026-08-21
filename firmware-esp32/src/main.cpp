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
//          MQTT unavailable/fail → persist remaining readings before any HTTPS attempt
//     → flushQueue: MQTT primary, HTTPS fallback, delete after transport accepts
//     → heartbeat (heartbeatInterval) — HTTPS POST
//     → updateStatusLed
//
// Tham chiếu: tasksprint S1-FW-* + S2-FW-* + S3-FW-* + S4-FW-*
// ==================================================================
#include <Arduino.h>
#include <ArduinoJson.h>

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
  #warning "config.h not found — fallback config.example.h"
#endif

#include "cli/serial_cli.h"
#include "cmd/command_handler.h"
#include "config/battery_map_runtime.h"
#include "config/device_identity.h"
#include "config/mqtt_config.h"
#include "config/nvs_store.h"
#include "config/runtime_config.h"
#include "config/wifi_config.h"
#include "net/setup_portal.h"
#include "net/wifi_manager.h"
#include "net/time_sync.h"
#include "net/http_client.h"
#include "net/mqtt_client.h"
#include "net/backoff.h"
#include "bms/mock_bms.h"
#include "bms/bms_source.h"
#include "bms/modbus_bms.h"
#include "sensor/sht31.h"
#include "sensor/ina226.h"
#include "sensor/ds18b20.h"
#include "sensor/mq2.h"                 // Sprint 6 (S6-FW-01)
#include "sensor/water_leak.h"          // Sprint 6 (S6-FW-02)
#include "sensor/environmental_incident.h"  // Sprint 6 — shared reporter
#include "core/payload.h"
#include "core/idempotency_key.h"
#include "ota/ota_update.h"             // Sprint 7 (S7-FW-01/02)
#include "queue/local_queue.h"
#include "core/ingest_policy.h"
#include "core/reprovision_policy.h"
#include "core/reading_filter.h"
#include "core/ingest_result.h"
#include "provision/provision.h"
#include "telemetry/heartbeat.h"
#include "ui/status_led.h"
#include "portal/setup_portal.h"

#include <cstring>

namespace {

provision::ProvisionedConfig s_provCfg;
bool     s_provisionDone   = false;

uint32_t s_lastIngestMs    = 0;
uint32_t s_batchSeq        = 0;
uint32_t s_okCount         = 0;
uint32_t s_failCount       = 0;
uint32_t s_partialIngestCount = 0;   // GH-748
uint32_t s_queuedCount     = 0;
uint32_t s_flushedCount    = 0;

net::Backoff s_backoff;

bool deviceIdentityReady() {
  return identity::deviceCode()[0] != '\0' && identity::apiKey()[0] != '\0';
}

// Chỉ xanh sau khi backend đã ACK provision/heartbeat hoặc MQTT đã nhận telemetry
// trong chính lần boot này. Wi-Fi connected không đồng nghĩa backend reachable.
bool s_backendAcknowledged = false;

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
  Serial.printf("  Backend : %s\n", runtimecfg::backendUrl());
  Serial.printf("  Pins    : %u × %u sources\n",
                static_cast<unsigned>(MOCK_BATTERY_COUNT),
                static_cast<unsigned>(bms::kSourcesPerBattery));
  Serial.printf("  Scenario: %s\n", bms::mockScenarioName());
  Serial.println("========================================");
}

void ensureProvisioned() {
  if (!deviceIdentityReady()) return;
  if (s_provisionDone) return;
  if (s_provCfg.provisioned) {
    s_provisionDone = true;
    // Sprint 5 (S5-FW-06): siteId now available — wire vào SHT31.
    sensor::sht31SetSiteId(s_provCfg.siteId);
    // Sprint 6 (S6-FW-01/02): siteId dùng chung cho MQ-2 + water leak reporter.
    sensor::envIncidentSetSiteId(s_provCfg.siteId);

    // IOT3-46 — nhánh này chạy khi NVS đã có `provd=1`, tức TỪ LẦN BOOT THỨ HAI TRỞ ĐI.
    // Thiếu dòng dưới thì lần boot đầu (vừa provision xong) chạy đẹp, còn mọi lần boot sau
    // âm thầm chạy HTTPS-only — và KHÔNG có log lỗi nào, vì HTTPS vẫn hoạt động bình thường.
    // `mqttApplyConfig()` tự bỏ qua khi cấu hình không đổi, gọi nhiều lần vô hại.
    net::mqttApplyConfig();
    return;
  }
  if (!net::wifiIsConnected() || !net::timeIsSynced()) return;

  static uint32_t s_nextAttemptMs = 0;
  if (millis() < s_nextAttemptMs) return;

  Serial.println("[main] device chưa provisioned — chạy provision flow...");
  ui::ledSet(ui::LedState::Provisioning);
  if (provision::runProvisionFlow(FW_VERSION, HARDWARE_REVISION, s_provCfg)) {
    s_provisionDone = true;
    s_backendAcknowledged = true;
    telemetry::heartbeatSetInterval(s_provCfg.heartbeatIntervalMs);
    // S5-FW-06: provision response chứa siteId → wire vào SHT31.
    sensor::sht31SetSiteId(s_provCfg.siteId);
    // S6-FW-01/02: siteId dùng chung cho MQ-2 + water leak reporter.
    sensor::envIncidentSetSiteId(s_provCfg.siteId);
  } else {
    s_nextAttemptMs = millis() + 30000;
    Serial.println("[main] provision fail — retry sau 30s");
  }
}

// IOT3-44 — broker từ chối đăng nhập liên tục ⇒ refresh credential tại chỗ.
//
// Khác hẳn lỗi mạng: chờ không giải quyết được gì, vì mật khẩu trong NVS đã không còn khớp
// `mqtt_password_plaintext` bên backend (admin xoay key, hoặc thiết bị được tạo lại). Đường tự
// lành duy nhất là gọi lại `/provision` để nhận credential mới. Không được xóa
// cờ provision trước: nếu API cũng đang lỗi thì gateway sẽ mắc kẹt màu tím và
// ngừng cả sampling/queue dù cấu hình cũ vẫn còn nguyên.
void checkMqttCredentialHealth() {
  static uint32_t s_lastReprovMs   = 0;
  static bool     s_everReprov     = false;

  if (!s_provisionDone) return;   // provision đang/chưa chạy — để `ensureProvisioned()` lo

  if (!core::shouldReprovisionOnAuthFailure(net::mqttAuthFailureCount(),
                                            net::mqttAuthFailureThreshold(),
                                            millis(), s_lastReprovMs, s_everReprov)) {
    return;
  }

  Serial.printf("[main] MQTT bị từ chối xác thực %lu lần liên tiếp — refresh credential "
                "qua /provision (hạ nhiệt %lu phút)\n",
                static_cast<unsigned long>(net::mqttAuthFailureCount()),
                static_cast<unsigned long>(core::kReprovisionCooldownMs / 60000UL));

  provision::ProvisionedConfig refreshed = s_provCfg;
  if (net::wifiIsConnected() && net::timeIsSynced() &&
      provision::runProvisionFlow(FW_VERSION, HARDWARE_REVISION, refreshed)) {
    s_provCfg = refreshed;
    s_provisionDone = true;
    s_backendAcknowledged = true;
    telemetry::heartbeatSetInterval(s_provCfg.heartbeatIntervalMs);
    sensor::sht31SetSiteId(s_provCfg.siteId);
    sensor::envIncidentSetSiteId(s_provCfg.siteId);
    Serial.println("[main] MQTT credential refresh OK — giữ trạng thái provisioned");
  } else {
    Serial.println("[main] MQTT credential refresh FAIL — giữ cấu hình cũ, tiếp tục queue/HTTPS fallback");
  }
  net::mqttResetAuthFailures();
  s_lastReprovMs = millis();
  s_everReprov   = true;
}

// Sprint 3 (S3-FW-04): build production payload với multi-source.
// Sprint 5 (S5-FW-07): dispatch qua bms_source — mock hoặc real BMS theo USE_MOCK_BMS.
size_t buildBatchPayload(const char* isoTs, size_t* outReadingCount) {
  core::SensorReading readings[kMaxReadings];
  const size_t nBat = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::bmsSourcePollAll(readings, nBat);
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

    // GH-748 — 2xx KHÔNG có nghĩa cả batch đã vào. Backend trả
    // { totalReceived, inserted, skipped }; trước đây firmware không hề đọc, nên việc bỏ
    // số đo (sai mapping asset, giá trị ngoài dải) diễn ra trong im lặng.
    //
    // KHÔNG gửi lại phần bị bỏ: `skipped` của backend là mapping_invalid + outlier — cả hai
    // vĩnh viễn, gửi lại đúng dữ liệu đó chỉ ra đúng kết quả đó. Cái cần cứu là TÍN HIỆU.
    const core::IngestResult ing =
        core::parseIngestResult(res.responseSnippet, strlen(res.responseSnippet));
    if (ing.isPartial()) {
      s_partialIngestCount++;
      Serial.printf("[ingest] ⚠ NHẬN THIẾU: %d/%d reading vào được, %d bị bỏ.\n",
                    ing.inserted, ing.totalReceived, ing.skipped);
      Serial.println("[ingest]   Nguyên nhân thường gặp: serial pin chưa được map cho thiết bị");
      Serial.println("[ingest]   này, hoặc giá trị ngoài dải vật lý. Kiểm tra provisioning/hiệu chuẩn.");
    }
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
// Sprint 5 fix: GROUP BY SERIAL dynamic — KHÔNG assume fixed kSourcesPerBattery slots.
// Lý do: real path (USE_MOCK_BMS=0) có thể skip INA226/DS18B20 fail → battery có
// 1 hoặc 2 reading thay vì 3. Slot indexing cũ `b * 3` sẽ misgroup.
//
// Returns true nếu TẤT CẢ battery có ≥ 1 reading publish OK; false nếu ANY fail.
// GH-740 — báo lại serial ĐÃ publish để caller loại chúng khỏi fallback HTTPS.
// `outPublished` nhận con trỏ vào chính `readings[i].serial` (sống hết vòng gọi).
bool ingestViaMqtt(const char* isoTs, size_t batteryCount,
                   const core::SensorReading* readings, size_t readingCount,
                   const char** outPublished, size_t outCap, size_t* outPublishedCount) {
  if (outPublishedCount) *outPublishedCount = 0;
  (void)batteryCount;  // không còn dùng — group by serial dynamic
  if (!net::mqttIsConnected()) return false;
  if (readings == nullptr || readingCount == 0) return false;

  // Buffer per-battery: 3 source × ~384B = ~1.2KB + envelope.
  static char perPinBuf[core::payloadBufferSize(bms::kSourcesPerBattery) + 128];

  // ---- Step 1: Enumerate unique serials (max kMockMaxBatteries) ----
  // Dùng pointer-comparison + strcmp để tránh allocate (FW single-threaded loop OK).
  const char* uniqueSerials[bms::kMockMaxBatteries] = {nullptr};
  size_t nUnique = 0;
  for (size_t i = 0; i < readingCount; ++i) {
    const char* s = readings[i].serial;
    if (!s || s[0] == '\0') continue;
    bool seen = false;
    for (size_t k = 0; k < nUnique; ++k) {
      if (strcmp(uniqueSerials[k], s) == 0) { seen = true; break; }
    }
    if (!seen && nUnique < bms::kMockMaxBatteries) {
      uniqueSerials[nUnique++] = s;
    }
  }
  if (nUnique == 0) {
    Serial.println("[mqtt-ingest] không có battery serial nào trong readings — skip");
    return false;
  }

  // ---- Step 2: For each unique serial, collect matching readings + publish ----
  // Buffer per-battery group: max kSourcesPerBattery readings + safety margin.
  core::SensorReading group[bms::kSourcesPerBattery + 2];

  size_t okCount = 0;
  for (size_t s = 0; s < nUnique; ++s) {
    const char* serial = uniqueSerials[s];
    size_t nGroup = 0;
    for (size_t i = 0; i < readingCount && nGroup < (bms::kSourcesPerBattery + 2); ++i) {
      if (strcmp(readings[i].serial, serial) == 0) {
        group[nGroup++] = readings[i];
      }
    }
    if (nGroup == 0) continue;

    size_t bodyLen = core::buildProductionBatchPayload(
        group, nGroup, isoTs, identity::deviceCode(),
        perPinBuf, sizeof(perPinBuf));
    if (bodyLen == 0) {
      Serial.printf("[mqtt-ingest] battery %s payload build FAIL\n", serial);
      return false;
    }

    bool pubOk = net::mqttPublishTelemetry(serial, perPinBuf, bodyLen);
    Serial.printf("[mqtt-ingest] pub %s (%u readings, %u bytes) → %s\n",
                  serial, static_cast<unsigned>(nGroup),
                  static_cast<unsigned>(bodyLen),
                  pubOk ? "OK" : "FAIL");
    if (!pubOk) {
      // GH-740 — dừng tại đây, NHƯNG danh sách serial đã gửi vẫn được trả về để fallback
      // HTTPS bỏ qua chúng. Trước đây chỉ `return false` nên caller gửi lại cả batch.
      return false;
    }
    if (outPublished && outPublishedCount && *outPublishedCount < outCap) {
      outPublished[(*outPublishedCount)++] = serial;
    }
    okCount++;
  }
  return okCount == nUnique;
}

enum class QueuedMqttResult : uint8_t {
  NotTried,
  Published,
  Failed,
};

// Queue files contain the HTTPS batch shape. When MQTT recovers, split the
// exact saved items by battery serial and publish them on the per-battery
// telemetry topics. This keeps HTTPS as fallback instead of making an old
// queue depend on HTTPS forever after MQTT is healthy again.
QueuedMqttResult publishQueuedViaMqtt(const char* body, size_t bodyLen) {
  if (!net::mqttIsConnected() ||
      net::mqttConsecutiveFailCount() >= MQTT_PUBLISH_FAIL_THRESHOLD) {
    return QueuedMqttResult::NotTried;
  }
  if (!body || bodyLen == 0) return QueuedMqttResult::NotTried;

  JsonDocument source;
  const DeserializationError parseError = deserializeJson(source, body, bodyLen);
  if (parseError) {
    Serial.printf("[flush-mqtt] queued JSON parse failed: %s — use HTTPS fallback\n",
                  parseError.c_str());
    return QueuedMqttResult::NotTried;
  }

  const JsonArrayConst items = source["items"].as<JsonArrayConst>();
  if (items.isNull() || items.size() == 0) return QueuedMqttResult::NotTried;

  const char* serials[bms::kMockMaxBatteries] = {nullptr};
  size_t serialCount = 0;
  for (JsonObjectConst item : items) {
    const char* serial = item["batteryAssetSerial"] | "";
    if (serial[0] == '\0') {
      Serial.println("[flush-mqtt] item has no batteryAssetSerial — use HTTPS fallback");
      return QueuedMqttResult::NotTried;
    }

    bool seen = false;
    for (size_t i = 0; i < serialCount; ++i) {
      if (strcmp(serials[i], serial) == 0) { seen = true; break; }
    }
    if (!seen) {
      if (serialCount >= bms::kMockMaxBatteries) {
        Serial.println("[flush-mqtt] too many battery groups — use HTTPS fallback");
        return QueuedMqttResult::NotTried;
      }
      serials[serialCount++] = serial;
    }
  }

  static char perBatteryBuf[core::payloadBufferSize(kMaxReadings) + 128];
  for (size_t i = 0; i < serialCount; ++i) {
    JsonDocument groupDoc;
    JsonArray groupItems = groupDoc["items"].to<JsonArray>();
    for (JsonObjectConst item : items) {
      const char* itemSerial = item["batteryAssetSerial"] | "";
      if (strcmp(itemSerial, serials[i]) == 0) groupItems.add(item);
    }

    const size_t groupLen = serializeJson(groupDoc, perBatteryBuf, sizeof(perBatteryBuf));
    if (groupLen == 0 || groupLen >= sizeof(perBatteryBuf)) {
      Serial.printf("[flush-mqtt] payload build failed for %s — use HTTPS fallback\n",
                    serials[i]);
      return QueuedMqttResult::NotTried;
    }
    if (!net::mqttPublishTelemetry(serials[i], perBatteryBuf, groupLen)) {
      Serial.printf("[flush-mqtt] publish %s failed — use HTTPS fallback\n", serials[i]);
      return QueuedMqttResult::Failed;
    }
  }

  Serial.printf("[flush-mqtt] published %u battery group(s), %u item(s)\n",
                static_cast<unsigned>(serialCount),
                static_cast<unsigned>(items.size()));
  return QueuedMqttResult::Published;
}

// GH-737 — LẤY MẪU + XẾP HÀNG KHI KHÔNG CÓ MẠNG.
//
// Trước đây main loop mất Wi-Fi thì chỉ `s_failCount++`: không đọc BMS, không ghi gì vào
// hàng đợi. Hàng đợi CHỈ được nạp từ nhánh lỗi tạm thời của ingestOnce() — mà nhánh đó đòi
// phải online trước đã. Hệ quả: toàn bộ telemetry trong khoảng mất mạng biến mất, trái cam
// kết "không mất dữ liệu 5 phút" (README §88, overall.iot.md §403-413).
//
// Hàm này cố ý lặp lại các bước dựng payload của ingestOnce() thay vì tái cấu trúc hàm đó:
// ingestOnce() còn phải giữ `readings` cho đường MQTT, tách ra sẽ đụng cả nhánh đang chạy tốt.
//
// Mốc thời gian: đồng hồ hệ thống ESP32 vẫn chạy sau khi mất Wi-Fi (NTP chỉ ĐẶT giờ một lần),
// nên isoNow()/timeEpoch() vẫn đúng trong lúc offline. Chỉ trường hợp CHƯA TỪNG sync NTP mới
// không tạo được bản ghi hợp lệ — khi đó trả false và caller đếm fail.
bool sampleAndQueueOffline() {
  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) return false;

  core::SensorReading readings[kMaxReadings];
  const size_t nBat = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::bmsSourcePollAll(readings, nBat);
  if (n == 0) return false;

  const size_t bodyLen = core::buildProductionBatchPayload(
      readings, n, ts, identity::deviceCode(),
      s_payloadBuf, sizeof(s_payloadBuf));
  if (bodyLen == 0) return false;

  // Khoá idempotency sinh NGAY LÚC LẤY MẪU và đi cùng bản ghi vào hàng đợi: khi đẩy bù sau
  // reconnect, backend nhận đúng khoá đó nên gửi lại nhiều lần cũng không sinh bản ghi trùng.
  if (!core::generateIdempotencyKeyV4(s_idempBuf, sizeof(s_idempBuf))) return false;

  const uint32_t epoch = net::timeEpoch();
  if (epoch == 0) return false;

  // queueEnqueue dùng epoch giây làm tên file và tự tăng epoch khi trùng, nên
  // nhịp 1 giây hoặc một tick chạm đúng ranh giới vẫn không ghi đè batch cũ.
  return queue::queueEnqueue(epoch, s_payloadBuf, bodyLen, s_idempBuf);
}

bool ingestOnce() {
  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[ingest] NTP not synced — skip");
    return false;
  }

  // Sinh readings 1 lần (dùng chung cho cả MQTT + HTTPS fallback).
  // Sprint 5 (S5-FW-07): dispatch mock vs real BMS qua bms_source.
  core::SensorReading readings[kMaxReadings];
  const size_t nBat = MOCK_BATTERY_COUNT > bms::kMockMaxBatteries
                      ? bms::kMockMaxBatteries
                      : MOCK_BATTERY_COUNT;
  const size_t n = bms::bmsSourcePollAll(readings, nBat);
  if (n == 0) return false;

  // ---- Sprint 4 (S4-FW-04 + S4-FW-06): chọn transport ----
  // Ưu tiên MQTT trừ khi: chưa connect / streak fail ≥ threshold.
  // mqtt_client tự reset streak khi reconnect thành công.
  bool tryMqtt = net::mqttIsConnected() &&
                 (net::mqttConsecutiveFailCount() < MQTT_PUBLISH_FAIL_THRESHOLD);

  // GH-740 — serial đã publish qua MQTT, để loại khỏi fallback HTTPS.
  const char* mqttPublished[bms::kMockMaxBatteries] = {nullptr};
  size_t      mqttPublishedCount = 0;

  if (tryMqtt) {
    if (ingestViaMqtt(ts, nBat, readings, n,
                      mqttPublished, bms::kMockMaxBatteries, &mqttPublishedCount)) {
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
  // GH-740 — chỉ gửi phần MQTT CHƯA đẩy được. Publish một phần rồi fallback toàn bộ batch
  // là ghi trùng những nhóm đã vào backend qua MQTT (khoá idempotency HTTPS không cứu được,
  // vì bản ghi kia vào bằng đường khác với hình dạng payload khác).
  core::SensorReading remaining[kMaxReadings];
  const size_t nRemaining = core::filterOutPublished(
      readings, n, mqttPublished, mqttPublishedCount, remaining, kMaxReadings);

  if (nRemaining == 0) {
    Serial.println("[ingest] MQTT đã đẩy hết — không cần fallback HTTPS");
    return true;
  }
  if (nRemaining < n) {
    Serial.printf("[ingest] fallback HTTPS %u/%u reading (bỏ %u đã publish qua MQTT)\n",
                  static_cast<unsigned>(nRemaining), static_cast<unsigned>(n),
                  static_cast<unsigned>(n - nRemaining));
  }

  size_t bodyLen = core::buildProductionBatchPayload(
      remaining, nRemaining, ts, identity::deviceCode(),
      s_payloadBuf, sizeof(s_payloadBuf));
  if (bodyLen == 0) return false;

  // Sprint 3 (S3-FW-02): sinh Idempotency-Key UUIDv4 cho batch này.
  if (!core::generateIdempotencyKeyV4(s_idempBuf, sizeof(s_idempBuf))) {
    Serial.println("[ingest] FAIL: idempotency key gen");
    return false;
  }

  // Store-and-forward fallback: make the payload durable BEFORE the HTTP call.
  // tryFlushQueue() runs immediately after this ingest tick and removes the pair
  // only after a backend 2xx. During an outage, HTTP backoff no longer prevents
  // the one-second sampling path from continuing to append new measurements.
  const uint32_t epoch = net::timeEpoch();
  if (epoch != 0 && queue::queueEnqueue(epoch, s_payloadBuf, bodyLen, s_idempBuf)) {
    s_queuedCount++;
    Serial.printf("[ingest] MQTT unavailable → queued HTTPS fallback %u readings (depth=%u)\n",
                  static_cast<unsigned>(nRemaining),
                  static_cast<unsigned>(queue::queueSize()));
    return false;
  }

  // Emergency path when both SD and LittleFS are unavailable. Direct HTTP is
  // still safer than dropping a sample, but the log makes the loss of durable
  // retry semantics explicit.
  Serial.println("[ingest] queue unavailable — emergency direct HTTPS fallback");
  const PostOutcome outcome = postBatch(s_payloadBuf, bodyLen, s_idempBuf);
  if (outcome == PostOutcome::Success) {
    s_backoff.reset();
    return true;
  }
  if (outcome == PostOutcome::TransientFailure) s_backoff.recordFailure();
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

  const QueuedMqttResult mqttOutcome = publishQueuedViaMqtt(qBody, bodyLen);
  if (mqttOutcome == QueuedMqttResult::Published) {
    if (!queue::queueDelete(epoch)) {
      const uint32_t waitMs = s_backoff.recordFailure();
      Serial.printf("[flush-mqtt] publish accepted but local delete failed — retry after %lums\n",
                    static_cast<unsigned long>(waitMs));
      return;
    }
    s_flushedCount++;
    s_backendAcknowledged = true;
    s_backoff.reset();
    Serial.printf("[flush-mqtt] OK — queue depth=%u\n",
                  static_cast<unsigned>(queue::queueSize()));
    return;
  }
  if (mqttOutcome == QueuedMqttResult::Failed) {
    Serial.println("[flush] MQTT publish failed — falling back to HTTPS");
  }

  PostOutcome outcome = postBatch(qBody, bodyLen, qIdem[0] ? qIdem : nullptr);
  if (outcome == PostOutcome::Success) {
    if (!queue::queueDelete(epoch)) {
      const uint32_t waitMs = s_backoff.recordFailure();
      Serial.printf("[flush] backend ACKed but local delete failed — retry idem after %lums\n",
                    static_cast<unsigned long>(waitMs));
      return;
    }
    s_flushedCount++;
    s_backoff.reset();
    Serial.printf("[flush] OK — queue depth=%u\n",
                  static_cast<unsigned>(queue::queueSize()));
    return;
  }
  if (outcome == PostOutcome::PermanentFailure) {
    // 4xx → data sai, retry vô ích → drop khỏi queue + reset backoff để các batch sau retry bình thường.
    if (!queue::queueDelete(epoch)) {
      const uint32_t waitMs = s_backoff.recordFailure();
      Serial.printf("[flush] permanent 4xx but local delete failed — retry cleanup after %lums\n",
                    static_cast<unsigned long>(waitMs));
      return;
    }
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
  if (!deviceIdentityReady()) {
    ui::ledSet(ui::LedState::Setup);
    return;
  }
  // IOT3-54 — thứ tự xét đi từ "cần người can thiệp nhất" xuống "để yên được".
  // Trạng thái MẠNG phải đứng trên trạng thái hàng đợi: chưa có mạng thì hàng đợi đầy là hệ quả,
  // không phải nguyên nhân, mà đèn chỉ nói được MỘT điều nên phải nói cái gốc.
  switch (net::wifiPhase()) {
    case net::WifiPhase::Unconfigured:
      ui::ledSet(ui::LedState::Setup);          // tím nháy — mở trang cài đặt lên đi
      return;
    case net::WifiPhase::Recovery:
      ui::ledSet(ui::LedState::Recovery);       // tím/cam xen kẽ — mất mạng lâu, AP đang bật
      return;
    case net::WifiPhase::Connecting:
      ui::ledSet(ui::LedState::WifiSearching);  // cam — đang tìm mạng
      return;
    case net::WifiPhase::Connected:
      break;
  }

  if (!s_backendAcknowledged && !net::mqttIsConnected()) {
    // MQTT là đường telemetry chính. HTTP heartbeat production có thể lỗi tạm thời
    // trong khi broker vẫn nhận dữ liệu đều; trường hợp đó thiết bị vẫn online.
    // Chỉ báo offline khi cả MQTT lẫn lần xác nhận backend gần nhất đều không có.
    // Chưa provision: tím để người cài đặt biết thiết bị vẫn đang ghép backend.
    ui::ledSet(s_provisionDone ? ui::LedState::Offline : ui::LedState::Provisioning);
    return;
  }

  if (queue::queueSize() > 0) {
    ui::ledSet(ui::LedState::Queued);           // xanh nháy — còn hàng đợi
  } else {
    ui::ledSet(ui::LedState::Online);           // xanh đều
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
                "heap=%u rssi=%ddBm wifi=%s prov=%s / "
                "cfg wifi=%s mqtt=%s batmap=%s(%u pin)\n",
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
                s_provisionDone ? "yes" : "no",
                // IOT3-47 — nguồn cấu hình. Câu hỏi đầu tiên khi thiết bị "nối sai chỗ" luôn là
                // "nó đang đọc NVS hay đang đọc bản build?", trước đây phải đoán.
                wificfg::isFromNvs() ? "nvs" : "compile",
                mqttcfg::isFromNvs() ? "nvs" : "compile",
                batmap::isFromNvs()  ? "nvs" : "compile",
                static_cast<unsigned>(batmap::count()));

  // Sprint 5 — sensor stats (chỉ relevant nếu USE_MOCK_BMS=0; mock mode counters luôn 0).
#if !USE_MOCK_BMS
  Serial.printf("[s5-stats] mode=%s modbus ok=%lu fail=%lu / ina226 ok=%lu fail=%lu / "
                "ds18b20 ok=%lu fail=%lu / sht31 post ok=%lu fail=%lu\n",
                bms::bmsSourceMode(),
                static_cast<unsigned long>(bms::modbusPollOkCount()),
                static_cast<unsigned long>(bms::modbusPollFailCount()),
                static_cast<unsigned long>(sensor::ina226ReadOkCount()),
                static_cast<unsigned long>(sensor::ina226ReadFailCount()),
                static_cast<unsigned long>(sensor::ds18b20ReadOkCount()),
                static_cast<unsigned long>(sensor::ds18b20ReadFailCount()),
                static_cast<unsigned long>(sensor::sht31PostOkCount()),
                static_cast<unsigned long>(sensor::sht31PostFailCount()));
#endif

  // Sprint 6 (S6-FW-01/02): environmental incident sensors (chạy cả mock + real mode).
  Serial.printf("[s6-env] mq2 raw=%lu reports=%lu / water wet=%s reports=%lu / "
                "incident http ok=%lu fail=%lu\n",
                static_cast<unsigned long>(sensor::mq2LastRaw()),
                static_cast<unsigned long>(sensor::mq2ReportCount()),
                sensor::waterLeakIsWet() ? "yes" : "no",
                static_cast<unsigned long>(sensor::waterLeakReportCount()),
                static_cast<unsigned long>(sensor::envIncidentReportOkCount()),
                static_cast<unsigned long>(sensor::envIncidentReportFailCount()));

  // Sprint 7 (S7-FW-01/02): OTA status.
  Serial.printf("[s7-ota] verify_mode=%s checks=%lu updates_ok=%lu\n",
                ota::otaInVerifyMode() ? "yes" : "no",
                static_cast<unsigned long>(ota::otaCheckCount()),
                static_cast<unsigned long>(ota::otaUpdateOkCount()));
}

void logMemoryLayoutOnce() {
  Serial.printf("[memory] flash=%uKB sketch=%uKB ota-free=%uKB / "
                "heap-free=%u/%uKB psram-free=%u/%uKB / "
                "queue=%s used=%llu/%lluKB\n",
                static_cast<unsigned>(ESP.getFlashChipSize() / 1024U),
                static_cast<unsigned>(ESP.getSketchSize() / 1024U),
                static_cast<unsigned>(ESP.getFreeSketchSpace() / 1024U),
                static_cast<unsigned>(ESP.getFreeHeap() / 1024U),
                static_cast<unsigned>(ESP.getHeapSize() / 1024U),
                static_cast<unsigned>(ESP.getFreePsram() / 1024U),
                static_cast<unsigned>(ESP.getPsramSize() / 1024U),
                queue::queueStorageName(),
                queue::queueStorageUsedBytes() / 1024ULL,
                queue::queueStorageTotalBytes() / 1024ULL);
}

}  // namespace

void appTask(void* pv);

void setup() {
  Serial.begin(SERIAL_BAUD);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  // NVS-backed settings are loaded below before Wi-Fi, HTTP and MQTT start.

  ui::ledBegin();
  ui::ledSet(ui::LedState::Offline);

  // IOT3-45 — NVS phải mở TRƯỚC WiFi: từ sprint này SSID/mật khẩu, broker MQTT và bảng pin đều
  // nằm trong NVS, còn macro trong config.h chỉ là đường lui. Giữ thứ tự cũ (WiFi trước NVS) thì
  // lần boot nào cũng nối bằng mạng compile-time rồi mới biết NVS có mạng khác.
  storage::nvsBegin();
  identity::identityBegin();      // deviceCode — mqttcfg cần để suy tiền tố topic dự phòng
  wificfg::begin();               // IOT3-36
  mqttcfg::begin();               // IOT3-37
  batmap::begin();                // IOT3-49
  runtimecfg::runtimeConfigBegin();

  printBanner();

  net::portalStart(net::PortalMode::AlwaysAvailable);
  portal::setupPortalBegin();     // authenticated AP/LAN app on port 8080
  net::wifiBegin();               // connect STA in background; setup AP stays available
  net::timeSyncBegin();
  provision::loadProvisioned(s_provCfg);
  if (s_provCfg.provisioned) {
    Serial.printf("[main] loaded provisioned config: polling=%lums hb=%lums site=%s ntp=%s\n",
                  static_cast<unsigned long>(s_provCfg.pollingIntervalMs),
                  static_cast<unsigned long>(s_provCfg.heartbeatIntervalMs),
                  s_provCfg.siteId, s_provCfg.ntpServer);
    s_provisionDone = true;
  }

  net::httpClientBegin();

  // Sprint 7 (S7-FW-01/02): OTA — gọi SỚM (sau nvsBegin+identityBegin+httpClientBegin,
  // TRƯỚC các init rủi ro bms/sensor/mqtt). Lý do: boot-counter self-healing phải
  // tăng được ngay cả khi FW mới crash trong các init đó → mới rollback được (B2).
  // Nếu vừa boot sau OTA → verify-mode; nếu boot-loop → rollback ngay (reboot).
  ota::otaBegin();

  // Sprint 5 (S5-FW-07): bms_source init thay cho mockBegin trực tiếp.
  // Mock mode → delegate mock_bms; Real mode → init Modbus + INA226 + DS18B20 + SHT31.
  bms::bmsSourceBegin();
  Serial.printf("[setup] BMS source mode: %s\n", bms::bmsSourceMode());

  // Sprint 5 (S5-FW-06): wire siteId vào SHT31 — backend AmbientReadingItem.SiteId required.
  // Nếu chưa provision (s_provCfg.siteId rỗng), sht31PostNow tự skip cho đến khi
  // provision flow xong + main.cpp re-set qua sht31SetSiteId.
  if (s_provCfg.provisioned) {
    sensor::sht31SetSiteId(s_provCfg.siteId);
  }

  // Sprint 6 (S6-FW-01/02): environmental sensors MQ-2 (khói) + water leak.
  // Độc lập USE_MOCK_BMS (cảm biến môi trường, không liên quan BMS). Begin luôn —
  // tự no-op nếu MQ2_ENABLED / WATER_LEAK_ENABLED = 0.
  sensor::mq2Begin();
  sensor::waterLeakBegin();
  // siteId dùng chung cho cả 2 reporter — set ngay nếu đã provisioned (loaded từ NVS),
  // nếu chưa thì ensureProvisioned() set sau khi provision flow xong.
  if (s_provCfg.provisioned) {
    sensor::envIncidentSetSiteId(s_provCfg.siteId);
  }

  telemetry::heartbeatBegin(s_provCfg.heartbeatIntervalMs);
  cli::cliBegin();

  // Sprint 3
  queue::queueBegin();
  logMemoryLayoutOnce();

  // Sprint 4 (S4-FW-01/02/03/05): MQTT broker client + downlink handler.
  // mqttBegin() load CA cert từ LittleFS (queue đã mount LittleFS lúc queueBegin
  // — share an toàn vì LittleFS API idempotent).
  // IOT3-40 — đăng ký handler downlink VÔ ĐIỀU KIỆN. Từ sprint này `mqttBegin()` trả false ở
  // trạng thái hoàn toàn bình thường "chưa provision", và MQTT sẽ lên sau đó qua
  // `mqttApplyConfig()`. Cột handler vào nhánh thành công như trước thì thiết bị nối được broker
  // nhưng câm lệnh downlink cho tới lần khởi động lại — mà chẳng có log nào nói vì sao.
  cmd::handlerBegin();
  // S4-FW-05: wire polling setter — cmd `set_interval` cập nhật s_provCfg.
  cmd::setPollingHandler([](uint32_t newMs) -> bool {
    s_provCfg.pollingIntervalMs = newMs;
    Serial.printf("[main] polling interval đã đổi runtime → %lums\n",
                  static_cast<unsigned long>(newMs));
    return true;
  });
  cmd::setBmsSwitchHandler([](const char* serial, uint8_t target, bool enable,
                              bool* outCharge, bool* outDischarge,
                              char* outError, size_t errorLen) -> bool {
#if USE_MOCK_BMS
    snprintf(outError, errorLen, "firmware dang chay che do mock - khong co bus BMS that");
    return false;
#else
    const uint8_t unitId = batmap::unitIdForSerial(serial);
    if (unitId == 0) {
      snprintf(outError, errorLen, "serial khong co trong bang anh xa pin");
      return false;
    }
    const auto result = bms::modbusWriteSwitch(
        unitId, static_cast<bms::SwitchTarget>(target), enable);
    if (outCharge) *outCharge = result.chargeEnabled;
    if (outDischarge) *outDischarge = result.dischargeEnabled;
    if (!result.ok) snprintf(outError, errorLen, "%s", result.error);
    return result.ok;
#endif
  });

  if (!net::mqttBegin()) {
    Serial.println("[setup] MQTT chưa khởi tạo — chạy HTTPS-only (S4-FW-06 fallback)");
  }

  // loopTask của Arduino chỉ có 8KB stack — TLS handshake cộng HTTPClient khi
  // POST batch làm chạm watchpoint cuối stack, chip panic "Unhandled debug
  // exception" ngay sau heartbeat. CONFIG_ARDUINO_LOOP_STACK_SIZE nằm trong
  // sdkconfig đã biên dịch sẵn nên build_flags không đổi được. Giải pháp: chạy
  // toàn bộ thân vòng lặp trong task riêng có stack 16KB.
  xTaskCreatePinnedToCore(appTask, "appLoop", 16384, nullptr, 1, nullptr, 1);

  Serial.println("[setup] done — entering loop()");
}

void appLoopBody() {
  net::wifiTick();
  net::timeSyncTick();
  cli::cliTick();
  ui::ledTick();                 // IOT3-54 — bơm hiệu ứng nháy
  ensureProvisioned();

  // QR mới xóa cờ provision để lấy lại credential MQTT. Không thử credential
  // cũ trong thời gian này: một TLS connect lỗi có thể chặn appTask nhiều giây
  // và làm chậm chính request /provision cần để tự phục hồi.
  if (s_provisionDone && deviceIdentityReady()) {
    net::mqttTick();
    checkMqttCredentialHealth();
  }

  const uint32_t now = millis();
  const uint32_t configuredPollInterval = s_provCfg.provisioned
      ? s_provCfg.pollingIntervalMs
      : INGEST_INTERVAL_MS;
  // This production build targets one-second telemetry even when an older
  // backend device record still contains the historical 5s/10s default.
  const uint32_t pollInterval = configuredPollInterval > INGEST_INTERVAL_MS
      ? INGEST_INTERVAL_MS
      : configuredPollInterval;

  if (s_provisionDone && now - s_lastIngestMs >= pollInterval) {
    s_lastIngestMs = now;

    // GH-737 — quyết định tách sang core::ingestAction() (hàm thuần, test ở env:native).
    switch (core::ingestAction(net::wifiIsConnected(), net::timeIsSynced())) {
    case core::IngestAction::PostOnline:
      if (ingestOnce()) {
        s_okCount++;
        s_backendAcknowledged = true;
      } else {
        s_failCount++;
      }
      break;

    case core::IngestAction::QueueOffline: {
      // GH-737 — mất mạng nhưng đồng hồ vẫn chạy: VẪN đọc BMS và xếp hàng, để đẩy bù
      // sau khi có mạng lại. Trước đây nhánh này chỉ tăng bộ đếm lỗi ⇒ mất trắng dữ liệu.
      if (sampleAndQueueOffline()) {
        s_queuedCount++;
        Serial.printf("[ingest] offline → queued (depth=%u)\n",
                      static_cast<unsigned>(queue::queueSize()));
      } else {
        s_failCount++;
      }
      break;
    }

    case core::IngestAction::SkipNoClock:
      // Chưa từng sync NTP ⇒ không có mốc thời gian hợp lệ để ghi bản ghi.
      s_failCount++;
      break;
    }
  }

  // Sprint 3: flush queue khi backoff allow
  if (s_provisionDone) tryFlushQueue();

  // Sprint 7 (S7-FW-01/02): OTA tick — UNCONDITIONAL (verify-mode cần chạy cả khi
  // offline để bắt deadline rollback). Network ops tự gate bên trong.
  ota::otaTick();

  // Sprint 2: heartbeat
  if (s_provisionDone && net::wifiIsConnected() && net::timeIsSynced()) {
    const uint32_t okBefore = telemetry::heartbeatOkCount();
    const uint32_t failBefore = telemetry::heartbeatFailCount();
    telemetry::heartbeatTick();
    if (telemetry::heartbeatOkCount() != okBefore) {
      s_backendAcknowledged = true;
    } else if (telemetry::heartbeatFailCount() != failBefore && !net::mqttIsConnected()) {
      s_backendAcknowledged = false;
    }
  }

  // Sprint 5 (S5-FW-06): SHT31 ambient post mỗi SHT31_POLL_INTERVAL_MS (60s default).
  // Tự skip nếu USE_MOCK_BMS=1 (sht31Begin() chưa init nên Tick no-op).
  if (net::wifiIsConnected() && net::timeIsSynced()) {
    // SHT31 là cảm biến MÔI TRƯỜNG (nhiệt/ẩm) — chỉ để báo cáo, gate theo mạng là hợp lý.
    sensor::sht31Tick();
  }

  // GH-736 — CẢM BIẾN AN TOÀN CHẠY VÔ ĐIỀU KIỆN.
  //
  // Trước đây MQ-2 và rò nước bị gate chung với SHT31 ("gate online giống sht31"). Hệ quả:
  // xung khí/nước xảy ra trong lúc mất Wi-Fi hoặc NTP chưa sync sẽ KHÔNG được lấy mẫu, và
  // nếu điều kiện hết trước khi có mạng lại thì sự cố biến mất không dấu vết. Mất mạng không
  // làm pin bớt cháy — gộp cảm biến an toàn chung với cảm biến báo cáo là biến sự cố mạng
  // thành sự cố an toàn.
  //
  // An toàn khi gỡ gate: envIncidentReport() tự trả false NGAY (không gọi mạng, không chặn)
  // khi thiếu siteId/NTP, và cả hai cảm biến đã có sẵn cơ chế latch `s_pendingReport` để thử
  // lại ở tick sau. Nên offline vẫn lấy mẫu + chốt sự cố, có mạng thì đẩy đi.
  sensor::mq2Tick();
  sensor::waterLeakTick();

  updateStatusLed();
  logStatsPeriodic();
}

void appTask(void* pv) {
  (void)pv;
  for (;;) {
    appLoopBody();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Keep WebServer on Arduino's loopTask (the same task that created it in setup).
// Network ingest remains on appTask because TLS needs the larger 16 KB stack.
void loop() {
  portal::setupPortalTick();
  vTaskDelay(pdMS_TO_TICKS(10));
}
