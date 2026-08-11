// ==================================================================
// Sprint 4 — S4-FW-05 (#44): command_handler implementation
// ==================================================================
#include "cmd/command_handler.h"

#include "ota/ota_update.h"

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
BmsSwitchHandler s_bmsSwitchHandler = nullptr;

uint32_t s_cmdReceived = 0;
uint32_t s_ackOk       = 0;
uint32_t s_ackFailed   = 0;
uint32_t s_unknownType = 0;

// ---- Ack builders ----
// Buffer đủ chứa ack kèm object state (cmdId ≤ 64 + status + error ≤ 120).
constexpr size_t kAckBufLen = 320;

void publishAck(const char* cmdId, const char* status, const char* errOrNull,
                const char* serial = nullptr, const bool* charge = nullptr,
                const bool* discharge = nullptr) {
  char ack[kAckBufLen];
  JsonDocument doc;
  doc["cmdId"]  = cmdId  ? cmdId  : "";
  doc["status"] = status ? status : "unknown";
  if (errOrNull && errOrNull[0] != '\0') doc["error"] = errOrNull;
  if (serial && charge && discharge) {
    JsonObject state = doc["state"].to<JsonObject>();
    state["serial"] = serial;
    state["chargeEnabled"] = *charge;
    state["dischargeEnabled"] = *discharge;
  }
  size_t n = serializeJson(doc, ack, sizeof(ack));

  if (n == 0) {
    // Buffer overflow (kAckBufLen=320). Fallback: gửi minimal ack KHÔNG error
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
  if (ota::otaRequestCheck()) {
    publishAck(parsed.cmdId, "ok", "ota check scheduled");
    return;
  }
  publishAck(parsed.cmdId, "rejected", ota::otaLastRejectReason());
}

void handleSetBmsSwitch(const logic::ParsedCommand& parsed) {
  if (!parsed.hasSwitchParams) {
    publishAck(parsed.cmdId, "failed", "missing or invalid serial/target/enable");
    return;
  }
  if (!s_bmsSwitchHandler) {
    publishAck(parsed.cmdId, "failed", "no BMS switch handler registered");
    return;
  }

  bool charge = false;
  bool discharge = false;
  char error[64] = {0};
  const bool ok = s_bmsSwitchHandler(
      parsed.switchSerial, parsed.switchTarget, parsed.switchEnable,
      &charge, &discharge, error, sizeof(error));
  Serial.printf("[cmd] set_bms_switch serial=%s target=%u enable=%s -> %s\n",
                parsed.switchSerial, parsed.switchTarget,
                parsed.switchEnable ? "true" : "false", ok ? "OK" : "REJECTED");
  publishAck(parsed.cmdId, ok ? "ok" : "rejected",
             ok ? nullptr : error, parsed.switchSerial, &charge, &discharge);
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
    case logic::CommandKind::SetBmsSwitch:
      handleSetBmsSwitch(parsed); break;
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

void setBmsSwitchHandler(BmsSwitchHandler h) {
  s_bmsSwitchHandler = h;
}

uint32_t cmdReceivedCount()    { return s_cmdReceived; }
uint32_t cmdAckOkCount()       { return s_ackOk; }
uint32_t cmdAckFailedCount()   { return s_ackFailed; }
uint32_t cmdUnknownTypeCount() { return s_unknownType; }

}  // namespace cmd
