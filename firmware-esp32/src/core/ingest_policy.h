#pragma once
//
// GH-737 — quyết định "làm gì với một chu kỳ lấy mẫu" tách khỏi main loop.
//
// Vì sao tách: lỗi gốc nằm ở đúng ba dòng điều kiện trong `loop()` — mất Wi-Fi thì chỉ
// `s_failCount++`, không đọc BMS, không xếp hàng, nên toàn bộ telemetry trong khoảng mất
// mạng biến mất. Điều kiện đó nằm trong main.cpp (cần Arduino) nên không test được. Đưa ra
// đây thành hàm THUẦN thì bắt được cả lớp lỗi này ở env:native.
//

namespace core {

/// Hành động cho một chu kỳ lấy mẫu.
enum class IngestAction {
  /// Có mạng + có giờ ⇒ đọc BMS rồi gửi thẳng (MQTT, fallback HTTPS).
  PostOnline = 1,

  /// Mất mạng nhưng đồng hồ còn ⇒ VẪN đọc BMS, xếp vào hàng đợi bền vững, đẩy bù sau.
  QueueOffline = 2,

  /// Chưa từng đồng bộ NTP ⇒ không có mốc thời gian hợp lệ, không tạo được bản ghi.
  SkipNoClock = 3,
};

/// <summary>
/// Chọn hành động cho chu kỳ hiện tại.
/// </summary>
/// <remarks>
/// Đồng hồ hệ thống ESP32 vẫn chạy sau khi mất Wi-Fi (NTP chỉ ĐẶT giờ một lần), nên
/// <paramref name="timeSynced"/> vẫn đúng trong lúc offline — đó là lý do
/// <see cref="IngestAction::QueueOffline"/> tồn tại được: bản ghi xếp hàng vẫn có mốc
/// thời gian thật, không phải giờ lúc đẩy bù.
/// </remarks>
constexpr IngestAction ingestAction(bool wifiConnected, bool timeSynced) {
  if (!timeSynced) return IngestAction::SkipNoClock;
  return wifiConnected ? IngestAction::PostOnline : IngestAction::QueueOffline;
}

}  // namespace core
