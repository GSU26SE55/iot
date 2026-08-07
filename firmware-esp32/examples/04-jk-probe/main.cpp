// ==================================================================
// Thăm dò cổng UART của JK-BMS.
//
// Chân TX của BMS đo được 3,3V lúc rảnh → dây đúng, cổng sống, nhưng
// BMS không tự phát. Nó thuộc kiểu hỏi-đáp: phải gửi frame sang thì
// mới trả lời.
//
// Sketch gửi lần lượt hai họ frame của JK rồi nghe 2 giây mỗi lần,
// quét qua các tốc độ phổ biến:
//   A) 4E 57 …  — họ "NW", JK RS485/UART đời cũ
//   B) AA 55 90 EB … — họ JK02, dùng trên bản BLE và nhiều bản UART mới
//
// Nạp: pio run -e esp32-s3-jk-probe -t upload --upload-port COM5
// ==================================================================
#include <Arduino.h>

namespace {

// Họ A — "NW": đọc toàn bộ thông tin. 21 byte, kết thúc 68 + checksum.
const uint8_t kFrameNW[] = {
  0x4E, 0x57, 0x00, 0x13, 0x00, 0x00, 0x00, 0x00,
  0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x68, 0x00, 0x00, 0x01, 0x29
};

// Họ B — JK02: đọc cell info. 20 byte, byte cuối là checksum cộng dồn.
const uint8_t kFrameJK02[] = {
  0xAA, 0x55, 0x90, 0xEB, 0x96, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x11
};

constexpr uint32_t kBauds[] = {115200, 9600, 19200, 38400};
constexpr size_t   kBaudCount = sizeof(kBauds) / sizeof(kBauds[0]);

size_t s_baudIdx = 0;

void listen(const char* label, uint32_t ms) {
  uint32_t t0 = millis();
  uint32_t n  = 0;
  uint8_t  col = 0;
  while (millis() - t0 < ms) {
    while (Serial2.available()) {
      uint8_t b = Serial2.read();
      if (n == 0) Serial.printf("  tra loi %s: ", label);
      Serial.printf("%02X ", b);
      n++;
      if (++col >= 16) { Serial.println(); col = 0; }
    }
    delay(2);
  }
  if (n == 0) Serial.printf("  %s: im lang\n", label);
  else        Serial.printf("\n  >>> %s: NHAN %lu BYTE <<<\n", label, (unsigned long)n);
}

void probe(uint32_t baud) {
  Serial2.end();
  delay(60);
  Serial2.begin(baud, SERIAL_8N1, 18 /*RX*/, 17 /*TX*/);
  delay(60);
  while (Serial2.available()) Serial2.read();

  Serial.printf("\n===== baud %lu =====\n", (unsigned long)baud);

  Serial.println(" gui frame A (4E 57 - ho NW)");
  Serial2.write(kFrameNW, sizeof(kFrameNW));
  Serial2.flush();
  listen("A", 2000);

  while (Serial2.available()) Serial2.read();

  Serial.println(" gui frame B (AA 55 90 EB - ho JK02)");
  Serial2.write(kFrameJK02, sizeof(kFrameJK02));
  Serial2.flush();
  listen("B", 2000);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" JK-BMS UART probe — RX=18 TX=17");
  Serial.println("========================================");
}

void loop() {
  probe(kBauds[s_baudIdx]);
  s_baudIdx = (s_baudIdx + 1) % kBaudCount;
}
