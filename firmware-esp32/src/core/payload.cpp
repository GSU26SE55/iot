// ==================================================================
// Sprint 1 — S1-FW-05: Build legacy JSON batch payload (implementation)
//
// Acceptance (tasksprint.md S1-FW-05):
//   - JSON output match schema legacy (Items[].BatteryAssetId + Time + V/I/temp/SOC)
//   - Có unit test (test/test_payload/) — chạy bằng `pio test -e native`
//
// Dùng ArduinoJson v7 — serializeJson() trả số byte ghi.
// Lý do chọn legacy GUID thay vì Sprint 3 production serial:
//   - Sprint 1 demo "không cần pin thật" — backend BatteryService cũ đã có schema GUID;
//     đổi sang serial cần backend thêm migration (chuyển sang Sprint 3 sau).
// ==================================================================
#include "core/payload.h"

#include <ArduinoJson.h>
#include <string.h>

namespace core {

size_t buildLegacyBatchPayload(const SensorReading* readings,
                               size_t                    readingCount,
                               const char*               isoTimestamp,
                               const char*               deviceCode,
                               char*                     outBuf,
                               size_t                    outBufLen) {
  if (readings == nullptr || readingCount == 0) return 0;
  if (isoTimestamp == nullptr || isoTimestamp[0] == '\0') return 0;
  if (deviceCode == nullptr || deviceCode[0] == '\0') return 0;
  if (outBuf == nullptr || outBufLen < payloadBufferSize(readingCount)) return 0;

  // ArduinoJson v7: JsonDocument tự cấp phát (heap). OK với ESP32-S3 PSRAM,
  // unit test native cũng chạy được.
  // Schema legacy theo NI §7.4 "Backward compatibility (MVP)":
  //   top-level CHỈ có "items[]" — wrapper deviceTimestamp/readings là Sprint 3.
  //   items[] có batteryAssetId (Guid) + sensor fields.
  //   KHÔNG có sourceDeviceId, sourceType, sensorSourceCode (Sprint 3 production contract).
  // deviceCode param chỉ dùng cho header X-Api-Key trail / log, KHÔNG embed payload.
  (void)deviceCode;
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  for (size_t i = 0; i < readingCount; ++i) {
    const SensorReading& r = readings[i];

    JsonObject it = items.add<JsonObject>();
    it["batteryAssetId"] = r.batteryAssetId;
    it["time"]           = isoTimestamp;     // Sprint 1: 1 batch = 1 timestamp
    it["voltage"]        = r.voltage;
    it["current"]        = r.current;
    it["temperature"]    = r.temperature;
    it["socPercent"]     = r.socPercent;
    it["cycleCount"]     = r.cycleCount;
  }

  // serializeJson không nén — pretty=false, không thêm space.
  const size_t written = serializeJson(doc, outBuf, outBufLen);
  if (written == 0 || written >= outBufLen) {
    // overflow → trả 0 để caller biết
    return 0;
  }
  return written;
}

}  // namespace core
