// ==================================================================
// Sprint 4 — Pure decision logic cho downlink command.
//
// Tách khỏi command_handler.cpp để test native không phụ thuộc Arduino
// (mqtt_client / telemetry side-effect). Test ở `test/test_cmd_logic/`.
//
// Schema downlink (backend AdminIotDevicesController.SendCommand):
//   { "cmdId":"hex", "type":"set_interval|...|trigger_ota", "params":{...}, "issuedAt":"..." }
//
// Convention type names (BC 2 cách viết):
//   "set_interval"      = "set-interval"
//   "trigger_ota"       = "trigger-ota"
//   "request_heartbeat" = "request-heartbeat"
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace cmd { namespace logic {

enum class CommandKind : uint8_t {
  Unknown          = 0,
  SetInterval      = 1,
  TriggerOta       = 2,
  RequestHeartbeat = 3,
  SetBmsSwitch     = 4,
};

constexpr uint32_t kPollingMinSec = 1;
constexpr uint32_t kPollingMaxSec = 3600;   // 1h — defensive trên-bound

// Phân loại type string (case-insensitive, accept `_` và `-`).
CommandKind classifyType(const char* type);

// Pure check: pollingSeconds nằm trong bounds [1, 3600].
inline bool isValidPollingSeconds(uint32_t sec) {
  return sec >= kPollingMinSec && sec <= kPollingMaxSec;
}

// Output của parser — tất cả field null-terminated, không cấp phát heap.
struct ParsedCommand {
  bool        ok;                          // false = parse fail, xem parseError
  char        cmdId[64];                   // "" nếu missing trong payload
  char        type[40];                    // raw type string từ JSON
  CommandKind kind;                        // classifyType(type)
  bool        hasPollingSeconds;           // true nếu params.pollingSeconds hoặc pollingIntervalSeconds present
  uint32_t    pollingSeconds;              // 0 nếu !hasPollingSeconds
  bool        hasSwitchParams;             // true nếu set_bms_switch params hợp lệ
  char        switchSerial[64];            // serial của pin
  uint8_t     switchTarget;                // 1 = Charge, 2 = Discharge
  bool        switchEnable;                // true = bật, false = tắt
  char        parseError[120];             // "" nếu ok
};

// Pure JSON parse — KHÔNG side effect.
// `json` phải null-terminated, `len` = strlen (không tính null).
// Trả về ParsedCommand với `ok=false` nếu:
//   - json rỗng / null
//   - JSON malformed (ArduinoJson DeserializationError)
//   - field `type` thiếu hoặc rỗng (other field optional)
ParsedCommand parseCommandPayload(const char* json, size_t len);

}}  // namespace cmd::logic
