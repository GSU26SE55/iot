// ==================================================================
// Sprint 3 — S3-FW-05: Status LED on GPIO48 (WS2812 RGB)
//
// ESP32-S3-DevKitC-1 có 1 WS2812 RGB LED on-board at GPIO48.
// API dùng `neopixelWrite(pin, r, g, b)` — built-in từ Arduino-ESP32 v2.0.7+.
//
// Trạng thái:
//   ONLINE       — xanh sáng đều      — WiFi up + NTP synced + ingest ok
//   QUEUED       — xanh sáng đều       — WiFi up, queue đang tự flush
//   OFFLINE      — đỏ                  — WiFi down hoặc backend unreachable
//   PROVISIONING — tím sáng đều        — boot, đang gọi /provision
//   SETUP        — tím NHÁY            — chưa cấu hình, trang cài đặt đang mở (IOT3-54)
//   WIFISEARCHING— cam                 — có SSID, đang tìm mạng (IOT3-54)
//   RECOVERY     — tím/cam xen kẽ      — mất mạng ≥ 5 phút, vừa phát AP vừa thử lại (IOT3-54)
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
  Online       = 1,    // xanh, sáng đều — mọi thứ bình thường
  Queued       = 2,    // xanh, sáng đều — queue tự flush, không cần người can thiệp
  Offline      = 3,    // đỏ — mất mạng / backend không với tới
  Provisioning = 4,    // tím, sáng đều — đang gọi /provision

  // ---- IOT3-54: ba trạng thái về MẠNG, dành cho người lắp đặt ----
  // Đây là công cụ chẩn đoán DUY NHẤT khách hàng dùng được qua điện thoại ("đèn màu gì?"),
  // nên phải phân biệt được bằng mắt thường, không cần cắm Serial.
  Setup         = 5,   // tím NHÁY        — chưa cấu hình, trang cài đặt đang mở
  WifiSearching = 6,   // cam, sáng đều   — có SSID, đang tìm mạng
  Recovery      = 7,   // tím/cam XEN KẼ  — mất mạng lâu, vừa phát AP vừa thử lại
};

/// Kiểu nhấp nháy — tách khỏi màu để `led_palette.h` vẫn là logic thuần, test được ở native.
enum class LedPattern : uint8_t {
  Solid     = 0,   // một màu, sáng đều
  Blink     = 1,   // màu chính ↔ tắt
  Alternate = 2,   // màu chính ↔ màu phụ
};

/// Chu kỳ nháy (ms cho mỗi nửa). 500 ms đủ chậm để mắt thấy rõ là "đang nháy",
/// đủ nhanh để không bị tưởng là đèn hỏng.
constexpr uint32_t kBlinkHalfPeriodMs = 500;

// Khởi tạo pinMode + tắt LED ban đầu.
void ledBegin();

// Set state — gọi từ main loop khi state đổi. Có debounce 200ms (tránh nhấp nháy).
void ledSet(LedState s);

/// <summary>IOT3-54 — bơm hiệu ứng nháy. Gọi MỖI vòng loop.</summary>
/// <remarks>
/// Không gọi thì các trạng thái nháy/xen kẽ sẽ đứng im ở nửa chu kỳ đầu, tức là
/// `Setup` trông y hệt `Provisioning` và `Recovery` trông y hệt `Setup` — mất sạch
/// giá trị chẩn đoán mà không có gì báo lỗi.
/// </remarks>
void ledTick();

// Current state — caller có thể đọc để log.
LedState ledCurrent();

}  // namespace ui
