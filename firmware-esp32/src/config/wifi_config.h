// ==================================================================
// IOT3-36 — WiFi runtime store (SSID + mật khẩu), lưu NVS.
//
// Boot nạp theo thứ tự:
//   1. NVS  (khoá "wifissid" / "wifipass") — đặt qua trang setup hoặc Serial CLI
//   2. Compile-time (WIFI_SSID / WIFI_PASS trong config.h) — chỉ là FALLBACK
//
// Vì sao cần: đã chốt dùng WiFi của khách hàng, mà mỗi nhà một mạng. Nhúng cứng trong firmware
// nghĩa là mỗi thiết bị một bản build, và khách đổi mật khẩu là phải cầm cáp ra tận nơi reflash.
//
// Cùng khuôn `config/device_identity.h` (apiKey/deviceCode) để đọc code đỡ phải nhớ hai kiểu.
// ==================================================================
#pragma once
#include <cstddef>

#include "core/net_config_rules.h"

namespace wificfg {

/// Cỡ buffer = số ký tự tối đa + 1 cho ký tự kết thúc chuỗi.
constexpr size_t kSsidBufLen = core::kMaxWifiSsidChars + 1;
constexpr size_t kPassBufLen = core::kMaxWifiPassChars + 1;

/// Nạp từ NVS, thiếu thì lấy compile-time. Gọi 1 lần trong setup(), SAU storage::nvsBegin().
void begin();

/// Con trỏ tới buffer static — KHÔNG free, KHÔNG giữ qua lần `save()` kế tiếp.
const char* ssid();
const char* password();

/// True khi SSID dùng được. Mật khẩu rỗng vẫn hợp lệ (mạng mở).
bool isConfigured();

/// True nếu giá trị đang dùng đến từ NVS (khác: fallback compile-time). Dùng cho log chẩn đoán.
bool isFromNvs();

/// <summary>Ghi NVS + nạp lại cache RAM. Trả false nếu giá trị không hợp lệ hoặc NVS lỗi.</summary>
/// <remarks>
/// TỪ CHỐI trước khi ghi (bài học GH-749): ghi nguyên rồi cắt trong RAM khiến NVS giữ một giá trị
/// mà firmware không bao giờ dùng trọn được — khởi động lại là đổi mạng mà không ai hiểu vì sao.
/// </remarks>
bool save(const char* newSsid, const char* newPassword);

/// Xoá cấu hình WiFi khỏi NVS → lần boot sau quay về compile-time (và vào chế độ setup).
bool clear();

/// In trạng thái ra Serial, mật khẩu ĐƯỢC MASK.
void printStatus();

}  // namespace wificfg
