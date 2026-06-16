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

  // params optional — chỉ parse pollingSeconds / pollingIntervalSeconds (cho set_interval).
  JsonVariantConst params = doc["params"];
  if (!params.isNull() && params.is<JsonObjectConst>()) {
    if (params["pollingSeconds"].is<uint32_t>()) {
      r.pollingSeconds = params["pollingSeconds"].as<uint32_t>();
      r.hasPollingSeconds = true;
    } else if (params["pollingIntervalSeconds"].is<uint32_t>()) {
      r.pollingSeconds = params["pollingIntervalSeconds"].as<uint32_t>();
      r.hasPollingSeconds = true;
    }
  }

  r.ok = true;
  return r;
}

}}  // namespace cmd::logic
