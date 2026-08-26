// ==================================================================
// Sprint 5 — S5-FW-06: SHT31 ambient sensor implementation.
// Driver: adafruit/Adafruit SHT31 Library.
// ==================================================================
#include "sensor/sht31.h"

#if __has_include("config.h")
  #include "config.h"
#else
  #include "config.example.h"
#endif

#include "config/device_identity.h"
#include "net/http_client.h"
#include "net/time_sync.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>

#ifndef SHT31_ENABLED
  #define SHT31_ENABLED 1
#endif

#if SHT31_ENABLED
  #include <Adafruit_SHT31.h>
#endif

#include <cmath>
#include <cstring>

namespace sensor {

namespace {

#if SHT31_ENABLED
Adafruit_SHT31 s_sht31;
#endif
bool           s_inited       = false;
uint32_t       s_lastPostMs   = 0;
uint32_t       s_postOk       = 0;
uint32_t       s_postFail     = 0;

// Backend require Guid SiteId — set qua sht31SetSiteId() từ main.cpp sau provision.
constexpr size_t kSiteIdBufLen = 40;
char s_siteId[kSiteIdBufLen] = {0};

// Buffer JSON cho ambient reading — 1 item, ~300 bytes overhead.
constexpr size_t kAmbientPayloadBuf = 384;

}  // namespace

bool sht31Begin() {
#if !SHT31_ENABLED
  Serial.println("[sht31] disabled — no I2C probe/read");
  return false;
#else
  if (s_inited) return true;

  // Wire init idempotent — INA226 hoặc SHT31 init trước cũng OK.
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQUENCY_HZ);

  if (!s_sht31.begin(SHT31_I2C_ADDRESS)) {
    Serial.printf("[sht31] init FAIL — không respond tại 0x%02X (check wiring/I2C)\n",
                  SHT31_I2C_ADDRESS);
    return false;
  }

  // Disable on-chip heater (chỉ dùng để de-condense ngoài trời).
  s_sht31.heater(false);

  s_inited = true;
  Serial.printf("[sht31] init OK addr=0x%02X poll_interval=%lums\n",
                SHT31_I2C_ADDRESS,
                static_cast<unsigned long>(SHT31_POLL_INTERVAL_MS));
  return true;
#endif
}

bool sht31ReadOnce(float* tempOut, float* humOut) {
#if !SHT31_ENABLED
  (void)tempOut;
  (void)humOut;
  return false;
#else
  if (!s_inited) return false;
  float t = s_sht31.readTemperature();
  float h = s_sht31.readHumidity();
  if (std::isnan(t) || std::isnan(h)) {
    Serial.println("[sht31] read NaN — I2C lỗi hoặc sensor disconnect");
    return false;
  }
  // Range sanity check.
  if (t < -40.0f || t > 125.0f) {
    Serial.printf("[sht31] temp OUT OF RANGE = %.2f°C\n", t);
    return false;
  }
  if (h < 0.0f || h > 100.0f) {
    Serial.printf("[sht31] humidity OUT OF RANGE = %.2f%%\n", h);
    return false;
  }
  if (tempOut) *tempOut = t;
  if (humOut)  *humOut  = h;
  return true;
#endif
}

void sht31SetSiteId(const char* siteIdGuid) {
#if !SHT31_ENABLED
  (void)siteIdGuid;
  return;
#else
  if (!siteIdGuid) { s_siteId[0] = '\0'; return; }
  strncpy(s_siteId, siteIdGuid, kSiteIdBufLen - 1);
  s_siteId[kSiteIdBufLen - 1] = '\0';
  Serial.printf("[sht31] siteId set: %s\n", s_siteId);
#endif
}

bool sht31PostNow() {
#if !SHT31_ENABLED
  return false;
#else
  if (!s_inited) return false;

  // Backend AmbientReadingItem.SiteId REQUIRED (Guid). Nếu chưa provision xong
  // (s_siteId rỗng) → backend reject với validation error. Skip để tránh waste.
  if (s_siteId[0] == '\0') {
    Serial.println("[sht31] siteId chưa set (provision chưa xong?) — skip post");
    return false;
  }

  float temp = 0.0f, hum = 0.0f;
  if (!sht31ReadOnce(&temp, &hum)) {
    s_postFail++;
    return false;
  }

  char ts[24];
  if (!net::isoNow(ts, sizeof(ts))) {
    Serial.println("[sht31] NTP not synced — skip post");
    s_postFail++;
    return false;
  }

  // Build payload — verified với BatteryService AmbientReadingItem DTO:
  //   siteId (Guid, required), time, ambientTemperature (decimal), humidity?,
  //   solarIrradiance?, source (enum string), sourceDeviceId? (≤ 64)
  //
  // Backend route: POST /api/ambient/readings/batch
  // Auth: ApiKey + scope EnvironmentalIngest (S2-BE-03)
  char payload[kAmbientPayloadBuf];
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  JsonObject it = items.add<JsonObject>();
  it["siteId"]              = s_siteId;
  it["time"]                = ts;
  it["ambientTemperature"]  = temp;
  it["humidity"]            = hum;
  // AmbientReadingSourceEnum: IotSensor=1, WeatherApi=2. Backend KHÔNG register
  // JsonStringEnumConverter → System.Text.Json default = INT only (string fail).
  // Verified: backend/src/BatteryService.Domain/Enums/AmbientReadingSourceEnum.cs
  it["source"]              = 1;             // IotSensor
  it["sourceDeviceId"]      = identity::deviceCode();

  size_t n = serializeJson(doc, payload, sizeof(payload));
  if (n == 0 || n >= sizeof(payload)) {
    Serial.println("[sht31] payload serialize FAIL — buffer overflow");
    s_postFail++;
    return false;
  }

  net::PostResult res = net::httpPostJson(BACKEND_AMBIENT_PATH, payload, n);
  if (res.httpCode >= 200 && res.httpCode < 300) {
    s_postOk++;
    Serial.printf("[sht31] post OK %.1f°C %.1f%% (%d) [%lums]\n",
                  temp, hum, res.httpCode,
                  static_cast<unsigned long>(res.durationMs));
    return true;
  }
  s_postFail++;
  Serial.printf("[sht31] post FAIL code=%d resp=\"%s\"\n",
                res.httpCode, res.responseSnippet);
  return false;
#endif
}

void sht31Tick() {
#if SHT31_ENABLED
  if (!s_inited) return;
  const uint32_t now = millis();
  if (now - s_lastPostMs < SHT31_POLL_INTERVAL_MS) return;
  s_lastPostMs = now;
  sht31PostNow();
#endif
}

uint32_t sht31PostOkCount()   { return s_postOk; }
uint32_t sht31PostFailCount() { return s_postFail; }

}  // namespace sensor
