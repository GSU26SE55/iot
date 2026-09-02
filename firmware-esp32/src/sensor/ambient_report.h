// ==================================================================
// Báo cáo môi trường: gom nhiệt độ + khí gas + rò nước vào MỘT lần gửi.
//
// Vì sao gom: trước đây mỗi cảm biến tự giữ đồng hồ riêng và tự POST riêng. Ba đồng hồ đó trôi
// lệch nhau (mỗi lần gửi bị chặn bởi HTTP, còn DS18B20 chặn thêm 750 ms để chuyển đổi 12-bit),
// nên ở nhịp 15 s ba lần gửi rải ra cách nhau 5–6 s — trải gần bằng cả chu kỳ. Hậu quả: mỗi chu
// kỳ đọc đẻ ba hàng DB rời rạc, mỗi hàng chỉ có một cột có số, và KHÔNG cách nào gộp lại theo
// thời gian cho đúng (nới cửa sổ đủ rộng để gom một chu kỳ thì nó nuốt luôn chu kỳ kế bên).
//
// Gộp tại nguồn giải quyết dứt điểm: một mốc thời gian, một hàng, ba cột — và giảm luôn lưu
// lượng xuống còn 1/3 số request.
//
// Vẫn giữ tính độc lập giữa các cảm biến: cảm biến nào chưa sẵn sàng (DS18B20 chưa dò ra, MQ-2
// còn warm-up) thì trường đó vắng mặt trong payload, hai cảm biến còn lại vẫn được gửi.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

// Set siteId (Guid string) — gọi sau provision. Một siteId dùng chung cho cả ba cảm biến.
void ambientReportSetSiteId(const char* siteIdGuid);

// Gọi mỗi loop tick; tự throttle theo AMBIENT_POST_INTERVAL_MS. Caller phải đảm bảo WiFi + NTP
// đã sẵn sàng. No-op nếu chưa có siteId, chưa có NTP, hoặc không cảm biến nào có số liệu.
void ambientReportTick();

uint32_t ambientReportOkCount();
uint32_t ambientReportFailCount();

}  // namespace sensor
