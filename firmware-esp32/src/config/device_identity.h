// ==================================================================
// Sprint 2 — S2-FW-01: Device identity store (apiKey + deviceCode)
//
// Runtime store. Boot load thứ tự:
//   1. NVS (key "apikey" + "devcode") — đổi qua Serial cmd không cần reflash
//   2. Compile-time macros (API_KEY + DEVICE_CODE từ include/config.h)
//
// Tham chiếu:
//   - tasksprint.md S2-FW-01
//   - NI §9.1
// ==================================================================
#pragma once
#include <cstddef>

namespace identity {

constexpr size_t kMaxApiKeyLen     = 96;   // "iotk_" + 64 base62 + null
constexpr size_t kMaxDeviceCodeLen = 64;   // backend max length

// Khởi tạo runtime store. Gọi 1 lần trong setup() SAU storage::nvsBegin().
// Đọc NVS → nếu thiếu thì fallback về compile-time macros.
void identityBegin();

// Getters — trả con trỏ static, KHÔNG free.
const char* apiKey();
const char* deviceCode();

// Setters — ghi vào NVS + reload runtime cache. Trả false nếu NVS fail.
bool setApiKey(const char* newKey);
bool setDeviceCode(const char* newCode);

// Reset cả 2 về compile-time defaults (erase NVS).
bool resetToDefaults();

// Helper log — mask api key (chỉ in 4 ký tự đầu + 4 cuối).
// Format: "iotk***LAST" — dùng cho banner debug.
void maskedApiKey(char* outBuf, size_t outBufLen);

// In trạng thái identity ra Serial (mask api key). Dùng cho `show` command.
void printStatus();

}  // namespace identity
