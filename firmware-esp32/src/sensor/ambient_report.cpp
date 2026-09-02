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

#include <cmath>
#include <cstring>

#ifndef AMBIENT_POST_INTERVAL_MS
  #define AMBIENT_POST_INTERVAL_MS 15000UL
#endif

// Nhip DO bien dong, va cung la SAN chong doi: khong bao gio gui day hon muc nay.
#ifndef AMBIENT_MIN_POST_INTERVAL_MS
  #define AMBIENT_MIN_POST_INTERVAL_MS 2000UL
#endif
// Bien dong du lon de gui NGAY, khong doi het nhip heartbeat.
#ifndef AMBIENT_TEMP_DELTA_C
  #define AMBIENT_TEMP_DELTA_C 1.0f
#endif
#ifndef AMBIENT_GAS_DELTA_PCT
  #define AMBIENT_GAS_DELTA_PCT 5
#endif

namespace sensor {

namespace {

constexpr size_t kSiteIdBufLen = 40;
char     s_siteId[kSiteIdBufLen] = {0};
uint32_t s_lastPostMs = 0;
uint32_t s_lastEvalMs = 0;
// Gia tri cua lan GUI truoc, de do bien dong.
bool     s_hasSent    = false;
float    s_sentTemp   = 0.0f;
int      s_sentGas    = 0;
bool     s_sentWater  = false;
// `waterLeakIsWet()` doc-roi-xoa: ket qua phai giu o day cho toi khi GUI duoc, neu khong mot lan
// uot roi vao giua hai lan post se bien mat khong dau vet.
bool     s_pendingWet = false;
uint32_t s_okCount    = 0;
uint32_t s_failCount  = 0;
constexpr size_t kPayloadBuf = 384;

}  // namespace

void ambientReportSetSiteId(const char* siteIdGuid) {
  if (!siteIdGuid) { s_siteId[0] = '\0'; return; }
  strncpy(s_siteId, siteIdGuid, kSiteIdBufLen - 1);
  s_siteId[kSiteIdBufLen - 1] = '\0';
}

// Gui theo BIEN DONG, khong chi theo nhip co dinh.
//
// Nguong nam o backend (`AmbientThresholdConfig`) va giu nguyen nhu vay — thiet bi KHONG biet
// nguong, no chi tra loi mot cau de hon: "so do co nhay dang ke khong". Nho vay canh bao gan nhu
// tuc thi ma luc binh thuong van chi 1 dong/15 s, thay vi ha thang nhip gui xuong 2 s roi nhan so
// dong hypertable len 7 lan suot ngay de doi lay vai giay o nhung phut hiem hoi co su co.
void ambientReportTick() {
  const uint32_t now = millis();

  // San chong doi — ap cho MOI duong gui, ke ca khi bien dong lien tuc.
  if (now - s_lastPostMs < AMBIENT_MIN_POST_INTERVAL_MS) return;
  if (now - s_lastEvalMs < AMBIENT_MIN_POST_INTERVAL_MS) return;
  s_lastEvalMs = now;

  const bool gasReady   = mq2AmbientValueReady();
  const bool waterReady = waterLeakHasSample();
  // Gom trang thai uot NGAY, du lat nua co gui hay khong.
  if (waterReady && waterLeakIsWet()) s_pendingWet = true;
  const int gasNow = gasReady ? mq2LastPercent() : 0;

  // Nhiet do doc tu BAN NHO: `ds18b20ReadAmbient()` chan 750 ms, ma duong BMS da doc moi ~1 s roi.
  // Doc lai chi de so sanh la nhan doi 750 ms cham trong vong lap chinh.
  float cachedTemp = 0.0f;
  const bool hasCachedTemp = ds18b20LastAmbient(cachedTemp, 5000UL);

  const bool heartbeatDue = (now - s_lastPostMs) >= AMBIENT_POST_INTERVAL_MS;
  const bool tempJumped   = hasCachedTemp && s_hasSent &&
                            fabsf(cachedTemp - s_sentTemp) >= AMBIENT_TEMP_DELTA_C;
  const bool gasJumped    = gasReady && s_hasSent &&
                            abs(gasNow - s_sentGas) >= AMBIENT_GAS_DELTA_PCT;
  const bool waterChanged = waterReady && s_hasSent && (s_pendingWet != s_sentWater);

  if (!heartbeatDue && !tempJumped && !gasJumped && !waterChanged) return;

  s_lastPostMs = now;

  if (s_siteId[0] == '\0') { s_failCount++; return; }

  // Đọc cả ba TRƯỚC khi lấy mốc thời gian: DS18B20 chặn 750 ms, lấy mốc trước rồi mới đọc thì
  // mốc ghi vào DB lệch trước gần một giây so với lúc số liệu thực sự có.
  float temperature = 0.0f;
  const bool hasTemp  = ds18b20ReadAmbient(temperature);
  const bool hasGas   = gasReady;
  const bool hasWater = waterReady;
  // Trang thai uot da duoc gom o vong do phia tren (`s_pendingWet`), khong doc lai tu cam bien:
  // `waterLeakIsWet()` doc-roi-xoa nen goi them mot lan nua se nhan ve co da bi don sach, va
  // payload se bao DRY cho dung lan uot vua kich hoat viec gui nay.
  const bool waterWet = s_pendingWet;

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
  if (hasGas)   it["gasConcentration"]   = gasNow;
  if (hasWater) it["waterLeakDetected"]  = waterWet;
  it["source"]         = 1;  // AmbientReadingSourceEnum.IotSensor
  it["sourceDeviceId"] = identity::deviceCode();

  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) { s_failCount++; return; }

  net::PostResult res = net::httpPostJson(BACKEND_AMBIENT_PATH, payload, n);
  if (res.httpCode >= 200 && res.httpCode < 300) {
    s_okCount++;
    // Chot moc so sanh cho vong sau. Chi xoa `s_pendingWet` khi da GUI DUOC — post that bai ma
    // xoa la mat han lan uot do.
    s_hasSent    = true;
    s_sentTemp   = hasTemp ? temperature : s_sentTemp;
    s_sentGas    = hasGas ? gasNow : s_sentGas;
    s_sentWater  = waterWet;
    s_pendingWet = false;
    Serial.printf("[ambient] post OK temp=%s gas=%s water=%s (%d) [%lums]\n",
                  hasTemp ? String(temperature, 1).c_str() : "-",
                  hasGas ? String(gasNow).c_str() : "-",
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
