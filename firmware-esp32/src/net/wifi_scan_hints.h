#pragma once
//
// IOT3-53/55 — diễn giải kết quả quét WiFi thành lời cảnh báo người lắp đặt hiểu được.
//
// Dùng chung cho Serial CLI (`wifiscan`) và trang cấu hình. Hai cảnh báo dưới đây quyết định
// một buổi lắp đặt kéo dài 10 phút hay 2 tiếng:
//
//   1. WPA2-Enterprise — `WiFi.begin(ssid, pass)` KHÔNG hỗ trợ. Không cảnh báo thì khách gõ đi
//      gõ lại mật khẩu, và triệu chứng duy nhất là `[wifi] reconnecting...` lặp vô hạn.
//   2. Sóng yếu — thiết bị vẫn nối được lúc thử nhưng rớt lai rai về sau. Chỉ lộ ra sau nhiều ngày.
//
// Còn một cái bẫy thứ ba KHÔNG hiện được ở đây: mạng 5 GHz. ESP32-S3 chỉ có radio 2,4 GHz, nên
// mạng 5 GHz đơn giản là KHÔNG XUẤT HIỆN trong danh sách quét — không có cờ nào để đọc. Vì vậy
// mọi chỗ hiển thị danh sách PHẢI kèm câu "không thấy mạng của bạn? kiểm tra router có phát
// 2,4 GHz không", nếu không khách sẽ tưởng thiết bị hỏng.
//
#include <WiFi.h>

#include "core/net_config_rules.h"

namespace net {

/// `WiFi.begin(ssid, password)` chỉ làm được PSK. Enterprise cần cả `esp_wifi_sta_wpa2_ent_*`,
/// định danh người dùng và thường cả chứng chỉ — ngoài phạm vi thiết bị này.
inline bool isEnterpriseAuth(wifi_auth_mode_t mode) {
  return mode == WIFI_AUTH_WPA2_ENTERPRISE;
}

inline const char* describeAuthMode(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:            return "mở (không mật khẩu)";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:        return "WAPI";
    default:                        return "(không rõ)";
  }
}

/// Câu nhắc bắt buộc kèm mọi danh sách mạng — xem lý do ở đầu file.
inline const char* wifi24GhzOnlyNotice() {
  return "Không thấy mạng của bạn? Thiết bị chỉ bắt được 2,4 GHz — "
         "kiểm tra router có phát băng 2,4 GHz không.";
}

}  // namespace net
