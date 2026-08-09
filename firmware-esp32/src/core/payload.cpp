// ==================================================================
// Sprint 1 (#16) + Sprint 3 (#32) — JSON batch payload builders
// ==================================================================
#include "core/payload.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

namespace core {

// Backend PK của hypertable sensor_readings là (Time, BatteryAssetId) — KHÔNG gồm
// sensorSourceCode. isoNow() chỉ có độ phân giải giây, nên 3 reading cùng pin
// (primary/redundant/external-temp) trong 1 batch mà dùng chung timestamp sẽ
// vi phạm khóa chính → backend 500 cả batch. Vá millisecond = index item để mỗi
// item có Time duy nhất: "…T08:15:42Z" + idx 7 → "…T08:15:42.007Z".
static void patchItemTimestamp(const char* baseIso, size_t idx,
                               char* outBuf, size_t outBufLen) {
  const size_t n = strlen(baseIso);
  if (n < 2 || baseIso[n - 1] != 'Z' || n + 5 >= outBufLen) {
    // Format lạ hoặc buffer thiếu → giữ nguyên (an toàn hơn cắt xén).
    strncpy(outBuf, baseIso, outBufLen - 1);
    outBuf[outBufLen - 1] = '\0';
    return;
  }
  memcpy(outBuf, baseIso, n - 1);
  snprintf(outBuf + (n - 1), outBufLen - (n - 1), ".%03uZ",
           static_cast<unsigned>(idx % 1000u));
}

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
    char itemIso[32];
    patchItemTimestamp(isoTimestamp, i, itemIso, sizeof(itemIso));
    it["batteryAssetId"] = r.batteryAssetId;
    it["time"]           = itemIso;          // ms = index → Time duy nhất per item
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

// ==================================================================
// Sprint 3 — S3-FW-04 — Production batch payload
//
// Schema khớp `BatchIngestSensorReadingsCommand` + `SensorReadingItem`.
// items[] vẫn dùng (backend giữ tên field này, KHÔNG phải "readings[]").
// Mỗi item:
//   - batteryAssetSerial   (preferred over batteryAssetId per #IoT2-18)
//   - time, deviceTimestamp (deviceTimestamp cho clock skew check #IoT2-15)
//   - voltage, current, temperature, socPercent
//   - cycleCount (optional)
//   - sohPercent (optional — nếu hasSoh)
//   - chargingState (optional — nếu hasChargingState)
//   - bmsErrorCode (optional ≤ 64 — nếu hasBmsError)
//   - sensorSourceCode (≤ 20)
//   - sourceType (1=Bms, 2=IotGateway, 3=External)
// ==================================================================
size_t buildProductionBatchPayload(const SensorReading* readings,
                                   size_t                readingCount,
                                   const char*           isoTimestamp,
                                   const char*           deviceCode,
                                   char*                 outBuf,
                                   size_t                outBufLen) {
  if (readings == nullptr || readingCount == 0) return 0;
  if (isoTimestamp == nullptr || isoTimestamp[0] == '\0') return 0;
  if (deviceCode == nullptr || deviceCode[0] == '\0') return 0;
  if (outBuf == nullptr || outBufLen < payloadBufferSize(readingCount)) return 0;

  (void)deviceCode;   // header X-Device-Code mang, không embed body
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  for (size_t i = 0; i < readingCount; ++i) {
    const SensorReading& r = readings[i];

    JsonObject it = items.add<JsonObject>();
    // Sprint 3 priority: serial > id. Backend §52.5 resolve serial → BatteryAssetId.
    if (r.serial[0] != '\0') {
      it["batteryAssetSerial"] = r.serial;
    } else {
      it["batteryAssetId"] = r.batteryAssetId;
    }
    char itemIso[32];
    patchItemTimestamp(isoTimestamp, i, itemIso, sizeof(itemIso));
    it["time"]            = itemIso;          // ms = index → tránh đụng PK (Time, BatteryAssetId)
    it["deviceTimestamp"] = itemIso;          // #IoT2-15 clock skew check
    it["voltage"]         = r.voltage;
    it["current"]         = r.current;
    it["temperature"]     = r.temperature;
    it["socPercent"]      = r.socPercent;
    it["cycleCount"]      = r.cycleCount;

    // Sprint 3 — sourceType + sensorSourceCode CẤM hard-code (cross-source S6 §1.6.6).
    it["sourceType"]       = static_cast<int>(r.sourceType);
    if (r.sensorSourceCode[0] != '\0') {
      it["sensorSourceCode"] = r.sensorSourceCode;
    }

    // Optional fields — chỉ gửi nếu flag đã set (tránh null thừa).
    if (r.hasSoh) {
      it["sohPercent"] = r.sohPercent;
    }
    if (r.hasChargingState) {
      it["chargingState"] = static_cast<int>(r.chargingState);
    }
    if (r.hasBmsError && r.bmsErrorCode[0] != '\0') {
      it["bmsErrorCode"] = r.bmsErrorCode;
    }
  }

  const size_t written = serializeJson(doc, outBuf, outBufLen);
  if (written == 0 || written >= outBufLen) return 0;
  return written;
}

}  // namespace core
