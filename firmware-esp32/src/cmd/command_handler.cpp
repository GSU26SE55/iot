// ==================================================================
// Sprint 4 — S4-FW-05 (#44): command_handler implementation
// ==================================================================
#include "cmd/command_handler.h"

#include "cmd/cmd_logic.h"
#include "net/mqtt_client.h"
#include "telemetry/heartbeat.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

namespace cmd {

namespace {

SetPollingHandler s_setPollingHandler = nullptr;

uint32_t s_cmdReceived = 0;
uint32_t s_ackOk       = 0;
uint32_t s_ackFailed   = 0;
uint32_t s_unknownType = 0;

// ---- Ack builders ----
// Buffer đủ chứa ack ngắn (cmdId ≤ 64 + status + error ≤ 120) — 256 dư.
constexpr size_t kAckBufLen = 256;

void publishAck(const char* cmdId, const char* status, const char* errOrNull) {
  char ack[kAckBufLen];
  JsonDocument doc;
  doc["cmdId"]  = cmdId  ? cmdId  : "";
  doc["status"] = status ? status : "unknown";
  if (errOrNull && errOrNull[0] != '\0') doc["error"] = errOrNull;
  size_t n = serializeJson(doc, ack, sizeof(ack));

  if (n == 0) {
    // Buffer overflow (kAckBufLen=256). Fallback: gửi minimal ack KHÔNG error
    // để backend ít nhất biết cmdId đã được handle. Truncate cmdId ở 32 char.
    Serial.printf("[cmd] ack serialize overflow (cmdId=%zu, err=%zu) — fallback minimal\n",
                  cmdId ? strlen(cmdId) : 0,
                  errOrNull ? strlen(errOrNull) : 0);
    n = snprintf(ack, sizeof(ack),
                 "{\"cmdId\":\"%.32s\",\"status\":\"%s\"}",
                 cmdId  ? cmdId  : "",
                 status ? status : "unknown");
    if (n == 0 || n >= sizeof(ack)) {
      Serial.println("[cmd] fallback ack ALSO overflow — drop");
      s_ackFailed++;
      return;
    }
  }

  bool ok = net::mqttPublishCmdAck(ack, n);
  Serial.printf("[cmd] ack pub %s: %.*s\n", ok ? "OK" : "FAIL", static_cast<int>(n), ack);
  if (status && strcmp(status, "ok") == 0) {
    s_ackOk++;
  } else {
    s_ackFailed++;
  }
}

// ---- Dispatch handlers (side-effect wrappers — logic pure ở cmd_logic.h) ----

void handleSetInterval(const logic::ParsedCommand& parsed) {
  if (!parsed.hasPollingSeconds) {
    publishAck(parsed.cmdId, "failed", "missing pollingSeconds");
    return;
  }
  if (!logic::isValidPollingSeconds(parsed.pollingSeconds)) {
    publishAck(parsed.cmdId, "failed", "pollingSeconds out of range [1, 3600]");
    return;
  }
  if (!s_setPollingHandler) {
    publishAck(parsed.cmdId, "failed", "no polling handler registered");
    return;
  }
  uint32_t newMs = parsed.pollingSeconds * 1000UL;
  bool ok = s_setPollingHandler(newMs);
  Serial.printf("[cmd] set_interval → %lums (handler %s)\n",
                static_cast<unsigned long>(newMs), ok ? "OK" : "FAIL");
  publishAck(parsed.cmdId, ok ? "ok" : "failed",
             ok ? nullptr : "handler rejected new interval");
}

void handleRequestHeartbeat(const logic::ParsedCommand& parsed) {
  bool ok = telemetry::heartbeatSendNow();
  Serial.printf("[cmd] request_heartbeat → %s\n", ok ? "OK" : "FAIL");
  publishAck(parsed.cmdId, ok ? "ok" : "failed",
             ok ? nullptr : "heartbeat POST failed");
}

void handleTriggerOta(const logic::ParsedCommand& parsed) {
  // S7 sẽ implement esp_https_ota; Sprint 4 chỉ stub + ack.
  Serial.printf("[cmd] trigger_ota PLACEHOLDER — Sprint 7 sẽ implement (cmdId=%s)\n",
                parsed.cmdId);
  publishAck(parsed.cmdId, "ok", "ota scheduled (Sprint 7 placeholder)");
}

// ---- Main dispatcher ----

void onCommandPayload(const char* payload, size_t len) {
  s_cmdReceived++;

  // Pure parse — KHÔNG side effect.
  logic::ParsedCommand parsed = logic::parseCommandPayload(payload, len);
  if (!parsed.ok) {
    Serial.printf("[cmd] parse FAIL: %s\n", parsed.parseError);
    publishAck(parsed.cmdId, "failed", parsed.parseError);
    return;
  }

  Serial.printf("[cmd] RX cmdId=%s type=%s kind=%d\n",
                parsed.cmdId, parsed.type, static_cast<int>(parsed.kind));

  switch (parsed.kind) {
    case logic::CommandKind::SetInterval:
      handleSetInterval(parsed); break;
    case logic::CommandKind::RequestHeartbeat:
      handleRequestHeartbeat(parsed); break;
    case logic::CommandKind::TriggerOta:
      handleTriggerOta(parsed); break;
    case logic::CommandKind::Unknown:
    default:
      s_unknownType++;
      Serial.printf("[cmd] UNKNOWN type=%s\n", parsed.type);
      publishAck(parsed.cmdId, "unknown", "unsupported command type");
      break;
  }
}

}  // namespace

void handlerBegin() {
  net::mqttSetCommandCallback(onCommandPayload);
  Serial.println("[cmd] handler registered — listening solar/{dev}/cmd");
}

void setPollingHandler(SetPollingHandler h) {
  s_setPollingHandler = h;
}

uint32_t cmdReceivedCount()    { return s_cmdReceived; }
uint32_t cmdAckOkCount()       { return s_ackOk; }
uint32_t cmdAckFailedCount()   { return s_ackFailed; }
uint32_t cmdUnknownTypeCount() { return s_unknownType; }

}  // namespace cmd
