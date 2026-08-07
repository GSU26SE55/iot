#pragma once
//
// GH-745 — quyết định "có chạy firmware-check ngay bây giờ không".
//
// Lỗi gốc: lệnh từ xa `trigger_ota` chỉ in log "PLACEHOLDER" rồi ACK **"ok"**. OTA thật đã
// được làm ở Sprint 7 nhưng chỗ này không bao giờ được nối vào, nên người vận hành bấm
// "cập nhật ngay" thấy báo thành công mà thiết bị chẳng làm gì — vẫn đợi lịch 1 giờ.
//
// Tách thành hàm THUẦN vì nó gộp 5 điều kiện (bật/tắt, verify-mode, ép chạy, uptime tối
// thiểu, khoảng định kỳ) và phải chịu được tràn `millis()`. Nằm trong ota_update.cpp thì
// không test được.
//

#include <cstdint>

namespace core {

/// Kết quả quyết định — nêu RÕ lý do để ACK trả về đúng nguyên nhân từ chối.
enum class OtaCheckDecision {
  /// Chạy firmware-check ngay.
  Run = 1,
  /// OTA bị tắt bằng cấu hình (OTA_ENABLED=0).
  SkipDisabled = 2,
  /// Đang xác minh bản vừa flash — KHÔNG được chồng thêm bản mới.
  SkipVerifying = 3,
  /// Thiết bị vừa khởi động, chờ Wi-Fi/NTP/provision ổn định.
  SkipWarmup = 4,
  /// Chưa tới hạn định kỳ (và không có yêu cầu ép chạy).
  SkipTooSoon = 5,
};

struct OtaCheckInputs {
  bool     enabled = true;
  bool     verifying = false;
  /// Có lệnh `trigger_ota` đang chờ ⇒ bỏ qua khoảng chờ định kỳ.
  bool     forced = false;
  /// 0 = chưa từng check.
  uint32_t lastCheckMs = 0;
  uint32_t nowMs = 0;
  uint32_t intervalMs = 3600000UL;
  uint32_t warmupMs = 30000UL;
};

/// <summary>Quyết định cho một tick.</summary>
/// <remarks>
/// Thứ tự ưu tiên có chủ ý: <c>enabled</c> → <c>verifying</c> → <c>forced</c> → thời gian.
/// <b>forced KHÔNG vượt qua verifying</b>: đang xác minh bản vừa flash mà tải tiếp bản mới
/// là mất luôn đường lùi khi bản mới hỏng.
/// Riêng warm-up thì forced ĐƯỢC vượt: người vận hành bấm "cập nhật ngay" ngay sau khi cắm
/// điện là tình huống bình thường.
/// </remarks>
constexpr OtaCheckDecision decideOtaCheck(const OtaCheckInputs& in) {
  if (!in.enabled) return OtaCheckDecision::SkipDisabled;
  if (in.verifying) return OtaCheckDecision::SkipVerifying;
  if (in.forced) return OtaCheckDecision::Run;

  if (in.lastCheckMs == 0) {
    return in.nowMs < in.warmupMs ? OtaCheckDecision::SkipWarmup : OtaCheckDecision::Run;
  }

  // Hiệu không dấu ⇒ đúng cả khi millis() tràn sau 49,7 ngày (hiến pháp §2 bất biến #6).
  return (in.nowMs - in.lastCheckMs) >= in.intervalMs
             ? OtaCheckDecision::Run
             : OtaCheckDecision::SkipTooSoon;
}

}  // namespace core
