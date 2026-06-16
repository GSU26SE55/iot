// ==================================================================
// Sprint 3 — S3-FW-05: Status LED on GPIO48 (WS2812 RGB)
//
// ESP32-S3-DevKitC-1 có 1 WS2812 RGB LED on-board at GPIO48.
// API dùng `neopixelWrite(pin, r, g, b)` — built-in từ Arduino-ESP32 v2.0.7+.
//
// Trạng thái:
//   ONLINE      — xanh (0, 32, 0)    — WiFi up + NTP synced + ingest ok
//   QUEUED      — vàng (32, 24, 0)   — WiFi up nhưng queue có data chưa flush
//   OFFLINE     — đỏ (32, 0, 0)      — WiFi down hoặc backend unreachable
//   PROVISIONING — tím (16, 0, 32)   — boot, đang gọi /provision (bonus)
//
// Tham chiếu:
//   - tasksprint.md S3-FW-05
//   - wiring-diagram.md §1 (GPIO48 = on-board RGB)
// ==================================================================
#pragma once
#include <cstdint>

namespace ui {

enum class LedState : uint8_t {
  Off          = 0,
  Online       = 1,    // green
  Queued       = 2,    // yellow
  Offline      = 3,    // red
  Provisioning = 4,    // purple
};

// Khởi tạo pinMode + tắt LED ban đầu.
void ledBegin();

// Set state — gọi từ main loop khi state đổi. Có debounce 200ms (tránh nhấp nháy).
void ledSet(LedState s);

// Current state — caller có thể đọc để log.
LedState ledCurrent();

}  // namespace ui
