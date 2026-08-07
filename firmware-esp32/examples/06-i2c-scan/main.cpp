// ==================================================================
// Quét bus I2C trên GPIO8 (SDA) / GPIO9 (SCL).
//
// INA226 phải hiện ở 0x40, SHT3X phải hiện ở 0x44. Cả hai cùng vắng mặt
// nghĩa là bus chết — đảo SDA/SCL, thiếu nguồn 3V3, hoặc thiếu GND chung.
// Chỉ một module vắng mặt thì lỗi nằm riêng ở module đó.
//
// Nạp: pio run -e esp32-s3-i2c-scan -t upload --upload-port COM5
// ==================================================================
#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);
  Serial.println();
  Serial.println("========================================");
  Serial.println(" I2C scan — SDA=GPIO8 SCL=GPIO9");
  Serial.println("========================================");
  Wire.begin(8, 9, 100000UL);
}

void loop() {
  Serial.println("\n--- quet ---");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
      Serial.printf("  tim thay thiet bi tai 0x%02X", addr);
      if (addr == 0x40) Serial.print("  <-- INA226");
      if (addr == 0x44) Serial.print("  <-- SHT3X");
      Serial.println();
      found++;
    }
  }
  if (found == 0) Serial.println("  KHONG tim thay thiet bi nao — bus chet.");
  else Serial.printf("  tong: %d thiet bi\n", found);
  delay(3000);
}
