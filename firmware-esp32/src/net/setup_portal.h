// ==================================================================
// IOT3-52/53 — trang cấu hình tại chỗ (captive portal).
//
// Thiết bị tự phát một điểm phát sóng tên `SolarGW-XXXX`; kỹ thuật viên (hoặc chính khách hàng)
// nối điện thoại vào đó, điền SSID + mật khẩu WiFi nhà mình, kèm mã thiết bị và API key. Đây là
// đường DUY NHẤT để nạp thông tin vào thiết bị mà KHÔNG phải cắm cáp và nạp lại firmware.
//
// ⚠️ KHÔNG CHẶN `loop()`. Chạy `WiFiManager` ở chế độ non-blocking rồi bơm bằng `portalTick()`:
//    trong lúc chờ người ta điền form, thiết bị vẫn lấy mẫu pin và xếp vào hàng đợi. Dùng
//    `autoConnect()` (chặn) sẽ làm mất toàn bộ số đo suốt thời gian mở trang — có thể hàng giờ.
//
// Hai chế độ, khác nhau ở HẠN GIỜ:
//   - `FirstTimeSetup` — chưa có WiFi nào trong NVS. Chờ VÔ HẠN: chưa cấu hình thì tự đóng
//     trang đi cũng chẳng để làm gì, mà lại làm người lắp đặt mất đường vào.
//   - `Recovery` — đã từng chạy được nhưng mất mạng lâu. Hạn 10 phút rồi đóng, để thiết bị quay
//     về dồn sức thử lại mạng cũ (router khách sống lại là tự lành).
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace net {

enum class PortalMode : uint8_t {
  Off            = 0,
  FirstTimeSetup = 1,   // chờ vô hạn
  Recovery       = 2,   // hạn 10 phút
};

/// Tên AP: `SolarGW-` + 4 ký tự cuối MAC. Con trỏ tới buffer static.
/// Có hậu tố MAC để nhiều thiết bị bật cùng lúc không trùng tên — trùng tên thì kỹ thuật viên
/// không có cách nào biết mình đang cấu hình cái nào.
const char* portalApSsid();

/// <summary>Mở trang cấu hình. Trả false nếu đã mở sẵn hoặc mật khẩu AP không hợp lệ.</summary>
bool portalStart(PortalMode mode);

/// Bơm state machine của WiFiManager. Gọi MỖI vòng loop khi trang đang mở.
void portalTick();

/// Đóng trang + trả WiFi về chế độ station.
void portalStop();

bool       portalIsActive();
PortalMode portalMode();

/// Thời điểm (millis) trang được mở — dùng cho log và LED.
uint32_t portalStartedAtMs();

}  // namespace net
