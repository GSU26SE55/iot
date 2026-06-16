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

// Map state → (r, g, b) — palette spec S3-FW-05.
// Brightness low (≤ 32/255) tránh chói mắt khi test ban đêm.
inline constexpr RgbColor paletteForState(LedState s) {
  switch (s) {
    case LedState::Off:          return { 0,  0,  0};
    case LedState::Online:       return { 0, 32,  0};   // green
    case LedState::Queued:       return {32, 24,  0};   // yellow (R+G mix)
    case LedState::Offline:      return {32,  0,  0};   // red
    case LedState::Provisioning: return {16,  0, 32};   // purple (R+B mix)
  }
  return {0, 0, 0};
}

}  // namespace ui
