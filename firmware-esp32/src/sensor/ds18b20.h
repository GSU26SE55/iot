// ==================================================================
// Sprint 5 — S5-FW-05 (#59): DS18B20 1-Wire temperature sensor.
//
// Mỗi pin có 1 DS18B20 gắn vào thân (thermal contact). Bus 1-Wire chung
// (4.7kΩ pull-up). ESP32 enumerate tất cả 1-Wire device addresses, map
// theo thứ tự sang battery serial (config-driven).
//
// Reading tag: sourceType=IotGateway (2), sensorSourceCode="external-temp"
// → Sprint 6 cross-source pair với BMS temp.
//
// Hardware (WD §4.1):
//   DS18B20 DQ → ESP32 GPIO4 (+ 4.7kΩ pull-up đến VCC 3.3V)
//   VCC = 3.3V, GND chung
//
// Sprint 5 mapping: scan thứ tự — sensor đầu tiên = battery 0, v.v.
// Production: nên dùng `sensors.getAddress(addr, i)` + map cố định trong NVS.
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/reading.h"

namespace sensor {

// Init OneWire + DallasTemperature lib. Quét bus enumerate sensors.
// Trả số sensors detected (0 nếu bus rỗng hoặc lỗi pull-up).
size_t ds18b20Begin();

// Trigger conversion tất cả sensors + đọc per-index temp (°C).
// `index` = 0..(detected-1). Trả NaN-equivalent (DEVICE_DISCONNECTED_C = -127)
// nếu sensor disconnect.
float ds18b20ReadByIndex(size_t index);

// S5-FW-05 helper: tạo N reading "external-temp" — mỗi reading mapping
// theo thứ tự enumerate sensor → battery serial.
//
// `serials` array tương ứng index sensor. Returns số reading sinh.
size_t ds18b20BuildExternalTempReadings(const char* const* serials, size_t n,
                                        core::SensorReading* out, size_t maxOut);

uint32_t ds18b20ReadOkCount();
uint32_t ds18b20ReadFailCount();

// Nhiệt độ môi trường đại diện (probe #0) cho báo cáo ambient gộp.
// Trả false nếu chưa init / không dò ra sensor / probe mất kết nối — khi đó payload
// bỏ hẳn trường nhiệt độ thay vì gửi giá trị rác.
bool ds18b20ReadAmbient(float& outCelsius);

// Gia tri doc duoc gan nhat cua cam bien dau tien, KHONG cham bus 1-Wire.
// Tra false neu chua co lan doc nao hoac gia tri da cu hon `maxAgeMs`.
// Dung cho vong do bien dong: `ds18b20ReadAmbient()` chan 750 ms nen khong goi lien tuc duoc.
bool ds18b20LastAmbient(float& outCelsius, uint32_t maxAgeMs);

}  // namespace sensor
