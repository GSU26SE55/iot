// ==================================================================
// Sprint 1 — S1-FW-02: WiFi manager
//   - Connect WiFi station mode
//   - Auto reconnect khi rớt (non-blocking, throttle 5s)
//
// Tham chiếu:
//   - tasksprint.md S1-FW-02
//   - newiot.md §9.1 (firmware modules)
// ==================================================================
#pragma once
#include <cstdint>

#include "core/wifi_phase_policy.h"

namespace net {

/// <summary>
/// Khởi động WiFi station + đăng ký event handler. Gọi 1 lần trong setup(),
/// SAU <c>storage::nvsBegin()</c> và <c>wificfg::begin()</c>.
/// </summary>
/// <remarks>
/// <para>
/// IOT3-50 — KHÔNG còn nhận tham số. SSID/mật khẩu đọc thẳng từ <c>wificfg::</c> ở MỌI chỗ dùng,
/// nên đổi nóng qua trang setup hay CLI là có hiệu lực ngay, không cần khởi động lại. Bản cũ giữ
/// hai con trỏ tham số, mà đổi giá trị xong con trỏ vẫn chỉ vào chuỗi cũ.
/// </para>
/// <para>
/// Block tối đa ~30s chờ link đầu tiên, nhưng vẫn trả về khi thất bại (loop tự thử lại).
/// Chưa cấu hình WiFi thì trả về NGAY, không chờ — chờ 30 giây cho một SSID rỗng chỉ làm
/// người lắp đặt tưởng thiết bị treo.
/// </para>
/// </remarks>
void wifiBegin();

/// <summary>IOT3-50 — đổi mạng lúc đang chạy: lưu NVS rồi nối lại, KHÔNG khởi động lại.</summary>
/// <returns>false nếu giá trị không hợp lệ hoặc ghi NVS lỗi (mạng cũ giữ nguyên).</returns>
bool wifiReconfigure(const char* ssid, const char* password);

/// <summary>IOT3-51 — ba chế độ của máy trạng thái WiFi.</summary>
enum class WifiPhase : uint8_t {
  /// Chưa có SSID nào ⇒ mở trang cấu hình, CHỜ VÔ HẠN.
  Unconfigured = 0,
  /// Có SSID, đang thử nối (thử lại mỗi 5 s). Dưới 5 phút mất mạng vẫn ở đây.
  Connecting   = 1,
  /// Mất mạng ≥ 5 phút ⇒ VỪA phát AP cấu hình VỪA tiếp tục thử mạng cũ (`WIFI_AP_STA`).
  /// Router khách sống lại là thiết bị tự lành, không cần ai ra hiện trường.
  Recovery     = 2,
  /// Đang có mạng.
  Connected    = 3,
};

/// <summary>Gọi mỗi tick loop(). KHÔNG block.</summary>
/// <remarks>
/// Chạy máy trạng thái IOT3-51: mở/đóng trang cấu hình, thử nối lại (giới hạn 5 s một lần),
/// chuyển sang chế độ phục hồi khi mất mạng lâu. Lấy mẫu pin và xếp hàng đợi ở `loop()` vẫn
/// chạy bình thường suốt thời gian đó.
/// </remarks>
void wifiTick();

WifiPhase wifiPhase();

/// Đã mất mạng bao lâu (ms). 0 khi đang có mạng.
uint32_t wifiOfflineDurationMs();

/// Ngưỡng chuyển sang chế độ phục hồi — lấy từ `core/wifi_phase_policy.h` để chỉ có MỘT con số
/// trong cả cây mã (luật ở đó được test ở env:native).
constexpr uint32_t kRecoveryAfterOfflineMs = core::kRecoveryAfterOfflineMsDefault;

// True nếu WL_CONNECTED.
bool wifiIsConnected();

// RSSI hiện tại (dBm) hoặc 0 nếu chưa connect.
int8_t wifiRssi();

}  // namespace net
