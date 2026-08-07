#pragma once
//
// GH-736 — tính "cách đây bao lâu" từ hai mốc millis(), an toàn với tràn số.
//
// Vì sao cần: cảm biến an toàn (MQ-2, rò nước) nay lấy mẫu cả khi mất mạng, nên sự cố có
// thể được PHÁT HIỆN lúc offline nhưng chỉ GỬI ĐƯỢC sau khi có mạng — có khi hàng giờ sau.
// Nếu lúc gửi mới lấy giờ hiện tại thì backend ghi nhận sai thời điểm sự cố, làm hỏng cả
// điều tra lẫn tương quan với dữ liệu telemetry.
//
// Hàm THUẦN, không Arduino ⇒ test được ở env:native.
//

#include <cstdint>

namespace core {

/// <summary>
/// Số mili-giây đã trôi qua giữa <paramref name="startMs"/> và <paramref name="nowMs"/>.
/// </summary>
/// <remarks>
/// Phép trừ trên kiểu KHÔNG DẤU tự đúng khi millis() tràn sau ~49,7 ngày:
/// (uint32_t)(10 - 4294967290) = 16. Đây là lý do KHÔNG được đổi sang kiểu có dấu
/// hay so sánh trực tiếp `nowMs >= startMs` (xem hiến pháp §2 bất biến #6).
/// </remarks>
inline uint32_t elapsedMs(uint32_t startMs, uint32_t nowMs) {
  return nowMs - startMs;
}

/// <summary>Như trên nhưng quy ra giây (làm tròn xuống).</summary>
inline uint32_t elapsedSeconds(uint32_t startMs, uint32_t nowMs) {
  return elapsedMs(startMs, nowMs) / 1000U;
}

}  // namespace core
