#pragma once
//
// IOT3-36/37 — luật THUẦN cho cấu hình mạng runtime (WiFi + MQTT).
//
// Tách riêng khỏi `config/wifi_config.cpp` và `config/mqtt_config.cpp` vì hai file đó chạm NVS
// (Preferences của ESP32) nên không biên dịch được ở env:native. Mọi quyết định "giá trị này có
// dùng được không / tiền tố topic là gì" nằm ở đây để test bằng Unity trên laptop — cùng khuôn
// với `core/identity_validation.h` và `net/tls_ca.cpp`.
//

#include <cstddef>
#include <cstdint>

namespace core {

// ===================================================================== WiFi

/// Độ dài tối đa theo chuẩn 802.11: SSID 32 byte, PSK 63 ký tự (WPA2-PSK).
constexpr size_t kMaxWifiSsidChars = 32;
constexpr size_t kMaxWifiPassChars = 63;

enum class WifiFieldError : uint8_t {
  Ok = 0,
  Empty = 1,
  TooLong = 2,
  /// Có ký tự điều khiển (CR/LF/tab/NUL giữa chuỗi…).
  InvalidChar = 3,
};

/// <summary>
/// Kiểm SSID / mật khẩu WiFi.
/// </summary>
/// <remarks>
/// <para>
/// <b>KHÔNG dùng <c>validateIdentityField</c> cho WiFi.</b> Hàm đó chỉ nhận 0x21–0x7E, tức là
/// TỪ CHỐI KHOẢNG TRẮNG — trong khi SSID có dấu cách là chuyện thường ngày (<c>"My Home WiFi"</c>,
/// <c>"Nha Anh Nam 2.4G"</c>). Dùng nhầm sẽ chặn đúng những mạng phổ biến nhất, và người lắp đặt
/// sẽ tưởng thiết bị hỏng.
/// </para>
/// <para>
/// Ngược lại vẫn phải chặn ký tự điều khiển: chuỗi này đi vào <c>WiFi.begin()</c> và cả trang
/// cấu hình HTML, một ký tự CR/LF lọt vào là hỏng cả hai.
/// </para>
/// <param name="allowEmpty">
/// Mật khẩu rỗng là HỢP LỆ với mạng mở (không mã hoá) ⇒ truyền <c>true</c> khi kiểm mật khẩu.
/// SSID rỗng thì không bao giờ hợp lệ.
/// </param>
/// </remarks>
inline WifiFieldError validateWifiField(const char* value, size_t maxChars, bool allowEmpty) {
  if (value == nullptr) return WifiFieldError::Empty;
  if (value[0] == '\0') return allowEmpty ? WifiFieldError::Ok : WifiFieldError::Empty;

  size_t len = 0;
  for (; value[len] != '\0'; ++len) {
    if (len >= maxChars) return WifiFieldError::TooLong;
    const unsigned char c = static_cast<unsigned char>(value[len]);
    // 0x20 (space) ĐƯỢC PHÉP — khác validateIdentityField. 0x7F là DEL, vẫn là ký tự điều khiển.
    if (c < 0x20 || c == 0x7F) return WifiFieldError::InvalidChar;
  }
  return WifiFieldError::Ok;
}

inline const char* describeWifiError(WifiFieldError e) {
  switch (e) {
    case WifiFieldError::Ok:          return "hợp lệ";
    case WifiFieldError::Empty:       return "giá trị rỗng";
    case WifiFieldError::TooLong:     return "quá dài (SSID tối đa 32, mật khẩu tối đa 63 ký tự)";
    case WifiFieldError::InvalidChar: return "chứa ký tự điều khiển";
  }
  return "không rõ";
}

/// WiFi coi là "đã cấu hình" khi CÓ SSID. Mật khẩu rỗng vẫn hợp lệ (mạng mở).
inline bool wifiConfigUsable(const char* ssid) {
  return validateWifiField(ssid, kMaxWifiSsidChars, /*allowEmpty=*/false) == WifiFieldError::Ok;
}

/// <summary>Ngưỡng "sóng yếu" cảnh báo cho người lắp đặt (IOT3-53).</summary>
/// <remarks>
/// −75 dBm là ranh giới thực dụng: trên mức này TCP/TLS còn ổn định; dưới mức này thiết bị vẫn
/// nối được lúc thử nhưng sẽ rớt lai rai khi có người đi qua hoặc lò vi sóng chạy — và triệu chứng
/// ngoài hiện trường chỉ là "thỉnh thoảng mất dữ liệu", gần như không truy được từ xa.
/// </remarks>
constexpr int kWeakRssiDbm = -75;

inline bool wifiSignalIsWeak(int rssiDbm) { return rssiDbm < kWeakRssiDbm; }

// ===================================================================== MQTT

constexpr size_t kMaxMqttHostChars   = 64;
constexpr size_t kMaxMqttUserChars   = 64;
constexpr size_t kMaxMqttPassChars   = 64;
/// "solar/" + deviceCode(64) → 70; lấy 96 cho dư.
constexpr size_t kMaxMqttPrefixChars = 96;

/// Cổng MQTT hợp lệ. 0 là "chưa đặt", không phải cổng.
inline bool mqttPortUsable(int port) { return port > 0 && port <= 65535; }

/// <summary>
/// MQTT coi là "đã cấu hình" khi có ĐỦ host + cổng + username + mật khẩu.
/// </summary>
/// <remarks>
/// Đủ CẢ BỐN mới tính, vì thiếu bất kỳ cái nào thì <c>PubSubClient::connect()</c> cũng thất bại —
/// nhưng thất bại theo kiểu im lặng lặp lại mỗi 5 giây, rất tốn công truy. Backend đã cam kết trả
/// "cả sáu trường hoặc không trường nào", nên trạng thái nửa vời ở đây nghĩa là NVS hỏng hoặc
/// người dùng mới đặt tay một nửa qua CLI.
/// </remarks>
inline bool mqttConfigUsable(const char* host, int port, const char* user, const char* pass) {
  if (host == nullptr || host[0] == '\0') return false;
  if (!mqttPortUsable(port)) return false;
  if (user == nullptr || user[0] == '\0') return false;
  if (pass == nullptr || pass[0] == '\0') return false;
  return true;
}

// ===================================================================== mDNS endpoint

/// True khi hostname dùng miền link-local `.local` (không phân biệt hoa/thường).
/// Router gia đình thường trả NXDOMAIN cho tên này; firmware phải hỏi mDNS multicast.
inline bool isMdnsHostname(const char* host) {
  if (host == nullptr) return false;
  size_t len = 0;
  while (host[len] != '\0') ++len;
  constexpr char suffix[] = ".local";
  constexpr size_t suffixLen = sizeof(suffix) - 1;
  if (len <= suffixLen) return false;  // cần ít nhất một ký tự trước `.local`

  const size_t start = len - suffixLen;
  for (size_t i = 0; i < suffixLen; ++i) {
    char c = host[start + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != suffix[i]) return false;
  }
  return true;
}

/// Bỏ hậu tố `.local` để tạo label truyền cho `MDNS.queryHost()`.
/// Trả số ký tự đã ghi, hoặc 0 nếu hostname/buffer không hợp lệ.
inline size_t mdnsQueryLabel(const char* host, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return 0;
  out[0] = '\0';
  if (!isMdnsHostname(host)) return 0;

  size_t len = 0;
  while (host[len] != '\0') ++len;
  constexpr size_t suffixLen = sizeof(".local") - 1;
  const size_t labelLen = len - suffixLen;
  if (labelLen == 0 || labelLen >= outLen) return 0;

  for (size_t i = 0; i < labelLen; ++i) out[i] = host[i];
  out[labelLen] = '\0';
  return labelLen;
}

/// <summary>
/// Suy tiền tố topic từ deviceCode khi backend chưa cấp (chưa provision, hoặc bản backend cũ).
/// </summary>
/// <remarks>
/// Quy ước phải KHỚP <c>MqttBrokerEndpointProvider.TopicPrefixFor()</c> của backend:
/// <c>"solar/" + deviceCode.Trim().ToLowerInvariant()</c>. ACL Mosquitto dùng
/// <c>pattern write solar/%u/...</c> với <c>%u</c> = username = deviceCode chữ thường; so khớp
/// topic MQTT phân biệt hoa/thường và KHÔNG tắt được, nên sai chữ hoa là mất cả uplink lẫn downlink
/// mà không bên nào báo lỗi.
/// </remarks>
/// <returns>Số ký tự đã ghi (không kể ký tự kết thúc), 0 nếu buffer không đủ.</returns>
inline size_t deriveTopicPrefix(const char* deviceCode, char* out, size_t outLen) {
  if (out == nullptr || outLen == 0) return 0;
  out[0] = '\0';
  if (deviceCode == nullptr) return 0;

  const char kRoot[] = "solar/";
  const size_t rootLen = sizeof(kRoot) - 1;
  if (outLen <= rootLen) return 0;

  size_t i = 0;
  for (; i < rootLen; ++i) out[i] = kRoot[i];

  // Bỏ khoảng trắng đầu/cuối rồi hạ chữ thường — khớp Trim().ToLowerInvariant() của backend.
  size_t b = 0;
  while (deviceCode[b] == ' ' || deviceCode[b] == '\t') ++b;
  size_t e = b;
  while (deviceCode[e] != '\0') ++e;
  while (e > b && (deviceCode[e - 1] == ' ' || deviceCode[e - 1] == '\t')) --e;

  for (size_t k = b; k < e; ++k) {
    if (i >= outLen - 1) { out[0] = '\0'; return 0; }   // không vừa ⇒ trả rỗng, đừng cắt cụt
    char c = deviceCode[k];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    out[i++] = c;
  }
  out[i] = '\0';

  // Chỉ có "solar/" mà không có mã thiết bị ⇒ vô dụng. PHẢI xoá luôn buffer, không chỉ trả 0:
  // caller nào bỏ qua giá trị trả về sẽ dùng "solar/" làm tiền tố và publish lên `solar//telemetry`
  // — broker chặn theo ACL trong im lặng, không có gì trong log chỉ ra nguyên nhân.
  if (i == rootLen) { out[0] = '\0'; return 0; }
  return i;
}

}  // namespace core
