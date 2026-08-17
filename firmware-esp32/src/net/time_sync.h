// ==================================================================
// Sprint 1 — S1-FW-03: NTP time sync + ISO8601 helper
//
// Tham chiếu:
//   - tasksprint.md S1-FW-03
//   - newiot.md §12 #1 (bẫy clock skew → reading bị backend reject nếu lệch > 5 phút)
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace net {

// Gọi 1 lần trong setup(), SAU khi wifiBegin() trả. Không block.
// Sử dụng configTime() — ESP32 IDF SNTP chạy ngầm.
void timeSyncBegin();

// Gọi mỗi tick loop(). Nếu chưa sync sau khi WiFi up → request lại
// (configTime ngắt nếu rớt WiFi giữa chừng). Throttle 5s.
void timeSyncTick();

// True nếu RTC đã có giờ hợp lý (year >= 2024).
bool timeIsSynced();

// Trả epoch UNIX (giây). 0 nếu chưa sync.
uint32_t timeEpoch();

// Format giờ UTC hiện tại sang ISO8601 RFC3339 ("2026-06-13T08:15:42Z").
// outBuf phải ≥ 21 byte. Trả false nếu chưa sync (outBuf để rỗng).
bool isoNow(char* outBuf, size_t outBufLen);

/// GH-736 — như isoNow() nhưng lùi lại `secondsAgo` giây.
/// Dùng khi sự cố được PHÁT HIỆN lúc offline và chỉ GỬI ĐƯỢC sau khi có mạng: phải ghi
/// đúng thời điểm phát hiện, không phải thời điểm gửi.
bool isoNowMinus(uint32_t secondsAgo, char* outBuf, size_t outBufLen);

}  // namespace net
