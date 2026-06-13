// ==================================================================
// Sprint 2 — S2-FW-02: Provision flow
//
// Boot 1 lần đầu (sau provision chưa có) → POST /api/iot-devices/provision
// → lưu `pollingIntervalSeconds`, `heartbeatIntervalSeconds`, `ntpServer`,
// `siteId` về NVS (key "provd"=1, "pollIntS", "hbIntS", "ntpsv", "siteid").
//
// Lần boot sau: đọc từ NVS, KHÔNG gọi lại provision.
//
// Tham chiếu:
//   - tasksprint.md S2-FW-02 (AC: log "provisioned, polling=5s")
//   - overall.iot.md §B1
//   - backend api-battery.md POST /api/iot-devices/provision
//   - backend IotDeviceProvisionResultDto fields
// ==================================================================
#pragma once
#include <cstddef>
#include <cstdint>

namespace provision {

// Cấu hình runtime sau provision — load từ NVS khi đã provisioned.
struct ProvisionedConfig {
  bool     provisioned;             // có flag trong NVS không
  uint32_t pollingIntervalMs;       // pollingIntervalSeconds * 1000
  uint32_t heartbeatIntervalMs;     // heartbeatIntervalSeconds * 1000
  char     siteId[40];              // Guid string
  char     ntpServer[64];           // optional override
};

// Default values khi backend không trả (theo MO §52.4):
constexpr uint32_t kDefaultPollingMs    = 5000UL;     // S1-FW-07 spec 5s
constexpr uint32_t kDefaultHeartbeatMs  = 60000UL;    // S2-FW-03 spec 60s
constexpr const char* kDefaultNtpServer = "time.google.com";  // backend default

// Đọc trạng thái provision từ NVS. Luôn fill config với defaults trước khi đọc.
// Trả `cfg.provisioned = true` nếu NVS flag "provd"=1.
void loadProvisioned(ProvisionedConfig& cfg);

// Gọi POST /api/iot-devices/provision (1 lần, ngắt nếu fail).
// Body: { FirmwareVersion, HardwareRevision, DeviceTimestamp }.
// Response 200: parse `pollingIntervalSeconds`, `heartbeatIntervalSeconds`,
// `siteId`, `ntpServer` → ghi vào NVS, set flag "provd"=1.
//
// Trả true nếu provision thành công (đã ghi NVS). Caller có thể loadProvisioned() lại.
bool runProvisionFlow(const char* firmwareVersion,
                      const char* hardwareRevision,
                      ProvisionedConfig& outConfig);

// Force re-provision lần boot sau (dùng khi rotate key hoặc đổi config).
// Set "provd"=0 trong NVS — KHÔNG xóa other keys (siteId/ntpServer giữ làm fallback).
bool clearProvisionFlag();

}  // namespace provision
