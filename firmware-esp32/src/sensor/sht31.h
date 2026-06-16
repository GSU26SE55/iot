// ==================================================================
// Sprint 5 — S5-FW-06 (#60): SHT31 ambient temp + humidity sensor.
//
// SHT31 đo môi trường (ngoài pin) — KHÔNG phải sensor cell. POST sang
// endpoint riêng `/api/ambient-readings/batch` (MO §52.9bis, §1.8).
//
// Hardware (WD §4.2):
//   I2C chung với INA226. ESP32-S3 GPIO8 SDA, GPIO9 SCL.
//   SHT31 ADDR=GND → 0x44 default.
//
// Backend contract (verified với AmbientReadingsController.cs + BatchIngestAmbientReadingsCommand):
//   POST /api/ambient/readings/batch
//   Body: { items: [ { siteId(Guid required), time(UTC), ambientTemperature(decimal),
//                      humidity(decimal?), solarIrradiance(decimal?),
//                      source(int enum: IotSensor=1), sourceDeviceId(≤64) } ] }
//   Headers: X-Api-Key + X-Device-Code (Sprint 2)
//   Auth: ApiKey scheme + Policy "EnvironmentalIngest" (scope bitmask 1<<2 = 4)
//   Response: 201 Created với số reading insert/skip (dedup theo (SiteId, Time))
//   Validate: Items ≤ 100; SiteId + Time + AmbientTemperature required
//
// ⚠ DEPLOYMENT: admin tạo IoT device PHẢI set `ApiKeyScopes` include `EnvironmentalIngest=4`
//   (vd combined `SensorIngest|DeviceHeartbeat|EnvironmentalIngest|FirmwareCheck` = 15).
//   Nếu thiếu scope → backend trả 403 với mọi POST SHT31.
//
// Tham chiếu:
//   - tasksprint.md S5-FW-06
//   - OV §A6 (ambient sensor checklist)
//   - MO §52.9bis (ambient endpoint)
// ==================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace sensor {

// Init I2C bus (idempotent với ina226Begin) + verify SHT31 respond.
// Trả false nếu sensor không respond.
bool sht31Begin();

// Set siteId — backend AmbientReadingItem.SiteId REQUIRED (Guid).
// Caller (main.cpp) gọi sau provision flow (s_provCfg.siteId được fill).
// Nếu chưa set, sht31PostNow() sẽ skip + log warning (POST với SiteId rỗng = backend reject).
void sht31SetSiteId(const char* siteIdGuid);

// Đọc 1 sample. Trả false nếu I2C lỗi hoặc value invalid (NaN).
// `tempOut` °C, `humOut` % RH.
bool sht31ReadOnce(float* tempOut, float* humOut);

// Gọi mỗi loop tick. Tự check elapsed, gọi POST khi đủ interval
// (SHT31_POLL_INTERVAL_MS = 60s default).
// Caller PHẢI đảm bảo WiFi + NTP synced trước khi gọi.
void sht31Tick();

// Force gửi ambient reading ngay (debug). Trả true nếu HTTP 2xx.
bool sht31PostNow();

uint32_t sht31PostOkCount();
uint32_t sht31PostFailCount();

}  // namespace sensor
