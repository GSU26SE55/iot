// ==================================================================
// Sprint 2 — S2-FW-01: NVS (Non-Volatile Storage) wrapper
//
// Wrapper trên ESP32 Preferences API để lưu credentials + config runtime.
// Cho phép đổi apiKey/deviceCode/config qua Serial mà KHÔNG cần reflash.
//
// Namespace: "iot" (max 15 chars per Preferences spec).
// Keys (max 15 chars per key):
//   "apikey"     string  — API key plaintext (S2-FW-01)
//   "devcode"    string  — DeviceCode (S2-FW-01)
//   "provd"      uint8   — flag "đã provision" (0/1) (S2-FW-02)
//   "siteid"     string  — SiteId Guid từ provision response (S2-FW-02)
//   "pollIntS"   int32   — pollingIntervalSeconds từ configJson (S2-FW-02)
//   "hbIntS"     int32   — heartbeatIntervalSeconds từ configJson (S2-FW-02)
//   "ntpsv"      string  — NTP server từ configJson (S2-FW-02)
//   "fwver"      string  — last firmware version đã provision (S2-FW-02)
//
// Tham chiếu:
//   - tasksprint.md S2-FW-01
//   - NI §9.1 (config loader)
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace storage {

// Khởi tạo NVS partition. Gọi 1 lần trong setup().
// Trả false nếu mount fail (NVS corrupt — caller có thể erase + retry).
bool nvsBegin();

// Xóa toàn bộ namespace "iot" — dùng khi user gõ `clear` từ Serial hoặc
// khi cần reset device về factory state.
bool nvsErase();

// ---- String I/O ----

// Đọc key. Nếu thiếu → ghi `outBuf` = "" và trả false.
// Nếu thừa space (string trong NVS dài hơn outLen) → truncate + null-terminate.
bool nvsGetString(const char* key, char* outBuf, size_t outLen);

// Ghi key. Tự overwrite nếu đã có.
bool nvsPutString(const char* key, const char* value);

// ---- Integer I/O ----
int32_t  nvsGetInt32(const char* key, int32_t fallback);
bool     nvsPutInt32(const char* key, int32_t value);

uint8_t  nvsGetUInt8(const char* key, uint8_t fallback);
bool     nvsPutUInt8(const char* key, uint8_t value);

// ---- Utility ----

// Có key này trong NVS không (kiểm tra trước khi đọc string để tránh empty default).
bool nvsHasKey(const char* key);

// Số byte free trong namespace "iot" (debug).
size_t nvsFreeEntries();

}  // namespace storage
