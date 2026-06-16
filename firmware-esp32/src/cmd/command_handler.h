// ==================================================================
// Sprint 4 — S4-FW-05 (#44): Downlink command handler
//
// Parse JSON downlink trên topic `solar/{deviceCode}/cmd`, dispatch theo
// `type`, publish ack lên `solar/{deviceCode}/cmd/ack`.
//
// Schema downlink (backend AdminIotDevicesController.SendCommand):
//   {
//     "cmdId":   "uuid-hex",
//     "type":    "set_interval | trigger_ota | request_heartbeat | ...",
//     "params":  { ... type-specific ... },
//     "issuedAt": "2026-06-16T..."
//   }
//
// Schema ack (overall.md §52.14 topic 5 — backend DispatchCommandAck log):
//   {
//     "cmdId":  "<echo>",
//     "status": "ok" | "failed" | "unknown",
//     "error":  "<reason>"        // optional, chỉ khi failed
//   }
//
// Supported types (S4-FW-05 spec — backward compat 2 cách viết - và _):
//   - "set_interval"      / "set-interval"      — params.pollingSeconds
//   - "trigger_ota"       / "trigger-ota"       — Sprint 7 placeholder, ack "ok" + log
//   - "request_heartbeat" / "request-heartbeat" — force heartbeat now
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace cmd {

// Handler callback main wire vào. Trả true nếu apply thành công.
using SetPollingHandler = bool (*)(uint32_t newPollingIntervalMs);

// Init: register MQTT callback (gọi sau mqttBegin + setCommandCallback).
// Gọi 1 lần trong setup().
void handlerBegin();

// Wire handler đổi pollingInterval. main.cpp đăng ký lambda thao tác s_provCfg.
void setPollingHandler(SetPollingHandler h);

// Stats (logStatsPeriodic).
uint32_t cmdReceivedCount();
uint32_t cmdAckOkCount();
uint32_t cmdAckFailedCount();
uint32_t cmdUnknownTypeCount();

}  // namespace cmd
