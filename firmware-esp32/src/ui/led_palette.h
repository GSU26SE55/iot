// ==================================================================
// Sprint 3 — S3-FW-05: LED palette — state → RGB mapping
//
// Pure logic — không phụ thuộc neopixelWrite (Arduino API). Tách ra để test
// được trên native: chỉ map LedState → (r, g, b).
//
// status_led.cpp wrap palette này + write hardware GPIO48 WS2812.
// ==================================================================
#pragma once
#include <cstdint>

#include "ui/status_led.h"

namespace ui {

struct RgbColor {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

// Map state → (r, g, b) — palette spec S3-FW-05 + IOT3-54.
// Brightness low (≤ 32/255) tránh chói mắt khi test ban đêm.
inline constexpr RgbColor paletteForState(LedState s) {
  switch (s) {
    case LedState::Off:           return { 0,  0,  0};
    case LedState::Online:        return { 0, 32,  0};   // xanh lá
    case LedState::Queued:        return { 0, 32,  0};   // xanh lá đứng im như Online
    case LedState::Offline:       return {32,  0,  0};   // đỏ
    case LedState::Provisioning:  return {16,  0, 32};   // tím (R+B)
    case LedState::Setup:         return {16,  0, 32};   // tím — phân biệt bằng NHÁY
    case LedState::WifiSearching: return {32, 12,  0};   // cam (R nhiều + G ít)
    case LedState::Recovery:      return {16,  0, 32};   // tím, xen kẽ với cam
  }
  return {0, 0, 0};
}

/// <summary>Màu thứ hai của kiểu xen kẽ. Với các kiểu khác, trả về chính màu đó.</summary>
inline constexpr RgbColor secondaryPaletteForState(LedState s) {
  switch (s) {
    case LedState::Recovery: return {32, 12,  0};        // cam — nửa còn lại của nhịp
    default:                 return paletteForState(s);
  }
}

/// <summary>Kiểu nhấp nháy của từng trạng thái.</summary>
/// <remarks>
/// Queue đang tự đẩy không cần người xử lý nên dùng xanh đứng im như Online.
/// Chỉ Setup và Recovery nháy để báo trạng thái cần can thiệp.
/// </remarks>
inline constexpr LedPattern patternForState(LedState s) {
  switch (s) {
    case LedState::Setup:    return LedPattern::Blink;
    case LedState::Recovery: return LedPattern::Alternate;
    default:                 return LedPattern::Solid;
  }
}

}  // namespace ui
