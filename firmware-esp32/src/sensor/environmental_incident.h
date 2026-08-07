// ==================================================================
// Sprint 6 — S6-FW-01/02 (#63/#64): Environmental incident reporter.
//
// Reporter DÙNG CHUNG cho MQ-2 (gas — báo GasLeak, xem NS-24 #664) + water leak
// (Flood): build payload và POST sang backend `POST /api/environmental-incidents`.
//
// ⚠ Vì sao HTTPS chứ không MQTT (dù tasksprint ghi "publish qua MQTT"):
//   Backend MQTT bridge (MqttBridgeBackgroundService) CHỈ subscribe 4 wildcard
//   telemetry / heartbeat / status / cmd-ack (verified MqttTopicMap.cs) — KHÔNG
//   có topic incident. Đường duy nhất backend nhận smoke/water là REST endpoint
//   `/api/environmental-incidents` (ApiKey + scope EnvironmentalIngest). Nhất
//   quán với SHT31 ambient (S5-FW-06) cũng đi HTTPS endpoint riêng.
//
// Backend contract (verified ReportEnvironmentalIncidentCommand + Controller):
//   POST /api/environmental-incidents
//   Auth: [Authorize(AuthenticationSchemes="ApiKey", Policy="EnvironmentalIngest")]
//   Body: { siteId(Guid required), incidentType(int enum), severity(int enum,
//           default Critical=3), reportedBy(string? ≤256), detectedAt(UTC ISO8601),
//           notes(string? ≤1000) }
//   Response: 201 Created (mới) | 200 OK (dedup — đã có incident Open/Acknowledged
//             cùng site+type → reuse). Cả hai đều coi là thành công.
//
// ⚠ DEPLOYMENT: ApiKey của device PHẢI có scope EnvironmentalIngest (bitmask=4),
//   giống SHT31. Thiếu scope → backend trả 403.
// ==================================================================
#pragma once

#include <cstdint>

namespace sensor {

// EnvironmentalIncidentTypeEnum (backend) — INT (System.Text.Json không có
// JsonStringEnumConverter → string fail, phải gửi số).
enum class IncidentType : int {
  Smoke          = 1,
  FireDetected   = 2,
  GasLeak        = 3,
  Flood          = 4,
  OverheatHazard = 5,
  Other          = 99,
};

// AlertSeverityEnum (backend): Info=1, Warning=2, Critical=3.
enum class IncidentSeverity : int {
  Info     = 1,
  Warning  = 2,
  Critical = 3,
};

// Set siteId (Guid string) — backend require. Gọi từ main.cpp sau provision
// (giống sht31SetSiteId). MQ-2 + water leak share 1 siteId cho cả device.
void envIncidentSetSiteId(const char* siteIdGuid);

// Báo cáo 1 incident lên backend. Trả true nếu HTTP 2xx (201 created hoặc
// 200 dedup). `notes` optional (nullptr OK). Caller PHẢI đảm bảo WiFi + NTP
// synced; nếu siteId rỗng hoặc NTP chưa sync → skip + log + trả false.
/// GH-736 — `detectedSecondsAgo` = sự cố xảy ra cách đây bao lâu (0 = vừa xảy ra).
/// Cần thiết vì cảm biến an toàn nay lấy mẫu cả khi offline: sự cố có thể phát hiện lúc
/// mất mạng và chỉ gửi được hàng giờ sau — ghi giờ lúc gửi là ghi sai thời điểm sự cố.
/// GH-741 — kết quả gửi, phân biệt lỗi tạm thời với lỗi vĩnh viễn.
enum class IncidentReportResult {
  Success = 1,
  /// Mạng lỗi / 5xx / 408 / 429 — thử lại CÓ tác dụng, nhưng phải có backoff.
  Transient = 2,
  /// 4xx khác (400/401/403/404/409/422) — gửi lại vẫn hỏng. Phải DỪNG, không thử lại.
  Permanent = 3,
};

IncidentReportResult envIncidentReport(IncidentType type, IncidentSeverity severity,
                                       const char* notes, uint32_t detectedSecondsAgo = 0);

/// Số incident bị BỎ vì lỗi vĩnh viễn (sai scope API key, chưa provision…).
uint32_t envIncidentDroppedCount();

uint32_t envIncidentReportOkCount();
uint32_t envIncidentReportFailCount();

}  // namespace sensor
