#include "sensor/ambient_report.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "net/http_client.h"
#include "net/time_sync.h"
#include "sensor/ds18b20.h"
#include "sensor/mq2.h"
#include "sensor/water_leak.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>

#ifndef AMBIENT_POST_INTERVAL_MS
  #define AMBIENT_POST_INTERVAL_MS 15000UL
#endif

namespace sensor {

namespace {

constexpr size_t kSiteIdBufLen = 40;
char     s_siteId[kSiteIdBufLen] = {0};
uint32_t s_lastPostMs = 0;
uint32_t s_okCount    = 0;
uint32_t s_failCount  = 0;
constexpr size_t kPayloadBuf = 384;

}  // namespace

void ambientReportSetSiteId(const char* siteIdGuid) {
  if (!siteIdGuid) { s_siteId[0] = '\0'; return; }
  strncpy(s_siteId, siteIdGuid, kSiteIdBufLen - 1);
  s_siteId[kSiteIdBufLen - 1] = '\0';
}

void ambientReportTick() {
  const uint32_t now = millis();
  if (now - s_lastPostMs < AMBIENT_POST_INTERVAL_MS) return;
  s_lastPostMs = now;

  if (s_siteId[0] == '\0') { s_failCount++; return; }

  // Đọc cả ba TRƯỚC khi lấy mốc thời gian: DS18B20 chặn 750 ms, lấy mốc trước rồi mới đọc thì
  // mốc ghi vào DB lệch trước gần một giây so với lúc số liệu thực sự có.
  float temperature = 0.0f;
  const bool hasTemp  = ds18b20ReadAmbient(temperature);
  const bool hasGas   = mq2AmbientValueReady();
  const bool hasWater = waterLeakHasSample();
  // Đọc ĐÚNG MỘT lần: `waterLeakIsWet()` trả "đã từng ướt kể từ lần đọc trước" rồi XOÁ cờ latch.
  // Gọi lần hai (cho dòng log bên dưới) luôn nhận cờ đã bị lần một dọn sạch, nên payload báo WET
  // mà serial in DRY — đúng thứ làm cảm biến nước trông như chập chờn khi soi log.
  const bool waterWet = hasWater && waterLeakIsWet();

  // Không cảm biến nào có số thì bỏ hẳn lượt này: backend từ chối item rỗng (400), gửi đi chỉ
  // tốn một request để nhận lỗi.
  if (!hasTemp && !hasGas && !hasWater) return;

  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) { s_failCount++; return; }

  char payload[kPayloadBuf];
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  JsonObject it = items.add<JsonObject>();
  it["siteId"] = s_siteId;
  it["time"]   = ts;
  if (hasTemp)  it["ambientTemperature"] = temperature;
  if (hasGas)   it["gasConcentration"]   = mq2LastPercent();
  if (hasWater) it["waterLeakDetected"]  = waterWet;
  it["source"]         = 1;  // AmbientReadingSourceEnum.IotSensor
  it["sourceDeviceId"] = identity::deviceCode();

  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) { s_failCount++; return; }

  net::PostResult res = net::httpPostJson(BACKEND_AMBIENT_PATH, payload, n);
  if (res.httpCode >= 200 && res.httpCode < 300) {
    s_okCount++;
    Serial.printf("[ambient] post OK temp=%s gas=%s water=%s (%d) [%lums]\n",
                  hasTemp ? String(temperature, 1).c_str() : "-",
                  hasGas ? String(mq2LastPercent()).c_str() : "-",
                  hasWater ? (waterWet ? "WET" : "DRY") : "-",
                  res.httpCode, static_cast<unsigned long>(res.durationMs));
    return;
  }
  s_failCount++;
  Serial.printf("[ambient] post FAIL code=%d resp=\"%s\"\n",
                res.httpCode, res.responseSnippet);
}

uint32_t ambientReportOkCount()   { return s_okCount; }
uint32_t ambientReportFailCount() { return s_failCount; }

}  // namespace sensor
