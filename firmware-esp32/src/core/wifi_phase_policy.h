#pragma once
//
// IOT3-51/90 — luật THUẦN của máy trạng thái WiFi.
//
// Tách khỏi `net/wifi_manager.cpp` (file đó chạm driver WiFi) để test được ở env:native.
//
// Ba quyết định gộp vào một hàm, vì tách rời chúng ra là đúng chỗ sinh lỗi:
//   - Chưa có SSID  ⇒ mở trang cài đặt, chờ vô hạn.
//   - Mất mạng NGẮN ⇒ chỉ thử lại; KHÔNG mở AP. Mở AP mỗi lần router khách khởi động lại
//     sẽ làm điện thoại của khách nhảy vào AP của thiết bị và mất mạng nhà.
//   - Mất mạng DÀI  ⇒ vừa phát AP vừa TIẾP TỤC thử mạng cũ, để router sống lại là tự lành.
//

#include <cstdint>

namespace core {

enum class WifiPhaseDecision : uint8_t {
  Unconfigured = 0,
  Connecting   = 1,
  Recovery     = 2,
  Connected    = 3,
};

/// 5 phút. Ngắn hơn thì AP bật lên vì những lần rớt mạng vặt; dài hơn thì người đứng cạnh
/// thiết bị phải chờ quá lâu mới có đường vào để sửa.
constexpr uint32_t kRecoveryAfterOfflineMsDefault = 5UL * 60UL * 1000UL;

/// <summary>Chọn chế độ từ ba dữ kiện.</summary>
/// <param name="hasWifiConfig">NVS (hoặc compile-time) có SSID dùng được không.</param>
/// <param name="isConnected">Đang có link WiFi.</param>
/// <param name="offlineMs">Đã mất mạng bao lâu. Bỏ qua khi <paramref name="isConnected"/>.</param>
/// <remarks>
/// Thứ tự xét CÓ Ý NGHĨA: `isConnected` đứng trước `hasWifiConfig`. Đang chạy bằng SSID
/// compile-time (NVS trống) mà lại rơi vào nhánh "chưa cấu hình" thì thiết bị sẽ mở AP và
/// tự cắt kết nối đang tốt của chính nó.
/// </remarks>
inline WifiPhaseDecision decideWifiPhase(bool hasWifiConfig, bool isConnected, uint32_t offlineMs,
                                         uint32_t recoveryAfterMs = kRecoveryAfterOfflineMsDefault) {
  if (isConnected)     return WifiPhaseDecision::Connected;
  if (!hasWifiConfig)  return WifiPhaseDecision::Unconfigured;
  return offlineMs >= recoveryAfterMs ? WifiPhaseDecision::Recovery
                                      : WifiPhaseDecision::Connecting;
}

/// Có nên đang mở trang cấu hình ở chế độ này không.
inline bool phaseWantsPortal(WifiPhaseDecision p) {
  return p == WifiPhaseDecision::Unconfigured || p == WifiPhaseDecision::Recovery;
}

/// Có nên giữ chế độ station (tiếp tục thử mạng cũ) không.
/// LUÔN true trừ khi chưa có cấu hình gì để mà thử — đây chính là điều làm `Recovery` khác
/// với "chuyển hẳn sang AP".
inline bool phaseKeepsStation(WifiPhaseDecision p) {
  return p != WifiPhaseDecision::Unconfigured;
}

}  // namespace core
