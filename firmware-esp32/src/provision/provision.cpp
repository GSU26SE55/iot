// ==================================================================
// Sprint 2 — S2-FW-02: Provision flow implementation
// ==================================================================
#include "provision/provision.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#include "config/battery_map_runtime.h"
#include "config/mqtt_config.h"
#include "config/nvs_store.h"
#include "core/source_tags.h"
#include "net/http_client.h"
#include "net/mqtt_client.h"
#include "net/time_sync.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

namespace provision {

namespace {
constexpr const char* kEndpoint     = "/api/iot-devices/provision";
constexpr const char* kKeyProvd     = "provd";
constexpr const char* kKeySiteId    = "siteid";
constexpr const char* kKeyPollMs    = "pollIntS";
constexpr const char* kKeyHbMs      = "hbIntS";
constexpr const char* kKeyNtp       = "ntpsv";
constexpr const char* kKeyFwVer     = "fwver";

void copySafe(char* dst, size_t dstLen, const char* src) {
  if (dst == nullptr || dstLen == 0) return;
  if (src == nullptr) { dst[0] = '\0'; return; }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

/// <summary>IOT3-42 — nhận sáu trường MQTT trong `data` và áp vào `mqttcfg`.</summary>
/// <remarks>
/// Backend cam kết trả "cả sáu trường hoặc không trường nào" (`IotDeviceProvisionResultDto`),
/// nên `mqttBrokerHost` rỗng/null là cờ duy nhất cần xét: nghĩa là `Mqtt:Enabled=false`.
/// Trường hợp đó KHÔNG ghi đè NVS — thiết bị đã từng được cấp credential rồi mà backend tạm tắt
/// MQTT thì xoá đi là lần bật lại phải provision thủ công.
/// </remarks>
void applyMqttFromProvision(JsonObject data) {
  const char* brokerHost = data["mqttBrokerHost"] | "";
  if (brokerHost[0] == '\0') {
    Serial.println("[provision] MQTT chưa bật ở backend — HTTPS-only (giữ nguyên cấu hình cũ)");
    return;
  }

  const int   brokerPort = data["mqttBrokerPort"]  | 0;
  const bool  useTls     = data["mqttUseTls"]      | false;
  const char* prefix     = data["mqttTopicPrefix"] | "";
  const char* user       = data["mqttUsername"]    | "";
  const char* pass       = data["mqttPassword"]    | "";

  if (!mqttcfg::applyFromProvision(brokerHost, brokerPort, useTls,
                                   prefix[0] == '\0' ? nullptr : prefix, user, pass)) {
    // Không đổi (đã đúng sẵn) hoặc bị từ chối — `applyFromProvision` đã log lý do.
    // Vẫn phải cho `mqttApplyConfig()` chạy MỘT lần: nhánh này gặp cả ở boot đã-provision, khi
    // MQTT chưa hề được khởi tạo. Hàm đó tự nhận biết và chỉ init nếu chưa init.
    net::mqttApplyConfig();
    return;
  }
  net::mqttApplyConfig();
}

/// <summary>IOT3-49 — nhận `batteryMappings[]` và ghi vào bảng runtime.</summary>
void applyBatteryMappingsFromProvision(JsonObject data) {
  JsonArray arr = data["batteryMappings"].as<JsonArray>();
  if (arr.isNull()) {
    Serial.println("[provision] response không có batteryMappings[] — giữ bảng pin đang dùng");
    return;
  }

  core::BatteryMapEntry entries[core::kMaxBatteryMapEntries];
  size_t n = 0;
  size_t skipped = 0;
  for (JsonObject item : arr) {
    if (n >= core::kMaxBatteryMapEntries) { ++skipped; continue; }

    const char* serial = item["batteryAssetSerial"] | "";
    const int   unitId = item["unitId"]             | 0;
    const char* code   = item["sensorSourceCode"]   | core::kSourceCodePrimary;

    const auto err = core::validateBatteryMapEntry(serial, unitId, code);
    if (err != core::BatteryMapError::Ok) {
      Serial.printf("[provision] BỎ mapping serial='%s' unitId=%d — %s\n",
                    serial, unitId, core::describeBatteryMapError(err));
      ++skipped;
      continue;
    }

    core::BatteryMapEntry& e = entries[n++];
    copySafe(e.serial, sizeof(e.serial), serial);
    copySafe(e.sensorSourceCode, sizeof(e.sensorSourceCode), code);
    e.unitId = static_cast<uint8_t>(unitId);
  }

  if (skipped > 0) {
    Serial.printf("[provision] ⚠ %u mapping bị BỎ QUA — số liệu của những pin đó sẽ KHÔNG được gửi\n",
                  static_cast<unsigned>(skipped));
  }
  batmap::applyFromProvision(entries, n);
}
}  // namespace

void loadProvisioned(ProvisionedConfig& cfg) {
  cfg.provisioned        = (storage::nvsGetUInt8(kKeyProvd, 0) == 1);
  cfg.pollingIntervalMs  = static_cast<uint32_t>(
      storage::nvsGetInt32(kKeyPollMs, static_cast<int32_t>(kDefaultPollingMs)));
  cfg.heartbeatIntervalMs = static_cast<uint32_t>(
      storage::nvsGetInt32(kKeyHbMs, static_cast<int32_t>(kDefaultHeartbeatMs)));

  // Defensive bounds — nếu NVS corrupt (0 hoặc cực lớn) → fallback default.
  if (cfg.pollingIntervalMs < 1000 || cfg.pollingIntervalMs > 600000) {
    cfg.pollingIntervalMs = kDefaultPollingMs;
  }
  if (cfg.heartbeatIntervalMs < 10000 || cfg.heartbeatIntervalMs > 3600000) {
    cfg.heartbeatIntervalMs = kDefaultHeartbeatMs;
  }

  // siteId — NVS hoặc rỗng
  char buf[64];
  if (storage::nvsGetString(kKeySiteId, buf, sizeof(buf))) {
    copySafe(cfg.siteId, sizeof(cfg.siteId), buf);
  } else {
    cfg.siteId[0] = '\0';
  }
  if (storage::nvsGetString(kKeyNtp, buf, sizeof(buf))) {
    copySafe(cfg.ntpServer, sizeof(cfg.ntpServer), buf);
  } else {
    copySafe(cfg.ntpServer, sizeof(cfg.ntpServer), kDefaultNtpServer);
  }
}

bool runProvisionFlow(const char* firmwareVersion,
                      const char* hardwareRevision,
                      ProvisionedConfig& outConfig) {
  // 0) Pre-check: NTP synced (backend §52.3 reject nếu DeviceTimestamp lệch > 5 phút)
  char tsBuf[24];
  if (!net::isoNow(tsBuf, sizeof(tsBuf))) {
    Serial.println("[provision] FAIL: NTP chưa sync — skip provision");
    return false;
  }

  // 1) Build request body
  JsonDocument doc;
  doc["FirmwareVersion"]  = firmwareVersion;
  doc["HardwareRevision"] = hardwareRevision;
  doc["DeviceTimestamp"]  = tsBuf;

  char body[256];
  size_t bodyLen = serializeJson(doc, body, sizeof(body));
  if (bodyLen == 0 || bodyLen >= sizeof(body)) {
    Serial.println("[provision] FAIL: build body");
    return false;
  }

  // 2) POST với response buffer 4KB.
  //
  // IOT3-43 — nâng từ 2048. `batteryMappings[]` + `supportedSensors[]` đã ăn gần hết 2KB cũ; sáu
  // trường MQTT (host/port/tls/prefix/user/pass) thêm ~200 byte nữa là tràn. Tràn ở đây KHÔNG nổ
  // ầm ĩ: `httpPostJsonRecv` cắt cụt, JSON thành sai cú pháp, và thông báo duy nhất là
  // "[provision] FAIL: parse JSON IncompleteInput" — nhìn hệt như lỗi mạng.
  char respBuf[4096];
  size_t respLen = 0;
  Serial.printf("[provision] POST %s body=%u bytes\n",
                kEndpoint, static_cast<unsigned>(bodyLen));
  net::PostResult res = net::httpPostJsonRecv(kEndpoint, body, bodyLen,
                                              respBuf, sizeof(respBuf), &respLen);

  if (res.httpCode < 200 || res.httpCode >= 300) {
    Serial.printf("[provision] FAIL: HTTP %d — %s\n", res.httpCode, res.responseSnippet);
    return false;
  }

  // 3) Parse response — schema CommonResponse<IotDeviceProvisionResultDto>:
  //    { isSuccess, statusCode, data: { deviceId, deviceCode, siteId,
  //      heartbeatIntervalSeconds, pollingIntervalSeconds, ntpServer,
  //      batteryMappings[], supportedSensors[], ... } }
  JsonDocument respDoc;
  DeserializationError err = deserializeJson(respDoc, respBuf, respLen);
  if (err) {
    Serial.printf("[provision] FAIL: parse JSON %s\n", err.c_str());
    return false;
  }

  if (!respDoc["isSuccess"].as<bool>()) {
    const char* msg = respDoc["message"] | "unknown";
    Serial.printf("[provision] FAIL: backend isSuccess=false msg=%s\n", msg);
    return false;
  }

  JsonObject data = respDoc["data"].as<JsonObject>();
  if (data.isNull()) {
    Serial.println("[provision] FAIL: response không có data");
    return false;
  }

  int pollSec = data["pollingIntervalSeconds"] | 5;
  int hbSec   = data["heartbeatIntervalSeconds"] | 60;
  const char* siteId    = data["siteId"]    | "";
  const char* ntpServer = data["ntpServer"] | kDefaultNtpServer;

  // Sanity bounds
  if (pollSec < 1)   pollSec = 5;
  if (pollSec > 600) pollSec = 600;
  if (hbSec < 10)    hbSec = 60;
  if (hbSec > 3600)  hbSec = 3600;

  // 4) Lưu NVS
  storage::nvsPutInt32 (kKeyPollMs,  pollSec * 1000);
  storage::nvsPutInt32 (kKeyHbMs,    hbSec * 1000);
  storage::nvsPutString(kKeySiteId,  siteId);
  storage::nvsPutString(kKeyNtp,     ntpServer);
  storage::nvsPutString(kKeyFwVer,   firmwareVersion);
  storage::nvsPutUInt8 (kKeyProvd,   1);

  // 5) Fill output config
  outConfig.provisioned         = true;
  outConfig.pollingIntervalMs   = pollSec * 1000;
  outConfig.heartbeatIntervalMs = hbSec * 1000;
  copySafe(outConfig.siteId,    sizeof(outConfig.siteId),    siteId);
  copySafe(outConfig.ntpServer, sizeof(outConfig.ntpServer), ntpServer);

  // 6) IOT3-42 — sáu trường MQTT. Backend cam kết "cả sáu hoặc không trường nào"
  //    (IotDeviceProvisionResultDto), nên chỉ cần lấy `mqttBrokerHost` làm cờ.
  applyMqttFromProvision(data);

  // 7) IOT3-49 — bảng ánh xạ pin. Backend đã trả từ IoT-2 nhưng trước đây bị bỏ qua hoàn toàn.
  applyBatteryMappingsFromProvision(data);

  // 8) Log theo S2-FW-02 AC: "provisioned, polling=5s"
  Serial.printf("[provision] provisioned, polling=%ds, heartbeat=%ds, site=%s, ntp=%s\n",
                pollSec, hbSec, siteId, ntpServer);
  return true;
}

bool clearProvisionFlag() {
  return storage::nvsPutUInt8(kKeyProvd, 0);
}

}  // namespace provision
