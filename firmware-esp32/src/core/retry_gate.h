#pragma once
//
// GH-741 — "đã tới lúc thử gửi lại chưa?".
//
// Lỗi gốc: cảm biến an toàn giữ `s_pendingReport` rồi gọi lại reporter ở MỖI tick — MQ-2 mỗi
// 1s, rò nước mỗi 0,5s. Backend trả 403 (sai scope API key) là sinh ~180 request/phút, vô
// hạn, không bao giờ tự khỏi: bão log, bão mạng, tốn pin.
//
// Cổng này tách ra thành hàm THUẦN vì hai lý do: nó nằm trong file cần Arduino (không test
// được tại chỗ), và phép so mốc thời gian phải chịu được tràn `millis()` sau 49,7 ngày —
// đúng loại lỗi chỉ lộ ra sau nhiều tuần chạy ngoài hiện trường.
//

#include <cstdint>

namespace core {

/// <summary>
/// True nếu được phép thử gửi lúc <paramref name="nowMs"/>.
/// </summary>
/// <remarks>
/// So sánh qua hiệu <c>(int32_t)(now - nextAllowed)</c> thay vì <c>now >= nextAllowed</c>:
/// khi <c>millis()</c> tràn, phép so trực tiếp sẽ khoá cổng suốt ~49,7 ngày.
/// Xem hiến pháp §2 bất biến #6.
/// </remarks>
inline bool shouldAttemptReport(bool pending, uint32_t nowMs, uint32_t nextAllowedMs) {
  if (!pending) return false;
  return static_cast<int32_t>(nowMs - nextAllowedMs) >= 0;
}

}  // namespace core
