#pragma once
//
// GH-749 — kiểm giá trị định danh TRƯỚC khi ghi vào NVS.
//
// Lỗi gốc: `setDeviceCode()`/`setApiKey()` in ra chữ "truncate" rồi… ghi NGUYÊN chuỗi dài vào
// NVS, chỉ cắt ở buffer RAM, và vẫn trả `true`. Hậu quả: NVS giữ một giá trị mà firmware
// không bao giờ dùng trọn được; sau khi khởi động lại, giá trị nạp ra khác hẳn giá trị đang
// chạy lúc người dùng thấy báo "thành công".
//
// Ngoài độ dài còn phải kiểm KÝ TỰ: apiKey được nhét thẳng vào header `X-Api-Key`, nên một
// ký tự CR/LF lọt vào là **tiêm header HTTP**. Khoảng trắng cũng không hợp lệ vì deviceCode
// còn được ghép vào topic MQTT.
//
// Hàm THUẦN ⇒ test được ở env:native.
//

#include <cstddef>

namespace core {

enum class IdentityFieldError {
  Ok = 0,
  Empty = 1,
  /// Không vừa buffer (kể cả ký tự kết thúc chuỗi).
  TooLong = 2,
  /// Có ký tự điều khiển, khoảng trắng, hoặc ngoài bảng ASCII in được.
  InvalidChar = 3,
};

/// <summary>
/// Kiểm một giá trị định danh.
/// </summary>
/// <param name="bufferSize">
/// Kích thước buffer đích (đã tính ký tự kết thúc). Giá trị dùng được dài tối đa
/// <c>bufferSize - 1</c> — khớp đúng điều kiện <c>strlen(v) >= kMax…</c> của code cũ.
/// </param>
/// <remarks>
/// Chỉ nhận ASCII in được KHÔNG kể khoảng trắng (0x21–0x7E). Đủ rộng cho mọi giá trị backend
/// sinh ra (<c>iotk_</c> + base64url, và deviceCode dạng <c>gw-esp32-mvp-001</c>), đủ chặt để
/// chặn CR/LF/tab/space — thứ làm hỏng header HTTP lẫn topic MQTT.
/// </remarks>
inline IdentityFieldError validateIdentityField(const char* value, size_t bufferSize) {
  if (value == nullptr || value[0] == '\0') return IdentityFieldError::Empty;
  if (bufferSize < 2) return IdentityFieldError::TooLong;

  size_t len = 0;
  for (; value[len] != '\0'; ++len) {
    if (len >= bufferSize - 1) return IdentityFieldError::TooLong;

    const unsigned char c = static_cast<unsigned char>(value[len]);
    if (c < 0x21 || c > 0x7E) return IdentityFieldError::InvalidChar;
  }
  return IdentityFieldError::Ok;
}

/// Mô tả lỗi để in ra CLI — người dùng phải biết vì sao bị từ chối.
inline const char* describeIdentityError(IdentityFieldError e) {
  switch (e) {
    case IdentityFieldError::Ok:          return "hợp lệ";
    case IdentityFieldError::Empty:       return "giá trị rỗng";
    case IdentityFieldError::TooLong:     return "quá dài, không vừa bộ nhớ thiết bị";
    case IdentityFieldError::InvalidChar: return "chứa khoảng trắng hoặc ký tự không in được";
  }
  return "không rõ";
}

}  // namespace core
