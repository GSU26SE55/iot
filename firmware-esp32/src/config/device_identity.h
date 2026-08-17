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

// GH-749 — số KÝ TỰ tối đa, lấy đúng theo cột backend:
//   device_code       HasMaxLength(64)
//   api_key_plaintext HasMaxLength(128)
constexpr size_t kMaxApiKeyChars     = 128;
constexpr size_t kMaxDeviceCodeChars = 64;

// Cỡ BUFFER = số ký tự + 1 cho ký tự kết thúc chuỗi.
//
// Bản cũ đặt buffer đúng bằng số ký tự (64), nên một deviceCode 64 ký tự — hoàn toàn hợp lệ
// với backend — vẫn bị cắt mất ký tự cuối. Lỗi này nằm im vì code cũ cắt trong im lặng; nay
// giá trị quá khổ bị TỪ CHỐI nên phải chắc chắn mọi giá trị hợp lệ đều vừa, kẻo chặn nhầm.
constexpr size_t kMaxApiKeyLen     = kMaxApiKeyChars + 1;
constexpr size_t kMaxDeviceCodeLen = kMaxDeviceCodeChars + 1;

// Khởi tạo runtime store. Gọi 1 lần trong setup() SAU storage::nvsBegin().
// Đọc NVS → nếu thiếu thì fallback về compile-time macros.
void identityBegin();

// Getters — trả con trỏ static, KHÔNG free.
const char* apiKey();
const char* deviceCode();

// Setters — ghi vào NVS + reload runtime cache. Trả false nếu NVS fail.
bool setApiKey(const char* newKey);
bool setDeviceCode(const char* newCode);

// Mark the device as intentionally unpaired. Unlike resetToDefaults(), this
// suppresses compile-time identity fallbacks on the next boot so the web
// setup flow asks for a new QR code.
bool prepareForPairing();

// Reset cả 2 về compile-time defaults (erase NVS).
bool resetToDefaults();

// Helper log — mask api key (chỉ in 4 ký tự đầu + 4 cuối).
// Format: "iotk***LAST" — dùng cho banner debug.
void maskedApiKey(char* outBuf, size_t outBufLen);

// In trạng thái identity ra Serial (mask api key). Dùng cho `show` command.
void printStatus();

}  // namespace identity
