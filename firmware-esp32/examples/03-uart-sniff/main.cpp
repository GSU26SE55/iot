// ==================================================================
// Sniffer cổng UART của JK-BMS.
//
// Mục đích: phân biệt "dây sai" với "giao thức sai".
//   - Ra byte, frame mở đầu 4E 57  → dây ĐÚNG, BMS nói JK-native.
//                                     Việc còn lại là viết parser.
//   - Ra byte nhưng loạn xạ        → sai baud. Sketch tự quét nhiều baud.
//   - Im hoàn toàn ở mọi baud      → dây sai (đảo TX/RX, thiếu GND),
//                                     hoặc BMS chỉ trả lời khi được hỏi.
//
// Đấu dây: BMS TX → GPIO18, BMS RX → GPIO17, GND chung. VBAT KHÔNG nối.
// Nạp:  pio run -e esp32-s3-uart-sniff -t upload --upload-port COM5
// ==================================================================
#include <Arduino.h>

namespace {
constexpr uint32_t kBauds[] = {115200, 9600, 19200, 38400, 57600};
constexpr size_t   kBaudCount = sizeof(kBauds) / sizeof(kBauds[0]);
constexpr uint32_t kDwellMs = 6000;   // nghe mỗi baud 6 giây

size_t   s_idx      = 0;
uint32_t s_switchAt = 0;
uint32_t s_count    = 0;
uint8_t  s_col      = 0;
}  // namespace

void useBaud(size_t i) {
  Serial2.end();
  delay(50);
  Serial2.begin(kBauds[i], SERIAL_8N1, 18 /*RX*/, 17 /*TX*/);
  s_count = 0;
  s_col   = 0;
  Serial.printf("\n[sniff] ===== baud %lu — nghe %lu giay =====\n",
                (unsigned long)kBauds[i], (unsigned long)(kDwellMs / 1000));
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);

  Serial.println();
  Serial.println("========================================");
  Serial.println(" UART sniffer — cong UART cua JK-BMS");
  Serial.println(" RX=GPIO18  TX=GPIO17  GND chung");
  Serial.println(" Tim frame mo dau: 4E 57");
  Serial.println("========================================");

  useBaud(0);
  s_switchAt = millis();
}

void loop() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read();
    Serial.printf("%02X ", b);
    s_count++;
    if (++s_col >= 16) {
      Serial.println();
      s_col = 0;
    }
  }

  if (millis() - s_switchAt >= kDwellMs) {
    s_switchAt = millis();
    Serial.printf("\n[sniff] baud %lu → nhan duoc %lu byte\n",
                  (unsigned long)kBauds[s_idx], (unsigned long)s_count);
    s_idx = (s_idx + 1) % kBaudCount;
    useBaud(s_idx);
  }
}
