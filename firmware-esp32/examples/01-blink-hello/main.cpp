// ==================================================================
// Sprint 0 — S0-FW-02: Blink + Serial "hello"
// Build: pio run -e example-blink
// Flash: pio run -e example-blink -t upload
// Mon:   pio device monitor -b 115200
//
// Acceptance (tasksprint.md S0-FW-02):
//   - LED on-board nhấp nháy mỗi 500ms
//   - Serial Monitor in "hello" mỗi giây
// ==================================================================
#include <Arduino.h>

// ESP32-S3-DevKitC-1 có RGB LED trên GPIO48 (WS2812 — neopixel).
// Để giữ sketch tối giản (không cần lib NeoPixel), demo dùng LED_BUILTIN.
// Nếu board không có LED đơn → vẫn thấy Serial "hello" là pass.
#ifndef LED_BUILTIN
  #define LED_BUILTIN 2     // fallback
#endif

static uint32_t lastBlink = 0;
static uint32_t lastPrint = 0;
static bool ledState = false;
static uint32_t tick = 0;

void setup() {
  Serial.begin(115200);
  // Đợi USB-CDC ổn định (ESP32-S3 với ARDUINO_USB_CDC_ON_BOOT=1)
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) { delay(10); }

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" Sprint 0 — S0-FW-02");
  Serial.println(" Build: " __DATE__ " " __TIME__);
#ifdef FW_VERSION
  Serial.printf(" Version: %s\n", FW_VERSION);
#endif
  Serial.println("==============================");
}

void loop() {
  uint32_t now = millis();

  if (now - lastBlink >= 500) {
    lastBlink = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  if (now - lastPrint >= 1000) {
    lastPrint = now;
    tick++;
    Serial.printf("[%lu] hello (uptime=%lus)\n",
                  (unsigned long)tick, (unsigned long)(now / 1000));
  }
}
