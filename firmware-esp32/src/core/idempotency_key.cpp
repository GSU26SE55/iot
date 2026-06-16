// ==================================================================
// Sprint 3 — S3-FW-02: Idempotency-Key generator implementation
// ==================================================================
#include "core/idempotency_key.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

// esp_random() — chỉ có trên ESP32 (Arduino-ESP32). Native test dùng rand() fallback.
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
  #include <esp_random.h>
  static inline uint32_t platform_random32() { return esp_random(); }
#else
  #include <cstdlib>
  static inline uint32_t platform_random32() {
    // Compose 32-bit từ 2 lần rand() — đủ cho test native, không yêu cầu crypto-strength.
    return (static_cast<uint32_t>(rand()) << 16) ^ static_cast<uint32_t>(rand());
  }
#endif

namespace core {

bool generateIdempotencyKeyV4(char* outBuf, size_t outBufLen) {
  if (outBuf == nullptr || outBufLen < kIdempotencyKeyBuf) return false;

  // 16 random bytes (4 × uint32_t)
  uint8_t b[16];
  for (int i = 0; i < 4; ++i) {
    uint32_t r = platform_random32();
    b[i * 4 + 0] = static_cast<uint8_t>(r & 0xFF);
    b[i * 4 + 1] = static_cast<uint8_t>((r >> 8)  & 0xFF);
    b[i * 4 + 2] = static_cast<uint8_t>((r >> 16) & 0xFF);
    b[i * 4 + 3] = static_cast<uint8_t>((r >> 24) & 0xFF);
  }

  // RFC 4122 §4.4 — set version (4) + variant (10xx)
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);  // version 4
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);  // variant 10xx

  // Format: 8-4-4-4-12
  int n = std::snprintf(outBuf, outBufLen,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      b[0], b[1], b[2],  b[3],
      b[4], b[5],
      b[6], b[7],
      b[8], b[9],
      b[10], b[11], b[12], b[13], b[14], b[15]);

  return n == 36;
}

bool generateIdempotencyKeyDeviceCode(const char* deviceCode,
                                      uint32_t    epochSec,
                                      uint32_t    seq,
                                      char*       outBuf,
                                      size_t      outBufLen) {
  if (deviceCode == nullptr || deviceCode[0] == '\0' || outBuf == nullptr) return false;

  // Format: "<devcode>-<epoch>-<seq>" — devcode ≤ 64, epoch ≤ 10, seq ≤ 10 chars
  // Total ≤ 64 + 1 + 10 + 1 + 10 + 1 = 87
  int n = std::snprintf(outBuf, outBufLen, "%s-%lu-%lu",
                        deviceCode,
                        static_cast<unsigned long>(epochSec),
                        static_cast<unsigned long>(seq));
  return n > 0 && static_cast<size_t>(n) < outBufLen;
}

}  // namespace core
