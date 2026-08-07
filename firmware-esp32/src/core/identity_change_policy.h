#pragma once
//
// GH-747 — có được phép đổi deviceCode lúc đang chạy không?
//
// Lỗi gốc: CLI `set devcode` đổi deviceCode runtime rồi in "hot reloaded", nhưng
//   - username/password/clientId MQTT là **macro compile-time**, không đổi theo;
//   - phiên MQTT đang mở vẫn giữ LWT và subscription của deviceCode CŨ.
//
// Kết quả sau khi đổi: telemetry publish sang topic MỚI trong khi broker ACL vẫn khoá theo
// username CŨ ⇒ bị từ chối; và lệnh downlink vẫn về topic CŨ ⇒ không bao giờ tới nơi.
// Thiết bị "chạy" nhưng câm cả hai chiều — mà log lại báo thành công.
//
// Quy ước backend (IotApiKeyService.GenerateMqttCredential):
//     mqtt_username = lowercase(deviceCode)
// nên deviceCode mới CHỈ dùng được nếu lowercase của nó khớp username đang cấu hình.
//
// Hàm THUẦN ⇒ test được ở env:native.
//

#include <cstddef>
#include "core/identity_validation.h"

namespace core {

enum class IdentityChangeDecision {
  /// Đổi được: hợp lệ và khớp ACL (hoặc MQTT không dùng).
  Accept = 1,
  /// Chuỗi rỗng, quá dài, hoặc có ký tự không dùng được (xem `validateIdentityField`).
  RejectInvalid = 2,
  /// lowercase(deviceCode mới) != mqtt_username ⇒ đổi xong sẽ câm cả hai chiều.
  RejectAclMismatch = 3,
};

/// Số KÝ TỰ tối đa, theo cột `device_code` phía backend (`HasMaxLength(64)`).
/// Buffer NVS của firmware phải rộng hơn 1 byte — xem `identity::kMaxDeviceCodeLen`.
constexpr size_t kMaxDeviceCodeLen = 64;

namespace detail {

constexpr char toLowerAscii(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// So sánh không phân biệt hoa thường, CHỈ theo bảng ASCII.
/// Cố ý không dùng locale: username của broker do backend sinh bằng
/// `ToLowerInvariant()`, nên phải so đúng kiểu bất biến, không theo locale máy.
inline bool equalsIgnoreCaseAscii(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  size_t i = 0;
  for (; a[i] != '\0' && b[i] != '\0'; ++i) {
    if (toLowerAscii(a[i]) != toLowerAscii(b[i])) return false;
  }
  return a[i] == '\0' && b[i] == '\0';
}

}  // namespace detail

/// <summary>Quyết định cho một lần đổi deviceCode lúc đang chạy.</summary>
/// <param name="mqttEnabled">
/// False khi build không dùng MQTT — khi đó ACL không liên quan, chỉ cần chuỗi hợp lệ.
/// </param>
inline IdentityChangeDecision decideDeviceCodeChange(const char* newCode,
                                                     const char* mqttUsername,
                                                     bool mqttEnabled) {
  // GH-749 — dùng CHUNG bộ kiểm với lúc ghi NVS, để CLI không thể "duyệt" một giá trị mà
  // tầng lưu trữ sẽ từ chối. Bộ kiểm này chặn cả khoảng trắng và ký tự điều khiển: deviceCode
  // còn được ghép vào topic MQTT nên một ký tự xuống dòng lọt vào là hỏng cả phiên.
  if (validateIdentityField(newCode, kMaxDeviceCodeLen + 1) != IdentityFieldError::Ok) {
    return IdentityChangeDecision::RejectInvalid;
  }

  if (!mqttEnabled) return IdentityChangeDecision::Accept;

  return detail::equalsIgnoreCaseAscii(newCode, mqttUsername)
             ? IdentityChangeDecision::Accept
             : IdentityChangeDecision::RejectAclMismatch;
}

}  // namespace core
