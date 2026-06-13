// ==================================================================
// Sprint 1 — S1-FW-05: Build JSON batch payload theo LEGACY contract
//
// Authority:
//   - tasksprint.md Sprint 1 / S1-FW-05 (acceptance: "JSON output match schema legacy")
//   - newiot.md §7.4 "Backward compatibility (MVP)":
//       "vẫn chấp nhận payload cũ dùng items[].batteryAssetId (legacy mode) để demo nhanh"
//
// Schema legacy (Sprint 1):
// {
//   "items": [
//     {
//       "batteryAssetId": "11111111-1111-4111-8111-000000000001",   // Guid, required
//       "time":           "2026-06-13T08:15:42Z",                   // ISO8601 UTC
//       "voltage":        12.62,                                    // 0..1000 (NI §7.4)
//       "current":        -3.21,
//       "temperature":    27.4,                                     // -50..150 (NI §7.4)
//       "socPercent":     81.5,                                     // 0..100   (NI §7.4)
//       "cycleCount":     100                                       // optional
//     }
//   ]
// }
//
// KHÔNG có trong Sprint 1 (sẽ thêm Sprint 3 — S3-FW-04):
//   - top-level wrapper deviceTimestamp / readings[]
//   - per item: batteryAssetSerial, sourceType, sensorSourceCode, sohPercent,
//     chargingState, bmsErrorCode
//   - header: X-Device-Code, Idempotency-Key
// ==================================================================
#pragma once
#include <cstddef>

#include "core/reading.h"

namespace core {

// Buffer JSON tối đa cho batch N reading.
// Mỗi reading ~ 220 bytes (key + value padding).
constexpr size_t kPerReadingBytes = 256;
constexpr size_t kEnvelopeBytes   = 32;    // `{"items":[...]}` gọn

inline constexpr size_t payloadBufferSize(size_t readingCount) {
  return kEnvelopeBytes + readingCount * kPerReadingBytes;
}

// Build JSON legacy contract vào `outBuf`.
//
// Tham số:
//   readings       — mảng SensorReading sinh từ bms::mockGenerate()
//   readingCount   — số reading (≤ bms::kMockMaxBatteries)
//   isoTimestamp   — chuỗi ISO8601 UTC (output net::isoNow). Dùng cho items[i].time
//                    (Sprint 1: 1 batch dùng chung 1 timestamp).
//   deviceCode     — DEVICE_CODE từ config.h. Sprint 1 KHÔNG embed trong payload
//                    (giữ param để API ổn định khi Sprint 3 đổi sang production
//                    contract — lúc đó deviceCode sẽ vào header X-Device-Code).
//   outBuf         — buffer output (≥ payloadBufferSize(readingCount))
//   outBufLen      — sizeof(outBuf)
//
// Trả: số byte ghi vào outBuf (không gồm null), hoặc 0 nếu lỗi.
size_t buildLegacyBatchPayload(const SensorReading* readings,
                               size_t                    readingCount,
                               const char*               isoTimestamp,
                               const char*               deviceCode,
                               char*                     outBuf,
                               size_t                    outBufLen);

}  // namespace core
