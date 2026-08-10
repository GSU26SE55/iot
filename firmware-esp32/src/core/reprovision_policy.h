#pragma once
//
// IOT3-44 — luật THUẦN quyết định "có nên xin lại credential MQTT không".
//
// Tách khỏi main.cpp để test ở env:native, cùng khuôn với `core/ota_check_policy.h` và
// `core/ingest_policy.h`.
//
// Bài toán: broker từ chối đăng nhập (state 4/5) thì chờ bao lâu cũng vô ích — chỉ `/provision`
// cấp credential mới mới cứu được. Nhưng gọi `/provision` mỗi lần bị từ chối lại tạo một vòng lặp
// nện backend khi chính backend là thứ đang hỏng. Hai chốt chặn:
//   1. NGƯỠNG — đủ N lần từ chối LIÊN TIẾP mới đi (né trục trặc thoáng qua).
//   2. HẠ NHIỆT — đã đi rồi thì im trong T phút, dù có bị từ chối tiếp.
//

#include <cstdint>

namespace core {

/// 15 phút. Đủ dài để một lần triển khai backend hỏng không biến thành bão request, đủ ngắn để
/// sự cố thật (admin vừa xoay credential) tự lành trong một lần đi uống cà phê.
constexpr uint32_t kReprovisionCooldownMs = 15UL * 60UL * 1000UL;

/// <summary>Có nên xoá cờ provision để xin lại credential không?</summary>
/// <param name="authFailStreak">Số lần connect bị từ chối vì XÁC THỰC liên tiếp (state 4/5).</param>
/// <param name="threshold">Ngưỡng, thường là <c>net::mqttAuthFailureThreshold()</c> = 5.</param>
/// <param name="nowMs"><c>millis()</c>.</param>
/// <param name="lastReprovisionMs"><c>millis()</c> lúc lần re-provision gần nhất.</param>
/// <param name="everReprovisioned">false ở lần đầu — khi đó KHÔNG xét hạ nhiệt.</param>
/// <param name="cooldownMs">Thời gian hạ nhiệt.</param>
/// <remarks>
/// Phép trừ dưới đây cố ý làm trên <c>uint32_t</c>: <c>millis()</c> quay vòng sau ~49,7 ngày, và
/// số học không dấu cho ra đúng khoảng cách qua điểm quay vòng. So sánh kiểu
/// <c>now &gt; last + cooldown</c> thì tại thời điểm quay vòng sẽ khoá cứng re-provision suốt 49 ngày.
/// </remarks>
inline bool shouldReprovisionOnAuthFailure(uint32_t authFailStreak,
                                           int      threshold,
                                           uint32_t nowMs,
                                           uint32_t lastReprovisionMs,
                                           bool     everReprovisioned,
                                           uint32_t cooldownMs = kReprovisionCooldownMs) {
  if (threshold < 1) return false;
  if (authFailStreak < static_cast<uint32_t>(threshold)) return false;
  if (!everReprovisioned) return true;
  return (nowMs - lastReprovisionMs) >= cooldownMs;
}

}  // namespace core
