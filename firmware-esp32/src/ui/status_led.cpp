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
}

LedState ledCurrent() { return s_current; }

}  // namespace ui
