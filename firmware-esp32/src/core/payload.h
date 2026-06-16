// ==================================================================
// Sprint 1 — S1-FW-05: Legacy contract
// Sprint 3 — S3-FW-04: Production contract
//
// Authority:
//   - tasksprint.md S1-FW-05 + S3-FW-04
//   - newiot.md §7.4 (production + backward compat legacy)
//   - backend BatchIngestSensorReadingsCommand (SensorReadingItem fields)
//   - MO §52.5, §52.9 (sourceType per nguồn vật lý)
// ==================================================================
#pragma once
#include <cstddef>

#include "core/reading.h"

namespace core {

// Buffer JSON tối đa cho batch N reading.
// Sprint 1 legacy: ~250 bytes/reading.
// Sprint 3 production: ~380 bytes/reading (thêm field sohPercent, chargingState,
//                       bmsErrorCode, sensorSourceCode, sourceType, batteryAssetSerial,
//                       deviceTimestamp).
constexpr size_t kPerReadingBytes = 384;
constexpr size_t kEnvelopeBytes   = 32;

inline constexpr size_t payloadBufferSize(size_t readingCount) {
  return kEnvelopeBytes + readingCount * kPerReadingBytes;
}

// ---- Sprint 1 (#16) — Legacy contract (chỉ items[].batteryAssetId + 6 field cơ bản) ----
// Giữ cho regression test và mock backend backward compat.
size_t buildLegacyBatchPayload(const SensorReading* readings,
                               size_t                readingCount,
                               const char*           isoTimestamp,
                               const char*           deviceCode,
                               char*                 outBuf,
                               size_t                outBufLen);

// ---- Sprint 3 (#32 S3-FW-04) — Production contract ----
//
// Schema (backend SensorReadingItem):
// {
//   "items": [
//     {
//       "batteryAssetSerial": "BAT-2026-001",        ← priority over batteryAssetId
//       "time":               "2026-06-14T08:15:30Z",
//       "deviceTimestamp":    "2026-06-14T08:15:30Z",  ← #IoT2-15 clock skew check
//       "voltage": 12.6, "current": -5.2, "temperature": 35.4, "socPercent": 78.5,
//       "cycleCount": 120,
//       "sohPercent": 94.2,                            ← optional
//       "chargingState": 3,                            ← optional Idle/Charging/...
//       "bmsErrorCode": "OVT-PROTECT",                 ← optional ≤ 64 chars
//       "sensorSourceCode": "primary",                 ← required Sprint 3 ≤ 20 chars
//       "sourceType": 1                                ← required Bms=1, IotGateway=2
//     },
//     { ... cùng battery khác sourceType (cross-source S6 §1.6.6) ... }
//   ]
// }
//
// Tham số:
//   readings/readingCount    — mảng SensorReading từ mock::mockGenerateMultiSource
//   isoTimestamp             — chuỗi ISO8601 UTC dùng cho items[].time + items[].deviceTimestamp
//                              (Sprint 3: 1 batch = 1 timestamp; sau cần per-reading thì caller fill)
//   deviceCode               — DEVICE_CODE — không embed payload (header X-Device-Code mang)
//   outBuf / outBufLen       — caller cấp buffer ≥ payloadBufferSize(readingCount)
//
// Trả: số byte ghi vào outBuf (không gồm null), hoặc 0 nếu lỗi.
size_t buildProductionBatchPayload(const SensorReading* readings,
                                   size_t               readingCount,
                                   const char*          isoTimestamp,
                                   const char*          deviceCode,
                                   char*                outBuf,
                                   size_t               outBufLen);

}  // namespace core
