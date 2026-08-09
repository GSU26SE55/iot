// ==================================================================
// Sprint 3 — S3-FW-05: Status LED implementation
// ==================================================================
#include "ui/status_led.h"
#include "ui/led_palette.h"

#include <Arduino.h>

namespace ui {

namespace {
constexpr uint8_t kLedPin = 48;        // WS2812 on ESP32-S3-DevKitC-1
constexpr uint32_t kDebounceMs = 200;

LedState s_current      = LedState::Off;
uint32_t s_lastChangeMs = 0;

// IOT3-54 — pha nhấp nháy. `s_phaseHigh=true` là nửa "màu chính".
bool     s_phaseHigh    = true;
uint32_t s_lastBlinkMs  = 0;

// Brightness low (32/255) để không gây nhiễu test ban đêm.
void writeRgb(uint8_t r, uint8_t g, uint8_t b) {
#if defined(ARDUINO_ARCH_ESP32)
  // neopixelWrite available in Arduino-ESP32 v2.0.7+; works for ESP32-S3.
  neopixelWrite(kLedPin, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}
}  // namespace

void ledBegin() {
  pinMode(kLedPin, OUTPUT);
  writeRgb(0, 0, 0);
  s_current = LedState::Off;
  Serial.printf("[led] init GPIO%u (WS2812)\n", kLedPin);
}

void ledSet(LedState s) {
  if (s == s_current) return;
  uint32_t now = millis();
  if (now - s_lastChangeMs < kDebounceMs) return;

  RgbColor c = paletteForState(s);
  writeRgb(c.r, c.g, c.b);
  s_current      = s;
  s_lastChangeMs = now;
  // Trạng thái mới luôn bắt đầu ở nửa "màu chính", để lúc chuyển sang một trạng thái nháy
  // đèn không đứng im ở nửa tắt và trông như đã tắt hẳn.
  s_phaseHigh    = true;
  s_lastBlinkMs  = now;
}

void ledTick() {
  const LedPattern pattern = patternForState(s_current);
  if (pattern == LedPattern::Solid) return;

  const uint32_t now = millis();
  if (now - s_lastBlinkMs < kBlinkHalfPeriodMs) return;
  s_lastBlinkMs = now;
  s_phaseHigh   = !s_phaseHigh;

  if (s_phaseHigh) {
    const RgbColor c = paletteForState(s_current);
    writeRgb(c.r, c.g, c.b);
  } else if (pattern == LedPattern::Alternate) {
    const RgbColor c = secondaryPaletteForState(s_current);
    writeRgb(c.r, c.g, c.b);
  } else {
    writeRgb(0, 0, 0);
  }
}

LedState ledCurrent() { return s_current; }

}  // namespace ui
