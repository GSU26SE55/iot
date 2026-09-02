// ==================================================================
// Sprint 2 — S2-FW-01: NVS (Non-Volatile Storage) wrapper
//
// Wrapper trên ESP32 Preferences API để lưu credentials + config runtime.
// Cho phép đổi apiKey/deviceCode/config qua Serial mà KHÔNG cần reflash.
//
// Namespace: "iot" (max 15 chars per Preferences spec).
//
// ⚠️ Preferences giới hạn **15 ký tự cho MỖI KHOÁ**. Vượt quá thì `putString` trả 0 và giá trị
//    KHÔNG được ghi — không exception, không log, chỉ là lần boot sau thiếu cấu hình. Thêm khoá mới
//    phải đếm ký tự trước. Khoá dài nhất hiện tại là "wifissid"/"mqprefix" (8 ký tự).
//
// Khoá đang dùng:
//   -- danh tính + provision (Sprint 2) --
//   "apikey"     string  — API key plaintext (S2-FW-01)
//   "devcode"    string  — DeviceCode (S2-FW-01)
//   "provd"      uint8   — flag "đã provision" (0/1) (S2-FW-02)
//   "siteid"     string  — SiteId Guid từ provision response (S2-FW-02)
//   "pollIntS"   int32   — pollingIntervalSeconds từ configJson (S2-FW-02)
//   "hbIntS"     int32   — heartbeatIntervalSeconds từ configJson (S2-FW-02)
//   "ntpsv"      string  — NTP server từ configJson (S2-FW-02)
//   "fwver"      string  — last firmware version đã provision (S2-FW-02)
//
//   -- mạng + broker, cấp lúc chạy (IOT3-35) --
//   "wifissid"   string  — SSID WiFi khách hàng          (IOT3-36, config/wifi_config.cpp)
//   "wifipass"   string  — mật khẩu WiFi                 (IOT3-36)
//   "mqhost"     string  — host broker MQTT              (IOT3-37, config/mqtt_config.cpp)
//   "mqport"     int32   — cổng broker                   (IOT3-37)
//   "mqtls"      uint8   — dùng TLS hay không            (IOT3-37 — đây LÀ nguồn sự thật;
//                          mqtt_client chọn WiFiClientSecure/WiFiClient theo giá trị này
//                          lúc chạy. MQTT_USE_TLS chỉ là fallback khi key này trống.)
//   "mquser"     string  — username MQTT                 (IOT3-37)
//   "mqpass"     string  — mật khẩu MQTT                 (IOT3-37)
//   "mqprefix"   string  — tiền tố topic, vd solar/gw-esp32-001 (IOT3-37)
//   "batmap"     string  — bảng ánh xạ pin, mã hoá theo `core/battery_map_codec.h` (IOT3-49)
//
// Tham chiếu:
//   - tasksprint.md S2-FW-01
//   - NI §9.1 (config loader)
//   - overall.md §17 Sprint IoT-3 (IOT3-35..37, IOT3-49)
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
