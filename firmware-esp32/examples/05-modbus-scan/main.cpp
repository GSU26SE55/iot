// ==================================================================
// Quét Modbus trên cổng UART của JK-BMS.
//
// App JK đặt UART1 Protocol = "001 - JK BMS RS485 Modbus V1.0", Device
// Addr = 1. Nhưng mục 001 không ghi tốc độ, còn mục 013 ghi rõ (9600).
// Sketch này gửi đúng frame Modbus RTU rồi quét qua các tốc độ phổ biến,
// và thử vài địa chỉ thanh ghi hay dùng của JK.
//
// Frame: [addr=1][func=03][reg hi][reg lo][count hi][count lo][crc lo][crc hi]
//
// Đọc kết quả:
//   - Ra byte mở đầu 01 03 ...  → ĐÚNG baud, Modbus bắt tay được.
//   - Ra byte mở đầu 01 83 ...  → đúng baud nhưng sai địa chỉ thanh ghi.
//   - Ra byte loạn xạ           → sai baud.
//   - Im lặng ở mọi baud        → BMS không nghe, hoặc chưa bật cổng.
//
// Nạp: pio run -e esp32-s3-modbus-scan -t upload --upload-port COM5
// ==================================================================
#include <Arduino.h>

namespace {

constexpr uint32_t kBauds[]  = {115200};

struct Query {
  uint16_t address;
  uint16_t count;
  const char* label;
};

// JK Modbus V1.1 exposes its documented addresses directly. Some fields are
// 32-bit and therefore need two returned words; packed UINT8 pairs use one.
constexpr Query kQueries[] = {
  {0x1200, 16, "cell voltages 1-16"},
  {0x128A,  1, "MOS temperature"},
  {0x1290,  2, "pack voltage"},
  {0x1294,  2, "pack power"},
  {0x1298,  2, "pack current"},
  {0x129C,  2, "battery T1/T2"},
  {0x12A4,  1, "balance current"},
  {0x12A6,  1, "balance state + SOC"},
  {0x12A8,  2, "remaining capacity"},
  {0x12AC,  2, "full capacity"},
  {0x12B0,  2, "cycle count"},
  {0x12B8,  1, "SOH + precharge"},
  {0x12C0,  1, "charge + discharge status"},
};
constexpr size_t   kBaudN    = sizeof(kBauds) / sizeof(kBauds[0]);
constexpr size_t   kQueryN   = sizeof(kQueries) / sizeof(kQueries[0]);

uint16_t crc16(const uint8_t* buf, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= buf[i];
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else         crc >>= 1;
    }
  }
  return crc;
}

bool askOnce(uint16_t reg, uint16_t count) {
  uint8_t f[8];
  f[0] = 0x01;                       // slave address
  f[1] = 0x03;                       // read holding registers
  f[2] = static_cast<uint8_t>(reg >> 8);
  f[3] = static_cast<uint8_t>(reg & 0xFF);
  f[4] = static_cast<uint8_t>(count >> 8);
  f[5] = static_cast<uint8_t>(count & 0xFF);
  uint16_t c = crc16(f, 6);
  f[6] = static_cast<uint8_t>(c & 0xFF);
  f[7] = static_cast<uint8_t>(c >> 8);

  while (Serial2.available()) Serial2.read();
  Serial2.write(f, sizeof(f));
  Serial2.flush();

  uint32_t t0 = millis();
  uint32_t n  = 0;
  while (millis() - t0 < 800) {
    while (Serial2.available()) {
      uint8_t b = Serial2.read();
      if (n == 0) Serial.print("    <- ");
      Serial.printf("%02X ", b);
      n++;
    }
    delay(2);
  }
  if (n > 0) Serial.printf("  [%lu byte]\n", (unsigned long)n);
  return n > 0;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" Modbus scan — RX=18 TX=17, slave addr 1");
  Serial.println("========================================");
}

void loop() {
  for (size_t b = 0; b < kBaudN; b++) {
    Serial2.end();
    delay(60);
    Serial2.begin(kBauds[b], SERIAL_8N1, 18 /*RX*/, 17 /*TX*/);
    delay(60);

    Serial.printf("\n===== baud %lu =====\n", (unsigned long)kBauds[b]);
    bool any = false;
    for (size_t r = 0; r < kQueryN; r++) {
      Serial.printf("  %s @ 0x%04X (%u words): ",
                    kQueries[r].label, kQueries[r].address, kQueries[r].count);
      if (askOnce(kQueries[r].address, kQueries[r].count)) any = true;
      else Serial.println("im lang");
    }
    if (any) Serial.printf(">>> baud %lu CO PHAN HOI <<<\n", (unsigned long)kBauds[b]);
  }
  Serial.println("\n--- het mot vong, lap lai ---");
}
