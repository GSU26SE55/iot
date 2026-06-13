// ==================================================================
// Sprint 2 — S2-FW-01: Serial command handler
//
// Cho phép đổi apiKey/deviceCode/clear NVS qua Serial mà không cần reflash.
// Gọi `cliBegin()` trong setup() và `cliTick()` mỗi loop.
//
// Commands (gõ qua Serial Monitor 115200, Enter để confirm):
//   show              — In trạng thái identity + uptime + heap
//   set apikey <key>  — Lưu API key mới vào NVS, hot reload
//   set devcode <c>   — Lưu device code mới vào NVS, hot reload
//   clear             — Erase NVS, fallback compile-time
//   reboot            — ESP.restart()
//   help              — Liệt kê commands
// ==================================================================
#pragma once

namespace cli {

void cliBegin();
void cliTick();

}  // namespace cli
