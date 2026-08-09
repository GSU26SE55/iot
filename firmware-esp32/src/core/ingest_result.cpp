// GH-748 — xem ingest_result.h.

#include "core/ingest_result.h"

#include <ArduinoJson.h>

namespace core {

IngestResult parseIngestResult(const char* json, size_t len) {
  IngestResult out;
  if (json == nullptr || len == 0) return out;

  JsonDocument doc;
  // deserializeJson dừng ở lỗi cú pháp — thân bị cắt ngắn sẽ rơi vào đây và ta trả
  // parsed=false. KHÔNG cố "vớt vát" số đọc được: một con số đọc dở còn tệ hơn không đọc.
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) return out;

  JsonVariantConst data = doc["data"];
  if (data.isNull()) return out;

  // Thiếu trường nào thì coi như không đọc được, thay vì mặc định 0 — `inserted = 0` mặc
  // định sẽ bị hiểu thành "backend không nhận gì cả" và sinh cảnh báo giả.
  if (!data["totalReceived"].is<int>() || !data["inserted"].is<int>()) return out;

  out.totalReceived = data["totalReceived"].as<int>();
  out.inserted      = data["inserted"].as<int>();
  out.skipped       = data["skipped"].is<int>() ? data["skipped"].as<int>() : 0;
  out.parsed        = true;
  return out;
}

}  // namespace core
