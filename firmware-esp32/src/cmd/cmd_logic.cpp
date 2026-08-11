// ==================================================================
// Sprint 4 — Pure decision logic implementation.
// Pure C++ + ArduinoJson — KHÔNG depend Arduino runtime, test native được.
// ==================================================================
#include "cmd/cmd_logic.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstring>

namespace cmd { namespace logic {

namespace {

// Case-insensitive equal — accept "set_interval" vs "set-interval" vs "SET_INTERVAL".
// Áp dụng cho cả 2 alternative (vd "set_interval" hoặc "set-interval").
bool matches(const char* t, const char* a, const char* b) {
  if (!t) return false;
  return (strcasecmp(t, a) == 0) || (strcasecmp(t, b) == 0);
}

// Sao chép field từ JsonVariant vào buffer fixed-size — null-terminate.
void copyField(const char* src, char* dst, size_t dstLen) {
  if (!src) { dst[0] = '\0'; return; }
  size_t i = 0;
  while (src[i] && i < dstLen - 1) { dst[i] = src[i]; ++i; }
  dst[i] = '\0';
}

}  // namespace

CommandKind classifyType(const char* type) {
  if (!type || type[0] == '\0') return CommandKind::Unknown;
  if (matches(type, "set_interval",      "set-interval"))      return CommandKind::SetInterval;
  if (matches(type, "request_heartbeat", "request-heartbeat")) return CommandKind::RequestHeartbeat;
  if (matches(type, "trigger_ota",       "trigger-ota"))       return CommandKind::TriggerOta;
  if (matches(type, "set_bms_switch",    "set-bms-switch"))    return CommandKind::SetBmsSwitch;
  return CommandKind::Unknown;
}

ParsedCommand parseCommandPayload(const char* json, size_t len) {
  ParsedCommand r{};
  r.kind = CommandKind::Unknown;

  if (!json || len == 0) {
    copyField("empty payload", r.parseError, sizeof(r.parseError));
    return r;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    copyField(err.c_str(), r.parseError, sizeof(r.parseError));
    return r;
  }

  const char* cmdId = doc["cmdId"] | "";
  const char* type  = doc["type"]  | "";
  copyField(cmdId, r.cmdId, sizeof(r.cmdId));
  copyField(type,  r.type,  sizeof(r.type));

  if (r.type[0] == '\0') {
    copyField("missing field: type", r.parseError, sizeof(r.parseError));
    return r;
  }

  r.kind = classifyType(r.type);

  // params optional — parse pollingSeconds / set_bms_switch params.
  JsonVariantConst params = doc["params"];
  if (!params.isNull() && params.is<JsonObjectConst>()) {
    if (params["pollingSeconds"].is<uint32_t>()) {
      r.pollingSeconds = params["pollingSeconds"].as<uint32_t>();
      r.hasPollingSeconds = true;
    } else if (params["pollingIntervalSeconds"].is<uint32_t>()) {
      r.pollingSeconds = params["pollingIntervalSeconds"].as<uint32_t>();
      r.hasPollingSeconds = true;
    }

    if (r.kind == CommandKind::SetBmsSwitch) {
      const char* serial = params["serial"] | params["batteryAssetSerial"] | "";
      copyField(serial, r.switchSerial, sizeof(r.switchSerial));

      if (params["target"].is<uint8_t>()) {
        r.switchTarget = params["target"].as<uint8_t>();
      } else if (params["target"].is<const char*>()) {
        const char* tStr = params["target"].as<const char*>();
        if (strcasecmp(tStr, "charge") == 0) r.switchTarget = 1;
        else if (strcasecmp(tStr, "discharge") == 0) r.switchTarget = 2;
      }

      // Enable: bool / int / string
      if (params["enable"].is<bool>()) {
        r.switchEnable = params["enable"].as<bool>();
      } else if (params["enable"].is<int>()) {
        r.switchEnable = params["enable"].as<int>() != 0;
      } else if (params["enable"].is<const char*>()) {
        const char* eStr = params["enable"].as<const char*>();
        r.switchEnable = (strcasecmp(eStr, "true") == 0 || strcasecmp(eStr, "on") == 0 || strcasecmp(eStr, "enable") == 0);
      }

      if (r.switchSerial[0] != '\0' &&
          (r.switchTarget == 1 || r.switchTarget == 2)) {
        r.hasSwitchParams = true;
      }
    }
  }

  r.ok = true;
  return r;
}

}}  // namespace cmd::logic
