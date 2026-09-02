// ==================================================================
// Sprint 6 — S6-FW-02 (#64): Water leak sensor.
//
// Cảm biến rò nước digital (comparator onboard) → DO. ESP32 đọc GPIO2; khi chuyển
// cạnh khô→ướt → report EnvironmentalIncident(Flood) lên backend qua reporter dùng
// chung (environmental_incident, HTTPS).
//
// Hardware (WD §4.3):
//   Water DO/AO → ESP32-S3 GPIO2. VCC = 3V3, GND chung.
//   Module có loại ướt→HIGH, loại ướt→LOW → cấu hình WATER_LEAK_ACTIVE_HIGH.
//
// Chống spam: IncidentTrigger fire 1 lần ở cạnh lên + cooldown (WATER_LEAK_REARM_
// COOLDOWN_MS); backend cũng dedup theo (site, incidentType).
//
// siteId dùng CHUNG với MQ-2 qua envIncidentSetSiteId() (1 siteId/device) — water
// leak KHÔNG có setter riêng.
//
// Tham chiếu: tasksprint.md S6-FW-02, WD §4.3.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

// Init GPIO (INPUT_PULLUP). Trả false nếu WATER_LEAK_ENABLED=0.
bool waterLeakBegin();

// Gọi mỗi loop tick. Tự throttle WATER_LEAK_POLL_INTERVAL_MS. Khi cạnh khô→ướt
// → report Flood. Caller PHẢI đảm bảo WiFi + NTP synced (no-op report nếu chưa).
void waterLeakTick();

// Trạng thái ướt hiện tại (mẫu đọc gần nhất). false nếu chưa init.
bool waterLeakIsWet();

uint32_t waterLeakReportCount();   // số lần đã report incident

// Đã lấy được ít nhất một mẫu chưa. Trước mẫu đầu tiên, trạng thái "khô" chỉ là giá trị
// khởi tạo chứ không phải số đo, nên báo cáo ambient bỏ hẳn trường nước cho tới khi có mẫu.
bool waterLeakHasSample();

}  // namespace sensor
