// ==================================================================
// Sprint 2 — S2-FW-02: Provision flow implementation
// ==================================================================
#include "provision/provision.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#include "config/nvs_store.h"
#include "net/http_client.h"
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

  // 2) POST với response buffer 2KB (configJson có thể chứa BatteryMappings list)
  char respBuf[2048];
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

  // 6) Log theo S2-FW-02 AC: "provisioned, polling=5s"
  Serial.printf("[provision] provisioned, polling=%ds, heartbeat=%ds, site=%s, ntp=%s\n",
                pollSec, hbSec, siteId, ntpServer);
  return true;
}

bool clearProvisionFlag() {
  return storage::nvsPutUInt8(kKeyProvd, 0);
}

}  // namespace provision
