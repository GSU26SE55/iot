// ==================================================================
// Sprint 3 — S3-FW-02: Idempotency-Key generator
//
// Sinh `Idempotency-Key` cho mỗi batch — backend §IoT2-16 dedup theo
// (deviceCode, idempotencyKey) TTL 24h.
//
// Format mặc định: UUIDv4 — 36 chars + null.
// Lý do chọn UUIDv4:
//   - Spec NI §7.4 chấp nhận UUIDv4 hoặc deviceCode+epoch+seq
//   - UUIDv4 ít rủi ro collision khi nhiều device cùng restart (clock chưa NTP)
//   - Generator dùng esp_random() — ESP32 entropy từ WiFi/Bluetooth RF noise
//
// Tham chiếu:
//   - tasksprint.md S3-FW-02
//   - RFC 4122 §4.4 UUIDv4
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace core {

constexpr size_t kIdempotencyKeyLen = 36;       // UUID v4 dạng chuẩn (no braces)
constexpr size_t kIdempotencyKeyBuf = 40;       // 36 + null + padding

// Sinh UUIDv4 vào `outBuf`. `outBufLen` ≥ kIdempotencyKeyBuf.
// Trả true nếu thành công. Format: "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
// (version 4, variant 10xx).
bool generateIdempotencyKeyV4(char* outBuf, size_t outBufLen);

// Alt format: `<deviceCode>-<epoch_ms>-<seq>` — collision-safe per-device.
// `seq` là counter monotone do caller quản lý. Trả true nếu thành công.
bool generateIdempotencyKeyDeviceCode(const char* deviceCode,
                                      uint32_t    epochSec,
                                      uint32_t    seq,
                                      char*       outBuf,
                                      size_t      outBufLen);

}  // namespace core
