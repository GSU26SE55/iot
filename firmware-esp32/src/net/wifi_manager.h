// ==================================================================
// Sprint 1 — S1-FW-02: WiFi manager
//   - Connect WiFi station mode
//   - Auto reconnect khi rớt (non-blocking, throttle 5s)
//
// Tham chiếu:
//   - tasksprint.md S1-FW-02
//   - newiot.md §9.1 (firmware modules)
// ==================================================================
#pragma once
#include <cstdint>

namespace net {

// Khởi động WiFi station + register event handler. Gọi 1 lần trong setup().
// Block tối đa ~30s chờ link đầu tiên, nhưng sẽ return ngay cả khi fail
// (loop sẽ tự reconnect).
void wifiBegin(const char* ssid, const char* password);

// Gọi mỗi tick loop(). Nếu WiFi đang DOWN → trigger reconnect (throttle 5s).
// Không block.
void wifiTick();

// True nếu WL_CONNECTED.
bool wifiIsConnected();

// RSSI hiện tại (dBm) hoặc 0 nếu chưa connect.
int8_t wifiRssi();

}  // namespace net
